/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REWRITE_API_H
#define OZ_REWRITE_API_H
#include <stdint.h>
/* OpenZFS-private DOS packet, not a system-wide ACTION assignment.
 * Arg1: handler-private identity from an open FileHandle; Arg2: request;
 * Arg3: exact sizeof(request). Arg4/5 must be zero. The handler reauthorizes
 * the current packet sender for WRITE_DATA before reaching upstream ZPL.
 * Standard DOS result/error plus engine_error after valid packet admission.
 */
#define OZ_ACTION_REWRITE 0x4f5a5201L
#define OZ_REWRITE_VERSION 1
struct oz_rewrite_request {
    uint32_t version,reserved;
    uint64_t offset,length,flags,argument;
    int32_t engine_error;
    uint32_t reserved2;
};
#endif
