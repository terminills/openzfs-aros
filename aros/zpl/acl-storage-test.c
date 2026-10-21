/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_acl.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>
#include <proto/exec.h>

static void acl_trace(const char *stage, int result)
{
    char text[160];
    snprintf(text, sizeof (text), "ACL persistence %s result=%d setacl=%p commit=%p\n",
        stage, result, (void *)zfs_setacl, (void *)zil_commit);
    for (char *p = text; *p; p++) RawPutChar(*p);
}

/* Test the actual upstream mutation entry point and explicit ZIL commit.
 * The mount owns its real log; no successful journal stand-in is supplied. */
static int
persist_acl(znode_t *zp, uint32_t mask)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    ace_t ace = {0, mask, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE};
    vsecattr_t v = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS,
        .vsa_aclcnt = 1, .vsa_aclentp = &ace, .vsa_aclentsz = sizeof (ace),
        .vsa_aclflags = ACL_PROTECTED};
    if (zfs_setacl(zp, &v, B_FALSE, NULL) != EACCES ||
        zfs_setacl(zp, &v, B_TRUE, kcred) != ENOTSUP) return (EIO);
    uint64_t flags = zp->z_pflags;
    zp->z_pflags |= ZFS_IMMUTABLE;
    int denied = zfs_setacl(zp, &v, B_FALSE, kcred);
    zp->z_pflags = flags;
    if (denied != EPERM) return (EIO);
    v.vsa_aclentsz = 0;
    denied = zfs_setacl(zp, &v, B_FALSE, kcred);
    v.vsa_aclentsz = sizeof (ace);
    if (denied != EINVAL) return (EIO);
    acl_trace("before setacl", 0);
    int error = zfs_setacl(zp, &v, B_FALSE, kcred);
    acl_trace("after setacl", error);
    if (!error) error = zil_commit(fs->z_log, zp->z_id);
    acl_trace("after commit", error);
    return (error);
}

int
oz_acl_persistence_test(zfsvfs_t *fs, uint64_t object, boolean_t write,
    uint32_t final_mask)
{
    oz_znode_cache_init(fs);
    znode_t *zp = NULL;
    int error = zfs_zget(fs, object, &zp);
    uint64_t version = fs->z_version;
    boolean_t use_fuids = fs->z_use_fuids;
    fs->z_version = ZPL_VERSION;
    fs->z_use_fuids = B_TRUE;
    /* ACL records contain their data inline and require no write-data
     * callback. This fixture log is never used for file-write records. */
    if (!error && write) fs->z_log = zil_open(fs->z_os, NULL, NULL);
    if (!error && !write) {
        fs->z_log = dmu_objset_zil(fs->z_os);
        ace_t ace = {0, ACE_ALL_PERMS, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE};
        vsecattr_t v = {.vsa_mask = VSA_ACE | VSA_ACECNT, .vsa_aclcnt = 1,
            .vsa_aclentp = &ace, .vsa_aclentsz = sizeof (ace)};
        if (zfs_setacl(zp, &v, B_FALSE, kcred) != EROFS) error = EIO;
    }
    if (!error && write) {
        error = persist_acl(zp, ACE_READ_DATA | ACE_READ_ACL | ACE_READ_ATTRIBUTES);
        if (!error) {
            cred_t *reader = oz_cred_create(88, 88, 88, 0, NULL);
            if (!reader) error = ENOMEM;
            else {
                if (zfs_zaccess(zp, ACE_READ_DATA, 0, B_FALSE, reader, NULL) ||
                    zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, reader, NULL) != EACCES)
                    error = EIO;
                crfree(reader);
            }
        }
        /* Leave the requested final rights and protected flag for cold reads. */
        if (!error) error = persist_acl(zp, final_mask);
    }
    if (!error) {
        vsecattr_t v = {.vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS};
        error = zfs_getacl(zp, &v, B_FALSE, kcred);
        if (!error) {
            if (v.vsa_aclcnt != 1 || v.vsa_aclentsz != sizeof (ace_t) ||
                v.vsa_aclflags != ACL_PROTECTED ||
                ((ace_t *)v.vsa_aclentp)->a_access_mask != final_mask ||
                (zp->z_mode & 0777) != (final_mask == ACE_ALL_PERMS ? 0777 : 0444))
                error = EIO;
            kmem_free(v.vsa_aclentp, v.vsa_aclentsz);
        }
    }
    if (fs->z_log && write) zil_close(fs->z_log);
    fs->z_log = NULL;
    if (zp) oz_znode_rele(zp);
    int close_error = oz_znode_cache_fini(fs);
    fs->z_version = version;
    fs->z_use_fuids = use_fuids;
    return (error ? error : close_error);
}

