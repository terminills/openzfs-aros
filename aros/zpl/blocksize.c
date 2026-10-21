// SPDX-License-Identifier: CDDL-1.0
/*
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates. All rights reserved.
 * Copyright (c) 2012, 2018 by Delphix. All rights reserved.
 * Portions Copyright 2007 Jeremy Teo
 *
 * Extracted from upstream module/os/linux/zfs/zfs_znode_os.c (zfs-2.4.4).
 * This common DMU algorithm needs no Linux inode or VM adapter. The caller
 * must hold the znode's writer range lock and an assigned transaction with
 * appropriate write/SA holds. This does not grow the logical file length.
 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>

void
zfs_grow_blocksize(znode_t *zp, uint64_t size, dmu_tx_t *tx)
{
    int error;
    u_longlong_t dummy;
    if (size <= zp->z_blksz)
        return;
    if (zp->z_blksz && zp->z_size > zp->z_blksz)
        return;
    error = dmu_object_set_blocksize(ZTOZSB(zp)->z_os, zp->z_id,
        size, 0, tx);
    if (error == ENOTSUP)
        return;
    ASSERT0(error);
    dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &zp->z_blksz, &dummy);
}
