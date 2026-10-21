/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_STREAM_FILE_H
#define OZ_STREAM_FILE_H
#include <dos/dos.h>
#include <stdint.h>
#include <stddef.h>
struct oz_packet_security;
struct oz_stream_file;
/* Internal only: lock must come from a trusted native resolver, never a client
 * pointer. Keep it held through subsequent I/O and independently authorize
 * path traversal. This checks the locked object, not a supplied pathname. */
int oz_stream_lock_access(const struct oz_packet_security *,BPTR,uint32_t);
int oz_stream_directory_access(const struct oz_packet_security *,BPTR,uint32_t);
/* Internal component lookup beneath a trusted, already-reached directory.
 * Checks original caller search permission. Does not implicitly follow soft
 * links: ELOOP requires the resolver to authorize the expanded path explicitly.
 * Output is an owned lock; caller must authorize the final object's access. */
int oz_stream_child_lock(const struct oz_packet_security *,BPTR,const char *,BPTR *);
/* Walk relative to a trusted shared base lock, checking each directory and expanding
 * relative soft links explicitly. Absolute paths/links return EXDEV for the
 * namespace binder; this function never uses the daemon's current directory. */
int oz_stream_relative_lock(const struct oz_packet_security *,BPTR,const char *,BPTR *);
/* Fully qualified native path. Uses live namespace bindings, never daemon
 * current directory/PROGDIR. Does not start offline handlers or request media. */
int oz_stream_path_lock(const struct oz_packet_security *,const char *,BPTR *);
/* Consumes *lock only on success, opening that object rather than its name.
 * Caller must already have authorized traversal. The opaque handle enforces
 * the requested access even if DOS returns a read/write filehandle. */
int oz_stream_file_open(const struct oz_packet_security *,BPTR *lock,uint32_t,
    struct oz_stream_file **);
/* Existing empty output only; never truncates or creates. Requires exclusive
 * ownership of the native lock/handle and original caller write permission.
 * *lock becomes zero if converted, including a later empty-file check failure. */
int oz_stream_file_open_empty_output(const struct oz_packet_security *,BPTR *,
    struct oz_stream_file **);
int oz_stream_file_read(struct oz_stream_file *,void *,size_t,size_t *);
int oz_stream_file_write(struct oz_stream_file *,const void *,size_t,size_t *);
int oz_stream_file_close(struct oz_stream_file *);
#endif
