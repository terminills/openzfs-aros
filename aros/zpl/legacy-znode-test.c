/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_acl.h>
#include <sys/dmu_tx.h>
int oz_acl_read_test(znode_t *, uint32_t);

/* Temporary DMU objects using the upstream legacy layout, not a recreated
 * filesystem format. No namespace/create/unlink semantics are claimed. */
int
oz_legacy_znode_test(zfsvfs_t *fs)
{
    uint64_t object = 0, acl_object = 0, short_object = 0;
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
    dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, sizeof (zfs_ace_hdr_t));
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); return (error); }
    acl_object = dmu_object_alloc(fs->z_os, DMU_OT_ACL, 4096,
        DMU_OT_NONE, 0, tx);
    zfs_ace_hdr_t ace = {ACE_ACCESS_ALLOWED_ACE_TYPE, ACE_EVERYONE,
        ACE_READ_DATA};
    dmu_write(fs->z_os, acl_object, 0, sizeof (ace), &ace, tx, 0);
    object = dmu_object_alloc(fs->z_os, DMU_OT_PLAIN_FILE_CONTENTS, 4096,
        DMU_OT_ZNODE, sizeof (znode_phys_t), tx);
    short_object = dmu_object_alloc(fs->z_os, DMU_OT_PLAIN_FILE_CONTENTS, 4096,
        DMU_OT_ZNODE, 64, tx);
    dmu_buf_t *db = NULL;
    error = dmu_bonus_hold(fs->z_os, object, FTAG, &db);
    if (!error) {
        dmu_buf_will_dirty(db, tx);
        znode_phys_t *disk = db->db_data;
        memset(disk, 0, sizeof (*disk));
        disk->zp_gen = dmu_tx_get_txg(tx);
        disk->zp_mode = S_IFREG | 0640;
        disk->zp_links = 1;
        disk->zp_uid = 0x10000002aULL;
        disk->zp_gid = 99;
        disk->zp_atime[0] = 1700000001;
        disk->zp_atime[1] = 123;
        disk->zp_crtime[0] = 1700000000;
        disk->zp_acl.z_acl_extern_obj = acl_object;
        disk->zp_acl.z_acl_version = ZFS_ACL_VERSION_FUID;
        disk->zp_acl.z_acl_count = 1;
        disk->zp_acl.z_acl_size = sizeof (ace);
        dmu_buf_rele(db, FTAG);
    }
    dmu_tx_commit(tx);
    if (!error) {
        oz_znode_cache_init(fs);
        znode_t *zp = NULL;
        error = zfs_zget(fs, object, &zp);
        if (!error && (zp->z_is_sa || zp->z_mode != (S_IFREG | 0640) ||
            zp->z_uid != 0x10000002aULL || zp->z_gid != 99 ||
            zp->z_links != 1 || zp->z_atime.tv_sec != 1700000001 ||
            zp->z_atime.tv_nsec != 123 || zp->z_btime.tv_sec != 1700000000 ||
            zfs_external_acl(zp) != acl_object)) error = EIO;
        if (!error) error = oz_acl_read_test(zp, ACE_READ_DATA);
        if (!error) {
            inode_timespec_t stamp = {1700000999, 987654321};
            oz_znode_accessed_at(zp, &stamp);
            if (!zp->z_atime_dirty) error = EIO;
            if (!error) error = oz_znode_cache_sync(fs, B_TRUE);
            if (!error && (zp->z_atime_dirty || zp->z_is_sa)) error = EIO;
            dmu_object_info_t doi;
            if (!error) error = dmu_object_info(fs->z_os, object, &doi);
            if (!error && doi.doi_bonus_type != DMU_OT_ZNODE) error = EIO;
            db = NULL;
            if (!error) error = dmu_bonus_hold(fs->z_os, object, FTAG, &db);
            if (db) {
                const znode_phys_t *disk = db->db_data;
                if (disk->zp_atime[0] != (uint64_t)stamp.tv_sec ||
                    disk->zp_atime[1] != (uint64_t)stamp.tv_nsec ||
                    disk->zp_uid != 0x10000002aULL || disk->zp_gid != 99 ||
                    disk->zp_acl.z_acl_extern_obj != acl_object) error = EIO;
                dmu_buf_rele(db, FTAG);
            }
        }
        if (zp) oz_znode_rele(zp);
        zp = NULL;
        if (!error && (zfs_zget(fs, short_object, &zp) != ENOTSUP || zp)) error = EIO;
        if (zp) oz_znode_rele(zp);
        zp = NULL;
        if (!error && (zfs_zget(fs, acl_object, &zp) != ENOTSUP || zp)) error = EIO;
        if (zp) oz_znode_rele(zp);
        int cleanup = oz_znode_cache_fini(fs);
        if (cleanup) return (cleanup); /* Never free backing with held SA. */
    }
    tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_free(tx, object, 0, DMU_OBJECT_END);
    dmu_tx_hold_free(tx, acl_object, 0, DMU_OBJECT_END);
    dmu_tx_hold_free(tx, short_object, 0, DMU_OBJECT_END);
    int cleanup = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (cleanup) dmu_tx_abort(tx);
    else {
        cleanup = dmu_object_free(fs->z_os, object, tx);
        int e = dmu_object_free(fs->z_os, acl_object, tx);
        if (!cleanup) cleanup = e;
        e = dmu_object_free(fs->z_os, short_object, tx);
        if (!cleanup) cleanup = e;
        dmu_tx_commit(tx);
    }
    return (error ? error : cleanup);
}
