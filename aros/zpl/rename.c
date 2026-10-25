/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream zfs_rename and ancestry locks from zfs_vnops_os.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2013, 2016 by Delphix.
 * Native znodes/credentials; no Linux inode or implicit VFS validation.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/u8_textprep.h>
typedef struct zfs_zlock {
	krwlock_t	*zl_rwlock;	/* lock we acquired */
	znode_t		*zl_znode;	/* znode we held */
	struct zfs_zlock *zl_next;	/* next in list */
} zfs_zlock_t;

/*
 * Drop locks and release vnodes that were held by zfs_rename_lock().
 */
static void
zfs_rename_unlock(zfs_zlock_t **zlpp)
{
	zfs_zlock_t *zl;

	while ((zl = *zlpp) != NULL) {
		if (zl->zl_znode != NULL)
			zfs_zrele_async(zl->zl_znode);
		rw_exit(zl->zl_rwlock);
		*zlpp = zl->zl_next;
		kmem_free(zl, sizeof (*zl));
	}
}

/*
 * Search back through the directory tree, using the ".." entries.
 * Lock each directory in the chain to prevent concurrent renames.
 * Fail any attempt to move a directory into one of its own descendants.
 * XXX - z_parent_lock can overlap with map or grow locks
 */
static int
zfs_rename_lock(znode_t *szp, znode_t *tdzp, znode_t *sdzp, zfs_zlock_t **zlpp)
{
	zfs_zlock_t	*zl;
	znode_t		*zp = tdzp;
	uint64_t	rootid = ZTOZSB(zp)->z_root;
	uint64_t	oidp = zp->z_id;
	krwlock_t	*rwlp = &szp->z_parent_lock;
	krw_t		rw = RW_WRITER;

	/*
	 * First pass write-locks szp and compares to zp->z_id.
	 * Later passes read-lock zp and compare to zp->z_parent.
	 */
	do {
		if (!rw_tryenter(rwlp, rw)) {
			/*
			 * Another thread is renaming in this path.
			 * Note that if we are a WRITER, we don't have any
			 * parent_locks held yet.
			 */
			if (rw == RW_READER && zp->z_id > szp->z_id) {
				/*
				 * Drop our locks and restart
				 */
				zfs_rename_unlock(&zl);
				*zlpp = NULL;
				zp = tdzp;
				oidp = zp->z_id;
				rwlp = &szp->z_parent_lock;
				rw = RW_WRITER;
				continue;
			} else {
				/*
				 * Wait for other thread to drop its locks
				 */
				rw_enter(rwlp, rw);
			}
		}

		zl = kmem_alloc(sizeof (*zl), KM_SLEEP);
		zl->zl_rwlock = rwlp;
		zl->zl_znode = NULL;
		zl->zl_next = *zlpp;
		*zlpp = zl;

		if (oidp == szp->z_id)		/* We're a descendant of szp */
			return (SET_ERROR(EINVAL));

		if (oidp == rootid)		/* We've hit the top */
			return (0);

		if (rw == RW_READER) {		/* i.e. not the first pass */
			int error = zfs_zget(ZTOZSB(zp), oidp, &zp);
			if (error)
				return (error);
			zl->zl_znode = zp;
		}
		int error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PARENT(ZTOZSB(zp)),
		    &oidp, sizeof (oidp));
		/* Never walk using the previous object ID after an SA failure:
		 * it may reacquire our own parent lock. The caller releases the
		 * accumulated ancestry locks and references on this error path. */
		if (error)
			return (error);
		rwlp = &zp->z_parent_lock;
		rw = RW_READER;

	} while (zp->z_id != sdzp->z_id);

	return (0);
}

/*
 * Move an entry from the provided source directory to the target
 * directory.  Change the entry name as indicated.
 *
 *	IN:	sdzp	- Source directory containing the "old entry".
 *		snm	- Old entry name.
 *		tdzp	- Target directory to contain the "new entry".
 *		tnm	- New entry name.
 *		cr	- credentials of caller.
 *		flags	- case flags
 *		rflags  - RENAME_* flags
 *		wa_vap  - attributes for RENAME_WHITEOUT (must be a char 0:0).
 *		mnt_ns	- user namespace of the mount
 *
 *	RETURN:	0 on success, error code on failure.
 *
 * Timestamps:
 *	sdzp,tdzp - ctime|mtime updated
 */
