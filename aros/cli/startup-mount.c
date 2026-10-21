/* SPDX-License-Identifier: CDDL-1.0 */
/* Pool-scoped startup mount stage, using the real upstream libzfs. */
#include <libzfs.h>
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <errno.h>
/* Native command support shares the upstream command's libzfs handle. */
libzfs_handle_t *g_zfs;
struct state {unsigned mounted,already,skipped,failed;};
static int visit(zfs_handle_t *fs,void *arg)
{
    struct state *state=arg;char point[ZFS_MAXPROPLEN];
    if(zfs_get_type(fs)!=ZFS_TYPE_FILESYSTEM){zfs_close(fs);return 0;}
    if(zfs_prop_get(fs,ZFS_PROP_MOUNTPOINT,point,sizeof(point),NULL,NULL,0,B_TRUE))state->failed++;
    else if(zfs_prop_get_int(fs,ZFS_PROP_CANMOUNT)!=ZFS_CANMOUNT_ON||
        !strcmp(point,"none")||!strcmp(point,"legacy"))state->skipped++;
    else if(zfs_is_mounted(fs,NULL))state->already++;
    else if(zfs_mount(fs,NULL,0)||!zfs_is_mounted(fs,NULL)){
        fprintf(stderr,"Not mounted: %s\n",zfs_get_name(fs));state->failed++;
    }else state->mounted++;
    /* Children have independent properties, even if their parent cannot mount.
     * No pool-global mount-all, sharing, key prompts, or force flags. */
    if(zfs_iter_filesystems_v2(fs,0,visit,state)){
        fprintf(stderr,"Cannot enumerate %s: errno=%d libzfs=%d %s\n",zfs_get_name(fs),errno,
            libzfs_errno(g_zfs),libzfs_error_description(g_zfs));state->failed++;
    }
    zfs_close(fs);return 0;
}
int oz_startup_mount_pool(const char *name,uint64_t guid)
{
    if(!name||!*name||strpbrk(name,"/@#")||!guid)return 1;
    libzfs_handle_t *hdl=libzfs_init();if(!hdl)return 1;
    g_zfs=hdl;
    /* Report partial failure instead of libzfs's abort-on-unknown-error mode. */
    libzfs_print_on_error(hdl,B_FALSE);
    zpool_handle_t *pool=zpool_open(hdl,name);
    int error=1;
    if(pool&&zpool_get_prop_int(pool,ZPOOL_PROP_GUID,NULL)==guid){
        zfs_handle_t *root=zfs_open(hdl,name,ZFS_TYPE_FILESYSTEM);
        if(root){struct state state={0};visit(root,&state);
            printf("Startup mounts: mounted=%u already=%u skipped=%u failed=%u\n",
                state.mounted,state.already,state.skipped,state.failed);
            error=state.failed!=0;}
    }else fprintf(stderr,"Startup mount pool identity unavailable or mismatched\n");
    if(pool)zpool_close(pool);
    libzfs_fini(hdl);g_zfs=NULL;return error;
}
#ifndef OZ_STARTUP_MOUNT_LIBRARY
int main(int argc,char **argv)
{
    if(argc!=3||strlen(argv[2])!=16){
        fprintf(stderr,"Usage: OpenZFSStartupMount POOL GUID-16-HEX\n");return 1;}
    uint64_t guid=0;
    for(unsigned i=0;i<16;i++){
        unsigned d;char c=argv[2][i];
        if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;
        else if(c>='A'&&c<='F')d=c-'A'+10;else return 1;
        guid=(guid<<4)|d;
    }
    return oz_startup_mount_pool(argv[1],guid);
}
#endif
