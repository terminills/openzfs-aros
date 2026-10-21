/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream xattr directory creation/lookup from zfs_dir.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Internal helpers: callers own mount lifetime and permission admission.
 * Attribute file operations use upstream base-file named-attribute ACL checks.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/dmu_objset.h>

int
zfs_make_xattrdir(znode_t *zp, vattr_t *vap, znode_t **xzpp, cred_t *cr)
{
	if (!xzpp) return (EINVAL);
	*xzpp = NULL;
	if (!zp || !vap || !S_ISDIR(vap->va_mode)) return (EINVAL);
	if (!cr) return (EACCES);
	if (zfs_is_readonly(ZTOZSB(zp))) return (EROFS);
	if (!ZTOZSB(zp)->z_log) return (ENOTSUP);
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	znode_t *xzp;
	dmu_tx_t *tx;
	int error;
	zfs_acl_ids_t acl_ids;
	boolean_t fuid_dirtied;
#ifdef ZFS_DEBUG
	uint64_t parent;
#endif

	*xzpp = NULL;

	if ((error = zfs_acl_ids_create(zp, IS_XATTR, vap, cr, NULL,
	    &acl_ids, NULL)) != 0)
		return (error);
	if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, zp->z_projid)) {
		zfs_acl_ids_free(&acl_ids);
		return (SET_ERROR(EDQUOT));
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
	    ZFS_SA_BASE_ATTR_SIZE);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, FALSE, NULL);
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		zfs_acl_ids_free(&acl_ids);
		dmu_tx_abort(tx);
		return (error);
	}
	zfs_mknode(zp, vap, tx, cr, IS_XATTR, &xzp, &acl_ids);

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

#ifdef ZFS_DEBUG
	error = sa_lookup(xzp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (parent));
	ASSERT(error == 0 && parent == zp->z_id);
#endif

	VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs), &xzp->z_id,
	    sizeof (xzp->z_id), tx));

	if (!zp->z_unlinked)
		zfs_log_create(zfsvfs->z_log, tx, TX_MKXATTR, zp, xzp, "", NULL,
		    acl_ids.z_fuidp, vap);

	zfs_acl_ids_free(&acl_ids);
	dmu_tx_commit(tx);

	*xzpp = xzp;

	return (0);
}

/*
 * Return a znode for the extended attribute directory for zp.
 * ** If the directory does not already exist, it is created **
 *
 *	IN:	zp	- znode to obtain attribute directory from
 *		cr	- credentials of caller
 *		flags	- flags from the VOP_LOOKUP call
 *
 *	OUT:	xipp	- pointer to extended attribute znode
 *
 *	RETURN:	0 on success
 *		error number on failure
 */
int
zfs_get_xattrdir(znode_t *zp, znode_t **xzpp, cred_t *cr, int flags)
{
	if (!xzpp) return (EINVAL);
	*xzpp = NULL;
	if (!zp || (zp->z_pflags & ZFS_XATTR)) return (EINVAL);
	if (!cr) return (EACCES);
	if (flags & ~CREATE_XATTR_DIR) return (ENOTSUP);
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	znode_t		*xzp;
	zfs_dirlock_t	*dl;
	vattr_t		va = {0};
	int		error;
top:
	error = zfs_dirent_lock(&dl, zp, "", &xzp, ZXATTR, NULL, NULL);
	if (error)
		return (error);

	if (xzp != NULL) {
		*xzpp = xzp;
		zfs_dirent_unlock(dl);
		return (0);
	}

	if (!(flags & CREATE_XATTR_DIR)) {
		zfs_dirent_unlock(dl);
		return (SET_ERROR(ENOENT));
	}

	if (zfs_is_readonly(zfsvfs)) {
		zfs_dirent_unlock(dl);
		return (SET_ERROR(EROFS));
	}

	/*
	 * The ability to 'create' files in an attribute
	 * directory comes from the write_xattr permission on the base file.
	 *
	 * The ability to 'search' an attribute directory requires
	 * read_xattr permission on the base file.
	 *
	 * Once in a directory the ability to read/write attributes
	 * is controlled by the permissions on the attribute file.
	 */
	va.va_mask = ATTR_MODE | ATTR_UID | ATTR_GID;
	va.va_mode = S_IFDIR | S_ISVTX | 0777;
	zfs_fuid_map_ids(zp, cr, &va.va_uid, &va.va_gid);

	error = zfs_make_xattrdir(zp, &va, xzpp, cr);
	zfs_dirent_unlock(dl);

	if (error == ERESTART) {
		/* NB: we already did dmu_tx_wait() if necessary */
		goto top;
	}

	return (error);
}
