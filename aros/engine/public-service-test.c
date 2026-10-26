/* SPDX-License-Identifier: CDDL-1.0 */
/* Private-ISO lifecycle test; optional new read-only partition fixtures. */
#include "../ui/api.h"
#include "../ui/admission-api.h"
#include "../ui/mount-api.h"
#include "../ui/support-api.h"
#include "../ui/partition-api.h"
#include "../ui/zvol-api.h"
#include "../device/mountfile.h"
#include "../cli/process.h"
#include "../ui/project-worker.h"
#include "../ui/startup-settings.h"
#include "hostid-record.h"
#include "include/sys/mnttab.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/dos64.h>
#include <proto/security.h>
#include <proto/intuition.h>
#include <intuition/intuitionbase.h>
#include <libraries/security.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <dos/exall.h>
#include <dos/dos64.h>
#include <dos/notify.h>
#include <dos/filesystemids.h>
#include <exec/memory.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/trackdisk.h>
#include <hidd/storage.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <aros/debug.h>
/* Use one formatted kernel message, not independently interleavable bytes. */
static void note(const char *s){bug("%s",s);}
struct Library *DOS64Base;
static void diagnostic_log(const char *path)
{
    BPTR f=Open((CONST_STRPTR)path,MODE_OLDFILE);
    if(!f)return;
    char line[300];snprintf(line,sizeof(line),"Public diagnostic begin %s\n",path);note(line);
    for(unsigned total=0;total<16384;){
        LONG n=Read(f,line,sizeof(line)-1);if(n<=0)break;
        line[n]=0;note(line);total+=(unsigned)n;
    }
    Close(f);note("\nPublic diagnostic end\n");
}
static int desktop_detach(int gui,const char *phase)
{
    int error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
    unsigned retries=0;
    /* Only ordinary desktop teardown waits for transient inspection packets.
     * Deliberate held-reference refusal tests must remain single attempts. */
    while(gui&&error==EBUSY&&retries<50){
        Delay(5);++retries;
        error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
    }
    char line[160];snprintf(line,sizeof(line),"Public desktop detach phase=%s retries=%u errno=%d\n",phase,retries,error);note(line);
    return error;
}
static void desktop_windows(void)
{
    struct IntuitionBase *IntuitionBase=(struct IntuitionBase *)OpenLibrary((CONST_STRPTR)"intuition.library",0);
    if(!IntuitionBase)return;
    BOOL front=WBenchToFront();
    /* Copy under Intuition's lock, emit serial output only after releasing it. */
    char titles[24][96];unsigned count=0;
    ULONG key=LockIBase(0);
    for(struct Screen *s=IntuitionBase->FirstScreen;s&&count<24;s=s->NextScreen){
        snprintf(titles[count++],96,"Public desktop screen: %.65s\n",s->Title?(char *)s->Title:"(untitled)");
        for(struct Window *w=s->FirstWindow;w&&count<24;w=w->NextWindow)
            snprintf(titles[count++],96,"Public desktop window: %.65s\n",w->Title?(char *)w->Title:"(untitled)");
    }
    UnlockIBase(key);
    note(front?"Public desktop WBenchToFront=1\n":"Public desktop WBenchToFront=0\n");
    for(unsigned i=0;i<count;i++)note(titles[i]);
    CloseLibrary((struct Library *)IntuitionBase);
}
char *zfs_version_kernel(void);
#ifdef OZ_TEST_NATIVE_CPU
int aros_zfs_ncpus(void);
#endif
#ifdef OZ_TEST_INVENTORY_WORKER
int oz_inventory_worker_test(void);
#endif
#ifdef OZ_TEST_UPSTREAM_INIT
int oz_upstream_init_test(void);
int oz_upstream_mount_test(void);
int oz_upstream_promotion_readonly_test(int);
int oz_upstream_import_files_test(const char *);
int oz_upstream_file_vdev_test(int,const char *);
int oz_upstream_mount_policy_test(void);
int oz_upstream_device_test(int);
int oz_upstream_device_wait_test(int);
int oz_upstream_native_label_test(int);
#endif
#ifdef OZ_TEST_UPSTREAM_MOUNT_CACHE
int oz_upstream_mount_cache_test(const char *,int);
void oz_upstream_mount_cache_finish(void);
#endif
#ifdef OZ_NATIVE_DISPATCHER
int oz_public_ioctl_test(const char *);
int oz_public_bootenv_readonly_test(const char *);
int oz_public_bootenv_persistence_test(const char *,int);
int oz_public_accounting_upgrade_test(const char *,int);
#endif
static void child_exited(IPTR status,IPTR result){*(LONG *)result=(LONG)status;}
static int present(const char *name){Forbid();int found=FindPort((CONST_STRPTR)name)!=NULL;Permit();return found;}
static const char *storage_directory="RAM:OZPublicPool";
static unsigned storage_members=2,storage_layout=OZ_MIRROR,storage_missing=1;
static int storage_reply_received;
static int native_file_fixture;
static int storage_replace,storage_replaced;
static int storage_dspare;
static int storage_sequential;
static char storage_spare_name[160];
static void storage_member(char *path,size_t size,unsigned member)
{
    snprintf(path,size,"%s%sdisk%u%s",storage_directory,
        storage_directory[strlen(storage_directory)-1]==':'?"":"/",member,
        storage_replaced&&member==0?"-new":"");
}
static const char storage_payload[]="Public OpenZFS DOS payload\n";
static const char storage_passphrase[]="AROS public passphrase fixture\n";
static const char storage_rotated_passphrase[]="AROS rotated public passphrase fixture\n";
static int desktop_directory(const char *volume,const char *phase)
{
    BPTR root=Lock((CONST_STRPTR)volume,SHARED_LOCK);
    LONG doserr=IoErr();const char *stage="lock";
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
    struct ExAllData *buffer=AllocVec(4096,MEMF_ANY|MEMF_CLEAR);
    int error=0;unsigned next_found=0,all_found=0,next_count=0,all_count=0;
    unsigned next_large=0,all_large=0;
    if(ctl){ctl->eac_LastKey=0;ctl->eac_MatchString=NULL;ctl->eac_MatchFunc=NULL;}
    if(!root||!fib||!ctl||!buffer){error=ENOMEM;goto done;}
    stage="examine";
    if(!Examine(root,fib)){doserr=IoErr();error=EIO;goto done;}
    /* Verify the DOS identity independently of the desktop title's lifetime.
     * Callers pass a mounted root (OZPUBLIC: or OZCOLD:), not a subdirectory. */
    stage="root-name";
    size_t volume_length=strlen(volume);
    char root_path[256];
    if(!volume_length||volume[volume_length-1]!=':'||
       volume_length>=sizeof(root_path)||
       !NameFromLock(root,(STRPTR)root_path,sizeof(root_path))||
       strcmp(root_path,volume)||
       strlen((char *)fib->fib_FileName)!=volume_length-1||
       memcmp(fib->fib_FileName,volume,volume_length-1)){
        doserr=IoErr();error=EIO;goto done;
    }
    stage="exnext";
    while(ExNext(root,fib)){
        if(++next_count>64){error=EOVERFLOW;goto done;}
        if(!strcmp(fib->fib_FileName,"hello.txt")&&fib->fib_DirEntryType<0&&
           fib->fib_Size==sizeof(storage_payload)-1)++next_found;
        if(!strcmp(fib->fib_FileName,"large-sparse.bin")&&fib->fib_DirEntryType<0&&
           fib->fib_Size==(sizeof(fib->fib_Size)==4?INT32_MAX:UINT64_C(0x100001000)))++next_large;
    }
    doserr=IoErr();
    if(doserr!=ERROR_NO_MORE_ENTRIES||next_found!=1||next_large!=1){error=EIO;goto done;}
    stage="exnext64";
    struct FileInfoBlock64 wide;
    struct FileLock *fl=BADDR(root);
    if(!DoPkt(fl->fl_Task,ACTION_EXAMINE_OBJECT64,(SIPTR)root,(SIPTR)MKBADDR(&wide),0,0,0)){
        doserr=IoErr();error=EIO;goto done;
    }
    unsigned wide_found=0,wide_count=0;
    while(DoPkt(fl->fl_Task,ACTION_EXAMINE_NEXT64,(SIPTR)root,(SIPTR)MKBADDR(&wide),0,0,0)){
        if(++wide_count>64){error=EOVERFLOW;goto done;}
        /* Raw packet strings are BCPL; dos64.library normally converts. */
        if(wide.fib_FileName[0]==16&&!memcmp(wide.fib_FileName+1,"large-sparse.bin",16)&&
           wide.fib_Size==UINT64_C(0x100001000))++wide_found;
    }
    doserr=IoErr();
    if(doserr!=ERROR_NO_MORE_ENTRIES||wide_found!=1){error=EIO;goto done;}
    stage="dos64-library";
    DOS64Base=OpenLibrary((CONST_STRPTR)"dos64.library",0);
    if(!DOS64Base){doserr=IoErr();error=ENOSYS;goto done;}
    if(!Examine64(root,&wide,NULL)){doserr=IoErr();error=EIO;goto done;}
    wide_found=0;wide_count=0;
    while(ExNext64(root,&wide,NULL)){
        if(++wide_count>64){error=EOVERFLOW;goto done;}
        if(!strcmp((char *)wide.fib_FileName,"large-sparse.bin")&&
           wide.fib_Size==UINT64_C(0x100001000))++wide_found;
    }
    doserr=IoErr();
    if(doserr!=ERROR_NO_MORE_ENTRIES||wide_found!=1){error=EIO;goto done;}
    char large_path[96];snprintf(large_path,sizeof(large_path),"%slarge-sparse.bin",volume);
    BPTR large=Open((CONST_STRPTR)large_path,MODE_OLDFILE);
    if(!large){doserr=IoErr();error=EIO;goto done;}
    BOOL examined=ExamineFH64(large,&wide,NULL);doserr=IoErr();
    if(!examined||strcmp((char *)wide.fib_FileName,"large-sparse.bin")||
       wide.fib_Size!=UINT64_C(0x100001000))error=EIO;
    if(!Close(large))error=EIO;
    if(error)goto done;
    stage="dos64-invalid";
    if(DoPkt(fl->fl_Task,ACTION_EXAMINE_OBJECT64,(SIPTR)root,0,0,0,0)||
       IoErr()!=ERROR_BAD_NUMBER){doserr=IoErr();error=EIO;goto done;}
    if(DoPkt(fl->fl_Task,ACTION_EXAMINE_NEXT64,0,(SIPTR)MKBADDR(&wide),0,0,0)||
       IoErr()!=ERROR_INVALID_LOCK){doserr=IoErr();error=EIO;goto done;}
    if(DoPkt(fl->fl_Task,ACTION_EXAMINE_FH64,0,(SIPTR)MKBADDR(&wide),0,0,0)||
       IoErr()!=ERROR_INVALID_LOCK){doserr=IoErr();error=EIO;goto done;}
    wide.fib_DiskKey=INT32_MAX;
    if(DoPkt(fl->fl_Task,ACTION_EXAMINE_NEXT64,(SIPTR)root,(SIPTR)MKBADDR(&wide),0,0,0)||
       IoErr()!=ERROR_BAD_NUMBER){doserr=IoErr();error=EIO;goto done;}
    stage="exall";
    ctl->eac_LastKey=0;ctl->eac_MatchString=NULL;ctl->eac_MatchFunc=NULL;
    for(unsigned calls=0;calls<64;calls++){
        BOOL more=ExAll(root,buffer,4096,ED_OWNER,ctl);LONG ioerr=IoErr();
        doserr=ioerr;
        struct ExAllData *entry=buffer;
        for(ULONG n=0;n<ctl->eac_Entries;n++){
            uintptr_t low=(uintptr_t)buffer,high=low+4096;
            if((uintptr_t)entry<low||(uintptr_t)entry>high-sizeof(*entry)||++all_count>64){error=EOVERFLOW;goto done;}
            uintptr_t name=(uintptr_t)entry->ed_Name;
            if(name<low||name>=high||strnlen(entry->ed_Name,high-name)==high-name){error=EIO;goto done;}
            if(!strcmp(entry->ed_Name,"hello.txt")&&entry->ed_Type<0&&
               entry->ed_Size==sizeof(storage_payload)-1)++all_found;
            if(!strcmp(entry->ed_Name,"large-sparse.bin")&&entry->ed_Type<0&&
               entry->ed_Size==(sizeof(entry->ed_Size)==4?UINT32_MAX:UINT64_C(0x100001000)))++all_large;
            entry=entry->ed_Next;
        }
        if(!more){if(ioerr!=ERROR_NO_MORE_ENTRIES||all_found!=1||all_large!=1)error=EIO;goto done;}
    }
    error=EOVERFLOW;
done:
    if(DOS64Base){CloseLibrary(DOS64Base);DOS64Base=NULL;}
    if(root&&ctl&&buffer&&ctl->eac_LastKey)ExAllEnd(root,buffer,4096,ED_OWNER,ctl);
    if(buffer)FreeVec(buffer);
    if(ctl)FreeDosObject(DOS_EXALLCONTROL,ctl);
    if(fib)FreeDosObject(DOS_FIB,fib);
    if(root)UnLock(root);
    char line[240];snprintf(line,sizeof(line),"Public desktop directory %s phase=%s stage=%s exnext=%u/%u exall=%u/%u errno=%d doserr=%ld\n",
        error?"FAIL":"PASS",phase,stage,next_found,next_count,all_found,all_count,error,(long)doserr);note(line);
    return error;
}
static int storage_request_flags(unsigned op,const char *target,const char *argument,const char *options,unsigned flags){
    storage_reply_received=0;
    static struct oz_message m,out;memset(&m,0,sizeof(m));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=op;m.flags=flags;
    snprintf(m.target,sizeof(m.target),"%s",target);
    if(argument)snprintf(m.argument,sizeof(m.argument),"%s",argument);
    if(options)snprintf(m.options,sizeof(m.options),"%s",options);
    if(op==5||op==7){
        m.topology.device_count=storage_members;
        if(op==5){m.topology.group_count=1;m.topology.ashift=12;m.topology.groups[0].layout=storage_layout;}
        if(op==5&&storage_layout==OZ_DRAID){
            m.topology.groups[0].draid_parity=storage_missing;
            m.topology.groups[0].draid_data=1;
            m.topology.groups[0].draid_spares=1;
        }
        for(unsigned i=0;i<storage_members;i++)storage_member(m.topology.devices[i].path,
            sizeof(m.topology.devices[i].path),i);
    }
    if(op==13){
        m.topology.group_count=1;m.topology.device_count=1;m.topology.ashift=12;
        m.topology.groups[0].layout=OZ_STRIPE;
        snprintf(m.topology.devices[0].path,sizeof(m.topology.devices[0].path),"%s/disk0-new",storage_directory);
        if(storage_dspare)snprintf(m.topology.devices[0].path,sizeof(m.topology.devices[0].path),"%s",storage_spare_name);
    }
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);
    if(!error){do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);}
    if(!error){
        storage_reply_received=1;
        char line[256];snprintf(line,sizeof(line),"Public storage op=%u status=%lu %.170s\n",op,(unsigned long)out.status,out.detail);note(line);
        if(out.status!=OZ_OK)error=EIO;
    }
    if(ticket.number){int release=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=release;}
    return error;
}
static int storage_request(unsigned op,const char *target,const char *argument,const char *options){
    return storage_request_flags(op,target,argument,options,OZ_CONFIRMED);
}
/* Send alias spelling unchanged to the service, unlike CLI realpath(). */
static int storage_alias_request(unsigned op,unsigned kind)
{
    const char *saved=storage_directory;
    int assigned=0,error=0;
    BPTR empty=0,target=0;
    if(kind==0)assigned=AssignLate((CONST_STRPTR)"OZPOOLPATH",(CONST_STRPTR)saved);
    else if(kind==1)assigned=AssignPath((CONST_STRPTR)"OZPOOLPATH",(CONST_STRPTR)saved);
    else {
        empty=CreateDir((CONST_STRPTR)"RAM:OZPoolEmpty");
        if(!empty)return EIO;
        UnLock(empty);
        empty=Lock((CONST_STRPTR)"RAM:OZPoolEmpty",SHARED_LOCK);
        if(!empty)return EIO;
        assigned=AssignLock((CONST_STRPTR)"OZPOOLPATH",empty);
        if(assigned)empty=0;
        if(assigned){
            target=Lock((CONST_STRPTR)saved,SHARED_LOCK);
            if(!target||!AssignAdd((CONST_STRPTR)"OZPOOLPATH",target))error=EIO;
            else target=0;
        }
    }
    if(!assigned)error=EIO;
    if(!error){
        storage_directory="OZPOOLPATH:";
        error=storage_request(op,"public_mirror",NULL,op==7?"readonly=on":NULL);
        storage_directory=saved;
    }
    if(target)UnLock(target);
    if(empty)UnLock(empty);
    if(assigned&&!AssignLock((CONST_STRPTR)"OZPOOLPATH",0)&&!error)error=EIO;
    if(kind==2&&!DeleteFile((CONST_STRPTR)"RAM:OZPoolEmpty")&&!error)error=EIO;
    if(!error){
        char line[96];snprintf(line,sizeof(line),"Public pool assign PASS op=%u kind=%u removed=1\n",op,kind);note(line);
    }
    return error;
}
static int pool_file_refused(const char *first,const char *second,int expected)
{
    static struct oz_message m,out;memset(&m,0,sizeof(m));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=5;m.flags=OZ_CONFIRMED;strcpy(m.target,"public_refused");
    m.topology.device_count=2;m.topology.group_count=1;m.topology.ashift=12;
    m.topology.groups[0].layout=OZ_MIRROR;
    snprintf(m.topology.devices[0].path,sizeof(m.topology.devices[0].path),"%s",first);
    snprintf(m.topology.devices[1].path,sizeof(m.topology.devices[1].path),"%s",second);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);
    if(!error){do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);}
    char wanted[96];snprintf(wanted,sizeof(wanted),"Authenticated pool creation returned errno=%d",expected);
    if(!error&&(out.status!=OZ_ENGINE_ERROR||strcmp(out.detail,wanted)))error=EIO;
    if(ticket.number){int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;}
    return error;
}
static int verify_mounts(const char *name,ULONG flags)
{
#ifdef OZ_TEST_UPSTREAM_MOUNT_CACHE
    int upstream=oz_upstream_mount_cache_test(name,flags&OZ_MOUNT_READONLY);
    if(upstream)return upstream;
#endif
    char options[]="rollback, rw,ro,size=123,noexec";
    struct mnttab options_test={.mnt_mntopts=options};
    char *readonly=hasmntopt(&options_test,"ro");
    if(readonly!=strstr(options,",ro,")+1||
       hasmntopt(&options_test,"exec")||hasmntopt(&options_test,"")||
       !hasmntopt(&options_test,"rw")||!hasmntopt(&options_test,"size")||
       !hasmntopt(&options_test,"size=123")||hasmntopt(&options_test,"size=12")||
       strcmp(options,"rollback, rw,ro,size=123,noexec"))return EIO;
    struct oz_mount_snapshot *allocated=NULL;
    int fetched=oz_mounts_read(OZ_ADMISSION_PORT,&allocated);
    if(fetched)return fetched;
    struct oz_mount_record *records=(void *)(allocated+1);
    int valid=allocated->count==(name?1:0);
    if(valid&&name)valid=!strcmp(records[0].dataset,"public_mirror/files")&&
        !strcmp(records[0].mountpoint,name)&&records[0].flags==flags;
    free(allocated);if(!valid)return EIO;
    if(oz_mounts_read("OpenZFS.Test.Nonexistent.Mounts",&allocated)!=ENOENT||allocated)return EIO;
    struct oz_mount_snapshot query={.version=OZ_MOUNTS_VERSION};
    int error=oz_mounts_query(OZ_ADMISSION_PORT,&query,sizeof(query));
    if(!name)return error?error:query.count?EIO:0;
    if(error!=ENOSPC||query.count!=1)return EIO;
    struct {struct oz_mount_snapshot header;struct oz_mount_record record;} result={0};
    result.header.version=OZ_MOUNTS_VERSION;result.header.capacity=1;
    error=oz_mounts_query(OZ_ADMISSION_PORT,&result,sizeof(result));
    if(error)return error;
    if(result.header.count!=1||strcmp(result.record.dataset,"public_mirror/files")||
       strcmp(result.record.mountpoint,name)||result.record.flags!=flags)return EIO;
    /* Response count is never accepted as input, and capacity must match the
     * exact packet length. Failed validation must not rewrite caller bytes. */
    if(oz_mounts_query(OZ_ADMISSION_PORT,&result,sizeof(result))!=EINVAL)return EIO;
    result.header.count=0;result.header.capacity=0;
    if(oz_mounts_query(OZ_ADMISSION_PORT,&result,sizeof(result))!=EINVAL)return EIO;
    if(strcmp(result.record.mountpoint,name))return EIO;
    return 0;
}
static int device_read_rpc_test(int denied)
{
    unsigned char bytes[512],saved[512];
    memset(bytes,0xa7,sizeof(bytes));memcpy(saved,bytes,sizeof(saved));
    int error=oz_device_read_bytes(OZ_ADMISSION_PORT,"ata.device",0,0,bytes,sizeof(bytes));
    if(denied) {
        if(error!=EACCES||memcmp(bytes,saved,sizeof(bytes)))return EIO;
        note("Public native device read PASS denied=1 unchanged=1\n");
        return 0;
    }
    if(error||bytes[510]!=0x55||bytes[511]!=0xaa||bytes[450]!=0xbf||bytes[482]!=0xbf)return EIO;
    /* Actual fixture LBA starts, not merely a plausible signature. */
    if(bytes[454]!=1||bytes[455]!=0x10||bytes[456]||bytes[457]||
       bytes[486]!=1||bytes[487]!=0x20||bytes[488]!=2||bytes[489])return EIO;
    for(unsigned i=0;i<8;i++) {
        memset(bytes,0xa7,sizeof(bytes));
        if(oz_device_read_bytes(OZ_ADMISSION_PORT,"ata.device",1,0,bytes,sizeof(bytes)))return EIO;
        for(unsigned j=0;j<sizeof(bytes);j++)if(bytes[j])return EIO;
    }
    if(oz_device_read_bytes(OZ_ADMISSION_PORT,"ata.device",0,268435456-512,bytes,sizeof(bytes)))return EIO;
    for(unsigned j=0;j<sizeof(bytes);j++)if(bytes[j])return EIO;
    memset(bytes,0xa7,sizeof(bytes));
#define READ_FAIL(port,dev,unit,off,count,wanted) do { \
    if(oz_device_read_bytes(port,dev,unit,off,bytes,count)!=(wanted)|| \
       memcmp(bytes,saved,sizeof(bytes)))return EIO; \
} while(0)
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,1,512,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,0,511,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,268435456,512,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,UINT64_MAX,512,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,0,0,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",0,0,OZ_DEVICE_READ_MAX+1,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"ata.device",-1,0,512,EINVAL);
    READ_FAIL(OZ_ADMISSION_PORT,"openzfs-absent.device",0,0,512,ENODEV);
    READ_FAIL("OpenZFS.Test.Absent","ata.device",0,0,512,ENOENT);
#undef READ_FAIL
    ULONG size=sizeof(struct oz_device_read_query)+512;
    struct oz_device_read_query *raw=calloc(1,size);
    if(!raw)return ENOMEM;
    raw->request.version=1;strcpy(raw->request.device,"ata.device");raw->count=512;
    memcpy(raw+1,saved,512);
    error=oz_device_read_request(OZ_ADMISSION_PORT,raw,size-1)==EINVAL?0:EIO;
    raw->reserved=1;
    if(oz_device_read_request(OZ_ADMISSION_PORT,raw,size)!=EINVAL)error=EIO;
    raw->reserved=0;raw->request.version=2;
    if(oz_device_read_request(OZ_ADMISSION_PORT,raw,size)!=EINVAL)error=EIO;
    raw->request.version=1;raw->count=UINT32_MAX;
    if(oz_device_read_request(OZ_ADMISSION_PORT,raw,size)!=EINVAL)error=EIO;
    if(memcmp(raw+1,saved,512))error=EIO;
    free(raw);
    if(error)return error;
    unsigned char *large=malloc(OZ_DEVICE_READ_MAX);
    if(!large)return ENOMEM;
    error=oz_device_read_bytes(OZ_ADMISSION_PORT,"ata.device",1,0,large,OZ_DEVICE_READ_MAX);
    if(!error)for(ULONG i=0;i<OZ_DEVICE_READ_MAX;i++)if(large[i]){error=EIO;break;}
    free(large);
    if(!error)note("Public native device read PASS denied=0 mbr=1 blank=8 last-sector=1 max=1 bounds=1 unchanged=1\n");
    return error;
}
static int inventory_rpc_test(int denied)
{
    struct oz_storage_inventory *value=malloc(sizeof(*value)),*saved=malloc(sizeof(*saved));
    struct oz_storage_inventory_query *raw=calloc(1,sizeof(*raw));
    int error=0;
    if(!value||!saved||!raw){error=ENOMEM;goto done;}
    memset(value,0xa5,sizeof(*value));*saved=*value;
    int result=oz_storage_inventory_read(OZ_ADMISSION_PORT,value);
    if(denied) {
        if(result!=EACCES||memcmp(value,saved,sizeof(*value)))error=EIO;
        goto done;
    }
    if(result){bug("Native inventory failed error=%d\n",result);error=result;goto done;}
    BPTR label=Lock((CONST_STRPTR)"SYS:openzfs-label-test-only",SHARED_LOCK);
    unsigned expected=label?4:3;if(label)UnLock(label);
    BPTR changed=Lock((CONST_STRPTR)"SYS:openzfs-native-changes-5",SHARED_LOCK);
    if(changed){expected=4;UnLock(changed);}
    BPTR ahci_marker=Lock((CONST_STRPTR)"SYS:openzfs-ahci-query-test",SHARED_LOCK);
    int ahci=ahci_marker!=0;if(ahci_marker)UnLock(ahci_marker);
    changed=Lock((CONST_STRPTR)"SYS:openzfs-native-changes-6",SHARED_LOCK);
    int changed_ahci=changed!=0;
    if(changed){ahci=1;UnLock(changed);}
    if(ahci) {
        bug("Public AHCI inventory count=%u expected=%u\n",value->count,expected+1);
        for(uint32_t i=0;i<value->count;i++)bug("Public AHCI inventory device=%s unit=%d type=%u model=%s serial=%s\n",
            value->units[i].device,value->units[i].unit,value->units[i].type,
            value->units[i].model,value->units[i].serial);
    }
    for(unsigned cycle=0;cycle<8&&!error;cycle++) {
        if(value->version!=1||value->count!=expected+(unsigned)ahci){error=EIO;break;}
        unsigned seen=0,seen_ahci=0;
        for(uint32_t i=0;i<value->count;i++) {
            const struct oz_storage_unit *u=&value->units[i];
            if(ahci&&!strcmp(u->device,"ahci.device")) {
                /* Compare stock and corrected AHCI packages: stock reports a
                 * fixed disk, the inquiry-capable overlay reports its SSD type. */
                int type_ok=u->type==vHidd_StorageUnit_Type_SolidStateDisk||
                    (changed_ahci&&u->type==vHidd_StorageUnit_Type_FixedDisk);
                if(u->unit||!u->model[0]||!u->serial[0]||
                   !type_ok||seen_ahci++)error=EIO;
                continue;
            }
            if(strcmp(u->device,"ata.device")||u->unit>=expected||
               !u->model[0]||!u->serial[0]||u->type!=(u->unit==2?4:2)||
               (seen&(1U<<u->unit))){error=EIO;break;}
            seen|=1U<<u->unit;
        }
        if(seen!=(1U<<expected)-1)error=EIO;
        if(seen_ahci!=(unsigned)ahci)error=EIO;
        if(!error&&oz_storage_inventory_read(OZ_ADMISSION_PORT,value))error=EIO;
    }
    *saved=*value;
    if(oz_storage_inventory_read("OpenZFS.Test.Absent",value)!=ENOENT||
       memcmp(value,saved,sizeof(*value)))error=EIO;
    raw->inventory=*saved;
    if(oz_storage_inventory_request(OZ_ADMISSION_PORT,raw,sizeof(*raw)-1)!=EINVAL||
       oz_storage_inventory_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))!=EINVAL)error=EIO;
    raw->version=1;raw->reserved=1;
    if(oz_storage_inventory_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))!=EINVAL||
       memcmp(&raw->inventory,saved,sizeof(*saved)))error=EIO;
