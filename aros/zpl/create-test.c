/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zap.h>
#include <sys/dmu_objset.h>
#include <proto/exec.h>

/* Private mirror-test link only. All ordinary commits use the real ZIL. */
static zilog_t *fail_create_log;
static unsigned failed_create_commits;
extern int __real_zil_commit(zilog_t *, uint64_t);
int __wrap_zil_commit(zilog_t *log, uint64_t object)
{
    if (log == fail_create_log && object == 0) {
        fail_create_log = NULL;
        failed_create_commits++;
        return EIO;
    }
    return __real_zil_commit(log, object);
}

static int commit_failure_test(zfsvfs_t *fs, znode_t *parent, unsigned kind)
{
    static const char *names[] = {"create-sync-error", "mkdir-sync-error",
        "symlink-sync-error"};
    static const mode_t modes[] = {S_IFREG | 0640, S_IFDIR | 0750,
        S_IFLNK | 0777};
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = modes[kind]};
    uint64_t saved_sync = fs->z_os->os_sync;
    znode_t *node = NULL;
    unsigned before = failed_create_commits;
    fs->z_os->os_sync = ZFS_SYNC_ALWAYS;
    fail_create_log = fs->z_log;
    int result;
    if (kind == 0)
        result = zfs_create(parent, (char *)names[kind], &va, 1, S_IWUSR,
            &node, kcred, 0, NULL, NULL);
    else if (kind == 1)
        result = zfs_mkdir(parent, (char *)names[kind], &va, &node,
            kcred, 0, NULL, NULL);
    else
        result = zfs_symlink(parent, (char *)names[kind], &va,
            "created-file", &node, kcred, 0, NULL);
    fail_create_log = NULL;
    fs->z_os->os_sync = saved_sync;
    int error = result != EIO || node != NULL ||
        failed_create_commits != before + 1 ? EIO : 0;
    if (node) oz_znode_rele(node);
    /* The transaction can already exist despite the failed durability
     * acknowledgment. Do not assert rollback or retry exclusive creation. */
    uint64_t entry;
    int lookup = zap_lookup(fs->z_os, parent->z_id, names[kind],
        8, 1, &entry);
    if (!lookup) {
        node = NULL;
        lookup = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &node);
        if (!lookup) {
            if (node->z_refs != 1 && !error) error = EIO;
            oz_znode_rele(node);
        }
    }
    if (!error) error = lookup;
    char text[128];
    snprintf(text, sizeof (text),
        "Native namespace sync-failure kind=%u injected=%u result=%d\n",
        kind, failed_create_commits - before, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return error;
}

static int attempt(znode_t *parent, vattr_t *va, int exclusive, int mode,
    cred_t *cr, int expected, znode_t *original)
{
    znode_t *node = NULL;
    int result = zfs_create(parent, "created-file", va, exclusive, mode,
        &node, cr, 0, NULL, NULL);
    int error = result != expected || (expected ? node != NULL : node != original) ? EIO : 0;
    if (node) oz_znode_rele(node);
    return (error);
}

int oz_create_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry;
    znode_t *parent = NULL, *file = NULL;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &parent);
    if (!error) error = zap_lookup(fs->z_os, parent->z_id, "created-file", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &file);
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFREG | 0640};
    if (!error) error = attempt(parent, &va, 0, S_IRUSR, NULL, EACCES, file);
    if (!error) error = attempt(parent, &va, 0, S_IRUSR, kcred, 0, file);
    if (!error && write) {
        for (unsigned kind = 0; !error && kind < 3; kind++)
            error = commit_failure_test(fs, parent, kind);
        unsigned char bytes[64]; memset(bytes, 0xb6, sizeof (bytes));
        size_t residual = sizeof (bytes);
        if (!error) error = zfs_write_simple(file, bytes, sizeof (bytes), 0, &residual);
        if (!error && (residual || file->z_size != sizeof (bytes))) error = EIO;
        if (!error) error = zfs_fsync(file, 0, kcred);
        if (!error) error = attempt(parent, &va, 0, S_IRUSR, kcred, 0, file);
        va.va_mask |= ATTR_SIZE;
        if (!error) error = attempt(parent, &va, 1, S_IWUSR, kcred, EEXIST, file);
        cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
        if (!reader) error = ENOMEM;
        else {
            /* Even a mode=0 caller may not bypass truncate admission. */
            if (!error) error = attempt(parent, &va, 0, 0, reader, EACCES, file);
            oz_cred_release(reader);
        }
        uint64_t saved_flags = file->z_pflags;
        file->z_pflags |= ZFS_APPENDONLY;
        if (!error) error = attempt(parent, &va, 0, 0, kcred, EPERM, file);
        file->z_pflags = saved_flags | ZFS_IMMUTABLE;
        if (!error) error = attempt(parent, &va, 0, 0, kcred, EPERM, file);
        file->z_pflags = saved_flags;
        zilog_t *log = fs->z_log; fs->z_log = NULL;
        if (!error) error = attempt(parent, &va, 0, 0, kcred, ENOTSUP, file);
        fs->z_log = log;
        if (!error && file->z_size != sizeof (bytes)) error = EIO;
        unsigned char actual[sizeof (bytes)];
        struct iovec vec = {actual, sizeof (actual)}; zfs_uio_t uio;
        zfs_uio_iovec_init(&uio, &vec, 1, 0, UIO_SYSSPACE, sizeof (actual), 0);
        if (!error) error = zfs_read(file, &uio, 0, kcred);
        if (!error && (uio.uio_resid || memcmp(bytes, actual, sizeof (bytes)))) error = EIO;
        if (!error) error = attempt(parent, &va, 0, S_IWUSR, kcred, 0, file);
        if (!error) error = zfs_fsync(file, 0, kcred);
    } else if (!error) {
        va.va_mask |= ATTR_SIZE;
        error = attempt(parent, &va, 0, 0, kcred, EROFS, file);
        znode_t *missing = NULL;
        if (!error && (zfs_create(parent, "must-not-create", &va, 1, 0,
            &missing, kcred, 0, NULL, NULL) != EROFS || missing)) error = EIO;
        if (missing) oz_znode_rele(missing);
        if (!error && zap_contains(fs->z_os, parent->z_id, "must-not-create") != ENOENT) error = EIO;
    }
    uint64_t size = UINT64_MAX;
    if (!error) error = sa_lookup(file->z_sa_hdl, SA_ZPL_SIZE(fs), &size, 8);
    if (!error && (size || file->z_size)) error = EIO;
    if (file) oz_znode_rele(file);
    if (parent) oz_znode_rele(parent);
    char text[100]; snprintf(text, sizeof (text),
        "Native upstream create/open/truncate write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
