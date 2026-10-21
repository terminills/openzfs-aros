/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_DOS_HANDLER_H
#define OZ_DOS_HANDLER_H
#include <exec/types.h>
#include <stddef.h>
struct oz_zpl;
struct oz_mount;
struct DosList;
/* All lifecycle and packet service calls run on the creating task. The caller
 * retains the filesystem until detach succeeds. Locks, files and queued DOS
 * requests prevent detach. Endpoint destruction additionally requires all
 * subscriptions and notification replies to have been released. */
/* Default mounts require security.library sender identity and upstream ACLs.
 * Missing/unconfigured identities fail closed; existing references may still
 * be released after logout. Shared handles are reauthorized per request. */
int oz_dos_mount(struct oz_zpl *, const char *name, struct oz_mount **);
/* Explicit public read-only diagnostic fixture, never production admission. */
int oz_dos_mount_fixture(struct oz_zpl *, const char *name, struct oz_mount **);
/* Internal owner-task startup integration, not an unauthenticated IPC API.
 * Adopt an already-published, inactive DLT_DEVICE with the exact given name.
 * Keep its startup metadata and caller-owned allocation intact. On success
 * the caller must retain that allocation until joined unmount completes;
 * detach removes it from DOS but retains the live late-packet endpoint.
 * Failure leaves the supplied entry unchanged. signal=-1 allocates a signal. */
int oz_dos_mount_adopt(struct oz_zpl *,const char *,struct DosList *,
    struct oz_mount **,int authenticated,int signal);
/* Borrow one signal allocated by the owning service task. Every endpoint,
 * including retired ones, must be pumped after that signal wakes the task.
 * The caller retains the signal until every endpoint has been destroyed. */
int oz_dos_mount_shared(struct oz_zpl *,const char *,struct oz_mount **,int authenticated,int signal);
ULONG oz_dos_signals(struct oz_mount *);
void oz_dos_pump(struct oz_mount *);
int oz_dos_unmount(struct oz_mount *);
/* Detach storage but retain the endpoint so already-resolved DOS requests can
 * receive DEVICE_NOT_MOUNTED safely. Desktop subscriptions remain owned by the
 * retired endpoint: keep pumping replies and EndNotify after disk removal.
 * Destroy only after callers and subscriptions quiesce. */
int oz_dos_detach(struct oz_mount *);
/* Non-destructive owner-task preflight. A later detach must recheck: queued
 * packets or DOS-list contention can change between calls. No reservation. */
int oz_dos_detach_check(struct oz_mount *);
/* Owner-task diagnostic: bytes held by handle/lock bookkeeping and auxiliary
 * ports, excluding stable late-packet endpoint identity and subscriptions. */
size_t oz_dos_bookkeeping_bytes(const struct oz_mount *);
struct oz_packet_security;
struct oz_dos_stream;
/* Same-owner-task stream read without sending packets back to ourselves.
 * Captured caller is borrowed through close. ENODEV means this published
 * device/volume does not own the requested absolute path. Bound assigns and
 * soft links remain for the namespace adapter; never bypass ACLs on fallback. */
int oz_dos_stream_open(struct oz_mount *,const struct oz_packet_security *,
    const char *,struct oz_dos_stream **);
int oz_dos_stream_read(struct oz_dos_stream *,void *,size_t,size_t *);
void oz_dos_stream_close(struct oz_dos_stream *);
/* Existing-empty output is exclusive and never truncated. Missing output is
 * staged privately and published without replacement only on commit.
 * Finish syncs changed data and consumes the handle, including on error. */
int oz_dos_stream_open_output(struct oz_mount *,const struct oz_packet_security *,
    const char *,struct oz_dos_stream **);
int oz_dos_stream_write(void *,const void *,size_t);
int oz_dos_stream_finish(struct oz_dos_stream *,int commit);
#endif
