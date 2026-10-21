/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>

static void *
reader(void *arg)
{
    cred_t *cr = arg;
    int error = CRED() != NULL;
    struct oz_cred_scope *scope = NULL;
    if (oz_cred_push(cr, &scope) || CRED() != cr) error = 1;
    for (unsigned i = 0; i < 10000; i++) {
        crhold(cr);
        if (crgetuid(cr) != 1234 || crgetgid(cr) != 99 ||
            !groupmember(77, cr) || groupmember(0, cr))
            error = 1;
        crfree(cr);
    }
    if (scope && oz_cred_pop(scope)) error = 1;
    if (CRED() != NULL) error = 1;
    crfree(cr); /* worker owns one reference even if its join fails */
    return ((void *)(uintptr_t)error);
}

int
oz_cred_test(void)
{
    if (crgetuid(NULL) != UID_NOBODY || crgetruid(NULL) != UID_NOBODY ||
        crgetgid(NULL) != GID_NOBODY || crgetngroups(NULL) || crgetgroups(NULL) ||
        groupmember(GID_NOBODY, NULL) || secpolicy_zfs(NULL) != EPERM)
        return (1);
    crhold(NULL); crfree(NULL);
    cred_t *root = kcred;
    crhold(root); crfree(root);
    if (crgetuid(root) || crgetruid(root) || crgetgid(root) ||
        !groupmember(0, root) || secpolicy_zfs(root) ||
        zfs_secpolicy_snapshot_perms("test", root) ||
        zfs_secpolicy_rename_perms("old", "pool/new", root) ||
        zfs_secpolicy_destroy_perms("test", root))
        return (2);
    gid_t groups[] = {55, 77, 88};
    cred_t *cr = oz_cred_create(1234, 4321, 99, 3, groups);
    if (!cr) return (3);
    groups[1] = 0; /* Snapshot must own its copy, not borrow caller storage. */
    int error = 0;
    if (crgetuid(cr) != 1234 || crgetruid(cr) != 4321 || crgetgid(cr) != 99 ||
        crgetngroups(cr) != 3 || !crgetgroups(cr) || crgetgroups(cr)[1] != 77 ||
        !groupmember(99, cr) || !groupmember(55, cr) || !groupmember(77, cr) ||
        !groupmember(88, cr) || groupmember(0, cr) || secpolicy_zfs(cr) != EPERM ||
        zfs_secpolicy_snapshot_perms("test", NULL) != EPERM ||
        zfs_secpolicy_rename_perms("old", "new", NULL) != EPERM ||
        zfs_secpolicy_destroy_perms("test", NULL) != EPERM)
        error = 4;
    pthread_t threads[4];
    struct oz_cred_scope *outer = NULL, *inner = NULL;
    if (CRED() || oz_cred_push(NULL, &inner) != EACCES || inner ||
        oz_cred_push(kcred, &outer) || CRED() != kcred ||
        oz_cred_push(cr, &inner) || CRED() != cr ||
        oz_cred_pop(outer) != EINVAL)
        error = 12;
    if (inner && oz_cred_pop(inner)) error = 12;
    if (outer && CRED() != kcred) error = 12;
    unsigned started = 0;
    for (; !error && started < 4; started++) {
        crhold(cr);
        if (pthread_create(&threads[started], NULL, reader, cr)) {
            crfree(cr);
            error = 5;
            break;
        }
    }
    for (unsigned i = 0; i < started; i++) {
        void *result = NULL;
        if (pthread_join(threads[i], &result) || result)
            error = 6;
    }
    if (outer && oz_cred_pop(outer)) error = 12;
    if (CRED()) error = 12;
    crfree(cr);
    if (error) return (error);
    if (oz_cred_create(1, 1, 1, 1, NULL) ||
        oz_cred_create(1, 1, 1, SIZE_MAX, groups))
        return (7);
    cr = oz_cred_create(2, 2, 3, 0, NULL);
    if (!cr) return (8);
    if (crgetngroups(cr) || crgetgroups(cr) || !groupmember(3, cr)) error = 9;
    crfree(cr);
    /* Test both root-owned setuid and other set-ID files. Real UID zero
     * and group zero alone must never confer the effective-UID privilege. */
    cred_t *effective_root = oz_cred_create(0, 1234, 99, 0, NULL);
    cred_t *real_root = oz_cred_create(1234, 0, 0, 0, NULL);
    if (!effective_root || !real_root) {
        crfree(effective_root);
        crfree(real_root);
        return (10);
    }
    for (unsigned flag = 0; flag < 2; flag++) {
        if (secpolicy_vnode_setid_retain(NULL, NULL, flag) != EPERM ||
            secpolicy_vnode_setid_retain(NULL, real_root, flag) != EPERM ||
            secpolicy_vnode_setid_retain(NULL, effective_root, flag) ||
            secpolicy_vnode_setid_retain(NULL, kcred, flag))
            error = 11;
        if (secpolicy_sys_config(NULL,flag) != EPERM ||
            secpolicy_sys_config(real_root,flag) != EPERM ||
            secpolicy_sys_config(effective_root,flag) ||
            secpolicy_sys_config(kcred,flag)) error = 13;
    }
    if (secpolicy_zinject(NULL) != EACCES ||
        secpolicy_zinject(real_root) != EACCES ||
        secpolicy_zinject(effective_root) || secpolicy_zinject(kcred) ||
        is_system_labeled()) error = 14;
    crfree(effective_root);
    crfree(real_root);
    return (error);
}
