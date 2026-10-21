/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_vnops.h>
#include <sys/zap.h>
#include <proto/exec.h>

static int credential_test(znode_t *parent)
{
    gid_t group = 99;
    cred_t *reader = oz_cred_create(77, 77, 88, 1, &group);
    if (!reader) return (ENOMEM);
    vattr_t va = {.va_mask = ATTR_MODE | ATTR_UID | ATTR_GID,
        .va_mode = S_IFREG | 0640, .va_uid = 0, .va_gid = 0};
    zfs_acl_ids_t ids;
    int error = zfs_acl_ids_create(parent, 0, &va, NULL, NULL, &ids, NULL) == EACCES ? 0 : EIO;
    if (!error) error = zfs_acl_ids_create(parent, 0, &va, reader, NULL, &ids, NULL);
    if (!error) {
        if (ids.z_fuid != 77 || ids.z_fgid != 88) error = EIO;
        zfs_acl_ids_free(&ids);
    }
    va.va_gid = 99;
    if (!error) error = zfs_acl_ids_create(parent, 0, &va, reader, NULL, &ids, NULL);
    if (!error) {
        if (ids.z_fuid != 77 || ids.z_fgid != 99) error = EIO;
        zfs_acl_ids_free(&ids);
    }
    uint64_t saved_gid = parent->z_gid, saved_mode = parent->z_mode;
    parent->z_mode |= S_ISGID;
    parent->z_gid = 999;
    va.va_mask = ATTR_MODE;
    va.va_mode = S_IFDIR | 0750;
    if (!error) error = zfs_acl_ids_create(parent, 0, &va, reader, NULL, &ids, NULL);
    if (!error) {
        if (ids.z_fuid != 77 || ids.z_fgid != 999 || !(ids.z_mode & S_ISGID)) error = EIO;
        zfs_acl_ids_free(&ids);
    }
    parent->z_gid = saved_gid;
    parent->z_mode = saved_mode;
    oz_cred_release(reader);
    return (error);
}

/* Internal creation transaction; public create admission is not bypassed by
 * exposing this fixture as a handler operation. */
static int create_node(znode_t *parent, char *name, mode_t mode, boolean_t discard)
{
    zfsvfs_t *fs = ZTOZSB(parent);
    if (S_ISREG(mode) && !discard) {
        vattr_t va = {.va_mask = ATTR_MODE, .va_mode = mode};
        znode_t *created = NULL;
        int error = zfs_create(parent, name, &va, 1, S_IRUSR | S_IWUSR,
            &created, kcred, 0, NULL, NULL);
        if (created) oz_znode_rele(created);
        return (error);
    }
    if (S_ISDIR(mode) && !discard) {
        vattr_t va = {.va_mask = ATTR_MODE, .va_mode = mode};
        znode_t *created = NULL;
        int error = zfs_mkdir(parent, name, &va, &created, kcred, 0, NULL, NULL);
        if (created) oz_znode_rele(created);
        return (error);
    }
    zfs_dirlock_t *lock = NULL;
    znode_t *node = NULL;
    int error = zfs_dirent_lock(&lock, parent, name, &node, ZNEW, NULL, NULL);
    if (error) return (error);
    vattr_t va = {.va_mask = ATTR_MODE | ATTR_UID | ATTR_GID,
        .va_mode = mode, .va_uid = 0, .va_gid = 0};
    zfs_acl_ids_t ids;
    error = zfs_acl_ids_create(parent, 0, &va, kcred, NULL, &ids, NULL);
    if (!error) {
        dmu_tx_t *tx = dmu_tx_create(fs->z_os);
        dmu_tx_hold_sa_create(tx, ids.z_aclp->z_acl_bytes + ZFS_SA_BASE_ATTR_SIZE);
        dmu_tx_hold_zap(tx, parent->z_id, B_TRUE, name);
        if (S_ISDIR(mode)) dmu_tx_hold_zap(tx, DMU_NEW_OBJECT, B_FALSE, NULL);
        dmu_tx_hold_sa(tx, parent->z_sa_hdl, B_FALSE);
        if (fs->z_fuid_dirty) zfs_fuid_txhold(fs, tx);
        zfs_sa_upgrade_txholds(tx, parent);
        error = dmu_tx_assign(tx, DMU_TX_WAIT);
        if (error) dmu_tx_abort(tx);
        else {
            zfs_mknode(parent, &va, tx, kcred, 0, &node, &ids);
            if (discard) zfs_znode_delete(node, tx);
            else error = zfs_link_create(lock, node, tx, ZNEW);
            if (fs->z_fuid_dirty) zfs_fuid_sync(fs, tx);
            if (!error && !discard) zfs_log_create(fs->z_log, tx,
                S_ISDIR(mode) ? TX_MKDIR : TX_CREATE, parent, node, name,
                NULL, ids.z_fuidp, &va);
            dmu_tx_commit(tx);
            if (discard) {
                znode_t *stale = NULL;
                if (zfs_zget(fs, node->z_id, &stale) != ENOENT || stale) error = EIO;
                if (stale) oz_znode_rele(stale);
            }
        }
        zfs_acl_ids_free(&ids);
    }
    if (node) oz_znode_rele(node);
    zfs_dirent_unlock(lock);
    return (error);
}

