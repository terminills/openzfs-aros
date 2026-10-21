/* SPDX-License-Identifier: CDDL-1.0 */
/*
 * Upstream zfs_dir.c mutation/link-count helpers and zfs_link from
 * zfs_vnops_os.c, adapted to native cached znodes instead of Linux inodes.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2013, 2016 by Delphix. All rights reserved.
 * Copyright 2017 Nexenta Systems, Inc.
 * Internal helpers require caller-owned transactions, entry locks, mount and
 * znode lifetimes. Final unlinked-object reclamation remains separate work.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/zfeature.h>
#include <sys/txg.h>
#include <sys/u8_textprep.h>

void
zfs_unlinked_add(znode_t *zp, dmu_tx_t *tx)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);

	ASSERT(zp->z_unlinked);
	ASSERT0(zp->z_links);

	VERIFY3U(0, ==,
	    zap_add_int(zfsvfs->z_os, zfsvfs->z_unlinkedobj, zp->z_id, tx));

	dataset_kstats_update_nunlinks_kstat(&zfsvfs->z_kstat, 1);
}

static uint64_t
zfs_dirent(znode_t *zp, uint64_t mode)
{
	uint64_t de = zp->z_id;

	if (ZTOZSB(zp)->z_version >= ZPL_VERSION_DIRENT_TYPE)
		de |= IFTODT(mode) << 60;
	return (de);
}

/*
 * Link zp into dl.  Can fail in the following cases :
 * - if zp has been unlinked.
 * - if the number of entries with the same hash (aka. colliding entries)
 *    exceed the capacity of a leaf-block of fatzap and splitting of the
 *    leaf-block does not help.
 */
int
zfs_link_create(zfs_dirlock_t *dl, znode_t *zp, dmu_tx_t *tx, int flag)
{
	znode_t *dzp = dl->dl_dzp;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	uint64_t value;
	int zp_is_dir = S_ISDIR(zp->z_mode);
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	uint64_t links;
	int count = 0;
	int error;

	mutex_enter(&zp->z_lock);

	if (!(flag & ZRENAMING)) {
		if (zp->z_unlinked) {	/* no new links to unlinked zp */
			ASSERT(!(flag & (ZNEW | ZEXISTS)));
			mutex_exit(&zp->z_lock);
			return (SET_ERROR(ENOENT));
		}
		if (!(flag & ZNEW)) {
			/* Linux VFS caps links at this upstream limit. The native
			 * path must enforce it while holding the znode lock too. */
			if (zp->z_links >= ((1ULL << 31) - 1)) {
				mutex_exit(&zp->z_lock);
				return (SET_ERROR(EMLINK));
			}
			/*
			 * ZNEW nodes come from zfs_mknode() where the link
			 * count has already been initialised
			 */
			++zp->z_links;
			links = zp->z_links;
			SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
			    NULL, &links, sizeof (links));
		}
	}

	value = zfs_dirent(zp, zp->z_mode);
	error = zap_add(ZTOZSB(zp)->z_os, dzp->z_id, dl->dl_name, 8, 1,
	    &value, tx);

	/*
	 * zap_add could fail to add the entry if it exceeds the capacity of the
	 * leaf-block and zap_leaf_split() failed to help.
	 * The caller of this routine is responsible for failing the transaction
	 * which will rollback the SA updates done above.
	 */
	if (error != 0) {
		if (!(flag & ZRENAMING) && !(flag & ZNEW))
			--zp->z_links;
		mutex_exit(&zp->z_lock);
		return (error);
	}

	/*
	 * If we added a longname activate the SPA_FEATURE_LONGNAME.
	 */
	if (strlen(dl->dl_name) >= ZAP_MAXNAMELEN) {
		dsl_dataset_t *ds = dmu_objset_ds(zfsvfs->z_os);
		ds->ds_feature_activation[SPA_FEATURE_LONGNAME] =
		    (void *)B_TRUE;
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_PARENT(zfsvfs), NULL,
	    &dzp->z_id, sizeof (dzp->z_id));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &zp->z_pflags, sizeof (zp->z_pflags));

	if (!(flag & ZNEW)) {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
		    ctime, sizeof (ctime));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime,
		    ctime);
	}
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	mutex_exit(&zp->z_lock);

	mutex_enter(&dzp->z_lock);
	dzp->z_size++;
	if (zp_is_dir)
		++dzp->z_links;
	links = dzp->z_links;
	count = 0;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs), NULL,
	    &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs), NULL,
	    &links, sizeof (links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL,
	    mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL,
	    ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs), NULL,
	    &dzp->z_pflags, sizeof (dzp->z_pflags));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);
	mutex_exit(&dzp->z_lock);

	return (0);
}

