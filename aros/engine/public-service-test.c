/* SPDX-License-Identifier: CDDL-1.0 */
/* Private-ISO lifecycle test; optional new read-only partition fixtures. */
#include "../ui/api.h"
#include "../ui/admission-api.h"
#include "../ui/mount-api.h"
#include "../ui/support-api.h"
#include "../ui/partition-api.h"
#include "include/sys/mnttab.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dostags.h>
#include <exec/memory.h>
#include <hidd/storage.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <aros/debug.h>
/* Use one formatted kernel message, not independently interleavable bytes. */
static void note(const char *s){bug("%s",s);}
char *zfs_version_kernel(void);
#ifdef OZ_TEST_INVENTORY_WORKER
int oz_inventory_worker_test(void);
#endif
#ifdef OZ_TEST_UPSTREAM_INIT
int oz_upstream_init_test(void);
int oz_upstream_mount_test(void);
int oz_upstream_promotion_readonly_test(int);
int oz_upstream_import_files_test(void);
int oz_upstream_file_vdev_test(int);
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
#endif
static void child_exited(IPTR status,IPTR result){*(LONG *)result=(LONG)status;}
static int present(const char *name){Forbid();int found=FindPort((CONST_STRPTR)name)!=NULL;Permit();return found;}
static const char *storage_directory="RAM:OZPublicPool";
static const char storage_payload[]="Public OpenZFS DOS payload\n";
static int storage_request(unsigned op,const char *target,const char *argument,const char *options){
    static struct oz_message m,out;memset(&m,0,sizeof(m));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=op;m.flags=OZ_CONFIRMED;
    snprintf(m.target,sizeof(m.target),"%s",target);
    if(argument)snprintf(m.argument,sizeof(m.argument),"%s",argument);
    if(options)snprintf(m.options,sizeof(m.options),"%s",options);
    if(op==5||op==7){
        m.topology.device_count=2;
        if(op==5){m.topology.group_count=1;m.topology.ashift=12;m.topology.groups[0].layout=OZ_MIRROR;}
        for(unsigned i=0;i<2;i++)snprintf(m.topology.devices[i].path,
            sizeof(m.topology.devices[i].path),"%s%sdisk%u",storage_directory,
            storage_directory[strlen(storage_directory)-1]==':'?"":"/",i);
    }
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);
    if(!error){do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);}
    if(!error){
        char line[256];snprintf(line,sizeof(line),"Public storage op=%u status=%lu %.170s\n",op,(unsigned long)out.status,out.detail);note(line);
        if(out.status!=OZ_OK)error=EIO;
    }
    if(ticket.number){int release=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=release;}
    return error;
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
    BPTR ahci_marker=Lock((CONST_STRPTR)"SYS:openzfs-ahci-query-test",SHARED_LOCK);
    int ahci=ahci_marker!=0;if(ahci_marker)UnLock(ahci_marker);
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
                if(u->unit||!u->model[0]||!u->serial[0]||
                   u->type!=vHidd_StorageUnit_Type_SolidStateDisk||seen_ahci++)error=EIO;
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
        if(denied) {
            if(error!=EACCES||memcmp(&value,&saved,sizeof(value)))return EIO;
        } else {
            if(error||value.version!=1||value.length!=36||
               memcmp(value.data+8,"SATA    ",8)||memcmp(value.data+16,"QEMU HARDDISK",13))return EIO;
            struct oz_device_geometry geometry={0};
            if(oz_device_geometry_read(OZ_ADMISSION_PORT,"ahci.device",0,&geometry)||
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
static int cli_result(const char *command,const char *expected)
{
    if(SystemTags((CONST_STRPTR)command,TAG_DONE)!=0)return EIO;
    BPTR file=Open((CONST_STRPTR)"SYS:cli-package-output.txt",MODE_OLDFILE);
    if(!file)return EIO;
    char text[256]={0};LONG count=Read(file,text,sizeof(text)-1);
    int closed=Close(file);
    if(count<0||!closed)return EIO;
    while(count>0&&(text[count-1]=='\n'||text[count-1]=='\r'))text[--count]=0;
    return strcmp(text,expected)?EIO:0;
}
static int storage_test(int gui){
    int error=0;BPTR file=0,lock=CreateDir((CONST_STRPTR)storage_directory);
    if(!lock)return EIO;
    char location[256];
    if(NameFromLock(lock,(STRPTR)location,sizeof(location))){note("Public fixture directory: ");note(location);note("\n");}
    UnLock(lock);
    void *zeros=AllocVec(1024*1024,MEMF_ANY|MEMF_CLEAR);if(!zeros)return ENOMEM;
    for(unsigned i=0;i<2&&!error;i++){
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
    STORAGE_CHECK(!verify_mounts(NULL,0));
    STORAGE_CHECK(!partition_rpc_test());
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
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_init_test());
#endif
    STORAGE_CHECK(!storage_alias_request(5,0));
#ifdef OZ_NATIVE_DISPATCHER
    STORAGE_CHECK(!oz_public_ioctl_test("public_mirror"));
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_mount_test());
    STORAGE_CHECK(!oz_upstream_promotion_readonly_test(0));
    STORAGE_CHECK(!oz_upstream_file_vdev_test(0));
#endif
#else
    STORAGE_CHECK(!storage_request(37,"public_mirror/files",NULL,NULL));
#endif
    STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","bad:name",OZ_MOUNT_ATTACH,0)==EINVAL);
    STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZPUBLIC",OZ_MOUNT_ATTACH,0));
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
        STORAGE_CHECK(!cli_result("C:zpool list -H -o name public_mirror >SYS:cli-package-output.txt","public_mirror"));
        STORAGE_CHECK(!cli_result("C:zfs list -H -o name public_mirror/files >SYS:cli-package-output.txt","public_mirror/files"));
        STORAGE_CHECK(!cli_result("C:zfs get -H -o value type public_mirror/files >SYS:cli-package-output.txt","filesystem"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs snapshot public_mirror/files@cli_roundtrip",TAG_DONE)==0);
        STORAGE_CHECK(!cli_result("C:zfs list -H -o name -t snapshot public_mirror/files@cli_roundtrip >SYS:cli-package-output.txt","public_mirror/files@cli_roundtrip"));
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:zfs destroy public_mirror/files@cli_roundtrip",TAG_DONE)==0);
        note("Public packaged CLI PASS pool-list=1 dataset-list=1 property=1 snapshot-create-list-destroy=1\n");
    }
    if(gui){
        note("Public storage GUI ready: mounted public_mirror/files with hello.txt\n");
        STORAGE_CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        note("Public storage GUI closed; verifying export and readonly reimport\n");
    }
    STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0));
    STORAGE_CHECK(oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0)==ENOENT);
    STORAGE_CHECK(!verify_mounts(NULL,0));
    STORAGE_CHECK(!storage_request(8,"public_mirror",NULL,NULL));
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_import_files_test());
    STORAGE_CHECK(!oz_upstream_file_vdev_test(1));
