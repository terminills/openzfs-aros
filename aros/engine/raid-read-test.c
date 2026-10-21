/* SPDX-License-Identifier: CDDL-1.0 */
/* Hosted imports of explicitly generated disposable file-vdev fixtures.
 * Default/cold modes are readonly; WRITE-DISPOSABLE-DEGRADED opts into ZPL
 * writes. Real DOS packets verify reads; no Linux-mounted filesystems. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_scan.h>
#include <fcntl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dostags.h>
#include <dos/filehandler.h>
#include "dos-handler.h"
#include "zpl-fixture.h"

static const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
static unsigned char payload_byte(unsigned offset)
{
    uint32_t x=offset+0x9e3779b9U;x^=x>>16;x*=0x7feb352dU;
    x^=x>>15;x*=0x846ca68bU;x^=x>>16;return x;
}
static int write_payload(const char *dataset)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;
    int rc=oz_zpl_open_writable(dataset,&fs);
    if(!rc)rc=oz_zpl_create_as(fs,oz_zpl_root(fs),"raid-write.bin",0,kcred,&info);
    unsigned char bytes[4096];
    for(unsigned offset=0;offset<262144&&!rc;offset+=sizeof(bytes)) {
        for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=payload_byte(offset+i);
        size_t written=0;
        rc=oz_zpl_write_as(fs,info.object,offset,bytes,sizeof(bytes),&written,kcred);
        if(!rc&&written!=sizeof(bytes))rc=EIO;
    }
    if(!rc)rc=oz_zpl_sync(fs);
    int error=oz_zpl_close(fs);return rc?rc:error;
}
struct client {struct Message done;struct MsgPort *port;int result,verify_payload;uint64_t used,available;};
static void *read_client(void *arg)
{
    struct client *c=arg;int rc=0;char data[128];
    BPTR root=Lock((CONST_STRPTR)"OZRAID:",SHARED_LOCK);
    struct InfoData info;
    if(!root||!Info(root,&info))rc=EIO;
    if(!rc) {
        uint64_t block=info.id_BytesPerBlock;
        uint64_t total=c->used+c->available;
        if(getenv("OPENZFS_TEST_ARRAY50")&&total<UINT64_C(50000000000000))rc=EIO;
        if(!block||!info.id_VolumeNode||!info.id_InUse||
            info.id_DiskState!=ID_WRITE_PROTECTED||total<c->used||
            (uint64_t)info.id_NumBlocks!=total/block||
            (uint64_t)info.id_NumBlocksUsed!=c->used/block+(c->used%block!=0))rc=EIO;
        printf("DOS capacity used=%llu available=%llu block=%llu blocks=%llu used-blocks=%llu errno=%d\n",
            (unsigned long long)c->used,(unsigned long long)c->available,
            (unsigned long long)block,(unsigned long long)info.id_NumBlocks,
            (unsigned long long)info.id_NumBlocksUsed,rc);
    }
    if(!rc) {
        struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
        struct DosList *device=FindDosEntry(list,(CONST_STRPTR)"OZRAID",LDF_DEVICES);
        struct FileSysStartupMsg *startup=device?
            BADDR(device->dol_misc.dol_handler.dol_Startup):NULL;
        if(!startup||!startup->fssm_Device||AROS_BSTR_strlen(startup->fssm_Device)||
            AROS_BSTR_ADDR(startup->fssm_Device)[0]||!startup->fssm_Environ)rc=EIO;
        UnLockDosList(LDF_DEVICES|LDF_READ);
        printf("DOS startup descriptor errno=%d\n",rc);
    }
    if(root)UnLock(root);
    const char *names[]={"OZRAID:hello.txt","OZRAID:subdir/nested.txt"};
    for(unsigned i=0;i<2&&!rc;i++) {
        BPTR f=Open((CONST_STRPTR)names[i],MODE_OLDFILE);
        if(!f){rc=EIO;break;}
        LONG n=Read(f,data,sizeof(data));
        if(n!=sizeof(expected)-1||memcmp(data,expected,sizeof(expected)-1))rc=EIO;
        if(Read(f,data,1)!=0||Seek(f,0,OFFSET_BEGINNING)!=sizeof(expected)-1||
            Read(f,data,7)!=7||memcmp(data,expected,7))rc=EIO;
        if(!Close(f))rc=EIO;
    }
    if(!rc&&c->verify_payload) {
        BPTR f=Open((CONST_STRPTR)"OZRAID:raid-write.bin",MODE_OLDFILE);
        if(!f)rc=EIO;
        unsigned char bytes[4096];
        for(unsigned offset=0;f&&offset<262144&&!rc;offset+=sizeof(bytes)) {
            if(Read(f,bytes,sizeof(bytes))!=sizeof(bytes)){rc=EIO;break;}
            for(unsigned i=0;i<sizeof(bytes);i++)if(bytes[i]!=payload_byte(offset+i)){rc=EIO;break;}
        }
        if(f){if(Read(f,bytes,1)!=0)rc=EIO;if(!Close(f))rc=EIO;}
    }
    BPTR f=Open((CONST_STRPTR)names[0],MODE_NEWFILE);
    if(f||IoErr()!=ERROR_DISK_WRITE_PROTECTED)rc=EIO;
    if(f)Close(f);
    if(!rc&&getenv("OPENZFS_TEST_ARRAY50_GUI")) {
        rc=SystemTags((CONST_STRPTR)"Run >T:wanderer.log SYS:System/Wanderer/Wanderer",
            SYS_Asynch,FALSE,TAG_DONE)!=0?EIO:0;
        if(!rc) {
            printf("ARRAY50 GUI READY volume=OZRAID readonly=1; close all drawers before creating SYS:array50-gui-finish\n");
            int finished=0;
            for(unsigned attempt=0;attempt<500&&!finished;attempt++) {
                BPTR marker=Lock((CONST_STRPTR)"SYS:array50-gui-finish",SHARED_LOCK);
                if(marker){UnLock(marker);finished=1;}
                else Delay(50);
            }
            if(!finished)rc=ETIMEDOUT;
        }
    }
    c->result=rc;PutMsg(c->port,&c->done);return NULL;
}
static int mounted_read(const char *dataset,int verify_payload)
{
    struct oz_zpl *fs=NULL;struct oz_mount *mount=NULL;
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    int rc=oz_zpl_open(dataset,&fs);
    if(!rc)rc=oz_dos_mount_fixture(fs,"OZRAID",&mount);
    struct client c={0};c.port=done;c.verify_payload=verify_payload;c.done.mn_Node.ln_Type=NT_MESSAGE;
    if(!rc)oz_zpl_space(fs,&c.used,&c.available);
    c.done.mn_Length=sizeof(c.done);pthread_t thread;
    if(!rc)rc=pthread_create(&thread,NULL,read_client,&c);
    if(!rc) {
        do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}
            while(!GetMsg(done));
        pthread_join(thread,NULL);rc=c.result;
    }
    if(getenv("OPENZFS_TEST_ARRAY50_GUI")&&mount) {
        /* Wanderer is still alive and may retain notification/DeviceProc
         * references. Detach storage, but keep pumping the retired endpoint
         * until the owning host stops this whole private process group. */
        if(!rc) {
            for(unsigned attempt=0;attempt<500;attempt++) {
                rc=oz_dos_detach(mount);
                if(rc!=EBUSY)break;
                oz_dos_pump(mount);Delay(1);
            }
        }
        if(!rc)rc=oz_zpl_close(fs);
        printf("ARRAY50 GUI detached %s errno=%d endpoint-retained=1\n",rc?"FAIL":"PASS",rc);
        for(;;){Wait(oz_dos_signals(mount));oz_dos_pump(mount);}
    }
    if(mount){int error=oz_dos_unmount(mount);if(!rc)rc=error;}
    int error=oz_zpl_close(fs);if(!rc)rc=error;
    DeleteMsgPort(done);return rc;
}
static int read_pool(const char *type,int mode)
{
    int degraded=mode>=1&&mode<=3,writing=mode==2||mode==4;
    char log[128],line[512],pool[128]={0},path[256]={0},dataset[160];
    snprintf(log,sizeof(log),"/SYS/%s-%s-create.log",mode>=2?"raidwrite":"raid",type);
    FILE *f=fopen(log,"r");if(!f)return errno;
    int passed=0;
    while(fgets(line,sizeof(line),f)) {
        char found_type[32],found_pool[128];
        if(sscanf(line,"Creating upstream %31s %127s",found_type,found_pool)==2&&
            !strcmp(found_type,type))strcpy(pool,found_pool);
        sscanf(line,"Exported upstream XDR configuration: %255s",path);
        char verdict[96];snprintf(verdict,sizeof(verdict),"RESULT native %s data roundtrip PASS",type);
        if(strstr(line,verdict))passed=1;
    }
    fclose(f);
    const char *prefix="/SYS/OpenZFS-test-";size_t n=strlen(prefix);
    if(!passed||strncmp(path,prefix,n)||strlen(path)!=n+8+14||strcmp(path+n+8,"/export.nvlist"))return EINVAL;
    for(unsigned i=0;i<8;i++)if(!strchr("0123456789abcdef",path[n+i]))return EINVAL;
    char wanted[128];snprintf(wanted,sizeof(wanted),"aros_%s_%.8s",type,path+n);
    if(strcmp(wanted,pool))return EINVAL;
    f=fopen(path,"rb");if(!f)return errno;
    char packed[65536];size_t length=fread(packed,1,sizeof(packed),f);
    int valid=feof(f)&&!ferror(f);fclose(f);if(!valid)return EIO;
    nvlist_t *config=NULL,*props=fnvlist_alloc();
    int rc=nvlist_unpack(packed,length,&config,0);
    fnvlist_add_string(props,"cachefile","none");fnvlist_add_uint64(props,"readonly",!writing);
    spa_config_path="";kernel_init(SPA_MODE_READ|(writing?SPA_MODE_WRITE:0));
    if(!rc)rc=spa_import(pool,config,props,0);
    if(!rc) {
        spa_t *spa=NULL;rc=spa_open(pool,&spa,FTAG);
        if(!rc) {
            if(mode==4) {
                int finished=0;
                for(unsigned attempt=0;attempt<180&&!rc;attempt++) {
                    pool_scan_stat_t scan={0};rc=spa_scan_get_stats(spa,&scan);
                    if(rc==ENOENT)rc=0;
                    printf("RAID %s resilver poll=%u func=%llu state=%llu repaired=%llu errors=%llu\n",
                        type,attempt,(unsigned long long)scan.pss_func,
                        (unsigned long long)scan.pss_state,(unsigned long long)scan.pss_processed,
                        (unsigned long long)scan.pss_errors);
                    if(!rc&&scan.pss_func==POOL_SCAN_RESILVER&&scan.pss_state==DSS_FINISHED) {
                        spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
                        int pending=vdev_resilver_needed(spa->spa_root_vdev,NULL,NULL);
                        spa_config_exit(spa,SCL_CONFIG,FTAG);
                        if(!scan.pss_processed||scan.pss_errors||pending)rc=EIO;
                        else finished=1;
                        break;
                    }
                    if(!rc)Delay(50);
                }
                if(!rc&&!finished)rc=ETIMEDOUT;
            }
            vdev_state_t state=spa->spa_root_vdev->vdev_state;
            printf("RAID %s degraded=%d root-state=%u\n",type,degraded,(unsigned)state);
            if(state!=(degraded?VDEV_STATE_DEGRADED:VDEV_STATE_HEALTHY)||!!spa_writeable(spa)!=writing)rc=EIO;
            unsigned parity=!strcmp(type,"mirror")?0:type[5]-'0';
            vdev_t *root=spa->spa_root_vdev;
            if(root->vdev_children!=1)rc=EIO;
            else {
                vdev_t *top=root->vdev_child[0];unsigned missing=0;
                for(unsigned i=0;i<top->vdev_children;i++)
                    missing+=top->vdev_child[i]->vdev_state!=VDEV_STATE_HEALTHY;
                if(strcmp(top->vdev_ops->vdev_op_type,parity?"raidz":"mirror")||
                    top->vdev_children!=(getenv("OPENZFS_TEST_ARRAY50")?5:parity+2)||
                    (parity&&vdev_get_nparity(top)!=parity)||
                    missing!=(degraded?(parity?parity:1):0))rc=EIO;
                printf("RAID %s leaves=%u parity=%u unavailable=%u\n",type,
                    (unsigned)top->vdev_children,parity,missing);
            }
            spa_close(spa,FTAG);
        }
    }
    snprintf(dataset,sizeof(dataset),"%s/files",pool);
    if(!rc&&mode==2)rc=write_payload(dataset);
    if(!rc)rc=mounted_read(dataset,mode>=2);
    if(!rc&&writing)rc=spa_export(pool,NULL,B_FALSE,B_FALSE);
    kernel_fini();fnvlist_free(props);nvlist_free(config);
    printf("RESULT cold RAID DOS %s degraded=%d mode=%d errno=%d %s\n",type,degraded,mode,rc,rc?"FAIL":"PASS");
    return rc;
}
int main(int argc,char **argv)
{
    int mode=0;
    if(argc==2) {
        if(!strcmp(argv[1],"DEGRADED"))mode=1;
        else if(!strcmp(argv[1],"WRITE-DISPOSABLE-DEGRADED"))mode=2;
        else if(!strcmp(argv[1],"VERIFY-DEGRADED"))mode=3;
        else if(!strcmp(argv[1],"RECOVER-DISPOSABLE"))mode=4;
        else if(!strcmp(argv[1],"VERIFY-RECOVERED"))mode=5;
        else return 20;
    } else if(argc!=1)return 20;
    setvbuf(stdout,NULL,_IONBF,0);
    const char *array50=getenv("OPENZFS_TEST_ARRAY50");
    if(array50&&(strcmp(array50,"1")||mode))return 20;
    const char *gui=getenv("OPENZFS_TEST_ARRAY50_GUI");
    if(gui&&(!array50||strcmp(gui,"1")))return 20;
    const char *types[]={"mirror","raidz1","raidz2","raidz3"};
    for(unsigned i=0;i<4;i++){
        if(array50&&i!=1)continue;
        if(read_pool(types[i],mode))return 20;
    }
    printf("RESULT cold RAID DOS matrix PASS\n");return 0;
}
