/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <sys/txg.h>

/* Native mount-owned cache. Idle nodes can be reclaimed at owner sync points.
 * Callers retain mount lifetime protection; cache finalization requires
 * exclusive mount access. No Linux inode lifetime is fabricated here. */
void
oz_znode_cache_init(zfsvfs_t *fs)
{
    mutex_init(&fs->z_znodes_lock, NULL, MUTEX_DEFAULT, NULL);
    fs->z_cache_walkers = 0;
    list_create(&fs->z_all_znodes, sizeof (znode_t),
        offsetof(znode_t, z_link_node));
}

/* Upstream zfs-2.4.4 zfs_rangelock_cb algorithm from zfs_znode_os.c.
 * Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Copyright (c) 2012, 2018 by Delphix. */
static void
range_callback(zfs_locked_range_t *range, void *arg)
{
    znode_t *zp = arg;
    if (range->lr_type == RL_APPEND) {
        range->lr_offset = zp->z_size;
        range->lr_type = RL_WRITER;
    }
    uint64_t end = MAX(zp->z_size, range->lr_offset + range->lr_length);
    if (zp->z_size <= zp->z_blksz && end > zp->z_blksz &&
        (!ISP2(zp->z_blksz) || zp->z_blksz < ZTOZSB(zp)->z_max_blksz)) {
        range->lr_offset = 0;
        range->lr_length = UINT64_MAX;
    }
}

static void
dispose(znode_t *zp)
{
    if (zp->z_acl_cached) zfs_acl_free(zp->z_acl_cached);
    if (zp->z_xattr_cached) nvlist_free(zp->z_xattr_cached);
    if (zp->z_sa_hdl) sa_handle_destroy(zp->z_sa_hdl);
    zfs_rangelock_fini(&zp->z_rangelock);
    rw_destroy(&zp->z_xattr_lock);
    mutex_destroy(&zp->z_acl_lock);
    rw_destroy(&zp->z_name_lock);
    rw_destroy(&zp->z_parent_lock);
    mutex_destroy(&zp->z_lock);
    kmem_free(zp, sizeof (*zp));
}

