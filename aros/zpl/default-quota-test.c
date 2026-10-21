/* SPDX-License-Identifier: CDDL-1.0 */
/* Isolated diskless guest only: exclusive RAM files, no device arguments. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_quota.h>
#include <sys/dmu_objset.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/zap.h>
#include <sys/zap_impl.h>
#include <sys/u8_textprep.h>
#include <sys/txg.h>
#include <zfs_prop.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include "../ui/api.h"
#include "../engine/snapshot-unmount.h"
#include "read-mount.h"
#include "../engine/zpl-fixture.h"
#include "../engine/dos-handler.h"
#include "../engine/file-scope.h"
#include "../cli/session-cleanup.h"
#ifdef OZ_NATIVE_DISPATCHER
#include <sys/dsl_dir.h>
#include <sys/dsl_prop.h>
#include <sys/zvol.h>
#include <sys/zvol_impl.h>
#include "../engine/zvol-native.h"
#include "../engine/control-file.h"
#include "../engine/ioctl-copy.h"
#endif
int oz_zpl_dataset_create(const char *, cred_t *);
int oz_zpl_dataset_create_options(const char *,const char *,cred_t *);
int oz_cred_test(void);
int oz_caller_test(void);
int oz_send_caller_test(const char *);

#define CHECK(expr) do { int e = (expr); if (e) { \
    char b[160]; snprintf(b, sizeof(b), "FAIL quota line=%d error=%d\n", __LINE__, e); \
    for(char *p=b; *p; p++) { RawPutChar(*p); } return e; } } while (0)
static const zfs_prop_t props[] = {
    ZFS_PROP_DEFAULTUSERQUOTA, ZFS_PROP_DEFAULTGROUPQUOTA,
    ZFS_PROP_DEFAULTPROJECTQUOTA, ZFS_PROP_DEFAULTUSEROBJQUOTA,
    ZFS_PROP_DEFAULTGROUPOBJQUOTA, ZFS_PROP_DEFAULTPROJECTOBJQUOTA
};
struct counter_worker { atomic_t *counter; int decrement; };
static void *counter_thread(void *arg)
{
    struct counter_worker *w=arg;
    for(unsigned i=0;i<20000;i++) {
        if(w->decrement)atomic_dec(w->counter);else atomic_inc(w->counter);
    }
    return NULL;
}
static int platform_primitives_test(void)
{
    CHECK(RW_NONE!=RW_READER && RW_NONE!=RW_WRITER?0:EIO);
    struct node { int value;struct hlist_node link; } nodes[3]={{.value=1},{.value=2},{.value=3}};
    struct hlist_head head;INIT_HLIST_HEAD(&head);
    for(unsigned i=0;i<3;i++){INIT_HLIST_NODE(&nodes[i].link);hlist_add_head(&nodes[i].link,&head);}
    struct hlist_node *p;unsigned expected=3;
    hlist_for_each(p,&head)CHECK(hlist_entry(p,struct node,link)->value==expected--?0:EIO);
    CHECK(!expected?0:EIO);
    hlist_del(&nodes[1].link); /* middle: successor backlink must change */
    CHECK(nodes[0].link.pprev==&nodes[2].link.next?0:EIO);
    hlist_del(&nodes[2].link);CHECK(head.first==&nodes[0].link && nodes[0].link.pprev==&head.first?0:EIO);
    hlist_del(&nodes[0].link);CHECK(!head.first?0:EIO);
    INIT_HLIST_NODE(&nodes[1].link);hlist_add_head(&nodes[1].link,&head);
    hlist_del(&nodes[1].link);CHECK(!head.first?0:EIO);
    atomic_t counter={0};pthread_t threads[8];struct counter_worker w={.counter=&counter};
    CHECK(atomic_inc(&counter)==1 && atomic_dec(&counter)==0?0:EIO);
    for(unsigned round=0;round<2;round++) {
        w.decrement=round;
        for(unsigned i=0;i<8;i++)CHECK(pthread_create(&threads[i],NULL,counter_thread,&w));
        for(unsigned i=0;i<8;i++)CHECK(pthread_join(threads[i],NULL));
        CHECK(atomic_read(&counter)==(round?0:160000)?0:EIO);
    }
    const char *s="RESULT native platform primitives PASS hlist=1 counters=320000 threads=8\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
