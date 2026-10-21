/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_sa.h>
#include "xattr.h"
#include <sys/zap.h>
#include <pthread.h>
#include <proto/exec.h>

struct xattr_worker { znode_t *base, *dir; int error; };
static void *create_attribute(void *arg)
{
    struct xattr_worker *w = arg;
    w->error = oz_xattr_set(w->base, "user.race", "race", 4, OZ_XATTR_CREATE, kcred);
    return NULL;
}
static void *get_dir(void *arg)
{
    struct xattr_worker *w = arg;
    w->error = zfs_get_xattrdir(w->base, &w->dir, kcred, CREATE_XATTR_DIR);
    return (NULL);
}

int oz_xattr_dir_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry;
    znode_t *base = NULL, *dir = NULL, *file = NULL;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zap_lookup(fs->z_os, ZFS_DIRENT_OBJ(entry), "created-file", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &base);
    if (!error && write) {
        if (zfs_get_xattrdir(base, &dir, kcred, 0) != ENOENT || dir) error = EIO;
        zilog_t *saved = fs->z_log; fs->z_log = NULL;
        if (!error && (zfs_get_xattrdir(base, &dir, kcred, CREATE_XATTR_DIR) != ENOTSUP || dir)) error = EIO;
        fs->z_log = saved;
    }
    if (!error && (zfs_get_xattrdir(base, &dir, NULL, 0) != EACCES || dir)) error = EIO;
    if (!error && write) {
        struct xattr_worker workers[2] = {{.base = base}, {.base = base}};
        pthread_t threads[2]; unsigned started = 0;
        for (; started < 2; started++) {
            error = pthread_create(&threads[started], NULL, get_dir, &workers[started]);
            if (error) break;
        }
        for (unsigned i = 0; i < started; i++) {
            pthread_join(threads[i], NULL);
            if (!error) error = workers[i].error;
        }
        if (!error && (!workers[0].dir || workers[0].dir != workers[1].dir)) error = EIO;
        dir = workers[0].dir;
        if (workers[1].dir) oz_znode_rele(workers[1].dir);
    } else if (!error) error = zfs_get_xattrdir(base, &dir, kcred, 0);
    if (!error && (!dir || !S_ISDIR(dir->z_mode) || !(dir->z_pflags & ZFS_XATTR) ||
        dir->z_xattr_parent != base->z_id || dir->z_uid != base->z_uid ||
        dir->z_gid != base->z_gid)) error = EIO;
    if (!error) {
        znode_t *again = NULL;
        error = zfs_get_xattrdir(base, &again, kcred, CREATE_XATTR_DIR);
        if (!error && again != dir) error = EIO;
        if (again) oz_znode_rele(again);
        if (!error && (zfs_get_xattrdir(dir, &again, kcred, CREATE_XATTR_DIR) != EINVAL || again)) error = EIO;
    }
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFREG | 0640};
    unsigned char expected[64], actual[64];
    for (unsigned i = 0; i < sizeof (expected); i++) expected[i] = 0xc3 ^ i;
    if (!error && write) {
        cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
        if (!reader) error = ENOMEM;
        else {
            if (zfs_zaccess(dir, ACE_ADD_FILE, 0, B_FALSE, reader, NULL) != EACCES) error = EIO;
            znode_t *denied = NULL;
            if (!error && (zfs_create(dir, "denied", &va, 1, S_IWUSR,
                &denied, reader, 0, NULL, NULL) != EACCES || denied)) error = EIO;
            if (denied) oz_znode_rele(denied);
            oz_cred_release(reader);
        }
        if (!error) error = zfs_create(dir, "user.demo", &va, 1,
            S_IRUSR | S_IWUSR, &file, kcred, 0, NULL, NULL);
        size_t residual = sizeof (expected);
        if (!error) error = zfs_write_simple(file, expected, sizeof (expected), 0, &residual);
        if (!error && residual) error = EIO;
        if (!error) error = zfs_fsync(file, 0, kcred);
        if (!error) error = zil_commit(fs->z_log, 0);
    } else if (!error) {
        error = zap_lookup(fs->z_os, dir->z_id, "user.demo", 8, 1, &entry);
        if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &file);
    }
    if (!error && (file->z_size != sizeof (expected) || !(file->z_pflags & ZFS_XATTR) ||
        !S_ISREG(file->z_mode) || file->z_xattr_parent != dir->z_id)) error = EIO;
    if (!error && zap_contains(fs->z_os, dir->z_id, "denied") != ENOENT) error = EIO;
    if (!error) {
        struct iovec vec = {actual, sizeof (actual)}; zfs_uio_t uio;
        zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, sizeof (actual), 0);
        error = zfs_read(file, &uio, 0, kcred);
        if (!error && (uio.uio_resid || memcmp(expected, actual, sizeof (actual)))) error = EIO;
    }
    size_t length = 0;
    if (!error) error = oz_xattr_get(base, "user.demo", NULL, 0, &length, kcred);
    if (!error && length != sizeof (expected)) error = EIO;
    memset(actual, 0, sizeof (actual));
    if (!error) error = oz_xattr_get(base, "user.demo", actual, sizeof (actual), &length, kcred);
    if (!error && (length != sizeof (expected) || memcmp(actual, expected, length))) error = EIO;
    if (!error && (oz_xattr_get(base, "user.missing", actual, sizeof (actual),
        &length, kcred) != ENODATA || length)) error = EIO;
    actual[0] = 0x5a;
    if (!error && (oz_xattr_get(base, "user.demo", actual, 1, &length, kcred) != ERANGE ||
        length != sizeof (expected) || actual[0] != 0x5a)) error = EIO;
    if (!error && oz_xattr_get(base, "user.demo", actual, sizeof (actual), &length, NULL) != EACCES)
        error = EIO;
    cred_t *denied = oz_cred_create(77,77,88,0,NULL);
    if (!denied) error = ENOMEM;
    if (!error && oz_xattr_get(base, "user.demo", actual, sizeof (actual), &length, denied) != EACCES)
        error = EIO;
    oz_cred_release(denied);
    /* Populate the real upstream SA nvlist representation, not a test-side
     * alternate format. The getter must also read this after a cold import. */
    const unsigned char inline_value[] = {0,0x41,0xff,0x52,0};
    if (!error && write) {
        rw_enter(&base->z_xattr_lock, RW_WRITER);
        error = nvlist_add_byte_array(base->z_xattr_cached, "user.inline",
            inline_value, sizeof (inline_value));
        if (!error) error = zfs_sa_set_xattr(base, "user.inline", inline_value, sizeof (inline_value));
        rw_exit(&base->z_xattr_lock);
    }
    if (!error) error = oz_xattr_get(base, "user.inline", actual, sizeof (actual), &length, kcred);
    if (!error && (length != sizeof (inline_value) || memcmp(actual, inline_value, length))) error = EIO;
    unsigned char *large = malloc(40000), *copy = malloc(40000);
    if (!large || !copy) error = ENOMEM;
    if (large) for (unsigned i = 0; i < 40000; i++) large[i] = i ^ (i >> 8);
    if (!error && write) {
        boolean_t old_sa = fs->z_xattr_sa;
        fs->z_xattr_sa = B_TRUE;
        if (oz_xattr_set(base, "user.mutable", "x", 1,
            OZ_XATTR_CREATE | OZ_XATTR_REPLACE, kcred) != EINVAL) error = EIO;
        zilog_t *saved_log = fs->z_log; fs->z_log = NULL;
        if (!error && oz_xattr_set(base, "user.mutable", "x", 1, 0, kcred) != ENOTSUP) error = EIO;
        fs->z_log = saved_log;
        uint64_t saved_flags = base->z_pflags;
        base->z_pflags |= ZFS_IMMUTABLE;
        if (!error && oz_xattr_set(base, "user.mutable", "x", 1, 0, kcred) != EPERM) error = EIO;
        base->z_pflags = saved_flags;
        cred_t *outsider = oz_cred_create(77,77,88,0,NULL);
        if (!outsider) error = ENOMEM;
        if (!error && oz_xattr_set(base, "user.mutable", "x", 1, 0, outsider) != EACCES) error = EIO;
        oz_cred_release(outsider);
        if (oz_xattr_set(base, "user.mutable", inline_value, sizeof (inline_value),
            OZ_XATTR_REPLACE, kcred) != ENODATA) error = EIO;
        if (!error) error = oz_xattr_set(base, "user.mutable", inline_value,
            sizeof (inline_value), OZ_XATTR_CREATE, kcred);
        if (!error && oz_xattr_set(base, "user.mutable", large, 40000,
            OZ_XATTR_CREATE, kcred) != EEXIST) error = EIO;
        if (!error) error = oz_xattr_set(base, "user.mutable", large, 40000,
            OZ_XATTR_REPLACE, kcred);
        if (!error) error = oz_xattr_get(base, "user.mutable", copy, 40000, &length, kcred);
        if (!error && (length != 40000 || memcmp(large, copy, 40000))) error = EIO;
        if (!error) error = oz_xattr_set(base, "user.mutable", inline_value,
            sizeof (inline_value), OZ_XATTR_REPLACE, kcred);
        if (!error) error = oz_xattr_set(base, "user.large", large, 40000, OZ_XATTR_CREATE, kcred);
        if (!error) error = oz_xattr_set(base, "user.empty", "", 0, OZ_XATTR_CREATE, kcred);
        if (!error) error = oz_xattr_set(base, "user.gone", "x", 1, OZ_XATTR_CREATE, kcred);
        if (!error) error = oz_xattr_set(base, "user.gone", NULL, 0, OZ_XATTR_REPLACE, kcred);
        if (!error && oz_xattr_set(base, "user.gone", NULL, 0, OZ_XATTR_REPLACE, kcred) != ENODATA)
            error = EIO;
        if (!error) error = oz_xattr_set(base, "user.gone", NULL, 0, 0, kcred);
        if (!error && oz_xattr_set(base, "user.empty", "x", 1, 0, NULL) != EACCES) error = EIO;
        if (!error) {
            struct xattr_worker racers[2] = {{.base=base},{.base=base}};
            pthread_t threads[2]; unsigned started=0;
            for (; started<2; started++) {
                error=pthread_create(&threads[started],NULL,create_attribute,&racers[started]);
                if(error)break;
            }
            for(unsigned i=0;i<started;i++)pthread_join(threads[i],NULL);
            if (!error && !((racers[0].error==0 && racers[1].error==EEXIST) ||
                (racers[1].error==0 && racers[0].error==EEXIST)))error=EIO;
            if (!error)error=oz_xattr_set(base,"user.race",NULL,0,OZ_XATTR_REPLACE,kcred);
        }
        fs->z_xattr_sa = old_sa;
        if (!error) error = zil_commit(fs->z_log, 0);
    } else if (!error && oz_xattr_set(base, "user.mutable", "x", 1, 0, kcred) != EROFS)
        error = EIO;
    if (!error) error = oz_xattr_get(base, "user.mutable", actual, sizeof (actual), &length, kcred);
    if (!error && (length != sizeof (inline_value) || memcmp(actual, inline_value, length))) error = EIO;
    if (!error) error = oz_xattr_get(base, "user.large", copy, 40000, &length, kcred);
    if (!error && (length != 40000 || memcmp(large, copy, 40000))) error = EIO;
    if (!error) error = oz_xattr_get(base, "user.empty", actual, sizeof (actual), &length, kcred);
    if (!error && length) error = EIO;
    if (!error && oz_xattr_get(base, "user.gone", NULL, 0, &length, kcred) != ENODATA) error = EIO;
    if (!error && oz_xattr_get(base, "user.race", NULL, 0, &length, kcred) != ENODATA) error = EIO;
    if (!error && (zap_contains(fs->z_os,dir->z_id,"user.mutable") != ENOENT ||
        zap_contains(fs->z_os,dir->z_id,"user.large") != 0))error=EIO;
    if (!error) {
        uchar_t *stored; uint_t stored_size;
        rw_enter(&base->z_xattr_lock,RW_READER);
        if (nvlist_lookup_byte_array(base->z_xattr_cached,"user.large",&stored,&stored_size) != ENOENT ||
            nvlist_lookup_byte_array(base->z_xattr_cached,"user.mutable",&stored,&stored_size) ||
            stored_size != sizeof(inline_value))error=EIO;
        rw_exit(&base->z_xattr_lock);
    }
    free(copy); free(large);
    if (file) oz_znode_rele(file);
    if (dir) oz_znode_rele(dir);
    if (base) oz_znode_rele(base);
    char text[100]; snprintf(text, sizeof (text),
        "Native upstream xattr directory/file write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