int
oz_acl_sa_test(znode_t *zp, boolean_t write)
{
    zfsvfs_t *fs = ZTOZSB(zp);
    zfs_ace_hdr_t expected = {ACE_ACCESS_ALLOWED_ACE_TYPE, ACE_EVERYONE,
        ACE_ALL_PERMS}, actual;
    uint64_t count = 0;
    int size = 0;
    int error = sa_size(zp->z_sa_hdl, SA_ZPL_DACL_ACES(fs), &size);
    if (!error && size != sizeof (expected)) error = EIO;
    if (!error) error = sa_lookup(zp->z_sa_hdl, SA_ZPL_DACL_COUNT(fs),
        &count, sizeof (count));
    if (!error && count != 1) error = EIO;
    if (!error) error = sa_lookup(zp->z_sa_hdl, SA_ZPL_DACL_ACES(fs),
        &actual, sizeof (actual));
    if (!error && memcmp(&actual, &expected, sizeof (actual))) error = EIO;
    if (error || !write) return (error);
    /* Rewrite the identical, valid fixture ACL through two storage chunks.
     * Splitting inside the ACE tests serialization, not ACE interpretation. */
    zfs_acl_t acl = {0};
    zfs_acl_node_t first = { .z_acldata = &expected, .z_size = 3 };
    zfs_acl_node_t second = { .z_acldata = (char *)&expected + 3,
        .z_size = sizeof (expected) - 3 };
    list_create(&acl.z_acl, sizeof (zfs_acl_node_t),
        offsetof(zfs_acl_node_t, z_next));
    list_insert_tail(&acl.z_acl, &first);
    list_insert_tail(&acl.z_acl, &second);
    zfs_acl_locator_cb_t cb = { .cb_aclp = &acl };
    sa_bulk_attr_t bulk[1];
    int n = 0;
    SA_ADD_BULK_ATTR(bulk, n, SA_ZPL_DACL_ACES(fs),
        zfs_acl_data_locator, &cb, sizeof (expected));
    dmu_tx_t *tx = dmu_tx_create(fs->z_os);
    dmu_tx_hold_sa(tx, zp->z_sa_hdl, B_FALSE);
    error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) dmu_tx_abort(tx);
    else {
        error = sa_bulk_update(zp->z_sa_hdl, bulk, n, tx);
        dmu_tx_commit(tx);
    }
    list_remove(&acl.z_acl, &second);
    list_remove(&acl.z_acl, &first);
    list_destroy(&acl.z_acl);
    if (!error) error = oz_acl_sa_test(zp, B_FALSE);
    return (error);
}

int
oz_acl_storage_test(void)
{
    znode_t native = { .z_is_sa = B_TRUE };
    if (zfs_external_acl(&native) != 0) return (EIO);
    /* Verify upstream's callback contract: whole list chunks, not clipping
     * to buflen. SA owns splitting a returned chunk across bonus/spill data. */
    zfs_acl_t acl = {0};
    unsigned char a[7] = {1,2,3,4,5,6,7}, b[3] = {8,9,10};
    zfs_acl_node_t first = { .z_acldata = a, .z_size = sizeof (a) };
    zfs_acl_node_t second = { .z_acldata = b, .z_size = sizeof (b) };
    list_create(&acl.z_acl, sizeof (zfs_acl_node_t),
        offsetof(zfs_acl_node_t, z_next));
    list_insert_tail(&acl.z_acl, &first);
    list_insert_tail(&acl.z_acl, &second);
    zfs_acl_locator_cb_t cb = { .cb_aclp = &acl };
    void *data = NULL;
    uint32_t size = 0;
    int error = 0;
    zfs_acl_data_locator(&data, &size, 1, B_TRUE, &cb);
    if (data != a || size != sizeof (a)) error = EIO;
    zfs_acl_data_locator(&data, &size, 2, B_FALSE, &cb);
    if (data != b || size != sizeof (b)) error = EIO;
    zfs_acl_data_locator(&data, &size, 0, B_TRUE, &cb);
    if (data != a || size != sizeof (a)) error = EIO;
    list_remove(&acl.z_acl, &second);
    list_remove(&acl.z_acl, &first);
    list_destroy(&acl.z_acl);
    return (error);
}
