/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zap.h>
#include <proto/exec.h>

int oz_symlink_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry;
    znode_t *parent = NULL;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zap_lookup(fs->z_os, ZFS_DIRENT_OBJ(entry),
        "created-directory", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &parent);
    char *target = malloc(MAXPATHLEN + 2), *actual = malloc(MAXPATHLEN + 8);
    if (!target || !actual) error = ENOMEM;
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFLNK | 0777};
    if (!error) {
        znode_t *bad = NULL;
        if (zfs_symlink(parent, "reject", &va, "hello", &bad, NULL, 0, NULL) != EACCES || bad) error = EIO;
        if (zfs_symlink(parent, "reject", &va, NULL, &bad, kcred, 0, NULL) != EINVAL || bad) error = EIO;
        if (write) {
            cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
            if (!reader) error = ENOMEM;
            else {
                if (zfs_symlink(parent, "reject", &va, "hello", &bad, reader, 0, NULL) != EACCES || bad) error = EIO;
                if (bad) { oz_znode_rele(bad); bad = NULL; }
                oz_cred_release(reader);
            }
            uint64_t saved_flags = parent->z_pflags;
            parent->z_pflags |= ZFS_XATTR;
            if (zfs_symlink(parent, "reject", &va, "hello", &bad, kcred, 0, NULL) != EINVAL || bad) error = EIO;
            parent->z_pflags = saved_flags;
            memset(target, 'x', MAXPATHLEN + 1); target[MAXPATHLEN + 1] = 0;
            if (zfs_symlink(parent, "reject", &va, target, &bad, kcred, 0, NULL) != ENAMETOOLONG || bad) error = EIO;
            zilog_t *log = fs->z_log; fs->z_log = NULL;
            if (zfs_symlink(parent, "reject", &va, "hello", &bad, kcred, 0, NULL) != ENOTSUP || bad) error = EIO;
            fs->z_log = log;
        } else if (zfs_symlink(parent, "reject", &va, "hello", &bad, kcred, 0, NULL) != EROFS || bad) error = EIO;
        if (zap_contains(fs->z_os, parent->z_id, "reject") != ENOENT) error = EIO;
    }
    char *names[] = {"short-link", "long-link"};
    for (unsigned i = 0; i < 2 && !error; i++) {
        size_t len = i ? MAXPATHLEN : 19;
        for (size_t j = 0; j < len; j++) target[j] = 'a' + j % 26;
        target[len] = 0;
        znode_t *node = NULL;
        if (write) error = zfs_symlink(parent, names[i], &va, target, &node, kcred, 0, NULL);
        else {
            error = zap_lookup(fs->z_os, parent->z_id, names[i], 8, 1, &entry);
            if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &node);
        }
        if (!error && (!node || !S_ISLNK(node->z_mode) || node->z_size != len || node->z_links != 1)) error = EIO;
        struct iovec vec = {actual, len + 8}; zfs_uio_t uio;
        zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, len + 8, 0);
        memset(actual, 0x5a, len + 8);
        if (!error && (zfs_readlink(parent, &uio, kcred) != EINVAL || uio.uio_resid != len + 8)) error = EIO;
        if (!error && (zfs_readlink(node, &uio, NULL) != EACCES || uio.uio_resid != len + 8)) error = EIO;
        if (!error) error = zfs_readlink(node, &uio, kcred);
        if (!error && (uio.uio_resid != 8 || memcmp(target, actual, len) || actual[len] != 0x5a)) error = EIO;
        vec = (struct iovec){actual, 7};
        zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, 7, 0);
        if (!error) error = zfs_readlink(node, &uio, kcred);
        if (!error && (uio.uio_resid || memcmp(target, actual, 7))) error = EIO;
        if (!error && write) {
            znode_t *duplicate = NULL;
            if (zfs_symlink(parent, names[i], &va, target, &duplicate, kcred, 0, NULL) != EEXIST || duplicate) error = EIO;
            if (duplicate) oz_znode_rele(duplicate);
        }
        if (node) oz_znode_rele(node);
    }
    if (!error && write) error = zil_commit(fs->z_log, 0);
    if (parent) oz_znode_rele(parent);
    free(target); free(actual);
    char text[100]; snprintf(text, sizeof (text),
        "Native upstream symlink/readlink write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
