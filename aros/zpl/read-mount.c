/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/txg.h>
#include <sys/zap.h>
#include <sys/zfs_dir.h>
#include <zfs_comutil.h>
#include <proto/exec.h>
#include <stdio.h>
#include "read-mount.h"
#include "native-protection.h"
#include "xattr.h"
#include "../engine/zpl-fixture.h"
#include "../ui/mount-api.h"
int oz_dataset_read_counter(dataset_kstats_t *, const char *, uint64_t *);
extern zil_replay_func_t *const zfs_replay_vector[TX_MAX_TYPE];
static void mount_failure(const char *stage,int error)
{
    char line[128];snprintf(line,sizeof(line),"OpenZFS mount failed stage=%s errno=%d\n",stage,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
}

/* Native mount bridge. Recovery finishes before exposing a writable mount;
 * record-specific recovery coverage and other mount policies remain work. */
struct oz_mount_policy { zfsvfs_t *fs; boolean_t readonly, exec; };
struct oz_read_mount {
    zfsvfs_t fs;
    struct oz_mount_policy *policy;
    boolean_t unpublished, owns_objset, suspended, mount_readonly;
    struct Task *owner;
    dsl_dataset_t *suspended_ds;
    spa_t *suspended_spa;
    uint64_t suspended_object;
};
int oz_read_mount_is_owner(struct oz_read_mount *mount)
{ return mount && mount->owner==FindTask(NULL); }

/* All caches, callbacks and publication references must already be gone. */
static void mount_free(struct oz_read_mount *mount)
{
    if (mount->owns_objset)
        dmu_objset_disown(mount->fs.z_os, B_TRUE, &mount->fs);
    kmem_free(mount, sizeof (*mount));
}

static void recordsize_changed(void *arg, uint64_t value)
{
    zfsvfs_t *fs = ((struct oz_mount_policy *)arg)->fs;
    ASSERT3U(value, >=, SPA_MINBLOCKSIZE);
    ASSERT3U(value, <=, spa_maxblocksize(dmu_objset_spa(fs->z_os)));
    ASSERT(ISP2(value));
    fs->z_max_blksz = value;
}
static void readonly_changed(void *arg, uint64_t value)
{
    struct oz_mount_policy *policy = arg;
    /* Dataset changes must never undo the caller's read-only mount choice. */
    policy->fs->z_readonly = policy->readonly || policy->fs->z_issnap || value != 0;
}
static void aclmode_changed(void *arg, uint64_t value)
{ ((struct oz_mount_policy *)arg)->fs->z_acl_mode = value; }
static void aclinherit_changed(void *arg, uint64_t value)
{ ((struct oz_mount_policy *)arg)->fs->z_acl_inherit = value; }
static void atime_changed(void *arg, uint64_t value)
{ ((struct oz_mount_policy *)arg)->fs->z_atime = value != 0; }
static void relatime_changed(void *arg, uint64_t value)
{ ((struct oz_mount_policy *)arg)->fs->z_relatime = value != 0; }
static void exec_changed(void *arg, uint64_t value)
{ ((struct oz_mount_policy *)arg)->exec = value != 0; }

/* These upstream DSL registrations deliver the initial value and all later
 * changes. The objset must remain owned until close has drained callbacks.
 * Other mount policies (xattr, devices, etc.) remain separate work. */
int oz_mount_policy_open(zfsvfs_t *fs, boolean_t readonly,
    struct oz_mount_policy **out)
{
    if (!out) return (EINVAL);
    *out = NULL;
    if (!fs || !fs->z_os) return (EINVAL);
    struct oz_mount_policy *policy = kmem_zalloc(sizeof (*policy), KM_SLEEP);
    policy->fs = fs; policy->readonly = readonly;
    struct { const char *name; dsl_prop_changed_cb_t *callback; } props[] = {
        {"recordsize", recordsize_changed}, {"readonly", readonly_changed},
        {"atime", atime_changed}, {"relatime", relatime_changed},
        {"exec", exec_changed},
        {"aclmode", aclmode_changed}, {"aclinherit", aclinherit_changed}
    };
    dsl_pool_t *dp = dmu_objset_pool(fs->z_os);
    dsl_pool_config_enter(dp, policy);
    int error = 0;
    for (unsigned i = 0; i < ARRAY_SIZE(props) && !error; i++)
        error = dsl_prop_register(dmu_objset_ds(fs->z_os),
            props[i].name, props[i].callback, policy);
    if (error) dsl_prop_unregister_all(dmu_objset_ds(fs->z_os), policy);
    dsl_pool_config_exit(dp, policy);
    if (error) { kmem_free(policy, sizeof (*policy)); return (error); }
    *out = policy;
    return (0);
}
void oz_mount_policy_close(struct oz_mount_policy *policy)
{
    if (!policy) return;
    /* Upstream unregister serializes against callback delivery with dd_lock;
     * after it returns, no callback retains this mount-owned argument. */
    dsl_prop_unregister_all(dmu_objset_ds(policy->fs->z_os), policy);
    kmem_free(policy, sizeof (*policy));
}

/* Portable metadata portion of upstream zfsvfs_init (zfs_vfsops.c).
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Copyright (c) 2026, TrueNAS. Portions Copyright 2010 Robert Milkowski.
 * Caller provides a zeroed, exclusively owned native mount. No locks or SA
 * handles are allocated here; an error leaves no cleanup obligations. */
int
oz_zpl_mount_metadata(zfsvfs_t *fs, objset_t *os)
{
    if (!fs || !os) return (EINVAL);
    fs->z_os = os;
    fs->z_parent = fs;
    fs->z_issnap = dmu_objset_is_snapshot(os);
    int error = zfs_get_zplprop(os, ZFS_PROP_VERSION, &fs->z_version);
    if (error) return (error);
    if (fs->z_version > zfs_zpl_version_map(spa_version(dmu_objset_spa(os))))
        return (ENOTSUP);
    uint64_t value;
#define PROP(prop, field) do { \
    error = zfs_get_zplprop(os, prop, &value); \
    if (error) return (error); \
    fs->field = value; \
} while (0)
    PROP(ZFS_PROP_NORMALIZE, z_norm);
    PROP(ZFS_PROP_UTF8ONLY, z_utf8);
    PROP(ZFS_PROP_CASE, z_case);
    PROP(ZFS_PROP_ACLTYPE, z_acl_type);
    if (fs->z_case == ZFS_CASE_INSENSITIVE || fs->z_case == ZFS_CASE_MIXED)
        fs->z_norm |= U8_TEXTPREP_TOUPPER;
    fs->z_use_fuids = USE_FUIDS(fs->z_version, os);
    fs->z_use_sa = USE_SA(fs->z_version, os);
    error = zfs_get_zplprop(os, ZFS_PROP_XATTR, &value);
    if (error && error != ENOENT) return (error);
    fs->z_xattr_sa = !error && value == ZFS_XATTR_SA;
    PROP(ZFS_PROP_DEFAULTUSERQUOTA, z_defaultuserquota);
    PROP(ZFS_PROP_DEFAULTGROUPQUOTA, z_defaultgroupquota);
    PROP(ZFS_PROP_DEFAULTPROJECTQUOTA, z_defaultprojectquota);
    PROP(ZFS_PROP_DEFAULTUSEROBJQUOTA, z_defaultuserobjquota);
    PROP(ZFS_PROP_DEFAULTGROUPOBJQUOTA, z_defaultgroupobjquota);
    PROP(ZFS_PROP_DEFAULTPROJECTOBJQUOTA, z_defaultprojectobjquota);
#undef PROP
    error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1, &fs->z_root);
    if (error) return (error);
    if (!fs->z_root) return (EIO);
    error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_UNLINKED_SET, 8, 1, &fs->z_unlinkedobj);
    if (error) return (error);
    struct { const char *name; uint64_t *object; } optional[] = {
        {zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA], &fs->z_userquota_obj},
        {zfs_userquota_prop_prefixes[ZFS_PROP_GROUPQUOTA], &fs->z_groupquota_obj},
        {zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTQUOTA], &fs->z_projectquota_obj},
        {zfs_userquota_prop_prefixes[ZFS_PROP_USEROBJQUOTA], &fs->z_userobjquota_obj},
        {zfs_userquota_prop_prefixes[ZFS_PROP_GROUPOBJQUOTA], &fs->z_groupobjquota_obj},
        {zfs_userquota_prop_prefixes[ZFS_PROP_PROJECTOBJQUOTA], &fs->z_projectobjquota_obj},
        {ZFS_FUID_TABLES, &fs->z_fuid_obj}, {ZFS_SHARES_DIR, &fs->z_shares_dir}
    };
    for (unsigned i = 0; i < ARRAY_SIZE(optional); i++) {
        error = zap_lookup(os, MASTER_NODE_OBJ, optional[i].name, 8, 1, optional[i].object);
        if (error == ENOENT) *optional[i].object = 0;
        else if (error) return (error);
    }
    /* Initial recordsize snapshot; writable mounts also need registration of
     * the upstream property callbacks before exposing mutable operations. */
    dsl_pool_t *dp = dmu_objset_pool(os);
    boolean_t already_held = dsl_pool_config_held(dp);
    if (!already_held) dsl_pool_config_enter(dp, fs);
    error = dsl_prop_get_int_ds(dmu_objset_ds(os), "recordsize", &fs->z_max_blksz);
    if (!already_held) dsl_pool_config_exit(dp, fs);
    return (error);
}

