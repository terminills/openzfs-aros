/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream zfs_purgedir/zfs_rmnode transaction algorithms from zfs_dir.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2013, 2016 by Delphix.
 * Native adaptation: explicit exclusive mount admission, no Linux eviction.
 * Failures retain cache state and the persistent unlinked set for retry.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/zap.h>
static int
zfs_purgedir(znode_t *dzp)
{
	zap_cursor_t	zc;
	zap_attribute_t	*zap = zap_attribute_alloc();
	znode_t		*xzp;
	dmu_tx_t	*tx;
	zfsvfs_t	*zfsvfs = ZTOZSB(dzp);
	zfs_dirlock_t	dl;
	int skipped = 0;
	int error;

	for (zap_cursor_init(&zc, zfsvfs->z_os, dzp->z_id);
	    (error = zap_cursor_retrieve(&zc, zap)) == 0;
	    zap_cursor_advance(&zc)) {
		error = zfs_zget(zfsvfs,
		    ZFS_DIRENT_OBJ(zap->za_first_integer), &xzp);
		if (error) {
			skipped += 1;
			continue;
		}

		ASSERT(S_ISREG(xzp->z_mode) ||
		    S_ISLNK(xzp->z_mode));

		tx = dmu_tx_create(zfsvfs->z_os);
		dmu_tx_hold_sa(tx, dzp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_zap(tx, dzp->z_id, FALSE, zap->za_name);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
		/* Is this really needed ? */
		zfs_sa_upgrade_txholds(tx, xzp);
		dmu_tx_mark_netfree(tx);
		error = dmu_tx_assign(tx, DMU_TX_WAIT);
		if (error) {
			dmu_tx_abort(tx);
			zfs_zrele_async(xzp);
			skipped += 1;
			continue;
		}
		memset(&dl, 0, sizeof (dl));
		dl.dl_dzp = dzp;
		dl.dl_name = zap->za_name;

		error = zfs_link_destroy(&dl, xzp, tx, 0, NULL);
		if (error)
			skipped += 1;
		dmu_tx_commit(tx);

		zfs_zrele_async(xzp);
	}
	zap_cursor_fini(&zc);
	zap_attribute_free(zap);
	if (error != ENOENT)
		skipped += 1;
	return (skipped);
}

static int
reap_node(znode_t *zp)
{
	zfsvfs_t	*zfsvfs = ZTOZSB(zp);
	objset_t	*os = zfsvfs->z_os;
	znode_t		*xzp = NULL;
	dmu_tx_t	*tx;
	uint64_t	acl_obj;
	uint64_t	xattr_obj;
	uint64_t	links;
	int		error;

	ASSERT0(zp->z_links);
	ASSERT3U(zp->z_refs, ==, 1); /* Exclusive drainer's reference. */

	/*
	 * If this is an attribute directory, purge its contents.
	 */
	if (S_ISDIR(zp->z_mode) && (zp->z_pflags & ZFS_XATTR)) {
		if (zfs_purgedir(zp) != 0) {
			/*
			 * Not enough space to delete some xattrs.
			 * Leave it in the unlinked set.
			 */
			return (EIO); /* Preserve cache and persistent queue for retry. */
		}
	}

	/*
	 * Free up all the data in the file.  We don't do this for directories
	 * because we need truncate and remove to be in the same tx, like in
	 * zfs_znode_delete(). Otherwise, if we crash here we'll end up with
	 * an inconsistent truncated zap object in the delete queue.  Note a
	 * truncated file is harmless since it only contains user data.
	 */
	if (S_ISREG(zp->z_mode)) {
		error = dmu_free_long_range(os, zp->z_id, 0, DMU_OBJECT_END);
		if (error) {
			/*
			 * Not enough space or we were interrupted by unmount.
			 * Leave the file in the unlinked set.
			 */
			return (error); /* Preserve cache and persistent queue for retry. */
		}
	}

	/*
	 * If the file has extended attributes, we're going to unlink
	 * the xattr dir.
	 */
	error = sa_lookup(zp->z_sa_hdl, SA_ZPL_XATTR(zfsvfs),
	    &xattr_obj, sizeof (xattr_obj));
	if (error == 0 && xattr_obj) {
		error = zfs_zget(zfsvfs, xattr_obj, &xzp);
		if (error) return (error);
	} else if (error != 0 && error != ENOENT) {
		return (error);
	}

	acl_obj = zfs_external_acl(zp);

	/*
	 * Set up the final transaction.
	 */
	tx = dmu_tx_create(os);
	dmu_tx_hold_free(tx, zp->z_id, 0, DMU_OBJECT_END);
	dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, FALSE, NULL);
	if (xzp) {
		dmu_tx_hold_zap(tx, zfsvfs->z_unlinkedobj, TRUE, NULL);
		dmu_tx_hold_sa(tx, xzp->z_sa_hdl, B_FALSE);
	}
	if (acl_obj)
		dmu_tx_hold_free(tx, acl_obj, 0, DMU_OBJECT_END);

	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		/*
		 * Not enough space to delete the file.  Leave it in the
		 * unlinked set, leaking it until the fs is remounted (at
		 * which point we'll call zfs_unlinked_drain() to process it).
		 */
		dmu_tx_abort(tx);
		goto out;
	}

	if (xzp) {
		ASSERT0(error);
		mutex_enter(&xzp->z_lock);
		xzp->z_unlinked = B_TRUE;	/* mark xzp for deletion */
		xzp->z_links = 0;		/* no more links to it */
		links = 0;
		VERIFY0(sa_update(xzp->z_sa_hdl, SA_ZPL_LINKS(zfsvfs),
		    &links, sizeof (links), tx));
		mutex_exit(&xzp->z_lock);
		zfs_unlinked_add(xzp, tx);
	}

	mutex_enter(&os->os_dsl_dataset->ds_dir->dd_activity_lock);

	/*
	 * Remove this znode from the unlinked set.  If a has rollback has
	 * occurred while a file is open and unlinked.  Then when the file
	 * is closed post rollback it will not exist in the rolled back
	 * version of the unlinked object.
	 */
	error = zap_remove_int(zfsvfs->z_os, zfsvfs->z_unlinkedobj,
	    zp->z_id, tx);
	VERIFY(error == 0 || error == ENOENT);

	uint64_t count;
	if (zap_count(os, zfsvfs->z_unlinkedobj, &count) == 0 && count == 0) {
		cv_broadcast(&os->os_dsl_dataset->ds_dir->dd_activity_cv);
	}

	mutex_exit(&os->os_dsl_dataset->ds_dir->dd_activity_lock);

	dataset_kstats_update_nunlinked_kstat(&zfsvfs->z_kstat, 1);

	/* No references survive: atime has no observer once deletion commits. */
	zp->z_atime_dirty = B_FALSE;
	zfs_znode_delete(zp, tx);

	dmu_tx_commit(tx);
