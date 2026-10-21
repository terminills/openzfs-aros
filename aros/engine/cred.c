/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <limits.h>
#include <sys/aros_idmap.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_prop.h>
#include <sys/dmu_objset.h>
#include <zfs_namecheck.h>
_Static_assert(sizeof (uid_t) == sizeof (uint32_t), "AROS ZFS needs 32-bit uid_t");
_Static_assert(sizeof (gid_t) == sizeof (uint32_t), "AROS ZFS needs 32-bit gid_t");

struct cred {
    uid_t uid, ruid;
    gid_t gid;
    int ngroups;
    volatile uint32_t references;
    gid_t groups[];
};

static struct cred kernel_identity = {
    .uid = 0, .ruid = 0, .gid = 0, .references = UINT32_MAX
};

cred_t *
oz_cred_create(uid_t uid, uid_t ruid, gid_t gid, size_t count, const gid_t *groups)
{
    if (uid == OZ_ID_UNMAPPED || ruid == OZ_ID_UNMAPPED || gid == OZ_ID_UNMAPPED ||
        (count && !groups) || count > INT_MAX ||
        count > (SIZE_MAX - sizeof (cred_t)) / sizeof (gid_t)) {
        errno = EINVAL;
        return (NULL);
    }
    for (size_t i = 0; i < count; i++)
        if (groups[i] == OZ_ID_UNMAPPED) { errno = EINVAL; return (NULL); }
    cred_t *cr = malloc(sizeof (*cr) + count * sizeof (gid_t));
    if (!cr)
        return (NULL);
    cr->uid = uid;
    cr->ruid = ruid;
    cr->gid = gid;
    cr->ngroups = count;
    cr->references = 1;
    if (count)
        memcpy(cr->groups, groups, count * sizeof (gid_t));
    return (cr);
}

cred_t *oz_cred_kernel(void) { return (&kernel_identity); }

struct oz_cred_scope {
    cred_t *cred;
    struct oz_cred_scope *previous;
};
static pthread_once_t scope_once = PTHREAD_ONCE_INIT;
static pthread_key_t scope_key;
static int scope_error;
static void scope_destroy(void *value)
{
    struct oz_cred_scope *scope = value;
    while (scope) {
        struct oz_cred_scope *previous = scope->previous;
        oz_cred_release(scope->cred);
        free(scope);
        scope = previous;
    }
}
static void scope_init(void)
{ scope_error = pthread_key_create(&scope_key, scope_destroy); }
static int scope_ready(void)
{
    int error = pthread_once(&scope_once, scope_init);
    return error ? error : scope_error;
}
cred_t *oz_cred_current(void)
{
    if (scope_ready()) return NULL;
    struct oz_cred_scope *scope = pthread_getspecific(scope_key);
    return scope ? scope->cred : NULL;
}
int oz_cred_push(cred_t *cr, struct oz_cred_scope **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!cr) return EACCES;
    int error = scope_ready();
    if (error) return error;
    struct oz_cred_scope *scope = malloc(sizeof(*scope));
    if (!scope) return ENOMEM;
    scope->cred = cr;
    scope->previous = pthread_getspecific(scope_key);
    oz_cred_hold(cr);
    error = pthread_setspecific(scope_key, scope);
    if (error) { oz_cred_release(cr); free(scope); return error; }
    *out = scope;
    return 0;
}
int oz_cred_pop(struct oz_cred_scope *scope)
{
    int error = scope_ready();
    if (error) return error;
    /* Check thread ownership and LIFO order before dereferencing the token. */
    if (!scope || pthread_getspecific(scope_key) != scope) return EINVAL;
    error = pthread_setspecific(scope_key, scope->previous);
    if (error) return error;
    oz_cred_release(scope->cred);
    free(scope);
    return 0;
}

void
oz_cred_hold(cred_t *cr)
{
    if (cr && cr != &kernel_identity) {
        uint32_t count;
        do {
            count = __atomic_load_n(&cr->references, __ATOMIC_RELAXED);
            VERIFY3U(count, >, 0);
            VERIFY3U(count, <, UINT32_MAX);
        } while (!__atomic_compare_exchange_n(&cr->references, &count,
            count + 1, B_FALSE, __ATOMIC_RELAXED, __ATOMIC_RELAXED));
    }
}