static int
mount_open_failed(struct oz_read_mount *mount, struct oz_read_mount **out, int error)
{
    int cleanup = oz_read_mount_close(mount);
    /* Never replace the admission failure with a cleanup error or let the
     * owner disown an objset while a cache still borrows its SA handles. */
    if (cleanup) {
        *out = mount;
        cmn_err(CE_WARN, "OpenZFS failed mount cleanup %d (admission %d)",
            cleanup, error);
    }
    return error;
}

static int
mount_initialize(struct oz_read_mount *mount, objset_t *os,
    sa_attr_type_t *attrs, uint64_t root, boolean_t readonly,
    struct oz_read_mount **out)
{
    mount->unpublished = B_TRUE;
    mount->owner = FindTask(NULL);
    mount->mount_readonly = readonly;
    zfsvfs_t *fs = &mount->fs;
    int error = oz_zpl_mount_metadata(fs, os);
    if (!error && fs->z_root != root) error = EINVAL;
    /* Names use upstream directory locking and dataset-specific ZAP matching. */
    if (!error && !readonly && (!spa_writeable(dmu_objset_spa(os)) || fs->z_issnap))
        error = EROFS;
    if (error) { mount_free(mount); return (error); }
    fs->z_attr_table = attrs;
    fs->z_readonly = readonly;
    rw_init(&fs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
    ZFS_TEARDOWN_INIT(fs);
    oz_mount_refs_init(fs);
    oz_znode_cache_init(fs);
    error = oz_mount_policy_open(fs, readonly, &mount->policy);
    if (error) {mount_failure("policy",error);return mount_open_failed(mount, out, error);}
    znode_t *zp;
    error = zfs_zget(fs, root, &zp);
    if (!error) {
        if (!S_ISDIR(zp->z_mode)) error = ENOTDIR;
        oz_znode_rele(zp);
    }
    if (error) {mount_failure("root-znode",error);return mount_open_failed(mount, out, error);}
    error = dataset_kstats_create(&fs->z_kstat, os);
    if (error) {mount_failure("dataset-kstats",error);return mount_open_failed(mount, out, error);}
    if (!readonly) {
        if (fs->z_readonly) return mount_open_failed(mount, out, EROFS);
        fs->z_log = zil_open(os, zfs_get_data, NULL);
        /* No caller can access this mount until recovery succeeds. Follow
         * upstream ordering: reclaim old unlinked objects before replay.
         * The checked entry point retains the log on callback/I/O failure. */
        error = oz_unlinked_drain_exclusive(fs);
        if(error)mount_failure("unlinked-drain",error);
        if (!error) {
            boolean_t replayed;
            fs->z_replay = B_TRUE;
            error = zil_replay_checked(os, fs, zfs_replay_vector, &replayed);
            if(error)mount_failure("zil-replay",error);
            fs->z_replay = B_FALSE;
        }
        if (error) return mount_open_failed(mount, out, error);
    }
    error = oz_mount_publish(fs);
    if (error) {mount_failure("publish",error);return mount_open_failed(mount, out, error);}
    mount->unpublished = B_FALSE;
    *out = mount;
    return (0);
}

int
oz_read_mount_open_mode(objset_t *os, sa_attr_type_t *attrs, uint64_t root,
    boolean_t readonly, struct oz_read_mount **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!os || !attrs || !root) return EINVAL;
    struct oz_read_mount *mount = kmem_zalloc(sizeof (*mount), KM_SLEEP);
    return mount_initialize(mount, os, attrs, root, readonly, out);
}

