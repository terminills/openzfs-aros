/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_tx.h>
#include <sys/spa.h>

static boolean_t
not_after(const inode_timespec_t *a, const inode_timespec_t *b)
{
    return (a->tv_sec < b->tv_sec ||
        (a->tv_sec == b->tv_sec && a->tv_nsec <= b->tv_nsec));
}

/* Native cached atime update. Mount properties are stable under the caller's
 * teardown hold. This function takes z_lock; callers must not already hold it.
 * The future inactive/sync path must persist dirty atime, never discard it. */
void
oz_znode_accessed_at(znode_t *zp, const inode_timespec_t *now)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    ASSERT3S(now->tv_nsec, >=, 0);
    ASSERT3S(now->tv_nsec, <, NANOSEC);
    if (!fs->z_atime || fs->z_readonly || fs->z_issnap || fs->z_unmounted)
        return;
    mutex_enter(&zp->z_lock);
    uint64_t age = (uint64_t)now->tv_sec - (uint64_t)zp->z_atime.tv_sec;
    boolean_t expired = now->tv_sec >= zp->z_atime.tv_sec &&
        (age > 86400 || (age == 86400 && now->tv_nsec >= zp->z_atime.tv_nsec));
    if (!fs->z_relatime || not_after(&zp->z_atime, &zp->z_mtime) ||
        not_after(&zp->z_atime, &zp->z_ctime) || expired) {
        zp->z_atime = *now;
        zp->z_atime_dirty = B_TRUE;
    }
    mutex_exit(&zp->z_lock);
}

void
oz_znode_accessed(znode_t *zp)
{
    inode_timespec_t now;
    gethrestime(&now);
    oz_znode_accessed_at(zp, &now);
}

/* Upstream transaction callers provide arrays for the timestamps they will
 * write through SA. Serialize native cached metadata too. A pending atime
 * remains dirty: writing mtime/ctime does not imply atime was persisted. */
void
zfs_tstamp_update_setup(znode_t *zp, uint_t flag, uint64_t mtime[2],
    uint64_t ctime[2])
{
    inode_timespec_t now;
    gethrestime(&now);
    ASSERT(!(flag & ATTR_MTIME) || mtime != NULL);
    ASSERT(!(flag & ATTR_CTIME) || ctime != NULL);
    /* ACL mutation already holds z_lock; buffered writes may not. Preserve
     * the caller's ownership rather than recursively locking a plain mutex. */
    boolean_t held = MUTEX_HELD(&zp->z_lock);
    if (!held) mutex_enter(&zp->z_lock);
    zp->z_seq++;
    if (flag & ATTR_ATIME) {
        zp->z_atime = now;
        zp->z_atime_dirty = B_TRUE;
    }
    if (flag & ATTR_MTIME) {
        ZFS_TIME_ENCODE(&now, mtime);
        zp->z_mtime = now;
        if (ZTOZSB(zp)->z_use_fuids)
            zp->z_pflags |= ZFS_ARCHIVE | ZFS_AV_MODIFIED;
    }
    if (flag & ATTR_CTIME) {
        ZFS_TIME_ENCODE(&now, ctime);
        zp->z_ctime = now;
        if (ZTOZSB(zp)->z_use_fuids)
            zp->z_pflags |= ZFS_ARCHIVE;
    }
    if (!held) mutex_exit(&zp->z_lock);
}

/* Caller retains the mount teardown hold and znode/SA lifetime throughout.
 * Commit schedules a TXG write; it is not a synchronous durability guarantee.
 * SA updates the existing native or legacy atime attribute in place; this
 * does not request or claim a legacy-to-SA format upgrade. */
int
oz_znode_flush_atime(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    if (fs->z_unmounted) return (EIO);
    if (fs->z_readonly || fs->z_issnap) return (EROFS);
    if (!fs->z_os || !fs->z_attr_table || !zp->z_sa_hdl) return (EINVAL);
    if (zfs_is_readonly(fs))
        return (EROFS);
    mutex_enter(&zp->z_lock);
    boolean_t dirty = zp->z_atime_dirty;
    boolean_t unlinked = zp->z_unlinked;
    mutex_exit(&zp->z_lock);
    if (unlinked) return (ENOENT);
    if (!dirty) return (0);
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) {
        dmu_tx_abort(tx);
        return (error);
    }
    /* Assign before taking the metadata lock: assignment may wait for a TXG.
     * Recheck state and encode the latest timestamp under that lock, so a
     * concurrent access cannot be lost by clearing an older dirty snapshot. */
    mutex_enter(&zp->z_lock);
    if (zp->z_unlinked) {
        error = ENOENT;
    } else if (zp->z_atime_dirty) {
        uint64_t stamp[2];
        ZFS_TIME_ENCODE(&zp->z_atime, stamp);
        error = sa_update(zp->z_sa_hdl, SA_ZPL_ATIME(fs), stamp,
            sizeof (stamp), tx);
        if (!error) zp->z_atime_dirty = B_FALSE;
    }
    mutex_exit(&zp->z_lock);
    /* An assigned transaction must be committed, including on SA error. */
    dmu_tx_commit(tx);
    return (error);
}
