/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_ZPL_ZNODE_IMPL_H
#define OZ_AROS_ZPL_ZNODE_IMPL_H
#include <sys/zfs_vfsops.h>
#include <sys/zfs_rlock.h>
#include <sys/zfs_sa.h>

/* Cached native metadata; object lifetime is managed by the AROS adapter,
 * not by a fabricated host inode. Hold/release remain real external calls. */
#define ZNODE_OS_FIELDS \
    zfsvfs_t *z_zfsvfs; \
    uint64_t z_uid, z_gid, z_links, z_gen, z_blocks; \
    inode_timespec_t z_atime, z_mtime, z_ctime, z_btime; \
    uint64_t z_refs

#define ZTOZSB(zp) ((zp)->z_zfsvfs)
#define ZTOUID(zp) ((zp)->z_uid)
#define ZTOGID(zp) ((zp)->z_gid)
#define ZTONLNK(zp) ((zp)->z_links)
#define ZTOTYPE(zp) ((zp)->z_mode)
#define Z_ISBLK(t) S_ISBLK(t)
#define Z_ISCHR(t) S_ISCHR(t)
#define Z_ISLNK(t) S_ISLNK(t)
#define Z_ISDIR(t) S_ISDIR(t)
#define Z_ISDEV(t) (S_ISCHR(t) || S_ISBLK(t) || S_ISFIFO(t))

struct znode;
/* Initialize once before zget; finalize with exclusive mount lifetime access.
 * EBUSY retains the entire cache for retry/explicit dirty-resource cleanup. */
void oz_znode_cache_init(zfsvfs_t *);
int oz_znode_cache_fini(zfsvfs_t *);
int oz_znode_cache_abort_unpublished(zfsvfs_t *);
int oz_znode_cache_sync(zfsvfs_t *, boolean_t);
uint64_t oz_znode_cache_trim(zfsvfs_t *, uint64_t);
/* Synchronous reclamation: caller must exclude all concurrent mount users. */
int oz_unlinked_drain_exclusive(zfsvfs_t *);
void oz_znode_hold(struct znode *);
void oz_znode_rele(struct znode *);
void oz_znode_accessed(struct znode *);
void oz_znode_accessed_at(struct znode *, const inode_timespec_t *);
int oz_znode_flush_atime(struct znode *);
#define ZFS_ACCESSTIME_STAMP(fs, zp) oz_znode_accessed(zp)
#define zhold(zp) oz_znode_hold(zp)
#define zrele(zp) oz_znode_rele(zp)

static inline int
zfs_enter(zfsvfs_t *fs, const char *tag)
{
    ZFS_TEARDOWN_ENTER_READ(fs, tag);
    if (fs->z_unmounted) {
        ZFS_TEARDOWN_EXIT_READ(fs, tag);
        return (SET_ERROR(EIO));
    }
    return (0);
}

static inline void
zfs_exit(zfsvfs_t *fs, const char *tag)
{
    ZFS_TEARDOWN_EXIT_READ(fs, tag);
}

#define ZFS_TIME_ENCODE(tp, stamp) do { \
    (stamp)[0] = (uint64_t)(tp)->tv_sec; \
    (stamp)[1] = (uint64_t)(tp)->tv_nsec; \
} while (0)
#define ZFS_TIME_DECODE(tp, stamp) do { \
    (tp)->tv_sec = (time_t)(stamp)[0]; \
    (tp)->tv_nsec = (long)(stamp)[1]; \
} while (0)
#endif