int
oz_read_mount_open_dataset(const char *dataset, boolean_t readonly,
    struct oz_read_mount **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!dataset) return EINVAL;
    /* dmu_objset_own(readonly=false) may schedule accounting upgrades before
     * mount_initialize checks the pool. Reject here, before any write task.
     * The service owner serializes pool and mount lifecycle operations. */
    if (!readonly) {
        dsl_pool_t *dp;
        int error = dsl_pool_hold(dataset, FTAG, &dp);
        if (error) return error;
        boolean_t writable = spa_writeable(dp->dp_spa);
        dsl_pool_rele(dp, FTAG);
        if (!writable) return EROFS;
    }
    struct oz_read_mount *mount = kmem_zalloc(sizeof (*mount), KM_SLEEP);
    zfsvfs_t *fs = &mount->fs;
    int error = dmu_objset_own(dataset, DMU_OST_ZFS, readonly, B_TRUE,
        fs, &fs->z_os);
    if (error) { mount_failure("objset-own",error);mount_free(mount); return error; }
    mount->owns_objset = B_TRUE;
    uint64_t sa = 0, version, root;
    sa_attr_type_t *attrs = NULL;
    error = zap_lookup(fs->z_os, MASTER_NODE_OBJ, ZPL_VERSION_STR, 8, 1, &version);
    if (!error && (version < ZPL_VERSION_INITIAL ||
        version > zfs_zpl_version_map(spa_version(dmu_objset_spa(fs->z_os)))))
        error = ENOTSUP;
    /* Upstream initializes the legacy attribute layout with SA object zero.
     * Pre-SA datasets have no ZFS_SA_ATTRS master-node entry; mounting must
     * neither require that entry nor implicitly upgrade the filesystem. */
    if (!error && USE_SA(version, fs->z_os))
        error = zap_lookup(fs->z_os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa);
    if (!error) error = sa_setup(fs->z_os, sa, zfs_attr_table, ZPL_END, &attrs);
    if (!error && USE_SA(version, fs->z_os))
        sa_register_update_callback(fs->z_os, zfs_sa_upgrade);
    if (!error) error = zap_lookup(fs->z_os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1, &root);
    if (!error && !root) error = EIO;
    if (error) { mount_free(mount); return error; }
    return mount_initialize(mount, fs->z_os, attrs, root, readonly, out);
}

objset_t *oz_read_mount_objset(struct oz_read_mount *mount)
{ return mount ? mount->fs.z_os : NULL; }
uint64_t oz_read_mount_root(struct oz_read_mount *mount)
{ return mount ? mount->fs.z_root : 0; }
int oz_read_mount_ended(struct oz_read_mount *mount)
{ return !mount || mount->fs.z_unmounted; }

int oz_read_mount_open(objset_t *os,sa_attr_type_t *attrs,uint64_t root,
    struct oz_read_mount **out)
{ return oz_read_mount_open_mode(os,attrs,root,B_TRUE,out); }

/* The native service serializes lifecycle operations and DOS packet dispatch
 * on its owner task. Worker callers must submit through that owner, never
 * suspend a mount while the owner can still pump raw DOS operations. */
int zfs_suspend_fs(zfsvfs_t *fs)
{
    if (!fs || fs->z_use_hold) return EINVAL;
    struct oz_read_mount *mount = (struct oz_read_mount *)fs;
    if (mount->owner != FindTask(NULL)) return EDEADLK;
    if (mount->suspended) return EBUSY;
    if (!mount->owns_objset || !fs->z_os) return ENOTSUP;
    if (dmu_objset_ds(fs->z_os)->ds_owner != fs) return EBUSY;
    ZFS_TEARDOWN_ENTER_WRITE(fs, FTAG);
    int error = fs->z_unmounted ? EIO : oz_read_mount_sync(mount);
    /* Refuse before detaching anything if an unsupported active resource
     * survived teardown exclusion. Retained idle node references are valid. */
    mutex_enter(&fs->z_znodes_lock);
    for (znode_t *zp = list_head(&fs->z_all_znodes); !error && zp;
        zp = list_next(&fs->z_all_znodes, zp)) {
        if (zp->z_dirlocks || zp->z_mapcnt || zp->z_sync_cnt ||
            (zp->z_atime_dirty && !zp->z_unlinked)) error = EBUSY;
    }
    mutex_exit(&fs->z_znodes_lock);
    if (error) { ZFS_TEARDOWN_EXIT_WRITE(fs, FTAG); return error; }
    if (fs->z_log) { zil_close(fs->z_log); fs->z_log = NULL; }
    for (znode_t *zp = list_head(&fs->z_all_znodes); zp;
        zp = list_next(&fs->z_all_znodes, zp)) {
        zp->z_suspended = zp->z_sa_hdl != NULL;
        if (zp->z_unlinked) zp->z_atime_dirty = B_FALSE;
        if (zp->z_suspended) zfs_znode_dmu_fini(zp);
    }
    oz_mount_policy_close(mount->policy); mount->policy = NULL;
    dataset_kstats_destroy(&fs->z_kstat);
    zfs_fuid_destroy(fs); fs->z_fuid_loaded = B_FALSE;
    fs->z_fuid_dirty = B_FALSE; fs->z_fuid_size = 0;
    objset_t *os = fs->z_os;
    if (spa_writeable(dmu_objset_spa(os))) txg_wait_synced(dmu_objset_pool(os), 0);
    dmu_objset_evict_dbufs(os);
    mount->suspended_ds = dmu_objset_ds(os);
    mount->suspended_spa = dmu_objset_spa(os);
    mount->suspended_object = mount->suspended_ds->ds_object;
    mutex_enter(&os->os_user_ptr_lock);
    ASSERT(dmu_objset_get_user(os) == fs);
    dmu_objset_set_user(os, NULL);
    fs->z_os = NULL; fs->z_attr_table = NULL;
    mutex_exit(&os->os_user_ptr_lock);
    /* Rollback/receive teardown must release filesystem wait long holds,
     * just like unmount. Detach first so newly arriving waits see no ZPL. */
    dsl_dir_cancel_waiters(mount->suspended_ds->ds_dir);
    mount->suspended = B_TRUE;
    /* Ownership and publication-owner reference survive. Writer stays held
     * across the upstream rollback/receive and matching resume call. */
    return 0;
}

