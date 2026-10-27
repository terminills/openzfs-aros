/* SPDX-License-Identifier: CDDL-1.0 */
/* Readonly acceptance against the explicitly supplied disposable mirror. */
#include "../ui/api.h"
#include "../ui/admission-api.h"
#include "../ui/mount-api.h"
#include "../ui/handler-api.h"
#include "../ui/gpt-api.h"
#include "../ui/partition-api.h"
#include "../ui/admission-worker.h"
#include "../ui/startup-import.h"
#include "../ui/startup-settings.h"
#include "../ui/settings-file.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef OZ_NATIVE_IMPORT_REWIND_NORMAL
#define OZ_REWIND_FLAG "F"
#define OZ_REWIND_POLICY "safe"
#else
#define OZ_REWIND_FLAG "FX"
#define OZ_REWIND_POLICY "extreme"
#endif
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
#include <libraries/poseidon.h>
#include <proto/poseidon.h>
#endif
#ifndef OZ_NATIVE_USB_RECOVERY_ROUNDS
#define OZ_NATIVE_USB_RECOVERY_ROUNDS 1
#endif
#ifdef OZ_NATIVE_USB_MEMBERS
#define OZ_MEMBER_DEVICE "usbscsi.device"
#elif defined(OZ_NATIVE_AHCI_MEMBERS)
#define OZ_MEMBER_DEVICE "ahci.device"
#else
#define OZ_MEMBER_DEVICE "ata.device"
#endif
static void note(const char *s){while(*s)RawPutChar(*s++);RawPutChar('\n');}
#ifdef OZ_NATIVE_DOS_CACHE_PRESSURE
static int dos_cache_pressure(const char *volume, int create)
{
    char directory[80], path[120];
    snprintf(directory,sizeof(directory),"%s:cache-pressure",volume);
    if(create){
        BPTR dir=CreateDir((CONST_STRPTR)directory);
        if(!dir)return EIO;
        UnLock(dir);
        for(unsigned i=0;i<320;i++){
            snprintf(path,sizeof(path),"%s/item-%u",directory,i);
            UQUAD payload=0x12340000ULL+i;
            BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);
            if(!file)return EIO;
            int good=Write(file,&payload,sizeof(payload))==sizeof(payload);
            if(!Flush(file))good=0;
            if(!Close(file))good=0;
            if(!good)return EIO;
        }
    }
    for(unsigned pass=0;pass<3;pass++){
        BPTR dir=Lock((CONST_STRPTR)directory,ACCESS_READ);
        if(!dir)return EIO;
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,TAG_DONE);
        if(!fib){UnLock(dir);return ENOMEM;}
        unsigned count=0;unsigned char seen[320]={0};
        int error=Examine(dir,fib)?0:EIO;
        while(!error&&ExNext(dir,fib)){
            unsigned i;char extra;
            if(sscanf(fib->fib_FileName,"item-%u%c",&i,&extra)!=1||
                i>=320||seen[i]||fib->fib_DirEntryType>=0||
                fib->fib_Size!=sizeof(UQUAD)){error=EIO;break;}
            seen[i]=1;count++;
        }
        if(!error&&(IoErr()!=ERROR_NO_MORE_ENTRIES||count!=320))error=EIO;
        FreeDosObject(DOS_FIB,fib);UnLock(dir);
        if(error)return error;
        for(unsigned i=0;i<320;i++){
            snprintf(path,sizeof(path),"%s/item-%u",directory,i);
            UQUAD payload=0;
            BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);
            if(!file)return EIO;
            int good=Read(file,&payload,sizeof(payload))==sizeof(payload)&&
                payload==0x12340000ULL+i;
            if(!Close(file))good=0;
            if(!good)return EIO;
        }
    }
    return 0;
}
#endif
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
static void usb_stack_errors(void)
{
    struct Library *ps=OpenLibrary((CONST_STRPTR)"poseidon.library",4);
    if(!ps){note("USB error snapshot unavailable: poseidon.library");return;}
    char (*lines)[384]=calloc(64,sizeof(*lines));
    if(!lines){CloseLibrary(ps);return;}
    unsigned count=0;
    struct List *list=NULL;
    psdLockReadPBase();
    psdGetAttrs(PGA_STACK,NULL,PA_ErrorMsgList,&list,TAG_END);
    if(list)for(struct Node *node=list->lh_TailPred;node->ln_Pred&&count<64;node=node->ln_Pred){
        IPTR level=0;STRPTR origin=NULL,message=NULL;
        psdGetAttrs(PGA_ERRORMSG,node,EMA_Level,&level,EMA_Origin,&origin,EMA_Msg,&message,TAG_END);
        snprintf(lines[count++],384,"USB stack error level=%lu origin=%.64s message=%.256s",
            (unsigned long)level,origin?(const char *)origin:"?",message?(const char *)message:"?");
    }
    psdUnlockPBase();
    /* Copy while protected, print only after releasing the stack lock. */
    note("USB stack error snapshot: newest first, at most 64 records");
    for(unsigned i=0;i<count;i++)note(lines[i]);
    free(lines);CloseLibrary(ps);
}
static int reinserted_partitions_ready(void)
{
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog)return ENOMEM;
    int errors[2]={ENODEV,ENODEV};
    for(unsigned attempt=0;attempt<30;attempt++){
        for(unsigned unit=0;unit<2;unit++){
            errors[unit]=oz_partitions_read(OZ_ADMISSION_PORT,OZ_MEMBER_DEVICE,unit,catalog);
            if(!errors[unit]){
                int found=0;
                for(unsigned i=0;i<catalog->count;i++)
                    if(catalog->entries[i].position==0)found=1;
                if(!found)errors[unit]=ENOENT;
            }
        }
        if(!errors[0]&&!errors[1]){
            free(catalog);
            note("RESULT native USB reinsertion readiness PASS partition-catalogs=2 readonly-probe=1");
            return 0;
        }
        char line[128];snprintf(line,sizeof(line),
            "USB reinsertion readiness attempt=%u unit0=%d unit1=%d",
            attempt+1,errors[0],errors[1]);note(line);
        Delay(50);
    }
    usb_stack_errors();
    /* Diagnose unit reassignment without adopting any alternate device. */
    for(unsigned unit=0;unit<8;unit++){
        struct oz_device_geometry geometry={0};
        int error=oz_device_geometry_read(OZ_ADMISSION_PORT,OZ_MEMBER_DEVICE,unit,&geometry);
        char line[160];snprintf(line,sizeof(line),
            "USB readiness failure geometry unit=%u errno=%d bytes=%llu sector=%u",
            unit,error,(unsigned long long)geometry.bytes,(unsigned)geometry.sector_size);
        note(line);
    }
    free(catalog);return errors[0]?errors[0]:errors[1];
}
#endif
#ifdef OZ_QEMU_COMPLETION_PORT
/* Private QEMU fixture only. Keep the final result off the shared SMP serial
 * stream; this is not a production device or filesystem interface. */
static void complete(int error)
{
    char record[96];snprintf(record,sizeof(record),
        "OZ-NATIVE-COMPLETION-v1 %s line=%d\n",error?"FAIL":"PASS",error);
    for(const unsigned char *p=(const unsigned char *)record;*p;p++)
        __asm__ volatile("outb %0,%1" : : "a"(*p),"Nd"(OZ_QEMU_COMPLETION_PORT) : "memory");
}
#endif
#ifdef OZ_NATIVE_EMPTY_ZVOL
static int empty_zvol_before_service(void)
{
    Forbid();int exists=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();
    if(exists)return 1;
    if(SystemTags((CONST_STRPTR)"Run >T:empty-device.log C:OpenZFSDevice EMPTY",TAG_DONE))return 2;
    struct oz_mapping_catalog catalog;
    int operation=0;
    for(unsigned attempt=0;attempt<500;attempt++){
        struct oz_mapping_worker *worker=NULL;
        if(oz_mapping_worker_start(&worker))return 3;
        int error;
        while((error=oz_mapping_worker_poll(worker,&catalog,&operation))==EAGAIN)Delay(1);
        if(error)return 4;
        if(oz_mapping_worker_free(worker))return 5;
        if(!operation)break;
        if(operation!=ENXIO)return 6;
        Delay(1);
    }
    if(operation||catalog.count!=32)return 7;
    for(unsigned i=0;i<32;i++)
        if(catalog.units[i].flags||catalog.units[i].bytes||catalog.units[i].dataset[0])return 8;
    struct oz_mapping_worker *worker=NULL;
    if(oz_mapping_worker_remap_start(0,"qemu_mirror/device-volume",0,&worker))return 9;
    int error;
    while((error=oz_mapping_worker_poll(worker,&catalog,&operation))==EAGAIN)Delay(1);
    if(error)return 10;
    int remapped=oz_mapping_worker_remapped(worker);
    if(oz_mapping_worker_free(worker)||!operation||remapped)return 11;
    if(oz_mapping_worker_start(&worker))return 12;
    while((error=oz_mapping_worker_poll(worker,&catalog,&operation))==EAGAIN)Delay(1);
    if(error)return 13;
    if(oz_mapping_worker_free(worker)||operation||catalog.count!=32)return 14;
    for(unsigned i=0;i<32;i++)
        if(catalog.units[i].flags||catalog.units[i].bytes||catalog.units[i].dataset[0])return 15;
    Forbid();exists=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();
    if(exists)return 16;
    note("RESULT native zvol preservice startup PASS absent=32 no-service=1 remap-refused=1");
    return 0;
}
#endif
#if defined(OZ_NATIVE_GPT_TEST) || defined(OZ_NATIVE_GPT_READONLY)
/* Exercise the exact manager worker, including input ownership, error output
 * preservation and refusal to free before a successful join/poll. */