static int
load(zfsvfs_t *fs, uint64_t object, znode_t **out)
{
    dmu_object_info_t doi;
    int error = dmu_object_info(fs->z_os, object, &doi);
    if (error) return (error);
    if (doi.doi_bonus_type != DMU_OT_SA &&
        doi.doi_bonus_type != DMU_OT_ZNODE) return (ENOTSUP);
    if (doi.doi_bonus_type == DMU_OT_ZNODE &&
        doi.doi_bonus_size < sizeof (znode_phys_t)) return (ENOTSUP);
    znode_t *zp = kmem_zalloc(sizeof (*zp), KM_SLEEP);
    zp->z_zfsvfs = fs;
    zp->z_id = object;
    zp->z_is_sa = doi.doi_bonus_type == DMU_OT_SA;
    /* Upstream SA layout upgrades (including project-ID insertion) recover
     * the owning znode through this handle. Its lifetime ends with the node. */
    error = sa_handle_get(fs->z_os, object, zp, SA_HDL_PRIVATE, &zp->z_sa_hdl);
    if (error) { kmem_free(zp, sizeof (*zp)); return (error); }
    uint64_t mode, parent, times[4][2];
    sa_bulk_attr_t bulk[12];
    int n = 0;
#define ADD(attr, value) SA_ADD_BULK_ATTR(bulk, n, fs->z_attr_table[attr], NULL, value, sizeof (*(value)))
    ADD(ZPL_MODE, &mode);
    ADD(ZPL_GEN, &zp->z_gen);
    ADD(ZPL_SIZE, &zp->z_size);
    ADD(ZPL_LINKS, &zp->z_links);
    ADD(ZPL_FLAGS, &zp->z_pflags);
    ADD(ZPL_PARENT, &parent);
    ADD(ZPL_UID, &zp->z_uid);
    ADD(ZPL_GID, &zp->z_gid);
    ADD(ZPL_ATIME, &times[0]);
    ADD(ZPL_MTIME, &times[1]);
    ADD(ZPL_CTIME, &times[2]);
    ADD(ZPL_CRTIME, &times[3]);
#undef ADD
    error = sa_bulk_lookup(zp->z_sa_hdl, bulk, n);
    if (!error && (!zp->z_gen || mode > UINT32_MAX)) error = EIO;
    if (!error && (zp->z_pflags & ZFS_PROJID))
        error = sa_lookup(zp->z_sa_hdl, SA_ZPL_PROJID(fs), &zp->z_projid, 8);
    if (error) {
        sa_handle_destroy(zp->z_sa_hdl);
        kmem_free(zp, sizeof (*zp));
        return (error);
    }
    zp->z_mode = mode;
    zp->z_unlinked = zp->z_links == 0;
    zp->z_blksz = doi.doi_data_block_size;
    zp->z_dnodesize = doi.doi_dnodesize;
    zp->z_seq = 0x7A4653;
    zp->z_refs = 1;
    if (zp->z_pflags & ZFS_XATTR) zp->z_xattr_parent = parent;
    ZFS_TIME_DECODE(&zp->z_atime, times[0]);
    ZFS_TIME_DECODE(&zp->z_mtime, times[1]);
    ZFS_TIME_DECODE(&zp->z_ctime, times[2]);
    ZFS_TIME_DECODE(&zp->z_btime, times[3]);
    mutex_init(&zp->z_lock, NULL, MUTEX_DEFAULT, NULL);
    rw_init(&zp->z_parent_lock, NULL, RW_DEFAULT, NULL);
    rw_init(&zp->z_name_lock, NULL, RW_DEFAULT, NULL);
    mutex_init(&zp->z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
    rw_init(&zp->z_xattr_lock, NULL, RW_DEFAULT, NULL);
    zfs_rangelock_init(&zp->z_rangelock, range_callback, zp);
    zfs_znode_update_vfs(zp);
    *out = zp;
    return (0);
}

/* Native lifecycle counterparts of upstream zfs_znode_dmu_fini/rezget.
 * The lifecycle owner excludes native operations and holds teardown writer.
 * Retain node address, list membership, references and locks across rebind;
 * no stale SA/ACL/xattr data may survive into the resumed filesystem. */
void
zfs_znode_dmu_fini(znode_t *zp)
{
    ASSERT(ZFS_TEARDOWN_WRITE_HELD(ZTOZSB(zp)));
    ASSERT(!zp->z_atime_dirty && !zp->z_dirlocks && !zp->z_mapcnt);
    if (zp->z_sa_hdl) {
        sa_handle_destroy(zp->z_sa_hdl);
        zp->z_sa_hdl = NULL;
    }
}

int
zfs_rezget(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    ASSERT(ZFS_TEARDOWN_WRITE_HELD(fs));
    ASSERT0P(zp->z_sa_hdl);
    ASSERT(!zp->z_atime_dirty && !zp->z_dirlocks && !zp->z_mapcnt);
    if (zp->z_acl_cached) {
        zfs_acl_free(zp->z_acl_cached);
        zp->z_acl_cached = NULL;
    }
    if (zp->z_xattr_cached) {
        nvlist_free(zp->z_xattr_cached);
        zp->z_xattr_cached = NULL;
    }
    /* Read into private state first: failed identity validation must leave
     * the retained node doomed, not attached to a different received file. */
    znode_t *fresh = NULL;
    int error = load(fs, zp->z_id, &fresh);
    if (error) return error;
    if (fresh->z_gen != zp->z_gen ||
        (fresh->z_mode & S_IFMT) != (zp->z_mode & S_IFMT)) {
        dispose(fresh);
        return SET_ERROR(EIO);
    }
    zp->z_unlinked = fresh->z_unlinked;
    if (zp->z_unlinked) {
        dispose(fresh);
        return 0; /* upstream: unlinked received nodes remain detached */
    }
    zp->z_size = fresh->z_size;
    zp->z_links = fresh->z_links;
    zp->z_mode = fresh->z_mode;
    zp->z_pflags = fresh->z_pflags;
    zp->z_uid = fresh->z_uid;
    zp->z_gid = fresh->z_gid;
    zp->z_projid = fresh->z_projid;
    zp->z_xattr_parent = fresh->z_xattr_parent;
    zp->z_is_sa = fresh->z_is_sa;
    zp->z_blksz = fresh->z_blksz;
    zp->z_dnodesize = fresh->z_dnodesize;
    zp->z_blocks = fresh->z_blocks;
    zp->z_atime = fresh->z_atime;
    zp->z_mtime = fresh->z_mtime;
    zp->z_ctime = fresh->z_ctime;
    zp->z_btime = fresh->z_btime;
    zp->z_sa_hdl = fresh->z_sa_hdl;
    fresh->z_sa_hdl = NULL;
    dispose(fresh);
    return 0;
}

/* Native consumers read z_mode/z_size directly, with no duplicate Linux
 * inode. Publish the actual allocated 512-byte block count in native cached
 * metadata, including the dnode slots as upstream VFS accounting requires;
 * a sparse file's logical length is not its physical allocation. */
void
zfs_znode_update_vfs(znode_t *zp)
{
    ASSERT(zp != NULL);
    if (zp->z_is_ctldir) return;
    uint32_t blocksize;
    u_longlong_t blocks;
    dmu_object_size_from_db(sa_get_db(zp->z_sa_hdl), &blocksize, &blocks);
    boolean_t held = MUTEX_HELD(&zp->z_lock);
    if (!held) mutex_enter(&zp->z_lock);
    zp->z_blocks = blocks;
    if (!held) mutex_exit(&zp->z_lock);
}

int
zfs_zget(zfsvfs_t *fs, uint64_t object, znode_t **out)
{
    if (!out) return (EINVAL);
    *out = NULL;
    if (fs && fs->z_unmounted) return EIO;
    if (!fs || !fs->z_os || !fs->z_attr_table) return (EINVAL);
    mutex_enter(&fs->z_znodes_lock);
    int error = 0;
    if (fs->z_unmounted) error = EIO;
    else {
        znode_t *zp;
        for (zp = list_head(&fs->z_all_znodes); zp;
            zp = list_next(&fs->z_all_znodes, zp)) {
            if (zp->z_id == object && zp->z_sa_hdl) break;
        }
        if (zp) {
            VERIFY3U(zp->z_refs, <, UINT64_MAX);
            zp->z_refs++;
            *out = zp;
        } else {
            /* Never hold the reference/cache lock over DMU I/O: ZIL
             * completion must be able to release refs while TXGs wait. */
            mutex_exit(&fs->z_znodes_lock);
            znode_t *candidate = NULL;
            error = load(fs, object, &candidate);
            if (error) return (error);
            mutex_enter(&fs->z_znodes_lock);
            if (fs->z_unmounted) error = EIO;
            else {
                for (zp = list_head(&fs->z_all_znodes); zp;
                    zp = list_next(&fs->z_all_znodes, zp)) {
                    if (zp->z_id == object && zp->z_sa_hdl) break;
                }
                if (zp) {
                    VERIFY3U(zp->z_refs, <, UINT64_MAX);
                    zp->z_refs++;
                    *out = zp;
                } else {
                    list_insert_tail(&fs->z_all_znodes, candidate);
                    *out = candidate;
                    candidate = NULL;
                }
            }
            mutex_exit(&fs->z_znodes_lock);
            if (candidate) dispose(candidate);
            return (error);
        }
    }
    mutex_exit(&fs->z_znodes_lock);
    return (error);
}

void
oz_znode_hold(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    mutex_enter(&fs->z_znodes_lock);
    VERIFY3U(zp->z_refs, >, 0);
    VERIFY3U(zp->z_refs, <, UINT64_MAX);
    zp->z_refs++;
    mutex_exit(&fs->z_znodes_lock);
}

/* Upstream object/external-ACL deletion with native SA lifetime. Used for
 * unpublished creation rollback under namespace exclusion, and final unlink
 * by the exclusive drainer after reference admission and data/xattr handling.
 * Retain a handle-less cache tombstone until safe owner trim or teardown:
 * cache-sync traversal
 * may retain native pointers, and object numbers may subsequently be reused.
 * zget ignores these tombstones, so reused numbers get fresh native state. */
void
zfs_znode_delete(znode_t *zp, dmu_tx_t *tx)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    mutex_enter(&zp->z_lock);
    VERIFY(zp->z_sa_hdl != NULL);
    VERIFY(!zp->z_atime_dirty && !zp->z_dirlocks && !zp->z_mapcnt);
    uint64_t acl = zfs_external_acl(zp);
    if (acl) {
        VERIFY(!zp->z_is_sa);
        VERIFY0(dmu_object_free(fs->z_os, acl, tx));
    }
    VERIFY0(dmu_object_free(fs->z_os, zp->z_id, tx));
    mutex_enter(&fs->z_znodes_lock);
    sa_handle_t *handle = zp->z_sa_hdl;
    zp->z_sa_hdl = NULL;
    mutex_exit(&fs->z_znodes_lock);
    sa_handle_destroy(handle);
    mutex_exit(&zp->z_lock);
}

