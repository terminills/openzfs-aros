/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream setattr and xattr-owner propagation from zfs_vnops_os.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2013, 2016 by Delphix.
 * Native VFS policy and cached metadata replace Linux inode services.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <sys/zfs_quota.h>
#include <sys/dmu_objset.h>
#include "setattr-policy.h"
static int
zfs_setattr_dir(znode_t *dzp)
{
	znode_t *dxip = dzp;
	znode_t *xip = NULL;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	objset_t	*os = zfsvfs->z_os;
	zap_cursor_t	zc;
	zap_attribute_t	*zap;
	zfs_dirlock_t	*dl;
	znode_t		*zp = NULL;
	dmu_tx_t	*tx = NULL;
	uint64_t	uid, gid;
	sa_bulk_attr_t	bulk[4];
	int		count;
	int		err;

	zap = zap_attribute_alloc();
	zap_cursor_init(&zc, os, dzp->z_id);
	while ((err = zap_cursor_retrieve(&zc, zap)) == 0) {
		count = 0;
		if (zap->za_integer_length != 8 || zap->za_num_integers != 1) {
			err = ENXIO;
			break;
		}

		err = zfs_dirent_lock(&dl, dzp, (char *)zap->za_name, &zp,
		    ZEXISTS, NULL, NULL);
		if (err == ENOENT)
			goto next;
		if (err)
			break;

		xip = zp;
		if (xip->z_uid == dxip->z_uid &&
		    xip->z_gid == dxip->z_gid &&
		    zp->z_projid == dzp->z_projid)
			goto next;

		tx = dmu_tx_create(os);
		if (!(zp->z_pflags & ZFS_PROJID))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);

		err = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (err)
			break;

		mutex_enter(&dzp->z_lock);

		if (xip->z_uid != dxip->z_uid) {
			xip->z_uid = dxip->z_uid;
			uid = dxip->z_uid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &uid, sizeof (uid));
		}

		if (xip->z_gid != dxip->z_gid) {
			xip->z_gid = dxip->z_gid;
			gid = dxip->z_gid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs), NULL,
			    &gid, sizeof (gid));
		}


		uint64_t projid = dzp->z_projid;
		if (zp->z_projid != projid) {
			if (!(zp->z_pflags & ZFS_PROJID)) {
				err = sa_add_projid(zp->z_sa_hdl, tx, projid);
				if (unlikely(err == EEXIST)) {
					err = 0;
				} else if (err != 0) {
					goto sa_add_projid_err;
				} else {
					projid = ZFS_INVALID_PROJID;
				}
			}

			if (projid != ZFS_INVALID_PROJID) {
				zp->z_projid = projid;
				SA_ADD_BULK_ATTR(bulk, count,
				    SA_ZPL_PROJID(zfsvfs), NULL, &zp->z_projid,
				    sizeof (zp->z_projid));
			}
		}

sa_add_projid_err:
		mutex_exit(&dzp->z_lock);

		if (likely(count > 0)) {
			err = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
			dmu_tx_commit(tx);
		} else if (projid == ZFS_INVALID_PROJID) {
			dmu_tx_commit(tx);
		} else {
			dmu_tx_abort(tx);
		}
		tx = NULL;
		if (err != 0 && err != ENOENT)
			break;

next:
		if (zp) {
			zrele(zp);
			zp = NULL;
			zfs_dirent_unlock(dl);
		}
		zap_cursor_advance(&zc);
	}

	if (tx)
		dmu_tx_abort(tx);
	if (zp) {
		zrele(zp);
		zfs_dirent_unlock(dl);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);

	return (err == ENOENT ? 0 : err);
}

/*
 * Set the file attributes to the values contained in the
 * vattr structure.
 *
 *	IN:	zp	- znode of file to be modified.
 *		vap	- new attribute values.
 *			  If ATTR_XVATTR set, then optional attrs are being set
 *		flags	- ATTR_UTIME set if non-default time values provided.
 *			- ATTR_NOACLCHECK (CIFS context only).
 *		cr	- credentials of caller.
 *		mnt_ns	- user namespace of the mount
 *
 *	RETURN:	0 if success
 *		error code if failure
 *
 * Timestamps:
 *	ip - ctime updated, mtime updated if size changed.
 */