static int gpt_via_worker(const char *endpoint,struct oz_gpt_query *query)
{
    struct oz_gpt_worker *worker=NULL;
    struct oz_gpt_query input=*query,result;
    memset(&result,0xa5,sizeof(result));
    struct oz_gpt_query untouched=result;
    int error=oz_gpt_worker_start(endpoint,&input,&worker);
    if(error)return error;
    memset(&input,0,sizeof(input));
    int lifetime_error=oz_gpt_worker_free(worker)!=EBUSY;
    int operation_error=0;
    do {error=oz_gpt_worker_poll(worker,&result,&operation_error);
        if(error)Delay(1);
    } while(error); /* Never release storage while the worker may still use it. */
    if(operation_error&&memcmp(&result,&untouched,sizeof(result)))lifetime_error=1;
    if(oz_gpt_worker_free(worker))lifetime_error=1;
    if(lifetime_error)return EPROTO;
    if(!operation_error)*query=result;
    return operation_error;
}
#define oz_gpt_request gpt_via_worker
#endif
static int request_paths(unsigned op,const char *options,int expected,unsigned first,unsigned second)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;strcpy(m->target,"qemu_mirror");
    if(op==37||op==38)strcpy(m->target,"qemu_mirror/api-names");
    if(op==50)strcpy(m->target,"qemu_mirror/files@before");
    if(options){
        if(op==17)strcpy(m->argument,options); /* clear takes a vdev GUID */
        else strcpy(m->options,options);
    }
    if(op==5){m->topology.group_count=1;m->topology.ashift=12;
        m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_MIRROR;}
    if(op==7||op==5){m->topology.device_count=2;
        for(unsigned i=0;i<2;i++)snprintf(m->topology.devices[i].path,256,
            "/AROS/" OZ_MEMBER_DEVICE "/%u/partition/0",i?second:first);}
#if defined(OZ_NATIVE_IMPORT_RAIDZ) || defined(OZ_NATIVE_IMPORT_RAIDZ1)
    if(op==7){m->topology.device_count=3;
        snprintf(m->topology.devices[2].path,256,"/AROS/ata.device/%u/partition/0",
            first==0&&second==1?3:101);}
#endif
#ifdef OZ_NATIVE_IMPORT_RAIDZ3
    if(op==7){m->topology.device_count=4;
        snprintf(m->topology.devices[3].path,256,"/AROS/ahci.device/%u/partition/0",
            first==0&&second==1?0:102);}
#endif
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error)do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error){note(out->detail);char wanted[32];snprintf(wanted,sizeof(wanted),"errno=%d",expected);
        if(!expected&&options&&strstr(options,"assessment=on")&&
           (out->row_count!=1||strcmp(out->rows[0].kind,"recovery-assessment")||
#ifdef OZ_NATIVE_IMPORT_ASSESS_HEALTHY
            strcmp(out->rows[0].state,"CURRENT")
#else
            strcmp(out->rows[0].state,"RECOVERABLE")||!out->rows[0].used
#endif
           ))error=EIO;
        const char *reported=strstr(out->detail,"errno=");
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!reported||strcmp(reported,wanted))error=EIO;
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        if(!error)error=released;}
    if(error){char line[96];snprintf(line,sizeof(line),"Native request op=%u error=%d",op,error);note(line);}
    free(m);free(out);return error;
}
static int request(unsigned op,const char *options,int expected)
{return request_paths(op,options,expected,0,1);}
#ifdef OZ_NATIVE_IMPORT_REWIND_MANAGER
static int recovery_parser_test(void)
{
    static const struct {const char *text;int valid,mode,ro;} cases[]={
        {"readonly=on;recovery=none",1,OZ_IMPORT_CURRENT,1},
        {"readonly=on;recovery=safe",1,OZ_IMPORT_RECOVER,1},
        {"readonly=off;recovery=extreme;guid=123456789abcdef0",1,OZ_IMPORT_EXTREME,0},
        {"readonly=on;rewind=on;recovery=extreme",0,0,0},
        {"readonly=on;recovery=safe;recovery=extreme",0,0,0},
        {"readonly=on;recovery=unknown",0,0,0},
        {"recovery=extreme",0,0,0},{"readonly=on;recovery=extreme;",0,0,0}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        struct oz_import_policy policy,before;
        memset(&policy,0xa5,sizeof(policy));before=policy;
        int valid=oz_parse_import_policy(cases[i].text,&policy);
        if(valid!=cases[i].valid)return EINVAL;
        if(!valid&&memcmp(&policy,&before,sizeof(policy)))return EINVAL;
        if(valid&&(policy.readonly!=cases[i].ro||policy.recovery!=cases[i].mode))return EINVAL;
        if(i==2&&policy.expected_guid!=0x123456789abcdef0ULL)return EINVAL;
    }
    int ro=-1,destroyed=-1,checkpoint=-1;UQUAD guid=~0ULL;
    if(oz_parse_import_identity("readonly=on;recovery=extreme",&ro,&destroyed,&checkpoint,&guid)||
        ro!=-1||destroyed!=-1||checkpoint!=-1||guid!=~0ULL)return EINVAL;
    note("RESULT native recovery parser PASS modes=3 guid=1 malformed=5 unchanged=1 legacy-refused=1");
    static const char *invalid[]={"readonly=off;recovery=safe;assessment=on",
        "readonly=on;assessment=on","readonly=on;rewind=on;assessment=on",
        "readonly=on;recovery=extreme;assessment=on;assessment=off",
        "readonly=on;recovery=safe;assessment=maybe"};
    struct oz_import_policy assessment;
    if(!oz_parse_import_policy("readonly=on;recovery=safe;assessment=on",&assessment)||
       !assessment.assessment||assessment.recovery!=OZ_IMPORT_RECOVER)return EINVAL;
    if(!oz_parse_import_policy("readonly=on;recovery=extreme;assessment=on",&assessment)||
       !assessment.assessment||assessment.recovery!=OZ_IMPORT_EXTREME)return EINVAL;
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++) {
        struct oz_import_policy before=assessment;
        if(oz_parse_import_policy(invalid[i],&assessment)||memcmp(&before,&assessment,sizeof(before)))return EINVAL;
    }
    note("RESULT native assessment parser PASS modes=2 malformed=5 unchanged=1");
    return 0;
}
#endif
#ifdef OZ_NATIVE_USB_SUSPEND_GUARD
static int suspended_history(unsigned raw)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=33;m->flags=OZ_CONFIRMED|(raw?OZ_RAW:0);
    strcpy(m->target,"qemu_mirror");
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error)do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error) {
        char wanted[32],line[256];snprintf(wanted,sizeof(wanted),"errno=%d",EAGAIN);
        snprintf(line,sizeof(line),"Suspended history raw=%u status=%u rows=%u next=%llu detail=%s",
            raw,(unsigned)out->status,(unsigned)out->row_count,
            (unsigned long long)out->next_cursor,out->detail);note(line);
        const char *reported=strstr(out->detail,"errno=");
        if(out->status!=OZ_ENGINE_ERROR||out->row_count||out->next_cursor||
            !reported||strcmp(reported,wanted))error=EIO;
    }
    if(ticket.number){int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;}
    free(m);free(out);return error;
}
#endif
static int health_expected(const char *expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return ENOMEM;
    struct oz_client client;
    if(!oz_client_open(&client)){free(m);return ENOMEM;}
    m->operation=4;strcpy(m->target,"qemu_mirror");
    int error=oz_submit(&client,m)==OZ_OK?0:EIO;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error&&(out->status!=OZ_OK||!out->row_count||
        strcmp(out->rows[0].state,expected)
        ))error=EIO;
    if(!error)note(out->rows[0].state);
    else if(out){char diagnostic[192];snprintf(diagnostic,sizeof(diagnostic),
        "Health mismatch expected=%s status=%u rows=%u actual=%s",
        expected,(unsigned)out->status,(unsigned)out->row_count,
        out->row_count?out->rows[0].state:"<none>");note(diagnostic);}
    free(m);if(out)oz_message_free(out);oz_client_close(&client);return error;
}
static int pool_list_filter(void)
{
    static const char *names[]={"","qemu_mirror","qemu_mir","qemu_*","qemu_mirror"};
    struct oz_client client;
    struct oz_message *m=calloc(1,sizeof(*m));
    if(!m)return ENOMEM;
    if(!oz_client_open(&client)){free(m);return ENOMEM;}
    int error=0;
    for(unsigned i=0;i<5&&!error;i++) {
        memset(m,0,sizeof(*m));m->operation=3;
        strcpy(m->target,names[i]);m->cursor=i==4?1:0;
        struct oz_message *out=NULL;
        if(oz_submit(&client,m)!=OZ_OK){error=EIO;break;}
        while(!(out=oz_poll(&client)))Delay(1);
        unsigned expected=i<2?1:0;
        if(out->status!=OZ_OK||out->row_count!=expected||out->next_cursor||
            (expected&&(strcmp(out->rows[0].name,"qemu_mirror")||!out->rows[0].total)))error=EIO;
        char line[192];snprintf(line,sizeof(line),
            "Pool list exact filter case=%u status=%u rows=%u error=%d",
            i,(unsigned)out->status,(unsigned)out->row_count,error);note(line);
        oz_message_free(out);
    }
    free(m);oz_client_close(&client);return error;
}
static int health(void)
{
    int error=pool_list_filter();
    if(error)return error;
#ifdef OZ_NATIVE_IMPORT_DEGRADED
    return health_expected("DEGRADED");
#else
    return health_expected("ONLINE");
#endif
}
static int pool_guid(UQUAD *guid)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return ENOMEM;
    if(!oz_client_open(&client)){free(m);return ENOMEM;}
    m->operation=31;strcpy(m->target,"qemu_mirror");strcpy(m->argument,"guid");
    int error=oz_submit(&client,m)==OZ_OK?0:EIO;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error&&(out->status!=OZ_OK||out->row_count!=1||strcmp(out->rows[0].name,"guid")))error=EIO;
    if(!error){char *end;errno=0;*guid=strtoull(out->rows[0].value,&end,10);
        if(errno||!*guid||*end)error=EINVAL;}
    free(m);if(out)oz_message_free(out);oz_client_close(&client);return error;
}
#ifdef OZ_NATIVE_KEEP_NAMES
static int namespace_property(const char *name,const char *expected)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return ENOMEM;
    if(!oz_client_open(&client)){free(m);return ENOMEM;}
    m->operation=40;strcpy(m->target,"qemu_mirror/api-names");
    snprintf(m->argument,sizeof(m->argument),"%s",name);
    int error=oz_submit(&client,m)==OZ_OK?0:EIO;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(out){char line[512];snprintf(line,sizeof(line),"Namespace query %s status=%u rows=%u name=%s value=%s detail=%s",
        name,(unsigned)out->status,(unsigned)out->row_count,out->row_count?out->rows[0].name:"",
        out->row_count?out->rows[0].value:"",out->detail);note(line);}
    if(!error&&(out->status!=OZ_OK||out->row_count!=1||
        strcmp(out->rows[0].name,name)||strcmp(out->rows[0].value,expected)))error=EIO;
    if(!error){char line[128];snprintf(line,sizeof(line),"Persisted namespace property %s=%s",name,expected);note(line);}
    free(m);if(out)oz_message_free(out);oz_client_close(&client);return error;
}
#endif
int main(void)
{
    int error=0;BPTR file=0;struct Library *secBase=NULL;
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
#ifdef OZ_QEMU_COMPLETION_FAIL
    CHECK(0); /* Explicit harness failure qualification, never a release flag. */
#endif
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase&&secIsConfigured());
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    CHECK(secLoginA(login)==secOWNER_SYSTEM);
#ifdef OZ_NATIVE_ENGINE_LIBRARY_LIFETIME
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSLibraryTest",TAG_DONE)==0);
#endif
#ifdef OZ_NATIVE_INVENTORY
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSInventory",TAG_DONE)==0);
    extern int oz_inventory_worker_test(void);
    CHECK(!oz_inventory_worker_test());