done:
    free(raw);free(saved);free(value);
    if(!error)note(denied?"Public native inventory PASS denied=1 unchanged=1\n":
        "Public native inventory PASS denied=0 cycles=8 units=1 optical=1 identity=1 malformed=1 absent=1\n");
    else bug("Native inventory test failed error=%d\n",error);
    return error;
}
static int inquiry_rpc_test(int denied)
{
    int claim=oz_device_dos_claim_read(OZ_ADMISSION_PORT,"ata.device",0);
    if(denied) {if(claim!=EACCES)return EIO;}
    else {
        if(claim&&claim!=EBUSY)return EIO;
        if(oz_device_dos_claim_read(OZ_ADMISSION_PORT,"openzfs-absent.device",0)!=ENODEV)return EIO;
        if(oz_device_dos_claim_read("OpenZFS.Test.Absent","ata.device",0)!=ENOENT)return EIO;
        struct oz_partition_request q={0};
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q)-1)!=EINVAL)return EIO;
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q))!=EINVAL)return EIO;
        q.version=1;strcpy(q.device,"ata.device");q.reserved=1;
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q))!=EINVAL)return EIO;
        q.reserved=0;q.reserved2=1;
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q))!=EINVAL)return EIO;
        q.reserved2=0;q.unit=-1;
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q))!=EINVAL)return EIO;
        q.unit=0;memset(q.device,'x',sizeof(q.device));
        struct oz_partition_request saved=q;
        if(oz_device_dos_claim_request(OZ_ADMISSION_PORT,&q,sizeof(q))!=EINVAL||memcmp(&q,&saved,sizeof(q)))return EIO;
    }
    note(denied?"Public native DOS claim RPC PASS denied=1\n":
        "Public native DOS claim RPC PASS denied=0 missing=1 malformed=1\n");
    struct oz_device_inquiry value,saved;
    memset(&value,0xa5,sizeof(value));saved=value;
    int error=oz_device_inquiry_read(OZ_ADMISSION_PORT,"ata.device",0,&value);
    if(denied) {
        if(error!=EACCES||memcmp(&value,&saved,sizeof(value)))return EIO;
    } else {
        if(error||value.version!=1||value.length<36||value.length>96)return EIO;
        saved=value;
        if(oz_device_inquiry_read(OZ_ADMISSION_PORT,"openzfs-absent.device",0,&value)!=ENODEV||
           memcmp(&value,&saved,sizeof(value)))return EIO;
        if(oz_device_inquiry_read("OpenZFS.Test.Absent","ata.device",0,&value)!=ENOENT||
           memcmp(&value,&saved,sizeof(value)))return EIO;
        struct oz_device_inquiry_query raw={0};raw.inquiry=saved;
        if(oz_device_inquiry_request(OZ_ADMISSION_PORT,&raw,sizeof(raw)-1)!=EINVAL)return EIO;
        if(oz_device_inquiry_request(OZ_ADMISSION_PORT,&raw,sizeof(raw))!=EINVAL)return EIO;
        raw.request.version=1;strcpy(raw.request.device,"ata.device");raw.request.reserved=1;
        if(oz_device_inquiry_request(OZ_ADMISSION_PORT,&raw,sizeof(raw))!=EINVAL)return EIO;
        raw.request.reserved=0;raw.request.unit=-1;
        if(oz_device_inquiry_request(OZ_ADMISSION_PORT,&raw,sizeof(raw))!=EINVAL)return EIO;
        raw.request.unit=0;memset(raw.request.device,'x',sizeof(raw.request.device));
        if(oz_device_inquiry_request(OZ_ADMISSION_PORT,&raw,sizeof(raw))!=EINVAL||
           memcmp(&raw.inquiry,&saved,sizeof(saved)))return EIO;
    }
    note(denied?"Public native inquiry PASS denied=1 unchanged=1\n":
        "Public native inquiry PASS denied=0 absent=1 malformed=1 unchanged=1\n");
    BPTR ahci_marker=Lock((CONST_STRPTR)"SYS:openzfs-ahci-query-test",SHARED_LOCK);
    if(ahci_marker) {
        UnLock(ahci_marker);memset(&value,0xa5,sizeof(value));saved=value;
        error=oz_device_inquiry_read(OZ_ADMISSION_PORT,"ahci.device",0,&value);
        if(!denied)bug("Public AHCI inquiry diagnostic errno=%d version=%u length=%u vendor=%.8s product=%.16s\n",
            error,value.version,value.length,error?"":(char *)value.data+8,error?"":(char *)value.data+16);
        if(denied) {
            if(error!=EACCES||memcmp(&value,&saved,sizeof(value)))return EIO;
        } else {
            if(error||value.version!=1||value.length!=36||
               memcmp(value.data+8,"SATA    ",8)||memcmp(value.data+16,"QEMU HARDDISK",13))return EIO;
            struct oz_device_geometry geometry={0};
            int geometry_error=oz_device_geometry_read(OZ_ADMISSION_PORT,"ahci.device",0,&geometry);
            bug("Public AHCI geometry diagnostic errno=%d version=%u sector=%u bytes=%llu\n",
                geometry_error,geometry.version,geometry.sector_size,(unsigned long long)geometry.bytes);
            if(geometry_error||
               geometry.version!=1||geometry.sector_size!=512||geometry.bytes!=67108864)return EIO;
        }
        note(denied?"Public AHCI inquiry PASS denied=1 unchanged=1\n":
            "Public AHCI inquiry PASS denied=0 identity=1 geometry=1\n");
    }
    return 0;
}
static int partition_rpc_test(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-partition-test-only",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    struct oz_partition_catalog *saved=malloc(sizeof(*saved));
    struct oz_partition_query *raw=calloc(1,sizeof(*raw));
    struct Library *secBase=NULL;int switched=0,error=0;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
#define PART_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    PART_CHECK(catalog&&saved&&raw);
    PART_CHECK(!device_read_rpc_test(0));
    PART_CHECK(!inquiry_rpc_test(0));
    PART_CHECK(!inventory_rpc_test(0));
    struct oz_device_geometry geometry={0},geometry_saved;
    struct oz_device_geometry_query geometry_raw={0};
    for(unsigned i=0;i<16;i++)for(int unit=0;unit<2;unit++) {
        PART_CHECK(!oz_device_geometry_read(OZ_ADMISSION_PORT,"ata.device",unit,&geometry));
        PART_CHECK(geometry.version==1&&geometry.sector_size==512&&geometry.bytes==268435456);
    }
    geometry_saved=geometry;
#ifdef OZ_TEST_UPSTREAM_INIT
    PART_CHECK(!oz_upstream_device_test(0));
    PART_CHECK(!oz_upstream_device_wait_test(0));
    PART_CHECK(!oz_upstream_native_label_test(0));
#endif
    PART_CHECK(oz_device_geometry_read(OZ_ADMISSION_PORT,"openzfs-absent.device",0,&geometry)==ENODEV);
    PART_CHECK(!memcmp(&geometry,&geometry_saved,sizeof(geometry)));
    PART_CHECK(oz_device_geometry_read("OpenZFS.Test.Absent","ata.device",0,&geometry)==ENOENT);
    PART_CHECK(!memcmp(&geometry,&geometry_saved,sizeof(geometry)));
    geometry_raw.request.version=1;strcpy(geometry_raw.request.device,"ata.device");
    geometry_raw.geometry=geometry_saved;
    PART_CHECK(oz_device_geometry_request(OZ_ADMISSION_PORT,&geometry_raw,sizeof(geometry_raw)-1)==EINVAL);
    geometry_raw.request.version=2;
    PART_CHECK(oz_device_geometry_request(OZ_ADMISSION_PORT,&geometry_raw,sizeof(geometry_raw))==EINVAL);
    geometry_raw.request.version=1;geometry_raw.request.reserved2=1;
    PART_CHECK(oz_device_geometry_request(OZ_ADMISSION_PORT,&geometry_raw,sizeof(geometry_raw))==EINVAL);
    geometry_raw.request.reserved2=0;geometry_raw.request.unit=-1;
    PART_CHECK(oz_device_geometry_request(OZ_ADMISSION_PORT,&geometry_raw,sizeof(geometry_raw))==EINVAL);
    geometry_raw.request.unit=0;memset(geometry_raw.request.device,'x',sizeof(geometry_raw.request.device));
    PART_CHECK(oz_device_geometry_request(OZ_ADMISSION_PORT,&geometry_raw,sizeof(geometry_raw))==EINVAL);
    PART_CHECK(!memcmp(&geometry_raw.geometry,&geometry_saved,sizeof(geometry_saved)));
    for(unsigned i=0;i<16;i++) {
        PART_CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
        PART_CHECK(catalog->version==1&&catalog->table_type==2&&catalog->count==2&&
            catalog->sector_size==512&&catalog->disk_sectors==524288);
        PART_CHECK(catalog->entries[0].position==0&&catalog->entries[1].position==2&&
            catalog->entries[0].first_sector==4097&&catalog->entries[0].last_sector==135168&&
            catalog->entries[1].first_sector==139265&&catalog->entries[1].last_sector==270336);
        PART_CHECK(catalog->entries[0].type_length==1&&catalog->entries[0].type[0]==0xbf&&
            catalog->entries[1].type_length==1&&catalog->entries[1].type[0]==0xbf);
    }
    *saved=*catalog;
    PART_CHECK(oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",1,catalog)==EIO);
    PART_CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    PART_CHECK(oz_partitions_read("OpenZFS.Test.Absent","ata.device",0,catalog)==ENOENT);
    PART_CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    PART_CHECK(oz_partitions_read(OZ_ADMISSION_PORT,"",0,catalog)==EINVAL);
    PART_CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    raw->request.version=OZ_PARTITION_QUERY_VERSION;strcpy(raw->request.device,"ata.device");
    raw->catalog=*saved;
    PART_CHECK(oz_partitions_request(OZ_ADMISSION_PORT,raw,sizeof(*raw)-1)==EINVAL);
    raw->request.version++;
    PART_CHECK(oz_partitions_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))==EINVAL);
    raw->request.version=OZ_PARTITION_QUERY_VERSION;raw->request.reserved=1;
    PART_CHECK(oz_partitions_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))==EINVAL);
    raw->request.reserved=0;raw->request.unit=-1;
    PART_CHECK(oz_partitions_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))==EINVAL);
    raw->request.unit=0;memset(raw->request.device,'x',sizeof(raw->request.device));
    PART_CHECK(oz_partitions_request(OZ_ADMISSION_PORT,raw,sizeof(*raw))==EINVAL);
    PART_CHECK(!memcmp(&raw->catalog,saved,sizeof(*saved)));
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);PART_CHECK(secBase);
    secLogoutA(quiet);switched=1;
    PART_CHECK(secLoginA(reader)==((77UL<<16)|88));
    PART_CHECK(!device_read_rpc_test(1));
    PART_CHECK(!inquiry_rpc_test(1));
    PART_CHECK(!inventory_rpc_test(1));
#ifdef OZ_TEST_UPSTREAM_INIT
    PART_CHECK(!oz_upstream_device_test(1));
    PART_CHECK(!oz_upstream_device_wait_test(1));
    PART_CHECK(!oz_upstream_native_label_test(1));
#endif
    PART_CHECK(oz_device_geometry_read(OZ_ADMISSION_PORT,"ata.device",0,&geometry)==EACCES);
    PART_CHECK(!memcmp(&geometry,&geometry_saved,sizeof(geometry)));
    PART_CHECK(oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog)==EACCES);
    PART_CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    /* The same caller task changes identity: no cached root authorization. */
    secLogoutA(quiet);PART_CHECK(secLoginA(root)==secOWNER_SYSTEM);switched=0;
    PART_CHECK(!device_read_rpc_test(0));
#ifdef OZ_TEST_UPSTREAM_INIT
    PART_CHECK(!oz_upstream_native_label_test(0));
    PART_CHECK(!oz_upstream_device_test(0));
#endif
    PART_CHECK(!oz_device_geometry_read(OZ_ADMISSION_PORT,"ata.device",1,&geometry));
    PART_CHECK(!memcmp(&geometry,&geometry_saved,sizeof(geometry)));
    PART_CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
    PART_CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    note("Public native partition RPC PASS cycles=16 exact-extents=1 slots=1 bounds=1 malformed=1 denied=1 restored=1 unchanged-output=1\n");
    note("Public native geometry RPC PASS cycles=16 devices=2 blank=1 absent=1 malformed=1 denied=1 restored=1 unchanged-output=1\n");
done:
    if(switched){secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;}
    if(secBase)CloseLibrary(secBase);
    free(raw);free(saved);free(catalog);
    if(error){char line[96];snprintf(line,sizeof(line),"Public partition RPC failure=%d\n",error);note(line);}
    return error;
#undef PART_CHECK
}
/* Opt-in integration probe: load, but never execute, a known ELF through DOS. */
static int storage_loader_test(int initially_enabled)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-loader-policy-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    const char *path="OZPUBLIC:loader-probe";
    BPTR input=Open((CONST_STRPTR)"C:OpenZFSPublicServiceTest",MODE_OLDFILE);
    BPTR output=0;int error=input?0:EIO;LONG length;char bytes[8192];
    if(!error){output=Open((CONST_STRPTR)path,MODE_NEWFILE);if(!output)error=EIO;}
    while(!error&&(length=Read(input,bytes,sizeof(bytes)))>0)
        if(Write(output,bytes,length)!=length)error=EIO;
    if(!error&&length<0)error=EIO;
    if(output&&!Close(output))error=EIO;
    if(input&&!Close(input))error=EIO;
    if(!error&&!SetProtection((CONST_STRPTR)path,0))error=EIO;
    for(unsigned phase=0;phase<3&&!error;phase++) {
        unsigned enabled=phase!=1;
        error=storage_request(41,"public_mirror/files",enabled?"exec=on":"exec=off",NULL);
        if(error)break;
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
        BPTR readable=Open((CONST_STRPTR)path,MODE_OLDFILE);
        if(!fib||!readable)error=EIO;
        ULONG protection=0;
        if(!error) {
            if(!ExamineFH(readable,fib))error=EIO;
            else protection=fib->fib_Protection;
            if(!!(protection&FIBF_EXECUTE)==!!enabled)error=EIO;
            if(Read(readable,bytes,4)!=4||memcmp(bytes,"\177ELF",4))error=EIO;
        }
        if(readable&&!Close(readable))error=EIO;
        if(fib)FreeDosObject(DOS_FIB,fib);
        if(error)break;
        for(unsigned api=0;api<2&&!error;api++) {
            BPTR segment=api?NewLoadSeg((CONST_STRPTR)path,NULL):LoadSeg((CONST_STRPTR)path);
            LONG io_error=IoErr();
            char line[192];snprintf(line,sizeof(line),
                "Native loader policy phase=%u api=%s exec=%s protection=%lu readable=1 loaded=%d ioerr=%ld\n",
                phase,api?"NewLoadSeg":"LoadSeg",enabled?"on":"off",
                (unsigned long)protection,!!segment,(long)io_error);note(line);
            if(segment)UnLoadSeg(segment);
            if(enabled&&!segment)error=EIO;
            if(!enabled&&(segment||io_error!=ERROR_READ_PROTECTED))error=EACCES;
        }
    }
    /* Restore the private fixture even when the actual loader bypasses policy. */
    int restore=storage_request(41,"public_mirror/files",initially_enabled?"exec=on":"exec=off",NULL);
    if(!error)error=restore;
    if(!DeleteFile((CONST_STRPTR)path)&&!error)error=EIO;
    note(error?"RESULT native loader execution policy FAIL\n":
        "RESULT native loader execution policy PASS\n");
    return error;
}
static LONG cli_command(const char *command)
{
    BPTR errors=Open((CONST_STRPTR)"T:public-cli-errors.log",MODE_NEWFILE);
    LONG result=SystemTags((CONST_STRPTR)command,SYS_Error,errors?errors:Output(),TAG_DONE);
    LONG io_error=IoErr();
    if(errors){Close(errors);if(result)diagnostic_log("T:public-cli-errors.log");}
    char message[512];
    snprintf(message,sizeof(message),"Public CLI command result=%ld ioerr=%ld: %s\n",
        (long)result,(long)io_error,command);
    note(message);
    return result;
}
static int cli_result(const char *command,const char *expected)
{
    if(cli_command(command)!=0)return EIO;
    BPTR file=Open((CONST_STRPTR)"T:cli-package-output.txt",MODE_OLDFILE);
    if(!file)return EIO;
    char text[256]={0};LONG count=Read(file,text,sizeof(text)-1);
    int closed=Close(file);
    if(count<0||!closed)return EIO;
    while(count>0&&(text[count-1]=='\n'||text[count-1]=='\r'))text[--count]=0;
    return strcmp(text,expected)?EIO:0;
}
static int manager_upgrade_request(ULONG flags,const char *options,ULONG expected)
{
    struct oz_message request={0};struct oz_project_worker *worker=NULL;
    request.magic=OZ_MAGIC;request.version=OZ_VERSION;request.msg.mn_Length=sizeof(request);
    request.operation=47;request.flags=flags;
    strcpy(request.target,"public_mirror/upgradecheck");
    snprintf(request.options,sizeof(request.options),"%s",options);
    int error=oz_project_worker_start(&request,&worker);
    if(error)return error;
    do{error=oz_project_worker_poll(worker,&request);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    int released=oz_project_worker_free(worker);
    if(!error)error=released;
    if(!error&&request.status!=expected)error=EIO;
    note(request.detail);note("\n");
    return error;
}
static int cli_filesystem_upgrade(int manager)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    int error=0,created=0,mounted=0;BPTR file=0;
    const char payload[]="Native CLI filesystem upgrade preserves data";
    char actual[sizeof(payload)];
#define UPGRADE_CHECK(x) do{if(!(x)){error=EIO;goto done;}}while(0)
    UPGRADE_CHECK(!cli_command("C:zfs create -u -o version=3 -o mountpoint=OZVERSION: public_mirror/upgradecheck"));created=1;
    UPGRADE_CHECK(!cli_result("C:zfs get -H -o value version public_mirror/upgradecheck >T:cli-package-output.txt","3"));
    UPGRADE_CHECK(!cli_command("C:zfs mount public_mirror/upgradecheck"));mounted=1;
    file=Open((CONST_STRPTR)"OZVERSION:payload",MODE_NEWFILE);UPGRADE_CHECK(file);
    UPGRADE_CHECK(Write(file,payload,sizeof(payload))==sizeof(payload));
    {int closed=Close(file);file=0;UPGRADE_CHECK(closed);}
    if(manager){
        UPGRADE_CHECK(!manager_upgrade_request(0,"version=5",OZ_ENGINE_ERROR));
        UPGRADE_CHECK(!manager_upgrade_request(OZ_CONFIRMED,"version=6",OZ_ENGINE_ERROR));
        UPGRADE_CHECK(!cli_result("C:zfs get -H -o value version public_mirror/upgradecheck >T:cli-package-output.txt","3"));
        UPGRADE_CHECK(!manager_upgrade_request(OZ_CONFIRMED,"version=5",OZ_OK));
    } else UPGRADE_CHECK(!cli_command("C:zfs upgrade -V 5 public_mirror/upgradecheck"));
    UPGRADE_CHECK(!cli_result("C:zfs get -H -o value version public_mirror/upgradecheck >T:cli-package-output.txt","5"));
    UPGRADE_CHECK(!cli_command("C:zfs unmount public_mirror/upgradecheck"));mounted=0;
    UPGRADE_CHECK(!cli_command("C:zfs mount public_mirror/upgradecheck"));mounted=1;
    file=Open((CONST_STRPTR)"OZVERSION:payload",MODE_OLDFILE);UPGRADE_CHECK(file);
    UPGRADE_CHECK(Read(file,actual,sizeof(actual))==sizeof(actual));
    UPGRADE_CHECK(!memcmp(payload,actual,sizeof(payload)));
done:
    if(file&&!Close(file))error=EIO;
    if(mounted&&cli_command("C:zfs unmount public_mirror/upgradecheck"))error=EIO;
    if(created&&cli_command("C:zfs destroy public_mirror/upgradecheck"))error=EIO;
    note(error?"Public CLI filesystem upgrade FAIL\n":manager?
        "Public Manager upgrade worker PASS unconfirmed=1 invalid=1 unchanged=1 version5=1 remount=1 exact-data=1 cleanup=1\n":
        "Public CLI filesystem upgrade PASS version3=1 version5=1 remount=1 exact-data=1 cleanup=1\n");
    return error;
#undef UPGRADE_CHECK
}
static int cli_trim_unsupported(void)
{
    char *commands[][5]={{"C:zpool","trim","public_mirror",NULL,NULL},
        {"C:zpool","trim","-s","public_mirror",NULL},
        {"C:zpool","trim","-c","public_mirror",NULL}};
    for(unsigned i=0;i<sizeof(commands)/sizeof(*commands);i++) {
        BPTR errors=Open((CONST_STRPTR)"T:public-cli-errors.log",MODE_NEWFILE);
        if(!errors)return EIO;
        struct oz_cli_result result={0};
        int error=oz_cli_spawn_exact(commands[i][0],commands[i],Output(),errors,&result);
        if(!Close(errors))error=EIO;
        diagnostic_log("T:public-cli-errors.log");
        if(error||result.dos_error||!result.status)return EIO;
        BPTR f=Open((CONST_STRPTR)"T:public-cli-errors.log",MODE_OLDFILE);
        if(!f)return EIO;
        char text[2048]={0};LONG n=Read(f,text,sizeof(text)-1);
        int closed=Close(f);
        const char *first=strstr(text,"trim operations are not supported by this device");
        if(n<=0||n==sizeof(text)-1||!closed||!first||
            !strstr(first+1,"trim operations are not supported by this device"))return EIO;
    }
    note("Public CLI TRIM unsupported PASS start=1 suspend=1 cancel=1 upstream-device-errors=1\n");
    return 0;
}
static int companion_tools_test(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-companion-tools-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    LONG runtime=cli_command("C:OpenZFSRuntimeCheck >T:companion-runtime.log");
    diagnostic_log("T:companion-runtime.log");
    if(runtime)return EIO;
    if(cli_command("C:OpenZFSStartup INVALID")!=1||
       cli_command("C:OpenZFSStartup 0000000000000000 CHECK")!=1||
       cli_command("C:OpenZFSStartupMount public_mirror INVALID")!=1||
       cli_command("C:OpenZFSHostIdentity 00000000 SAVE")!=20||
       cli_command("C:OpenZFSHostIdentity 12345678")!=20)return EIO;
    note("Public companion tools PASS runtime-prerequisites=1 invalid-arguments=1 no-identity-write=1\n");
    return 0;
}
/* Exercise real command publication in fresh guest-RAM assigns only. Never
 * overwrite the boot image's ENV/ENVARC or carry a test host ID into service. */
static int companion_configuration_test(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-companion-tools-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    const char *assigns[]={"ENV","ENVARC"};
    const char *roots[]={"RAM:OZCompanionEnv","RAM:OZCompanionArchive"};
    BPTR saved[2]={0,0};int assigned[2]={0,0},created[2]={0,0},error=0;
    struct oz_startup_settings record={0},loaded={0};unsigned published=0;
#define CONFIG_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    for(unsigned i=0;i<2;i++){
        char path[32];snprintf(path,sizeof(path),"%s:",assigns[i]);
        saved[i]=Lock((CONST_STRPTR)path,SHARED_LOCK);CONFIG_CHECK(saved[i]);
        BPTR directory=CreateDir((CONST_STRPTR)roots[i]);CONFIG_CHECK(directory);created[i]=1;
        if(!SetOwner((STRPTR)roots[i],secOWNER_SYSTEM)||!SetProtection((CONST_STRPTR)roots[i],0)){
            UnLock(directory);CONFIG_CHECK(0);
        }
        UnLock(directory);
        directory=Lock((CONST_STRPTR)roots[i],SHARED_LOCK);CONFIG_CHECK(directory);
        if(!AssignLock((CONST_STRPTR)assigns[i],directory)){UnLock(directory);CONFIG_CHECK(0);}
        assigned[i]=1;
    }
    {
        BPTR probe=CreateDir((CONST_STRPTR)"ENV:configuration-probe");
        CONFIG_CHECK(probe);
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
        if(fib&&Examine(probe,fib)){
            char line[160];snprintf(line,sizeof(line),"Public configuration created-directory owner=%u:%u protection=%lx type=%ld\n",
                fib->fib_OwnerUID,fib->fib_OwnerGID,(unsigned long)fib->fib_Protection,(long)fib->fib_DirEntryType);note(line);
        }
        if(fib)FreeDosObject(DOS_FIB,fib);
        UnLock(probe);CONFIG_CHECK(DeleteFile((CONST_STRPTR)"ENV:configuration-probe"));
    }
    LONG identity_result=cli_command("C:OpenZFSHostIdentity 5a465301 SAVE >T:companion-identity.log");
    diagnostic_log("T:companion-identity.log");
    CONFIG_CHECK(!identity_result);
    for(unsigned i=0;i<2;i++){
        char path[96];snprintf(path,sizeof(path),"%s:OpenZFSHost.prefs",assigns[i]);
        BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);CONFIG_CHECK(file);
        uint32_t identity=0;int result=oz_hostid_record_read(file,&identity),closed=Close(file);
        CONFIG_CHECK(!result&&closed&&identity==UINT32_C(0x5a465301));
    }
    record.guid=UINT64_C(0x5a46530000000001);record.count=1;
    strcpy(record.pool,"companion_disabled");strcpy(record.paths[0],"RAM:must-not-be-opened");
    CONFIG_CHECK(!oz_startup_settings_apply_trusted(&record,1,&published));
    CONFIG_CHECK(published==(OZ_STARTUP_PUBLISHED_ENV|OZ_STARTUP_PUBLISHED_ENVARC));
    CONFIG_CHECK(!cli_command("C:OpenZFSStartup 5a46530000000001 CHECK"));
    CONFIG_CHECK(!cli_command("C:OpenZFSStartup 5a46530000000001"));
    CONFIG_CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-5a46530000000001.prefs"));
    CONFIG_CHECK(cli_command("C:OpenZFSStartup 5a46530000000001 CHECK")==1);
    CONFIG_CHECK(!cli_command("C:OpenZFSStartup 5a46530000000001 PREPARE"));
    CONFIG_CHECK(!oz_startup_settings_trusted(record.guid,&loaded)&&loaded.guid==record.guid&&
        !loaded.flags&&loaded.count==1&&!strcmp(loaded.pool,record.pool)&&!strcmp(loaded.paths[0],record.paths[0]));
    CONFIG_CHECK(!cli_command("C:OpenZFSStartup 5a46530000000001 RESTORE"));
    CONFIG_CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
cleanup:
    for(unsigned i=0;i<2;i++){
        if(assigned[i]){
            if(AssignLock((CONST_STRPTR)assigns[i],saved[i]))saved[i]=0;
            else if(!error)error=__LINE__;
        }
        if(saved[i])UnLock(saved[i]);
        if(created[i]){
            const char *names[]={"OpenZFSHost.prefs","OpenZFS-5a46530000000001.prefs"};
            for(unsigned n=0;n<2;n++){
                char path[128];snprintf(path,sizeof(path),"%s/%s",roots[i],names[n]);
                if(!DeleteFile((CONST_STRPTR)path)&&IoErr()!=ERROR_OBJECT_NOT_FOUND&&!error)error=__LINE__;
            }
            if(!DeleteFile((CONST_STRPTR)roots[i])&&!error)error=__LINE__;
        }
    }
    if(error){char line[96];snprintf(line,sizeof(line),"Public companion configuration FAIL line=%d DOS=%ld\n",error,(long)IoErr());note(line);}
    else note("Public companion configuration PASS hostid-save=1 iff-readback=2 trusted-check=1 prepare=1 disabled-restore=1 assigns-restored=1\n");
#undef CONFIG_CHECK
    return error;
}
static int companion_persistent_settings(int cold)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-startup-archive-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    int error=0,mounted=0,created=0;BPTR saved[2]={0,0};int assigned[2]={0,0};
    const char *assigns[]={"ENV","ENVARC"};
    const char *roots[]={"RAM:OZPersistentEnv","OZARCH:Prefs"};
    struct oz_startup_settings record={0},loaded={0};unsigned published=0;
