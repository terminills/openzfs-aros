/* SPDX-License-Identifier: CDDL-1.0 */
/* Bounded client acceptance against the reusable real DOS frontend. */
#include <sys/zfs_context.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/exall.h>
#include <pthread.h>
#include <stdint.h>
#include <stddef.h>
#include <string.h>
#include <errno.h>
#include <stdio.h>
#include <sys/cred.h>
#include <sys/acl.h>
#include <libraries/security.h>
#include <proto/security.h>
#include "zpl-fixture.h"
#include "dos-handler.h"
struct client {struct MsgPort *port;struct Message done;int result,stage,busy_check,unmount_result,secure;};
struct startup_reader {
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    int ready,release;
};
static void *startup_reader_hold(void *arg)
{
    struct startup_reader *r=arg;
    LockDosList(LDF_ALL|LDF_READ);
    pthread_mutex_lock(&r->mutex);
    r->ready=1;pthread_cond_signal(&r->condition);
    while(!r->release)pthread_cond_wait(&r->condition,&r->mutex);
    pthread_mutex_unlock(&r->mutex);
    UnLockDosList(LDF_ALL|LDF_READ);
    return NULL;
}
static int startup_adopt_with_reader(struct oz_zpl *fs,struct DosList *entry,struct oz_mount **out)
{
    struct startup_reader r={.mutex=PTHREAD_MUTEX_INITIALIZER,.condition=PTHREAD_COND_INITIALIZER};
    pthread_t thread;
    int error=pthread_create(&thread,NULL,startup_reader_hold,&r);
    if(!error) {
        pthread_mutex_lock(&r.mutex);
        while(!r.ready)pthread_cond_wait(&r.condition,&r.mutex);
        pthread_mutex_unlock(&r.mutex);
        error=oz_dos_mount_adopt(fs,"OZTEST",entry,out,1,-1);
        pthread_mutex_lock(&r.mutex);
        r.release=1;pthread_cond_signal(&r.condition);
        pthread_mutex_unlock(&r.mutex);
        pthread_join(thread,NULL);
    }
    pthread_cond_destroy(&r.condition);pthread_mutex_destroy(&r.mutex);
    return error;
}
static int check_busy(struct client *c)
{
    struct MsgPort *reply=CreateMsgPort();if(!reply)return ENOMEM;
    c->done.mn_ReplyPort=reply;c->busy_check=1;
    PutMsg(c->port,&c->done);WaitPort(reply);GetMsg(reply);
    c->done.mn_ReplyPort=NULL;c->busy_check=0;DeleteMsgPort(reply);
    return c->unmount_result==EBUSY?0:EIO;
}
static int exclusive_lock_checks(void)
{
    const char *name="OZTEST:hello.txt";
    BPTR shared=Lock((CONST_STRPTR)name,SHARED_LOCK),lock=0,other=0;
    int error=shared?0:EIO;
    if(!error) {
        lock=Lock((CONST_STRPTR)name,EXCLUSIVE_LOCK);
        if(lock||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(lock)UnLock(lock);
    }
    if(shared)UnLock(shared);
    BPTR file=Open((CONST_STRPTR)name,MODE_OLDFILE);
    if(!file)error=EIO;
    if(!error) {
        lock=Lock((CONST_STRPTR)name,EXCLUSIVE_LOCK);
        if(lock||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(lock)UnLock(lock);
    }
    if(file)Close(file);
    lock=0;
    if(!error)lock=Lock((CONST_STRPTR)name,EXCLUSIVE_LOCK);
    if(!lock)error=EIO;
    if(lock) {
        struct FileInfoBlock fib;
        if(((struct FileLock *)BADDR(lock))->fl_Access!=EXCLUSIVE_LOCK||
            !Examine(lock,&fib)||strcmp((char *)fib.fib_FileName,"hello.txt"))error=EIO;
        const LONG modes[]={SHARED_LOCK,EXCLUSIVE_LOCK};
        for(unsigned i=0;i<2&&!error;i++) {
            other=Lock((CONST_STRPTR)name,modes[i]);
            if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
            if(other)UnLock(other);
        }
        other=DupLock(lock);
        if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(other)UnLock(other);
        file=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(file||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(file)Close(file);
        UnLock(lock);
    }
    if(!error) {
        file=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(!file)error=EIO;
        if(file)Close(file);
    }
    /* ParentDir must not manufacture a shared reference to an exclusively
     * locked directory. A lock does not recursively lock its descendants. */
    lock=Lock((CONST_STRPTR)"OZTEST:subdir",EXCLUSIVE_LOCK);
    other=Lock((CONST_STRPTR)"OZTEST:subdir/nested.txt",SHARED_LOCK);
    if(!lock||!other)error=EIO;
    if(lock&&other) {
        BPTR parent=ParentDir(other);
        if(parent||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(parent)UnLock(parent);
    }
    if(other)UnLock(other);
    if(lock)UnLock(lock);
    char message[96];snprintf(message,sizeof(message),"Native DOS exclusive lock conflicts=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
struct exall_hook_state {unsigned calls,seen;int error,mode;};
AROS_UFH3(static IPTR, exall_match_hook,
    AROS_UFHA(struct Hook *,hook,A0),
    AROS_UFHA(struct ExAllData *,entry,A2),
    AROS_UFHA(LONG *,level,A1))
{
    AROS_USERFUNC_INIT
    struct exall_hook_state *state=hook->h_Data;
    state->calls++;
    if(!level||*level!=ED_OWNER||!entry||!entry->ed_Name) {
        state->error=EIO;return FALSE;
    }
    unsigned bit=!strcmp((char *)entry->ed_Name,"hello.txt")?1:
        !strcmp((char *)entry->ed_Name,"subdir")?2:0;
    if(!bit||(state->seen&bit)||entry->ed_OwnerUID!=secROOT_UID||
        entry->ed_OwnerGID!=secROOT_GID||!entry->ed_Comment||*entry->ed_Comment)
        state->error=EIO;
    state->seen|=bit;
    return state->mode==2||(state->mode==0&&bit==1);
    AROS_USERFUNC_EXIT
}
static int exall_hook_checks(BPTR root)
{
    int error=0;
    for(int mode=0;mode<3&&!error;mode++) {
        struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
        union {IPTR alignment;unsigned char bytes[512];} storage;
        struct exall_hook_state state={.mode=mode};
        /* AROS stores an erased entry pointer; CALLHOOKPKT restores the
         * register-aware three-argument ABI used by AROS_UFH3 above. */
        struct Hook hook={.h_Entry=(HOOKFUNC)(void (*)(void))exall_match_hook,.h_Data=&state};
        if(!ctl)return ENOMEM;
        ctl->eac_MatchFunc=&hook;
        unsigned count=0,calls=0;
        for(;;) {
            size_t capacity=sizeof(struct ExAllData)+12;
            memset(storage.bytes,0xa5,sizeof(storage.bytes));
            BOOL more=ExAll(root,(void *)storage.bytes,capacity,ED_OWNER,ctl);
            LONG why=IoErr();count+=ctl->eac_Entries;
            for(size_t i=capacity;i<sizeof(storage.bytes);i++)
                if(storage.bytes[i]!=0xa5)error=EIO;
            if(ctl->eac_Entries>1||++calls>4)error=EIO;
            if(ctl->eac_Entries) {
                struct ExAllData *entry=(void *)storage.bytes;
                if(entry->ed_Next||(mode==0&&strcmp((char *)entry->ed_Name,"hello.txt")))error=EIO;
            }
            if(error||!more){if(!error&&why!=ERROR_NO_MORE_ENTRIES)error=EIO;break;}
        }
        if(state.error||state.calls!=2||state.seen!=3||count!=(mode==0?1:mode==1?0:2))error=EIO;
        ExAllEnd(root,(void *)storage.bytes,sizeof(storage.bytes),ED_OWNER,ctl);
        if(ctl->eac_LastKey||ctl->eac_Entries)error=EIO;
        FreeDosObject(DOS_EXALLCONTROL,ctl);
    }
    struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
    if(!ctl)return ENOMEM;
    union {IPTR alignment;unsigned char bytes[512];} storage;
    struct FileLock *fl=BADDR(root);
    for(unsigned test=0;test<7&&!error;test++) {
        SIPTR size=sizeof(storage.bytes),level=ED_OWNER;
        IPTR buffer=(IPTR)storage.bytes,control=(IPTR)ctl;
        if(test==0)level=0;
        if(test==1)level=-1;
        if(test==2)level=ED_OWNER+1;
        if(test==3)size=-1;
        if(test==4)buffer++;
        if(test==5)buffer=0;
        if(test==6)control=0;
        ctl->eac_Entries=123;
        if(DoPkt(fl->fl_Task,ACTION_EXAMINE_ALL,(SIPTR)root,buffer,size,level,control)||
            IoErr()!=ERROR_BAD_NUMBER||(control&&ctl->eac_Entries)||ctl->eac_LastKey)error=EIO;
    }
    FreeDosObject(DOS_EXALLCONTROL,ctl);
    char message[96];snprintf(message,sizeof(message),"Native DOS ExAll hooks/invalid packets=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
/* Public DOS enumeration acceptance, now dispatched to the native handler. */
static int exall_checks(void)
{
    BPTR root=Lock((CONST_STRPTR)"OZTEST:",SHARED_LOCK);
    int error=root?0:EIO;
    for(LONG level=ED_NAME;level<=ED_OWNER&&!error;level++) {
        for(unsigned small=0;small<2&&!error;small++) {
            struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
            union {IPTR alignment;unsigned char bytes[1024];} storage;
            if(!ctl){error=ENOMEM;break;}
            unsigned seen=0,calls=0;
            const size_t fields[]={0,offsetof(struct ExAllData,ed_Type),
                offsetof(struct ExAllData,ed_Size),offsetof(struct ExAllData,ed_Prot),
                offsetof(struct ExAllData,ed_Days),offsetof(struct ExAllData,ed_Comment),
                offsetof(struct ExAllData,ed_OwnerUID),sizeof(struct ExAllData)};
            LONG capacity=small?fields[level]+12:sizeof(storage.bytes);
            for(;;) {
                BOOL more=ExAll(root,(struct ExAllData *)storage.bytes,capacity,level,ctl);
                LONG why=IoErr();unsigned count=0;
                struct ExAllData *entry=(struct ExAllData *)storage.bytes;
                while(count<ctl->eac_Entries&&!error) {
                    if(!entry||++count>2){error=EIO;break;}
                    unsigned bit=!strcmp((char *)entry->ed_Name,"hello.txt")?1:
                        !strcmp((char *)entry->ed_Name,"subdir")?2:0;
                    if(!bit||(seen&bit)){error=EIO;break;}seen|=bit;
                    if(level>=ED_TYPE&&entry->ed_Type!=(bit==1?ST_FILE:ST_USERDIR))error=EIO;
                    if(level>=ED_SIZE&&bit==2&&entry->ed_Size)error=EIO;
                    if(level>=ED_PROTECTION&&!(entry->ed_Prot&FIBF_WRITE))error=EIO;
                    if(level>=ED_COMMENT&&(!entry->ed_Comment||*entry->ed_Comment))error=EIO;
                    if(level>=ED_OWNER&&(entry->ed_OwnerUID!=secROOT_UID||
                        entry->ed_OwnerGID!=secROOT_GID))error=EIO;
                    entry=entry->ed_Next;
                }
                if(count!=ctl->eac_Entries||(count&&entry)||++calls>4)error=EIO;
                if(error||!more) {
                    if(!error&&(why!=ERROR_NO_MORE_ENTRIES||seen!=3||
                        (small&&calls<2)))error=EIO;
                    break;
                }
            }
            FreeDosObject(DOS_EXALLCONTROL,ctl);
        }
    }
    if(!error) {
        struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
        union {IPTR alignment;unsigned char bytes[512];} storage;
        char pattern[64];
        if(!ctl)error=ENOMEM;
        else {
            if(ExAll(root,(struct ExAllData *)storage.bytes,sizeof(IPTR),ED_OWNER,ctl)||
                IoErr()!=ERROR_BUFFER_OVERFLOW||ctl->eac_Entries)error=EIO;
            if(ParsePatternNoCase((CONST_STRPTR)"HELLO.#?",(STRPTR)pattern,sizeof(pattern))<0)error=EIO;
            ctl->eac_MatchString=(UBYTE *)pattern;
            if(!error) {
                BOOL more=ExAll(root,(struct ExAllData *)storage.bytes,sizeof(storage.bytes),ED_OWNER,ctl);
                struct ExAllData *entry=(struct ExAllData *)storage.bytes;
                if(more||IoErr()!=ERROR_NO_MORE_ENTRIES||ctl->eac_Entries!=1||
                    strcmp((char *)entry->ed_Name,"hello.txt")||entry->ed_Next)error=EIO;
            }
            FreeDosObject(DOS_EXALLCONTROL,ctl);
        }
    }
    if(!error)error=exall_hook_checks(root);
    if(root)UnLock(root);
    char message[96];snprintf(message,sizeof(message),"Native DOS ExAll levels/continuation/filter=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int filehandle_lock_checks(void)
{
    const char *name="OZTEST:subdir/nested.txt";
    BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK),file=0,copy=0,parent=0;
    int error=lock?0:EIO;
    if(lock) {
        file=OpenFromLock(lock);
        if(file)lock=0; /* Ownership transferred; never UnLock a consumed lock. */
        else error=EIO;
    }
    if(file) {
        struct FileInfoBlock fib;
        if(IsInteractive(file))error=EIO;
        if(!ExamineFH(file,&fib)||strcmp((char *)fib.fib_FileName,"nested.txt")||
            fib.fib_DirEntryType!=ST_FILE||fib.fib_Size<=0)error=EIO;
        copy=DupLockFromFH(file);parent=ParentOfFH(file);
        if(!copy||!parent||!Examine(parent,&fib)||strcmp((char *)fib.fib_FileName,"subdir"))error=EIO;
        char bytes[7];
        if(Read(file,bytes,7)!=7||memcmp(bytes,"OpenZFS",7))error=EIO;
        if(!Close(file))error=EIO;
        if(copy&&(!Examine(copy,&fib)||strcmp((char *)fib.fib_FileName,"nested.txt")))error=EIO;
    }
    if(lock)UnLock(lock);
    if(copy)UnLock(copy);
    if(parent)UnLock(parent);
    lock=Lock((CONST_STRPTR)name,EXCLUSIVE_LOCK);file=0;
    if(!lock)error=EIO;
    if(lock) {
        file=OpenFromLock(lock);
        if(file)lock=0;else error=EIO;
    }
    if(file) {
        struct FileInfoBlock fib;
        if(!ExamineFH(file,&fib))error=EIO;
        copy=DupLockFromFH(file);
        if(copy||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(copy)UnLock(copy);
        BPTR other=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(other)Close(other);
        if(!Close(file))error=EIO;
    }
    if(lock)UnLock(lock);
    /* Failure must retain the original lock and its exclusive state. */
    lock=Lock((CONST_STRPTR)"OZTEST:subdir",EXCLUSIVE_LOCK);
    if(!lock)error=EIO;
    if(lock) {
        file=OpenFromLock(lock);
        if(file||IoErr()!=ERROR_OBJECT_WRONG_TYPE)error=EIO;
        if(file)Close(file);
        else {
            struct FileInfoBlock fib;
            if(!Examine(lock,&fib)||fib.fib_DirEntryType!=ST_USERDIR)error=EIO;
            UnLock(lock);
        }
    }
    char message[96];snprintf(message,sizeof(message),"Native DOS filehandle lock conversion/metadata=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int change_mode_checks(void)
{
    const char *name="OZTEST:hello.txt";
    BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK),other=0,file=0;
    int error=lock?0:EIO;
    if(lock)other=DupLock(lock);
    if(!other)error=EIO;
    if(!error&&(ChangeMode(CHANGE_LOCK,lock,EXCLUSIVE_LOCK)||IoErr()!=ERROR_OBJECT_IN_USE))error=EIO;
    if(lock&&((struct FileLock *)BADDR(lock))->fl_Access!=SHARED_LOCK)error=EIO;
    if(other)UnLock(other);
    if(!error&&!ChangeMode(CHANGE_LOCK,lock,EXCLUSIVE_LOCK))error=EIO;
    if(!error&&!ChangeMode(CHANGE_LOCK,lock,EXCLUSIVE_LOCK))error=EIO;
    if(!error) {
        other=Lock((CONST_STRPTR)name,SHARED_LOCK);
        if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(other)UnLock(other);
        if(ChangeMode(CHANGE_LOCK,lock,123)||IoErr()!=ERROR_BAD_NUMBER||
            ((struct FileLock *)BADDR(lock))->fl_Access!=EXCLUSIVE_LOCK)error=EIO;
    }
    if(!error&&!ChangeMode(CHANGE_LOCK,lock,SHARED_LOCK))error=EIO;
    if(!error)file=Open((CONST_STRPTR)name,MODE_OLDFILE);
    if(!file)error=EIO;
    if(file&&lock&&(ChangeMode(CHANGE_FH,file,EXCLUSIVE_LOCK)||IoErr()!=ERROR_OBJECT_IN_USE))error=EIO;
    if(lock)UnLock(lock);
    if(file) {
        if(!ChangeMode(CHANGE_FH,file,EXCLUSIVE_LOCK))error=EIO;
        other=DupLockFromFH(file);
        if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(other)UnLock(other);
        if(!ChangeMode(CHANGE_FH,file,SHARED_LOCK))error=EIO;
        other=DupLockFromFH(file);
        if(!other)error=EIO;
        if(other)UnLock(other);
        if(!Close(file))error=EIO;
    }
    char message[96];snprintf(message,sizeof(message),"Native DOS ChangeMode upgrade/downgrade=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int device_metadata(const struct DosList *node,const struct MsgPort *port)
{
    if(!node||node->dol_Type!=DLT_DEVICE||node->dol_Task!=port)return EIO;
    const struct FileSysStartupMsg *startup=BADDR(node->dol_misc.dol_handler.dol_Startup);
    if(!startup||!startup->fssm_Device||AROS_BSTR_strlen(startup->fssm_Device)||
        AROS_BSTR_ADDR(startup->fssm_Device)[0]||startup->fssm_Unit||startup->fssm_Flags)return EIO;
    const struct DosEnvec *env=BADDR(startup->fssm_Environ);
    if(!env||env->de_TableSize!=DE_DOSTYPE||env->de_DosType!=0x5a465301)return EIO;
    return 0;
}
static struct DosList *metadata_entry(void)
{
    struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
    struct DosList *node=FindDosEntry(list,(CONST_STRPTR)"OZTEST",LDF_DEVICES);
    UnLockDosList(LDF_DEVICES|LDF_READ);
    /* Test-owner contract: endpoint entries remain allocated until the
     * retained client has released every DeviceProc and joined. */
    return node;
}
static int secfs_queries(int authenticated)
{
    struct DevProc *device=GetDeviceProc((CONST_STRPTR)"OZTEST:",NULL);
    int error=device?0:EIO;
    if(device) {
        error=device_metadata(metadata_entry(),device->dvp_Port);
        SIPTR answer=DoPkt(device->dvp_Port,ACTION_IS_SECFS,0,0,0,0,0);
        if(answer!=(authenticated?DOSTRUE:DOSFALSE)||IoErr())error=EIO;
        answer=DoPkt(device->dvp_Port,ACTION_GET_SECFS_VERSION,0,0,0,0,0);
        if(answer!=secFS_API_VERSION||IoErr())error=EIO;
        FreeDeviceProc(device);
    }
    char message[96];snprintf(message,sizeof(message),
        "Native MuFS capability queries authenticated=%d result=%d\n",authenticated,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int remount_queries(struct client *c)
{
    /* Retain real DeviceProc references, not freed memory. The owner must
     * keep each detached endpoint alive until this client has quiesced. */
    struct DevProc *old[3]={0};
    struct DosList *oldnodes[3]={0};
    struct MsgPort *reply=CreateMsgPort();if(!reply)return ENOMEM;
    int error=0;
    for(unsigned round=0;round<3&&!error;round++) {
        old[round]=GetDeviceProc((CONST_STRPTR)"OZTEST:",NULL);
        if(!old[round]){error=EIO;break;}
        oldnodes[round]=metadata_entry();
        if(device_metadata(oldnodes[round],old[round]->dvp_Port)){error=EIO;break;}
        c->done.mn_ReplyPort=reply;c->busy_check=2;
        PutMsg(c->port,&c->done);WaitPort(reply);GetMsg(reply);
        c->done.mn_ReplyPort=NULL;c->busy_check=0;
        if(c->unmount_result){error=c->unmount_result;break;}
        struct DevProc *current=GetDeviceProc((CONST_STRPTR)"OZTEST:",NULL);
        if(!current){error=EIO;break;}
        for(unsigned i=0;i<=round;i++) {
            if(current->dvp_Port==old[i]->dvp_Port)error=EIO;
            if(device_metadata(oldnodes[i],old[i]->dvp_Port))error=EIO;
            const LONG queries[]={ACTION_IS_FILESYSTEM,ACTION_CURRENT_VOLUME,
                ACTION_IS_SECFS,ACTION_GET_SECFS_VERSION};
            for(unsigned q=0;q<sizeof(queries)/sizeof(queries[0]);q++) {
                SIPTR result=DoPkt(old[i]->dvp_Port,queries[q],0,0,0,0,0);
                if(result!=DOSFALSE||IoErr()!=ERROR_DEVICE_NOT_MOUNTED)error=EIO;
            }
        }
        if(device_metadata(metadata_entry(),current->dvp_Port))error=EIO;
        SIPTR result=DoPkt(current->dvp_Port,ACTION_IS_SECFS,0,0,0,0,0);
        if(result!=(c->secure?DOSTRUE:DOSFALSE)||IoErr())error=EIO;
        result=DoPkt(current->dvp_Port,ACTION_GET_SECFS_VERSION,0,0,0,0,0);
        if(result!=secFS_API_VERSION||IoErr())error=EIO;
        FreeDeviceProc(current);
    }
    for(unsigned i=0;i<3;i++)if(old[i])FreeDeviceProc(old[i]);
    DeleteMsgPort(reply);
    char message[120];snprintf(message,sizeof(message),
        "Native MuFS retained endpoints/remount authenticated=%d result=%d\n",c->secure,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static void *client_run(void *arg)
{
    struct client *c=arg;int result=secfs_queries(c->secure);
    struct Library *secBase=NULL;ULONG previous=0;int logged=0;
    if(c->secure) {
        secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase||!secIsConfigured())result=EACCES;
        if(!result) {
            previous=secGetTaskOwner(FindTask(NULL));
            BPTR denied=Open((CONST_STRPTR)"OZTEST:hello.txt",MODE_OLDFILE);
            if(denied){Close(denied);result=EIO;}
            else if(IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            struct TagItem tags[]={{secT_UserID,(IPTR)"ozreader"},
                {secT_Password,(IPTR)""},{TAG_DONE,0}};
            ULONG owner=secLoginA(tags);logged=owner!=0;
            if(owner!=((77UL<<16)|88))result=EACCES;
        }
    }
    const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    c->stage=1;
    for(unsigned iteration=0;iteration<3&&!result;iteration++) {
        BPTR file=Open((CONST_STRPTR)"OZTEST:hello.txt",MODE_OLDFILE);
        if(!file){result=IoErr()?IoErr():EIO;break;}
        if(iteration==0)result=check_busy(c);
        char buf[128]={0};LONG n=Read(file,buf,10);
        if(n!=10 || memcmp(buf,expected,10))result=EIO;
        if(!result && Seek(file,0,OFFSET_BEGINNING)!=10)result=EIO;
        if(!result) {
            n=Read(file,buf,sizeof(buf));
            if(n!=sizeof(expected)-1 || memcmp(buf,expected,sizeof(expected)-1))result=EIO;
        }
        if(!result && Read(file,buf,1)!=0)result=EIO;
        if(!Close(file)&&!result)result=EIO;
    }
    if(!result)c->stage=2;
    BPTR root=0,duplicate=0,filelock=0,parent=0;
    struct FileInfoBlock fib;
    if(!result) {
        root=Lock((CONST_STRPTR)"OZTEST:",SHARED_LOCK);
        if(!root||!Examine(root,&fib)||fib.fib_DirEntryType!=ST_ROOT ||
            fib.fib_OwnerUID!=secROOT_UID || fib.fib_OwnerGID!=secROOT_GID)result=EIO;
        struct InfoData info;
        if(!result&&(!Info(root,&info)||info.id_DiskState!=ID_WRITE_PROTECTED||
            !info.id_NumBlocksUsed||info.id_NumBlocks<=info.id_NumBlocksUsed||
            !info.id_VolumeNode||!info.id_InUse))result=EIO;
        if(!result)result=check_busy(c);
    }
    if(!result) {
        duplicate=DupLock(root);
        if(!duplicate||SameLock(root,duplicate)!=LOCK_SAME)result=EIO;
    }
    if(!result)c->stage=3;
    if(!result) {
        unsigned seen=0;
        for(unsigned i=0;i<2&&!result;i++) {
            if(!ExNext(root,&fib)){result=EIO;break;}
            if(!strcmp((char *)fib.fib_FileName,"hello.txt")&&fib.fib_DirEntryType==ST_FILE&&
                fib.fib_Size==sizeof(expected)-1&&!(seen&1))seen|=1;
            else if(!strcmp((char *)fib.fib_FileName,"subdir")&&fib.fib_DirEntryType==ST_USERDIR&&!(seen&2))seen|=2;
            else result=EIO;
        }
        if(seen!=3)result=EIO;
        if(!result&&(ExNext(root,&fib)||IoErr()!=ERROR_NO_MORE_ENTRIES))result=EIO;
        if(!result&&(!Examine(root,&fib)||!ExNext(root,&fib)))result=EIO;
    }
    if(!result)c->stage=4;
    if(!result) {
        BPTR previous=CurrentDir(root);
        filelock=Lock((CONST_STRPTR)"hello.txt",SHARED_LOCK);
        BPTR file=Open((CONST_STRPTR)"hello.txt",MODE_OLDFILE);
        CurrentDir(previous);
        if(!filelock||!file)result=EIO;
        if(file) {
            char buf[128];LONG n=Read(file,buf,sizeof(buf));
            if(n!=sizeof(expected)-1||memcmp(buf,expected,sizeof(expected)-1))result=EIO;
            Close(file);
        }
        if(filelock) {
            if(!Examine(filelock,&fib)||fib.fib_DirEntryType!=ST_FILE ||
                fib.fib_OwnerUID!=secROOT_UID || fib.fib_OwnerGID!=secROOT_GID)result=EIO;
            parent=ParentDir(filelock);
            if(!parent||SameLock(parent,root)!=LOCK_SAME)result=EIO;
        }
    }
    if(parent)UnLock(parent);
    if(filelock)UnLock(filelock);
    if(duplicate)UnLock(duplicate);
    if(root)UnLock(root);
    if(!result)c->stage=7;
    if(!result) {
        BPTR dir=Lock((CONST_STRPTR)"OZTEST:subdir",SHARED_LOCK);
        BPTR nested=Lock((CONST_STRPTR)"OZTEST:subdir/nested.txt",SHARED_LOCK);
        BPTR up=nested?ParentDir(nested):0;
        if(!dir||!nested||!up||SameLock(up,dir)!=LOCK_SAME)result=EIO;
        if(!result&&(!Examine(up,&fib)||strcmp((char *)fib.fib_FileName,"subdir")||
            !ExNext(up,&fib)||strcmp((char *)fib.fib_FileName,"nested.txt")))result=EIO;
        BPTR old=dir?CurrentDir(dir):0;
        BPTR file=dir?Open((CONST_STRPTR)"nested.txt",MODE_OLDFILE):0;
        if(dir)CurrentDir(old);
        if(!file)result=EIO;
        if(file) {
            char buf[128];LONG n=Read(file,buf,sizeof(buf));
            if(n!=sizeof(expected)-1||memcmp(buf,expected,sizeof(expected)-1))result=EIO;
            Close(file);
        }
        file=Open((CONST_STRPTR)"OZTEST:subdir//hello.txt",MODE_OLDFILE);
        if(!file)result=EIO;else Close(file);
        file=Open((CONST_STRPTR)"OZTEST:hello.txt/nested.txt",MODE_OLDFILE);
        if(file){Close(file);result=EIO;}else if(IoErr()!=ERROR_OBJECT_WRONG_TYPE)result=EIO;
        if(up)UnLock(up);
        if(nested)UnLock(nested);
        if(dir)UnLock(dir);
    }
    if(!result)c->stage=5;
    if(!result) {
        BPTR file=Open((CONST_STRPTR)"OZTEST:absent.txt",MODE_OLDFILE);
        if(file){Close(file);result=EIO;}
        else if(IoErr()!=ERROR_OBJECT_NOT_FOUND)result=EIO;
    }
    if(!result)c->stage=6;
    if(!result) {
        BPTR file=Open((CONST_STRPTR)"OZTEST:must-not-create.txt",MODE_NEWFILE);
        if(file){Close(file);result=EIO;}
        else if(IoErr()!=ERROR_DISK_WRITE_PROTECTED)result=EIO;
    }
    if(!result)result=exclusive_lock_checks();
    if(!result)result=filehandle_lock_checks();
    if(!result)result=change_mode_checks();
    if(!result)result=exall_checks();
    if(c->secure&&logged) {
        BPTR held=0,heldlock=0;
        if(!result) {
            held=Open((CONST_STRPTR)"OZTEST:hello.txt",MODE_OLDFILE);
            heldlock=Lock((CONST_STRPTR)"OZTEST:",SHARED_LOCK);
            if(!held||!heldlock)result=EIO;
        }
        struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(tags)!=previous)result=EIO;
        if(secfs_queries(1))result=EIO;
        if(held) {
            char sentinel=0x5a;
            if(Read(held,&sentinel,1)!=-1 || IoErr()!=ERROR_READ_PROTECTED || sentinel!=0x5a)result=EIO;
            if(Seek(held,0,OFFSET_BEGINNING)!=-1 || IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            if(ChangeMode(CHANGE_FH,held,EXCLUSIVE_LOCK)||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            struct FileHandle *publicfh=BADDR(held);
            const LONG actions64[]={ACTION_GET_FILE_POSITION64,ACTION_GET_FILE_SIZE64,
                ACTION_CHANGE_FILE_POSITION64,ACTION_CHANGE_FILE_SIZE64};
            for(unsigned i=0;i<4;i++) {
                SIPTR reply=DoPkt(publicfh->fh_Type,actions64[i],publicfh->fh_Arg1,
                    0,OFFSET_BEGINNING,0,0);
                if(reply!=(i<2?-1:DOSFALSE)||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            }
            struct FileInfoBlock metadata;
            if(ExamineFH(held,&metadata)||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            BPTR denied=DupLockFromFH(held);
            if(denied||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            if(denied)UnLock(denied);
            denied=ParentOfFH(held);
            if(denied||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            if(denied)UnLock(denied);
            if(!Close(held))result=EIO;
        }
        if(heldlock) {
            struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
            union {IPTR alignment;unsigned char bytes[256];} storage;
            if(!ctl)result=ENOMEM;
            else {
                ctl->eac_Entries=123;
                if(ExAll(heldlock,(struct ExAllData *)storage.bytes,sizeof(storage.bytes),ED_NAME,ctl)||
                    IoErr()!=ERROR_READ_PROTECTED||ctl->eac_Entries)result=EIO;
                FreeDosObject(DOS_EXALLCONTROL,ctl);
            }
            struct FileInfoBlock fib;
            if(ChangeMode(CHANGE_LOCK,heldlock,EXCLUSIVE_LOCK)||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            if(Examine(heldlock,&fib)||IoErr()!=ERROR_READ_PROTECTED)result=EIO;
            UnLock(heldlock);
        }
    }
    if(secBase)CloseLibrary(secBase);
    if(!result)result=remount_queries(c);
    c->result=result;PutMsg(c->port,&c->done);return NULL;
}

static int acl_bridge_test(struct oz_zpl *fs,int execute_error)
{
    /* Explicit test credential, not an authentication shortcut in the
     * handler. Real packet-owner capture has separate native tests. */
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if(!reader)return ENOMEM;
    struct oz_zpl_info info;
    int rc=oz_zpl_resolve_as(fs,oz_zpl_root(fs),"subdir/nested.txt",&info,reader);
    if(!rc) {
        char actual[128]={0};
        const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        if(info.size!=sizeof(expected)-1)rc=EIO;
        if(!rc)rc=oz_zpl_read_as(fs,info.object,0,actual,info.size,reader);
        if(!rc&&memcmp(actual,expected,info.size))rc=EIO;
        if(!rc) {
            int exec=oz_zpl_access(fs,info.object,ACE_EXECUTE,reader);
            int write=oz_zpl_access(fs,info.object,ACE_WRITE_DATA,reader);
            int invalid=oz_zpl_access(fs,info.object,0x80000000u,reader);
            if(exec!=execute_error||write!=EROFS||invalid!=EINVAL) {
                char line[120];snprintf(line,sizeof(line),"ACL fixture access exec=%d write=%d invalid=%d\n",exec,write,invalid);
                for(char *p=line;*p;p++)RawPutChar(*p);
                rc=EIO;
            }
        }
        actual[0]=0x5a;
        if(!rc&&(oz_zpl_read_as(fs,info.object,0,actual,1,NULL)!=EACCES ||
            actual[0]!=0x5a))rc=EIO;
    }
    memset(&info,0xa5,sizeof(info));
    if(!rc&&(oz_zpl_resolve_as(fs,oz_zpl_root(fs),"subdir/nested.txt",&info,NULL)!=EACCES ||
        info.object || info.size || info.parent))rc=EIO;
    oz_cred_release(reader);
    char text[96];snprintf(text,sizeof(text),"Native mount ACL traversal/read bridge=%d\n",rc);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return rc;
}

static int handler_test_mode(struct oz_zpl *fs,int secure,int execute_error)
{
    int acl=acl_bridge_test(fs,execute_error);if(acl)return acl;
    /* Exercise the live read bridge's error paths before the DOS packet
     * client. Subsequent reads and mount teardown verify reference balance. */
    uint64_t object,size,reads_before,bytes_before,counter;char byte;
    if(oz_zpl_counter(fs,"reads",&reads_before) ||
        oz_zpl_counter(fs,"nread",&bytes_before))return EIO;
    counter=UINT64_MAX;
    if(oz_zpl_counter(fs,"absent-counter",&counter)!=ENOENT ||
        counter!=UINT64_MAX ||
        oz_zpl_counter(fs,"dataset_name",&counter)!=ENOTSUP ||
        counter!=UINT64_MAX || oz_zpl_counter(fs,NULL,&counter)!=EINVAL ||
        oz_zpl_counter(fs,"reads",NULL)!=EINVAL)return EIO;
    int boundary=oz_zpl_lookup(fs,"hello.txt",&object,&size);
    if(boundary)return boundary;
    int bounds[]={oz_zpl_read(fs,object,size,&byte,1),
        oz_zpl_read(fs,object,UINT64_MAX,&byte,1),
        oz_zpl_read(fs,object,0,NULL,1),
        oz_zpl_read(fs,oz_zpl_root(fs),0,&byte,1),
        oz_zpl_read(fs,object,size,NULL,0)};
    const int wanted[]={EINVAL,EOVERFLOW,EINVAL,EISDIR,0};
    for(unsigned i=0;i<5;i++)if(bounds[i]!=wanted[i]) {
        char line[100];snprintf(line,sizeof(line),"DOS read boundary %u actual=%d expected=%d\n",i,bounds[i],wanted[i]);
        for(char *p=line;*p;p++)RawPutChar(*p);
        return EIO;
    }
    if(oz_zpl_counter(fs,"reads",&counter) || counter!=reads_before ||
        oz_zpl_counter(fs,"nread",&counter) || counter!=bytes_before)return EIO;
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    struct oz_mount *mount=NULL;
    struct DosList *adopted=NULL;
    struct DosEnvec adopted_environment={0};
    struct FileSysStartupMsg adopted_startup={0};
    BSTR adopted_device=AROS_CONST_BSTR("");
    int result=0;
    if(secure) {
        adopted=MakeDosEntry((CONST_STRPTR)"OZTEST",DLT_DEVICE);
        if(!adopted){DeleteMsgPort(done);return ENOMEM;}
        adopted_environment.de_TableSize=DE_DOSTYPE;
        adopted_environment.de_DosType=0x5a465301;
        adopted_startup.fssm_Device=adopted_device;
        adopted_startup.fssm_Environ=MKBADDR(&adopted_environment);
        adopted->dol_misc.dol_handler.dol_Startup=MKBADDR(&adopted_startup);
        if(!AddDosEntry(adopted)){FreeDosEntry(adopted);DeleteMsgPort(done);return EEXIST;}
        /* Identity, active-entry and volume-publication failures must not
         * alter or free Mount's caller-owned device allocation. */
        if(oz_dos_mount_adopt(fs,"OZWRONG",adopted,&mount,1,-1)!=EINVAL||mount)result=EIO;
        adopted->dol_Task=done;
        if(oz_dos_mount_adopt(fs,"OZTEST",adopted,&mount,1,-1)!=EINVAL||mount||adopted->dol_Task!=done)result=EIO;
        adopted->dol_Task=NULL;
        struct DosList *collision=MakeDosEntry((CONST_STRPTR)"OZTEST",DLT_VOLUME);
        if(!collision)result=ENOMEM;
        else {
            if(!AddDosEntry(collision))result=EIO;
            else {
                if(oz_dos_mount_adopt(fs,"OZTEST",adopted,&mount,1,-1)!=EEXIST||mount||adopted->dol_Task)result=EIO;
                RemDosEntry(collision);
            }
            FreeDosEntry(collision);
        }
        if(!result)result=startup_adopt_with_reader(fs,adopted,&mount);
    } else result=oz_dos_mount_fixture(fs,"OZTEST",&mount);
    if(result){if(adopted){RemDosEntry(adopted);FreeDosEntry(adopted);}DeleteMsgPort(done);return result;}
    struct oz_mount *duplicate=NULL;
    int duplicate_rc=oz_dos_mount_fixture(fs,"OZTEST",&duplicate);
    if(duplicate_rc!=EEXIST) {
        if(duplicate)oz_dos_unmount(duplicate);
        oz_dos_unmount(mount);if(adopted)FreeDosEntry(adopted);DeleteMsgPort(done);return EIO;
    }
    struct client client;memset(&client,0,sizeof(client));client.port=done;client.secure=secure;
    struct oz_mount *retired[3]={0};unsigned retired_count=0;
    client.done.mn_Node.ln_Type=NT_MESSAGE;client.done.mn_Length=sizeof(client.done);
    pthread_t thread;result=pthread_create(&thread,NULL,client_run,&client);
    if(!result) {
        for(;;) {
            ULONG signals=(1UL<<done->mp_SigBit)|oz_dos_signals(mount);
            for(unsigned i=0;i<retired_count;i++)signals|=oz_dos_signals(retired[i]);
            Wait(signals);
            oz_dos_pump(mount);
            for(unsigned i=0;i<retired_count;i++)oz_dos_pump(retired[i]);
            struct Message *message=GetMsg(done);
            if(message) {
                if(!client.busy_check)break;
                if(client.busy_check==1)client.unmount_result=oz_dos_unmount(mount);
                else if(retired_count==3)client.unmount_result=EOVERFLOW;
                else {
                    size_t retained_bytes=oz_dos_bookkeeping_bytes(mount);
                    client.unmount_result=oz_dos_detach(mount);
                    if(!client.unmount_result) {
                        if(oz_dos_bookkeeping_bytes(mount)||(!retired_count&&!retained_bytes))
                            client.unmount_result=EIO;
                    }
                    if(!client.unmount_result) {
                        struct oz_mount *replacement=NULL;
                        client.unmount_result=secure?oz_dos_mount(fs,"OZTEST",&replacement):
                            oz_dos_mount_fixture(fs,"OZTEST",&replacement);
                        if(!client.unmount_result) {
                            retired[retired_count++]=mount;mount=replacement;
                        }
                    }
                }
                ReplyMsg(message);
            }
        }
        pthread_join(thread,NULL);result=client.result;
        char diagnostic[128];snprintf(diagnostic,sizeof(diagnostic),
            "DOS lock/enumeration/relative-open tests stage=%d result=%d\n",client.stage,result);
        for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    }
    if(!result && (oz_zpl_counter(fs,"reads",&counter) ||
        counter!=reads_before+9 || oz_zpl_counter(fs,"nread",&counter) ||
        counter!=bytes_before+37+5*size ||
        oz_zpl_counter(fs,"writes",&counter) || counter ||
        oz_zpl_counter(fs,"nwritten",&counter) || counter))result=EIO;
    char stats[96];snprintf(stats,sizeof(stats),
        "DOS handler upstream dataset counters result=%d\n",result);
    for(char *p=stats;*p;p++)RawPutChar(*p);
    int rc=oz_dos_unmount(mount);if(!result)result=rc;
    /* All retained DeviceProc references were released before joining the
     * client; only now may the old endpoint allocations be destroyed. */
    for(unsigned i=0;i<retired_count;i++) {
        rc=oz_dos_unmount(retired[i]);if(!result)result=rc;
    }
    if(adopted) {
        if(adopted->dol_Task||adopted->dol_misc.dol_handler.dol_Startup!=MKBADDR(&adopted_startup)||
            adopted_startup.fssm_Environ!=MKBADDR(&adopted_environment)||
            adopted_startup.fssm_Device!=adopted_device||
            adopted_environment.de_DosType!=0x5a465301)result=EIO;
        /* Only the original caller releases its borrowed device entry. */
        FreeDosEntry(adopted);
        char text[96];snprintf(text,sizeof(text),"DOS startup entry adoption rollback/read/retirement result=%d\n",result);
        for(char *p=text;*p;p++)RawPutChar(*p);
    }
    DeleteMsgPort(done);
    if(secure) {
        char text[100];snprintf(text,sizeof(text),"Authenticated DOS handler ACL/logout/lifetime=%d\n",result);
        for(char *p=text;*p;p++)RawPutChar(*p);
    }
    return result;
}
static void *restricted_client(void *arg)
{
    struct client *c=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int error=(!secBase||!secIsConfigured())?EACCES:0;
    for(int root=0;root<2&&!error;root++) {
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)(root?"oztestroot":"ozreader")},
            {secT_Password,(IPTR)""},{TAG_DONE,0}};
        ULONG owner=secLoginA(login);
        if(!owner){error=EACCES;break;}
        const char *native_names[]={"OZACL:native-denied","OZACL:native-allowed",
            "OZACL:native-no-search/child"};
        for(unsigned i=0;i<3;i++) {
            BPTR opened=Open((CONST_STRPTR)native_names[i],MODE_OLDFILE);
            if(root||i==1) { if(!opened)error=EIO; }
            else if(opened||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
            if(opened)Close(opened);
        }
        if(root) {
            BPTR metadata=Lock((CONST_STRPTR)"OZACL:native-denied",SHARED_LOCK);
            struct FileInfoBlock native_fib;
            if(!metadata||!Examine(metadata,&native_fib)||
                (native_fib.fib_Protection&(FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE))!=
                (FIBF_READ|FIBF_WRITE|FIBF_EXECUTE|FIBF_DELETE))error=EIO;
            if(metadata)UnLock(metadata);
            /* An already-open handle does not retain root's native access
             * after the sending task changes identity. */
            BPTR held=Open((CONST_STRPTR)"OZACL:native-denied",MODE_OLDFILE);
            struct TagItem reader_login[]={{secT_UserID,(IPTR)"ozreader"},
                {secT_Password,(IPTR)""},{TAG_DONE,0}};
            ULONG reader=secLoginA(reader_login);
            if(!held||reader!=((77UL<<16)|88))error=EIO;
            if(held&&reader) {
                unsigned char byte;
                if(Read(held,&byte,1)!=-1||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
            }
            if(reader) {
                struct TagItem restore[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
                if(secLogoutA(restore)!=secOWNER_SYSTEM)error=EIO;
            }
            if(held)Close(held);
        }
        const char *paths[]={"OZACL:hello.txt","OZACL:subdir/nested.txt","OZACL:hello-alias.txt"};
        for(unsigned i=0;i<3;i++) {
            BPTR file=Open((CONST_STRPTR)paths[i],MODE_OLDFILE);
            if(!root) {
                if(file||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
            } else if(!file)error=EIO;
            else {
                unsigned char byte=0;
                if(Read(file,&byte,1)!=1||byte!=(i==1?'O':0xa7))error=EIO;
            }
            if(file)Close(file);
        }
        const char *created[]={"OZACL:subdir/created-file","OZACL:subdir/created-directory"};
        for(unsigned i=0;i<2;i++) {
            BPTR lock=Lock((CONST_STRPTR)created[i],SHARED_LOCK);
            if(!root) {
                if(lock||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
            } else {
                struct FileInfoBlock info;
                if(!lock||!Examine(lock,&info)||
                    (i ? info.fib_DirEntryType<=0 : info.fib_DirEntryType>=0)||
                    (!i&&info.fib_Size!=0))error=EIO;
            }
            if(lock)UnLock(lock);
        }
        BPTR dir=Lock((CONST_STRPTR)"OZACL:subdir",SHARED_LOCK);
        struct FileInfoBlock fib;
        BPTR owned=Lock((CONST_STRPTR)"OZACL:subdir/setattr-file",SHARED_LOCK);
        /* Owning the file cannot bypass the parent's non-searchable ACL. */
        if(!root) {
            if(owned || IoErr()!=ERROR_READ_PROTECTED)error=EIO;
        } else if(!owned || !Examine(owned,&fib) || fib.fib_OwnerUID!=77 ||
            fib.fib_OwnerGID!=99 || fib.fib_Size!=8)error=EIO;
        if(owned)UnLock(owned);
        if(!dir||!Examine(dir,&fib))error=EIO;
        if(dir) {
            LONG next=ExNext(dir,&fib);
            if(root?!next:(next||IoErr()!=ERROR_READ_PROTECTED))error=EIO;
            UnLock(dir);
        }
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous||secGetTaskOwner(FindTask(NULL))!=previous)error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    c->result=error;PutMsg(c->port,&c->done);return NULL;
}

static int restricted_handler_test(void)
{
    struct oz_zpl *fs=NULL;struct oz_mount *mount=NULL;
    int error=oz_zpl_open("qemu_mirror/writefiles",&fs);
    if(error)return error;
    struct MsgPort *done=CreateMsgPort();
    if(!done)error=ENOMEM;
    if(!error)error=oz_dos_mount(fs,"OZACL",&mount);
    if(!error) {
        struct client c={0};c.port=done;
        c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
        pthread_t thread;error=pthread_create(&thread,NULL,restricted_client,&c);
        if(!error) {
            do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));
                oz_dos_pump(mount);} while(!GetMsg(done));
            pthread_join(thread,NULL);error=c.result;
        }
    }
    uint64_t count;
    if(!error&&(oz_zpl_counter(fs,"reads",&count)||count!=3||
        oz_zpl_counter(fs,"nread",&count)||count!=3||
        oz_zpl_counter(fs,"writes",&count)||count))error=EIO;
    if(mount){int rc=oz_dos_unmount(mount);if(!error)error=rc;}
    if(done)DeleteMsgPort(done);
    int rc=oz_zpl_close(fs);if(!error)error=rc;
    char text[100];snprintf(text,sizeof(text),"Authenticated DOS restricted file/directory ACL=%d\n",error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return error;
}

static void batch_packet(struct StandardPacket *p,LONG action)
{
    memset(p,0,sizeof(*p));p->sp_Msg.mn_Node.ln_Name=(void *)&p->sp_Pkt;
    p->sp_Msg.mn_Length=sizeof(*p);p->sp_Pkt.dp_Link=&p->sp_Msg;p->sp_Pkt.dp_Type=action;
}
static int batch_state_test(struct oz_zpl *fs)
{
    struct oz_mount *mount=NULL;struct MsgPort *reply=CreateMsgPort();
    if(!reply)return ENOMEM;
    int error=oz_dos_mount_fixture(fs,"OZBATCH",&mount);
    if(error){DeleteMsgPort(reply);return error;}
    struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
    struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)"OZBATCH",LDF_DEVICES);
    struct MsgPort *port=entry?entry->dol_Task:NULL;
    UnLockDosList(LDF_DEVICES|LDF_READ);
    if(!port){oz_dos_unmount(mount);DeleteMsgPort(reply);return ENOENT;}
    struct StandardPacket create,info_packet,release;
    struct InfoData info;
    batch_packet(&create,ACTION_COPY_DIR); /* null source: duplicate root */
    batch_packet(&info_packet,ACTION_DISK_INFO);
    info_packet.sp_Pkt.dp_Arg1=(SIPTR)MKBADDR(&info);
    /* Both queued before the owning task drains either. */
    SendPkt(&create.sp_Pkt,port,reply);SendPkt(&info_packet.sp_Pkt,port,reply);
    oz_dos_pump(mount);
    unsigned replies=0;while(GetMsg(reply))replies++;
    BPTR lock=(BPTR)create.sp_Pkt.dp_Res1;
    if(replies!=2||!lock||create.sp_Pkt.dp_Res2||info_packet.sp_Pkt.dp_Res2||!info.id_InUse)error=EIO;
    if(lock) {
        batch_packet(&release,ACTION_FREE_LOCK);release.sp_Pkt.dp_Arg1=(SIPTR)lock;
        SendPkt(&release.sp_Pkt,port,reply);oz_dos_pump(mount);
        if(GetMsg(reply)!=&release.sp_Msg||release.sp_Pkt.dp_Res1!=DOSTRUE||release.sp_Pkt.dp_Res2)error=EIO;
    }
    int detached=oz_dos_unmount(mount);if(!error)error=detached;
    DeleteMsgPort(reply);
    char line[100];snprintf(line,sizeof(line),"DOS same-drain dynamic lock visibility=%d\n",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}

/* Dataset-independent mount/enumeration/read suite, also used after cold
 * label discovery. The restricted fixture suite below has its own dataset. */
static int volume_collision_test(struct oz_zpl *fs)
{
    /* AROS permits duplicate volume nodes. Our explicit DOS mount alias
     * must not shadow a foreign volume merely because no device shares it. */
    struct DosList *foreign=MakeDosEntry((CONST_STRPTR)"OzCollision",DLT_VOLUME);
    if(!foreign)return ENOMEM;
    if(!AddDosEntry(foreign)){FreeDosEntry(foreign);return EIO;}
    struct oz_mount *mount=NULL;
    int rc=oz_dos_mount_fixture(fs,"OZCOLLISION",&mount);
    int rejected=rc==EEXIST&&!mount;
    int error=rejected?0:EIO;
    if(mount){int close=oz_dos_unmount(mount);if(close)error=close;}
    struct DosList *list=LockDosList(LDF_VOLUMES|LDF_DEVICES|LDF_READ);
    int preserved=FindDosEntry(list,(CONST_STRPTR)"OzCollision",LDF_VOLUMES)==foreign&&
        !FindDosEntry(list,(CONST_STRPTR)"OzCollision",LDF_DEVICES);
    if(!preserved)error=EIO;
    UnLockDosList(LDF_VOLUMES|LDF_DEVICES|LDF_READ);
    RemDosEntry(foreign);FreeDosEntry(foreign);
    if(!error) {
        rc=oz_dos_mount_fixture(fs,"OZCOLLISION",&mount);
        if(rc)error=rc;
        else error=oz_dos_unmount(mount);
    }
    char line[128];snprintf(line,sizeof(line),
        "DOS volume collision result=%d rejected=%d foreign-preserved=%d retry=%d\n",
        error,rejected,preserved,!error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}

#ifdef OZ_DOS_ALLOC_TEST
extern void oz_dos_test_fail_allocation(unsigned);
extern void oz_dos_test_fail_creation(void);
extern void oz_dos_test_fail_publication(void);
extern void oz_dos_test_fail_after_publication(void);
extern unsigned oz_dos_test_allocation_hits(void);
static void *allocation_lock_client(void *arg)
{
    struct client *c=arg;
    BPTR lock=Lock((CONST_STRPTR)"OZALLOC:",SHARED_LOCK);
    if(lock||IoErr()!=ERROR_NO_FREE_STORE)c->result=EIO;
    if(lock)UnLock(lock);
    lock=Lock((CONST_STRPTR)"OZALLOC:",SHARED_LOCK);
    if(!lock)c->result=EIO;
    else {
        struct FileInfoBlock info;
        if(!Examine(lock,&info)||info.fib_DirEntryType<=0)c->result=EIO;
        UnLock(lock);
    }
    PutMsg(c->port,&c->done);return NULL;
}
static int lock_allocation_test(struct oz_zpl *fs)
{
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    struct oz_mount *mount=NULL;
    int error=oz_dos_mount_fixture(fs,"OZALLOC",&mount);
    if(error){DeleteMsgPort(done);return error;}
    struct client c={.port=done};
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;
    oz_dos_test_fail_allocation(1);
    error=pthread_create(&thread,NULL,allocation_lock_client,&c);
    if(!error) {
        for(;;) {
            Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));
            oz_dos_pump(mount);
            if(GetMsg(done))break;
        }
        pthread_join(thread,NULL);error=c.result;
    }
    unsigned hits=oz_dos_test_allocation_hits();
    if(hits!=1)error=EIO;
    int close=oz_dos_unmount(mount);if(!error)error=close;
    DeleteMsgPort(done);
    char line[100];snprintf(line,sizeof(line),
        "DOS lock allocation failure hits=%u result=%d\n",hits,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int mount_allocation_test(struct oz_zpl *fs)
{
    BYTE signal=AllocSignal(-1);if(signal<0)return ENOMEM;
    int error=0;
    for(unsigned nth=1;nth<=2&&!error;nth++) {
        struct oz_mount *mount=NULL;
        oz_dos_test_fail_allocation(nth);
        int rc=oz_dos_mount_shared(fs,"OZALLOC",&mount,0,signal);
        unsigned hits=oz_dos_test_allocation_hits();
        if(rc!=ENOMEM||mount||hits!=1)error=EIO;
        if(mount){int close=oz_dos_unmount(mount);if(close)error=close;}
        struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
        if(FindDosEntry(list,(CONST_STRPTR)"OZALLOC",LDF_ALL))error=EIO;
        UnLockDosList(LDF_ALL|LDF_READ);
        if(!error) {
            rc=oz_dos_mount_shared(fs,"OZALLOC",&mount,0,signal);
            if(rc)error=rc;else error=oz_dos_unmount(mount);
        }
        char line[100];snprintf(line,sizeof(line),
            "DOS mount allocation failure nth=%u hits=%u result=%d\n",nth,hits,error);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    for(unsigned nth=1;nth<=2&&!error;nth++) {
        struct DosList *device=MakeDosEntry((CONST_STRPTR)"OZALLOC",DLT_DEVICE);
        if(!device){error=ENOMEM;break;}
        struct FileSysStartupMsg startup={0};
        device->dol_misc.dol_handler.dol_Startup=MKBADDR(&startup);
        BSTR name=device->dol_Name;
        if(!AddDosEntry(device)){FreeDosEntry(device);error=EIO;break;}
        struct oz_mount *mount=NULL;
        oz_dos_test_fail_allocation(nth);
        int rc=oz_dos_mount_adopt(fs,"OZALLOC",device,&mount,0,signal);
        unsigned hits=oz_dos_test_allocation_hits();
        if(rc!=ENOMEM||mount||hits!=1)error=EIO;
        if(mount){int close=oz_dos_unmount(mount);if(close)error=close;}
        struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
        if(FindDosEntry(list,(CONST_STRPTR)"OZALLOC",LDF_DEVICES)!=device||
            FindDosEntry(list,(CONST_STRPTR)"OZALLOC",LDF_VOLUMES)||
            device->dol_Task||device->dol_Type!=DLT_DEVICE||device->dol_Name!=name||
            device->dol_misc.dol_handler.dol_Startup!=MKBADDR(&startup))error=EIO;
        UnLockDosList(LDF_ALL|LDF_READ);
        if(!error) {
            rc=oz_dos_mount_adopt(fs,"OZALLOC",device,&mount,0,signal);
            if(rc)error=rc;else error=oz_dos_unmount(mount);
            if(device->dol_Task||device->dol_misc.dol_handler.dol_Startup!=MKBADDR(&startup))error=EIO;
        }
        list=LockDosList(LDF_ALL|LDF_WRITE);
        if(FindDosEntry(list,(CONST_STRPTR)"OZALLOC",LDF_DEVICES)==device)
            RemDosEntry(device);
        UnLockDosList(LDF_ALL|LDF_WRITE);
        FreeDosEntry(device);
        char line[110];snprintf(line,sizeof(line),
            "DOS adopted mount allocation failure nth=%u hits=%u result=%d\n",nth,hits,error);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    FreeSignal(signal);return error;
}
#endif

int oz_dos_read_mount_test(struct oz_zpl *fs,int execute_error)
{
    int error=volume_collision_test(fs);
#ifdef OZ_DOS_ALLOC_TEST
    if(!error)error=mount_allocation_test(fs);
    if(!error)error=lock_allocation_test(fs);
#endif
    if(!error)error=batch_state_test(fs);
    if(!error)error=handler_test_mode(fs,0,execute_error);
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(marker){fclose(marker);if(!error)error=handler_test_mode(fs,1,execute_error);}
    return error;
}

int oz_dos_handler_test(struct oz_zpl *fs)
{
    int error=oz_dos_read_mount_test(fs,EACCES);
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(marker){fclose(marker);if(!error)error=restricted_handler_test();}
    return error;
}

static void *property_client(void *arg)
{
    struct client *c=arg;
    const struct DateStamp expected={10000,123,1234};
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int error=(!secBase||!secIsConfigured())?EACCES:0;
    for(int reader=0;reader<2&&!error;reader++) {
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)(reader?"ozreader":"oztestroot")},
            {secT_Password,(IPTR)""},{TAG_DONE,0}};
        ULONG owner=secLoginA(login);
        if(!owner){error=EACCES;break;}
        if(!c->secure) {
            if(SetProtection((CONST_STRPTR)"OZPROP:hello.txt",0)||IoErr()!=ERROR_DISK_WRITE_PROTECTED)
                error=EIO;
            if(SetFileDate((CONST_STRPTR)"OZPROP:hello.txt",&expected)||IoErr()!=ERROR_DISK_WRITE_PROTECTED)
                error=EIO;
        } else if(reader) {
            /* ZFS fixture grants EVERYONE all rights; native ownership must
             * still reject this caller's property mutation. */
            if(SetProtection((CONST_STRPTR)"OZPROP:hello.txt",0)||IoErr()!=ERROR_WRITE_PROTECTED)
                error=EIO;
            if(SetFileDate((CONST_STRPTR)"OZPROP:hello.txt",&expected)||IoErr()!=ERROR_WRITE_PROTECTED)
                error=EIO;
        } else {
            const struct DateStamp invalid[]={{-1,0,0},{0,-1,0},{0,1440,0},
                {0,0,-1},{0,0,60*TICKS_PER_SECOND}};
            for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0])&&!error;i++)
                if(SetFileDate((CONST_STRPTR)"OZPROP:hello.txt",&invalid[i])||IoErr()!=ERROR_BAD_NUMBER)
                    error=EIO;
            if(!error&&!SetFileDate((CONST_STRPTR)"OZPROP:hello.txt",&expected))error=EIO;
            if(!error&&(SetFileDate((CONST_STRPTR)"OZPROP:missing",&expected)||
                IoErr()!=ERROR_OBJECT_NOT_FOUND))error=EIO;
            if(!SetProtection((CONST_STRPTR)"OZPROP:hello.txt",FIBF_READ|FIBF_WRITE))error=EIO;
            if(!error&&!SetProtection((CONST_STRPTR)"OZPROP:hello.txt",0))error=EIO;
            if(!error&&!SetProtection((CONST_STRPTR)"OZPROP:hello.txt",(LONG)0x81234567))error=EIO;
            BPTR lock=Lock((CONST_STRPTR)"OZPROP:hello.txt",SHARED_LOCK);
            struct FileInfoBlock fib;
            if(!lock||!Examine(lock,&fib)||(uint32_t)fib.fib_Protection!=0x81234567)error=EIO;
            if(lock)UnLock(lock);
            if(!error&&SetProtection((CONST_STRPTR)"OZPROP:missing",0))error=EIO;
            else if(!error&&IoErr()!=ERROR_OBJECT_NOT_FOUND)error=EIO;
        }
        if(!reader&&!error) {
            BPTR lock=Lock((CONST_STRPTR)"OZPROP:hello.txt",SHARED_LOCK);
            struct FileInfoBlock fib;
            if(!lock||!Examine(lock,&fib)||fib.fib_Date.ds_Days!=expected.ds_Days||
                fib.fib_Date.ds_Minute!=expected.ds_Minute||fib.fib_Date.ds_Tick!=expected.ds_Tick)
                error=EIO;
            if(lock)UnLock(lock);
        }
        if(!error&&c->secure&&!reader) {
            if(!SetProtection((CONST_STRPTR)"OZPROP:subdir/nested.txt",FIBF_READ|FIBF_WRITE))error=EIO;
        }
        if(!error&&c->secure&&reader) {
            BPTR denied=Open((CONST_STRPTR)"OZPROP:subdir/nested.txt",MODE_OLDFILE);
            if(denied||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
            if(denied)Close(denied);
            /* The owner must not need READ to restore READ, or to change a
             * date. ZFS attributes and native property rights still apply. */
            if(!error&&!SetFileDate((CONST_STRPTR)"OZPROP:subdir/nested.txt",&expected))error=EIO;
            if(!error&&!SetProtection((CONST_STRPTR)"OZPROP:subdir/nested.txt",0))error=EIO;
        }
        if(!error&&(reader||!c->secure)) {
            BPTR lock=Lock((CONST_STRPTR)"OZPROP:subdir/nested.txt",SHARED_LOCK);
            struct FileInfoBlock fib;
            if(!lock||!Examine(lock,&fib)||fib.fib_OwnerUID!=77||fib.fib_OwnerGID!=88||
                fib.fib_Date.ds_Days!=expected.ds_Days||fib.fib_Date.ds_Minute!=expected.ds_Minute||
                fib.fib_Date.ds_Tick!=expected.ds_Tick)error=EIO;
            if(lock)UnLock(lock);
            BPTR file=Open((CONST_STRPTR)"OZPROP:subdir/nested.txt",MODE_OLDFILE);
            char byte;
            if(!file||Read(file,&byte,1)!=1||byte!='O')error=EIO;
            if(file)Close(file);
        }
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous)error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    c->result=error;PutMsg(c->port,&c->done);return NULL;
}

int oz_dos_property_test(struct oz_zpl *fs)
{
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(!marker)return 0;
    fclose(marker);
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    struct oz_mount *mount=NULL;
    int error=oz_dos_mount(fs,"OZPROP",&mount);
    if(!error) {
        struct client c={0};c.port=done;c.secure=!oz_zpl_readonly(fs);
        c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
        pthread_t thread;error=pthread_create(&thread,NULL,property_client,&c);
        if(!error) {
            do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}
                while(!GetMsg(done));
            pthread_join(thread,NULL);error=c.result;
        }
    }
    if(mount){int close=oz_dos_unmount(mount);if(!error)error=close;}
    DeleteMsgPort(done);
    char text[100];snprintf(text,sizeof(text),"Native DOS SetProtection readonly=%d result=%d\n",
        oz_zpl_readonly(fs),error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS SetFileDate readonly=%d result=%d\n",oz_zpl_readonly(fs),error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS owner property restoration readonly=%d result=%d\n",oz_zpl_readonly(fs),error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return error;
}

static int delete_client_checks(void)
{
    const char *dir="OZWRITE:delete-test",*name="OZWRITE:delete-test/file";
    int error=0;
    BPTR lock=CreateDir((CONST_STRPTR)dir);
    if(!lock)return EIO;
    if(DeleteFile((CONST_STRPTR)dir)||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
    UnLock(lock);
    BPTR file=Open((CONST_STRPTR)name,MODE_READWRITE);
    if(!file)return EIO;
    if(Write(file,"delete",6)!=6)error=EIO;
    if(DeleteFile((CONST_STRPTR)name)||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
    if(!Close(file))error=EIO;
    if(!error&&!SetProtection((CONST_STRPTR)name,FIBF_DELETE))error=EIO;
    if(!error&&(DeleteFile((CONST_STRPTR)name)||IoErr()!=ERROR_DELETE_PROTECTED))error=EIO;
    if(!error&&!SetProtection((CONST_STRPTR)name,0))error=EIO;
    if(!error&&(DeleteFile((CONST_STRPTR)dir)||IoErr()!=ERROR_DIRECTORY_NOT_EMPTY))error=EIO;
    if(!error) {
        lock=Lock((CONST_STRPTR)name,SHARED_LOCK);
        if(!lock)error=EIO;
        if(!error&&(DeleteFile((CONST_STRPTR)name)||IoErr()!=ERROR_OBJECT_IN_USE))error=EIO;
        if(lock)UnLock(lock);
    }
    if(!error&&!DeleteFile((CONST_STRPTR)name))error=EIO;
    if(!error&&(DeleteFile((CONST_STRPTR)name)||IoErr()!=ERROR_OBJECT_NOT_FOUND))error=EIO;
    if(!error&&!DeleteFile((CONST_STRPTR)dir))error=EIO;
    return error;
}

static int rename_client_checks(void)
{
    const char *old="OZWRITE:move-old",*moved="OZWRITE:made-by-reader/moved";
    int error=0;
    BPTR file=Open((CONST_STRPTR)old,MODE_READWRITE);
    if(!file)return EIO;
    if(Write(file,"rename",6)!=6)error=EIO;
    BPTR lock=Lock((CONST_STRPTR)old,SHARED_LOCK);
    if(!lock)error=EIO;
    if(!error&&!Rename((CONST_STRPTR)old,(CONST_STRPTR)moved))error=EIO;
    if(!error) {
        struct FileInfoBlock fib;char path[256];
        if(!Examine(lock,&fib)||strcmp((char *)fib.fib_FileName,"moved")||
            !NameFromLock(lock,(STRPTR)path,sizeof(path))||!strstr(path,"made-by-reader/moved"))error=EIO;
        char bytes[6];
        if(Seek(file,0,OFFSET_BEGINNING)!=6||Read(file,bytes,6)!=6||memcmp(bytes,"rename",6))error=EIO;
    }
    if(lock)UnLock(lock);
    if(!Close(file))error=EIO;
    if(!error&&(Rename((CONST_STRPTR)moved,(CONST_STRPTR)"OZWRITE:made-by-reader/inside")||
        IoErr()!=ERROR_OBJECT_EXISTS))error=EIO;
    if(!error&&(Rename((CONST_STRPTR)"OZWRITE:hello.txt",(CONST_STRPTR)"OZWRITE:must-not-rename")||
        IoErr()!=ERROR_WRITE_PROTECTED))error=EIO;
    BPTR directory=0;
    if(!error)directory=CreateDir((CONST_STRPTR)"OZWRITE:rename-dir");
    if(!directory)error=EIO;
    if(!error) {
        file=Open((CONST_STRPTR)"OZWRITE:rename-dir/payload",MODE_READWRITE);
        if(!file||Write(file,"contents",8)!=8)error=EIO;
        if(file&&!Close(file))error=EIO;
    }
    if(!error&&!Rename((CONST_STRPTR)"OZWRITE:rename-dir",
        (CONST_STRPTR)"OZWRITE:made-by-reader/relocated"))error=EIO;
    if(directory)UnLock(directory);
    if(!error&&Rename((CONST_STRPTR)"OZWRITE:made-by-reader",
        (CONST_STRPTR)"OZWRITE:made-by-reader/relocated/cycle"))error=EIO;
    if(!error) {
        char longname[192];const char *prefix="OZWRITE:made-by-reader/";
        size_t prefixsize=strlen(prefix);memcpy(longname,prefix,prefixsize);
        memset(longname+prefixsize,'q',120);longname[prefixsize+120]=0;
        file=Open((CONST_STRPTR)longname,MODE_READWRITE);
        lock=Lock((CONST_STRPTR)longname,SHARED_LOCK);
        struct FileInfoBlock fib;
        if(!file||!lock)error=EIO;
        if(!error&&(Examine(lock,&fib)||IoErr()!=ERROR_LINE_TOO_LONG))error=EIO;
        /* Native ExAll must not pass the long name through a classic FIB.
         * Use the reader-owned directory: root has intentionally unreadable
         * metadata fixtures and must continue to reject enumeration there. */
        BPTR root=Lock((CONST_STRPTR)"OZWRITE:made-by-reader",SHARED_LOCK);
        struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
        union {IPTR alignment;unsigned char bytes[512];} storage;
        char pattern[64];
        if(!root||!ctl)error=EIO;
        if(!error) {
            if(ParsePatternNoCase((CONST_STRPTR)"q#?",(STRPTR)pattern,sizeof(pattern))<0)error=EIO;
            ctl->eac_MatchString=(UBYTE *)pattern;
            struct FileLock *fl=BADDR(root);
            SIPTR answer=DoPkt(fl->fl_Task,ACTION_EXAMINE_ALL,(SIPTR)root,
                (IPTR)storage.bytes,sizeof(storage.bytes),ED_OWNER,(IPTR)ctl);
            struct ExAllData *entry=(void *)storage.bytes;
            LONG enum_error=IoErr();
            if(answer||enum_error!=ERROR_NO_MORE_ENTRIES||ctl->eac_Entries!=1) {
                char diagnostic[160];snprintf(diagnostic,sizeof(diagnostic),
                    "Native ExAll long-name reply=%ld error=%ld entries=%lu key=%lu\n",
                    (long)answer,(long)enum_error,(unsigned long)ctl->eac_Entries,
                    (unsigned long)ctl->eac_LastKey);
                for(char *p=diagnostic;*p;p++)RawPutChar(*p);
            }
            if(answer||enum_error!=ERROR_NO_MORE_ENTRIES||ctl->eac_Entries!=1||
                strcmp((char *)entry->ed_Name,longname+prefixsize)||entry->ed_Next)error=EIO;
            if(!DoPkt(fl->fl_Task,ACTION_EXAMINE_ALL_END,(SIPTR)root,
                (IPTR)storage.bytes,sizeof(storage.bytes),ED_OWNER,(IPTR)ctl)||
                ctl->eac_LastKey||ctl->eac_Entries)error=EIO;
        }
        if(ctl)FreeDosObject(DOS_EXALLCONTROL,ctl);
        if(root)UnLock(root);
        if(!error&&!Rename((CONST_STRPTR)longname,(CONST_STRPTR)"OZWRITE:long-renamed"))error=EIO;
        if(!error&&(!Examine(lock,&fib)||strcmp((char *)fib.fib_FileName,"long-renamed")))error=EIO;
        if(lock)UnLock(lock);
        if(file&&!Close(file))error=EIO;
        if(!error&&!DeleteFile((CONST_STRPTR)"OZWRITE:long-renamed"))error=EIO;
    }
    return error;
}

static int replacement_client_checks(void)
{
    const char *name="OZWRITE:replacement";
    int error=0;char bytes[8];
    BPTR old=Open((CONST_STRPTR)name,MODE_READWRITE);
    if(!old)return EIO;
    if(Write(old,"old",3)!=3)error=EIO;
    BPTR other=Open((CONST_STRPTR)name,MODE_NEWFILE);
    if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
    if(other)Close(other);
    if(Seek(old,0,OFFSET_BEGINNING)!=3||Read(old,bytes,8)!=3||memcmp(bytes,"old",3))error=EIO;
    if(!Close(old))error=EIO;
    BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK);
    if(!lock)error=EIO;
    if(!error) {
        other=Open((CONST_STRPTR)name,MODE_NEWFILE);
        if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(other)Close(other);
    }
    if(lock)UnLock(lock);
    if(!error&&!SetProtection((CONST_STRPTR)name,FIBF_DELETE))error=EIO;
    if(!error) {
        other=Open((CONST_STRPTR)name,MODE_NEWFILE);
        if(other||IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
        if(other)Close(other);
    }
    if(!error&&!SetProtection((CONST_STRPTR)name,0))error=EIO;
    BPTR file=0;
    if(!error)file=Open((CONST_STRPTR)name,MODE_NEWFILE);
    if(!file)error=EIO;
    if(file) {
        if(Read(file,bytes,1)!=0||Write(file,"fresh",5)!=5)error=EIO;
        const LONG modes[]={MODE_OLDFILE,MODE_READWRITE,MODE_NEWFILE};
        for(unsigned i=0;i<3&&!error;i++) {
            other=Open((CONST_STRPTR)name,modes[i]);
            if(other||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
            if(other)Close(other);
        }
        lock=Lock((CONST_STRPTR)name,SHARED_LOCK);
        if(lock||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(lock)UnLock(lock);
        if(Rename((CONST_STRPTR)name,(CONST_STRPTR)"OZWRITE:must-not-replace-move")||
            IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(SetProtection((CONST_STRPTR)name,0)||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(DeleteFile((CONST_STRPTR)name)||IoErr()!=ERROR_OBJECT_IN_USE)error=EIO;
        if(!Close(file))error=EIO;
    }
    if(!error) {
        file=Open((CONST_STRPTR)"OZWRITE:brand-new",MODE_NEWFILE);
        if(!file||Write(file,"new",3)!=3)error=EIO;
        if(file&&!Close(file))error=EIO;
    }
    return error;
}

static int write_only_client_checks(struct Library *secBase)
{
    const char *name="OZWRITE:write-only";
    BPTR file=Open((CONST_STRPTR)name,MODE_READWRITE);
    if(!file)return EIO;
    int error=Write(file,"before",6)==6?0:EIO;
    if(!Close(file))error=EIO;
    /* SetProtection preserves group bits under secLimitDOSSetProtection.
     * Use the explicit API so inherited GROUP_READ cannot grant the read. */
    if(!error&&!secSetProtection((CONST_STRPTR)name,FIBF_READ))error=EIO;
    if(!error) {
        file=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(file||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
        if(file)Close(file);
    }
    file=0;
    if(!error)file=Open((CONST_STRPTR)name,MODE_READWRITE);
    if(!file)error=EIO;
    if(file) {
        char byte=0x5a;
        if(Read(file,&byte,1)!=-1||IoErr()!=ERROR_READ_PROTECTED||byte!=0x5a)error=EIO;
        if(Seek(file,0,OFFSET_END)!=0||Write(file,"!",1)!=1)error=EIO;
        if(Seek(file,0,OFFSET_BEGINNING)!=7||Write(file,"after!",6)!=6)error=EIO;
        if(!Close(file))error=EIO;
    }
    if(!error&&!SetProtection((CONST_STRPTR)name,0))error=EIO;
    file=0;
    if(!error)file=Open((CONST_STRPTR)name,MODE_OLDFILE);
    if(!file)error=EIO;
    if(file) {
        char bytes[8];
        if(Read(file,bytes,sizeof(bytes))!=7||memcmp(bytes,"after!!",7))error=EIO;
        /* Reauthorize a held handle: neither READ nor WRITE permits Seek. */
        if(!SetProtection((CONST_STRPTR)name,FIBF_READ|FIBF_WRITE)||
            Seek(file,0,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
        if(!Close(file))error=EIO;
    }
    if(!error&&!SetProtection((CONST_STRPTR)name,0))error=EIO;
    if(!error&&!DeleteFile((CONST_STRPTR)name))error=EIO;
    char message[100];snprintf(message,sizeof(message),
        "Native DOS write-only open/seek/read denial=%d\n",error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}

static SIPTR file_packet64(BPTR file,LONG action,int64_t offset,LONG mode)
{
    struct FileHandle *fh=BADDR(file);
    return DoPkt(fh->fh_Type,action,fh->fh_Arg1,(SIPTR)offset,mode,0,0);
}
static int sparse64_client_checks(int write)
{
    const uint64_t boundary=1ULL<<32,size=boundary+4096;
    const char marker[]="AROS64-boundary!";
    const size_t length=sizeof(marker)-1;
    BPTR file=Open((CONST_STRPTR)"OZWRITE:sparse64",write?MODE_READWRITE:MODE_OLDFILE);
    int error=file?0:EIO;
    if(file) {
        if(write) {
            if(!file_packet64(file,ACTION_CHANGE_FILE_SIZE64,size,OFFSET_BEGINNING))error=EIO;
            if(!error&&!file_packet64(file,ACTION_CHANGE_FILE_POSITION64,boundary-8,OFFSET_BEGINNING))error=EIO;
            if(!error&&Write(file,marker,length)!=(LONG)length)error=EIO;
        } else if(file_packet64(file,ACTION_CHANGE_FILE_SIZE64,size,OFFSET_BEGINNING)||
            IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
        if(!error&&file_packet64(file,ACTION_GET_FILE_SIZE64,0,0)!=(SIPTR)size)error=EIO;
        if(!error&&!file_packet64(file,ACTION_CHANGE_FILE_POSITION64,boundary-16,OFFSET_BEGINNING))error=EIO;
        unsigned char bytes[32];memset(bytes,0xa5,sizeof(bytes));
        if(!error&&Read(file,bytes,sizeof(bytes))!=sizeof(bytes))error=EIO;
        if(!error&&memcmp(bytes+8,marker,length))error=EIO;
        for(unsigned i=0;i<sizeof(bytes)&&!error;i++)
            if((i<8||i>=8+length)&&bytes[i])error=EIO;
        uint64_t position=boundary+16;
        if(!error&&file_packet64(file,ACTION_GET_FILE_POSITION64,0,0)!=(SIPTR)position)error=EIO;
        if(!error&&(file_packet64(file,ACTION_CHANGE_FILE_POSITION64,INT64_MIN,OFFSET_CURRENT)||
            IoErr()!=ERROR_SEEK_ERROR))error=EIO;
        if(!error&&(file_packet64(file,ACTION_CHANGE_FILE_POSITION64,1,OFFSET_END)||
            IoErr()!=ERROR_SEEK_ERROR))error=EIO;
        if(!error&&(Seek(file,0,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_OBJECT_TOO_LARGE))error=EIO;
        if(!error&&file_packet64(file,ACTION_GET_FILE_POSITION64,0,0)!=(SIPTR)position)error=EIO;
        if(write&&!error&&(file_packet64(file,ACTION_CHANGE_FILE_SIZE64,INT64_MAX,OFFSET_END)||
            IoErr()!=ERROR_OBJECT_TOO_LARGE))error=EIO;
        if(!error&&file_packet64(file,ACTION_GET_FILE_SIZE64,0,0)!=(SIPTR)size)error=EIO;
        struct FileInfoBlock fib;
        if(!error&&(ExamineFH(file,&fib)||IoErr()!=ERROR_OBJECT_TOO_LARGE))error=EIO;
        if(!error&&!file_packet64(file,ACTION_CHANGE_FILE_POSITION64,-1,OFFSET_END))error=EIO;
        unsigned char end=0xff;
        if(!error&&(Read(file,&end,1)!=1||end||Read(file,&end,1)!=0))error=EIO;
        if(!Close(file))error=EIO;
    }
    char message[100];snprintf(message,sizeof(message),"Native DOS sparse64 write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int owner_client_checks(int write,struct Library *secBase)
{
    const char *name="OZWRITE:owner-test";
    int error=0;
    if(write) {
        BPTR file=Open((CONST_STRPTR)name,MODE_NEWFILE);
        if(!file)return EIO;
        if(Write(file,"owner",5)!=5)error=EIO;
        if(!Close(file))error=EIO;
        const ULONG owners[]={0,secOWNER_SYSTEM,(65534UL<<16)|65534,(77UL<<16)|88};
        for(unsigned i=0;i<4&&!error;i++) {
            if(!SetOwner((STRPTR)name,owners[i]))error=EIO;
            BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK);struct FileInfoBlock fib;
            if(!lock||!Examine(lock,&fib)||fib.fib_OwnerUID!=(owners[i]>>16)||
                fib.fib_OwnerGID!=(owners[i]&0xffff))error=EIO;
            if(lock)UnLock(lock);
        }
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        if(!error) {
            if(secLoginA(login)!=((77UL<<16)|88))error=EACCES;
            else {
                if(!SetOwner((STRPTR)name,(77UL<<16)|99))error=EIO;
                const ULONG denied[]={(55UL<<16)|66,secOWNER_SYSTEM,(77UL<<16)|66};
                for(unsigned i=0;i<3;i++)
                    if(SetOwner((STRPTR)name,denied[i])||IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
                if(SetOwner((STRPTR)"OZWRITE:hello.txt",(77UL<<16)|88)||
                    IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
                struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
                if(secLogoutA(logout)!=previous)error=EIO;
            }
        }
        if(!error&&(SetOwner((STRPTR)"OZWRITE:missing-owner-test",secOWNER_SYSTEM)||
            IoErr()!=ERROR_OBJECT_NOT_FOUND))error=EIO;
    } else if(SetOwner((STRPTR)name,secOWNER_SYSTEM)||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
    BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK);struct FileInfoBlock fib;
    if(!lock||!Examine(lock,&fib)||fib.fib_OwnerUID!=77||fib.fib_OwnerGID!=99)error=EIO;
    if(lock)UnLock(lock);
    BPTR file=Open((CONST_STRPTR)name,MODE_OLDFILE);char bytes[6];
    if(!file||Read(file,bytes,6)!=5||memcmp(bytes,"owner",5))error=EIO;
    if(file&&!Close(file))error=EIO;
    char message[100];snprintf(message,sizeof(message),"Native DOS SetOwner write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int comment_client_checks(int write,struct Library *secBase)
{
    const char *name="OZWRITE:comment-test";
    char maximum[80];memset(maximum,'C',79);maximum[79]=0;
    int error=0;
    if(write) {
        BPTR file=Open((CONST_STRPTR)name,MODE_READWRITE);
        if(!file)return EIO;
        if(!Close(file))error=EIO;
        const char *values[]={"first comment",maximum,"","persistent AROS comment"};
        for(unsigned i=0;i<4&&!error;i++) {
            if(!SetComment((CONST_STRPTR)name,(CONST_STRPTR)values[i]))error=EIO;
            BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK);struct FileInfoBlock fib;
            if(!lock||!Examine(lock,&fib)||strcmp((char *)fib.fib_Comment,values[i]))error=EIO;
            if(lock)UnLock(lock);
        }
        char oversized[81];memset(oversized,'X',80);oversized[80]=0;
        if(!error&&(SetComment((CONST_STRPTR)name,(CONST_STRPTR)oversized)||IoErr()!=ERROR_COMMENT_TOO_BIG))error=EIO;
        if(!error&&(SetComment((CONST_STRPTR)"OZWRITE:missing-comment",(CONST_STRPTR)"no")||
            IoErr()!=ERROR_OBJECT_NOT_FOUND))error=EIO;
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        if(!error) {
            if(secLoginA(login)!=((77UL<<16)|88))error=EACCES;
            else {
                if(SetComment((CONST_STRPTR)name,(CONST_STRPTR)"not owner")||IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
                if(!SetComment((CONST_STRPTR)"OZWRITE:owner-test",(CONST_STRPTR)"reader comment"))error=EIO;
                struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
                if(secLogoutA(logout)!=previous)error=EIO;
            }
        }
    } else if(SetComment((CONST_STRPTR)name,(CONST_STRPTR)"readonly")||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
    const char *paths[]={name,"OZWRITE:owner-test"};
    const char *comments[]={"persistent AROS comment","reader comment"};
    for(unsigned i=0;i<2&&!error;i++) {
        BPTR file=Open((CONST_STRPTR)paths[i],MODE_OLDFILE);struct FileInfoBlock fib;
        if(!file||!ExamineFH(file,&fib)||strcmp((char *)fib.fib_Comment,comments[i]))error=EIO;
        if(file&&!Close(file))error=EIO;
    }
    char message[100];snprintf(message,sizeof(message),"Native DOS SetComment write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int alias_metadata(BPTR lock,const char *name,const char *parentname)
{
    struct FileInfoBlock fib;char path[256];
    if(!Examine(lock,&fib)||strcmp((char *)fib.fib_FileName,name)||
        !NameFromLock(lock,(STRPTR)path,sizeof(path))||!strstr(path,name))return EIO;
    BPTR parent=ParentDir(lock);int error=0;
    if(!parent||!Examine(parent,&fib)||strcmp((char *)fib.fib_FileName,parentname))error=EIO;
    if(parent)UnLock(parent);
    return error;
}
static int alias_handle_metadata(BPTR file,const char *name,const char *parentname)
{
    struct FileInfoBlock fib;int error=0;
    if(!ExamineFH(file,&fib)||strcmp((char *)fib.fib_FileName,name))error=EIO;
    BPTR copy=DupLockFromFH(file),parent=ParentOfFH(file);
    if(!copy||alias_metadata(copy,name,parentname))error=EIO;
    if(!parent||!Examine(parent,&fib)||strcmp((char *)fib.fib_FileName,parentname))error=EIO;
    if(copy)UnLock(copy);
    if(parent)UnLock(parent);
    return error;
}
static int hardlink_client_checks(int write,struct Library *secBase)
{
    const char *source="OZWRITE:hardlink-source",*alias="OZWRITE:hardlink-alias";
    int error=0;BPTR lock=0,file=0;
    if(write) {
        file=Open((CONST_STRPTR)source,MODE_READWRITE);
        if(!file||Write(file,"linkdata",8)!=8)error=EIO;
        if(file&&!Close(file))error=EIO;
        lock=Lock((CONST_STRPTR)source,SHARED_LOCK);
        if(!lock)error=EIO;
        if(!error&&!MakeLink((CONST_STRPTR)alias,(SIPTR)lock,FALSE))error=EIO;
        if(!error&&(MakeLink((CONST_STRPTR)alias,(SIPTR)lock,FALSE)||IoErr()!=ERROR_OBJECT_EXISTS))error=EIO;
        BPTR other=Lock((CONST_STRPTR)alias,SHARED_LOCK);
        if(!other||SameLock(lock,other)!=LOCK_SAME)error=EIO;
        if(!error&&(alias_metadata(lock,"hardlink-source","OZWRITE")||
            alias_metadata(other,"hardlink-alias","OZWRITE")))error=EIO;
        const char *cross="OZWRITE:subdir/hardlink-alias";
        if(!error&&!MakeLink((CONST_STRPTR)cross,(SIPTR)lock,FALSE))error=EIO;
        BPTR crosslock=Lock((CONST_STRPTR)cross,SHARED_LOCK),crossfile=0;
        if(!crosslock||alias_metadata(crosslock,"hardlink-alias","subdir"))error=EIO;
        BPTR copy=crosslock?DupLock(crosslock):0;
        if(!copy)error=EIO;
        if(copy) {
            crossfile=OpenFromLock(copy);
            if(!crossfile){UnLock(copy);error=EIO;}
            else if(alias_handle_metadata(crossfile,"hardlink-alias","subdir"))error=EIO;
        }
        if(!error&&!Rename((CONST_STRPTR)cross,(CONST_STRPTR)"OZWRITE:hardlink-moved"))error=EIO;
        if(!error&&(alias_metadata(crosslock,"hardlink-moved","OZWRITE")||
            alias_handle_metadata(crossfile,"hardlink-moved","OZWRITE")||
            alias_metadata(other,"hardlink-alias","OZWRITE")||
            alias_metadata(lock,"hardlink-source","OZWRITE")))error=EIO;
        if(crossfile&&!Close(crossfile))error=EIO;
        if(crosslock)UnLock(crosslock);
        if(other)UnLock(other);
        if(lock)UnLock(lock);
        lock=0;
        file=Open((CONST_STRPTR)alias,MODE_READWRITE);
        if(!file||Write(file,"LINK",4)!=4)error=EIO;
        if(file&&!Close(file))error=EIO;
        file=Open((CONST_STRPTR)source,MODE_OLDFILE);
        char bytes[8];
        if(!file||Read(file,bytes,8)!=8||memcmp(bytes,"LINKdata",8))error=EIO;
        if(file&&!Close(file))error=EIO;
        if(!error&&!DeleteFile((CONST_STRPTR)source))error=EIO;
        BPTR dir=Lock((CONST_STRPTR)"OZWRITE:subdir",SHARED_LOCK);
        if(!dir||MakeLink((CONST_STRPTR)"OZWRITE:hardlink-dir",(SIPTR)dir,FALSE)||
            IoErr()!=ERROR_OBJECT_WRONG_TYPE)error=EIO;
        if(dir)UnLock(dir);
        BPTR foreign=Lock((CONST_STRPTR)"RAM:",SHARED_LOCK);
        if(!foreign||MakeLink((CONST_STRPTR)"OZWRITE:hardlink-foreign",(SIPTR)foreign,FALSE)||
            IoErr()!=ERROR_INVALID_LOCK)error=EIO;
        if(foreign)UnLock(foreign);
        lock=Lock((CONST_STRPTR)alias,SHARED_LOCK);
        if(!lock)error=EIO;
        ULONG previous=secGetTaskOwner(FindTask(NULL));
        struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        if(!secLoginA(login))error=EACCES;
        else {
            if(MakeLink((CONST_STRPTR)"OZWRITE:hardlink-denied",(SIPTR)lock,FALSE)||
                IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
            struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
            if(secLogoutA(logout)!=previous)error=EIO;
        }
        if(lock)UnLock(lock);
        lock=0;
    }
    lock=Lock((CONST_STRPTR)alias,SHARED_LOCK);
    if(!lock)error=EIO;
    if(lock&&alias_metadata(lock,"hardlink-alias","OZWRITE"))error=EIO;
    BPTR moved=Lock((CONST_STRPTR)"OZWRITE:hardlink-moved",SHARED_LOCK);
    if(!moved||alias_metadata(moved,"hardlink-moved","OZWRITE")||SameLock(lock,moved)!=LOCK_SAME)error=EIO;
    if(moved)UnLock(moved);
    if(!write&&lock&&(MakeLink((CONST_STRPTR)"OZWRITE:hardlink-readonly",(SIPTR)lock,FALSE)||
        IoErr()!=ERROR_DISK_WRITE_PROTECTED))error=EIO;
    if(lock)UnLock(lock);
    file=Open((CONST_STRPTR)alias,MODE_OLDFILE);char bytes[8];
    if(!file||Read(file,bytes,8)!=8||memcmp(bytes,"LINKdata",8))error=EIO;
    if(file&&!Close(file))error=EIO;
    file=Open((CONST_STRPTR)source,MODE_OLDFILE);
    if(file||IoErr()!=ERROR_OBJECT_NOT_FOUND)error=EIO;
    if(file)Close(file);
    char message[96];snprintf(message,sizeof(message),"Native DOS hardlink write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int symlink_client_checks(int write)
{
    const char *names[]={"OZWRITE:symlink-file","OZWRITE:subdir/symlink-up",
        "OZWRITE:symlink-dir","OZWRITE:symlink-dangling","OZWRITE:symlink-loop"};
    const char *targets[]={"hardlink-alias","/hardlink-alias","subdir","absent-link-target","symlink-loop"};
    int error=0;
    if(write)for(unsigned i=0;i<5&&!error;i++)
        if(!MakeLink((CONST_STRPTR)names[i],(SIPTR)targets[i],TRUE))error=EIO;
    if(write&&!error&&(MakeLink((CONST_STRPTR)names[0],(SIPTR)targets[0],TRUE)||
        IoErr()!=ERROR_OBJECT_EXISTS))error=EIO;
    if(write&&!error) {
        const char *target="OZWRITE:soft-output-target";
        if(!MakeLink((CONST_STRPTR)"OZWRITE:symlink-output",(SIPTR)target,TRUE))error=EIO;
        BPTR file=Open((CONST_STRPTR)"OZWRITE:symlink-output",MODE_NEWFILE);
        if(!file||Write(file,"rewrite",7)!=7)error=EIO;
        if(file&&!Close(file))error=EIO;
        file=Open((CONST_STRPTR)"OZWRITE:symlink-output",MODE_NEWFILE);
        if(!file||Write(file,"updated",7)!=7)error=EIO;
        if(file&&!Close(file))error=EIO;
        if(!DeleteFile((CONST_STRPTR)names[0])||
            !MakeLink((CONST_STRPTR)names[0],(SIPTR)targets[0],TRUE))error=EIO;
    }
    if(!write&&(MakeLink((CONST_STRPTR)"OZWRITE:symlink-readonly",(SIPTR)targets[0],TRUE)||
        IoErr()!=ERROR_DISK_WRITE_PROTECTED))error=EIO;
    for(unsigned i=0;i<2&&!error;i++) {
        BPTR file=Open((CONST_STRPTR)names[i],MODE_OLDFILE);char bytes[8];
        if(!file||Read(file,bytes,8)!=8||memcmp(bytes,"LINKdata",8))error=EIO;
        if(file&&!Close(file))error=EIO;
    }
    if(!error) {
        BPTR file=Open((CONST_STRPTR)"OZWRITE:symlink-dir/nested.txt",MODE_OLDFILE);char bytes[7];
        if(!file||Read(file,bytes,7)!=7||memcmp(bytes,"OpenZFS",7))error=EIO;
        if(file&&!Close(file))error=EIO;
        BPTR dir=Lock((CONST_STRPTR)names[2],SHARED_LOCK);struct FileInfoBlock fib;
        if(!dir||!Examine(dir,&fib)||strcmp((char *)fib.fib_FileName,"subdir"))error=EIO;
        if(dir)UnLock(dir);
    }
    if(!error) {
        BPTR file=Open((CONST_STRPTR)names[3],MODE_OLDFILE);
        if(file||IoErr()!=ERROR_OBJECT_NOT_FOUND)error=EIO;
        if(file)Close(file);
        file=Open((CONST_STRPTR)names[4],MODE_OLDFILE);
        if(file||IoErr()!=ERROR_TOO_MANY_LEVELS)error=EIO;
        if(file)Close(file);
    }
    struct DevProc *device=GetDeviceProc((CONST_STRPTR)"OZWRITE:",NULL);
    if(!device)error=EIO;
    else {
        char result[128];memset(result,0x5a,sizeof(result));
        if(ReadLink(device->dvp_Port,0,(CONST_STRPTR)names[0],(STRPTR)result,3)!=-2||result[0]!=0x5a)error=EIO;
        LONG n=ReadLink(device->dvp_Port,0,(CONST_STRPTR)names[0],(STRPTR)result,sizeof(result));
        if(n!=(LONG)strlen("OZWRITE:hardlink-alias")||strcmp(result,"OZWRITE:hardlink-alias"))error=EIO;
        n=ReadLink(device->dvp_Port,0,(CONST_STRPTR)"OZWRITE:symlink-output",(STRPTR)result,sizeof(result));
        if(n!=(LONG)strlen("OZWRITE:soft-output-target")||strcmp(result,"OZWRITE:soft-output-target"))error=EIO;
        FreeDeviceProc(device);
    }
    BPTR output=Open((CONST_STRPTR)"OZWRITE:symlink-output",MODE_OLDFILE);char contents[7];
    if(!output||Read(output,contents,7)!=7||memcmp(contents,"updated",7))error=EIO;
    if(output&&!Close(output))error=EIO;
    BPTR root=Lock((CONST_STRPTR)"OZWRITE:",SHARED_LOCK);
    struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
    union {IPTR alignment;unsigned char bytes[512];} storage;
    char pattern[64];
    if(!root||!ctl)error=EIO;
    else {
        if(ParsePatternNoCase((CONST_STRPTR)"symlink-file",(STRPTR)pattern,sizeof(pattern))<0)error=EIO;
        ctl->eac_MatchString=(UBYTE *)pattern;
        BOOL more=ExAll(root,(void *)storage.bytes,sizeof(storage.bytes),ED_TYPE,ctl);
        struct ExAllData *entry=(void *)storage.bytes;
        if(more||IoErr()!=ERROR_NO_MORE_ENTRIES||ctl->eac_Entries!=1||
            entry->ed_Type!=ST_SOFTLINK||strcmp((char *)entry->ed_Name,"symlink-file"))error=EIO;
    }
    if(ctl)FreeDosObject(DOS_EXALLCONTROL,ctl);
    if(root)UnLock(root);
    char message[100];snprintf(message,sizeof(message),"Native DOS symlink paths/loops write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int symlink_boundary_checks(int write)
{
    int error=0,stage=1;
    const char *cross="OZWRITE:symlink-cross-volume",*ram="RAM:oz-openzfs-cross-target";
    char target[701],expected[800],output[1024];
    for(unsigned i=0;i<700;i++)target[i]='a'+i%26;
    target[700]=0;
    snprintf(expected,sizeof(expected),"OZWRITE:%s/suffix",target);
    size_t needed=strlen(expected);
    if(write) {
        if(!MakeLink((CONST_STRPTR)cross,(SIPTR)ram,TRUE)||
            !MakeLink((CONST_STRPTR)"OZWRITE:symlink-long",(SIPTR)target,TRUE)||
            !MakeLink((CONST_STRPTR)"OZWRITE:symlink-ram-root",(SIPTR)"RAM:",TRUE)||
            !MakeLink((CONST_STRPTR)"OZWRITE:symlink-dir-slash",(SIPTR)"subdir/",TRUE))error=EIO;
    }
    if(error)goto done;
    stage=2;
    /* RAM belongs to this disposable guest, and is reconstructed each boot;
     * no host files, user session or hardware volume is involved. */
    BPTR file=Open((CONST_STRPTR)ram,MODE_NEWFILE);
    if(!file||Write(file,"cross-volume",12)!=12)error=EIO;
    if(file&&!Close(file))error=EIO;
    if(error)goto done;
    stage=3;
    file=Open((CONST_STRPTR)cross,MODE_OLDFILE);
    char bytes[12];
    if(!file||Read(file,bytes,12)!=12||memcmp(bytes,"cross-volume",12))error=EIO;
    if(file&&!Close(file))error=EIO;
    if(error)goto done;
    stage=4;
    BPTR via=Lock((CONST_STRPTR)cross,SHARED_LOCK),direct=Lock((CONST_STRPTR)ram,SHARED_LOCK);
    if(!via||!direct||SameLock(via,direct)!=LOCK_SAME)error=EIO;
    if(via)UnLock(via);
    if(direct)UnLock(direct);
    if(error)goto done;
    stage=5;
    struct DevProc *device=GetDeviceProc((CONST_STRPTR)"OZWRITE:",NULL);
    if(!device)error=EIO;
    else {
        const char *path="OZWRITE:symlink-long/suffix";
        memset(output,0x5a,sizeof(output));
        if(ReadLink(device->dvp_Port,0,(CONST_STRPTR)path,(STRPTR)output,needed)!=-2)error=EIO;
        for(unsigned i=0;i<sizeof(output);i++)if(output[i]!=0x5a)error=EIO;
        LONG n=ReadLink(device->dvp_Port,0,(CONST_STRPTR)path,(STRPTR)output,needed+1);
        if(n!=(LONG)needed||strcmp(output,expected)||output[needed+1]!=0x5a)error=EIO;
        /* Native callers may use the same input and output buffer. */
        snprintf(output,sizeof(output),"%s",path);
        n=ReadLink(device->dvp_Port,0,(CONST_STRPTR)output,(STRPTR)output,sizeof(output));
        if(n!=(LONG)needed||strcmp(output,expected))error=EIO;
        FreeDeviceProc(device);
    }
    if(error)goto done;
    stage=6;
    const char *paths[]={"OZWRITE:symlink-ram-root/oz-openzfs-cross-target",
                         "OZWRITE:symlink-dir-slash/nested.txt"};
    const char *resolved[]={ram,"OZWRITE:subdir/nested.txt"};
    device=GetDeviceProc((CONST_STRPTR)"OZWRITE:",NULL);
    if(!device)error=EIO;
    else {
        for(unsigned i=0;i<2&&!error;i++) {
            LONG n=ReadLink(device->dvp_Port,0,(CONST_STRPTR)paths[i],(STRPTR)output,sizeof(output));
            if(n!=(LONG)strlen(resolved[i])||strcmp(output,resolved[i])){error=EIO;break;}
            via=Lock((CONST_STRPTR)paths[i],SHARED_LOCK);
            direct=Lock((CONST_STRPTR)resolved[i],SHARED_LOCK);
            if(!via||!direct||SameLock(via,direct)!=LOCK_SAME)error=EIO;
            if(via)UnLock(via);
            if(direct)UnLock(direct);
        }
        FreeDeviceProc(device);
    }
    if(error)goto done;
    stage=7;
    if(!DeleteFile((CONST_STRPTR)ram))error=EIO;
done:
    if(error) {
        char diagnostic[96];snprintf(diagnostic,sizeof(diagnostic),"Symlink boundary failed stage=%d ioerr=%ld\n",stage,(long)IoErr());
        for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    }
    char message[100];snprintf(message,sizeof(message),"Native DOS symlink cross-volume/bounds write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static int symlink_permission_checks(int write,struct Library *secBase)
{
    const char *target="OZWRITE:link-private-target",*link="OZWRITE:link-public-name";
    int error=0;
    if(write) {
        BPTR file=Open((CONST_STRPTR)target,MODE_READWRITE);
        if(!file||Write(file,"private",7)!=7)error=EIO;
        if(file&&!Close(file))error=EIO;
        if(!secSetProtection((CONST_STRPTR)target,0)||
            !MakeLink((CONST_STRPTR)link,(SIPTR)target,TRUE)||
            !secSetProtection((CONST_STRPTR)link,FIBF_GRP_READ|FIBF_OTR_READ))error=EIO;
    }
    ULONG root=secGetTaskOwner(FindTask(NULL));
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    int logged=secLoginA(reader)!=0;
    if(!logged)error=EACCES;
    if(logged) {
        struct DevProc *device=GetDeviceProc((CONST_STRPTR)"OZWRITE:",NULL);
        if(!device)error=EIO;
        else {
            char output[128];
            if(ReadLink(device->dvp_Port,0,(CONST_STRPTR)link,(STRPTR)output,sizeof(output))!=(LONG)strlen(target)||
                strcmp(output,target))error=EIO;
            FreeDeviceProc(device);
        }
        BPTR denied=Open((CONST_STRPTR)link,MODE_OLDFILE);
        if(denied||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
        if(denied)Close(denied);
        BPTR deniedlock=Lock((CONST_STRPTR)link,SHARED_LOCK);
        if(deniedlock||IoErr()!=ERROR_READ_PROTECTED)error=EIO;
        if(deniedlock)UnLock(deniedlock);
        if(secLogoutA(logout)!=root)error=EIO;
    }
    if(write&&!error) {
        if(!secSetProtection((CONST_STRPTR)target,FIBF_GRP_READ|FIBF_OTR_READ))error=EIO;
        logged=secLoginA(reader)!=0;
        if(!logged)error=EACCES;
        if(logged) {
            BPTR held=Open((CONST_STRPTR)link,MODE_OLDFILE);char bytes[7];
            if(!held||Read(held,bytes,7)!=7||memcmp(bytes,"private",7)||
                Seek(held,0,OFFSET_BEGINNING)!=7)error=EIO;
            ULONG owner=secGetTaskOwner(FindTask(NULL));
            struct TagItem admin[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
            if(!secLoginA(admin))error=EACCES;
            else {
                if(!secSetProtection((CONST_STRPTR)target,0))error=EIO;
                if(secLogoutA(logout)!=owner)error=EIO;
            }
            if(held) {
                char sentinel=0x5a;
                if(Read(held,&sentinel,1)!=-1||IoErr()!=ERROR_READ_PROTECTED||sentinel!=0x5a)error=EIO;
                if(!Close(held))error=EIO;
            }
            if(secLogoutA(logout)!=root)error=EIO;
        }
    }
    char message[100];snprintf(message,sizeof(message),"Native DOS symlink target authorization write=%d result=%d\n",write,error);
    for(char *p=message;*p;p++)RawPutChar(*p);
    return error;
}
static void *write_client(void *arg)
{
    struct client *c=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int error=(!secBase||!secIsConfigured())?EACCES:0;
    ULONG previous=secBase?secGetTaskOwner(FindTask(NULL)):0;
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    int logged=!error&&secLoginA(login)!=0;
    if(!logged)error=EACCES;
    if(!error)error=sparse64_client_checks(c->secure);
    if(!error)error=owner_client_checks(c->secure,secBase);
    if(!error)error=comment_client_checks(c->secure,secBase);
    if(!error)error=hardlink_client_checks(c->secure,secBase);
    if(!error)error=symlink_client_checks(c->secure);
    if(!error)error=symlink_boundary_checks(c->secure);
    if(!error)error=symlink_permission_checks(c->secure,secBase);
    unsigned char expected[4096],actual[4096];
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(i*37)^(i>>8);
    BPTR file=0,second=0;
    if(!error)file=Open((CONST_STRPTR)"OZWRITE:hello.txt",MODE_OLDFILE);
    if(!file)error=EIO;
    if(!error)second=Open((CONST_STRPTR)"OZWRITE:hello.txt",MODE_OLDFILE);
    if(!second)error=EIO;
    if(!error&&c->secure) {
        if(!SetProtection((CONST_STRPTR)"OZWRITE:hello.txt",0))error=EIO;
        if(!error&&Write(file,expected,2048)!=2048)error=EIO;
        if(!error&&Write(file,expected+2048,2048)!=2048)error=EIO;
        if(!error&&Write(file,expected,0)!=0)error=EIO;
        if(!error&&SetFileSize(file,32,OFFSET_BEGINNING)!=32)error=EIO;
        if(!error&&(Read(second,actual,64)!=32||memcmp(actual,expected,32)))error=EIO;
        if(!error&&(SetFileSize(file,16,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_SEEK_ERROR))error=EIO;
        if(!error&&Seek(second,0,OFFSET_BEGINNING)!=32)error=EIO;
        if(!error&&(SetFileSize(file,-33,OFFSET_END)!=-1||IoErr()!=ERROR_SEEK_ERROR))error=EIO;
        if(!error&&SetFileSize(file,4096-32,OFFSET_CURRENT)!=4096)error=EIO;
        if(!error&&Read(second,actual,sizeof(actual))!=sizeof(actual))error=EIO;
        if(!error&&memcmp(actual,expected,32))error=EIO;
        for(unsigned i=32;i<sizeof(actual)&&!error;i++)if(actual[i])error=EIO;
        if(!error&&Seek(file,0,OFFSET_BEGINNING)!=32)error=EIO;
        if(!error&&Write(file,expected,sizeof(expected))!=sizeof(expected))error=EIO;
        if(!error&&Seek(second,0,OFFSET_BEGINNING)!=4096)error=EIO;
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},
            {secT_Password,(IPTR)""},{TAG_DONE,0}};
        ULONG root=secGetTaskOwner(FindTask(NULL));
        if(!error) {
            if(!secLoginA(reader))error=EACCES;
            else {
                if(Write(file,expected,1)!=-1||IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
                if(SetFileSize(file,0,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_WRITE_PROTECTED)error=EIO;
                ULONG oldprot=secGetDefProtection(FindTask(NULL));
                struct TagItem defaults[]={{secT_DefProtection,FIBF_EXECUTE|FIBF_GRP_READ},{TAG_DONE,0}};
                if(!secSetDefProtectionA(defaults))error=EIO;
                BPTR created=0;
                if(!error)created=Open((CONST_STRPTR)"OZWRITE:subdir/created-by-reader",MODE_READWRITE);
                if(!created)error=EIO;
                if(created) {
                    if(Write(created,expected,8)!=8)error=EIO;
                    if(!Close(created))error=EIO;
                }
                defaults[0].ti_Data=FIBF_GRP_READ;
                if(!secSetDefProtectionA(defaults))error=EIO;
                BPTR directory=0;
                if(!error)directory=CreateDir((CONST_STRPTR)"OZWRITE:made-by-reader");
                if(!directory)error=EIO;
                if(directory)UnLock(directory);
                if(!error) {
                    directory=CreateDir((CONST_STRPTR)"OZWRITE:made-by-reader");
                    if(directory||IoErr()!=ERROR_OBJECT_EXISTS)error=EIO;
                    if(directory)UnLock(directory);
                }
                if(!error) {
                    created=Open((CONST_STRPTR)"OZWRITE:made-by-reader/inside",MODE_READWRITE);
                    if(!created||Write(created,expected,8)!=8)error=EIO;
                    if(created&&!Close(created))error=EIO;
                }
                if(!error)error=delete_client_checks();
                if(!error)error=rename_client_checks();
                if(!error)error=replacement_client_checks();
                if(!error)error=write_only_client_checks(secBase);
                defaults[0].ti_Data=oldprot;
                if(!secSetDefProtectionA(defaults))error=EIO;
                struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
                if(secLogoutA(logout)!=root)error=EIO;
            }
        }
    } else if(!error) {
        if(Write(file,expected,1)!=-1||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
        if(SetFileSize(file,0,OFFSET_BEGINNING)!=-1||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
        BPTR denied=CreateDir((CONST_STRPTR)"OZWRITE:must-not-create");
        if(denied||IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
        if(denied)UnLock(denied);
        if(DeleteFile((CONST_STRPTR)"OZWRITE:made-by-reader/inside")||
            IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
        if(Rename((CONST_STRPTR)"OZWRITE:made-by-reader/moved",(CONST_STRPTR)"OZWRITE:must-not-rename")||
            IoErr()!=ERROR_DISK_WRITE_PROTECTED)error=EIO;
    }
    /* The second handle predates extension. Both Read and SEEK_END must
     * observe the current znode size, not an open-time EOF snapshot. */
    if(!error&&Read(second,actual,sizeof(actual))!=sizeof(actual))error=EIO;
    if(!error&&memcmp(actual,expected,sizeof(actual)))error=EIO;
    if(!error&&Seek(second,0,OFFSET_END)!=sizeof(actual))error=EIO;
    if(!error&&Read(second,actual,1)!=0)error=EIO;
    if(!error&&Seek(second,-1,OFFSET_END)!=sizeof(actual))error=EIO;
    if(!error&&(Read(second,actual,1)!=1||actual[0]!=expected[4095]))error=EIO;
    if(!error) {
        const char *name="OZWRITE:subdir/created-by-reader";
        BPTR created=Open((CONST_STRPTR)name,c->secure?MODE_READWRITE:MODE_OLDFILE);
        if(!created||Read(created,actual,16)!=8||memcmp(actual,expected,8))error=EIO;
        if(created&&!Close(created))error=EIO;
        BPTR lock=Lock((CONST_STRPTR)name,SHARED_LOCK);
        struct FileInfoBlock fib;
        uint32_t protection=FIBF_EXECUTE|FIBF_GRP_READ;
        if(!c->secure)protection|=FIBF_WRITE|FIBF_DELETE;
        if(!lock||!Examine(lock,&fib)||fib.fib_OwnerUID!=77||fib.fib_OwnerGID!=88||
            fib.fib_Size!=8||(uint32_t)fib.fib_Protection!=protection)error=EIO;
        if(lock)UnLock(lock);
    }
    if(second&&!Close(second))error=EIO;
    if(!error) {
        BPTR directory=Lock((CONST_STRPTR)"OZWRITE:made-by-reader",SHARED_LOCK);
        struct FileInfoBlock fib;
        uint32_t protection=FIBF_GRP_READ;
        if(!c->secure)protection|=FIBF_WRITE|FIBF_DELETE;
        if(!directory||!Examine(directory,&fib)||fib.fib_DirEntryType<=0||
            fib.fib_OwnerUID!=77||fib.fib_OwnerGID!=88||
            (uint32_t)fib.fib_Protection!=protection)error=EIO;
        if(directory)UnLock(directory);
        BPTR inside=Open((CONST_STRPTR)"OZWRITE:made-by-reader/inside",MODE_OLDFILE);
        if(!inside||Read(inside,actual,16)!=8||memcmp(actual,expected,8))error=EIO;
        if(inside&&!Close(inside))error=EIO;
    }
    if(file&&!Close(file))error=EIO;
    if(!error) {
        const char *paths[]={"OZWRITE:replacement","OZWRITE:brand-new"};
        const char *values[]={"fresh","new"};
        for(unsigned i=0;i<2&&!error;i++) {
            BPTR fresh=Open((CONST_STRPTR)paths[i],MODE_OLDFILE);
            size_t size=strlen(values[i]);
            if(!fresh||Read(fresh,actual,16)!=(LONG)size||memcmp(actual,values[i],size))error=EIO;
            if(fresh&&!Close(fresh))error=EIO;
            BPTR lock=Lock((CONST_STRPTR)paths[i],SHARED_LOCK);struct FileInfoBlock fib;
            uint32_t protection=FIBF_GRP_READ|(!c->secure?(FIBF_WRITE|FIBF_DELETE):0);
            if(!lock||!Examine(lock,&fib)||fib.fib_OwnerUID!=77||fib.fib_OwnerGID!=88||
                (uint32_t)fib.fib_Protection!=protection)error=EIO;
            if(lock)UnLock(lock);
        }
    }
    if(!error) {
        const char *paths[]={"OZWRITE:made-by-reader/moved","OZWRITE:made-by-reader/relocated/payload"};
        const char *values[]={"rename","contents"};
        for(unsigned i=0;i<2&&!error;i++) {
            BPTR moved=Open((CONST_STRPTR)paths[i],MODE_OLDFILE);
            size_t size=strlen(values[i]);
            if(!moved||Read(moved,actual,16)!=(LONG)size||memcmp(actual,values[i],size))error=EIO;
            if(moved&&!Close(moved))error=EIO;
        }
        const char *absent[]={"OZWRITE:move-old","OZWRITE:rename-dir","OZWRITE:must-not-rename"};
        for(unsigned i=0;i<3&&!error;i++) {
            BPTR missing=Lock((CONST_STRPTR)absent[i],SHARED_LOCK);
            if(missing||IoErr()!=ERROR_OBJECT_NOT_FOUND)error=EIO;
            if(missing)UnLock(missing);
        }
    }
    if(!error) {
        BPTR missing=Lock((CONST_STRPTR)"OZWRITE:delete-test",SHARED_LOCK);
        if(missing||IoErr()!=ERROR_OBJECT_NOT_FOUND)error=EIO;
        if(missing)UnLock(missing);
    }
    if(logged) {
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous)error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    c->result=error;PutMsg(c->port,&c->done);return NULL;
}

#ifdef OZ_DOS_ALLOC_TEST
struct allocation_output_client {
    struct client client;
    unsigned char expected[256];
    size_t length;
    int create_failure;
    char path[64];
};
static void *allocation_output_client_run(void *arg)
{
    struct allocation_output_client *c=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int error=(!secBase||!secIsConfigured())?EACCES:0;
    ULONG previous=secBase?secGetTaskOwner(FindTask(NULL)):0;
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},
        {secT_Password,(IPTR)""},{TAG_DONE,0}};
    int logged=!error&&secLoginA(login)!=0;
    if(!logged)error=EACCES;
    if(!error) {
        BPTR file=Open((CONST_STRPTR)c->path,MODE_NEWFILE);
        if(file||IoErr()!=(c->create_failure?ERROR_DISK_FULL:ERROR_NO_FREE_STORE))error=EIO;
        if(file)Close(file);
        file=Open((CONST_STRPTR)c->path,MODE_OLDFILE);
        unsigned char actual[256],extra;
        if(!file)error=EIO;
        else {
            if(Read(file,actual,c->length)!=(LONG)c->length||
                memcmp(actual,c->expected,c->length)||Read(file,&extra,1)!=0)error=EIO;
            if(!Close(file))error=EIO;
        }
    }
    if(logged) {
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous)error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    c->client.result=error;PutMsg(c->client.port,&c->client.done);return NULL;
}
static int output_allocation_test(struct oz_zpl *fs,struct oz_mount *mount,struct MsgPort *done,int create_failure)
{
    struct allocation_output_client c={0};c.client.port=done;c.create_failure=create_failure;
    const char *leaf=create_failure==3?"post-publish":"hello.txt";
    snprintf(c.path,sizeof(c.path),"OZWRITE:%s",leaf);
    uint64_t object,size,after,after_size;
    int error=0;
    if(create_failure==3) {
        struct oz_zpl_info initial;size_t written=0;
        error=oz_zpl_create_as(fs,oz_zpl_root(fs),leaf,0,kcred,&initial);
        if(!error)error=oz_zpl_write_as(fs,initial.object,0,"original",8,&written,kcred);
        if(!error&&written!=8)error=EIO;
    }
    if(!error)error=oz_zpl_lookup(fs,leaf,&object,&size);
    if(!error&&(!size||size>sizeof(c.expected)))error=EOVERFLOW;
    if(!error)error=oz_zpl_read(fs,object,0,c.expected,size);
    if(error)return error;
    c.length=create_failure==3?0:size;c.client.done.mn_Node.ln_Type=NT_MESSAGE;
    c.client.done.mn_Length=sizeof(c.client.done);
    pthread_t thread;
    if(create_failure==3)oz_dos_test_fail_after_publication();
    else if(create_failure==2)oz_dos_test_fail_publication();
    else if(create_failure)oz_dos_test_fail_creation();else oz_dos_test_fail_allocation(1);
    error=pthread_create(&thread,NULL,allocation_output_client_run,&c);
    if(!error) {
        do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}
            while(!GetMsg(done));
        pthread_join(thread,NULL);error=c.client.result;
    }
    unsigned hits=oz_dos_test_allocation_hits();
    if(hits!=1)error=EIO;
    if(!error)error=oz_zpl_lookup(fs,leaf,&after,&after_size);
    if(!error&&(create_failure==3?(after==object||after_size):(after!=object||after_size!=size)))error=EIO;
    if(!error&&create_failure>=2) {
        char name[256];struct oz_zpl_info entry;
        for(unsigned index=0;;index++) {
            int rc=oz_zpl_nth(fs,oz_zpl_root(fs),index,name,sizeof(name),&entry);
            if(rc==ENOENT)break;
            if(rc){error=rc;break;}
            if(!strncmp(name,".ozreplace-",11)){error=EIO;break;}
        }
    }
    char line[160];snprintf(line,sizeof(line),
        "DOS output %s failure hits=%u result=%d original-preserved=%d\n",
        create_failure==2?"publication":create_failure?"creation":"allocation",hits,error,!error);
    if(create_failure==3) {
        if(!error)error=oz_zpl_remove_as(fs,oz_zpl_root(fs),leaf,0,kcred);
        snprintf(line,sizeof(line),
            "DOS output post-publication failure hits=%u result=%d published-preserved=%d\n",hits,error,!error);
    }
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
#endif

struct replace_guard_test {int calls,deny;};
static int replace_guard_test_check(void *context)
{struct replace_guard_test *test=context;test->calls++;return test->deny?EACCES:0;}
static int replacement_identity_test(struct oz_zpl *fs)
{
    uint64_t root=oz_zpl_root(fs);int error=0;
    for(int linked=0;linked<2&&!error;linked++) {
        struct oz_zpl_info fresh,old,found,bad;size_t written=0;
        struct replace_guard_test check={0};
        error=oz_zpl_create_as(fs,root,"guard-old",0,kcred,&old);
        if(!error)error=oz_zpl_write_as(fs,old.object,0,"original",8,&written,kcred);
        if(!error&&written!=8)error=EIO;
        if(!error)error=oz_zpl_create_as(fs,root,"guard-fresh",0,kcred,&fresh);
        if(!error&&linked)error=oz_zpl_link_as(fs,root,old.object,"guard-alias",kcred);
        if(!error) {
            bad=fresh;bad.generation++;
            int rc=oz_zpl_replace_as(fs,root,"guard-fresh","guard-old",&bad,&old,
                kcred,replace_guard_test_check,&check);
            if(rc!=ESTALE||check.calls)error=EIO;
        }
        if(!error) {
            bad=old;bad.generation++;
            int rc=oz_zpl_replace_as(fs,root,"guard-fresh","guard-old",&fresh,&bad,
                kcred,replace_guard_test_check,&check);
            if(rc!=ESTALE||check.calls)error=EIO;
        }
        if(!error) {
            check.deny=1;
            int rc=oz_zpl_replace_as(fs,root,"guard-fresh","guard-old",&fresh,&old,
                kcred,replace_guard_test_check,&check);
            if(rc!=EACCES||check.calls!=1)error=EIO;
        }
        if(!error)error=oz_zpl_lookup_at(fs,root,"guard-old",&found);
        if(!error&&(found.object!=old.object||found.generation!=old.generation||found.size!=8))error=EIO;
        char bytes[8];
        if(!error)error=oz_zpl_read(fs,old.object,0,bytes,sizeof(bytes));
        if(!error&&memcmp(bytes,"original",8))error=EIO;
        if(!error) {
            check.deny=0;
            error=oz_zpl_replace_as(fs,root,"guard-fresh","guard-old",&fresh,&old,
                kcred,replace_guard_test_check,&check);
        }
        if(!error)error=oz_zpl_lookup_at(fs,root,"guard-old",&found);
        if(!error&&(found.object!=fresh.object||found.generation!=fresh.generation||found.size))error=EIO;
        if(!error&&oz_zpl_lookup_at(fs,root,"guard-fresh",&found)!=ENOENT)error=EIO;
        if(!error&&linked) {
            error=oz_zpl_lookup_at(fs,root,"guard-alias",&found);
            if(!error&&(found.object!=old.object||found.size!=8))error=EIO;
            if(!error)error=oz_zpl_read(fs,old.object,0,bytes,sizeof(bytes));
            if(!error&&memcmp(bytes,"original",8))error=EIO;
            if(!error)error=oz_zpl_remove_as(fs,root,"guard-alias",0,kcred);
        }
        if(!error&&oz_zpl_stat(fs,old.object,&found)!=ENOENT)error=EIO;
        char line[128];snprintf(line,sizeof(line),
            "DOS replacement identity/reclaim hardlink=%d result=%d policy-calls=%d\n",linked,error,check.calls);
        for(char *p=line;*p;p++)RawPutChar(*p);
        if(!error)error=oz_zpl_remove_as(fs,root,"guard-old",0,kcred);
    }
    return error;
}

int oz_dos_write_test(int write)
{
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(!marker)return 0;
    fclose(marker);
    struct oz_zpl *fs=NULL;struct oz_mount *mount=NULL;
    const char *dataset="qemu_mirror/doswrite";
    int error=write?oz_zpl_fixture_create(dataset):0;
    if(!error)error=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    if(!error&&write)error=replacement_identity_test(fs);
    struct MsgPort *done=CreateMsgPort();if(!done&&!error)error=ENOMEM;
    if(!error)error=oz_dos_mount(fs,"OZWRITE",&mount);
#ifdef OZ_DOS_ALLOC_TEST
    if(!error&&write)error=output_allocation_test(fs,mount,done,0);
    if(!error&&write)error=output_allocation_test(fs,mount,done,1);
    if(!error&&write)error=output_allocation_test(fs,mount,done,2);
    if(!error&&write)error=output_allocation_test(fs,mount,done,3);
#endif
    if(!error) {
        struct client c={0};c.port=done;c.secure=write;
        c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
        pthread_t thread;error=pthread_create(&thread,NULL,write_client,&c);
        if(!error) {
            do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}
                while(!GetMsg(done));
            pthread_join(thread,NULL);error=c.result;
        }
    }
    if(mount){int rc=oz_dos_unmount(mount);if(!error)error=rc;}
    if(done)DeleteMsgPort(done);
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    char text[100];snprintf(text,sizeof(text),"Native DOS content write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS resize write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS create defaults write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS mkdir write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS delete/reclaim write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS rename/move write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    snprintf(text,sizeof(text),"Native DOS replacement/exclusive write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return error;
}
