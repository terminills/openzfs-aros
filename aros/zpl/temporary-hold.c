/* SPDX-License-Identifier: CDDL-1.0 */
/* Native counterpart of upstream zfsvfs_create_hold/free. Temporary ioctl
 * state is never published as a DOS mount and never owns a ZIL or znodes.
 * Success retains dmu_objset_hold's pool config read lock. The dispatcher
 * drops that lock before teardown acquisition, reacquires it before rele,
 * and finally calls zfsvfs_free after releasing the objset. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <sys/zap.h>
#include <sys/zfs_sa.h>
#include "read-mount.h"

void zfsvfs_free(zfsvfs_t *fs)
{
    if (!fs) return;
    ASSERT(fs->z_use_hold);
    ASSERT0(fs->z_native_refs);
    ASSERT0P(fs->z_log);
    ASSERT(list_is_empty(&fs->z_all_znodes));
    /* The objset may already be gone. Only mount-private allocations here. */
    zfs_fuid_destroy(fs);
    list_destroy(&fs->z_all_znodes);
    mutex_destroy(&fs->z_znodes_lock);
    mutex_destroy(&fs->z_lock);
    rw_destroy(&fs->z_fuid_lock);
    rw_destroy(&fs->z_teardown_inactive_lock);
    ZFS_TEARDOWN_DESTROY(fs);
    oz_mount_refs_fini(fs);
    kmem_free(fs, sizeof(*fs));
}

int zfsvfs_create_hold(const char *name, zfsvfs_t **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!name || !*name) return EINVAL;
    zfsvfs_t *fs = kmem_zalloc(sizeof(*fs), KM_SLEEP);
    fs->z_use_hold = B_TRUE;
    mutex_init(&fs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
    mutex_init(&fs->z_lock, NULL, MUTEX_DEFAULT, NULL);
    list_create(&fs->z_all_znodes, sizeof(znode_t), offsetof(znode_t,z_link_node));
    rw_init(&fs->z_fuid_lock, NULL, RW_DEFAULT, NULL);
    rw_init(&fs->z_teardown_inactive_lock, NULL, RW_DEFAULT, NULL);
    ZFS_TEARDOWN_INIT(fs);
    oz_mount_refs_init(fs);
    objset_t *os = NULL;
    int error = dmu_objset_hold(name, fs, &os);
    if (!error && dmu_objset_type(os) != DMU_OST_ZFS) error = EINVAL;
    if (!error) error = oz_zpl_mount_metadata(fs, os);
    uint64_t sa = 0;
    if (!error && fs->z_use_sa)
        error = zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa);
    if (!error) error = sa_setup(os, sa, zfs_attr_table, ZPL_END, &fs->z_attr_table);
    if (!error && fs->z_version >= ZPL_VERSION_SA)
        sa_register_update_callback(os, zfs_sa_upgrade);
    if (error) {
        if (os) dmu_objset_rele(os, fs);
        zfsvfs_free(fs);
        return error;
    }
    fs->z_drain_task = TASKQID_INVALID;
    fs->z_drain_cancel = B_TRUE;
    *out = fs;
    return 0;
}