void
oz_cred_release(cred_t *cr)
{
    if (cr && cr != &kernel_identity) {
        uint32_t previous = __atomic_fetch_sub(&cr->references, 1, __ATOMIC_ACQ_REL);
        VERIFY3U(previous, >, 0);
        if (previous == 1)
            free(cr);
    }
}

uid_t crgetuid(cred_t *cr) { return (cr ? cr->uid : UID_NOBODY); }
uid_t crgetruid(cred_t *cr) { return (cr ? cr->ruid : UID_NOBODY); }
gid_t crgetgid(cred_t *cr) { return (cr ? cr->gid : GID_NOBODY); }
int crgetngroups(cred_t *cr) { return (cr ? cr->ngroups : 0); }
/* Upstream's borrowed-pointer API: callers must not modify this snapshot. */
gid_t *crgetgroups(cred_t *cr) { return (cr && cr->ngroups ? cr->groups : NULL); }

int
groupmember(gid_t gid, cred_t *cr)
{
    if (!cr)
        return (0);
    if (cr->gid == gid)
        return (1);
    for (int i = 0; i < cr->ngroups; i++)
        if (cr->groups[i] == gid)
            return (1);
    return (0);
}

int
secpolicy_zfs(const cred_t *cr)
{
    return (cr && cr->uid == 0 ? 0 : EPERM);
}

/* Native policy has one global privilege domain, not Linux capabilities or
 * Solaris zones. Only the authenticated effective-root identity admitted at
 * the service boundary can configure pools or authorize fault injection.
 * Dataset delegation, real UID zero and group zero do not grant this power. */
int secpolicy_sys_config(const cred_t *cr, boolean_t checkonly)
{
    (void)checkonly; /* No privilege acquisition or mutation in either mode. */
    return secpolicy_zfs(cr);
}

int secpolicy_zinject(const cred_t *cr)
{
    return secpolicy_zfs(cr) ? EACCES : 0;
}

/* MuFS/security.library ownership is not a Solaris Trusted Extensions MLS
 * label policy. Advertise that absence so upstream rejects mlslabel changes. */
boolean_t is_system_labeled(void) { return B_FALSE; }

/* AROS currently has no per-credential CAP_FSETID equivalent in this adapter.
 * Retaining executable set-ID bits is privileged, even for the file owner.
 * The upstream write path clears and journals those bits when we deny it.
 * Native security.library root must be explicitly mapped before reaching here;
 * a native owner number of zero is not an authenticated POSIX root credential.
 */
int
secpolicy_vnode_setid_retain(struct znode *zp, const cred_t *cr,
    boolean_t issuidroot)
{
    (void)zp;
    (void)issuidroot;
    return (cr && cr->uid == 0 ? 0 : EPERM);
}

/* Port of the write-permission and compound-operation policy from
 * module/zfs/zfs_ioctl.c. AROS has one global namespace (no local zones).
 * Keep the upstream zoned-dataset restriction for non-administrators.
 * Authentication is the caller's responsibility; NULL never means root or
 * an anonymous user eligible for an "everyone" delegation. */
static int dataset_write_perms_ds(dsl_dataset_t *ds,const char *perm,cred_t *cr)
{
    if(!cr)return EPERM;
    if(!secpolicy_zfs(cr))return 0;
    uint64_t zoned;
    int error=dsl_prop_get_int_ds(ds,zfs_prop_to_name(ZFS_PROP_ZONED),&zoned);
    return error?ENOENT:zoned?EPERM:dsl_deleg_access_impl(ds,perm,cr);
}
static int dataset_write_perms(const char *name, const char *perm, cred_t *cr)
{
    if (!cr) return EPERM;
    if (!name || !*name) return EINVAL;
    if (!secpolicy_zfs(cr)) return 0;
    dsl_pool_t *dp;
    dsl_dataset_t *ds;
    int error = dsl_pool_hold(name, FTAG, &dp);
    if (error) return error;
    error = dsl_dataset_hold(dp, name, FTAG, &ds);
    if (!error) {
        error = dataset_write_perms_ds(ds,perm,cr);
        dsl_dataset_rele(ds, FTAG);
    }
    dsl_pool_rele(dp, FTAG);
    return error;
}

