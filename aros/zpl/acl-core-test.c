/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_acl.h>
int oz_acl_evaluate(znode_t *, uint32_t, cred_t *, uint32_t *);
int oz_acl_check(znode_t *, uint32_t, cred_t *, uint32_t *);
int oz_acl_check_append(znode_t *, uint32_t, boolean_t, cred_t *, uint32_t *);

static int
importacl_test(void)
{
    ace_t entries[] = {
        {0, ACE_READ_DATA | ACE_WRITE_DATA, ACE_OWNER, ACE_ACCESS_ALLOWED_ACE_TYPE},
        {0, ACE_WRITE_DATA, ACE_GROUP | ACE_IDENTIFIER_GROUP, ACE_ACCESS_DENIED_ACE_TYPE},
        {0, ACE_READ_DATA, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE}
    };
    int error = 0;
    const uint64_t versions[] = { ZPL_VERSION_INITIAL, ZPL_VERSION_FUID };
    for (unsigned v = 0; v < 2; v++) {
        zfsvfs_t fs = { .z_version = versions[v] };
        vsecattr_t input = { .vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS,
            .vsa_aclcnt = 3, .vsa_aclentp = entries, .vsa_aclentsz = sizeof (entries),
            .vsa_aclflags = ACL_PROTECTED | ACL_DEFAULTED | ACL_AUTO_INHERIT };
        zfs_acl_t *acl = NULL;
        zfs_fuid_info_t *fuids = NULL;
        int rc = zfs_vsec_2_aclp(&fs, S_IFDIR | 0755, &input, kcred, &fuids, &acl);
        if (rc) return (rc);
        if (!acl || acl->z_acl_count != 3 || fuids ||
            acl->z_acl_bytes != 3 * (v ? sizeof (zfs_ace_hdr_t) : sizeof (zfs_oldace_t)) ||
            (acl->z_hints & (ZFS_ACL_PROTECTED | ZFS_ACL_DEFAULTED | ZFS_ACL_AUTO_INHERIT)) !=
                (ZFS_ACL_PROTECTED | ZFS_ACL_DEFAULTED | ZFS_ACL_AUTO_INHERIT)) error = EIO;
        zfs_acl_node_t *node = list_head(&acl->z_acl);
        void *entry = node->z_acldata;
        for (unsigned i = 0; i < 3; i++) {
            if (acl->z_ops->ace_mask_get(entry) != entries[i].a_access_mask ||
                acl->z_ops->ace_flags_get(entry) != entries[i].a_flags ||
                acl->z_ops->ace_type_get(entry) != entries[i].a_type) error = EIO;
            entry = (char *)entry + acl->z_ops->ace_size(entry);
        }
        zfs_acl_free(acl); acl = NULL;
        if (fuids) { zfs_fuid_info_free(fuids); fuids = NULL; }
        if (zfs_vsec_2_aclp(&fs, S_IFREG | 0644, &input, NULL, &fuids, &acl) != EACCES || acl)
            error = EIO;
        input.vsa_aclentsz--;
        if (zfs_vsec_2_aclp(&fs, S_IFREG | 0644, &input, kcred, &fuids, &acl) != EINVAL || acl)
            error = EIO;
        input.vsa_aclentsz++;
        entries[2].a_flags |= ACE_INHERIT_ONLY_ACE;
        if (zfs_vsec_2_aclp(&fs, S_IFDIR | 0755, &input, kcred, &fuids, &acl) != EINVAL || acl)
            error = EIO;
        entries[2].a_flags &= ~ACE_INHERIT_ONLY_ACE;
    }
    return (error);
}