/*
 * The match type in the code for this function should conform to:
 *
 * ------------------------------------------------------------------------
 * fs type  | z_norm      | lookup type | match type
 * ---------|-------------|-------------|----------------------------------
 * CS !norm | 0           |           0 | 0 (exact)
 * CS  norm | formX       |           0 | MT_NORMALIZE
 * CI !norm | upper       |   !ZCIEXACT | MT_NORMALIZE
 * CI !norm | upper       |    ZCIEXACT | MT_NORMALIZE | MT_MATCH_CASE
 * CI  norm | upper|formX |   !ZCIEXACT | MT_NORMALIZE
 * CI  norm | upper|formX |    ZCIEXACT | MT_NORMALIZE | MT_MATCH_CASE
 * CM !norm | upper       |    !ZCILOOK | MT_NORMALIZE | MT_MATCH_CASE
 * CM !norm | upper       |     ZCILOOK | MT_NORMALIZE
 * CM  norm | upper|formX |    !ZCILOOK | MT_NORMALIZE | MT_MATCH_CASE
 * CM  norm | upper|formX |     ZCILOOK | MT_NORMALIZE
 *
 * Abbreviations:
 *    CS = Case Sensitive, CI = Case Insensitive, CM = Case Mixed
 *    upper = case folding set by fs type on creation (U8_TEXTPREP_TOUPPER)
 *    formX = unicode normalization form set on fs creation
 */
static int
zfs_dropname(zfs_dirlock_t *dl, znode_t *zp, znode_t *dzp, dmu_tx_t *tx,
    int flag)
{
	int error;

	if (ZTOZSB(zp)->z_norm) {
		matchtype_t mt = MT_NORMALIZE;

		if ((ZTOZSB(zp)->z_case == ZFS_CASE_INSENSITIVE &&
		    (flag & ZCIEXACT)) ||
		    (ZTOZSB(zp)->z_case == ZFS_CASE_MIXED &&
		    !(flag & ZCILOOK))) {
			mt |= MT_MATCH_CASE;
		}

		error = zap_remove_norm(ZTOZSB(zp)->z_os, dzp->z_id,
		    dl->dl_name, mt, tx);
	} else {
		error = zap_remove(ZTOZSB(zp)->z_os, dzp->z_id, dl->dl_name,
		    tx);
	}

	return (error);
}

static int
zfs_drop_nlink_locked(znode_t *zp, dmu_tx_t *tx, boolean_t *unlinkedp)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	int		zp_is_dir = S_ISDIR(zp->z_mode);
	boolean_t	unlinked = B_FALSE;
	sa_bulk_attr_t	bulk[3];
	uint64_t	mtime[2], ctime[2];
	uint64_t	links;
	int		count = 0;
	int		error;

	if (zp_is_dir && !zfs_dirempty(zp))
		return (SET_ERROR(ENOTEMPTY));

	if (zp->z_links <= zp_is_dir) {
		zfs_panic_recover("zfs: link count on %lu is %u, "
		    "should be at least %u", zp->z_id,
		    (int)zp->z_links, zp_is_dir + 1);
		zp->z_links = zp_is_dir + 1;
	}
	--zp->z_links;
	if (zp->z_links == zp_is_dir) {
		zp->z_unlinked = B_TRUE;
		zp->z_links = 0;
		unlinked = B_TRUE;
	} else {
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
		    NULL, &ctime, sizeof (ctime));
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &zp->z_pflags, sizeof (zp->z_pflags));
		zfs_tstamp_update_setup(zp, STATE_CHANGED, mtime,
		    ctime);
	}
	links = zp->z_links;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
	    NULL, &links, sizeof (links));
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	if (unlinkedp != NULL)
		*unlinkedp = unlinked;
	else if (unlinked)
		zfs_unlinked_add(zp, tx);

	return (0);
}