struct snapshot_client {
    struct MsgPort *port;
    struct Message done;
    struct Task *task;
    const char *snapshot;
    int error;
};
static int scoped_reader(void *arg,void *buffer,size_t length,size_t *done)
{
    (void)arg;*done=length?1:0;
    if(length)*(char *)buffer='Z';
    return 0;
}
static void scoped_release(void *arg){++*(unsigned *)arg;}
struct session_file_worker {
    struct oz_session_cleanup *session;
    pthread_mutex_t gate;
    int descriptor,error;
    zfs_file_t *held;
};
static void *session_file_finish(void *arg)
{
    struct session_file_worker *w=arg;
    pthread_mutex_lock(&w->gate);
    zfs_file_t *missing=NULL;
    if(oz_session_file_get(w->session,w->descriptor,&missing)!=EBADF||missing)
        w->error=EIO;
    char byte=0;ssize_t resid=1;
    if(zfs_file_read(w->held,&byte,1,&resid)||byte!='Z'||resid)w->error=EIO;
    zfs_file_put(w->held);
    oz_session_cleanup_release(w->session);
    pthread_mutex_unlock(&w->gate);
    return NULL;
}
static int session_files_test(void)
{
    for(unsigned round=0;round<16;round++) {
        struct oz_session_cleanup *s=NULL,*other=NULL;
        zfs_file_t *file=NULL,*held=NULL,*missing=NULL;
        unsigned released=0;int ids[OZ_SESSION_FILES],replacement=-1;
        CHECK(oz_session_cleanup_create(&s));
        CHECK(oz_session_cleanup_create(&other));
        CHECK(zfs_file_from_reader(scoped_reader,&released,&file));
        CHECK(oz_file_set_private(file,&released,scoped_release));
        for(unsigned i=0;i<OZ_SESSION_FILES;i++)CHECK(oz_session_file_insert(s,file,&ids[i]));
        CHECK(oz_session_file_insert(s,file,&replacement)==EMFILE && replacement==-1?0:EIO);
        CHECK(oz_session_file_get(other,ids[0],&missing)==EBADF && !missing?0:EIO);
        CHECK(oz_session_file_get(s,ids[0],&held));
        CHECK(oz_session_file_close(s,ids[0]));
        CHECK(oz_session_file_close(s,ids[0])==EBADF?0:EIO);
        CHECK(oz_session_file_insert(s,file,&replacement));
        CHECK(replacement>ids[OZ_SESSION_FILES-1]?0:EIO);
        CHECK(oz_session_file_get(s,ids[0],&missing)==EBADF && !missing?0:EIO);
        struct oz_file_scope *scope=NULL,*rejected=NULL;
        CHECK(oz_session_file_scope(s,&scope));
        CHECK(oz_session_cleanup_hold(s)); /* inspect closed state safely */
        zfs_file_close(file);
        oz_session_cleanup_close(s);
        CHECK(oz_session_file_get(s,replacement,&missing)==EBADF && !missing?0:EIO);
        CHECK(oz_session_file_insert(s,held,&replacement)==EBADF?0:EIO);
        CHECK(oz_session_file_scope(s,&rejected)==EBADF && !rejected?0:EIO);
        oz_session_cleanup_release(s); /* scope and held file outlive session */
        oz_session_cleanup_close(other);
        zfs_file_t *scoped=zfs_file_get(ids[1]);CHECK(scoped==held?0:EIO);
        zfs_file_put(scoped);CHECK(oz_file_scope_leave(scope));
        CHECK(!released?0:EIO);
        char byte=0;ssize_t resid=1;CHECK(zfs_file_read(held,&byte,1,&resid));
        CHECK(byte=='Z' && !resid?0:EIO);
        zfs_file_put(held);CHECK(released==1?0:EIO);
    }
    for(unsigned round=0;round<16;round++) {
        struct session_file_worker w={0};unsigned released=0;
        zfs_file_t *file=NULL;pthread_t thread;
        CHECK(pthread_mutex_init(&w.gate,NULL));CHECK(pthread_mutex_lock(&w.gate));
        CHECK(oz_session_cleanup_create(&w.session));
        CHECK(zfs_file_from_reader(scoped_reader,&released,&file));
        CHECK(oz_file_set_private(file,&released,scoped_release));
        CHECK(oz_session_file_insert(w.session,file,&w.descriptor));
        CHECK(oz_session_cleanup_hold(w.session));
        CHECK(oz_session_file_get(w.session,w.descriptor,&w.held));
        CHECK(pthread_create(&thread,NULL,session_file_finish,&w));
        zfs_file_close(file);oz_session_cleanup_close(w.session);
        CHECK(!released?0:EIO);
        CHECK(pthread_mutex_unlock(&w.gate));CHECK(pthread_join(thread,NULL));
        CHECK(w.error);CHECK(released==1?0:EIO);CHECK(pthread_mutex_destroy(&w.gate));
    }
    const char *s="RESULT native session files PASS rounds=16 capacity=64 stale=1 retained=1 threads=16\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
struct file_worker {zfs_file_t *file;int error;};
static void *file_worker_run(void *arg)
{
    struct file_worker *w=arg;
    if(zfs_file_get(7)){w->error=EIO;return NULL;} /* no inherited scope */
    struct oz_file_binding binding={7,w->file};struct oz_file_scope *scope=NULL;
    w->error=oz_file_scope_enter(&binding,1,&scope);if(w->error)return NULL;
    for(unsigned i=0;i<1000;i++) {
        zfs_file_t *fp=zfs_file_get(7);
        if(fp!=w->file){w->error=EIO;break;}
        zfs_file_put(fp);
    }
    /* Deliberately abandon the scope; native TLS destructor must release it. */
    return NULL;
}
static int file_scope_test(const char *dir)
{
    unsigned released=0;zfs_file_t *file=NULL;
    CHECK(zfs_file_from_reader(scoped_reader,&released,&file));
    CHECK(oz_file_set_private(file,&released,scoped_release));
    CHECK(zfs_file_private(file)==&released && !zfs_file_get(7) && !zfs_file_get(-1)?0:EIO);
    struct oz_file_binding bindings[2]={{7,file},{7,file}};
    struct oz_file_scope *outer=NULL,*inner=NULL;
    CHECK(oz_file_scope_enter(bindings,2,&outer)==EINVAL && !outer?0:EIO);
    CHECK(oz_file_scope_enter(bindings,1,&outer));
    CHECK(oz_file_set_private(file,&released,scoped_release)==EBUSY?0:EIO);
    bindings[0].descriptor=8; /* registered descriptor was snapshotted */
    CHECK(!zfs_file_get(8)?0:EIO);
    CHECK(oz_file_scope_enter(NULL,0,&inner));
    CHECK(!zfs_file_get(7) && oz_file_scope_leave(outer)==EINVAL?0:EIO);
    CHECK(oz_file_scope_leave(inner));
    pthread_t threads[8];struct file_worker workers[8];
    for(unsigned i=0;i<8;i++){workers[i]=(struct file_worker){.file=file};CHECK(pthread_create(&threads[i],NULL,file_worker_run,&workers[i]));}
    for(unsigned i=0;i<8;i++){CHECK(pthread_join(threads[i],NULL));CHECK(workers[i].error);}
    zfs_file_t *held=zfs_file_get(7);CHECK(held==file?0:EIO);
    CHECK(oz_file_scope_leave(outer));CHECK(!zfs_file_get(7)?0:EIO);
    zfs_file_close(file);CHECK(!released?0:EIO); /* held survives original owner */
    char byte=0;ssize_t resid=1;CHECK(zfs_file_read(held,&byte,1,&resid));
    CHECK(byte=='Z' && !resid && zfs_file_private(held)==&released?0:EIO);
    zfs_file_put(held);CHECK(released==1?0:EIO);
    char path[160];snprintf(path,sizeof(path),"%s/scoped-file",dir);
    CHECK(zfs_file_open(path,O_CREAT|O_EXCL|O_RDWR,0600,kcred,&file));
    const char payload[]="retained native RAM file";
    CHECK(zfs_file_write(file,payload,sizeof(payload),NULL));
    struct oz_file_binding disk_binding={9,file};
    CHECK(oz_file_scope_enter(&disk_binding,1,&outer));
    held=zfs_file_get(9);CHECK(held==file && !zfs_file_private(held)?0:EIO);
    zfs_file_close(file);CHECK(oz_file_scope_leave(outer));
    char actual[sizeof(payload)];CHECK(zfs_file_pread(held,actual,sizeof(actual),0,NULL));
    CHECK(!memcmp(actual,payload,sizeof(payload))?0:EIO);zfs_file_put(held);
    CHECK(zfs_file_open(path,O_RDONLY,0,kcred,&file));
    CHECK(zfs_file_read(file,actual,sizeof(actual),NULL));
    CHECK(!memcmp(actual,payload,sizeof(payload))?0:EIO);zfs_file_close(file);
    CHECK(zfs_file_unlink(path));
    const char *s="RESULT native file scopes PASS threads=8 exit-cleanup=1 retained-read=2 final-close=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static void *snapshot_reader(void *arg)
{
    struct snapshot_client *c=arg;
    c->task=FindTask(NULL);
    if(zfsctl_snapshot_unmount(c->snapshot,MNT_FORCE)!=EDEADLK)c->error=EIO;
    BPTR lock=Lock((CONST_STRPTR)"OZSNAPTEST:",ACCESS_READ);
    if(!lock)c->error=EIO;
    PutMsg(c->port,&c->done);
    Wait(SIGBREAKF_CTRL_E);
    if(lock)UnLock(lock);
    PutMsg(c->port,&c->done);
    return NULL;
}
static void snapshot_wait(struct MsgPort *port)
{
    while(!GetMsg(port)) {
        Wait((1UL<<port->mp_SigBit)|oz_engine_signals());
        oz_engine_pump();
    }
}
static int snapshot_unmount_test(const char *dataset)
{
    char snap[128]; snprintf(snap,sizeof(snap),"%s@unmount",dataset);
    nvlist_t *snaps=fnvlist_alloc(),*errors=fnvlist_alloc();
    fnvlist_add_boolean(snaps,snap);
    CHECK(dsl_dataset_snapshot(snaps,NULL,errors));
    fnvlist_free(snaps);fnvlist_free(errors);
    CHECK(oz_send_caller_test(snap));
    const char *send_result="RESULT native send caller PASS complete=1 failed=1\n";
    while(*send_result)RawPutChar(*send_result++);
    CHECK(zfsctl_snapshot_unmount(dataset,0)==EINVAL ? 0 : EIO);
    CHECK(zfsctl_snapshot_unmount(snap,2)==EINVAL ? 0 : EIO);
    CHECK(zfsctl_snapshot_unmount(snap,MNT_FORCE)); /* not mounted */
    static struct oz_message m;
    memset(&m,0,sizeof(m));m.operation=43;m.flags=OZ_CONFIRMED;
    strcpy(m.target,snap);strcpy(m.argument,"OZSNAPTEST");strcpy(m.options,"readonly=on");
    /* Internal fixture invocation; public mutation still requires admission. */
    oz_engine_dispatch(&m);CHECK(m.status==OZ_OK ? 0 : EIO);
    struct snapshot_client c={0};c.port=CreateMsgPort();c.snapshot=snap;
    CHECK(c.port ? 0 : ENOMEM);
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,snapshot_reader,&c));
    snapshot_wait(c.port);CHECK(c.error);
    CHECK(zfsctl_snapshot_unmount(snap,MNT_FORCE)==EBUSY ? 0 : EIO);
    Signal(c.task,SIGBREAKF_CTRL_E);
    snapshot_wait(c.port);CHECK(pthread_join(thread,NULL));
    DeleteMsgPort(c.port);
    CHECK(zfsctl_snapshot_unmount(snap,MNT_FORCE));
    CHECK(zfsctl_snapshot_unmount(snap,0));
    struct oz_zpl *reopened=NULL;
    CHECK(oz_zpl_open(snap,&reopened));
    objset_t *snapshot_os;
    CHECK(dmu_objset_hold(snap,FTAG,&snapshot_os));
    mutex_enter(&snapshot_os->os_user_ptr_lock);
    boolean_t owner_matches=dmu_objset_get_user(snapshot_os)==dmu_objset_ds(snapshot_os)->ds_owner;
    mutex_exit(&snapshot_os->os_user_ptr_lock);
    CHECK(owner_matches ? 0 : EIO);
    CHECK(dsl_dataset_long_held(dmu_objset_ds(snapshot_os)) ? 0 : EIO);
    dmu_objset_rele(snapshot_os,FTAG);
    CHECK(oz_zpl_close(reopened));
    const char *s="RESULT native snapshot unmount PASS busy=1 detached=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static void creation_properties_cb(objset_t *os,void *props,cred_t *cr,dmu_tx_t *tx)
{zfs_create_fs(os,cr,props,tx);}
struct rename_client {struct MsgPort *port;struct Message done;const char *alias;int error;};
static void *rename_client_run(void *arg)
{
    struct rename_client *c=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    ULONG previous=secBase?secGetTaskOwner(FindTask(NULL)):0,logged=0;
    if(secBase) {
        struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        logged=secLoginA(login);
    }
    BPTR lock=0,link=0,file=0;struct FileInfoBlock *fib=NULL;
    if(!secBase||logged!=secOWNER_SYSTEM)c->error=EACCES;
    if(!c->error) {
        lock=Lock((CONST_STRPTR)"OZRENAME:caf\xc3\xa9.txt",ACCESS_READ);
        link=Lock((CONST_STRPTR)"OZRENAME:distinct-link",ACCESS_READ);
        file=Open((CONST_STRPTR)"OZRENAME:caf\xc3\xa9.txt",MODE_OLDFILE);
        fib=AllocDosObject(DOS_FIB,NULL);
        if(!lock||!link||!file||!fib)c->error=EIO;
    }
    if(!c->error&&!Rename((CONST_STRPTR)c->alias,(CONST_STRPTR)"OZRENAME:dos-renamed"))c->error=EIO;
    if(!c->error) {
        char path[256];
        if(!Examine(lock,fib)||strcmp(fib->fib_FileName,"dos-renamed"))c->error=EIO;
        if(!NameFromLock(lock,(STRPTR)path,sizeof(path))||!strstr(path,":dos-renamed"))c->error=EIO;
        if(!ExamineFH(file,fib)||strcmp(fib->fib_FileName,"dos-renamed"))c->error=EIO;
        if(!Examine(link,fib)||strcmp(fib->fib_FileName,"distinct-link"))c->error=EIO;
        if(!NameFromLock(link,(STRPTR)path,sizeof(path))||!strstr(path,":distinct-link"))c->error=EIO;
    }
    if(!c->error) {
        BPTR root=Lock((CONST_STRPTR)"OZRENAME:",ACCESS_READ);
        unsigned renamed=0,linked=0,unicode=0,total=0;
        if(!root||!Examine(root,fib))c->error=EIO;
        else {
            while(ExNext(root,fib)) {
                total++;
                renamed+=!strcmp(fib->fib_FileName,"dos-renamed");
                linked+=!strcmp(fib->fib_FileName,"distinct-link");
                unicode+=!strcmp(fib->fib_FileName,"keep-\xc3\xa9");
            }
            if(IoErr()!=ERROR_NO_MORE_ENTRIES||total!=3||renamed!=1||linked!=1||unicode!=1)c->error=EIO;
        }
        if(root)UnLock(root);
    }
    if(file&&!Close(file))c->error=EIO;
    if(lock)UnLock(lock);if(link)UnLock(link);if(fib)FreeDosObject(DOS_FIB,fib);
    if(logged) {struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous)c->error=EIO;}
    if(secBase)CloseLibrary(secBase);
    PutMsg(c->port,&c->done);return NULL;
}
static int dos_rename_alias_test(const char *dataset,const char *alias)
{
    struct oz_zpl *fs=NULL;struct oz_mount *endpoint=NULL;
    CHECK(oz_zpl_open_writable(dataset,&fs));
    CHECK(oz_dos_mount(fs,"OZRENAME",&endpoint));
    struct rename_client c={.alias=alias};c.port=CreateMsgPort();CHECK(c.port?0:ENOMEM);
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,rename_client_run,&c));
    while(!GetMsg(c.port)) {Wait((1UL<<c.port->mp_SigBit)|oz_dos_signals(endpoint));oz_dos_pump(endpoint);}
    CHECK(pthread_join(thread,NULL));DeleteMsgPort(c.port);
    CHECK(oz_dos_detach(endpoint));CHECK(oz_zpl_close(fs));CHECK(c.error);
    const char *s="RESULT native DOS alias rename PASS lock=1 handle=1 hardlink=1 enumeration=3 unicode=1\n";
    while(*s)RawPutChar(*s++);return 0;
}
static int utf8_mount_test(const char *pool,uint64_t normalization,uint64_t case_mode)
{
    char dataset[128];snprintf(dataset,sizeof(dataset),"%s/utf8-%llu-%llu",pool,
        (unsigned long long)normalization,(unsigned long long)case_mode);
    nvlist_t *props=fnvlist_alloc();
    fnvlist_add_uint64(props,"version",ZPL_VERSION_SA);
    fnvlist_add_uint64(props,"utf8only",1);
    fnvlist_add_uint64(props,"normalization",normalization);
    fnvlist_add_uint64(props,"casesensitivity",case_mode);
    struct oz_cred_scope *scope=NULL;CHECK(oz_cred_push(kcred,&scope));
    int error=dmu_objset_create(dataset,DMU_OST_ZFS,0,NULL,creation_properties_cb,props);
    CHECK(oz_cred_pop(scope));fnvlist_free(props);CHECK(error);
    objset_t *os=NULL;CHECK(dmu_objset_hold(dataset,FTAG,&os));
    uint64_t version,norm,utf8,sense;
    CHECK(zfs_get_zplprop(os,ZFS_PROP_VERSION,&version));
    CHECK(zfs_get_zplprop(os,ZFS_PROP_NORMALIZE,&norm));
    CHECK(zfs_get_zplprop(os,ZFS_PROP_UTF8ONLY,&utf8));
    CHECK(zfs_get_zplprop(os,ZFS_PROP_CASE,&sense));
    dmu_objset_rele(os,FTAG);
    char metadata[128];snprintf(metadata,sizeof(metadata),"UTF8 metadata version=%llu norm=%llu utf8=%llu case=%llu\n",
        (unsigned long long)version,(unsigned long long)norm,(unsigned long long)utf8,(unsigned long long)sense);
    for(char *p=metadata;*p;p++)RawPutChar(*p);
    CHECK(version==ZPL_VERSION_SA && norm==normalization && utf8==1 && sense==case_mode?0:EIO);
    const char *name="caf\xc3\xa9.txt",*renamed="renamed-\xe2\x82\xac.txt";
    const char *invalid="bad-\xc0\xaf";
    struct oz_read_mount *mount=NULL;struct oz_zpl_info info,found;
    CHECK(oz_read_mount_open_dataset(dataset,B_FALSE,&mount));
    uint64_t root=oz_read_mount_root(mount);
    CHECK(oz_read_mount_create(mount,root,name,0,kcred,&info));
    CHECK(oz_read_mount_lookup(mount,root,name,&found));
    CHECK(found.object==info.object?0:EIO);
    const char *equivalent="cafe\xcc\x81.txt";
    if(normalization) {
        CHECK(oz_read_mount_lookup(mount,root,equivalent,&found));
        CHECK(found.object==info.object?0:EIO);
        CHECK(oz_read_mount_create(mount,root,equivalent,0,kcred,&found)==EEXIST?0:EIO);
    }
    const char *upper="CAF\xc3\x89.TXT";
    int same=-1;
    CHECK(oz_read_mount_same_name(mount,name,equivalent,&same));
    CHECK(same==(normalization!=0)?0:EIO);
    CHECK(oz_read_mount_same_name(mount,name,upper,&same));
    CHECK(same==(case_mode==ZFS_CASE_INSENSITIVE)?0:EIO);
    CHECK(oz_read_mount_link(mount,root,info.object,"distinct-link",kcred));
    CHECK(oz_read_mount_lookup(mount,root,"distinct-link",&found));
    CHECK(found.object==info.object?0:EIO);
    CHECK(oz_read_mount_same_name(mount,name,"distinct-link",&same));
    CHECK(!same?0:EIO);
    /* Fullwidth digit is compatibility-equivalent but not canonically
     * equivalent to ASCII. Unlike a ligature it is unaffected by case fold. */
    const char *wide="digit-\xef\xbc\x91",*ascii="digit-1";
    int compatibility=normalization==U8_TEXTPREP_NFKC||normalization==U8_TEXTPREP_NFKD;
    struct oz_zpl_info wide_info;
    CHECK(oz_read_mount_create(mount,root,wide,0,kcred,&wide_info));
    CHECK(oz_read_mount_same_name(mount,wide,ascii,&same));
    CHECK(same==compatibility?0:EIO);
    if(compatibility) {
        CHECK(oz_read_mount_lookup(mount,root,ascii,&found));
        CHECK(found.object==wide_info.object?0:EIO);
        CHECK(oz_read_mount_create(mount,root,ascii,0,kcred,&found)==EEXIST?0:EIO);
        CHECK(oz_read_mount_remove(mount,root,ascii,0,kcred));
        CHECK(oz_read_mount_lookup(mount,root,wide,&found)==ENOENT?0:EIO);
    } else {
        CHECK(oz_read_mount_lookup(mount,root,ascii,&found)==ENOENT?0:EIO);
        CHECK(oz_read_mount_create(mount,root,ascii,0,kcred,&found));
        CHECK(found.object!=wide_info.object?0:EIO);
        CHECK(oz_read_mount_remove(mount,root,ascii,0,kcred));
        CHECK(oz_read_mount_remove(mount,root,wide,0,kcred));
    }
    CHECK(oz_read_mount_create(mount,root,"keep-\xc3\xa9",0,kcred,&found));
    if(case_mode==ZFS_CASE_INSENSITIVE) {
        CHECK(oz_read_mount_lookup(mount,root,upper,&found));
        CHECK(found.object==info.object?0:EIO);
        CHECK(oz_read_mount_create(mount,root,upper,0,kcred,&found)==EEXIST?0:EIO);
    } else {
        CHECK(oz_read_mount_lookup(mount,root,upper,&found)==ENOENT?0:EIO);
        CHECK(oz_read_mount_create(mount,root,upper,0,kcred,&found));
        CHECK(found.object!=info.object?0:EIO);
        CHECK(oz_read_mount_remove(mount,root,upper,0,kcred));
        CHECK(oz_read_mount_lookup(mount,root,name,&found));
        CHECK(found.object==info.object?0:EIO);
    }
    CHECK(oz_read_mount_create(mount,root,invalid,0,kcred,&found)==EILSEQ?0:EIO);
    CHECK(oz_read_mount_mkdir(mount,root,invalid,0,kcred,&found)==EILSEQ?0:EIO);
    CHECK(oz_read_mount_lookup(mount,root,invalid,&found)==EILSEQ?0:EIO);
    CHECK(oz_read_mount_rename(mount,root,name,root,invalid,kcred)==EILSEQ?0:EIO);
    CHECK(oz_read_mount_rename(mount,root,case_mode==ZFS_CASE_INSENSITIVE?upper:
        normalization?equivalent:name,root,renamed,kcred));
    CHECK(oz_read_mount_sync(mount));CHECK(oz_read_mount_close(mount));mount=NULL;
    CHECK(oz_read_mount_open_dataset(dataset,B_TRUE,&mount));
    CHECK(oz_read_mount_lookup(mount,oz_read_mount_root(mount),renamed,&found));
    CHECK(found.object==info.object?0:EIO);
    CHECK(oz_read_mount_lookup(mount,oz_read_mount_root(mount),name,&found)==ENOENT?0:EIO);
    CHECK(oz_read_mount_lookup(mount,oz_read_mount_root(mount),"distinct-link",&found));
    CHECK(found.object==info.object?0:EIO);
    CHECK(oz_read_mount_close(mount));
    const char *s=case_mode!=ZFS_CASE_SENSITIVE?
        "RESULT native case mount PASS lookup=1 duplicate-policy=1 rename=1 reopen=1\n":normalization?
        "RESULT native normalized mount PASS equivalent=1 duplicate=1 case-sensitive=1 rename=1 reopen=1\n":
        "RESULT native UTF8 mount PASS create=1 rename=1 rejected=4 reopen=1\n";
    while(*s)RawPutChar(*s++);
    /* Restore the original spelling, then exercise actual authenticated DOS
     * packets while retaining both the source references and a hard link. */
    CHECK(oz_read_mount_open_dataset(dataset,B_FALSE,&mount));
    CHECK(oz_read_mount_rename(mount,oz_read_mount_root(mount),renamed,
        oz_read_mount_root(mount),name,kcred));
    CHECK(oz_read_mount_close(mount));
    CHECK(dos_rename_alias_test(dataset,case_mode==ZFS_CASE_INSENSITIVE?
        "OZRENAME:CAF\xc3\x89.TXT":normalization?"OZRENAME:cafe\xcc\x81.txt":
        "OZRENAME:caf\xc3\xa9.txt"));
    return 0;
}
static int creation_properties_test(const char *pool)
{
    const uint64_t cases[]={ZFS_CASE_SENSITIVE,ZFS_CASE_MIXED,ZFS_CASE_INSENSITIVE};
    for(unsigned i=0;i<ARRAY_SIZE(cases);i++) {
        char name[128];snprintf(name,sizeof(name),"%s/properties%u",pool,i);
        uint64_t version=i?ZPL_VERSION_4:ZPL_VERSION_3;
        uint64_t norm=i?U8_TEXTPREP_NFC:0,utf8=i!=0;
        nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_VERSION),version);
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_CASE),cases[i]);
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_NORMALIZE),norm);
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_UTF8ONLY),utf8);
        struct oz_cred_scope *scope=NULL;CHECK(oz_cred_push(kcred,&scope));
        int created=dmu_objset_create(name,DMU_OST_ZFS,0,NULL,creation_properties_cb,props);
        CHECK(oz_cred_pop(scope));CHECK(created);
        fnvlist_free(props);
        /* Reacquire on-disk metadata rather than inspecting the temporary
         * creation state. Empty root ZAP encoding must match its properties. */
        objset_t *os;CHECK(dmu_objset_hold(name,FTAG,&os));
        const zfs_prop_t keys[]={ZFS_PROP_VERSION,ZFS_PROP_CASE,ZFS_PROP_NORMALIZE,ZFS_PROP_UTF8ONLY};
        const uint64_t expected[]={version,cases[i],norm,utf8};
        for(unsigned j=0;j<ARRAY_SIZE(keys);j++) {
            uint64_t value=UINT64_MAX;CHECK(zfs_get_zplprop(os,keys[j],&value));
            CHECK(value==expected[j]?0:EIO);
        }
        uint64_t root;CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZFS_ROOT_OBJ,8,1,&root));
        dmu_buf_t *db=NULL;CHECK(dmu_buf_hold(os,root,0,FTAG,&db,DMU_READ_NO_PREFETCH));
        mzap_phys_t *zap=db->db_data;
        boolean_t valid=zap->mz_block_type==ZBT_MICRO &&
            zap->mz_normflags==(norm|(i?U8_TEXTPREP_TOUPPER:0));
        dmu_buf_rele(db,FTAG);CHECK(valid?0:EIO);
        dmu_objset_rele(os,FTAG);
    }
    const char *s="RESULT native creation properties PASS cases=3 root-normalization=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int legacy_versions_test(const char *pool)
{
    for(uint64_t version=ZPL_VERSION_INITIAL;version<ZPL_VERSION_SA;version++) {
        char name[128];snprintf(name,sizeof(name),"%s/legacy-version-%u",pool,(unsigned)version);
        nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props,zfs_prop_to_name(ZFS_PROP_VERSION),version);
        struct oz_cred_scope *scope=NULL;CHECK(oz_cred_push(kcred,&scope));
        int created=dmu_objset_create(name,DMU_OST_ZFS,0,NULL,creation_properties_cb,props);
        CHECK(oz_cred_pop(scope));fnvlist_free(props);CHECK(created);
        struct oz_zpl *mount=NULL;
        struct oz_zpl_info info;
        const char payload[]="Legacy filesystem version matrix";
        size_t written=0;
        CHECK(oz_zpl_open_writable(name,&mount));
        CHECK(oz_zpl_create_as(mount,oz_zpl_root(mount),"payload",0644,kcred,&info));
        CHECK(oz_zpl_write_as(mount,info.object,0,payload,sizeof(payload),&written,kcred));
        CHECK(written==sizeof(payload)?0:EIO);
        CHECK(oz_zpl_sync(mount));
        CHECK(oz_zpl_close(mount));
        CHECK(oz_zpl_open(name,&mount));
        uint64_t object=0,size=0;
        char actual[sizeof(payload)];
        CHECK(oz_zpl_lookup(mount,"payload",&object,&size));
        CHECK(size==sizeof(payload)?0:EIO);
        CHECK(oz_zpl_read(mount,object,0,actual,sizeof(actual)));
        CHECK(memcmp(actual,payload,sizeof(actual))?EIO:0);
        CHECK(oz_zpl_close(mount));
        objset_t *os;CHECK(dmu_objset_hold(name,FTAG,&os));
        uint64_t saved=0,sa=0;
        CHECK(zfs_get_zplprop(os,ZFS_PROP_VERSION,&saved));
        CHECK(saved==version?0:EIO);
        CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa)==ENOENT?0:EIO);
        CHECK(!dsl_dataset_has_owner(dmu_objset_ds(os))?0:EIO);
        dmu_objset_rele(os,FTAG);
    }
    const char *s="RESULT native legacy versions PASS versions=1-4 write=1 remount=1 unchanged-format=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int version_test(const char *pool)
{
    char name[96]; snprintf(name,sizeof(name),"%s/legacy",pool);
    CHECK(oz_zpl_dataset_create(name,kcred));
    struct oz_zpl *writer=NULL;
    struct oz_zpl_info legacy_file;
    const char legacy_payload[]="AROS pre-SA write and reopen";
    size_t legacy_written=0;
    CHECK(oz_zpl_open_writable(name,&writer));
    CHECK(oz_zpl_create_as(writer,oz_zpl_root(writer),"before-upgrade",0644,kcred,&legacy_file));
    CHECK(oz_zpl_write_as(writer,legacy_file.object,0,legacy_payload,
        sizeof(legacy_payload),&legacy_written,kcred));
    CHECK(legacy_written==sizeof(legacy_payload) ? 0 : EIO);
    CHECK(oz_zpl_sync(writer));
    CHECK(oz_zpl_close(writer));
    /* A genuine pre-SA filesystem must mount without upgrading its format.
     * Repeated read-only opens must also release objset ownership. */
    for(unsigned attempt=0;attempt<2;attempt++) {
        struct oz_zpl *legacy=NULL;
        CHECK(oz_zpl_open(name,&legacy));
        struct oz_zpl_info info;
        CHECK(oz_zpl_stat(legacy,oz_zpl_root(legacy),&info));
        CHECK(info.directory ? 0 : EIO);
        uint64_t object=0,size=0;
        char actual[sizeof(legacy_payload)];
        CHECK(oz_zpl_lookup(legacy,"before-upgrade",&object,&size));
        CHECK(size==sizeof(legacy_payload) ? 0 : EIO);
        CHECK(oz_zpl_read(legacy,object,0,actual,sizeof(actual)));
        CHECK(memcmp(actual,legacy_payload,sizeof(actual)) ? EIO : 0);
        CHECK(oz_zpl_close(legacy));
        objset_t *os;
        CHECK(dmu_objset_hold(name,FTAG,&os));
        uint64_t version=0,sa=0;
        CHECK(zfs_get_zplprop(os,ZFS_PROP_VERSION,&version));
        CHECK(version==ZPL_VERSION_USERSPACE ? 0 : EIO);
        CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa)==ENOENT ? 0 : EIO);
        CHECK(!dsl_dataset_has_owner(dmu_objset_ds(os)) ? 0 : EIO);
        dmu_objset_rele(os,FTAG);
    }
    for(unsigned phase=0;phase<3;phase++) {
        zfsvfs_t *fs=NULL;
        CHECK(zfsvfs_create_hold(name,&fs));
        objset_t *os=fs->z_os; dsl_pool_t *dp=dmu_objset_pool(os);
        dsl_pool_config_exit(dp,fs);
        ZFS_TEARDOWN_ENTER_WRITE(fs,FTAG);
        CHECK(fs->z_version == (phase==2 ? ZPL_VERSION_SA : ZPL_VERSION_USERSPACE) ? 0 : EIO);
        CHECK(zfs_set_version(fs,0)==EINVAL ? 0 : EIO);
        CHECK(zfs_set_version(fs,ZPL_VERSION+1)==EINVAL ? 0 : EIO);
        CHECK(zfs_set_version(fs,fs->z_version-1)==EINVAL ? 0 : EIO);
        uint64_t sa=0;
        if(phase==0) {
            CHECK(zfs_set_version(fs,ZPL_VERSION_SA)==ENOTSUP ? 0 : EIO);
            CHECK(!fs->z_use_sa && fs->z_version==ZPL_VERSION_USERSPACE ? 0 : EIO);
            CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa)==ENOENT ? 0 : EIO);
        } else {
            CHECK(zfs_set_version(fs,ZPL_VERSION_SA));
            CHECK(fs->z_use_sa && fs->z_use_fuids && os->os_version==ZPL_VERSION_SA ? 0 : EIO);
            CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa));
            CHECK(sa ? 0 : EIO);
            uint64_t version=0;
            CHECK(zap_lookup(os,MASTER_NODE_OBJ,ZPL_VERSION_STR,8,1,&version));
            CHECK(version==ZPL_VERSION_SA ? 0 : EIO);
        }
        txg_wait_synced(dp,0);
        ZFS_TEARDOWN_EXIT(fs,FTAG);
        dsl_pool_config_enter(dp,fs);
        dmu_objset_rele(os,fs); zfsvfs_free(fs);
        if(phase==0) {
            spa_t *spa; CHECK(spa_open(pool,&spa,FTAG));
            spa_upgrade(spa,SPA_VERSION);
            txg_wait_synced(spa->spa_dsl_pool,0);
            spa_close(spa,FTAG);
        }
    }
    struct oz_zpl *mounted=NULL;
    CHECK(oz_zpl_open_writable(name,&mounted));
    uint64_t legacy_object=0,legacy_size=0;
    char legacy_actual[sizeof(legacy_payload)];
    CHECK(oz_zpl_lookup(mounted,"before-upgrade",&legacy_object,&legacy_size));
    CHECK(legacy_size==sizeof(legacy_payload) ? 0 : EIO);
    CHECK(oz_zpl_read(mounted,legacy_object,0,legacy_actual,sizeof(legacy_actual)));
    CHECK(memcmp(legacy_actual,legacy_payload,sizeof(legacy_actual)) ? EIO : 0);
    struct oz_zpl_info info;
    CHECK(oz_zpl_stat(mounted,oz_zpl_root(mounted),&info));
    CHECK(info.directory ? 0 : EIO);
    CHECK(oz_zpl_create_as(mounted,oz_zpl_root(mounted),"after-upgrade",0644,kcred,&info));
    const char payload[]="AROS upstream legacy upgrade data";
    size_t written=0;
    CHECK(oz_zpl_write_as(mounted,info.object,0,payload,sizeof(payload),&written,kcred));
    CHECK(written==sizeof(payload) ? 0 : EIO);
    CHECK(oz_zpl_sync(mounted));
    CHECK(oz_zpl_close(mounted));
    CHECK(oz_zpl_open(name,&mounted));
    uint64_t object=0,size=0; char actual[sizeof(payload)];
    CHECK(oz_zpl_lookup(mounted,"after-upgrade",&object,&size));
    CHECK(size==sizeof(payload) ? 0 : EIO);
    CHECK(oz_zpl_read(mounted,object,0,actual,sizeof(actual)));
    CHECK(memcmp(actual,payload,sizeof(payload)) ? EIO : 0);
    CHECK(oz_zpl_close(mounted));
    const char *s="RESULT native filesystem version PASS legacy=4 upgraded=5\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int temporary_holds(const char *dataset)
{
    zfsvfs_t *fs = (void *)1;
    CHECK(zfsvfs_create_hold(NULL, &fs) == EINVAL && !fs ? 0 : EIO);
    CHECK(zfsvfs_create_hold(dataset, NULL) == EINVAL ? 0 : EIO);
    char missing[128]; snprintf(missing,sizeof(missing),"%s/absent",dataset);
    CHECK(zfsvfs_create_hold(missing, &fs) == ENOENT && !fs ? 0 : EIO);
    char wrong[128]; snprintf(wrong,sizeof(wrong),"%s/wrongtype",dataset);
    CHECK(dmu_objset_create(wrong, DMU_OST_OTHER, 0, NULL, NULL, NULL));
    CHECK(zfsvfs_create_hold(wrong, &fs) == EINVAL && !fs ? 0 : EIO);
    char empty[128]; snprintf(empty,sizeof(empty),"%s/nozpl",dataset);
    CHECK(dmu_objset_create(empty, DMU_OST_ZFS, 0, NULL, NULL, NULL));
    CHECK(zfsvfs_create_hold(empty, &fs) != 0 && !fs ? 0 : EIO);
    for(unsigned i=0;i<16;i++) {
        CHECK(zfsvfs_create_hold(dataset, &fs));
        objset_t *os = fs->z_os;
        dsl_pool_t *dp = dmu_objset_pool(os);
        CHECK(fs->z_use_hold && !fs->z_native_refs &&
            dsl_pool_config_held(dp) ? 0 : EIO);
        mutex_enter(&os->os_user_ptr_lock);
        boolean_t unpublished = dmu_objset_get_user(os) == NULL;
        mutex_exit(&os->os_user_ptr_lock);
        CHECK(unpublished ? 0 : EIO);
        /* Exactly the upstream dispatcher's config/teardown lock handoff. */
        dsl_pool_config_exit(dp, fs);
        ZFS_TEARDOWN_ENTER_WRITE(fs, FTAG);
        CHECK(!zfs_vfs_held(fs) && !dsl_pool_config_held(dp) ? 0 : EIO);
        CHECK(zfs_set_default_quota(fs,ZFS_PROP_DEFAULTUSERQUOTA,1ULL<<34));
        CHECK(zfs_set_default_quota(fs,ZFS_PROP_DEFAULTUSERQUOTA,0));
        ZFS_TEARDOWN_EXIT(fs, FTAG);
        dsl_pool_config_enter(dp, fs);
        dmu_objset_rele(os, fs);
        zfsvfs_free(fs);
    }
    const char *s="RESULT native temporary holds PASS cycles=16\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
/* Disposable pool only: change real SA identity fields and restore them to
 * exercise resume rejection without claiming an actual online rollback. */
static int resume_attribute(zfsvfs_t *fs,uint64_t object,sa_attr_type_t attr,uint64_t value)
{
    sa_handle_t *handle=NULL;
    CHECK(sa_handle_get(fs->z_os,object,NULL,SA_HDL_PRIVATE,&handle));
    dmu_tx_t *tx=dmu_tx_create(fs->z_os);
    dmu_tx_hold_sa(tx,handle,B_FALSE);
    int error=dmu_tx_assign(tx,DMU_TX_WAIT);
    if(!error) { error=sa_update(handle,attr,&value,sizeof(value),tx);dmu_tx_commit(tx); }
    else dmu_tx_abort(tx);
    sa_handle_destroy(handle);
    return error;
}
struct identity_client {
    struct MsgPort *port;
    struct Message done;
    struct Task *task;
    int error;
};
static void *identity_reader(void *arg)
{
    struct identity_client *c=arg;c->task=FindTask(NULL);
    BPTR file=Open((CONST_STRPTR)"OZIDENTITY:hello.txt",MODE_OLDFILE);
    LONG open_error=file?0:IoErr();
    BPTR lock=Lock((CONST_STRPTR)"OZIDENTITY:hello.txt",ACCESS_READ);
    if(!file||!lock) {
        c->error=EIO;
        char text[120];snprintf(text,sizeof(text),"DOS identity initial open=%ld lock=%ld\n",
            (long)open_error,(long)(lock?0:IoErr()));
        for(char *p=text;*p;p++)RawPutChar(*p);
    }
    PutMsg(c->port,&c->done);Wait(SIGBREAKF_CTRL_E);
    char byte=0;struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(!fib)c->error=ENOMEM;
    if(file && (Read(file,&byte,1)!=-1 || IoErr()!=ERROR_INVALID_LOCK))c->error=EIO;
    if(lock && fib && (Examine(lock,fib) || IoErr()!=ERROR_INVALID_LOCK))c->error=EIO;
    if(lock) {
        BPTR parent=ParentDir(lock);
        if(parent || IoErr()!=ERROR_INVALID_LOCK)c->error=EIO;
        if(parent)UnLock(parent);
    }
    PutMsg(c->port,&c->done);Wait(SIGBREAKF_CTRL_E);
    /* Restoring the original metadata must not resurrect a reference that
     * already observed a different identity. Only a fresh open is valid. */
    if(file && (Read(file,&byte,1)!=-1 || IoErr()!=ERROR_INVALID_LOCK))c->error=EIO;
    if(lock && fib && (Examine(lock,fib) || IoErr()!=ERROR_INVALID_LOCK))c->error=EIO;
    if(file && !Close(file))c->error=EIO;
    if(lock)UnLock(lock);
    if(fib)FreeDosObject(DOS_FIB,fib);
    file=Open((CONST_STRPTR)"OZIDENTITY:hello.txt",MODE_OLDFILE);
    if(!file)c->error=EIO;
    else {if(Read(file,&byte,1)!=1 || byte!='O')c->error=EIO;if(!Close(file))c->error=EIO;}
    PutMsg(c->port,&c->done);
    return NULL;
}
static void identity_wait(struct identity_client *c,struct oz_mount *mount)
{
    while(!GetMsg(c->port)) {
        Wait((1UL<<c->port->mp_SigBit)|oz_dos_signals(mount));
        oz_dos_pump(mount);
    }
}
static int dos_identity_test(const char *dataset,unsigned phase)
{
    struct oz_zpl *mount=NULL;struct oz_mount *endpoint=NULL;
    /* This diskless fixture does not configure security.library. Expose
     * only the explicit read-only fixture endpoint; administrative SA
     * mutation below stays inside the test owner, never a DOS write path. */
    CHECK(oz_zpl_open(dataset,&mount));
    CHECK(oz_dos_mount_fixture(mount,"OZIDENTITY",&endpoint));
    struct identity_client c={0};c.port=CreateMsgPort();CHECK(c.port?0:ENOMEM);
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,identity_reader,&c));
    identity_wait(&c,endpoint);CHECK(c.error);
    uint64_t object,size;CHECK(oz_zpl_lookup(mount,"hello.txt",&object,&size));
    if(phase==2)object=oz_zpl_root(mount);
    CHECK(oz_zpl_sync(mount));
    objset_t *os;CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);zfsvfs_t *fs=dmu_objset_get_user(os);
    int error=zfs_vfs_ref(&fs);mutex_exit(&os->os_user_ptr_lock);
    dmu_objset_rele(os,FTAG);CHECK(error);
    ZFS_TEARDOWN_ENTER_WRITE(fs,FTAG);
    znode_t *zp=NULL;CHECK(zfs_zget(fs,object,&zp));
    sa_attr_type_t attribute=phase==1?SA_ZPL_MODE(fs):SA_ZPL_GEN(fs);
    uint64_t saved=phase==1?zp->z_mode:zp->z_gen;
    uint64_t replacement=phase==1?(saved&~S_IFMT)|S_IFDIR:saved+1;
    zfs_znode_dmu_fini(zp);
    CHECK(resume_attribute(fs,object,attribute,replacement));
    CHECK(zfs_rezget(zp)==EIO?0:EIO);oz_znode_rele(zp);
    ZFS_TEARDOWN_EXIT_WRITE(fs,FTAG);
    Signal(c.task,SIGBREAKF_CTRL_E);identity_wait(&c,endpoint);
    CHECK(c.error);
    CHECK(oz_zpl_sync(mount));
    ZFS_TEARDOWN_ENTER_WRITE(fs,FTAG);
    CHECK(zfs_zget(fs,object,&zp));zfs_znode_dmu_fini(zp);
    CHECK(resume_attribute(fs,object,attribute,saved));
    CHECK(zfs_rezget(zp)==EIO?0:EIO);oz_znode_rele(zp);
    ZFS_TEARDOWN_EXIT_WRITE(fs,FTAG);zfs_vfs_rele(fs);
    Signal(c.task,SIGBREAKF_CTRL_E);identity_wait(&c,endpoint);
    CHECK(pthread_join(thread,NULL));CHECK(c.error);DeleteMsgPort(c.port);
    CHECK(oz_dos_detach(endpoint));CHECK(oz_zpl_close(mount));
    /* Retired endpoint intentionally retained for late DOS packets. */
    return 0;
}
struct deleteq_wait_test {
    dsl_dataset_t *ds;
    boolean_t waited, done;
    int error;
};
static void *deleteq_wait_thread(void *arg)
{
    struct deleteq_wait_test *w=arg;
    dsl_dir_t *dd=w->ds->ds_dir;
    mutex_enter(&dd->dd_activity_lock);
    dd->dd_activity_waiters++;
    w->error=dsl_dir_wait(dd,w->ds,ZFS_WAIT_DELETEQ,&w->waited);
    w->done=B_TRUE;
    if (--dd->dd_activity_waiters == 0)
        cv_signal(&dd->dd_activity_cv);
    mutex_exit(&dd->dd_activity_lock);
    return NULL;
}
static int deleteq_wait_test(const char *dataset, unsigned cancel)
{
    struct oz_zpl *mount=NULL;
    CHECK(oz_zpl_open_writable(dataset,&mount));
    struct oz_zpl_info info;
    CHECK(oz_zpl_create_as(mount,oz_zpl_root(mount),"held-deleteq",0600,kcred,&info));
    objset_t *os;
    CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs=dmu_objset_get_user(os);
    int error=zfs_vfs_ref(&fs);
    mutex_exit(&os->os_user_ptr_lock);
    /* The mount and acquired native reference retain the objset. Do not
     * retain dmu_objset_hold's config lock across synchronous mutations. */
    dmu_objset_rele(os,FTAG);
    CHECK(error);
    znode_t *held=NULL;CHECK(zfs_zget(fs,info.object,&held));
    /* Deliberately retain a POSIX-style unlinked node. The DOS removal
     * wrapper also drains synchronously and reports EBUSY for this case. */
    znode_t *parent=NULL;CHECK(zfs_zget(fs,fs->z_root,&parent));
    error=zfs_remove(parent,"held-deleteq",kcred,0);
    oz_znode_rele(parent);CHECK(error);
    struct deleteq_wait_test w={.ds=dmu_objset_ds(os)};
    dsl_dir_t *dd=w.ds->ds_dir;
    uint64_t count;CHECK(zap_count(os,fs->z_unlinkedobj,&count));
    CHECK(count && held->z_unlinked ? 0:EIO);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,deleteq_wait_thread,&w));
    boolean_t pending=B_FALSE;
    for(unsigned i=0;i<100;i++) {
        mutex_enter(&dd->dd_activity_lock);
        pending=w.waited && !w.done;
        boolean_t done=w.done;
        mutex_exit(&dd->dd_activity_lock);
        if(pending || done)break;
        Delay(1);
    }
    if (cancel) {
        zfs_vfs_rele(fs);
        if (cancel==2)CHECK(zfs_suspend_fs(fs));
        else CHECK(oz_mount_unpublish(fs));
        CHECK(pthread_join(thread,NULL));
        CHECK(pending && w.done && w.waited && w.error==EINTR ? 0:EIO);
        if (cancel==2)CHECK(zfs_resume_fs(fs,w.ds));
        else CHECK(oz_mount_publish(fs));
        mutex_enter(&os->os_user_ptr_lock);
        error=zfs_vfs_ref(&fs);
        mutex_exit(&os->os_user_ptr_lock);
        CHECK(error);
        CHECK(dd->dd_activity_cancelled ? EIO:0);
    }
    /* Release the real unlinked node, then use the native reclamation path.
     * Its last-entry removal must wake upstream's actual condition wait. */
    oz_znode_rele(held);
    ZFS_TEARDOWN_ENTER_WRITE(fs,FTAG);
    error=oz_unlinked_drain_exclusive(fs);
    ZFS_TEARDOWN_EXIT_WRITE(fs,FTAG);
    boolean_t completed=B_FALSE;
    for(unsigned i=0;i<100;i++) {
        mutex_enter(&dd->dd_activity_lock);
        completed=w.done;
        mutex_exit(&dd->dd_activity_lock);
        if(completed)break;
        Delay(1);
    }
    if(!completed) {
        mutex_enter(&dd->dd_activity_lock);
        dd->dd_activity_cancelled=B_TRUE;
        cv_broadcast(&dd->dd_activity_cv);
        mutex_exit(&dd->dd_activity_lock);
    }
    if (!cancel)CHECK(pthread_join(thread,NULL));
    mutex_enter(&dd->dd_activity_lock);
    dd->dd_activity_cancelled=B_FALSE;
    mutex_exit(&dd->dd_activity_lock);
    CHECK(error);CHECK(pending && completed && w.waited &&
        w.error==(cancel?EINTR:0) ? 0:EIO);
    CHECK(zap_count(os,fs->z_unlinkedobj,&count));CHECK(count ? EIO:0);
    zfs_vfs_rele(fs);
    CHECK(oz_zpl_close(mount));
    const char *s=cancel==2?
        "RESULT native filesystem deleteq suspend PASS pending=1 interrupted=1 resumed=1\n":cancel?
        "RESULT native filesystem deleteq cancel PASS pending=1 interrupted=1 republished=1\n":
        "RESULT native filesystem deleteq wait PASS pending=1 drained=1 waited=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int node_resume_test(const char *dataset)
{
    struct oz_zpl *mount=NULL;
    CHECK(oz_zpl_open_writable(dataset,&mount));
    uint64_t object,size;
    CHECK(oz_zpl_lookup(mount,"hello.txt",&object,&size));
    objset_t *os;
    CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs=dmu_objset_get_user(os);
    int error=zfs_vfs_ref(&fs);
    mutex_exit(&os->os_user_ptr_lock);
    dmu_objset_rele(os,FTAG);CHECK(error);
    CHECK(oz_zpl_sync(mount));
    ZFS_TEARDOWN_ENTER_WRITE(fs,FTAG);
    znode_t *zp=NULL;CHECK(zfs_zget(fs,object,&zp));oz_znode_hold(zp);
    uint64_t generation=zp->z_gen,mode=zp->z_mode,links=zp->z_links;
    uint64_t uid=zp->z_uid;
    CHECK(zp->z_refs==2 ? 0 : EIO);
    zp->z_xattr_cached=fnvlist_alloc();
    fnvlist_add_string(zp->z_xattr_cached,"fixture-stale","discard");
    zfs_znode_dmu_fini(zp);zfs_znode_dmu_fini(zp);
    CHECK(zfs_verify_zp(zp)==EIO ? 0 : EIO);
    zp->z_size=UINT64_MAX;zp->z_uid=UINT64_MAX;
    CHECK(zfs_rezget(zp));
    CHECK(zp->z_sa_hdl && zp->z_refs==2 && zp->z_size==size &&
        zp->z_uid==uid && !zp->z_xattr_cached ? 0 : EIO);
    zfs_znode_dmu_fini(zp);
    CHECK(resume_attribute(fs,object,SA_ZPL_GEN(fs),generation+1));
    CHECK(zfs_rezget(zp)==EIO && !zp->z_sa_hdl && zp->z_gen==generation ? 0 : EIO);
    CHECK(resume_attribute(fs,object,SA_ZPL_GEN(fs),generation));
    CHECK(resume_attribute(fs,object,SA_ZPL_MODE(fs),(mode&~S_IFMT)|S_IFDIR));
    CHECK(zfs_rezget(zp)==EIO && !zp->z_sa_hdl && zp->z_mode==mode ? 0 : EIO);
    CHECK(resume_attribute(fs,object,SA_ZPL_MODE(fs),mode));
    CHECK(resume_attribute(fs,object,SA_ZPL_LINKS(fs),0));
    CHECK(zfs_rezget(zp));
    CHECK(zp->z_unlinked && !zp->z_sa_hdl && zp->z_refs==2 ? 0 : EIO);
    CHECK(resume_attribute(fs,object,SA_ZPL_LINKS(fs),links));
    CHECK(zfs_rezget(zp));
    CHECK(!zp->z_unlinked && zp->z_sa_hdl && zp->z_refs==2 ? 0 : EIO);
    oz_znode_rele(zp);oz_znode_rele(zp);
    ZFS_TEARDOWN_EXIT_WRITE(fs,FTAG);
    zfs_vfs_rele(fs);
    CHECK(oz_zpl_sync(mount));CHECK(oz_zpl_close(mount));
    CHECK(oz_zpl_open(dataset,&mount));
    uint64_t reopened,bytes;CHECK(oz_zpl_lookup(mount,"hello.txt",&reopened,&bytes));
    CHECK(reopened==object && bytes==size ? 0 : EIO);
    CHECK(oz_zpl_close(mount));
    const char *s="RESULT native node resume PASS identity=3 refs=2\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static void *wrong_suspend_task(void *arg)
{return (void *)(uintptr_t)zfs_suspend_fs(arg);}
struct end_client { struct identity_client client; zfsvfs_t *fs; dsl_dataset_t *ds; };
static void *ended_reader(void *arg)
{
    struct end_client *test=arg;struct identity_client *c=&test->client;
    c->task=FindTask(NULL);
    if(zfs_end_fs(test->fs,test->ds)!=EDEADLK)c->error=EIO;
    BPTR file=Open((CONST_STRPTR)"OZENDTEST:hello.txt",MODE_OLDFILE);
    BPTR lock=Lock((CONST_STRPTR)"OZENDTEST:hello.txt",ACCESS_READ);
    if(!file||!lock)c->error=EIO;
    PutMsg(c->port,&c->done);Wait(SIGBREAKF_CTRL_E);
    char byte;struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(!fib)c->error=ENOMEM;
    if(file && (Read(file,&byte,1)!=-1 || IoErr()!=ERROR_DEVICE_NOT_MOUNTED))c->error=EIO;
    if(lock && fib && (Examine(lock,fib) || IoErr()!=ERROR_DEVICE_NOT_MOUNTED))c->error=EIO;
    if(file && !Close(file))c->error=EIO;
    if(lock)UnLock(lock);
    if(fib)FreeDosObject(DOS_FIB,fib);
    PutMsg(c->port,&c->done);
    return NULL;
}
static int mounted_end_test(const char *dataset)
{
    static struct oz_message m;
    memset(&m,0,sizeof(m));m.operation=43;m.flags=OZ_CONFIRMED;
    strcpy(m.target,dataset);strcpy(m.argument,"OZENDTEST");strcpy(m.options,"readonly=on");
    oz_engine_dispatch(&m);CHECK(m.status==OZ_OK?0:EIO);
    objset_t *os;CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);zfsvfs_t *fs=dmu_objset_get_user(os);
    int error=zfs_vfs_ref(&fs);mutex_exit(&os->os_user_ptr_lock);
    dsl_dataset_t *ds=dmu_objset_ds(os);dmu_objset_rele(os,FTAG);CHECK(error);
    CHECK(zfs_end_fs(fs,ds)==EINVAL?0:EIO); /* no matching suspend */
    struct end_client test={.fs=fs,.ds=ds};struct identity_client *c=&test.client;
    c->port=CreateMsgPort();CHECK(c->port?0:ENOMEM);
    c->done.mn_Node.ln_Type=NT_MESSAGE;c->done.mn_Length=sizeof(c->done);
    pthread_t thread;CHECK(pthread_create(&thread,NULL,ended_reader,&test));
    snapshot_wait(c->port);CHECK(c->error);
    znode_t *root=NULL;CHECK(zfs_zget(fs,fs->z_root,&root));
    CHECK(zfs_suspend_fs(fs));CHECK(zfs_end_fs(fs,ds));
    CHECK(fs->z_unmounted && !root->z_sa_hdl && !ZFS_TEARDOWN_WRITE_HELD(fs)?0:EIO);
    znode_t *rejected=NULL;CHECK(zfs_zget(fs,fs->z_root,&rejected)==EIO && !rejected?0:EIO);
    CHECK(zfs_resume_fs(fs,ds)==EINVAL?0:EIO);
    oz_engine_pump(); /* busy DOS references must not free the ended mount */
    CHECK(ds->ds_owner==fs?0:EIO);
    Signal(c->task,SIGBREAKF_CTRL_E);snapshot_wait(c->port);
    CHECK(pthread_join(thread,NULL));CHECK(c->error);DeleteMsgPort(c->port);
    CHECK(ds->ds_owner==fs?0:EIO); /* our native references still protect it */
    oz_znode_rele(root);zfs_vfs_rele(fs);oz_engine_pump();
    CHECK(dmu_objset_hold(dataset,FTAG,&os));
    CHECK(!dsl_dataset_has_owner(dmu_objset_ds(os))?0:EIO);
    dmu_objset_rele(os,FTAG);
    struct oz_zpl *fresh=NULL;CHECK(oz_zpl_open(dataset,&fresh));
    uint64_t object,size;char byte;CHECK(oz_zpl_lookup(fresh,"hello.txt",&object,&size));
    CHECK(oz_zpl_read(fresh,object,0,&byte,1));CHECK(byte=='O'?0:EIO);
    CHECK(oz_zpl_close(fresh));
    const char *s="RESULT native mounted end PASS stale-io=1 cleanup=1 disown=1 reopen=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int mounted_rollback_test(const char *dataset)
{
    struct oz_zpl *mount=NULL;CHECK(oz_zpl_open_writable(dataset,&mount));
    uint64_t object,size;CHECK(oz_zpl_lookup(mount,"hello.txt",&object,&size));
    char original[16],actual[16];CHECK(oz_zpl_read(mount,object,0,original,sizeof(original)));
    CHECK(oz_zpl_sync(mount));
    char snap[128];snprintf(snap,sizeof(snap),"%s@native-resume",dataset);
    nvlist_t *snaps=fnvlist_alloc(),*errors=fnvlist_alloc();fnvlist_add_boolean(snaps,snap);
    CHECK(dsl_dataset_snapshot(snaps,NULL,errors));fnvlist_free(snaps);fnvlist_free(errors);
    objset_t *os;CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);zfsvfs_t *fs=dmu_objset_get_user(os);
    int error=zfs_vfs_ref(&fs);mutex_exit(&os->os_user_ptr_lock);
    dmu_objset_rele(os,FTAG);CHECK(error);
    pthread_t thread;void *result=NULL;
    CHECK(pthread_create(&thread,NULL,wrong_suspend_task,fs));CHECK(pthread_join(thread,&result));
    CHECK((uintptr_t)result==EDEADLK ? 0 : EIO);
    for(unsigned round=0;round<2;round++) {
        size_t written=0;const char changed[]="changed after snapshot";
        CHECK(oz_zpl_write_as(mount,object,0,changed,sizeof(changed),&written,kcred));
        CHECK(written==sizeof(changed)?0:EIO);
        struct oz_zpl_info added;CHECK(oz_zpl_create_as(mount,oz_zpl_root(mount),"after-snapshot",0644,kcred,&added));
        znode_t *kept=NULL,*removed=NULL;
        CHECK(zfs_zget(fs,object,&kept));CHECK(zfs_zget(fs,added.object,&removed));
        dsl_dataset_t *ds=dmu_objset_ds(fs->z_os);
        CHECK(zfs_suspend_fs(fs));
        CHECK(!fs->z_os && zfs_vfs_held(fs) && !kept->z_sa_hdl && !removed->z_sa_hdl ? 0:EIO);
        CHECK(oz_zpl_close(mount)==EBUSY ? 0:EIO);
        nvlist_t *out=fnvlist_alloc();
        error=dsl_dataset_rollback(dataset,snap,fs,out);
        int resumed=zfs_resume_fs(fs,ds);fnvlist_free(out);
        CHECK(error);CHECK(resumed);
        CHECK(fs->z_os && dmu_objset_ds(fs->z_os)->ds_owner==fs &&
            !zfs_verify_zp(kept) && zfs_verify_zp(removed)==EIO ? 0:EIO);
        oz_znode_rele(kept);oz_znode_rele(removed);
        CHECK(oz_zpl_read(mount,object,0,actual,sizeof(actual)));
        CHECK(!memcmp(actual,original,sizeof(actual))?0:EIO);
        CHECK(oz_zpl_lookup(mount,"after-snapshot",&added.object,&added.size)==ENOENT?0:EIO);
    }
    /* A failed upstream operation must still be resumable without losing
     * the mount or retaining the teardown writer lock. */
    dsl_dataset_t *ds=dmu_objset_ds(fs->z_os);CHECK(zfs_suspend_fs(fs));
    nvlist_t *out=fnvlist_alloc();
    error=dsl_dataset_rollback(dataset,"missing@snapshot",fs,out);
    int resumed=zfs_resume_fs(fs,ds);fnvlist_free(out);
    CHECK(error?0:EIO);CHECK(resumed);
    CHECK(oz_zpl_read(mount,object,0,actual,sizeof(actual)));
    CHECK(!memcmp(actual,original,sizeof(actual))?0:EIO);
    zfs_vfs_rele(fs);CHECK(oz_zpl_close(mount));
    const char *s="RESULT native mounted rollback PASS rounds=2 failed-resume=1 owner-task=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int project_attributes_test(const char *pool, int write)
{
#define PROJECT_STEP(text) do {const char *p="Project stage: " text "\n";while(*p)RawPutChar(*p++);}while(0)
    PROJECT_STEP("enter");
    char dataset[192];snprintf(dataset,sizeof(dataset),"%s/project-attributes",pool);
    if(write){
        spa_t *spa=NULL;CHECK(spa_open(pool,&spa,FTAG));
        nvlist_t *features=fnvlist_alloc();
        fnvlist_add_uint64(features,"feature@project_quota",0);
        fnvlist_add_uint64(features,"feature@userobj_accounting",0);
        PROJECT_STEP("enable features");
        int result=spa_prop_set(spa,features);fnvlist_free(features);spa_close(spa,FTAG);CHECK(result);
        PROJECT_STEP("create fixture");
        CHECK(oz_zpl_fixture_create(dataset));
    }
    struct oz_zpl *mount=NULL;
    PROJECT_STEP("open mount");
    CHECK(write?oz_zpl_open_writable(dataset,&mount):oz_zpl_open(dataset,&mount));
    objset_t *os=NULL;CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs=dmu_objset_get_user(os);int result=zfs_vfs_ref(&fs);
    mutex_exit(&os->os_user_ptr_lock);dmu_objset_rele(os,FTAG);CHECK(result);
    znode_t *root=NULL;CHECK(zfs_zget(fs,fs->z_root,&root));
    PROJECT_STEP("root acquired");
    xvattr_t xva;xva_init(&xva);XVA_SET_REQ(&xva,XAT_PROJID);XVA_SET_REQ(&xva,XAT_PROJINHERIT);
    xva.xva_xoptattrs.xoa_projid=1000;xva.xva_xoptattrs.xoa_projinherit=1;
    if(write){
        xvattr_t invalid=xva;invalid.xva_mapsize++;
        CHECK(zfs_setattr(root,&invalid.xva_vattr,0,kcred,NULL)==EINVAL?0:EIO);
        invalid=xva;XVA_SET_REQ(&invalid,XAT_IMMUTABLE);
        CHECK(zfs_setattr(root,&invalid.xva_vattr,0,kcred,NULL)==ENOTSUP?0:EIO);
        invalid=xva;invalid.xva_xoptattrs.xoa_projid=UINT32_MAX;
        CHECK(zfs_setattr(root,&invalid.xva_vattr,0,kcred,NULL)==EINVAL?0:EIO);
        cred_t *other=oz_cred_create(77,77,88,0,NULL);CHECK(other?0:ENOMEM);
        int denied=zfs_setattr(root,&xva.xva_vattr,0,other,NULL);oz_cred_release(other);
        CHECK(denied==EACCES && root->z_projid==0?0:EIO);
        PROJECT_STEP("set root project");
        char address[160];snprintf(address,sizeof(address),"Project setattr=%p root=%p fs=%p\n",(void *)zfs_setattr,(void *)root,(void *)fs);
        for(char *p=address;*p;p++)RawPutChar(*p);
        CHECK(zfs_setattr(root,&xva.xva_vattr,0,kcred,NULL));
        CHECK(xva.xva_rtnattrmap[0]==0?0:EIO); /* caller request is untouched */
        uint32_t observed=99;int inherited=99;
        CHECK(oz_zpl_project_get(mount,root->z_id,&observed,&inherited,NULL)==EACCES?0:EIO);
        CHECK(observed==99&&inherited==99?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,1000,2,kcred)==EINVAL?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,UINT32_MAX,1,kcred)==EINVAL?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,1000,1,NULL)==EACCES?0:EIO);
        other=oz_cred_create(77,77,88,0,NULL);CHECK(other?0:ENOMEM);
        denied=oz_zpl_project_set(mount,root->z_id,1001,0,other);oz_cred_release(other);
        CHECK(denied==EACCES?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,1000,0,kcred));
        CHECK(oz_zpl_project_get(mount,root->z_id,&observed,&inherited,kcred));
        CHECK(observed==1000&&!inherited?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,1000,1,kcred));
        struct oz_zpl_info child;
        CHECK(oz_zpl_create_as(mount,fs->z_root,"inherited-project",0644,kcred,&child));
        CHECK(oz_zpl_sync(mount));
        const zfs_prop_t limits[]={ZFS_PROP_DEFAULTPROJECTQUOTA,ZFS_PROP_DEFAULTPROJECTOBJQUOTA};
        for(unsigned kind=0;kind<2;kind++){
            CHECK(zfs_set_default_quota(fs,limits[kind],1));
            CHECK(oz_zpl_create_as(mount,fs->z_root,"quota-blocked",0644,kcred,&child)==EDQUOT?0:EIO);
            CHECK(zfs_set_default_quota(fs,limits[kind],0));
            CHECK(oz_zpl_create_as(mount,fs->z_root,"quota-blocked",0644,kcred,&child));
            CHECK(oz_zpl_remove_as(mount,fs->z_root,"quota-blocked",0,kcred));
            CHECK(oz_zpl_sync(mount));
        }
        PROJECT_STEP("nonzero project quota byte/object enforcement passed");
    }else {
        CHECK(zfs_setattr(root,&xva.xva_vattr,0,kcred,NULL)==EROFS?0:EIO);
        CHECK(oz_zpl_project_set(mount,root->z_id,1001,0,kcred)==EROFS?0:EIO);
    }
    uint32_t observed=0;int inherited=0;
    CHECK(oz_zpl_project_get(mount,root->z_id,&observed,&inherited,kcred));
    CHECK(observed==1000&&inherited?0:EIO);
    uint64_t stored=0;CHECK(sa_lookup(root->z_sa_hdl,SA_ZPL_PROJID(fs),&stored,8));
    CHECK(stored==1000 && root->z_projid==1000 && (root->z_pflags&ZFS_PROJINHERIT)?0:EIO);
    uint64_t entry;CHECK(zap_lookup(fs->z_os,fs->z_root,"inherited-project",8,1,&entry));
    znode_t *child=NULL;CHECK(zfs_zget(fs,ZFS_DIRENT_OBJ(entry),&child));
    CHECK(child->z_projid==1000?0:EIO);
    CHECK(sa_lookup(child->z_sa_hdl,SA_ZPL_PROJID(fs),&stored,8));CHECK(stored==1000?0:EIO);
    oz_znode_rele(child);oz_znode_rele(root);zfs_vfs_rele(fs);CHECK(oz_zpl_close(mount));
    char text[128];snprintf(text,sizeof(text),"RESULT native project attributes PASS write=%d inherited=1 stored=1\n",write);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return 0;