static int
getacl_test(znode_t *zp, uint32_t mask, cred_t *reader)
{
    vsecattr_t v = { .vsa_mask = VSA_ACE | VSA_ACECNT | VSA_ACE_ACLFLAGS };
    int error = zfs_getacl(zp, &v, B_FALSE, kcred);
    if (error) return (error);
    if (v.vsa_aclcnt != 1 || v.vsa_aclentsz != sizeof (ace_t) ||
        !v.vsa_aclentp) error = EIO;
    else {
        ace_t *ace = v.vsa_aclentp;
        if (ace->a_type != ACE_ACCESS_ALLOWED_ACE_TYPE ||
            ace->a_flags != ACE_EVERYONE || ace->a_access_mask != mask)
            error = EIO;
    }
    if (v.vsa_aclentp) kmem_free(v.vsa_aclentp, v.vsa_aclentsz);
    uint64_t saved = zp->z_pflags;
    zp->z_pflags |= ZFS_ACL_DEFAULTED | ZFS_ACL_PROTECTED | ZFS_ACL_AUTO_INHERIT;
    v = (vsecattr_t){ .vsa_mask = VSA_ACECNT | VSA_ACE_ACLFLAGS };
    if (zfs_getacl(zp, &v, B_FALSE, kcred) || v.vsa_aclcnt != 1 ||
        v.vsa_aclentp || v.vsa_aclentsz ||
        v.vsa_aclflags != (ACL_DEFAULTED | ACL_PROTECTED | ACL_AUTO_INHERIT))
        error = EIO;
    zp->z_pflags = saved;
    v = (vsecattr_t){ .vsa_mask = VSA_ACECNT };
    if (zfs_getacl(zp, &v, B_FALSE, reader) !=
        ((mask & ACE_READ_ACL) ? 0 : EACCES)) error = EIO;
    if (zfs_getacl(zp, &v, B_FALSE, NULL) != EACCES ||
        zfs_getacl(zp, &v, B_TRUE, kcred) != ENOTSUP ||
        zfs_getacl(zp, NULL, B_FALSE, kcred) != EINVAL) error = EIO;
    v = (vsecattr_t){0};
    if (zfs_getacl(zp, &v, B_FALSE, kcred) != ENOSYS) error = EIO;
    return (error);
}

