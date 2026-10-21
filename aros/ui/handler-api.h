/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_HANDLER_API_H
#define OZ_HANDLER_API_H
#include <exec/types.h>
struct DosList;
struct MsgPort;
#define OZ_HANDLER_ATTACH 0x4f5a4d44L
#define OZ_HANDLER_VERSION 1
#define OZ_HANDLER_WRITABLE 1
/* Native same-address-space startup protocol, not a wire/remote API.
 * Input node must be the exact inactive, published device. Caller retains
 * it and all startup metadata for the lifetime of the resident endpoint.
 * Endpoint is output only. Attach is authenticated and administrator-only. */
struct oz_handler_attach {
    ULONG version,flags;
    struct DosList *device;
    struct MsgPort *endpoint;
    char dataset[256],name[32];
};
int oz_handler_attach_request(const char *,struct oz_handler_attach *);
#endif
