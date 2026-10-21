/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_FILE_SCOPE_H
#define OZ_FILE_SCOPE_H
#include <sys/zfs_file.h>
struct oz_file_binding { int descriptor; zfs_file_t *file; };
struct oz_file_scope;
/* Trusted service-side bindings only, after authenticating the owning client.
 * This does not import numeric POSIX descriptors or validate client pointers.
 * Enter snapshots bindings and retains files. Scopes are per-thread, nested,
 * non-inherited and LIFO; a nested scope does not see the outer descriptors. */
int oz_file_scope_enter(const struct oz_file_binding *, size_t, struct oz_file_scope **);
int oz_file_scope_leave(struct oz_file_scope *);
zfs_file_t *oz_file_scope_get(int);
#endif