int
oz_acl_read_test(znode_t *zp, uint32_t mask)
{
    zfs_acl_t *cached = NULL, *again = NULL, *copy = NULL;
    mutex_enter(&zp->z_acl_lock);
    int error = zfs_acl_node_read(zp, B_FALSE, &cached, B_FALSE);
    if (!error) error = zfs_acl_node_read(zp, B_FALSE, &again, B_FALSE);
    if (!error && (again != cached || zp->z_acl_cached != cached)) error = EIO;
    if (!error) error = zfs_acl_node_read(zp, B_FALSE, &copy, B_TRUE);
    if (!error && (copy == cached || zp->z_acl_cached != cached)) error = EIO;
    if (!error) {
        zfs_acl_t *both[] = {cached, copy};
        zfs_ace_hdr_t expected = {ACE_ACCESS_ALLOWED_ACE_TYPE, ACE_EVERYONE, mask};
        for (unsigned i = 0; i < 2; i++) {
            zfs_acl_node_t *node = list_head(&both[i]->z_acl);
            if (both[i]->z_version != ZFS_ACL_VERSION_FUID ||
                both[i]->z_acl_count != 1 || both[i]->z_acl_bytes != sizeof (expected) ||
                !node || node->z_size != sizeof (expected) ||
                memcmp(node->z_acldata, &expected, sizeof (expected))) error = EIO;
        }
    }
    if (copy && copy != cached) zfs_acl_free(copy);
    /* Keep the read cache owned by the znode; finalization must free it. */
    mutex_exit(&zp->z_acl_lock);
    if (!error) {
        cred_t *reader = oz_cred_create(88, 88, 88, 0, NULL);
        if (!reader) return (ENOMEM);
        if (getacl_test(zp, mask, reader)) error = EIO;
        uint32_t remaining;
        if (oz_acl_evaluate(zp, ACE_READ_DATA, reader, &remaining) || remaining)
            error = EIO;
        int expected = (mask & ACE_WRITE_DATA) ? 0 : EACCES;
        if (oz_acl_evaluate(zp, ACE_WRITE_DATA, reader, &remaining) != expected ||
            remaining != (expected ? ACE_WRITE_DATA : 0)) error = EIO;
        zfsvfs_t *fs = ZTOZSB(zp);
        boolean_t readonly = zfs_is_readonly(fs);
        if (zfs_zaccess(zp, ACE_READ_DATA, 0, B_FALSE, reader, NULL) ||
            zfs_zaccess_rwx(zp, S_IRUSR, 0, reader, NULL) ||
            zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, reader, NULL) !=
                (readonly ? EROFS : expected) ||
            zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, kcred, NULL) !=
                (readonly ? EROFS : 0)) error = EIO;
        if (zfs_zaccess(zp, ACE_READ_DATA, 0, B_FALSE, NULL, NULL) != EACCES ||
            zfs_zaccess(zp, ACE_READ_DATA, 0, B_TRUE, reader, NULL) != ENOTSUP ||
            zfs_zaccess(zp, 0x80000000U, 0, B_FALSE, reader, NULL) != EINVAL)
            error = EIO;
        if (oz_acl_check(zp, ACE_READ_DATA, reader, &remaining)) error = EIO;
        if (oz_acl_check(zp, ACE_WRITE_DATA, reader, &remaining) !=
            (readonly ? EROFS : expected)) error = EIO;
        /* Change only the private cached fixture ACL, never its stored ACL.
         * Append rights must not authorize arbitrary writes or other missing
         * rights in a compound request. Restore before later policy tests. */
        mutex_enter(&zp->z_acl_lock);
        zfs_acl_node_t *node = list_head(&cached->z_acl);
        zfs_ace_hdr_t *entry = node->z_acldata;
        uint32_t saved_mask = entry->z_access_mask;
        entry->z_access_mask = ACE_APPEND_DATA;
        mutex_exit(&zp->z_acl_lock);
        if (zfs_zaccess(zp, ACE_WRITE_DATA, V_APPEND, B_FALSE, reader, NULL) !=
                (readonly ? EROFS : 0) ||
            zfs_zaccess(zp, ACE_WRITE_DATA, 0, B_FALSE, reader, NULL) !=
                (readonly ? EROFS : EACCES)) error = EIO;
        /* Implicit owner rights and privileged overrides are in the full
         * entry point, unlike the raw ACE evaluator tested separately. */
        /* The legacy fixture deliberately has an unmapped foreign FUID.
         * Its low 32 bits are not a local owner identity. */
        if (zp->z_uid <= UINT32_MAX) {
            cred_t *owner = oz_cred_create(zp->z_uid, zp->z_uid, 99, 0, NULL);
            if (!owner) error = ENOMEM;
            else {
                if (zfs_zaccess(zp, ACE_READ_ACL | ACE_READ_ATTRIBUTES, 0,
                    B_FALSE, owner, NULL)) error = EIO;
                crfree(owner);
            }
        } else {
            cred_t *low = oz_cred_create((uid_t)zp->z_uid, (uid_t)zp->z_uid,
                99, 0, NULL);
            if (!low) error = ENOMEM;
            else {
                if (zfs_zaccess(zp, ACE_READ_ACL | ACE_READ_ATTRIBUTES, 0,
                    B_FALSE, low, NULL) != EACCES) error = EIO;
                crfree(low);
            }
        }
        mode_t saved_mode = zp->z_mode;
        zp->z_mode = S_IFREG | 0600;
        if (zfs_zaccess(zp, ACE_EXECUTE, 0, B_FALSE, kcred, NULL) != EACCES)
            error = EIO;
        zp->z_mode |= S_IXUSR;
        if (zfs_zaccess(zp, ACE_EXECUTE, 0, B_FALSE, kcred, NULL)) error = EIO;
        zp->z_mode = saved_mode;
        if (oz_acl_check(zp, ACE_WRITE_DATA, reader, &remaining) !=
            (readonly ? EROFS : EACCES)) error = EIO;
        if (oz_acl_check_append(zp, ACE_WRITE_DATA, B_TRUE, reader, &remaining) !=
            (readonly ? EROFS : 0) || (!readonly && remaining)) error = EIO;
        if (oz_acl_check_append(zp, ACE_READ_DATA | ACE_WRITE_DATA, B_TRUE,
            reader, &remaining) != (readonly ? EROFS : EACCES)) error = EIO;
        if (oz_acl_check_append(zp, ACE_WRITE_DATA, B_TRUE, NULL, &remaining) != EACCES)
            error = EIO;
        mutex_enter(&zp->z_acl_lock);
        entry->z_access_mask = saved_mask;
        mutex_exit(&zp->z_acl_lock);
        uint64_t saved = zp->z_pflags;
        struct { uint64_t flag; uint32_t request; int result; } policies[] = {
            {ZFS_IMMUTABLE, ACE_WRITE_DATA, readonly ? EROFS : EPERM},
            {ZFS_NOUNLINK, ACE_DELETE, readonly ? EROFS : EPERM},
            {ZFS_AV_QUARANTINED, ACE_READ_DATA, EACCES},
            {ZFS_READONLY, ACE_WRITE_DATA, readonly ? EROFS : EPERM}
        };
        for (unsigned i = 0; i < sizeof (policies) / sizeof (policies[0]); i++) {
            zp->z_pflags = saved | policies[i].flag;
            if (oz_acl_check(zp, policies[i].request, reader, &remaining) != policies[i].result ||
                oz_acl_check(zp, policies[i].request, kcred, &remaining) != policies[i].result)
                error = EIO;
            int ordinary = policies[i].result, privileged = policies[i].result;
            if (policies[i].flag == ZFS_READONLY && !readonly) {
                ordinary = EACCES;
                privileged = 0;
            }
            if (zfs_zaccess(zp, policies[i].request, 0, B_FALSE, reader, NULL) != ordinary ||
                zfs_zaccess(zp, policies[i].request, 0, B_FALSE, kcred, NULL) != privileged)
                error = EIO;
        }
        zp->z_pflags = saved;
        fs->z_replay = B_TRUE;
        if (oz_acl_check(zp, ACE_READ_DATA, reader, &remaining) != ENOTSUP) error = EIO;
        if (zfs_zaccess(zp,ACE_READ_DATA,0,B_FALSE,reader,NULL)!=EACCES ||
            zfs_zaccess(zp,ACE_READ_DATA,0,B_FALSE,kcred,NULL)!=0) error=EIO;
        fs->z_replay = B_FALSE;
        fs->z_unmounted = B_TRUE;
        if (oz_acl_check(zp, ACE_READ_DATA, reader, &remaining) != EIO) error = EIO;
        fs->z_unmounted = B_FALSE;
        crfree(reader);
    }
    return (error);
}