#undef PROJECT_STEP
}
static int phase(const char *dataset, int stage)
{
    struct oz_zpl *mount = NULL;
    CHECK(stage == 3 ? oz_zpl_open(dataset, &mount) :
        oz_zpl_open_writable(dataset, &mount));
    struct oz_zpl *duplicate=NULL;
    CHECK(oz_zpl_open(dataset,&duplicate)==EBUSY && !duplicate ? 0 : EIO);
    objset_t *os;
    CHECK(dmu_objset_hold(dataset, FTAG, &os));
    mutex_enter(&os->os_user_ptr_lock);
    zfsvfs_t *fs = dmu_objset_get_user(os);
    int ref_error = zfs_vfs_ref(&fs);
    mutex_exit(&os->os_user_ptr_lock);
    dmu_objset_rele(os, FTAG);
    CHECK(ref_error);
    CHECK(dmu_objset_ds(fs->z_os)->ds_owner==fs ? 0 : EIO);
    CHECK(dsl_dataset_long_held(dmu_objset_ds(fs->z_os)) ? 0 : EIO);
    CHECK(oz_zpl_close(mount)==EBUSY ? 0 : EIO);
    CHECK(dmu_objset_ds(fs->z_os)->ds_owner==fs ? 0 : EIO);
    ZFS_TEARDOWN_ENTER_WRITE(fs, FTAG);
    if(stage==3) {
        uint64_t user_bytes=0,group_bytes=0;
        CHECK(zfs_userspace_one(fs,ZFS_PROP_USERUSED,"",0,&user_bytes));
        CHECK(zfs_userspace_one(fs,ZFS_PROP_GROUPUSED,"",0,&group_bytes));
        CHECK(user_bytes>0 && group_bytes>0 ? 0:EIO);
        const char *accounting="RESULT native ZPL accounting PASS persisted-user=1 persisted-group=1\n";
        while(*accounting)RawPutChar(*accounting++);
    }
    uint64_t *cached[] = { &fs->z_defaultuserquota, &fs->z_defaultgroupquota,
        &fs->z_defaultprojectquota, &fs->z_defaultuserobjquota,
        &fs->z_defaultgroupobjquota, &fs->z_defaultprojectobjquota };
    CHECK(zfs_set_default_quota(fs, ZFS_PROP_VERSION, 1) == EINVAL ? 0 : EIO);
    for (unsigned i=0; i<ARRAY_SIZE(props); i++) {
        uint64_t value = (1ULL<<33) + i + 1, disk = 0;
        if (stage == 3) {
            CHECK(*cached[i] == 0 ? 0 : EIO);
            CHECK(zfs_set_default_quota(fs, props[i], value) == EROFS ? 0 : EIO);
            CHECK(zap_lookup(fs->z_os, MASTER_NODE_OBJ,
                zfs_prop_to_name(props[i]), 8, 1, &disk) == ENOENT ? 0 : EIO);
            CHECK(*cached[i] == 0 ? 0 : EIO);
            continue;
        }
        if (stage == 1) CHECK(*cached[i] == value ? 0 : EIO);
        else CHECK(*cached[i] == 0 ? 0 : EIO);
        if (stage != 2) {
            CHECK(zfs_set_default_quota(fs, props[i], stage ? 0 : value));
            CHECK(*cached[i] == (stage ? 0 : value) ? 0 : EIO);
            if (stage) CHECK(zfs_set_default_quota(fs, props[i], 0));
        }
        int error = zap_lookup(fs->z_os, MASTER_NODE_OBJ,
            zfs_prop_to_name(props[i]), 8, 1, &disk);
        CHECK(stage ? (error == ENOENT ? 0 : EIO) : error);
        if (!stage) CHECK(disk == value ? 0 : EIO);
        fs->z_readonly = B_TRUE;
        CHECK(zfs_set_default_quota(fs, props[i], stage ? 0 : value));
        fs->z_readonly = B_FALSE;
        CHECK(*cached[i] == (stage ? 0 : value) ? 0 : EIO);
    }
    if (stage != 3) txg_wait_synced(dmu_objset_pool(fs->z_os), 0);
    ZFS_TEARDOWN_EXIT_WRITE(fs, FTAG);
    zfs_vfs_rele(fs);
    CHECK(oz_zpl_close(mount));
    CHECK(dmu_objset_hold(dataset,FTAG,&os));
    mutex_enter(&os->os_user_ptr_lock);
    boolean_t unpublished=!dmu_objset_get_user(os);
    mutex_exit(&os->os_user_ptr_lock);
    CHECK(!dsl_dataset_has_owner(dmu_objset_ds(os)) && unpublished ? 0 : EIO);
    dmu_objset_rele(os,FTAG);
    return 0;
}
#ifdef OZ_NATIVE_DISPATCHER
static int native_creation_ioctl_test(const char *pool)
{
    char parent[160],child[180];snprintf(parent,sizeof(parent),"%s/iocnames",pool);
    snprintf(child,sizeof(child),"%s/child",parent);
    struct oz_cred_scope *scope=NULL;CHECK(oz_cred_push(kcred,&scope));
    for(unsigned step=0;step<2;step++) {
        nvlist_t *in=fnvlist_alloc(),*props=fnvlist_alloc();
        fnvlist_add_int32(in,"type",DMU_OST_ZFS);
        if(!step) {
            fnvlist_add_uint64(props,"normalization",U8_TEXTPREP_NFC);
            fnvlist_add_uint64(props,"utf8only",1);
            fnvlist_add_uint64(props,"casesensitivity",ZFS_CASE_INSENSITIVE);
            fnvlist_add_nvlist(in,"props",props);
        }
        char *packed=NULL;size_t size=0;
        CHECK(nvlist_pack(in,&packed,&size,NV_ENCODE_NATIVE,0));
        zfs_cmd_t *zc=calloc(1,sizeof(*zc));char *output=malloc(65536);
        CHECK(zc&&output?0:ENOMEM);
        strlcpy(zc->zc_name,step?child:parent,sizeof(zc->zc_name));
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
        zc->zc_nvlist_dst=(uintptr_t)output;zc->zc_nvlist_dst_size=65536;
        struct oz_ioctl_range ranges[]={{packed,size,OZ_IOCTL_INPUT},{output,65536,OZ_IOCTL_OUTPUT}};
        struct oz_ioctl_copy_scope *copy=NULL;CHECK(oz_ioctl_copy_enter(ranges,2,&copy));
        int error=zfsdev_ioctl_common(ZFS_IOC_CREATE-ZFS_IOC_FIRST,zc,FKIOCTL);
        CHECK(oz_ioctl_copy_leave(copy));free(output);free(zc);free(packed);
        fnvlist_free(props);fnvlist_free(in);CHECK(error);
        objset_t *os=NULL;CHECK(dmu_objset_hold(step?child:parent,FTAG,&os));
        uint64_t norm,utf8,sense;
        CHECK(zfs_get_zplprop(os,ZFS_PROP_NORMALIZE,&norm));
        CHECK(zfs_get_zplprop(os,ZFS_PROP_UTF8ONLY,&utf8));
        CHECK(zfs_get_zplprop(os,ZFS_PROP_CASE,&sense));
        dmu_objset_rele(os,FTAG);
        CHECK(norm==U8_TEXTPREP_NFC&&utf8==1&&sense==ZFS_CASE_INSENSITIVE?0:EIO);
        struct oz_read_mount *mount=NULL;struct oz_zpl_info made,found;
        CHECK(oz_read_mount_open_dataset(step?child:parent,B_FALSE,&mount));
        CHECK(oz_read_mount_create(mount,oz_read_mount_root(mount),"caf\xc3\xa9",0,kcred,&made));
        CHECK(oz_read_mount_lookup(mount,oz_read_mount_root(mount),"CAFE\xcc\x81",&found));
        CHECK(made.object==found.object?0:EIO);CHECK(oz_read_mount_close(mount));
    }
    char typed[200];snprintf(typed,sizeof(typed),"%s/typed",parent);
    CHECK(oz_zpl_dataset_create_options(typed,"utf8only=on,utf8only=off",kcred)==EINVAL?0:EIO);
    CHECK(oz_zpl_dataset_create_options(typed,"unknown=on",kcred)==ENOTSUP?0:EIO);
    objset_t *absent=NULL;
    CHECK(dmu_objset_hold(typed,FTAG,&absent)==ENOENT?0:EIO);
    CHECK(oz_zpl_dataset_create_options(typed,"normalization=formD,utf8only=on,casesensitivity=mixed",kcred));
    CHECK(dmu_objset_hold(typed,FTAG,&absent));
    uint64_t norm,sense;CHECK(zfs_get_zplprop(absent,ZFS_PROP_NORMALIZE,&norm));
    CHECK(zfs_get_zplprop(absent,ZFS_PROP_CASE,&sense));
    dmu_objset_rele(absent,FTAG);CHECK(norm==U8_TEXTPREP_NFD&&sense==ZFS_CASE_MIXED?0:EIO);
    CHECK(oz_cred_pop(scope));
    const char *s="RESULT native upstream create PASS properties=3 inherited=3 lookup=2\n";
    while(*s)RawPutChar(*s++);return 0;
}
static int native_dispatcher_test(const char *pool)
{
    /* Trusted resident buffers only; this is not a client pointer transport. */
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));CHECK(zc?0:ENOMEM);
    char *buffer=malloc(1024*1024);CHECK(buffer?0:ENOMEM);
    struct oz_cred_scope *scope=NULL;
    CHECK(oz_cred_push(kcred,&scope));
    /* The full service startup, not this fixture, owns initial activation. */
    CHECK(zfs_ioctl_native_init()==EBUSY?0:EIO);
    CHECK(oz_control_start()==EBUSY?0:EIO);
    zfs_file_t *held=NULL;CHECK(oz_control_file_open(&held));
    CHECK(!oz_engine_stop()?0:EIO);
    zfs_file_close(held);
    CHECK(oz_control_stop());CHECK(zfs_ioctl_native_fini());
    /* Exceed the SDK's 64 TLS keys to expose missing per-cycle deletion. */
    for(unsigned round=0;round<80;round++) {
        CHECK(zfs_ioctl_native_init());
        CHECK(zfs_ioctl_native_init()==EBUSY?0:EIO);
        CHECK(oz_control_start());
        zfs_file_t *control=NULL;CHECK(oz_control_file_open(&control));
        CHECK(oz_control_stop()==EBUSY?0:EIO);
        memset(zc,0,sizeof(*zc));
        strlcpy(zc->zc_name,pool,sizeof(zc->zc_name));
        zc->zc_nvlist_dst=(uintptr_t)buffer;zc->zc_nvlist_dst_size=1024*1024;
        CHECK(zfsdev_ioctl_common(ZFS_IOC_POOL_STATS-ZFS_IOC_FIRST,zc,FKIOCTL)==EFAULT?0:EIO);
        zc->zc_nvlist_dst_size=1024*1024;
        struct oz_ioctl_range range={buffer,1024*1024,OZ_IOCTL_OUTPUT};
        struct oz_ioctl_copy_scope *copy=NULL;
        CHECK(oz_ioctl_copy_enter(&range,1,&copy));
        int error=zfsdev_ioctl_common(ZFS_IOC_POOL_STATS-ZFS_IOC_FIRST,zc,FKIOCTL);
        CHECK(oz_ioctl_copy_leave(copy));CHECK(error);
        CHECK(zc->zc_cookie==0 && zc->zc_nvlist_dst_size>0 &&
            zc->zc_nvlist_dst_size<=1024*1024?0:EIO);
        nvlist_t *stats=NULL;CHECK(nvlist_unpack(buffer,zc->zc_nvlist_dst_size,&stats,0));
        const char *reported=NULL;CHECK(nvlist_lookup_string(stats,ZPOOL_CONFIG_POOL_NAME,&reported));
        CHECK(!strcmp(pool,reported)?0:EIO);fnvlist_free(stats);
        CHECK(zfsdev_ioctl_common(ZFS_IOC_LAST-ZFS_IOC_FIRST,zc,FKIOCTL)==ZFS_ERR_IOC_CMD_UNAVAIL?0:EIO);
        zfs_file_close(control);CHECK(oz_control_stop());
        CHECK(zfs_ioctl_native_fini());
        CHECK(zfs_ioctl_native_fini()==ENODEV?0:EIO);
        CHECK(zfsdev_ioctl_common(ZFS_IOC_POOL_STATS-ZFS_IOC_FIRST,zc,FKIOCTL)==ZFS_ERR_IOC_CMD_UNAVAIL?0:EIO);
    }
    /* Restore the lifecycle owned by oz_engine_stop. */
    CHECK(zfs_ioctl_native_init());CHECK(oz_control_start());
    CHECK(oz_cred_pop(scope));free(buffer);free(zc);
    const char *s="RESULT native dispatcher runtime PASS cycles=80 upstream-stats=1 stopped-denial=1\n";
    while(*s)RawPutChar(*s++);
    return 0;
}
static int native_volume_test(const char *pool)
{
    char name[160],path[170];snprintf(name,sizeof(name),"%s/volume",pool);
    snprintf(path,sizeof(path),"ZVOL:%s",name);
    nvlist_t *props=fnvlist_alloc();
    fnvlist_add_uint64(props,"volsize",1024*1024);
    fnvlist_add_uint64(props,"volblocksize",8192);
    zfs_creat_t create={.zct_props=props};
    struct oz_cred_scope *scope=NULL;CHECK(oz_cred_push(kcred,&scope));
    int error=dmu_objset_create(name,DMU_OST_ZVOL,0,NULL,zvol_create_cb,&create);
    CHECK(oz_cred_pop(scope));fnvlist_free(props);CHECK(error);
    /* dmu_objset_create synchronously completes upstream minor creation. */
    CHECK(zvol_is_zvol(path)?0:EIO);
    CHECK(zvol_os_create_minor(name)==EEXIST?0:EIO);
    struct oz_zvol_handle *handle=NULL;
    CHECK(oz_zvol_open(name,B_TRUE,NULL,&handle)==EACCES && !handle?0:EIO);
    CHECK(oz_zvol_open(name,B_TRUE,kcred,&handle));
    uint64_t size=0,block=0;boolean_t readonly;
    CHECK(oz_zvol_info(handle,&size,&block,&readonly));
    CHECK(size==1024*1024 && block==8192 && !readonly?0:EIO);
    unsigned char input[512],actual[512];for(unsigned i=0;i<sizeof(input);i++)input[i]=(unsigned char)(i*7);
    size_t done=0;
    CHECK(oz_zvol_io(handle,1,input,sizeof(input),B_TRUE,&done)==EINVAL && !done?0:EIO);
    CHECK(oz_zvol_io(handle,size,input,sizeof(input),B_TRUE,&done)==EINVAL && !done?0:EIO);
    CHECK(oz_zvol_io(handle,512,input,sizeof(input),B_TRUE,&done));CHECK(done==sizeof(input)?0:EIO);
    CHECK(oz_zvol_flush(handle));
    CHECK(oz_zvol_io(handle,512,actual,sizeof(actual),B_FALSE,&done));
    CHECK(done==sizeof(actual) && !memcmp(input,actual,sizeof(input))?0:EIO);
    CHECK(spa_export(pool,NULL,B_FALSE,B_FALSE)==EBUSY?0:EIO);
    CHECK(zvol_set_volsize(name,2*1024*1024));
    CHECK(oz_zvol_info(handle,&size,&block,&readonly));CHECK(size==2*1024*1024?0:EIO);
    oz_zvol_close(handle);
    zfs_file_t *file=NULL;CHECK(zfs_file_open(path,O_RDONLY,0,kcred,&file));
    zfs_file_attr_t attr;CHECK(zfs_file_getattr(file,&attr));
    CHECK(attr.zfa_size==size && S_ISBLK(attr.zfa_mode)?0:EIO);
    ssize_t resid=sizeof(actual);CHECK(zfs_file_pread(file,actual,sizeof(actual),512,&resid));
    CHECK(!resid && !memcmp(input,actual,sizeof(input))?0:EIO);
    CHECK(zfs_file_pwrite(file,input,sizeof(input),512,9,&resid)==EROFS?0:EIO);
    zfs_file_close(file);
    spa_t *spa=NULL;CHECK(spa_open(pool,&spa,FTAG));
    char renamed[160],newpath[170],snapshot[180],snappath[190];
    snprintf(renamed,sizeof(renamed),"%s/renamed-volume",pool);
    snprintf(newpath,sizeof(newpath),"ZVOL:%s",renamed);
    CHECK(oz_cred_push(kcred,&scope));
    error=dsl_dir_rename(name,renamed);
    CHECK(oz_cred_pop(scope));CHECK(error);
    /* Rename schedules native minors from the syncing transaction. */
    taskq_wait(spa->spa_zvol_taskq);
    CHECK(!zvol_is_zvol(path) && zvol_is_zvol(newpath)?0:EIO);
    CHECK(oz_zvol_open(name,B_FALSE,kcred,&handle)==ENOENT?0:EIO);
    CHECK(oz_zvol_open(renamed,B_TRUE,kcred,&handle));
    CHECK(oz_zvol_io(handle,512,actual,sizeof(actual),B_FALSE,&done));
    CHECK(done==sizeof(actual) && !memcmp(input,actual,sizeof(input))?0:EIO);
    oz_zvol_close(handle);
    CHECK(dsl_prop_set_int(renamed,"snapdev",ZPROP_SRC_LOCAL,ZFS_SNAPDEV_VISIBLE));
    snprintf(snapshot,sizeof(snapshot),"%s@native-volume",renamed);
    snprintf(snappath,sizeof(snappath),"ZVOL:%s",snapshot);
    nvlist_t *snaps=fnvlist_alloc(),*errors=fnvlist_alloc();
    fnvlist_add_boolean(snaps,snapshot);
    CHECK(oz_cred_push(kcred,&scope));
    error=dsl_dataset_snapshot(snaps,NULL,errors);
    CHECK(oz_cred_pop(scope));fnvlist_free(snaps);fnvlist_free(errors);CHECK(error);
    CHECK(zvol_is_zvol(snappath)?0:EIO);
    CHECK(oz_zvol_open(snapshot,B_TRUE,kcred,&handle)==EROFS && !handle?0:EIO);
    CHECK(oz_zvol_open(snapshot,B_FALSE,kcred,&handle));
    CHECK(oz_zvol_io(handle,512,actual,sizeof(actual),B_FALSE,&done));
    CHECK(done==sizeof(actual) && !memcmp(input,actual,sizeof(input))?0:EIO);
    CHECK(oz_zvol_io(handle,512,input,sizeof(input),B_TRUE,&done)==EROFS && !done?0:EIO);
    oz_zvol_close(handle);
    zvol_remove_minors(spa,renamed,B_FALSE);
    CHECK(!zvol_is_zvol(newpath) && !zvol_is_zvol(snappath)?0:EIO);
    /* Rebuild registration from actual datasets, not retained native buffers. */
    zvol_create_minors(renamed);
    CHECK(zvol_is_zvol(newpath) && zvol_is_zvol(snappath)?0:EIO);
    CHECK(oz_zvol_open(renamed,B_FALSE,kcred,&handle));
    CHECK(oz_zvol_io(handle,512,actual,sizeof(actual),B_FALSE,&done));
    CHECK(done==sizeof(actual) && !memcmp(input,actual,sizeof(input))?0:EIO);
    oz_zvol_close(handle);
    zvol_remove_minors(spa,renamed,B_FALSE);
    zvol_remove_minors(spa,name,B_FALSE);spa_close(spa,FTAG);
    CHECK(!zvol_is_zvol(path) && oz_zvol_open(name,B_FALSE,kcred,&handle)==ENOENT?0:EIO);
    const char *result="RESULT native zvol PASS write=1 reopen=1 readonly=1 resize=1 export-busy=1\n";
    while(*result)RawPutChar(*result++);
    result="RESULT native zvol lifecycle PASS rename=1 snapshot-readonly=1 recreate=1\n";
    while(*result)RawPutChar(*result++);
    return 0;
}
#endif
static int run(void)
{
    FILE *marker = fopen("SYS:openzfs-quota-test-only", "r");
    if (!marker) return EACCES;
    fclose(marker);
    CHECK(platform_primitives_test());
    CHECK(oz_cred_test());
    CHECK(oz_caller_test());
    const char *caller_result="RESULT native caller scopes PASS threads=16\n";
    while(*caller_result)RawPutChar(*caller_result++);
    const char *policy_result="RESULT native dispatcher credentials PASS\n";
    while(*policy_result)RawPutChar(*policy_result++);
    uint32_t nonce; CHECK(getentropy(&nonce, sizeof(nonce)) ? EIO : 0);
    char dir[96], path[128], pool[64], dataset[96], cache[128];
    snprintf(dir,sizeof(dir),"/RAM/oz-quota-%08x",nonce);
    CHECK(mkdir(dir,0700) ? errno : 0);
    snprintf(pool,sizeof(pool),"quota_%08x",nonce);
    snprintf(dataset,sizeof(dataset),"%s/files",pool);
    snprintf(cache,sizeof(cache),"%s/cache",dir); spa_config_path=cache;
    CHECK(setenv("ZFS_HOSTID","0x514f5441",1) ? errno : 0);
    nvlist_t *leaf[2], *mirror=fnvlist_alloc(), *root=fnvlist_alloc(), *options=fnvlist_alloc();
    for(unsigned i=0;i<2;i++) {
        snprintf(path,sizeof(path),"%s/disk%u",dir,i);
        int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);
        if(fd<0)return errno;
        CHECK(ftruncate(fd,128LL*1024*1024) ? errno : 0);
        CHECK(close(fd) ? errno : 0);
        leaf[i]=fnvlist_alloc();
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_FILE);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_PATH,path);
        fnvlist_add_uint64(leaf[i],ZPOOL_CONFIG_ASHIFT,12);
    }
    fnvlist_add_string(mirror,ZPOOL_CONFIG_TYPE,VDEV_TYPE_MIRROR);
    fnvlist_add_nvlist_array(mirror,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)leaf,2);
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&mirror,1);
    fnvlist_add_string(options,"cachefile","none");
    fnvlist_add_uint64(options,"version",SPA_VERSION_USERSPACE);
    CHECK(oz_engine_start(1) ? 0 : EIO);
    CHECK(file_scope_test(dir));
    CHECK(session_files_test());
    CHECK(spa_create(pool,root,options,NULL,NULL));
