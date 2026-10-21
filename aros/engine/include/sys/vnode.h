/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_SYS_VNODE_H
#define OZ_AROS_SYS_VNODE_H
#include <sys/types.h>
#include <sys/time.h>

/* Native filesystem attributes, shared by libzpool and the AROS ZPL adapter.
 * These are not a Linux inode or dentry, and contain no host OS pointers. */
typedef struct vattr {
    uint32_t va_mask;
    mode_t va_mode;
    uid_t va_uid;
    gid_t va_gid;
    uint64_t va_fsid;
    uint64_t va_nodeid;
    uint32_t va_nlink;
    uint64_t va_size;
    inode_timespec_t va_atime;
    inode_timespec_t va_mtime;
    inode_timespec_t va_ctime;
    dev_t va_rdev;
    uint64_t va_nblocks;
    uint32_t va_blksize;
} vattr_t;

#define ATTR_XVATTR 0x10000
/* Upstream internal permission-check flag. Never accept this from an
 * untrusted DOS/management request as authority to bypass an ACL check. */
#define ATTR_NOACLCHECK 0x20
/* Upstream internal xattr lookup flag, not a public DOS packet flag. */
#define CREATE_XATTR_DIR 0x04
#endif
