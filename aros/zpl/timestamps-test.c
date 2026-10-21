/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>

int
oz_timestamps_test(void)
{
    zfsvfs_t fs = { .z_atime = B_TRUE };
    znode_t zp = { .z_zfsvfs = &fs };
    mutex_init(&zp.z_lock, NULL, MUTEX_DEFAULT, NULL);
    inode_timespec_t now = {100000, 123};
    int error = 0;
    oz_znode_accessed_at(&zp, &now);
    if (!zp.z_atime_dirty || zp.z_atime.tv_sec != now.tv_sec ||
        zp.z_atime.tv_nsec != now.tv_nsec) error = 1;
    for (unsigned mode = 0; mode < 4; mode++) {
        fs.z_atime = mode != 0; fs.z_readonly = mode == 1;
        fs.z_issnap = mode == 2; fs.z_unmounted = mode == 3;
        zp.z_atime_dirty = B_FALSE;
        now.tv_sec++;
        oz_znode_accessed_at(&zp, &now);
        if (zp.z_atime_dirty || zp.z_atime.tv_sec != 100000) error = 2;
    }
    fs.z_atime = B_TRUE; fs.z_unmounted = B_FALSE;
    fs.z_relatime = B_TRUE;
    zp.z_mtime = zp.z_ctime = (inode_timespec_t){99999, 999};
    oz_znode_accessed_at(&zp, &now);
    if (zp.z_atime_dirty) error = 3;
    now.tv_sec = 186399;
    oz_znode_accessed_at(&zp, &now);
    if (zp.z_atime_dirty) error = 4;
    now.tv_sec = 186400;
    now.tv_nsec = 122; /* One nanosecond short of the full 24 hours. */
    oz_znode_accessed_at(&zp, &now);
    if (zp.z_atime_dirty) error = 9;
    now.tv_nsec = 123;
    oz_znode_accessed_at(&zp, &now);
    if (!zp.z_atime_dirty || zp.z_atime.tv_sec != 186400) error = 5;
    zp.z_atime_dirty = B_FALSE;
    zp.z_ctime = zp.z_atime; /* Equal ctime must trigger relatime update. */
    now.tv_nsec++;
    oz_znode_accessed_at(&zp, &now);
    if (!zp.z_atime_dirty || zp.z_atime.tv_nsec != now.tv_nsec) error = 6;
    zp.z_atime_dirty = B_FALSE;
    now.tv_sec = 100; /* A backward wall-clock jump is not an expired age. */
    oz_znode_accessed_at(&zp, &now);
    if (zp.z_atime_dirty) error = 10;
    zp.z_atime_dirty = B_TRUE;
    uint64_t mt[2], ct[2];
    fs.z_use_fuids = B_TRUE;
    zfs_tstamp_update_setup(&zp, CONTENT_MODIFIED, mt, ct);
    if (zp.z_seq != 1 || !zp.z_atime_dirty || mt[0] != ct[0] || mt[1] != ct[1] ||
        mt[0] != (uint64_t)zp.z_mtime.tv_sec || mt[1] != (uint64_t)zp.z_mtime.tv_nsec ||
        ct[0] != (uint64_t)zp.z_ctime.tv_sec ||
        (zp.z_pflags & (ZFS_ARCHIVE | ZFS_AV_MODIFIED)) != (ZFS_ARCHIVE | ZFS_AV_MODIFIED))
        error = 7;
    fs.z_use_fuids = B_FALSE; zp.z_pflags = 0;
    mutex_enter(&zp.z_lock);
    zfs_tstamp_update_setup(&zp, STATE_CHANGED, NULL, ct);
    if (!MUTEX_HELD(&zp.z_lock)) error = 11;
    mutex_exit(&zp.z_lock);
    if (zp.z_seq != 2 || zp.z_pflags) error = 8;
    mutex_destroy(&zp.z_lock);
    return (error);
}
