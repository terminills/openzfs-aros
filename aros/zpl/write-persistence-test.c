/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_prop.h>
#include <sys/zap.h>
#include <proto/exec.h>
#include "../engine/zpl-fixture.h"
#include "read-mount.h"
int oz_dataset_read_counter(dataset_kstats_t *, const char *, uint64_t *);
int oz_dirlock_test(zfsvfs_t *);
int oz_link_test(zfsvfs_t *, znode_t *, boolean_t);
int oz_mknode_test(zfsvfs_t *, boolean_t);
int oz_symlink_test(zfsvfs_t *, boolean_t);
int oz_create_test(zfsvfs_t *, boolean_t);
int oz_xattr_dir_test(zfsvfs_t *, boolean_t);
int oz_rmdir_test(zfsvfs_t *, boolean_t);
int oz_remove_test(zfsvfs_t *, boolean_t);
int oz_rename_test(zfsvfs_t *, boolean_t);
int oz_setattr_test(zfsvfs_t *, boolean_t);
int oz_native_protection_test(zfsvfs_t *, boolean_t);

static int metadata_fixture(objset_t *os)
{
    dmu_tx_t *tx = dmu_tx_create(os);
    dmu_tx_hold_zap(tx, MASTER_NODE_OBJ, B_TRUE, NULL);
    dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_TRUE, NULL);
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); return (error); }
    uint64_t limit = 1073741824;
    error = zap_add(os, MASTER_NODE_OBJ,
        zfs_prop_to_name(ZFS_PROP_DEFAULTUSERQUOTA), 8, 1, &limit, tx);
    if (!error) {
        uint64_t quota = zap_create(os, DMU_OT_USERGROUP_QUOTA, DMU_OT_NONE, 0, tx);
        error = zap_add(os, MASTER_NODE_OBJ,
            zfs_userquota_prop_prefixes[ZFS_PROP_USERQUOTA], 8, 1, &quota, tx);
    }
    dmu_tx_commit(tx);
    return (error);
}

static int restrict_acl(znode_t *zp, uint32_t mask)
{
    ace_t ace = {0, mask, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE};
    vsecattr_t v = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS,
        .vsa_aclcnt = 1, .vsa_aclentp = &ace, .vsa_aclentsz = sizeof (ace),
        .vsa_aclflags = ACL_PROTECTED};
    int error = zfs_setacl(zp, &v, B_FALSE, kcred);
    return (error ? error : zil_commit(ZTOZSB(zp)->z_log, zp->z_id));
}

static int policy_test(zfsvfs_t *fs, const char *name, boolean_t write)
{
    struct oz_mount_policy *policy = NULL;
    int error = oz_mount_policy_open(fs, !write, &policy);
    if (error) return (error);
    if (fs->z_max_blksz != 65536 || fs->z_readonly != !write) error = EIO;
    uint_t mode = fs->z_acl_mode, inherit = fs->z_acl_inherit;
    if (!error && write) {
        error = dsl_prop_set_int(name, "recordsize", ZPROP_SRC_LOCAL, 32768);
        if (!error && fs->z_max_blksz != 32768) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "readonly", ZPROP_SRC_LOCAL, 1);
        if (!error && !zfs_is_readonly(fs)) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "readonly", ZPROP_SRC_LOCAL, 0);
        if (!error && zfs_is_readonly(fs)) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "aclmode", ZPROP_SRC_LOCAL, ZFS_ACL_RESTRICTED);
        if (!error && fs->z_acl_mode != ZFS_ACL_RESTRICTED) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "aclinherit", ZPROP_SRC_LOCAL, ZFS_ACL_PASSTHROUGH);
        if (!error && fs->z_acl_inherit != ZFS_ACL_PASSTHROUGH) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "aclmode", ZPROP_SRC_LOCAL, mode);
        if (!error) error = dsl_prop_set_int(name, "aclinherit", ZPROP_SRC_LOCAL, inherit);
    }
    oz_mount_policy_close(policy);
    if (!error && write) {
        error = dsl_prop_set_int(name, "recordsize", ZPROP_SRC_LOCAL, 65536);
        /* After unregister, changing the dataset must no longer mutate fs. */
        if (!error && fs->z_max_blksz != 32768) error = EIO;
        policy = NULL;
        if (!error) error = oz_mount_policy_open(fs, B_TRUE, &policy);
        if (!error && (!fs->z_readonly || fs->z_max_blksz != 65536)) error = EIO;
        if (!error) error = dsl_prop_set_int(name, "readonly", ZPROP_SRC_LOCAL, 0);
        if (!error && !fs->z_readonly) error = EIO;
        oz_mount_policy_close(policy);
    }
    fs->z_readonly = !write;
    return (error);
}

