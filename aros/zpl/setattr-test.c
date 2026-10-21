/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <proto/exec.h>

static int check_ids(znode_t *zp, uint64_t uid, uint64_t gid)
{
    uint64_t stored_uid, stored_gid;
    int error = sa_lookup(zp->z_sa_hdl,SA_ZPL_UID(ZTOZSB(zp)),&stored_uid,8);
    if (!error) error = sa_lookup(zp->z_sa_hdl,SA_ZPL_GID(ZTOZSB(zp)),&stored_gid,8);
    if (!error && (zp->z_uid != uid || zp->z_gid != gid ||
        stored_uid != uid || stored_gid != gid)) error = EIO;
    return (error);
}
int oz_setattr_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry;
    znode_t *parent = NULL, *file = NULL, *xdir = NULL, *attr = NULL;
    int error = zap_lookup(fs->z_os,fs->z_root,"subdir",8,1,&entry);
    if (!error) error = zfs_zget(fs,ZFS_DIRENT_OBJ(entry),&parent);
    vattr_t va = {.va_mask = ATTR_MODE,.va_mode = S_IFREG | 0666};
    if (!error && write) error = zfs_create(parent,"setattr-file",&va,1,
        S_IRUSR | S_IWUSR,&file,kcred,0,NULL,NULL);
    else if (!error) {
        error = zap_lookup(fs->z_os,parent->z_id,"setattr-file",8,1,&entry);
        if (!error) error = zfs_zget(fs,ZFS_DIRENT_OBJ(entry),&file);
    }
    if (!error) error = zfs_get_xattrdir(file,&xdir,kcred,write ? CREATE_XATTR_DIR : 0);
    if (!error && write) error = zfs_create(xdir,"user.owner",&va,1,
        S_IWUSR,&attr,kcred,0,NULL,NULL);
    else if (!error) {
        error = zap_lookup(fs->z_os,xdir->z_id,"user.owner",8,1,&entry);
        if (!error) error = zfs_zget(fs,ZFS_DIRENT_OBJ(entry),&attr);
    }
    unsigned char bytes[16], actual[16];
    for (unsigned i = 0; i < 16; i++) bytes[i] = i + 0x70;
    if (!error && write) {
        size_t residual = 16;
        error = zfs_write_simple(file,bytes,16,0,&residual);
        if (!error && residual) error = EIO;
        cred_t *reader = oz_cred_create(88,88,88,0,NULL);
        if (!reader) error = ENOMEM;
        else {
            va = (vattr_t){.va_mask = ATTR_CTIME};
            if (!error && zfs_setattr(file,&va,0,kcred,NULL) != ENOTSUP) error = EIO;
            fs->z_replay = B_TRUE;
            if (!error && zfs_setattr(file,&va,0,reader,NULL) != EACCES) error = EIO;
            fs->z_replay = B_FALSE;
            va = (vattr_t){.va_mask = ATTR_SIZE | ATTR_UID,.va_size = 0,.va_uid = 55};
            if (!error && (zfs_setattr(file,&va,0,reader,NULL) != EPERM ||
                file->z_size != 16)) error = EIO;
            oz_cred_release(reader);
        }
        va = (vattr_t){.va_mask = ATTR_SIZE,.va_size = 0};
        if (!error && zfs_setattr(file,&va,0,NULL,NULL) != EACCES) error = EIO;
        if (!error && zfs_setattr(file,&va,ATTR_NOACLCHECK,kcred,NULL) != ENOTSUP) error = EIO;
        file->z_pflags |= ZFS_APPENDONLY;
        if (!error && zfs_setattr(file,&va,0,kcred,NULL) != EPERM) error = EIO;
        file->z_pflags &= ~ZFS_APPENDONLY;
        zilog_t *saved = fs->z_log; fs->z_log = NULL;
        if (!error && zfs_setattr(file,&va,0,kcred,NULL) != ENOTSUP) error = EIO;
        fs->z_log = saved;
        va.va_mask |= ATTR_MTIME; va.va_mtime.tv_nsec = 1000000000;
        if (!error && (zfs_setattr(file,&va,0,kcred,NULL) != EOVERFLOW ||
            file->z_size != 16)) error = EIO;
        va = (vattr_t){.va_mask = ATTR_UID | ATTR_GID,.va_uid = 77,.va_gid = 88};
        if (!error) error = zfs_setattr(file,&va,0,kcred,NULL);
        gid_t group = 99;
        cred_t *owner = oz_cred_create(77,77,88,1,&group);
        if (!owner) error = ENOMEM;
        else {
            va = (vattr_t){.va_mask = ATTR_UID,.va_uid = 55};
            if (!error && zfs_setattr(file,&va,0,owner,NULL) != EPERM) error = EIO;
            va = (vattr_t){.va_mask = ATTR_GID,.va_gid = 666};
            if (!error && zfs_setattr(file,&va,0,owner,NULL) != EPERM) error = EIO;
            va.va_gid = 99;
            if (!error) error = zfs_setattr(file,&va,0,owner,NULL);
            va = (vattr_t){.va_mask = ATTR_MODE,.va_mode = 0640};
            if (!error) error = zfs_setattr(file,&va,0,owner,NULL);
            oz_cred_release(owner);
        }
        va = (vattr_t){.va_mask = ATTR_SIZE,.va_size = 8};
        if (!error) error = zfs_setattr(file,&va,0,kcred,NULL);
        /* Verify content before setting the deterministic timestamp. A later
         * read legitimately dirties atime and mount sync persists it. */
        if (!error) {
            struct iovec vec = {actual,8}; zfs_uio_t uio;
            zfs_uio_iovec_init(&uio,&vec,1,0,UIO_SYSSPACE,8,0);
            error = zfs_read(file,&uio,0,kcred);
            if (!error && (uio.uio_resid || memcmp(actual,bytes,8))) error = EIO;
        }
        va = (vattr_t){.va_mask = ATTR_ATIME | ATTR_MTIME,
            .va_atime = {1800000000,123},.va_mtime = {1800000001,456}};
        if (!error) error = zfs_setattr(file,&va,0,kcred,NULL);
        if (!error) error = zfs_fsync(file,0,kcred);
        if (!error) error = zil_commit(fs->z_log,0);
    } else if (!error) {
        va = (vattr_t){.va_mask = ATTR_MODE,.va_mode = 0777};
        if (zfs_setattr(file,&va,0,kcred,NULL) != EROFS) error = EIO;
    }
    if (!error) error = check_ids(file,77,99);
    if (!error) error = check_ids(xdir,77,99);
    if (!error) error = check_ids(attr,77,99);
    uint64_t atime[2], mtime[2], stored_mode, size;
    if (!error) error = sa_lookup(file->z_sa_hdl,SA_ZPL_ATIME(fs),atime,sizeof (atime));
    if (!error) error = sa_lookup(file->z_sa_hdl,SA_ZPL_MTIME(fs),mtime,sizeof (mtime));
    if (!error) error = sa_lookup(file->z_sa_hdl,SA_ZPL_MODE(fs),&stored_mode,8);
    if (!error) error = sa_lookup(file->z_sa_hdl,SA_ZPL_SIZE(fs),&size,8);
    if (!error && (atime[0] != 1800000000 || atime[1] != 123 ||
        mtime[0] != 1800000001 || mtime[1] != 456 ||
        stored_mode != (S_IFREG | 0640) || file->z_mode != stored_mode ||
        size != 8 || file->z_size != 8)) error = EIO;
    if (!error && !write) {
        struct iovec vec = {actual,8}; zfs_uio_t uio;
        zfs_uio_iovec_init(&uio,&vec,1,0,UIO_SYSSPACE,8,0);
        error = zfs_read(file,&uio,0,kcred);
        if (!error && (uio.uio_resid || memcmp(actual,bytes,8))) error = EIO;
    }
    if (attr) oz_znode_rele(attr);
    if (xdir) oz_znode_rele(xdir);
    if (file) oz_znode_rele(file);
    if (parent) oz_znode_rele(parent);
    char text[100]; snprintf(text,sizeof (text),
        "Native upstream setattr/owner/mode/times write=%d result=%d\n",write,error);
    for (char *p=text; *p; p++) RawPutChar(*p);
    return (error);
}