#define PS_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    marker=Lock((CONST_STRPTR)"RAM:OZArchiveReady",SHARED_LOCK);
    if(marker)UnLock(marker);
    else{
        PS_CHECK(!cli_command("C:Mount SYS:OZARCH"));mounted=1;
        if(!cold){
            PS_CHECK(Inhibit((CONST_STRPTR)"OZARCH:",DOSTRUE));
            PS_CHECK(Format((CONST_STRPTR)"OZARCH:",(CONST_STRPTR)"OZSettings",ID_DOS_muFS_DISK));
        }
        BPTR ready=Open((CONST_STRPTR)"RAM:OZArchiveReady",MODE_NEWFILE);PS_CHECK(ready);PS_CHECK(Close(ready));
    }
    mounted=1;PS_CHECK(Inhibit((CONST_STRPTR)"OZARCH:",DOSFALSE));
    for(unsigned i=0;i<2;i++){
        char path[32];snprintf(path,sizeof(path),"%s:",assigns[i]);
        saved[i]=Lock((CONST_STRPTR)path,SHARED_LOCK);PS_CHECK(saved[i]);
        BPTR directory=Lock((CONST_STRPTR)roots[i],SHARED_LOCK);
        if(!directory){
            PS_CHECK(i==0||!cold);
            directory=CreateDir((CONST_STRPTR)roots[i]);PS_CHECK(directory);
            if(i==0)created=1;
            if(!SetOwner((STRPTR)roots[i],secOWNER_SYSTEM)||!SetProtection((CONST_STRPTR)roots[i],0)){
                UnLock(directory);PS_CHECK(0);
            }
            UnLock(directory);directory=Lock((CONST_STRPTR)roots[i],SHARED_LOCK);PS_CHECK(directory);
        }else if(i==0){UnLock(directory);PS_CHECK(0);} /* Never reuse a pre-existing RAM fixture. */
        if(!AssignLock((CONST_STRPTR)assigns[i],directory)){UnLock(directory);PS_CHECK(0);}
        assigned[i]=1;
    }
    record.guid=UINT64_C(0x5a46530000000002);record.count=1;
    strcpy(record.pool,"persistent_disabled");strcpy(record.paths[0],"RAM:must-not-be-opened");
    if(!cold){
        LONG status=cli_command("C:OpenZFSHostIdentity 5a465302 SAVE >T:persistent-identity.log");
        diagnostic_log("T:persistent-identity.log");PS_CHECK(!status);
        int applied=oz_startup_settings_apply_trusted(&record,1,&published);
        char detail[128];snprintf(detail,sizeof(detail),"Public persistent publish errno=%d published=%u DOS=%ld\n",applied,published,(long)IoErr());note(detail);
        PS_CHECK(!applied);
        PS_CHECK(published==(OZ_STARTUP_PUBLISHED_ENV|OZ_STARTUP_PUBLISHED_ENVARC));
        PS_CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-5a46530000000002.prefs"));
    }
    BPTR file=Open((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs",MODE_OLDFILE);PS_CHECK(file);
    uint32_t identity=0;int result=oz_hostid_record_read(file,&identity),closed=Close(file);
    PS_CHECK(!result&&closed&&identity==UINT32_C(0x5a465302));
    PS_CHECK(cli_command("C:OpenZFSStartup 5a46530000000002 CHECK")==1);
    LONG status=cli_command("C:OpenZFSStartup 5a46530000000002 PREPARE >T:persistent-prepare.log");
    diagnostic_log("T:persistent-prepare.log");PS_CHECK(!status);
    PS_CHECK(!oz_startup_settings_trusted(record.guid,&loaded)&&loaded.guid==record.guid&&
        !loaded.flags&&loaded.count==1&&!strcmp(loaded.pool,record.pool)&&!strcmp(loaded.paths[0],record.paths[0]));
    PS_CHECK(!cli_command("C:OpenZFSStartup 5a46530000000002 RESTORE"));
    PS_CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
cleanup:
    for(unsigned i=0;i<2;i++){
        if(assigned[i]){
            if(AssignLock((CONST_STRPTR)assigns[i],saved[i]))saved[i]=0;
            else if(!error)error=__LINE__;
        }
        if(saved[i])UnLock(saved[i]);
    }
    if(created){
        const char *names[]={"OpenZFSHost.prefs","OpenZFS-5a46530000000002.prefs"};
        for(unsigned i=0;i<2;i++){
            char path[128];snprintf(path,sizeof(path),"%s/%s",roots[0],names[i]);
            if(!DeleteFile((CONST_STRPTR)path)&&IoErr()!=ERROR_OBJECT_NOT_FOUND&&!error)error=__LINE__;
        }
        if(!DeleteFile((CONST_STRPTR)roots[0])&&!error)error=__LINE__;
    }
    if(mounted&&!Inhibit((CONST_STRPTR)"OZARCH:",DOSTRUE)&&!error)error=__LINE__;
    char line[160];snprintf(line,sizeof(line),error?"Public persistent settings FAIL write=%d line=%d DOS=%ld\n":
        "Public persistent settings PASS write=%d archive-mufs=1 hostid-iff=1 trusted-prepare=1 disabled-restore=1\n",!cold,error,(long)IoErr());note(line);
#undef PS_CHECK
    return error;
}
static int companion_mount_test(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-companion-tools-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    char *args[]={"zpool","list","-H","-o","name","public_mirror",NULL};
    char **lines=NULL;int count=0;
    int child=oz_cli_run_process("C:zpool",args,NULL,4,&lines,&count);
    int helper_ok=!child&&count==1&&!strcmp(lines[0],"public_mirror");
    oz_cli_free_lines(lines,count);lines=NULL;count=0;
    if(!helper_ok)return EIO;
    child=oz_cli_run_process("C:OpenZFSAbsentChild",args,NULL,4,&lines,&count);
    helper_ok=child==255&&count==0;
    oz_cli_free_lines(lines,count);
    if(!helper_ok)return EIO;
    note("Public exec helper PASS real-zpool=1 captured-output=1 missing-executable=1\n");
    int error=0,changed=0;char command[256],text[64]={0};
    struct oz_mount_snapshot *snapshot=NULL;
#define CM_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    CM_CHECK(!cli_command("C:zpool get -H -p -o value guid public_mirror >T:companion-guid.txt"));
    BPTR file=Open((CONST_STRPTR)"T:companion-guid.txt",MODE_OLDFILE);CM_CHECK(file);
    LONG length=Read(file,text,sizeof(text)-1);int closed=Close(file);CM_CHECK(length>0&&closed);
    char *end=NULL;unsigned long long guid=strtoull(text,&end,10);
    CM_CHECK(guid&&end!=text&&(*end=='\n'||*end=='\r'||!*end));
    CM_CHECK(!cli_command("C:zfs set canmount=off public_mirror"));changed=1;
    CM_CHECK(!cli_command("C:zfs set mountpoint=OZSTARTUP: public_mirror/files"));
    /* Changing mountpoint may mount through libzfs's change list. Start the
     * explicit startup test unmounted, rather than crediting that side effect. */
    CM_CHECK(!cli_command("C:zfs unmount public_mirror/files"));
    snprintf(command,sizeof(command),"C:OpenZFSStartupMount public_mirror %016llx",guid^UINT64_C(1));
    CM_CHECK(cli_command(command)==1);CM_CHECK(!verify_mounts(NULL,0));
    snprintf(command,sizeof(command),"C:OpenZFSStartupMount public_mirror %016llx >T:companion-mount.log",guid);
    LONG status=cli_command(command);diagnostic_log("T:companion-mount.log");CM_CHECK(!status);
    CM_CHECK(!oz_mounts_read(OZ_ADMISSION_PORT,&snapshot));
    struct oz_mount_record *records=(void *)(snapshot+1);
    CM_CHECK(snapshot->count==1&&!strcmp(records[0].dataset,"public_mirror/files"));
    CM_CHECK(!verify_mounts(records[0].mountpoint,0));
    BPTR lock=Lock((CONST_STRPTR)records[0].mountpoint,SHARED_LOCK);CM_CHECK(lock);UnLock(lock);
    status=cli_command(command);diagnostic_log("T:companion-mount.log");CM_CHECK(!status);
    CM_CHECK(!verify_mounts(records[0].mountpoint,0));
cleanup:
    free(snapshot);
    int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
    if(detached&&detached!=ENOENT&&!error)error=__LINE__;
    if(changed&&cli_command("C:zfs set canmount=on public_mirror")&&!error)error=__LINE__;
    if(changed&&cli_command("C:zfs inherit mountpoint public_mirror/files")&&!error)error=__LINE__;
    if(!error)error=verify_mounts(NULL,0);
    if(error){char line[96];snprintf(line,sizeof(line),"Public companion mount FAIL line=%d\n",error);note(line);}
    else note("Public companion mount PASS guid-bound=1 wrong-guid-refused=1 parent-skipped=1 child-mounted=1 repeat-idempotent=1 detached=1\n");
#undef CM_CHECK
    return error;
}
/* FAT is only fixture data. When the independent muFS archive is enabled,
 * publish policy there and use the production trusted RESTORE path on boot. */
static int storage_no_pools(void);
static int companion_import_test(int write)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-startup-import-test",SHARED_LOCK);
    if(!marker)return write?0:ENOENT;
    UnLock(marker);
    struct oz_startup_settings record={0};char path[256],command[256],text[64]={0};
    snprintf(path,sizeof(path),"%s/startup.prefs",storage_directory);
    int error=0,archive=0,assigned=0;BPTR file=0,saved=0;unsigned published=0;
#define CI_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    CI_CHECK(native_file_fixture);
    marker=Lock((CONST_STRPTR)"SYS:openzfs-startup-archive-test",SHARED_LOCK);
    if(marker){
        UnLock(marker);archive=1;
        CI_CHECK(Inhibit((CONST_STRPTR)"OZARCH:",DOSFALSE));
        saved=Lock((CONST_STRPTR)"ENVARC:",SHARED_LOCK);CI_CHECK(saved);
        BPTR root=Lock((CONST_STRPTR)"OZARCH:Prefs",SHARED_LOCK);CI_CHECK(root);
        if(!AssignLock((CONST_STRPTR)"ENVARC",root)){UnLock(root);CI_CHECK(0);}
        assigned=1;
    }
    if(write){
        CI_CHECK(!cli_command("C:zpool get -H -p -o value guid public_mirror >T:companion-guid.txt"));
        file=Open((CONST_STRPTR)"T:companion-guid.txt",MODE_OLDFILE);CI_CHECK(file);
        LONG length=Read(file,text,sizeof(text)-1);int closed=Close(file);file=0;
        CI_CHECK(length>0&&closed);char *end=NULL;
        record.guid=strtoull(text,&end,10);CI_CHECK(record.guid&&end!=text&&(*end=='\n'||*end=='\r'||!*end));
        record.flags=OZ_STARTUP_ENABLED|OZ_STARTUP_READONLY;record.count=storage_members;
        if(archive)record.flags|=OZ_STARTUP_MOUNT;
        strcpy(record.pool,"public_mirror");
        for(unsigned i=0;i<record.count;i++)storage_member(record.paths[i],sizeof(record.paths[i]),i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);CI_CHECK(file);
        CI_CHECK(!oz_startup_settings_write(file,&record));
        closed=Close(file);file=0;CI_CHECK(closed);
        if(archive){
            CI_CHECK(!oz_startup_settings_apply_trusted(&record,1,&published));
            CI_CHECK(published==(OZ_STARTUP_PUBLISHED_ENV|OZ_STARTUP_PUBLISHED_ENVARC));
        }
        note("Public startup import record PASS persisted-fixture=1 readonly=1 enabled=1\n");
    }else{
        file=Open((CONST_STRPTR)path,MODE_OLDFILE);CI_CHECK(file);
        int result=oz_startup_settings_read(file,&record),closed=Close(file);file=0;
        CI_CHECK(!result&&closed&&record.count==storage_members&&!strcmp(record.pool,"public_mirror")&&
            record.flags==(OZ_STARTUP_ENABLED|OZ_STARTUP_READONLY|(archive?OZ_STARTUP_MOUNT:0)));
        for(unsigned i=0;i<record.count;i++){
            char expected[256];storage_member(expected,sizeof(expected),i);CI_CHECK(!strcmp(record.paths[i],expected));
        }
        /* The device paths alone must not authorize importing another pool. */
        uint64_t actual_guid=record.guid;record.guid^=UINT64_C(0x8000000000000000);
        if(!record.guid)record.guid=1;
        CI_CHECK(!oz_startup_settings_apply_trusted(&record,0,&published));
        snprintf(command,sizeof(command),"C:OpenZFSStartup %016llx >T:companion-import-wrong.log",(unsigned long long)record.guid);
        LONG refused=cli_command(command);diagnostic_log("T:companion-import-wrong.log");
        snprintf(path,sizeof(path),"ENV:OpenZFS-%016llx.prefs",(unsigned long long)record.guid);
        CI_CHECK(DeleteFile((CONST_STRPTR)path));published=0;record.guid=actual_guid;
        CI_CHECK(refused==1&&!storage_no_pools());
        if(!archive){
            CI_CHECK(!oz_startup_settings_apply_trusted(&record,0,&published));
            CI_CHECK(published==OZ_STARTUP_PUBLISHED_ENV);
        }else published=OZ_STARTUP_PUBLISHED_ENV;
        snprintf(command,sizeof(command),"C:OpenZFSStartup %016llx%s >T:companion-import.log",(unsigned long long)record.guid,archive?" RESTORE":"");
        LONG status=cli_command(command);diagnostic_log("T:companion-import.log");CI_CHECK(!status);
        if(archive){
            CI_CHECK(!verify_mounts("OZCOLD:",OZ_MOUNT_READONLY));
            BPTR root=Lock((CONST_STRPTR)"OZCOLD:",SHARED_LOCK);CI_CHECK(root);UnLock(root);
            note("Public persistent enabled restore PASS trusted-envarc=1 imported=1 mounted=1 readonly=1\n");
        }
        note("Public startup import command PASS fresh-guest=1 guid-bound=1 readonly=1 wrong-guid-refused=1\n");
    }
cleanup:
    if(file)Close(file);
    if(published&OZ_STARTUP_PUBLISHED_ENV){
        snprintf(path,sizeof(path),"ENV:OpenZFS-%016llx.prefs",(unsigned long long)record.guid);
        if(!DeleteFile((CONST_STRPTR)path)&&!error)error=__LINE__;
    }
    if(assigned){if(AssignLock((CONST_STRPTR)"ENVARC",saved))saved=0;else if(!error)error=__LINE__;}
    if(saved)UnLock(saved);
    if(archive&&!Inhibit((CONST_STRPTR)"OZARCH:",DOSTRUE)&&!error)error=__LINE__;
    if(error){char line[100];snprintf(line,sizeof(line),"Public startup import command FAIL write=%d line=%d DOS=%ld\n",write,error,(long)IoErr());note(line);}
#undef CI_CHECK
    return error;
}
/* Exercise repair through the shipped API/commands, not linked SPA internals.
 * disk0 is later the only available mirror member in the cold degraded test. */
static int storage_repair_data(const char *root,int write)
{
    char path[96];snprintf(path,sizeof(path),"%sresilver.bin",root);
    BPTR file=Open((CONST_STRPTR)path,write?MODE_NEWFILE:MODE_OLDFILE);
    if(!file)return EIO;
    unsigned char expected[4096],actual[4096];int error=0;
    unsigned state=0x12345678;
    for(unsigned block=0;block<256&&!error;block++){
        for(unsigned i=0;i<sizeof(expected);i++){
            state^=state<<13;state^=state>>17;state^=state<<5;
            expected[i]=(unsigned char)state;
        }
        if(write){if(Write(file,expected,sizeof(expected))!=sizeof(expected))error=EIO;}
        else if(Read(file,actual,sizeof(actual))!=sizeof(actual)||memcmp(actual,expected,sizeof(actual)))error=EIO;
    }
    if(write){if(!Flush(file))error=EIO;}
    else if(!error&&Read(file,actual,1)!=0)error=EIO;
    if(!Close(file))error=EIO;
    if(!error)note(write?"Public resilver payload written bytes=1048576\n":
        "Public cold resilver payload PASS bytes=1048576 exact=1\n");
    return error;
}
static int storage_repair_status(char *guid,size_t size,const char *state,int finished)
{
    struct oz_client client;static struct oz_message request;
    if(!oz_client_open(&client))return ENOMEM;
    memset(&request,0,sizeof(request));request.operation=4;
    strcpy(request.target,"public_mirror");
    int error=oz_submit(&client,&request)==OZ_OK?0:EIO;
    if(!error){
        struct oz_message *reply=NULL;
        while(!(reply=oz_poll(&client)))Delay(1);
        error=EIO;
        if(reply->status==OZ_OK&&!reply->next_cursor){
            const char *suffix=storage_replaced?"/disk0-new":"/disk0";
            unsigned leaves=0,old_present=0;
            for(unsigned i=0;i<reply->row_count;i++){
                const struct oz_row *row=&reply->rows[i];size_t n=strlen(row->name);
                if(!strcmp(row->kind,"file"))leaves++;
                if(n>=6&&!strcmp(row->name+n-6,"/disk0"))old_present=1;
                if(strcmp(row->kind,"file")||n<strlen(suffix)||strcmp(row->name+n-strlen(suffix),suffix)||strcmp(row->state,state))continue;
                const char *field=strstr(row->value," guid=");
                if(!field)continue;
                char *end;unsigned long long value=strtoull(field+6,&end,10);
                if(!value||*end!=' ')continue;
                snprintf(guid,size,"%llu",value);error=0;
            }
            if(storage_replaced&&(leaves!=2||old_present))error=EAGAIN;
            if(!error&&finished){
                const char *scan=strstr(reply->detail,"scan=resilver state=finished ");
                unsigned long long examined=0,total=0,issued=0,errors=1;
                if(!scan||sscanf(scan,"scan=resilver state=finished examined=%llu to-examine=%llu issued=%llu errors=%llu",
                    &examined,&total,&issued,&errors)!=4||!issued||errors)error=EAGAIN;
                if(!error){note("Public resilver scan PASS ");note(scan);note("\n");}
            }
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int storage_repair_test(void)
{
    char guid[32];int error=storage_repair_status(guid,sizeof(guid),"ONLINE",0);
    if(!error)error=storage_request(16,"public_mirror",guid,NULL);
    if(!error)error=storage_repair_status(guid,sizeof(guid),"OFFLINE",0);
    if(!error)error=storage_repair_data("OZPUBLIC:",1);
    if(!error&&cli_command("C:zpool sync public_mirror"))error=EIO;
    if(!error)error=storage_request(15,"public_mirror",guid,NULL);
    if(!error&&cli_command("C:zpool wait -t resilver public_mirror"))error=EIO;
    if(!error){
        for(unsigned i=0;i<120;i++){
            error=storage_repair_status(guid,sizeof(guid),"ONLINE",1);
            if(error!=EAGAIN)break;
            Delay(50);
        }
    }
    if(!error)note("Public mirror resilver PASS offline=1 missed-write=1 online=1 finished=1 issued-nonzero=1 errors=0\n");
    return error;
}
static int storage_replace_test(void)
{
    char old_guid[32],new_guid[32],path[256];
    int error=storage_repair_status(old_guid,sizeof(old_guid),"ONLINE",0);
    if(error)return error;
    snprintf(path,sizeof(path),"%s/disk0-new",storage_directory);
    BPTR exists=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(exists){UnLock(exists);return EEXIST;}
    if(IoErr()!=ERROR_OBJECT_NOT_FOUND)return EIO;
    void *zeros=AllocVec(1024*1024,MEMF_ANY|MEMF_CLEAR);
    if(!zeros)return ENOMEM;
    BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);
    if(!file)error=EIO;
    for(unsigned i=0;i<128&&!error;i++)if(Write(file,zeros,1024*1024)!=1024*1024)error=EIO;
    if(file){if(!Flush(file))error=EIO;if(!Close(file))error=EIO;}
    FreeVec(zeros);
    if(!error)error=storage_request(13,"public_mirror",old_guid,NULL);
    if(!error&&cli_command("C:zpool wait -t resilver,replace public_mirror"))error=EIO;
    if(!error){
        storage_replaced=1;
        for(unsigned i=0;i<120;i++){
            error=storage_repair_status(new_guid,sizeof(new_guid),"ONLINE",1);
            if(error!=EAGAIN)break;
            Delay(50);
        }
        if(!error&&!strcmp(old_guid,new_guid))error=EIO;
    }
    if(!error)note("Public mirror replacement PASS new-guid=1 old-absent=1 leaves=2 finished=1 errors=0\n");
    return error;
}
static int storage_spare_status(char *guid,size_t size,int active)
{
    struct oz_client client;static struct oz_message request;
    if(!oz_client_open(&client))return ENOMEM;
    memset(&request,0,sizeof(request));request.operation=4;strcpy(request.target,"public_mirror");
    int error=oz_submit(&client,&request)==OZ_OK?0:EIO;
    if(!error){
        struct oz_message *reply=NULL;while(!(reply=oz_poll(&client)))Delay(1);
        unsigned leaf=0,spare=0;
        if(reply->status==OZ_OK&&!reply->next_cursor){
            for(unsigned i=0;i<reply->row_count;i++){
                const struct oz_row *r=&reply->rows[i];size_t n=strlen(r->name);
                if(!strcmp(r->kind,"file")&&n>=6&&!strcmp(r->name+n-6,"/disk0")&&
                    !strcmp(r->state,active?"OFFLINE":"ONLINE")){
                    const char *field=strstr(r->value," guid=");char *end=NULL;
                    unsigned long long id=field?strtoull(field+6,&end,10):0;
                    if(id&&end&&*end==' '){snprintf(guid,size,"%llu",id);leaf=1;}
                }
                if(!strcmp(r->kind,"dspare")&&strstr(r->value,"role=spare ")&&
                    !strcmp(r->state,active?"INUSE":"ONLINE")){
                    snprintf(storage_spare_name,sizeof(storage_spare_name),"%s",r->name);spare++;
                }
            }
        }
        error=leaf&&spare==1?0:EIO;
        if(error){
            note("Public dRAID spare status mismatch: ");note(reply->detail);note("\n");
            for(unsigned i=0;i<reply->row_count;i++){
                note(reply->rows[i].kind);note(" ");note(reply->rows[i].name);note(" ");
                note(reply->rows[i].state);note(" ");note(reply->rows[i].value);note("\n");
            }
        }
        unsigned long long completed_rebuild_end=0;
        if(!error&&active&&storage_sequential){
            unsigned complete=0;
            for(unsigned i=0;i<reply->row_count;i++){
                const struct oz_row *r=&reply->rows[i];
                unsigned long long id,issued,rebuilt,errors,start,end;
                if(!strcmp(r->kind,"rebuild")&&!strcmp(r->state,"COMPLETE")&&r->used&&r->progress_permille==1000&&
                    sscanf(r->value,"guid=%llu issued=%llu rebuilt=%llu errors=%llu start=%llu end=%llu",&id,&issued,&rebuilt,&errors,&start,&end)==6&&
                    id&&issued&&rebuilt&&!errors&&end&&end>=start){complete++;completed_rebuild_end=end;note("Public sequential rebuild row PASS ");note(r->value);note("\n");}
            }
            if(complete!=1)error=EAGAIN;
        }
        if(!error&&active){
            const char *prefix=storage_sequential?"scan=scrub state=finished ":"scan=resilver state=finished ";
            const char *scan=strstr(reply->detail,prefix);
            unsigned long long examined,total,issued,errors,start,end;
            if(!scan||sscanf(scan+strlen(prefix),"examined=%llu to-examine=%llu issued=%llu errors=%llu start=%llu end=%llu",
                &examined,&total,&issued,&errors,&start,&end)!=6||!issued||errors||
                (storage_sequential&&(start<completed_rebuild_end||end<start)))error=EAGAIN;
            if(!error){note("Public dRAID spare scan PASS ");note(scan);note("\n");}
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int storage_spare_test(void)
{
    char guid[32];int error=storage_spare_status(guid,sizeof(guid),0);
    if(!error){
        strcpy(storage_spare_name,"draid1-999-999");
        int refused=storage_request(13,"public_mirror",guid,NULL);
        if(!refused||!storage_reply_received)error=EIO;
        if(!error)error=storage_spare_status(guid,sizeof(guid),0);
        if(!error)note("Public dRAID spare refusal PASS absent=1 original-online=1 spare-available=1\n");
    }
    if(!error&&storage_sequential){
        if(storage_request(13,"public_mirror",guid,"rebuild=invalid")!=EINVAL)error=EIO;
        if(!error)error=storage_spare_status(guid,sizeof(guid),0);
        if(!error)note("Public sequential option refusal PASS invalid=1 original-online=1\n");
    }
    if(!error)error=storage_repair_data("OZPUBLIC:",1);
    if(!error&&cli_command("C:zpool sync public_mirror"))error=EIO;
    if(!error)error=storage_request(16,"public_mirror",guid,NULL);
    if(!error&&storage_sequential&&cli_command("C:zpool set feature@device_rebuild=enabled public_mirror"))error=EIO;
    if(!error)error=storage_request(13,"public_mirror",guid,storage_sequential?"rebuild=on":NULL);
    if(!error&&cli_command("C:zpool wait -t resilver public_mirror"))error=EIO;
    /* Upstream automatically starts the checksum scrub after the last
     * rebuild. Starting another races that scrub and can return EBUSY. */
    if(!error&&storage_sequential&&cli_command("C:zpool wait -t scrub public_mirror"))error=EIO;
    if(!error)for(unsigned i=0;i<120;i++){
        error=storage_spare_status(guid,sizeof(guid),1);
        if(error!=EAGAIN)break;
        Delay(50);
    }
    if(!error)note("Public dRAID spare PASS offline=1 inuse=1 finished=1 errors=0\n");
    if(!error&&storage_sequential)note("Public sequential rebuild PASS complete=1 rebuilt-nonzero=1 errors=0 scrub-finished=1\n");
    return error;
}
static int storage_sparse64(const char *root,int write)
{
    char path[96];snprintf(path,sizeof(path),"%slarge-sparse.bin",root);
    BPTR file=Open((CONST_STRPTR)path,write?MODE_NEWFILE:MODE_OLDFILE);
    if(!file)return EIO;
    struct FileHandle *fh=BADDR(file);
    const uint64_t boundary=UINT64_C(0x100000000),size=boundary+4096;
    int error=0;const char payload[]="Across the 4 GiB boundary";
#define SPACKET(action,offset,mode) DoPkt(fh->fh_Type,action,fh->fh_Arg1,(IPTR)(offset),mode,0,0)
    if(write){
        if(!SPACKET(ACTION_CHANGE_FILE_SIZE64,size,OFFSET_BEGINNING)||
            !SPACKET(ACTION_CHANGE_FILE_POSITION64,boundary-8,OFFSET_BEGINNING)||
            Write(file,payload,sizeof(payload))!=sizeof(payload)||!Flush(file))error=EIO;
    }else if(SPACKET(ACTION_CHANGE_FILE_SIZE64,size+1,OFFSET_BEGINNING)||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
    if(!error&&SPACKET(ACTION_GET_FILE_SIZE64,0,0)!=(SIPTR)size)error=EIO;
    unsigned char bytes[64],expected[64]={0};memcpy(expected+8,payload,sizeof(payload));
    if(!error&&(!SPACKET(ACTION_CHANGE_FILE_POSITION64,boundary-16,OFFSET_BEGINNING)||
        Read(file,bytes,sizeof(bytes))!=sizeof(bytes)||memcmp(bytes,expected,sizeof(bytes))))error=EIO;
    if(!error&&(Seek(file,0,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_OBJECT_TOO_LARGE||
        SPACKET(ACTION_GET_FILE_POSITION64,0,0)!=(SIPTR)(boundary+48)))error=EIO;
    if(!Close(file))error=EIO;
#undef SPACKET
    if(!error){char line[128];snprintf(line,sizeof(line),"Public sparse64 PASS write=%d boundary=4294967296 size=4294971392 holes=1 legacy-refused=1\n",write);note(line);}
    return error;
}
/* Exercise the shipped transport, not engine-internal test entry points.
 * Retain the volume for independent cold/degraded imports. */
static struct NotifyMessage *notification_wait(struct MsgPort *port)
{
    for(unsigned i=0;i<100;i++){
        struct NotifyMessage *message=(void *)GetMsg(port);
        if(message)return message;
        Delay(1);
    }
    return NULL;
}
static int storage_notifications(void)
{
    const char *path="OZPUBLIC:notification.txt";
    struct MsgPort *port=CreateMsgPort();if(!port)return ENOMEM;
    struct NotifyRequest request={0};
    request.nr_Name=(STRPTR)path;request.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
    request.nr_stuff.nr_Msg.nr_Port=port;
    int active=0,error=0;BPTR file=0;struct NotifyMessage *held=NULL;
#define NOTIFY_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    NOTIFY_CHECK(StartNotify(&request));active=1;
    file=Open((CONST_STRPTR)path,MODE_NEWFILE);NOTIFY_CHECK(file);
    NOTIFY_CHECK(Write(file,"notify",6)==6);
    int closed=Close(file);file=0;NOTIFY_CHECK(closed);
    held=notification_wait(port);NOTIFY_CHECK(held);
    NOTIFY_CHECK(held->nm_Class==NOTIFY_CLASS&&held->nm_Code==NOTIFY_CODE&&held->nm_NReq==&request);
    NOTIFY_CHECK(SetComment((CONST_STRPTR)path,(CONST_STRPTR)"pending"));
    struct Message *extra=GetMsg(port);
    if(extra){ReplyMsg(extra);NOTIFY_CHECK(0);}
    ReplyMsg(&held->nm_ExecMessage);held=NULL;
    held=notification_wait(port);NOTIFY_CHECK(held);
    NOTIFY_CHECK(held->nm_NReq==&request&&held->nm_Class==NOTIFY_CLASS&&held->nm_Code==NOTIFY_CODE);
    ReplyMsg(&held->nm_ExecMessage);held=NULL;
    EndNotify(&request);active=0;
    NOTIFY_CHECK(SetComment((CONST_STRPTR)path,(CONST_STRPTR)"after removal"));
    Delay(2);extra=GetMsg(port);
    if(extra){ReplyMsg(extra);NOTIFY_CHECK(0);}
    for(unsigned cycle=0;cycle<3;cycle++){
        memset(&request,0,sizeof(request));
        request.nr_Name=(STRPTR)path;
        request.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY|NRF_NOTIFY_INITIAL;
        request.nr_stuff.nr_Msg.nr_Port=port;
        NOTIFY_CHECK(StartNotify(&request));active=1;
        held=notification_wait(port);NOTIFY_CHECK(held);
        EndNotify(&request);active=0;
        /* EndNotify cannot reclaim a message already held by its caller.
         * Reuse the request before replying, so the retired watch must not
         * decrement the new subscription's count or deliver pending events. */
        memset(&request,0,sizeof(request));
        request.nr_Name=(STRPTR)path;
        request.nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY;
        request.nr_stuff.nr_Msg.nr_Port=port;
        NOTIFY_CHECK(StartNotify(&request));active=1;
        ReplyMsg(&held->nm_ExecMessage);held=NULL;
        NOTIFY_CHECK(SetComment((CONST_STRPTR)path,(CONST_STRPTR)"reused request"));
        held=notification_wait(port);NOTIFY_CHECK(held);
        NOTIFY_CHECK(held->nm_NReq==&request&&request.nr_MsgCount==1);
        ReplyMsg(&held->nm_ExecMessage);held=NULL;
        EndNotify(&request);active=0;
        Delay(2);extra=GetMsg(port);
        if(extra){ReplyMsg(extra);NOTIFY_CHECK(0);}
    }
cleanup:
    if(file)Close(file);
    if(held)ReplyMsg(&held->nm_ExecMessage);
    if(active)EndNotify(&request);
    struct Message *message;while((message=GetMsg(port)))ReplyMsg(message);
    DeleteMsgPort(port);
    if(!DeleteFile((CONST_STRPTR)path)&&!error)error=__LINE__;
    if(error)bug("Public DOS notifications FAIL line=%d DOS=%ld\n",error,(long)IoErr());
    else note("Public DOS notifications PASS absent-watch=1 create=1 wait-reply=1 coalesced=1 end=1 late-reply-reuse=3\n");
#undef NOTIFY_CHECK
    return error;
}
static int notification_signal_wait(ULONG mask)
{
    for(unsigned i=0;i<100;i++){
        if(SetSignal(0,mask)&mask)return 1;
        Delay(1);
    }
    return 0;
}
static int storage_signal_notifications(void)
{
    const char *path="OZPUBLIC:signal-notification.txt";
    BYTE signal=AllocSignal(-1);if(signal<0)return ENOMEM;
    ULONG mask=1UL<<signal;SetSignal(0,mask);
    struct NotifyRequest request={0};
    request.nr_Name=(STRPTR)path;
    request.nr_Flags=NRF_SEND_SIGNAL|NRF_NOTIFY_INITIAL;
    request.nr_stuff.nr_Signal.nr_Task=FindTask(NULL);
    request.nr_stuff.nr_Signal.nr_SignalNum=signal;
    int active=0,error=0;BPTR file=0;
#define SIGNAL_CHECK(x) do{if(!(x)){error=__LINE__;goto cleanup;}}while(0)
    file=Open((CONST_STRPTR)path,MODE_NEWFILE);SIGNAL_CHECK(file);
    int closed=Close(file);file=0;SIGNAL_CHECK(closed);
    SIGNAL_CHECK(StartNotify(&request));active=1;
    SIGNAL_CHECK(notification_signal_wait(mask));
    SIGNAL_CHECK(SetComment((CONST_STRPTR)path,(CONST_STRPTR)"signal change"));
    SIGNAL_CHECK(notification_signal_wait(mask));
    SIGNAL_CHECK(DeleteFile((CONST_STRPTR)path));
    SIGNAL_CHECK(notification_signal_wait(mask));
    EndNotify(&request);active=0;SetSignal(0,mask);
    file=Open((CONST_STRPTR)path,MODE_NEWFILE);SIGNAL_CHECK(file);
    closed=Close(file);file=0;SIGNAL_CHECK(closed);
    Delay(2);SIGNAL_CHECK(!(SetSignal(0,mask)&mask));
cleanup:
    if(file)Close(file);
    if(active)EndNotify(&request);
    SetSignal(0,mask);FreeSignal(signal);
    if(!DeleteFile((CONST_STRPTR)path)&&!error)error=__LINE__;
    if(error)bug("Public DOS signal notifications FAIL line=%d DOS=%ld\n",error,(long)IoErr());
    else note("Public DOS signal notifications PASS initial=1 metadata=1 deletion=1 end=1\n");
#undef SIGNAL_CHECK
    return error;
}
static int storage_metadata(const char *volume,int write)
{
    char path[128];snprintf(path,sizeof(path),"%smetadata.txt",volume);
    const char *comment="OpenZFS native metadata";
    struct DateStamp date={17000,721,1234};
    const ULONG protection=FIBF_SCRIPT|FIBF_ARCHIVE;
    if(write){
        BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);
        if(!file)return EIO;
        int okay=Write(file,"metadata",8)==8;
        if(!Close(file)||!okay)return EIO;
        if(!SetComment((CONST_STRPTR)path,(CONST_STRPTR)comment)||
           !SetProtection((CONST_STRPTR)path,protection)||
           !SetFileDate((CONST_STRPTR)path,&date))return EIO;
    }else{
        LONG result=SetComment((CONST_STRPTR)path,(CONST_STRPTR)"must not persist"),dos=IoErr();
        bug("Metadata readonly comment result=%ld DOS=%ld\n",(long)result,(long)dos);
        if(result||dos!=ERROR_DISK_WRITE_PROTECTED)return EIO;
        result=SetProtection((CONST_STRPTR)path,0);dos=IoErr();
        bug("Metadata readonly protection result=%ld DOS=%ld\n",(long)result,(long)dos);
        if(result||dos!=ERROR_DISK_WRITE_PROTECTED)return EIO;
        struct DateStamp changed={17001,0,0};
        result=SetFileDate((CONST_STRPTR)path,&changed);dos=IoErr();
        bug("Metadata readonly timestamp result=%ld DOS=%ld\n",(long)result,(long)dos);
        if(result||dos!=ERROR_DISK_WRITE_PROTECTED)return EIO;
    }
    BPTR lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    /* Examine presents effective readonly restrictions in addition to stored
     * protection. This must not be mistaken for changed on-disk metadata. */
    ULONG shown=protection|(write?0:FIBF_WRITE|FIBF_DELETE);
    int examined=lock&&fib&&Examine(lock,fib);
    int okay=examined&&!strcmp(fib->fib_Comment,comment)&&
        fib->fib_Protection==shown&&fib->fib_Size==8&&
        fib->fib_Date.ds_Days==date.ds_Days&&fib->fib_Date.ds_Minute==date.ds_Minute&&
        fib->fib_Date.ds_Tick==date.ds_Tick;
    if(examined)bug("Metadata observed write=%d comment='%s' protection=%lx size=%ld date=%ld/%ld/%ld DOS=%ld\n",
        write,fib->fib_Comment,(unsigned long)fib->fib_Protection,(long)fib->fib_Size,
        (long)fib->fib_Date.ds_Days,(long)fib->fib_Date.ds_Minute,(long)fib->fib_Date.ds_Tick,(long)IoErr());
    if(fib)FreeDosObject(DOS_FIB,fib);
    if(lock)UnLock(lock);
    if(!okay)return EIO;
    BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);char bytes[8];
    if(!file)return EIO;
    okay=Read(file,bytes,sizeof(bytes))==sizeof(bytes)&&!memcmp(bytes,"metadata",8);
    if(!Close(file)||!okay)return EIO;
    char line[160];snprintf(line,sizeof(line),"Public DOS metadata PASS write=%d comment=1 protection=1 timestamp=1 exact-data=1\n",write);note(line);
    return 0;
}
static int storage_links(const char *volume,int write)
{
    char source[96],hard[96],soft[96],dangling[96],denied[96];
    snprintf(source,sizeof(source),"%slink-source",volume);
    snprintf(hard,sizeof(hard),"%slink-hard",volume);
    snprintf(soft,sizeof(soft),"%slink-soft",volume);
    snprintf(dangling,sizeof(dangling),"%slink-dangling",volume);
    snprintf(denied,sizeof(denied),"%slink-denied",volume);
    const char payload[]="Persistent native DOS links";
    int error=0;BPTR file=0,lock=0;
#define LINK_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    if(write){
        file=Open((CONST_STRPTR)source,MODE_NEWFILE);LINK_CHECK(file);
        LINK_CHECK(Write(file,payload,sizeof(payload))==sizeof(payload));
        LINK_CHECK(Flush(file));int closed=Close(file);file=0;LINK_CHECK(closed);
        lock=Lock((CONST_STRPTR)source,SHARED_LOCK);LINK_CHECK(lock);
        LINK_CHECK(MakeLink((CONST_STRPTR)hard,(SIPTR)lock,LINK_HARD));
        LINK_CHECK(!MakeLink((CONST_STRPTR)hard,(SIPTR)lock,LINK_HARD)&&IoErr()==ERROR_OBJECT_EXISTS);
        UnLock(lock);lock=0;
        LINK_CHECK(MakeLink((CONST_STRPTR)soft,(SIPTR)"link-hard",LINK_SOFT));
        LINK_CHECK(MakeLink((CONST_STRPTR)dangling,(SIPTR)"link-absent",LINK_SOFT));
        LINK_CHECK(DeleteFile((CONST_STRPTR)source));
    }
    const char *paths[]={hard,soft};
    for(unsigned i=0;i<2;i++){
        char bytes[sizeof(payload)+1];
        file=Open((CONST_STRPTR)paths[i],MODE_OLDFILE);LINK_CHECK(file);
        LINK_CHECK(Read(file,bytes,sizeof(bytes))==sizeof(payload)&&!memcmp(bytes,payload,sizeof(payload)));
        int closed=Close(file);file=0;LINK_CHECK(closed);
    }
    lock=Lock((CONST_STRPTR)source,SHARED_LOCK);
    LINK_CHECK(!lock&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    file=Open((CONST_STRPTR)dangling,MODE_OLDFILE);
    LINK_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    if(!write){
        lock=Lock((CONST_STRPTR)hard,SHARED_LOCK);LINK_CHECK(lock);
        LINK_CHECK(!MakeLink((CONST_STRPTR)denied,(SIPTR)lock,LINK_HARD)&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        UnLock(lock);lock=0;
        LINK_CHECK(!MakeLink((CONST_STRPTR)denied,(SIPTR)"link-hard",LINK_SOFT)&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    }
done:
    if(file)Close(file);
    if(lock)UnLock(lock);
    char line[160];
    if(error)snprintf(line,sizeof(line),"Public DOS links FAIL write=%d line=%d DOS=%ld\n",write,error,(long)IoErr());
    else snprintf(line,sizeof(line),"Public DOS links PASS write=%d hard=1 relative-soft=1 unlinked-source=1 dangling-refused=1\n",write);
    note(line);
#undef LINK_CHECK
    return error;
}
static int storage_zvol_test(int create)
{
    struct oz_ticket session={0};
    struct {struct oz_zvol_request q;unsigned char bytes[24576];} packet;
    LONG fd=-1;int error=0;
#define VCHECK(x) do{if(!(x)){char line[96];snprintf(line,sizeof(line),"Public zvol FAIL line=%d cold=%d\n",__LINE__,!create);note(line);error=EIO;goto done;}}while(0)
#define VSET(op) do{memset(&packet,0,sizeof(packet));packet.q.session=session;packet.q.version=OZ_ZVOL_VERSION;packet.q.operation=(op);packet.q.descriptor=fd;}while(0)
#define VCALL(expected) do{ULONG size=sizeof(packet.q);if(packet.q.operation==OZ_ZVOL_READ||packet.q.operation==OZ_ZVOL_WRITE)size+=packet.q.count;VCHECK(!oz_zvol_request(OZ_ADMISSION_PORT,&packet,size)&&packet.q.engine_error==(expected));}while(0)
    if(create)VCHECK(!cli_command("C:zfs create -s -V 2M -b 8K public_mirror/public_volume"));
    VCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
    VSET(OZ_ZVOL_OPEN);strcpy(packet.q.name,"public_mirror/public_volume");
    packet.q.flags=create?OZ_ZVOL_WRITABLE:0;VCALL(0);fd=packet.q.descriptor;VCHECK(fd>0);
    VSET(OZ_ZVOL_INFO);VCALL(0);
    VCHECK(packet.q.size==2097152&&packet.q.block_size==8192&&packet.q.readonly==!create);
    if(create){
        VSET(OZ_ZVOL_WRITE);packet.q.count=sizeof(packet.bytes);
        for(unsigned i=0;i<sizeof(packet.bytes);i++)packet.bytes[i]=(unsigned char)(i*7+13);
        VCALL(0);VCHECK(packet.q.completed==sizeof(packet.bytes));
        VSET(OZ_ZVOL_DISCARD);packet.q.offset=512;packet.q.count=16384;
        VCALL(0);VCHECK(packet.q.completed==16384);
        VSET(OZ_ZVOL_FLUSH);VCALL(0);
    }else{
        VSET(OZ_ZVOL_WRITE);packet.q.count=512;VCALL(EROFS);VCHECK(!packet.q.completed);
    }
    VSET(OZ_ZVOL_READ);packet.q.count=sizeof(packet.bytes);VCALL(0);
    VCHECK(packet.q.completed==sizeof(packet.bytes));
    for(unsigned i=0;i<sizeof(packet.bytes);i++)
        VCHECK(packet.bytes[i]==(i>=8192&&i<16384?0:(unsigned char)(i*7+13)));
    VSET(OZ_ZVOL_READ);packet.q.offset=2097152;packet.q.count=512;
    memset(packet.bytes,0xa7,512);VCALL(EINVAL);VCHECK(!packet.q.completed);
    for(unsigned i=0;i<512;i++)VCHECK(packet.bytes[i]==0xa7);
    VSET(OZ_ZVOL_CLOSE);VCALL(0);
    VSET(OZ_ZVOL_INFO);VCALL(EBADF);
done:
    if(session.number){int closed=oz_session_close(OZ_ADMISSION_PORT,&session);if(!error)error=closed;}
    if(!error)note(create?"Public zvol PASS create=1 geometry=1 write=1 discard-edges=1 flush=1 exact-read=1 bounds=1 stale-descriptor=1\n":
        "Public cold zvol PASS exact-read=1 discard-edges=1 readonly=1 bounds=1 stale-descriptor=1\n");
    return error;
#undef VCALL
#undef VSET
#undef VCHECK
}
static int storage_device_test(int write)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-device-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    static int started;
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    unsigned char *bytes=AllocVec(131072,MEMF_PUBLIC);
    int error=0,opened=0,mapped=0;
#define DCHECK(x) do{if(!(x)){char line[128];snprintf(line,sizeof(line),"Public device FAIL line=%d cold=%d io=%d\n",__LINE__,!write,io?io->io_Error:0);note(line);error=EIO;goto done;}}while(0)
    DCHECK(io&&bytes);
    if(!started){
        BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE);
        BPTR output=Open((CONST_STRPTR)"T:public-device.log",MODE_NEWFILE);
        BPTR errors=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);
        if(!input||!output||!errors){if(input)Close(input);if(output)Close(output);if(errors)Close(errors);DCHECK(0);}
        LONG launched=SystemTags((CONST_STRPTR)"C:OpenZFSDevice EMPTY",SYS_Asynch,TRUE,
            SYS_Input,input,SYS_Output,output,SYS_Error,errors,NP_StackSize,262144,TAG_DONE);
        if(launched==-1){Close(input);Close(output);Close(errors);DCHECK(0);}
        for(unsigned i=0;i<500;i++){
            if(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0)){opened=1;break;}
            Delay(1);
        }
        DCHECK(opened);CloseDevice((struct IORequest *)io);opened=0;started=1;
        DCHECK(cli_command("C:OpenZFSDevice EMPTY")==10);
    }
    DCHECK(!cli_command(write?"C:OpenZFSDevice REMAP 0 public_mirror/public_volume WRITABLE":
        "C:OpenZFSDevice REMAP 0 public_mirror/public_volume"));mapped=1;
    for(unsigned pass=0;pass<3;pass++){
        DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));opened=1;
        struct DriveGeometry geometry={0};
        io->io_Command=TD_GETGEOMETRY;io->io_Data=&geometry;io->io_Length=sizeof(geometry);
        DCHECK(!DoIO((struct IORequest *)io)&&geometry.dg_SectorSize==512&&geometry.dg_TotalSectors==4096);
        /* A separate eject command must not revoke this active client's unit. */
        DCHECK(cli_command("C:OpenZFSDevice EJECT 0")!=0);
        io->io_Data=bytes;io->io_Length=131072;io->io_Offset=32768;
        if(write&&pass==0){
            for(unsigned i=0;i<131072;i++)bytes[i]=(unsigned char)(i*11+i/512+29);
            io->io_Command=CMD_WRITE;SendIO((struct IORequest *)io);
            DCHECK(!WaitIO((struct IORequest *)io)&&io->io_Actual==131072);
            io->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)io));
        }else if(!write){
            memset(bytes,0xee,131072);io->io_Command=CMD_WRITE;
            DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt&&!io->io_Actual);
        }
        memset(bytes,0,131072);io->io_Command=CMD_READ;
        SendIO((struct IORequest *)io);DCHECK(!WaitIO((struct IORequest *)io)&&io->io_Actual==131072);
        for(unsigned i=0;i<131072;i++)DCHECK(bytes[i]==(unsigned char)(i*11+i/512+29));
        io->io_Offset=2097152;io->io_Length=512;memset(bytes,0xa7,512);
        DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH&&!io->io_Actual);
        for(unsigned i=0;i<512;i++)DCHECK(bytes[i]==0xa7);
        DCHECK(!GetMsg(port));
        CloseDevice((struct IORequest *)io);opened=0;
    }
