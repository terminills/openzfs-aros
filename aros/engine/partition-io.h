/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PARTITION_IO_H
#define OZ_PARTITION_IO_H
#include "device-io.h"
/* Resolve a root-table native position to exact I/O bounds. Caller must own
 * device admission; this is NOT persistent identity or an exclusive claim. */
int oz_device_open_partition(const char *,unsigned,uint32_t,int,struct oz_device **);
#endif
