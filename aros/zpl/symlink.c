/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream zfs_symlink/readlink adapted to native znodes.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/dmu_objset.h>
#include <sys/u8_textprep.h>

int
zfs_symlink(znode_t *dzp, char *name, vattr_t *vap, char *link,
    znode_t **zpp, cred_t *cr, int flags, zidmap_t *mnt_ns)
{
	if (!zpp) return (EINVAL);
	*zpp = NULL;
	if (!dzp || !vap || !name || !*name || strchr(name, '/') || !link)
		return (EINVAL);
	if (!cr) return (EACCES);
	if (!S_ISDIR(dzp->z_mode)) return (ENOTDIR);
	if (dzp->z_pflags & ZFS_XATTR) return (EINVAL);
	if (!S_ISLNK(vap->va_mode)) return (EINVAL);
	if (mnt_ns || (flags & ~FIGNORECASE) || (vap->va_mask & ATTR_XVATTR))
		return (ENOTSUP);
	if (!ZTOZSB(dzp)->z_longname && strlen(name) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);
	znode_t		*zp;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zilog_t		*zilog;
	uint64_t	len = strnlen(link, MAXPATHLEN + 1);
	int		error;
	int		zflg = ZNEW;
	zfs_acl_ids_t	acl_ids;
	boolean_t	fuid_dirtied;
	uint64_t	txtype = TX_SYMLINK;
	boolean_t	waited = B_FALSE;

	ASSERT(S_ISLNK(vap->va_mode));

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

	if (zfsvfs->z_utf8 && u8_validate(name, strlen(name),
	    NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

	if (len > MAXPATHLEN) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(ENAMETOOLONG));
	}

	if ((error = zfs_acl_ids_create(dzp, 0,
	    vap, cr, NULL, &acl_ids, mnt_ns)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
top:
	*zpp = NULL;

	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, dzp, name, &zp, zflg, NULL, NULL);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_zaccess(dzp, ACE_ADD_FILE, 0, B_FALSE, cr, mnt_ns))) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, ZFS_DEFAULT_PROJID)) {
		zfs_acl_ids_free(&acl_ids);
		zfs_dirent_unlock(dl);
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EDQUOT));
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, MAX(1, len));
	dmu_tx_hold_zap(tx, dzp->z_id, TRUE, name);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE + len);
	dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
	if (!zfsvfs->z_use_sa && acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
		dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0,
		    acl_ids.z_aclp->z_acl_bytes);
	}
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
	if (error) {
		zfs_dirent_unlock(dl);
		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			goto top;
		}
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Create a new object for the symlink.
	 * for version 4 ZPL datasets the symlink will be an SA attribute
	 */
	zfs_mknode(dzp, vap, tx, cr, 0, &zp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_update(zp->z_sa_hdl, SA_ZPL_SYMLINK(zfsvfs),
		    link, len, tx);
	else {
		zfs_sa_symlink(zp, link, len, tx);
		error = 0;
	}
	mutex_exit(&zp->z_lock);

	zp->z_size = len;
	if (!error) error = sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(zfsvfs),
	    &zp->z_size, sizeof (zp->z_size), tx);
	/*
	 * Insert the new object into the directory.
	 */
	if (!error) error = zfs_link_create(dl, zp, tx, ZNEW);
	if (error != 0) {
		zfs_znode_delete(zp, tx);
	} else {
		if (flags & FIGNORECASE)
			txtype |= TX_CI;
		zfs_log_symlink(zilog, tx, txtype, dzp, zp, name, link);

		zfs_znode_update_vfs(dzp);
		zfs_znode_update_vfs(zp);
	}

	zfs_acl_ids_free(&acl_ids);

	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	if (error == 0) {
		*zpp = zp;
	} else {
		zrele(zp);
	}

	zfs_exit(zfsvfs, FTAG);
	return (error);
}

int
zfs_readlink(znode_t *zp, zfs_uio_t *uio, cred_t *cr)
{
	if (!zp || !uio || !S_ISLNK(zp->z_mode) || zfs_uio_offset(uio) < 0)
		return (EINVAL);
	if (!cr) return (EACCES);
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	int		error;

	if ((error = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (error);

	mutex_enter(&zp->z_lock);
	if (zp->z_is_sa)
		error = sa_lookup_uio(zp->z_sa_hdl,
		    SA_ZPL_SYMLINK(zfsvfs), uio);
	else
		error = zfs_sa_readlink(zp, uio);
	mutex_exit(&zp->z_lock);

	zfs_exit(zfsvfs, FTAG);
	return (error);
}
