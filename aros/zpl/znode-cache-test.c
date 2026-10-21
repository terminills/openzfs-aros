/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <pthread.h>
int oz_acl_read_test(znode_t *, uint32_t);

/* Exercise the actual upstream vnode read, including rejection paths which
 * the DOS exact-length wrapper intentionally screens out. */
static int
upstream_read_test(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    boolean_t atime = fs->z_atime;
    fs->z_atime = B_FALSE;
    ZFS_TEARDOWN_INIT(fs);
    unsigned char expected[16], actual[16];
    int error = dmu_read(fs->z_os, zp->z_id, 0, sizeof (expected), expected, 0);
    struct iovec vec = {actual, sizeof (actual)};
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, sizeof (actual), 0);
    if (!error) error = zfs_read(zp, &u, 0, NULL);
    if (!error && (u.uio_resid || memcmp(actual, expected, sizeof (actual)))) error = EIO;
    uint64_t flags = zp->z_pflags;
    zp->z_pflags |= ZFS_AV_QUARANTINED;
    memset(actual, 0xa5, sizeof (actual));
    zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, sizeof (actual), 0);
    if (!error && (zfs_read(zp, &u, 0, NULL) != EACCES ||
        u.uio_resid != sizeof (actual) || actual[0] != 0xa5)) error = EIO;
    zp->z_pflags = flags;
    zfs_uio_iovec_init(&u, &vec, 1, -1, UIO_SYSSPACE, sizeof (actual), 0);
    if (!error && zfs_read(zp, &u, 0, NULL) != EINVAL) error = EIO;
    zfs_uio_iovec_init(&u, &vec, 1, zp->z_size, UIO_SYSSPACE, sizeof (actual), 0);
    if (!error && (zfs_read(zp, &u, 0, NULL) || u.uio_resid != sizeof (actual))) error = EIO;
    fs->z_unmounted = B_TRUE;
    if (!error && zfs_read(zp, &u, 0, NULL) != EIO) error = EIO;
    fs->z_unmounted = B_FALSE;
    if (!error && (zn_has_cached_data(zp, 0, UINT64_MAX) ||
        mappedread(zp, 1, &u) != ENOTSUP || u.uio_resid != sizeof (actual))) error = EIO;
    ZFS_TEARDOWN_DESTROY(fs);
    fs->z_atime = atime;
    return (error);
}

struct worker { zfsvfs_t *fs; znode_t *expected; int error; };
struct gate { pthread_mutex_t lock; pthread_cond_t cv; int go; };
struct first_worker {
    struct gate *gate; zfsvfs_t *fs; uint64_t object; znode_t *result; int error;
};
static void *
first_lookup(void *arg)
{
    struct first_worker *w = arg;
    pthread_mutex_lock(&w->gate->lock);
    while (!w->gate->go) pthread_cond_wait(&w->gate->cv, &w->gate->lock);
    pthread_mutex_unlock(&w->gate->lock);
    w->error = zfs_zget(w->fs, w->object, &w->result);
    return (NULL);
}

static int
competing_loaders(zfsvfs_t *fs, uint64_t object, znode_t **out)
{
    struct gate gate;
    int error = pthread_mutex_init(&gate.lock, NULL);
    if (error) return (error);
    error = pthread_cond_init(&gate.cv, NULL);
    if (error) { pthread_mutex_destroy(&gate.lock); return (error); }
    gate.go = 0;
    pthread_t threads[4];
    struct first_worker workers[4];
    unsigned started = 0;
    for (; started < 4; started++) {
        workers[started] = (struct first_worker){&gate, fs, object, NULL, 0};
        error = pthread_create(&threads[started], NULL, first_lookup, &workers[started]);
        if (error) break;
    }
    pthread_mutex_lock(&gate.lock);
    gate.go = 1;
    pthread_cond_broadcast(&gate.cv);
    pthread_mutex_unlock(&gate.lock);
    for (unsigned i = 0; i < started; i++) {
        int joined = pthread_join(threads[i], NULL);
        if (!error) error = joined ? joined : workers[i].error;
    }
    if (!error) {
        *out = workers[0].result;
        if (!*out || (*out)->z_refs != 4) error = EIO;
        for (unsigned i = 1; i < started; i++)
            if (workers[i].result != *out) error = EIO;
    }
    for (unsigned i = 0; i < started; i++)
        if (workers[i].result && (i != 0 || error))
            zfs_zrele_async(workers[i].result);
    if (error) *out = NULL;
    pthread_cond_destroy(&gate.cv);
    pthread_mutex_destroy(&gate.lock);
    return (error);
}
static void *
lookup_worker(void *arg)
{
    struct worker *w = arg;
    for (unsigned i = 0; i < 100; i++) {
        znode_t *zp = NULL;
        int error = zfs_zget(w->fs, w->expected->z_id, &zp);
        if (error || zp != w->expected) {
            if (zp) oz_znode_rele(zp);
            w->error = error ? error : EIO;
            break;
        }
        oz_znode_hold(zp);
        oz_znode_rele(zp);
        oz_znode_rele(zp);
    }
    return (NULL);
}

