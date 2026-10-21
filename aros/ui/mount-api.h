/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_MOUNT_API_H
#define OZ_MOUNT_API_H
#include <stddef.h>
#include <exec/types.h>
#define OZ_MOUNTS_ACTION 0x4f5a4d3bL
#define OZ_MOUNTS_VERSION 1
#define OZ_MOUNTS_LIMIT 65536UL
#define OZ_MOUNT_READONLY 1UL
#define OZ_MOUNT_CONTROL_ACTION 0x4f5a4d3dL
#define OZ_MOUNT_ATTACH 1UL
#define OZ_MOUNT_DETACH 2UL
#define OZ_MOUNT_POLICY_ACTION 0x4f5a4d40L
#define OZ_POLICY_READONLY 1UL
#define OZ_POLICY_ATIME 2UL
#define OZ_POLICY_RELATIME 4UL
#define OZ_POLICY_EXEC 8UL
#define OZ_POLICY_KNOWN 15UL
struct oz_mount_policy_query {
    ULONG version, supported, enabled, reserved;
    char dataset[256];
};
int oz_mount_policy_query(const char *,const char *,ULONG *,ULONG *);
#define OZ_MOUNT_SHARES_ACTION 0x4f5a4d41L
struct oz_mount_shares_query {
    ULONG version, reserved;
    UQUAD object;
    char dataset[256];
};
int oz_mount_shares_query(const char *,const char *,UQUAD *);
struct oz_mount_control {
    ULONG version, operation, flags, reserved;
    char dataset[256], volume[32];
};
/* Volume is a native DOS name without colon. Errors are native errno values,
 * not translated GUI status codes. Detach never forces busy DOS references. */
int oz_mount_control(const char *, const char *, const char *, ULONG, ULONG);
struct oz_mount_record { char dataset[256],mountpoint[34]; ULONG flags; };
int oz_mounts_record_for_root(const char *, const char *, struct oz_mount_record *);
/* Pointer-free response, followed by capacity records. Input count/reserved
 * must be zero. ENOSPC returns required count, no partial records. A successful
 * request is one owner-task snapshot, not a series of live paginated views.
 * Only this resident OpenZFS service's active DOS mounts are reported. */
struct oz_mount_snapshot { ULONG version,capacity,count,reserved; };
int oz_mounts_query(const char *,void *,size_t);
/* Allocates an immutable snapshot owned by the caller (release with free).
 * Bounded retries only for read-only size races; never fabricates an empty
 * table on missing service or authentication failure. */
int oz_mounts_read(const char *,struct oz_mount_snapshot **);
/* Resolve a real DOS lock against mounted roots (including volume assigns).
 * Does not infer device identity from a path prefix. Output is empty on error. */
int oz_mounts_dataset_for_path(const char *,const char *,char *,size_t);
#endif