done:
    if(opened)CloseDevice((struct IORequest *)io);
    if(mapped){int eject=cli_command("C:OpenZFSDevice EJECT 0");if(!error&&eject)error=EIO;}
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    if(bytes)FreeVec(bytes);
    if(!error)note(write?"Public device PASS geometry=1 async-128KiB=1 reopen=3 exact-read=1 eject=1 busy-refused=1 bounds=1\n":
        "Public cold device PASS geometry=1 readonly=1 reopen=3 exact-read=1 eject=1 busy-refused=1 bounds=1\n");
    return error;
#undef DCHECK
}
static int storage_zvol_filesystem(int create)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-zvol-filesystem-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    marker=Lock((CONST_STRPTR)"SYS:openzfs-zvol-ffs-test",SHARED_LOCK);
    int ffs=marker!=0;if(marker)UnLock(marker);
    int error=0;BPTR file=0,root=0;
    unsigned char *bytes=AllocVec(131072,MEMF_PUBLIC);
#define FCHECK(x) do{if(!(x)){char line[128];snprintf(line,sizeof(line),"Public zvol filesystem FAIL line=%d cold=%d dos=%ld\n",__LINE__,!create,(long)IoErr());note(line);error=EIO;goto done;}}while(0)
    FCHECK(bytes);
    if(create)FCHECK(!cli_command("C:zfs create -s -V 64M -b 8K public_mirror/filesystem_volume"));
    FCHECK(!cli_command(create?"C:OpenZFSDevice REMAP 1 public_mirror/filesystem_volume WRITABLE":
        "C:OpenZFSDevice REMAP 1 public_mirror/filesystem_volume"));
    FCHECK(!cli_command(ffs?"C:OpenZFSDevice MOUNTFILE 1 FFS >T:OZVFS":
        "C:OpenZFSDevice MOUNTFILE 1 FAT >T:OZVFS"));
    FCHECK(!cli_command("C:Mount T:OZVFS"));
    if(create){
        FCHECK(Inhibit((CONST_STRPTR)"OZVFS:",DOSTRUE));
        FCHECK(Format((CONST_STRPTR)"OZVFS:",(CONST_STRPTR)"PublicZvol",ffs?ID_FFS_DISK:ID_MSDOS_DISK));
        FCHECK(Inhibit((CONST_STRPTR)"OZVFS:",DOSFALSE));
        for(unsigned i=0;i<131072;i++)bytes[i]=(unsigned char)(i*17+i/512+41);
        file=Open((CONST_STRPTR)"OZVFS:payload.bin",MODE_NEWFILE);FCHECK(file);
        FCHECK(Write(file,bytes,131072)==131072&&Flush(file));
        int closed=Close(file);file=0;FCHECK(closed);
        /* Drain filesystem metadata, then rediscover from the device. */
        FCHECK(Inhibit((CONST_STRPTR)"OZVFS:",DOSTRUE));
        FCHECK(Inhibit((CONST_STRPTR)"OZVFS:",DOSFALSE));
    }
    root=Lock((CONST_STRPTR)"OZVFS:",SHARED_LOCK);FCHECK(root);
    struct FileInfoBlock fib;struct InfoData info;
    FCHECK(Examine(root,&fib));int found=0;
    while(ExNext(root,&fib))if(!strcmp(fib.fib_FileName,"payload.bin")&&fib.fib_Size==131072)found++;
    FCHECK(IoErr()==ERROR_NO_MORE_ENTRIES&&found==1);
    FCHECK(Info(root,&info));
    if(ffs)FCHECK(info.id_DiskType==ID_FFS_DISK);
    if(!create)FCHECK(info.id_DiskState==ID_WRITE_PROTECTED);
    UnLock(root);root=0;
    struct DosList *list=LockDosList(LDF_VOLUMES|LDF_READ);
    struct DosList *volume=FindDosEntry(list,(CONST_STRPTR)"PublicZvol",LDF_VOLUMES);
    int visible=volume&&volume->dol_Task;
    UnLockDosList(LDF_VOLUMES|LDF_READ);FCHECK(visible);
    if(!create){
        if(ffs)note("Public FFS denial requester expected create\n");
        file=Open((CONST_STRPTR)"OZVFS:denied.bin",MODE_NEWFILE);
        FCHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        if(ffs)note("Public FFS denial resolved create\n");
        if(ffs)note("Public FFS denial requester expected delete\n");
        FCHECK(!DeleteFile((CONST_STRPTR)"OZVFS:payload.bin")&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        if(ffs)note("Public FFS denial resolved delete\n");
        if(ffs)note("Public FFS denial requester expected rename\n");
        FCHECK(!Rename((CONST_STRPTR)"OZVFS:payload.bin",(CONST_STRPTR)"OZVFS:renamed.bin")&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        if(ffs)note("Public FFS denial resolved rename\n");
    }
    FCHECK(cli_command("C:OpenZFSDevice EJECT 1")!=0);
    file=Open((CONST_STRPTR)"OZVFS:payload.bin",MODE_OLDFILE);FCHECK(file);
    memset(bytes,0,131072);FCHECK(Read(file,bytes,131072)==131072);
    for(unsigned i=0;i<131072;i++)FCHECK(bytes[i]==(unsigned char)(i*17+i/512+41));
    int closed=Close(file);file=0;FCHECK(closed);
    FCHECK(Inhibit((CONST_STRPTR)"OZVFS:",DOSTRUE));
    struct MsgPort *filesystem=DeviceProc((CONST_STRPTR)"OZVFS:");FCHECK(filesystem);
    list=LockDosList(LDF_DEVICES|LDF_READ);
    struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)"OZVFS",LDF_DEVICES);
    int owned=entry&&entry->dol_Task==filesystem;
    UnLockDosList(LDF_DEVICES|LDF_READ);FCHECK(owned);
    /* Retire only this private fixture node before its handler exits. */
    FCHECK(RemDosEntry(entry));
    FCHECK(DoPkt(filesystem,ACTION_DIE,0,0,0,0,0));
    FCHECK(!cli_command("C:OpenZFSDevice EJECT 1"));
done:
    if(file)Close(file);
    if(root)UnLock(root);
    if(bytes)FreeVec(bytes);
    if(!error)note(create?"Public zvol filesystem PASS format=1 write=1 flush=1 exact-read=1 directory=1 volume-list=1 busy-refused=1 shutdown=1\n":
        "Public cold zvol filesystem PASS exact-read=1 readonly=1 directory=1 volume-list=1 busy-refused=1 shutdown=1\n");
    if(!error&&ffs)note("Public FFS zvol type PASS dos-type-verified=1\n");
    return error;
#undef FCHECK
}
static int storage_gui_volume_verify(void)
{
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    int error=0,opened=0;BPTR file=0;
    struct oz_device_mapping mapping={0};unsigned char bytes[512];
    char expected[OZ_MOUNTFILE_SIZE],saved[OZ_MOUNTFILE_SIZE];
#define GCHECK(x) do{if(!(x)){char line[96];snprintf(line,sizeof(line),"Public GUI volume FAIL line=%d\n",__LINE__);note(line);error=EIO;goto done;}}while(0)
    GCHECK(io);
    GCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",5,(struct IORequest *)io,0));opened=1;
    io->io_Command=OZCMD_GETMAPPING;io->io_Data=&mapping;io->io_Length=sizeof(mapping);
    GCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(mapping));
    GCHECK(mapping.unit==5&&mapping.bytes==2097152&&mapping.sector_size==512&&
        mapping.flags==(OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY)&&
        !strcmp(mapping.dataset,"public_mirror/public_volume"));
    io->io_Command=CMD_READ;io->io_Data=bytes;io->io_Length=sizeof(bytes);io->io_Offset=32768;
    GCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(bytes));
    for(unsigned i=0;i<sizeof(bytes);i++)GCHECK(bytes[i]==(unsigned char)(i*11+i/512+29));
    io->io_Command=CMD_WRITE;GCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt&&!io->io_Actual);
    GCHECK(!oz_mountfile_format(&mapping,1,0,expected,sizeof(expected)));
    file=Open((CONST_STRPTR)"T:OZBLOCK",MODE_OLDFILE);GCHECK(file);
    LONG n=Read(file,saved,sizeof(saved));
    GCHECK(n==(LONG)strlen(expected)&&!memcmp(saved,expected,n));
done:
    if(file)Close(file);
    if(opened)CloseDevice((struct IORequest *)io);
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    if(!error&&cli_command("C:OpenZFSDevice EJECT 5"))error=EIO;
    if(!error)note("Public GUI volume PASS actual-mapping=1 readonly=1 exact-data=1 saved-mountfile=1 eject=1\n");
    return error;
