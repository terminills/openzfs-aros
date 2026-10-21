/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_destroy.h>
#include <sys/dmu_objset.h>
#include <zfs_deleg.h>
#include "zpl-fixture.h"
int oz_zpl_dataset_create(const char *, cred_t *);
int oz_packet_delegated_create_test(const char *, int);
int oz_admission_endpoint_test(const char *);

/* Disposable native pool only. Use the upstream nvlist validator and real
 * DSL on-disk delegation, not an adapter-local permission table. */
static int grant(const char *dataset, const char *who, const char *permission,
    boolean_t unset)
{
    nvlist_t *acl = fnvlist_alloc(), *perms = fnvlist_alloc();
    fnvlist_add_boolean(perms, permission);
    fnvlist_add_nvlist(acl, who, perms);
    int error = zfs_deleg_verify_nvlist(acl);
    if (!error) error = dsl_deleg_set(dataset, acl, unset);
    fnvlist_free(perms);
    fnvlist_free(acl);
    return error;
}

int oz_delegation_test(void)
{
    const char *ds = "qemu_mirror/files";
    gid_t group = 99;
    cred_t *reader = oz_cred_create(77, 77, 88, 1, &group);
    cred_t *other = oz_cred_create(78, 78, 88, 0, NULL);
    if (!reader || !other) { crfree(reader); crfree(other); return ENOMEM; }
    int error = 0;
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    int native_identity=marker!=NULL;
    if(marker)fclose(marker);
#define CHECK(expr) do { if (!(expr)) { error = __LINE__; goto out; } } while (0)
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == EPERM);
    CHECK(grant(ds, "ul$77", "snapshot", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, other) == EPERM);
    CHECK(zfs_secpolicy_snapshot_perms(ds, NULL) == EPERM);
    CHECK(grant(ds, "ul$77", "snapshot", B_TRUE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == EPERM);
    CHECK(grant(ds, "gl$99", "snapshot", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, other) == EPERM);
    CHECK(grant(ds, "gl$99", "snapshot", B_TRUE) == 0);
    /* Local parent grants do not inherit; descendant grants do. */
    CHECK(grant("qemu_mirror", "ul$77", "snapshot", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == EPERM);
    CHECK(grant("qemu_mirror", "ul$77", "snapshot", B_TRUE) == 0);
    CHECK(grant("qemu_mirror", "ud$77", "snapshot", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == 0);
    CHECK(grant("qemu_mirror", "ud$77", "snapshot", B_TRUE) == 0);
    /* Named permission-set expansion is performed by upstream DSL. */
    CHECK(grant(ds, "s-$@oztest", "snapshot", B_FALSE) == 0);
    CHECK(grant(ds, "Ul$77", "@oztest", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, other) == EPERM);
    CHECK(grant(ds, "Ul$77", "@oztest", B_TRUE) == 0);
    CHECK(grant(ds, "s-$@oztest", "snapshot", B_TRUE) == 0);
    CHECK(grant(ds, "el$", "snapshot", B_FALSE) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, other) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, NULL) == EPERM);
    CHECK(dsl_prop_set_int(ds, "zoned", ZPROP_SRC_LOCAL, 1) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == EPERM);
    CHECK(zfs_secpolicy_snapshot_perms(ds, kcred) == 0);
    CHECK(dsl_prop_inherit(ds, "zoned", ZPROP_SRC_INHERITED) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(ds, reader) == 0);
    CHECK(grant(ds, "el$", "snapshot", B_TRUE) == 0);
    /* Existing snapshots require descendant (not local) permissions. */
    CHECK(dmu_objset_snapshot_one(ds, "delegation-test") == 0);
    CHECK(grant(ds, "ul$77", "destroy", B_FALSE) == 0);
    CHECK(grant(ds, "ul$77", "mount", B_FALSE) == 0);
    CHECK(zfs_secpolicy_destroy_perms("qemu_mirror/files@delegation-test", reader) == EPERM);
    CHECK(grant(ds, "ud$77", "destroy", B_FALSE) == 0);
    CHECK(grant(ds, "ud$77", "mount", B_FALSE) == 0);
    CHECK(zfs_secpolicy_destroy_perms("qemu_mirror/files@delegation-test", reader) == 0);
    CHECK(dsl_destroy_snapshot("qemu_mirror/files@delegation-test", B_FALSE) == 0);
    CHECK(grant(ds, "ud$77", "destroy", B_TRUE) == 0);
    CHECK(grant(ds, "ud$77", "mount", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "destroy", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "mount", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "destroy", B_FALSE) == 0);
    CHECK(zfs_secpolicy_destroy_perms(ds, reader) == EPERM);
    CHECK(grant(ds, "ul$77", "mount", B_FALSE) == 0);
    CHECK(zfs_secpolicy_destroy_perms(ds, reader) == 0);
    CHECK(grant(ds, "ul$77", "rename", B_FALSE) == 0);
    CHECK(zfs_secpolicy_rename_perms(ds, "qemu_mirror/moved", reader) == EPERM);
    CHECK(grant("qemu_mirror", "ul$77", "create", B_FALSE) == 0);
    CHECK(zfs_secpolicy_rename_perms(ds, "qemu_mirror/moved", reader) == EPERM);
    CHECK(grant("qemu_mirror", "ul$77", "mount", B_FALSE) == 0);
    CHECK(zfs_secpolicy_rename_perms(ds, "qemu_mirror/moved", reader) == 0);
    CHECK(zfs_secpolicy_rename_perms(ds, "qemu_mirror/moved", other) == EPERM);
    CHECK(grant("qemu_mirror", "ul$77", "mount", B_TRUE) == 0);
    CHECK(grant("qemu_mirror", "ul$77", "create", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "rename", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "mount", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "destroy", B_TRUE) == 0);
    CHECK(zfs_secpolicy_destroy_perms(ds, reader) == EPERM);
    const char *child = "qemu_mirror/files/delegated-child";
    CHECK(oz_zpl_dataset_create(child, NULL) == EACCES);
    CHECK(oz_zpl_dataset_create(child, reader) == EPERM);
    if(native_identity)CHECK(oz_packet_delegated_create_test(child,EPERM) == 0);
    CHECK(grant(ds, "ul$77", "create", B_FALSE) == 0);
    CHECK(oz_zpl_dataset_create(child, reader) == EPERM);
    if(native_identity)CHECK(oz_packet_delegated_create_test(child,EPERM) == 0);
    CHECK(grant(ds, "ul$77", "mount", B_FALSE) == 0);
    CHECK(grant(ds, "c-$", "snapshot", B_FALSE) == 0);
    CHECK(oz_zpl_dataset_create(child, other) == EPERM);
    CHECK(CRED() == NULL);
    CHECK(oz_zpl_dataset_create(child, reader) == 0);
    CHECK(CRED() == NULL);
    CHECK(oz_zpl_dataset_create(child, reader) == EEXIST);
    CHECK(CRED() == NULL);
    CHECK(zfs_secpolicy_snapshot_perms(child, reader) == 0);
    CHECK(zfs_secpolicy_snapshot_perms(child, other) == EPERM);
    struct oz_zpl *fs = NULL;
    CHECK(oz_zpl_open(child, &fs) == 0);
    struct oz_zpl_info info;
    int stat_error = oz_zpl_stat(fs, oz_zpl_root(fs), &info);
    int close_error = oz_zpl_close(fs);
    CHECK(stat_error == 0 && close_error == 0);
    CHECK(info.mapped_uid == 77 && info.mapped_gid == 88 && info.directory);
    CHECK(dsl_destroy_head(child) == 0);
    if(native_identity) {
        CHECK(oz_packet_delegated_create_test(child,0) == 0);
        CHECK(oz_packet_delegated_create_test(child,EEXIST) == 0);
        CHECK(zfs_secpolicy_snapshot_perms(child,reader) == 0);
        CHECK(zfs_secpolicy_snapshot_perms(child,other) == EPERM);
        fs=NULL;
        CHECK(oz_zpl_open(child,&fs) == 0);
        stat_error=oz_zpl_stat(fs,oz_zpl_root(fs),&info);
        close_error=oz_zpl_close(fs);
        CHECK(stat_error == 0 && close_error == 0);
        CHECK(info.mapped_uid == 77 && info.mapped_gid == 88);
        CHECK(dsl_destroy_head(child) == 0);
        CHECK(oz_admission_endpoint_test(child) == 0);
        CHECK(zfs_secpolicy_snapshot_perms(child,reader) == 0);
        fs=NULL;CHECK(oz_zpl_open(child,&fs) == 0);
        stat_error=oz_zpl_stat(fs,oz_zpl_root(fs),&info);
        close_error=oz_zpl_close(fs);
        CHECK(stat_error == 0 && close_error == 0);
        CHECK(info.mapped_uid == 77 && info.mapped_gid == 88);
        CHECK(dsl_destroy_head(child) == 0);
    }
    CHECK(grant(ds, "c-$", "snapshot", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "create", B_TRUE) == 0);
    CHECK(grant(ds, "ul$77", "mount", B_TRUE) == 0);
out:
    crfree(reader);
    crfree(other);
    return error;
#undef CHECK
}