#endif
#ifdef OZ_NATIVE_SERVICE_RELEASE
    {
        const char *modes[]={"TEST-DROP","TEST-WRITE","TEST-NATIVE","UNKNOWN","WRITABLE EXTRA"};
        for(unsigned i=0;i<sizeof(modes)/sizeof(modes[0]);i++){
            char command[160];snprintf(command,sizeof(command),"C:OpenZFSServiceNative %s",modes[i]);
            CHECK(SystemTags((CONST_STRPTR)command,TAG_DONE)==20);
        }
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);CHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=SystemTags((CONST_STRPTR)"C:OpenZFSServiceNative WRITABLE",TAG_DONE);
        secLogoutA(quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);CHECK(denied==20);
        const char *ports[]={OZ_PORT,OZ_TEST_PORT,OZ_NATIVE_TEST_PORT,OZ_ADMISSION_PORT,OZ_ADMISSION_TEST_PORT};
        for(unsigned i=0;i<sizeof(ports)/sizeof(ports[0]);i++){
            char starting[160];snprintf(starting,sizeof(starting),"%s.Starting",ports[i]);
            Forbid();int exists=FindPort((CONST_STRPTR)ports[i])!=NULL||FindPort((CONST_STRPTR)starting)!=NULL;Permit();
            CHECK(!exists);
        }
        note("RESULT native service release startup PASS diagnostic-modes-refused=3 malformed-refused=2 unauthorized-writable-refused=1 no-ports=1");
    }
#endif
#ifdef OZ_NATIVE_EMPTY_ZVOL
    CHECK(!empty_zvol_before_service());
#endif
    CHECK(SystemTags((CONST_STRPTR)
#ifdef OZ_NATIVE_GPT_READONLY
        "Run >T:service.log C:OpenZFSServiceNative"
#else
        "Run >T:service.log C:OpenZFSServiceNative WRITABLE"
#endif
        ,TAG_DONE)==0);
    for(unsigned i=0;i<1500;i++){Forbid();int ready=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();if(ready)break;Delay(1);}
#ifdef OZ_NATIVE_IMPORT_REWIND
#ifdef OZ_NATIVE_IMPORT_REWIND_SEED
    /* Make adjacent dirty transaction roots on disposable good media.
     * Deliberately leave the pool imported: the harness stops this private
     * VM after completion instead of adding export-time transactions. */
    CHECK(!request(7,"readonly=off",0));
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZSEED",OZ_MOUNT_ATTACH,0));
    for(unsigned generation=0;generation<8;generation++) {
        file=Open((CONST_STRPTR)"OZSEED:recovery-seed",MODE_NEWFILE);CHECK(file);
        CHECK(Write(file,&generation,sizeof(generation))==sizeof(generation));
        CHECK(Flush(file));CHECK(Close(file));file=0;
        CHECK(SystemTags((CONST_STRPTR)"C:zpool sync qemu_mirror",TAG_DONE)==0);
    }
    note("RESULT native recovery seed PASS generations=8 sync=8 exported=0");
    goto done;
#endif
#ifdef OZ_NATIVE_IMPORT_REWIND_MANAGER
    CHECK(!recovery_parser_test());
    {
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);CHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=request(7,"readonly=on;recovery=" OZ_REWIND_POLICY,EPERM);
        int assessment_denied=request(7,"readonly=on;recovery=" OZ_REWIND_POLICY ";assessment=on",EPERM);
        secLogoutA(quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(!denied&&!assessment_denied);
        note("RESULT native Manager recovery authorization PASS nonroot-denied=1 root-restored=1");
    }
#endif
#ifndef OZ_NATIVE_IMPORT_REWIND_COLD
#ifdef OZ_NATIVE_IMPORT_ASSESSMENT
    CHECK(!request(7,"readonly=on;recovery=" OZ_REWIND_POLICY ";assessment=on;guid=0000000000000001",EINVAL));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    note("RESULT native assessment identity PASS mismatch-refused=1 pool-absent=1");
    for(unsigned assessment_cycle=0;assessment_cycle<2;assessment_cycle++) {
        CHECK(!request(7,"readonly=on;recovery=" OZ_REWIND_POLICY ";assessment=on",0));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    }
    note("RESULT native Manager assessment PASS cycles=2 structured=2 pool-absent=2");
#ifdef OZ_NATIVE_IMPORT_ASSESS_HEALTHY
    note("RESULT native Manager healthy assessment PASS current=2 unloaded=2");
    goto done;
#endif
#endif
#ifdef OZ_NATIVE_IMPORT_REWIND_DRYRUN
    /* Upstream reports a recoverable dry-run through load-info while
     * returning failure: status alone cannot distinguish it from an error. */
    file=Open((CONST_STRPTR)"T:recovery-assessment",MODE_NEWFILE);CHECK(file);
    LONG assessment_result=SystemTags((CONST_STRPTR)"C:zpool import -N -f -n" OZ_REWIND_FLAG " -o cachefile=none -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",SYS_Output,file,TAG_DONE);
    CHECK(Close(file));file=0;CHECK(assessment_result>0);
    file=Open((CONST_STRPTR)"T:recovery-assessment",MODE_OLDFILE);CHECK(file);
    char assessment[2048];LONG assessment_length=Read(file,assessment,sizeof(assessment)-1);
    CHECK(assessment_length>0);assessment[assessment_length]=0;
    CHECK(Close(file));file=0;note(assessment);
    CHECK(strstr(assessment,"Would be able to return qemu_mirror to its state as of"));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    note("RESULT native recovery assessment PASS flag=n" OZ_REWIND_FLAG " recoverable-report=1 pool-absent=1");
