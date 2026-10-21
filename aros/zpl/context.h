/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ZPL_CONTEXT_H
#define OZ_ZPL_CONTEXT_H
/* AROS handlers are native processes, not Linux kernel modules. Reuse the
 * already verified native pthread/SPL implementation, then expose the upstream
 * ZPL declarations. Do not switch core mutex/allocator ABI to Linux SPL. */
#include <sys/zfs_context.h>
#include <sys/ccompat.h>
/* Native libzpool has READER=0/WRITER=1. NONE must not alias READER;
 * upstream zvol tests it before taking a suspend lock. Never pass to rw_enter. */
#define RW_NONE (-1)
/* AVL embeds an extra ABI padding word in libzpool. Freeze the shared layout
 * before exposing _KERNEL ZPL declarations, including nested refcounts/dbufs. */
#include <sys/avl.h>
/* AROS has no Linux idmapped mount object. Keep mapping opaque until the
 * credential boundary is implemented; never alias it to a host namespace. */
typedef struct oz_idmap zidmap_t;
typedef mode_t umode_t;
typedef uint64_t rlim64_t;
/* Match the native SPL page size rather than assuming a host page shift. */
#ifndef PAGESHIFT
#define PAGESHIFT (__builtin_ctzl((unsigned long)PAGESIZE))
#endif
/* These are representation aliases, not identity/permission mappings. Native
 * cached owners retain all 64 FUID bits until zfs_fuid_map_id handles them. */
#define KUID_TO_SUID(id) (id)
#define KGID_TO_SGID(id) (id)
#define ATTR_MODE AT_MODE
#define ATTR_UID AT_UID
#define ATTR_GID AT_GID
#define ATTR_SIZE AT_SIZE
#define ATTR_ATIME AT_ATIME
#define ATTR_MTIME AT_MTIME
#define ATTR_CTIME AT_CTIME
#define OZ_AROS_ZPL 1
#define _KERNEL 1
#endif
