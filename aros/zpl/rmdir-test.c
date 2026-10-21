/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <proto/exec.h>
#include "xattr.h"

int
oz_rmdir_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry, count;
    znode_t *parent = NULL, *dir = NULL, *xdir = NULL, *attr = NULL;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &parent);
    if (!error && !write) {
        if (zap_contains(fs->z_os, parent->z_id, "remove-me") != ENOENT ||
            zfs_rmdir(parent, "remove-me", NULL, kcred, 0) != EROFS ||
            oz_unlinked_drain_exclusive(fs) != EROFS) error = EIO;
    }
    uint64_t ids[3] = {0};
    unsigned char bytes[64], actual[64];
    memset(bytes, 0x5c, sizeof (bytes));
    if (!error && write) {
        if (zfs_rmdir(parent, "created-directory", NULL, NULL, 0) != EACCES ||
            zfs_rmdir(parent, "created-directory", NULL, kcred, 0) != ENOTEMPTY ||
            zfs_rmdir(parent, "created-file", NULL, kcred, 0) != ENOTDIR)
            error = EIO;
        zilog_t *saved = fs->z_log; fs->z_log = NULL;
        if (!error && zfs_rmdir(parent, "created-directory", NULL, kcred, 0) != ENOTSUP)
            error = EIO;
        fs->z_log = saved;
        vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFDIR | 0750};
        if (!error) error = zfs_mkdir(parent, "remove-me", &va, &dir, kcred, 0, NULL, NULL);
        if (!error) {
            ids[0] = dir->z_id;
            if (zfs_rmdir(parent, "remove-me", dir, kcred, 0) != EINVAL) error = EIO;
            dir->z_pflags |= ZFS_IMMUTABLE;
            if (!error && zfs_rmdir(parent, "remove-me", NULL, kcred, 0) != EPERM) error = EIO;
            dir->z_pflags &= ~ZFS_IMMUTABLE;
        }
        if (!error) {
            ace_t aces[] = {
                {77, ACE_DELETE, 0, ACE_ACCESS_DENIED_ACE_TYPE},
                {0, ACE_ALL_PERMS, ACE_OWNER, ACE_ACCESS_ALLOWED_ACE_TYPE}
            };
            vsecattr_t acl = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS,
                .vsa_aclcnt = 2, .vsa_aclentp = aces, .vsa_aclentsz = sizeof (aces)};
            error = zfs_setacl(dir, &acl, B_FALSE, kcred);
            cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
            if (!reader) error = ENOMEM;
            else {
                /* A target DENY must win even if the parent grants deletion. */
                if (!error && zfs_rmdir(parent, "remove-me", NULL, reader, 0) != EPERM)
                    error = EIO;
                mode_t saved_mode = parent->z_mode;
                parent->z_mode |= S_ISVTX;
                if (!error && (zfs_sticky_remove_access(parent, dir, reader) != EPERM ||
                    zfs_sticky_remove_access(parent, dir, kcred) != 0 ||
                    zfs_sticky_remove_access(parent, dir, NULL) != EACCES)) error = EIO;
                parent->z_mode = saved_mode;
                oz_cred_release(reader);
            }
            if (!error && zap_contains(fs->z_os, parent->z_id, "remove-me")) error = EIO;
        }
        if (!error) error = zfs_get_xattrdir(dir, &xdir, kcred, CREATE_XATTR_DIR);
        va.va_mode = S_IFREG | 0600;
        if (!error) {
            ids[1] = xdir->z_id;
            error = zfs_create(xdir, "user.discard", &va, 1, S_IWUSR, &attr,
                kcred, 0, NULL, NULL);
        }
        if (!error) {
            ids[2] = attr->z_id;
            size_t residual = sizeof (bytes);
            error = zfs_write_simple(attr, bytes, sizeof (bytes), 0, &residual);
            if (!error && residual) error = EIO;
            if (!error) error = zfs_fsync(attr, 0, kcred);
        }
        if (xdir) { oz_znode_rele(xdir); xdir = NULL; }
        if (!error) {
            size_t missing = 123;
            if (oz_xattr_get(dir, "user.absent", NULL, 0, &missing, kcred) != ENODATA ||
                missing || !dir->z_xattr_cached) error = EIO;
        }
        if (!error) error = zfs_rmdir(parent, "remove-me", NULL, kcred, 0);
        if (!error && (!dir->z_unlinked || dir->z_links ||
            zap_contains(fs->z_os, parent->z_id, "remove-me") != ENOENT ||
            oz_unlinked_drain_exclusive(fs) != EBUSY)) error = EIO;
        if (dir) { oz_znode_rele(dir); dir = NULL; }
        /* Parent and xattr directory can now retire, but the open attribute
         * file must stay readable until its last reference is released. */
        if (!error && oz_unlinked_drain_exclusive(fs) != EBUSY) error = EIO;
        if (!error) {
            struct iovec vec = {actual, sizeof (actual)}; zfs_uio_t uio;
            zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, sizeof (actual), 0);
            error = zfs_read(attr, &uio, 0, kcred);
            if (!error && (uio.uio_resid || memcmp(actual, bytes, sizeof (bytes)))) error = EIO;
        }
        if (attr) { oz_znode_rele(attr); attr = NULL; }
        if (!error) error = oz_unlinked_drain_exclusive(fs);
        if (!error) error = zil_commit(fs->z_log, 0);
        for (unsigned i = 0; i < 3 && !error; i++) {
            dmu_object_info_t doi;
            if (dmu_object_info(fs->z_os, ids[i], &doi) != ENOENT) error = EIO;
        }
    }
    if (!error) error = zap_count(fs->z_os, fs->z_unlinkedobj, &count);
    if (!error && count) error = EIO;
    if (attr) oz_znode_rele(attr);
    if (xdir) oz_znode_rele(xdir);
    if (dir) oz_znode_rele(dir);
    if (parent) oz_znode_rele(parent);
    char text[100]; snprintf(text, sizeof (text),
        "Native upstream rmdir/unlinked/xattr cleanup write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