#endif
#ifdef OZ_NATIVE_IMPORT_REWIND_REFUSE_NORMAL
    /* This fixture has latest txg 536, next surviving txg 520. Upstream
     * normal recovery is bounded by TXG_DEFER_SIZE (2), so do not silently
     * promote it to extreme recovery. The later readonly extreme cycles
     * also verify that these refused imports did not commit repairs. */
    CHECK(!request(7,"readonly=on;recovery=safe",ENXIO));
    /* TRY_REWIND preserves the original load error, unlike DO_REWIND. */
    CHECK(!request(7,"readonly=on;recovery=safe;assessment=on",EIO));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    file=Open((CONST_STRPTR)"T:normal-recovery-error",MODE_NEWFILE);CHECK(file);
    LONG normal_result=SystemTags((CONST_STRPTR)"C:zpool import -N -f -F -o cachefile=none -o readonly=on -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",SYS_Error,file,TAG_DONE);
    CHECK(Close(file));file=0;CHECK(normal_result>0);
    file=Open((CONST_STRPTR)"T:normal-recovery-error",MODE_OLDFILE);CHECK(file);
    char normal_error[1024];LONG normal_length=Read(file,normal_error,sizeof(normal_error)-1);
    CHECK(normal_length>0);normal_error[normal_length]=0;
    CHECK(Close(file));file=0;note(normal_error);
    CHECK(strstr(normal_error,"cannot import")&&strstr(normal_error,"one or more devices is currently unavailable"));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    note("RESULT native normal recovery boundary PASS Manager-ENXIO=1 CLI-refused=1 diagnostic=1 absent=2 no-extreme-fallback=1");
#endif
    for(unsigned cycle=0;cycle<2;cycle++) {
        /* Latest MOS root is deliberately damaged in BOTH disposable
         * members. An ordinary import must fail again after readonly
         * recovery; otherwise a preview may have silently committed. */
        CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -f -o cachefile=none -o readonly=on -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)>0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
#ifdef OZ_NATIVE_IMPORT_REWIND_MANAGER
        CHECK(!request(7,"readonly=on;recovery=" OZ_REWIND_POLICY,0));
#else
        CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -f -" OZ_REWIND_FLAG " -o cachefile=none -o readonly=on -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)==0);
#endif
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZREWIND",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        char data[128];file=Open((CONST_STRPTR)"OZREWIND:hello.txt",MODE_OLDFILE);CHECK(file);
        CHECK(Read(file,data,sizeof(data))==sizeof(expected)-1);
        CHECK(!memcmp(data,expected,sizeof(expected)-1));CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZREWIND:must-not-write",MODE_NEWFILE);CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
    }
    note("RESULT native damaged import rewind PASS cycles=2 ordinary-refused=2 absent-after-refusal=2 readonly-" OZ_REWIND_FLAG "=2 exact-DOS=2 write-refused=2");
#endif
#ifdef OZ_NATIVE_IMPORT_REWIND_COMMIT
    note("READY native writable recovery request");
#ifdef OZ_NATIVE_CLEAR_REWIND
    {
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);CHECK(secLoginA(reader)==((77UL<<16)|88));
        LONG denied=SystemTags((CONST_STRPTR)"C:zpool import -N -V -f -o cachefile=none -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE);
        secLogoutA(quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);CHECK(denied>0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
        note("RESULT native verbatim privilege PASS nonroot-refused=1 absent=1 root-restored=1");
    }
    CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -V -f -o cachefile=none -o readonly=on -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)>0);
    CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)>0);
    CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -V -f -o cachefile=none -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)==0);
    note("RESULT native verbatim admission PASS readonly-refused=1 absent=1 writable-admitted=1");
    file=Open((CONST_STRPTR)"T:clear-assessment",MODE_NEWFILE);CHECK(file);
    LONG clear_assessment=SystemTags((CONST_STRPTR)"C:zpool clear -n" OZ_REWIND_FLAG " qemu_mirror",SYS_Output,file,SYS_Error,file,TAG_DONE);
    LONG clear_closed=Close(file);file=0;
    file=Open((CONST_STRPTR)"T:clear-assessment",MODE_OLDFILE);CHECK(file);
    char clear_report[2048];LONG clear_length=Read(file,clear_report,sizeof(clear_report)-1);
    CHECK(clear_length>0);clear_report[clear_length]=0;CHECK(Close(file));file=0;note(clear_report);
    {char result[96];snprintf(result,sizeof(result),"Clear assessment exit=%ld close=%ld",clear_assessment,clear_closed);note(result);}
    CHECK(clear_closed&&clear_assessment==0);
    CHECK(strstr(clear_report,"Would be able to return qemu_mirror to its state as of"));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool clear -" OZ_REWIND_FLAG " qemu_mirror",TAG_DONE)==0);
    note("RESULT native damaged clear rewind PASS assessment=1 recoverable-report=1 commit=1");
#else
#ifdef OZ_NATIVE_IMPORT_REWIND_MANAGER
    CHECK(!request(7,"readonly=off;recovery=" OZ_REWIND_POLICY,0));
#else
    CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -f -" OZ_REWIND_FLAG " -o cachefile=none -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)==0);
#endif
#endif
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZREWIND",OZ_MOUNT_ATTACH,0));
    file=Open((CONST_STRPTR)"OZREWIND:after-recovery",MODE_NEWFILE);CHECK(file);
    CHECK(Write(file,"committed-after-rewind",22)==22);CHECK(Flush(file));CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
    note("RESULT native damaged rewind commit PASS writable-" OZ_REWIND_FLAG "=1 new-file=1 flushed=1 exported=1");
#endif
#if defined(OZ_NATIVE_IMPORT_REWIND_COMMIT) || defined(OZ_NATIVE_IMPORT_REWIND_COLD)
    CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -f -o cachefile=none -o readonly=on -d /AROS/ata.device/0/partition/0 -d /AROS/ata.device/1/partition/0 qemu_mirror",TAG_DONE)==0);
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZREWIND",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    {
        const char original[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        const char *names[]={"OZREWIND:hello.txt","OZREWIND:after-recovery"};
        const char *expected[]={original,"committed-after-rewind"};
        for(unsigned i=0;i<2;i++) {
            char data[128];file=Open((CONST_STRPTR)names[i],MODE_OLDFILE);CHECK(file);
            CHECK(Read(file,data,sizeof(data))==(LONG)strlen(expected[i]));
            CHECK(!memcmp(data,expected[i],strlen(expected[i])));CHECK(Close(file));file=0;
        }
    }
    file=Open((CONST_STRPTR)"OZREWIND:cold-must-not-write",MODE_NEWFILE);CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
    note("RESULT native repaired pool readback PASS ordinary-import=1 original-file=1 post-recovery-file=1 readonly=1");
#endif
#ifdef OZ_NATIVE_IMPORT_REWIND_MANAGER
    note("RESULT native Manager recovery API PASS real-admission=1 upstream-rewind=1 exact-DOS=1");
#endif
    goto done;
#endif
#ifdef OZ_NATIVE_LABELCLEAR_ONLY
    CHECK(!request(7,"readonly=off",0));
    CHECK(!health());
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceCommandTest",TAG_DONE)==0);
    goto done;
#endif
#ifdef OZ_NATIVE_DATASET_ADMIN_ONLY
    CHECK(!request(7,"readonly=off",0));
    CHECK(!health());
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceCommandTest",TAG_DONE)==0);
    goto done;
#endif
#ifdef OZ_NATIVE_LIVE_CREATE_ONLY
    CHECK(!request(7,"readonly=off",0));
    CHECK(!health());
    CHECK(!request(23,NULL,0));
    CHECK(!request(17,NULL,0));
    CHECK(!request(24,NULL,0));
    CHECK(!health());
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceCommandTest",TAG_DONE)==0);
    note("RESULT native post-reopen live create PASS reguid=1 clear=1 reopen=1 create-destroy=1");
    goto done;
#endif
#ifdef OZ_NATIVE_FILE_CORRUPTION
    CHECK(!request(7,"readonly=off",0));
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZCORRUPT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZCORRUPT:hello.txt",MODE_OLDFILE);CHECK(file);
    {
        char bytes[128]={0},line[128];
        LONG n=Read(file,bytes,sizeof(bytes)),reason=IoErr();
        snprintf(line,sizeof(line),"Native corrupt file DOS read count=%ld IoErr=%ld",(long)n,(long)reason);note(line);
        CHECK(n==-1&&reason!=0);
        CHECK(Seek(file,0,OFFSET_CURRENT)==0);
        for(unsigned retry=0;retry<3;retry++) {
            n=Read(file,bytes,sizeof(bytes));LONG retry_error=IoErr();
            CHECK(n==-1&&retry_error==ERROR_NOT_A_DOS_DISK);
            CHECK(Seek(file,0,OFFSET_CURRENT)==0);
        }
        note("Native corrupt file repeated-read PASS retries=3 position-unchanged=1");
        CHECK(Close(file));file=0;
        CHECK(SystemTags((CONST_STRPTR)"C:zpool status -v qemu_mirror >T:corrupt-status.txt",TAG_DONE)==0);
        file=Open((CONST_STRPTR)"T:corrupt-status.txt",MODE_OLDFILE);CHECK(file);
        char status[4096]={0};LONG length=Read(file,status,sizeof(status)-1);
        CHECK(length>0&&length<(LONG)sizeof(status)-1);note(status);
        CHECK(strstr(status,"hello.txt")!=NULL);
        CHECK(Close(file));file=0;
        CHECK(reason==ERROR_NOT_A_DOS_DISK);
    }
#ifdef OZ_NATIVE_FILE_CORRUPTION_GUI
    CHECK(SystemTags((CONST_STRPTR)"C:IPrefs",TAG_DONE)==0);
    note("Native real corruption Manager READY: query pool errors for qemu_mirror, then close Manager");
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#endif
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    CHECK(!request(8,NULL,0));
    goto done;
