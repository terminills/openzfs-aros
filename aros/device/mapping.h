/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_DEVICE_MAPPING_H
#define OZ_DEVICE_MAPPING_H
#include <exec/types.h>
/* Private openzfs.device command, advertised by NSCMD_DEVICEQUERY.
 * Read-only snapshot; same system-owner authorization as other device I/O.
 * io_Data points to this output structure; io_Length must cover it.
 * Name is the current explicit mapping, not a live dataset-rename lookup.
 * Absent units retain that name but return zero capacity/block size. */
#define OZCMD_GETMAPPING 0x4f5a
#define OZCMD_SETMAPPING 0x4f5b
#define OZCMD_EJECTMAPPING 0x4f5c
#define OZ_EJECT_VERSION 1
/* Guarded administrative eject: exactly one open handle, no subscriptions or
 * other queued requests, and matching generation. Flush precedes close.
 * io_Actual=1 means the mapping was released, even if later session cleanup
 * reports an error. This does not unmount a filesystem. */
struct oz_device_eject { ULONG version,structure_size,expected_change; };
#define OZ_REMAP_VERSION 1
#define OZ_REMAP_WRITABLE 1
/* Explicit replacement of an ejected unit only. Caller must be its only open
 * handle; subscriptions and queued requests prevent replacement. No mount or
 * detach is implicit. Match TD_CHANGENUM to reject stale requests. */
struct oz_device_remap {
    ULONG version,structure_size,flags,expected_change;
    char dataset[256];
};
#define OZ_MAPPING_VERSION 1
#define OZ_MAPPING_PRESENT 1
#define OZ_MAPPING_READONLY 2
struct oz_device_mapping {
    ULONG version,structure_size,unit,unit_count;
    ULONG flags,sector_size;
    UQUAD bytes;
    char dataset[256];
};
#endif