/*
 * Forcefully drop an nlink reference from (zp) and mark it for deletion if it
 * was the last link. This *must* only be done to znodes which have already
 * been zfs_link_destroy()'d with ZRENAMING. This is explicitly only used in
 * the error path of zfs_rename(), where we have to correct the nlink count if
 * we failed to link the target as well as failing to re-link the original
 * znodes.
 */
int
zfs_drop_nlink(znode_t *zp, dmu_tx_t *tx, boolean_t *unlinkedp)
{
	int error;

	mutex_enter(&zp->z_lock);
	error = zfs_drop_nlink_locked(zp, tx, unlinkedp);
	mutex_exit(&zp->z_lock);

	return (error);
}

/*
 * Unlink zp from dl, and mark zp for deletion if this was the last link. Can
 * fail if zp is a mount point (EBUSY) or a non-empty directory (ENOTEMPTY).
 * If 'unlinkedp' is NULL, we put unlinked znodes on the unlinked list.
 * If it's non-NULL, we use it to indicate whether the znode needs deletion,
 * and it's the caller's job to do it.
 */
int
zfs_link_destroy(zfs_dirlock_t *dl, znode_t *zp, dmu_tx_t *tx, int flag,
    boolean_t *unlinkedp)
{
	znode_t *dzp = dl->dl_dzp;
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	int zp_is_dir = S_ISDIR(zp->z_mode);
	boolean_t unlinked = B_FALSE;
	sa_bulk_attr_t bulk[5];
	uint64_t mtime[2], ctime[2];
	uint64_t links;
	int count = 0;
	int error;

	if (!(flag & ZRENAMING)) {
		mutex_enter(&zp->z_lock);

		if (zp_is_dir && !zfs_dirempty(zp)) {
			mutex_exit(&zp->z_lock);
			return (SET_ERROR(ENOTEMPTY));
		}

		/*
		 * If we get here, we are going to try to remove the object.
		 * First try removing the name from the directory; if that
		 * fails, return the error.
		 */
		error = zfs_dropname(dl, zp, dzp, tx, flag);
		if (error != 0) {
			mutex_exit(&zp->z_lock);
			return (error);
		}

		/* The only error is !zfs_dirempty() and we checked earlier. */
		error = zfs_drop_nlink_locked(zp, tx, &unlinked);
		ASSERT0(error);
		mutex_exit(&zp->z_lock);
	} else {
		error = zfs_dropname(dl, zp, dzp, tx, flag);
		if (error != 0)
			return (error);
	}

	mutex_enter(&dzp->z_lock);
	dzp->z_size--;		/* one dirent removed */
	if (zp_is_dir)
		--dzp->z_links;	/* ".." link from zp */
	links = dzp->z_links;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_LINKS(zfsvfs),
	    NULL, &links, sizeof (links));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &dzp->z_size, sizeof (dzp->z_size));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs),
	    NULL, ctime, sizeof (ctime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs),
	    NULL, mtime, sizeof (mtime));
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &dzp->z_pflags, sizeof (dzp->z_pflags));
	zfs_tstamp_update_setup(dzp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(dzp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);
	mutex_exit(&dzp->z_lock);

	if (unlinkedp != NULL)
		*unlinkedp = unlinked;
	else if (unlinked)
		zfs_unlinked_add(zp, tx);

	return (0);
}