out:
	if (xzp)
		zfs_zrele_async(xzp);
	return (error);
}

/* Caller excludes all other mount operations, including cache sync and new
 * holds. This is a synchronous reclamation boundary, never a zrele callback:
 * TXG/ZIL completion can therefore release references without waiting on I/O.
 * A live reference keeps its unlinked object intact and makes draining EBUSY.
 */
int
oz_unlinked_drain_exclusive(zfsvfs_t *fs)
{
    if (!fs || !fs->z_os || fs->z_unmounted) return (EINVAL);
    if (zfs_is_readonly(fs)) return (EROFS);
    for (;;) {
        zap_cursor_t cursor;
        zap_attribute_t *attr = zap_attribute_alloc();
        zap_cursor_init(&cursor, fs->z_os, fs->z_unlinkedobj);
        int error = zap_cursor_retrieve(&cursor, attr);
        if (!error && (attr->za_integer_length != 8 || attr->za_num_integers != 1))
            error = EIO;
        uint64_t object = error ? 0 : attr->za_first_integer;
        zap_cursor_fini(&cursor);
        zap_attribute_free(attr);
        if (error) return (error == ENOENT ? 0 : error);
        znode_t *zp = NULL;
        error = zfs_zget(fs, object, &zp);
        if (error) return (error);
        mutex_enter(&fs->z_znodes_lock);
        boolean_t busy = zp->z_refs != 1;
        mutex_exit(&fs->z_znodes_lock);
        /* No external znode holds remain. The owned xattr nvlist contains
         * copied metadata, not an active observer; dispose frees it. */
        if (busy || zp->z_mapcnt || zp->z_sync_cnt ||
            zp->z_dirlocks) error = EBUSY;
        else if (!zp->z_unlinked || zp->z_links) error = EIO;
        else error = reap_node(zp);
        oz_znode_rele(zp);
        if (error) return (error);
    }
}
