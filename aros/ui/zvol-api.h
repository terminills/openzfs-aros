/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ZVOL_API_H
#define OZ_ZVOL_API_H
#include "admission-api.h"
#define OZ_ZVOL_ACTION 0x4f5a5a56L
#define OZ_ZVOL_VERSION 1
#define OZ_ZVOL_IO_MAX 65536
enum { OZ_ZVOL_OPEN=1,OZ_ZVOL_INFO,OZ_ZVOL_READ,OZ_ZVOL_WRITE,
    OZ_ZVOL_FLUSH,OZ_ZVOL_CLOSE,OZ_ZVOL_DISCARD,OZ_ZVOL_CAPABILITIES };
#define OZ_ZVOL_WRITABLE 1
#define OZ_ZVOL_CAP_DISCARD 1
/* CAPABILITIES takes zero flags and returns supported bits in flags. */
/* Session-owned descriptors, never engine pointers. Open name is a dataset,
 * not a DOS path. READ/WRITE carry count bytes immediately after this header.
 * All output fields must be zero on input. Transport errors leave bytes alone;
 * accepted requests report engine_error and completed (including partial I/O).
 * DISCARD carries offset/count but no payload. Only complete volume blocks
 * within that sector-aligned range are freed; partial edge blocks are kept.
 * completed is the accepted request length, not a physical-trim byte count.
 * Each call authenticates anew. Close the session to retire all its handles.
 * This transport is not itself an Exec .device or automatic DOS publication. */
struct oz_zvol_request {
    struct oz_ticket session;
    ULONG version,operation;
    LONG descriptor;
    ULONG flags;
    UQUAD offset;
    ULONG count,completed;
    UQUAD size,block_size;
    ULONG readonly;
    LONG engine_error;
    char name[256];
};
int oz_zvol_request(const char *,void *,ULONG);
#endif
