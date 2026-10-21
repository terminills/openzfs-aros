/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IOCTL_COPY_H
#define OZ_IOCTL_COPY_H
#include <stddef.h>
#include <stdint.h>
#ifndef FKIOCTL
#define FKIOCTL 0x80000000U
#endif
#define OZ_IOCTL_INPUT 1U
#define OZ_IOCTL_OUTPUT 2U
struct oz_ioctl_range { void *base; size_t size; unsigned access; };
struct oz_ioctl_copy_scope;
/* Internal service API, NOT a pointer validation or authentication API.
 * Register only service-owned allocations created while marshalling a request,
 * never addresses/range descriptions supplied by a client. Buffers remain
 * caller-owned and must outlive the scope. Nested scopes are thread-local;
 * they do not inherit their parent's ranges. Pop in LIFO order. Thread exit
 * frees scope metadata only; request cleanup still owns the buffers. */
int oz_ioctl_copy_enter(const struct oz_ioctl_range *, size_t,
    struct oz_ioctl_copy_scope **);
int oz_ioctl_copy_leave(struct oz_ioctl_copy_scope *);
int ddi_copyin(const void *, void *, size_t, int);
int ddi_copyout(const void *, void *, size_t, int);
int xcopyout(const void *, void *, size_t);
int copyinstr(const void *, void *, size_t, size_t *);
#endif
