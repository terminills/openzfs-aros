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
 * Copyright 2008 Sun Microsystems, Inc.  All rights reserved.
 * Use is subject to license terms.
 */

#include_next <unistd.h>

#ifndef _LIBSPL_UNISTD_H
#define	_LIBSPL_UNISTD_H

#include <sys/ioctl.h>

#ifdef __AROS__
/* Native off_t is already 64-bit. Some SDK revisions export the functions
 * but omit their public declarations and the explicit large-file aliases. */
#include <sys/types.h>
#ifdef __cplusplus
extern "C" {
#endif
extern ssize_t pread(int, void *, size_t, off_t);
extern ssize_t pwrite(int, const void *, size_t, off_t);
/* Requires the native pipe2 runtime extension and its generated import stub;
 * do not emulate O_CLOEXEC with a racy pipe()/fcntl() pair. */
extern int pipe2(int [2], int);
#ifdef __cplusplus
}
#endif
#define pread64 pread
#define pwrite64 pwrite
#endif

#if !defined(HAVE_ISSETUGID)
#include <sys/types.h>
#define	issetugid() (geteuid() == 0 || getegid() == 0)
#endif

#endif /* _LIBSPL_UNISTD_H */
