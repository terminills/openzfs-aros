/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include "../../lib/libzfs/libzfs_impl.h"
#include <errno.h>
#include <string.h>
#include <aros/debug.h>
static libzfs_handle_t handle;
static int initialized;
void oz_upstream_mount_cache_finish(void)
{
    if(initialized)libzfs_mnttab_fini(&handle);
    memset(&handle,0,sizeof(handle));initialized=0;
}
int oz_upstream_mount_cache_test(const char *name,int readonly)
{
    if(!initialized){libzfs_mnttab_init(&handle);initialized=1;}
    /* First lookup must refresh a previous mount/unmount state. Subsequent
     * iterations alternate owned-cache reuse and fresh registry snapshots. */
    for(unsigned i=0;i<32;i++) {
        libzfs_mnttab_cache(&handle,(i&1)?B_TRUE:B_FALSE);
        struct mnttab entry={0};
        int error=libzfs_mnttab_find(&handle,"public_mirror/files",&entry);
        if(!name) {
            if(error!=ENOENT||avl_numnodes(&handle.libzfs_mnttab_cache))return EIO;
        } else {
            if(error||strcmp(entry.mnt_special,"public_mirror/files")||
               strcmp(entry.mnt_mountp,name)||strcmp(entry.mnt_fstype,"zfs")||
               strcmp(entry.mnt_mntopts,readonly?"ro":"rw")||
               avl_numnodes(&handle.libzfs_mnttab_cache)!=1)return EIO;
        }
    }
    bug("Public upstream mount cache PASS state=%s iterations=32\n",name?name:"empty");
    return 0;
}
