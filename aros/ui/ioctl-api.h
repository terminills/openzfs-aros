/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IOCTL_API_H
#define OZ_IOCTL_API_H
#include <stddef.h>
#include "admission-api.h"
#define OZ_IOCTL_ACTION 0x4f5a4d3aL
#define OZ_IOCTL_STREAM_ACTION 0x4f5a4d42L
#define OZ_IOCTL_WIRE_MAGIC 0x4f5a4931UL
#define OZ_IOCTL_WIRE_VERSION 3
#define OZ_IOCTL_HISTORY_LIMIT 4096
/* Pointer-free envelope followed by command_size bytes of upstream zfs_cmd_t,
 * src_size input bytes, conf_size input bytes, history_size input bytes and
 * dst_capacity output bytes. History is a bounded NUL-terminated export log.
 * All pointer fields in the command must be zero. The server reconstructs
 * pointers only into its private snapshot, never into caller memory.
 * Transport errors are returned by the packet; completed upstream calls put
 * their errno in engine_error, including ENOMEM with a larger required size.
 * No retry of mutations is implied by this API. */
struct oz_ioctl_wire {
    struct oz_ticket session;
    ULONG magic,version,command_size,command;
    LONG descriptor;
    ULONG reserved;
    UQUAD src_size,conf_size,history_size,dst_capacity;
    LONG engine_error;
    /* Optional session-local control descriptor dedicated to one client
     * thread's single-use command history. Zero requests isolated no-history
     * execution. This is never a pointer or a client POSIX descriptor. */
    ULONG history_descriptor;
};
int oz_ioctl_request(const char *,void *,size_t);
/* Packed CREATE/key requests can contain wrapping keys. Volatile writes keep
 * cleanup observable even when the compiler can see the following free(). */
static inline void oz_ioctl_wire_clear(void *buffer,size_t length)
{
    volatile unsigned char *bytes=buffer;
    while(length--)*bytes++=0;
}
#endif
