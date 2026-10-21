/* SPDX-License-Identifier: CDDL-1.0 */
/* Native adaptation of upstream Linux zfs_set_version (zfs_vfsops.c).
 * Portions Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Caller authorizes the request and holds filesystem teardown writer access. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_sa.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/zap.h>
#include <zfs_comutil.h>

int zfs_set_version(zfsvfs_t *fs, uint64_t version)
{
    if (!fs || !fs->z_os) return EINVAL;
    if (version < ZPL_VERSION_INITIAL || version > ZPL_VERSION ||
        version < fs->z_version) return EINVAL;
    objset_t *os = fs->z_os;
    if (zfs_spa_version_map(version) > spa_version(dmu_objset_spa(os)))
        return ENOTSUP;
    if (dmu_objset_is_snapshot(os) || !spa_writeable(dmu_objset_spa(os)))
        return EROFS;
    boolean_t upgrade_sa = version >= ZPL_VERSION_SA && !fs->z_use_sa;
    dmu_tx_t *tx = dmu_tx_create(os);
    dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_FALSE, ZPL_VERSION_STR);
    if (upgrade_sa) {
        dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE, ZFS_SA_ATTRS);
        dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_FALSE, NULL);
    }
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); return error; }
    error = zap_update(os, MASTER_NODE_OBJ, ZPL_VERSION_STR, 8, 1, &version, tx);
    if (error) { dmu_tx_commit(tx); return error; }
    if (upgrade_sa) {
        uint64_t sa = zap_create(os, DMU_OT_SA_MASTER_NODE, DMU_OT_NONE, 0, tx);
        VERIFY0(zap_add(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa, tx));
        VERIFY0(sa_set_sa_object(os, sa));
        sa_register_update_callback(os, zfs_sa_upgrade);
    }
    spa_history_log_internal_ds(dmu_objset_ds(os), "upgrade", tx,
        "from %llu to %llu", (u_longlong_t)fs->z_version, (u_longlong_t)version);
    dmu_tx_commit(tx);
    fs->z_version = version;
    os->os_version = version;
    fs->z_use_fuids = USE_FUIDS(version, os);
    fs->z_use_sa = USE_SA(version, os);
    return 0;
}
