/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_ZPL_VNOPS_OS_H
#define OZ_AROS_ZPL_VNOPS_OS_H
#include <fcntl.h>
#include <sys/fcntl.h>
#include <sys/zfs_znode.h>
#include <sys/pathname.h>

/* Private ZPL request bit, never forwarded to the AROS POSIX open() API.
 * A public DOS/management interface must translate and validate its flags. */
#define OZ_ZPL_IO_DIRECT 0x10000000
#ifndef O_DIRECT
#define O_DIRECT OZ_ZPL_IO_DIRECT
#endif
_Static_assert((O_DIRECT & (O_ACCMODE | O_CREAT | O_EXCL | O_TRUNC |
    O_APPEND | O_SYNC | O_DSYNC | O_RSYNC | O_NONBLOCK | O_ASYNC |
    O_CLOEXEC | O_DIRECTORY | O_NOFOLLOW)) == 0, "ZPL direct flag collision");

int zn_rlimit_fsize(uint64_t);
/* Internal kernel/replay write helper; never a DOS authentication boundary. */
int zfs_write_simple(znode_t *, const void *, size_t, int64_t, size_t *);
int zfs_space(znode_t *, int, flock64_t *, int, int64_t, cred_t *);
int zn_rlimit_fsize_uio(znode_t *, zfs_uio_t *);

/* Separate file-backed mapping cache, not the upstream ARC. The current
 * native policy permits byte I/O but no shared file pager: z_mapcnt must
 * remain zero and mapped-page update/flush paths fail loudly. Replace that
 * policy with real coherence before exposing shared mappings. */
boolean_t zn_has_cached_data(znode_t *, uint64_t, uint64_t);
void zn_flush_cached_data(znode_t *, boolean_t);
void zfs_zrele_async(znode_t *);

/* Native adapter entry points. Deliberately no Linux inode, dentry, page or
 * namespace arguments. Implementations are required before the write service
 * can link; these declarations are not successful placeholder operations. */
int zfs_lookup(znode_t *, char *, znode_t **, int, cred_t *, int *, pathname_t *);
int zfs_create(znode_t *, char *, vattr_t *, int, int, znode_t **, cred_t *,
    int, vsecattr_t *, zidmap_t *);
int zfs_remove(znode_t *, char *, cred_t *, int);
int zfs_mkdir(znode_t *, char *, vattr_t *, znode_t **, cred_t *, int,
    vsecattr_t *, zidmap_t *);
int zfs_rmdir(znode_t *, char *, znode_t *, cred_t *, int);
int zfs_setattr(znode_t *, vattr_t *, int, cred_t *, zidmap_t *);
/* Internal upstream request flags; public APIs must translate their flags. */
#define RENAME_NOREPLACE (1U << 0)
#define RENAME_EXCHANGE (1U << 1)
#define RENAME_WHITEOUT (1U << 2)
int zfs_rename(znode_t *, char *, znode_t *, char *, cred_t *, int, uint64_t,
    vattr_t *, zidmap_t *);
/* Trusted DOS replacement of a prepared empty file in the same directory.
 * Validate identities and caller policy while both directory entries lock. */
struct oz_rename_guard {
    uint64_t source, source_generation, target, target_generation;
    int (*check)(void *);
    void *context;
};
int oz_zfs_replace(znode_t *,char *,char *,cred_t *,const struct oz_rename_guard *);
int zfs_symlink(znode_t *, char *, vattr_t *, char *, znode_t **, cred_t *, int,
    zidmap_t *);
int zfs_link(znode_t *, znode_t *, char *, cred_t *, int);
int zfs_readlink(znode_t *, zfs_uio_t *, cred_t *);
#endif