#ifndef OZ_NATIVE_DISPATCHER
int zfs_secpolicy_snapshot_perms(const char *name, cred_t *cr)
{ return dataset_write_perms(name, ZFS_DELEG_PERM_SNAPSHOT, cr); }
#endif
int oz_dataset_mount_perms(const char *name,cred_t *cr)
{ return dataset_write_perms(name,ZFS_DELEG_PERM_MOUNT,cr); }
int oz_dataset_userprop_perms(const char *name,cred_t *cr)
{ return dataset_write_perms(name,ZFS_DELEG_PERM_USERPROP,cr); }
int oz_dataset_property_perms(const char *name,const char *property,cred_t *cr)
{ return dataset_write_perms(name,property,cr); }

/* Upstream zfs_secpolicy_send_impl/new, without pretending that a raw send
 * grant also authorizes plaintext. Native caller authentication is separate. */
int oz_send_perms(const char *name,boolean_t raw,cred_t *cr)
{
    if(!cr)return EPERM;
    if(!name||dataset_namecheck(name,NULL,NULL))return EINVAL;
    if(!secpolicy_zfs(cr))return 0;
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
    int error=dsl_pool_hold(name,FTAG,&dp);if(error)return error;
    error=dsl_dataset_hold(dp,name,FTAG,&ds);
    if(!error) {
        uint64_t zoned;
        error=dsl_prop_get_int_ds(ds,zfs_prop_to_name(ZFS_PROP_ZONED),&zoned);
        if(error)error=ENOENT;
        else if(zoned)error=EPERM;
        else {
            error=dsl_deleg_access_impl(ds,ZFS_DELEG_PERM_SEND,cr);
            if(error&&raw)error=dsl_deleg_access_impl(ds,ZFS_DELEG_PERM_SEND_RAW,cr);
            if(error&&raw&&ds->ds_dir->dd_crypto_obj)
                error=dsl_deleg_access_impl(ds,ZFS_DELEG_PERM_SEND_ENCRYPTED,cr);
        }
    }
    if(ds)dsl_dataset_rele(ds,FTAG);
    dsl_pool_rele(dp,FTAG);return error;
}

/* libzfs_core checks receive permission on the existing filesystem, or its
 * immediate parent for a new filesystem. Match zfs_secpolicy_recv on that
 * held dataset: receive (or append without force), plus mount and create. */
int oz_receive_perms(const char *name,boolean_t force,cred_t *cr)
{
    if(!cr)return EPERM;
    if(!name||dataset_namecheck(name,NULL,NULL)||strpbrk(name,"@#"))return EINVAL;
    if(!secpolicy_zfs(cr))return 0;
    char target[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(target,name,sizeof(target))>=sizeof(target))return ENAMETOOLONG;
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
    int error=dsl_pool_hold(target,FTAG,&dp);if(error)return error;
    error=dsl_dataset_hold(dp,target,FTAG,&ds);
    if(error==ENOENT) {
        char *slash=strrchr(target,'/');
        if(slash){*slash=0;error=dsl_dataset_hold(dp,target,FTAG,&ds);}
    }
    if(!error) {
        error=dataset_write_perms_ds(ds,ZFS_DELEG_PERM_RECEIVE,cr);
        if(error&&!force)error=dataset_write_perms_ds(ds,ZFS_DELEG_PERM_RECEIVE_APPEND,cr);
        if(!error)error=dataset_write_perms_ds(ds,ZFS_DELEG_PERM_MOUNT,cr);
        if(!error)error=dataset_write_perms_ds(ds,ZFS_DELEG_PERM_CREATE,cr);
    }
    if(ds)dsl_dataset_rele(ds,FTAG);
    dsl_pool_rele(dp,FTAG);return error;
}
int oz_dataset_rollback_perms(const char *name,cred_t *cr)
{ return dataset_write_perms(name,ZFS_DELEG_PERM_ROLLBACK,cr); }
int oz_bookmark_perms(const char *name,int destroy,cred_t *cr)
{
    char dataset[ZFS_MAX_DATASET_NAME_LEN];
    if(!name||strlcpy(dataset,name,sizeof(dataset))>=sizeof(dataset))return EINVAL;
    char *hash=strchr(dataset,'#');
    if(!hash)return EINVAL;
    *hash=0;
    return dataset_write_perms(dataset,destroy?ZFS_DELEG_PERM_DESTROY:ZFS_DELEG_PERM_BOOKMARK,cr);
}
/* Upstream zfs_secpolicy_promote: promote+mount on the clone, and promote
 * on its origin. Return the actual origin name under the same pool hold so
 * the service can reject live native mounts before changing ancestry. */
