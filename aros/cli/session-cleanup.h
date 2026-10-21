/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SESSION_CLEANUP_H
#define OZ_SESSION_CLEANUP_H
#include <sys/zfs_context.h>
#include <sys/zfs_file.h>
#include "../ui/admission-api.h"
struct oz_file_scope;
struct oz_session_cleanup;
int oz_session_cleanup_create(struct oz_session_cleanup **);
/* Internal service pointers, never client-supplied handles. The registry must
 * hold its lock or an existing reference while acquiring another reference.
 * Close consumes the owner's reference exactly once. Existing operations may
 * finish, but no new holds/registrations are accepted after close. */
int oz_session_cleanup_hold(struct oz_session_cleanup *);
int oz_session_cleanup_add(struct oz_session_cleanup *, void (*)(void *), void *);
void oz_session_cleanup_close(struct oz_session_cleanup *);
void oz_session_cleanup_release(struct oz_session_cleanup *);
/* Borrowed immutable identity; caller must retain a live session reference.
 * Future ioctl transport must validate ticket ownership before obtaining it. */
proc_t *oz_session_cleanup_caller(struct oz_session_cleanup *);
/* Service-only: authenticate the session ticket before accessing this table.
 * These are session-local descriptors, not client POSIX file descriptors.
 * Insert retains a live file; get returns an independently held reference.
 * Numbers never repeat during a session. Scope snapshots survive close. */
#define OZ_SESSION_FILES OZ_SESSION_FILE_LIMIT
int oz_session_file_insert(struct oz_session_cleanup *, zfs_file_t *, int *);
int oz_session_file_get(struct oz_session_cleanup *, int, zfs_file_t **);
int oz_session_file_close(struct oz_session_cleanup *, int);
int oz_session_file_scope(struct oz_session_cleanup *, struct oz_file_scope **);
/* Transfer a kmem_strdup pool name into/out of a dedicated control slot.
 * Take reserves the slot until put; concurrent reuse returns EBUSY.
 * Put always consumes the string, including after session close. */
int oz_session_history_take(struct oz_session_cleanup *,int,char **);
void oz_session_history_put(struct oz_session_cleanup *,int,char *);
#endif
