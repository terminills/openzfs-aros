/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <sys/mount.h>
#include <errno.h>
#include <string.h>
#include "../../lib/libzfs/libzfs_impl.h"
#include "../ui/mount-api.h"
#include "../ui/admission-api.h"

int do_mount(zfs_handle_t *zhp,const char *mountpoint,const char *options,int flags)
{
    if(!zhp||!mountpoint)return EINVAL;
    /* MS_CRYPT is consumed by upstream key loading before this call. */
    if(flags&~MS_CRYPT)return ENOTSUP;
    size_t n=strlen(mountpoint);
    if(n<2||mountpoint[n-1]!=':')return EINVAL;
    char volume[32];
    if(n-1>=sizeof(volume))return ENAMETOOLONG;
    memcpy(volume,mountpoint,n-1);volume[n-1]=0;
    if(strspn(volume,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=n-1)return EINVAL;
    ULONG mode=0;
    for(const char *p=options?options:"";*p;) {
        const char *end=strchr(p,',');if(!end)end=p+strlen(p);
        size_t length=end-p;
        if(length==2&&!memcmp(p,"ro",2))mode=OZ_MOUNT_READONLY;
        else if(length==2&&!memcmp(p,"rw",2))mode=0;
        else if(length&&!(length==8&&!memcmp(p,"defaults",8))&&
                !(length==7&&!memcmp(p,"zfsutil",7)))return ENOTSUP;
        p=*end?end+1:end;
    }
    return oz_mount_control(OZ_ADMISSION_PORT,zfs_get_name(zhp),volume,OZ_MOUNT_ATTACH,mode);
}

int do_unmount(zfs_handle_t *zhp,const char *mountpoint,int flags)
{
    if(!mountpoint)return EINVAL;
    if(flags&~MS_CRYPT)return ENOTSUP;
    struct oz_mount_snapshot *snapshot=NULL;
    int error=oz_mounts_read(OZ_ADMISSION_PORT,&snapshot);
    if(error)return error;
    struct oz_mount_record *records=(void *)(snapshot+1);
    const char *dataset=NULL;
    for(ULONG i=0;i<snapshot->count;i++) {
        if(!strcasecmp(records[i].mountpoint,mountpoint)) {
            if(zhp&&strcmp(zfs_get_name(zhp),records[i].dataset)){error=EINVAL;break;}
            dataset=records[i].dataset;break;
        }
    }
    if(!error)error=dataset?oz_mount_control(OZ_ADMISSION_PORT,dataset,"",OZ_MOUNT_DETACH,0):ENOENT;
    free(snapshot);return error;
}

int zfs_mount_setattr(zfs_handle_t *zhp,uint32_t flags)
{
    if(!zhp){errno=EINVAL;return -1;}
    /* A property setter refreshes dataset stats but not this per-handle
     * mount-option cache. Do not compare against the old ro/rw snapshot. */
    free(zhp->zfs_mntopts);zhp->zfs_mntopts=NULL;zhp->zfs_mntcheck=B_FALSE;
    boolean_t cached=zhp->zfs_hdl->libzfs_mnttab_enable;
    libzfs_mnttab_cache(zhp->zfs_hdl,B_FALSE);
    const struct {uint32_t flag;ULONG policy;zfs_prop_t prop;} map[]={
        {ZFS_MNT_PROP_READONLY,OZ_POLICY_READONLY,ZFS_PROP_READONLY},
        {ZFS_MNT_PROP_ATIME,OZ_POLICY_ATIME,ZFS_PROP_ATIME},
        {ZFS_MNT_PROP_RELATIME,OZ_POLICY_RELATIME,ZFS_PROP_RELATIME},
        {ZFS_MNT_PROP_EXEC,OZ_POLICY_EXEC,ZFS_PROP_EXEC}
    };
    uint32_t known=0;ULONG wanted=0,expected=0,supported=0,enabled=0;
    for(unsigned i=0;i<sizeof(map)/sizeof(map[0]);i++) {
        known|=map[i].flag;
        if(flags&map[i].flag) {
            wanted|=map[i].policy;
            if(zfs_prop_get_int(zhp,map[i].prop))expected|=map[i].policy;
        }
    }
    libzfs_mnttab_cache(zhp->zfs_hdl,cached);
    int error=(flags&~known)?ENOTSUP:
        oz_mount_policy_query(OZ_ADMISSION_PORT,zfs_get_name(zhp),&supported,&enabled);
    /* DSL callbacks already apply these properties to the native mount.
     * Verify effective policy instead of tearing down busy DOS references. */
    if(!error&&(wanted&~supported))error=ENOTSUP;
    if(!error&&(enabled&wanted)!=expected)error=EAGAIN;
    if(error) {
        errno=error;
        zfs_error_aux(zhp->zfs_hdl,"%s",error==ENOTSUP?
            "requested native mount policy is not implemented":error==EAGAIN?
            "effective mount policy changed or does not match the dataset":strerror(error));
        return zfs_error_fmt(zhp->zfs_hdl,EZFS_MOUNTFAILED,
            "cannot verify native mount properties for '%s'",zfs_get_name(zhp));
    }
    return 0;
}