#ifdef OZ_NATIVE_DISPATCHER
    CHECK(native_volume_test(pool));
    CHECK(native_dispatcher_test(pool));
#endif
    CHECK(creation_properties_test(pool));
    CHECK(legacy_versions_test(pool));
    CHECK(version_test(pool));
#ifdef OZ_NATIVE_DISPATCHER
    CHECK(native_creation_ioctl_test(pool));
#endif
    /* version_test upgrades the deliberately legacy pool to SA support. */
    CHECK(utf8_mount_test(pool,0,ZFS_CASE_SENSITIVE));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFC,ZFS_CASE_SENSITIVE));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFD,ZFS_CASE_SENSITIVE));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFKC,ZFS_CASE_SENSITIVE));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFKD,ZFS_CASE_SENSITIVE));
    CHECK(utf8_mount_test(pool,0,ZFS_CASE_INSENSITIVE));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFC,ZFS_CASE_INSENSITIVE));
    CHECK(utf8_mount_test(pool,0,ZFS_CASE_MIXED));
    CHECK(utf8_mount_test(pool,U8_TEXTPREP_NFC,ZFS_CASE_MIXED));
    CHECK(oz_zpl_fixture_create(dataset));
    objset_t *accounted;
    CHECK(dmu_objset_hold(dataset,FTAG,&accounted));
    CHECK(dmu_objset_userused_enabled(accounted) ? 0 : EIO);
    /* The upstream decoder must really be installed, not just a cached
     * initialized flag. Legacy bonus fields exercise the dispatcher here;
     * native SA accounting is exercised by the subsequent real writes. */
    znode_phys_t bonus={0};zfs_file_info_t info={0};
    bonus.zp_uid=123;bonus.zp_gid=456;bonus.zp_gen=789;
    CHECK(dmu_get_file_info(accounted,DMU_OT_ZNODE,&bonus,&info));
    CHECK(info.zfi_user==123 && info.zfi_group==456 && info.zfi_generation==789 ? 0:EIO);
    dmu_objset_rele(accounted,FTAG);
    CHECK(deleteq_wait_test(dataset,2));
    CHECK(deleteq_wait_test(dataset,B_TRUE));
    CHECK(deleteq_wait_test(dataset,B_FALSE));
    CHECK(node_resume_test(dataset));
    for(unsigned phase=0;phase<3;phase++)CHECK(dos_identity_test(dataset,phase));
    const char *identity="RESULT native DOS identity PASS cases=3 stale-parent=1 persistent=1 cleanup=1 reopen=1\n";
    while(*identity)RawPutChar(*identity++);
    CHECK(mounted_rollback_test(dataset));
    CHECK(mounted_end_test(dataset));
    CHECK(temporary_holds(dataset));
    for(int stage=0;stage<3;stage++) CHECK(phase(dataset,stage));
    CHECK(snapshot_unmount_test(dataset));
    CHECK(project_attributes_test(pool,1));
    nvlist_t *exported = NULL, *import_options = fnvlist_alloc();
    CHECK(spa_export(pool,&exported,B_FALSE,B_FALSE));
    fnvlist_add_uint64(import_options,"readonly",1);
    fnvlist_add_string(import_options,"cachefile","none");
    CHECK(spa_import(pool,exported,import_options,0));
    CHECK(phase(dataset,3));
    CHECK(project_attributes_test(pool,0));
    const char *ownership="RESULT native mount ownership PASS phases=4 snapshot=1 rejected=2\n";
    while(*ownership)RawPutChar(*ownership++);
    fnvlist_free(import_options); fnvlist_free(exported);
    CHECK(spa_export(pool,NULL,B_FALSE,B_FALSE));
    /* Native retired DOS endpoints must remain alive for late packets. */
    CHECK(oz_engine_stop()==0 ? 0 : EIO);
    for(unsigned i=0;i<2;i++) fnvlist_free(leaf[i]);
    fnvlist_free(mirror); fnvlist_free(root); fnvlist_free(options);
    return 0;
}
int main(void)
{
    int error=run();
    const char *s=error ? "RESULT native default quota FAIL\n" :
        "RESULT native default quota PASS properties=6 phases=4\n";
    while(*s)RawPutChar(*s++);
    if(!error)for(;;){Wait(oz_engine_signals());oz_engine_pump();}
    return error ? 20 : 0;
}
