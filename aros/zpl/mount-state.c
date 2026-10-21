/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_quota.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/zap.h>

/* Native znodes/caches and DOS ports are mount-owned, not global BSD/Linux
 * VFS objects. The global ZPL lifecycle still must register the real upstream
 * bonus-buffer decoder: the DMU uses it for user/group/project accounting.
 * Start/stop run on the engine owner. Finalization follows kernel_fini(), so
 * no pool sync or taskq can still invoke the callback being removed. */
static boolean_t native_zpl_initialized;
void zfs_init(void)
{
    VERIFY(!native_zpl_initialized);
    dmu_objset_register_type(DMU_OST_ZFS, zpl_get_file_info);
    native_zpl_initialized = B_TRUE;
}

void zfs_fini(void)
{
    VERIFY(native_zpl_initialized);
    dmu_objset_register_type(DMU_OST_ZFS, NULL);
    native_zpl_initialized = B_FALSE;
}

void oz_mount_refs_init(zfsvfs_t *fs)
{
    fs->z_native_refs = 0;
    mutex_init(&fs->z_native_ref_lock, NULL, MUTEX_DEFAULT, NULL);
}

void oz_mount_refs_fini(zfsvfs_t *fs)
{
    ASSERT0(fs->z_native_refs);
    mutex_destroy(&fs->z_native_ref_lock);
}

int oz_mount_publish(zfsvfs_t *fs)
{
    if (!fs || !fs->z_os) return EINVAL;
    objset_t *os = fs->z_os;
    dsl_dir_t *dd = dmu_objset_ds(os)->ds_dir;
    /* Same lock order as the waiter: activity before publication. */
    mutex_enter(&dd->dd_activity_lock);
    mutex_enter(&os->os_user_ptr_lock);
    mutex_enter(&fs->z_native_ref_lock);
    int error = dmu_objset_get_user(os) || fs->z_native_refs ? EBUSY : 0;
    if (!error) {
        dd->dd_activity_cancelled = B_FALSE;
        fs->z_native_refs = 1;
        dmu_objset_set_user(os, fs);
    }
    mutex_exit(&fs->z_native_ref_lock);
    mutex_exit(&os->os_user_ptr_lock);
    mutex_exit(&dd->dd_activity_lock);
    return error;
}

int oz_mount_unpublish(zfsvfs_t *fs)
{
    if (!fs || !fs->z_os) return EINVAL;
    objset_t *os = fs->z_os;
    mutex_enter(&os->os_user_ptr_lock);
    mutex_enter(&fs->z_native_ref_lock);
    int error = fs->z_native_refs > 1 ? EBUSY : 0;
    boolean_t detached = B_FALSE;
    if (!error && fs->z_native_refs) {
        ASSERT(dmu_objset_get_user(os) == fs);
        dmu_objset_set_user(os, NULL);
        fs->z_native_refs = 0;
        detached = B_TRUE;
    }
    mutex_exit(&fs->z_native_ref_lock);
    mutex_exit(&os->os_user_ptr_lock);
    /* Never wait while holding publication/ref locks: waiters need to
     * inspect the now-unpublished objset before releasing their long holds. */
    if (detached)
        dsl_dir_cancel_waiters(dmu_objset_ds(os)->ds_dir);
    return error;
}

/* Upstream getzfsvfs_impl holds os_user_ptr_lock across lookup and acquire. */
int zfs_vfs_ref(zfsvfs_t **out)
{
    if (!out || !*out) return ESRCH;
    zfsvfs_t *fs = *out;
    ASSERT(MUTEX_HELD(&fs->z_os->os_user_ptr_lock));
    mutex_enter(&fs->z_native_ref_lock);
    int error = !fs->z_native_refs ? ESRCH :
        fs->z_native_refs == SIZE_MAX ? EOVERFLOW : 0;
    if (!error) ++fs->z_native_refs;
    mutex_exit(&fs->z_native_ref_lock);
    return error;
}

void zfs_vfs_rele(zfsvfs_t *fs)
{
    mutex_enter(&fs->z_native_ref_lock);
    ASSERT3U(fs->z_native_refs, >, 1);
    --fs->z_native_refs;
    mutex_exit(&fs->z_native_ref_lock);
}

boolean_t zfs_vfs_held(zfsvfs_t *fs)
{
    mutex_enter(&fs->z_native_ref_lock);
    boolean_t held = fs->z_native_refs != 0;
    mutex_exit(&fs->z_native_ref_lock);
    return held;
}

/* Caller holds mount lifetime/teardown protection. z_readonly is the native
 * effective mount policy (including the dataset readonly property); mount
 * initialization and property callbacks must maintain it. Engine restrictions
 * remain authoritative even if the native cached flags are stale. */
boolean_t
zfs_is_readonly(zfsvfs_t *fs)
{
    if (fs == NULL || fs->z_os == NULL || fs->z_unmounted ||
        fs->z_readonly || fs->z_issnap)
        return (B_TRUE);
    return (dmu_objset_is_snapshot(fs->z_os) ||
        !spa_writeable(dmu_objset_spa(fs->z_os)));
}

boolean_t
zfs_get_vfs_flag_unmounted(objset_t *os)
{
    ASSERT3U(dmu_objset_type(os), ==, DMU_OST_ZFS);
    mutex_enter(&os->os_user_ptr_lock);
    const zfsvfs_t *fs = dmu_objset_get_user(os);
    boolean_t unmounted = fs != NULL && fs->z_unmounted;
    mutex_exit(&os->os_user_ptr_lock);
    return (unmounted);
}

/* Called by upstream dsl_dir_wait with its activity lock and dataset hold.
 * Snapshot mount state under the publication lock: unpublish can free the
 * native mount after we drop it, so no zfsvfs pointer escapes this section.
 * The caller's dataset hold keeps the objset alive for the ZAP query. */
int
oz_zpl_deleteq_pending(dsl_dataset_t *ds, boolean_t *pending)
{
    objset_t *os;
    *pending = B_FALSE;
    int error = dmu_objset_from_ds(ds, &os);
    if (error != 0 || dmu_objset_type(os) != DMU_OST_ZFS)
        return error;

    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs = dmu_objset_get_user(os);
    boolean_t writable = fs != NULL && !zfs_is_readonly(fs);
    uint64_t unlinked = writable ? fs->z_unlinkedobj : 0;
    mutex_exit(&os->os_user_ptr_lock);
    if (!writable)
        return 0;

    uint64_t count;
    error = zap_count(os, unlinked, &count);
    if (error == 0)
        *pending = count != 0;
    return error;
}