/*
 * Indicate whether the directory is empty.  Works with or without z_lock
 * held, but can only be consider a hint in the latter case.  Returns true
 * if only "." and ".." remain and there's no work in progress.
 *
 * The internal ZAP size, rather than zp->z_size, needs to be checked since
 * some consumers (Lustre) do not strictly maintain an accurate SA_ZPL_SIZE.
 */
boolean_t
zfs_dirempty(znode_t *dzp)
{
	zfsvfs_t *zfsvfs = ZTOZSB(dzp);
	uint64_t count;
	int error;

	if (dzp->z_dirlocks != NULL)
		return (B_FALSE);

	error = zap_count(zfsvfs->z_os, dzp->z_id, &count);
	if (error != 0 || count != 0)
		return (B_FALSE);

	return (B_TRUE);
}

int
zfs_link(znode_t *tdzp, znode_t *szp, char *name, cred_t *cr,
    int flags)
{
	if (!tdzp || !szp || !name || !*name || strchr(name, '/')) return (EINVAL);
	if (!cr) return (EACCES);
	if (!S_ISDIR(tdzp->z_mode)) return (ENOTDIR);
	if (ZTOZSB(tdzp) != ZTOZSB(szp)) return (EXDEV);
	if (flags & ~FIGNORECASE) return (ENOTSUP);
	if (!ZTOZSB(tdzp)->z_longname && strlen(name) >= ZAP_MAXNAMELEN)
		return (ENAMETOOLONG);
	znode_t		*tzp;
	zfsvfs_t	*zfsvfs = ZTOZSB(tdzp);
	zilog_t		*zilog;
	zfs_dirlock_t	*dl;
	dmu_tx_t	*tx;
	int		error;
	int		zf = ZNEW;
	uint64_t	parent;
	uid_t		owner;
	boolean_t	waited = B_FALSE;
	boolean_t	is_tmpfile = 0;
	uint64_t	txg;

	/* Native anonymous tmpfile linking is not exposed yet. */
	is_tmpfile = B_FALSE;

	ASSERT(S_ISDIR(tdzp->z_mode));

	if (name == NULL)
		return (SET_ERROR(EINVAL));

	if ((error = zfs_enter_verify_zp(zfsvfs, tdzp, FTAG)) != 0)
		return (error);
	zilog = zfsvfs->z_log;
	if (zfs_is_readonly(zfsvfs) || !zilog) {
		error = zfs_is_readonly(zfsvfs) ? EROFS : ENOTSUP;
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	/*
	 * POSIX dictates that we return EPERM here.
	 * Better choices include ENOTSUP or EISDIR.
	 */
	if (S_ISDIR(szp->z_mode)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_verify_zp(szp)) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	/* Native callers have no Linux VFS to enforce inode immutability.
	 * Replay retains upstream internal semantics, bypassing this VFS guard. */
	if (!zfsvfs->z_replay && (szp->z_pflags & (ZFS_IMMUTABLE | ZFS_APPENDONLY))) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	/*
	 * If we are using project inheritance, means if the directory has
	 * ZFS_PROJINHERIT set, then its descendant directories will inherit
	 * not only the project ID, but also the ZFS_PROJINHERIT flag. Under
	 * such case, we only allow hard link creation in our tree when the
	 * project IDs are the same.
	 */
	if (tdzp->z_pflags & ZFS_PROJINHERIT &&
	    tdzp->z_projid != szp->z_projid) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
	}

	/*
	 * We check i_sb because snapshots and the ctldir must have different
	 * super blocks.
	 */
	if (ZTOZSB(szp) != zfsvfs) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EXDEV));
	}

	/* Prevent links to .zfs/shares files */

	if ((error = sa_lookup(szp->z_sa_hdl, SA_ZPL_PARENT(zfsvfs),
	    &parent, sizeof (uint64_t))) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	if (parent == zfsvfs->z_shares_dir) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if (zfsvfs->z_utf8 && u8_validate(name,
	    strlen(name), NULL, U8_VALIDATE_ENTIRE, &error) < 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EILSEQ));
	}
	if (flags & FIGNORECASE)
		zf |= ZCILOOK;

	/*
	 * We do not support links between attributes and non-attributes
	 * because of the potential security risk of creating links
	 * into "normal" file space in order to circumvent restrictions
	 * imposed in attribute space.
	 */
	if ((szp->z_pflags & ZFS_XATTR) != (tdzp->z_pflags & ZFS_XATTR)) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EINVAL));
	}

	owner = zfs_fuid_map_id(zfsvfs, szp->z_uid,
	    cr, ZFS_OWNER);
	if (owner != crgetuid(cr) && crgetuid(cr) != 0) {
		zfs_exit(zfsvfs, FTAG);
		return (SET_ERROR(EPERM));
	}

	if ((error = zfs_zaccess(tdzp, ACE_ADD_FILE, 0, B_FALSE, cr,
	    NULL))) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