void
oz_znode_rele(znode_t *zp)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    mutex_enter(&fs->z_znodes_lock);
    VERIFY3U(zp->z_refs, >, 0);
    zp->z_refs--;
    mutex_exit(&fs->z_znodes_lock);
}

void
zfs_zrele_async(znode_t *zp)
{
    /* Unlike iput(), dropping the last native consumer reference cannot
     * evict, flush, unlink or wait for TXG I/O. The mount owns idle nodes.
     * The cache lock is never held across load I/O. Therefore ZIL callers
     * need no taskq just to decrement the reference. Reclamation happens
     * separately at explicit owner trim points, never in this path. */
    oz_znode_rele(zp);
}

int
oz_znode_cache_sync(zfsvfs_t *fs, boolean_t wait)
{
    /* Caller retains mount lifetime, excluding finalization. Pin list topology
     * across this walk, including the borrowed tail. Never hold the mutex over SA/TXG
     * work; keep a consumer reference while visiting each node instead. */
    if (!fs || !fs->z_os || fs->z_unmounted) return (EIO);
    int error = 0;
    mutex_enter(&fs->z_znodes_lock);
    fs->z_cache_walkers++;
    znode_t *zp = list_head(&fs->z_all_znodes);
    znode_t *last = list_tail(&fs->z_all_znodes);
    while (zp) {
        VERIFY3U(zp->z_refs, <, UINT64_MAX);
        zp->z_refs++;
        mutex_exit(&fs->z_znodes_lock);
        mutex_enter(&zp->z_lock);
        /* Unlink retains the node until outstanding consumers release it.
         * Its pending atime is no longer a namespace durability obligation;
         * the exclusive reaper discards it when deleting the object. */
        boolean_t dirty = zp->z_atime_dirty && !zp->z_unlinked;
        mutex_exit(&zp->z_lock);
        int e = dirty ? oz_znode_flush_atime(zp) : 0;
        if (e == ENOENT) {
            /* Unlink can win after the snapshot above or during TXG assign.
             * Only suppress this specific lifetime transition, not an SA
             * lookup failure on a still-linked node. */
            mutex_enter(&zp->z_lock);
            if (zp->z_unlinked) e = 0;
            mutex_exit(&zp->z_lock);
        }
        if (!error) error = e;
        mutex_enter(&fs->z_znodes_lock);
        VERIFY3U(zp->z_refs, >, 0);
        zp->z_refs--;
        /* Bound this pass to the nodes present when it began. */
        zp = zp == last ? NULL : list_next(&fs->z_all_znodes, zp);
    }
    VERIFY3U(fs->z_cache_walkers, >, 0);
    fs->z_cache_walkers--;
    mutex_exit(&fs->z_znodes_lock);
    /* Assigned transactions may have succeeded before another node failed.
     * A synchronous caller still waits for those writes, preserving the error.
     * Read-only imported pools have no syncing writer to wait on. */
    if (wait && spa_writeable(dmu_objset_spa(fs->z_os)))
        txg_wait_synced(dmu_objset_pool(fs->z_os), 0);
    return (error);
}

