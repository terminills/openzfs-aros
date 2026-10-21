/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable native pool only. Never open the represented Unix devices. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/dmu_objset.h>
#include <zfs_prop.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/exall.h>
#include "zpl-fixture.h"
#include "dos-handler.h"
static void create_special_fs(objset_t *os,void *props,cred_t *cr,dmu_tx_t *tx)
{zfs_create_fs(os,cr,props,tx);}
static const char *names[]={"fifo","socket","character","block","ordinary"};
static const mode_t modes[]={S_IFIFO,S_IFSOCK,S_IFCHR,S_IFBLK,S_IFREG};
static const mode_t permissions[]={0700,0070,0007,0000,0600};
static ULONG expected_protection(unsigned index,int writable)
{
    const ULONG deny=FIBF_READ|FIBF_WRITE|FIBF_EXECUTE;
    const ULONG expected[]={0,deny|FIBF_GRP_READ|FIBF_GRP_WRITE|FIBF_GRP_EXECUTE,
        deny|FIBF_OTR_READ|FIBF_OTR_WRITE|FIBF_OTR_EXECUTE,deny,FIBF_EXECUTE};
    ULONG result=expected[index];
    if(!writable) {
        result|=FIBF_WRITE|FIBF_DELETE;
        result&=~(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE);
    }
    return result;
}
static const char payload[]="visible\n";
struct special_client {struct Message done;struct MsgPort *port;int error,writable,authenticated;};
static int entry_index(const char *name)
{for(unsigned i=0;i<5;i++)if(!strcmp(name,names[i]))return i;return -1;}
static void *dos_client(void *arg)
{
    struct special_client *c=arg;int error=0,stage=1;unsigned seen=0;
    struct Library *secBase=NULL;ULONG previous=0;int logged=0;
    if(c->authenticated) {
        secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase||!secIsConfigured())error=EACCES;
        if(!error) {
            previous=secGetTaskOwner(FindTask(NULL));
            struct TagItem tags[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
            ULONG owner=secLoginA(tags);logged=owner!=0;
            if(owner!=secOWNER_SYSTEM)error=EACCES;
        }
    }
    BPTR root=error?0:Lock((CONST_STRPTR)"OZSPECIAL:",SHARED_LOCK);
    struct FileInfoBlock fib;
    if(!root||!Examine(root,&fib))error=EIO;
    while(!error&&ExNext(root,&fib)) {
        int i=entry_index((char *)fib.fib_FileName);
        if(i<0||(seen&(1U<<i))||fib.fib_DirEntryType!=(i==0?ST_PIPEFILE:ST_FILE))error=EIO;
        else seen|=1U<<i;
        if(i>=0&&fib.fib_Protection!=expected_protection(i,c->writable))error=EIO;
    }
    if(!error&&(IoErr()!=ERROR_NO_MORE_ENTRIES||seen!=31))error=EIO;
    if(!error)stage=2;
    struct ExAllControl *control=AllocDosObject(DOS_EXALLCONTROL,NULL);
    if(!control)error=ENOMEM;
    if(!error) {
        unsigned char buffer[2048] __attribute__((aligned(8)));
        LONG more=ExAll(root,(struct ExAllData *)buffer,sizeof(buffer),ED_PROTECTION,control);
        if(more||IoErr()!=ERROR_NO_MORE_ENTRIES||control->eac_Entries!=5)error=EIO;
        unsigned exseen=0;
        for(struct ExAllData *e=(void *)buffer;!error&&e;e=e->ed_Next) {
            int i=entry_index((char *)e->ed_Name);
            if(i<0||(exseen&(1U<<i))||e->ed_Type!=(i==0?ST_PIPEFILE:ST_FILE))error=EIO;
            else exseen|=1U<<i;
            if(i>=0&&e->ed_Prot!=expected_protection(i,c->writable))error=EIO;
        }
        if(exseen!=31)error=EIO;
    }
    if(control)FreeDosObject(DOS_EXALLCONTROL,control);
    if(root)UnLock(root);
    for(unsigned i=0;i<4&&!error;i++) {
        stage=10+i;
        char path[80];snprintf(path,sizeof(path),"OZSPECIAL:%s",names[i]);
        BPTR lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
        if(!lock||!Examine(lock,&fib)||fib.fib_DirEntryType!=(i==0?ST_PIPEFILE:ST_FILE))error=EIO;
        if(!error&&fib.fib_Protection!=expected_protection(i,c->writable))error=EIO;
        if(lock) {
            if(!error)stage=20+i;
            BPTR from=OpenFromLock(lock);
            if(from){error=EIO;Close(from);}
            else {if(IoErr()!=ERROR_OBJECT_WRONG_TYPE)error=EIO;UnLock(lock);}
        }
        const LONG modes[]={MODE_OLDFILE,MODE_READWRITE,MODE_NEWFILE};
        for(unsigned op=0;op<3&&!error;op++) {
            stage=100+i*10+op;
            BPTR f=Open((CONST_STRPTR)path,modes[op]);
            LONG expected=op&&!c->writable?ERROR_DISK_WRITE_PROTECTED:ERROR_OBJECT_WRONG_TYPE;
            if(f){error=EIO;Close(f);}else if(IoErr()!=expected)error=EIO;
        }
    }
    if(!error) {
        stage=3;
        BPTR f=Open((CONST_STRPTR)"OZSPECIAL:ordinary",MODE_OLDFILE);char bytes[32];
        if(!f)error=EIO;
        else {if(Read(f,bytes,sizeof(bytes))!=sizeof(payload)-1||memcmp(bytes,payload,sizeof(payload)-1))error=EIO;Close(f);}
    }
    char report[160];snprintf(report,sizeof(report),"Special DOS client stage=%d seen=%u error=%d ioerr=%ld\n",
        stage,seen,error,(long)IoErr());
    for(char *p=report;*p;p++)RawPutChar(*p);
    if(logged) {
        struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(tags)!=previous)error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    c->error=error;PutMsg(c->port,&c->done);return NULL;
}
static int dos_test(struct oz_zpl *fs,int authenticated,int writable)
{
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    struct oz_mount *mount=NULL;
    int error=authenticated?oz_dos_mount(fs,"OZSPECIAL",&mount):oz_dos_mount_fixture(fs,"OZSPECIAL",&mount);
    struct special_client c={0};c.port=done;c.writable=writable;c.authenticated=authenticated;
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;
    if(!error)error=pthread_create(&thread,NULL,dos_client,&c);
    if(!error) {
        do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}while(!GetMsg(done));
        pthread_join(thread,NULL);error=c.error;
    }
    if(mount){int e=oz_dos_unmount(mount);if(!error)error=e;}
    DeleteMsgPort(done);return error;
}
static int special_version(unsigned version,boolean_t writing)
{
    char dataset[96];snprintf(dataset,sizeof(dataset),"qemu_mirror/specials-v%u",version);
    int error=0;
    if(writing) {
        nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_VERSION),version);
        struct oz_cred_scope *scope=NULL;error=oz_cred_push(kcred,&scope);
        if(!error) {
            error=dmu_objset_create(dataset,DMU_OST_ZFS,0,NULL,create_special_fs,props);
            int popped=oz_cred_pop(scope);if(!error)error=popped;
        }
        fnvlist_free(props);
    }
    struct oz_zpl *mount=NULL;
    if(!error)error=writing?oz_zpl_open_writable(dataset,&mount):oz_zpl_open(dataset,&mount);
    if(!error&&writing) {
        objset_t *os=NULL;
        error=dmu_objset_hold(dataset,FTAG,&os);
        zfsvfs_t *fs=NULL;
        if(!error) {
            mutex_enter(&os->os_user_ptr_lock);fs=dmu_objset_get_user(os);
            mutex_exit(&os->os_user_ptr_lock);dmu_objset_rele(os,FTAG);
            if(!fs)error=ESRCH;
        }
        /* Single fixture owner keeps the mount alive; no config lock while
         * assigning create transactions. */
        znode_t *parent=NULL;
        if(!error)error=zfs_zget(fs,oz_zpl_root(mount),&parent);
        for(unsigned i=0;i<5&&!error;i++) {
            vattr_t va={.va_mask=ATTR_MODE,.va_mode=modes[i]|permissions[i],.va_rdev=0};
            znode_t *node=NULL;
            error=zfs_create(parent,(char *)names[i],&va,1,0,&node,kcred,0,NULL,NULL);
            if(!error&&i==4) {
                size_t residual=sizeof(payload)-1;
                error=zfs_write_simple(node,(void *)payload,sizeof(payload)-1,0,&residual);
                if(!error&&residual)error=EIO;
                if(!error)error=zfs_fsync(node,0,kcred);
            }
            if(node)oz_znode_rele(node);
        }
        if(parent)oz_znode_rele(parent);
        if(!error)error=oz_zpl_sync(mount);
    }
    unsigned enumerated=0,seen=0;
    for(unsigned i=0;i<5&&!error;i++) {
        struct oz_zpl_info info;char name[256];
        error=oz_zpl_nth(mount,oz_zpl_root(mount),i,name,sizeof(name),&info);
        if(!error) {
            int index=entry_index(name);
            if(index<0||(seen&(1U<<index))||info.directory||info.symlink||
                info.special!=(index==4?OZ_SPECIAL_NONE:index+1))error=EIO;
            else if(info.has_native_protection||(info.mode&0777)!=permissions[index])error=EIO;
            else {enumerated++;seen|=1U<<index;}
        }
    }
    uint64_t objects[4]={0};
    for(unsigned i=0;i<4&&!error;i++) {
        struct oz_zpl_info info;char byte=0;
        error=oz_zpl_lookup_at(mount,oz_zpl_root(mount),names[i],&info);
        if(!error){objects[i]=info.object;if(oz_zpl_read(mount,info.object,0,&byte,0)!=ENOTSUP)error=EIO;}
    }
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");int authenticated=marker!=NULL;
    if(marker)fclose(marker);
    if(!error&&writing&&!authenticated) {
        error=oz_zpl_close(mount);mount=NULL;
        if(!error)error=oz_zpl_open(dataset,&mount);
    }
    if(!error)error=dos_test(mount,authenticated,writing&&authenticated);
    for(unsigned i=0;i<4&&!error;i++) {
        struct oz_zpl_info info;
        error=oz_zpl_lookup_at(mount,oz_zpl_root(mount),names[i],&info);
        if(!error&&(info.object!=objects[i]||info.special!=i+1||info.size))error=EIO;
    }
    int close_error=oz_zpl_close(mount);if(!error)error=close_error;
    char line[160];snprintf(line,sizeof(line),
        "RESULT native special-file metadata %s version=%u write=%d enumerated=%u error=%d\n",
        error?"FAIL":"PASS",version,writing,enumerated,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
int oz_special_file_test(boolean_t writing)
{
    int error=0;
    for(unsigned version=1;version<=5&&!error;version++)error=special_version(version,writing);
    return error;
}
