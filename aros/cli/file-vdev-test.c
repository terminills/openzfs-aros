/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <libzfs_core.h>
#include <aros/debug.h>
#include <errno.h>
#include "../../cmd/zpool/zpool_util.h"
#include "../../lib/libzfs/libzfs_impl.h"
#include "platform-hooks.h"
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
libzfs_handle_t *g_zfs;

static int file_flush_test(void)
{
    const char *path="RAM:oz-cli-flush";
    const char payload[]="OpenZFS native file flush";
    char readback[sizeof(payload)];
    errno=0;
    if(zfs_dev_flush(-1)!=-1||errno!=EBADF)return EIO;
    int fd=open(path,O_CREAT|O_TRUNC|O_RDWR,0600);
    if(fd<0)return errno?errno:EIO;
    int error=0;
    if(write(fd,payload,sizeof(payload))!=sizeof(payload)||zfs_dev_flush(fd)) {
        bug("Native file flush failed errno=%d\n",errno);error=EIO;
    }
    if(close(fd))error=EIO;
    errno=0;
    if(zfs_dev_flush(fd)!=-1||errno!=EBADF)error=EIO;
    fd=open(path,O_RDONLY);
    if(fd<0)error=EIO;
    else {
        if(read(fd,readback,sizeof(readback))!=sizeof(readback)||
           memcmp(readback,payload,sizeof(payload)))error=EIO;
        errno=0;
        if(zfs_dev_flush(fd)!=-1||errno!=EBADF)error=EIO;
        if(close(fd))error=EIO;
    }
    if(unlink(path))error=EIO;
    if(!error)bug("Public upstream file flush PASS write=1 reopen=1 invalid=1 closed=1 readonly=1\n");
    return error;
}

static int upgrade_notice_test(zpool_handle_t *pool,int phase)
{
    char bootfs[ZPOOL_MAXPROPLEN]={0},text[1024];
    if(zpool_get_prop(pool,ZPOOL_PROP_BOOTFS,bootfs,sizeof(bootfs),NULL,B_FALSE)||
       strcmp(bootfs,"-")){bug("Upgrade notice initial bootfs failed errno=%d value=%s\n",errno,bootfs);return EIO;}
    FILE *out=fopen("RAM:oz-upgrade-notice","w+");
    if(!out){bug("Upgrade notice stream open failed errno=%d\n",errno);return errno?errno:EIO;}
    const char *stage="unset-output";
    int error=oz_pool_upgrade_notice(pool,out)||fflush(out)||ftell(out)!=0?EIO:0;
    int changed=0;
    if(!error&&phase==0) {
        stage="set-bootfs";
        if(zpool_set_prop(pool,"bootfs","public_mirror"))error=EIO;
        else changed=1;
        if(error)bug("Upgrade notice set bootfs failed errno=%d libzfs=%s\n",errno,libzfs_error_description(g_zfs));
        if(!error)stage="configured-output";
        if(!error&&(zpool_props_refresh(pool)||oz_pool_upgrade_notice(pool,out)||fflush(out)))error=EIO;
        if(!error) {
            rewind(out);
            size_t count=fread(text,1,sizeof(text)-1,out);text[count]=0;
            if(ferror(out)||!strstr(text,"bootfs set to 'public_mirror'")||
               !strstr(text,"No boot code was changed."))error=EIO;
        }
        if(!error)stage="restore-bootfs";
        if(changed&&(zpool_set_prop(pool,"bootfs","")||zpool_props_refresh(pool)))error=EIO;
        if(!error&&(zpool_get_prop(pool,ZPOOL_PROP_BOOTFS,bootfs,sizeof(bootfs),NULL,B_FALSE)||
           strcmp(bootfs,"-")))error=EIO;
    }
    if(!error&&phase==2) {
        stage="readonly-denial";
        errno=0;
        if(zpool_set_prop(pool,"bootfs","public_mirror/files")!=-1||errno!=EROFS)error=EIO;
        if(!error&&(zpool_props_refresh(pool)||
           zpool_get_prop(pool,ZPOOL_PROP_BOOTFS,bootfs,sizeof(bootfs),NULL,B_FALSE)||
           strcmp(bootfs,"-")))error=EIO;
    }
    if(error)bug("Upgrade notice failed phase=%d stage=%s errno=%d\n",phase,stage,errno);
    if(fclose(out))error=EIO;
    if(remove("RAM:oz-upgrade-notice"))error=EIO;
    if(!error)bug("Public upstream upgrade notice PASS phase=%d unset=1 configured=%d no-boot-write=1 readonly-denied=%d\n",phase,phase==0,phase==2);
    return error;
}

