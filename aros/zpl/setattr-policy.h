/* SPDX-License-Identifier: CDDL-1.0 */
/* Native VFS admission for the upstream setattr transaction path. Explicit
 * POSIX credentials only; native security identities are resolved upstream
 * of this boundary. Validate all requested rights before changing file size.
 */
static int
native_setattr_admit(znode_t *zp, vattr_t *va, cred_t *cr)
{
    if (!cr) return (EACCES);
    uint_t mask = va->va_mask;
    uid_t uid = crgetuid(cr);
    uid_t owner = zfs_fuid_map_id(ZTOZSB(zp), zp->z_uid, cr, ZFS_OWNER);
    gid_t group = zfs_fuid_map_id(ZTOZSB(zp), zp->z_gid, cr, ZFS_GROUP);
    boolean_t root = uid == 0;
    if ((mask & ATTR_XVATTR) && uid != owner && !root) return (EACCES);
    if (mask & ATTR_SIZE) {
        if (zp->z_pflags & (ZFS_IMMUTABLE | ZFS_APPENDONLY)) return (EPERM);
        int error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr, NULL);
        if (error) return (error);
    }
    if ((mask & ATTR_MODE) && uid != owner && !root &&
        zfs_zaccess(zp, ACE_WRITE_ACL, 0, B_FALSE, cr, NULL)) return (EPERM);
    if ((mask & (ATTR_ATIME | ATTR_MTIME)) && uid != owner && !root &&
        zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0, B_FALSE, cr, NULL)) return (EPERM);
    if ((mask & ATTR_UID) && va->va_uid != owner && !root) {
        if (va->va_uid != uid ||
            zfs_zaccess(zp, ACE_WRITE_OWNER, 0, B_FALSE, cr, NULL)) return (EPERM);
    }
    if ((mask & ATTR_UID) && va->va_uid == owner && uid != owner && !root &&
        zfs_zaccess(zp, ACE_WRITE_OWNER, 0, B_FALSE, cr, NULL)) return (EPERM);
    if ((mask & ATTR_GID) && !root) {
        if (va->va_gid != group && !zfs_groupmember(ZTOZSB(zp), va->va_gid, cr))
            return (EPERM);
        if (uid != owner &&
            zfs_zaccess(zp, ACE_WRITE_OWNER, 0, B_FALSE, cr, NULL)) return (EPERM);
    }
    return (0);
}
static void
native_setid_clear(vattr_t *va, cred_t *cr)
{
    if (crgetuid(cr) && (va->va_mode & (S_ISUID | S_ISGID))) {
        va->va_mask |= ATTR_MODE;
        va->va_mode &= ~(S_ISUID | S_ISGID);
    }
}
static int
native_setid_admit(znode_t *zp, vattr_t *va, const vattr_t *old, cred_t *cr)
{
    if (!crgetuid(cr)) return (0);
    if ((va->va_mode & S_ISUID) && crgetuid(cr) != old->va_uid) return (EPERM);
    gid_t group = (va->va_mask & ATTR_GID) ? va->va_gid : old->va_gid;
    if ((va->va_mode & S_ISGID) && !zfs_groupmember(ZTOZSB(zp), group, cr))
        va->va_mode &= ~S_ISGID;
    if (!S_ISDIR(zp->z_mode)) va->va_mode &= ~S_ISVTX;
    return (0);
}