#undef GCHECK
}
static int pool_root_payload(const char *dataset,int create,int readonly)
{
    const char *volume="OZDISCOVERY";
    int error=oz_mount_control(OZ_ADMISSION_PORT,dataset,volume,OZ_MOUNT_ATTACH,
        readonly?OZ_MOUNT_READONLY:0);
    if(error)return error;
    BPTR file=Open((CONST_STRPTR)"OZDISCOVERY:payload",create?MODE_NEWFILE:MODE_OLDFILE);
    if(!file)error=EIO;
    if(file){
        if(create){
            if(Write(file,storage_payload,sizeof(storage_payload)-1)!=sizeof(storage_payload)-1||!Flush(file))error=EIO;
        }else{
            char data[sizeof(storage_payload)]={0};
            if(Read(file,data,sizeof(data))!=sizeof(storage_payload)-1||
                memcmp(data,storage_payload,sizeof(storage_payload)-1))error=EIO;
        }
        if(!Close(file))error=EIO;
    }
    if(!error&&readonly){
        file=Open((CONST_STRPTR)"OZDISCOVERY:denied",MODE_NEWFILE);
        LONG dos_error=IoErr();
        if(file){Close(file);error=EIO;}
        else if(dos_error!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
    }
    int detached=oz_mount_control(OZ_ADMISSION_PORT,dataset,"",OZ_MOUNT_DETACH,0);
    return error?error:detached;
}
static int file_reuse_label_denials(void)
{
    /* These two RAM files belong exclusively to storage_file_reuse and their
     * pool has been destroyed. Preserve every byte, including on failure. */
    const ULONG extent=64UL*1024*1024,chunk=128UL*1024,label_bytes=112UL*1024;
    unsigned char *before[2]={AllocVec(extent,MEMF_PUBLIC),AllocVec(extent,MEMF_PUBLIC)};
    unsigned char *check=AllocVec(chunk,MEMF_PUBLIC),*saved=AllocVec(label_bytes,MEMF_PUBLIC);
    unsigned char *replacement=AllocVec(label_bytes,MEMF_PUBLIC|MEMF_CLEAR);
    BPTR file=0;int error=0,changed=0;ULONG damaged=0;
#define FCHECK(x) do{if(!(x)){char line[96];snprintf(line,sizeof(line),"Public file label denial FAIL line=%d\n",__LINE__);note(line);error=EIO;goto done;}}while(0)
    FCHECK(before[0]&&before[1]&&check&&saved&&replacement);
    for(unsigned i=0;i<2;i++){
        char path[80];storage_member(path,sizeof(path),i);
        file=Open((CONST_STRPTR)path,MODE_OLDFILE);FCHECK(file);
        for(ULONG offset=0;offset<extent;offset+=chunk)
            FCHECK(Read(file,before[i]+offset,chunk)==chunk);
        FCHECK(Read(file,check,1)==0);
        int closed=Close(file);file=0;FCHECK(closed);
    }
    for(unsigned conflict=0;conflict<3;conflict++){
        for(unsigned label=0;label<4;label++){
            damaged=label*256UL*1024+(label<2?0:extent-4UL*256*1024)+16UL*1024;
            memcpy(saved,before[0]+damaged,label_bytes);
            /* Use the donor's SAME label position: the checksum verifier is
             * offset-bound. Copying label zero here would test a bad checksum
             * instead of a conflicting member identity for labels 1..3. */
            if(conflict==1)memcpy(replacement,before[1]+damaged,label_bytes);
            if(conflict==2){memcpy(replacement,saved,label_bytes);replacement[label_bytes-1]^=1;}
            memcpy(before[0]+damaged,replacement,label_bytes);
            file=Open((CONST_STRPTR)"RAM:OZFileReuse/disk0",MODE_READWRITE);FCHECK(file);
            FCHECK(Seek(file,damaged,OFFSET_BEGINNING)!=-1);changed=1;
            FCHECK(Write(file,replacement,label_bytes)==label_bytes&&Flush(file));
            int closed=Close(file);file=0;FCHECK(closed);
            FCHECK(storage_request(5,"public_file_refused",NULL,NULL)!=0);
            FCHECK(cli_command("C:zpool create -f -o cachefile=none -O mountpoint=none public_file_refused mirror /RAM/OZFileReuse/disk0 /RAM/OZFileReuse/disk1")!=0);
            for(unsigned i=0;i<2;i++){
                char path[80];storage_member(path,sizeof(path),i);
                file=Open((CONST_STRPTR)path,MODE_OLDFILE);FCHECK(file);
                for(ULONG offset=0;offset<extent;offset+=chunk){
                    FCHECK(Read(file,check,chunk)==chunk);
                    FCHECK(!memcmp(check,before[i]+offset,chunk));
                }
                FCHECK(Read(file,check,1)==0);
                closed=Close(file);file=0;FCHECK(closed);
            }
            file=Open((CONST_STRPTR)"RAM:OZFileReuse/disk0",MODE_READWRITE);FCHECK(file);
            FCHECK(Seek(file,damaged,OFFSET_BEGINNING)!=-1);
            FCHECK(Write(file,saved,label_bytes)==label_bytes&&Flush(file));
            closed=Close(file);file=0;FCHECK(closed);changed=0;
            memcpy(before[0]+damaged,saved,label_bytes);
        }
    }
done:
    if(file)Close(file);
    if(changed){
        file=Open((CONST_STRPTR)"RAM:OZFileReuse/disk0",MODE_READWRITE);
        if(file){
            if(Seek(file,damaged,OFFSET_BEGINNING)==-1||
                Write(file,saved,label_bytes)!=label_bytes||!Flush(file))
                note("Public file label restore FAIL\n");
            if(!Close(file))note("Public file label restore close FAIL\n");
        }else note("Public file label restore open FAIL\n");
    }
    for(unsigned i=0;i<2;i++)if(before[i])FreeVec(before[i]);
    if(check)FreeVec(check);if(saved)FreeVec(saved);if(replacement)FreeVec(replacement);
    if(!error)note("Public file label denial PASS corrupt=4 conflicting-member=4 checksum=4 manager-denied=12 forced-cli-denied=12 whole-members-unchanged=12 restored=12\n");
    if(!error)note("Public file conflicting label offsets PASS same-position=4\n");
    return error;
#undef FCHECK
}
static int reuse_change_guid(const char *suffix,char *guid,unsigned wanted_leaves)
{
    /* File paths are canonicalized to DOS volume syntax by admission; each
     * fixture basename is unique. Native paths retain their full identity. */
    if(strncmp(suffix,"/AROS/",6)){
        const char *base=strrchr(suffix,'/');if(base)suffix=base;
    }
    struct oz_client client;static struct oz_message request;
    if(!oz_client_open(&client))return ENOMEM;
    memset(&request,0,sizeof(request));request.operation=4;
    strcpy(request.target,"public_reuse_target");
    int error=oz_submit(&client,&request)==OZ_OK?0:EIO;
    if(!error){
        struct oz_message *reply=NULL;while(!(reply=oz_poll(&client)))Delay(1);
        unsigned leaves=0;error=EIO;guid[0]=0;
        if(reply->status==OZ_OK&&!reply->next_cursor){
            for(unsigned i=0;i<reply->row_count;i++){
                struct oz_row *row=&reply->rows[i];size_t n=strlen(row->name),s=strlen(suffix);
                if(strcmp(row->kind,"file")&&strcmp(row->kind,"disk"))continue;
                if(strcmp(row->state,"ONLINE"))break;
                leaves++;
                if(n<s||strcmp(row->name+n-s,suffix))continue;
                const char *field=strstr(row->value," guid=");char *end=NULL;
                unsigned long long value=field?strtoull(field+6,&end,10):0;
                if(value&&*end==' ')snprintf(guid,32,"%llu",value);
            }
            if(guid[0]&&leaves==wanted_leaves)error=0;
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int reuse_change_request(int manager,unsigned op,const char *guid,
    const char *first,const char *second,int force)
{
    if(!manager){
        char command[768];
        if(op==9)snprintf(command,sizeof(command),"C:zpool add %s public_reuse_target mirror %s %s",force?"-f":"",first,second);
        else snprintf(command,sizeof(command),"C:zpool %s %s public_reuse_target %s %s",op==11?"attach":"replace",force?"-f":"",guid,first);
        return cli_command(command);
    }
    static struct oz_message m,out;memset(&m,0,sizeof(m));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=op;m.flags=OZ_CONFIRMED;strcpy(m.target,"public_reuse_target");
    if(op!=9)snprintf(m.argument,sizeof(m.argument),"%s",guid);
    m.topology.group_count=1;m.topology.device_count=second?2:1;m.topology.ashift=12;
    m.topology.groups[0].layout=second?OZ_MIRROR:OZ_STRIPE;
    snprintf(m.topology.devices[0].path,sizeof(m.topology.devices[0].path),"%s",first);
    if(second)snprintf(m.topology.devices[1].path,sizeof(m.topology.devices[1].path),"%s",second);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);
    if(!error){do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);}
    if(!error&&out.status!=OZ_OK)error=EIO;
    if(ticket.number){int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;}
    return error;
}
static int storage_destroyed_changes(unsigned native,unsigned lane)
{
    const char *directory_name=native_file_fixture?"OZBACK:OZReuseChanges":"RAM:OZReuseChanges";
    char paths[5][96],command[768];
    for(unsigned i=0;i<5;i++)snprintf(paths[i],sizeof(paths[i]),"/%s/OZReuseChanges/disk%u",native_file_fixture?"OZBACK":"RAM",i);
    /* The native discovery fixture has two private 64 MiB MBR extents.
     * Exercise them as attach/replace candidates, then as an added mirror.
     * Remaining members are disposable RAM files; never clear native labels. */
    unsigned first=native==1||native>=3?0:3;
    if(native){
        snprintf(paths[first],sizeof(paths[first]),"/AROS/ata.device/0/partition/0");
        snprintf(paths[first+1],sizeof(paths[first+1]),"/AROS/ata.device/0/partition/2");
    }
    if(native>=3){
        const char *device=native==4?"ahci.device":"ata.device";
        unsigned unit=native==4?0:3;
        for(unsigned i=2;i<5;i++)snprintf(paths[i],sizeof(paths[i]),"/AROS/%s/%u/partition/%u",device,unit,i-2);
        struct oz_device_geometry geometry={0};
        int queried=oz_device_geometry_read(OZ_ADMISSION_PORT,device,unit,&geometry);
        if(queried||geometry.bytes!=UINT64_C(268435456)||geometry.sector_size!=512){
            char line[180];snprintf(line,sizeof(line),"Public all-native geometry FAIL device=%s unit=%u error=%d bytes=%llu sector=%u\n",device,unit,queried,(unsigned long long)geometry.bytes,geometry.sector_size);note(line);return EINVAL;
        }
        struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
        if(!catalog)return ENOMEM;
        int result=oz_partitions_read(OZ_ADMISSION_PORT,device,unit,catalog);
        int valid=!result&&catalog->count==3;
        const uint64_t starts[]={4097,139265,274433};
        for(unsigned i=0;valid&&i<3;i++)valid=catalog->entries[i].position==i&&
            catalog->entries[i].first_sector==starts[i]&&catalog->entries[i].last_sector==starts[i]+131071;
        if(!valid){char line[180];snprintf(line,sizeof(line),"Public all-native partitions FAIL device=%s error=%d count=%u\n",device,result,catalog->count);note(line);}
        free(catalog);if(!valid)return EINVAL;
    }
    BPTR file=0;void *zeros=AllocVec(1024*1024,MEMF_PUBLIC|MEMF_CLEAR);int error=0;
    char guid[32];
#define XCHECK(x) do{if(!(x)){char line[96];snprintf(line,sizeof(line),"Public destroyed changes FAIL line=%d\n",__LINE__);note(line);error=EIO;goto done;}}while(0)
    XCHECK(zeros);
    BPTR directory=CreateDir((CONST_STRPTR)directory_name);XCHECK(directory);UnLock(directory);
    for(int manager=0;manager<2;manager++){
        if(native&&manager!=lane)continue;
        for(unsigned i=0;i<5;i++){
            if(native>=3||(native&&(i==first||i==first+1)))continue;
            char path[80];snprintf(path,sizeof(path),"%s/disk%u",directory_name,i);
            file=Open((CONST_STRPTR)path,MODE_NEWFILE);XCHECK(file);
            for(unsigned n=0;n<64;n++)XCHECK(Write(file,zeros,1024*1024)==1024*1024);
            XCHECK(Flush(file));int closed=Close(file);file=0;XCHECK(closed);
        }
        snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -o ashift=12 -O mountpoint=none public_reuse_source mirror %s %s",paths[0],paths[1]);XCHECK(!cli_command(command));
        snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -o ashift=12 -O mountpoint=none public_reuse_target %s",paths[2]);XCHECK(!cli_command(command));
        snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -o ashift=12 -O mountpoint=none public_reuse_donor mirror %s %s",paths[3],paths[4]);XCHECK(!cli_command(command));
        XCHECK(!pool_root_payload("public_reuse_target",1,0));
        XCHECK(!reuse_change_guid(paths[2],guid,1));
        if(native>=2){
            XCHECK(reuse_change_request(manager,9,NULL,paths[3],paths[4],1)!=0);
            XCHECK(!cli_command("C:zpool export public_reuse_donor"));
            XCHECK(reuse_change_request(manager,9,NULL,paths[3],paths[4],1)!=0);
            XCHECK(!reuse_change_guid(paths[2],guid,1));
            XCHECK(!pool_root_payload("public_reuse_target",0,0));
            snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -d %s -d %s public_reuse_donor",paths[3],paths[4]);XCHECK(!cli_command(command));
        }
        for(unsigned exported=0;exported<2;exported++){
            if(exported)XCHECK(!cli_command("C:zpool export public_reuse_source"));
            XCHECK(reuse_change_request(manager,11,guid,paths[0],NULL,1)!=0);
            XCHECK(reuse_change_request(manager,13,guid,paths[0],NULL,1)!=0);
            XCHECK(reuse_change_request(manager,9,NULL,paths[0],paths[1],1)!=0);
            XCHECK(!reuse_change_guid(paths[2],guid,1));
            XCHECK(!pool_root_payload("public_reuse_target",0,0));
        }
        snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -d %s -d %s public_reuse_source",paths[0],paths[1]);XCHECK(!cli_command(command));
        XCHECK(!cli_command("C:zpool destroy public_reuse_source"));
        XCHECK(!reuse_change_request(manager,11,guid,paths[0],NULL,0));
        XCHECK(!cli_command("C:zpool wait -t resilver public_reuse_target"));
        XCHECK(!reuse_change_guid(paths[0],guid,2));
        XCHECK(!pool_root_payload("public_reuse_target",0,0));
        XCHECK(!reuse_change_request(manager,13,guid,paths[1],NULL,0));
        XCHECK(!cli_command("C:zpool wait -t resilver,replace public_reuse_target"));
        XCHECK(!reuse_change_guid(paths[1],guid,2));
        XCHECK(!pool_root_payload("public_reuse_target",0,0));
        XCHECK(!cli_command("C:zpool destroy public_reuse_donor"));
        XCHECK(!reuse_change_request(manager,9,NULL,paths[3],paths[4],0));
        XCHECK(!reuse_change_guid(paths[4],guid,4));
        XCHECK(!pool_root_payload("public_reuse_target",0,0));
        if(native>=3){
            XCHECK(!cli_command("C:zpool export public_reuse_target"));
            snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -d %s -d %s -d %s -d %s public_reuse_target public_native_target",paths[1],paths[2],paths[3],paths[4]);
            XCHECK(!cli_command(command));
            XCHECK(!pool_root_payload("public_native_target",0,0));
            XCHECK(!cli_command("C:zpool export public_native_target"));
            XCHECK(DeleteFile((CONST_STRPTR)directory_name));
            note("Public all-native changed writer PASS attach=1 replace=1 add=1 native-leaves=4 exact-read=1 exported=1\n");
            goto done;
        }
        if(manager&&native_file_fixture){
            XCHECK(!cli_command("C:zpool export public_reuse_target"));
            note("Public destroyed changes Manager PASS attach=1 replace=1 add=1 active-refused=3 exported-refused=3 exact-read=5 retained=1\n");
            goto done;
        }
        XCHECK(!cli_command("C:zpool destroy public_reuse_target"));
        for(unsigned i=0;i<5;i++){
            if(native&&(i==first||i==first+1))continue;
            char path[80];snprintf(path,sizeof(path),"%s/disk%u",directory_name,i);XCHECK(DeleteFile((CONST_STRPTR)path));
        }
        if(native){char line[160];snprintf(line,sizeof(line),"Public native changes PASS lane=%s candidates=%s exact-read=5 cleanup=1\n",manager?"Manager":"CLI",native==1?"attach-replace":"add");note(line);}
        else note(manager?"Public destroyed changes Manager PASS attach=1 replace=1 add=1 active-refused=3 exported-refused=3 exact-read=5 cleanup=1\n":
            "Public destroyed changes CLI PASS attach=1 replace=1 add=1 active-force-refused=3 exported-force-refused=3 exact-read=5 cleanup=1\n");
    }
    XCHECK(DeleteFile((CONST_STRPTR)directory_name));
done:
    if(file)Close(file);if(zeros)FreeVec(zeros);return error;
#undef XCHECK
}
static int storage_destroyed_cold(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    if(!native_file_fixture)return 0;
    const char *members[]={"OZBACK:OZReuseChanges/disk0","OZBACK:OZReuseChanges/disk2","OZBACK:OZReuseChanges/disk4"};
    const char *hidden[]={"OZBACK:OZReuseChanges/retired0","OZBACK:OZReuseChanges/held2","OZBACK:OZReuseChanges/held4"};
    unsigned moved=0;int error=0;
    for(unsigned phase=0;phase<3&&!error;phase++){
        if(phase==1){
            for(;moved<3;moved++){
                BPTR exists=Lock((CONST_STRPTR)hidden[moved],SHARED_LOCK);
                if(exists){UnLock(exists);error=EEXIST;break;}
                if(IoErr()!=ERROR_OBJECT_NOT_FOUND||!Rename((CONST_STRPTR)members[moved],(CONST_STRPTR)hidden[moved])){error=EIO;break;}
            }
        }
        if(!error)error=cli_command("C:zpool import -N -o cachefile=none -o readonly=on -d /OZBACK/OZReuseChanges/disk1 -d /OZBACK/OZReuseChanges/disk2 -d /OZBACK/OZReuseChanges/disk3 -d /OZBACK/OZReuseChanges/disk4 public_reuse_target")?EIO:0;
        if(!error){
            error=pool_root_payload("public_reuse_target",0,1);
            int exported=cli_command("C:zpool export public_reuse_target");
            if(!error&&exported)error=EIO;
        }
        if(phase==1){
            while(moved){--moved;if(!Rename((CONST_STRPTR)hidden[moved],(CONST_STRPTR)members[moved]))error=EIO;}
        }
        if(!error){char line[160];snprintf(line,sizeof(line),"Public changed pool cold PASS phase=%u exact-read=1 readonly-write-denied=1 exported=1\n",phase);note(line);}
    }
    while(moved){--moved;if(!Rename((CONST_STRPTR)hidden[moved],(CONST_STRPTR)members[moved]))error=EIO;}
    return error;
}
static int storage_file_reuse(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    const char *saved_directory=storage_directory;
    unsigned saved_members=storage_members,saved_layout=storage_layout;
    int saved_replaced=storage_replaced,error=0;BPTR file=0;
    void *zeros=AllocVec(1024*1024,MEMF_PUBLIC|MEMF_CLEAR);
    storage_directory="RAM:OZFileReuse";storage_members=2;storage_layout=OZ_MIRROR;storage_replaced=0;
#define RCHECK(x) do{if(!(x)){error=EIO;goto done;}}while(0)
    RCHECK(zeros);
    BPTR directory=CreateDir((CONST_STRPTR)storage_directory);RCHECK(directory);UnLock(directory);
    for(unsigned i=0;i<2;i++){
        char path[80];storage_member(path,sizeof(path),i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);RCHECK(file);
        for(unsigned n=0;n<64;n++)RCHECK(Write(file,zeros,1024*1024)==1024*1024);
        RCHECK(Flush(file));int closed=Close(file);file=0;RCHECK(closed);
    }
    RCHECK(!storage_request(5,"public_file_reuse",NULL,NULL));
    RCHECK(!pool_root_payload("public_file_reuse",1,0));
    RCHECK(!cli_command("C:zpool destroy public_file_reuse"));
    RCHECK(!file_reuse_label_denials());
    RCHECK(!cli_command("C:zpool create -o cachefile=none -O mountpoint=none public_file_reuse mirror /RAM/OZFileReuse/disk0 /RAM/OZFileReuse/disk1"));
    RCHECK(pool_root_payload("public_file_reuse",0,0)==EIO);
    RCHECK(!pool_root_payload("public_file_reuse",1,0));
    RCHECK(!pool_root_payload("public_file_reuse",0,0));
    RCHECK(!cli_command("C:zpool export public_file_reuse"));
    RCHECK(storage_request(5,"public_file_refused",NULL,NULL)!=0);
    RCHECK(cli_command("C:zpool create -f -o cachefile=none -O mountpoint=none public_file_refused mirror /RAM/OZFileReuse/disk0 /RAM/OZFileReuse/disk1")!=0);
    RCHECK(!storage_request(7,"public_file_reuse",NULL,"readonly=off"));
    RCHECK(!pool_root_payload("public_file_reuse",0,0));
    RCHECK(!cli_command("C:zpool destroy public_file_reuse"));
    RCHECK(!storage_request(5,"public_file_reuse",NULL,NULL));
    RCHECK(pool_root_payload("public_file_reuse",0,0)==EIO);
    RCHECK(!pool_root_payload("public_file_reuse",1,0));
    RCHECK(!pool_root_payload("public_file_reuse",0,0));
    RCHECK(!cli_command("C:zpool destroy public_file_reuse"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:OZFileReuse/disk0"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:OZFileReuse/disk1"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:OZFileReuse"));
    RCHECK(!storage_destroyed_changes(0,0));
done:
    if(file)Close(file);if(zeros)FreeVec(zeros);
    storage_directory=saved_directory;storage_members=saved_members;storage_layout=saved_layout;storage_replaced=saved_replaced;
    if(!error)note("Public file destroyed reuse PASS cli=1 manager-api=1 old-payload-absent=2 exact-read=3 exported-refused=2 cleanup=1\n");
    return error;
#undef RCHECK
}
/* Opt-in forward acceptance gate for upstream create -f semantics. Uses only
 * freshly allocated RAM files; never consumes a caller-supplied device path. */
static int storage_force_create(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-force-create-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    const char *saved_directory=storage_directory;
    unsigned saved_members=storage_members,saved_layout=storage_layout;
    int saved_replaced=storage_replaced,error=0;BPTR file=0;
    void *zeros=AllocVec(1024*1024,MEMF_PUBLIC|MEMF_CLEAR);
    storage_directory="RAM:OZForceCreate";storage_members=2;storage_layout=OZ_MIRROR;storage_replaced=0;
    const char *stage="fixture";
#define FCHECK(x) do{if(!(x)){error=EIO;goto done;}}while(0)
    FCHECK(zeros);
    /* CreateDir refuses a pre-existing fixture; no truncate/reuse fallback. */
    BPTR directory=CreateDir((CONST_STRPTR)storage_directory);FCHECK(directory);UnLock(directory);
    for(unsigned i=0;i<2;i++){
        char path[80];storage_member(path,sizeof(path),i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);FCHECK(file);
        for(unsigned n=0;n<64;n++)FCHECK(Write(file,zeros,1024*1024)==1024*1024);
        FCHECK(Flush(file));int closed=Close(file);file=0;FCHECK(closed);
    }
    stage="source-create";
    FCHECK(!cli_command("C:zpool create -o cachefile=none -O mountpoint=none public_force_source mirror /RAM/OZForceCreate/disk0 /RAM/OZForceCreate/disk1"));
    FCHECK(!pool_root_payload("public_force_source",1,0));
    stage="active-refusal";
    FCHECK(cli_command("C:zpool create -f -o cachefile=none -O mountpoint=none public_force_new mirror /RAM/OZForceCreate/disk0 /RAM/OZForceCreate/disk1")!=0);
    FCHECK(!pool_root_payload("public_force_source",0,0));
    FCHECK(!cli_command("C:zpool export public_force_source"));
    stage="no-force-refusal";
    FCHECK(cli_command("C:zpool create -o cachefile=none -O mountpoint=none public_force_new mirror /RAM/OZForceCreate/disk0 /RAM/OZForceCreate/disk1")!=0);
    FCHECK(!cli_command("C:zpool import -N -o cachefile=none -d /RAM/OZForceCreate/disk0 -d /RAM/OZForceCreate/disk1 public_force_source"));
    FCHECK(!pool_root_payload("public_force_source",0,0));
    FCHECK(!cli_command("C:zpool export public_force_source"));
    note("Public force-create prerequisites PASS active-refused=1 no-force-refused=1 source-payload-preserved=2\n");
    stage="exported-force-create";
    FCHECK(!cli_command("C:zpool create -f -o cachefile=none -O mountpoint=none public_force_new mirror /RAM/OZForceCreate/disk0 /RAM/OZForceCreate/disk1"));
    stage="new-pool-data";
    FCHECK(pool_root_payload("public_force_new",0,0)==EIO);
    FCHECK(!pool_root_payload("public_force_new",1,0));
    FCHECK(!cli_command("C:zpool export public_force_new"));
    FCHECK(!cli_command("C:zpool import -N -o cachefile=none -o readonly=on -d /RAM/OZForceCreate/disk0 -d /RAM/OZForceCreate/disk1 public_force_new"));
    FCHECK(!pool_root_payload("public_force_new",0,1));
    FCHECK(!cli_command("C:zpool export public_force_new"));
    FCHECK(DeleteFile((CONST_STRPTR)"RAM:OZForceCreate/disk0"));
    FCHECK(DeleteFile((CONST_STRPTR)"RAM:OZForceCreate/disk1"));
    FCHECK(DeleteFile((CONST_STRPTR)"RAM:OZForceCreate"));
done:
    if(file)Close(file);if(zeros)FreeVec(zeros);
    storage_directory=saved_directory;storage_members=saved_members;storage_layout=saved_layout;storage_replaced=saved_replaced;
    if(error){char line[160];snprintf(line,sizeof(line),"Public force-create FAIL stage=%s errno=%d\n",stage,error);note(line);}
    else note("Public force-create PASS active-refused=1 no-force-refused=1 exported-replaced=1 old-payload-absent=1 readonly-exact=1 cleanup=1\n");
    return error;
#undef FCHECK
}
static int native_reuse_corrupt_labels(const char *create_command,int conflict)
{
    /* Only called after the private runner's exact 256MiB MBR geometry and
     * partition extents have been verified. No physical device invocation. */
    const ULONG extent=64UL*1024*1024,base=4097UL*512,chunk=128UL*1024,label_bytes=112UL*1024;
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    unsigned char *before=AllocVec(extent,MEMF_PUBLIC),*check=AllocVec(chunk,MEMF_PUBLIC);
    unsigned char *saved=AllocVec(label_bytes,MEMF_PUBLIC),*replacement=AllocVec(label_bytes,MEMF_PUBLIC|MEMF_CLEAR);
    int error=0,opened=0,changed=0;ULONG damaged=0;
#define LCHECK(x) do{if(!(x)){error=EIO;goto done;}}while(0)
    LCHECK(io&&before&&check&&saved&&replacement);
    LCHECK(!OpenDevice((CONST_STRPTR)"ata.device",0,(struct IORequest *)io,0));opened=1;
    for(ULONG offset=0;offset<extent;offset+=chunk){
        io->io_Command=CMD_READ;io->io_Data=before+offset;io->io_Length=chunk;io->io_Offset=base+offset;
        LCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==chunk);
    }
    /* On-disk vdev labels are 256KiB; their nvlist follows 16KiB of pads.
     * Break each copy separately: three valid destroyed labels must not be
     * sufficient for destructive reuse when the fourth is undecodable. */
    for(unsigned label=0;label<4;label++){
        damaged=label*256UL*1024+(label<2?0:extent-4UL*256*1024)+16UL*1024;
        if(conflict){
            /* The destroyed neighbor's same-position label has a valid
             * offset checksum but a different pool/member identity. */
            io->io_Command=CMD_READ;io->io_Data=replacement;io->io_Length=label_bytes;
            io->io_Offset=139265UL*512+damaged;
            LCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==label_bytes);
        }
        memcpy(saved,before+damaged,label_bytes);memcpy(before+damaged,replacement,label_bytes);changed=1;
        io->io_Command=CMD_WRITE;io->io_Data=before+damaged;io->io_Length=label_bytes;io->io_Offset=base+damaged;
        LCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==label_bytes);
        LCHECK(cli_command(create_command)!=0);
        for(ULONG offset=0;offset<extent;offset+=chunk){
            io->io_Command=CMD_READ;io->io_Data=check;io->io_Length=chunk;io->io_Offset=base+offset;
            LCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==chunk&&!memcmp(check,before+offset,chunk));
        }
        memcpy(before+damaged,saved,label_bytes);
        io->io_Command=CMD_WRITE;io->io_Data=saved;io->io_Length=label_bytes;io->io_Offset=base+damaged;
        LCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==label_bytes);changed=0;
    }
done:
    if(opened&&changed){
        io->io_Command=CMD_WRITE;io->io_Data=saved;io->io_Length=label_bytes;io->io_Offset=base+damaged;
        if(DoIO((struct IORequest *)io)||io->io_Actual!=label_bytes)error=EIO;
    }
    if(opened)CloseDevice((struct IORequest *)io);
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    if(before)FreeVec(before);if(check)FreeVec(check);
    if(saved)FreeVec(saved);if(replacement)FreeVec(replacement);
    if(!error)note(conflict?
        "Public native conflicting reuse PASS labels=4 create-denied=4 full-extent-unchanged=4 restored=4\n":
        "Public native corrupt reuse PASS labels=4 create-denied=4 full-extent-unchanged=4 restored=4\n");
    if(!error&&conflict)note("Public native conflicting label offsets PASS same-position=4\n");
    return error;
#undef LCHECK
}
static int storage_native_discovery(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-native-discovery-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    marker=Lock((CONST_STRPTR)"SYS:openzfs-native-cold-test",SHARED_LOCK);
    int retain=marker!=0;if(marker)UnLock(marker);
    /* Runner supplies only fresh snapshot-backed virtual disks. Use its
     * explicit MBR extent; implicit whole-disk provisioning is not allowed. */
    struct oz_device_geometry geometry={0};
    int error=oz_device_geometry_read(OZ_ADMISSION_PORT,"ata.device",0,&geometry);
    if(error||geometry.bytes!=UINT64_C(268435456)||geometry.sector_size!=512)return EINVAL;
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog)return ENOMEM;
    error=oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog);
    int valid=!error&&catalog->count==2&&catalog->entries[0].position==0&&
        catalog->entries[0].first_sector==4097&&catalog->entries[0].last_sector==135168&&
        catalog->entries[1].position==2&&catalog->entries[1].first_sector==139265&&
        catalog->entries[1].last_sector==270336;
    free(catalog);if(!valid)return EINVAL;
    unsigned char key[32];for(unsigned i=0;i<sizeof(key);i++)key[i]=(unsigned char)(i+1);
    BPTR keyfile=Open((CONST_STRPTR)"RAM:discovery-root.key",MODE_NEWFILE);
    if(!keyfile)return EIO;
    LONG written=Write(keyfile,key,sizeof(key));int closed=Close(keyfile);
    if(written!=sizeof(key)||!closed)return EIO;
    memset(key,0x7a,sizeof(key));
    keyfile=Open((CONST_STRPTR)"RAM:discovery-wrong.key",MODE_NEWFILE);
    if(!keyfile)return EIO;
    written=Write(keyfile,key,sizeof(key));closed=Close(keyfile);
    if(written!=sizeof(key)||!closed)return EIO;
    /* Keep a different writable pool active on the other extent throughout
     * discovery/import. An unrelated scan candidate must not be reserved or
     * accidentally exported/adopted as part of the selected pool. */
    error=cli_result("C:zpool create -o cachefile=none -O mountpoint=none public_neighbor /AROS/ata.device/0/partition/2 >T:cli-package-output.txt", "");
    if(!error)error=cli_result("C:zfs set org.openzfs:discovery=preserved public_neighbor >T:cli-package-output.txt", "");
    if(error)return error;
    const char *commands[]={
        "C:zpool create -o cachefile=none -O mountpoint=none -O encryption=aes-256-gcm -O keyformat=raw -O keylocation=file:///RAM/discovery-root.key -O compression=lz4 -O casesensitivity=mixed -O normalization=formD public_discovery /AROS/ata.device/0/partition/0",
        "C:zpool export public_discovery",
        "C:zpool import",
        "C:zpool import -N -o cachefile=none -o readonly=on public_discovery",
        "C:zpool get -H -o value readonly public_discovery",
        "C:zpool export public_discovery",
        "C:zpool import -N -o cachefile=none public_discovery",
        "C:zpool destroy public_discovery"
    };
    for(unsigned i=0;i<sizeof(commands)/sizeof(commands[0]);i++){
        char command[1024];snprintf(command,sizeof(command),"%s >T:cli-package-output.txt",
            retain&&i==7?"C:zpool export public_discovery":commands[i]);
        if(i==2){
            error=cli_command(command)?EIO:0;
            BPTR output=Open((CONST_STRPTR)"T:cli-package-output.txt",MODE_OLDFILE);
            if(!output)error=EIO;
            else{
                char text[2048]={0};LONG count=Read(output,text,sizeof(text)-1);
                if(!Close(output)||count<=0||!strstr(text,"public_discovery"))error=EIO;
            }
        }else error=cli_result(command,i==4?"on":"");
        diagnostic_log("T:cli-package-output.txt");
        if(!error&&(i==0||i==1)){
            /* Force must not bypass active reservations or the service's
             * exported-member policy. Subsequent exact reads prove retention. */
            if(!cli_command("C:zpool create -f -o cachefile=none -O mountpoint=none public_reuse_denied /AROS/ata.device/0/partition/0"))error=EIO;
        }
        if(!error&&(i==3||i==6)){
            error=cli_result("C:zfs get -H -o value keystatus public_discovery >T:cli-package-output.txt","unavailable");
            if(!error&&!cli_command("C:zfs load-key -L file:///RAM/discovery-wrong.key public_discovery"))error=EIO;
            if(!error)error=cli_result("C:zfs get -H -o value keystatus public_discovery >T:cli-package-output.txt","unavailable");
            if(!error){
                int mounted=oz_mount_control(OZ_ADMISSION_PORT,"public_discovery","OZDISCOVERY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
                if(!mounted){oz_mount_control(OZ_ADMISSION_PORT,"public_discovery","",OZ_MOUNT_DETACH,0);error=EIO;}
                else if(mounted!=EACCES)error=mounted;
            }
            if(!error)error=cli_command("C:zfs load-key public_discovery")?EIO:0;
        }
        if(!error&&(i==0||i==3||i==6)){
            const char *props[]={"encryption","keyformat","encryptionroot","compression","casesensitivity","normalization"};
            const char *values[]={"aes-256-gcm","raw","public_discovery","lz4","mixed","formD"};
            for(unsigned p=0;p<sizeof(props)/sizeof(props[0])&&!error;p++){
                snprintf(command,sizeof(command),"C:zfs get -H -o value %s public_discovery >T:cli-package-output.txt",props[p]);
                error=cli_result(command,values[p]);
            }
        }
        if(!error&&(i==0||i==3||i==6))error=pool_root_payload("public_discovery",i==0,i==3);
        if(error){char line[100];snprintf(line,sizeof(line),"Public native discovery FAIL step=%u errno=%d\n",i,error);note(line);return error;}
    }
    if(!retain){
        /* Reuse the exact destroyed extent, never clear it in the harness.
         * A new root must not expose the previous pool's payload. */
        error=native_reuse_corrupt_labels(commands[0],0);
        if(!error)error=cli_command(commands[0])?EIO:0;
        if(!error&&pool_root_payload("public_discovery",0,0)!=EIO)error=EIO;
        if(!error)error=pool_root_payload("public_discovery",1,0);
        if(!error)error=pool_root_payload("public_discovery",0,0);
        if(!error)error=cli_command("C:zpool destroy public_discovery")?EIO:0;
        if(error)return error;
        note("Public native destroyed reuse PASS same-extent=1 no-force=1 old-payload-absent=1 new-write=1 destroyed=1\n");
    }
    error=cli_result("C:zpool list -H -o health public_neighbor >T:cli-package-output.txt", "ONLINE");
    if(!error)error=cli_result("C:zfs get -H -o value org.openzfs:discovery public_neighbor >T:cli-package-output.txt", "preserved");
    if(!error)error=cli_result("C:zpool destroy public_neighbor >T:cli-package-output.txt", "");
    if(error)return error;
    if(!retain){
        error=native_reuse_corrupt_labels(commands[0],1);
        if(!error)error=cli_command(commands[0])?EIO:0;
        if(!error)error=pool_root_payload("public_discovery",1,0);
        if(!error)error=pool_root_payload("public_discovery",0,0);
        if(!error)error=cli_command("C:zpool destroy public_discovery")?EIO:0;
        if(error)return error;
        note("Public native conflicting restore PASS recreate=1 exact-data=1 destroyed=1\n");
    }
    if(!DeleteFile((CONST_STRPTR)"RAM:discovery-root.key"))return EIO;
    if(!DeleteFile((CONST_STRPTR)"RAM:discovery-wrong.key"))return EIO;
    note("Public native reuse guards PASS active-force-denied=1 exported-force-denied=1 original-data-preserved=1\n");
    if(retain){
        note("Public native cold writer PASS encrypted-root=1 exact-write=1 detached=1 exported=1\n");
        note("Public native cold retained PASS properties=6 locked-import=2 wrong-key-denied=2 key-reload=2 exact-data=1 neighbor-cleanup=1\n");
        return 0;
    }
    note("Public native discovery PASS create=1 default-scan=1 readonly-import=1 rw-import=1 destroy=1\n");
    note("Public native discovery coexistence PASS separate-partitions=1 neighbor-online=1 neighbor-property=1 cleanup=1\n");
    note("Public native discovery data PASS handler-write=1 readonly-import-read=1 readonly-write-denied=1 writable-import-read=1 cleanup=1\n");
    note("Public native encrypted root PASS properties=6 locked-import=2 mount-denied=2 key-reload=2 exact-data=1 cleanup=1\n");
    note("Public native encrypted root wrong key PASS denied=2 remains-locked=2 mount-denied=2 correct-recovery=2\n");
    /* A replaced member retains active historical labels. Each native lane
     * therefore gets a fresh disposable VM fixture, not a harness label wipe. */
    for(unsigned fixture=1;fixture<=6;fixture++){
        char path[80];snprintf(path,sizeof(path),"SYS:openzfs-native-changes-%u",fixture);
        marker=Lock((CONST_STRPTR)path,SHARED_LOCK);
        if(marker){UnLock(marker);error=storage_destroyed_changes(fixture>=5?fixture-2:fixture<=2?1:2,fixture>=5?1:(fixture-1)%2);if(error)return error;}
    }
    return 0;
}
static int storage_native_cold(int create)
{
    BPTR changed=Lock((CONST_STRPTR)"SYS:openzfs-native-changes-5",SHARED_LOCK);
    int ahci=0;
    if(!changed){changed=Lock((CONST_STRPTR)"SYS:openzfs-native-changes-6",SHARED_LOCK);ahci=changed!=0;}
    if(changed){
        UnLock(changed);if(create)return 0;
        BPTR missing=Lock((CONST_STRPTR)"SYS:openzfs-native-missing-ata",SHARED_LOCK);
        int degraded=missing!=0;if(missing)UnLock(missing);
        BPTR mode=Lock((CONST_STRPTR)"SYS:openzfs-native-write",SHARED_LOCK);
        int write=mode!=0;if(mode)UnLock(mode);
        mode=Lock((CONST_STRPTR)"SYS:openzfs-native-heal",SHARED_LOCK);
        int heal=mode!=0;if(mode)UnLock(mode);
        mode=Lock((CONST_STRPTR)"SYS:openzfs-native-verify",SHARED_LOCK);
        int verify=mode!=0;if(mode)UnLock(mode);
        int readonly=!write&&!heal;
        char command[768];const char *device=ahci?"ahci.device":"ata.device";unsigned unit=ahci?0:3;
        /* Authorize every known member path, including the absent unit.
         * Absence is pinned explicitly; labels cannot authorize extra paths. */
        snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -o readonly=%s -d /AROS/ata.device/0/partition/2 -d /AROS/%s/%u/partition/0 -d /AROS/%s/%u/partition/1 -d /AROS/%s/%u/partition/2 public_native_target",readonly?"on":"off",device,unit,device,unit,device,unit);
        int error=cli_command(command)?EIO:0;
        if(!error&&heal)error=cli_command("C:zpool wait -t resilver public_native_target")?EIO:0;
        if(!error&&heal){
            struct oz_client client;static struct oz_message request;
            if(!oz_client_open(&client))error=ENOMEM;
            else{
                memset(&request,0,sizeof(request));request.operation=4;
                strcpy(request.target,"public_native_target");
                if(oz_submit(&client,&request)!=OZ_OK)error=EIO;
                else{
                    struct oz_message *reply=NULL;
                    while(!(reply=oz_poll(&client)))Delay(1);
                    const char *scan=strstr(reply->detail,"scan=resilver state=finished ");
                    unsigned long long examined=0,total=0,issued=0,errors=1;
                    if(reply->status!=OZ_OK||!scan||sscanf(scan,
                        "scan=resilver state=finished examined=%llu to-examine=%llu issued=%llu errors=%llu",
                        &examined,&total,&issued,&errors)!=4||!issued||errors)error=EIO;
                    if(!error){note("Public native healing scan PASS ");note(scan);note("\n");}
                    oz_message_free(reply);
                }
                oz_client_close(&client);
            }
        }
        if(!error)error=cli_result("C:zpool list -H -o health public_native_target >T:cli-package-output.txt",(degraded||verify)?"DEGRADED":"ONLINE");
        if(!error)error=pool_root_payload("public_native_target",0,readonly);
        if(!error&&(write||heal||verify)){
            error=oz_mount_control(OZ_ADMISSION_PORT,"public_native_target","OZNATIVEREPAIR",OZ_MOUNT_ATTACH,
                readonly?OZ_MOUNT_READONLY:0);
            if(!error){
                error=storage_repair_data("OZNATIVEREPAIR:",write);
                int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_native_target","",OZ_MOUNT_DETACH,0);
                if(!error)error=detached;
            }
        }
        if(!error&&write)error=cli_command("C:zpool sync public_native_target")?EIO:0;
        if(!error&&heal){
            /* Persist peer isolation for the next fresh read-only VM. A
             * same-VM read could be satisfied from ARC instead of the disk. */
            snprintf(command,sizeof(command),"C:zpool offline public_native_target /AROS/%s/%u/partition/0",device,unit);
            error=cli_command(command)?EIO:0;
            if(!error)error=cli_result("C:zpool list -H -o health public_native_target >T:cli-package-output.txt","DEGRADED");
            if(!error)note("Public native restored member isolation PASS peer-offline=1\n");
        }
        if(!error)error=cli_command("C:zpool export public_native_target")?EIO:0;
        if(!error&&(write||heal||verify)){
            note(write?"Public native degraded write PASS bytes=1048576 synced=1 exported=1\n":
                heal?"Public native restored healing PASS issued-nonzero=1 errors=0 exact-data=1 exported=1\n":
                "Public native healed cold PASS exact-data=1 readonly-write-denied=1 exported=1\n");
        }else if(!error)note(degraded?
            "Public all-native changed reader PASS degraded=1 exact-read=1 readonly-write-denied=1 exported=1\n":
            "Public all-native changed reader PASS online=1 exact-read=1 readonly-write-denied=1 exported=1\n");
        return error;
    }
    /* This fixture is only enabled by the isolated runner. The writer exports
     * cleanly; the reader runs in a different VM with no retained ARC or keys. */
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-native-cold-test",SHARED_LOCK);
    if(!marker)return create?0:EINVAL;
    UnLock(marker);
    if(create)return 0; /* storage_native_discovery retained the exported pool. */
    unsigned char key[32];for(unsigned i=0;i<sizeof(key);i++)key[i]=(unsigned char)(i+1);
    BPTR file=Open((CONST_STRPTR)"RAM:discovery-root.key",MODE_NEWFILE);
    if(!file)return EIO;
    LONG written=Write(file,key,sizeof(key));int closed=Close(file);
    if(written!=sizeof(key)||!closed)return EIO;
    int error=cli_command("C:zpool import -N -o cachefile=none -o readonly=on public_discovery")?EIO:0;
    if(!error&&!create){
        error=cli_result("C:zfs get -H -o value keystatus public_discovery >T:cli-package-output.txt","unavailable");
        if(!error){
            int mounted=oz_mount_control(OZ_ADMISSION_PORT,"public_discovery","OZDISCOVERY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
            if(!mounted){oz_mount_control(OZ_ADMISSION_PORT,"public_discovery","",OZ_MOUNT_DETACH,0);error=EIO;}
            else if(mounted!=EACCES)error=mounted;
        }
        if(!error)error=cli_command("C:zfs load-key public_discovery")?EIO:0;
        if(!error)error=cli_result("C:zpool get -H -o value readonly public_discovery >T:cli-package-output.txt","on");
    }
    if(!error)error=pool_root_payload("public_discovery",0,1);
    if(!error)error=cli_command("C:zpool export public_discovery")?EIO:0;
    if(!DeleteFile((CONST_STRPTR)"RAM:discovery-root.key")&&!error)error=EIO;
    if(!error)note(create?
        "Public native cold writer PASS encrypted-root=1 exact-write=1 detached=1 exported=1\n":
        "Public native cold reader PASS locked=1 mount-denied=1 key-loaded=1 exact-read=1 readonly-write-denied=1 exported=1\n");
    return error;
}
static int destroy_identity(const char *snapshot,unsigned flags,int permitted)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOENT;
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    int error=0;
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))error=EACCES;
    if(!error){
        int result=storage_request_flags(38,snapshot,NULL,"defer=on",flags);
        if(!storage_reply_received||(permitted?result!=0:result!=EIO))error=EACCES;
    }
    /* Restore root even if the request or assertion failed. */
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;
    CloseLibrary(secBase);
    return error;
}
static int key_request_identity(unsigned op,const char *target,const char *path,
    const char *options,unsigned flags,int permitted)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOENT;
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    int error=0;
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))error=EACCES;
    if(!error){
        int result=storage_request_flags(op,target,path,options,flags);
        if(!storage_reply_received||(permitted?result!=0:result!=EIO))error=EACCES;
    }
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;
    CloseLibrary(secBase);
    return error;
}
static int key_check_file_identity(const char *path,int permitted)
{return key_request_identity(62,"public_mirror/encrypted",path,NULL,OZ_CONFIRMED|OZ_DRYRUN,permitted);}
static int key_change_identity(const char *path,const char *options,int permitted)
{return key_request_identity(64,"public_mirror/check_hex",path,options,OZ_CONFIRMED,permitted);}
static int key_check_identity(int permitted)
{return key_check_file_identity("RAM:public-test.key",permitted);}
static int clone_worker_identity(const char *destination,int permitted)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOENT;
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    int error=0;struct oz_project_worker *worker=NULL;
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))error=EACCES;
    struct oz_message request={0};
    request.magic=OZ_MAGIC;request.version=OZ_VERSION;request.msg.mn_Length=sizeof(request);
    request.operation=52;request.flags=OZ_CONFIRMED;
    strcpy(request.target,"public_mirror/files@cli_roundtrip");
    snprintf(request.argument,sizeof(request.argument),"%s",destination);
    strcpy(request.options,"compression=off;mountpoint=none");
    if(!error)error=oz_project_worker_start(&request,&worker);
    if(!error){
        error=EAGAIN;
        for(unsigned tick=0;tick<1500&&error==EAGAIN;tick++){
            error=oz_project_worker_poll(worker,&request);
            if(error==EAGAIN)Delay(1);
        }
        if(!error){
            error=oz_project_worker_free(worker);worker=NULL;
            if(!error&&(request.status!=(permitted?OZ_OK:OZ_ENGINE_ERROR)||
               !strstr(request.detail,permitted?"Upstream zfs clone: exit=0":
                   "Upstream zfs clone: exit=1")))error=EACCES;
        }
    }
    /* Restore the fixture identity even when the denial assertion fails. */
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;
    CloseLibrary(secBase);
    if(error)bug("Public clone identity worker permitted=%d errno=%d detail=%s\n",permitted,error,request.detail);
    return error;
}
static int storage_test(int gui){
    int error=0;BPTR file=0,lock=CreateDir((CONST_STRPTR)storage_directory);
    if(!lock)return EIO;
    char location[256];
    if(NameFromLock(lock,(STRPTR)location,sizeof(location))){note("Public fixture directory: ");note(location);note("\n");}
    UnLock(lock);
    void *zeros=AllocVec(1024*1024,MEMF_ANY|MEMF_CLEAR);if(!zeros)return ENOMEM;
    for(unsigned i=0;i<storage_members&&!error;i++){
        char path[64];snprintf(path,sizeof(path),"%s/disk%u",storage_directory,i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);
        if(!file){error=EIO;break;}
        for(unsigned n=0;n<128&&!error;n++)if(Write(file,zeros,1024*1024)!=1024*1024)error=EIO;
        if(!Flush(file)&&!error)error=EIO;
        if(!Close(file)&&!error)error=EIO;
        file=0;
    }
    FreeVec(zeros);if(error)return error;
    BPTR alias=Lock((CONST_STRPTR)storage_directory,SHARED_LOCK);
    if(!alias)return EIO;
    if(!AssignLock((CONST_STRPTR)"OZPOOLALIAS",alias)){UnLock(alias);return EIO;}
    char duplicate[80];snprintf(duplicate,sizeof(duplicate),"%s/disk0",storage_directory);
    error=pool_file_refused("OZPOOLALIAS:disk0",duplicate,EEXIST);
    if(!AssignLock((CONST_STRPTR)"OZPOOLALIAS",0)&&!error)error=EIO;
    if(error)return error;
    note("Public pool alias identity PASS duplicate-refused=1\n");
#define STORAGE_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    STORAGE_CHECK(!companion_tools_test());
    STORAGE_CHECK(!verify_mounts(NULL,0));
    STORAGE_CHECK(!partition_rpc_test());
    /* Blank-label assertions must precede deliberate pool creation. Destroy
     * retains destroyed-pool labels, so discovery is not a blanking step. */
    STORAGE_CHECK(!storage_native_discovery());
    STORAGE_CHECK(!storage_file_reuse());
    STORAGE_CHECK(!storage_force_create());
    char engine_version[128],small_version[1]={'x'};
    STORAGE_CHECK(!oz_engine_version_query(OZ_ADMISSION_PORT,engine_version,sizeof(engine_version)));
    STORAGE_CHECK(!strncmp(engine_version,"zfs-",4));
    char *reported_version=zfs_version_kernel();
    if(!reported_version){error=__LINE__;goto done;}
    int same_version=!strcmp(engine_version,reported_version);
    free(reported_version);
    STORAGE_CHECK(same_version);
    note("Public native engine version value=");note(engine_version);note("\n");
    STORAGE_CHECK(oz_engine_version_query(OZ_ADMISSION_PORT,small_version,sizeof(small_version))==ENOSPC&&!small_version[0]);
    STORAGE_CHECK(oz_engine_version_query(OZ_ADMISSION_PORT,NULL,0)==EINVAL);
    engine_version[0]='x';
    STORAGE_CHECK(oz_engine_version_query("OpenZFS.Test.Missing",engine_version,sizeof(engine_version))==ENOENT&&!engine_version[0]);
    note("Public native engine version PASS query=1 upstream=1 bounds=1 missing=1\n");
    int supported=0;
    STORAGE_CHECK(!oz_support_query(OZ_ADMISSION_PORT,"features.pool","org.illumos:lz4_compress",&supported)&&supported);
    STORAGE_CHECK(!oz_support_query(OZ_ADMISSION_PORT,"properties.dataset","compression",&supported)&&supported);
    STORAGE_CHECK(!oz_support_query(OZ_ADMISSION_PORT,"properties.pool","ashift",&supported)&&supported);
    STORAGE_CHECK(!oz_support_query(OZ_ADMISSION_PORT,"properties.vdev","path",&supported)&&supported);
    STORAGE_CHECK(!oz_support_query(OZ_ADMISSION_PORT,"features.pool","not.a:feature",&supported)&&!supported);
    STORAGE_CHECK(oz_support_query(OZ_ADMISSION_PORT,"unknown.scope","name",&supported)==ENOTSUP&&!supported);
    STORAGE_CHECK(oz_support_query("OpenZFS.Test.Missing","features.pool","org.illumos:lz4_compress",&supported)==ENOENT&&!supported);
    note("Public native supported tables PASS feature=1 properties=3 unknown=1 missing=1\n");
#ifdef OZ_TEST_NATIVE_CPU
    int cpus=aros_zfs_ncpus();
    STORAGE_CHECK(cpus>0);
    for(unsigned i=0;i<128;i++)STORAGE_CHECK(aros_zfs_ncpus()==cpus);
    char cpu_result[96];snprintf(cpu_result,sizeof(cpu_result),
        "Public native CPU topology PASS cpus=%d queries=128\n",cpus);note(cpu_result);
#endif
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_init_test());
#endif
    STORAGE_CHECK(!storage_alias_request(5,0));
    STORAGE_CHECK(!pool_root_payload("public_mirror",1,0));
    note("Public pool root payload PASS written=1 flushed=1 detached=1\n");
