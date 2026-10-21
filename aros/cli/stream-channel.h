/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_STREAM_CHANNEL_H
#define OZ_STREAM_CHANNEL_H
#include <exec/ports.h>
#include <stddef.h>
#include <stdint.h>
#define OZ_STREAM_CHUNK 32768
#define OZ_STREAM_READ 1
#define OZ_STREAM_WRITE 2
/* Internal endpoint from retained authenticated admission, never an address
 * supplied in a client nvlist. Keep peer alive until enclosing operation ends. */
struct oz_stream_endpoint {struct MsgPort *peer; uint64_t operation;};
int oz_stream_read(void *,void *,size_t,size_t *);
int oz_stream_write(void *,const void *,size_t,size_t *);
/* Local callback, never transferred. Dispatch only a private operation port.
 * Every nonnull message is replied before return. */
int oz_stream_dispatch(struct Message *,uint64_t,unsigned,
    int (*)(void *,unsigned,void *,size_t,size_t *),void *);
/* Synchronous client pump. Keeps packet, caller storage and reply port alive
 * through the final DOS reply. Callback and context stay client-local. */
int oz_ioctl_stream_request(const char *,void *,size_t,unsigned,
    int (*)(void *,unsigned,void *,size_t,size_t *),void *);
#endif