/* phase 0: active, 1: exported, 2: readonly import with exported labels. */
int oz_upstream_file_vdev_test(int phase)
{
    int error=phase==0?file_flush_test():0;
    g_zfs=libzfs_init();if(!g_zfs)return errno?errno:EIO;
    int expected=phase==1?ENOENT:phase==2?EROFS:0;
    for(unsigned force=0;force<2&&!error;force++) {
        nvlist_t *input=fnvlist_alloc();
        fnvlist_add_boolean_value(input,"force",force?B_TRUE:B_FALSE);
        int result=lzc_sync("public_mirror",input,NULL);
        nvlist_free(input);
        if(result!=expected) {
            bug("Public pool sync FAIL phase=%d force=%u actual=%d expected=%d\n",phase,force,result,expected);
            error=EIO;
        }
    }
    if(!error)bug("Public upstream pool sync PASS phase=%d ordinary=1 forced=1 errno=%d\n",phase,expected);
    if(phase!=1) {
        zpool_handle_t *pool=zpool_open(g_zfs,"public_mirror");
        if(!pool)error=EIO;
        else {
            if(!error)error=upgrade_notice_test(pool,phase);
            if(!error) {
                boolean_t force=B_FALSE;
                errno=0;
                int synced=zpool_sync_one(pool,&force);
                if(phase==0?synced!=0:(synced!=-1||errno!=EROFS))error=EIO;
            }
            errno=0;
            if(zpool_power_current_state(pool,"RAM:OZPublicPool/disk0")!=-1||errno!=ENOTSUP||
               zpool_power(pool,"RAM:OZPublicPool/disk0",B_TRUE)!=ENOTSUP||
               zpool_power(pool,"RAM:OZPublicPool/disk0",B_FALSE)!=ENOTSUP)error=EIO;
            if(!error)bug("Public upstream enclosure unsupported PASS phase=%d query=1 on=1 off=1\n",phase);
            if(zpool_props_refresh(pool)||
               zpool_get_prop_int(pool,ZPOOL_PROP_READONLY,NULL)!=(phase==2)||
               !zpool_get_prop_int(pool,ZPOOL_PROP_SIZE,NULL))error=EIO;
            zpool_close(pool);
        }
    }
    for(unsigned i=0;i<2&&!error;i++) {
        char path[64];snprintf(path,sizeof(path),"RAM:OZPublicPool/disk%u",i);
        if(check_file(path,B_FALSE,B_FALSE)!=-1)error=EIO;
        int forced=check_file(path,B_TRUE,B_FALSE);
        if(forced!=(phase==1?0:-1))error=EIO;
        /* Fail the control query without touching the backing file. */
        int saved=g_zfs->libzfs_fd;
        g_zfs->libzfs_fd=-1;
        int failed_query=check_file(path,B_TRUE,B_FALSE);
        g_zfs->libzfs_fd=saved;
        if(failed_query!=-1){bug("File vdev failed-query phase=%d member=%u result=%d errno=%d\n",phase,i,failed_query,errno);error=EIO;}
    }
    if(!error&&(check_file("RAM:OZPublicPool/missing",B_TRUE,B_FALSE)!=-1||
        check_file("RAM:OZPublicPool",B_TRUE,B_FALSE)!=-1))error=EIO;
    libzfs_fini(g_zfs);g_zfs=NULL;
    if(!error)bug("Public upstream file vdev PASS phase=%d members=2 force-policy=1 missing=1 directory=1 query-failure=1\n",phase);
    return error;
}
