/* SPDX-License-Identifier: CDDL-1.0 */
/* Native adaptation of upstream zpl_xattr_get_sa/get_dir selection.
 * Copyright (c) 2011, Lawrence Livermore National Security, LLC.
 * Upstream SA nvlist and directory-backed attribute formats are unchanged.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_sa.h>
#include <sys/dmu_tx.h>
#include "xattr.h"

int oz_xattr_get(znode_t *zp, const char *name, void *value, size_t capacity,
    size_t *length, cred_t *cr)
{
    if (!length) return EINVAL;
    *length = 0;
    if (!zp || !name || !*name || strchr(name, '/') ||
        (!value && capacity)) return EINVAL;
    if (strlen(name) >= MAXNAMELEN) return ENAMETOOLONG;
    if (!cr) return EACCES;
    if (zp->z_pflags & ZFS_XATTR) return EINVAL;
    zfsvfs_t *fs = ZTOZSB(zp);
    int error = zfs_enter_verify_zp(fs, zp, FTAG);
    if (error) return error;
    error = zfs_zaccess(zp, ACE_READ_NAMED_ATTRS, 0, B_FALSE, cr, NULL);
    if (error) { zfs_exit(fs, FTAG); return error; }
    rw_enter(&zp->z_xattr_lock, RW_READER);
    error = ENOENT;
    if (fs->z_use_sa && zp->z_is_sa) {
        mutex_enter(&zp->z_lock);
        error = zp->z_xattr_cached ? 0 : zfs_sa_get_xattr(zp);
        mutex_exit(&zp->z_lock);
        if (!error) {
            uchar_t *bytes; uint_t size;
            error = nvlist_lookup_byte_array(zp->z_xattr_cached, name, &bytes, &size);
            if (!error) {
                *length = size;
                if (value && capacity < size) error = ERANGE;
                else if (value) memcpy(value, bytes, size);
            }
        }
        if (error != ENOENT) goto out;
    }
    znode_t *dir = NULL, *file = NULL;
    zfs_dirlock_t *lock = NULL;
    error = zfs_get_xattrdir(zp, &dir, cr, 0);
    if (!error) error = zfs_dirent_lock(&lock, dir, (char *)name, &file,
        ZEXISTS | ZSHARED, NULL, NULL);
    if (!error) {
        if (!S_ISREG(file->z_mode)) error = EIO;
        else error = zfs_zaccess(file, ACE_READ_DATA, 0, B_FALSE, cr, NULL);
    }
    if (!error) {
        uint64_t size = file->z_size;
        if (size > SIZE_MAX) error = EOVERFLOW;
        else {
            *length = size;
            if (value && capacity < size) error = ERANGE;
            else if (value && size) {
                struct iovec vec = {value, size}; zfs_uio_t uio;
                zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, size, 0);
                error = zfs_read(file, &uio, 0, cr);
                if (!error && uio.uio_resid) error = EIO;
            }
        }
    }
    if (lock) zfs_dirent_unlock(lock);
    if (file) oz_znode_rele(file);
    if (dir) oz_znode_rele(dir);
out:
    rw_exit(&zp->z_xattr_lock);
    zfs_exit(fs, FTAG);
    return error == ENOENT ? ENODATA : error;
}

/* Upstream cached SA mutation: discard an inconsistent cache on failure so
 * the next lookup reconstructs the authoritative nvlist from the SA. */
static int set_sa(znode_t *zp, const char *name, const void *value, size_t size)
{
    int error = 0;
    mutex_enter(&zp->z_lock);
    if (!zp->z_xattr_cached) error = zfs_sa_get_xattr(zp);
    mutex_exit(&zp->z_lock);
    if (error) return error;
    if (value) {
        if (size > DXATTR_MAX_ENTRY_SIZE) return EFBIG;
        size_t packed;
        error = nvlist_size(zp->z_xattr_cached, &packed, NV_ENCODE_XDR);
        if (error) return error;
        if (packed > DXATTR_MAX_SA_SIZE) return EFBIG;
        error = nvlist_add_byte_array(zp->z_xattr_cached, name, value, size);
    } else error = nvlist_remove(zp->z_xattr_cached, name, DATA_TYPE_BYTE_ARRAY);
    if (!error) error = zfs_sa_set_xattr(zp, name, value, size);
    if (error) {
        nvlist_free(zp->z_xattr_cached);
        zp->z_xattr_cached = NULL;
    }
    return error;
}

static int touch_xattr(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); return error; }
    uint64_t ctime[2];
    zfs_tstamp_update_setup(zp, STATE_CHANGED, NULL, ctime);
    error = sa_update(zp->z_sa_hdl, SA_ZPL_CTIME(fs), ctime, sizeof (ctime), tx);
    dmu_tx_commit(tx);
    return error;
}