int zfs_resume_fs(zfsvfs_t *fs, dsl_dataset_t *ds)
{
    if (!fs || fs->z_use_hold || !ds) return EINVAL;
    struct oz_read_mount *mount = (struct oz_read_mount *)fs;
    if (mount->owner != FindTask(NULL)) return EDEADLK;
    /* Accounting upgrade releases and reacquires the dataset to expand its
     * objset. Its allocation address may change; do not dereference or use
     * the old pointer as identity. Pool and dataset object remain stable.
     * A receive may change the dataset GUID, so that is not a mount key. */
    if (!mount->suspended || mount->suspended_spa != dsl_dataset_get_spa(ds) ||
        mount->suspended_object != ds->ds_object) return EINVAL;
    ASSERT(ZFS_TEARDOWN_WRITE_HELD(fs));
    VERIFY3P(ds->ds_owner, ==, fs);
    VERIFY(dsl_dataset_long_held(ds));
    dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
    objset_t *os = NULL;
    dsl_pool_config_enter(dp, FTAG);
    int error = dmu_objset_from_ds(ds, &os);
    dsl_pool_config_exit(dp, FTAG);
    /* As upstream, ownership must yield the current objset even if it was
     * evicted during rollback. Failure cannot safely discard that ownership. */
    VERIFY0(error);
    fs->z_os = os;
    error = oz_zpl_mount_metadata(fs, os);
    uint64_t sa = 0;
    if (!error && fs->z_use_sa)
        error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa);
    if (!error) error = sa_setup(os, sa, zfs_attr_table, ZPL_END, &fs->z_attr_table);
    if (!error && fs->z_use_sa) sa_register_update_callback(os, zfs_sa_upgrade);
    if (!error) error = oz_mount_policy_open(fs, mount->mount_readonly, &mount->policy);
    if (!error) error = dataset_kstats_create(&fs->z_kstat, os);
    if (!error && !mount->mount_readonly && !fs->z_issnap)
        fs->z_log = zil_open(os, zfs_get_data, NULL);
    if (!error) {
        for (znode_t *zp = list_head(&fs->z_all_znodes); zp;
            zp = list_next(&fs->z_all_znodes, zp)) {
            if (!zp->z_suspended) continue;
            zp->z_suspended = B_FALSE;
            (void) zfs_rezget(zp); /* stale individual nodes remain doomed */
        }
    } else {
        fs->z_unmounted = B_TRUE;
        mount->unpublished = B_TRUE; /* close-only, no replay or reaping */
    }
    mutex_enter(&ds->ds_dir->dd_activity_lock);
    if (!error) ds->ds_dir->dd_activity_cancelled = B_FALSE;
    mutex_enter(&os->os_user_ptr_lock);
    ASSERT0P(dmu_objset_get_user(os));
    dmu_objset_set_user(os, fs);
    mutex_exit(&os->os_user_ptr_lock);
    mutex_exit(&ds->ds_dir->dd_activity_lock);
    mount->suspended = B_FALSE; mount->suspended_ds = NULL;
    mount->suspended_spa = NULL; mount->suspended_object = 0;
    ZFS_TEARDOWN_EXIT_WRITE(fs, FTAG);
    return error;
}

/* Upstream redacted online receive cannot resume the old namespace. Keep
 * lifetime/ownership until the native owner drains DOS references and closes
 * this mount; never reconstruct SA handles on an intentionally ended fs. */
int zfs_end_fs(zfsvfs_t *fs, dsl_dataset_t *ds)
{
    if (!fs || fs->z_use_hold || !ds) return EINVAL;
    struct oz_read_mount *mount = (struct oz_read_mount *)fs;
    if (mount->owner != FindTask(NULL)) return EDEADLK;
    if (!mount->suspended || mount->suspended_ds != ds) return EINVAL;
    ASSERT(ZFS_TEARDOWN_WRITE_HELD(fs));
    VERIFY3P(ds->ds_owner, ==, fs);
    VERIFY(dsl_dataset_long_held(ds));
    dsl_pool_t *dp = spa_get_dsl(dsl_dataset_get_spa(ds));
    objset_t *os = NULL;
    dsl_pool_config_enter(dp, FTAG);
    VERIFY0(dmu_objset_from_ds(ds, &os));
    dsl_pool_config_exit(dp, FTAG);
    fs->z_os = os;
    fs->z_unmounted = B_TRUE;
    mount->unpublished = B_TRUE; /* final close skips replay/reaping */
    mutex_enter(&os->os_user_ptr_lock);
    ASSERT0P(dmu_objset_get_user(os));
    dmu_objset_set_user(os, fs);
    mutex_exit(&os->os_user_ptr_lock);
    mount->suspended = B_FALSE;
    mount->suspended_ds = NULL;
    mount->suspended_spa = NULL; mount->suspended_object = 0;
    ZFS_TEARDOWN_EXIT_WRITE(fs, FTAG);
    return 0;
}

int oz_read_mount_sync(struct oz_read_mount *mount)
{
    if (!mount) return EINVAL;
    if (mount->suspended) return EBUSY;
    if (mount->fs.z_unmounted) return EIO;
    int error = mount->fs.z_log ? zil_commit(mount->fs.z_log,0) : 0;
    int flush = oz_znode_cache_sync(&mount->fs, B_TRUE);
    /* Only the lifecycle owner trims: other callers can flush while the
     * owner holds borrowed cache pointers during suspend/resume. */
    if (!error && !flush && oz_read_mount_is_owner(mount))
        (void) oz_znode_cache_trim(&mount->fs, 256);
    return error ? error : flush;
}

int oz_read_mount_readonly(struct oz_read_mount *mount)
{ return !mount || zfs_is_readonly(&mount->fs) || !mount->fs.z_log; }

int oz_read_mount_protection_store(struct oz_read_mount *mount,uint64_t object,
    uint32_t bits,cred_t *cr)
{
    if (!mount) return EINVAL;
    if (!cr) return EACCES;
    znode_t *zp = NULL;
    int error = zfs_zget(&mount->fs,object,&zp);
    if (!error) error = oz_native_protection_store(zp,bits,cr);
    if (zp) oz_znode_rele(zp);
    return error;
}

int oz_read_mount_comment_get(struct oz_read_mount *mount,uint64_t object,char *buffer,size_t capacity)
{
    if(!mount||!buffer||!capacity)return EINVAL;
    buffer[0]=0;
    znode_t *zp=NULL;int error=zfs_zget(&mount->fs,object,&zp);
    size_t length=0;char record[79];
    if(!error)error=oz_xattr_get(zp,"system.aros.comment",record,sizeof(record),&length,kcred);
    if(error==ENODATA)error=0;
    else if(!error) {
        if(length>=capacity)error=ERANGE;
        else if(memchr(record,0,length))error=EIO;
        else {memcpy(buffer,record,length);buffer[length]=0;}
    }
    if(error)buffer[0]=0;
    if(zp)oz_znode_rele(zp);
    return error;
}
int oz_read_mount_comment_store(struct oz_read_mount *mount,uint64_t object,const char *comment,cred_t *cr)
{
    if(!mount||!comment)return EINVAL;
    if(!cr)return EACCES;
    size_t length=strnlen(comment,80);
    if(length>=80)return ENAMETOOLONG;
    if(oz_read_mount_readonly(mount))return EROFS;
    znode_t *zp=NULL;int error=zfs_zget(&mount->fs,object,&zp);
    if(!error)error=zfs_zaccess(zp,ACE_WRITE_ATTRIBUTES,0,B_FALSE,cr,NULL);
    if(!error)error=oz_xattr_set(zp,"system.aros.comment",comment,length,0,cr);
    if(zp)oz_znode_rele(zp);
    return error;
}

