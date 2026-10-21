/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_fuid.h>
#include <sys/aros_idmap.h>

/* Executes the actual upstream kernel-side FUID functions and AVL cache.
 * This in-memory cache test does not claim an on-disk FUID sync/replay test. */
int
oz_fuid_test(void)
{
    char domain[] = "AROS.test";
    struct oz_idmap_entry entries[] = {
        {domain, 77, 1234, B_FALSE},
        {domain, 77, 99, B_TRUE},
        {"OTHER.test", 77, 4321, B_FALSE}
    };
    struct oz_idmap *map = NULL;
    if (oz_idmap_create(entries, 3, &map)) return (1);
    domain[0] = 'X'; /* Mapping owns its domain strings. */
    uint32_t id;
    int error = 0;
    if (oz_idmap_lookup(map, "AROS.test", 77, B_FALSE, &id) || id != 1234 ||
        oz_idmap_lookup(map, "AROS.test", 77, B_TRUE, &id) || id != 99 ||
        oz_idmap_lookup(map, "OTHER.test", 77, B_FALSE, &id) || id != 4321)
        error = 2;
    if (oz_idmap_lookup(map, "missing", 77, B_FALSE, &id) != ENOENT || id != OZ_ID_UNMAPPED ||
        oz_idmap_lookup(NULL, "AROS.test", 77, B_FALSE, &id) != ENOENT || id != OZ_ID_UNMAPPED)
        error = 3;
    zfsvfs_t fs = {0};
    fs.z_idmap = map;
    fs.z_use_fuids = B_TRUE;
    fs.z_fuid_loaded = B_TRUE;
    rw_init(&fs.z_fuid_lock, NULL, RW_DEFAULT, NULL);
    zfs_fuid_avl_tree_create(&fs.z_fuid_idx, &fs.z_fuid_domain);
    char encoded[32] = {0};
    if (zfs_id_to_fuidstr(&fs, "AROS.test", 77, encoded, sizeof (encoded), B_TRUE))
        error = 4;
    uint64_t fuid = strtoull(encoded, NULL, 16);
    if (FUID_INDEX(fuid) == 0 || FUID_RID(fuid) != 77 ||
        zfs_fuid_map_id(&fs, fuid, NULL, ZFS_OWNER) != 1234 ||
        zfs_fuid_map_id(&fs, fuid, NULL, ZFS_ACE_USER) != 1234 ||
        zfs_fuid_map_id(&fs, fuid, NULL, ZFS_GROUP) != 99 ||
        zfs_fuid_map_id(&fs, fuid, NULL, ZFS_ACE_GROUP) != 99 ||
        zfs_fuid_map_id(&fs, FUID_ENCODE(FUID_INDEX(fuid), 0), NULL, ZFS_OWNER) != OZ_ID_UNMAPPED ||
        zfs_fuid_map_id(&fs, FUID_ENCODE(999, 0), NULL, ZFS_OWNER) != OZ_ID_UNMAPPED ||
        zfs_fuid_map_id(NULL, fuid, NULL, ZFS_OWNER) != OZ_ID_UNMAPPED ||
        zfs_fuid_map_id(&fs, 42, NULL, ZFS_OWNER) != 42)
        error = 5;
    cred_t *cr = oz_cred_create(1234, 1234, 99, 0, NULL);
    if (!cr || !zfs_groupmember(&fs, fuid, cr) ||
        zfs_groupmember(&fs, FUID_ENCODE(FUID_INDEX(fuid), 0), cr))
        error = 6;
    crfree(cr);
    zfs_fuid_destroy(&fs);
    rw_destroy(&fs.z_fuid_lock);
    oz_idmap_destroy(map);
    entries[1] = entries[0];
    if (oz_idmap_create(entries, 2, &map) != EEXIST || map) error = 7;
    entries[0].local_id = OZ_ID_UNMAPPED;
    if (oz_idmap_create(entries, 1, &map) != EINVAL || map) error = 8;
    if (oz_cred_create(OZ_ID_UNMAPPED, 1, 1, 0, NULL) ||
        oz_cred_create(1, 1, OZ_ID_UNMAPPED, 0, NULL)) error = 9;
    gid_t invalid = OZ_ID_UNMAPPED;
    if (oz_cred_create(1, 1, 1, 1, &invalid)) error = 10;
    return (error);
}