int
oz_znode_cache_test(zfsvfs_t *fs, uint64_t object)
{
    oz_znode_cache_init(fs);
    znode_t *first = NULL, *second = NULL;
    int error = competing_loaders(fs, object, &first);
    if (!error && (!S_ISREG(first->z_mode) || !first->z_gen ||
        first->z_links != 1 || !first->z_size || first->z_refs != 1)) error = EIO;
    if (!error) {
        dmu_object_info_t info;
        error = dmu_object_info(fs->z_os, object, &info);
        /* Upstream VFS accounting includes dnode slots; raw object-info
         * physical blocks alone excludes that metadata allocation. */
        uint64_t expected_blocks = error ? 0 : info.doi_physical_blocks_512 +
            info.doi_dnodesize / 512;
        if (!error && first->z_blocks != expected_blocks) error = EIO;
        if (!error) {
            uint64_t size = first->z_size;
            mode_t mode = first->z_mode;
            mutex_enter(&first->z_lock);
            first->z_blocks = UINT64_MAX;
            zfs_znode_update_vfs(first);
            if (!MUTEX_HELD(&first->z_lock) ||
                first->z_blocks != expected_blocks ||
                first->z_size != size || first->z_mode != mode) error = EIO;
            mutex_exit(&first->z_lock);
        }
        znode_t control = {.z_is_ctldir = B_TRUE, .z_blocks = 55};
        zfs_znode_update_vfs(&control);
        if (control.z_blocks != 55) error = EIO;
    }
    if (!error) error = oz_acl_read_test(first, ACE_ALL_PERMS);
    if (!error) error = upstream_read_test(first);
    if (!error) error = zfs_zget(fs, object, &second);
    if (!error && (first != second || first->z_refs != 2)) error = EIO;
    if (second) oz_znode_rele(second);
    if (!error) {
        (void) oz_znode_cache_trim(fs, 0);
        if (first->z_refs != 1 || !first->z_sa_hdl) error = EIO;
    }
    if (!error && oz_znode_cache_fini(fs) != EBUSY) error = EIO;
    if (!error && oz_znode_cache_abort_unpublished(fs) != EBUSY) error = EIO;
    if (!error) {
        pthread_t threads[4];
        struct worker workers[4];
        unsigned started = 0;
        for (; started < 4; started++) {
            workers[started] = (struct worker){fs, first, 0};
            error = pthread_create(&threads[started], NULL,
                lookup_worker, &workers[started]);
            if (error) break;
        }
        for (unsigned i = 0; i < started; i++) {
            int joined = pthread_join(threads[i], NULL);
            if (!error) error = joined ? joined : workers[i].error;
        }
        if (!error && first->z_refs != 1) error = EIO;
    }
    if (!error) {
        fs->z_unmounted = B_TRUE;
        second = first;
        if (zfs_zget(fs, object, &second) != EIO || second) error = EIO;
        fs->z_unmounted = B_FALSE;
        second = first;
        if (!zfs_zget(fs, 999999, &second) || second) error = EIO;
    }
    /* Last-consumer async release must retain the idle mount-owned node;
     * the following dirty-refusal/reacquisition checks use that same node. */
    if (first) zfs_zrele_async(first);
    if (!error) {
        first->z_atime_dirty = B_TRUE;
        (void) oz_znode_cache_trim(fs, 0);
        if (!first->z_atime_dirty || !first->z_sa_hdl) error = EIO;
        if (oz_znode_cache_fini(fs) != EBUSY) error = EIO;
        /* Cache-wide sync must not report ENOENT for an unlinked dirty node.
         * Do not clear its dirty state or weaken teardown admission. */
        first->z_unlinked = B_TRUE;
        if (oz_znode_cache_sync(fs, B_TRUE) || !first->z_atime_dirty ||
            first->z_refs || oz_znode_cache_fini(fs) != EBUSY) error = EIO;
        first->z_unlinked = B_FALSE;
        if (zfs_is_readonly(fs)) {
            /* Injected dirty state must not become a write on cold imports. */
            if (oz_znode_cache_sync(fs, B_TRUE) != EROFS ||
                !first->z_atime_dirty || first->z_refs) error = EIO;
            first->z_atime_dirty = B_FALSE;
        } else {
            first->z_atime.tv_sec = 1700000002;
            first->z_atime.tv_nsec = 456;
            if (oz_znode_cache_sync(fs, B_TRUE) || first->z_atime_dirty ||
                first->z_refs) error = EIO;
            uint64_t stamp[2];
            if (sa_lookup(first->z_sa_hdl, SA_ZPL_ATIME(fs), stamp,
                sizeof (stamp)) || stamp[0] != 1700000002 || stamp[1] != 456)
                error = EIO;
        }
        if (!error && oz_znode_cache_sync(fs, B_TRUE)) error = EIO;
        if (!error) error = zfs_zget(fs, object, &second);
        if (!error && second != first) error = EIO;
        if (second) oz_znode_rele(second);
    }
    if (!error) {
        uint64_t generation = first->z_gen, size = first->z_size;
        mutex_enter(&fs->z_znodes_lock);
        fs->z_cache_walkers++;
        mutex_exit(&fs->z_znodes_lock);
        if (oz_znode_cache_trim(fs, 0) || first->z_gen != generation) error = EIO;
        mutex_enter(&fs->z_znodes_lock);
        fs->z_cache_walkers--;
        mutex_exit(&fs->z_znodes_lock);
        first = NULL; /* No borrowed pointer survives successful reclamation. */
        if (!error && !oz_znode_cache_trim(fs, 0)) error = EIO;
        if (!error && !list_is_empty(&fs->z_all_znodes)) error = EIO;
        second = NULL;
        if (!error) error = zfs_zget(fs, object, &second);
        if (!error && (second->z_gen != generation || second->z_size != size)) error = EIO;
        if (second) oz_znode_rele(second);
    }
    int cleanup = oz_znode_cache_fini(fs);
    if (!error && !cleanup) {
        /* Simulate unpublished recovery cache state. Discarding it must not
         * write atime or delete the on-disk object, even on readonly boots. */
        oz_znode_cache_init(fs);
        first = NULL;
        error = zfs_zget(fs, object, &first);
        inode_timespec_t saved_atime = {0};
        if (!error) {
            saved_atime = first->z_atime;
            first->z_atime.tv_sec = 42;
            first->z_atime_dirty = B_TRUE;
            first->z_unlinked = B_TRUE;
            oz_znode_rele(first);
        }
        cleanup = oz_znode_cache_abort_unpublished(fs);
        if (!error && !cleanup) {
            oz_znode_cache_init(fs);
            first = NULL;
            error = zfs_zget(fs, object, &first);
            if (!error && (first->z_unlinked ||
                first->z_atime.tv_sec != saved_atime.tv_sec ||
                first->z_atime.tv_nsec != saved_atime.tv_nsec)) error = EIO;
            if (first) oz_znode_rele(first);
            cleanup = oz_znode_cache_fini(fs);
        }
    }
    return (error ? error : cleanup);
}
