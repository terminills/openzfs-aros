/* SPDX-License-Identifier: CDDL-1.0 */
/* Writes ONLY new disposable QEMU images, with explicit marker and sizes. */
#include "device-io.h"
#include "efi-uuid.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
static void note(const char *s){while(*s)RawPutChar(*s++);}
int oz_efi_native_test(void);
int oz_efi_native_cold_test(void);
static int efi_uuid_test(void)
{
    unsigned char ids[16][16];
    if(oz_efi_uuid_generate(NULL)!=EINVAL)return EIO;
    for(unsigned i=0;i<16;i++) {
        int error=oz_efi_uuid_generate(ids[i]);if(error)return error;
        if((ids[i][6]>>4)!=1||(ids[i][8]&0xc0)!=0x80)return EIO;
        for(unsigned j=0;j<i;j++)if(!memcmp(ids[i],ids[j],16))return EIO;
    }
    note("RESULT native EFI UUID PASS library=1 count=16 network-order=1 version=1 variant=1\n");
    return 0;
}
/* Fixture-only: these two blank images were created by this run. Never copy
 * this detach procedure into production admission; removing DOS nodes is not
 * a device-wide ownership protocol. Leave handlers inhibited during raw I/O. */
static struct DosList *fixture_mounts[2];
static int fixture_inhibited[2];
static const char *fixture_names[2]={"HD0:","HD1:"};
static int detach_fixture_mounts(void)
{
    for(unsigned unit=0;unit<2;unit++) {
        struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
        struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)fixture_names[unit],LDF_DEVICES);
        struct FileSysStartupMsg *s=entry?BADDR(entry->dol_misc.dol_handler.dol_Startup):NULL;
        int valid=s&&TypeOfMem(s)&&s->fssm_Unit==unit&&s->fssm_Device&&
            !strcmp((char *)AROS_BSTR_ADDR(s->fssm_Device),"ata.device");
        UnLockDosList(LDF_DEVICES|LDF_READ);
        if(!valid)return EINVAL;
        struct oz_device *disk=NULL;
        int rc=oz_device_open("ata.device",unit,1,&disk);
        oz_device_close(disk);
        if(rc!=EBUSY)return EIO;
        if(!Inhibit((CONST_STRPTR)fixture_names[unit],TRUE))return EIO;
        fixture_inhibited[unit]=1;
        if(!RemDosEntry(entry))return EIO;
        fixture_mounts[unit]=entry;
    }
    note("RESULT native active DOS claims PASS units=2 denied=1 inhibited=1 detached=1\n");
    return 0;
}
static int restore_fixture_mounts(void)
{
    int error=0;
    for(unsigned unit=0;unit<2;unit++) {
        if(fixture_mounts[unit]&&!AddDosEntry(fixture_mounts[unit])){error=EIO;continue;}
        fixture_mounts[unit]=NULL;
        if(fixture_inhibited[unit]&&!Inhibit((CONST_STRPTR)fixture_names[unit],FALSE))error=EIO;
        fixture_inhibited[unit]=0;
        struct oz_device *disk=NULL;
        int rc=oz_device_open("ata.device",unit,1,&disk);
        oz_device_close(disk);
        if(rc!=EBUSY)error=EIO;
    }
    note(error?"RESULT native DOS claims restored FAIL\n":
        "RESULT native DOS claims restored PASS units=2 denied=1\n");
    return error;
}
static int dos_claim_test(void)
{
    struct DosList *entry=MakeDosEntry((CONST_STRPTR)"OpenZFSRangeClaim",DLT_DEVICE);
    struct FileSysStartupMsg startup={0};
    struct DosEnvec env={0};
    struct oz_device *disk=NULL;
    struct oz_device *bound=NULL;
    struct oz_device_binding *binding=NULL,*rejected=NULL;
    int error=0,added=0;
#define CLAIM_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CLAIM_CHECK(entry);
    env.de_TableSize=DE_DOSTYPE;env.de_DosType=0x444f5301;
    startup.fssm_Device=AROS_CONST_BSTR("ata.device");
    startup.fssm_Unit=0;startup.fssm_Environ=MKBADDR(&env);
    entry->dol_misc.dol_handler.dol_Startup=MKBADDR(&startup);
    CLAIM_CHECK(AddDosEntry(entry));added=1;
    /* No handler is started. The DOS configuration itself reserves unit 0. */
    CLAIM_CHECK(oz_device_open("ata.device",0,1,&disk)==EBUSY&&!disk);
    CLAIM_CHECK(oz_device_open_range("ata.device",0,1,512,512,&disk)==EBUSY&&!disk);
    CLAIM_CHECK(!oz_device_open("ata.device",0,0,&disk));
    oz_device_close(disk);disk=NULL;
    {int rc=oz_device_open("ata.device",1,1,&disk);
        char line[96];snprintf(line,sizeof(line),"DOS claim other-unit open rc=%d\n",rc);note(line);
        CLAIM_CHECK(!rc);}
    oz_device_close(disk);disk=NULL;
    CLAIM_CHECK(RemDosEntry(entry));added=0;
    CLAIM_CHECK(!oz_device_open("ata.device",0,1,&disk));
    CLAIM_CHECK(!oz_device_bind_writable("/AROS/revoked-test",disk,&binding));
    /* A writer admitted before a DOS claim must not survive that claim. */
    unsigned char before[512],after[512],data[512];
    memset(data,0x69,sizeof(data));
    CLAIM_CHECK(!oz_device_transfer(disk,before,512,4096,0));
    CLAIM_CHECK(AddDosEntry(entry));added=1;
    CLAIM_CHECK(oz_device_transfer(disk,data,512,4096,1)==EBUSY);
    CLAIM_CHECK(oz_device_bound_open("/AROS/revoked-test",1,&bound)==EBUSY&&!bound);
    CLAIM_CHECK(oz_device_bind_writable("/AROS/revoked-alias",disk,&rejected)==EBUSY&&!rejected);
    CLAIM_CHECK(!oz_device_bound_open("/AROS/revoked-test",0,&bound));
    oz_device_close(bound);bound=NULL;
    CLAIM_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    CLAIM_CHECK(!memcmp(before,after,512));
    CLAIM_CHECK(RemDosEntry(entry));added=0;
    CLAIM_CHECK(oz_device_transfer(disk,data,512,4096,1)==EBUSY);
    CLAIM_CHECK(!oz_device_flush(disk));
    oz_device_unbind(binding);binding=NULL;
    oz_device_close(disk);disk=NULL;
    CLAIM_CHECK(!oz_device_open("ata.device",0,1,&disk));
    CLAIM_CHECK(!oz_device_transfer(disk,before,512,4096,1));
    CLAIM_CHECK(!oz_device_flush(disk));
    CLAIM_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    CLAIM_CHECK(!memcmp(before,after,512));
    note("RESULT native DOS revocation PASS late-claim=1 unchanged=1 sticky=1 reopen=1\n");
done:
    oz_device_close(bound);oz_device_unbind(binding);oz_device_unbind(rejected);
    oz_device_close(disk);
    if(added)RemDosEntry(entry);
    if(entry)FreeDosEntry(entry);
    {char line[160];snprintf(line,sizeof(line),
        "RESULT native DOS claim %s inactive=1 whole=1 range=1 read=1 other-unit=1 removed=1 line=%d\n",
        error?"FAIL":"PASS",error);note(line);}
    return error;
#undef CLAIM_CHECK
}
struct collision {unsigned unit;uint64_t base;int result;};
struct parallel_write {
    struct oz_device *disk;
    unsigned char *data,*readback;
    int *go,error;
};
static void *parallel_writer(void *arg)
{
    struct parallel_write *p=arg;
    while(!__atomic_load_n(p->go,__ATOMIC_ACQUIRE))Delay(1);
    for(unsigned i=0;i<16&&!p->error;i++)
        p->error=oz_device_transfer(p->disk,p->data,1024*1024,0,1);
    if(!p->error)p->error=oz_device_flush(p->disk);
    if(!p->error)p->error=oz_device_transfer(p->disk,p->readback,1024*1024,0,0);
    if(!p->error&&memcmp(p->data,p->readback,1024*1024))p->error=EIO;
    return NULL;
}
static int parallel_write_test(void)
{
    struct parallel_write p[2]={{0}};pthread_t threads[2];
    unsigned started=0;int go=0,error=0;
    for(unsigned i=0;i<2&&!error;i++) {
        p[i].go=&go;p[i].data=malloc(1024*1024);p[i].readback=malloc(1024*1024);
        if(!p[i].data||!p[i].readback){error=ENOMEM;break;}
        memset(p[i].data,0x51+i,1024*1024);
        error=oz_device_open_range("ata.device",0,1,(16+2*i)*1024*1024,1024*1024,&p[i].disk);
        if(!error){error=pthread_create(&threads[i],NULL,parallel_writer,&p[i]);if(!error)started++;}
    }
    __atomic_store_n(&go,1,__ATOMIC_RELEASE);
    /* Namespace publication must queue with our writers, not turn their
     * temporary DOS-list exclusion into a spurious mount or I/O EBUSY. */
    for(unsigned i=0;i<64&&!error;i++) {
        error=oz_device_publication_begin();
        if(!error)oz_device_publication_end();
    }
    for(unsigned i=0;i<started;i++) {
        int joined=pthread_join(threads[i],NULL);
        if(!error)error=joined?joined:p[i].error;
    }
    for(unsigned i=0;i<2;i++){oz_device_close(p[i].disk);free(p[i].data);free(p[i].readback);}
    char line[160];snprintf(line,sizeof(line),
        "RESULT native parallel write %s writers=2 chunks=32 publication=64 errno=%d first=%d second=%d\n",
        error?"FAIL":"PASS",error,p[0].error,p[1].error);note(line);return error;
}
static void *try_collision(void *argument)
{
    struct collision *c=argument;struct oz_device *disk=NULL;
    c->result=oz_device_open_range("ata.device",c->unit,1,c->base,512,&disk);
    oz_device_close(disk);return NULL;
}
struct write_probe {struct oz_device *disk;unsigned char data[512];int error;};
static void *try_write(void *argument)
{
    struct write_probe *p=argument;
    p->error=oz_device_transfer(p->disk,p->data,sizeof(p->data),4096,1);
    return NULL;
}
static void *try_publication(void *argument)
{
    int *error=argument;
    *error=oz_device_publication_begin();
    if(!*error)oz_device_publication_end();
    return NULL;
}
static int dos_write_publication_test(void)
{
    struct oz_device *disk=NULL;struct write_probe probe={0};
    unsigned char before[512],after[512];int error=0,locked=0,started=0;
    pthread_t thread;
#define PUB_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    PUB_CHECK(!oz_device_open("ata.device",0,1,&disk));probe.disk=disk;
    PUB_CHECK(!oz_device_transfer(disk,before,512,4096,0));
    memset(probe.data,0x9b,sizeof(probe.data));
    /* Another task's DOS write lock prevents our protected write interval.
     * Join while still holding the lock proves the writer did not block. */
    PUB_CHECK(LockDosList(LDF_ALL|LDF_WRITE));locked=1;
    PUB_CHECK(!pthread_create(&thread,NULL,try_write,&probe));started=1;
    PUB_CHECK(!pthread_join(thread,NULL));started=0;
    PUB_CHECK(probe.error==EBUSY);
    int publication_error=0;
    PUB_CHECK(!pthread_create(&thread,NULL,try_publication,&publication_error));started=1;
    PUB_CHECK(!pthread_join(thread,NULL));started=0;
    PUB_CHECK(publication_error==EBUSY);
    UnLockDosList(LDF_ALL|LDF_WRITE);locked=0;
    PUB_CHECK(!oz_device_publication_begin());oz_device_publication_end();
    PUB_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    PUB_CHECK(!memcmp(before,after,512));
    /* Entry publication also excludes writes without exclusive list access. */
    PUB_CHECK(LockDosList(LDF_ENTRY|LDF_WRITE));locked=2;
    PUB_CHECK(!pthread_create(&thread,NULL,try_write,&probe));started=1;
    PUB_CHECK(!pthread_join(thread,NULL));started=0;
    PUB_CHECK(probe.error==EBUSY);
    UnLockDosList(LDF_ENTRY|LDF_WRITE);locked=0;
    PUB_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    PUB_CHECK(!memcmp(before,after,512));
    /* Independent admission readers must coexist with raw writes. Neither
     * changes DOS entries; both must still exclude the mutators above. */
    PUB_CHECK(LockDosList(LDF_ALL|LDF_ENTRY|LDF_READ));locked=3;
    PUB_CHECK(!pthread_create(&thread,NULL,try_write,&probe));started=1;
    PUB_CHECK(!pthread_join(thread,NULL));started=0;
    PUB_CHECK(probe.error==0);
    UnLockDosList(LDF_ALL|LDF_ENTRY|LDF_READ);locked=0;
    PUB_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    PUB_CHECK(!memcmp(probe.data,after,512));
    /* Transient DOS contention did not revoke the writer. */
    PUB_CHECK(!oz_device_transfer(disk,probe.data,512,4096,1));
    PUB_CHECK(!oz_device_flush(disk));
    PUB_CHECK(!oz_device_transfer(disk,after,512,4096,0));
    PUB_CHECK(!memcmp(probe.data,after,512));
    PUB_CHECK(!oz_device_transfer(disk,before,512,4096,1));
    PUB_CHECK(!oz_device_flush(disk));
done:
    if(locked==1)UnLockDosList(LDF_ALL|LDF_WRITE);
    if(locked==2)UnLockDosList(LDF_ENTRY|LDF_WRITE);
    if(locked==3)UnLockDosList(LDF_ALL|LDF_ENTRY|LDF_READ);
    if(started)pthread_join(thread,NULL);
    oz_device_close(disk);
    char line[180];snprintf(line,sizeof(line),
        "RESULT native write DOS publication %s contention=1 nonblocking=1 unchanged=1 retry=1 readers-coexist=1 entry-excluded=1 line=%d\n",
        error?"FAIL":"PASS",error);note(line);
    if(!error)note("RESULT native shared entry read lock PASS writer-completed=1 mutators-excluded=1\n");
    return error;
#undef PUB_CHECK
}
static int exercise(unsigned unit,uint64_t size,uint64_t base)
{
    const size_t length=2*1024*1024;
    struct oz_device *whole=NULL,*part=NULL,*readonly=NULL,*bad=NULL,*retained=NULL;
    struct oz_device_binding *binding=NULL,*duplicate=NULL;
    unsigned char *data=malloc(length),*readback=malloc(length);
    unsigned char guard[512],before[512],after[512];int error=0;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(data&&readback);
    CHECK(!oz_device_open("ata.device",unit,0,&whole));
    CHECK(oz_device_size(whole)==size&&oz_device_sector(whole)==512);
    CHECK(!oz_device_transfer(whole,before,512,base-512,0));
    CHECK(!oz_device_transfer(whole,after,512,base+length,0));
    CHECK(!oz_device_transfer(whole,readback,length,base,0));
    /* Refuse to modify any nonblank test extent or adjacent guard. */
    for(size_t i=0;i<length;i++)CHECK(readback[i]==0);
    for(unsigned i=0;i<512;i++)CHECK(before[i]==0&&after[i]==0);
    CHECK(!oz_device_open_range("ata.device",unit,1,base,length,&part));
    CHECK(oz_device_size(part)==length&&oz_device_sector(part)==512);
    CHECK(oz_device_open("ata.device",unit,1,&bad)==EBUSY&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base,length,&bad)==EBUSY&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base-512,1024,&bad)==EBUSY&&!bad);
    struct collision c={unit,base,0};pthread_t worker;
    CHECK(!pthread_create(&worker,NULL,try_collision,&c));
    CHECK(!pthread_join(worker,NULL));
    CHECK(c.result==EBUSY);
    CHECK(!oz_device_open_range("ata.device",unit,1,base+length,512,&bad));
    oz_device_close(bad);bad=NULL;
    CHECK(!oz_device_open_range("ata.device",unit,0,base,length,&readonly));
    CHECK(oz_device_bind_readonly("/AROS/test-extent",part,&binding)==EROFS&&!binding);
    CHECK(oz_device_bind_readonly("/AROS/test-extent",readonly,&binding)==EBUSY&&!binding);
    oz_device_close(part);part=NULL;
    CHECK(!oz_device_bind_readonly("/AROS/test-extent",readonly,&binding));
    CHECK(oz_device_open("ata.device",unit,1,&bad)==EBUSY&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base,length,&bad)==EBUSY&&!bad);
    CHECK(!oz_device_open_range("ata.device",unit,1,base+length,512,&bad));
    oz_device_close(bad);bad=NULL;
    CHECK(oz_device_bind_readonly("/AROS/test-extent",readonly,&duplicate)==EEXIST&&!duplicate);
    CHECK(oz_device_bound_open("/AROS/test-extent/other",0,&bad)==ENOENT&&!bad);
    CHECK(oz_device_bound_open("/AROS/test-extent",1,&bad)==EROFS&&!bad);
    CHECK(!oz_device_bound_open("/AROS/test-extent",0,&bad));
    CHECK(bad==readonly&&oz_device_size(bad)==length);
    oz_device_unbind(binding);binding=NULL;
    CHECK(oz_device_bound_open("/AROS/test-extent",0,&retained)==ENOENT&&!retained);
    oz_device_close(readonly);readonly=bad;bad=NULL;
    CHECK(!oz_device_transfer(readonly,readback,length,0,0));
    c.result=0;
    CHECK(!pthread_create(&worker,NULL,try_collision,&c));
    CHECK(!pthread_join(worker,NULL));
    CHECK(c.result==EBUSY);
    oz_device_close(readonly);readonly=NULL;
    CHECK(!oz_device_open_range("ata.device",unit,1,base,length,&part));
    CHECK(!oz_device_open_range("ata.device",unit,0,base,length,&readonly));
    CHECK(oz_device_bind_writable("/AROS/test-write",readonly,&binding)==EROFS&&!binding);
    CHECK(!oz_device_bind_writable("/AROS/test-write",part,&binding));
    CHECK(oz_device_bind_writable("/AROS/test-write",part,&duplicate)==EEXIST&&!duplicate);
    CHECK(oz_device_bind_writable("/AROS/test-write-alias",part,&duplicate)==EBUSY&&!duplicate);
    CHECK(oz_device_binding_repath_end(binding,0)==EINVAL);
    CHECK(oz_device_binding_repath_begin(binding,part,whole,"/AROS/repath")==ESTALE);
    CHECK(oz_device_binding_repath_begin(binding,readonly,readonly,"/AROS/repath")==ESTALE);
    CHECK(!oz_device_bind_unavailable("/AROS/repath-collision",&duplicate));
    CHECK(oz_device_binding_repath_begin(binding,part,readonly,"/AROS/repath-collision")==EEXIST);
    oz_device_unbind(duplicate);duplicate=NULL;
    CHECK(!oz_device_binding_repath_begin(binding,part,readonly,"/AROS/repath"));
    CHECK(oz_device_bind_unavailable("/AROS/repath",&duplicate)==EEXIST&&!duplicate);
    CHECK(oz_device_binding_repath_begin(binding,part,readonly,"/AROS/other")==EBUSY);
    CHECK(!oz_device_bound_open("/AROS/repath",1,&bad)&&bad==part);
    oz_device_close(bad);bad=NULL;
    CHECK(!oz_device_bound_open("/AROS/test-write",1,&bad)&&bad==part);
    oz_device_close(bad);bad=NULL;
    CHECK(!oz_device_binding_repath_end(binding,EIO));
    CHECK(oz_device_bound_open("/AROS/repath",1,&bad)==ENOENT&&!bad);
    CHECK(!oz_device_bound_open("/AROS/test-write",1,&bad)&&bad==part);
    oz_device_close(bad);bad=NULL;
    CHECK(!oz_device_binding_repath_begin(binding,part,readonly,"/AROS/repath"));
    CHECK(!oz_device_binding_repath_end(binding,0));
    CHECK(oz_device_bound_open("/AROS/test-write",1,&bad)==ENOENT&&!bad);
    CHECK(!oz_device_bound_open("/AROS/repath",1,&bad)&&bad==part);
    oz_device_close(bad);bad=NULL;
    CHECK(oz_device_open_range("ata.device",unit,1,base,length,&bad)==EBUSY&&!bad);
    CHECK(!oz_device_binding_repath_begin(binding,part,readonly,"/AROS/test-write"));
    CHECK(!oz_device_binding_repath_end(binding,0));
    {char line[160];snprintf(line,sizeof(line),
        "RESULT native binding repath PASS unit=%u identity=1 rollback=1 commit=1 collisions=1 reservation=1\n",unit);note(line);}
    CHECK(!oz_device_bound_open("/AROS/test-write",1,&retained));
    CHECK(retained==part&&oz_device_size(retained)==length);
    oz_device_close(part);part=NULL;
    oz_device_unbind(binding);binding=NULL;
    CHECK(oz_device_bound_open("/AROS/test-write",1,&bad)==ENOENT&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base,length,&bad)==EBUSY&&!bad);
    /* All following real read/write cycles now use the bound-open reference
     * after both admission's reference and the name binding were released. */
    part=retained;retained=NULL;
    CHECK(oz_device_open_range("ata.device",unit,1,base,0,&bad)==EINVAL&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base+1,length,&bad)==EINVAL&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base,length-1,&bad)==EINVAL&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,size-512,1024,&bad)==EINVAL&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,UINT64_MAX,512,&bad)==EINVAL&&!bad);
    CHECK(oz_device_open_range("ata.device",unit,1,base,UINT64_MAX,&bad)==EINVAL&&!bad);
    for(unsigned cycle=0;cycle<4;cycle++) {
        for(size_t i=0;i<length;i++)data[i]=(unsigned char)(i*13+cycle+unit+1);
        CHECK(!oz_device_transfer(part,data,length,0,1));
        CHECK(!oz_device_flush(part));
        CHECK(!oz_device_transfer(readonly,readback,length,0,0));
        CHECK(!memcmp(data,readback,length));
        CHECK(!oz_device_transfer(whole,readback,length,base,0));
        CHECK(!memcmp(data,readback,length));
        CHECK(oz_device_transfer(part,data,512,length,1)==EINVAL);
        CHECK(oz_device_transfer(part,data,1024,length-512,1)==EINVAL);
        CHECK(oz_device_transfer(part,data,512,UINT64_MAX,1)==EINVAL);
        CHECK(oz_device_transfer(part,data,511,0,1)==EINVAL);
        CHECK(oz_device_transfer(part,data,512,1,1)==EINVAL);
        CHECK(oz_device_transfer(readonly,data,512,0,1)==EROFS);
        CHECK(!oz_device_transfer(part,NULL,0,length,0));
        CHECK(!oz_device_transfer(part,NULL,0,length,1));
        CHECK(!oz_device_transfer(whole,guard,512,base-512,0));
        CHECK(!memcmp(guard,before,512));
        CHECK(!oz_device_transfer(whole,guard,512,base+length,0));
        CHECK(!memcmp(guard,after,512));
        CHECK(!oz_device_transfer(part,readback,length,0,0));
        CHECK(!memcmp(data,readback,length));
    }
    CHECK(oz_device_retain(NULL)==EINVAL);
    CHECK(!oz_device_retain(part));retained=part;
    oz_device_close(part);part=NULL;
    CHECK(oz_device_open_range("ata.device",unit,1,base,length,&bad)==EBUSY&&!bad);
    CHECK(oz_device_size(retained)==length);
    CHECK(!oz_device_transfer(retained,readback,length,0,0));
    CHECK(!memcmp(data,readback,length));
    CHECK(!oz_device_flush(retained));
    oz_device_close(retained);retained=NULL;
    CHECK(!oz_device_open_range("ata.device",unit,1,base,length,&bad));
    oz_device_close(bad);bad=NULL;
    oz_device_close(readonly);readonly=NULL;
    CHECK(!oz_device_open_range("ata.device",unit,0,base,length,&readonly));
    CHECK(!oz_device_transfer(readonly,readback,length,0,0));
    CHECK(!memcmp(data,readback,length));
