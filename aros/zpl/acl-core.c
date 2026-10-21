/* SPDX-License-Identifier: CDDL-1.0 */
/* Compile upstream ACE/ACL representation, iteration and conversion directly.
 * The OS-specific access checks are not replaced by successful placeholders. */
#include <sys/zfs_znode.h>

/* Native callers supply an explicit byte length. Validate the complete input
 * before upstream conversion can inspect variable-sized object ACEs or create
 * identity mappings. Upstream still owns ACE semantic validation/conversion. */
static int
oz_vsec_validate(const vsecattr_t *v)
{
    if (!v->vsa_aclentp || v->vsa_aclcnt <= 0 ||
        v->vsa_aclcnt > MAX_ACL_ENTRIES ||
        (uintptr_t)v->vsa_aclentp % _Alignof(ace_t)) return (EINVAL);
    size_t used = 0;
    for (int i = 0; i < v->vsa_aclcnt; i++) {
        if (used > v->vsa_aclentsz ||
            sizeof (ace_t) > v->vsa_aclentsz - used) return (EINVAL);
        const ace_t *ace = (const ace_t *)((const char *)v->vsa_aclentp + used);
        size_t bytes = sizeof (*ace);
        switch (ace->a_type) {
        case ACE_ACCESS_ALLOWED_OBJECT_ACE_TYPE:
        case ACE_ACCESS_DENIED_OBJECT_ACE_TYPE:
        case ACE_SYSTEM_AUDIT_OBJECT_ACE_TYPE:
        case ACE_SYSTEM_ALARM_OBJECT_ACE_TYPE:
            bytes = sizeof (ace_object_t);
            break;
        }
        if (bytes > v->vsa_aclentsz - used) return (EINVAL);
        used += bytes;
    }
    return (used == v->vsa_aclentsz ? 0 : EINVAL);
}

/* AROS has no Linux VFS to perform the discretionary override for us.
 * These policies use explicit POSIX credential snapshots; they do not map
 * or authenticate native security.library identities. */
static int
oz_access_policy(const cred_t *cr, znode_t *zp, uid_t owner,
    mode_t current, mode_t wanted)
{
    (void)owner;
    if (!cr) return (EACCES);
    mode_t missing = wanted & ~current;
    if (!missing) return (0);
    if (crgetuid((cred_t *)cr) != 0) return (EACCES);
    if ((missing & S_IXUSR) && !S_ISDIR(zp->z_mode) &&
        !(zp->z_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) return (EACCES);
    return (0);
}

static int
oz_owner_policy(const cred_t *cr, uid_t owner)
{
    return (cr && (crgetuid((cred_t *)cr) == owner ||
        crgetuid((cred_t *)cr) == 0) ? 0 : EPERM);
}

static int
oz_remove_policy(const cred_t *cr)
{
    return (cr && crgetuid((cred_t *)cr) == 0 ? 0 : EPERM);
}

#define secpolicy_vnode_access2 oz_access_policy
#define secpolicy_vnode_chown oz_owner_policy
#define secpolicy_vnode_setdac oz_owner_policy
#define secpolicy_vnode_remove oz_remove_policy
/* Upstream sticky-directory policy using native owner IDs, not Linux inodes. */
int
zfs_sticky_remove_access(znode_t *dzp, znode_t *zp, cred_t *cr)
{
    if (!dzp || !zp) return (EINVAL);
    if (!cr) return (EACCES);
    zfsvfs_t *fs = ZTOZSB(dzp);
    if (fs->z_replay && cr != kcred) return (EACCES);
    if (!(dzp->z_mode & S_ISVTX)) return (0);
    uid_t owner = zfs_fuid_map_id(fs, dzp->z_uid, cr, ZFS_OWNER);
    uid_t target = zfs_fuid_map_id(fs, zp->z_uid, cr, ZFS_OWNER);
    uid_t uid = crgetuid(cr);
    if (uid == owner || uid == target ||
        zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr, NULL) == 0)
        return (0);
    return (oz_remove_policy(cr));
}
#define OZ_AROS_ACL_CORE 1
#include "../../module/os/linux/zfs/zfs_acl.c"

/* Explicit ACE decision only, not a substitute for dataset/immutable/replay
 * checks, authenticated caller identity or privilege policy in zfs_zaccess. */
int
oz_acl_evaluate(znode_t *zp, uint32_t requested, cred_t *cr,
    uint32_t *remaining)
{
    if (!zp || !remaining || (requested & ~ACE_ALL_PERMS)) return (EINVAL);
    *remaining = requested;
    if (!cr) return (EACCES);
    int error = zfs_zaccess_aces_check(zp, remaining, B_FALSE, cr, NULL);
    return (error == -1 ? EACCES : error);
}

/* Dataset/file restrictions followed by full ordered ACE evaluation. This
 * intentionally exposes no skip-ACL/replay/privilege-override argument; it
 * remains a component of, not a replacement for, complete zfs_zaccess. */
int
oz_acl_check_append(znode_t *zp, uint32_t requested, boolean_t append,
    cred_t *cr, uint32_t *remaining)
{
    if (!zp || !ZTOZSB(zp) || !remaining || (requested & ~ACE_ALL_PERMS))
        return (EINVAL);
    *remaining = requested;
    if (!cr) return (EACCES);
    zfsvfs_t *fs = ZTOZSB(zp);
    if (fs->z_unmounted) return (EIO);
    if (fs->z_replay) return (ENOTSUP);
    boolean_t check_privs;
    int error = zfs_zaccess_common(zp, requested, remaining, &check_privs,
        B_FALSE, cr, NULL);
    if (error && check_privs && append)
        error = zfs_zaccess_append(zp, remaining, &check_privs, cr, NULL);
    return (error == -1 ? EACCES : error);
}

int
oz_acl_check(znode_t *zp, uint32_t requested, cred_t *cr, uint32_t *remaining)
{
    return (oz_acl_check_append(zp, requested, B_FALSE, cr, remaining));
}
