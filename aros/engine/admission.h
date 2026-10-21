/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ADMISSION_H
#define OZ_ADMISSION_H
#include "../ui/api.h"
#include "../ui/admission-api.h"
#include "../ui/mount-api.h"
#include "../ui/handler-api.h"
#include <sys/cred.h>
struct DosPacket;
struct oz_admitted;
struct oz_packet_security;
struct oz_admission_server;
struct oz_session_cleanup;
/* Owner-task binding to management's mount-aware lifecycle adapter.
 * Install before accepting requests; never replace while jobs exist. */
void oz_admission_bind_lifecycle(int (*)(struct oz_message *, cred_t *,const struct oz_packet_security *));
void oz_admission_bind_replication(int (*)(struct oz_message *,const struct oz_packet_security *));
void oz_admission_bind_key_load(int (*)(struct oz_message *,const struct oz_packet_security *));
void oz_admission_bind_create(int (*)(struct oz_message *,const struct oz_packet_security *));
void oz_admission_bind_mounts(int (*)(struct oz_mount_record *,ULONG,ULONG *));
void oz_admission_bind_mount_policy(int (*)(const char *,ULONG *,ULONG *));
void oz_admission_bind_mount_shares(int (*)(const char *,UQUAD *));
void oz_admission_bind_handler(int (*)(struct oz_handler_attach *,cred_t *));
/* The key-file binding handles both load (62) and replacement (64), retaining
 * the original native identity for source-file access in either case. */
struct oz_admission_server *oz_admission_server_open(const char *);
ULONG oz_admission_server_signals(struct oz_admission_server *);
void oz_admission_server_pump(struct oz_admission_server *);
int oz_admission_server_close(struct oz_admission_server *);
/* Call before engine teardown. Live client sessions must release their
 * engine-owned cleanup state while the engine is still available. */
int oz_admission_server_busy(struct oz_admission_server *);
/* Internal transport entry point: service task only; caller comes from fresh
 * packet authentication, never a client-provided UID. Success retains the
 * session, released with oz_session_cleanup_release after the operation. */
int oz_admission_session_acquire(struct oz_admission_server *,
    const struct oz_ticket *, cred_t *, struct oz_session_cleanup **);
/* Owning service task serializes all calls. The sender holds its DOS packet,
 * reply port and payload through ReplyPkt. Arg1 is oz_message*, arg2 its size.
 * Capture copies all input and retains the native caller; no caller pointers
 * survive. Acceptance does not promise operation success or authorize it. */
int oz_admitted_capture(struct DosPacket *, struct MsgPort *, struct oz_admitted **);
int oz_admitted_execute(struct oz_admitted *);
/* Owner task only, with a freshly authenticated caller. Cancellation is only
 * possible before execution starts; retain the result until normal release. */
int oz_admitted_cancel(struct oz_admitted *, cred_t *);
/* caller must be freshly authenticated by the transport, never client IDs.
 * Results are available only to the submitting UID or an administrator. */
int oz_admitted_result(struct oz_admitted *, cred_t *, struct oz_message *);
void oz_admitted_free(struct oz_admitted *);
/* Borrowed immutable native identity, retained until job destruction. The
 * service port supplied at capture must outlive the job. Not client-supplied. */
const struct oz_packet_security *oz_admitted_security(const struct oz_admitted *);
#endif