done:
    oz_device_unbind(duplicate);oz_device_unbind(binding);
    oz_device_close(retained);oz_device_close(bad);oz_device_close(readonly);oz_device_close(part);oz_device_close(whole);
    free(data);free(readback);
    {char line[200];snprintf(line,sizeof(line),
        "Native range unit=%u base=%llu cycles=4 guards=2 bounds=1 readonly=1 reopen=1 reservations=1 worker=1 retained=1 line=%d %s\n",
        unit,(unsigned long long)base,error,error?"FAIL":"PASS");note(line);}
    return error;
#undef CHECK
}
int main(int argc,char **argv)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-device-range-test-only",SHARED_LOCK);
    if(!marker)return 20;
    UnLock(marker);
    if(argc!=2||strcmp(argv[1],"DISPOSABLE"))return 20;
    marker=Lock((CONST_STRPTR)"SYS:openzfs-efi-cold-only",SHARED_LOCK);
    if(marker){UnLock(marker);return oz_efi_native_cold_test()?20:0;}
    int error=efi_uuid_test();
    if(!error)error=detach_fixture_mounts();
    if(!error)error=dos_claim_test();
    if(!error)error=dos_write_publication_test();
    if(!error)error=parallel_write_test();
    if(!error)error=exercise(0,UINT64_C(268435456),UINT64_C(4097)*512);
    if(!error)error=exercise(1,UINT64_C(8589934592),UINT64_C(0x100000000)-512);
    if(!error)error=oz_efi_native_test();
    {int rc=restore_fixture_mounts();if(!error)error=rc;}
    note(error?"RESULT native bounded device I/O FAIL\n":
        "RESULT native bounded device I/O PASS units=2 cross-4GiB=1 translated=1 guards=1 bounds=1 readonly=1 reopen=1\n");
    return error?20:0;
}