int
zfs_setattr(znode_t *zp, vattr_t *vap, int flags, cred_t *cr, zidmap_t *mnt_ns)
{
	if (!zp || !vap) return (EINVAL);
	if (!cr) return (EACCES);
	/* Upstream ZIL replay supplies CTIME with each restored attribute record.
	 * This is an internal recovery input, not a new caller-settable field. */
	if (ZTOZSB(zp)->z_replay && cr != kcred) return (EACCES);
	uint_t replay_mask = ZTOZSB(zp)->z_replay ? ATTR_CTIME : 0;
	if (mnt_ns || flags || (vap->va_mask & ~(ATTR_MODE | ATTR_UID |
	    ATTR_GID | ATTR_SIZE | ATTR_ATIME | ATTR_MTIME | ATTR_XVATTR | replay_mask))) return (ENOTSUP);
	if ((vap->va_mask & ATTR_SIZE) && vap->va_size > MAXOFFSET_T) return (EFBIG);
	if ((vap->va_mask & ATTR_MODE) && (vap->va_mode & ~(S_IFMT | 07777)))
		return (EINVAL);
	/* Do not modify the caller's vattr or use Linux-prepopulated timestamps. */
	xvattr_t request = {0};
	request.xva_vattr = *vap;
	if (vap->va_mask & ATTR_XVATTR) {
		/* Trusted kernel-side xvattr only. Do not retain caller return-map
		 * pointers or admit unrelated flags without native policy. */
		const xvattr_t *input = (const xvattr_t *)vap;
		xvattr_t allowed;
		xva_init(&allowed);
		if (input->xva_magic != allowed.xva_magic ||
		    input->xva_mapsize != XVA_MAPSIZE) return (EINVAL);
		XVA_SET_REQ(&allowed, XAT_PROJID);
		XVA_SET_REQ(&allowed, XAT_PROJINHERIT);
		for (unsigned i = 0; i < XVA_MAPSIZE; i++)
			if (input->xva_reqattrmap[i] & ~allowed.xva_reqattrmap[i])
				return (ENOTSUP);
		if (XVA_ISSET_REQ(input, XAT_PROJID) &&
		    input->xva_xoptattrs.xoa_projid >= UINT32_MAX) return (EINVAL);
		if (XVA_ISSET_REQ(input, XAT_PROJINHERIT) &&
		    input->xva_xoptattrs.xoa_projinherit > 1) return (EINVAL);
		xva_init(&request);
		request.xva_vattr = *vap;
		memcpy(request.xva_reqattrmap, input->xva_reqattrmap,
		    sizeof (request.xva_reqattrmap));
		request.xva_xoptattrs = input->xva_xoptattrs;
	}
	vap = &request.xva_vattr;
	if (!vap->va_mask) return (0);
	znode_t *ip;
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	objset_t	*os;
	zilog_t		*zilog;
	dmu_tx_t	*tx;
	vattr_t		oldva;
	xvattr_t	*tmpxvattr;
	uint_t		mask = vap->va_mask;
	uint_t		saved_mask = 0;
	int		trim_mask = 0;
	uint64_t	new_mode;
	uint64_t	new_kuid = 0, new_kgid = 0, new_uid, new_gid;
	uint64_t	xattr_obj;
	uint64_t	mtime[2], ctime[2], atime[2];
	uint64_t	projid = ZFS_INVALID_PROJID;
	znode_t		*attrzp;
	int		need_policy = FALSE;
	int		err, err2 = 0;
	zfs_fuid_info_t *fuidp = NULL;
	xvattr_t *xvap = (xvattr_t *)vap;	/* vap may be an xvattr_t * */
	xoptattr_t	*xoap;
	zfs_acl_t	*aclp;
	boolean_t skipaclchk = (flags & ATTR_NOACLCHECK) ? B_TRUE : B_FALSE;
	boolean_t	fuid_dirtied = B_FALSE;
	boolean_t	handle_eadir = B_FALSE;
	sa_bulk_attr_t	*bulk, *xattr_bulk;
	int		count = 0, xattr_count = 0, bulks = 8;

	if (mask == 0)
		return (0);

	if ((err = zfs_enter_verify_zp(zfsvfs, zp, FTAG)) != 0)
		return (err);
	ip = zp;
	os = zfsvfs->z_os;

	/*
	 * If this is a xvattr_t, then get a pointer to the structure of
	 * optional attributes.  If this is NULL, then we have a vattr_t.
	 */
	xoap = xva_getxoptattr(xvap);
	if (xoap != NULL && (mask & ATTR_XVATTR)) {
		if (XVA_ISSET_REQ(xvap, XAT_PROJID)) {
			if (!dmu_objset_projectquota_enabled(os) ||
			    (!S_ISREG(ip->z_mode) && !S_ISDIR(ip->z_mode))) {
				zfs_exit(zfsvfs, FTAG);
				return (SET_ERROR(ENOTSUP));
			}

			projid = xoap->xoa_projid;
			if (unlikely(projid == ZFS_INVALID_PROJID)) {
				zfs_exit(zfsvfs, FTAG);
				return (SET_ERROR(EINVAL));
			}

			if (projid == zp->z_projid && zp->z_pflags & ZFS_PROJID)
				projid = ZFS_INVALID_PROJID;
			else
				need_policy = TRUE;
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJINHERIT) &&
		    (xoap->xoa_projinherit !=
		    ((zp->z_pflags & ZFS_PROJINHERIT) != 0)) &&
		    (!dmu_objset_projectquota_enabled(os) ||
		    (!S_ISREG(ip->z_mode) && !S_ISDIR(ip->z_mode)))) {
			zfs_exit(zfsvfs, FTAG);
			return (SET_ERROR(ENOTSUP));
		}
	}

	zilog = zfsvfs->z_log;
	if (zfs_is_readonly(zfsvfs) || !zilog) {
		err = zfs_is_readonly(zfsvfs) ? EROFS : ENOTSUP;
		zfs_exit(zfsvfs, FTAG);
		return (err);
	}
	/* Validate combined requests before upstream's early truncation step. */
	err = native_setattr_admit(zp, vap, cr);
	if (!err && (mask & ATTR_MODE) && zfsvfs->z_acl_mode == ZFS_ACL_RESTRICTED &&
	    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) err = EPERM;
	if (!err && (mask & ATTR_MODE)) {
		vattr_t before = {.va_mode = zp->z_mode};
		zfs_fuid_map_ids(zp, cr, &before.va_uid, &before.va_gid);
		err = native_setid_admit(zp, vap, &before, cr);
	}
	if (err) { zfs_exit(zfsvfs, FTAG); return (err); }
	gethrestime(&vap->va_ctime);
	if ((mask & ATTR_SIZE) && !(mask & ATTR_MTIME)) vap->va_mtime = vap->va_ctime;
	vap->va_mask |= ATTR_CTIME;
	mask = vap->va_mask;

	/*
	 * Make sure that if we have ephemeral uid/gid or xvattr specified
	 * that file system is at proper version level
	 */

	if (zfsvfs->z_use_fuids == B_FALSE &&
	    (((mask & ATTR_UID) && IS_EPHEMERAL(vap->va_uid)) ||
	    ((mask & ATTR_GID) && IS_EPHEMERAL(vap->va_gid)) ||
	    (mask & ATTR_XVATTR))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	if (mask & ATTR_SIZE && S_ISDIR(ip->z_mode)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EISDIR));
	}

	if (mask & ATTR_SIZE && !S_ISREG(ip->z_mode) && !S_ISFIFO(ip->z_mode)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	tmpxvattr = kmem_alloc(sizeof (xvattr_t), KM_SLEEP);
	xva_init(tmpxvattr);

	bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * bulks, KM_SLEEP);
	xattr_bulk = kmem_alloc(sizeof (sa_bulk_attr_t) * bulks, KM_SLEEP);

	/*
	 * Immutable files can only alter immutable bit and atime
	 */
	if ((zp->z_pflags & ZFS_IMMUTABLE) &&
	    ((mask & (ATTR_SIZE|ATTR_UID|ATTR_GID|ATTR_MTIME|ATTR_MODE)) ||
	    ((mask & ATTR_XVATTR) && XVA_ISSET_REQ(xvap, XAT_CREATETIME)))) {
		err = SET_ERROR(EPERM);
		goto out3;
	}

	/* ZFS_READONLY will be handled in zfs_zaccess() */

	/*
	 * Verify timestamps doesn't overflow 32 bits.
	 * ZFS can handle large timestamps, but 32bit syscalls can't
	 * handle times greater than 2039.  This check should be removed
	 * once large timestamps are fully supported.
	 */
	if (mask & (ATTR_ATIME | ATTR_MTIME)) {
		if (((mask & ATTR_ATIME) &&
		    (vap->va_atime.tv_nsec < 0 || vap->va_atime.tv_nsec >= 1000000000)) ||
		    ((mask & ATTR_MTIME) &&
		    (vap->va_mtime.tv_nsec < 0 || vap->va_mtime.tv_nsec >= 1000000000))) {
			err = SET_ERROR(EOVERFLOW);
			goto out3;
		}
	}

