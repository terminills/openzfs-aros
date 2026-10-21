/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream zfs_remove namespace/transaction path from zfs_vnops_os.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2013, 2016 by Delphix.
 * Native nodes are mount-owned rather than Linux inode-owned: use upstream's
 * deferred unlinked-set path unconditionally. The exclusive drainer owns
 * reference admission, xattr retirement and object freeing, never zrele.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/dmu_objset.h>

int
zfs_remove(znode_t *dzp, char *name, cred_t *cr, int flags)
{
    if (!dzp || !name || !*name || strchr(name, '/')) return (EINVAL);
    if (!cr) return (EACCES);
    if (!S_ISDIR(dzp->z_mode)) return (ENOTDIR);
    if (flags & ~FIGNORECASE) return (ENOTSUP);
    zfsvfs_t *fs = ZTOZSB(dzp);
    int error = zfs_enter_verify_zp(fs, dzp, FTAG);
    if (error) return (error);
    zilog_t *zilog = fs->z_log;
    if (zfs_is_readonly(fs) || !zilog) {
        error = zfs_is_readonly(fs) ? EROFS : ENOTSUP;
        zfs_exit(fs, FTAG);
        return (error);
    }
    int zflg = ZEXISTS | ((flags & FIGNORECASE) ? ZCILOOK : 0);
    boolean_t waited = B_FALSE;
top:
    ;
    znode_t *zp = NULL;
    zfs_dirlock_t *dl;
    error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
    if (error) { zfs_exit(fs, FTAG); return (error); }
    error = zfs_zaccess_delete(dzp, zp, cr, NULL);
    if (error) goto out;
    if (S_ISDIR(zp->z_mode)) { error = EPERM; goto out; }

    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
    dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
    zfs_sa_upgrade_txholds(tx, zp);
    zfs_sa_upgrade_txholds(tx, dzp);
    dmu_tx_hold_zap(tx, fs->z_unlinkedobj, FALSE, NULL);
    dmu_tx_mark_netfree(tx);
    error = dmu_tx_assign(tx, (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
    if (error) {
        zfs_dirent_unlock(dl);
        if (error == ERESTART) {
            waited = B_TRUE;
            dmu_tx_wait(tx);
            dmu_tx_abort(tx);
            zrele(zp);
            goto top;
        }
        dmu_tx_abort(tx);
        zrele(zp);
        zfs_exit(fs, FTAG);
        return (error);
    }
    boolean_t unlinked = B_FALSE;
    error = zfs_link_destroy(dl, zp, tx, zflg, &unlinked);
    if (!error) {
        if (unlinked) zfs_unlinked_add(zp, tx);
        uint64_t type = TX_REMOVE | ((flags & FIGNORECASE) ? TX_CI : 0);
        zfs_log_remove(zilog, tx, type, dzp, name, zp->z_id, unlinked);
    }
    dmu_tx_commit(tx);
out:
    zfs_dirent_unlock(dl);
    zfs_znode_update_vfs(dzp);
    zfs_znode_update_vfs(zp);
    zfs_zrele_async(zp);
    if (!error && fs->z_os->os_sync == ZFS_SYNC_ALWAYS)
        error = zil_commit(zilog, 0);
    zfs_exit(fs, FTAG);
    return (error);
}