#ifdef OZ_NATIVE_DISPATCHER
    STORAGE_CHECK(!oz_public_ioctl_test("public_mirror"));
    if(native_file_fixture)STORAGE_CHECK(!oz_public_bootenv_persistence_test("public_mirror",1));
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_mount_test());
    STORAGE_CHECK(!oz_upstream_promotion_readonly_test(0));
    STORAGE_CHECK(!oz_upstream_file_vdev_test(0,storage_directory));
#endif
#else
    STORAGE_CHECK(!storage_request(37,"public_mirror/files",NULL,NULL));
#endif
    STORAGE_CHECK(!companion_mount_test());
    STORAGE_CHECK(!cli_filesystem_upgrade(0));
    STORAGE_CHECK(!cli_filesystem_upgrade(1));
    STORAGE_CHECK(!companion_import_test(1));
    STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","bad:name",OZ_MOUNT_ATTACH,0)==EINVAL);
    STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZPUBLIC",OZ_MOUNT_ATTACH,0));
#ifdef OZ_NATIVE_DISPATCHER
    STORAGE_CHECK(!oz_public_accounting_upgrade_test("public_mirror/files",0));
    note("Public accounting mounted upgrade PASS writable=1 repeat=1\n");
#endif
    STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZDUPLICATE",OZ_MOUNT_ATTACH,0)==EEXIST);
    {
        BPTR busy=Lock((CONST_STRPTR)"OZPUBLIC:",ACCESS_READ);
        STORAGE_CHECK(busy!=0);
        int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
        UnLock(busy);
        STORAGE_CHECK(detached==EBUSY);
    }
    STORAGE_CHECK(!verify_mounts("OZPUBLIC:",0));
    UQUAD shares_object=~(UQUAD)0;
    STORAGE_CHECK(!oz_mount_shares_query(OZ_ADMISSION_PORT,"public_mirror/files",&shares_object)&&!shares_object);
    shares_object=~(UQUAD)0;
    STORAGE_CHECK(oz_mount_shares_query(OZ_ADMISSION_PORT,"public_mirror/unmounted",&shares_object)==ENOENT&&!shares_object);
    shares_object=~(UQUAD)0;
    STORAGE_CHECK(oz_mount_shares_query("OpenZFS.Test.Missing","public_mirror/files",&shares_object)==ENOENT&&!shares_object);
    STORAGE_CHECK(oz_mount_shares_query(OZ_ADMISSION_PORT,NULL,&shares_object)==EINVAL&&!shares_object);
    note("Public native shares object PASS absent=1 unmounted=1 missing=1 invalid=1\n");
    ULONG supported_policy=0,enabled_policy=0,initial_policy=0;
    STORAGE_CHECK(!oz_mount_policy_query(OZ_ADMISSION_PORT,"public_mirror/files",&supported_policy,&initial_policy));
    STORAGE_CHECK(supported_policy==OZ_POLICY_KNOWN);
    const struct {const char *name;ULONG flag;} policies[]={
        {"readonly",OZ_POLICY_READONLY},{"atime",OZ_POLICY_ATIME},
        {"relatime",OZ_POLICY_RELATIME},{"exec",OZ_POLICY_EXEC}
    };
    for(unsigned i=0;i<sizeof(policies)/sizeof(policies[0]);i++) {
        for(unsigned value=0;value<2;value++) {
            char setting[40];snprintf(setting,sizeof(setting),"%s=%s",policies[i].name,value?"on":"off");
            STORAGE_CHECK(!storage_request(41,"public_mirror/files",setting,NULL));
            STORAGE_CHECK(!oz_mount_policy_query(OZ_ADMISSION_PORT,"public_mirror/files",&supported_policy,&enabled_policy));
            STORAGE_CHECK(!!(enabled_policy&policies[i].flag)==value);
#ifdef OZ_TEST_UPSTREAM_INIT
            STORAGE_CHECK(!oz_upstream_mount_policy_test());
#endif
        }
        char restore[40];snprintf(restore,sizeof(restore),"%s=%s",policies[i].name,
            (initial_policy&policies[i].flag)?"on":"off");
        STORAGE_CHECK(!storage_request(41,"public_mirror/files",restore,NULL));
    }
    STORAGE_CHECK(oz_mount_policy_query(OZ_ADMISSION_PORT,"public_mirror/unmounted",&supported_policy,&enabled_policy)==ENOENT&&!supported_policy&&!enabled_policy);
    STORAGE_CHECK(oz_mount_policy_query("OpenZFS.Test.Missing","public_mirror/files",&supported_policy,&enabled_policy)==ENOENT&&!supported_policy&&!enabled_policy);
    note("Public native mount policy PASS properties=4 transitions=8 missing=1 absent=1\n");
    STORAGE_CHECK(!storage_loader_test(!!(initial_policy&OZ_POLICY_EXEC)));
    const char *payload=storage_payload;char actual[sizeof(storage_payload)+1];
    file=Open((CONST_STRPTR)"OZPUBLIC:hello.txt",MODE_NEWFILE);STORAGE_CHECK(file);
    STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
    STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZPUBLIC:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
    STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
    STORAGE_CHECK(Close(file));file=0;
    STORAGE_CHECK(!storage_sparse64("OZPUBLIC:",1));
    STORAGE_CHECK(!storage_links("OZPUBLIC:",1));
    STORAGE_CHECK(!storage_metadata("OZPUBLIC:",1));
    STORAGE_CHECK(!storage_notifications());
    STORAGE_CHECK(!storage_signal_notifications());
    STORAGE_CHECK(!desktop_directory("OZPUBLIC:","post-sparse64"));
    {
        char dataset[256];
        STORAGE_CHECK(!oz_mounts_dataset_for_path(OZ_ADMISSION_PORT,"OZPUBLIC:hello.txt",dataset,sizeof(dataset)));
        STORAGE_CHECK(!strcmp(dataset,"public_mirror/files"));
        STORAGE_CHECK(oz_mounts_dataset_for_path(OZ_ADMISSION_PORT,"OZPUBLIC:hello.txt",dataset,2)==ENAMETOOLONG&&!dataset[0]);
        STORAGE_CHECK(oz_mounts_dataset_for_path(OZ_ADMISSION_PORT,"OZPUBLIC:absent",dataset,sizeof(dataset))==ENOENT&&!dataset[0]);
        STORAGE_CHECK(oz_mounts_dataset_for_path(OZ_ADMISSION_PORT,"RAM:",dataset,sizeof(dataset))==ENODEV&&!dataset[0]);
        STORAGE_CHECK(oz_mounts_dataset_for_path("OpenZFS.Test.Missing.Path.Service","OZPUBLIC:",dataset,sizeof(dataset))==ENOENT&&!dataset[0]);
        BPTR alias=Lock((CONST_STRPTR)"OZPUBLIC:",SHARED_LOCK);
        STORAGE_CHECK(alias);
        int assigned=AssignLock((CONST_STRPTR)"OZPUBLICALIAS",alias);
        if(!assigned)UnLock(alias);
        STORAGE_CHECK(assigned);
        STORAGE_CHECK(!pool_file_refused("OZPUBLICALIAS:hello.txt","OZPUBLICALIAS:other",EDEADLK));
        note("Public pool self-assign PASS deadlock-refused=1 responsive=1\n");
        int resolved=oz_mounts_dataset_for_path(OZ_ADMISSION_PORT,"OZPUBLICALIAS:hello.txt",dataset,sizeof(dataset));
        struct oz_mount_record root_record;
        int root_result=oz_mounts_record_for_root(OZ_ADMISSION_PORT,"OZPUBLICALIAS:",&root_record);
        int removed=AssignLock((CONST_STRPTR)"OZPUBLICALIAS",0);
        STORAGE_CHECK(!resolved&&removed&&!strcmp(dataset,"public_mirror/files"));
        STORAGE_CHECK(!root_result&&!strcmp(root_record.dataset,"public_mirror/files")&&
            !strcmp(root_record.mountpoint,"OZPUBLIC:")&&!root_record.flags);
        STORAGE_CHECK(oz_mounts_record_for_root(OZ_ADMISSION_PORT,"OZPUBLIC:hello.txt",&root_record)==ENODEV&&!root_record.dataset[0]);
        STORAGE_CHECK(oz_mounts_record_for_root(OZ_ADMISSION_PORT,"RAM:",&root_record)==ENODEV&&!root_record.dataset[0]);
        STORAGE_CHECK(oz_mounts_record_for_root(OZ_ADMISSION_PORT,"OZPUBLIC:missing-file",&root_record)==ENOENT&&!root_record.dataset[0]);
        note("Public native root resolution PASS alias=1 file-rejected=1 unrelated=1 absent=1\n");
        note("Public native path dataset PASS file=1 alias=1 unrelated=1 absent=1 bounds=1\n");
    }
    BPTR cli_marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(cli_marker){
        UnLock(cli_marker);
        STORAGE_CHECK(!storage_zvol_test(1));
        STORAGE_CHECK(!storage_device_test(1));
        STORAGE_CHECK(!storage_zvol_filesystem(1));
        if(native_file_fixture&&storage_layout==OZ_MIRROR){
            STORAGE_CHECK(!storage_repair_test());
            if(storage_replace)STORAGE_CHECK(!storage_replace_test());
        }
        if(storage_dspare)STORAGE_CHECK(!storage_spare_test());
        STORAGE_CHECK(!cli_result("C:zpool list -H -o name public_mirror >T:cli-package-output.txt","public_mirror"));
        STORAGE_CHECK(!cli_trim_unsupported());
        STORAGE_CHECK(!cli_result("C:zfs list -H -o name public_mirror/files >T:cli-package-output.txt","public_mirror/files"));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value type public_mirror/files >T:cli-package-output.txt","filesystem"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs snapshot public_mirror/files@cli_roundtrip",TAG_DONE)==0);
        {
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/defer_tree"));
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/defer_tree/child"));
            STORAGE_CHECK(!cli_command("C:zfs snapshot -r public_mirror/defer_tree@held"));
            STORAGE_CHECK(!cli_command("C:zfs hold keep public_mirror/defer_tree/child@held"));
            {
                struct oz_message preview={0};
                preview.magic=OZ_MAGIC;preview.version=OZ_VERSION;preview.msg.mn_Length=sizeof(preview);
                preview.operation=38;preview.flags=OZ_CONFIRMED|OZ_DRYRUN|OZ_RECURSIVE;
                strcpy(preview.target,"public_mirror/defer_tree@held");
                strcpy(preview.options,"defer=on");
                STORAGE_CHECK(oz_request_route(&preview)==OZ_ROUTE_PROJECT);
                struct oz_project_worker *worker=NULL;
                STORAGE_CHECK(!oz_project_worker_start(&preview,&worker));
                int result=EAGAIN;
                for(unsigned tick=0;tick<1500&&result==EAGAIN;tick++){
                    result=oz_project_worker_poll(worker,&preview);
                    if(result==EAGAIN)Delay(1);
                }
                STORAGE_CHECK(!result);
                STORAGE_CHECK(!oz_project_worker_free(worker));
                STORAGE_CHECK(preview.status==OZ_OK&&strstr(preview.detail,"public_mirror/defer_tree@held")&&
                    strstr(preview.detail,"public_mirror/defer_tree/child@held"));
                STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree@held >T:cli-package-output.txt","off"));
                STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree/child@held >T:cli-package-output.txt","off"));
                STORAGE_CHECK(!cli_result("C:zfs get -H -o value userrefs public_mirror/defer_tree/child@held >T:cli-package-output.txt","1"));
                preview.flags&=~OZ_DRYRUN;
                STORAGE_CHECK(oz_request_route(&preview)==OZ_ROUTE_AUTHENTICATED);
                STORAGE_CHECK(oz_destroy_preview_execute(&preview)==EINVAL);
                preview.flags|=OZ_DRYRUN|OZ_FORCE;
                STORAGE_CHECK(oz_destroy_preview_execute(&preview)==EINVAL);
                preview.flags=OZ_DRYRUN|OZ_RECURSIVE;
                STORAGE_CHECK(oz_destroy_preview_execute(&preview)==EINVAL);
                note("Public destruction dry-run PASS async=1 recursive-plan=1 deferred-unchanged=1 hold-unchanged=1 dryrun-required=1 force-refused=1 confirmation=1\n");
            }
            STORAGE_CHECK(!cli_command("C:zfs snapshot public_mirror/defer_tree@permission_probe"));
            STORAGE_CHECK(!cli_command("C:zfs allow -l -u 77 mount,destroy public_mirror/defer_tree"));
            STORAGE_CHECK(!destroy_identity("public_mirror/defer_tree@permission_probe",OZ_CONFIRMED,0));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree@permission_probe >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_command("C:zfs allow -d -u 77 mount,destroy public_mirror/defer_tree"));
            STORAGE_CHECK(!destroy_identity("public_mirror/defer_tree@permission_probe",OZ_CONFIRMED,1));
            STORAGE_CHECK(cli_command("C:zfs list -t snapshot public_mirror/defer_tree@permission_probe")!=0);
            STORAGE_CHECK(!cli_command("C:zfs unallow -d -u 77 mount,destroy public_mirror/defer_tree"));
            STORAGE_CHECK(!destroy_identity("public_mirror/defer_tree@held",OZ_CONFIRMED|OZ_RECURSIVE,0));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree@held >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree/child@held >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_command("C:zfs unallow -l -u 77 mount,destroy public_mirror/defer_tree"));
            note("Public recursive deferred authorization PASS local-only-denied=1 descendant-granted=1 revoked-denied=1 no-partial-mutation=1 identity-restored=1\n");
            STORAGE_CHECK(storage_request_flags(38,"public_mirror/defer_tree@held",NULL,"defer=on",OZ_RECURSIVE)!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree@held >T:cli-package-output.txt","off"));
            STORAGE_CHECK(storage_request_flags(38,"public_mirror/defer_tree",NULL,"defer=on",OZ_CONFIRMED|OZ_RECURSIVE)!=0);
            STORAGE_CHECK(!storage_request_flags(38,"public_mirror/defer_tree@held",NULL,"defer=on",OZ_CONFIRMED|OZ_RECURSIVE));
            STORAGE_CHECK(cli_command("C:zfs list -t snapshot public_mirror/defer_tree@held")!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_tree/child@held >T:cli-package-output.txt","on"));
            STORAGE_CHECK(!cli_command("C:zfs release keep public_mirror/defer_tree/child@held"));
            STORAGE_CHECK(cli_command("C:zfs list -t snapshot public_mirror/defer_tree/child@held")!=0);
            STORAGE_CHECK(!cli_command("C:zfs destroy -r public_mirror/defer_tree"));
            note("Public recursive deferred destroy PASS confirmation=1 dataset-refused=1 unheld-removed=1 held-retained=1 release-removes=1 cleanup=1\n");
            /* Leave real held/deferred snapshots in the exported pool for
             * the fresh-guest healthy/degraded/restored import checks. */
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/defer_pending"));
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/defer_pending/child"));
            STORAGE_CHECK(!cli_command("C:zfs snapshot -r public_mirror/defer_pending@pending"));
            STORAGE_CHECK(!cli_command("C:zfs hold -r cold_keep public_mirror/defer_pending@pending"));
            STORAGE_CHECK(!storage_request_flags(38,"public_mirror/defer_pending@pending",NULL,"defer=on",OZ_CONFIRMED|OZ_RECURSIVE));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_pending@pending >T:cli-package-output.txt","on"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/defer_pending/child@pending >T:cli-package-output.txt","on"));
            note("Public pending deferred snapshots PASS recursive=1 parent-held=1 child-held=1 retained-for-cold=1\n");
        }
        {
            struct oz_message clone={0};
            clone.magic=OZ_MAGIC;clone.version=OZ_VERSION;clone.msg.mn_Length=sizeof(clone);
            clone.operation=52;clone.flags=OZ_CONFIRMED;
            strcpy(clone.target,"public_mirror/files@cli_roundtrip");
            strcpy(clone.argument,"public_mirror/manager_clone");
            strcpy(clone.options,"compression=off;quota=32M;mountpoint=none");
            STORAGE_CHECK(oz_request_route(&clone)==OZ_ROUTE_PROJECT);
            struct oz_project_worker *worker=NULL;
            STORAGE_CHECK(!oz_project_worker_start(&clone,&worker));
            int worker_error=EAGAIN;
            for(unsigned tick=0;tick<1500&&worker_error==EAGAIN;tick++){
                worker_error=oz_project_worker_poll(worker,&clone);
                if(worker_error==EAGAIN)Delay(1);
            }
            STORAGE_CHECK(!worker_error);
            STORAGE_CHECK(!oz_project_worker_free(worker));worker=NULL;
            STORAGE_CHECK(clone.status==OZ_OK);
            STORAGE_CHECK(!cli_result("C:zfs get -Hp -o value quota public_mirror/manager_clone >T:cli-package-output.txt","33554432"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value compression public_mirror/manager_clone >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value mounted public_mirror/manager_clone >T:cli-package-output.txt","no"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/manager_clone"));
            STORAGE_CHECK(!clone_worker_identity("public_mirror/denied_clone",0));
            STORAGE_CHECK(cli_command("C:zfs list public_mirror/denied_clone")!=0);
            note("Public Manager clone authorization PASS nonroot-worker=1 upstream-denied=1 absent-destination=1 identity-restored=1\n");
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/delegated_clones"));
            STORAGE_CHECK(!cli_command("C:zfs allow -u 77 clone public_mirror/files"));
            STORAGE_CHECK(!cli_command("C:zfs allow -u 77 create,mount,compression,mountpoint public_mirror/delegated_clones"));
            STORAGE_CHECK(!clone_worker_identity("public_mirror/delegated_clones/child",1));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value origin public_mirror/delegated_clones/child >T:cli-package-output.txt","public_mirror/files@cli_roundtrip"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value compression public_mirror/delegated_clones/child >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value mounted public_mirror/delegated_clones/child >T:cli-package-output.txt","no"));
            STORAGE_CHECK(!cli_command("C:zfs unallow -u 77 clone public_mirror/files"));
            STORAGE_CHECK(!cli_command("C:zfs unallow -u 77 create,mount,compression,mountpoint public_mirror/delegated_clones"));
            STORAGE_CHECK(!clone_worker_identity("public_mirror/delegated_clones/after_revoke",0));
            STORAGE_CHECK(cli_command("C:zfs list public_mirror/delegated_clones/after_revoke")!=0);
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/delegated_clones/child"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/delegated_clones"));
            note("Public Manager clone delegation PASS granted=1 property=1 no-mount=1 revoked=1 no-stale-authority=1 cleanup=1\n");
            strcpy(clone.options,"quota=not-a-size");
            STORAGE_CHECK(!oz_clone_execute(&clone)&&clone.status==OZ_ENGINE_ERROR);
            STORAGE_CHECK(cli_command("C:zfs list public_mirror/manager_clone")!=0);
            strcpy(clone.options,"quota=1M;quota=2M");
            STORAGE_CHECK(oz_clone_execute(&clone)==EINVAL);
            strcpy(clone.options,"quota=32M;");
            STORAGE_CHECK(oz_clone_execute(&clone)==EINVAL);
            strcpy(clone.options,"quota=32M");clone.flags=0;
            STORAGE_CHECK(oz_clone_execute(&clone)==EINVAL);
            note("Public Manager clone properties PASS quota=1 compression=1 no-mount=1 duplicate-refused=1 malformed-refused=1 confirmation=1 async-worker=1 invalid-property-no-create=1\n");
        }
        STORAGE_CHECK(!cli_result("C:zfs list -H -o name -t snapshot public_mirror/files@cli_roundtrip >T:cli-package-output.txt","public_mirror/files@cli_roundtrip"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs clone -o mountpoint=none public_mirror/files@cli_roundtrip public_mirror/cli_clone",TAG_DONE)==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value origin public_mirror/cli_clone >T:cli-package-output.txt","public_mirror/files@cli_roundtrip"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs rename public_mirror/cli_clone public_mirror/cli_renamed",TAG_DONE)==0);
        STORAGE_CHECK(cli_command("C:zpool set feature@lz4_compress=enabled public_mirror")==0);
        STORAGE_CHECK(!cli_result("C:zpool get -H -o value feature@lz4_compress public_mirror >T:cli-package-output.txt","active"));
        STORAGE_CHECK(cli_command("C:zfs set compression=lz4 public_mirror/cli_renamed")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value compression public_mirror/cli_renamed >T:cli-package-output.txt","lz4"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs snapshot public_mirror/cli_renamed@rollback",TAG_DONE)==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","OZCLIRW",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZCLIRW:after-snapshot.txt",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
        STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs rollback public_mirror/cli_renamed@rollback")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","OZCLIRO",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZCLIRO:after-snapshot.txt",MODE_OLDFILE);
        STORAGE_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        file=Open((CONST_STRPTR)"OZCLIRO:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs send public_mirror/cli_renamed@rollback >T:cli-replication.zfs")==0);
        STORAGE_CHECK(cli_command("C:zfs receive -u -o mountpoint=none public_mirror/cli_received <T:cli-replication.zfs")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_received","OZCLIRECV",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        STORAGE_CHECK(!storage_sparse64("OZCLIRECV:",0));
        file=Open((CONST_STRPTR)"OZCLIRECV:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZCLIRECV:after-snapshot.txt",MODE_OLDFILE);
        STORAGE_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_received","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","OZCLIINC",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZCLIINC:incremental.txt",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
        STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_renamed","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs snapshot public_mirror/cli_renamed@incremental")==0);
        STORAGE_CHECK(cli_command("C:zfs send -i public_mirror/cli_renamed@rollback public_mirror/cli_renamed@incremental >T:cli-incremental.zfs")==0);
        STORAGE_CHECK(cli_command("C:zfs receive -u public_mirror/cli_received <T:cli-incremental.zfs")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_received","OZCLIINCRO",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        STORAGE_CHECK(!storage_sparse64("OZCLIINCRO:",0));
        file=Open((CONST_STRPTR)"OZCLIINCRO:incremental.txt",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZCLIINCRO:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/cli_received","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/cli_received@incremental")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/cli_received@rollback")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/cli_received")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/cli_renamed@incremental")==0);
        note("Public packaged replication PASS send=1 receive=1 mounted-data=1 snapshot-boundary=1 incremental=1\n");
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs destroy public_mirror/cli_renamed@rollback",TAG_DONE)==0);
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs destroy public_mirror/cli_renamed",TAG_DONE)==0);
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs destroy public_mirror/files@cli_roundtrip",TAG_DONE)==0);
        note("Public packaged CLI PASS pool-list=1 dataset-list=1 property=1 snapshot-create-list-destroy=1 clone=1 rename=1 set-get=1 rollback-data=1\n");
        /* Public, disposable fixture bytes, not user key material. RAM only. */
        unsigned char key[32];for(unsigned i=0;i<sizeof(key);i++)key[i]=(unsigned char)(i+1);
        file=Open((CONST_STRPTR)"RAM:public-test.key",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,key,sizeof(key))==sizeof(key));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(cli_command("C:zpool set feature@encryption=enabled public_mirror")==0);
        STORAGE_CHECK(cli_command("C:zfs create -u -o mountpoint=none -o encryption=aes-256-gcm -o keyformat=raw -o keylocation=file:///RAM/public-test.key public_mirror/encrypted")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value encryption public_mirror/encrypted >T:cli-package-output.txt","aes-256-gcm"));
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCRYPT",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
        STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/encrypted")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        memset(key,0x7a,sizeof(key));file=Open((CONST_STRPTR)"RAM:public-wrong.key",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,key,sizeof(key))==sizeof(key));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-wrong.key public_mirror/encrypted")!=0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        STORAGE_CHECK(!storage_request_flags(62,"public_mirror/encrypted","RAM:public-test.key",NULL,OZ_CONFIRMED|OZ_DRYRUN));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCHECKKEY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)!=0);
        STORAGE_CHECK(storage_request_flags(62,"public_mirror/encrypted","RAM:public-wrong.key",NULL,OZ_CONFIRMED|OZ_DRYRUN)!=0);
        STORAGE_CHECK(storage_request_flags(62,"public_mirror/encrypted","RAM:public-test.key",NULL,OZ_DRYRUN)!=0);
        STORAGE_CHECK(storage_request_flags(62,"public_mirror/encrypted","RAM:public-test.key",NULL,OZ_CONFIRMED|OZ_DRYRUN|OZ_FORCE)!=0);
        STORAGE_CHECK(storage_request_flags(64,"public_mirror/encrypted","RAM:public-test.key",NULL,OZ_CONFIRMED|OZ_DRYRUN)!=0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        note("Public authenticated key dry-run PASS correct=1 wrong-refused=1 locked=1 mount-refused=1 confirmation=1 force-refused=1 change-key-refused=1\n");
        STORAGE_CHECK(!key_check_identity(0));
        STORAGE_CHECK(!cli_command("C:zfs allow -l -u 77 load-key public_mirror/encrypted"));
        STORAGE_CHECK(!key_check_identity(1));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        /* Dataset delegation must not bypass separate key-file access.
         * This key lives on our real DOS handler, not RAM's permissive policy. */
        unsigned char private_key[32];for(unsigned i=0;i<sizeof(private_key);i++)private_key[i]=(unsigned char)(i+1);
        file=Open((CONST_STRPTR)"OZPUBLIC:permission-check.key",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,private_key,sizeof(private_key))==sizeof(private_key));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(SetOwner((STRPTR)"OZPUBLIC:permission-check.key",secOWNER_SYSTEM));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:permission-check.key",0));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:permission-check.key",0));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:permission-check.key",FIBF_OTR_READ));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:permission-check.key",1));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:permission-check.key",0));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:permission-check.key",0));
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"OZPUBLIC:permission-check.key"));
        note("Public key file authorization PASS delegated=1 private-denied=1 readable-allowed=1 revoked-denied=1 cleanup=1\n");
        BPTR keydir=CreateDir((CONST_STRPTR)"OZPUBLIC:private-key-dir");STORAGE_CHECK(keydir);UnLock(keydir);
        file=Open((CONST_STRPTR)"OZPUBLIC:private-key-dir/check.key",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,private_key,sizeof(private_key))==sizeof(private_key));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(SetOwner((STRPTR)"OZPUBLIC:private-key-dir",secOWNER_SYSTEM));
        STORAGE_CHECK(SetOwner((STRPTR)"OZPUBLIC:private-key-dir/check.key",secOWNER_SYSTEM));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:private-key-dir/check.key",FIBF_OTR_READ));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:private-key-dir",0));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:private-key-dir/check.key",0));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:private-key-dir",FIBF_OTR_EXECUTE));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:private-key-dir/check.key",1));
        STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:private-key-dir",0));
        STORAGE_CHECK(!key_check_file_identity("OZPUBLIC:private-key-dir/check.key",0));
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"OZPUBLIC:private-key-dir/check.key"));
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"OZPUBLIC:private-key-dir"));
        note("Public key traversal authorization PASS readable-file=1 private-parent-denied=1 search-allowed=1 revoked-denied=1 cleanup=1\n");
        STORAGE_CHECK(!cli_command("C:zfs unallow -l -u 77 load-key public_mirror/encrypted"));
        STORAGE_CHECK(!key_check_identity(0));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable"));
        note("Public key verification delegation PASS unprivileged-denied=1 explicit-grant=1 remains-locked=1 revoke-denied=1 root-restored=1\n");
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-test.key public_mirror/encrypted")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","available"));
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCRYPT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/encrypted")==0);
        STORAGE_CHECK(cli_command("C:zfs snapshot public_mirror/encrypted@raw")==0);
        STORAGE_CHECK(cli_command("C:zfs send -w public_mirror/encrypted@raw >T:cli-raw-encrypted.zfs")==0);
        STORAGE_CHECK(cli_command("C:zfs receive -u -o mountpoint=none public_mirror/encrypted_received <T:cli-raw-encrypted.zfs")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value encryption public_mirror/encrypted_received >T:cli-package-output.txt","aes-256-gcm"));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted_received >T:cli-package-output.txt","unavailable"));
        STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","OZRAW",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)!=0);
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-test.key public_mirror/encrypted_received")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","OZRAW",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZRAW:payload",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/encrypted_received")==0);
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-test.key public_mirror/encrypted")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZRAWADD",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZRAWADD:incremental",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
        STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/encrypted")==0);
        STORAGE_CHECK(cli_command("C:zfs snapshot public_mirror/encrypted@raw_incremental")==0);
        STORAGE_CHECK(cli_command("C:zfs send -w -i public_mirror/encrypted@raw public_mirror/encrypted@raw_incremental >T:cli-raw-incremental.zfs")==0);
        STORAGE_CHECK(cli_command("C:zfs receive -u public_mirror/encrypted_received <T:cli-raw-incremental.zfs")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted_received >T:cli-package-output.txt","unavailable"));
        STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","OZRAWINC",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)!=0);
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-test.key public_mirror/encrypted_received")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","OZRAWINC",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZRAWINC:incremental",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZRAWINC:payload",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted_received","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/encrypted_received")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted_received@raw_incremental")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted@raw_incremental")==0);
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"T:cli-raw-incremental.zfs"));
        note("Public raw incremental replication PASS locked-send=1 locked-receive=1 locked-mount-denied=1 exact-old-new=1 cleanup=1\n");
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted_received@raw")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted_received")==0);
        STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted@raw")==0);
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"T:cli-raw-encrypted.zfs"));
        note("Public raw encrypted replication PASS locked-send=1 receive=1 locked-mount-denied=1 key-load=1 exact-read=1 cleanup=1\n");
        if(!native_file_fixture)STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/encrypted")==0);
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:public-test.key"));
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:public-wrong.key"));
        note("Public packaged encryption PASS create=1 unload=1 wrong-key-denied=1 reload=1 exact-read=1 cleanup=1\n");
        {
            const char hexkey[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
            file=Open((CONST_STRPTR)"RAM:public-hex.key",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,hexkey,sizeof(hexkey)-1)==sizeof(hexkey)-1);STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(!cli_command("C:zfs create -u -o mountpoint=none -o encryption=aes-256-gcm -o keyformat=hex -o keylocation=file:///RAM/public-hex.key public_mirror/check_hex"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/check_hex"));
            for(unsigned repeat=0;repeat<3;repeat++)
                STORAGE_CHECK(!storage_request_flags(62,"public_mirror/check_hex","RAM:public-hex.key",NULL,OZ_CONFIRMED|OZ_DRYRUN));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/check_hex >T:cli-package-output.txt","unavailable"));
            STORAGE_CHECK(!storage_request(62,"public_mirror/check_hex","RAM:public-hex.key",NULL));
            STORAGE_CHECK(!storage_request_flags(62,"public_mirror/check_hex","RAM:public-hex.key",NULL,OZ_CONFIRMED|OZ_DRYRUN));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/check_hex >T:cli-package-output.txt","available"));
            const char *bad_change[]={"keyformat=invalid","keyformat=hex;pbkdf2iters=400000",
                "keyformat=passphrase;pbkdf2iters=99999","pbkdf2iters=4294967296",
                "keyformat=hex;keyformat=raw","compression=off","keylocation=invalid","keyformat=hex;"};
            for(unsigned i=0;i<sizeof(bad_change)/sizeof(bad_change[0]);i++)
                STORAGE_CHECK(storage_request(64,"public_mirror/check_hex","RAM:public-hex.key",bad_change[i])!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","hex"));
            STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","OZREKEY",OZ_MOUNT_ATTACH,0));
            file=Open((CONST_STRPTR)"OZREKEY:payload",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
            STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","",OZ_MOUNT_DETACH,0));
            file=Open((CONST_STRPTR)"RAM:rekey-pass",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,storage_passphrase,sizeof(storage_passphrase)-1)==sizeof(storage_passphrase)-1);STORAGE_CHECK(Close(file));file=0;
            const char *pass_options="keyformat=passphrase;pbkdf2iters=400000;keylocation=file:///RAM/rekey-pass";
            STORAGE_CHECK(!key_change_identity("RAM:rekey-pass",pass_options,0));
            STORAGE_CHECK(!cli_command("C:zfs allow -l -u 77 load-key public_mirror/check_hex"));
            STORAGE_CHECK(!key_change_identity("RAM:rekey-pass",pass_options,0));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","hex"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keylocation public_mirror/check_hex >T:cli-package-output.txt","file:///RAM/public-hex.key"));
            STORAGE_CHECK(!cli_command("C:zfs allow -l -u 77 change-key public_mirror/check_hex"));
            file=Open((CONST_STRPTR)"OZPUBLIC:change-private.key",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,storage_passphrase,sizeof(storage_passphrase)-1)==sizeof(storage_passphrase)-1);STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(SetOwner((STRPTR)"OZPUBLIC:change-private.key",secOWNER_SYSTEM));
            STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:change-private.key",0));
            STORAGE_CHECK(!key_change_identity("OZPUBLIC:change-private.key",pass_options,0));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","hex"));
            STORAGE_CHECK(SetProtection((CONST_STRPTR)"OZPUBLIC:change-private.key",FIBF_OTR_READ));
            STORAGE_CHECK(!key_change_identity("OZPUBLIC:change-private.key",pass_options,1));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"OZPUBLIC:change-private.key"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","passphrase"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value pbkdf2iters public_mirror/check_hex >T:cli-package-output.txt","400000"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keylocation public_mirror/check_hex >T:cli-package-output.txt","file:///RAM/rekey-pass"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/check_hex"));
            STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-hex.key public_mirror/check_hex")!=0);
            STORAGE_CHECK(!cli_command("C:zfs load-key public_mirror/check_hex"));
            unsigned char rekey_raw[32];memset(rekey_raw,0x5c,sizeof(rekey_raw));
            file=Open((CONST_STRPTR)"RAM:rekey-raw",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,rekey_raw,sizeof(rekey_raw))==sizeof(rekey_raw));STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(!cli_command("C:zfs unallow -l -u 77 change-key public_mirror/check_hex"));
            STORAGE_CHECK(!key_change_identity("RAM:rekey-raw","keyformat=raw;keylocation=prompt",0));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","passphrase"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value pbkdf2iters public_mirror/check_hex >T:cli-package-output.txt","400000"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keylocation public_mirror/check_hex >T:cli-package-output.txt","file:///RAM/rekey-pass"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/check_hex >T:cli-package-output.txt","available"));
            STORAGE_CHECK(!cli_command("C:zfs unallow -l -u 77 load-key public_mirror/check_hex"));
            note("Public key change authorization PASS no-grant-denied=1 load-only-denied=1 private-file-denied=1 explicit-grant=1 revoke-denied=1 properties-preserved=1 root-restored=1\n");
            STORAGE_CHECK(!storage_request(64,"public_mirror/check_hex","RAM:rekey-raw","keyformat=raw;keylocation=prompt"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","raw"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/check_hex"));
            STORAGE_CHECK(!storage_request(62,"public_mirror/check_hex","RAM:rekey-raw",NULL));
            STORAGE_CHECK(!storage_request(64,"public_mirror/check_hex","RAM:public-hex.key","keyformat=hex;keylocation=file:///RAM/public-hex.key"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/check_hex"));
            STORAGE_CHECK(!cli_command("C:zfs load-key public_mirror/check_hex"));
            STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","OZREKEY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
            file=Open((CONST_STRPTR)"OZREKEY:payload",MODE_OLDFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","",OZ_MOUNT_DETACH,0));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:rekey-pass"));STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:rekey-raw"));
            note("Public key format rotation PASS invalid-options=8 hex-to-passphrase=1 iterations=400000 location=1 old-key-denied=1 passphrase-to-raw=1 raw-to-hex=1 exact-data=1\n");
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/check_hex"));
            file=Open((CONST_STRPTR)"RAM:public-hex.key",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,"invalid hex\n",12)==12);STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(storage_request_flags(62,"public_mirror/check_hex","RAM:public-hex.key",NULL,OZ_CONFIRMED|OZ_DRYRUN)!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/check_hex >T:cli-package-output.txt","unavailable"));
            if(!native_file_fixture)STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/check_hex"));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:public-hex.key"));
            note("Public hex key verification PASS newline=1 repeated=3 locked-preserved=1 loaded-preserved=1 malformed-refused=1 cleanup=1\n");
        }
        /* Exercise external PBKDF2 through the actual upstream CLI/dispatcher. */
        file=Open((CONST_STRPTR)"RAM:public-passphrase",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,storage_passphrase,sizeof(storage_passphrase)-1)==sizeof(storage_passphrase)-1);
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(cli_command("C:zfs create -u -o mountpoint=none -o encryption=aes-256-gcm -o keyformat=passphrase -o pbkdf2iters=350000 -o keylocation=file:///RAM/public-passphrase public_mirror/passphrase")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/passphrase >T:cli-package-output.txt","passphrase"));
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","OZPASS",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZPASS:payload",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,payload,sizeof(storage_payload)-1)==sizeof(storage_payload)-1);
        STORAGE_CHECK(Flush(file));STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","",OZ_MOUNT_DETACH,0));
        file=Open((CONST_STRPTR)"RAM:public-passphrase-new",MODE_NEWFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Write(file,storage_rotated_passphrase,sizeof(storage_rotated_passphrase)-1)==sizeof(storage_rotated_passphrase)-1);
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(cli_command("C:zfs change-key -o keyformat=passphrase -o pbkdf2iters=400000 -o keylocation=file:///RAM/public-passphrase-new public_mirror/passphrase")==0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value pbkdf2iters public_mirror/passphrase >T:cli-package-output.txt","400000"));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/passphrase")==0);
        STORAGE_CHECK(cli_command("C:zfs load-key -L file:///RAM/public-passphrase public_mirror/passphrase")!=0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/passphrase >T:cli-package-output.txt","unavailable"));
        STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","OZPASS",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)!=0);
        STORAGE_CHECK(!storage_request_flags(62,"public_mirror/passphrase","RAM:public-passphrase-new",NULL,OZ_CONFIRMED|OZ_DRYRUN));
        STORAGE_CHECK(storage_request_flags(62,"public_mirror/passphrase","RAM:public-passphrase",NULL,OZ_CONFIRMED|OZ_DRYRUN)!=0);
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/passphrase >T:cli-package-output.txt","unavailable"));
        note("Public passphrase key verification PASS pbkdf2=400000 current=1 old-refused=1 locked-preserved=1\n");
        STORAGE_CHECK(cli_command("C:zfs load-key public_mirror/passphrase")==0);
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","OZPASS",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZPASS:payload",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","",OZ_MOUNT_DETACH,0));
        STORAGE_CHECK(cli_command("C:zfs unload-key public_mirror/passphrase")==0);
        if(!native_file_fixture)STORAGE_CHECK(cli_command("C:zfs destroy public_mirror/passphrase")==0);
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:public-passphrase"));
        STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:public-passphrase-new"));
        note("Public passphrase rotation PASS iterations=400000 old-key-denied=1 still-locked=1 new-key-load=1 exact-read=1 cleanup=1\n");
        note("Public passphrase encryption PASS create-pbkdf2=350000 locked-mount-denied=1 reload=1 exact-read=1 cleanup=1\n");
    }
    if(gui){
        BPTR key_change_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-key-change-test",SHARED_LOCK);
        if(key_change_gui)UnLock(key_change_gui);
        BPTR key_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-key-check-test",SHARED_LOCK);
        STORAGE_CHECK(!key_change_gui||key_gui);
        if(key_gui){
            UnLock(key_gui);
            unsigned char gui_key[32];for(unsigned i=0;i<sizeof(gui_key);i++)gui_key[i]=(unsigned char)(i+1);
            file=Open((CONST_STRPTR)"RAM:gui-check.key",MODE_NEWFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Write(file,gui_key,sizeof(gui_key))==sizeof(gui_key));STORAGE_CHECK(Close(file));file=0;
            STORAGE_CHECK(!cli_command("C:zfs create -u -o mountpoint=none -o encryption=aes-256-gcm -o keyformat=raw -o keylocation=file:///RAM/gui-check.key public_mirror/gui_key"));
            if(key_change_gui){
                file=Open((CONST_STRPTR)"RAM:gui-change.key",MODE_NEWFILE);STORAGE_CHECK(file);
                STORAGE_CHECK(Write(file,storage_passphrase,sizeof(storage_passphrase)-1)==sizeof(storage_passphrase)-1);STORAGE_CHECK(Close(file));file=0;
                note("Public GUI key change READY target=public_mirror/gui_key argument=RAM:gui-change.key options=keyformat=passphrase;pbkdf2iters=400000;keylocation=file:///RAM/gui-change.key\n");
            }else{
                STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/gui_key"));
                note("Public GUI key check READY target=public_mirror/gui_key argument=RAM:gui-check.key dry-run=1\n");
            }
        }
        BPTR preview_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-destroy-preview-test",SHARED_LOCK);
        if(preview_gui)UnLock(preview_gui);
        BPTR defer_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-defer-test",SHARED_LOCK);
        if(defer_gui){
            UnLock(defer_gui);
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/gui_defer"));
            STORAGE_CHECK(!cli_command("C:zfs create -o mountpoint=none public_mirror/gui_defer/child"));
            STORAGE_CHECK(!cli_command("C:zfs snapshot -r public_mirror/gui_defer@gui"));
            STORAGE_CHECK(!cli_command("C:zfs hold gui_keep public_mirror/gui_defer/child@gui"));
            note("Public GUI deferred READY target=public_mirror/gui_defer@gui options=defer=on recursive=1\n");
        }
        BPTR clone_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-clone-test",SHARED_LOCK);
        if(clone_gui){
            UnLock(clone_gui);
            STORAGE_CHECK(!cli_command("C:zfs snapshot public_mirror/files@gui_clone"));
            note("Public GUI clone READY source=public_mirror/files@gui_clone destination=public_mirror/gui_clone options=compression=off;quota=32M;mountpoint=none\n");
        }
        BPTR desktop=Lock((CONST_STRPTR)"SYS:openzfs-gui-desktop-test",SHARED_LOCK);
        if(desktop){
            UnLock(desktop);
            STORAGE_CHECK(!desktop_directory("OZPUBLIC:","before-wanderer"));
            STORAGE_CHECK(!cli_command("Run >T:wanderer-public.log SYS:System/Wanderer/Wanderer"));
            Delay(50);
            STORAGE_CHECK(!cli_command("C:WBRun OZPUBLIC:"));
            Delay(50);desktop_windows();
            STORAGE_CHECK(!desktop_directory("OZPUBLIC:","with-wanderer"));
            note("Public desktop READY: inspect mounted OZPUBLIC and hello.txt in Wanderer; close drawer windows before closing Manager. Wanderer stays running.\n");
        }
        BPTR volume_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-volume-test",SHARED_LOCK);
        if(volume_gui){UnLock(volume_gui);note("Public GUI volume READY: Devices, unit 5, public_mirror/public_volume, read-only Remap; save whole-volume FFS mount file T:OZBLOCK, then close Manager\n");}
        BPTR health_gui=Lock((CONST_STRPTR)"SYS:openzfs-gui-health-test",SHARED_LOCK);
        if(health_gui){UnLock(health_gui);note("Public GUI health READY: query Pool health for public_mirror in Simple mode, Save response, then close Manager\n");}
        note("Public storage GUI ready: mounted public_mirror/files with hello.txt\n");
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        if(health_gui){
            static char report[32768];
            BPTR saved=Open((CONST_STRPTR)"T:OpenZFS-Response.txt",MODE_OLDFILE);
            STORAGE_CHECK(saved!=BNULL);
            LONG count=Read(saved,report,sizeof(report)-1);
            LONG close_ok=Close(saved);
            STORAGE_CHECK(count>0&&count<(LONG)sizeof(report)-1&&close_ok);
            report[count]=0;
            STORAGE_CHECK(strstr(report,"pool | public_mirror | ONLINE")!=NULL);
            STORAGE_CHECK(strstr(report,"role=pool guid=")!=NULL);
            STORAGE_CHECK(strstr(report,"read-errors=0 write-errors=0 checksum-errors=0")!=NULL);
            STORAGE_CHECK(strstr(report,"parent=public_mirror")!=NULL);
            STORAGE_CHECK(strstr(report,"scan=")!=NULL&&strstr(report,"checkpoint=")!=NULL);
            STORAGE_CHECK(strstr(report,"Errors: read ")==NULL);
            note("Public GUI health saved response PASS raw-topology=1 counters=1 scan=1 checkpoint=1 not-display-summary=1\n");
        }
        if(key_change_gui){
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keyformat public_mirror/gui_key >T:cli-package-output.txt","passphrase"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value pbkdf2iters public_mirror/gui_key >T:cli-package-output.txt","400000"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keylocation public_mirror/gui_key >T:cli-package-output.txt","file:///RAM/gui-change.key"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/gui_key"));
            STORAGE_CHECK(storage_request(62,"public_mirror/gui_key","RAM:gui-check.key",NULL)!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/gui_key >T:cli-package-output.txt","unavailable"));
            STORAGE_CHECK(!cli_command("C:zfs load-key public_mirror/gui_key"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/gui_key"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/gui_key"));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:gui-check.key"));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:gui-change.key"));
            note("Public GUI key change PASS format=passphrase iterations=400000 location=1 old-key-denied=1 new-key-load=1 cleanup=1\n");
        }else if(key_gui){
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/gui_key >T:cli-package-output.txt","unavailable"));
            STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/gui_key","OZGUIKEY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)!=0);
            STORAGE_CHECK(!storage_request(62,"public_mirror/gui_key","RAM:gui-check.key",NULL));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value keystatus public_mirror/gui_key >T:cli-package-output.txt","available"));
            STORAGE_CHECK(!cli_command("C:zfs unload-key public_mirror/gui_key"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/gui_key"));
            STORAGE_CHECK(DeleteFile((CONST_STRPTR)"RAM:gui-check.key"));
            note("Public GUI key check PASS locked=1 mount-refused=1 normal-load=1 cleanup=1\n");
        }
        if(preview_gui){
            STORAGE_CHECK(defer_gui);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/gui_defer@gui >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/gui_defer/child@gui >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value userrefs public_mirror/gui_defer/child@gui >T:cli-package-output.txt","1"));
            STORAGE_CHECK(!cli_command("C:zfs release gui_keep public_mirror/gui_defer/child@gui"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/gui_defer/child@gui >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_command("C:zfs destroy -r public_mirror/gui_defer"));
            note("Public GUI destruction preview PASS parent-unchanged=1 child-unchanged=1 hold-retained=1 release-does-not-destroy=1 cleanup=1\n");
        }else if(defer_gui){
            STORAGE_CHECK(cli_command("C:zfs list -t snapshot public_mirror/gui_defer@gui")!=0);
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value defer_destroy public_mirror/gui_defer/child@gui >T:cli-package-output.txt","on"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value userrefs public_mirror/gui_defer/child@gui >T:cli-package-output.txt","1"));
            STORAGE_CHECK(!cli_command("C:zfs release gui_keep public_mirror/gui_defer/child@gui"));
            STORAGE_CHECK(cli_command("C:zfs list -t snapshot public_mirror/gui_defer/child@gui")!=0);
            STORAGE_CHECK(!cli_command("C:zfs destroy -r public_mirror/gui_defer"));
            note("Public GUI deferred PASS parent-removed=1 child-deferred=1 hold-retained=1 release-removes=1 cleanup=1\n");
        }
        if(clone_gui){
            STORAGE_CHECK(!cli_result("C:zfs get -Hp -o value quota public_mirror/gui_clone >T:cli-package-output.txt","33554432"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value compression public_mirror/gui_clone >T:cli-package-output.txt","off"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value mounted public_mirror/gui_clone >T:cli-package-output.txt","no"));
            STORAGE_CHECK(!cli_result("C:zfs get -H -o value origin public_mirror/gui_clone >T:cli-package-output.txt","public_mirror/files@gui_clone"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/gui_clone"));
            STORAGE_CHECK(!cli_command("C:zfs destroy public_mirror/files@gui_clone"));
            note("Public GUI clone PASS quota=1 compression=1 no-mount=1 origin=1 cleanup=1\n");
        }
        if(volume_gui)STORAGE_CHECK(!storage_gui_volume_verify());
        if(desktop){
            STORAGE_CHECK(!desktop_directory("OZPUBLIC:","after-ui"));
            file=Open((CONST_STRPTR)"OZPUBLIC:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
            STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
            STORAGE_CHECK(Close(file));file=0;
            note("Public desktop post-UI data PASS exact-read=1\n");
        }
        note("Public storage GUI closed; verifying export and readonly reimport\n");
    }
    STORAGE_CHECK(!desktop_detach(gui,"post-ui"));
    STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0)==ENOENT);
    STORAGE_CHECK(!verify_mounts(NULL,0));
    BPTR handler_marker=Lock((CONST_STRPTR)"SYS:openzfs-handler-package-test",SHARED_LOCK);
    if(handler_marker){
        UnLock(handler_marker);
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:Mount SYS:OZPACKAGE",TAG_DONE)==0);
        file=Open((CONST_STRPTR)"OZPACKAGE:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
        STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
        STORAGE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZPACKAGE:denied.txt",MODE_NEWFILE);
        STORAGE_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        STORAGE_CHECK(!desktop_detach(gui,"packaged-handler"));
        STORAGE_CHECK(!verify_mounts(NULL,0));
        note("Public packaged handler PASS mount=1 exact-read=1 readonly=1 detach=1\n");
    }
    BPTR archive_marker=Lock((CONST_STRPTR)"SYS:openzfs-startup-archive-test",SHARED_LOCK);
    if(archive_marker){
        UnLock(archive_marker);
        STORAGE_CHECK(!cli_command("C:zfs set mountpoint=none public_mirror"));
        STORAGE_CHECK(!cli_command("C:zfs set mountpoint=OZCOLD: public_mirror/files"));
        STORAGE_CHECK(!cli_command("C:zfs unmount public_mirror/files"));
    }
    STORAGE_CHECK(!storage_request(8,"public_mirror",NULL,NULL));
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_import_files_test(storage_directory));
    STORAGE_CHECK(!oz_upstream_file_vdev_test(1,storage_directory));