top:
	attrzp = NULL;
	aclp = NULL;

	/* Can this be moved to before the top label? */
	if (zfs_is_readonly(zfsvfs)) {
		err = SET_ERROR(EROFS);
		goto out3;
	}

	/*
	 * First validate permissions
	 */

	if (mask & ATTR_SIZE) {
		err = zfs_zaccess(zp, ACE_WRITE_DATA, 0, skipaclchk, cr,
		    mnt_ns);
		if (err)
			goto out3;

		/*
		 * XXX - Note, we are not providing any open
		 * mode flags here (like FNDELAY), so we may
		 * block if there are locks present... this
		 * should be addressed in openat().
		 */
		/* XXX - would it be OK to generate a log record here? */
		err = zfs_freesp(zp, vap->va_size, 0, 0, FALSE);
		if (err)
			goto out3;
	}

	if (mask & (ATTR_ATIME|ATTR_MTIME) ||
	    ((mask & ATTR_XVATTR) && (XVA_ISSET_REQ(xvap, XAT_HIDDEN) ||
	    XVA_ISSET_REQ(xvap, XAT_READONLY) ||
	    XVA_ISSET_REQ(xvap, XAT_ARCHIVE) ||
	    XVA_ISSET_REQ(xvap, XAT_OFFLINE) ||
	    XVA_ISSET_REQ(xvap, XAT_SPARSE) ||
	    XVA_ISSET_REQ(xvap, XAT_CREATETIME) ||
	    XVA_ISSET_REQ(xvap, XAT_SYSTEM)))) {
		need_policy = zfs_zaccess(zp, ACE_WRITE_ATTRIBUTES, 0,
		    skipaclchk, cr, mnt_ns);
	}

	if (mask & (ATTR_UID|ATTR_GID)) {
		int	idmask = (mask & (ATTR_UID|ATTR_GID));
		int	take_owner;
		int	take_group;
		uid_t	uid;
		gid_t	gid;

		/*
		 * NOTE: even if a new mode is being set,
		 * we may clear S_ISUID/S_ISGID bits.
		 */

		if (!(mask & ATTR_MODE))
			vap->va_mode = zp->z_mode;

		/*
		 * Take ownership or chgrp to group we are a member of
		 */

		uid = vap->va_uid;
		gid = vap->va_gid;
		take_owner = (mask & ATTR_UID) && (uid == crgetuid(cr));
		take_group = (mask & ATTR_GID) &&
		    zfs_groupmember(zfsvfs, gid, cr);

		/*
		 * If both ATTR_UID and ATTR_GID are set then take_owner and
		 * take_group must both be set in order to allow taking
		 * ownership.
		 *
		 * Otherwise, send the check through secpolicy_vnode_setattr()
		 *
		 */

		if (((idmask == (ATTR_UID|ATTR_GID)) &&
		    take_owner && take_group) ||
		    ((idmask == ATTR_UID) && take_owner) ||
		    ((idmask == ATTR_GID) && take_group)) {
			if (zfs_zaccess(zp, ACE_WRITE_OWNER, 0,
			    skipaclchk, cr, mnt_ns) == 0) {
				/*
				 * Remove setuid/setgid for non-privileged users
				 */
				(void) native_setid_clear(vap, cr);
				trim_mask = (mask & (ATTR_UID|ATTR_GID));
			} else {
				need_policy =  TRUE;
			}
		} else {
			need_policy =  TRUE;
		}
	}

	mutex_enter(&zp->z_lock);
	oldva.va_mode = zp->z_mode;
	zfs_fuid_map_ids(zp, cr, &oldva.va_uid, &oldva.va_gid);
	if (mask & ATTR_XVATTR) {
		/*
		 * Update xvattr mask to include only those attributes
		 * that are actually changing.
		 *
		 * the bits will be restored prior to actually setting
		 * the attributes so the caller thinks they were set.
		 */
		if (XVA_ISSET_REQ(xvap, XAT_APPENDONLY)) {
			if (xoap->xoa_appendonly !=
			    ((zp->z_pflags & ZFS_APPENDONLY) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_APPENDONLY);
				XVA_SET_REQ(tmpxvattr, XAT_APPENDONLY);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_PROJINHERIT)) {
			if (xoap->xoa_projinherit !=
			    ((zp->z_pflags & ZFS_PROJINHERIT) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_PROJINHERIT);
				XVA_SET_REQ(tmpxvattr, XAT_PROJINHERIT);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NOUNLINK)) {
			if (xoap->xoa_nounlink !=
			    ((zp->z_pflags & ZFS_NOUNLINK) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NOUNLINK);
				XVA_SET_REQ(tmpxvattr, XAT_NOUNLINK);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_IMMUTABLE)) {
			if (xoap->xoa_immutable !=
			    ((zp->z_pflags & ZFS_IMMUTABLE) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_IMMUTABLE);
				XVA_SET_REQ(tmpxvattr, XAT_IMMUTABLE);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_NODUMP)) {
			if (xoap->xoa_nodump !=
			    ((zp->z_pflags & ZFS_NODUMP) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_NODUMP);
				XVA_SET_REQ(tmpxvattr, XAT_NODUMP);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_MODIFIED)) {
			if (xoap->xoa_av_modified !=
			    ((zp->z_pflags & ZFS_AV_MODIFIED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_MODIFIED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_MODIFIED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_QUARANTINED)) {
			if ((!S_ISREG(ip->z_mode) &&
			    xoap->xoa_av_quarantined) ||
			    xoap->xoa_av_quarantined !=
			    ((zp->z_pflags & ZFS_AV_QUARANTINED) != 0)) {
				need_policy = TRUE;
			} else {
				XVA_CLR_REQ(xvap, XAT_AV_QUARANTINED);
				XVA_SET_REQ(tmpxvattr, XAT_AV_QUARANTINED);
			}
		}

		if (XVA_ISSET_REQ(xvap, XAT_REPARSE)) {
			mutex_exit(&zp->z_lock);
			err = SET_ERROR(EPERM);
			goto out3;
		}

		if (need_policy == FALSE &&
		    (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP) ||
		    XVA_ISSET_REQ(xvap, XAT_OPAQUE))) {
			need_policy = TRUE;
		}
	}

	mutex_exit(&zp->z_lock);

	if (mask & ATTR_MODE) {
		if (zfs_zaccess(zp, ACE_WRITE_ACL, 0, skipaclchk, cr,
		    mnt_ns) == 0) {
			err = native_setid_admit(ip, vap, &oldva, cr);
			if (err)
				goto out3;
			trim_mask |= ATTR_MODE;
		} else {
			need_policy = TRUE;
		}
	}

	if (need_policy) {
		/*
		 * If trim_mask is set then take ownership
		 * has been granted or write_acl is present and user
		 * has the ability to modify mode.  In that case remove
		 * UID|GID and or MODE from mask so that
		 * secpolicy_vnode_setattr() doesn't revoke it.
		 */

		if (trim_mask) {
			saved_mask = vap->va_mask;
			vap->va_mask &= ~trim_mask;
		}
		err = native_setattr_admit(zp, vap, cr);
		if (err)
			goto out3;

		if (trim_mask)
			vap->va_mask |= saved_mask;
	}

	/*
	 * secpolicy_vnode_setattr, or take ownership may have
	 * changed va_mask
	 */
	mask = vap->va_mask;

	if ((mask & (ATTR_UID | ATTR_GID)) || projid != ZFS_INVALID_PROJID) {
		handle_eadir = B_TRUE;
		err = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
		    &xattr_obj, sizeof (xattr_obj));

		if (err == 0 && xattr_obj) {
			err = zfs_zget(ZTOZSB(zp), xattr_obj, &attrzp);
			if (err)
				goto out2;
		}
		if (mask & ATTR_UID) {
			new_kuid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_uid, cr, ZFS_OWNER, &fuidp);
			if (new_kuid != zp->z_uid &&
			    zfs_id_overquota(zfsvfs, DMU_USERUSED_OBJECT,
			    new_kuid)) {
				if (attrzp)
					zrele(attrzp);
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (mask & ATTR_GID) {
			new_kgid = zfs_fuid_create(zfsvfs,
			    (uint64_t)vap->va_gid, cr, ZFS_GROUP, &fuidp);
			if (new_kgid != zp->z_gid &&
			    zfs_id_overquota(zfsvfs, DMU_GROUPUSED_OBJECT,
			    new_kgid)) {
				if (attrzp)
					zrele(attrzp);
				err = SET_ERROR(EDQUOT);
				goto out2;
			}
		}

		if (projid != ZFS_INVALID_PROJID &&
		    zfs_id_overquota(zfsvfs, DMU_PROJECTUSED_OBJECT, projid)) {
			if (attrzp)
				zrele(attrzp);
			err = EDQUOT;
			goto out2;
		}
	}
	tx = dmu_tx_create(os);

	if (mask & ATTR_MODE) {
		uint64_t pmode = zp->z_mode;
		uint64_t acl_obj;
		new_mode = (pmode & S_IFMT) | (vap->va_mode & ~S_IFMT);

		if (ZTOZSB(zp)->z_acl_mode == ZFS_ACL_RESTRICTED &&
		    !(zp->z_pflags & ZFS_ACL_TRIVIAL)) {
			err = EPERM;
			goto out;
		}

		if ((err = zfs_acl_chmod_setattr(zp, &aclp, new_mode)))
			goto out;

		mutex_enter(&zp->z_lock);
		if (!zp->z_is_sa && ((acl_obj = zfs_external_acl(zp)) != 0)) {
			/*
			 * Are we upgrading ACL from old V0 format
			 * to V1 format?
			 */
			if (zfsvfs->z_version >= ZPL_VERSION_FUID &&
			    zfs_znode_acl_version(zp) ==
			    ZFS_ACL_VERSION_INITIAL) {
				dmu_tx_hold_free(tx, acl_obj, 0,
				    DMU_OBJECT_END);
				dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
				    0, aclp->z_acl_bytes);
			} else {
				dmu_tx_hold_write(tx, acl_obj, 0,
				    aclp->z_acl_bytes);
			}
		} else if (!zp->z_is_sa && aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, aclp->z_acl_bytes);
		}
		mutex_exit(&zp->z_lock);
		dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
	} else {
		if (((mask & ATTR_XVATTR) &&
		    XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP)) ||
		    (projid != ZFS_INVALID_PROJID &&
		    !(zp->z_pflags & ZFS_PROJID)))
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_TRUE);
		else
			dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	}

	if (attrzp) {
		dmu_tx_hold_sa(tx, attrzp->z_sa_hdl, B_FALSE);
	}

	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);

	zfs_sa_upgrade_txholds(tx, zp);

	err = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (err)
		goto out;

	count = 0;
	/*
	 * Set each attribute requested.
	 * We group settings according to the locks they need to acquire.
	 *
	 * Note: you cannot set ctime directly, although it will be
	 * updated as a side-effect of calling this function.
	 */

	if (projid != ZFS_INVALID_PROJID && !(zp->z_pflags & ZFS_PROJID)) {
		/*
		 * For the existed object that is upgraded from old system,
		 * its on-disk layout has no slot for the project ID attribute.
		 * But quota accounting logic needs to access related slots by
		 * offset directly. So we need to adjust old objects' layout
		 * to make the project ID to some unified and fixed offset.
		 */
		if (attrzp)
			err = sa_add_projid(attrzp->z_sa_hdl, tx, projid);
		if (err == 0)
			err = sa_add_projid(zp->z_sa_hdl, tx, projid);

		if (unlikely(err == EEXIST))
			err = 0;
		else if (err != 0)
			goto out;
		else
			projid = ZFS_INVALID_PROJID;
	}

	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_enter(&zp->z_acl_lock);
	mutex_enter(&zp->z_lock);

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (attrzp) {
		/*
		 * attrzp is zp's hidden xattr directory, so the second
		 * znode lock acquisition is nested rather than recursive.
		 */
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_enter(&attrzp->z_acl_lock);
		mutex_enter(&attrzp->z_lock);
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_FLAGS(zfsvfs), NULL, &attrzp->z_pflags,
		    sizeof (attrzp->z_pflags));
		if (projid != ZFS_INVALID_PROJID) {
			attrzp->z_projid = projid;
			SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
			    SA_ZPL_PROJID(zfsvfs), NULL, &attrzp->z_projid,
			    sizeof (attrzp->z_projid));
		}
	}

	if (mask & (ATTR_UID|ATTR_GID)) {

		if (mask & ATTR_UID) {
			zp->z_uid = new_kuid;
			new_uid = zp->z_uid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_UID(zfsvfs), NULL,
			    &new_uid, sizeof (new_uid));
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_UID(zfsvfs), NULL, &new_uid,
				    sizeof (new_uid));
				attrzp->z_uid = new_uid;
			}
		}

		if (mask & ATTR_GID) {
			zp->z_gid = new_kgid;
			new_gid = zp->z_gid;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_GID(zfsvfs),
			    NULL, &new_gid, sizeof (new_gid));
			if (attrzp) {
				SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
				    SA_ZPL_GID(zfsvfs), NULL, &new_gid,
				    sizeof (new_gid));
				attrzp->z_gid = new_kgid;
			}
		}
		if (!(mask & ATTR_MODE)) {
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs),
			    NULL, &new_mode, sizeof (new_mode));
			new_mode = zp->z_mode;
		}
		err = zfs_acl_chown_setattr(zp);
		ASSERT0(err);
		if (attrzp) {
			err = zfs_acl_chown_setattr(attrzp);
			ASSERT0(err);
		}
	}

	if (mask & ATTR_MODE) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MODE(zfsvfs), NULL,
		    &new_mode, sizeof (new_mode));
		zp->z_mode = new_mode;
		ASSERT3P(aclp, !=, NULL);
		err = zfs_aclset_common(zp, aclp, cr, tx);
		ASSERT0(err);
		if (zp->z_acl_cached)
			zfs_acl_free(zp->z_acl_cached);
		zp->z_acl_cached = aclp;
		aclp = NULL;
	}

	if ((mask & ATTR_ATIME) || zp->z_atime_dirty) {
		zp->z_atime_dirty = B_FALSE;
		if (mask & ATTR_ATIME) zp->z_atime = vap->va_atime;
		inode_timespec_t tmp_atime = zp->z_atime;
		ZFS_TIME_ENCODE(&tmp_atime, atime);
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_ATIME(zfsvfs), NULL,
		    &atime, sizeof (atime));
	}

	if (mask & (ATTR_MTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_mtime, mtime);
		zp->z_mtime = vap->va_mtime;

		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
		    mtime, sizeof (mtime));
	}

	if (mask & (ATTR_CTIME | ATTR_SIZE)) {
		ZFS_TIME_ENCODE(&vap->va_ctime, ctime);
		zp->z_ctime = vap->va_ctime;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
	}

	if (projid != ZFS_INVALID_PROJID) {
		zp->z_projid = projid;
		SA_ADD_BULK_ATTR(bulk, count,
		    SA_ZPL_PROJID(zfsvfs), NULL, &zp->z_projid,
		    sizeof (zp->z_projid));
	}

	if (attrzp && mask) {
		SA_ADD_BULK_ATTR(xattr_bulk, xattr_count,
		    SA_ZPL_CTIME(zfsvfs), NULL, &ctime,
		    sizeof (ctime));
	}

	/*
	 * Do this after setting timestamps to prevent timestamp
	 * update from toggling bit
	 */

	if (xoap && (mask & ATTR_XVATTR)) {

		/*
		 * restore trimmed off masks
		 * so that return masks can be set for caller.
		 */

		if (XVA_ISSET_REQ(tmpxvattr, XAT_APPENDONLY)) {
			XVA_SET_REQ(xvap, XAT_APPENDONLY);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NOUNLINK)) {
			XVA_SET_REQ(xvap, XAT_NOUNLINK);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_IMMUTABLE)) {
			XVA_SET_REQ(xvap, XAT_IMMUTABLE);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_NODUMP)) {
			XVA_SET_REQ(xvap, XAT_NODUMP);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_MODIFIED)) {
			XVA_SET_REQ(xvap, XAT_AV_MODIFIED);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_AV_QUARANTINED)) {
			XVA_SET_REQ(xvap, XAT_AV_QUARANTINED);
		}
		if (XVA_ISSET_REQ(tmpxvattr, XAT_PROJINHERIT)) {
			XVA_SET_REQ(xvap, XAT_PROJINHERIT);
		}

		if (XVA_ISSET_REQ(xvap, XAT_AV_SCANSTAMP))
			ASSERT(S_ISREG(ip->z_mode));

		zfs_xvattr_set(zp, xvap, tx);
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	if (mask != 0) {
		zfs_log_setattr(zilog, tx, TX_SETATTR, zp, vap, mask, fuidp);
		/*
		 * Ensure that the z_seq is always incremented on setattr
		 * operation. This is required for change accounting for
		 * NFS clients.
		 *
		 * ATTR_MODE already increments via zfs_acl_chmod_setattr.
		 * ATTR_SIZE already increments via zfs_freesp.
		 */
		if (!(mask & (ATTR_MODE | ATTR_SIZE)))
			zp->z_seq++;
	}

	mutex_exit(&zp->z_lock);
	if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
		mutex_exit(&zp->z_acl_lock);

	if (attrzp) {
		if (mask & (ATTR_UID|ATTR_GID|ATTR_MODE))
			mutex_exit(&attrzp->z_acl_lock);
		mutex_exit(&attrzp->z_lock);
	}
out:
	if (err == 0 && xattr_count > 0) {
		err2 = sa_bulk_update(attrzp->z_sa_hdl, xattr_bulk,
		    xattr_count, tx);
		ASSERT0(err2);
	}

	if (aclp)
		zfs_acl_free(aclp);

	if (fuidp) {
		zfs_fuid_info_free(fuidp);
		fuidp = NULL;
	}

	if (err) {
		dmu_tx_abort(tx);
		if (attrzp)
			zrele(attrzp);
		if (err == ERESTART)
			goto top;
	} else {
		if (count > 0)
			err2 = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
		dmu_tx_commit(tx);
		if (attrzp) {
			if (err2 == 0 && handle_eadir)
				err = zfs_setattr_dir(attrzp);
			zrele(attrzp);
		}
		zfs_znode_update_vfs(zp);
	}

out2:
	if (err == 0 && os->os_sync == ZFS_SYNC_ALWAYS)
		err = zil_commit(zilog, 0);

out3:
	kmem_free(xattr_bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(bulk, sizeof (sa_bulk_attr_t) * bulks);
	kmem_free(tmpxvattr, sizeof (xvattr_t));
	zfs_exit(zfsvfs, FTAG);
	return (err);
}