int oz_read_mount_set_mtime(struct oz_read_mount *mount,uint64_t object,
    int64_t seconds,uint32_t nanos,cred_t *cr)
{
    if (!mount || seconds < 0 || nanos >= 1000000000) return EINVAL;
    if (!cr) return EACCES;
    znode_t *zp = NULL;
    int error = zfs_zget(&mount->fs,object,&zp);
    vattr_t va = {.va_mask = ATTR_MTIME,.va_mtime = {seconds,nanos}};
    if (!error) error = zfs_setattr(zp,&va,0,cr,NULL);
    if (zp) oz_znode_rele(zp);
    return error;
}

int oz_read_mount_set_owner(struct oz_read_mount *mount,uint64_t object,
    uint32_t uid,uint32_t gid,cred_t *cr)
{
    if (!mount) return EINVAL;
    if (!cr) return EACCES;
    znode_t *zp = NULL;
    int error = zfs_zget(&mount->fs,object,&zp);
    vattr_t va = {.va_mask = ATTR_UID | ATTR_GID,.va_uid = uid,.va_gid = gid};
    if (!error) error = zfs_setattr(zp,&va,0,cr,NULL);
    if (zp) oz_znode_rele(zp);
    return error;
}

int oz_read_mount_project_get(struct oz_read_mount *mount,uint64_t object,
    uint32_t *id,int *inherit,cred_t *cr)
{
    if(!mount||!id||!inherit)return EINVAL;
    if(!cr)return EACCES;
    znode_t *zp=NULL;int error=zfs_zget(&mount->fs,object,&zp);
    if(!error)error=zfs_zaccess(zp,ACE_READ_ATTRIBUTES,0,B_FALSE,cr,NULL);
    if(!error){
        mutex_enter(&zp->z_lock);
        if(zp->z_projid>=UINT32_MAX)error=EOVERFLOW;
        else {*id=(uint32_t)zp->z_projid;*inherit=(zp->z_pflags&ZFS_PROJINHERIT)!=0;}
        mutex_exit(&zp->z_lock);
    }
    if(zp)oz_znode_rele(zp);
    return error;
}
int oz_read_mount_project_set(struct oz_read_mount *mount,uint64_t object,
    uint32_t id,int inherit,cred_t *cr)
{
    if(!mount||id==UINT32_MAX||(inherit!=0&&inherit!=1))return EINVAL;
    if(!cr)return EACCES;
    znode_t *zp=NULL;int error=zfs_zget(&mount->fs,object,&zp);
    if(!error){
        xvattr_t va;xva_init(&va);
        XVA_SET_REQ(&va,XAT_PROJID);XVA_SET_REQ(&va,XAT_PROJINHERIT);
        va.xva_xoptattrs.xoa_projid=id;va.xva_xoptattrs.xoa_projinherit=inherit;
        error=zfs_setattr(zp,&va.xva_vattr,0,cr,NULL);
    }
    if(zp)oz_znode_rele(zp);
    return error;
}
int
oz_read_mount_close(struct oz_read_mount *mount)
{
    if (!mount) return (0);
    /* Match suspend/resume/end: the owner excludes raw DOS operations.
     * Publication refs protect upstream borrowers, not an owner's transient
     * native cache pointers. Reject before unpublishing or draining the ZIL. */
    if (!oz_read_mount_is_owner(mount)) return EDEADLK;
    if (mount->suspended) return EBUSY;
    /* Fail before any teardown if upstream ioctl borrowers still exist.
     * Once detached, failed teardown remains owner-only until close retries;
     * never expose partly dismantled mount state to new ioctl lookups. */
    int detach = oz_mount_unpublish(&mount->fs);
    if (detach) return detach;
    if (mount->fs.z_log && mount->unpublished) {
        /* Checked replay has persisted its successful prefix. Do not reap
         * objects or rewrite intent records while admission is failing. */
        zil_close(mount->fs.z_log);
        mount->fs.z_log = NULL;
    }
    if (mount->fs.z_log) {
        int sync = oz_read_mount_sync(mount);
        if (sync) return sync;
        if (!zfs_is_readonly(&mount->fs)) {
            int reap = oz_unlinked_drain_exclusive(&mount->fs);
            if (reap) return reap;
            sync = oz_read_mount_sync(mount);
            if (sync) return sync;
        }
        /* Drain asynchronous ZIL references while native nodes still exist. */
        zil_close(mount->fs.z_log);
        mount->fs.z_log = NULL;
    }
    /* No asynchronous mount borrowers remain in this bridge. A busy
     * cache here is a lifetime bug, never grounds to disown a live objset. */
    int error = mount->unpublished ?
        oz_znode_cache_abort_unpublished(&mount->fs) : oz_znode_cache_fini(&mount->fs);
    if (error) return (error);
    oz_mount_policy_close(mount->policy);
    dataset_kstats_destroy(&mount->fs.z_kstat);
    zfs_fuid_destroy(&mount->fs);
    rw_destroy(&mount->fs.z_fuid_lock);
    ZFS_TEARDOWN_DESTROY(&mount->fs);
    oz_mount_refs_fini(&mount->fs);
    mount_free(mount);
    return (0);
}

