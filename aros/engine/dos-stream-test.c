/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/acl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <pthread.h>
#include <libraries/security.h>
#include <dos/dos.h>
#include "zpl-fixture.h"
#include "dos-handler.h"
#include "packet-cred.h"
int oz_packet_reader_work_test(int (*)(const struct oz_packet_security *,void *),void *);
struct stream_fixture {struct oz_zpl *fs;struct oz_mount *mount;uint64_t object;int write;};
struct list_holder {struct MsgPort *ready;struct Message message;ULONG flags;int locked;};
static void *hold_dos_list(void *arg)
{
    struct list_holder *holder=arg;
    struct DosList *list=LockDosList(holder->flags);
    holder->locked=list!=NULL;
    PutMsg(holder->ready,&holder->message);
    /* Bounded even against the old blocking detach: release the list after
     * two guest seconds so the negative control can report failure. */
    if(list){Delay(100);UnLockDosList(holder->flags);}
    return NULL;
}
static int detach_contention(struct oz_mount *mount)
{
    struct list_holder holder={.flags=LDF_ALL|LDF_READ};pthread_t thread;
    holder.ready=CreateMsgPort();if(!holder.ready)return ENOMEM;
    holder.message.mn_Node.ln_Type=NT_MESSAGE;
    holder.message.mn_Length=sizeof(holder.message);
    int error=pthread_create(&thread,NULL,hold_dos_list,&holder),observed=-1;
    if(!error){
        WaitPort(holder.ready);GetMsg(holder.ready);
        if(!holder.locked)error=EIO;
        else observed=oz_dos_detach(mount);
        int joined=pthread_join(thread,NULL);if(!error)error=joined;
        if(!error&&observed!=EBUSY)error=EIO;
    }
    DeleteMsgPort(holder.ready);
    char line[128];snprintf(line,sizeof(line),"Native detach DOS-list contention observed=%d expected=%d result=%d\n",observed,EBUSY,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int mount_contention(struct oz_zpl *fs)
{
    struct list_holder holder={.flags=LDF_ENTRY|LDF_WRITE};pthread_t thread;
    struct oz_mount *mount=NULL;int observed=-1;
    holder.ready=CreateMsgPort();if(!holder.ready)return ENOMEM;
    holder.message.mn_Node.ln_Type=NT_MESSAGE;holder.message.mn_Length=sizeof(holder.message);
    int error=pthread_create(&thread,NULL,hold_dos_list,&holder);
    if(!error){
        WaitPort(holder.ready);GetMsg(holder.ready);
        if(!holder.locked)error=EIO;
        else observed=oz_dos_mount(fs,"OZMOUNTPOL",&mount);
        int joined=pthread_join(thread,NULL);if(!error)error=joined;
        if(!error&&(observed!=EBUSY||mount))error=EIO;
    }
    DeleteMsgPort(holder.ready);
    if(mount){int rc=oz_dos_unmount(mount);if(!error)error=rc;mount=NULL;}
    if(!error){
        struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
        if(FindDosEntry(list,(CONST_STRPTR)"OZMOUNTPOL",LDF_ALL))error=EIO;
        UnLockDosList(LDF_ALL|LDF_READ);
    }
    /* A refused publication must not reserve the name or poison a retry. */
    if(!error)error=oz_dos_mount(fs,"OZMOUNTPOL",&mount);
    if(!error){
        struct oz_mount *duplicate=NULL;
        if(oz_dos_mount(fs,"OZMOUNTPOL",&duplicate)!=EEXIST||duplicate)error=EIO;
        if(duplicate){int rc=oz_dos_unmount(duplicate);if(!error)error=rc;}
    }
    if(mount){int rc=oz_dos_unmount(mount);if(!error)error=rc;}
    char line[128];snprintf(line,sizeof(line),"Native mount DOS-entry contention observed=%d expected=%d result=%d\n",observed,EBUSY,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int staged_writer(const struct oz_packet_security *security,void *arg)
{
    struct stream_fixture *f=arg;struct oz_dos_stream *stream=NULL;
    struct oz_zpl_info info;char bytes[16]={0};uint32_t defaults=0;int error=0;
    uint64_t root=oz_zpl_root(f->fs);
#define STAGE_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    STAGE_CHECK(!oz_packet_security_defaults(security,&defaults));
    if(f->write){
        STAGE_CHECK(oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:created",&stream)==EACCES&&!stream);
        STAGE_CHECK(!oz_zpl_protection_store(f->fs,root,FIBF_OTR_READ|FIBF_OTR_WRITE|FIBF_OTR_EXECUTE,kcred));
        STAGE_CHECK(oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:/bad-parent",&stream)==ENOENT&&!stream);
        STAGE_CHECK(oz_zpl_lookup_at(f->fs,root,"bad-parent",&info)==ENOENT);
        STAGE_CHECK(!oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:created",&stream));
        STAGE_CHECK(!oz_dos_stream_write(stream,"created",7));
        STAGE_CHECK(oz_zpl_lookup_at(f->fs,root,"created",&info)==ENOENT);
        {int rc=oz_dos_stream_finish(stream,1);stream=NULL;STAGE_CHECK(!rc);}
        STAGE_CHECK(!oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:aborted",&stream));
        STAGE_CHECK(!oz_dos_stream_write(stream,"partial",7));
        {int rc=oz_dos_stream_finish(stream,0);stream=NULL;STAGE_CHECK(!rc);}
        STAGE_CHECK(oz_zpl_lookup_at(f->fs,root,"aborted",&info)==ENOENT);
        STAGE_CHECK(!oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:revoked",&stream));
        STAGE_CHECK(!oz_dos_stream_write(stream,"partial",7));
        STAGE_CHECK(!oz_zpl_protection_store(f->fs,root,FIBF_OTR_READ|FIBF_OTR_EXECUTE,kcred));
        {int rc=oz_dos_stream_finish(stream,1);stream=NULL;STAGE_CHECK(rc==EACCES);}
        STAGE_CHECK(oz_zpl_lookup_at(f->fs,root,"revoked",&info)==ENOENT);
        STAGE_CHECK(!oz_zpl_protection_store(f->fs,root,FIBF_OTR_READ|FIBF_OTR_WRITE|FIBF_OTR_EXECUTE,kcred));
        STAGE_CHECK(!oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:collision",&stream));
        STAGE_CHECK(!oz_dos_stream_write(stream,"partial",7));
        STAGE_CHECK(!oz_zpl_create_as(f->fs,root,"collision",0,kcred,&info));
        uint64_t collision=info.object;
        {int rc=oz_dos_stream_finish(stream,1);stream=NULL;STAGE_CHECK(rc==EEXIST);}
        STAGE_CHECK(!oz_zpl_lookup_at(f->fs,root,"collision",&info)&&info.object==collision&&!info.size);
        STAGE_CHECK(!oz_zpl_protection_store(f->fs,root,FIBF_OTR_READ|FIBF_OTR_EXECUTE,kcred));
    }
    STAGE_CHECK(!oz_zpl_lookup_at(f->fs,root,"created",&info));
    STAGE_CHECK(info.size==7&&info.mapped_uid==77&&info.mapped_gid==88&&info.has_native_protection&&info.native_protection==defaults);
    STAGE_CHECK(!oz_zpl_read_as(f->fs,info.object,0,bytes,7,kcred)&&!memcmp(bytes,"created",7));
    for(uint32_t i=0;;i++){
        char name[256];int rc=oz_zpl_nth(f->fs,root,i,name,sizeof(name),&info);
        if(rc==ENOENT)break;
        STAGE_CHECK(!rc&&strncmp(name,".ozsend-",8));
    }
done:
    if(stream)oz_dos_stream_finish(stream,0);
    return error;
#undef STAGE_CHECK
}
static int writer(const struct oz_packet_security *security,void *arg)
{
    struct stream_fixture *f=arg;struct oz_dos_stream *stream=NULL,*other=NULL;
    struct oz_zpl_info info;char bytes[16]={0};size_t actual=0;int error=0;
#define WRITE_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    WRITE_CHECK(crgetuid(oz_packet_security_cred(security))==77);
    WRITE_CHECK(!oz_zpl_lookup_at(f->fs,oz_zpl_root(f->fs),"output",&info));
    if(!f->write){
        WRITE_CHECK(oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:output",&stream)==EROFS&&!stream);
    }else{
        WRITE_CHECK(!oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:output",&stream));
        WRITE_CHECK(oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:output",&other)==EBUSY&&!other);
        WRITE_CHECK(oz_dos_stream_read(stream,bytes,sizeof(bytes),&actual)==EBADF&&!actual);
        WRITE_CHECK(oz_dos_detach(f->mount)==EBUSY);
        WRITE_CHECK(!oz_zpl_protection_store(f->fs,info.object,0,kcred));
        WRITE_CHECK(oz_dos_stream_write(stream,"denied",6)==EACCES);
        WRITE_CHECK(!oz_zpl_stat(f->fs,info.object,&info)&&!info.size);
        WRITE_CHECK(!oz_zpl_protection_store(f->fs,info.object,FIBF_OTR_WRITE,kcred));
        WRITE_CHECK(!oz_dos_stream_write(stream,"written",7));
        {int rc=oz_dos_stream_finish(stream,1);stream=NULL;WRITE_CHECK(!rc);}
        WRITE_CHECK(oz_dos_stream_open_output(f->mount,security,"OZSTREAMPOL:output",&stream)==EEXIST&&!stream);
    }
    WRITE_CHECK(!oz_zpl_stat(f->fs,info.object,&info)&&info.size==7);
    WRITE_CHECK(!oz_zpl_read_as(f->fs,info.object,0,bytes,7,kcred)&&!memcmp(bytes,"written",7));
done:
    if(other)oz_dos_stream_finish(other,0);
    if(stream)oz_dos_stream_finish(stream,0);
    return error;
#undef WRITE_CHECK
}
static int reader(const struct oz_packet_security *security,void *arg)
{
    struct stream_fixture *f=arg;struct oz_dos_stream *stream=NULL,*other=NULL;
    const char *path="OZSTREAMPOL:hello.txt";int error=0;size_t count=0;char bytes[128];
#define CHECK_STREAM(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK_STREAM(crgetuid(oz_packet_security_cred(security))==77);
    CHECK_STREAM(!oz_dos_stream_open(f->mount,security,path,&stream));
    CHECK_STREAM(oz_dos_stream_open(f->mount,security,path,&other)==EBUSY&&!other);
    CHECK_STREAM(oz_dos_detach(f->mount)==EBUSY);
    if(f->write){
        CHECK_STREAM(!oz_zpl_protection_store(f->fs,f->object,0,kcred));
        CHECK_STREAM(oz_dos_stream_read(stream,bytes,sizeof(bytes),&count)==EACCES&&!count);
        CHECK_STREAM(!oz_zpl_protection_store(f->fs,f->object,FIBF_OTR_READ,kcred));
    }
    CHECK_STREAM(!oz_dos_stream_read(stream,bytes,sizeof(bytes),&count));
    const char *expected="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    CHECK_STREAM(count==strlen(expected)&&!memcmp(bytes,expected,count));
    CHECK_STREAM(!oz_dos_stream_read(stream,bytes,sizeof(bytes),&count)&&!count);
    oz_dos_stream_close(stream);stream=NULL;
    if(f->write){
        CHECK_STREAM(!oz_zpl_protection_store(f->fs,oz_zpl_root(f->fs),FIBF_OTR_READ,kcred));
        CHECK_STREAM(oz_dos_stream_open(f->mount,security,path,&stream)==EACCES&&!stream);
        CHECK_STREAM(!oz_zpl_protection_store(f->fs,oz_zpl_root(f->fs),FIBF_OTR_READ|FIBF_OTR_EXECUTE,kcred));
    }
    CHECK_STREAM(oz_dos_stream_open(f->mount,security,"RAM:unrelated",&stream)==ENODEV&&!stream);
done:
    if(other)oz_dos_stream_close(other);
    if(stream)oz_dos_stream_close(stream);
    return error;
#undef CHECK_STREAM
}
static int acl_reader(const struct oz_packet_security *security,void *arg)
{
    struct stream_fixture *f=arg;struct oz_dos_stream *stream=NULL;
    struct cred *cred=oz_packet_security_cred(security);
    struct oz_zpl_info file,dir;int error=0;
    if(crgetuid(cred)!=77)return EACCES;
    error=oz_zpl_lookup_at(f->fs,oz_zpl_root(f->fs),"hello.txt",&file);
    if(!error)error=oz_zpl_lookup_at(f->fs,oz_zpl_root(f->fs),"subdir",&dir);
    /* Prove the denial exists in the actual upstream ACL independently of
     * native protection. kcred can read both; the captured caller cannot. */
    if(!error&&(oz_zpl_access(f->fs,file.object,ACE_READ_DATA,cred)!=EACCES||
        oz_zpl_access(f->fs,dir.object,ACE_EXECUTE,cred)!=EACCES||
        oz_zpl_access(f->fs,file.object,ACE_READ_DATA,kcred)||
        oz_zpl_access(f->fs,dir.object,ACE_EXECUTE,kcred)))error=EIO;
    const char *paths[]={"OZSTREAMACL:hello.txt","OZSTREAMACL:subdir/nested.txt"};
    for(unsigned i=0;i<2&&!error;i++){
        int rc=oz_dos_stream_open(f->mount,security,paths[i],&stream);
        if(rc!=EACCES||stream)error=EIO;
        if(stream){oz_dos_stream_close(stream);stream=NULL;}
    }
    return error;
}
static int acl_stream_test(void)
{
    /* The baseline's persisted writefiles fixture has a metadata-only file
     * ACL and a non-searchable directory ACL, created with upstream setacl. */
    struct stream_fixture f={0};int error=oz_zpl_open("qemu_mirror/writefiles",&f.fs);
    if(!error)error=oz_dos_mount(f.fs,"OZSTREAMACL",&f.mount);
    if(!error)error=oz_packet_reader_work_test(acl_reader,&f);
    if(f.mount){int rc=oz_dos_unmount(f.mount);if(!error)error=rc;if(rc)return error;}
    if(f.fs){int rc=oz_zpl_close(f.fs);if(!error)error=rc;}
    char line[100];snprintf(line,sizeof(line),"Native direct stream upstream ACL denial result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
int oz_dos_stream_policy_test(int write)
{
    const char *dataset="qemu_mirror/direct-stream-policy";
    struct stream_fixture f={.write=write};uint64_t size;
    int error=write?oz_zpl_fixture_create(dataset):0;
    if(!error)error=write?oz_zpl_open_writable(dataset,&f.fs):oz_zpl_open(dataset,&f.fs);
    if(!error)error=oz_zpl_lookup(f.fs,"hello.txt",&f.object,&size);
    if(!error&&write)error=oz_zpl_protection_store(f.fs,f.object,FIBF_OTR_READ,kcred);
    if(!error&&write)error=oz_zpl_protection_store(f.fs,oz_zpl_root(f.fs),FIBF_OTR_READ|FIBF_OTR_EXECUTE,kcred);
    if(!error&&write){struct oz_zpl_info info;error=oz_zpl_create_as(f.fs,oz_zpl_root(f.fs),"output",FIBF_OTR_WRITE,kcred,&info);}
    if(!error)error=mount_contention(f.fs);
    if(!error)error=oz_dos_mount(f.fs,"OZSTREAMPOL",&f.mount);
    if(!error)error=oz_packet_reader_work_test(reader,&f);
    if(!error)error=oz_packet_reader_work_test(writer,&f);
    if(!error)error=oz_packet_reader_work_test(staged_writer,&f);
    if(!error)error=detach_contention(f.mount);
    if(!error)error=oz_packet_reader_work_test(reader,&f);
    if(f.mount){int rc=oz_dos_unmount(f.mount);if(!error)error=rc;if(rc)return error;}
    if(f.fs){int rc=oz_zpl_sync(f.fs);if(!error)error=rc;rc=oz_zpl_close(f.fs);if(!error)error=rc;}
    if(!error)error=acl_stream_test();
    char line[128];snprintf(line,sizeof(line),"Native direct stream original-caller policy write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
