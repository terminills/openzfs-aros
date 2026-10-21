/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>
int oz_acl_sa_test(znode_t *, boolean_t);
int oz_acl_persistence_test(zfsvfs_t *, uint64_t, boolean_t, uint32_t);
int oz_znode_cache_test(zfsvfs_t *, uint64_t);
int oz_legacy_znode_test(zfsvfs_t *);

/* The fixture starts with one 4096-byte data block. Exercise the upstream
 * growth algorithm and verify the resulting dnode geometry on cold import. */
static int
blocksize_test(znode_t *zp, boolean_t write)
{
    u_longlong_t ignored;
    dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &zp->z_blksz, &ignored);
    if (!write) return (zp->z_blksz == 8192 ? 0 : EIO);
    if (zp->z_blksz != 4096) return (EIO);
    zfsvfs_t *fs = ZTOZSB(zp);
    int error = sa_lookup(zp->z_sa_hdl, SA_ZPL_SIZE(fs), &zp->z_size,
        sizeof (zp->z_size));
    if (error) return (error);
    zfs_rangelock_init(&zp->z_rangelock, NULL, NULL);
    zfs_locked_range_t *lr = zfs_rangelock_enter(&zp->z_rangelock,
        0, UINT64_MAX, RL_WRITER);
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_write(tx, zp->z_id, 0, 8192);
    dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
    error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) dmu_tx_abort(tx);
    else {
        zfs_grow_blocksize(zp, 2048, tx); /* no shrinking */
        if (zp->z_blksz != 4096) error = EIO;
        uint64_t saved_size = zp->z_size;
        zp->z_size = 4097; /* cached multi-block guard, no disk mutation */
        zfs_grow_blocksize(zp, 8192, tx);
        if (zp->z_blksz != 4096) error = EIO;
        zp->z_size = saved_size;
        zfs_grow_blocksize(zp, 8192, tx);
        if (zp->z_blksz != 8192 || zp->z_size != saved_size) error = EIO;
        dmu_tx_commit(tx);
    }
    zfs_rangelock_exit(lr);
    zfs_rangelock_fini(&zp->z_rangelock);
    return (error);
}

static int
mount_state_test(objset_t *os, zfsvfs_t *fs)
{
    zfsvfs_t detached = {0};
    if (!zfs_is_readonly(NULL) || !zfs_is_readonly(&detached))
        return (EIO);
    boolean_t saved_readonly = fs->z_readonly;
    boolean_t saved_snapshot = fs->z_issnap;
    fs->z_readonly = B_FALSE;
    fs->z_issnap = B_FALSE;
    boolean_t engine_readonly = !spa_writeable(dmu_objset_spa(os)) ||
        dmu_objset_is_snapshot(os);
    int error = zfs_is_readonly(fs) != engine_readonly ? EIO : 0;
    fs->z_readonly = B_TRUE;
    if (!zfs_is_readonly(fs)) error = EIO;
    fs->z_readonly = B_FALSE;
    fs->z_issnap = B_TRUE;
    if (!zfs_is_readonly(fs)) error = EIO;
    fs->z_issnap = saved_snapshot;
    fs->z_readonly = saved_readonly;
    mutex_enter(&os->os_user_ptr_lock);
    if (dmu_objset_get_user(os)) {
        mutex_exit(&os->os_user_ptr_lock);
        return (EBUSY);
    }
    dmu_objset_set_user(os, fs);
    mutex_exit(&os->os_user_ptr_lock);
    if (zfs_get_vfs_flag_unmounted(os)) error = EIO;
    mutex_enter(&os->os_user_ptr_lock);
    fs->z_unmounted = B_TRUE;
    mutex_exit(&os->os_user_ptr_lock);
    if (!zfs_get_vfs_flag_unmounted(os)) error = EIO;
    if (!zfs_is_readonly(fs)) error = EIO;
    mutex_enter(&os->os_user_ptr_lock);
    dmu_objset_set_user(os, NULL);
    fs->z_unmounted = B_FALSE;
    mutex_exit(&os->os_user_ptr_lock);
    if (zfs_get_vfs_flag_unmounted(os)) error = EIO;
    return (error);
}