static int mkdir_guards(znode_t *parent, boolean_t write)
{
    zfsvfs_t *fs = ZTOZSB(parent);
    znode_t *node = NULL;
    vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFDIR | 0750};
    int error = 0;
    if (zfs_mkdir(parent, "reject", &va, &node, NULL, 0, NULL, NULL) != EACCES || node) error = EIO;
    if (zfs_mkdir(parent, "invalid/name", &va, &node, kcred, 0, NULL, NULL) != EINVAL || node) error = EIO;
    if (write) {
        cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
        if (!reader) return (ENOMEM);
        /* The initial fixture deliberately grants everyone ACE_ALL_PERMS.
         * Install a real restrictive ACL before testing denied creation. */
        vsecattr_t saved = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS};
        int acl_error = zfs_getacl(parent, &saved, B_FALSE, kcred);
        if (!acl_error) {
            ace_t ace = {0, ACE_READ_ATTRIBUTES | ACE_READ_ACL |
                ACE_EXECUTE | ACE_LIST_DIRECTORY, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE};
            vsecattr_t restricted = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS,
                .vsa_aclcnt = 1, .vsa_aclentp = &ace, .vsa_aclentsz = sizeof (ace),
                .vsa_aclflags = ACL_PROTECTED};
            acl_error = zfs_setacl(parent, &restricted, B_FALSE, kcred);
            if (!acl_error && (zfs_mkdir(parent, "reject", &va, &node, reader,
                0, NULL, NULL) != EACCES || node)) error = EIO;
            int restore = zfs_setacl(parent, &saved, B_FALSE, kcred);
            if (!acl_error) acl_error = restore;
            if (saved.vsa_aclentp) kmem_free(saved.vsa_aclentp, saved.vsa_aclentsz);
        }
        if (node) { oz_znode_rele(node); node = NULL; }
        if (!error) error = acl_error;
        oz_cred_release(reader);
        zilog_t *log = fs->z_log;
        fs->z_log = NULL;
        if (zfs_mkdir(parent, "reject", &va, &node, kcred, 0, NULL, NULL) != ENOTSUP || node) error = EIO;
        fs->z_log = log;
    } else if (zfs_mkdir(parent, "reject", &va, &node, kcred, 0, NULL, NULL) != EROFS || node) error = EIO;
    if (zap_contains(fs->z_os, parent->z_id, "reject") != ENOENT) error = EIO;
    return (error);
}

int oz_mknode_test(zfsvfs_t *fs, boolean_t write)
{
    uint64_t entry;
    znode_t *parent = NULL;
    int error = zap_lookup(fs->z_os, fs->z_root, "subdir", 8, 1, &entry);
    if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &parent);
    if (!error) error = credential_test(parent);
    if (!error) error = mkdir_guards(parent, write);
    if (!error && write) error = create_node(parent, "discarded", S_IFREG | 0644, B_TRUE);
    if (!error && write) error = create_node(parent, "discarded-directory", S_IFDIR | 0755, B_TRUE);
    if (!error && zap_contains(fs->z_os, parent->z_id, "discarded") != ENOENT) error = EIO;
    if (!error && zap_contains(fs->z_os, parent->z_id, "discarded-directory") != ENOENT) error = EIO;
    char *names[] = {"created-file", "created-directory"};
    mode_t modes[] = {S_IFREG | 0640, S_IFDIR | 0750};
    for (unsigned i = 0; i < 2 && !error; i++) {
        if (write) error = create_node(parent, names[i], modes[i], B_FALSE);
        znode_t *node = NULL;
        if (!error) error = zap_lookup(fs->z_os, parent->z_id, names[i], 8, 1, &entry);
        if (!error) error = zfs_zget(fs, ZFS_DIRENT_OBJ(entry), &node);
        /* The subsequent symlink fixture adds two names to this directory;
         * the cold boot must observe those persisted children. */
        if (!error && (node->z_mode != modes[i] || node->z_size != (i ? (write ? 2 : 4) : 0) ||
            node->z_links != (i ? 2 : 1) || node->z_uid || node->z_gid ||
            !node->z_gen || !node->z_is_sa || node->z_unlinked)) error = EIO;
        uint64_t on_disk_parent = 0;
        if (!error) error = sa_lookup(node->z_sa_hdl, SA_ZPL_PARENT(fs), &on_disk_parent, 8);
        if (!error && on_disk_parent != parent->z_id) error = EIO;
        if (!error) error = zfs_zaccess(node, ACE_READ_DATA, 0, B_FALSE, kcred, NULL);
        if (node) oz_znode_rele(node);
    }
    if (!error && write) {
        znode_t *duplicate = NULL;
        vattr_t va = {.va_mask = ATTR_MODE, .va_mode = S_IFDIR | 0750};
        if (zfs_mkdir(parent, names[1], &va, &duplicate, kcred, 0, NULL, NULL) != EEXIST || duplicate) error = EIO;
        if (duplicate) oz_znode_rele(duplicate);
    }
    if (!error && write) error = zil_commit(fs->z_log, 0);
    if (parent) oz_znode_rele(parent);
    char text[100];
    snprintf(text, sizeof (text), "Native upstream mknode write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    snprintf(text, sizeof (text), "Native upstream mkdir/rollback write=%d result=%d\n", write, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return (error);
}
