/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_ZPL_VFSOPS_H
#define OZ_AROS_ZPL_VFSOPS_H
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/dmu.h>
#include <sys/zil.h>
#include <sys/sa.h>
#include <sys/rrwlock.h>
#include <sys/dataset_kstats.h>
#include <sys/aros_idmap.h>
#include "../../lifecycle.h"

/* Native mount state for upstream ZPL. No Linux superblock or BSD vnode is
 * embedded here. These fields retain their upstream meaning; initialization,
 * callback registration and teardown must precede any ZPL operation. */
typedef struct zfsvfs {
    struct zfsvfs *z_parent;
    objset_t *z_os;
    const struct oz_idmap *z_idmap;
    uint64_t z_root, z_unlinkedobj, z_max_blksz;
    uint64_t z_fuid_obj, z_fuid_size;
    avl_tree_t z_fuid_idx, z_fuid_domain;
    krwlock_t z_fuid_lock;
    boolean_t z_fuid_loaded, z_fuid_dirty;
    struct zfs_fuid_info *z_fuid_replay;
    zilog_t *z_log;
    uint_t z_acl_mode, z_acl_inherit, z_acl_type;
    zfs_case_t z_case;
    boolean_t z_utf8;
    int z_norm;
    boolean_t z_readonly, z_atime, z_relatime, z_unmounted;
    boolean_t z_issnap, z_use_fuids, z_use_sa, z_xattr_sa;
    boolean_t z_replay, z_draining, z_drain_cancel, z_longname;
    rrmlock_t z_teardown_lock;
    krwlock_t z_teardown_inactive_lock;
    list_t z_all_znodes;
    kmutex_t z_znodes_lock;
    uint64_t z_version, z_shares_dir, z_replay_eof;
    dataset_kstats_t z_kstat;
    kmutex_t z_lock;
    uint64_t z_userquota_obj, z_groupquota_obj, z_projectquota_obj;
    uint64_t z_userobjquota_obj, z_groupobjquota_obj, z_projectobjquota_obj;
    uint64_t z_defaultuserquota, z_defaultgroupquota, z_defaultprojectquota;
    uint64_t z_defaultuserobjquota, z_defaultgroupobjquota;
    uint64_t z_defaultprojectobjquota;
    sa_attr_type_t *z_attr_table;
    uint64_t z_hold_size;
    avl_tree_t *z_hold_trees;
    kmutex_t *z_hold_locks;
    taskqid_t z_drain_task;
    /* Mount-private lifetime lock survives replacement/eviction of z_os.
     * Publication lock order: os_user_ptr_lock, then z_native_ref_lock. */
    kmutex_t z_native_ref_lock;
    size_t z_native_refs;
    boolean_t z_use_hold;
    uint64_t z_cache_walkers;
    /* Cross-parent namespace moves: teardown read -> rename -> dirent.
     * Native replacement for the host VFS ancestry serialization contract. */
    kmutex_t z_rename_lock;
} zfsvfs_t;

#define ZFS_TEARDOWN_INIT(fs) rrm_init(&(fs)->z_teardown_lock, B_FALSE)
#define ZFS_TEARDOWN_DESTROY(fs) rrm_destroy(&(fs)->z_teardown_lock)
#define ZFS_TEARDOWN_ENTER_READ(fs, tag) rrm_enter_read(&(fs)->z_teardown_lock, tag)
#define ZFS_TEARDOWN_ENTER_WRITE(fs, tag) rrm_enter(&(fs)->z_teardown_lock, RW_WRITER, tag)
#define ZFS_TEARDOWN_EXIT_READ(fs, tag) rrm_exit(&(fs)->z_teardown_lock, tag)
#define ZFS_TEARDOWN_EXIT_WRITE(fs, tag) rrm_exit(&(fs)->z_teardown_lock, tag)
#define ZFS_TEARDOWN_EXIT(fs, tag) rrm_exit(&(fs)->z_teardown_lock, tag)
#define ZFS_TEARDOWN_READ_HELD(fs) RRM_READ_HELD(&(fs)->z_teardown_lock)
#define ZFS_TEARDOWN_WRITE_HELD(fs) RRM_WRITE_HELD(&(fs)->z_teardown_lock)
#define ZFS_TEARDOWN_HELD(fs) RRM_LOCK_HELD(&(fs)->z_teardown_lock)

boolean_t zfs_is_readonly(zfsvfs_t *);
int zfs_set_default_quota(zfsvfs_t *, zfs_prop_t, uint64_t);
int zfs_set_version(zfsvfs_t *, uint64_t);
struct dsl_dataset;
int zfs_suspend_fs(zfsvfs_t *);
int zfs_resume_fs(zfsvfs_t *, struct dsl_dataset *);
int zfs_end_fs(zfsvfs_t *, struct dsl_dataset *);
int zfsvfs_create_hold(const char *, zfsvfs_t **);
void zfsvfs_free(zfsvfs_t *);
int oz_mount_publish(zfsvfs_t *);
int oz_mount_unpublish(zfsvfs_t *);
void oz_mount_refs_init(zfsvfs_t *);
void oz_mount_refs_fini(zfsvfs_t *);
#endif