top:
	/*
	 * Attempt to lock directory; fail if entry already exists.
	 */
	error = zfs_dirent_lock(&dl, tdzp, name, &tzp, zf, NULL, NULL);
	if (error) {
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}

	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, szp->z_sa_hdl, B_FALSE);
	dmu_tx_hold_zap(tx, tdzp->z_id, TRUE, name);
	if (is_tmpfile)
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);

	zfs_sa_upgrade_txholds(tx, szp);
	zfs_sa_upgrade_txholds(tx, tdzp);
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
		dmu_tx_abort(tx);
		zfs_exit(zfsvfs, FTAG);
		return (error);
	}
	/* unmark z_unlinked so zfs_link_create will not reject */
	if (is_tmpfile)
		szp->z_unlinked = B_FALSE;
	error = zfs_link_create(dl, szp, tx, 0);

	if (error == 0) {
		uint64_t txtype = TX_LINK;
		/*
		 * tmpfile is created to be in z_unlinkedobj, so remove it.
		 * Also, we don't log in ZIL, because all previous file
		 * operation on the tmpfile are ignored by ZIL. Instead we
		 * always wait for txg to sync to make sure all previous
		 * operation are sync safe.
		 */
		if (is_tmpfile) {
			VERIFY0(zap_remove_int(zfsvfs->z_os,
			    zfsvfs->z_unlinkedobj, szp->z_id, tx));
		} else {
			if (flags & FIGNORECASE)
				txtype |= TX_CI;
			zfs_log_link(zilog, tx, txtype, tdzp, szp, name);
		}
	} else if (is_tmpfile) {
		/* restore z_unlinked since when linking failed */
		szp->z_unlinked = B_TRUE;
	}
	txg = dmu_tx_get_txg(tx);
	dmu_tx_commit(tx);

	zfs_dirent_unlock(dl);

	if (error == 0) {
		if (!is_tmpfile && zfsvfs->z_os->os_sync == ZFS_SYNC_ALWAYS)
			error = zil_commit(zilog, 0);

		if (is_tmpfile && zfsvfs->z_os->os_sync != ZFS_SYNC_DISABLED) {
			txg_wait_flag_t wait_flags =
			    spa_get_failmode(dmu_objset_spa(zfsvfs->z_os)) ==
			    ZIO_FAILURE_MODE_CONTINUE ? TXG_WAIT_SUSPEND : 0;
			error = txg_wait_synced_flags(
			    dmu_objset_pool(zfsvfs->z_os), txg, wait_flags);
			if (error != 0) {
				ASSERT3U(error, ==, ESHUTDOWN);
				error = SET_ERROR(EIO);
			}
		}
	}

	zfs_znode_update_vfs(tdzp);
	zfs_znode_update_vfs(szp);
	zfs_exit(zfsvfs, FTAG);
	return (error);
}