#endif
#if defined(OZ_NATIVE_WHOLE_CLI) && defined(OZ_NATIVE_VERIFY_WRITES)
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceCommandTest",TAG_DONE)==0);
#endif
#ifdef OZ_NATIVE_GPT_READONLY
    struct oz_gpt_query readonly={.version=OZ_GPT_VERSION,.operation=OZ_GPT_LABEL,
        .flags=OZ_GPT_CONFIRMED|OZ_GPT_REPLACE_TABLE,.sector_size=512,
        .bytes=536870912,.unit=0};
    strcpy(readonly.device,"ata.device");strcpy(readonly.name,"AROS OpenZFS API");
    CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&readonly)==EROFS&&!readonly.changed);
    readonly.operation=OZ_GPT_EXPAND;readonly.flags=OZ_GPT_CONFIRMED;readonly.name[0]=0;
    CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&readonly)==EROFS&&!readonly.changed);
    note("RESULT native GPT readonly API PASS label=1 expand=1");
    goto done;
#endif
#if defined(OZ_NATIVE_WRITABLE_TEST) || defined(OZ_NATIVE_VERIFY_WRITES)
    static const char written[]="Native writable OpenZFS pool through AROS DOS.\n";
#ifndef OZ_NATIVE_VERIFY_WRITES
    CHECK(!request(7,"readonly=off",0));
    CHECK(!health());
#if !defined(OZ_NATIVE_IMPORT_DEGRADED)
    CHECK(!request(23,NULL,0));
    CHECK(!request(17,NULL,0));
    CHECK(!request(24,NULL,0));
    CHECK(!health());
    note("RESULT native reguid and reopen PASS");
#endif
#ifdef OZ_NATIVE_WHOLE_CLI
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSServiceCommandTest",TAG_DONE)==0);
#endif
    CHECK(!request(37,"utf8only=on,utf8only=off",EINVAL));
    CHECK(!request(37,"unknown=on",ENOTSUP));
#ifdef OZ_NATIVE_CREATE_GUI
    note("Native creation GUI READY: create qemu_mirror/api-names with normalization=formC,utf8only=on,casesensitivity=insensitive; disable dry-run, then close manager");
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    CHECK(!request(37,"normalization=formC,utf8only=on,casesensitivity=insensitive",0));
#endif
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","OZNAMES",OZ_MOUNT_ATTACH,0));
    file=Open((CONST_STRPTR)"OZNAMES:caf\xc3\xa9",MODE_NEWFILE);CHECK(file);
    CHECK(Write(file,"names",5)==5);CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZNAMES:CAFE\xcc\x81",MODE_OLDFILE);CHECK(file);
    {char bytes[5];CHECK(Read(file,bytes,sizeof(bytes))==5&&!memcmp(bytes,"names",5));}
    CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","",OZ_MOUNT_DETACH,0));
#ifndef OZ_NATIVE_KEEP_NAMES
    CHECK(!request(38,NULL,0));
#ifdef OZ_NATIVE_CREATE_GUI
    note("RESULT native GUI create PASS normalized-case-read=1 destroy=1");
#endif
    note("RESULT native typed create PASS rejected=2 normalized-case-read=1 destroy=1");
#else
    note("RESULT native namespace retained PASS normalized-case-read=1");
#endif
    {int rc=oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,0);
        char status[80];snprintf(status,sizeof(status),"Native writable mount errno=%d",rc);note(status);CHECK(!rc);}
    file=Open((CONST_STRPTR)"OZNATIVE:native-write.txt",MODE_NEWFILE);CHECK(file);
    CHECK(Write(file,written,sizeof(written)-1)==sizeof(written)-1);
    CHECK(Flush(file));CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    CHECK(!request(8,NULL,0));
#endif
    CHECK(!request(7,"readonly=on",0));
    CHECK(!request(23,NULL,EROFS));
#ifdef OZ_NATIVE_KEEP_NAMES
    CHECK(!namespace_property("normalization","formC"));
    CHECK(!namespace_property("utf8only","on"));
    CHECK(!namespace_property("casesensitivity","insensitive"));
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","OZNAMES",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZNAMES:CAFE\xcc\x81",MODE_OLDFILE);CHECK(file);
    {char bytes[6]={0};CHECK(Read(file,bytes,sizeof(bytes))==5&&!memcmp(bytes,"names",5));}
    CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","",OZ_MOUNT_DETACH,0));
#ifdef OZ_NATIVE_VERIFY_WRITES
    note("RESULT native cold namespace PASS properties=3 normalized-case-read=1 readonly=1");
#else
    note("RESULT native reopened namespace PASS properties=3 normalized-case-read=1 readonly=1");
#endif
#endif
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZNATIVE:native-write.txt",MODE_OLDFILE);CHECK(file);
    char persisted[sizeof(written)]={0};
    CHECK(Read(file,persisted,sizeof(persisted))==sizeof(written)-1);
    CHECK(!memcmp(persisted,written,sizeof(written)-1));
    CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    CHECK(!request(8,NULL,0));
#ifdef OZ_NATIVE_VERIFY_WRITES
    note("RESULT native cold write verification PASS readonly=1 persisted=1");
#else
    note("RESULT native writable import PASS dos-write=1 flush=1 export=1 reimport=1 persisted=1");
#endif
#endif
    for(unsigned attempt=0;attempt<2;attempt++)
        CHECK(!request_paths(7,"readonly=on",ENOENT,99,100));
    /* A same-named pool on supplied devices is not sufficient identity. */
#ifndef OZ_NATIVE_USB_CREATED_RECOVERY
    CHECK(!request(7,"readonly=on;guid=0000000000000001",EINVAL));
#endif
    /* Duplicate paths are rejected by admission before an engine job exists. */
    CHECK(request_paths(7,"readonly=on",0,0,0)==EINVAL);
#ifdef OZ_NATIVE_GPT_TEST
    struct oz_gpt_query gpt={.version=OZ_GPT_VERSION,.operation=OZ_GPT_LABEL,
        .flags=OZ_GPT_CONFIRMED|OZ_GPT_REPLACE_TABLE,.sector_size=512,
        .bytes=536870912,.unit=0};
    strcpy(gpt.device,"ata.device");strcpy(gpt.name,"AROS OpenZFS API");
    gpt.flags=OZ_GPT_CONFIRMED;
    CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&gpt)==EINVAL&&!gpt.changed);
    gpt.flags|=OZ_GPT_REPLACE_TABLE;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);
    CHECK(secLoginA(reader)==((77UL<<16)|88));
    CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&gpt)==EPERM&&!gpt.changed);
    secLogoutA(quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);
#endif
    UQUAD recorded_guid=0;
