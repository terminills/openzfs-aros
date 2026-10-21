// SPDX-License-Identifier: CDDL-1.0
/*
 * CDDL HEADER START
 *
 * The contents of this file are subject to the terms of the
 * Common Development and Distribution License, Version 1.0 only
 * (the "License").  You may not use this file except in compliance
 * with the License.
 *
 * You can obtain a copy of the license at usr/src/OPENSOLARIS.LICENSE
 * or https://opensource.org/licenses/CDDL-1.0.
 * See the License for the specific language governing permissions
 * and limitations under the License.
 *
 * When distributing Covered Code, include this CDDL HEADER in each
 * file and include the License file at usr/src/OPENSOLARIS.LICENSE.
 * If applicable, add the following below this CDDL HEADER, with the
 * fields enclosed by brackets "[]" replaced with your own identifying
 * information: Portions Copyright [yyyy] [name of copyright owner]
 *
 * CDDL HEADER END
 */
/*
 * Copyright 2005 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

/*	Copyright (c) 1984, 1986, 1987, 1988, 1989 AT&T	*/
/*	  All Rights Reserved  	*/

/*
 * University Copyright- Copyright (c) 1982, 1986, 1988
 * The Regents of the University of California
 * All Rights Reserved
 *
 * University Acknowledgment- Portions of this document are derived from
 * software developed by the University of California, Berkeley, and its
 * contributors.
 */

#ifndef	_LIBSPL_SYS_UIO_H
#define	_LIBSPL_SYS_UIO_H

#include <sys/sysmacros.h>
#include <sys/types.h>
#include_next <sys/uio.h>

#ifdef __APPLE__
#include <sys/_types/_iovec_t.h>
#endif

#include <stdint.h>
#ifdef __AROS__
#define UIO_DIRECT 0x0001
#endif
typedef struct iovec iovec_t;

#if defined(__linux__) || defined(__APPLE__) || defined(__AROS__)
typedef enum zfs_uio_rw {
	UIO_READ =	0,
	UIO_WRITE =	1,
} zfs_uio_rw_t;

typedef enum zfs_uio_seg {
	UIO_SYSSPACE =	0,
} zfs_uio_seg_t;

#elif defined(__FreeBSD__)
typedef enum uio_seg  zfs_uio_seg_t;
#endif

typedef struct zfs_uio {
	struct iovec	*uio_iov;	/* pointer to array of iovecs */
	int		uio_iovcnt;	/* number of iovecs */
	offset_t	uio_loffset;	/* file offset */
	zfs_uio_seg_t	uio_segflg;	/* address space (kernel or user) */
	uint16_t	uio_fmode;	/* file mode flags */
	uint16_t	uio_extflg;	/* extended flags */
	ssize_t		uio_resid;	/* residual count */
#ifdef __AROS__
	offset_t	uio_soffset;	/* initial logical offset */
	size_t		uio_skip;	/* consumed bytes in current iovec */
	struct iovec *uio_initial_iov;
	int uio_initial_iovcnt;
	size_t uio_initial_skip;
	ssize_t uio_initial_resid;
	struct {
		void **pages;
		size_t npages;
	} uio_dio;
#endif
} zfs_uio_t;

#ifdef __AROS__
int zfs_uiomove(void *, size_t, zfs_uio_rw_t, zfs_uio_t *);
int zfs_uiocopy(void *, size_t, zfs_uio_rw_t, zfs_uio_t *, size_t *);
void zfs_uioskip(zfs_uio_t *, size_t);
int zfs_uio_prefaultpages(ssize_t, zfs_uio_t *);
void zfs_uio_advance(zfs_uio_t *, ssize_t);
/* Synchronous shared-address-space transfers use the checked native mover. */
#define zfs_uio_fault_move(p, n, rw, u) zfs_uiomove((p), (n), (rw), (u))