#endif
    STORAGE_CHECK(!storage_alias_request(7,2));
#ifdef OZ_NATIVE_DISPATCHER
    STORAGE_CHECK(!oz_public_bootenv_readonly_test("public_mirror"));
    STORAGE_CHECK(!oz_public_accounting_upgrade_test("public_mirror/files",1));
#endif
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_file_vdev_test(2,storage_directory));
    STORAGE_CHECK(!oz_upstream_promotion_readonly_test(1));
#endif
    STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZPUBLICRO",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    STORAGE_CHECK(!oz_mount_policy_query(OZ_ADMISSION_PORT,"public_mirror/files",&supported_policy,&enabled_policy)&&
        (enabled_policy&OZ_POLICY_READONLY));
    STORAGE_CHECK(!verify_mounts("OZPUBLICRO:",OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZPUBLICRO:hello.txt",MODE_OLDFILE);STORAGE_CHECK(file);
    STORAGE_CHECK(Read(file,actual,sizeof(actual))==sizeof(storage_payload)-1&&!memcmp(actual,payload,sizeof(storage_payload)-1));
    STORAGE_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZPUBLICRO:denied.txt",MODE_NEWFILE);
    STORAGE_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    STORAGE_CHECK(!desktop_detach(gui,"readonly-reimport"));
    note("Public native mount control PASS writable=1 readonly=1 busy=1 duplicate=1 absent=1 invalid=1\n");
    STORAGE_CHECK(!storage_request(8,"public_mirror",NULL,NULL));
    STORAGE_CHECK(!verify_mounts(NULL,0));
    note("Public native mount snapshot PASS empty=1 writable=1 readonly=1 retired-absent=1 bounds=1\n");
    note("Public authenticated mirror, DOS write/read and readonly reimport PASS\n");
done:
    if(file)Close(file);
#ifdef OZ_TEST_UPSTREAM_MOUNT_CACHE
    oz_upstream_mount_cache_finish();
#endif
    if(error){char line[96];snprintf(line,sizeof(line),"Public storage failure=%d\n",error);note(line);
        diagnostic_log("T:wanderer-public.log");
        diagnostic_log("T:service-first.log");
        diagnostic_log("T:service-second.log");
        diagnostic_log("T:service-writable.log");
    }
    return error;
}
static int storage_encrypted_cold(void)
{
    if(!native_file_fixture)return 0;
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    int error=cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable");
    if(error)return error;
    /* No cached key may leak across exports or a fresh boot. */
    if(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCRYPTCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return EIO;
    unsigned char key[32];for(unsigned i=0;i<sizeof(key);i++)key[i]=(unsigned char)(i+1);
    BPTR file=Open((CONST_STRPTR)"RAM:public-test.key",MODE_NEWFILE);
    if(!file)return EIO;
    if(Write(file,key,sizeof(key))!=sizeof(key))error=EIO;
    if(!Close(file))error=EIO;
    if(!error)error=storage_request_flags(62,"public_mirror/encrypted","RAM:public-test.key",NULL,OZ_CONFIRMED|OZ_DRYRUN);
    if(!error)error=cli_result("C:zfs get -H -o value keystatus public_mirror/encrypted >T:cli-package-output.txt","unavailable");
    if(!error&&!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCHECKCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))error=EIO;
    if(!error)note("Public cold key verification PASS readonly-import=1 correct=1 remains-locked=1 mount-refused=1\n");
    if(!error&&cli_command("C:zfs load-key -L file:///RAM/public-test.key public_mirror/encrypted"))error=EIO;
    if(!DeleteFile((CONST_STRPTR)"RAM:public-test.key"))error=EIO;
    if(error)return error;
    error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","OZCRYPTCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(error)return error;
    char bytes[sizeof(storage_payload)+1];file=Open((CONST_STRPTR)"OZCRYPTCOLD:payload",MODE_OLDFILE);
    if(!file)return EIO;
    LONG n=Read(file,bytes,sizeof(bytes));
    if(!Close(file)||n!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))error=EIO;
    int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/encrypted","",OZ_MOUNT_DETACH,0);
    if(!error)error=detached;
    if(!error&&cli_command("C:zfs unload-key public_mirror/encrypted"))error=EIO;
    if(!error)note("Public cold encryption PASS locked=1 mount-denied=1 load=1 exact-read=1 unload=1\n");
    return error;
}
static int storage_rotated_format_cold(void)
{
    if(!native_file_fixture)return 0;
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    if(cli_result("C:zfs get -H -o value keyformat public_mirror/check_hex >T:cli-package-output.txt","hex")||
       cli_result("C:zfs get -H -o value keylocation public_mirror/check_hex >T:cli-package-output.txt","file:///RAM/public-hex.key")||
       cli_result("C:zfs get -H -o value keystatus public_mirror/check_hex >T:cli-package-output.txt","unavailable"))return EIO;
    const char key[]="0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef\n";
    BPTR file=Open((CONST_STRPTR)"RAM:public-hex.key",MODE_NEWFILE);if(!file)return EIO;
    int error=Write(file,key,sizeof(key)-1)==sizeof(key)-1?0:EIO;
    if(!Close(file))error=EIO;
    if(!error)error=storage_request(62,"public_mirror/check_hex","RAM:public-hex.key",NULL);
    if(!DeleteFile((CONST_STRPTR)"RAM:public-hex.key"))error=EIO;
    if(error)return error;
    error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","OZREKEYCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(error)return error;
    char bytes[sizeof(storage_payload)+1];
    file=Open((CONST_STRPTR)"OZREKEYCOLD:payload",MODE_OLDFILE);
    if(!file)error=EIO;
    else{
        LONG count=Read(file,bytes,sizeof(bytes));
        if(!Close(file)||count!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))error=EIO;
    }
    int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/check_hex","",OZ_MOUNT_DETACH,0);
    if(!error)error=detached;
    if(!error)error=storage_request(63,"public_mirror/check_hex",NULL,NULL);
    if(!error)note("Public cold key format rotation PASS format=hex location=1 locked=1 reload=1 exact-data=1 unload=1\n");
    return error;
}
static int storage_rotated_passphrase_cold(void)
{
    if(!native_file_fixture)return 0;
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(!marker)return 0;
    UnLock(marker);
    if(cli_result("C:zfs get -H -o value pbkdf2iters public_mirror/passphrase >T:cli-package-output.txt","400000")||
        cli_result("C:zfs get -H -o value keystatus public_mirror/passphrase >T:cli-package-output.txt","unavailable"))return EIO;
    if(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","OZPASSCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return EIO;
    const char *keys[]={storage_passphrase,storage_rotated_passphrase};
    for(unsigned i=0;i<2;i++){
        BPTR file=Open((CONST_STRPTR)"RAM:public-cold-passphrase",MODE_NEWFILE);
        if(!file)return EIO;
        LONG n=Write(file,keys[i],strlen(keys[i]));
        if(!Close(file)||n!=(LONG)strlen(keys[i]))return EIO;
        LONG rc=cli_command("C:zfs load-key -L file:///RAM/public-cold-passphrase public_mirror/passphrase");
        if(!DeleteFile((CONST_STRPTR)"RAM:public-cold-passphrase"))return EIO;
        if((i==0&&rc==0)||(i==1&&rc!=0))return EIO;
        if(i==0&&cli_result("C:zfs get -H -o value keystatus public_mirror/passphrase >T:cli-package-output.txt","unavailable"))return EIO;
    }
    int error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","OZPASSCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(error)return error;
    BPTR file=Open((CONST_STRPTR)"OZPASSCOLD:payload",MODE_OLDFILE);
    if(!file)return EIO;
    char bytes[sizeof(storage_payload)+1];LONG n=Read(file,bytes,sizeof(bytes));
    if(!Close(file)||n!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))error=EIO;
    int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/passphrase","",OZ_MOUNT_DETACH,0);
    if(!error)error=detached;
    if(!error&&cli_command("C:zfs unload-key public_mirror/passphrase"))error=EIO;
    if(!error)note("Public cold passphrase rotation PASS iterations=400000 old-key-denied=1 locked=1 new-key-load=1 exact-read=1 unload=1\n");
    return error;
}
static int storage_cold_once(void)
{
    int error=companion_import_test(0);
    if(error==ENOENT)error=storage_alias_request(7,1);
    if(error)return error;
#ifdef OZ_NATIVE_DISPATCHER
    error=oz_public_bootenv_persistence_test("public_mirror",0);
    if(!error)error=oz_public_bootenv_readonly_test("public_mirror");
    if(error)return error;
#endif
    error=pool_root_payload("public_mirror",0,1);
    if(error)return error;
    note("Public cold pool root PASS exact-read=1 readonly-write-denied=1 detached=1\n");
    BPTR cli_defer=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
    if(cli_defer){
        UnLock(cli_defer);
        const char *snapshots[]={"public_mirror/defer_pending@pending",
            "public_mirror/defer_pending/child@pending"};
        for(unsigned i=0;i<2&&!error;i++){
            char command[384];
            snprintf(command,sizeof(command),"C:zfs get -H -o value defer_destroy %s >T:cli-package-output.txt",snapshots[i]);
            error=cli_result(command,"on");
            if(!error){
                snprintf(command,sizeof(command),"C:zfs get -H -o value userrefs %s >T:cli-package-output.txt",snapshots[i]);
                error=cli_result(command,"1");
            }
        }
        if(!error&&!cli_command("C:zfs release -r cold_keep public_mirror/defer_pending@pending"))error=EIO;
        for(unsigned i=0;i<2&&!error;i++){
            char command[384];
            snprintf(command,sizeof(command),"C:zfs get -H -o value userrefs %s >T:cli-package-output.txt",snapshots[i]);
            error=cli_result(command,"1");
        }
        if(error)return error;
        note("Public cold deferred snapshots PASS parent=1 child=1 pending=1 holds=1 readonly-release-refused=1 unchanged=1\n");
    }
    if(storage_sequential){
        char guid[32];error=storage_spare_status(guid,sizeof(guid),1);
        if(error)return error;
        note("Public cold sequential status PASS complete=1 rebuilt-nonzero=1 scrub-finished=1\n");
    }
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-gui-snapshot-required",SHARED_LOCK);
    if(marker){
        UnLock(marker);
        /* Read-only enumeration belongs to the public query API, not the
         * mutation-admission packet protocol. Match the Manager's route. */
        struct oz_client query;static struct oz_message request;
        if(!oz_client_open(&query))return ENOMEM;
        memset(&request,0,sizeof(request));request.operation=36;
        strcpy(request.target,"public_mirror/files@hosted_gui");
        strcpy(request.options,"snapshots=on");
        error=oz_submit(&query,&request)==OZ_OK?0:EIO;
        if(!error){
            struct oz_message *reply=NULL;
            while(!(reply=oz_poll(&query)))Delay(1);
            if(reply->status!=OZ_OK||reply->row_count!=1||
                strcmp(reply->rows[0].name,request.target)||strcmp(reply->rows[0].kind,"snapshot"))error=EIO;
            oz_message_free(reply);
        }
        oz_client_close(&query);
        if(error)return error;
        note("Public cold GUI snapshot retained PASS\n");
    }
    marker=Lock((CONST_STRPTR)"SYS:openzfs-startup-archive-test",SHARED_LOCK);
    if(marker){UnLock(marker);error=verify_mounts("OZCOLD:",OZ_MOUNT_READONLY);}
    else error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(error)return error;
    char bytes[sizeof(storage_payload)+1];
    BPTR file=Open((CONST_STRPTR)"OZCOLD:hello.txt",MODE_OLDFILE);
    if(!file)return EIO;
    LONG n=Read(file,bytes,sizeof(bytes));int closed=Close(file);
    if(!closed||n!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))return EIO;
    file=Open((CONST_STRPTR)"OZCOLD:denied.txt",MODE_NEWFILE);
    if(file){Close(file);return EIO;}
    if(IoErr()!=ERROR_DISK_WRITE_PROTECTED)return EIO;
    error=storage_sparse64("OZCOLD:",0);
    if(error)return error;
    error=storage_links("OZCOLD:",0);
    if(!error)error=storage_metadata("OZCOLD:",0);
    if(error)return error;
    error=desktop_directory("OZCOLD:","cold-import");
    if(error)return error;
    if(native_file_fixture&&(storage_layout==OZ_MIRROR||storage_dspare)){
        BPTR cli=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
        if(cli){UnLock(cli);error=storage_repair_data("OZCOLD:",0);if(error)return error;}
    }
    error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
    if(!error)error=storage_encrypted_cold();
    if(!error)error=storage_rotated_format_cold();
    if(!error)error=storage_rotated_passphrase_cold();
    if(!error){
        BPTR cli=Lock((CONST_STRPTR)"SYS:openzfs-cli-package-test",SHARED_LOCK);
        if(cli){UnLock(cli);error=storage_zvol_test(0);if(!error)error=storage_device_test(0);
            if(!error)error=storage_zvol_filesystem(0);}
    }
    if(!error)error=storage_request(8,"public_mirror",NULL,NULL);
    if(!error)note("Public cold hosted DOS PASS exact=1 readonly-refusal=1 exported=1\n");
    return error;
}
static int storage_no_pools(void)
{
    struct oz_client client;static struct oz_message request;
    if(!oz_client_open(&client))return ENOMEM;
    memset(&request,0,sizeof(request));request.operation=3;
    int error=oz_submit(&client,&request)==OZ_OK?0:EIO;
    if(!error){struct oz_message *reply=NULL;
        while(!(reply=oz_poll(&client)))Delay(1);
        if(reply->status!=OZ_OK||!reply->engine_available||reply->row_count)error=EIO;
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int storage_cold_test(void)
{
    int error=storage_destroyed_cold();
    if(!error)error=storage_cold_once();
    if(error)return error;
    /* Hide the topology's tolerated number of disposable members without
     * changing their bytes; restore every name even if degraded import fails. */
    char member[OZ_MAX_DEVICES][256],hidden[OZ_MAX_DEVICES][256];unsigned moved=0;
    if(storage_members>OZ_MAX_DEVICES||storage_missing>=storage_members)return EINVAL;
    for(unsigned i=0;i<storage_missing;i++){
        storage_member(member[i],sizeof(member[i]),storage_dspare?i:i+1);
        snprintf(hidden[i],sizeof(hidden[i]),"%s-held",member[i]);
        BPTR exists=Lock((CONST_STRPTR)hidden[i],SHARED_LOCK);
        if(exists){UnLock(exists);error=EEXIST;break;}
        if(IoErr()!=ERROR_OBJECT_NOT_FOUND||!Rename((CONST_STRPTR)member[i],(CONST_STRPTR)hidden[i])){error=EIO;break;}
        moved++;
    }
    if(!error)error=storage_cold_once();
    while(moved){--moved;if(!Rename((CONST_STRPTR)hidden[moved],(CONST_STRPTR)member[moved])&&!error)error=EIO;}
    if(!error){char line[128];snprintf(line,sizeof(line),"Public cold degraded assign PASS missing=%u exact=1 readonly=1 restored=1\n",storage_missing);note(line);}
    if(!error&&storage_replaced)note("Public cold replacement PASS replacement-only=1 exact=1\n");
    if(!error&&storage_dspare)note("Public cold dRAID spare PASS original-absent=1 exact=1\n");
    if(error)return error;
    if(storage_dspare){
        /* Qualify the one-data/one-spare layouts after losing the original
         * offline member plus as many live children as the parity width. */
        for(unsigned i=0;i<=storage_missing;i++){
            storage_member(member[i],sizeof(member[i]),i);
            snprintf(hidden[i],sizeof(hidden[i]),"%s-held",member[i]);
            BPTR exists=Lock((CONST_STRPTR)hidden[i],SHARED_LOCK);
            if(exists){UnLock(exists);error=EEXIST;break;}
            if(IoErr()!=ERROR_OBJECT_NOT_FOUND||!Rename((CONST_STRPTR)member[i],(CONST_STRPTR)hidden[i])){error=EIO;break;}
            moved++;
        }
        if(!error)error=storage_cold_once();
        while(moved){--moved;if(!Rename((CONST_STRPTR)hidden[moved],(CONST_STRPTR)member[moved])&&!error)error=EIO;}
        if(error)return error;
        char line[128];snprintf(line,sizeof(line),"Public cold dRAID rebuilt redundancy PASS missing=%u exact=1 restored=1\n",storage_missing+1);note(line);
    }
    /* Exceed parity (both leaves for mirror). A transport failure alone is
     * not proof of rejection: require a backend reply and responsive queries. */
    unsigned losses=storage_dspare?storage_members:storage_missing+1;
    for(unsigned i=0;i<losses;i++){
        storage_member(member[i],sizeof(member[i]),i);
        snprintf(hidden[i],sizeof(hidden[i]),"%s-held",member[i]);
        BPTR exists=Lock((CONST_STRPTR)hidden[i],SHARED_LOCK);
        if(exists){UnLock(exists);error=EEXIST;break;}
        if(IoErr()!=ERROR_OBJECT_NOT_FOUND||!Rename((CONST_STRPTR)member[i],(CONST_STRPTR)hidden[i])){error=EIO;break;}
        moved++;
    }
    if(!error){
        int refused=storage_alias_request(7,1);
        if(!refused||!storage_reply_received)error=EIO;
        if(!error)error=storage_no_pools();
        if(!error)error=verify_mounts(NULL,0);
    }
    while(moved){--moved;if(!Rename((CONST_STRPTR)hidden[moved],(CONST_STRPTR)member[moved])&&!error)error=EIO;}
    if(!error)error=storage_cold_once();
    if(!error){char line[160];snprintf(line,sizeof(line),"Public cold beyond-parity PASS missing=%u refused=1 empty=1 responsive=1 restored-read=1\n",losses);note(line);}
    return error;
}
static int storage_cold_release(void)
{
    int error=storage_request(7,"public_mirror",NULL,"readonly=off");
    const char *snapshots[]={"public_mirror/defer_pending@pending",
        "public_mirror/defer_pending/child@pending"};
    for(unsigned i=0;i<2&&!error;i++){
        char command[384];
        snprintf(command,sizeof(command),"C:zfs get -H -o value defer_destroy %s >T:cli-package-output.txt",snapshots[i]);
        error=cli_result(command,"on");
    }
    if(!error)error=storage_request_flags(56,"public_mirror/defer_pending@pending","cold_keep",NULL,OZ_CONFIRMED|OZ_RECURSIVE);
    for(unsigned phase=0;phase<2&&!error;phase++){
        if(phase){
            error=storage_request(8,"public_mirror",NULL,NULL);
            if(!error)error=storage_request(7,"public_mirror",NULL,"readonly=on");
        }
        for(unsigned i=0;i<2&&!error;i++){
            char command[384];
            snprintf(command,sizeof(command),"C:zfs list -t snapshot %s",snapshots[i]);
            if(!cli_command(command))error=EIO;
        }
        /* Successful queries distinguish disappearance from a broken CLI. */
        if(!error)error=cli_result("C:zfs get -H -o value type public_mirror/defer_pending/child >T:cli-package-output.txt","filesystem");
    }
    if(!error)error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZRELEASE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(!error){
        char bytes[sizeof(storage_payload)+1];
        BPTR file=Open((CONST_STRPTR)"OZRELEASE:hello.txt",MODE_OLDFILE);
        if(!file)error=EIO;
        else {
            LONG count=Read(file,bytes,sizeof(bytes));int closed=Close(file);
            if(!closed||count!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))error=EIO;
        }
        int detached=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
        if(!error)error=detached;
    }
    if(!error)error=storage_request(8,"public_mirror",NULL,NULL);
    if(!error)note("Public cold writable release PASS pending-before=1 recursive-release=1 parent-absent=1 child-absent=1 reimport-absent=1 exact-data=1 exported=1\n");
    return error;
}
int main(int argc,char **argv){
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-service-test-only",SHARED_LOCK);
    if(!marker)return 20;
    UnLock(marker);
    BPTR layout_file=Open((CONST_STRPTR)"SYS:openzfs-test-layout",MODE_OLDFILE);
    if(layout_file){
        char layout[16]={0};LONG count=Read(layout_file,layout,sizeof(layout)-1);Close(layout_file);
        if(count<=0)return 20;
        while(count>0&&(layout[count-1]=='\n'||layout[count-1]=='\r'))layout[--count]=0;
        if(!strcmp(layout,"raidz1")){storage_layout=OZ_RAIDZ1;storage_members=3;storage_missing=1;}
        else if(!strcmp(layout,"raidz2")){storage_layout=OZ_RAIDZ2;storage_members=4;storage_missing=2;}
        else if(!strcmp(layout,"raidz3")){storage_layout=OZ_RAIDZ3;storage_members=5;storage_missing=3;}
        else if(!strcmp(layout,"draid1")){storage_layout=OZ_DRAID;storage_members=3;storage_missing=1;}
        else if(!strcmp(layout,"draid2")){storage_layout=OZ_DRAID;storage_members=4;storage_missing=2;}
        else if(!strcmp(layout,"draid3")){storage_layout=OZ_DRAID;storage_members=5;storage_missing=3;}
        else if(strcmp(layout,"mirror"))return 20;
    }
    marker=Lock((CONST_STRPTR)"SYS:openzfs-native-file-fixture",SHARED_LOCK);
    if(marker){UnLock(marker);storage_directory="OZBACK:OZPublicPool";native_file_fixture=1;}
    marker=Lock((CONST_STRPTR)"SYS:openzfs-hosted-file-fixture",SHARED_LOCK);
    if(marker){
        UnLock(marker);storage_directory="EMU:OZPublicPool";
        marker=Lock((CONST_STRPTR)"SYS:openzfs-hosted-assign-fixture",SHARED_LOCK);
        if(marker){UnLock(marker);storage_directory="SYS:OZPublicPool";}
    }
    if(argc==2&&!strcmp(argv[1],"LEAK-SESSIONS")) {
        struct oz_ticket tickets[OZ_ADMISSION_SLOTS];
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
            if(oz_session_open(OZ_ADMISSION_PORT,&tickets[i]))return 20;
#ifdef OZ_NATIVE_DISPATCHER
            LONG abandoned_fd;
            if(oz_control_open(OZ_ADMISSION_PORT,&tickets[i],&abandoned_fd))return 20;
#endif
        }
        BPTR output=Open((CONST_STRPTR)"RAM:oz-abandoned-tickets",MODE_NEWFILE);
        if(!output)return 20;
        int okay=Write(output,tickets,sizeof(tickets))==sizeof(tickets)&&Flush(output);
        if(!Close(output))okay=0;
        /* Deliberately no session_close: the real task-removal hook must reap. */
        return okay?0:20;
    }
#ifdef OZ_TEST_INVENTORY_WORKER
    if(oz_inventory_worker_test())return 20;
#endif
    struct oz_client client;int error=0,opened=0;
    struct Library *secBase=NULL;int logged=0;
    int gui=argc==2&&!strcmp(argv[1],"GUI");
    int native_cold=argc==2&&!strcmp(argv[1],"NATIVE-COLD");
    int cold_release=argc==2&&!strcmp(argv[1],"STORAGE-COLD-RELEASE");
    int cold=cold_release||(argc==2&&!strcmp(argv[1],"STORAGE-COLD"));
    marker=Lock((CONST_STRPTR)"SYS:openzfs-sequential-test",SHARED_LOCK);
    if(marker){UnLock(marker);storage_sequential=1;}
    marker=Lock((CONST_STRPTR)"SYS:openzfs-dspare-test",SHARED_LOCK);
    if(marker){
        UnLock(marker);storage_dspare=1;
        if(!native_file_fixture||storage_layout!=OZ_DRAID)return 20;
    }
    if(storage_sequential&&!storage_dspare)return 20;
    marker=Lock((CONST_STRPTR)"SYS:openzfs-replacement-test",SHARED_LOCK);
    if(marker){
        UnLock(marker);storage_replace=1;storage_replaced=cold;
        if(!native_file_fixture||storage_layout!=OZ_MIRROR)return 20;
    }
    if(cold&&!strcmp(storage_directory,"RAM:OZPublicPool"))return 20;
    int storage=native_cold||cold||gui||(argc==2&&!strcmp(argv[1],"STORAGE"));
    int startup=storage||(argc==2&&!strcmp(argv[1],"STARTUP"));
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    if(startup){
        CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceNative INVALID",TAG_DONE)==20);
        CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
        secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        CHECK(secLoginA(reader)==((77UL<<16)|88));logged=1;
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceNative WRITABLE",TAG_DONE)==20);
        CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
        secLogoutA(quiet);logged=0;
        CHECK(secLoginA(root)==secOWNER_SYSTEM);logged=1;
        CHECK(!companion_configuration_test());
        CHECK(!companion_persistent_settings(cold));
        CHECK(SystemTags((CONST_STRPTR)"Run >T:service-writable.log C:OpenZFSServiceNative WRITABLE",TAG_DONE)==0);
        note("Public startup invalid-mode and non-root denial PASS\n");
    }
    for(unsigned i=0;i<600&&!present(OZ_PORT);i++)Delay(5);
    CHECK(present(OZ_PORT));
    CHECK(oz_client_open(&client));opened=1;
    CHECK(!strcmp(client.server_name,OZ_PORT));
    for(unsigned op=1;op<=3;op++){
        static struct oz_message request;
        memset(&request,0,sizeof(request));request.operation=op;
        CHECK(oz_submit(&client,&request)==OZ_OK);
        struct oz_message *reply=NULL;
        while(!(reply=oz_poll(&client)))Delay(1);
        int valid=reply->status==OZ_OK&&reply->engine_available&&reply->row_count==0;
        if(op==2)valid=valid&&!!(reply->capability_words[0]&(1UL<<6))==startup;
        char line[192];snprintf(line,sizeof(line),"Public service op=%u status=%lu engine=%lu rows=%lu\n",op,
            (unsigned long)reply->status,(unsigned long)reply->engine_available,(unsigned long)reply->row_count);
        note(line);oz_message_free(reply);CHECK(valid);
    }
    CHECK(present(OZ_ADMISSION_PORT));
    CHECK(!present(OZ_NATIVE_TEST_PORT)&&!present(OZ_ADMISSION_TEST_PORT));
    if(startup) {
        struct oz_ticket sessions[OZ_ADMISSION_SLOTS],extra={0};
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
            int session_error=oz_session_open(OZ_ADMISSION_PORT,&sessions[i]);
            if(session_error){char line[80];snprintf(line,sizeof(line),"Public session open error=%d slot=%u\n",session_error,i);note(line);}
            CHECK(!session_error);
            CHECK(sessions[i].epoch&&sessions[i].number);
            if(i)CHECK(sessions[i].epoch==sessions[0].epoch&&sessions[i].number>sessions[i-1].number);
        }
        CHECK(oz_session_open(OZ_ADMISSION_PORT,&extra)==EBUSY);
        CHECK(!extra.epoch&&!extra.number);
        struct oz_ticket stale=sessions[0];stale.epoch^=1;
        CHECK(oz_session_close(OZ_ADMISSION_PORT,&stale)==ENOENT);
#ifdef OZ_NATIVE_DISPATCHER
        LONG descriptors[64],replacement=-1;
        CHECK(oz_control_open(OZ_ADMISSION_PORT,&stale,&replacement)==ENOENT && replacement==-1);
        for(unsigned i=0;i<64;i++) {
            CHECK(!oz_control_open(OZ_ADMISSION_PORT,&sessions[0],&descriptors[i]));
            CHECK(descriptors[i]>0 && (!i||descriptors[i]>descriptors[i-1]));
        }
        CHECK(oz_control_open(OZ_ADMISSION_PORT,&sessions[0],&replacement)==EMFILE && replacement==-1);
        CHECK(oz_control_close(OZ_ADMISSION_PORT,&sessions[1],descriptors[0])==EBADF);
        CHECK(!oz_control_close(OZ_ADMISSION_PORT,&sessions[0],descriptors[0]));
        CHECK(oz_control_close(OZ_ADMISSION_PORT,&sessions[0],descriptors[0])==EBADF);
        CHECK(!oz_control_open(OZ_ADMISSION_PORT,&sessions[0],&replacement));
        CHECK(replacement>descriptors[63]);
#endif
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        secLogoutA(quiet);logged=0;
        CHECK(secLoginA(reader)==((77UL<<16)|88));logged=1;
        CHECK(oz_session_close(OZ_ADMISSION_PORT,&sessions[0])==EACCES);
#ifdef OZ_NATIVE_DISPATCHER
        LONG denied_fd=-1;
        CHECK(oz_control_open(OZ_ADMISSION_PORT,&sessions[0],&denied_fd)==EACCES && denied_fd==-1);
        CHECK(oz_control_close(OZ_ADMISSION_PORT,&sessions[0],replacement)==EACCES);
#endif
        secLogoutA(quiet);logged=0;
        CHECK(secLoginA(root)==secOWNER_SYSTEM);logged=1;
        Forbid();struct MsgPort *active=FindPort((CONST_STRPTR)OZ_PORT);
        if(active)Signal(active->mp_SigTask,SIGBREAKF_CTRL_C);
        Permit();CHECK(active);
        Delay(25);CHECK(present(OZ_PORT)&&present(OZ_ADMISSION_PORT));
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
            CHECK(!oz_session_close(OZ_ADMISSION_PORT,&sessions[i]));
        CHECK(oz_session_close(OZ_ADMISSION_PORT,&sessions[0])==ENOENT);
#ifdef OZ_NATIVE_DISPATCHER
        CHECK(oz_control_close(OZ_ADMISSION_PORT,&sessions[0],replacement)==ENOENT);
        note("Public native control descriptors PASS capacity=64 ownership=1 stale=1 session-cleanup=1\n");
#endif
        CHECK(!oz_session_open(OZ_ADMISSION_PORT,&extra));
        CHECK(extra.epoch==sessions[0].epoch&&extra.number>sessions[OZ_ADMISSION_SLOTS-1].number);
        CHECK(!oz_session_close(OZ_ADMISSION_PORT,&extra));
        secLogoutA(quiet);logged=0;
        CHECK(secLoginA(reader)==((77UL<<16)|88));logged=1;
        CHECK(!oz_session_open(OZ_ADMISSION_PORT,&extra));
        CHECK(!oz_session_close(OZ_ADMISSION_PORT,&extra));
        secLogoutA(quiet);logged=0;
        CHECK(secLoginA(root)==secOWNER_SYSTEM);logged=1;
        for(unsigned cycle=0;cycle<4;cycle++) {
            LONG status=-1;
            BPTR segment=LoadSeg((CONST_STRPTR)"C:OpenZFSPublicServiceTest");CHECK(segment);
            struct Process *child=CreateNewProcTags(NP_Seglist,segment,
                NP_FreeSeglist,FALSE,NP_Cli,TRUE,NP_StackSize,262144,
                NP_Name,(IPTR)"OpenZFS abandoned-session child",
                NP_Arguments,(IPTR)"LEAK-SESSIONS\n",NP_Synchronous,TRUE,
                NP_ExitCode,(IPTR)child_exited,NP_ExitData,(IPTR)&status,TAG_DONE);
            UnLoadSeg(segment);CHECK(child&&status==0);
            struct oz_ticket abandoned[OZ_ADMISSION_SLOTS];
            BPTR input=Open((CONST_STRPTR)"RAM:oz-abandoned-tickets",MODE_OLDFILE);CHECK(input);
            LONG bytes=Read(input,abandoned,sizeof(abandoned));
            int closed=Close(input);CHECK(bytes==sizeof(abandoned)&&closed);
            for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
                CHECK(oz_session_close(OZ_ADMISSION_PORT,&abandoned[i])==ENOENT);
            CHECK(!oz_session_open(OZ_ADMISSION_PORT,&extra));
            CHECK(!oz_session_close(OZ_ADMISSION_PORT,&extra));
        }
        note("Public task-exit session reclamation PASS cycles=4 sessions=32\n");
        note("Public authenticated sessions ownership, capacity, stale handles and shutdown guard PASS\n");
    }
    /* Even a read-only mount publishes a DOS endpoint and therefore requires
     * admission. Confirmation is not caller authentication. No pool exists. */
    {
        static struct oz_message request;memset(&request,0,sizeof(request));
        request.operation=43;request.flags=OZ_CONFIRMED;
        strcpy(request.target,"not-imported/never");strcpy(request.argument,"OZNEVER");
        strcpy(request.options,"readonly=on");
        CHECK(oz_submit(&client,&request)==OZ_OK);
        struct oz_message *reply=NULL;while(!(reply=oz_poll(&client)))Delay(1);
        int denied=reply->status==OZ_UNSUPPORTED&&strstr(reply->detail,"authenticated admission")!=NULL;
        oz_message_free(reply);CHECK(denied);
        note("Public mutation requires authenticated admission PASS\n");
    }
    if(storage)CHECK(!(native_cold?storage_native_cold(0):cold_release?storage_cold_release():cold?storage_cold_test():storage_test(gui)));
    if(storage&&!native_cold&&!cold&&!gui)CHECK(!storage_native_cold(1));
    if(storage&&native_file_fixture){
        /* Close the disposable FAT backing volume after all pools export.
         * DOS file Flush does not drain deferred FAT directory renames.
         * The runner may quit QEMU as soon as it sees the final PASS. */
        CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        note("Public backing clean detach PASS\n");
    }
    /* Signal only the service addressed and verified above in the marked,
     * diskless private guest. No mounted endpoints or pools exist here. */
    Forbid();struct MsgPort *port=FindPort((CONST_STRPTR)OZ_PORT);
    if(port)Signal(port->mp_SigTask,SIGBREAKF_CTRL_C);
    Permit();CHECK(port);
    if(storage){
        /* Retired DOS ports cannot yet be reclaimed safely. Verify that the
         * service remains resident rather than freeing clients' endpoints. */
        Delay(25);CHECK(present(OZ_PORT)&&present(OZ_ADMISSION_PORT));
        note("Public service retains retired DOS endpoints after export\n");
        goto done;
    }
    for(unsigned i=0;i<600&&(present(OZ_PORT)||present(OZ_ADMISSION_PORT));i++)Delay(5);
    CHECK(!present(OZ_PORT)&&!present(OZ_ADMISSION_PORT));
done:
    if(opened)oz_client_close(&client);
    if(logged){struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};secLogoutA(quiet);}
    if(secBase)CloseLibrary(secBase);
    {char line[128];snprintf(line,sizeof(line),"Public service test line=%d\nRESULT public service %s %s\n",error,storage?"storage":"lifecycle",error?"FAIL":"PASS");note(line);}
    return error?20:0;
}