#ifdef OZ_NATIVE_USB_CREATED_RECOVERY
    for(unsigned cycle=1;cycle<2;cycle++) {
        CHECK(!request(5,NULL,0));
        CHECK(!request(37,"normalization=formC,utf8only=on",0));
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","OZCREATED",OZ_MOUNT_ATTACH,0));
        file=Open((CONST_STRPTR)"OZCREATED:recovery",MODE_NEWFILE);CHECK(file);
        CHECK(Write(file,"created-before-loss",19)==19);
        CHECK(Flush(file));CHECK(Close(file));file=0;
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","",OZ_MOUNT_DETACH,0));
        note("READY native created pool recovery no-export=1");
#else
    for(unsigned cycle=0;cycle<2;cycle++) {
        char import_mode[80];strcpy(import_mode,"readonly=on");
        if(cycle)snprintf(import_mode,sizeof(import_mode),"readonly=on;guid=%016llx",(unsigned long long)recorded_guid);
        if(!cycle)CHECK(!request(7,import_mode,0));
        else {
            static struct oz_startup_settings settings;
            static struct oz_startup_import_result result;
            settings.guid=recorded_guid;settings.count=2;settings.flags=OZ_STARTUP_READONLY;
#if defined(OZ_NATIVE_USB_REMOVE_WRITABLE) || defined(OZ_NATIVE_USB_ALL_LOST_WRITABLE)
            settings.flags=0;
#endif
            strcpy(settings.pool,"qemu_mirror");
            for(unsigned i=0;i<2;i++)snprintf(settings.paths[i],256,"/AROS/" OZ_MEMBER_DEVICE "/%u/partition/0",i);
#if defined(OZ_NATIVE_IMPORT_RAIDZ) || defined(OZ_NATIVE_IMPORT_RAIDZ1)
            settings.count=3;strcpy(settings.paths[2],"/AROS/ata.device/3/partition/0");
#endif
#ifdef OZ_NATIVE_IMPORT_RAIDZ3
            settings.count=4;strcpy(settings.paths[3],"/AROS/ahci.device/0/partition/0");
#endif
            unsigned published=0;
            CHECK(!oz_startup_settings_apply(&settings,0,&published)&&published==1);
            CHECK(!oz_startup_import("OpenZFS.NoSuch.Test.Port",recorded_guid,&result)&&result.skipped&&!result.imported);
            settings.flags|=OZ_STARTUP_ENABLED;
            CHECK(!oz_startup_settings_apply(&settings,0,&published));
            CHECK(oz_startup_import("OpenZFS.NoSuch.Test.Port",recorded_guid,&result)==ENOENT&&!result.imported);
            char env_path[96],transaction_path[104],previous_path[120];
            snprintf(env_path,sizeof(env_path),"ENV:OpenZFS-%016llx.prefs",(unsigned long long)recorded_guid);
            CHECK(!oz_settings_transaction_path(env_path,transaction_path,sizeof(transaction_path)));
            snprintf(previous_path,sizeof(previous_path),"%s/previous",transaction_path);
            BPTR transaction=CreateDir((CONST_STRPTR)transaction_path);CHECK(transaction);UnLock(transaction);
            CHECK(Rename((CONST_STRPTR)env_path,(CONST_STRPTR)previous_path));
            CHECK(oz_startup_import(OZ_ADMISSION_PORT,recorded_guid,&result)==EBUSY&&result.recovered&&!result.imported&&!result.ticket.number);
            CHECK(Rename((CONST_STRPTR)previous_path,(CONST_STRPTR)env_path));
            CHECK(DeleteFile((CONST_STRPTR)transaction_path));
            struct TagItem startup_quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
            struct TagItem startup_reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
            secLogoutA(startup_quiet);CHECK(secLoginA(startup_reader)==((77UL<<16)|88));
            CHECK(oz_startup_import_trusted(OZ_ADMISSION_PORT,recorded_guid,&result)==EPERM&&!result.imported&&!result.ticket.number);
            int denied=oz_startup_import(OZ_ADMISSION_PORT,recorded_guid,&result);
            note(result.detail);
            CHECK(denied==EIO&&result.status==OZ_ENGINE_ERROR&&!result.imported&&!result.ticket.number&&
                !strcmp(result.detail,"Authenticated pool import returned errno=1"));
            secLogoutA(startup_quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);
            CHECK(secSetProtection((CONST_STRPTR)env_path,FIBF_OTR_WRITE));
            CHECK(oz_startup_import_trusted(OZ_ADMISSION_PORT,recorded_guid,&result)==EACCES&&!result.imported&&!result.ticket.number);
            CHECK(secSetProtection((CONST_STRPTR)env_path,FIBF_OTR_READ));
            CHECK(!oz_startup_import_trusted(OZ_ADMISSION_PORT,recorded_guid,&result)&&result.imported&&!result.skipped&&!result.ticket.number);
            note("RESULT native trusted startup import PASS ordinary-caller=denied unsafe-file=denied protected-file=imported");
            CHECK(oz_startup_import(OZ_ADMISSION_PORT,recorded_guid,&result)==EIO&&!result.imported&&
                result.status==OZ_ENGINE_ERROR&&!result.ticket.number);
            note("RESULT native startup authority PASS recovery-refused=1 ordinary-user-denied=1 root-import=1 duplicate-reported=1");
            note("RESULT native saved-record import PASS disabled-skip=1 absent-service=1 authenticated-import=1");
        }
#endif
        CHECK(!pool_guid(&recorded_guid));
#ifndef OZ_NATIVE_USB_CREATED_RECOVERY
        if(cycle)note("RESULT native expected-GUID import PASS mismatch-refused=1 matching-import=1");
#endif
        CHECK(!health());
#ifdef OZ_NATIVE_USB_REMOVE
        if(cycle==1){
            for(unsigned recovery=0;recovery<OZ_NATIVE_USB_RECOVERY_ROUNDS;recovery++){
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            static const char sentinel[]="Committed before in-flight USB loss.\n";
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZINFLIGHT",OZ_MOUNT_ATTACH,0));
            file=Open((CONST_STRPTR)"OZINFLIGHT:committed-before-loss",MODE_NEWFILE);CHECK(file);
            CHECK(Write(file,(APTR)sentinel,sizeof(sentinel))==sizeof(sentinel));
            CHECK(Flush(file));CHECK(Close(file));file=0;
#endif
            CHECK(!request(26,NULL,0));
#endif
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            file=Open((CONST_STRPTR)"OZINFLIGHT:interrupted-write",MODE_NEWFILE);CHECK(file);
            unsigned char *chunk=malloc(65536);CHECK(chunk);
            memset(chunk,0xa5,65536);
#endif
            note("READY native USB member removal member=ozmember0");
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            unsigned full_writes=0,committed_writes=0;LONG write_result=65536,write_error=0,flush_error=0;
            for(unsigned write_no=0;write_no<2048;write_no++){
                write_result=Write(file,chunk,65536);
                if(write_result!=65536){write_error=IoErr();break;}
                full_writes++;
                if(!Flush(file)){flush_error=IoErr();break;}
                committed_writes++;
            }
            LONG close_result=Close(file);file=0;free(chunk);
            {char line[192];snprintf(line,sizeof(line),
                "Native USB in-flight write full=%u committed=%u write-result=%ld write-errno=%ld flush-errno=%ld close=%ld",
                full_writes,committed_writes,(long)write_result,(long)write_error,(long)flush_error,(long)close_result);note(line);}
            CHECK(committed_writes>0&&(write_result!=65536||flush_error));
            note("RESULT native USB in-flight write interruption PASS wrote-before-error=1 error-observed=1");
#endif
            int removed=0;
            for(unsigned attempt=0;attempt<30;attempt++){
                Delay(50);
#ifdef OZ_NATIVE_USB_ALL_LOST
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
                /* Suspension may win the race with this observation. */
                if(!health_expected("UNAVAIL") || !health_expected("SUSPENDED")){
                    removed=1;break;
                }
#else
                if(!health_expected("UNAVAIL")){removed=1;break;}
#endif
#else
                if(!health_expected("DEGRADED")){removed=1;break;}
#endif
            }
            CHECK(removed);
#ifdef OZ_NATIVE_USB_ALL_LOST
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
            note("RESULT native USB total loss PASS health=UNAVAIL-or-SUSPENDED responsive=1");
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            CHECK(oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0)==EAGAIN);
            note("RESULT native suspended unmount guard PASS error=EAGAIN responsive=1 namespace-retained=1");
#endif
#else
            note("RESULT native USB total loss PASS health=UNAVAIL responsive=1");
#endif
#else
            note("RESULT native USB member removal PASS idle-health=DEGRADED");
#endif
#ifdef OZ_NATIVE_USB_REINSERT
            note("READY native USB member reinsertion member=ozmember0");
            /* Allow the host hotplug and normal AROS USB enumeration. The
             * retired handle must not resume just because media reappears. */
            Delay(500);
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
            CHECK(!reinserted_partitions_ready());
            CHECK(!health_expected("SUSPENDED"));
#ifdef OZ_NATIVE_USB_SUSPEND_GUARD
            CHECK(!suspended_history(0));
            CHECK(!suspended_history(1));
            note("RESULT native suspended history PASS summary=EAGAIN raw=EAGAIN empty-errors=2 errno-detail=2 responsive=1");
            CHECK(!request(8,NULL,EAGAIN));
            CHECK(!request(6,NULL,EAGAIN));
            CHECK(!health_expected("SUSPENDED"));
            note("RESULT native suspended export guard PASS export=EAGAIN destroy=EAGAIN responsive=1");
            goto done; /* Negative safety test, not successful pool recovery. */
#endif
            note("READY native USB writable explicit clear");
            CHECK(!request(17,"18446744073709551615",ENODEV));
            CHECK(!health_expected("SUSPENDED"));
            note("RESULT native suspended clear invalid target PASS error=ENODEV suspended=1");
#ifdef OZ_NATIVE_USB_SWAP_MEMBERS
#ifdef OZ_NATIVE_USB_CLI_CLEAR
            CHECK(SystemTags((CONST_STRPTR)"C:zpool clear qemu_mirror",TAG_DONE)>0);
            CHECK(!health_expected("SUSPENDED"));
            note("RESULT native CLI wrong-member clear PASS refused=1 suspended=1");
#endif
            CHECK(!request(17,NULL,ESTALE));
            CHECK(!health_expected("SUSPENDED"));
            note("RESULT native wrong-member readmission PASS clear=ESTALE suspended=1");
            goto done;
#endif
#ifdef OZ_NATIVE_USB_CLI_CLEAR
            CHECK(SystemTags((CONST_STRPTR)"C:zpool clear qemu_mirror",TAG_DONE)==0);
            note("RESULT native CLI suspended clear PASS returned=0");
#else
            CHECK(!request(17,NULL,0));
#endif
            CHECK(!health_expected("ONLINE"));
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
#endif
#elif defined(OZ_NATIVE_USB_ALL_LOST)
            CHECK(!health_expected("UNAVAIL"));
#else
            CHECK(!health_expected("DEGRADED"));
#endif
            CHECK(!request(8,NULL,0));
#ifdef OZ_NATIVE_USB_RELOCATE
            CHECK(!request(7,"readonly=on",EACCES));
            CHECK(!request_paths(7,"readonly=on",0,2,1));
#else
            CHECK(!request(7,recovery+1<OZ_NATIVE_USB_RECOVERY_ROUNDS ?
                "readonly=off" : "readonly=on",0));
#endif
            CHECK(!health_expected("ONLINE"));
            note("RESULT native USB reinsertion PASS no-auto-adopt=1 fresh-import=ONLINE");
#ifdef OZ_NATIVE_USB_ALL_LOST_WRITABLE
            note("RESULT native USB writable idle recovery PASS synced-before-loss=1 fresh-import=ONLINE");
#endif
#ifdef OZ_NATIVE_USB_INFLIGHT_WRITE
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZINFLIGHT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
            file=Open((CONST_STRPTR)"OZINFLIGHT:committed-before-loss",MODE_OLDFILE);CHECK(file);
            {char data[sizeof(sentinel)]={0};
                CHECK(Read(file,data,sizeof(data))==sizeof(sentinel));
                CHECK(!memcmp(data,sentinel,sizeof(sentinel)));
                CHECK(Read(file,data,1)==0);}
            CHECK(Close(file));file=0;
            /* A successful Flush is an acknowledged durability boundary.
             * Unacknowledged tail bytes may or may not survive, but every
             * successfully flushed chunk must survive the readonly reimport. */
            file=Open((CONST_STRPTR)"OZINFLIGHT:interrupted-write",MODE_OLDFILE);CHECK(file);
            {unsigned char data[4096];
                for(unsigned block=0;block<committed_writes*16;block++) {
                    CHECK(Read(file,data,sizeof(data))==sizeof(data));
                    for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==0xa5);
                }
            }
            CHECK(Close(file));file=0;
            {char line[192];snprintf(line,sizeof(line),
                "RESULT native USB acknowledged writes PASS chunks=%u bytes=%llu readonly-reimport=1 exact-prefix=1",
                committed_writes,(unsigned long long)committed_writes*65536);note(line);}
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
            note("RESULT native USB in-flight recovery PASS committed-sentinel=1 fresh-import=ONLINE");
#endif
#ifdef OZ_NATIVE_USB_RELOCATE
            note("RESULT native USB relocation PASS admitted-unit=2 identity=pool-label old-path-refused=1");
#endif
#endif
            }
        }