static inline void
zfs_uio_fault_disable(zfs_uio_t *uio, boolean_t disable)
{
	/* Native synchronous buffers are not demand-paged user mappings. There
	 * is no page-fault retry mode to toggle; unsupported segments must not
	 * enter this path. The actual mover still validates every span. */
	ASSERT(uio != NULL && uio->uio_segflg == UIO_SYSSPACE);
	(void)disable;
}

static inline void
zfs_uio_setoffset(zfs_uio_t *uio, offset_t off)
{
	uio->uio_loffset = off;
}

static inline void
zfs_uio_setsoffset(zfs_uio_t *uio, offset_t off)
{
	ASSERT3S(uio->uio_loffset, ==, off);
	uio->uio_soffset = off;
}
#define zfs_uio_soffset(uio) ((uio)->uio_soffset)

static inline void
zfs_uio_iovec_init(zfs_uio_t *uio, struct iovec *iov, unsigned long count,
    offset_t offset, zfs_uio_seg_t seg, ssize_t resid, size_t skip)
{
	*uio = (zfs_uio_t){ .uio_iov = iov, .uio_iovcnt = count,
	    .uio_loffset = offset, .uio_segflg = seg, .uio_resid = resid,
	    .uio_soffset = offset, .uio_skip = skip };
	uio->uio_initial_iov = iov;
	uio->uio_initial_iovcnt = count;
	uio->uio_initial_skip = skip;
	uio->uio_initial_resid = resid;
}
#endif

#define	zfs_uio_segflg(uio)		(uio)->uio_segflg
#define	zfs_uio_offset(uio)		(uio)->uio_loffset
#define	zfs_uio_resid(uio)		(uio)->uio_resid
#define	zfs_uio_iovcnt(uio)		(uio)->uio_iovcnt
#define	zfs_uio_iovlen(uio, idx)	(uio)->uio_iov[(idx)].iov_len
#define	zfs_uio_iovbase(uio, idx)	(uio)->uio_iov[(idx)].iov_base

#ifndef __AROS__
static inline boolean_t
zfs_dio_page_aligned(void *buf)
{
	return ((((unsigned long)(buf) & (PAGESIZE - 1)) == 0) ?
	    B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_offset_aligned(uint64_t offset, uint64_t blksz)
{
	return ((IS_P2ALIGNED(offset, blksz)) ? B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_size_aligned(uint64_t size, uint64_t blksz)
{
	return (((size % blksz) == 0) ? B_TRUE : B_FALSE);
}

static inline boolean_t
zfs_dio_aligned(uint64_t offset, uint64_t size, uint64_t blksz)
{
	return ((zfs_dio_offset_aligned(offset, blksz) &&
	    zfs_dio_size_aligned(size, blksz)) ? B_TRUE : B_FALSE);
}

static inline void
zfs_uio_iov_at_index(zfs_uio_t *uio, uint_t idx, void **base, uint64_t *len)
{
	*base = zfs_uio_iovbase(uio, idx);
	*len = zfs_uio_iovlen(uio, idx);
}
#endif

#ifndef __AROS__
static inline void
zfs_uio_advance(zfs_uio_t *uio, ssize_t size)
{
	uio->uio_resid -= size;
	uio->uio_loffset += size;
}
#endif

#ifdef __AROS__
/* Share upstream alignment/vector helpers with the real ZPL rather than
 * duplicating kernel interfaces in the userspace SPL header. */
#include <sys/uio_impl.h>
#else
static inline offset_t
zfs_uio_index_at_offset(zfs_uio_t *uio, offset_t off, uint_t *vec_idx)
{
	*vec_idx = 0;
	while (*vec_idx < (uint_t)zfs_uio_iovcnt(uio) &&
	    off >= (offset_t)zfs_uio_iovlen(uio, *vec_idx)) {
		off -= zfs_uio_iovlen(uio, *vec_idx);
		(*vec_idx)++;
	}

	return (off);
}
#endif

#endif	/* _SYS_UIO_H */