int
oz_acl_core_test(void)
{
    /* Real common byteswap entry points required by cross-endian ZIL replay.
     * This tests representation conversion, not replay of an intent log. */
    ace_t original = {0x12345678, ACE_READ_DATA | ACE_READ_ACL,
        ACE_IDENTIFIER_GROUP, ACE_ACCESS_DENIED_ACE_TYPE};
    for (unsigned legacy = 0; legacy < 2; legacy++) {
        ace_t swapped = original;
        if (legacy) zfs_oldace_byteswap(&swapped, 1);
        else zfs_ace_byteswap(&swapped, sizeof (swapped), B_FALSE);
        if (swapped.a_who != BSWAP_32(original.a_who) ||
            swapped.a_access_mask != BSWAP_32(original.a_access_mask) ||
            swapped.a_flags != BSWAP_16(original.a_flags) ||
            swapped.a_type != BSWAP_16(original.a_type)) return (EIO);
        if (legacy) zfs_oldace_byteswap(&swapped, 1);
        else zfs_ace_byteswap(&swapped, sizeof (swapped), B_FALSE);
        if (memcmp(&swapped, &original, sizeof (original))) return (EIO);
    }
    int import_error = importacl_test();
    if (import_error) return (import_error);
    const zfs_oldace_t old[3] = {
        {0, ACE_READ_DATA | ACE_WRITE_DATA, ACE_OWNER, ACE_ACCESS_ALLOWED_ACE_TYPE},
        {0, ACE_WRITE_DATA, ACE_GROUP | ACE_IDENTIFIER_GROUP, ACE_ACCESS_DENIED_ACE_TYPE},
        {0, ACE_READ_DATA, ACE_EVERYONE, ACE_ACCESS_ALLOWED_ACE_TYPE}
    };
    zfsvfs_t fs = {0};
    znode_t zp = { .z_zfsvfs = &fs, .z_mode = S_IFREG | 0640 };
    zfs_acl_t *acl = zfs_acl_alloc(ZFS_ACL_VERSION_INITIAL);
    zfs_acl_node_t *first = zfs_acl_node_alloc(2 * sizeof (old[0]));
    zfs_acl_node_t *second = zfs_acl_node_alloc(sizeof (old[0]));
    memcpy(first->z_acldata, old, 2 * sizeof (old[0]));
    memcpy(second->z_acldata, &old[2], sizeof (old[0]));
    first->z_ace_count = 2;
    second->z_ace_count = 1;
    list_insert_tail(&acl->z_acl, first);
    list_insert_tail(&acl->z_acl, second);
    acl->z_acl_count = 3;
    acl->z_acl_bytes = sizeof (old);
    zfs_acl_xform(&zp, acl, kcred);
    int error = 0;
    first = list_head(&acl->z_acl);
    if (acl->z_version != ZFS_ACL_VERSION_FUID || acl->z_acl_count != 3 ||
        acl->z_acl_bytes != 3 * sizeof (zfs_ace_hdr_t) || !first ||
        list_next(&acl->z_acl, first) != NULL) error = EIO;
    if (!error) {
        zfs_ace_hdr_t *result = first->z_acldata;
        for (unsigned i = 0; i < 3; i++) {
            if (result[i].z_flags != old[i].z_flags ||
                result[i].z_type != old[i].z_type ||
                result[i].z_access_mask != old[i].z_access_mask ||
                acl->z_ops->ace_size(&result[i]) != sizeof (result[i])) error = EIO;
        }
    }
    if (!error) {
        zp.z_uid = 42;
        zp.z_gid = 99;
        zp.z_acl_cached = acl;
        mutex_init(&zp.z_acl_lock, NULL, MUTEX_DEFAULT, NULL);
        cred_t *owner = oz_cred_create(42, 42, 99, 0, NULL);
        cred_t *member = oz_cred_create(77, 77, 99, 0, NULL);
        cred_t *other = oz_cred_create(88, 88, 88, 0, NULL);
        if (!owner || !member || !other) error = ENOMEM;
        if (!error) {
            struct { cred_t *cr; uint32_t mask; int result; } cases[] = {
                {owner, ACE_READ_DATA | ACE_WRITE_DATA, 0},
                {member, ACE_WRITE_DATA, EACCES},
                {member, ACE_READ_DATA, 0},
                {other, ACE_WRITE_DATA, EACCES},
                {other, ACE_READ_DATA, 0},
                {kcred, ACE_WRITE_DATA, EACCES}, /* evaluator has no root bypass */
                {NULL, ACE_READ_DATA, EACCES},
                {other, 0, 0},
                {other, 0x80000000U, EINVAL}
            };
            for (unsigned i = 0; i < sizeof (cases) / sizeof (cases[0]); i++) {
                uint32_t remaining = cases[i].mask;
                int e = oz_acl_evaluate(&zp, cases[i].mask, cases[i].cr, &remaining);
                if (e != cases[i].result || (!e && remaining)) error = EIO;
            }
            zfs_ace_hdr_t *entries = first->z_acldata;
            entries[0].z_type = ACE_ACCESS_DENIED_ACE_TYPE;
            uint32_t remaining = 0;
            if (oz_acl_evaluate(&zp, ACE_READ_DATA, owner, &remaining) != EACCES ||
                remaining != ACE_READ_DATA) error = EIO;
            /* Inherit-only entries on a directory describe descendants,
             * not access to that directory itself. */
            zp.z_mode = S_IFDIR | 0755;
            for (unsigned i = 0; i < 3; i++)
                entries[i].z_flags |= ACE_FILE_INHERIT_ACE | ACE_INHERIT_ONLY_ACE;
            if (oz_acl_evaluate(&zp, ACE_READ_DATA, other, &remaining) != EACCES)
                error = EIO;
            entries[2].z_flags &= ~ACE_INHERIT_ONLY_ACE;
            if (oz_acl_evaluate(&zp, ACE_READ_DATA, other, &remaining) || remaining)
                error = EIO;
        }
        crfree(owner); crfree(member); crfree(other);
        zp.z_acl_cached = NULL;
        mutex_destroy(&zp.z_acl_lock);
    }
    zfs_acl_free(acl);
    return (error);
}
