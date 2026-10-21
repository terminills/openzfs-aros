/* SPDX-License-Identifier: CDDL-1.0 */
/* Cold writable import of an orderly exported pool; NOT crash replay. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <proto/exec.h>
#include "zpl-fixture.h"
#include "ioctl-copy.h"
int oz_legacy_cold_test(boolean_t);
static void report(const char *label,int result)
{
    char text[128];snprintf(text,sizeof(text),"%s=%d\n",label,result);
    for(char *p=text;*p;p++)RawPutChar(*p);
}
static int checksum_repair(int unrecoverable,int logonly)
{
    spa_t *spa=NULL;
    int error=spa_open("qemu_mirror",&spa,FTAG);
    if(error)return error;
    if(!logonly)error=spa_scan(spa,POOL_SCAN_SCRUB);
    boolean_t waited=B_FALSE;
    if(!error&&!logonly)error=spa_wait("qemu_mirror",ZPOOL_WAIT_SCRUB,&waited);
    pool_scan_stat_t scan={0};
    if(!error&&!logonly)error=spa_scan_get_stats(spa,&scan);
    vdev_t *root=spa->spa_root_vdev;
    if(!error&&(root->vdev_children!=1||root->vdev_child[0]->vdev_children!=2))error=EINVAL;
    if(!error&&!logonly&&!root->vdev_child[0]->vdev_child[0]->vdev_stat.vs_checksum_errors)error=EIO;
    if(!error&&!logonly&&unrecoverable&&!root->vdev_child[0]->vdev_child[1]->vdev_stat.vs_checksum_errors)error=EIO;
    if(!error&&!logonly&&(scan.pss_state!=DSS_FINISHED||(!unrecoverable&&scan.pss_errors)||
        (unrecoverable&&!scan.pss_errors)))error=EIO;
    if(!logonly)report("cold checksum scrub detected and completed",error);
    spa_close(spa,FTAG);
    objset_t *os=NULL;
    if(!error)error=dmu_objset_own("qemu_mirror/payload",DMU_OST_OTHER,B_TRUE,B_FALSE,FTAG,&os);
    unsigned char actual[16384];
    if(!error&&!logonly) {
        int read_error=dmu_read(os,1,0,sizeof(actual),actual,0);
        error=unrecoverable?(read_error==ECKSUM?0:EIO):read_error;
    }
    for(unsigned i=0;i<sizeof(actual)&&!error&&!unrecoverable;i++) {
        unsigned char expected=(i*37u)^(i>>5)^0xa7^((i>=3077&&i<9078)?0x5a:0);
        if(actual[i]!=expected)error=EIO;
    }
    uint64_t objset=os?dmu_objset_id(os):0;
    if(os)dmu_objset_disown(os,B_FALSE,FTAG);
    if(!logonly)report(unrecoverable?"cold checksum unrecoverable read rejected":"cold checksum payload comparison",error);
    if(!error&&unrecoverable) {
        zbookmark_phys_t records[16]={0};uint64_t unused=16;
        struct oz_ioctl_range range={records,sizeof(records),OZ_IOCTL_OUTPUT};
        struct oz_ioctl_copy_scope *scope=NULL;
        spa=NULL;error=spa_open("qemu_mirror",&spa,FTAG);
        if(!error)error=oz_ioctl_copy_enter(&range,1,&scope);
        if(!error)error=spa_get_errlog(spa,records,&unused);
        if(scope){int leave=oz_ioctl_copy_leave(scope);if(!error)error=leave;}
        int found=0;
        if(!error&&unused<=16)for(uint64_t i=unused;i<16;i++)
            if(records[i].zb_objset==objset&&records[i].zb_object==1&&
                records[i].zb_level==0&&records[i].zb_blkid==0)found=1;
        if(!error&&!found)error=EIO;
        if(spa)spa_close(spa,FTAG);
        report(logonly?"cold checksum persistent error log attribution":"cold checksum actual error log attribution",error);
    }
    return error;
}
int main(int argc,char **argv)
{
    int logonly=argc==2&&!strcmp(argv[1],"CHECKSUM-LOG");
    int unrecoverable=logonly||(argc==2&&!strcmp(argv[1],"CHECKSUM-UNRECOVERABLE"));
    int checksum=unrecoverable||(argc==2&&!strcmp(argv[1],"CHECKSUM"));
    if(argc!=1&&!checksum)return 20;
    int fd=open("/SYS/export.nvlist",O_RDONLY);if(fd<0)return 20;
    struct stat st;int error=fstat(fd,&st);
    if(error||st.st_size<=0||st.st_size>1024*1024){close(fd);return 20;}
    char *bytes=malloc(st.st_size);if(!bytes){close(fd);return 20;}
    if(read(fd,bytes,st.st_size)!=st.st_size){close(fd);free(bytes);return 20;}
    close(fd);nvlist_t *config=NULL;
    error=nvlist_unpack(bytes,st.st_size,&config,0);free(bytes);
    if(error)return 20;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1)) {
        nvlist_free(config);return 20;
    }
    spa_config_path="/RAM/nonexistent-openzfs.cache";
    kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);
    nvlist_t *props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    error=spa_import("qemu_mirror",config,props,0);
    report("cold writable spa_import",error);
    if(!error&&checksum)error=checksum_repair(unrecoverable,logonly);
    uint32_t expected=0x81234567;
    for(unsigned pass=0;pass<3&&!error;pass++) {
        struct oz_zpl *fs=NULL;
        error=pass<2?oz_zpl_open_writable("qemu_mirror/mountlife",&fs):
            oz_zpl_open("qemu_mirror/mountlife",&fs);
        uint64_t object=0,size=0;struct oz_zpl_info info;
        if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
        if(!error)error=oz_zpl_stat(fs,object,&info);
        if(!error&&(!info.has_native_protection||info.native_protection!=expected))error=EIO;
        if(!error&&pass<2) {
            expected=pass==0?0x81234568:0x81234567;
            error=oz_zpl_protection_store(fs,object,expected,kcred);
            if(!error)error=oz_zpl_sync(fs);
        }
        int close=oz_zpl_close(fs);if(!error)error=close;
        report("cold writer reopen pass",error);
    }
    if(!error)error=oz_legacy_cold_test(B_FALSE);
    if(!error)error=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
    report("cold writer export",error);
    fnvlist_free(props);nvlist_free(config);kernel_fini();
    const char *result=error?"RESULT cold writable reopen FAIL\n":"RESULT cold writable reopen PASS\n";
    for(const char *p=result;*p;p++)RawPutChar(*p);
    return error?20:0;
}
