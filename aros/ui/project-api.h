/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PROJECT_API_H
#define OZ_PROJECT_API_H
#include <stdint.h>
/* OpenZFS-private DOS packet, not a system-wide ACTION assignment.
 * Arg1: held FileLock BPTR; Arg2: request; Arg3: exact sizeof(request).
 * Arg4/5 must be zero. Sender identity is captured for every request.
 * Standard DOS result/error plus engine_error after valid packet admission.
 * Queries leave project/inherit unchanged on failure. No implicit traversal.
 */
#define OZ_ACTION_PROJECT 0x4f5a5001L
#define OZ_PROJECT_VERSION 1
#define OZ_PROJECT_GET 0
#define OZ_PROJECT_SET 1
struct oz_project_request {
    uint32_t version,operation,project,inherit;
    int32_t engine_error;
    uint32_t reserved;
};
#endif