int
oz_read_mount_stat(struct oz_read_mount *mount, uint64_t object,
    struct oz_zpl_info *info)
{
    if (!mount || !info) return (EINVAL);
    memset(info, 0, sizeof (*info));
    znode_t *zp;
    int error = zfs_zget(&mount->fs, object, &zp);
    if (error) return (error);
    if (S_ISFIFO(zp->z_mode)) info->special = OZ_SPECIAL_FIFO;
    else if (S_ISSOCK(zp->z_mode)) info->special = OZ_SPECIAL_SOCKET;
    else if (S_ISCHR(zp->z_mode)) info->special = OZ_SPECIAL_CHARACTER;
    else if (S_ISBLK(zp->z_mode)) info->special = OZ_SPECIAL_BLOCK;
    else if (!S_ISREG(zp->z_mode) && !S_ISDIR(zp->z_mode) && !S_ISLNK(zp->z_mode)) error = ENOTSUP;
    if (!error) {
        zfs_znode_update_vfs(zp);
        mutex_enter(&zp->z_lock);
        info->object = object;
        info->generation = zp->z_gen;
        info->mode = zp->z_mode;
        info->directory = S_ISDIR(zp->z_mode);
        info->symlink = S_ISLNK(zp->z_mode);
        info->size = zp->z_size;
        info->mtime = zp->z_mtime.tv_sec;
        info->mtime_nsec = zp->z_mtime.tv_nsec;
        info->blocks = zp->z_blocks;
        info->uid = zp->z_uid;
        info->gid = zp->z_gid;
        mutex_exit(&zp->z_lock);
        info->mapped_uid = zfs_fuid_map_id(&mount->fs, info->uid, NULL, ZFS_OWNER);
        info->mapped_gid = zfs_fuid_map_id(&mount->fs, info->gid, NULL, ZFS_GROUP);
        error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT((&mount->fs)),
            &info->parent, sizeof (info->parent));
        boolean_t present = B_FALSE;
        if (!error) error = oz_native_protection_get(zp, &info->native_protection, &present);
        if (!error) info->has_native_protection = present;
    }
    oz_znode_rele(zp);
    /* Directory enumeration must not retain every visited object until a
     * write-side sync. Only the lifecycle owner can retire idle nodes. */
    if (oz_read_mount_is_owner(mount))
        (void) oz_znode_cache_trim(&mount->fs, 256);
    return (error);
}

int oz_read_mount_same_name(struct oz_read_mount *mount,const char *a,const char *b,int *same)
{
    if (!mount || !a || !b || !same) return EINVAL;
    *same=0;
    /* Match the default ZPL lookup policy, not wide mutation-lock policy.
     * Mixed datasets preserve distinct case names unless explicitly asked
     * for ZCILOOK; native DOS requests currently do not set that flag. */
    int flags=mount->fs.z_norm,error=0;
    if (mount->fs.z_case==ZFS_CASE_MIXED) flags &= ~U8_TEXTPREP_TOUPPER;
    if (!flags) { *same=!strcmp(a,b);return 0; }
    int result=u8_strcmp(a,b,0,flags,U8_UNICODE_LATEST,&error);
    if (!error) *same=result==0;
    return error;
}

int
oz_read_mount_lookup(struct oz_read_mount *mount, uint64_t parent,
    const char *name, struct oz_zpl_info *info)
{
    if (!mount || !name || !info) return (EINVAL);
    int validation = 0;
    if (mount->fs.z_utf8 && u8_validate((char *)name, strlen(name), NULL,
        U8_VALIDATE_ENTIRE, &validation) < 0) return (EILSEQ);
    znode_t *dir = NULL, *child = NULL;
    zfs_dirlock_t *lock = NULL;
    int error = zfs_zget(&mount->fs, parent, &dir);
    if (!error) error = zfs_dirent_lock(&lock, dir, (char *)name, &child,
        ZEXISTS | ZSHARED, NULL, NULL);
    if (!error) error = oz_read_mount_stat(mount, child->z_id, info);
    if (child) oz_znode_rele(child);
    if (lock) zfs_dirent_unlock(lock);
    if (dir) oz_znode_rele(dir);
    return (error);
}

static int create_node(struct oz_read_mount *mount,uint64_t parent,const char *name,
    uint32_t protection,cred_t *cr,struct oz_zpl_info *info,int directory,const char *target)
{
    if (!info) return EINVAL;
    memset(info,0,sizeof(*info));
    if (!mount || !name || !*name || strchr(name,'/') || strchr(name,':') ||
        !strcmp(name,".") || !strcmp(name,"..")) return EINVAL;
    if (!cr) return EACCES;
    if (oz_read_mount_readonly(mount)) return EROFS;
    znode_t *dir = NULL,*file = NULL;
    int error = zfs_zget(&mount->fs,parent,&dir);
    vattr_t va = {.va_mask=ATTR_MODE,.va_mode=target?(S_IFLNK|0777):directory?(S_IFDIR|0777):(S_IFREG|0666)};
    if (!error) error = target?zfs_symlink(dir,(char *)name,&va,(char *)target,&file,cr,0,NULL):
        directory?zfs_mkdir(dir,(char *)name,&va,&file,cr,0,NULL,NULL):
        zfs_create(dir,(char *)name,&va,1,0,&file,cr,0,NULL,NULL);
    if (!error) {
        /* Initial native metadata is part of creation, not a subsequent
         * user edit subject to the newly inherited restrictive ACL. */
        error = oz_native_protection_store(file,protection,kcred);
        if (!error) error = oz_read_mount_stat(mount,file->z_id,info);
        if (!error) error = oz_read_mount_sync(mount);
        if (error) {
            int undo = directory?zfs_rmdir(dir,(char *)name,NULL,kcred,0):
                zfs_remove(dir,(char *)name,kcred,0);
            memset(info,0,sizeof(*info));
            if (undo) error = undo;
        }
    }
    if (file) oz_znode_rele(file);
    if (dir) oz_znode_rele(dir);
    return error;
}

int oz_read_mount_create(struct oz_read_mount *mount,uint64_t parent,const char *name,
    uint32_t protection,cred_t *cr,struct oz_zpl_info *info)
{return create_node(mount,parent,name,protection,cr,info,0,NULL);}
int oz_read_mount_mkdir(struct oz_read_mount *mount,uint64_t parent,const char *name,
    uint32_t protection,cred_t *cr,struct oz_zpl_info *info)
{return create_node(mount,parent,name,protection,cr,info,1,NULL);}
int oz_read_mount_symlink(struct oz_read_mount *mount,uint64_t parent,const char *name,
    const char *target,uint32_t protection,cred_t *cr)
{
    if(!target||!*target)return EINVAL;
    struct oz_zpl_info info;
    return create_node(mount,parent,name,protection,cr,&info,0,target);
}
int oz_read_mount_readlink(struct oz_read_mount *mount,uint64_t object,char *buffer,size_t size,cred_t *cr)
{
    if(!mount||!buffer||!size)return EINVAL;
    if(!cr)return EACCES;
    znode_t *node=NULL;int error=zfs_zget(&mount->fs,object,&node);
    if(!error&&!S_ISLNK(node->z_mode))error=EINVAL;
    if(!error&&node->z_size>=size)error=ENAMETOOLONG;
    if(!error) {
        struct iovec vec={buffer,size-1};zfs_uio_t uio;
        zfs_uio_iovec_init(&uio,&vec,1,0,UIO_SYSSPACE,size-1,0);
        error=zfs_readlink(node,&uio,cr);
        if(!error)buffer[size-1-uio.uio_resid]=0;
    }
    if(node)oz_znode_rele(node);
    return error;
}