static int write_rejections(znode_t *zp, const char *name)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    unsigned char byte = 0x5a;
    struct iovec vec = {&byte, 1}; zfs_uio_t u;
    uint64_t flags = zp->z_pflags, size = zp->z_size;
    zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, 1, 0);
    struct oz_mount_policy *policy = NULL;
    uint64_t saved_recordsize = fs->z_max_blksz;
    int error = oz_mount_policy_open(fs, B_FALSE, &policy);
    if (!error) error = dsl_prop_set_int(name, "readonly", ZPROP_SRC_LOCAL, 1);
    if (!error && (zfs_write(zp, &u, 0, kcred) != EROFS || u.uio_resid != 1)) error = EIO;
    int restore = dsl_prop_set_int(name, "readonly", ZPROP_SRC_LOCAL, 0);
    if (!error) error = restore;
    if (!error && fs->z_readonly) error = EIO;
    oz_mount_policy_close(policy);
    fs->z_max_blksz = saved_recordsize;
    if (error) return (error);
    zp->z_pflags = flags | ZFS_IMMUTABLE;
    if (zfs_write(zp, &u, 0, kcred) != EPERM || u.uio_resid != 1) error = EIO;
    zp->z_pflags = flags | ZFS_APPENDONLY;
    if (zfs_write(zp, &u, 0, kcred) != EPERM || u.uio_resid != 1) error = EIO;
    zp->z_pflags = flags;
    zfs_uio_iovec_init(&u, &vec, 1, -1, UIO_SYSSPACE, 1, 0);
    if (zfs_write(zp, &u, 0, kcred) != EINVAL || u.uio_resid != 1 ||
        zp->z_size != size) error = EIO;
    return (error);
}

static int space_test(znode_t *zp, boolean_t write, unsigned char *expected,
    unsigned char *actual, size_t total)
{
    enum { HOLE = 4091, LENGTH = 6003, SHRINK = 14001 };
    flock64_t range = {.l_type = F_WRLCK, .l_whence = SEEK_SET,
        .l_start = HOLE, .l_len = LENGTH};
    int error = 0;
    if (write) {
        zfsvfs_t *fs = ZTOZSB(zp);
        uint64_t size = zp->z_size;
        if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, NULL) != EACCES ||
            zfs_space(zp, -1, &range, O_RDWR, 0, kcred) != EINVAL) error = EIO;
        range.l_start = -1;
        if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred) != EINVAL) error = EIO;
        range.l_start = INT64_MAX;
        if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred) != EOVERFLOW) error = EIO;
        range.l_start = HOLE;
        range.l_len = -1;
        if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred) != EINVAL) error = EIO;
        range.l_len = LENGTH; range.l_whence = SEEK_CUR;
        if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred) != EINVAL) error = EIO;
        range.l_whence = SEEK_SET;
        znode_t *directory = NULL;
        if (zfs_zget(fs, fs->z_root, &directory)) error = EIO;
        if (directory) {
            if (zfs_space(directory, F_FREESP, &range, O_RDWR, 0, kcred) != EISDIR) error = EIO;
            oz_znode_rele(directory);
        }
        zilog_t *log = fs->z_log; fs->z_log = NULL;
        int no_log = zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred);
        fs->z_log = log;
        if (no_log != ENOTSUP || zp->z_size != size) error = EIO;
        if (!error) error = zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred);
        if (!error && zp->z_size != size) error = EIO;
        if (!error) error = zfs_fsync(zp, 0, kcred);
    } else if (zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred) != EROFS)
        error = EIO;
    memset(expected + HOLE, 0, LENGTH);
    if (!error && write) {
        struct iovec vec = {actual, total}; zfs_uio_t u;
        zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, total, 0);
        error = zfs_read(zp, &u, 0, kcred);
        if (!error && (u.uio_resid || memcmp(actual, expected, total))) error = EIO;
        range.l_start = SHRINK; range.l_len = 0;
        if (!error) error = zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred);
        if (!error && zp->z_size != SHRINK) error = EIO;
        if (!error) error = zfs_fsync(zp, 0, kcred);
        range.l_start = total;
        if (!error) error = zfs_space(zp, F_FREESP, &range, O_RDWR, 0, kcred);
        if (!error && zp->z_size != total) error = EIO;
        if (!error) error = zfs_fsync(zp, 0, kcred);
    }
    memset(expected + SHRINK, 0, total - SHRINK);
    char message[100];snprintf(message, sizeof (message),
        "Native ZPL hole/truncate/extend write=%d result=%d\n", write, error);
    for (char *p = message; *p; p++) RawPutChar(*p);
    return (error);
}

/* Isolated native vnode-write acceptance, NOT a public writable DOS mount.
 * Own an independent fixture dataset so directory/readonly tests are intact.
 * Use the upstream ZIL get-data callback, never a successful journal stub. */
