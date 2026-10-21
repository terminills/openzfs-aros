/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REPLICATION_H
#define OZ_REPLICATION_H
#include <sys/zfs_file.h>
/* Receive a normal full/incremental stream from its beginning. Input is
 * borrowed and must already be authorized for this caller. The management
 * service must serialize target lifecycle. Full native builds support mounted
 * filesystems; reduced builds require unmount. No property override or implicit
 * mount here. This entry
 * point does not retain state if the receive is interrupted. */
int oz_receive_stream(const char *,const char *,zfs_file_t *,cred_t *);
/* Preserve upstream receive state on interruption when resumable is true. */
int oz_receive_stream_resumable(const char *,const char *,zfs_file_t *,cred_t *,int);
/* Explicit force uses receive permission, never receive:append alone. */
int oz_receive_stream_options(const char *,const char *,zfs_file_t *,cred_t *,int,int);
/* Discard interrupted receive state, not a healthy dataset. Caller must
 * serialize lifecycle and reject mounted targets, as for receive itself. */
int oz_receive_abort(const char *,cred_t *);
/* Ordinary (non-raw) full/incremental send. Output must already be authorized;
 * callback must consume the complete chunk or return an errno. It is internal,
 * never a function pointer accepted through the management protocol. */
int oz_send_stream(const char *,const char *,cred_t *,
    int (*)(void *,const void *,size_t),void *);
#define OZ_SEND_COMPRESSED 1u
#define OZ_SEND_LARGE_BLOCKS 2u
#define OZ_SEND_EMBEDDED 4u
#define OZ_SEND_RAW 32u
#define OZ_SEND_REPRESENTATION (OZ_SEND_COMPRESSED|OZ_SEND_LARGE_BLOCKS|OZ_SEND_EMBEDDED)
#define OZ_SEND_SUPPORTED (OZ_SEND_REPRESENTATION|OZ_SEND_RAW)
/* Parses a private mutable copy, rejecting duplicates and unknown options.
 * from points into that copy. Validate before opening any output. */
int oz_send_options(char *,const char **,unsigned *,const char **,const char **);
int oz_send_redact_validate(const char *,const char *);
int oz_send_stream_redacted(const char *,const char *,unsigned,const char *,cred_t *,
    int (*)(void *,const void *,size_t),void *);
struct oz_resume_send {
    uint64_t object,offset;
    unsigned flags;
    /* Resolved completed upstream bookmark, never a caller-supplied pointer. */
    char redact[256];
};
int oz_send_resume_prepare(const char *,const char *,const char *,cred_t *,struct oz_resume_send *);
int oz_send_stream_token(const char *,const char *,const char *,cred_t *,
    int (*)(void *,const void *,size_t),void *);
int oz_send_stream_flags(const char *,const char *,unsigned,cred_t *,
    int (*)(void *,const void *,size_t),void *);
#endif
