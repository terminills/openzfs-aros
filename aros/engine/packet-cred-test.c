/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <dos/dosextens.h>
#include <libraries/security.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include "packet-cred.h"
#include "admission.h"
#include "stream-file.h"
#include "stream-output.h"
int oz_zpl_dataset_create(const char *, cred_t *);

struct packet_sender {
    struct MsgPort *destination;
    struct Message failed;
    int error;
    const char *dataset;
};

static int traversal_expect(const char *path,int observed,int expected)
{
    if(observed==expected)return 0;
    char line[192];snprintf(line,sizeof(line),"Native stream relative path=%s observed=%d expected=%d dos=%ld\n",
        path,observed,expected,(long)IoErr());
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return EIO;
}
static int native_stream_namespace_checks(const struct oz_packet_security *caller,BPTR parent)
{
    const char *names[]={"OZSTRMBOUND","OZSTRMLATE","OZSTRMPATH","OZSTRMLOOP","OZSTRMMULTI","OZSTRMSELF"};
    const char *dir="RAM:OpenZFS-native-stream-directory-test";
    int created[5]={0},error=0,changed=0;LONG protection=0;
    BPTR root=0,lock=0;
    struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
    for(unsigned i=0;i<6;i++)if(FindDosEntry(list,(CONST_STRPTR)names[i],LDF_ALL))error=EEXIST;
    UnLockDosList(LDF_ALL|LDF_READ);
    if(error)return error;
    root=Lock((CONST_STRPTR)"RAM:",ACCESS_READ);if(!root)return EIO;
    struct FileInfoBlock *info=AllocDosObject(DOS_FIB,NULL);
    if(!info)error=ENOMEM;
    if(!error){
        if(!Examine(root,info))error=EIO;
        else protection=info->fib_Protection;
    }
    if(info)FreeDosObject(DOS_FIB,info);
    if(!error){
        if(!SetProtection((CONST_STRPTR)"RAM:",protection|FIBF_OTR_EXECUTE))error=EIO;
        else changed=1;
    }
    if(!error){
        lock=DupLock(parent);
        if(!lock||!AssignLock((CONST_STRPTR)names[0],lock))error=EIO;
        else {created[0]=1;lock=0;}
        if(lock){UnLock(lock);lock=0;}
    }
    if(!error){if(!AssignLate((CONST_STRPTR)names[1],(CONST_STRPTR)dir))error=EIO;else created[1]=1;}
    if(!error){if(!AssignPath((CONST_STRPTR)names[2],(CONST_STRPTR)"OZSTRMLATE:"))error=EIO;else created[2]=1;}
    if(!error){if(!AssignLate((CONST_STRPTR)names[3],(CONST_STRPTR)"OZSTRMLOOP:"))error=EIO;else created[3]=1;}
    if(!error){
        lock=Lock((CONST_STRPTR)"RAM:OpenZFS-native-stream-directory-test/sub",ACCESS_READ);
        if(!lock||!AssignLock((CONST_STRPTR)names[4],lock))error=EIO;
        else {created[4]=1;lock=0;}
        if(lock){UnLock(lock);lock=0;}
    }
    if(!error){
        lock=DupLock(parent);
        if(!lock||!AssignAdd((CONST_STRPTR)names[4],lock))error=EIO;
        else lock=0;
        if(lock){UnLock(lock);lock=0;}
    }
    const char *paths[]={"RAM:OpenZFS-native-stream-directory-test/stream",
        "RAM:OpenZFS-native-stream-directory-test/alias","OZSTRMBOUND:stream",
        "OZSTRMLATE:stream","OZSTRMPATH:stream","OZSTRMMULTI:stream"};
    for(unsigned i=0;i<6&&!error;i++){
        error=traversal_expect(paths[i],oz_stream_path_lock(caller,paths[i],&lock),0);
        if(!error)error=oz_stream_lock_access(caller,lock,secAt_Read);
        if(lock){UnLock(lock);lock=0;}
    }
    if(!error)error=traversal_expect("assign cycle",oz_stream_path_lock(caller,"OZSTRMLOOP:stream",&lock),ELOOP);
    if(!error)error=traversal_expect("missing volume",oz_stream_path_lock(caller,"OZSTRMABSENT:stream",&lock),ENOENT);
    if(!error)error=traversal_expect("PROGDIR",oz_stream_path_lock(caller,"PROGDIR:stream",&lock),EINVAL);
    if(!error){
        list=LockDosList(LDF_ALL|LDF_READ);
        struct DosList *late=FindDosEntry(list,(CONST_STRPTR)names[1],LDF_ALL);
        struct DosList *nonbinding=FindDosEntry(list,(CONST_STRPTR)names[2],LDF_ALL);
        if(!late||!nonbinding||late->dol_Type!=DLT_LATE||nonbinding->dol_Type!=DLT_NONBINDING)error=EIO;
        UnLockDosList(LDF_ALL|LDF_READ);
    }
    if(!error){
        struct MsgPort *self=CreateMsgPort();
        struct DosList *device=MakeDosEntry((CONST_STRPTR)names[5],DLT_DEVICE);
        int added=0;
        if(!self||!device)error=ENOMEM;
        else {
            device->dol_Task=self;
            LockDosList(LDF_DEVICES|LDF_WRITE);
            added=AddDosEntry(device);
            UnLockDosList(LDF_DEVICES|LDF_WRITE);
            if(!added)error=EIO;
            else error=traversal_expect("self handler",oz_stream_path_lock(caller,"OZSTRMSELF:stream",&lock),EDEADLK);
        }
        if(added){LockDosList(LDF_DEVICES|LDF_WRITE);RemDosEntry(device);UnLockDosList(LDF_DEVICES|LDF_WRITE);}
        if(device)FreeDosEntry(device);
        if(self)DeleteMsgPort(self);
    }
    if(lock)UnLock(lock);
    for(unsigned i=0;i<5;i++)if(created[i]&&!AssignLock((CONST_STRPTR)names[i],0)&&!error)error=EIO;
    if(changed&&!SetProtection((CONST_STRPTR)"RAM:",protection)&&!error)error=EIO;
    UnLock(root);
    char line[128];snprintf(line,sizeof(line),"Native stream namespace authorization result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int native_stream_traversal_checks(const struct oz_packet_security *caller)
{
    const char *dir="RAM:OpenZFS-native-stream-directory-test";
    const char *name="RAM:OpenZFS-native-stream-directory-test/stream";
    const char *alias="RAM:OpenZFS-native-stream-directory-test/alias";
    const char *relative="RAM:OpenZFS-native-stream-directory-test/relative";
    const char *cycle="RAM:OpenZFS-native-stream-directory-test/cycle";
    const char *sub="RAM:OpenZFS-native-stream-directory-test/sub";
    BPTR parent=Lock((CONST_STRPTR)dir,ACCESS_READ),child=0,file=0;
    if(parent){UnLock(parent);return EEXIST;}
    parent=CreateDir((CONST_STRPTR)dir);if(!parent)return EIO;
    int error=0,linked=0,relative_linked=0,cycle_linked=0,sub_created=0;
    struct oz_stream_file *stream=NULL;
    file=Open((CONST_STRPTR)name,MODE_NEWFILE);
    if(!file)error=EIO;
    else if(!Close(file))error=EIO;
    if(!error&&(!SetOwner((CONST_STRPTR)dir,(55UL<<16)|99)||
        !SetOwner((CONST_STRPTR)name,(55UL<<16)|99)||
        !SetProtection((CONST_STRPTR)name,FIBF_GRP_READ)||
        !SetProtection((CONST_STRPTR)dir,FIBF_GRP_READ)))error=EIO;
    if(!error&&(oz_stream_child_lock(caller,parent,"stream",&child)!=EACCES||child))error=EIO;
    if(!error&&!SetProtection((CONST_STRPTR)dir,FIBF_GRP_EXECUTE))error=EIO;
    if(!error&&oz_stream_child_lock(caller,parent,"missing",&child)!=ENOENT)error=EIO;
    if(!error&&oz_stream_child_lock(caller,parent,"a/stream",&child)!=EINVAL)error=EIO;
    if(!error&&oz_stream_child_lock(caller,parent,"RAM:stream",&child)!=EINVAL)error=EIO;
    if(!error){
        if(!MakeLink((CONST_STRPTR)alias,(SIPTR)name,LINK_SOFT))error=EIO;
        else linked=1;
    }
    if(!error&&(oz_stream_child_lock(caller,parent,"alias",&child)!=ELOOP||child))error=EIO;
    if(!error)error=traversal_expect("exclusive base",oz_stream_relative_lock(caller,parent,"alias",&child),EBUSY);
    if(!error){
        UnLock(parent);parent=Lock((CONST_STRPTR)dir,ACCESS_READ);
        if(!parent)error=EIO;
    }
    if(!error)error=traversal_expect("alias",oz_stream_relative_lock(caller,parent,"alias",&child),EXDEV);
    if(!error){
        if(!MakeLink((CONST_STRPTR)relative,(SIPTR)"stream",LINK_SOFT))error=EIO;
        else relative_linked=1;
    }
    if(!error){
        if(!MakeLink((CONST_STRPTR)cycle,(SIPTR)"cycle",LINK_SOFT))error=EIO;
        else cycle_linked=1;
    }
    if(!error)error=traversal_expect("cycle",oz_stream_relative_lock(caller,parent,"cycle",&child),ELOOP);
    if(!error){
        BPTR created=CreateDir((CONST_STRPTR)sub);
        if(!created)error=EIO;else {UnLock(created);sub_created=1;}
    }
    if(!error&&(!SetOwner((CONST_STRPTR)sub,(55UL<<16)|99)||
        !SetProtection((CONST_STRPTR)sub,FIBF_GRP_READ)))error=EIO;
    if(!error)error=traversal_expect("denied sub//stream",oz_stream_relative_lock(caller,parent,"sub//stream",&child),EACCES);
    if(!error&&!SetProtection((CONST_STRPTR)sub,FIBF_GRP_EXECUTE))error=EIO;
    const char *paths[]={"stream","relative","sub//stream"};
    for(unsigned i=0;i<3&&!error;i++){
        error=traversal_expect(paths[i],oz_stream_relative_lock(caller,parent,paths[i],&child),0);
        if(!error)error=oz_stream_lock_access(caller,child,secAt_Read);
        if(child){UnLock(child);child=0;}
    }
    if(!error)error=native_stream_namespace_checks(caller,parent);
    if(!error)error=oz_stream_child_lock(caller,parent,"stream",&child);
    if(!error)error=oz_stream_file_open(caller,&child,secAt_Read,&stream);
    if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
    if(child)UnLock(child);
    UnLock(parent);
    if(linked&&!DeleteFile((CONST_STRPTR)alias)&&!error)error=EIO;
    if(relative_linked&&!DeleteFile((CONST_STRPTR)relative)&&!error)error=EIO;
    if(cycle_linked&&!DeleteFile((CONST_STRPTR)cycle)&&!error)error=EIO;
    if(sub_created&&!DeleteFile((CONST_STRPTR)sub)&&!error)error=EIO;
    if(!DeleteFile((CONST_STRPTR)name)&&!error)error=EIO;
    if(!DeleteFile((CONST_STRPTR)dir)&&!error)error=EIO;
    char line[128];snprintf(line,sizeof(line),"Native stream directory search authorization result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}

static int native_created_output_checks(const struct oz_packet_security *caller)
{
    const char *dir="RAM:OpenZFS-native-create-output-test";
    const char *path="RAM:OpenZFS-native-create-output-test/result";
    BPTR lock=Lock((CONST_STRPTR)dir,ACCESS_READ),file=0;
    if(lock){UnLock(lock);return EEXIST;}
    lock=CreateDir((CONST_STRPTR)dir);if(!lock)return EIO;UnLock(lock);lock=0;
    int error=0;struct oz_stream_output *output=NULL;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    ULONG defaults=secBase?secGetDefProtection(NULL):0;
#define NEW_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    NEW_CHECK(secBase);
    NEW_CHECK(SetOwner((CONST_STRPTR)dir,(55UL<<16)|99));
    NEW_CHECK(SetProtection((CONST_STRPTR)dir,FIBF_GRP_EXECUTE));
    NEW_CHECK(oz_stream_output_begin(caller,path,&output)==EACCES&&!output);
    NEW_CHECK(SetProtection((CONST_STRPTR)dir,FIBF_GRP_EXECUTE|FIBF_GRP_WRITE));
    NEW_CHECK(!oz_stream_output_begin(caller,path,&output)&&output);
    NEW_CHECK(secGetDefProtection(NULL)==defaults);
    NEW_CHECK(!oz_stream_output_write(output,"created",7));
    lock=Lock((CONST_STRPTR)path,ACCESS_READ);NEW_CHECK(!lock&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    {int rc=oz_stream_output_finish(output,1);output=NULL;NEW_CHECK(!rc);}
    file=Open((CONST_STRPTR)path,MODE_OLDFILE);NEW_CHECK(file);
    char bytes[16];LONG n=Read(file,bytes,sizeof(bytes));NEW_CHECK(n==7&&!memcmp(bytes,"created",7));
    {LONG rc=Close(file);file=0;NEW_CHECK(rc);}
    lock=Lock((CONST_STRPTR)path,ACCESS_READ);NEW_CHECK(lock);
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);NEW_CHECK(fib);
    int metadata=Examine(lock,fib)&&fib->fib_OwnerUID==77&&fib->fib_OwnerGID==88&&
        fib->fib_Protection==(FIBF_DELETE|FIBF_GRP_READ);
    FreeDosObject(DOS_FIB,fib);UnLock(lock);lock=0;NEW_CHECK(metadata);
    NEW_CHECK(!oz_stream_output_begin(caller,path,&output));
    NEW_CHECK(!oz_stream_output_write(output,"collision",9));
    {int rc=oz_stream_output_finish(output,1);output=NULL;NEW_CHECK(rc==EEXIST);}
    file=Open((CONST_STRPTR)path,MODE_OLDFILE);NEW_CHECK(file);
    n=Read(file,bytes,sizeof(bytes));NEW_CHECK(n==7&&!memcmp(bytes,"created",7));
    {LONG rc=Close(file);file=0;NEW_CHECK(rc);}
    NEW_CHECK(SetProtection((CONST_STRPTR)path,0));
    NEW_CHECK(DeleteFile((CONST_STRPTR)path));
    NEW_CHECK(!oz_stream_output_begin(caller,path,&output));
    NEW_CHECK(!oz_stream_output_write(output,"aborted",7));
    {int rc=oz_stream_output_finish(output,0);output=NULL;NEW_CHECK(!rc);}
    lock=Lock((CONST_STRPTR)path,ACCESS_READ);NEW_CHECK(!lock&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    NEW_CHECK(!oz_stream_output_begin(caller,path,&output));
    NEW_CHECK(!oz_stream_output_write(output,"revoked",7));
    NEW_CHECK(SetProtection((CONST_STRPTR)dir,FIBF_GRP_EXECUTE));
    {int rc=oz_stream_output_finish(output,1);output=NULL;NEW_CHECK(rc==EACCES);}
    lock=Lock((CONST_STRPTR)path,ACCESS_READ);NEW_CHECK(!lock&&IoErr()==ERROR_OBJECT_NOT_FOUND);
done:
    if(output)oz_stream_output_finish(output,0);
    if(file)Close(file);
    if(lock)UnLock(lock);
    SetProtection((CONST_STRPTR)path,0);DeleteFile((CONST_STRPTR)path);
    if(!DeleteFile((CONST_STRPTR)dir)&&!error)error=EIO;
    if(secBase){if(secGetDefProtection(NULL)!=defaults&&!error)error=EIO;CloseLibrary(secBase);}
    char line[128];snprintf(line,sizeof(line),"Native staged stream output creation result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef NEW_CHECK
}

static int native_empty_output_checks(const struct oz_packet_security *caller)
{
    const char *name="RAM:OpenZFS-native-empty-output-test";
    BPTR lock=Lock((CONST_STRPTR)name,ACCESS_READ),other=0,handle=0;
    if(lock){UnLock(lock);return EEXIST;}
    handle=Open((CONST_STRPTR)name,MODE_NEWFILE);if(!handle)return EIO;
    if(!Close(handle))return EIO;
    handle=0;int error=0;struct oz_stream_file *stream=NULL;
#define OUT_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    OUT_CHECK(SetOwner((CONST_STRPTR)name,(55UL<<16)|99));
    OUT_CHECK(SetProtection((CONST_STRPTR)name,FIBF_GRP_READ));
    lock=Lock((CONST_STRPTR)name,ACCESS_READ);OUT_CHECK(lock);
    OUT_CHECK(oz_stream_file_open_empty_output(caller,&lock,&stream)==EACCES&&lock&&!stream);
    OUT_CHECK(SetProtection((CONST_STRPTR)name,FIBF_GRP_WRITE));
    other=DupLock(lock);OUT_CHECK(other);
    OUT_CHECK(oz_stream_file_open_empty_output(caller,&lock,&stream)==EBUSY&&lock&&!stream);
    UnLock(other);other=0;
    OUT_CHECK(!oz_stream_file_open_empty_output(caller,&lock,&stream)&&!lock&&stream);
    other=Lock((CONST_STRPTR)name,ACCESS_READ);OUT_CHECK(!other&&IoErr()==ERROR_OBJECT_IN_USE);
    size_t count=0;
    OUT_CHECK(!oz_stream_file_write(stream,"sent",4,&count)&&count==4);
    {int rc=oz_stream_file_close(stream);stream=NULL;OUT_CHECK(!rc);}
    lock=Lock((CONST_STRPTR)name,ACCESS_READ);OUT_CHECK(lock);
    OUT_CHECK(oz_stream_file_open_empty_output(caller,&lock,&stream)==EEXIST&&!lock&&!stream);
    handle=Open((CONST_STRPTR)name,MODE_OLDFILE);OUT_CHECK(handle);
    char bytes[8];LONG n=Read(handle,bytes,sizeof(bytes));
    OUT_CHECK(n==4&&!memcmp(bytes,"sent",4));
done:
    if(stream)oz_stream_file_close(stream);
    if(handle)Close(handle);
    if(other)UnLock(other);
    if(lock)UnLock(lock);
    if(!DeleteFile((CONST_STRPTR)name)&&!error)error=EIO;
    char line[128];snprintf(line,sizeof(line),"Native exclusive empty stream output result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef OUT_CHECK
}

static int native_stream_file_checks(const struct oz_packet_security *caller)
{
    const char *name="RAM:OpenZFS-native-stream-authority-test";
    BPTR lock=Lock((CONST_STRPTR)name,ACCESS_READ),file=0;
    if(lock){UnLock(lock);return EEXIST;}
    file=Open((CONST_STRPTR)name,MODE_NEWFILE);if(!file)return EIO;
    int error=0;
    LONG written=Write(file,"stream",6);
    LONG closed=Close(file);
    if(written!=6||!closed){DeleteFile((CONST_STRPTR)name);return EIO;}
    if(!SetOwner((CONST_STRPTR)name,(55UL<<16)|99)||
        !SetProtection((CONST_STRPTR)name,FIBF_GRP_READ))error=EIO;
    if(!error){lock=Lock((CONST_STRPTR)name,ACCESS_READ);if(!lock)error=EIO;}
    if(!error&&oz_stream_lock_access(caller,lock,secAt_Read))error=EIO;
    if(!error&&oz_stream_lock_access(caller,lock,secAt_Write)!=EACCES)error=EIO;
    if(!error&&oz_stream_lock_access(NULL,lock,secAt_Read)!=EACCES)error=EIO;
    if(!error&&oz_stream_lock_access(caller,0,secAt_Read)!=EINVAL)error=EIO;
    if(!error&&oz_stream_lock_access(caller,lock,secAt_Execute)!=EINVAL)error=EIO;
    if(!error&&oz_stream_lock_access(caller,lock,0)!=EINVAL)error=EIO;
    if(!error){
        BPTR transfer=DupLock(lock);struct oz_stream_file *stream=NULL;
        char bytes[8]={0};size_t count=0;
        if(!transfer)error=EIO;
        if(!error&&(oz_stream_file_open(caller,&transfer,secAt_Write,&stream)!=EACCES||
            !transfer||stream))error=EIO;
        if(!error)error=oz_stream_file_open(caller,&transfer,secAt_Read,&stream);
        if(!error&&(transfer||!stream))error=EIO;
        if(!error)error=oz_stream_file_read(stream,bytes,sizeof(bytes),&count);
        if(!error&&(count!=6||memcmp(bytes,"stream",6)))error=EIO;
        if(!error&&(oz_stream_file_write(stream,"blocked",7,&count)!=EACCES||count))error=EIO;
        if(!error)error=oz_stream_file_read(stream,bytes,sizeof(bytes),&count);
        if(!error&&count)error=EIO; /* EOF, not a transport failure. */
        if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
        if(transfer)UnLock(transfer);
    }
    if(!error&&!SetProtection((CONST_STRPTR)name,0))error=EIO;
    if(!error&&oz_stream_lock_access(caller,lock,secAt_Read)!=EACCES)error=EIO;
    if(!error&&!SetProtection((CONST_STRPTR)name,FIBF_GRP_WRITE))error=EIO;
    if(!error){
        BPTR transfer=DupLock(lock);struct oz_stream_file *stream=NULL;
        char bytes[8];size_t count=0;
        if(!transfer)error=EIO;
        if(!error)error=oz_stream_file_open(caller,&transfer,secAt_Write,&stream);
        if(!error&&(transfer||!stream))error=EIO;
        if(!error&&(oz_stream_file_read(stream,bytes,sizeof(bytes),&count)!=EACCES||count))error=EIO;
        if(!error)error=oz_stream_file_write(stream,"output",6,&count);
        if(!error&&count!=6)error=EIO;
        if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
        if(transfer)UnLock(transfer);
    }
    if(lock)UnLock(lock);
    if(!DeleteFile((CONST_STRPTR)name)&&!error)error=EIO;
    if(!error){
        lock=Lock((CONST_STRPTR)"RAM:",ACCESS_READ);
        if(!lock)error=EIO;
        else {
            if(oz_stream_lock_access(caller,lock,secAt_Read)!=ENOTSUP)error=EIO;
            UnLock(lock);
        }
    }
    /* A trusted internal lock targeting this task must be rejected before
     * Examine sends a synchronous DOS packet back to the service itself. */
    if(!error){
        struct MsgPort self={0};struct FileLock self_lock={0};
        self.mp_SigTask=FindTask(NULL);self_lock.fl_Task=&self;
        if(oz_stream_lock_access(caller,MKBADDR(&self_lock),secAt_Read)!=EDEADLK)error=EIO;
    }
    char line[128];snprintf(line,sizeof(line),"Native locked stream file caller access result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    if(!error)error=native_empty_output_checks(caller);
    return error?error:native_created_output_checks(caller);
}

/* Exercise actual security.library semantics, not a POSIX mode approximation. */
static int native_policy_checks(const struct oz_packet_security *context)
{
    const uint32_t mine=(77UL<<16)|88, other=(55UL<<16)|66;
    uint32_t reason=0;
    const uint32_t deny=FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE;
    if (oz_packet_security_access(context,mine,0,secAt_Read|secAt_Write,&reason) || reason ||
        oz_packet_security_access(context,mine,deny,secAt_Read,&reason)!=EACCES ||
        !(reason & secAC_OWNER_DENIED) ||
        oz_packet_security_access(context,other,0,secAt_Read,NULL)!=EACCES ||
        oz_packet_security_access(context,other,FIBF_OTR_READ,secAt_Read,NULL) ||
        oz_packet_security_access(context,(55UL<<16)|99,FIBF_GRP_READ,secAt_Read,NULL) ||
        oz_packet_security_access(context,(55UL<<16)|99,FIBF_GRP_READ,
            secAt_Read|secAt_Write,NULL)!=EACCES ||
        oz_packet_security_access(context,mine,0,0,NULL)!=EINVAL ||
        oz_packet_security_access(context,mine,0,16,NULL)!=EINVAL ||
        oz_packet_security_property(context,mine) ||
        oz_packet_security_set_owner(context,mine,mine) ||
        oz_packet_security_set_owner(context,mine,(77UL<<16)|99) ||
        oz_packet_security_set_owner(context,mine,other)!=EPERM ||
        oz_packet_security_set_owner(context,other,mine)!=EPERM ||
        oz_packet_security_property(context,0) ||
        oz_packet_security_property(context,other)!=EPERM) return EIO;
    return 0;
}

static void *sender_task(void *arg)
{
    struct packet_sender *s=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    struct MsgPort *reply=CreateMsgPort();
    ULONG previous=secBase?secGetTaskOwner(FindTask(NULL)):0;
    ULONG logged=0;
    struct oz_message *request=s->dataset?calloc(1,sizeof(*request)):NULL;
    if(s->dataset&&!request)s->error=ENOMEM;
    if(!secBase||!reply)s->error=ENOMEM;
    struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(!s->error) {
        logged=secLoginA(login);
        if(logged!=((77UL<<16)|88))s->error=EACCES;
        struct TagItem defaults[]={{secT_DefProtection,FIBF_DELETE|FIBF_GRP_READ},
            {TAG_DONE,0}};
        if(!s->error && !secSetDefProtectionA(defaults))s->error=EIO;
    }
    if(s->error)PutMsg(s->destination,&s->failed);
    else {
        struct StandardPacket packet={0};
        packet.sp_Msg.mn_Node.ln_Name=(void *)&packet.sp_Pkt;
        packet.sp_Msg.mn_Length=sizeof(packet);
        packet.sp_Pkt.dp_Link=&packet.sp_Msg;
        packet.sp_Pkt.dp_Type=ACTION_IS_FILESYSTEM;
        if(request) {
            request->magic=OZ_MAGIC;request->version=OZ_VERSION;
            request->msg.mn_Length=sizeof(*request);
            request->operation=37;request->flags=OZ_CONFIRMED;
            snprintf(request->target,sizeof(request->target),"%s",s->dataset);
            packet.sp_Pkt.dp_Type=OZ_ADMISSION_ACTION;
            packet.sp_Pkt.dp_Arg1=(IPTR)request;
            packet.sp_Pkt.dp_Arg2=sizeof(*request);
        }
        SendPkt(&packet.sp_Pkt,s->destination,reply);
        WaitPort(reply);
        if(GetMsg(reply)!=&packet.sp_Msg || packet.sp_Pkt.dp_Res1!=DOSTRUE)
            s->error=EIO;
    }
    /* Admission must own a copy, not this payload or the sender's storage. */
    if(request){memset(request,0xa5,sizeof(*request));free(request);}
    if(logged) {
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous)s->error=EIO;
    }
    if(reply)DeleteMsgPort(reply);
    if(secBase)CloseLibrary(secBase);
    return NULL;
}

static int cross_task_capture(struct Library *secBase, const char *dataset,
    int expected_create,int (*work)(const struct oz_packet_security *,void *),
    void *arg)
{
    ULONG previous=secGetTaskOwner(FindTask(NULL));
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    ULONG logged=secLoginA(login);
    if(!logged)return EACCES;
    int error=logged==secOWNER_SYSTEM?0:EACCES;
    struct MsgPort *port=CreateMsgPort();
    if(!port)error=ENOMEM;
    struct packet_sender sender={.destination=port,.dataset=dataset};
    struct oz_admitted *admitted=NULL;
    struct oz_admitted *cancel_owner=NULL,*cancel_admin=NULL;
    cred_t *captured=NULL;
    struct oz_packet_security *context=NULL;
    pthread_t thread;
    if(!error)error=pthread_create(&thread,NULL,sender_task,&sender);
    if(!error) {
        WaitPort(port);
        struct Message *message=GetMsg(port);
        /* Read the worker's final error only after join: its logout cleanup
         * may still be updating it after the failure notification. */
        if(message==&sender.failed)error=EIO;
        else {
            struct DosPacket *packet=(void *)message->mn_Node.ln_Name;
            error=oz_packet_security_begin(packet,port,&context);
            if(!error) {
                captured=oz_packet_security_cred(context);
                oz_cred_hold(captured);
                uint32_t defaults=0;
                error=oz_packet_security_defaults(context,&defaults);
                if(!error && (defaults!=(FIBF_DELETE|FIBF_GRP_READ) ||
                    defaults!=(uint32_t)secGetPktDefProtection(packet)))error=EIO;
                if(!error)error=native_policy_checks(context);
            }
            if(!error && (crgetuid(captured)!=77 || crgetgid(captured)!=88 ||
                !groupmember(99,captured) || secpolicy_zfs(captured)!=EPERM ||
                secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM))error=EIO;
            if(!error&&dataset)error=oz_admitted_capture(packet,port,&admitted);
            if(!error&&dataset)error=oz_admitted_capture(packet,port,&cancel_owner);
            if(!error&&dataset)error=oz_admitted_capture(packet,port,&cancel_admin);
            ReplyPkt(packet,DOSTRUE,0);
        }
        int joined=pthread_join(thread,NULL);
        if(!error)error=joined?joined:sender.error;
        /* Snapshot remains valid after sender logout, port deletion and exit. */
        if(!error && (!captured || crgetuid(captured)!=77 ||
            !groupmember(99,captured)))error=EIO;
        if(!error)error=native_policy_checks(context);
    }
    if(!error&&work)error=work(context,arg);
    oz_packet_security_end(context);
    context=NULL;
    /* The admitted operation owns an independent native security snapshot,
     * not the original context, packet or sender task. */
    if(!error&&dataset) {
        const struct oz_packet_security *retained=oz_admitted_security(admitted);
        error=native_policy_checks(retained);
        if(!error)error=native_stream_file_checks(retained);
        if(!error)error=native_stream_traversal_checks(retained);
        if(!error&&oz_packet_security_access_on(retained,NULL,0,0,secAt_Read,NULL)!=EINVAL)error=EIO;
        if(!error&&oz_packet_security_access_on(retained,port,(55UL<<16)|99,
            FIBF_GRP_READ,secAt_Read,NULL))error=EIO;
        if(!error&&oz_packet_security_access_on(retained,port,(55UL<<16)|99,
            FIBF_GRP_READ,secAt_Write,NULL)!=EACCES)error=EIO;
    }
    if(!error && (!captured || crgetuid(captured)!=77 || crgetgid(captured)!=88 ||
        !groupmember(99,captured) || secpolicy_zfs(captured)!=EPERM))error=EIO;
    /* Execute only after sender logout/exit and context destruction. The
     * service task is root here, but the operation must use the reader. */
    if(!error && dataset) {
        struct oz_message *result=calloc(1,sizeof(*result));
        cred_t *other=oz_cred_create(78,78,88,0,NULL);
        if(!result||!other)error=ENOMEM;
        if(CRED()!=NULL)error=EIO;
        if(!error&&oz_admitted_result(admitted,captured,result)!=EAGAIN)error=EIO;
        if(!error&&(oz_admitted_cancel(NULL,captured)!=EINVAL ||
            oz_admitted_cancel(cancel_owner,NULL)!=EACCES ||
            oz_admitted_cancel(cancel_owner,other)!=EACCES ||
            oz_admitted_result(cancel_owner,captured,result)!=EAGAIN ||
            oz_admitted_cancel(cancel_owner,captured) ||
            oz_admitted_cancel(cancel_owner,captured)!=EALREADY ||
            oz_admitted_execute(cancel_owner)!=EALREADY ||
            oz_admitted_result(cancel_owner,other,result)!=EACCES ||
            oz_admitted_result(cancel_owner,captured,result) ||
            result->status!=OZ_CANCELLED || strcmp(result->target,dataset) ||
            oz_admitted_cancel(cancel_admin,kcred) ||
            oz_admitted_execute(cancel_admin)!=EALREADY ||
            oz_admitted_result(cancel_admin,captured,result) ||
            result->status!=OZ_CANCELLED))error=EIO;
        if(!error){
            int actual=oz_admitted_execute(admitted);
            if(actual!=expected_create){
                char line[160];snprintf(line,sizeof(line),
                    "Native retained create mismatch dataset=%s actual=%d expected=%d\n",
                    dataset,actual,expected_create);
                for(const char *p=line;*p;p++)RawPutChar(*p);
                error=EIO;
            }
        }
        if(!error&&(oz_admitted_execute(admitted)!=EALREADY ||
            oz_admitted_cancel(admitted,captured)!=EALREADY ||
            oz_admitted_result(admitted,NULL,result)!=EACCES ||
            oz_admitted_result(admitted,other,result)!=EACCES ||
            oz_admitted_result(admitted,captured,result) ||
            strcmp(result->target,dataset) ||
            result->status!=(expected_create?OZ_ENGINE_ERROR:OZ_OK) ||
            result->msg.mn_ReplyPort || result->reply_name[0] ||
            oz_admitted_result(admitted,kcred,result)))error=EIO;
        if(CRED()!=NULL)error=EIO;
        crfree(other);free(result);
    }
    oz_admitted_free(admitted);
    oz_admitted_free(cancel_owner);
    oz_admitted_free(cancel_admin);
    crfree(captured);
    if(port)DeleteMsgPort(port);
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    if(secLogoutA(logout)!=previous || secGetTaskOwner(FindTask(NULL))!=previous)error=EIO;
    char diagnostic[100];snprintf(diagnostic,sizeof(diagnostic),
        "Native cross-task DOS sender identity/lifetime=%d\n",error);
    for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    snprintf(diagnostic,sizeof(diagnostic),
        "Native packet security access/property/defaults=%d\n",error);
    for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    snprintf(diagnostic,sizeof(diagnostic),
        "Native retained credential after packet context destruction=%d\n",error);
    for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    return error;
}

int oz_packet_delegated_create_test(const char *dataset, int expected)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOTSUP;
    int error=cross_task_capture(secBase,dataset,expected,NULL,NULL);
    CloseLibrary(secBase);
    char line[100];snprintf(line,sizeof(line),
        "Native MuFS packet delegated create expected=%d result=%d\n",expected,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}

/* Private test callback runs after the native reader sender has exited. */
int oz_packet_reader_work_test(int (*work)(const struct oz_packet_security *,void *),void *arg)
{
    if(!work)return EINVAL;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOTSUP;
    int error=cross_task_capture(secBase,NULL,0,work,arg);
    CloseLibrary(secBase);return error;
}

static int authenticated_fixture(struct Library *secBase, struct DosPacket *packet)
{
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(!marker)return 0;
    fclose(marker);
    if(!secBase || !secIsConfigured())return EACCES;
    const char *names[]={"oztestroot","ozreader"};
    const ULONG owners[]={0xffffffffUL,(77UL<<16)|88};
    int error=0;
    for(unsigned i=0;i<2&&!error;i++) {
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)names[i]},
            {secT_Password,(IPTR)""},{TAG_DONE,0}};
        ULONG owner=secLoginA(login);
        if(!owner) { error=EACCES; break; }
        if(owner!=owners[i])error=EACCES;
        cred_t *cr=NULL;
        if(!error)error=oz_cred_from_packet(packet,&cr);
        if(!error && (crgetuid(cr)!=(i?77:0) || crgetgid(cr)!=(i?88:0) ||
            secpolicy_zfs(cr)!=(i?EPERM:0)))error=EIO;
        if(!error && i && (!groupmember(99,cr)||groupmember(0,cr)))error=EIO;
        crfree(cr);
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        ULONG restored=secLogoutA(logout);
        if(restored!=previous || secGetTaskOwner(FindTask(NULL))!=previous)error=EIO;
    }
    char diagnostic[100];snprintf(diagnostic,sizeof(diagnostic),
        "Native authenticated packet root/reader/group capture=%d\n",error);
    for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    if(!error)error=cross_task_capture(secBase,NULL,0,NULL,NULL);
    return error;
}

int oz_packet_cred_test(void)
{
    uint32_t packed=0;
    if (oz_aros_owner_from_ids(0,0,&packed) || packed!=secOWNER_SYSTEM ||
        oz_aros_owner_from_ids(77,88,&packed) || packed!=((77UL<<16)|88) ||
        oz_aros_owner_from_ids(65534,65535,&packed) || packed!=(65534UL<<16) ||
        oz_aros_owner_from_ids(65535,0,&packed) || packed!=secROOT_GID ||
        oz_aros_owner_from_ids(65536,0,&packed)!=EOVERFLOW || packed ||
        oz_aros_owner_from_ids(0,65536,&packed)!=EOVERFLOW || packed ||
        oz_aros_owner_from_ids((1ULL<<32)|77,88,&packed)!=EOVERFLOW || packed ||
        oz_aros_owner_from_ids(UINT32_MAX,88,&packed)!=EOVERFLOW || packed ||
        oz_aros_owner_from_ids(0,0,NULL)!=EINVAL) return EIO;
    const uint32_t native_ids[]={0,1,77,65534,65535};
    for(unsigned i=0;i<5;i++)for(unsigned j=0;j<5;j++) {
        uint32_t owner=(native_ids[i]<<16)|native_ids[j],uid,gid;
        if(oz_aros_ids_from_owner(owner,&uid,&gid)||
            oz_aros_owner_from_ids(uid,gid,&packed)||packed!=owner)return EIO;
    }
    struct { struct secExtOwner owner; UWORD groups[4]; } fixture = {
        {77, secNOBODY_GID, 4}, {88, secROOT_GID, secNOBODY_GID, 65534}
    };
    _Static_assert(offsetof(__typeof__(fixture), groups) == sizeof(struct secExtOwner),
        "native supplementary groups layout");
    cred_t *cr = NULL;
    int error = oz_cred_from_aros_owner(&fixture.owner, &cr);
    if (error) return error;
    fixture.groups[0] = 99;
    if (crgetuid(cr) != 77 || crgetruid(cr) != 77 || crgetgid(cr) != 65535 ||
        crgetngroups(cr) != 4 || !groupmember(88, cr) || groupmember(99, cr) ||
        !groupmember(0, cr) || !groupmember(65534, cr) || !groupmember(65535, cr) ||
        secpolicy_zfs(cr) != EPERM) error = EIO;
    crfree(cr);
    fixture.owner = (struct secExtOwner){secROOT_UID, secROOT_GID, 0};
    if (!error) error = oz_cred_from_aros_owner(&fixture.owner, &cr);
    if (!error) {
        if (crgetuid(cr) || crgetruid(cr) || crgetgid(cr) || secpolicy_zfs(cr)) error = EIO;
        crfree(cr);
    }
    /* Distinct ordinary high ID must not collide with root or nobody group. */
    fixture.owner = (struct secExtOwner){65534, 65534, 0};
    if (!error) error = oz_cred_from_aros_owner(&fixture.owner, &cr);
    if (!error) {
        if (crgetuid(cr) != 65534 || crgetgid(cr) != 65534 ||
            groupmember(65535, cr) || secpolicy_zfs(cr) != EPERM) error = EIO;
        crfree(cr);
    }
    fixture.owner.uid = secNOBODY_UID;
    cr = (void *)1;
    if (oz_cred_from_aros_owner(&fixture.owner, &cr) != EACCES || cr ||
        oz_cred_from_aros_owner(NULL, &cr) != EACCES || cr ||
        oz_cred_from_aros_owner(&fixture.owner, NULL) != EINVAL) error = EIO;
    struct DosPacket packet = {0};
    struct oz_packet_security *context=(void *)1;
    uint32_t defaults=123, reason=0;
    if (oz_packet_security_begin(NULL,NULL,&context)!=EINVAL || context ||
        oz_packet_security_defaults(NULL,&defaults)!=EACCES || defaults ||
        oz_packet_security_access(NULL,0,0,secAt_Read,&reason)!=EACCES ||
        !(reason & secAC_PERMISSION_DENIED) ||
        oz_packet_security_property(NULL,0)!=EACCES ||
        oz_packet_security_cred(NULL))error=EIO;
    cr = (void *)1;
    if (oz_cred_from_packet(&packet, &cr) != EACCES || cr ||
        oz_cred_from_packet(NULL, &cr) != EINVAL || cr) error = EIO;
    struct MsgPort *port = CreateMsgPort();
    if (!port) return error ? error : ENOMEM;
    packet.dp_Port = port;
    struct Library *secBase = OpenLibrary((CONST_STRPTR)SECURITYNAME, SECURITYVERSION);
    cred_t *expected = NULL;
    int expected_error = ENOTSUP;
    if (secBase) {
        expected_error = EACCES;
        if (secIsConfigured()) {
            struct secExtOwner *owner = secGetPktOwner(&packet);
            expected_error = oz_cred_from_aros_owner(owner, &expected);
            if (owner) secFreeExtOwner(owner);
        }
    }
    int captured = oz_cred_from_packet(&packet, &cr);
    if (captured != expected_error) error = EIO;
    if (!captured) {
        if (!cr || !expected || crgetuid(cr) != crgetuid(expected) ||
            crgetgid(cr) != crgetgid(expected) || crgetngroups(cr) != crgetngroups(expected)) error = EIO;
        else for (int i = 0; i < crgetngroups(cr); i++)
            if (crgetgroups(cr)[i] != crgetgroups(expected)[i]) error = EIO;
    } else if (cr) error = EIO;
    char diagnostic[120];
    snprintf(diagnostic,sizeof(diagnostic),
        "Native packet identity capture errno=%d expected=%d (0 means live identity)\n",
        captured,expected_error);
    for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    crfree(cr); crfree(expected);
    if(!error)error=authenticated_fixture(secBase,&packet);
    if(secBase)CloseLibrary(secBase);
    DeleteMsgPort(port);
    return error;
}