/* Caller excludes lifecycle transitions. Reclaim only idle, clean nodes;
 * never flush, delete disk objects, or wait for a TXG from reference release.
 * Active cache walkers retain list topology, not just their current node. */
uint64_t
oz_znode_cache_trim(zfsvfs_t *fs, uint64_t keep)
{
    list_t retired;
    list_create(&retired, sizeof (znode_t), offsetof(znode_t, z_link_node));
    uint64_t count = 0, freed = 0;
    mutex_enter(&fs->z_znodes_lock);
    if (!fs->z_cache_walkers) {
        znode_t *zp, *next;
        for (zp = list_head(&fs->z_all_znodes); zp;
            zp = list_next(&fs->z_all_znodes, zp)) count++;
        for (zp = list_head(&fs->z_all_znodes); zp && count > keep; zp = next) {
            next = list_next(&fs->z_all_znodes, zp);
            if (zp->z_refs || zp->z_atime_dirty || zp->z_dirlocks ||
                zp->z_mapcnt || zp->z_sync_cnt || zp->z_suspended ||
                (zp->z_unlinked && zp->z_sa_hdl)) continue;
            list_remove(&fs->z_all_znodes, zp);
            list_insert_tail(&retired, zp);
            count--; freed++;
        }
    }
    mutex_exit(&fs->z_znodes_lock);
    znode_t *zp;
    while ((zp = list_remove_head(&retired)) != NULL) dispose(zp);
    list_destroy(&retired);
    return freed;
}