int oz_write_persistence_test(boolean_t write)
{
    const char *name = "qemu_mirror/writefiles";
    enum { BASE = 16384, START = 3077, LENGTH = 6001, APPEND = 31,
        TOTAL = BASE + APPEND };
    int error = write ? oz_zpl_fixture_create(name) : 0;
    if (!error && write) error = dsl_prop_set_int(name, "recordsize", ZPROP_SRC_LOCAL, 65536);
    if (error) return (error);
    zfsvfs_t fs = {.z_readonly = !write};
    error = dmu_objset_own(name, DMU_OST_ZFS, !write, B_FALSE, &fs, &fs.z_os);
    if (error) return (error);
    if (write) error = metadata_fixture(fs.z_os);
    if (!error) error = oz_zpl_mount_metadata(&fs, fs.z_os);
    if (!error && (fs.z_version != ZPL_VERSION || !fs.z_use_fuids ||
        !fs.z_use_sa || !fs.z_root || !fs.z_unlinkedobj ||
        fs.z_max_blksz != 65536 || fs.z_defaultuserquota != 1073741824 ||
        !fs.z_userquota_obj || fs.z_groupquota_obj || fs.z_defaultgroupquota))
        error = EIO;
    if (error) { dmu_objset_disown(fs.z_os, B_FALSE, &fs); return (error); }
    error = policy_test(&fs, name, write);
    char policy_result[100];
    snprintf(policy_result, sizeof (policy_result),
        "Native mount policy callbacks write=%d result=%d\n", write, error);
    for (char *p = policy_result; *p; p++) RawPutChar(*p);
    if (error) { dmu_objset_disown(fs.z_os, B_FALSE, &fs); return (error); }
    /* Keep this write boundary test at its deliberate small record size. */
    fs.z_max_blksz = BASE;
    uint64_t sa, entry;
    error = zap_lookup(fs.z_os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa);
    if (!error) error = sa_setup(fs.z_os, sa, zfs_attr_table, ZPL_END, &fs.z_attr_table);
    if (!error) error = zap_lookup(fs.z_os, MASTER_NODE_OBJ, ZFS_ROOT_OBJ, 8, 1, &fs.z_root);
    if (!error) error = zap_lookup(fs.z_os, fs.z_root, "hello.txt", 8, 1, &entry);
    if (error) { dmu_objset_disown(fs.z_os, B_FALSE, &fs); return (error); }
    ZFS_TEARDOWN_INIT(&fs);
    oz_mount_refs_init(&fs);
    oz_znode_cache_init(&fs);
    error = dataset_kstats_create(&fs.z_kstat, fs.z_os);
    znode_t *zp = NULL;
    if (!error) error = zfs_zget(&fs, ZFS_DIRENT_OBJ(entry), &zp);
    if (!error) error = oz_dirlock_test(&fs);
    unsigned char *expected = malloc(TOTAL), *actual = malloc(TOTAL);
    if (!expected || !actual) error = ENOMEM;
    if (!error) for (unsigned i = 0; i < TOTAL; i++)
        expected[i] = (i * 37u) ^ (i >> 5) ^ 0xa7;
    if (!error && write) fs.z_log = zil_open(fs.z_os, zfs_get_data, NULL);
    if (!error) error = oz_link_test(&fs, zp, write);
    if (!error) error = oz_mknode_test(&fs, write);
    if (!error) error = oz_symlink_test(&fs, write);
    if (!error && write) error = write_rejections(zp, name);
    if (!error && write) {
        struct iovec vec = {expected, BASE}; zfs_uio_t u;
        size_t residual = SIZE_MAX;
        if (zfs_write_simple(NULL, expected, 1, 0, &residual) != EINVAL ||
            zfs_write_simple(zp, NULL, 1, 0, &residual) != EINVAL ||
            zfs_write_simple(zp, expected, 1, -1, &residual) != EINVAL ||
            zfs_write_simple(zp, expected, 2, INT64_MAX, &residual) != EOVERFLOW ||
            residual != SIZE_MAX) error = EIO;
        if (!error) error = zfs_write_simple(zp, expected, BASE, 0, &residual);
        if (!error && (residual || zp->z_size != BASE)) error = EIO;
        if (!error) error = zfs_fsync(zp, 0, kcred);
        for (unsigned i = START; i < START + LENGTH; i++) expected[i] ^= 0x5a;
        vec = (struct iovec){expected + START, LENGTH};
        if (!error) error = zfs_write_simple(zp, expected + START, LENGTH, START, NULL);
        if (!error) error = zfs_fsync(zp, 0, kcred);
        vec = (struct iovec){expected + BASE, APPEND};
        zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, APPEND, 0);
        if (!error) error = zfs_write(zp, &u, O_APPEND | O_SYNC, kcred);
        if (!error && (u.uio_resid || u.uio_loffset != TOTAL)) error = EIO;
        uint64_t count;
        if (!error && (oz_dataset_read_counter(&fs.z_kstat, "writes", &count) || count != 3 ||
            oz_dataset_read_counter(&fs.z_kstat, "nwritten", &count) ||
            count != BASE + LENGTH + APPEND)) error = EIO;
    } else if (!error) {
        for (unsigned i = START; i < START + LENGTH; i++) expected[i] ^= 0x5a;
        struct iovec vec = {expected, 1}; zfs_uio_t u;
        zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, 1, 0);
        if (zfs_write(zp, &u, 0, kcred) != EROFS || u.uio_resid != 1) error = EIO;
    }
    if (!error) error = space_test(zp, write, expected, actual, TOTAL);
    if (!error) {
        struct iovec vec = {actual, TOTAL}; zfs_uio_t u;
        zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, TOTAL, 0);
        error = zfs_read(zp, &u, 0, kcred);
        if (!error && (zp->z_size != TOTAL || u.uio_resid ||
            memcmp(actual, expected, TOTAL))) error = EIO;
        uint64_t persisted_size;
        if (!error) error = sa_lookup(zp->z_sa_hdl, SA_ZPL_SIZE((&fs)),
            &persisted_size, sizeof (persisted_size));
        if (!error && persisted_size != TOTAL) error = EIO;
    }
    if (!error) error = oz_create_test(&fs, write);
    if (!error) error = oz_xattr_dir_test(&fs, write);
    if (!error) error = oz_rmdir_test(&fs, write);
    if (!error) error = oz_remove_test(&fs, write);
    if (!error) error = oz_rename_test(&fs, write);
    if (!error) error = oz_setattr_test(&fs, write);
    if (!error) error = oz_native_protection_test(&fs, write);
    if (!error && write) {
        /* Real persisted ACLs: metadata-only file and non-searchable directory.
         * The child remains readable; denial must occur during traversal. */
        error = restrict_acl(zp, ACE_READ_ATTRIBUTES | ACE_READ_ACL);
        if (!error) {
            cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
            if (!reader) error = ENOMEM;
            else {
                flock64_t denied = {.l_whence = SEEK_SET, .l_start = 1};
                if (zfs_space(zp, F_FREESP, &denied, O_RDWR, 0, reader) != EACCES ||
                    zp->z_size != TOTAL) error = EIO;
                oz_cred_release(reader);
            }
        }
        znode_t *dir = NULL;
        if (!error) error = zap_lookup(fs.z_os, fs.z_root, "subdir", 8, 1, &entry);
        if (!error) error = zfs_zget(&fs, ZFS_DIRENT_OBJ(entry), &dir);
        if (!error) error = restrict_acl(dir,
            ACE_READ_ATTRIBUTES | ACE_READ_ACL | ACE_LIST_DIRECTORY);
        if (dir) oz_znode_rele(dir);
    }
    if (fs.z_log) { zil_close(fs.z_log); fs.z_log = NULL; }
    if (zp) oz_znode_rele(zp);
    /* The fixture uses native znodes directly rather than mount_close().
     * Its read/readlink checks leave pending atime on linked nodes, which
     * must be persisted before the strict cache teardown boundary. */
    if (!error) error = oz_znode_cache_sync(&fs, B_TRUE);
    int cleanup = oz_znode_cache_fini(&fs);
    if (cleanup == EBUSY) {
        mutex_enter(&fs.z_znodes_lock);
        for (znode_t *node=list_head(&fs.z_all_znodes);node;
            node=list_next(&fs.z_all_znodes,node)) {
            if (!node->z_refs && !node->z_atime_dirty &&
                !(node->z_unlinked && node->z_sa_hdl) && !node->z_dirlocks &&
                !node->z_mapcnt && !node->z_sync_cnt) continue;
            char line[256];
            snprintf(line,sizeof(line),"Write fixture busy object=%llu refs=%llu atime=%u unlinked=%u sa=%u dirlocks=%u map=%llu sync=%u\n",
                (unsigned long long)node->z_id,(unsigned long long)node->z_refs,
                node->z_atime_dirty,node->z_unlinked,node->z_sa_hdl!=NULL,
                node->z_dirlocks!=NULL,(unsigned long long)node->z_mapcnt,node->z_sync_cnt);
            for(const char *p=line;*p;p++)RawPutChar(*p);
        }
        mutex_exit(&fs.z_znodes_lock);
    }
    dataset_kstats_destroy(&fs.z_kstat);
    ZFS_TEARDOWN_DESTROY(&fs);
    oz_mount_refs_fini(&fs);
    free(actual); free(expected);
    dmu_objset_disown(fs.z_os, B_FALSE, &fs);
    return (error ? error : cleanup);
}
