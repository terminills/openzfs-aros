/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <pthread.h>
#include <proto/exec.h>

struct unlink_worker { znode_t *parent; int result; };
static void *remove_same_name(void *arg)
{
    struct unlink_worker *w = arg;
    w->result = zfs_remove(w->parent, "removed-race", kcred, 0);
    return (NULL);
}

int
oz_remove_test(zfsvfs_t *fs, boolean_t write)
{
    znode_t *parent = NULL, *file = NULL;
    uint64_t entry, count, object = 0;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &parent);
    if (!error && !write) {
        if (zfs_remove(parent, "removed-file", kcred, 0) != EROFS) error = EIO;
    }
    unsigned char *bytes = kmem_alloc(8193, KM_SLEEP);
    unsigned char *actual = kmem_alloc(8193, KM_SLEEP);
    for (unsigned i = 0; i < 8193; i++) bytes[i] = (i * 31) ^ (i >> 7);
    if (!error && write) {
        if (zfs_remove(parent, "created-directory", kcred, 0) != EPERM ||
            zfs_remove(parent, "missing-remove", kcred, 0) != ENOENT ||
            zfs_remove(parent, "created-file", NULL, 0) != EACCES ||
            zfs_remove(parent, "created-file", kcred, 0x80000000) != ENOTSUP)
            error = EIO;
        vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFREG | 0600};
        if (!error) error = zfs_create(parent, "removed-file", &va, 1,
            S_IRUSR | S_IWUSR, &file, kcred, 0, NULL, NULL);
        if (!error) {
            object = file->z_id;
            size_t residual = 8192;
            error = zfs_write_simple(file, bytes, 8192, 0, &residual);
            if (!error && residual) error = EIO;
            if (!error) error = zfs_fsync(file, 0, kcred);
        }
        if (!error) {
            file->z_pflags |= ZFS_NOUNLINK;
            if (zfs_remove(parent, "removed-file", kcred, 0) != EPERM) error = EIO;
            file->z_pflags &= ~ZFS_NOUNLINK;
            zilog_t *saved = fs->z_log; fs->z_log = NULL;
            if (!error && zfs_remove(parent, "removed-file", kcred, 0) != ENOTSUP)
                error = EIO;
            fs->z_log = saved;
        }
        if (!error) error = zfs_remove(parent, "removed-file", kcred, 0);
        if (!error && (!file->z_unlinked || file->z_links ||
            zfs_remove(parent, "removed-file", kcred, 0) != ENOENT ||
            zfs_link(parent, file, "must-not-resurrect", kcred, 0) != ENOENT ||
            oz_unlinked_drain_exclusive(fs) != EBUSY)) error = EIO;
        /* Existing references still support I/O after the last name is gone. */
        if (!error) {
            size_t residual = 1;
            error = zfs_write_simple(file, bytes + 8192, 1, 8192, &residual);
            if (!error && residual) error = EIO;
            if (!error) error = zfs_fsync(file, 0, kcred);
        }
        if (!error) {
            struct iovec vec = {actual, 8193}; zfs_uio_t uio;
            zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, 8193, 0);
            error = zfs_read(file, &uio, 0, kcred);
            if (!error && (uio.uio_resid || memcmp(actual, bytes, 8193))) error = EIO;
        }
        if (file) { oz_znode_rele(file); file = NULL; }
        /* Match the DOS bridge: reading before deletion can leave atime
         * dirty, and sync precedes reclamation of the unlinked object. */
        if (!error) error = oz_znode_cache_sync(fs, B_TRUE);
        if (!error) error = oz_unlinked_drain_exclusive(fs);
        if (!error) {
            dmu_object_info_t doi;
            if (dmu_object_info(fs->z_os, object, &doi) != ENOENT) error = EIO;
        }
        if (!error) {
            va.va_mode = S_IFLNK | 0777;
            error = zfs_symlink(parent, "removed-link", &va, "created-file",
                &file, kcred, 0, NULL);
            if (file) { oz_znode_rele(file); file = NULL; }
            if (!error) error = zfs_remove(parent, "removed-link", kcred, 0);
            if (!error) error = oz_unlinked_drain_exclusive(fs);
        }
        if (!error) {
            va.va_mode = S_IFREG | 0600;
            error = zfs_create(parent, "removed-race", &va, 1,
                S_IWUSR, &file, kcred, 0, NULL, NULL);
            if (!error) {
                struct unlink_worker workers[2] = {{.parent = parent}, {.parent = parent}};
                pthread_t threads[2]; unsigned started = 0;
                for (; started < 2; started++) {
                    error = pthread_create(&threads[started], NULL, remove_same_name, &workers[started]);
                    if (error) break;
                }
                for (unsigned i = 0; i < started; i++) pthread_join(threads[i], NULL);
                if (!error && !((workers[0].result == 0 && workers[1].result == ENOENT) ||
                    (workers[1].result == 0 && workers[0].result == ENOENT))) error = EIO;
                if (!error && (!file->z_unlinked || file->z_links)) error = EIO;
            }
            if (file) { oz_znode_rele(file); file = NULL; }
            if (!error) error = oz_unlinked_drain_exclusive(fs);
        }
        if (!error) error = zil_commit(fs->z_log, 0);
    }
    const char *names[] = {"removed-file", "removed-link", "must-not-resurrect", "removed-race"};
    for (unsigned i = 0; i < 4 && !error; i++)
        if (zap_contains(fs->z_os, parent->z_id, names[i]) != ENOENT) error = EIO;
    if (!error) error = zap_count(fs->z_os, fs->z_unlinkedobj, &count);
    if (!error && count) error = EIO;
    if (file) oz_znode_rele(file);
    if (parent) oz_znode_rele(parent);
    kmem_free(actual, 8193); kmem_free(bytes, 8193);
    char text[100]; snprintf(text, sizeof (text),
        "Native upstream unlink/open-file/symlink write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