#endif
#ifdef OZ_NATIVE_GPT_TEST
        CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&gpt)==EBUSY&&!gpt.changed);
#endif
#ifdef OZ_NATIVE_USB_CREATED_RECOVERY
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","OZCREATED",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        file=Open((CONST_STRPTR)"OZCREATED:recovery",MODE_OLDFILE);CHECK(file);
        char data[20];CHECK(Read(file,data,sizeof(data))==19&&!memcmp(data,"created-before-loss",19));
        CHECK(Close(file));file=0;
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/api-names","",OZ_MOUNT_DETACH,0));
        CHECK(!request(8,NULL,0));
        note("RESULT native created pool recovery PASS first-export-after-clear=1 exact-read=1");
        goto done;
#endif
#ifdef OZ_NATIVE_USB_REMOVE_WRITABLE
        if(cycle==1){
            static const char payload[]="USB mirror survivor committed write.\n";
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,0));
            file=Open((CONST_STRPTR)"OZNATIVE:usb-survivor.txt",MODE_NEWFILE);CHECK(file);
            CHECK(Write(file,payload,sizeof(payload))==sizeof(payload));
            CHECK(Flush(file));CHECK(Close(file));file=0;
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
            CHECK(!request(8,NULL,0));
            CHECK(!request(7,"readonly=on",0));
            CHECK(!health_expected("DEGRADED"));
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
            file=Open((CONST_STRPTR)"OZNATIVE:usb-survivor.txt",MODE_OLDFILE);CHECK(file);
            char data[sizeof(payload)];CHECK(Read(file,data,sizeof(data))==sizeof(data));
            CHECK(!memcmp(data,payload,sizeof(data)));CHECK(Read(file,data,1)==0);
            CHECK(Close(file));file=0;
            CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
            note("RESULT native USB degraded write PASS flush=1 survivor-reimport=1 exact-read=1");
        }
#endif
        CHECK(oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,0)==EROFS);
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
#ifdef OZ_NATIVE_USB_COLD
        {
            static const char payload[]="USB mirror survivor committed write.\n";
            file=Open((CONST_STRPTR)"OZNATIVE:usb-survivor.txt",MODE_OLDFILE);CHECK(file);
            char data[sizeof(payload)];CHECK(Read(file,data,sizeof(data))==sizeof(data));
            CHECK(!memcmp(data,payload,sizeof(data)));CHECK(Read(file,data,1)==0);
            CHECK(Close(file));file=0;
            note("RESULT native USB cold survivor PASS exact-read=1 readonly=1");
        }
#endif
#ifdef OZ_NATIVE_USB_COLD_ACK_CHUNKS
        {
            static const char sentinel[]="Committed before in-flight USB loss.\n";
            file=Open((CONST_STRPTR)"OZNATIVE:committed-before-loss",MODE_OLDFILE);CHECK(file);
            char saved[sizeof(sentinel)];CHECK(Read(file,saved,sizeof(saved))==sizeof(saved));
            CHECK(!memcmp(saved,sentinel,sizeof(saved)));CHECK(Read(file,saved,1)==0);
            CHECK(Close(file));file=0;
            file=Open((CONST_STRPTR)"OZNATIVE:interrupted-write",MODE_OLDFILE);CHECK(file);
            unsigned char data[4096];
            for(unsigned block=0;block<OZ_NATIVE_USB_COLD_ACK_CHUNKS*16;block++) {
                CHECK(Read(file,data,sizeof(data))==sizeof(data));
                for(unsigned i=0;i<sizeof(data);i++)CHECK(data[i]==0xa5);
            }
            CHECK(Close(file));file=0;
            char line[160];snprintf(line,sizeof(line),
                "RESULT native USB cold acknowledged PASS chunks=%u exact-prefix=1 sentinel=1 readonly=1",
                (unsigned)OZ_NATIVE_USB_COLD_ACK_CHUNKS);note(line);
        }
#endif
        for(unsigned reopen=0;reopen<3;reopen++) {
            static const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
            file=Open((CONST_STRPTR)"OZNATIVE:hello.txt",MODE_OLDFILE);CHECK(file);
            char data[128]={0};LONG n=Read(file,data,sizeof(data));
            CHECK(n==(LONG)(sizeof(expected)-1));
            CHECK(!memcmp(data,expected,sizeof(expected)-1));
            CHECK(Read(file,data,1)==0);
            CHECK(Seek(file,0,OFFSET_BEGINNING)==n);
            CHECK(Read(file,data,7)==7&&!memcmp(data,expected,7));
            CHECK(Close(file));file=0;
        }
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
        CHECK(!request(8,NULL,0));
    }
#ifdef OZ_NATIVE_GPT_TEST
    /* Only this opt-in runner's overlay is modified, after both pools export. */
    gpt.bytes-=512;
    CHECK(oz_gpt_request(OZ_ADMISSION_PORT,&gpt)==ESTALE&&!gpt.changed);
    gpt.bytes+=512;
    {int rc=oz_gpt_request(OZ_ADMISSION_PORT,&gpt);char status[80];
        snprintf(status,sizeof(status),"Native GPT API label errno=%d",rc);note(status);
        CHECK(!rc&&gpt.changed==1);}
    gpt.operation=OZ_GPT_EXPAND;gpt.flags=OZ_GPT_CONFIRMED;
    gpt.name[0]=0;gpt.changed=0;
    CHECK(!oz_gpt_request(OZ_ADMISSION_PORT,&gpt)&&!gpt.changed);
    note("RESULT native GPT API PASS confirmation=1 retained-pool=1 stale=1 label=1 expand=1");
#ifdef OZ_NATIVE_GPT_CLI
    extern int oz_upstream_gpt_test(void);
    CHECK(!oz_upstream_gpt_test());
#endif
#endif
#ifdef OZ_NATIVE_SNAPRENAME_GUI
    CHECK(!request(7,"readonly=off",0));
    CHECK(!request(50,NULL,0));
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files@before","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZNATIVE:hello.txt",MODE_OLDFILE);CHECK(file);
    CHECK(Close(file));file=0;
    note("Native mounted snapshot rename GUI READY: rename qemu_mirror/files@before to qemu_mirror/files@after; disable dry-run, confirm, apply, then close manager");
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    {struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
     int absent=FindDosEntry(list,(CONST_STRPTR)"OZNATIVE",LDF_DEVICES)==NULL;
     UnLockDosList(LDF_DEVICES|LDF_READ);CHECK(absent);}
    CHECK(oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files@before","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY)==ENOENT);
    CHECK(!request(8,NULL,0));
    CHECK(!request(7,"readonly=on",0));
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files@after","OZNATIVE",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    file=Open((CONST_STRPTR)"OZNATIVE:hello.txt",MODE_OLDFILE);CHECK(file);
    {static const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
     char data[sizeof(expected)];CHECK(Read(file,data,sizeof(data))==sizeof(expected)-1);
     CHECK(!memcmp(data,expected,sizeof(expected)-1));}
    CHECK(Close(file));file=0;
    CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files@after","",OZ_MOUNT_DETACH,0));
    CHECK(!request(8,NULL,0));
    note("RESULT native GUI snapshot rename PASS old-absent=1 readonly-reimport-read=1");
    note("RESULT native GUI mounted snapshot rename PASS mounted-before=1 detached-after=1");
