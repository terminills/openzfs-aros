/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_dir.h>
#include <sys/zap.h>
#include <proto/exec.h>


int oz_link_test(zfsvfs_t *fs, znode_t *file, boolean_t write)
{
    znode_t *parent = NULL;
    int error = zfs_zget(fs, fs->z_root, &parent);
    if (error) return (error);
    char name[] = "hello-alias.txt";
    if (write) {
        if (zfs_link(parent, file, name, NULL, 0) != EACCES ||
            zfs_link(parent, parent, name, kcred, 0) != EPERM) error = EIO;
        cred_t *reader = oz_cred_create(77, 77, 88, 0, NULL);
        if (!reader) error = ENOMEM;
        else {
            if (zfs_link(parent, file, name, reader, 0) != EPERM) error = EIO;
            oz_cred_release(reader);
        }
        uint64_t saved_flags = file->z_pflags, saved_links = file->z_links;
        file->z_pflags |= ZFS_IMMUTABLE;
        if (zfs_link(parent, file, name, kcred, 0) != EPERM) error = EIO;
        file->z_pflags = saved_flags | ZFS_APPENDONLY;
        if (zfs_link(parent, file, name, kcred, 0) != EPERM) error = EIO;
        file->z_pflags = saved_flags;
        file->z_links = (1ULL << 31) - 1;
        if (zfs_link(parent, file, name, kcred, 0) != EMLINK) error = EIO;
        file->z_links = saved_links;
        if (zap_contains(fs->z_os, parent->z_id, name) != ENOENT) error = EIO;
        if (!error) error = zfs_link(parent, file, name, kcred, 0);
        if (!error && (file->z_links != 2 || parent->z_size != 5)) error = EIO;
        if (!error && zfs_link(parent, file, name, kcred, 0) != EEXIST) error = EIO;
        if (!error) error = zil_commit(fs->z_log, 0);
        if (!error) error = zfs_remove(parent, name, kcred, 0);
        if (!error && (file->z_links != 1 || parent->z_size != 4 || file->z_unlinked)) error = EIO;
        if (!error && zap_contains(fs->z_os, parent->z_id, name) != ENOENT) error = EIO;
        if (!error) error = zil_commit(fs->z_log, 0);
        if (!error) error = zfs_link(parent, file, name, kcred, 0);
        if (!error) error = zil_commit(fs->z_log, 0);
    } else if (zfs_link(parent, file, "must-not-link", kcred, 0) != EROFS) error = EIO;
    uint64_t entry = 0, links = 0, size = 0, entries = 0;
    if (!error) error = zap_lookup(fs->z_os, parent->z_id, name, 8, 1, &entry);
    if (!error) error = sa_lookup(file->z_sa_hdl, SA_ZPL_LINKS(fs), &links, 8);
    if (!error) error = sa_lookup(parent->z_sa_hdl, SA_ZPL_SIZE(fs), &size, 8);
    if (!error) error = zap_count(fs->z_os, parent->z_id, &entries);
    /* Later fixtures add siblings before cold import. Persisted directory
     * size must match the actual ZAP plus dot entries, not the early count. */
    if (!error && (ZFS_DIRENT_OBJ(entry) != file->z_id || links != 2 || size != entries + 2 ||
        file->z_links != 2 || parent->z_size != size)) error = EIO;
    oz_znode_rele(parent);
    char text[100];snprintf(text,sizeof (text),"Native hardlink/remove/relink write=%d result=%d\n",write,error);
    for (char *p=text; *p; p++) RawPutChar(*p);
    return (error);
}