static int set_dir(znode_t *base, const char *name, const void *value,
    size_t size, cred_t *cr)
{
    znode_t *dir = NULL, *file = NULL;
    int error = zfs_get_xattrdir(base, &dir, cr, value ? CREATE_XATTR_DIR : 0);
    if (error) return error == ENOENT && !value ? 0 : error;
    if (!value) {
        error = zfs_remove(dir, (char *)name, cr, 0);
        if (error == ENOENT) error = 0;
    } else {
        /* Follow zpl_xattr_set_dir: the base object's WRITE_NAMED_ATTRS
         * admission is authoritative, not the hidden file creator's UID.
         * Holding base->z_xattr_lock excludes competing attribute changes. */
        zfs_dirlock_t *lock = NULL;
        error = zfs_dirent_lock(&lock,dir,(char *)name,&file,
            ZEXISTS | ZSHARED,NULL,NULL);
        if (lock) zfs_dirent_unlock(lock);
        if (error == ENOENT) {
            vattr_t va = {.va_mask = ATTR_MODE,.va_mode = S_IFREG | 0644};
            /* Native zaccess maps this xattr directory back to the base's
             * WRITE_NAMED_ATTRS. Keep that check; no ACL bypass is needed. */
            error = zfs_create(dir,(char *)name,&va,0,0,&file,
                cr,0,NULL,NULL);
        }
        if (!error && !S_ISREG(file->z_mode)) error = EIO;
        if (!error) error = zfs_freesp(file,0,0,0,B_TRUE);
        if (!error) error = zfs_write_simple(file,value,size,0,NULL);
    }
    if (!error) error = touch_xattr(base);
    if (file) oz_znode_rele(file);
    oz_znode_rele(dir);
    return error;
}

int oz_xattr_set(znode_t *zp, const char *name, const void *value,
    size_t size, int flags, cred_t *cr)
{
    if (!zp || !name || !*name || strchr(name, '/') || (!value && size)) return EINVAL;
    if (strlen(name) >= MAXNAMELEN) return ENAMETOOLONG;
    if (size > INT64_MAX) return EFBIG;
    if (flags & ~(OZ_XATTR_CREATE | OZ_XATTR_REPLACE) ||
        flags == (OZ_XATTR_CREATE | OZ_XATTR_REPLACE)) return EINVAL;
    if (!cr) return EACCES;
    if (zp->z_pflags & ZFS_XATTR) return EINVAL;
    zfsvfs_t *fs = ZTOZSB(zp);
    int error = zfs_enter_verify_zp(fs, zp, FTAG);
    if (error) return error;
    if (zfs_is_readonly(fs)) error = EROFS;
    else if (!fs->z_log) error = ENOTSUP;
    else if (zp->z_pflags & (ZFS_IMMUTABLE | ZFS_APPENDONLY)) error = EPERM;
    else error = zfs_zaccess(zp, ACE_WRITE_NAMED_ATTRS, 0, B_FALSE, cr, NULL);
    if (error) { zfs_exit(fs, FTAG); return error; }
    rw_enter(&zp->z_xattr_lock, RW_WRITER);
    boolean_t in_sa = B_FALSE, in_dir = B_FALSE;
    if (fs->z_use_sa && zp->z_is_sa) {
        mutex_enter(&zp->z_lock);
        if (!zp->z_xattr_cached) error = zfs_sa_get_xattr(zp);
        mutex_exit(&zp->z_lock);
        if (error) goto out_set;
        uchar_t *bytes; uint_t count;
        error = nvlist_lookup_byte_array(zp->z_xattr_cached, name, &bytes, &count);
        if (!error) in_sa = B_TRUE;
        else if (error != ENOENT) goto out_set;
    }
    znode_t *dir = NULL, *file = NULL;
    zfs_dirlock_t *lock = NULL;
    error = zfs_get_xattrdir(zp, &dir, cr, 0);
    if (!error) error = zfs_dirent_lock(&lock, dir, (char *)name, &file,
        ZEXISTS | ZSHARED, NULL, NULL);
    if (!error) in_dir = B_TRUE;
    if (lock) zfs_dirent_unlock(lock);
    if (file) oz_znode_rele(file);
    if (dir) oz_znode_rele(dir);
    if (error && error != ENOENT) goto out_set;
    error = 0;
    if ((in_sa || in_dir) && (flags & OZ_XATTR_CREATE)) { error = EEXIST; goto out_set; }
    if (!in_sa && !in_dir) {
        if (flags & OZ_XATTR_REPLACE) error = ENODATA;
        if (error || !value) goto out_set;
    }
    if (fs->z_use_sa && zp->z_is_sa && (fs->z_xattr_sa || (!value && in_sa))) {
        if (value || in_sa) error = set_sa(zp, name, value, size);
        if (!error) {
            if (in_dir) error = set_dir(zp, name, NULL, 0, cr);
            goto out_set;
        }
        /* Do not hide I/O/allocation failures behind another backend. */
        if (error != EFBIG) goto out_set;
    }
    error = set_dir(zp, name, value, size, cr);
    if (!error && in_sa) error = set_sa(zp, name, NULL, 0);
out_set:
    rw_exit(&zp->z_xattr_lock);
    zfs_exit(fs, FTAG);
    return error;
}