static int
cache_fini(zfsvfs_t *fs, boolean_t unpublished)
{
    /* Two-pass refusal: do not partially destroy a busy/unsupported cache.
     * Dirty atime must be explicitly persisted and live unlinked objects
     * reclaimed. Read-only xattr nvlists own no external references. */
    mutex_enter(&fs->z_znodes_lock);
    if (fs->z_cache_walkers) {
        mutex_exit(&fs->z_znodes_lock);
        return EBUSY;
    }
    znode_t *zp;
    for (zp = list_head(&fs->z_all_znodes); zp;
        zp = list_next(&fs->z_all_znodes, zp)) {
        if (zp->z_refs || (!unpublished && (zp->z_atime_dirty ||
            (zp->z_unlinked && zp->z_sa_hdl))) ||
            zp->z_dirlocks ||
            zp->z_mapcnt || zp->z_sync_cnt) {
            mutex_exit(&fs->z_znodes_lock);
            return (EBUSY);
        }
    }
    while ((zp = list_remove_head(&fs->z_all_znodes)) != NULL) dispose(zp);
    mutex_exit(&fs->z_znodes_lock);
    list_destroy(&fs->z_all_znodes);
    mutex_destroy(&fs->z_znodes_lock);
    return (0);
}

int oz_znode_cache_fini(zfsvfs_t *fs)
{ return cache_fini(fs, B_FALSE); }

/* Only for failed mount admission, after replay transactions and ZIL users
 * have drained. Drop private cache state, not on-disk objects: unlinked-set
 * entries must survive for the next mount. No caller has observed atime
 * updates in this unpublished mount. Borrowed/async references still block. */
int oz_znode_cache_abort_unpublished(zfsvfs_t *fs)
{ return cache_fini(fs, B_TRUE); }