int oz_read_mount_remove(struct oz_read_mount *mount,uint64_t parent,const char *name,
    int directory,cred_t *cr)
{
    if (!mount || !name || !*name || strchr(name,'/') || strchr(name,':') ||
        !strcmp(name,".") || !strcmp(name,"..")) return EINVAL;
    if (!cr) return EACCES;
    if (oz_read_mount_readonly(mount)) return EROFS;
    znode_t *dir = NULL;
    int error = zfs_zget(&mount->fs,parent,&dir);
    if (!error) error = directory?zfs_rmdir(dir,(char *)name,NULL,cr,0):
        zfs_remove(dir,(char *)name,cr,0);
    if (dir) oz_znode_rele(dir);
    const char *stage="namespace";
    if (!error) {stage="pre-reap sync";error = oz_read_mount_sync(mount);}
    /* Single-owner DOS admission excludes operations and target references
     * throughout unlink/reap. Do not call from a concurrent mount worker. */
    if (!error) {stage="reap";error = oz_unlinked_drain_exclusive(&mount->fs);}
    if (!error) {stage="post-reap sync";error = oz_read_mount_sync(mount);}
    if(error) {
        char diagnostic[384];
        snprintf(diagnostic,sizeof(diagnostic),"OpenZFS unlink phase=%s name=%s errno=%d\n",stage,name,error);
        for(const char *p=diagnostic;*p;p++)RawPutChar(*p);
    }
    return error;
}

static int mount_rename(struct oz_read_mount *mount,uint64_t source,const char *oldname,
    uint64_t target,const char *newname,cred_t *cr,uint64_t flags)
{
    if (!mount || !oldname || !newname || !*oldname || !*newname ||
        strpbrk(oldname,"/:") || strpbrk(newname,"/:") || !strcmp(oldname,".") ||
        !strcmp(oldname,"..") || !strcmp(newname,".") || !strcmp(newname,"..")) return EINVAL;
    if (!cr) return EACCES;
    if (oz_read_mount_readonly(mount)) return EROFS;
    znode_t *from=NULL,*to=NULL;
    int error=zfs_zget(&mount->fs,source,&from);
    if (!error) error=zfs_zget(&mount->fs,target,&to);
    vattr_t whiteout={.va_mask=ATTR_MODE,.va_mode=S_IFCHR,.va_rdev=0};
    if (!error) error=zfs_rename(from,(char *)oldname,to,(char *)newname,
        cr,0,flags,(flags & RENAME_WHITEOUT)?&whiteout:NULL,NULL);
    if (to) oz_znode_rele(to);
    if (from) oz_znode_rele(from);
    if (!error) error=oz_read_mount_sync(mount);
    return error;
}

int oz_read_mount_rename(struct oz_read_mount *mount,uint64_t source,const char *oldname,
    uint64_t target,const char *newname,cred_t *cr)
{return mount_rename(mount,source,oldname,target,newname,cr,RENAME_NOREPLACE);}

int oz_read_mount_exchange(struct oz_read_mount *mount,uint64_t source,const char *oldname,
    uint64_t target,const char *newname,cred_t *cr)
{return mount_rename(mount,source,oldname,target,newname,cr,RENAME_EXCHANGE);}

int oz_read_mount_whiteout(struct oz_read_mount *mount,uint64_t source,const char *oldname,
    uint64_t target,const char *newname,cred_t *cr)
{return mount_rename(mount,source,oldname,target,newname,cr,RENAME_WHITEOUT|RENAME_NOREPLACE);}

int oz_read_mount_whiteout_replace(struct oz_read_mount *mount,uint64_t source,const char *oldname,
    uint64_t target,const char *newname,cred_t *cr)
{return mount_rename(mount,source,oldname,target,newname,cr,RENAME_WHITEOUT);}

int oz_read_mount_replace(struct oz_read_mount *mount,uint64_t parent,
    const char *source,const char *target,const struct oz_zpl_info *fresh,
    const struct oz_zpl_info *old,cred_t *cr,int (*check)(void *),void *context)
{
    if(!mount||!source||!target||!fresh||!old||!check||
        !*source||!*target||strpbrk(source,"/:")||strpbrk(target,"/:")||
        !strcmp(source,".")||!strcmp(source,"..")||
        !strcmp(target,".")||!strcmp(target,".."))return EINVAL;
    if(!cr)return EACCES;
    if(oz_read_mount_readonly(mount))return EROFS;
    struct oz_rename_guard guard={fresh->object,fresh->generation,
        old->object,old->generation,check,context};
    znode_t *dir=NULL;
    int error=zfs_zget(&mount->fs,parent,&dir);
    if(!error)error=oz_zfs_replace(dir,(char *)source,(char *)target,cr,&guard);
    if(dir)oz_znode_rele(dir);
    if(!error)error=oz_read_mount_sync(mount);
    /* Same single-owner admission as remove: publication has dropped the
     * overwritten link and the sync drains ZIL holds before reclamation.
     * An old object with another hard link never enters the unlinked set. */
    if(!error)error=oz_unlinked_drain_exclusive(&mount->fs);
    if(!error)error=oz_read_mount_sync(mount);
    return error;
}

int oz_read_mount_link(struct oz_read_mount *mount,uint64_t parent,uint64_t object,
    const char *name,cred_t *cr)
{
    if(!mount||!name||!*name||strpbrk(name,"/:")||!strcmp(name,".")||!strcmp(name,".."))return EINVAL;
    if(!cr)return EACCES;
    if(oz_read_mount_readonly(mount))return EROFS;
    znode_t *dir=NULL,*file=NULL;
    int error=zfs_zget(&mount->fs,parent,&dir);
    if(!error)error=zfs_zget(&mount->fs,object,&file);
    if(!error)error=zfs_link(dir,file,(char *)name,cr,0);
    if(file)oz_znode_rele(file);
    if(dir)oz_znode_rele(dir);
    if(!error)error=oz_read_mount_sync(mount);
    return error;
}

int oz_read_mount_resize(struct oz_read_mount *mount,uint64_t object,uint64_t size,cred_t *cr)
{
    if (!mount) return EINVAL;
    if (!cr) return EACCES;
    if (size > INT64_MAX) return EOVERFLOW;
    if (oz_read_mount_readonly(mount)) return EROFS;
    znode_t *zp = NULL;
    int error = zfs_zget(&mount->fs,object,&zp);
    if (!error && !S_ISREG(zp->z_mode)) error = EISDIR;
    vattr_t va = {.va_mask = ATTR_SIZE,.va_size = size};
    if (!error) error = zfs_setattr(zp,&va,0,cr,NULL);
    if (!error) error = zfs_fsync(zp,0,cr);
    if (zp) oz_znode_rele(zp);
    return error;
}

