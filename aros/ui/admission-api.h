/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ADMISSION_API_H
#define OZ_ADMISSION_API_H
#include "api.h"
#define OZ_ADMISSION_PORT "OpenZFS.Management.Admission.v1"
#define OZ_ADMISSION_TEST_PORT "OpenZFS.Management.Admission.Test.v1"
#define OZ_ADMISSION_ACTION 0x4f5a4d33L
#define OZ_ADMISSION_QUERY 0x4f5a4d34L
#define OZ_ADMISSION_RELEASE 0x4f5a4d35L
#define OZ_SESSION_OPEN 0x4f5a4d36L
#define OZ_SESSION_CLOSE 0x4f5a4d37L
#define OZ_CONTROL_OPEN 0x4f5a4d38L
#define OZ_CONTROL_CLOSE 0x4f5a4d39L
#define OZ_ADMISSION_LIST 0x4f5a4d43L
#define OZ_ADMISSION_CANCEL 0x4f5a4d45L
#define OZ_ADMISSION_SLOTS 8
#define OZ_ADMISSION_LIST_MAX (2 * OZ_ADMISSION_SLOTS)
_Static_assert(OZ_ADMISSION_LIST_MAX<=OZ_MAX_ROWS,"request list exceeds reply rows");
#define OZ_SESSION_FILE_LIMIT 64
struct oz_ticket { UQUAD epoch, number; };
struct oz_control_request { struct oz_ticket session; LONG descriptor; ULONG reserved; };
/* Blocking worker calls. Never destroy the task/packet/reply port while in
 * flight. No automatic timeout/retry: accepted mutations have unknown outcome
 * if the service is lost. The UI must run these outside its event loop. */
int oz_admission_submit(const char *, const struct oz_message *, struct oz_ticket *);
int oz_admission_query(const char *, const struct oz_ticket *, struct oz_message *);
int oz_admission_release(const char *, const struct oz_ticket *);
/* Cancel only queued requests, as their owner or system identity. The ticket
 * remains queryable/releasable. EALREADY means execution already began. */
int oz_admission_cancel(const char *, const struct oz_ticket *);
/* Retained admission tickets and in-flight workers; caller-owned or all for
 * system identity. Released/drained entries disappear. No percentage. */
int oz_admission_list(const char *, struct oz_message *);
/* Explicit client lifetime only; opening a session does not advertise ioctl
 * support or confer authority. Every request is independently authenticated.
 * Lifetime belongs to the opening task: keep it alive while the session is
 * used. Removal of that task retires the session even if another task holds
 * its ticket. A future CLI process-wide owner must not be a short-lived worker.
 * Task-removal notifications are required; older runtimes return ENOTSUP. */
int oz_session_open(const char *, struct oz_ticket *);
int oz_session_close(const char *, const struct oz_ticket *);
/* Session-local service descriptors, not process POSIX FDs. Every call uses
 * fresh native authentication. Open does not grant any operation permission. */
int oz_control_open(const char *, const struct oz_ticket *, LONG *);
int oz_control_close(const char *, const struct oz_ticket *, LONG);
#endif
