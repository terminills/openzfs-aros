/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ZPL_FCNTL_H
#define OZ_ZPL_FCNTL_H
#include <fcntl.h>
#include <stdint.h>
/* Private ZPL space-operation descriptor, not the native POSIX fcntl ABI. */
typedef struct oz_zpl_flock64 {
    short l_type, l_whence;
    int64_t l_start, l_len;
    int32_t l_pid;
} flock64_t;
#define F_FREESP 11
/* Native ZPL offsets are already 64-bit. Never forward this to POSIX open. */
#define O_LARGEFILE 0
#endif