static int
rename_impl(znode_t *sdzp, char *snm, znode_t *tdzp, char *tnm,
    cred_t *cr, int flags, uint64_t rflags, vattr_t *wo_vap, zidmap_t *mnt_ns,
    const struct oz_rename_guard *guard)
{
	if (!sdzp || !tdzp || !snm || !tnm || !*snm || !*tnm ||
	    strchr(snm, '/') || strchr(tnm, '/')) return (EINVAL);
	if (!cr) return (EACCES);
	if (!S_ISDIR(sdzp->z_mode) || !S_ISDIR(tdzp->z_mode)) return (ENOTDIR);
	if (ZTOZSB(sdzp) != ZTOZSB(tdzp)) return (EXDEV);
	if (mnt_ns || (flags & ~FIGNORECASE) || wo_vap ||
	    (rflags & RENAME_WHITEOUT)) return (ENOTSUP);
	if (!ZTOZSB(sdzp)->z_longname && strlen(tnm) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);
	znode_t		*szp, *tzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(sdzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*sdl, *tdl;
	dmu_tx_t	*tx;
	zfs_zlock_t	*zl;
	int		cmp, serr, terr;
	int		error = 0;
	int		zflg = 0;
	boolean_t	waited = B_FALSE;
	/* Needed for whiteout inode creation. */
	boolean_t	fuid_dirtied;
	zfs_acl_ids_t	acl_ids;
	boolean_t	have_acl = B_FALSE;
	znode_t		*wzp = NULL;


	if (snm == NULL || tnm == NULL)
		return (SET_ERROR(EINVAL));

	if (rflags & ~(RENAME_NOREPLACE | RENAME_EXCHANGE | RENAME_WHITEOUT))
		return (SET_ERROR(EINVAL));

	/* Already checked by Linux VFS, but just to make sure. */
	if (rflags & RENAME_EXCHANGE &&
	    (rflags & (RENAME_NOREPLACE | RENAME_WHITEOUT)))
		return (SET_ERROR(EINVAL));

	/*
	 * Make sure we only get wo_vap iff. RENAME_WHITEOUT and that it's the
	 * right kind of vattr_t for the whiteout file. These are set
	 * internally by ZFS so should never be incorrect.
	 */
	VERIFY_EQUIV(rflags & RENAME_WHITEOUT, wo_vap != NULL);
	VERIFY_IMPLY(wo_vap, wo_vap->va_mode == S_IFCHR);
	VERIFY_IMPLY(wo_vap, wo_vap->va_rdev == 0);

	if ((error = zfs_enter_verify_zp(zfsvfs, sdzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;
	if (zfs_is_readonly(zfsvfs) || !zilog) {
		error = zfs_is_readonly(zfsvfs) ? EROFS : ENOTSUP;
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	if ((error = zfs_verify_zp(tdzp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * We check i_sb because snapshots and the ctldir must have different
	 * super blocks.
	 */
	if (ZTOZSB(tdzp) != ZTOZSB(sdzp) || tdzp->z_is_ctldir) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
	}

	if (zfsvfs->z_utf8 && u8_validate(tnm,
	    strlen(tnm), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}

	if (flags & FIGNORECASE)
		zflg |= ZCILOOK;

top:
	szp = NULL;
	tzp = NULL;
	zl = NULL;

	/*
	 * This is to prevent the creation of links into attribute space
	 * by renaming a linked file into/outof an attribute directory.
	 * See the comment in zfs_link() for why this is considered bad.
	 */
	if ((tdzp->z_pflags & ZFS_XATTR) != (sdzp->z_pflags & ZFS_XATTR)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	/*
	 * Lock source and target directory entries.  To prevent deadlock,
	 * a lock ordering must be defined.  We lock the directory with
	 * the smallest object id first, or if it's a tie, the one with
	 * the lexically first name.
	 */
	if (sdzp->z_id < tdzp->z_id) {
		cmp = -1;
	} else if (sdzp->z_id > tdzp->z_id) {
		cmp = 1;
	} else {
		/*
		 * First compare the two name arguments without
		 * considering any case folding.
		 */
		int nofold = (zfsvfs->z_norm & ~U8_TEXTPREP_TOUPPER);

		cmp = u8_strcmp(snm, tnm, 0, nofold, U8_UNICODE_LATEST, &error);
		ASSERT(error == 0 || !zfsvfs->z_utf8);
		if (cmp == 0) {
			/*
			 * POSIX: "If the old argument and the new argument
			 * both refer to links to the same existing file,
			 * the rename() function shall return successfully
			 * and perform no other action."
			 */
			/* Linux VFS normally establishes source existence first. */
			zfs_dirlock_t *same_dl;
			znode_t *same = NULL;
			error = zfs_zaccess(sdzp, ACE_EXECUTE, 0, B_FALSE, cr, NULL);
			if (!error) {
				error = zfs_dirent_lock(&same_dl, sdzp, snm, &same,
				    ZEXISTS | zflg, NULL, NULL);
				if (!error) {
					zfs_dirent_unlock(same_dl);
					zrele(same);
					if (rflags & RENAME_NOREPLACE) error = EEXIST;
				}
			}
			zfs_exit(zfsvfs, FTAG);
			return (error);
		}
		/*
		 * If the file system is case-folding, then we may
		 * have some more checking to do.  A case-folding file
		 * system is either supporting mixed case sensitivity
		 * access or is completely case-insensitive.  Note
		 * that the file system is always case preserving.
		 *
		 * In mixed sensitivity mode case sensitive behavior
		 * is the default.  FIGNORECASE must be used to
		 * explicitly request case insensitive behavior.
		 *
		 * If the source and target names provided differ only
		 * by case (e.g., a request to rename 'tim' to 'Tim'),
		 * we will treat this as a special case in the
		 * case-insensitive mode: as long as the source name
		 * is an exact match, we will allow this to proceed as
		 * a name-change request.
		 */
		if ((zfsvfs->z_case == ZFS_CASE_INSENSITIVE ||
		    (zfsvfs->z_case == ZFS_CASE_MIXED &&
		    flags & FIGNORECASE)) &&
		    u8_strcmp(snm, tnm, 0, zfsvfs->z_norm, U8_UNICODE_LATEST,
		    &error) == 0) {
			/*
			 * case preserving rename request, require exact
			 * name matches
			 */
			zflg |= ZCIEXACT;
			zflg &= ~ZCILOOK;
		}
	}

	/*
	 * If the source and destination directories are the same, we should
	 * grab the z_name_lock of that directory only once.
	 */
	if (sdzp == tdzp) {
		zflg |= ZHAVELOCK;
		rw_enter(&sdzp->z_name_lock, RW_READER);
	}

	if (cmp < 0) {
		serr = zfs_dirent_lock(&sdl, sdzp, snm, &szp,
		    ZEXISTS | zflg, NULL, NULL);
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, ZRENAMING | zflg, NULL, NULL);
	} else {
		terr = zfs_dirent_lock(&tdl,
		    tdzp, tnm, &tzp, zflg, NULL, NULL);
		serr = zfs_dirent_lock(&sdl,
		    sdzp, snm, &szp, ZEXISTS | ZRENAMING | zflg,
		    NULL, NULL);
	}

	if (serr) {
		/*
		 * Source entry invalid or not there.
		 */
		if (!terr) {
			zfs_dirent_unlock(tdl);
			if (tzp)
				zrele(tzp);
		}

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(snm, "..") == 0)
			serr = EINVAL;
		zfs_exit(zfsvfs, FTAG);
		return (serr);
	}
	if (terr) {
		zfs_dirent_unlock(sdl);
		zrele(szp);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (strcmp(tnm, "..") == 0)
			terr = EINVAL;
		zfs_exit(zfsvfs, FTAG);
		return (terr);
	}
	if (guard) {
		if (!tzp || szp->z_id != guard->source ||
		    szp->z_gen != guard->source_generation ||
		    tzp->z_id != guard->target || tzp->z_gen != guard->target_generation) {
			error = ESTALE; goto out;
		}
		if (!S_ISREG(szp->z_mode) || !S_ISREG(tzp->z_mode) ||
		    szp->z_size || szp->z_links != 1) { error = EBUSY; goto out; }
		if ((error = guard->check(guard->context))) goto out;
	}

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow renames into our tree when the project
	 * IDs are the same.
	 */
	if (tdzp->z_pflags & ZFS_PROJINHERIT &&
	    tdzp->z_projid != szp->z_projid) {
		error = SET_ERROR(EXDEV);
		goto out;
	}

	/*
	 * Must have write access at the source to remove the old entry
	 * and write access at the target to create the new entry.
	 * Note that if target and source are the same, this can be
	 * done in a single check.
	 */
	/* The prepared source is internal, not a caller-requested unlink. Check
	 * the requested replacement's delete/add rights, not extra delete rights
	 * on the newly inherited source ACL. No credential elevation is used. */
	if (guard) {
		error = zfs_zaccess_delete(tdzp, tzp, cr, mnt_ns);
		if (!error) error = zfs_zaccess(tdzp, ACE_ADD_FILE, 0, B_FALSE, cr, mnt_ns);
	} else error = zfs_zaccess_rename(sdzp, szp, tdzp, tzp, cr, mnt_ns);
	if (error)
		goto out;
	if ((rflags & RENAME_EXCHANGE) &&
	    (error = zfs_zaccess(sdzp,
	    (tzp && S_ISDIR(tzp->z_mode)) ? ACE_ADD_SUBDIRECTORY : ACE_ADD_FILE,
	    0, B_FALSE, cr, mnt_ns)))
		goto out;

	/* Siblings keep the same parent during exchange. Their existing name
	 * locks serialize both entries and no ancestry edge changes. Cross-parent
	 * directory exchange still needs a native two-tree ancestry contract. */
	if ((rflags & RENAME_EXCHANGE) && sdzp != tdzp &&
	    (S_ISDIR(szp->z_mode) || (tzp && S_ISDIR(tzp->z_mode)))) {
		error = ENOTSUP;
		goto out;
	}
	if (S_ISDIR(szp->z_mode) &&
	    !((rflags & RENAME_EXCHANGE) && sdzp == tdzp)) {
		/*
		 * Check to make sure rename is valid.
		 * Can't do a move like this: /usr/a/b to /usr/a/b/c/d
		 */
		if ((error = zfs_rename_lock(szp, tdzp, sdzp, &zl)))
			goto out;
	}

	/*
	 * Does target exist?
	 */
	if (tzp) {
		if (rflags & RENAME_NOREPLACE) {
			error = SET_ERROR(EEXIST);
			goto out;
		}
		/*
		 * Source and target must be the same type (unless exchanging).
		 */
		if (!(rflags & RENAME_EXCHANGE)) {
			boolean_t s_is_dir = S_ISDIR(szp->z_mode) != 0;
			boolean_t t_is_dir = S_ISDIR(tzp->z_mode) != 0;

			if (s_is_dir != t_is_dir) {
				error = SET_ERROR(s_is_dir ? ENOTDIR : EISDIR);
				goto out;
			}
		}
		/*
		 * POSIX dictates that when the source and target
		 * entries refer to the same file object, rename
		 * must do nothing and exit without error.
		 */
		if (szp->z_id == tzp->z_id) {
			error = 0;
			goto out;
		}
	} else if (rflags & RENAME_EXCHANGE) {
		/* Target must exist for RENAME_EXCHANGE. */
		error = SET_ERROR(ENOENT);
		goto out;
	}

	/* Set up inode creation for RENAME_WHITEOUT. */
	if (rflags & RENAME_WHITEOUT) {
		/*
		 * Whiteout files are not regular files or directories, so to
		 * match zfs_create() we do not inherit the project id.
		 */
		uint64_t wo_projid = ZFS_DEFAULT_PROJID;

		error = zfs_zaccess(sdzp, ACE_ADD_FILE, 0, B_FALSE, cr, mnt_ns);
		if (error)
			goto out;

		if (!have_acl) {
			error = zfs_acl_ids_create(sdzp, 0, wo_vap, cr, NULL,
			    &acl_ids, mnt_ns);
			if (error)
				goto out;
			have_acl = B_TRUE;
		}

		if (zfs_acl_ids_overquota(zfsvfs, &acl_ids, wo_projid)) {
			error = SET_ERROR(EDQUOT);
			goto out;
		}
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, sdzp->z_id,
	    (rflags & RENAME_EXCHANGE) ? TRUE : FALSE, snm);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, tnm);
	if (sdzp != tdzp) {
		dmu_tx_hold_sa(tx, tdzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tdzp);
	}
	if (tzp) {
		dmu_tx_hold_sa(tx, tzp->z_sa_hdl, B_FALSE);
		zfs_sa_upgrade_txholds(tx, tzp);
	}
	if (rflags & RENAME_WHITEOUT) {
		dmu_tx_hold_sa_create(tx, acl_ids.z_aclp->z_acl_bytes +
		    ZFS_SA_BASE_ATTR_SIZE);

		dmu_tx_hold_zap(tx, sdzp->z_id, TRUE, snm);
		dmu_tx_hold_sa(tx, sdzp->z_sa_hdl, B_FALSE);
		if (!zfsvfs->z_use_sa &&
		    acl_ids.z_aclp->z_acl_bytes > ZFS_ACE_SPACE) {
			dmu_tx_hold_write(tx, DMU_NEW_OBJECT,
			    0, acl_ids.z_aclp->z_acl_bytes);
		}
	}
	fuid_dirtied = zfsvfs->z_fuid_dirty;
	if (fuid_dirtied)
		zfs_fuid_txhold(zfsvfs, tx);
	zfs_sa_upgrade_txholds(tx, szp);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	error = dmu_tx_assign(tx,
	    (waited ? DMU_TX_NOTHROTTLE : 0) | DMU_TX_NOWAIT);
	if (error) {
		if (zl != NULL)
			zfs_rename_unlock(&zl);
		zfs_dirent_unlock(sdl);
		zfs_dirent_unlock(tdl);

		if (sdzp == tdzp)
			rw_exit(&sdzp->z_name_lock);

		if (error == ERESTART) {
			waited = B_TRUE;
			dmu_tx_wait(tx);
			dmu_tx_abort(tx);
			zrele(szp);
			if (tzp)
				zrele(tzp);
			goto top;
		}
		dmu_tx_abort(tx);
		zrele(szp);
		if (tzp)
			zrele(tzp);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * Unlink the source.
	 */
	szp->z_pflags |= ZFS_AV_MODIFIED;
	if (tdzp->z_pflags & ZFS_PROJINHERIT)
		szp->z_pflags |= ZFS_PROJINHERIT;

	error = sa_update(szp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
	    (void *)&szp->z_pflags, sizeof (uint64_t), tx);
	VERIFY0(error);

	error = zfs_link_destroy(sdl, szp, tx, ZRENAMING, NULL);
	if (error)
		goto commit;

	/*
	 * Unlink the target.
	 */
	if (tzp) {
		int tzflg = zflg;

		if (rflags & RENAME_EXCHANGE) {
			/* This inode will be re-linked soon. */
			tzflg |= ZRENAMING;

			tzp->z_pflags |= ZFS_AV_MODIFIED;
			if (sdzp->z_pflags & ZFS_PROJINHERIT)
				tzp->z_pflags |= ZFS_PROJINHERIT;

			error = sa_update(tzp->z_sa_hdl, SA_ZPL_FLAGS(zfsvfs),
			    (void *)&tzp->z_pflags, sizeof (uint64_t), tx);
			ASSERT0(error);
		}
		error = zfs_link_destroy(tdl, tzp, tx, tzflg, NULL);
		if (error)
			goto commit_link_szp;
	}

	/*
	 * Create the new target links:
	 *   * We always link the target.
	 *   * RENAME_EXCHANGE: Link the old target to the source.
	 *   * RENAME_WHITEOUT: Create a whiteout inode in-place of the source.
	 */
	error = zfs_link_create(tdl, szp, tx, ZRENAMING);
	if (error) {
		/*
		 * If we have removed the existing target, a subsequent call to
		 * zfs_link_create() to add back the same entry, but with a new
		 * dnode (szp), should not fail.
		 */
		ASSERT0P(tzp);
		goto commit_link_tzp;
	}

	switch (rflags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
	case RENAME_EXCHANGE:
		error = zfs_link_create(sdl, tzp, tx, ZRENAMING);
		/*
		 * The same argument as zfs_link_create() failing for
		 * szp applies here, since the source directory must
		 * have had an entry we are replacing.
		 */
		ASSERT0(error);
		if (error)
			goto commit_unlink_td_szp;
		break;
	case RENAME_WHITEOUT:
		zfs_mknode(sdzp, wo_vap, tx, cr, 0, &wzp, &acl_ids);
		error = zfs_link_create(sdl, wzp, tx, ZNEW);
		if (error) {
			zfs_znode_delete(wzp, tx);
			goto commit_unlink_td_szp;
		}
		break;
	}

	if (fuid_dirtied)
		zfs_fuid_sync(zfsvfs, tx);

	switch (rflags & (RENAME_EXCHANGE | RENAME_WHITEOUT)) {
	case RENAME_EXCHANGE:
		zfs_log_rename_exchange(zilog, tx,
		    (flags & FIGNORECASE ? TX_CI : 0), sdzp, sdl->dl_name,
		    tdzp, tdl->dl_name, szp);
		break;
	case RENAME_WHITEOUT:
		zfs_log_rename_whiteout(zilog, tx,
		    (flags & FIGNORECASE ? TX_CI : 0), sdzp, sdl->dl_name,
		    tdzp, tdl->dl_name, szp, wzp);
		break;
	default:
		ASSERT0(rflags & ~RENAME_NOREPLACE);
		zfs_log_rename(zilog, tx, (flags & FIGNORECASE ? TX_CI : 0),
		    sdzp, sdl->dl_name, tdzp, tdl->dl_name, szp);
		break;
	}

commit:
	dmu_tx_commit(tx);
out:
	if (have_acl)
		zfs_acl_ids_free(&acl_ids);

	zfs_znode_update_vfs(sdzp);
	if (sdzp == tdzp)
		rw_exit(&sdzp->z_name_lock);

	if (sdzp != tdzp)
		zfs_znode_update_vfs(tdzp);

	zfs_znode_update_vfs(szp);
	zrele(szp);
	if (wzp) {
		zfs_znode_update_vfs(wzp);
		zrele(wzp);
	}
	if (tzp) {
		zfs_znode_update_vfs(tzp);
		zrele(tzp);
	}

	if (zl != NULL)
		zfs_rename_unlock(&zl);

	zfs_dirent_unlock(sdl);
	zfs_dirent_unlock(tdl);

	if (error == 0 && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
		error = zil_commit(zilog, 0);

	zfs_exit(zfsvfs, FTAG);
	return (error);

	/*
	 * Clean-up path for broken link state.
	 *
	 * At this point we are in a (very) bad state, so we need to do our
	 * best to correct the state. In particular, all of the nlinks are
	 * wrong because we were destroying and creating links with ZRENAMING.
	 *
	 * In some form, all of these operations have to resolve the state:
	 *
	 *  * link_destroy() *must* succeed. Fortunately, this is very likely
	 *    since we only just created it.
	 *
	 *  * link_create()s are allowed to fail (though they shouldn't because
	 *    we only just unlinked them and are putting the entries back
	 *    during clean-up). But if they fail, we can just forcefully drop
	 *    the nlink value to (at the very least) avoid broken nlink values
	 *    -- though in the case of non-empty directories we will have to
	 *    panic (otherwise we'd have a leaked directory with a broken ..).
	 */
commit_unlink_td_szp:
	VERIFY0(zfs_link_destroy(tdl, szp, tx, ZRENAMING, NULL));
commit_link_tzp:
	if (tzp) {
		if (zfs_link_create(tdl, tzp, tx, ZRENAMING))
			VERIFY0(zfs_drop_nlink(tzp, tx, NULL));
	}
commit_link_szp:
	if (zfs_link_create(sdl, szp, tx, ZRENAMING))
		VERIFY0(zfs_drop_nlink(szp, tx, NULL));
	goto commit;
}

int
zfs_rename(znode_t *sdzp,char *snm,znode_t *tdzp,char *tnm,cred_t *cr,
    int flags,uint64_t rflags,vattr_t *wo_vap,zidmap_t *mnt_ns)
{return rename_impl(sdzp,snm,tdzp,tnm,cr,flags,rflags,wo_vap,mnt_ns,NULL);}

int
oz_zfs_replace(znode_t *parent,char *source,char *target,cred_t *cr,
    const struct oz_rename_guard *guard)
{
    if(!guard||!guard->check||!guard->source||!guard->target||
        !guard->source_generation||!guard->target_generation||
        guard->source==guard->target||!source||!target||!strcmp(source,target))return EINVAL;
    return rename_impl(parent,source,parent,target,cr,0,0,NULL,NULL,guard);
}