/* Actual native-SA persistence, separate from DOS-handler write support. */
int
oz_atime_persistence_test(boolean_t write)
{
    zfsvfs_t fs = { .z_atime = B_TRUE, .z_readonly = !write };
    znode_t zp = { .z_zfsvfs = &fs, .z_is_sa = B_TRUE };
    int error = dmu_objset_own("qemu_mirror/files", DMU_OST_ZFS,
        !write, B_FALSE, &fs, &fs.z_os);
    if (error) return (error);
    mutex_init(&zp.z_lock, NULL, MUTEX_DEFAULT, NULL);
    uint64_t sa, root, entry;
    error = mount_state_test(fs.z_os, &fs);
    if (!error) error = zap_lookup(fs.z_os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa);
    if (!error) error = sa_setup(fs.z_os, sa, zfs_attr_table, ZPL_END, &fs.z_attr_table);
    if (!error) error = zap_lookup(fs.z_os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1, &root);
    if (!error) error = zap_lookup(fs.z_os, root, "hello.txt", 8, 1, &entry);
    if (!error) {
        zp.z_id = ZFS_DIRENT_OBJ(entry);
        error = sa_handle_get(fs.z_os, zp.z_id, NULL, SA_HDL_PRIVATE, &zp.z_sa_hdl);
    }
    const inode_timespec_t expected = {1700000123, 456789123};
    if (!error) error = blocksize_test(&zp, write);
    if (!error) error = oz_acl_sa_test(&zp, write);
    if (!error) error = oz_znode_cache_test(&fs, zp.z_id);
    if (!error) error = oz_acl_persistence_test(&fs, zp.z_id, write, ACE_ALL_PERMS);
    if (!error) {
        uint64_t directory, nested;
        error = zap_lookup(fs.z_os, root, "subdir", 8, 1, &directory);
        if (!error) error = zap_lookup(fs.z_os, ZFS_DIRENT_OBJ(directory),
            "nested.txt", 8, 1, &nested);
        if (!error) error = oz_acl_persistence_test(&fs, ZFS_DIRENT_OBJ(nested),
            write, ACE_READ_DATA | ACE_READ_ACL | ACE_READ_ATTRIBUTES);
    }
    if (!error && write) error = oz_legacy_znode_test(&fs);
    if (!error && write) {
        oz_znode_accessed_at(&zp, &expected);
        fs.z_readonly = B_TRUE;
        if (oz_znode_flush_atime(&zp) != EROFS || !zp.z_atime_dirty) error = EIO;
        fs.z_readonly = B_FALSE;
        zp.z_unlinked = B_TRUE;
        if (oz_znode_flush_atime(&zp) != ENOENT || !zp.z_atime_dirty) error = EIO;
        zp.z_unlinked = B_FALSE;
        if (!error) error = oz_znode_flush_atime(&zp);
        if (!error && zp.z_atime_dirty) error = EIO;
        if (!error) error = oz_znode_flush_atime(&zp); /* clean is idempotent */
    }
    if (!error) {
        uint64_t actual[2];
        error = sa_lookup(zp.z_sa_hdl, SA_ZPL_ATIME((&fs)), actual, sizeof (actual));
        if (!error && (actual[0] != (uint64_t)expected.tv_sec ||
            actual[1] != (uint64_t)expected.tv_nsec)) error = EIO;
    }
    if (!error && !write) {
        /* A stale/misinitialized native mount flag must not permit a write
         * to a pool the engine itself imported read-only. */
        fs.z_readonly = B_FALSE;
        zp.z_atime = expected;
        zp.z_atime.tv_sec++;
        zp.z_atime_dirty = B_TRUE;
        if (oz_znode_flush_atime(&zp) != EROFS || !zp.z_atime_dirty) error = EIO;
    }
    if (zp.z_sa_hdl) sa_handle_destroy(zp.z_sa_hdl);
    mutex_destroy(&zp.z_lock);
    dmu_objset_disown(fs.z_os, B_FALSE, &fs);
    return (error);
}
