/* SPDX-License-Identifier: CDDL-1.0 */
/*
 * Native adaptation of zfs_extend/free_range/trunc/freesp from upstream
 * module/os/linux/zfs/zfs_znode_os.c and zfs_space from zfs_vnops_os.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Portions Copyright 2007 Jeremy Teo.
 * Native mapping policy rejects shared file mappings, so Linux page-cache
 * operations are not replaced by fake successful callbacks.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>

static int
zfs_extend(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	uint64_t newblksz;
	int error;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end <= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	if (end > zp->z_blksz &&
	    (!ISP2(zp->z_blksz) || zp->z_blksz < zfsvfs->z_max_blksz)) {
		/*
		 * We are growing the file past the current block size.
		 */
		if (zp->z_blksz > ZTOZSB(zp)->z_max_blksz) {
			/*
			 * File's blocksize is already larger than the
			 * "recordsize" property.  Only let it grow to
			 * the next power of 2.
			 */
			ASSERT(!ISP2(zp->z_blksz));
			newblksz = MIN(end, 1 << highbit64(zp->z_blksz));
		} else {
			newblksz = MIN(end, ZTOZSB(zp)->z_max_blksz);
		}
		dmu_tx_hold_write(tx, zp->z_id, 0, newblksz);
	} else {
		newblksz = 0;
	}

	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	if (newblksz)
		zfs_grow_blocksize(zp, newblksz, tx);

	zp->z_size = end;

	VERIFY0(sa_update(zp->z_sa_hdl, SA_ZPL_SIZE(ZTOZSB(zp)),
	    &zp->z_size, sizeof (zp->z_size), tx));

	zfs_rangelock_exit(lr);

	dmu_tx_commit(tx);

	return (0);
}

static int
zfs_free_range(znode_t *zp, uint64_t off, uint64_t len)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	zfs_locked_range_t *lr;
	int error;

	/*
	 * Lock the range being freed.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, off, len, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (off >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	if (off + len > zp->z_size)
		len = zp->z_size - off;

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, off, len);

	/* Native shared file mappings are rejected before this operation. */
	zfs_rangelock_exit(lr);

	return (error);
}

static int
zfs_trunc(znode_t *zp, uint64_t end)
{
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	dmu_tx_t *tx;
	zfs_locked_range_t *lr;
	int error;
	sa_bulk_attr_t bulk[2];
	int count = 0;

	/*
	 * We will change zp_size, lock the whole file.
	 */
	lr = zfs_rangelock_enter(&zp->z_rangelock, 0, UINT64_MAX, RL_WRITER);

	/*
	 * Nothing to do if file already at desired length.
	 */
	if (end >= zp->z_size) {
		zfs_rangelock_exit(lr);
		return (0);
	}

	error = dmu_free_long_range(zfsvfs->z_os, zp->z_id, end,
	    DMU_OBJECT_END);
	if (error) {
		zfs_rangelock_exit(lr);
		return (error);
	}
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	dmu_tx_mark_netfree(tx);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		zfs_rangelock_exit(lr);
		return (error);
	}

	zp->z_size = end;
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_SIZE(zfsvfs),
	    NULL, &zp->z_size, sizeof (zp->z_size));

	if (end == 0) {
		zp->z_pflags &= ~ZFS_SPARSE;
		SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
		    NULL, &zp->z_pflags, 8);
	}
	VERIFY0(sa_bulk_update(zp->z_sa_hdl, bulk, count, tx));

	dmu_tx_commit(tx);
	zfs_rangelock_exit(lr);

	return (0);
}

int
zfs_freesp(znode_t *zp, uint64_t off, uint64_t len, int flag, boolean_t log)
{
	dmu_tx_t *tx;
	zfsvfs_t *zfsvfs = ZTOZSB(zp);
	zilog_t *zilog = zfsvfs->z_log;
	uint64_t mode;
	uint64_t mtime[2], ctime[2];
	sa_bulk_attr_t bulk[3];
	int count = 0;
	int error;

	if (off > INT64_MAX || len > INT64_MAX - off) return (EOVERFLOW);
	if (zp->z_mapcnt) return (ENOTSUP);
	if (log && !zilog) return (ENOTSUP);
	if ((error = sa_lookup(zp->z_sa_hdl, SA_ZPL_MODE(zfsvfs), &mode,
	    sizeof (mode))) != 0)
		return (error);

	if (off > zp->z_size) {
		error =  zfs_extend(zp, off+len);
		if (error == 0 && log)
			goto log;
		goto out;
	}

	if (len == 0) {
		error = zfs_trunc(zp, off);
	} else {
		if ((error = zfs_free_range(zp, off, len)) == 0 &&
		    off + len > zp->z_size)
			error = zfs_extend(zp, off+len);
	}
	if (error || !log)
		goto out;
log:
	tx = dmu_tx_create(zfsvfs->z_os);
	dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
	zfs_sa_upgrade_txholds(tx, zp);
	error = dmu_tx_assign(tx, DMU_TX_WAIT);
	if (error) {
		dmu_tx_abort(tx);
		goto out;
	}

	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_MTIME(zfsvfs), NULL, mtime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_CTIME(zfsvfs), NULL, ctime, 16);
	SA_ADD_BULK_ATTR(bulk, count, SA_ZPL_FLAGS(zfsvfs),
	    NULL, &zp->z_pflags, 8);
	zfs_tstamp_update_setup(zp, CONTENT_MODIFIED, mtime, ctime);
	error = sa_bulk_update(zp->z_sa_hdl, bulk, count, tx);
	ASSERT0(error);

	zfs_log_truncate(zilog, tx, TX_TRUNCATE, zp, off, len);

	dmu_tx_commit(tx);

	zfs_znode_update_vfs(zp);
	error = 0;

out:
	/* No independent native file-page cache exists. Size is authoritative in
	 * the znode/SA; do not fabricate Linux truncate_setsize side effects. */
	return (error);
}

int
zfs_space(znode_t *zp, int cmd, flock64_t *range, int flags,
    int64_t offset, cred_t *cr)
{
    (void)offset;
    if (!zp || !range) return (EINVAL);
    if (!cr) return (EACCES);
    if (cmd != F_FREESP || range->l_whence != SEEK_SET ||
        range->l_start < 0 || range->l_len < 0) return (EINVAL);
    if ((uint64_t)range->l_len > INT64_MAX - (uint64_t)range->l_start)
        return (EOVERFLOW);
    zfsvfs_t *fs = ZTOZSB(zp);
    int error = zfs_enter_verify_zp(fs, zp, FTAG);
    if (error) return (error);
    if (zfs_is_readonly(fs)) error = EROFS;
    else if (!S_ISREG(zp->z_mode)) error = S_ISDIR(zp->z_mode) ? EISDIR : ENOTSUP;
    else error = zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, cr, NULL);
    if (!error) error = zfs_freesp(zp, range->l_start, range->l_len, flags, B_TRUE);
    zfs_exit(fs, FTAG);
    return (error);
}
