/* SPDX-License-Identifier: CDDL-1.0 */
/* Genuine pre-SA datasets on disposable QEMU disks, shared across boots. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/zap.h>
#include <zfs_prop.h>
#include <proto/exec.h>
#include <dos/dos.h>
#include "zpl-fixture.h"
#include "../zpl/read-mount.h"
#include <sys/cred.h>

static void legacy_create(objset_t *os, void *props, cred_t *cr, dmu_tx_t *tx)
{ zfs_create_fs(os, cr, props, tx); }

static void permission_result(unsigned version, const char *operation, int result, size_t count)
{
    char line[160];
    snprintf(line,sizeof(line),"Legacy permissions version=%u operation=%s result=%d count=%lu\n",
        version,operation,result,(unsigned long)count);
    for(char *p=line;*p;p++)RawPutChar(*p);
}

/* This single-threaded fixture owns the open mount throughout the call.
 * Set the upstream mode/ACL separately from the AROS protection xattr. */
static int legacy_mode(const char *dataset, uint64_t object, mode_t mode)
{
    objset_t *os=NULL;
    int error=dmu_objset_hold(dataset,FTAG,&os);
    if(error)return error;
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs=dmu_objset_get_user(os);
    mutex_exit(&os->os_user_ptr_lock);
    /* The fixture still owns its mount. Drop the objset hold/config lock
     * before a setter can assign a transaction and wait for syncing. */
    dmu_objset_rele(os,FTAG);
    znode_t *zp=NULL;
    if(!fs)error=ESRCH;
    if(!error)error=zfs_zget(fs,object,&zp);
    vattr_t va={.va_mask=ATTR_MODE,.va_mode=mode};
    if(!error)error=zfs_setattr(zp,&va,0,kcred,NULL);
    if(zp)oz_znode_rele(zp);
    return error;
}

static int legacy_one(unsigned version, boolean_t create)
{
    char dataset[96], payload[96];
    snprintf(dataset, sizeof(dataset), "qemu_mirror/legacy-cold-%u", version);
    snprintf(payload, sizeof(payload), "AROS cold legacy version %u payload", version);
    int error=0;
    if (create) {
        nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props, zfs_prop_to_name(ZFS_PROP_VERSION), version);
        struct oz_cred_scope *scope=NULL;
        error=oz_cred_push(kcred, &scope);
        if (!error) {
            error=dmu_objset_create(dataset, DMU_OST_ZFS, 0, NULL, legacy_create, props);
            int popped=oz_cred_pop(scope);
            if (!error) error=popped;
        }
        fnvlist_free(props);
    }
    struct oz_zpl *mount=NULL;
    if (!error) error=create ? oz_zpl_open_writable(dataset, &mount) : oz_zpl_open(dataset, &mount);
    uint64_t object=0, size=0;
    if (!error && create) {
        struct oz_zpl_info info;
        /* This API takes AROS protection bits, not a Unix mode. Owner bits
         * are deny bits; group/other rights are positive grants. */
        error=oz_zpl_create_as(mount, oz_zpl_root(mount), "payload",
            FIBF_GRP_READ|FIBF_OTR_READ, kcred, &info);
        size_t written=0;
        if (!error) error=oz_zpl_write_as(mount, info.object, 0, payload, strlen(payload)+1, &written, kcred);
        if (!error && written!=strlen(payload)+1) error=EIO;
        if (!error) error=legacy_mode(dataset,info.object,0644);
        if (!error) error=oz_zpl_create_as(mount, oz_zpl_root(mount), "private", 0, kcred, &info);
        written=0;
        if (!error) error=oz_zpl_write_as(mount, info.object, 0, payload, strlen(payload)+1, &written, kcred);
        if (!error && written!=strlen(payload)+1) error=EIO;
        if (!error) error=legacy_mode(dataset,info.object,0600);
        if (!error) error=oz_zpl_sync(mount);
    }
    if (!error) error=oz_zpl_lookup(mount, "payload", &object, &size);
    char actual[96];
    if (!error && size!=strlen(payload)+1) error=EIO;
    if (!error) error=oz_zpl_read(mount, object, 0, actual, size);
    if (!error && memcmp(actual, payload, size)) error=EIO;
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if (!reader && !error) error=ENOMEM;
    if (!error) {
        error=oz_zpl_read_as(mount, object, 0, actual, size, reader);
        permission_result(version,"public-read",error,size);
    }
    if (!error && memcmp(actual, payload, size)) error=EIO;
    if (!error) {
        int denied=oz_zpl_access(mount, object, ACE_WRITE_DATA, reader);
        permission_result(version,"write-access",denied,0);
        if(denied!=(create?EACCES:EROFS))error=EIO;
    }
    if (!error && create) {
        size_t written=0;
        int denied=oz_zpl_write_as(mount, object, 0, "X", 1, &written, reader);
        permission_result(version,"write",denied,written);
        if (denied!=EACCES || written) error=EIO;
        if (!error) error=oz_zpl_read(mount, object, 0, actual, size);
        if (!error && memcmp(actual, payload, size)) error=EIO;
    }
    if (!error) error=oz_zpl_lookup(mount, "private", &object, &size);
    if (!error && size!=strlen(payload)+1) error=EIO;
    if (!error) {
        memset(actual,0xa5,sizeof(actual));
        int denied=oz_zpl_read_as(mount, object, 0, actual, size, reader);
        permission_result(version,"private-read",denied,size);
        if (denied!=EACCES) error=EIO;
        for (unsigned i=0; i<sizeof(actual) && !error; i++)
            if ((unsigned char)actual[i]!=0xa5) error=EIO;
    }
    if (!error) error=oz_zpl_read(mount, object, 0, actual, size);
    if (!error && memcmp(actual, payload, size)) error=EIO;
    if (reader) oz_cred_release(reader);
    if (mount) {
        int closed=oz_zpl_close(mount);
        if (!error) error=closed;
    }
    objset_t *os=NULL;
    if (!error) error=dmu_objset_hold(dataset, FTAG, &os);
    if (!error) {
        uint64_t saved=0, sa=0;
        error=zfs_get_zplprop(os, ZFS_PROP_VERSION, &saved);
        if (!error && saved!=version) error=EIO;
        if (!error && zap_lookup(os, MASTER_NODE_OBJ, ZFS_SA_ATTRS, 8, 1, &sa)!=ENOENT) error=EIO;
        if (!error && dsl_dataset_has_owner(dmu_objset_ds(os))) error=EBUSY;
        dmu_objset_rele(os, FTAG);
    }
    return error;
}

int oz_legacy_cold_test(boolean_t create)
{
    int error=0;
    for (unsigned version=1; version<5 && !error; version++)
        error=legacy_one(version, create);
    char result[160];
    snprintf(result, sizeof(result), "RESULT legacy disk matrix %s create=%u versions=1-4 error=%d permissions=1\n",
        error ? "FAIL" : "PASS", (unsigned)create, error);
    for (char *p=result; *p; p++) RawPutChar(*p);
    return error;
}
