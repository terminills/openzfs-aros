/* SPDX-License-Identifier: CDDL-1.0 */
/* Native Exec IPC, shared production broker and upstream engine, private
 * QEMU disks only. This is not a GUI or standalone service-process test. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dsl_scan.h>
#include <sys/acl.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include "../ui/api.h"
#include "zpl-fixture.h"
#include "dos-handler.h"
static const char source[]="qemu_mirror/mountlife";
static const char snap[]="qemu_mirror/mountlife@management";
static const char clone[]="qemu_mirror/management-clone";
static const char original[]="original snapshot data";
static const char changed[]="modified clone payload";
static const char created_fs[]="qemu_mirror/management-created";
static void report(const char *label,int rc)
{
    char line[160];snprintf(line,sizeof(line),"%s=%d\n",label,rc);
    for(char *p=line;*p;p++)RawPutChar(*p);
}
static const char ipc_port[]="OpenZFS.Management.QEMUClone.v2";
static unsigned ipc_completed,scrub_status_polls;
struct ipc_client {struct Message done;struct MsgPort *port;struct oz_message *request;int result;};
static void *ipc_request(void *arg)
{
    struct ipc_client *c=arg;struct oz_client client;
    if(!oz_client_open(&client))c->result=ENOMEM;
    else {
        client.server_name=ipc_port;
        ULONG submitted=oz_submit(&client,c->request);
        if(submitted!=OZ_OK)c->result=EIO;
        else {
            struct oz_message *reply;
            while(!(reply=oz_poll(&client)))Delay(1);
            memcpy(c->request,reply,sizeof(*reply));oz_message_free(reply);
        }
        oz_client_close(&client);
    }
    PutMsg(c->port,&c->done);return NULL;
}
static int ipc_roundtrip(struct oz_message *request)
{
    struct MsgPort *server=CreateMsgPort(),*done=CreateMsgPort();
    if(!server||!done){if(server)DeleteMsgPort(server);if(done)DeleteMsgPort(done);return ENOMEM;}
    server->mp_Node.ln_Name=(char *)ipc_port;
    Forbid();
    if(FindPort((CONST_STRPTR)ipc_port)){Permit();DeleteMsgPort(server);DeleteMsgPort(done);return EEXIST;}
    AddPort(server);Permit();
    struct ipc_client c={0};c.port=done;c.request=request;
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;int rc=pthread_create(&thread,NULL,ipc_request,&c);
    if(!rc) {
        for(;;) {
            Wait((1UL<<done->mp_SigBit)|(1UL<<server->mp_SigBit));
            struct oz_message *m;
            while((m=(struct oz_message *)GetMsg(server)))oz_broker_process(m,0);
            if(GetMsg(done))break;
        }
        pthread_join(thread,NULL);rc=c.result;
    }
    Forbid();RemPort(server);Permit();
    DeleteMsgPort(server);DeleteMsgPort(done);
    if(!rc)ipc_completed++;
    report("management IPC transport",rc);return rc;
}
static int call(unsigned op,const char *target,const char *argument,unsigned flags,unsigned expected)
{
    static struct oz_message m;memset(&m,0,sizeof(m));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=op;m.flags=flags;
    snprintf(m.target,sizeof(m.target),"%s",target);
    snprintf(m.argument,sizeof(m.argument),"%s",argument);
    int transport=ipc_roundtrip(&m);if(transport)return transport;
    report("management operation",op);report("management status",m.status);
    for(char *p=m.detail;*p;p++)RawPutChar(*p);
    RawPutChar('\n');
    if(op==40&&expected==OZ_OK&&(m.row_count!=1||strcmp(m.rows[0].value,snap)))return EIO;
    if(op==52&&expected==OZ_OK&&!(m.capability_words[1]&(1UL<<(52-32))))return EIO;
    return m.status==expected?0:EIO;
}
static int file_check(const char *dataset,const char *expected,int write,int create)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;
    int rc=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    if(!rc)rc=create?oz_zpl_create_as(fs,oz_zpl_root(fs),"management.txt",0,kcred,&info):
        oz_zpl_lookup_at(fs,oz_zpl_root(fs),"management.txt",&info);
    if(!rc&&write) {
        size_t written=0;
        rc=oz_zpl_write_as(fs,info.object,0,expected,strlen(expected)+1,&written,kcred);
        if(!rc&&written!=strlen(expected)+1)rc=EIO;
        if(!rc)rc=oz_zpl_sync(fs);
    }
    char bytes[64];
    if(!rc)rc=oz_zpl_read_as(fs,info.object,0,bytes,strlen(expected)+1,kcred);
    if(!rc&&memcmp(bytes,expected,strlen(expected)+1))rc=EIO;
    int close=oz_zpl_close(fs);return rc?rc:close;
}
struct dos_client {struct Message done;struct MsgPort *port;const char *expected;int result;};
static void *dos_read_client(void *arg)
{
    struct dos_client *c=arg;int rc=0;char bytes[64];
    BPTR lock=Lock((CONST_STRPTR)"OZCLONE:management.txt",SHARED_LOCK);
    struct FileInfoBlock fib;
    if(!lock||!Examine(lock,&fib)||strcmp((char *)fib.fib_FileName,"management.txt")||
        fib.fib_Size!=(LONG)strlen(c->expected)+1)rc=EIO;
    if(lock)UnLock(lock);
    BPTR file=Open((CONST_STRPTR)"OZCLONE:management.txt",MODE_OLDFILE);
    if(!file)rc=EIO;
    if(file) {
        LONG n=Read(file,bytes,sizeof(bytes));
        if(n!=(LONG)strlen(c->expected)+1||memcmp(bytes,c->expected,strlen(c->expected)+1))rc=EIO;
        if(Read(file,bytes,1)!=0||Seek(file,0,OFFSET_BEGINNING)!=(LONG)strlen(c->expected)+1||
            Read(file,bytes,4)!=4||memcmp(bytes,c->expected,4))rc=EIO;
        if(!Close(file))rc=EIO;
    }
    file=Open((CONST_STRPTR)"OZCLONE:management.txt",MODE_NEWFILE);
    if(file||IoErr()!=ERROR_DISK_WRITE_PROTECTED)rc=EIO;
    if(file)Close(file);
    c->result=rc;PutMsg(c->port,&c->done);return NULL;
}
static int mounted_read(const char *dataset,const char *expected)
{
    struct oz_zpl *fs=NULL;struct oz_mount *mount=NULL;
    struct MsgPort *done=CreateMsgPort();if(!done)return ENOMEM;
    int rc=oz_zpl_open(dataset,&fs);
    if(!rc)rc=oz_dos_mount_fixture(fs,"OZCLONE",&mount);
    struct dos_client c={0};c.port=done;c.expected=expected;
    c.done.mn_Node.ln_Type=NT_MESSAGE;c.done.mn_Length=sizeof(c.done);
    pthread_t thread;
    if(!rc)rc=pthread_create(&thread,NULL,dos_read_client,&c);
    if(!rc) {
        do {Wait((1UL<<done->mp_SigBit)|oz_dos_signals(mount));oz_dos_pump(mount);}
            while(!GetMsg(done));
        pthread_join(thread,NULL);rc=c.result;
    }
    if(mount){int unmount=oz_dos_unmount(mount);if(!rc)rc=unmount;}
    int close=oz_zpl_close(fs);if(!rc)rc=close;
    DeleteMsgPort(done);report("management mounted DOS read",rc);return rc;
}
static const char page_root[]="qemu_mirror/page-root";
static int status_test(int degraded)
{
    static struct oz_message m;memset(&m,0,sizeof(m));
    m.operation=4;strcpy(m.target,"qemu_mirror");
    int rc=ipc_roundtrip(&m);
    if(!rc&&(m.status!=OZ_OK||m.row_count!=4||m.next_cursor||
        !(m.capability_words[0]&(1UL<<4))))rc=EIO;
    unsigned pools=0,mirrors=0,leaves=0,online=0;char mirror[160]={0};
    for(unsigned i=0;i<m.row_count&&!rc;i++) {
        struct oz_row *r=&m.rows[i];
        if(!strcmp(r->kind,"pool")) {
            pools++;
            if(strcmp(r->name,"qemu_mirror")||r->parent[0]||!r->total||
                r->available+r->used!=r->total||
                strcmp(r->state,degraded?"DEGRADED":"ONLINE"))rc=EIO;
        } else if(!strcmp(r->kind,"mirror")) {
            mirrors++;strcpy(mirror,r->name);
            if(strcmp(r->parent,"qemu_mirror")||strcmp(r->state,degraded?"DEGRADED":"ONLINE"))rc=EIO;
        } else if(!strcmp(r->kind,"disk")) {
            leaves++;online+=!strcmp(r->state,"ONLINE");
            if(strcmp(r->parent,mirror)||
                (strcmp(r->name,"/AROS/ata.device/0")&&strcmp(r->name,"/AROS/ata.device/1")))rc=EIO;
        } else rc=EIO;
        if(!strstr(r->value,"guid=")||!strstr(r->value,"read-errors=")||
            !strstr(r->value,"write-errors=")||!strstr(r->value,"checksum-errors="))rc=EIO;
        char line[400];snprintf(line,sizeof(line),"status row %s parent=%s state=%s\n",r->name,r->parent,r->state);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    if(!rc&&(pools!=1||mirrors!=1||leaves!=2||online!=(degraded?1:2)))rc=EIO;
    if(!rc)rc=call(4,"missing_pool","",0,OZ_ENGINE_ERROR);
    if(!rc)rc=call(4,"qemu_mirror/not-a-pool","",0,OZ_ENGINE_ERROR);
    report(degraded?"management degraded status":"management mirror status",rc);return rc;
}
static int list_check(int snapshots,int recursive,UQUAD initial)
{
    static struct oz_message m;
    unsigned seen[28]={0},total=0,pages=0;
    UQUAD cursor=initial;int rc=0;
    do {
        memset(&m,0,sizeof(m));m.operation=36;m.cursor=cursor;
        m.flags=recursive?OZ_RECURSIVE:0;
        strcpy(m.target,page_root);
        if(snapshots)strcpy(m.options,"snapshots=on");
        rc=ipc_roundtrip(&m);if(rc)break;
        if(m.status!=OZ_OK||m.row_count>OZ_MAX_ROWS){rc=EIO;break;}
        for(unsigned i=0;i<m.row_count;i++) {
            struct oz_row *row=&m.rows[i];char name[160];int index=-1;
            if(!strcmp(row->name,page_root))index=0;
            for(unsigned j=1;j<=26;j++) {
                snprintf(name,sizeof(name),"%s/child-%02u",page_root,j);
                if(!strcmp(row->name,name))index=j;
            }
            snprintf(name,sizeof(name),"%s@page-snapshot",page_root);
            if(!strcmp(row->name,name))index=27;
            if(index<0||seen[index]++||(!snapshots&&index==27)||
                (!recursive&&index>0&&index<27)){rc=EIO;break;}
            const char *parent=index?page_root:"qemu_mirror";
            const char *kind=index==27?"snapshot":"filesystem";
            if(strcmp(row->parent,parent)||strcmp(row->kind,kind)) {
                char why[400];snprintf(why,sizeof(why),
                    "list row mismatch name=%s parent=%s expected=%s kind=%s\n",
                    row->name,row->parent,parent,row->kind);
                for(char *p=why;*p;p++)RawPutChar(*p);
                rc=EIO;break;
            }
        }
        total+=m.row_count;pages++;
        if(m.next_cursor&&(m.row_count!=OZ_MAX_ROWS||
            m.next_cursor!=cursor+m.row_count||m.next_cursor<=cursor))rc=EIO;
        cursor=m.next_cursor;
        if(pages>3)rc=EIO;
    } while(!rc&&cursor);
    unsigned expected=initial?0:1+(recursive?26:0)+(snapshots?1:0);
    if(!rc&&(total!=expected||pages!=(expected>OZ_MAX_ROWS?2:1)))rc=EIO;
    char line[160];snprintf(line,sizeof(line),
        "management list snapshots=%d recursive=%d initial=%llu rows=%u pages=%u result=%d\n",
        snapshots,recursive,(unsigned long long)initial,total,pages,rc);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return rc;
}
static int snapshot_list_test(void)
{
    static struct oz_message m;int rc=0;
    for(unsigned i=0;i<5&&!rc;i++) {
        memset(&m,0,sizeof(m));m.operation=36;
        strcpy(m.target,i>=3?"qemu_mirror/mountlife@absent-list-test":snap);
        if(i==1){strcpy(m.options,"snapshots=on");m.flags=OZ_RECURSIVE;}
        if(i==2||i==4)m.cursor=1;
        rc=ipc_roundtrip(&m);
        if(!rc&&(m.status!=(i>=3?OZ_ENGINE_ERROR:OZ_OK)||m.next_cursor))rc=EIO;
        unsigned expected=i<2?1:0;
        if(!rc&&m.row_count!=expected)rc=EIO;
        if(!rc&&expected&&(strcmp(m.rows[0].name,snap)||
            strcmp(m.rows[0].parent,source)||strcmp(m.rows[0].kind,"snapshot")))rc=EIO;
    }
    report("management direct snapshot list",rc);return rc;
}
static int pagination_test(int write)
{
    int rc=0;char name[160];
    if(write) {
        rc=call(52,snap,page_root,OZ_CONFIRMED,OZ_OK);
        for(unsigned i=1;i<=26&&!rc;i++) {
            snprintf(name,sizeof(name),"%s/child-%02u",page_root,i);
            rc=call(52,snap,name,OZ_CONFIRMED,OZ_OK);
        }
        snprintf(name,sizeof(name),"%s@page-snapshot",page_root);
        if(!rc)rc=call(50,name,"",OZ_CONFIRMED,OZ_OK);
    }
    if(!rc)rc=list_check(0,1,0);
    if(!rc)rc=list_check(1,1,0);
    if(!rc)rc=list_check(0,0,0);
    if(!rc)rc=list_check(1,0,0);
    if(!rc)rc=list_check(1,1,10000);
    if(!rc)rc=list_check(1,1,UINT64_MAX);
    report("management pagination",rc);return rc;
}
static int scrub_state(const char *expected)
{
    static struct oz_message m;memset(&m,0,sizeof(m));
    m.operation=4;strcpy(m.target,"qemu_mirror");
    int rc=ipc_roundtrip(&m);
    if(!rc&&(m.status!=OZ_OK||!(m.capability_words[0]&(1UL<<18))||
        !strstr(m.detail,expected)))rc=EIO;
    report(expected,rc);return rc;
}
static int holds_test(int write)
{
    const char *held="qemu_mirror/lifecycle-renamed@held";int rc=0;
    if(write) {
        rc=call(50,held,"",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(55,held,"keep",0,OZ_BAD_REQUEST);
        if(!rc)rc=call(55,held,"keep",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        if(!rc)rc=call(55,held,"keep",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(55,held,"keep",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(55,held,"release-me",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,held,"",OZ_CONFIRMED,OZ_BUSY);
        if(!rc)rc=call(56,held,"release-me",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(56,held,"release-me",OZ_CONFIRMED,OZ_ENGINE_ERROR);
    } else {
        rc=call(55,held,"denied",OZ_CONFIRMED,OZ_UNSUPPORTED);
        if(!rc)rc=call(56,held,"keep",OZ_CONFIRMED,OZ_UNSUPPORTED);
    }
    if(!rc) {
        static struct oz_message m;memset(&m,0,sizeof(m));
        m.operation=57;strcpy(m.target,held);rc=ipc_roundtrip(&m);
        if(!rc&&(m.status!=OZ_OK||m.row_count!=1||m.next_cursor||
            strcmp(m.rows[0].kind,"hold")||strcmp(m.rows[0].name,"keep")||
            strcmp(m.rows[0].parent,held)||strcmp(m.rows[0].state,"persistent")||
            strncmp(m.rows[0].value,"created-unix=",13)))rc=EIO;
    }
    if(!rc)rc=file_check(held,original,0,0);
    report("management persistent holds",rc);return rc;
}
static int hold_pages_test(int write)
{
    const char *held="qemu_mirror/lifecycle-renamed@hold-pages";
    const char *released="qemu_mirror/lifecycle-renamed@release-last";
    const char *missing="qemu_mirror/lifecycle-renamed@missing-hold";
    int rc=0;char tag[32];unsigned seen[27]={0};
    if(write) {
        rc=call(50,held,"",OZ_CONFIRMED,OZ_OK);
        for(unsigned i=0;i<27&&!rc;i++) {
            snprintf(tag,sizeof(tag),"hold-%02u",i);
            rc=call(55,held,tag,OZ_CONFIRMED,OZ_OK);
        }
        if(!rc)rc=call(55,missing,"missing",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(56,missing,"missing",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(50,released,"",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(55,released,"last",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,released,"",OZ_CONFIRMED,OZ_BUSY);
        if(!rc)rc=call(56,released,"last",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,released,"",OZ_CONFIRMED,OZ_OK);
    }
    for(unsigned i=0;i<6&&!rc;i++) {
        static struct oz_message m;memset(&m,0,sizeof(m));m.operation=57;
        strcpy(m.target,i==3?missing:i==4?source:i==5?snap:held);
        m.cursor=i==1?24:i==2?27:0;
        rc=ipc_roundtrip(&m);
        unsigned expected=i==0?24:i==1?3:0;
        if(!rc&&(m.status!=((i==3||i==4)?OZ_ENGINE_ERROR:OZ_OK)||
            m.row_count!=expected||m.next_cursor!=(i==0?24:0)))rc=EIO;
        for(unsigned j=0;j<m.row_count&&!rc;j++) {
            struct oz_row *r=&m.rows[j];int found=-1;
            for(unsigned k=0;k<27;k++) {
                snprintf(tag,sizeof(tag),"hold-%02u",k);
                if(!strcmp(r->name,tag))found=k;
            }
            if(found<0||seen[found]++||strcmp(r->kind,"hold")||
                strcmp(r->parent,held)||strcmp(r->state,"persistent")||
                strncmp(r->value,"created-unix=",13))rc=EIO;
        }
    }
    for(unsigned i=0;i<27&&!rc;i++)if(seen[i]!=1)rc=EIO;
    if(!rc) {
        struct oz_zpl *fs=NULL;int error=oz_zpl_open(released,&fs);
        if(error!=ENOENT)rc=EIO;
        if(fs)oz_zpl_close(fs);
    }
    report("management hold pagination and last release",rc);return rc;
}
static int lifecycle_test(int write)
{
    const char *old="qemu_mirror/lifecycle",*moved="qemu_mirror/lifecycle-renamed";
    int rc=0;
    if(write) {
        rc=call(37,old,"",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(37,"qemu_mirror/lifecycle/child","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=file_check(old,original,1,1);
        if(!rc)rc=call(39,old,moved,0,OZ_BAD_REQUEST);
        if(!rc)rc=call(39,old,moved,OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        if(!rc)rc=call(39,old,"another_pool/dataset",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(38,old,"",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(39,old,moved,OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(39,old,"qemu_mirror/missing-rename",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(50,"qemu_mirror/lifecycle-renamed@discard","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed@discard","",0,OZ_BAD_REQUEST);
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed@discard","",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        if(!rc)rc=call(38,snap,"",OZ_CONFIRMED,OZ_ENGINE_ERROR); /* live clone dependency */
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed@discard","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed/child","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(37,"qemu_mirror/lifecycle-renamed/discard","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed/discard","",OZ_CONFIRMED,OZ_OK);
        if(!rc) {
            static struct oz_message m;memset(&m,0,sizeof(m));
            m.operation=43;m.flags=OZ_CONFIRMED;
            strcpy(m.target,created_fs);strcpy(m.argument,"OZLIFEGUARD");strcpy(m.options,"readonly=on");
            rc=ipc_roundtrip(&m);
            if(!rc&&m.status!=OZ_OK)rc=EIO;
        }
        if(!rc)rc=call(39,created_fs,"qemu_mirror/mounted-rename",OZ_CONFIRMED,OZ_BUSY);
        if(!rc)rc=call(38,created_fs,"",OZ_CONFIRMED,OZ_BUSY);
        if(!rc)rc=call(44,created_fs,"",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(38,"qemu_mirror","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
    } else {
        rc=call(39,moved,"qemu_mirror/denied-rename",OZ_CONFIRMED,OZ_UNSUPPORTED);
        if(!rc)rc=call(38,moved,"",OZ_CONFIRMED,OZ_UNSUPPORTED);
    }
    const char *absent[]={old,"qemu_mirror/lifecycle-renamed/child","qemu_mirror/lifecycle-renamed/discard","qemu_mirror/lifecycle-renamed@discard"};
    for(unsigned i=0;i<4&&!rc;i++) {
        struct oz_zpl *fs=NULL;int error=oz_zpl_open(absent[i],&fs);
        if(error!=ENOENT)rc=EIO;
        if(fs)oz_zpl_close(fs);
    }
    if(!rc)rc=file_check(moved,original,0,0);
    if(!rc)rc=mounted_read(moved,original);
    report("management rename/destroy persistence",rc);return rc;
}
static int created_root(int empty,int writable)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;
    int rc=writable?oz_zpl_open_writable(created_fs,&fs):oz_zpl_open(created_fs,&fs);
    report("created root open",rc);
    if(!rc)rc=oz_zpl_stat(fs,oz_zpl_root(fs),&info);
    if(!rc&&(!info.directory||info.uid||info.gid||info.parent!=info.object))rc=EIO;
    report("created root metadata",rc);
    if(!rc&&empty) {
        char name[128];
        int entry=oz_zpl_nth(fs,oz_zpl_root(fs),0,name,sizeof(name),&info);
        report("created root first entry",entry);
        if(entry!=ENOENT)rc=EIO;
    }
    cred_t *other=oz_cred_create(77,77,88,0,NULL);
    if(!other&&!rc)rc=ENOMEM;
    if(!rc)rc=oz_zpl_access(fs,oz_zpl_root(fs),ACE_READ_DATA|ACE_EXECUTE,other);
    report("created root other read/traverse",rc);
    if(!rc) {
        int denied=oz_zpl_access(fs,oz_zpl_root(fs),ACE_WRITE_DATA,other);
        report("created root other write",denied);
        if(denied!=(writable?EACCES:EROFS))rc=EIO;
    }
    crfree(other);
    int close=oz_zpl_close(fs);return rc?rc:close;
}
static int dataset_create_test(int write)
{
    int rc=0;
    if(write) {
        rc=call(37,created_fs,"",0,OZ_BAD_REQUEST);
        if(!rc)rc=call(37,created_fs,"",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        if(!rc)rc=call(37,created_fs,"extra",OZ_CONFIRMED,OZ_UNSUPPORTED);
        if(!rc)rc=call(37,"qemu_mirror/not-a-dataset@snapshot","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(37,"qemu_mirror/missing-parent/child","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(37,created_fs,"",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=created_root(1,1);
        if(!rc)rc=call(37,created_fs,"",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(37,"qemu_mirror/management-created/child","",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=file_check(created_fs,original,1,1);
    } else rc=call(37,"qemu_mirror/denied-create","",OZ_CONFIRMED,OZ_UNSUPPORTED);
    if(!rc)rc=created_root(0,write);
    if(!rc)rc=file_check(created_fs,original,0,0);
    if(!rc)rc=mounted_read(created_fs,original);
    report("management dataset create/root ACL/persistence",rc);return rc;
}
static int scrub_test(int write)
{
    int rc=0;unsigned polls=0;
    if(write) {
        rc=call(18,"qemu_mirror","start",0,OZ_BAD_REQUEST);
        if(!rc)rc=call(18,"qemu_mirror","start",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        if(!rc)rc=call(18,"qemu_mirror/dataset","start",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(18,"missing_pool","start",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(18,"qemu_mirror","invalid",OZ_CONFIRMED,OZ_UNSUPPORTED);
        /* Upstream test/debug tunable, confined to this disposable guest.
         * Keep a small pool's scan alive long enough to verify controls. */
        zfs_scan_suspend_progress=1;
        if(!rc)rc=call(18,"qemu_mirror","start",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=call(18,"qemu_mirror","pause",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=scrub_state("scan=scrub state=paused");
        if(!rc)rc=call(18,"qemu_mirror","pause",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(18,"qemu_mirror","resume",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=scrub_state("scan=scrub state=scanning");
        if(!rc)rc=call(18,"qemu_mirror","stop",OZ_CONFIRMED,OZ_OK);
        if(!rc)rc=scrub_state("scan=scrub state=canceled");
        if(!rc)rc=call(18,"qemu_mirror","stop",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        zfs_scan_suspend_progress=0;
        report("management scrub controls",rc);
        if(!rc)rc=call(18,"qemu_mirror","start",OZ_CONFIRMED,OZ_OK);
    } else rc=call(18,"qemu_mirror","start",OZ_CONFIRMED,OZ_UNSUPPORTED);
    int finished=0;
    while(!rc&&!finished&&polls<60) {
        static struct oz_message m;memset(&m,0,sizeof(m));
        m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
        m.operation=4;strcpy(m.target,"qemu_mirror");
        rc=ipc_roundtrip(&m);polls++;
        if(!rc&&m.status!=OZ_OK)rc=EIO;
        char *scan=strstr(m.detail,"scan=scrub state=finished");
        if(!rc&&scan) {
            unsigned long long examined=0,total=0,issued=0,errors=0;
            if(sscanf(scan,"scan=scrub state=finished examined=%llu to-examine=%llu issued=%llu errors=%llu",
                &examined,&total,&issued,&errors)!=4||!examined||!issued||errors)rc=EIO;
            else finished=1;
            for(char *p=scan;*p;p++)RawPutChar(*p);
            RawPutChar('\n');
        }
        if(!write&&!finished&&!rc)rc=EIO;
        if(!rc&&!finished)Delay(50);
    }
    if(!rc&&!finished)rc=ETIMEDOUT;
    scrub_status_polls=polls;
    report("management scrub status polls",polls);
    report("management scrub completion",rc);return rc;
}
int main(int argc,char **argv)
{
    if(argc!=2||(strcmp(argv[1],"WRITE")&&strcmp(argv[1],"VERIFY")&&strcmp(argv[1],"DEGRADED")))return 20;
    int write=!strcmp(argv[1],"WRITE");
    int degraded=!strcmp(argv[1],"DEGRADED");
    int fd=open("/SYS/export.nvlist",O_RDONLY);if(fd<0)return 20;
    struct stat st;int rc=fstat(fd,&st);
    if(rc||st.st_size<=0||st.st_size>1024*1024){close(fd);return 20;}
    char *bytes=malloc(st.st_size);if(!bytes){close(fd);return 20;}
    if(read(fd,bytes,st.st_size)!=st.st_size){close(fd);free(bytes);return 20;}
    close(fd);nvlist_t *config=NULL;
    rc=nvlist_unpack(bytes,st.st_size,&config,0);free(bytes);if(rc)return 20;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1))return 20;
    if(!oz_engine_start(write))return 20;
    nvlist_t *props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"readonly",!write);
    rc=spa_import("qemu_mirror",config,props,0);report("management import",rc);
    if(!rc)rc=status_test(degraded);
    if(!rc&&write)rc=file_check(source,original,1,1);
    if(!rc&&write)rc=call(50,snap,"",OZ_CONFIRMED,OZ_OK);
    if(!rc&&write)rc=call(52,snap,clone,0,OZ_BAD_REQUEST);
    if(!rc&&write)rc=call(52,snap,clone,OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
    if(!rc&&write)rc=call(52,source,clone,OZ_CONFIRMED,OZ_ENGINE_ERROR);
    if(!rc&&write)rc=call(52,snap,"other_pool/clone",OZ_CONFIRMED,OZ_ENGINE_ERROR);
    if(!rc&&write)rc=call(52,"qemu_mirror/mountlife@missing",clone,OZ_CONFIRMED,OZ_ENGINE_ERROR);
    if(!rc&&write)rc=call(52,snap,clone,OZ_CONFIRMED,OZ_OK);
    if(!rc&&write)rc=call(52,snap,clone,OZ_CONFIRMED,OZ_ENGINE_ERROR);
    if(!rc&&write)rc=file_check(clone,original,0,0);
    if(!rc&&write)rc=file_check(clone,changed,1,0);
    if(!rc)rc=file_check(source,original,0,0);
    if(!rc)rc=file_check(snap,original,0,0);
    if(!rc)rc=file_check(clone,changed,0,0);
    if(!rc)rc=call(40,clone,"origin",0,OZ_OK);
    report("management clone independent data",rc);
    if(!rc)rc=mounted_read(clone,changed);
    if(!rc)rc=mounted_read(snap,original);
    if(!rc)rc=mounted_read(source,original);
    if(!rc)rc=pagination_test(write);
    if(!rc)rc=snapshot_list_test();
    if(!rc)rc=dataset_create_test(write);
    if(!rc)rc=lifecycle_test(write);
    if(!rc)rc=holds_test(write);
    if(!rc)rc=hold_pages_test(write);
    if(!rc)rc=scrub_test(write);
    if(!rc&&!write)rc=call(52,snap,"qemu_mirror/read-only-clone",OZ_CONFIRMED,OZ_UNSUPPORTED);
    /* A READ-only engine cannot execute pool export. Its ordinary shutdown
     * unloads the imported pool without mutating labels/configuration. */
    if(!rc&&write)rc=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
    if(!rc&&write) {
        fnvlist_add_uint64(props,"readonly",1);
        rc=spa_import("qemu_mirror",config,props,0);
        if(!rc)rc=call(55,"qemu_mirror/lifecycle-renamed@held","denied",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(56,"qemu_mirror/lifecycle-renamed@held","keep",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(39,"qemu_mirror/lifecycle-renamed","qemu_mirror/denied-rename",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(38,"qemu_mirror/lifecycle-renamed","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(37,"qemu_mirror/denied-create","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(18,"qemu_mirror","start",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=call(52,snap,"qemu_mirror/read-only-clone",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        if(!rc)rc=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
    }
    if(!rc&&ipc_completed!=(write?155:31)+scrub_status_polls)rc=EIO;
    report("management IPC verified total",ipc_completed);
    fnvlist_free(props);nvlist_free(config);
    int stopped=oz_engine_stop();
    /* The writer deliberately exercises a real service mount/unmount. Its
     * retired endpoint stays resident, as required by the production API. */
    if(!rc&&stopped==write)rc=EIO;
    report("management final",rc);
    const char *result=rc?"RESULT management clone FAIL\n":"RESULT management clone PASS\n";
    for(const char *p=result;*p;p++)RawPutChar(*p);
    if(!stopped)for(;;){Wait(oz_engine_signals());oz_engine_pump();}
    return rc?20:0;
}