#endif
#ifdef OZ_NATIVE_HANDLER_STARTUP
    CHECK(!request(7,"readonly=on",0));
    {
        /* Exercise the real generated handler startup, not just admission IPC.
         * Every failure must leave its registered DOS node inactive; the valid
         * OZBOOT activation below then proves the resident service still works. */
        static const struct { unsigned flags,dostype;const char *dataset; } bad[]={
            {2,0x5a465301,"qemu_mirror/files"},
            {0,0x444f5301,"qemu_mirror/files"},
            {0,0x5a465301,""},
            {0,0x5a465301,"qemu_mirror/no-such-handler-dataset"}
        };
        for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
            char path[40],command[64],name[32],device[40];
            snprintf(name,sizeof(name),"OZBAD%u",i);
            snprintf(path,sizeof(path),"T:%s",name);
            snprintf(command,sizeof(command),"C:Mount %s",path);
            snprintf(device,sizeof(device),"%s:",name);
            FILE *mountfile=fopen(path,"w");CHECK(mountfile);
            int saved=fprintf(mountfile,"FileSystem = L:OpenZFS-Handler\nDevice = openzfs\nUnit = 0\nFlags = %u\n"
                "Surfaces = 1\nBlocksPerTrack = 1\nLowCyl = 0\nHighCyl = 0\n"
                "DosType = 0x%08x\nControl = \"%s\"\nStackSize = 262144\nActivate = 0\n",
                bad[i].flags,bad[i].dostype,bad[i].dataset);
            int closed=fclose(mountfile);CHECK(saved>0&&!closed);
            CHECK(SystemTags((CONST_STRPTR)command,TAG_DONE)==0);
            struct DevProc *failed=GetDeviceProc((CONST_STRPTR)device,NULL);
            if(failed)FreeDeviceProc(failed);
            CHECK(!failed);
            struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
            struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)name,LDF_DEVICES);
            int inactive=entry&&!entry->dol_Task;
            UnLockDosList(LDF_DEVICES|LDF_READ);CHECK(inactive);
        }
        note("RESULT native handler invalid startup PASS flags=1 dostype=1 empty-control=1 missing-dataset=1 inactive-nodes=1");
    }
    {
        FILE *mountfile=fopen("T:OZBOOT","w");CHECK(mountfile);
        int saved=fputs("FileSystem = L:OpenZFS-Handler\nDevice = openzfs\nUnit = 0\nFlags = 0\n"
            "Surfaces = 1\nBlocksPerTrack = 1\nLowCyl = 0\nHighCyl = 0\n"
            "DosType = 0x5a465301\nControl = \"qemu_mirror/files\"\n"
            "StackSize = 262144\nActivate = 0\n",mountfile);
        int closed=fclose(mountfile);CHECK(saved>=0&&!closed);
        CHECK(SystemTags((CONST_STRPTR)"C:Mount T:OZBOOT",TAG_DONE)==0);
        struct DosList *before_list=LockDosList(LDF_DEVICES|LDF_READ);
        struct DosList *before=FindDosEntry(before_list,(CONST_STRPTR)"OZBOOT",LDF_DEVICES);
        UnLockDosList(LDF_DEVICES|LDF_READ);CHECK(before&&!before->dol_Task);
        struct oz_handler_attach denied={.version=OZ_HANDLER_VERSION,.device=before};
        strcpy(denied.dataset,"qemu_mirror/files");strcpy(denied.name,"OZBOOT");
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);CHECK(secLoginA(reader)==((77UL<<16)|88));
        int refused=oz_handler_attach_request(OZ_ADMISSION_PORT,&denied);
        struct DevProc *denied_start=GetDeviceProc((CONST_STRPTR)"OZBOOT:",NULL);
        if(denied_start)FreeDeviceProc(denied_start);
        secLogoutA(quiet);CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(refused==EPERM&&!denied_start&&!denied.endpoint&&!before->dol_Task);
        denied.version++;
        CHECK(oz_handler_attach_request(OZ_ADMISSION_PORT,&denied)==EINVAL&&!denied.endpoint&&!before->dol_Task);
        note("Native handler Mount registered; activating first DeviceProc");
        struct DevProc *first=GetDeviceProc((CONST_STRPTR)"OZBOOT:",NULL);CHECK(first);
        struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
        struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)"OZBOOT",LDF_DEVICES);
        struct MsgPort *engine=entry?entry->dol_Task:NULL;
        UnLockDosList(LDF_DEVICES|LDF_READ);
        CHECK(engine&&engine==first->dvp_Port);
        Forbid();struct Task *bootstrap_task=FindTask((CONST_STRPTR)"OZBOOT");
        struct MsgPort *bootstrap=bootstrap_task&&bootstrap_task->tc_Node.ln_Type==NT_PROCESS?
            &((struct Process *)bootstrap_task)->pr_MsgPort:NULL;Permit();
        CHECK(bootstrap&&bootstrap!=engine);
        CHECK(DoPkt(bootstrap,ACTION_IS_FILESYSTEM,0,0,0,0,0)==DOSTRUE);
        struct FileHandle handle={0};handle.fh_Type=bootstrap;
        CHECK(DoPkt(bootstrap,ACTION_FINDINPUT,(SIPTR)MKBADDR(&handle),0,
            (SIPTR)AROS_CONST_BSTR("hello.txt"),0,0)==DOSTRUE);
        static const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        char data[sizeof(expected)];
        CHECK(DoPkt(bootstrap,ACTION_READ,handle.fh_Arg1,(SIPTR)data,sizeof(data),0,0)==sizeof(expected)-1);
        CHECK(!memcmp(data,expected,sizeof(expected)-1));
        CHECK(DoPkt(bootstrap,ACTION_END,handle.fh_Arg1,0,0,0,0)==DOSTRUE);
        file=Open((CONST_STRPTR)"OZBOOT:hello.txt",MODE_OLDFILE);CHECK(file);
        CHECK(Read(file,data,sizeof(data))==sizeof(expected)-1&&!memcmp(data,expected,sizeof(expected)-1));
        CHECK(Close(file));file=0;
        CHECK(!Open((CONST_STRPTR)"OZBOOT:no-write",MODE_NEWFILE)&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
        CHECK(!DoPkt(first->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED);
        CHECK(!DoPkt(bootstrap,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED);
        FreeDeviceProc(first);
    }
    CHECK(!request(8,NULL,0));
    note("RESULT native standalone handler PASS Mount=1 bootstrap-forwarded=1 authenticated-read=1 readonly=1 late-detached=1 nonroot-denied=1 invalid-version=1");
    CHECK(!request(7,"readonly=off",0));
    {
        FILE *mountfile=fopen("T:OZBOOTRW","w");CHECK(mountfile);
        int saved=fputs("FileSystem = L:OpenZFS-Handler\nDevice = openzfs\nUnit = 0\nFlags = 1\n"
            "Surfaces = 1\nBlocksPerTrack = 1\nLowCyl = 0\nHighCyl = 0\n"
            "DosType = 0x5a465301\nControl = \"qemu_mirror/files\"\n"
            "StackSize = 262144\nActivate = 0\n",mountfile);
        int closed=fclose(mountfile);CHECK(saved>=0&&!closed);
        CHECK(SystemTags((CONST_STRPTR)"C:Mount T:OZBOOTRW",TAG_DONE)==0);
        static const char written[]="Standalone native handler persistent bytes.\n";
        file=Open((CONST_STRPTR)"OZBOOTRW:handler-created",MODE_NEWFILE);CHECK(file);
        CHECK(Write(file,(APTR)written,sizeof(written)-1)==sizeof(written)-1);
        CHECK(Flush(file));CHECK(Close(file));file=0;
#ifdef OZ_NATIVE_DOS_CACHE_PRESSURE
        CHECK(!dos_cache_pressure("OZBOOTRW",1));
#endif
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
        CHECK(!request(8,NULL,0));
        CHECK(!request(7,"readonly=on",0));
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZVERIFY",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        char data[sizeof(written)];
        file=Open((CONST_STRPTR)"OZVERIFY:handler-created",MODE_OLDFILE);CHECK(file);
        CHECK(Read(file,data,sizeof(data))==sizeof(written)-1&&!memcmp(data,written,sizeof(written)-1));
        CHECK(Close(file));file=0;
#ifdef OZ_NATIVE_DOS_CACHE_PRESSURE
        CHECK(!dos_cache_pressure("OZVERIFY",0));
        note("RESULT native DOS cache pressure PASS files=320 passes=3 writable=1 readonly-reimport=1");
#endif
        CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
        CHECK(!request(8,NULL,0));
    }
    note("RESULT native standalone writable handler PASS Mount=1 DOS-write=1 flush=1 export-reimport-readback=1");
#endif
#ifdef OZ_NATIVE_IMPORT_GUI
    note("Native import GUI ready: backend cycles passed; close manager to finish hash checks");
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#endif
done:
    if(file)Close(file);
    if(secBase)CloseLibrary(secBase);
    char line[160];snprintf(line,sizeof(line),
#if defined(OZ_NATIVE_IMPORT_REWIND)
        "RESULT native damaged rewind test %s line=%d",
#elif defined(OZ_NATIVE_FILE_CORRUPTION)
        "RESULT native real file corruption %s line=%d",
#elif defined(OZ_NATIVE_USB_CREATED_RECOVERY)
        "RESULT native created recovery test %s line=%d",
#elif defined(OZ_NATIVE_USB_SWAP_MEMBERS)
        "RESULT native wrong-member safety test %s line=%d",
#elif defined(OZ_NATIVE_USB_SUSPEND_GUARD)
        "RESULT native suspended safety test %s line=%d",
#elif defined(OZ_NATIVE_GPT_READONLY)
        "RESULT native GPT readonly test %s line=%d",
#else
        "RESULT native pool import %s cycles=2 dos-read=1 writable-denied=1 line=%d",
#endif
        error?"FAIL":"PASS",error);note(line);
#ifdef OZ_QEMU_COMPLETION_PORT
    complete(error);
#endif
    return error?20:0;
}
