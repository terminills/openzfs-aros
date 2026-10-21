/* SPDX-License-Identifier: CDDL-1.0 */
/* Cold import diagnostic; explicit degraded writer modes require fresh QEMU
 * COW overlays. Default and VERIFY modes bind native devices read-only. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdarg.h>
#include "zpl-fixture.h"
#include "device-io.h"
#include "partition-io.h"
int oz_dos_handler_test(struct oz_zpl *);
int oz_legacy_cold_test(boolean_t);
int oz_special_file_test(boolean_t);
int oz_mount_lifecycle_test(boolean_t);
int oz_dos_write_test(int);
int oz_atime_persistence_test(boolean_t);
int oz_write_persistence_test(boolean_t);
int oz_dio_cold_read_test(void);
int oz_zpl_layout_test(void);
static void report(const char *fmt,...)
{
    char buf[512];va_list ap;va_start(ap,fmt);vsnprintf(buf,sizeof(buf),fmt,ap);va_end(ap);
    Write(Output(),buf,strlen(buf));for(char *p=buf;*p;p++)RawPutChar(*p);
}
/* Fresh COW writer fixtures only. Verify new data across a separate boot,
 * rather than mistaking a same-process cache read for persistence. */
static int degraded_marker(int write, unsigned member)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;
    char name[40];snprintf(name,sizeof(name),"degraded-member-%u.bin",member);
    const char *dataset="qemu_mirror/degraded-marker";
    int rc=write?oz_zpl_fixture_create(dataset):0;
    if(!rc)rc=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    unsigned char *data=malloc(65536),*actual=malloc(65536);
    if(!data||!actual)rc=ENOMEM;
    if(!rc)rc=write?oz_zpl_create_as(fs,oz_zpl_root(fs),name,0,kcred,&info):
        oz_zpl_lookup_at(fs,oz_zpl_root(fs),name,&info);
    for(unsigned block=0;block<32&&!rc;block++) {
        for(unsigned i=0;i<65536;i++)data[i]=(i*37u+block*19u+member*71u)&255;
        if(write) {
            size_t done=0;
            rc=oz_zpl_write_as(fs,info.object,(uint64_t)block*65536,data,65536,&done,kcred);
            if(!rc&&done!=65536)rc=EIO;
        } else {
            rc=oz_zpl_read(fs,info.object,(uint64_t)block*65536,actual,65536);
            if(!rc&&memcmp(data,actual,65536))rc=EIO;
        }
    }
    if(!rc&&write)rc=oz_zpl_sync(fs);
    if(!rc)rc=oz_zpl_stat(fs,info.object,&info);
    if(!rc&&info.size!=2097152)rc=EIO;
    int close_rc=oz_zpl_close(fs);if(!rc)rc=close_rc;
    free(data);free(actual);
    report("degraded marker member=%u write=%d bytes=2097152 error=%d\n",member,write,rc);
    return rc;
}
static int wait_resilver(spa_t *spa)
{
    for(unsigned attempt=0;attempt<180;attempt++) {
        pool_scan_stat_t scan={0};int rc=spa_scan_get_stats(spa,&scan);
        if(rc&&rc!=ENOENT)return rc;
        if(scan.pss_func==POOL_SCAN_RESILVER&&scan.pss_state==DSS_FINISHED) {
            spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
            int pending=vdev_resilver_needed(spa->spa_root_vdev,NULL,NULL);
            spa_config_exit(spa,SCL_CONFIG,FTAG);
            report("large native resilver processed=%llu errors=%llu pending=%d\n",
                (unsigned long long)scan.pss_processed,(unsigned long long)scan.pss_errors,pending);
            return scan.pss_processed&&!scan.pss_errors&&!pending?0:EIO;
        }
        if(attempt%10==0)report("large native resilver poll=%u func=%llu state=%llu\n",
            attempt,(unsigned long long)scan.pss_func,(unsigned long long)scan.pss_state);
        Delay(50);
    }
    return ETIMEDOUT;
}
int main(int argc,char **argv)
{
    if(argc!=2&&argc!=3)return 20;
    unsigned raidz=!strcmp(argv[1],"READONLY-RAIDZ1")?1:
        !strcmp(argv[1],"READONLY-RAIDZ2")?2:!strcmp(argv[1],"READONLY-RAIDZ3")?3:0;
    unsigned missing_mask=0;
    if(argc==3) {
        if(!raidz)return 20;
        if(strlen(argv[2])==9&&!strncmp(argv[2],"MISSING-",8)&&
            argv[2][8]>='0'&&argv[2][8]<=(raidz==3?'3':'2'))
            missing_mask=1u<<(argv[2][8]-'0');
        else if(!strncmp(argv[2],"MASK-",5)) {
            const char *p=argv[2]+5;
            if(!*p)return 20;
            for(;*p;p++) {
                if(*p<'0'||*p>'9'||missing_mask>15)return 20;
                missing_mask=missing_mask*10+(*p-'0');
            }
        } else return 20;
        unsigned count=raidz==3?4:3,absent=0;
        if(missing_mask>=(1u<<count))return 20;
        for(unsigned i=0;i<count;i++)absent+=(missing_mask>>i)&1;
        if(absent>raidz)return 20;
    }
    int writer=!strcmp(argv[1],"WRITE-AHCI3T-0")||!strcmp(argv[1],"WRITE-AHCI3T-1");
    int verify=!strcmp(argv[1],"VERIFY-AHCI3T-0")||!strcmp(argv[1],"VERIFY-AHCI3T-1");
    int recover=!strcmp(argv[1],"RECOVER-AHCI3T-0")||!strcmp(argv[1],"RECOVER-AHCI3T-1");
    int repaired=!strcmp(argv[1],"REPAIRED-AHCI3T-0")||!strcmp(argv[1],"REPAIRED-AHCI3T-1");
    int writable=writer||recover;
    if(!raidz&&!writer&&!verify&&!recover&&!repaired&&strcmp(argv[1],"READONLY")&&strcmp(argv[1],"DEGRADED")&&strcmp(argv[1],"READONLY-AHCI3T")&&strcmp(argv[1],"READONLY-AHCI3T-0")&&strcmp(argv[1],"READONLY-AHCI3T-1"))return 20;
    unsigned marker_member=(writer||verify||recover||repaired)?argv[1][strlen(argv[1])-1]-'0':0;
    int survivor=!strcmp(argv[1],"READONLY-AHCI3T-0")?0:!strcmp(argv[1],"READONLY-AHCI3T-1")?1:-1;
    if(writer||verify)survivor=argv[1][strlen(argv[1])-1]-'0';
    if(repaired)survivor=1-marker_member;
    int ahci_large=survivor>=0||recover||!strcmp(argv[1],"READONLY-AHCI3T");
    int degraded=missing_mask||survivor>=0||!strcmp(argv[1],"DEGRADED");
    report("Cold boot OpenZFS native mirror import starting\n");
    if(oz_zpl_layout_test())return 20;
    int fd=open("/SYS/export.nvlist",O_RDONLY);if(fd<0)return 20;
    struct stat st;int rc=fstat(fd,&st);
    if(rc||st.st_size<=0||st.st_size>1024*1024){close(fd);return 20;}
    char *bytes=malloc(st.st_size);if(!bytes){close(fd);return 20;}
    if(read(fd,bytes,st.st_size)!=st.st_size){close(fd);free(bytes);return 20;}
    close(fd);nvlist_t *config=NULL;
    rc=nvlist_unpack(bytes,st.st_size,&config,0);free(bytes);
    if(rc){report("configuration unpack=%d\n",rc);return 20;}
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1)){nvlist_free(config);return 20;}
    struct oz_device_binding *bindings[4]={0};
    if(raidz) {
        const unsigned units[]={0,1,3,0};
        unsigned count=raidz==3?4:3;
        for(unsigned member=0;member<count&&!rc;member++) {
            const char *name=member==3?"ahci.device":"ata.device";
            struct oz_device *device=NULL;char path[80];
            snprintf(path,sizeof(path),"/AROS/%s/%u/partition/0",name,units[member]);
            if(missing_mask&(1u<<member)) {
                rc=oz_device_bind_unavailable(path,&bindings[member]);
                report("cold RAIDZ member=%u unavailable-binding=%d\n",member,rc);
                continue;
            }
            rc=oz_device_open(name,units[member],0,&device);
            if(!rc&&oz_device_size(device)!=512ULL*1024*1024)rc=EINVAL;
            oz_device_close(device);device=NULL;
            if(!rc)rc=oz_device_open_partition(name,units[member],0,0,&device);
            if(!rc&&oz_device_size(device)!=256ULL*1024*1024)rc=EINVAL;
            if(!rc)rc=oz_device_bind_readonly(path,device,&bindings[member]);
            oz_device_close(device);
            report("cold RAIDZ member=%u readonly-binding=%d\n",member,rc);
        }
        if(rc) {
            for(unsigned i=0;i<4;i++)oz_device_unbind(bindings[i]);
            nvlist_free(config);return 20;
        }
    }
    if(ahci_large)for(unsigned unit=0;unit<2;unit++) {
        struct oz_device *device=NULL;char path[80];
        snprintf(path,sizeof(path),"/AROS/ahci.device/%u/partition/0",unit);
        if(survivor>=0&&unit!=(unsigned)survivor)
            rc=oz_device_bind_unavailable(path,&bindings[unit]);
        else {
            rc=oz_device_open_partition("ahci.device",unit,0,writable,&device);
            if(!rc&&oz_device_size(device)!=(UINT64_C(3)<<40)-(UINT64_C(2)<<20))rc=EINVAL;
            if(!rc)rc=writable?oz_device_bind_writable(path,device,&bindings[unit]):
                oz_device_bind_readonly(path,device,&bindings[unit]);
        }
        oz_device_close(device);
        if(rc){
            report("large native readonly binding=%d\n",rc);
            for(unsigned i=0;i<2;i++)oz_device_unbind(bindings[i]);
            nvlist_free(config);return 20;
        }
    }
    spa_config_path="/RAM/nonexistent-openzfs.cache";
    kernel_init(SPA_MODE_READ|(writable?SPA_MODE_WRITE:0));
    nvlist_t *props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");fnvlist_add_uint64(props,"readonly",!writable);
    rc=spa_import("qemu_mirror",config,props,0);report("cold spa_import=%d\n",rc);
    if(!rc) {
        spa_t *spa=NULL;rc=spa_open("qemu_mirror",&spa,FTAG);
        if(!rc) {
            if(recover)rc=wait_resilver(spa);
            vdev_state_t actual=spa->spa_root_vdev->vdev_state;
            vdev_state_t expected=degraded?VDEV_STATE_DEGRADED:VDEV_STATE_HEALTHY;
            report("cold pool health=%u expected=%u\n",(unsigned)actual,(unsigned)expected);
            if(actual!=expected)rc=EIO;
            if(ahci_large) {
                uint64_t capacity=spa_get_dspace(spa);
                report("cold large-pool usable bytes=%llu\n",(unsigned long long)capacity);
                if(capacity<=(UINT64_C(2)<<40))rc=EOVERFLOW;
            }
            spa_close(spa,FTAG);
        }
    }
    if(!rc)rc=oz_legacy_cold_test(B_FALSE);
    if(!rc)rc=oz_special_file_test(B_FALSE);
    if(!rc){rc=oz_dio_cold_read_test();report("cold upstream direct UIO read/compare=%d\n",rc);}
    /* This test deliberately clears the mount flag and requires the pool
     * itself to reject atime writes. Keep it in every read-only boot. */
    if(!rc&&!writable){rc=oz_atime_persistence_test(B_FALSE);report("cold native SA persisted atime=%d\n",rc);}
    if(!rc){rc=oz_write_persistence_test(B_FALSE);report("cold upstream vnode written bytes/size=%d\n",rc);}
    if(!rc&&!writable)rc=oz_mount_lifecycle_test(B_FALSE);
    if(!rc)rc=oz_dos_write_test(0);
    struct oz_zpl *fs=NULL;
    if(!rc){rc=oz_zpl_open("qemu_mirror/files",&fs);report("cold ZPL open=%d\n",rc);}
    if(!rc){rc=oz_dos_handler_test(fs);report("cold DOS handler repeated file tests=%d\n",rc);}
    int close_rc=oz_zpl_close(fs);if(!rc)rc=close_rc;
    if(!rc&&(writer||verify||recover||repaired))rc=degraded_marker(writer,marker_member);
    if(!rc&&writable){rc=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);report("degraded writer export=%d\n",rc);}
    fnvlist_free(props);nvlist_free(config);kernel_fini();
    for(unsigned unit=0;unit<4;unit++)oz_device_unbind(bindings[unit]);
    report("RESULT cold-boot QEMU %s through AROS handler %s\n",
        degraded?"degraded mirror":"two-drive pool",rc?"FAIL":"PASS");return rc?20:0;
}
