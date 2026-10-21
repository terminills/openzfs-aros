/* SPDX-License-Identifier: CDDL-1.0 */
/* Native adaptation of upstream Linux zfs_set_default_quota. Caller authorizes
 * the request and holds teardown writer protection; this is not an IPC API. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <zfs_prop.h>

int zfs_set_default_quota(zfsvfs_t *fs, zfs_prop_t prop, uint64_t quota)
{
    if (!fs || !fs->z_os) return EINVAL;
    uint64_t *cached;
    switch (prop) {
    case ZFS_PROP_DEFAULTUSERQUOTA: cached = &fs->z_defaultuserquota; break;
    case ZFS_PROP_DEFAULTGROUPQUOTA: cached = &fs->z_defaultgroupquota; break;
    case ZFS_PROP_DEFAULTPROJECTQUOTA: cached = &fs->z_defaultprojectquota; break;
    case ZFS_PROP_DEFAULTUSEROBJQUOTA: cached = &fs->z_defaultuserobjquota; break;
    case ZFS_PROP_DEFAULTGROUPOBJQUOTA: cached = &fs->z_defaultgroupobjquota; break;
    case ZFS_PROP_DEFAULTPROJECTOBJQUOTA: cached = &fs->z_defaultprojectobjquota; break;
    default: return EINVAL;
    }
    /* Dataset/mount readonly controls file operations, not administrative
     * quota metadata. Never start a transaction on a snapshot/read-only SPA. */
    if (dmu_objset_is_snapshot(fs->z_os) ||
        !spa_writeable(dmu_objset_spa(fs->z_os))) return EROFS;
    const char *name = zfs_prop_to_name(prop);
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, name);
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); return error; }
    if (!quota) {
        error = zap_remove(fs->z_os, MASTER_NODE_OBJ, name, tx);
        if (error == ENOENT) error = 0;
    } else {
        error = zap_update(fs->z_os, MASTER_NODE_OBJ, name, 8, 1, &quota, tx);
    }
    if (!error) *cached = quota;
    dmu_tx_commit(tx);
    return error;
}
