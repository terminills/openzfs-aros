/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream zfs_rmdir from zfs_vnops_os.c with native admission.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2013, 2016 by Delphix.
 * Final deletion is deferred to exclusive native unlinked-set draining.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/dmu_objset.h>
int
zfs_rmdir(znode_t *dzp, char *name, znode_t *cwd, cred_t *cr,
    int flags)
{
	if (!dzp || !name || !*name || strchr(name, '/')) return (EINVAL);
	if (!cr) return (EACCES);
	if (!S_ISDIR(dzp->z_mode)) return (ENOTDIR);
	if (flags & ~FIGNORECASE) return (ENOTSUP);
	znode_t		*zp;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zflg = ZEXISTS;
	boolean_t	waited = B_FALSE;

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, dzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;
	if (zfs_is_readonly(zfsvfs) || !zilog) {
		error = zfs_is_readonly(zfsvfs) ? EROFS : ENOTSUP;
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;
top:
	zp = NULL;

	/*
	 * Attempt to lock directory; fail if entry doesn't exist.
	 */
	if ((error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg,
	    NULL, NULL))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess_delete(dzp, zp, cr, NULL))) {
		goto out;
	}

	if (!S_ISDIR(zp->z_mode)) {
		error = SET_ERROR(ENOTDIR);
		goto out;
	}

	if (zp == cwd) {
		error = SET_ERROR(EINVAL);
		goto out;
	}

	/*
	 * Grab a lock on the directory to make sure that no one is
	 * trying to add (or lookup) entries while we are removing it.
	 */
	rw_enter(&zp->z_name_lock, RW_WRITER);

	/*
	 * Grab a lock on the parent pointer to make sure we play well
	 * with the treewalk and directory rename code.
	 */
	rw_enter(&zp->z_parent_lock, RW_WRITER);

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_zap(tx, dzp->z_id, FALSE, name);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	zfs_sa_upgrade_txholds(tx, zp);
	zfs_sa_upgrade_txholds(tx, dzp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
	if (error) {
		rw_exit(&zp->z_parent_lock);
		rw_exit(&zp->z_name_lock);
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
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	error = zfs_link_destroy(dl, zp, tx, zflg, NULL);

	if (error == 0) {
		uint64_t txtype = TX_RMDIR;
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_remove(zilog, tx, txtype, dzp, name, ZFS_NO_OBJECT,
		    B_FALSE);
	}

	dmu_tx_commit(tx);

	rw_exit(&zp->z_parent_lock);
	rw_exit(&zp->z_name_lock);
out:
	zfs_dirent_unlock(dl);

	zfs_znode_update_vfs(dzp);
	zfs_znode_update_vfs(zp);
	zrele(zp);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