int oz_dataset_promote_perms(const char *name,cred_t *cr,char *origin_name)
{
    int error=dataset_write_perms(name,ZFS_DELEG_PERM_PROMOTE,cr);
    if(error)return error;
    dsl_pool_t *dp=NULL;dsl_dataset_t *clone=NULL,*origin=NULL;
    error=dsl_pool_hold(name,FTAG,&dp);
    if(error)return error;
    error=dsl_dataset_hold(dp,name,FTAG,&clone);
    if(!error&&!dsl_dir_is_clone(clone->ds_dir))error=EINVAL;
    if(!error)error=dsl_dataset_hold_obj(dp,dsl_dir_phys(clone->ds_dir)->dd_origin_obj,FTAG,&origin);
    if(!error)error=dataset_write_perms_ds(clone,ZFS_DELEG_PERM_MOUNT,cr);
    if(!error)error=dataset_write_perms_ds(origin,ZFS_DELEG_PERM_PROMOTE,cr);
    if(!error)dsl_dataset_name(origin,origin_name);
    if(origin)dsl_dataset_rele(origin,FTAG);
    if(clone)dsl_dataset_rele(clone,FTAG);
    dsl_pool_rele(dp,FTAG);
    return error;
}
int oz_dataset_create_perms(const char *parent, cred_t *cr)
{
    int error = dataset_write_perms(parent, ZFS_DELEG_PERM_CREATE, cr);
    return error ? error : dataset_write_perms(parent, ZFS_DELEG_PERM_MOUNT, cr);
}
int oz_snapshot_clone_perms(const char *origin,const char *destination,cred_t *cr)
{
    char parent[ZFS_MAX_DATASET_NAME_LEN];
    if(!destination)return EINVAL;
    if(strlcpy(parent,destination,sizeof(parent))>=sizeof(parent))return ENAMETOOLONG;
    char *cut=strrchr(parent,'/');
    if(!cut)return ENOENT;
    *cut=0;
    int error=dataset_write_perms(origin,ZFS_DELEG_PERM_CLONE,cr);
    return error?error:oz_dataset_create_perms(parent,cr);
}
int oz_snapshot_hold_perms(const char *snapshot,int release,cred_t *cr)
{
    char filesystem[ZFS_MAX_DATASET_NAME_LEN];
    int error=dmu_fsname(snapshot,filesystem);
    return error?error:dataset_write_perms(filesystem,
        release?ZFS_DELEG_PERM_RELEASE:ZFS_DELEG_PERM_HOLD,cr);
}
#ifndef OZ_NATIVE_DISPATCHER
int zfs_secpolicy_rename_perms(const char *from, const char *to, cred_t *cr)
{
    int error = dataset_write_perms(from, ZFS_DELEG_PERM_RENAME, cr);
    if (error) return error;
    error = dataset_write_perms(from, ZFS_DELEG_PERM_MOUNT, cr);
    if (error) return error;
    if (!to) return EINVAL;
    char parent[ZFS_MAX_DATASET_NAME_LEN];
    if (strlcpy(parent, to, sizeof(parent)) >= sizeof(parent)) return ENAMETOOLONG;
    char *cut = strrchr(parent, '@');
    if (!cut) cut = strrchr(parent, '/');
    if (!cut) return ENOENT;
    *cut = 0;
    error = dataset_write_perms(parent, ZFS_DELEG_PERM_CREATE, cr);
    if (error) return error;
    return dataset_write_perms(parent, ZFS_DELEG_PERM_MOUNT, cr);
}
int zfs_secpolicy_destroy_perms(const char *name, cred_t *cr)
{
    int error = dataset_write_perms(name, ZFS_DELEG_PERM_MOUNT, cr);
    return error ? error : dataset_write_perms(name, ZFS_DELEG_PERM_DESTROY, cr);
}
#endif /* upstream dispatcher supplies these policies, using native cred_t */