#endif
    STORAGE_CHECK(!storage_alias_request(7,2));
#ifdef OZ_TEST_UPSTREAM_INIT
    STORAGE_CHECK(!oz_upstream_file_vdev_test(2));
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
    STORAGE_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0));
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
    if(error){char line[96];snprintf(line,sizeof(line),"Public storage failure=%d\n",error);note(line);}
    return error;
}
static int storage_cold_once(void)
{
    int error=storage_alias_request(7,1);
    if(error)return error;
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
    error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","OZCOLD",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY);
    if(error)return error;
    char bytes[sizeof(storage_payload)+1];
    BPTR file=Open((CONST_STRPTR)"OZCOLD:hello.txt",MODE_OLDFILE);
    if(!file)return EIO;
    LONG n=Read(file,bytes,sizeof(bytes));int closed=Close(file);
    if(!closed||n!=sizeof(storage_payload)-1||memcmp(bytes,storage_payload,sizeof(storage_payload)-1))return EIO;
    file=Open((CONST_STRPTR)"OZCOLD:denied.txt",MODE_NEWFILE);
    if(file){Close(file);return EIO;}
    if(IoErr()!=ERROR_DISK_WRITE_PROTECTED)return EIO;
    error=oz_mount_control(OZ_ADMISSION_PORT,"public_mirror/files","",OZ_MOUNT_DETACH,0);
    if(!error)error=storage_request(8,"public_mirror",NULL,NULL);
    if(!error)note("Public cold hosted DOS PASS exact=1 readonly-refusal=1 exported=1\n");
    return error;
}
static int storage_cold_test(void)
{
    int error=storage_cold_once();
    if(error)return error;
    /* These are runner-created disposable members. Hide one without changing
     * its bytes, then restore its name even when degraded import fails. */
    char member[256],hidden[256];
    snprintf(member,sizeof(member),"%s/disk1",storage_directory);
    snprintf(hidden,sizeof(hidden),"%s/disk1-held",storage_directory);
    BPTR exists=Lock((CONST_STRPTR)hidden,SHARED_LOCK);
    if(exists){UnLock(exists);return EEXIST;}
    if(IoErr()!=ERROR_OBJECT_NOT_FOUND)return EIO;
    if(!Rename((CONST_STRPTR)member,(CONST_STRPTR)hidden))return EIO;
    error=storage_cold_once();
    if(!Rename((CONST_STRPTR)hidden,(CONST_STRPTR)member)&&!error)error=EIO;
    if(!error)note("Public cold degraded assign PASS missing=1 exact=1 readonly=1 restored=1\n");
    return error;
}
int main(int argc,char **argv){
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-service-test-only",SHARED_LOCK);
    if(!marker)return 20;
    UnLock(marker);
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
    int cold=argc==2&&!strcmp(argv[1],"STORAGE-COLD");
    if(cold&&!strcmp(storage_directory,"RAM:OZPublicPool"))return 20;
    int storage=cold||gui||(argc==2&&!strcmp(argv[1],"STORAGE"));
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
    if(storage)CHECK(!(cold?storage_cold_test():storage_test(gui)));
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