int oz_read_mount_write(struct oz_read_mount *mount,uint64_t object,
    uint64_t offset,const void *buffer,size_t size,size_t *written,cred_t *cr)
{
    if (!written) return EINVAL;
    *written = 0;
    if (!mount || (!buffer && size)) return EINVAL;
    if (!cr) return EACCES;
    if (offset > INT64_MAX || size > INT64_MAX-offset) return EOVERFLOW;
    if (oz_read_mount_readonly(mount)) return EROFS;
    znode_t *zp = NULL;
    int error = zfs_zget(&mount->fs,object,&zp);
    if (!error && !S_ISREG(zp->z_mode)) error = EISDIR;
    if (!error) error = zfs_zaccess(zp,ACE_WRITE_DATA,0,B_FALSE,cr,NULL);
    if (!error) {
        struct iovec vec = {(void *)buffer,size}; zfs_uio_t uio;
        zfs_uio_iovec_init(&uio,&vec,1,offset,UIO_SYSSPACE,size,0);
        error = zfs_write(zp,&uio,0,cr);
        *written = size-uio.uio_resid;
        /* A DOS success must not precede a failed log commit. Preserve
         * partial-write accounting even when a later sync reports error. */
        if (!error) error = zfs_fsync(zp,0,cr);
    }
    if (zp) oz_znode_rele(zp);
    return error;
}

int oz_read_mount_rewrite(struct oz_read_mount *mount,uint64_t object,
    uint64_t offset,uint64_t length,uint64_t flags,uint64_t argument,cred_t *cr)
{
    if(!mount)return EINVAL;
    if(!cr)return EACCES;
    if((flags&~ZFS_REWRITE_PHYSICAL)||argument)return EINVAL;
    if(oz_read_mount_readonly(mount))return EROFS;
    znode_t *zp=NULL;
    int error=zfs_zget(&mount->fs,object,&zp);
    if(!error&&!S_ISREG(zp->z_mode))error=S_ISDIR(zp->z_mode)?EISDIR:ENOTSUP;
    if(!error)error=zfs_zaccess(zp,ACE_WRITE_DATA,0,B_FALSE,cr,NULL);
    if(!error)error=zfs_rewrite(zp,offset,length,flags,argument);
    if(zp)oz_znode_rele(zp);
    return error;
}

int
oz_read_mount_read(struct oz_read_mount *mount, uint64_t object,
    uint64_t offset, void *buffer, size_t size)
{
    if (!mount || (!buffer && size)) return (EINVAL);
    if (offset > INT64_MAX || size > INT64_MAX - offset) return (EOVERFLOW);
    znode_t *zp;
    int error = zfs_zget(&mount->fs, object, &zp);
    if (error) return (error);
    if (!S_ISREG(zp->z_mode)) error = S_ISDIR(zp->z_mode) ? EISDIR : ENOTSUP;
    if (!error && size) {
        /* Frontend promises exact-sized reads after its EOF clamp. Do not
         * return synthetic zero-filled bytes beyond the file's logical end. */
        if (offset > zp->z_size || size > zp->z_size - offset) error = EINVAL;
        else {
            struct iovec vec = {buffer, size};
            zfs_uio_t uio;
            zfs_uio_iovec_init(&uio, &vec, 1, offset, UIO_SYSSPACE, size, 0);
            /* Upstream owns range locking, quarantine checks, DMU/ARC reads,
             * accounting and atime policy. Do not double-count here. This
             * bootstrap mount is immutable/read-only; namespace mutation and
             * authenticated request admission remain separate work. */
            error = zfs_read(zp, &uio, 0, NULL);
            if (!error && uio.uio_resid) error = EIO;
        }
    }
    oz_znode_rele(zp);
    return (error);
}

int
oz_read_mount_counter(struct oz_read_mount *mount, const char *name, uint64_t *out)
{
    if (!mount || !name || !out) return EINVAL;
    if (!strcmp(name, "cached_nodes")) {
        uint64_t count = 0;
        mutex_enter(&mount->fs.z_znodes_lock);
        for (znode_t *zp = list_head(&mount->fs.z_all_znodes); zp;
            zp = list_next(&mount->fs.z_all_znodes, zp)) count++;
        mutex_exit(&mount->fs.z_znodes_lock);
        *out = count;
        return 0;
    }
    return mount ? oz_dataset_read_counter(&mount->fs.z_kstat, name, out) : EINVAL;
}

int
oz_read_mount_access(struct oz_read_mount *mount, uint64_t object,
    uint32_t requested, cred_t *cr)
{
    if (!mount || (requested & ~ACE_ALL_PERMS)) return (EINVAL);
    if (!cr) return (EACCES);
    znode_t *zp;
    int error = zfs_zget(&mount->fs, object, &zp);
    if (error) return (error);
    if ((requested & ACE_EXECUTE) && S_ISREG(zp->z_mode) &&
        !oz_read_mount_exec(mount)) error = EACCES;
    else error = zfs_zaccess(zp, requested, 0, B_FALSE, cr, NULL);
    oz_znode_rele(zp);
    return (error);
}
int oz_read_mount_exec(struct oz_read_mount *mount)
{ return mount && mount->policy && mount->policy->exec; }

int oz_read_mount_policy(struct oz_read_mount *mount,uint32_t *values)
{
    if(!values)return EINVAL;
    *values=0;
    if(!mount||!mount->policy||mount->suspended||mount->fs.z_unmounted)return EBUSY;
    *values=(oz_read_mount_readonly(mount)?OZ_POLICY_READONLY:0)|
        (mount->fs.z_atime?OZ_POLICY_ATIME:0)|
        (mount->fs.z_relatime?OZ_POLICY_RELATIME:0)|
        (mount->policy->exec?OZ_POLICY_EXEC:0);
    return 0;
}
int oz_read_mount_shares_object(struct oz_read_mount *mount,uint64_t *object)
{
    if(!object)return EINVAL;
    *object=0;
    if(!mount||mount->suspended||mount->fs.z_unmounted)return EBUSY;
    /* Read upstream metadata, not a guessed object number or an assumed
     * absence on pools imported from another operating system. */
    uint64_t value=0;
    int error=zap_lookup(mount->fs.z_os,MASTER_NODE_OBJ,ZFS_SHARES_DIR,8,1,&value);
    if(error==ENOENT)return 0;
    if(!error)*object=value;
    return error;
}
