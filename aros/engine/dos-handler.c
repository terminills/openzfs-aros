/* SPDX-License-Identifier: CDDL-1.0 */
/* DOS packet frontend over upstream ZPL. Single-owner task API.
 * Unsupported operations fail explicitly. No private filesystem fallback. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <dos/exall.h>
#include <dos/dos64.h>
#include <dos/record.h>
#include <dos/notify.h>
#include <devices/timer.h>
#include <devices/input.h>
#include <devices/inputevent.h>
#include <utility/hooks.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
#include <sys/errno.h>
#include <sys/fs/zfs.h>
#include <stdio.h>
#include <stdlib.h>
#include "zpl-fixture.h"
#include "dos-handler.h"
#include "device-io.h"
#include "packet-cred.h"
#include "../ui/project-api.h"
#include "../ui/rewrite-api.h"
#include <sys/acl.h>
#include <libraries/security.h>
struct handle {struct handle *next;uint64_t object,size,position,parent,generation,parent_generation;int used,exclusive,changed,kind,stale,rename_match;char name[256];};
struct locked {struct FileLock lock;struct locked *next;int used,kind,stale,rename_match;uint64_t object,parent,generation,parent_generation;char name[256];};
struct record {struct record *next;struct handle *handle;uint64_t start,length;int shared;};
struct record_wait {struct record_wait *next;struct record *record;struct DosPacket *packet;struct timerequest *timer;};
struct watch {
    struct watch *next;
    struct NotifyRequest *request; /* NULL after removal; never dereference again */
    struct oz_packet_security *security;
    char *path;
    uint64_t object;
    unsigned outstanding;
    int present,directory,pending;
};
struct watch_message {struct NotifyMessage message;struct watch *watch;};
struct oz_mount {
    struct oz_zpl *fs;
    struct MsgPort *port;
    struct DosList *device,*volume;
    /* Logical dataset identity for DOS/security discovery, not invented
     * physical geometry. Retained with the endpoint after detach. */
    struct FileSysStartupMsg startup;
    struct DosEnvec environment;
    struct handle *handles;
    struct locked *locks;
    struct record *records;
    struct record_wait *record_waits;
    struct MsgPort *timer_port;
    struct watch *watches;
    struct MsgPort *notify_port;
    unsigned notify_outstanding;
    char name[32];
    int authenticated;
    int shared_signal;
    int borrowed_device;
    struct oz_packet_security *security; /* borrowed during this packet only */
};
static struct MsgPort *endpoint_port(struct oz_mount *m)
{
    if(m->shared_signal<0)return CreateMsgPort();
    struct MsgPort *p=calloc(1,sizeof(*p));
    if(!p)return NULL;
    NewList(&p->mp_MsgList);p->mp_Node.ln_Type=NT_MSGPORT;
    p->mp_Flags=PA_SIGNAL;p->mp_SigBit=m->shared_signal;p->mp_SigTask=FindTask(NULL);
    return p;
}
static void endpoint_port_free(struct oz_mount *m,struct MsgPort *p)
{
    if(!p)return;
    if(m->shared_signal<0)DeleteMsgPort(p);else free(p);
}
/* Detach has proved there are no live handles, locks or timer requests.
 * Late DOS packets use only the retained endpoint and detached DOS entries.
 * Notification reply ports must outlive every message sent to a subscriber. */
static void retired_resources_release(struct oz_mount *m)
{
    if(m->fs||m->records||m->record_waits)return;
    for(struct handle *h=m->handles;h;h=h->next)if(h->used)return;
    for(struct locked *l=m->locks;l;l=l->next)if(l->used)return;
    while(m->handles){struct handle *h=m->handles;m->handles=h->next;free(h);}
    while(m->locks){struct locked *l=m->locks;m->locks=l->next;free(l);}
    /* Keep auxiliary ports until the fully joined unmount path. A drained
     * queue does not establish that the producer has returned from ReplyMsg:
     * Exec may still read the port to signal its owner after enqueueing. */
}
size_t oz_dos_bookkeeping_bytes(const struct oz_mount *m)
{
    if(!m)return 0;
    size_t bytes=0;
    for(const struct handle *h=m->handles;h;h=h->next)bytes+=sizeof(*h);
    for(const struct locked *l=m->locks;l;l=l->next)bytes+=sizeof(*l);
    if(m->timer_port)bytes+=sizeof(*m->timer_port);
    if(m->notify_port)bytes+=sizeof(*m->notify_port);
    return bytes;
}
static int native_access_object_for(struct oz_mount *m,uint64_t object,uint32_t access,
    const struct oz_packet_security *security)
{
    if(!m->authenticated)return 0;
    struct oz_zpl_info info;
    int rc=oz_zpl_stat(m->fs,object,&info);
    if(rc)return rc;
    if(!info.has_native_protection)return oz_packet_security_volume_on(security,m->port,access);
    uint32_t owner;
    rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
    return rc?rc:oz_packet_security_access_on(security,m->port,owner,info.native_protection,access,NULL);
}
static int native_access_object(struct oz_mount *m,uint64_t object,uint32_t access)
{return native_access_object_for(m,object,access,m->security);}
struct replacement_policy {struct oz_mount *mount;uint64_t parent,object;};
static int replacement_check(void *context)
{
    struct replacement_policy *p=context;struct oz_mount *m=p->mount;
    for(struct handle *h=m->handles;h;h=h->next)
        if(h->used&&h->object==p->object)return EBUSY;
    for(struct locked *l=m->locks;l;l=l->next)
        if(l->used&&l->object==p->object)return EBUSY;
    int rc=native_access_object(m,p->object,secAt_Write|secAt_Delete);
    return rc?rc:native_access_object(m,p->parent,secAt_Write);
}
/* The DOS owner serializes namespace packets throughout prepare/publication
 * and cleanup. Do not use this helper from a concurrent mount worker. */
static int replace_output(struct oz_mount *m,uint64_t parent,const char *leaf,
    uint32_t defaults,cred_t *cr,struct oz_zpl_info *info)
{
    struct replacement_policy policy={m,parent,info->object};
    int rc=replacement_check(&policy);if(rc)return rc;
    struct oz_zpl_info old=*info,fresh,found;char temporary[64];
    unsigned attempt;
    for(attempt=0;attempt<64;attempt++) {
        snprintf(temporary,sizeof(temporary),".ozreplace-%08lx-%02x",
            (unsigned long)old.object,attempt);
        int same=0;rc=oz_zpl_same_name(m->fs,temporary,leaf,&same);
        if(rc)return rc;
        if(same)continue;
        rc=oz_zpl_create_as(m->fs,parent,temporary,defaults,cr,&fresh);
        if(rc==EEXIST)continue;
        if(rc)return rc;
        break;
    }
    if(attempt==64)return EEXIST;
    rc=oz_zpl_replace_as(m->fs,parent,temporary,leaf,&fresh,&old,cr,
        replacement_check,&policy);
    if(!rc){*info=fresh;return 0;}
    /* A sync error may follow successful publication. Never remove the
     * published object in an attempt to undo that error. */
    int lookup=oz_zpl_lookup_at(m->fs,parent,leaf,&found);
    if(!lookup&&found.object==fresh.object&&found.generation==fresh.generation)
        return rc;
    lookup=oz_zpl_lookup_at(m->fs,parent,temporary,&found);
    if(!lookup&&found.object==fresh.object&&found.generation==fresh.generation) {
        int cleanup=oz_zpl_remove_as(m->fs,parent,temporary,0,cr);
        if(cleanup) {
            char diagnostic[192];snprintf(diagnostic,sizeof(diagnostic),
                "OpenZFS replacement cleanup name=%s errno=%d original-error=%d\n",
                temporary,cleanup,rc);
            for(const char *p=diagnostic;*p;p++)RawPutChar(*p);
        }
    }
    return rc;
}
static int access_object_for(struct oz_mount *m,uint64_t object,uint32_t rights,cred_t *cr,
    const struct oz_packet_security *security)
{
    if(!m->authenticated)return 0;
    int rc=oz_zpl_access(m->fs,object,rights,cr);
    if(rc)return rc;
    uint32_t access=0;
    if(rights&(ACE_READ_DATA|ACE_READ_ATTRIBUTES|ACE_READ_NAMED_ATTRS|ACE_READ_ACL))access|=secAt_Read;
    if(rights&(ACE_WRITE_DATA|ACE_APPEND_DATA|ACE_WRITE_ATTRIBUTES|ACE_WRITE_NAMED_ATTRS|
        ACE_WRITE_ACL|ACE_WRITE_OWNER))access|=secAt_Write;
    if(rights&ACE_EXECUTE)access|=secAt_Execute;
    if(rights&(ACE_DELETE|ACE_DELETE_CHILD))access|=secAt_Delete;
    if(!access)return EINVAL;
    return native_access_object_for(m,object,access,security);
}
static int access_object(struct oz_mount *m,uint64_t object,uint32_t rights,cred_t *cr)
{return access_object_for(m,object,rights,cr,m->security);}
static int path_policy(void *context,uint64_t object,uint32_t rights,cred_t *cr)
{return access_object(context,object,rights,cr);}
static int resolve_path(struct oz_mount *m,uint64_t parent,const char *path,
    struct oz_zpl_info *info,cred_t *cr)
{return m->authenticated?oz_zpl_resolve_policy(m->fs,parent,path,info,cr,path_policy,m):
    oz_zpl_resolve(m->fs,parent,path,info);}
static int object_kind(const struct oz_zpl_info *info)
{return info->directory?1:info->symlink?2:info->special?2+info->special:0;}
/* DOS has a pipe entry type but no distinct character/block/socket types.
 * Their metadata remains enumerable; byte-stream opens refuse them below. */
static LONG entry_type(const struct oz_zpl_info *info)
{return info->directory?ST_USERDIR:info->symlink?ST_SOFTLINK:
    info->special==OZ_SPECIAL_FIFO?ST_PIPEFILE:ST_FILE;}
static int identity_current(struct oz_mount *m,uint64_t object,uint64_t generation,int kind)
{
    struct oz_zpl_info info;
    return m->fs && !oz_zpl_stat(m->fs,object,&info) &&
        generation && info.generation==generation && object_kind(&info)==kind;
}
static int handle_current(struct oz_mount *m,struct handle *h)
{
    if(!h||!h->used||h->stale)return 0;
    if(!identity_current(m,h->object,h->generation,h->kind) ||
        !identity_current(m,h->parent,h->parent_generation,1))h->stale=1;
    return !h->stale;
}
static struct handle *registered_handle(struct handle *handles,IPTR value)
{
    for(struct handle *h=handles;h;h=h->next)if(value==(IPTR)h&&h->used)return h;
    return NULL;
}
static struct handle *valid_handle(struct oz_mount *m,IPTR value)
{
    struct handle *h=registered_handle(m->handles,value);
    return handle_current(m,h)?h:NULL;
}
static int record_conflict(struct oz_mount *m,const struct record *candidate)
{
    for(struct record *r=m->records;r;r=r->next)
        if(r->handle!=candidate->handle&&r->handle->object==candidate->handle->object&&
            !(r->shared&&candidate->shared)&&r->length&&candidate->length&&
            r->start<candidate->start+candidate->length&&candidate->start<r->start+r->length)
            return 1;
    return 0;
}
static int record_access(struct oz_mount *m,struct handle *h,cred_t *cr)
{
    int rc=access_object(m,h->object,ACE_READ_DATA,cr);
    if((rc==EACCES||rc==EPERM)&&!oz_zpl_readonly(m->fs))
        rc=access_object(m,h->object,ACE_WRITE_DATA,cr);
    return rc;
}
static void record_wait_free(struct record_wait *w)
{
    if(!CheckIO(&w->timer->tr_node))AbortIO(&w->timer->tr_node);
    WaitIO(&w->timer->tr_node);
    CloseDevice(&w->timer->tr_node);DeleteIORequest(w->timer);
    free(w->record);free(w);
}
static void records_pump(struct oz_mount *m)
{
    struct record_wait **link=&m->record_waits;
    while(*link) {
        struct record_wait *w=*link;LONG error=0;
        if(!handle_current(m,w->record->handle))error=ERROR_INVALID_LOCK;
        else if(CheckIO(&w->timer->tr_node))error=ERROR_LOCK_TIMEOUT;
        else if(record_conflict(m,w->record)){link=&w->next;continue;}
        else if(m->authenticated) {
            struct oz_packet_security *security=NULL;
            int rc=oz_packet_security_begin(w->packet,m->port,&security);
            if(!rc) {
                m->security=security;
                rc=record_access(m,w->record->handle,oz_packet_security_cred(security));
                m->security=NULL;
            }
            oz_packet_security_end(security);
            if(rc)error=rc==ENOMEM?ERROR_NO_FREE_STORE:ERROR_READ_PROTECTED;
        }
        *link=w->next;
        if(!error){w->record->next=m->records;m->records=w->record;w->record=NULL;}
        struct DosPacket *packet=w->packet;
        record_wait_free(w);
        ReplyPkt(packet,error?DOSFALSE:DOSTRUE,error);
    }
}
static int record_queue(struct oz_mount *m,struct record *r,struct DosPacket *packet,ULONG ticks)
{
    if(!m->timer_port)m->timer_port=endpoint_port(m);
    if(!m->timer_port)return ERROR_NO_FREE_STORE;
    struct record_wait *w=calloc(1,sizeof(*w));
    if(!w)return ERROR_NO_FREE_STORE;
    w->timer=(struct timerequest *)CreateIORequest(m->timer_port,sizeof(*w->timer));
    if(!w->timer){free(w);return ERROR_NO_FREE_STORE;}
    if(OpenDevice((CONST_STRPTR)TIMERNAME,UNIT_MICROHZ,&w->timer->tr_node,0)) {
        DeleteIORequest(w->timer);free(w);return ERROR_ACTION_NOT_KNOWN;
    }
    w->record=r;w->packet=packet;
    w->timer->tr_node.io_Command=TR_ADDREQUEST;
    w->timer->tr_time.tv_secs=ticks/TICKS_PER_SECOND;
    w->timer->tr_time.tv_micro=(ticks%TICKS_PER_SECOND)*(1000000/TICKS_PER_SECOND);
    struct record_wait **tail=&m->record_waits;while(*tail)tail=&(*tail)->next;
    *tail=w;SendIO(&w->timer->tr_node);
    return 0;
}
static void record_close(struct oz_mount *m,struct handle *h)
{
    struct record **link=&m->records;
    while(*link) {
        struct record *r=*link;
        if(r->handle==h){*link=r->next;free(r);}else link=&r->next;
    }
}
/* Entries never move while DOS holds their addresses. Reserve before any
 * destructive open/create work; unused entries are reused after Close. */
static struct handle *reserve_handle(struct oz_mount *mount)
{
    for(struct handle *h=mount->handles;h;h=h->next)if(!h->used)return h;
    struct handle *h=calloc(1,sizeof(*h));
    if(h){h->next=mount->handles;mount->handles=h;}
    return h;
}
static int exclusive_object(struct oz_mount *mount,uint64_t object)
{
    for(struct handle *h=mount->handles;h;h=h->next)
        if(h->used&&h->exclusive&&h->object==object)return 1;
    for(struct locked *l=mount->locks;l;l=l->next)if(l->used&&
        l->lock.fl_Access==EXCLUSIVE_LOCK&&l->object==object)return 1;
    return 0;
}
static int referenced_object(struct oz_mount *mount,uint64_t object)
{
    for(struct handle *h=mount->handles;h;h=h->next)if(h->used&&h->object==object)return 1;
    for(struct locked *l=mount->locks;l;l=l->next)if(l->used&&l->object==object)return 1;
    return 0;
}
static LONG dos_error(int rc)
{
    /* Integrity and device failures are not access-control refusals. */
    if(rc==EIO||rc==ECKSUM)return ERROR_NOT_A_DOS_DISK;
    if(rc==ENOENT)return ERROR_OBJECT_NOT_FOUND;
    if(rc==OZ_ZPL_SOFTLINK)return ERROR_IS_SOFT_LINK;
    if(rc==EEXIST)return ERROR_OBJECT_EXISTS;
    if(rc==EBUSY)return ERROR_OBJECT_IN_USE;
    if(rc==ENOTEMPTY)return ERROR_DIRECTORY_NOT_EMPTY;
    if(rc==ENOMEM)return ERROR_NO_FREE_STORE;
    if(rc==EROFS)return ERROR_DISK_WRITE_PROTECTED;
    if(rc==ENOSPC||rc==EDQUOT)return ERROR_DISK_FULL;
    if(rc==EFBIG)return ERROR_OBJECT_TOO_LARGE;
    if(rc==EOVERFLOW)return ERROR_OBJECT_TOO_LARGE;
    if(rc==ENOTSUP||rc==ENOTDIR||rc==EISDIR)return ERROR_OBJECT_WRONG_TYPE;
    if(rc==ENAMETOOLONG)return ERROR_LINE_TOO_LONG;
    return ERROR_READ_PROTECTED;
}
/* Watches retain the registration identity, not the changing task's identity.
 * Recheck current object ACLs and native volume policy before each delivery. */
static int watch_resolve(struct oz_mount *m,struct watch *w,struct oz_zpl_info *info)
{
    struct oz_packet_security *saved=m->security;m->security=w->security;
    cred_t *cr=w->security?oz_packet_security_cred(w->security):NULL;
    int rc=resolve_path(m,oz_zpl_root(m->fs),w->path,info,cr);
    if(!rc&&info->directory)rc=access_object(m,info->object,ACE_LIST_DIRECTORY|ACE_EXECUTE,cr);
    if(rc==ENOENT) {
        /* Nonexistent names are valid watches only under an accessible
         * existing ancestor. A denial must not become an existence oracle. */
        char prefix[4097];snprintf(prefix,sizeof(prefix),"%s",w->path);
        do {
            char *slash=strrchr(prefix,'/');if(slash)*slash=0;else *prefix=0;
            rc=resolve_path(m,oz_zpl_root(m->fs),prefix,info,cr);
        } while(rc==ENOENT&&*prefix);
        if(!rc)rc=info->directory?access_object(m,info->object,ACE_LIST_DIRECTORY|ACE_EXECUTE,cr):ENOTDIR;
        if(!rc)rc=ENOENT;
    }
    m->security=saved;return rc;
}
static void watch_free(struct watch *w)
{oz_packet_security_end(w->security);free(w->path);free(w);}
static int watch_emit(struct oz_mount *m,struct watch *w)
{
    struct NotifyRequest *nr=w->request;
    if(!nr)return 0;
    if(nr->nr_Flags&NRF_SEND_SIGNAL) {
        Signal(nr->nr_stuff.nr_Signal.nr_Task,1UL<<nr->nr_stuff.nr_Signal.nr_SignalNum);
        w->pending=0;return 0;
    }
    if((nr->nr_Flags&NRF_WAIT_REPLY)&&w->outstanding){w->pending=1;return 0;}
    struct watch_message *nm=calloc(1,sizeof(*nm));
    if(!nm){w->pending=1;return ENOMEM;}
    nm->watch=w;nm->message.nm_NReq=nr;
    nm->message.nm_Class=NOTIFY_CLASS;nm->message.nm_Code=NOTIFY_CODE;
    nm->message.nm_ExecMessage.mn_ReplyPort=m->notify_port;
    nm->message.nm_ExecMessage.mn_Length=sizeof(*nm);
    w->outstanding++;m->notify_outstanding++;nr->nr_MsgCount++;
    w->pending=0;
    PutMsg(nr->nr_stuff.nr_Msg.nr_Port,&nm->message.nm_ExecMessage);return 0;
}
static void watches_pump(struct oz_mount *m)
{
    if(!m->notify_port)return;
    struct watch_message *nm;
    while((nm=(void *)GetMsg(m->notify_port))) {
        struct watch *w=nm->watch;
        w->outstanding--;m->notify_outstanding--;free(nm);
        /* EndNotify may already have freed the request and its fullname.
         * Only our independently retained watch may be touched now. */
        if(!w->request){if(!w->outstanding)watch_free(w);continue;}
        if(w->request->nr_MsgCount)w->request->nr_MsgCount--;
    }
    for(struct watch *w=m->watches;w;w=w->next)if(w->pending&&!w->outstanding) {
        /* A retired endpoint still owns notification replies, but no longer
         * has storage against which a subscription can be resolved. */
        if(!m->fs){w->pending=0;continue;}
        struct oz_zpl_info info;int rc=watch_resolve(m,w,&info);
        if(!rc||rc==ENOENT)watch_emit(m,w);
        else w->pending=0;
    }
}
static void watches_changed(struct oz_mount *m,uint64_t object,uint64_t parent,uint64_t other_parent)
{
    for(struct watch *w=m->watches;w;w=w->next) {
        struct oz_zpl_info info;int rc=watch_resolve(m,w,&info);
        if(rc&&rc!=ENOENT)continue;
        int present=!rc;
        int changed=w->present!=present||(present&&w->object!=info.object)||
            (w->present&&(w->object==object||
                (w->directory&&(w->object==parent||w->object==other_parent))));
        w->present=present;w->object=present?info.object:0;w->directory=present&&info.directory;
        if(changed)watch_emit(m,w);
    }
}
static int watch_add(struct oz_mount *m,struct NotifyRequest *nr,
    struct oz_packet_security **security)
{
    if(!nr||!nr->nr_FullName)return EINVAL;
    ULONG delivery=nr->nr_Flags&(NRF_SEND_MESSAGE|NRF_SEND_SIGNAL);
    if(delivery!=NRF_SEND_MESSAGE&&delivery!=NRF_SEND_SIGNAL)return EINVAL;
    if(delivery==NRF_SEND_MESSAGE&&!nr->nr_stuff.nr_Msg.nr_Port)return EINVAL;
    if(delivery==NRF_SEND_SIGNAL&&(!nr->nr_stuff.nr_Signal.nr_Task||
        nr->nr_stuff.nr_Signal.nr_SignalNum>=32))return EINVAL;
    for(struct watch *w=m->watches;w;w=w->next)if(w->request==nr)return EBUSY;
    const char *fullname=(const char *)nr->nr_FullName;
    const char *path=strchr(fullname,':');path=path?path+1:fullname;
    if(strnlen(path,4097)>4096)return ENAMETOOLONG;
    struct watch *w=calloc(1,sizeof(*w));if(!w)return ENOMEM;
    w->path=strdup(path);if(!w->path){free(w);return ENOMEM;}
    w->security=*security;
    struct oz_zpl_info info;int rc=watch_resolve(m,w,&info);
    if(rc&&rc!=ENOENT){w->security=NULL;watch_free(w);return rc;}
    w->present=!rc;w->object=!rc?info.object:0;w->directory=!rc&&info.directory;
    if(delivery==NRF_SEND_MESSAGE&&!m->notify_port)m->notify_port=endpoint_port(m);
    if(delivery==NRF_SEND_MESSAGE&&!m->notify_port){w->security=NULL;watch_free(w);return ENOMEM;}
    w->request=nr;
    if(w->present&&(nr->nr_Flags&NRF_NOTIFY_INITIAL)) {
        rc=watch_emit(m,w);
        if(rc){w->security=NULL;watch_free(w);return rc;}
    }
    *security=NULL; /* transfer the captured registration identity */
    w->next=m->watches;m->watches=w;nr->nr_Handler=m->port;return 0;
}
static int watch_remove(struct oz_mount *m,struct NotifyRequest *nr)
{
    struct watch **link=&m->watches;
    while(*link&&(*link)->request!=nr)link=&(*link)->next;
    if(!*link)return ENOENT;
    struct watch *w=*link;*link=w->next;
    w->request=NULL;w->pending=0;
    if(!w->outstanding)watch_free(w);
    return 0;
}
static SIPTR packet_failure(LONG action)
{
    return action==ACTION_READ_LINK||action==ACTION_READ||action==ACTION_SEEK||action==ACTION_WRITE||
        action==ACTION_SET_FILE_SIZE||action==ACTION_GET_FILE_POSITION64||
        action==ACTION_GET_FILE_SIZE64?-1:DOSFALSE;
}
static int relative_offset(uint64_t base,int64_t delta,uint64_t *result)
{
    if(base>INT64_MAX)return EOVERFLOW;
    if(delta<0) {
        uint64_t magnitude=(uint64_t)(-(delta+1))+1;
        if(magnitude>base)return EINVAL;
        *result=base-magnitude;
    } else {
        if((uint64_t)delta>INT64_MAX-base)return EOVERFLOW;
        *result=base+(uint64_t)delta;
    }
    return 0;
}
static struct locked *registered_lock(struct locked *locks,SIPTR value)
{
    for(struct locked *l=locks;l;l=l->next)if(value==(SIPTR)MKBADDR(&l->lock)&&l->used)return l;
    return NULL;
}
static struct locked *valid_lock(struct oz_mount *m,SIPTR value)
{
    struct locked *l=registered_lock(m->locks,value);
    if(!l||l->stale)return NULL;
    if(!identity_current(m,l->object,l->generation,l->kind) ||
        !identity_current(m,l->parent,l->parent_generation,1))l->stale=1;
    return l->stale?NULL:l;
}
static struct locked *reserve_lock(struct oz_mount *mount)
{
    for(struct locked *l=mount->locks;l;l=l->next)if(!l->used)return l;
    struct locked *l=calloc(1,sizeof(*l));
    if(l){l->next=mount->locks;mount->locks=l;}
    return l;
}
static SIPTR make_lock(struct oz_mount *mount,uint64_t object,uint64_t parent,const char *name,
    struct MsgPort *port,struct DosList *volume)
{
    struct oz_zpl_info info,parent_info;
    if(oz_zpl_stat(mount->fs,object,&info)||!info.generation ||
        oz_zpl_stat(mount->fs,parent,&parent_info)||!parent_info.directory)return 0;
    struct locked *l=reserve_lock(mount);
    if(l) {
        memset(&l->lock,0,sizeof(l->lock));l->used=1;l->object=object;l->parent=parent;
        l->generation=info.generation;l->kind=object_kind(&info);l->stale=0;
        l->parent_generation=parent_info.generation;
        snprintf(l->name,sizeof(l->name),"%s",name);
        l->lock.fl_Key=(IPTR)l;l->lock.fl_Access=SHARED_LOCK;
        l->lock.fl_Task=port;l->lock.fl_Volume=MKBADDR(volume);return (SIPTR)MKBADDR(&l->lock);
    }
    return 0;
}
static int packet_name(BSTR b,char *name,size_t capacity)
{
    if(!b)return EINVAL;
#ifdef AROS_FAST_BSTR
    size_t len=strnlen((const char *)AROS_BSTR_ADDR(b),capacity);
#else
    size_t len=AROS_BSTR_strlen(b);
#endif
    if(len>=capacity)return ENAMETOOLONG;
    memcpy(name,AROS_BSTR_ADDR(b),len);name[len]=0;return 0;
}
/* Directories cannot be hard linked; their canonical parent/name is unique.
 * Files instead retain the final path component and its resolved parent. */
static int path_location(struct oz_mount *m,uint64_t start,const char *path,
    const struct oz_zpl_info *info,cred_t *cr,uint64_t *parent,char name[256])
{
    if(info->directory) {
        *parent=info->parent;
        int rc=oz_zpl_name(m->fs,info->object,name,256);
        if(!rc&&!*name)snprintf(name,256,"%s",m->name);
        return rc;
    }
    const char *last=strrchr(path,'/');
    snprintf(name,256,"%s",last?last+1:path);
    if(!*name)return EINVAL;
    char prefix[256]={0};
    if(last){size_t n=last-path+1;if(n>=sizeof(prefix))return ENAMETOOLONG;memcpy(prefix,path,n);}
    struct oz_zpl_info dir;
    int rc=m->authenticated?oz_zpl_resolve_for(m->fs,start,prefix,&dir,cr,path_policy,m,ACE_EXECUTE):
        oz_zpl_resolve(m->fs,start,prefix,&dir);
    if(!rc)*parent=dir.object;
    return rc;
}
static ULONG display_protection(const struct oz_zpl_info *info)
{
    if(info->has_native_protection)return info->native_protection;
    /* Owner bits deny, group/other bits allow. Unix deletion is controlled
     * by the parent directory; do not invent a file delete ACL from mode. */
    ULONG bits=0;
    if(!(info->mode&0400))bits|=FIBF_READ;
    if(!(info->mode&0200))bits|=FIBF_WRITE;
    if(!(info->mode&0100))bits|=FIBF_EXECUTE;
    if(info->mode&0040)bits|=FIBF_GRP_READ;
    if(info->mode&0020)bits|=FIBF_GRP_WRITE;
    if(info->mode&0010)bits|=FIBF_GRP_EXECUTE;
    if(info->mode&0004)bits|=FIBF_OTR_READ;
    if(info->mode&0002)bits|=FIBF_OTR_WRITE;
    if(info->mode&0001)bits|=FIBF_OTR_EXECUTE;
    return bits;
}
static int fill_fib64(struct oz_zpl *fs,uint64_t object,const char *name,struct FileInfoBlock64 *fib)
{
    struct oz_zpl_info info;int rc=oz_zpl_stat(fs,object,&info);if(rc)return rc;
    if(strlen(name)>sizeof(fib->fib_FileName)-2)return ENAMETOOLONG;
    uint32_t owner;
    rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
    if(rc)return rc;
    char comment[80];rc=oz_zpl_comment_get(fs,object,comment,sizeof(comment));
    if(rc)return rc;
    memset(fib,0,sizeof(*fib));
    fib->fib_Comment[0]=strlen(comment);
    memcpy(fib->fib_Comment+1,comment,strlen(comment));
    fib->fib_OwnerUID=owner>>16;
    fib->fib_OwnerGID=owner&0xffff;
    fib->fib_DirEntryType=object==oz_zpl_root(fs)?ST_ROOT:entry_type(&info);
    fib->fib_EntryType=fib->fib_DirEntryType;
    /* DOS packet FileInfoBlock strings are BCPL even on FAST_BSTR builds;
     * dos.library fixfib converts them for the public Examine/ExNext API. */
    fib->fib_FileName[0]=strlen(name);memcpy(fib->fib_FileName+1,name,strlen(name));
    fib->fib_Size=info.directory?0:info.size;fib->fib_NumBlocks=info.blocks;
    fib->fib_Protection=display_protection(&info);
    if(!info.directory&&!info.symlink&&!oz_zpl_exec(fs)) {
        fib->fib_Protection |= FIBF_EXECUTE;
        fib->fib_Protection &= ~(FIBF_GRP_EXECUTE|FIBF_OTR_EXECUTE);
    }
    if(oz_zpl_readonly(fs)) {
        fib->fib_Protection |= FIBF_WRITE|FIBF_DELETE;
        fib->fib_Protection &= ~(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE);
    }
    uint64_t secs=info.mtime>=252460800?info.mtime-252460800:0;
    fib->fib_Date.ds_Days=secs/86400;fib->fib_Date.ds_Minute=(secs%86400)/60;
    fib->fib_Date.ds_Tick=(secs%60)*TICKS_PER_SECOND+
        (info.mtime>=252460800?info.mtime_nsec/(1000000000/TICKS_PER_SECOND):0);return 0;
}

static int fill_fib(struct oz_zpl *fs,uint64_t object,const char *name,struct FileInfoBlock *fib)
{
    struct FileInfoBlock64 wide;
    int rc=fill_fib64(fs,object,name,&wide);if(rc)return rc;
    memset(fib,0,sizeof(*fib));
    fib->fib_DiskKey=wide.fib_DiskKey;
    fib->fib_DirEntryType=wide.fib_DirEntryType;
    fib->fib_EntryType=wide.fib_EntryType;
    memcpy(fib->fib_FileName,wide.fib_FileName,sizeof(fib->fib_FileName));
    memcpy(fib->fib_Comment,wide.fib_Comment,sizeof(fib->fib_Comment));
    fib->fib_Protection=wide.fib_Protection;fib->fib_Date=wide.fib_Date;
    fib->fib_OwnerUID=wide.fib_OwnerUID;fib->fib_OwnerGID=wide.fib_OwnerGID;
    /* Legacy directory consumers must still see all entries. Match the
     * saturation policy used by PFS3; exact sizes use EXAMINE_*64. */
    fib->fib_Size=sizeof(fib->fib_Size)==4&&wide.fib_Size>INT32_MAX?INT32_MAX:wide.fib_Size;
    fib->fib_NumBlocks=sizeof(fib->fib_NumBlocks)==4&&wide.fib_NumBlocks>INT32_MAX?INT32_MAX:wide.fib_NumBlocks;
    return 0;
}

static LONG examine_all(struct oz_mount *mount,struct locked *lock,
    struct DosPacket *dp,cred_t *cr)
{
    struct ExAllControl *ctl=(void *)dp->dp_Arg5;
    unsigned char *buffer=(void *)dp->dp_Arg2;
    SIPTR capacity=dp->dp_Arg3,level=dp->dp_Arg4;
    if(!ctl)return ERROR_BAD_NUMBER;
    ctl->eac_Entries=0;
    if(!lock)return ERROR_INVALID_LOCK;
    if(!buffer||capacity<0||level<ED_NAME||level>ED_OWNER||
        (IPTR)buffer%sizeof(IPTR)||ctl->eac_LastKey>UINT32_MAX)
        return ERROR_BAD_NUMBER;
    struct oz_zpl_info info;
    int rc=oz_zpl_stat(mount->fs,lock->object,&info);
    if(rc)return dos_error(rc);
    if(!info.directory)return ERROR_OBJECT_WRONG_TYPE;
    rc=access_object(mount,lock->object,ACE_LIST_DIRECTORY|ACE_EXECUTE,cr);
    if(rc)return dos_error(rc);
    const size_t fields[]={0,offsetof(struct ExAllData,ed_Type),
        offsetof(struct ExAllData,ed_Size),offsetof(struct ExAllData,ed_Prot),
        offsetof(struct ExAllData,ed_Days),offsetof(struct ExAllData,ed_Comment),
        offsetof(struct ExAllData,ed_OwnerUID),sizeof(struct ExAllData)};
    struct ExAllData *previous=NULL;
    size_t used=0;
    for(;;) {
        char name[256],comment[80]={0};
        if(ctl->eac_LastKey==UINT32_MAX)return ERROR_BAD_NUMBER;
        rc=oz_zpl_nth(mount->fs,lock->object,ctl->eac_LastKey,name,sizeof(name),&info);
        if(rc)return rc==ENOENT?ERROR_NO_MORE_ENTRIES:dos_error(rc);
        rc=access_object(mount,info.object,ACE_READ_ATTRIBUTES,cr);
        if(rc)return dos_error(rc);
        if(ctl->eac_MatchString&&!MatchPatternNoCase(ctl->eac_MatchString,(STRPTR)name)) {
            ctl->eac_LastKey++;continue;
        }
        uint32_t owner=0;
        if(level>=ED_OWNER) {
            rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
            if(rc)return dos_error(rc);
        }
        if(level>=ED_COMMENT) {
            rc=oz_zpl_comment_get(mount->fs,info.object,comment,sizeof(comment));
            if(rc)return dos_error(rc);
        }
        size_t namesize=strlen(name)+1;
        size_t needed=fields[level]+namesize+(level>=ED_COMMENT?strlen(comment)+1:0);
        if(used>(size_t)capacity||needed>(size_t)capacity-used)
            return ctl->eac_Entries?0:ERROR_BUFFER_OVERFLOW;
        struct ExAllData *entry=(void *)(buffer+used);
        memset(entry,0,fields[level]);
        entry->ed_Name=buffer+used+fields[level];
        memcpy(entry->ed_Name,name,namesize);
        if(level>=ED_TYPE)entry->ed_Type=entry_type(&info);
        if(level>=ED_SIZE)entry->ed_Size=info.directory?0:
            sizeof(entry->ed_Size)==4&&info.size>UINT32_MAX?UINT32_MAX:info.size;
        if(level>=ED_PROTECTION) {
            entry->ed_Prot=display_protection(&info);
            if(!info.directory&&!info.symlink&&!oz_zpl_exec(mount->fs)) {
                entry->ed_Prot|=FIBF_EXECUTE;
                entry->ed_Prot&=~(FIBF_GRP_EXECUTE|FIBF_OTR_EXECUTE);
            }
            if(oz_zpl_readonly(mount->fs)) {
                entry->ed_Prot|=FIBF_WRITE|FIBF_DELETE;
                entry->ed_Prot&=~(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE);
            }
        }
        if(level>=ED_DATE) {
            uint64_t seconds=info.mtime>=252460800?info.mtime-252460800:0;
            entry->ed_Days=seconds/86400;entry->ed_Mins=(seconds%86400)/60;
            entry->ed_Ticks=(seconds%60)*TICKS_PER_SECOND+
                (info.mtime>=252460800?info.mtime_nsec/(1000000000/TICKS_PER_SECOND):0);
        }
        if(level>=ED_COMMENT) {
            entry->ed_Comment=entry->ed_Name+namesize;
            memcpy(entry->ed_Comment,comment,strlen(comment)+1);
        }
        if(level>=ED_OWNER){entry->ed_OwnerUID=owner>>16;entry->ed_OwnerGID=owner&0xffff;}
        /* Match native dos.library's hook ABI: object=entry, message=&type. */
        LONG type=level;
        ctl->eac_LastKey++;
        if(ctl->eac_MatchFunc&&!CALLHOOKPKT(ctl->eac_MatchFunc,entry,&type))continue;
        entry->ed_Next=NULL;
        if(previous)previous->ed_Next=entry;
        previous=entry;ctl->eac_Entries++;
        used+=(needed+sizeof(IPTR)-1)&~(sizeof(IPTR)-1);
    }
}

/* Match native filesystem volume publication: desktop listeners rescan DOS
 * entries on input-device disk events. Never emit while holding DOSList. */
static void volume_event(UBYTE event)
{
    struct MsgPort *port=CreateMsgPort();
    if(!port)return;
    struct IOStdReq *io=(struct IOStdReq *)CreateIORequest(port,sizeof(*io));
    if(io) {
        if(!OpenDevice((CONST_STRPTR)"input.device",0,(struct IORequest *)io,0)) {
            struct InputEvent input={0};input.ie_Class=event;
            io->io_Command=IND_WRITEEVENT;io->io_Data=&input;io->io_Length=sizeof(input);
            DoIO((struct IORequest *)io);
            CloseDevice((struct IORequest *)io);
        }
        DeleteIORequest((struct IORequest *)io);
    }
    DeleteMsgPort(port);
}
static int mount_impl(struct oz_zpl *fs,const char *name,struct oz_mount **out,int authenticated,int signal,
    struct DosList *existing)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!fs||!name||!*name||strlen(name)>=32||strpbrk(name,":/"))return EINVAL;
    if(!authenticated&&!oz_zpl_readonly(fs))return EINVAL;
    struct oz_mount *mount=calloc(1,sizeof(*mount));if(!mount)return ENOMEM;
    int (*publication_begin)(void)=existing?oz_device_startup_publication_begin:oz_device_publication_begin;
    void (*publication_end)(void)=existing?oz_device_startup_publication_end:oz_device_publication_end;
    mount->fs=fs;mount->authenticated=authenticated;mount->shared_signal=signal;strcpy(mount->name,name);
    mount->borrowed_device=existing!=NULL;
    mount->port=endpoint_port(mount);
    mount->device=existing?existing:MakeDosEntry((CONST_STRPTR)name,DLT_DEVICE);
    mount->volume=MakeDosEntry((CONST_STRPTR)name,DLT_VOLUME);
    int rc=ENOMEM;
    if(!mount->port||!mount->device||!mount->volume)goto fail;
    mount->volume->dol_Task=mount->port;
    mount->environment.de_TableSize=DE_DOSTYPE;
    mount->environment.de_DosType=0x5a465301;
    /* A logical dataset has no single Exec device. Publish a valid empty
     * BSTR nevertheless: native DOS consumers (including DiskInfo) read it
     * whenever dol_Startup is present. The constant outlives this endpoint. */
    mount->startup.fssm_Device=AROS_CONST_BSTR("");
    mount->startup.fssm_Environ=MKBADDR(&mount->environment);
    if(!existing) {
        mount->device->dol_Task=mount->port;
        mount->device->dol_misc.dol_handler.dol_Startup=MKBADDR(&mount->startup);
    }
    struct DeviceList *v=(struct DeviceList *)mount->volume;
    v->dl_DiskType=0x5a465301;DateStamp(&v->dl_VolumeDate);
    /* Ordinary creation publishes both entries under exclusive namespace
     * ownership. Startup adoption instead follows AddDosEntry's read plus
     * entry-write contract: RunHandler may wait while holding LDF_READ.
     * Its device already exists; attach its ready endpoint only after volume
     * insertion succeeds. Both paths reject lock contention without waiting. */
    rc=publication_begin();
    if(rc){
        char line[96];snprintf(line,sizeof(line),
            "OpenZFS DOS mount failed stage=publication-lock errno=%d\n",rc);
        const char *diagnostic=line;
        while(*diagnostic)RawPutChar(*diagnostic++);
        goto fail;
    }
    if(existing) {
        struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
        struct DosList *found=FindDosEntry(list,(CONST_STRPTR)name,LDF_DEVICES);
        int valid=found==existing&&existing->dol_Type==DLT_DEVICE&&!existing->dol_Task;
        UnLockDosList(LDF_DEVICES|LDF_READ);
        if(!valid){publication_end();rc=EINVAL;goto fail;}
    }
    /* AddDosEntry deliberately permits duplicate volume labels. A dataset's
     * explicit DOS alias must not hide a foreign volume of the same name.
     * Check both publication paths while holding the publication lock. */
    struct DosList *volumes=LockDosList(LDF_VOLUMES|LDF_READ);
    int collision=FindDosEntry(volumes,(CONST_STRPTR)name,LDF_VOLUMES)!=NULL;
    UnLockDosList(LDF_VOLUMES|LDF_READ);
    if(collision){publication_end();rc=EEXIST;goto fail;}
    if(!existing&&!AddDosEntry(mount->device)){
        publication_end();rc=EEXIST;goto fail;
    }
    if(!AddDosEntry(mount->volume)) {
        if(!existing)RemDosEntry(mount->device);
        publication_end();
        rc=EEXIST;goto fail;
    }
    if(existing)existing->dol_Task=mount->port;
    publication_end();
    *out=mount;
    volume_event(IECLASS_DISKINSERTED);
    return 0;
fail:
    if(mount->device&&!mount->borrowed_device)FreeDosEntry(mount->device);
    if(mount->volume)FreeDosEntry(mount->volume);
    endpoint_port_free(mount,mount->port);
    free(mount);return rc;
}
int oz_dos_mount(struct oz_zpl *fs,const char *name,struct oz_mount **out)
{return mount_impl(fs,name,out,1,-1,NULL);}
int oz_dos_mount_fixture(struct oz_zpl *fs,const char *name,struct oz_mount **out)
{return mount_impl(fs,name,out,0,-1,NULL);}
int oz_dos_mount_adopt(struct oz_zpl *fs,const char *name,struct DosList *device,
    struct oz_mount **out,int authenticated,int signal)
{
    if(out)*out=NULL;
    if(!device||signal < -1||signal>=32)return EINVAL;
    return mount_impl(fs,name,out,authenticated,signal,device);
}
int oz_dos_mount_shared(struct oz_zpl *fs,const char *name,struct oz_mount **out,int authenticated,int signal)
{
    if(signal<0||signal>=32)return EINVAL;
    return mount_impl(fs,name,out,authenticated,signal,NULL);
}
ULONG oz_dos_signals(struct oz_mount *mount)
{return mount?(1UL<<mount->port->mp_SigBit)|(mount->timer_port?1UL<<mount->timer_port->mp_SigBit:0)|
    (mount->notify_port?1UL<<mount->notify_port->mp_SigBit:0):0;}
void oz_dos_pump(struct oz_mount *mount)
{
    struct oz_zpl *fs=mount->fs;
    struct MsgPort *port=mount->port;
    struct DosList *volume=mount->volume;
    records_pump(mount);
    watches_pump(mount);
    struct Message *message;
    while((message=GetMsg(port))!=NULL) {
        /* Earlier packets in this same drain can grow either list. Never
         * validate later requests against a pre-drain head snapshot. */
        struct handle *handles=mount->handles;
        struct locked *locks=mount->locks;
        struct DosPacket *dp=(struct DosPacket *)message->mn_Node.ln_Name;
        if(!dp)continue;
        if(dp->dp_Type==ACTION_EXAMINE_ALL&&dp->dp_Arg5)
            ((struct ExAllControl *)dp->dp_Arg5)->eac_Entries=0;
        if(!fs) {
            if(dp->dp_Type==ACTION_REMOVE_NOTIFY) {
                int rc=watch_remove(mount,(void *)dp->dp_Arg1);
                ReplyPkt(dp,rc?DOSFALSE:DOSTRUE,rc?dos_error(rc):0);
                continue;
            }
            SIPTR failure=packet_failure(dp->dp_Type);
            ReplyPkt(dp,failure,ERROR_DEVICE_NOT_MOUNTED);continue;
        }
        /* Terminal receive failure must not resurrect the mount, but closing
         * existing handles/locks and subscriptions must remain possible. */
        if(oz_zpl_ended(fs) && dp->dp_Type!=ACTION_END &&
            dp->dp_Type!=ACTION_FREE_LOCK && dp->dp_Type!=ACTION_FREE_RECORD &&
            dp->dp_Type!=ACTION_REMOVE_NOTIFY) {
            ReplyPkt(dp,packet_failure(dp->dp_Type),ERROR_DEVICE_NOT_MOUNTED);
            continue;
        }
    SIPTR value=DOSFALSE;LONG error=0;int deferred=0;
    cred_t *cr=NULL;
    struct oz_packet_security *security=NULL;
    /* Volume discovery/capacity and releasing existing references disclose
     * no file contents. Cleanup must still work after caller logout. All
     * data/metadata requests use the current packet sender, never kcred. */
    int public_action=dp->dp_Type==ACTION_END || dp->dp_Type==ACTION_FREE_LOCK ||dp->dp_Type==ACTION_FREE_RECORD||
        dp->dp_Type==ACTION_REMOVE_NOTIFY||
        dp->dp_Type==ACTION_IS_FILESYSTEM || dp->dp_Type==ACTION_CURRENT_VOLUME ||
        dp->dp_Type==ACTION_IS_SECFS || dp->dp_Type==ACTION_GET_SECFS_VERSION ||
        dp->dp_Type==ACTION_INFO || dp->dp_Type==ACTION_DISK_INFO || dp->dp_Type==ACTION_FLUSH;
    if(mount->authenticated&&!public_action) {
        int rc=oz_packet_security_begin(dp,port,&security);
        if(rc) {
            value=packet_failure(dp->dp_Type);
            ReplyPkt(dp,value,rc==ENOMEM?ERROR_NO_FREE_STORE:ERROR_READ_PROTECTED);continue;
        }
        cr=oz_packet_security_cred(security);
    }
    mount->security=security;
    struct handle *h=(dp->dp_Type==ACTION_END||dp->dp_Type==ACTION_FREE_RECORD)?
        registered_handle(handles,dp->dp_Arg1):valid_handle(mount,dp->dp_Arg1);
    struct locked *lock=dp->dp_Type==ACTION_FREE_LOCK?
        registered_lock(locks,dp->dp_Arg1):valid_lock(mount,dp->dp_Arg1);
    switch(dp->dp_Type) {
    case OZ_ACTION_REWRITE: {
        struct oz_rewrite_request *request=(void *)dp->dp_Arg2;
        if(!request||dp->dp_Arg3!=sizeof(*request)||dp->dp_Arg4||dp->dp_Arg5){error=ERROR_BAD_NUMBER;break;}
        struct oz_rewrite_request input=*request;
        if(input.version!=OZ_REWRITE_VERSION||input.reserved||input.reserved2||
            (input.flags&~ZFS_REWRITE_PHYSICAL)||input.argument){error=ERROR_BAD_NUMBER;break;}
        int rc=0;
        if(!h)rc=EBADF;
        else if(!security||!cr)rc=EACCES;
        else if(oz_zpl_readonly(fs))rc=EROFS;
        else {
            struct oz_zpl_info info;
            rc=oz_zpl_stat(fs,h->object,&info);
            if(!rc&&(info.directory||info.special||info.symlink))rc=info.directory?EISDIR:ENOTSUP;
            if(!rc)rc=access_object(mount,h->object,ACE_WRITE_DATA,cr);
            if(!rc)rc=oz_zpl_rewrite_as(fs,h->object,input.offset,input.length,
                input.flags,input.argument,cr);
        }
        request->engine_error=rc;
        if(rc)error=dos_error(rc);else value=DOSTRUE;
        break;
    }
    case OZ_ACTION_PROJECT: {
        struct oz_project_request *request=(void *)dp->dp_Arg2;
        if(!request||dp->dp_Arg3!=sizeof(*request)||dp->dp_Arg4||dp->dp_Arg5){error=ERROR_BAD_NUMBER;break;}
        struct oz_project_request input=*request;
        if(input.version!=OZ_PROJECT_VERSION||input.reserved||input.operation>OZ_PROJECT_SET){error=ERROR_BAD_NUMBER;break;}
        int rc=0;
        uint32_t id=input.project;int inherit=input.inherit;
        if(!lock)rc=EBADF;
        else if(!security||!cr)rc=EACCES;
        else if(input.operation==OZ_PROJECT_GET){
            rc=access_object(mount,lock->object,ACE_READ_ATTRIBUTES,cr);
            if(!rc)rc=oz_zpl_project_get(fs,lock->object,&id,&inherit,cr);
        }else{
            struct oz_zpl_info info;
            if(input.project==UINT32_MAX||input.inherit>1)rc=EINVAL;
            if(!rc)rc=oz_zpl_stat(fs,lock->object,&info);
            uint32_t owner;
            if(!rc)rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
            if(!rc)rc=oz_packet_security_property(security,owner);
            if(!rc)rc=oz_packet_security_volume(security,secAt_Write);
            if(!rc&&exclusive_object(mount,info.object)&&lock->lock.fl_Access!=EXCLUSIVE_LOCK)rc=EBUSY;
            if(!rc)rc=oz_zpl_project_set(fs,lock->object,input.project,input.inherit,cr);
            if(!rc)rc=oz_zpl_sync(fs);
            if(!rc)watches_changed(mount,info.object,info.parent,0);
        }
        request->engine_error=rc;
        if(rc)error=dos_error(rc);
        else {request->project=id;request->inherit=inherit;value=DOSTRUE;}
        break;
    }
    case ACTION_ADD_NOTIFY: {
        int rc=watch_add(mount,(void *)dp->dp_Arg1,&security);
        if(rc)error=dos_error(rc);else value=DOSTRUE;
        break;
    }
    case ACTION_REMOVE_NOTIFY: {
        int rc=watch_remove(mount,(void *)dp->dp_Arg1);
        if(rc)error=dos_error(rc);else value=DOSTRUE;
        break;
    }
    case ACTION_LOCK_RECORD: {
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if((uint64_t)dp->dp_Arg2>UINT32_MAX||(uint64_t)dp->dp_Arg3>UINT32_MAX||
            (uint64_t)dp->dp_Arg4>REC_SHARED_IMMED||(uint64_t)dp->dp_Arg5>UINT32_MAX)
            {error=ERROR_BAD_NUMBER;break;}
        int rc=record_access(mount,h,cr);
        if(rc){error=rc==ENOMEM?ERROR_NO_FREE_STORE:ERROR_READ_PROTECTED;break;}
        struct record *r=calloc(1,sizeof(*r));
        if(!r){error=ERROR_NO_FREE_STORE;break;}
        r->handle=h;r->start=(ULONG)dp->dp_Arg2;r->length=(ULONG)dp->dp_Arg3;
        r->shared=(dp->dp_Arg4&2)!=0;
        if(!record_conflict(mount,r)) {
            r->next=mount->records;mount->records=r;value=DOSTRUE;
        } else {
            if(dp->dp_Arg4&1)error=ERROR_LOCK_COLLISION;
            else if(!dp->dp_Arg5)error=ERROR_LOCK_TIMEOUT;
            else {error=record_queue(mount,r,dp,(ULONG)dp->dp_Arg5);deferred=!error;}
            if(!deferred)free(r);
        }
        break;
    }
    case ACTION_FREE_RECORD: {
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if((uint64_t)dp->dp_Arg2>UINT32_MAX||(uint64_t)dp->dp_Arg3>UINT32_MAX)
            {error=ERROR_BAD_NUMBER;break;}
        struct record **link=&mount->records;
        while(*link&&((*link)->handle!=h||(*link)->start!=(ULONG)dp->dp_Arg2||
            (*link)->length!=(ULONG)dp->dp_Arg3))link=&(*link)->next;
        if(!*link){error=ERROR_RECORD_NOT_LOCKED;break;}
        struct record *r=*link;*link=r->next;free(r);value=DOSTRUE;break;
    }
    case ACTION_EXAMINE_ALL:
        error=examine_all(mount,lock,dp,cr);value=error?DOSFALSE:DOSTRUE;break;
    case ACTION_EXAMINE_ALL_END: {
        struct ExAllControl *ctl=(void *)dp->dp_Arg5;
        if(!lock)error=ERROR_INVALID_LOCK;
        else if(!ctl)error=ERROR_BAD_NUMBER;
        else {ctl->eac_LastKey=0;ctl->eac_Entries=0;value=DOSTRUE;}
        break;
    }
    case ACTION_MAKE_LINK: {
        if(dp->dp_Arg4==LINK_SOFT) {
            if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
            if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
            const char *target=(void *)dp->dp_Arg3;
            if(!target||!*target||strnlen(target,4097)>4096){error=ERROR_LINE_TOO_LONG;break;}
            char name[256],leaf[256];struct oz_zpl_info dir;
            int rc=packet_name((BSTR)dp->dp_Arg2,name,sizeof(name));
            if(rc){error=dos_error(rc);break;}
            char *colon=strchr(name,':'),*base=colon?colon+1:name,*last=strrchr(base,'/');
            snprintf(leaf,sizeof(leaf),"%s",last?last+1:base);if(last)last[1]=0;
            rc=oz_zpl_resolve_for(fs,(!colon&&lock)?lock->object:oz_zpl_root(fs),last?base:"",
                &dir,cr,path_policy,mount,ACE_EXECUTE|ACE_ADD_FILE);
            uint32_t defaults=0;
            if(!rc)rc=oz_packet_security_defaults(security,&defaults);
            if(!rc)rc=oz_zpl_symlink_as(fs,dir.object,leaf,target,defaults,cr);
            if(rc)error=dos_error(rc);else value=DOSTRUE;
            if(!rc)watches_changed(mount,0,dir.object,0);
            break;
        }
        if(dp->dp_Arg4!=LINK_HARD){error=ERROR_ACTION_NOT_KNOWN;break;}
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        struct locked *source=valid_lock(mount,dp->dp_Arg3);
        if(!source||(dp->dp_Arg1&&!lock)){error=ERROR_INVALID_LOCK;break;}
        char name[256],leaf[256];struct oz_zpl_info directory,info;
        int rc=packet_name((BSTR)dp->dp_Arg2,name,sizeof(name));
        if(rc){error=dos_error(rc);break;}
        char *colon=strchr(name,':'),*base=colon?colon+1:name,*last=strrchr(base,'/');
        snprintf(leaf,sizeof(leaf),"%s",last?last+1:base);
        if(last)last[1]=0;
        rc=oz_zpl_stat(fs,source->object,&info);
        if(!rc&&info.directory){error=ERROR_OBJECT_WRONG_TYPE;break;}
        if(!rc)rc=access_object(mount,source->object,ACE_READ_ATTRIBUTES,cr);
        uint32_t owner;
        if(!rc)rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
        if(!rc)rc=oz_packet_security_property(security,owner);
        if(!rc)rc=oz_zpl_resolve_for(fs,(!colon&&lock)?lock->object:oz_zpl_root(fs),
            last?base:"",&directory,cr,path_policy,mount,ACE_EXECUTE);
        if(!rc)rc=native_access_object(mount,directory.object,secAt_Write);
        if(!rc)rc=oz_zpl_link_as(fs,directory.object,source->object,leaf,cr);
        if(rc)error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);
        else value=DOSTRUE;
        if(!rc)watches_changed(mount,source->object,directory.object,0);
        break;
    }
    case ACTION_READ_LINK: {
        value=-1;
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        const char *input=(void *)dp->dp_Arg2;char *output=(void *)dp->dp_Arg3;
        if(!input||!output||dp->dp_Arg4<0){error=ERROR_BAD_NUMBER;break;}
        char path[4097],target[4097];size_t length=strnlen(input,sizeof(path));
        if(length==sizeof(path)){error=ERROR_LINE_TOO_LONG;break;}
        memcpy(path,input,length+1);
        char *colon=strchr(path,':'),*part=colon?colon+1:path;
        struct oz_zpl_info info;int rc=oz_zpl_stat(fs,(!colon&&lock)?lock->object:oz_zpl_root(fs),&info);
        int found=0;
        while(!rc&&*part) {
            if(!info.directory){rc=ENOTDIR;break;}
            rc=access_object(mount,info.object,ACE_EXECUTE,cr);if(rc)break;
            if(*part=='/') {
                if(info.object==oz_zpl_root(fs)){rc=ENOENT;break;}
                rc=oz_zpl_stat(fs,info.parent,&info);part++;continue;
            }
            char *end=strchr(part,'/');size_t n=end?(size_t)(end-part):strlen(part);
            char leaf[256];if(n>=sizeof(leaf)){rc=ENAMETOOLONG;break;}
            memcpy(leaf,part,n);leaf[n]=0;
            rc=oz_zpl_lookup_at(fs,info.object,leaf,&info);if(rc)break;
            if(info.symlink) {
                rc=access_object(mount,info.object,ACE_READ_ATTRIBUTES,cr);
                if(!rc)rc=oz_zpl_readlink_as(fs,info.object,target,sizeof(target),cr);
                if(rc)break;
                size_t prefix=strchr(target,':')?0:(size_t)(part-path);
                size_t target_length=strlen(target);
                /* AROS '/' is also parent traversal: do not manufacture a
                 * double separator, or RAM:/child instead of RAM:child. */
                if(end&&target_length&&target[target_length-1]=='/')target_length--;
                if(end&&target_length&&target[target_length-1]==':')end++;
                size_t tail=end?strlen(end):0,total=prefix+target_length+tail;
                if(total>=(size_t)dp->dp_Arg4){value=-2;found=1;break;}
                memcpy(output,path,prefix);memcpy(output+prefix,target,target_length);
                if(tail)memcpy(output+prefix+target_length,end,tail);
                output[total]=0;value=total;found=1;break;
            }
            part=end?end+1:part+n;
        }
        if(rc)error=dos_error(rc);else if(!found)error=ERROR_OBJECT_NOT_FOUND;
        break;
    }
    case ACTION_RENAME_OBJECT: {
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        struct locked *parents[2]={lock,valid_lock(mount,dp->dp_Arg3)};
        if((dp->dp_Arg1&&!parents[0])||(dp->dp_Arg3&&!parents[1]))
            {error=ERROR_INVALID_LOCK;break;}
        BSTR args[2]={(BSTR)dp->dp_Arg2,(BSTR)dp->dp_Arg4};
        char names[2][256],leaves[2][256];struct oz_zpl_info dirs[2],info;
        int rc=0;
        for(unsigned i=0;i<2&&!rc;i++) {
            rc=packet_name(args[i],names[i],sizeof(names[i]));
            if(rc)break;
            char *colon=strchr(names[i],':'),*base=colon?colon+1:names[i],*last=strrchr(base,'/');
            snprintf(leaves[i],sizeof(leaves[i]),"%s",last?last+1:base);
            if(last)last[1]=0;
            rc=oz_zpl_resolve_for(fs,(!colon&&parents[i])?parents[i]->object:oz_zpl_root(fs),
                last?base:"",&dirs[i],cr,path_policy,mount,ACE_EXECUTE);
            if(!rc)rc=native_access_object(mount,dirs[i].object,secAt_Write);
        }
        if(!rc)rc=oz_zpl_lookup_at(fs,dirs[0].object,leaves[0],&info);
        if(!rc&&exclusive_object(mount,info.object))rc=EBUSY;
        uint32_t owner;
        if(!rc)rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
        if(!rc)rc=oz_packet_security_property(security,owner);
        /* Compute cache matches before changing the namespace. A Unicode
         * comparison error must abort the operation, not leave half-updated
         * names. Object+parent alone would also select different hard links. */
        for(struct locked *entry=locks;entry;entry=entry->next) {
            entry->rename_match=0;
            if(!rc&&entry->used&&entry->object==info.object&&entry->parent==dirs[0].object)
                rc=oz_zpl_same_name(fs,entry->name,leaves[0],&entry->rename_match);
        }
        for(struct handle *entry=handles;entry;entry=entry->next) {
            entry->rename_match=0;
            if(!rc&&entry->used&&entry->object==info.object&&entry->parent==dirs[0].object)
                rc=oz_zpl_same_name(fs,entry->name,leaves[0],&entry->rename_match);
        }
        if(!rc)rc=oz_zpl_rename_as(fs,dirs[0].object,leaves[0],dirs[1].object,leaves[1],cr);
        if(rc)error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);
        else {
            /* File handles retain object IDs; locks must expose the new
             * leaf to Examine/NameFromLock after the namespace move. */
            for(struct locked *entry=locks;entry;entry=entry->next)if(entry->rename_match) {
                snprintf(entry->name,sizeof(entry->name),"%s",leaves[1]);
                entry->parent=dirs[1].object;
                entry->parent_generation=dirs[1].generation;
            }
            for(struct handle *entry=handles;entry;entry=entry->next)if(entry->rename_match) {
                snprintf(entry->name,sizeof(entry->name),"%s",leaves[1]);
                entry->parent=dirs[1].object;
                entry->parent_generation=dirs[1].generation;
            }
            value=DOSTRUE;
            watches_changed(mount,info.object,dirs[0].object,dirs[1].object);
        }
        break;
    }
    case ACTION_DELETE_OBJECT: {
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        char name[256],leaf[256];int rc=packet_name((BSTR)dp->dp_Arg2,name,sizeof(name));
        if(rc){error=dos_error(rc);break;}
        char *colon=strchr(name,':'),*base=colon?colon+1:name,*last=strrchr(base,'/');
        snprintf(leaf,sizeof(leaf),"%s",last?last+1:base);
        if(last)last[1]=0;
        struct oz_zpl_info directory,info;
        rc=oz_zpl_resolve_for(fs,(!colon&&lock)?lock->object:oz_zpl_root(fs),
            last?base:"",&directory,cr,path_policy,mount,ACE_EXECUTE);
        if(!rc)rc=oz_zpl_lookup_at(fs,directory.object,leaf,&info);
        /* Native AFS policy: target delete, parent write. Upstream unlink
         * supplies the ZFS DELETE-or-DELETE_CHILD ACL decision itself. */
        if(!rc)rc=native_access_object(mount,info.object,secAt_Delete);
        if(!rc)rc=native_access_object(mount,directory.object,secAt_Write);
        if(!rc)for(struct handle *entry=handles;entry;entry=entry->next)if(entry->used&&entry->object==info.object)rc=EBUSY;
        if(!rc)for(struct locked *entry=locks;entry;entry=entry->next)if(entry->used&&entry->object==info.object)rc=EBUSY;
        if(!rc)rc=oz_zpl_remove_as(fs,directory.object,leaf,info.directory,cr);
        if(rc)error=(rc==EACCES||rc==EPERM)?ERROR_DELETE_PROTECTED:dos_error(rc);
        else value=DOSTRUE;
        if(!rc)watches_changed(mount,info.object,directory.object,0);
        break;
    }
    case ACTION_CREATE_DIR: {
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        if(!reserve_lock(mount)){error=ERROR_NO_FREE_STORE;break;}
        char name[256],leaf[256];int rc=packet_name((BSTR)dp->dp_Arg2,name,sizeof(name));
        if(rc){error=dos_error(rc);break;}
        char *colon=strchr(name,':'),*base=colon?colon+1:name,*last=strrchr(base,'/');
        snprintf(leaf,sizeof(leaf),"%s",last?last+1:base);
        if(last)last[1]=0;
        struct oz_zpl_info directory,info;
        rc=oz_zpl_resolve_for(fs,(!colon&&lock)?lock->object:oz_zpl_root(fs),
            last?base:"",&directory,cr,path_policy,mount,ACE_EXECUTE|ACE_ADD_SUBDIRECTORY);
        uint32_t defaults=0;
        if(!rc)rc=oz_packet_security_defaults(security,&defaults);
        if(!rc)rc=oz_zpl_mkdir_as(fs,directory.object,leaf,defaults,cr,&info);
        if(rc){error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);break;}
        value=make_lock(mount,info.object,directory.object,leaf,port,volume);
        if(!value)error=ERROR_NO_FREE_STORE;
        watches_changed(mount,info.object,directory.object,0);
        break;
    }
    case ACTION_SET_COMMENT:
    case ACTION_SET_OWNER:
    case ACTION_SET_DATE:
    case ACTION_SET_PROTECT: {
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        if(!security){error=ERROR_WRITE_PROTECTED;break;}
        struct locked *parent=valid_lock(mount,dp->dp_Arg2);
        if(dp->dp_Arg2&&!parent){error=ERROR_INVALID_LOCK;break;}
        uint32_t bits=0,nanos=0;int64_t seconds=0;char comment[80];
        if(dp->dp_Type==ACTION_SET_COMMENT) {
            int rc=packet_name((BSTR)dp->dp_Arg4,comment,sizeof(comment));
            if(rc){error=rc==ENAMETOOLONG?ERROR_COMMENT_TOO_BIG:ERROR_BAD_NUMBER;break;}
        } else if(dp->dp_Type==ACTION_SET_DATE) {
            const struct DateStamp *date=(const struct DateStamp *)dp->dp_Arg4;
            if(!date||date->ds_Days<0||date->ds_Minute<0||date->ds_Minute>=1440||
                date->ds_Tick<0||date->ds_Tick>=60*TICKS_PER_SECOND)
                {error=ERROR_BAD_NUMBER;break;}
            seconds=252460800+(int64_t)date->ds_Days*86400+
                (int64_t)date->ds_Minute*60+date->ds_Tick/TICKS_PER_SECOND;
            nanos=(date->ds_Tick%TICKS_PER_SECOND)*(1000000000/TICKS_PER_SECOND);
        } else {
            bits=(uint32_t)dp->dp_Arg4;
            if(dp->dp_Arg4!=(SIPTR)(LONG)bits && dp->dp_Arg4!=(SIPTR)bits)
                {error=ERROR_BAD_NUMBER;break;}
        }
        char name[256];int rc=packet_name((BSTR)dp->dp_Arg3,name,sizeof(name));
        char *colon=rc?NULL:strchr(name,':');
        struct oz_zpl_info info;
        if(!rc)rc=oz_zpl_resolve_property(fs,(!colon&&parent)?parent->object:oz_zpl_root(fs),
            colon?colon+1:name,&info,cr,path_policy,mount,
            dp->dp_Type==ACTION_SET_OWNER?ACE_WRITE_OWNER:ACE_WRITE_ATTRIBUTES);
        uint32_t owner;
        if(!rc)rc=oz_aros_owner_from_ids(info.mapped_uid,info.mapped_gid,&owner);
        if(!rc)rc=dp->dp_Type==ACTION_SET_OWNER?
            oz_packet_security_set_owner(security,owner,bits):oz_packet_security_property(security,owner);
        if(!rc)rc=oz_packet_security_volume(security,secAt_Write);
        if(!rc&&exclusive_object(mount,info.object))rc=EBUSY;
        if(!rc&&dp->dp_Type==ACTION_SET_OWNER) {
            uint32_t uid,gid;rc=oz_aros_ids_from_owner(bits,&uid,&gid);
            if(!rc)rc=oz_zpl_set_owner(fs,info.object,uid,gid,cr);
        } else if(!rc&&dp->dp_Type==ACTION_SET_COMMENT)rc=oz_zpl_comment_store(fs,info.object,comment,cr);
        else if(!rc)rc=dp->dp_Type==ACTION_SET_DATE?
            oz_zpl_set_mtime(fs,info.object,seconds,nanos,cr):oz_zpl_protection_store(fs,info.object,bits,cr);
        if(!rc)rc=oz_zpl_sync(fs);
        if(rc)error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);
        else value=DOSTRUE;
        if(!rc)watches_changed(mount,info.object,info.parent,0);
        break;
    }
    case ACTION_LOCATE_OBJECT: {
        if(dp->dp_Arg3!=SHARED_LOCK&&dp->dp_Arg3!=EXCLUSIVE_LOCK)
            {error=ERROR_BAD_NUMBER;break;}
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        char name[256];int rc=packet_name((BSTR)dp->dp_Arg2,name,sizeof(name));
        if(rc){error=dos_error(rc);break;}
        char *colon=strchr(name,':');const char *base=colon?colon+1:name;
        uint64_t parent=(!colon&&lock)?lock->object:oz_zpl_root(fs);
        struct oz_zpl_info info;rc=resolve_path(mount,parent,base,&info,cr);
        if(!rc&&info.symlink)rc=OZ_ZPL_SOFTLINK;
        if(!rc&&exclusive_object(mount,info.object))rc=EBUSY;
        if(!rc&&dp->dp_Arg3==EXCLUSIVE_LOCK&&referenced_object(mount,info.object))rc=EBUSY;
        char leaf[256];uint64_t actual_parent=0;
        if(!rc&&!colon&&lock&&!*base) {
            actual_parent=lock->parent;snprintf(leaf,sizeof(leaf),"%s",lock->name);
        } else if(!rc)rc=path_location(mount,parent,base,&info,cr,&actual_parent,leaf);
        if(rc){error=dos_error(rc);break;}
        value=make_lock(mount,info.object,actual_parent,leaf,port,volume);
        if(!value)error=ERROR_NO_FREE_STORE;
        else ((struct FileLock *)BADDR(value))->fl_Access=dp->dp_Arg3;
        break;
    }
    case ACTION_CHANGE_MODE: {
        struct locked *targetlock=NULL;struct handle *targethandle=NULL;
        /* Public ChangeMode takes ULONG: negative lock constants can arrive
         * zero-extended on a 64-bit packet ABI. Accept either 32-bit form. */
        LONG mode=(LONG)dp->dp_Arg3;
        if((dp->dp_Arg3!=(SIPTR)mode&&dp->dp_Arg3!=(SIPTR)(ULONG)mode)||
            (mode!=SHARED_LOCK&&mode!=EXCLUSIVE_LOCK))
            {error=ERROR_BAD_NUMBER;break;}
        if(dp->dp_Arg1==CHANGE_LOCK)targetlock=valid_lock(mount,dp->dp_Arg2);
        else if(dp->dp_Arg1==CHANGE_FH) {
            /* Unlike most FH packets, ChangeMode passes the public BPTR. */
            struct FileHandle *fh=BADDR(dp->dp_Arg2);
            if(fh&&fh->fh_Type==port)targethandle=valid_handle(mount,fh->fh_Arg1);
        } else {error=ERROR_BAD_NUMBER;break;}
        if(!targetlock&&!targethandle){error=ERROR_INVALID_LOCK;break;}
        uint64_t object=targetlock?targetlock->object:targethandle->object;
        int rc=access_object(mount,object,targetlock?ACE_READ_ATTRIBUTES:ACE_READ_DATA,cr);
        if(targethandle&&(rc==EACCES||rc==EPERM)&&!oz_zpl_readonly(fs))
            rc=access_object(mount,object,ACE_WRITE_DATA,cr);
        if(rc){error=dos_error(rc);break;}
        if(mode==EXCLUSIVE_LOCK) {
            for(struct handle *entry=handles;entry;entry=entry->next)if(entry->used&&entry!=targethandle&&
                entry->object==object)error=ERROR_OBJECT_IN_USE;
            for(struct locked *entry=locks;entry;entry=entry->next)if(entry->used&&entry!=targetlock&&
                entry->object==object)error=ERROR_OBJECT_IN_USE;
        }
        if(error)break;
        if(targetlock)targetlock->lock.fl_Access=mode;
        else targethandle->exclusive=mode==EXCLUSIVE_LOCK;
        value=DOSTRUE;break;
    }
    case ACTION_FREE_LOCK:
        if(!dp->dp_Arg1)value=DOSTRUE;
        else if(!lock)error=ERROR_INVALID_LOCK;
        else {lock->used=0;value=DOSTRUE;}break;
    case ACTION_COPY_DIR:
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        if(exclusive_object(mount,lock?lock->object:oz_zpl_root(fs)))
            {error=ERROR_OBJECT_IN_USE;break;}
        if(access_object(mount,lock?lock->object:oz_zpl_root(fs),ACE_READ_ATTRIBUTES,cr))
            {error=ERROR_READ_PROTECTED;break;}
        value=make_lock(mount,lock?lock->object:oz_zpl_root(fs),lock?lock->parent:oz_zpl_root(fs),lock?lock->name:mount->name,port,volume);
        if(!value)error=ERROR_NO_FREE_STORE;
        break;
    case ACTION_COPY_DIR_FH: {
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if(exclusive_object(mount,h->object)){error=ERROR_OBJECT_IN_USE;break;}
        int rc=access_object(mount,h->object,ACE_READ_ATTRIBUTES,cr);
        if(rc){error=dos_error(rc);break;}
        value=make_lock(mount,h->object,h->parent,h->name,port,volume);
        if(!value)error=ERROR_NO_FREE_STORE;
        break;
    }
    case ACTION_PARENT_FH:
    case ACTION_PARENT: {
        int fromfh=dp->dp_Type==ACTION_PARENT_FH;
        if(fromfh?!h:!lock){error=ERROR_INVALID_LOCK;break;}
        uint64_t object=fromfh?h->object:lock->object;
        if(object==oz_zpl_root(fs))break;
        uint64_t parent=fromfh?h->parent:lock->parent;
        if(!identity_current(mount,parent,fromfh?h->parent_generation:lock->parent_generation,1))
            {error=ERROR_INVALID_LOCK;break;}
        struct oz_zpl_info info;int rc=oz_zpl_stat(fs,parent,&info);
        if(!rc)rc=access_object(mount,object,ACE_READ_ATTRIBUTES,cr);
        if(!rc)rc=access_object(mount,parent,ACE_READ_ATTRIBUTES|ACE_EXECUTE,cr);
        if(!rc&&exclusive_object(mount,parent))rc=EBUSY;
        if(rc){error=dos_error(rc);break;}
        char leaf[256];rc=oz_zpl_name(fs,parent,leaf,sizeof(leaf));
        if(rc){error=dos_error(rc);break;}
        value=make_lock(mount,parent,info.parent,*leaf?leaf:mount->name,port,volume);
        if(!value)error=ERROR_NO_FREE_STORE;
        break;
    }
    case ACTION_SAME_LOCK: {
        struct locked *other=valid_lock(mount,dp->dp_Arg2);
        if(!lock||!other)error=ERROR_INVALID_LOCK;
        /* The packet returns a boolean, unlike public SameLock's
         * three-way LOCK_* result (LOCK_SAME itself is zero). */
        else if(access_object(mount,lock->object,ACE_READ_ATTRIBUTES,cr)||
            access_object(mount,other->object,ACE_READ_ATTRIBUTES,cr))error=ERROR_READ_PROTECTED;
        else value=lock->object==other->object?DOSTRUE:DOSFALSE;
        break;
    }
    case ACTION_EXAMINE_OBJECT64:
    case ACTION_EXAMINE_OBJECT: {
        if(dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        struct FileInfoBlock *fib=BADDR(dp->dp_Arg2);
        if(!fib){error=ERROR_BAD_NUMBER;break;}
        int rc=access_object(mount,lock?lock->object:oz_zpl_root(fs),ACE_READ_ATTRIBUTES,cr);
        if(!rc)rc=dp->dp_Type==ACTION_EXAMINE_OBJECT64?
            fill_fib64(fs,lock?lock->object:oz_zpl_root(fs),lock?lock->name:mount->name,(void *)fib):
            fill_fib(fs,lock?lock->object:oz_zpl_root(fs),lock?lock->name:mount->name,fib);
        if(rc)error=dos_error(rc);else value=DOSTRUE;break;
    }
    case ACTION_EXAMINE_FH64:
    case ACTION_EXAMINE_FH: {
        if(!h){error=ERROR_INVALID_LOCK;break;}
        struct FileInfoBlock *fib=BADDR(dp->dp_Arg2);
        if(!fib){error=ERROR_BAD_NUMBER;break;}
        int rc=access_object(mount,h->object,ACE_READ_ATTRIBUTES,cr);
        if(!rc)rc=dp->dp_Type==ACTION_EXAMINE_FH64?
            fill_fib64(fs,h->object,h->name,(void *)fib):fill_fib(fs,h->object,h->name,fib);
        if(rc)error=dos_error(rc);else value=DOSTRUE;break;
    }
    case ACTION_EXAMINE_NEXT64:
    case ACTION_EXAMINE_NEXT: {
        if(!lock){error=ERROR_INVALID_LOCK;break;}
        struct FileInfoBlock *fib=BADDR(dp->dp_Arg2);char name[256];struct oz_zpl_info info;
        if(!fib){error=ERROR_BAD_NUMBER;break;}
        IPTR key=dp->dp_Type==ACTION_EXAMINE_NEXT64?
            ((struct FileInfoBlock64 *)fib)->fib_DiskKey:fib->fib_DiskKey;
        if(key>=INT32_MAX){error=ERROR_BAD_NUMBER;break;}
        LONG next=key+1;
        int rc=access_object(mount,lock->object,ACE_LIST_DIRECTORY|ACE_EXECUTE,cr);
        if(!rc)rc=oz_zpl_nth(fs,lock->object,key,name,sizeof(name),&info);
        if(rc){error=rc==ENOENT?ERROR_NO_MORE_ENTRIES:dos_error(rc);break;}
        rc=access_object(mount,info.object,ACE_READ_ATTRIBUTES,cr);
        if(!rc)rc=dp->dp_Type==ACTION_EXAMINE_NEXT64?
            fill_fib64(fs,info.object,name,(void *)fib):fill_fib(fs,info.object,name,fib);
        if(rc)error=dos_error(rc);else {
            if(dp->dp_Type==ACTION_EXAMINE_NEXT64)((struct FileInfoBlock64 *)fib)->fib_DiskKey=next;
            else fib->fib_DiskKey=next;
            value=DOSTRUE;
        }break;
    }
    case ACTION_FH_FROM_LOCK: {
        struct FileHandle *fh=BADDR(dp->dp_Arg1);
        struct locked *source=valid_lock(mount,dp->dp_Arg2);
        if(!fh||!source){error=ERROR_INVALID_LOCK;break;}
        struct oz_zpl_info info;int rc=oz_zpl_stat(fs,source->object,&info);
        if(!rc&&info.directory)rc=EISDIR;
        if(!rc&&info.special)rc=ENOTSUP;
        if(!rc)rc=access_object(mount,source->object,ACE_READ_DATA,cr);
        struct handle *entry=rc?NULL:reserve_handle(mount);
        if(!rc&&!entry)rc=ENOMEM;
        if(rc){error=dos_error(rc);break;}
        entry->object=source->object;entry->size=info.size;
        entry->generation=info.generation;entry->kind=object_kind(&info);entry->stale=0;
        entry->parent=source->parent;snprintf(entry->name,sizeof(entry->name),"%s",source->name);
        entry->parent_generation=source->parent_generation;
        entry->position=0;entry->used=1;entry->changed=0;
        entry->exclusive=source->lock.fl_Access==EXCLUSIVE_LOCK;
        /* fh_Port is the legacy alias of fh_Interactive, not routing data.
         * DOS supplies fh_Type; regular files must never advertise a TTY. */
        fh->fh_Arg1=(IPTR)entry;fh->fh_Interactive=DOSFALSE;
        /* Consume only after every fallible step has succeeded. */
        source->used=0;value=DOSTRUE;break;
    }
    case ACTION_FINDOUTPUT:
    case ACTION_FINDUPDATE:
    case ACTION_FINDINPUT: {
        int output=dp->dp_Type==ACTION_FINDOUTPUT;
        int update=dp->dp_Type!=ACTION_FINDINPUT;
        if(update&&oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        struct FileHandle *fh=BADDR(dp->dp_Arg1);
        if(!fh){error=ERROR_INVALID_LOCK;break;}
        BSTR b=(BSTR)dp->dp_Arg3;char name[256];
        int name_error=packet_name(b,name,sizeof(name));
        struct locked *parent=valid_lock(mount,dp->dp_Arg2);
        if(dp->dp_Arg2&&!parent){error=ERROR_INVALID_LOCK;break;}
        if(name_error){error=dos_error(name_error);break;}
        char *colon=strchr(name,':');char *base=colon?colon+1:name;
        struct handle *entry=reserve_handle(mount);
        if(!entry){error=ERROR_NO_FREE_STORE;break;}
        struct oz_zpl_info info;
        uint32_t open_right=update?ACE_WRITE_DATA:ACE_READ_DATA;
        uint64_t start=(!colon&&parent)?parent->object:oz_zpl_root(fs);
        /* AFS admits FINDUPDATE using WRITE, not READ|WRITE. A later Read
         * still checks the current sender independently. */
        int rc=output?0:mount->authenticated?
            oz_zpl_resolve_for(fs,start,base,&info,cr,path_policy,mount,open_right):
            oz_zpl_resolve(fs,start,base,&info);
        if(!rc&&!output&&info.symlink)rc=OZ_ZPL_SOFTLINK;
        int created=0;uint64_t actual_parent=0;char actual_name[256];
        if(!rc&&!output) {
            if(!colon&&parent&&!*base) {
                actual_parent=parent->parent;snprintf(actual_name,sizeof(actual_name),"%s",parent->name);
            } else rc=path_location(mount,start,base,&info,cr,&actual_parent,actual_name);
        }
        if(output||(rc==ENOENT&&update)) {
            char leaf[256];char *last=strrchr(base,'/');
            snprintf(leaf,sizeof(leaf),"%s",last?last+1:base);
            if(last)last[1]=0;
            struct oz_zpl_info directory;
            rc=oz_zpl_resolve_for(fs,(!colon&&parent)?parent->object:oz_zpl_root(fs),
                last?base:"",&directory,cr,path_policy,mount,ACE_EXECUTE|ACE_ADD_FILE);
            uint32_t defaults=0;
            if(!rc)rc=oz_packet_security_defaults(security,&defaults);
            int replaced=0;
            if(!rc&&output) {
                rc=oz_zpl_lookup_at(fs,directory.object,leaf,&info);
                if(rc==ENOENT)rc=0;
                else if(!rc) {
                    if(info.symlink)rc=OZ_ZPL_SOFTLINK;
                    if(info.directory)rc=EISDIR;
                    if(info.special)rc=ENOTSUP;
                    for(struct handle *open=handles;open&&!rc;open=open->next)if(open->used&&open->object==info.object)rc=EBUSY;
                    for(struct locked *entry=locks;entry&&!rc;entry=entry->next)if(entry->used&&entry->object==info.object)rc=EBUSY;
                    if(!rc)rc=native_access_object(mount,info.object,secAt_Write|secAt_Delete);
                    if(!rc)rc=replace_output(mount,directory.object,leaf,defaults,cr,&info);
                    replaced=!rc;
                }
            }
            if(!rc&&!replaced)rc=oz_zpl_create_as(fs,directory.object,leaf,defaults,cr,&info);
            if(!rc){actual_parent=directory.object;snprintf(actual_name,sizeof(actual_name),"%s",leaf);}
            created=!rc;
        }
        if(!rc&&info.directory)rc=EISDIR;
        if(!rc&&info.special)rc=ENOTSUP;
        if(!rc&&!created&&exclusive_object(mount,info.object))rc=EBUSY;
        if(!rc&&!created)rc=access_object(mount,info.object,open_right,cr);
        struct oz_zpl_info parent_info;
        if(!rc)rc=oz_zpl_stat(fs,actual_parent,&parent_info);
        if(!rc&&!parent_info.directory)rc=ENOTDIR;
        if(rc){error=update&&(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);break;}
        entry->object=info.object;entry->size=info.size;
        entry->generation=info.generation;entry->kind=object_kind(&info);entry->stale=0;
        entry->parent=actual_parent;snprintf(entry->name,sizeof(entry->name),"%s",actual_name);
        entry->parent_generation=parent_info.generation;
        entry->position=0;entry->used=1;entry->exclusive=output;entry->changed=created;
        fh->fh_Arg1=(IPTR)entry;fh->fh_Interactive=DOSFALSE;
        if(created)watches_changed(mount,info.object,actual_parent,0);
        value=DOSTRUE;break;
    }
    case ACTION_READ: {
        value=-1;
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if(dp->dp_Arg3<0||(!dp->dp_Arg2&&dp->dp_Arg3)){error=ERROR_BAD_NUMBER;break;}
        uint64_t size=dp->dp_Arg3;
        int rc=access_object(mount,h->object,ACE_READ_DATA,cr);
        struct oz_zpl_info info;
        if(!rc)rc=oz_zpl_stat(fs,h->object,&info);
        if(!rc)h->size=info.size;
        uint64_t available=h->position<h->size?h->size-h->position:0;
        if(size>available)size=available;
        if(!rc&&size)rc=oz_zpl_read(fs,h->object,h->position,(void *)dp->dp_Arg2,size);
        if(rc){error=dos_error(rc);break;}
        h->position+=size;value=size;break;
    }
#if UINTPTR_MAX > UINT32_MAX
    /* Native AROS64 uses ordinary SIPTR packet fields (PFS3 contract), not
     * the AmigaOS4 32-bit DosPacket64 overlay. */
    case ACTION_GET_FILE_POSITION64:
    case ACTION_GET_FILE_SIZE64:
    case ACTION_CHANGE_FILE_POSITION64:
#endif
    case ACTION_SEEK: {
        int legacy=dp->dp_Type==ACTION_SEEK;
        int query=dp->dp_Type==ACTION_GET_FILE_POSITION64||dp->dp_Type==ACTION_GET_FILE_SIZE64;
        value=legacy||query?-1:DOSFALSE;if(!h){error=ERROR_INVALID_LOCK;break;}
        int access=access_object(mount,h->object,ACE_READ_DATA,cr);
        if((access==EACCES||access==EPERM)&&!oz_zpl_readonly(fs))
            access=access_object(mount,h->object,ACE_WRITE_DATA,cr);
        if(access){error=dos_error(access);break;}
        struct oz_zpl_info info;int rc=oz_zpl_stat(fs,h->object,&info);
        if(rc){error=dos_error(rc);break;}
        h->size=info.size;
        if(query) {
            uint64_t answer=dp->dp_Type==ACTION_GET_FILE_SIZE64?h->size:h->position;
            if(answer>INT64_MAX)error=ERROR_OBJECT_TOO_LARGE;else value=answer;
            break;
        }
        uint64_t base;
        if(dp->dp_Arg3==OFFSET_BEGINNING)base=0;
        else if(dp->dp_Arg3==OFFSET_CURRENT)base=h->position;
        else if(dp->dp_Arg3==OFFSET_END)base=h->size;
        else {error=ERROR_SEEK_ERROR;break;}
        int64_t delta=legacy?(int32_t)dp->dp_Arg2:(int64_t)dp->dp_Arg2;
        uint64_t position;
        if(relative_offset(base,delta,&position)||position>h->size){error=ERROR_SEEK_ERROR;break;}
        /* A classic LONG result must never silently wrap or move on error. */
        if(legacy&&(h->position>INT32_MAX||position>INT32_MAX))
            {error=ERROR_OBJECT_TOO_LARGE;break;}
        value=legacy?(SIPTR)h->position:DOSTRUE;h->position=position;break;
    }
    case ACTION_END:
        if(!h)error=ERROR_INVALID_LOCK;
        else {
            if(h->changed&&handle_current(mount,h))watches_changed(mount,h->object,h->parent,0);
            record_close(mount,h);h->used=0;value=DOSTRUE;
        }
        break;
    case ACTION_WRITE: {
        value=-1;
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        if(dp->dp_Arg3<0||(!dp->dp_Arg2&&dp->dp_Arg3)){error=ERROR_BAD_NUMBER;break;}
        int rc=access_object(mount,h->object,ACE_WRITE_DATA,cr);
        size_t written=0;
        if(!rc)rc=oz_zpl_write_as(fs,h->object,h->position,(const void *)dp->dp_Arg2,
            dp->dp_Arg3,&written,cr);
        h->position+=written;
        if(h->position>h->size)h->size=h->position;
        if(written){h->changed=1;watches_changed(mount,h->object,h->parent,0);}
        if(rc)error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);
        if(!rc||(written&&written<(size_t)dp->dp_Arg3))value=written;
        break;
    }
#if UINTPTR_MAX > UINT32_MAX
    case ACTION_CHANGE_FILE_SIZE64:
#endif
    case ACTION_SET_FILE_SIZE: {
        int legacy=dp->dp_Type==ACTION_SET_FILE_SIZE;
        value=legacy?-1:DOSFALSE;
        if(!h){error=ERROR_INVALID_LOCK;break;}
        if(oz_zpl_readonly(fs)){error=ERROR_DISK_WRITE_PROTECTED;break;}
        int rc=access_object(mount,h->object,ACE_WRITE_DATA,cr);
        struct oz_zpl_info info;
        if(!rc)rc=oz_zpl_stat(fs,h->object,&info);
        if(rc){error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);break;}
        uint64_t base;
        if(dp->dp_Arg3==OFFSET_BEGINNING)base=0;
        else if(dp->dp_Arg3==OFFSET_CURRENT)base=h->position;
        else if(dp->dp_Arg3==OFFSET_END)base=info.size;
        else {error=ERROR_SEEK_ERROR;break;}
        int64_t delta=legacy?(int32_t)dp->dp_Arg2:(int64_t)dp->dp_Arg2;
        uint64_t size;
        rc=relative_offset(base,delta,&size);
        if(rc){error=rc==EOVERFLOW?ERROR_OBJECT_TOO_LARGE:ERROR_SEEK_ERROR;break;}
        if(legacy&&size>INT32_MAX){error=ERROR_OBJECT_TOO_LARGE;break;}
        /* Match AROS AFS: do not truncate behind another open handle. */
        for(struct handle *entry=handles;entry;entry=entry->next)if(entry->used&&entry!=h&&
            entry->object==h->object&&entry->position>size)error=ERROR_SEEK_ERROR;
        if(error)break;
        rc=oz_zpl_resize_as(fs,h->object,size,cr);
        if(rc){error=(rc==EACCES||rc==EPERM)?ERROR_WRITE_PROTECTED:dos_error(rc);break;}
        for(struct handle *entry=handles;entry;entry=entry->next)if(entry->used&&entry->object==h->object)
            entry->size=size;
        if(h->position>size)h->position=size;
        h->changed=1;watches_changed(mount,h->object,h->parent,0);
        value=legacy?(SIPTR)size:DOSTRUE;break;
    }
    case ACTION_INFO:case ACTION_DISK_INFO: {
        if(dp->dp_Type==ACTION_INFO&&dp->dp_Arg1&&!lock){error=ERROR_INVALID_LOCK;break;}
        SIPTR info_pointer=dp->dp_Type==ACTION_INFO?dp->dp_Arg2:dp->dp_Arg1;
        struct InfoData *info=BADDR(info_pointer);
        if(!info){error=ERROR_BAD_NUMBER;break;}
        uint64_t used,available;oz_zpl_space(fs,&used,&available);
        if(available>UINT64_MAX-used){error=ERROR_BAD_NUMBER;break;}
        uint64_t total=used+available,block=512;
#if !(__DOS64)
        while(total/block>=INT32_MAX&&block<(1UL<<30))block*=2;
        if(total/block>=INT32_MAX){error=ERROR_BAD_NUMBER;break;}
#endif
        memset(info,0,sizeof(*info));info->id_DiskState=oz_zpl_readonly(fs)?ID_WRITE_PROTECTED:ID_VALIDATED;
        /* Filesystem logical capacity, not the sum of mirror leaf capacities. */
        info->id_NumBlocks=total/block;info->id_NumBlocksUsed=used/block+(used%block!=0);
        if(info->id_NumBlocksUsed>info->id_NumBlocks)info->id_NumBlocks=info->id_NumBlocksUsed;
        info->id_BytesPerBlock=block;info->id_DiskType=0x5a465301;
        info->id_VolumeNode=MKBADDR(volume);
        for(struct handle *entry=handles;entry;entry=entry->next)if(entry->used)info->id_InUse=DOSTRUE;
        for(struct locked *entry=locks;entry;entry=entry->next)if(entry->used)info->id_InUse=DOSTRUE;
        value=DOSTRUE;break;
    }
    case ACTION_CURRENT_VOLUME:value=(SIPTR)MKBADDR(volume);break;
    case ACTION_IS_FILESYSTEM:value=DOSTRUE;break;
    /* MuFS author API: these are capability queries, not an access grant or
     * proof that security.library has discovered this dynamic volume. */
    case ACTION_IS_SECFS:value=mount->authenticated?DOSTRUE:DOSFALSE;break;
    case ACTION_GET_SECFS_VERSION:value=secFS_API_VERSION;break;
    case ACTION_FLUSH: {
        int rc=oz_zpl_sync(fs);
        if(rc)error=dos_error(rc);else value=DOSTRUE;
        break;
    }
    default:error=ERROR_ACTION_NOT_KNOWN;break;
    }
    mount->security=NULL;
    oz_packet_security_end(security);
    if(!deferred)ReplyPkt(dp,value,error);
    records_pump(mount);
    watches_pump(mount);
    }
    retired_resources_release(mount);
}
struct oz_dos_stream {
    struct oz_mount *mount;
    struct handle *handle;
    const struct oz_packet_security *security;
    int output;
    uint64_t stage,parent,stage_generation,parent_generation;
    uint32_t protection;
    char stage_name[64],leaf[256];
};
static int stream_path_policy(void *context,uint64_t object,uint32_t rights,cred_t *cr)
{
    struct oz_dos_stream *stream=context;
    return access_object_for(stream->mount,object,rights,cr,stream->security);
}
static int stream_open_mode(struct oz_mount *mount,const struct oz_packet_security *security,
    const char *path,struct oz_dos_stream **out,int output)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!mount||!mount->fs)return ENODEV;
    if(!security)return EACCES;
    if(mount->port->mp_SigTask!=FindTask(NULL))return EINVAL;
    if(!path||strlen(path)>=4096)return EINVAL;
    const char *colon=strchr(path,':');
    if(!colon||colon==path||(size_t)(colon-path)>31)return EINVAL;
    char volume[32];memcpy(volume,path,(size_t)(colon-path));volume[colon-path]=0;
    /* Honor the live namespace, including an assign shadowing this name. */
    struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
    if(!list)return EIO;
    struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)volume,LDF_ALL);
    int ours=entry&&(entry==mount->device||entry==mount->volume);
    UnLockDosList(LDF_ALL|LDF_READ);
    if(!ours)return ENODEV;
    if(!mount->authenticated)return EACCES;
    if(output&&oz_zpl_readonly(mount->fs))return EROFS;
    struct oz_dos_stream *stream=calloc(1,sizeof(*stream));if(!stream)return ENOMEM;
    stream->mount=mount;stream->security=security;stream->output=output;
    cred_t *cr=oz_packet_security_cred(security);
    struct oz_zpl_info info;
    int error=oz_zpl_resolve_for(mount->fs,oz_zpl_root(mount->fs),colon+1,&info,
        cr,stream_path_policy,stream,output?ACE_WRITE_DATA:ACE_READ_DATA);
    if(error==OZ_ZPL_SOFTLINK)error=ELOOP;
    if(!error&&info.symlink)error=ELOOP;
    if(!error&&info.directory)error=EISDIR;
    if(!error&&info.special)error=ENOTSUP;
    if(!error&&output&&info.size)error=EEXIST;
    struct oz_zpl_info parent_info;
    if(!error)error=oz_zpl_stat(mount->fs,info.parent,&parent_info);
    if(!error&&!parent_info.directory)error=ENOTDIR;
    /* Preserve an object-stable input, and make lifecycle/other DOS users
     * observe the stream as an exclusive live handle until close. */
    for(struct handle *h=mount->handles;!error&&h;h=h->next)
        if(h->used&&h->object==info.object)error=EBUSY;
    for(struct locked *l=mount->locks;!error&&l;l=l->next)
        if(l->used&&l->object==info.object)error=EBUSY;
    if(!error){stream->handle=reserve_handle(mount);if(!stream->handle)error=ENOMEM;}
    if(error){free(stream);return error;}
    struct handle *h=stream->handle;
    h->object=info.object;h->parent=info.parent;h->size=info.size;h->position=0;
    h->generation=info.generation;h->kind=object_kind(&info);h->stale=0;
    h->parent_generation=parent_info.generation;
    h->used=1;h->exclusive=1;h->changed=0;
    snprintf(h->name,sizeof(h->name),"%s",strrchr(colon+1,'/')?strrchr(colon+1,'/')+1:colon+1);
    *out=stream;return 0;
}
int oz_dos_stream_open(struct oz_mount *mount,const struct oz_packet_security *security,
    const char *path,struct oz_dos_stream **out)
{return stream_open_mode(mount,security,path,out,0);}
static int stream_stage_begin(struct oz_mount *mount,const struct oz_packet_security *security,
    const char *path,struct oz_dos_stream **out)
{
    /* Called only after stream_open_mode identifies our volume and returns
     * ENOENT. All direct operations stay on that mount's owner task. */
    const char *relative=strchr(path,':')+1,*slash=strrchr(relative,'/');
    const char *leaf=slash?slash+1:relative;
    if(!*leaf||strlen(leaf)>=256||!strcmp(leaf,".")||!strcmp(leaf,"..")||strchr(leaf,':'))return EINVAL;
    char parent_path[4096];size_t n=slash?(size_t)(slash-relative)+1:0;
    memcpy(parent_path,relative,n);parent_path[n]=0;
    struct oz_dos_stream *stream=calloc(1,sizeof(*stream));if(!stream)return ENOMEM;
    stream->mount=mount;stream->security=security;stream->output=1;
    cred_t *cr=oz_packet_security_cred(security);struct oz_zpl_info parent,stage,payload;
    int error=oz_zpl_resolve_for(mount->fs,oz_zpl_root(mount->fs),parent_path,&parent,
        cr,stream_path_policy,stream,ACE_EXECUTE|ACE_ADD_FILE|ACE_ADD_SUBDIRECTORY);
    if(error==OZ_ZPL_SOFTLINK)error=ELOOP;
    if(!error&&!parent.directory)error=ENOTDIR;
    if(!error&&exclusive_object(mount,parent.object))error=EBUSY;
    if(!error)error=oz_packet_security_defaults(security,&stream->protection);
    if(!error){stream->handle=reserve_handle(mount);if(!stream->handle)error=ENOMEM;}
    if(error){free(stream);return error;}
    stream->parent=parent.object;snprintf(stream->leaf,sizeof(stream->leaf),"%s",leaf);
    stream->parent_generation=parent.generation;
    static unsigned serial;
    for(unsigned attempt=0;attempt<64;attempt++){
        snprintf(stream->stage_name,sizeof(stream->stage_name),".ozsend-%08lx-%08x",
            (unsigned long)((uintptr_t)mount&0xffffffffu),++serial);
        /* Original caller owns the private directory, not the daemon. Native
         * protection zero denies group/other traversal; upstream ACLs apply
         * independently to every creation/write/publication operation. */
        error=oz_zpl_mkdir_as(mount->fs,parent.object,stream->stage_name,0,cr,&stage);
        if(error!=EEXIST)break;
    }
    if(error){free(stream);return error;}
    stream->stage=stage.object;
    stream->stage_generation=stage.generation;
    error=oz_zpl_create_as(mount->fs,stage.object,"stream",0,cr,&payload);
    if(error){
        int cleanup=oz_zpl_remove_as(mount->fs,parent.object,stream->stage_name,1,cr);
        free(stream);return cleanup?cleanup:error;
    }
    struct handle *h=stream->handle;
    h->object=payload.object;h->parent=stage.object;h->size=0;h->position=0;
    h->generation=payload.generation;h->kind=object_kind(&payload);h->stale=0;
    h->parent_generation=stage.generation;
    h->used=1;h->exclusive=1;h->changed=0;snprintf(h->name,sizeof(h->name),"stream");
    *out=stream;return 0;
}
int oz_dos_stream_open_output(struct oz_mount *mount,const struct oz_packet_security *security,
    const char *path,struct oz_dos_stream **out)
{
    int error=stream_open_mode(mount,security,path,out,1);
    return error==ENOENT?stream_stage_begin(mount,security,path,out):error;
}
int oz_dos_stream_read(struct oz_dos_stream *stream,void *bytes,size_t length,size_t *actual)
{
    if(!actual)return EINVAL;
    *actual=0;
    if(!stream||(!bytes&&length)||length>INT32_MAX)return EINVAL;
    if(stream->output)return EBADF;
    struct oz_mount *m=stream->mount;struct handle *h=stream->handle;
    if(m->port->mp_SigTask!=FindTask(NULL)||!m->fs||!h->used)return EINVAL;
    if(!handle_current(m,h))return ESTALE;
    cred_t *cr=oz_packet_security_cred(stream->security);
    int error=access_object_for(m,h->object,ACE_READ_DATA,cr,stream->security);
    struct oz_zpl_info info;
    if(!error)error=oz_zpl_stat(m->fs,h->object,&info);
    if(error)return error;
    uint64_t available=h->position<info.size?info.size-h->position:0;
    if(length>available)length=(size_t)available;
    if(length)error=oz_zpl_read_as(m->fs,h->object,h->position,bytes,length,cr);
    if(!error){h->position+=length;*actual=length;}
    return error;
}
int oz_dos_stream_write(void *context,const void *bytes,size_t length)
{
    struct oz_dos_stream *stream=context;
    if(!stream||(!bytes&&length))return EINVAL;
    if(!stream->output)return EBADF;
    struct oz_mount *m=stream->mount;struct handle *h=stream->handle;
    if(m->port->mp_SigTask!=FindTask(NULL)||!m->fs||!h->used)return EINVAL;
    if(oz_zpl_readonly(m->fs))return EROFS;
    if(!handle_current(m,h))return ESTALE;
    cred_t *cr=oz_packet_security_cred(stream->security);
    const char *next=bytes;
    while(length){
        int error=access_object_for(m,h->object,ACE_WRITE_DATA,cr,stream->security);
        if(error)return error;
        size_t count=0,chunk=length>INT32_MAX?INT32_MAX:length;
        error=oz_zpl_write_as(m->fs,h->object,h->position,next,chunk,&count,cr);
        if(count>chunk)return EIO;
        h->position+=count;if(h->position>h->size)h->size=h->position;
        if(count)h->changed=1;
        if(error)return error;
        if(!count)return EIO;
        next+=count;length-=count;
    }
    return 0;
}
void oz_dos_stream_close(struct oz_dos_stream *stream)
{
    if(!stream)return;
    stream->handle->used=0;stream->handle->exclusive=0;free(stream);
}
int oz_dos_stream_finish(struct oz_dos_stream *stream,int commit)
{
    if(!stream)return EINVAL;
    struct oz_mount *m=stream->mount;struct handle *h=stream->handle;
    if(m->port->mp_SigTask!=FindTask(NULL)||!m->fs||!h->used)return EINVAL;
    /* Never publish/delete by stale object identity. Release the stream
     * reference, retaining any private staging directory for recovery. */
    if(!handle_current(m,h) || (stream->stage &&
        (!identity_current(m,stream->stage,stream->stage_generation,1) ||
         !identity_current(m,stream->parent,stream->parent_generation,1)))) {
        oz_dos_stream_close(stream);return ESTALE;
    }
    int error=stream->output&&h->changed?oz_zpl_sync(m->fs):0;
    if(stream->stage){
        cred_t *cr=oz_packet_security_cred(stream->security);int published=0;
        if(commit&&!error){
            error=access_object_for(m,stream->parent,ACE_EXECUTE|ACE_ADD_FILE,cr,stream->security);
            if(!error)error=oz_zpl_protection_store(m->fs,h->object,stream->protection,cr);
            if(!error){
                error=oz_zpl_rename_as(m->fs,stream->stage,"stream",stream->parent,stream->leaf,cr);
                if(!error)published=1;
                else {
                    /* Rename may have succeeded before its sync failed.
                     * Never delete or undo a published destination. */
                    struct oz_zpl_info final;
                    if(!oz_zpl_lookup_at(m->fs,stream->parent,stream->leaf,&final)&&final.object==h->object)published=1;
                }
            }
        }
        h->used=0;
        int cleanup=0;
        if(!published)cleanup=oz_zpl_remove_as(m->fs,stream->stage,"stream",0,cr);
        if(!cleanup)cleanup=oz_zpl_remove_as(m->fs,stream->parent,stream->stage_name,1,cr);
        if(!error)error=cleanup;
        if(published)watches_changed(m,h->object,stream->parent,0);
    }else if(h->changed)watches_changed(m,h->object,h->parent,0);
    oz_dos_stream_close(stream);return error;
}

static int detach_references_check(struct oz_mount *mount)
{
    if(!mount)return EINVAL;
    if(!mount->fs)return 0;
    if(mount->records||mount->record_waits) {
        fprintf(stderr,"DOS unmount %s busy: records=%d waiters=%d watches=%d notifications=%u\n",
            mount->name,mount->records!=NULL,mount->record_waits!=NULL,
            mount->watches!=NULL,mount->notify_outstanding);
        return EBUSY;
    }
    for(struct handle *h=mount->handles;h;h=h->next)if(h->used) {
        fprintf(stderr,"DOS unmount %s busy: open file object=%llu\n",mount->name,
            (unsigned long long)h->object);return EBUSY;
    }
    for(struct locked *l=mount->locks;l;l=l->next)if(l->used) {
        fprintf(stderr,"DOS unmount %s busy: lock object=%llu name=%s\n",mount->name,
            (unsigned long long)l->object,l->name);return EBUSY;
    }
    return 0;
}
int oz_dos_detach_check(struct oz_mount *mount)
{
    int error=detach_references_check(mount);
    if(error||!mount->fs)return error;
    error=oz_device_publication_begin();
    if(error)return error;
    if(mount->port->mp_MsgList.lh_Head->ln_Succ)error=EBUSY;
    oz_device_publication_end();
    return error;
}
int oz_dos_detach(struct oz_mount *mount)
{
    if(!mount)return EINVAL;
    if(!mount->fs)return 0;
    watches_pump(mount);
    int error=detach_references_check(mount);
    if(error)return error;
    /* A list reader may be waiting for a DOS packet from this very task.
     * Never wait here: return to the service loop so it can drain requests.
     * RemDosEntry nests this lock once we own it; no namespace is changed
     * when admission fails. */
    int publication=oz_device_publication_begin();
    if(publication)return publication;
    if(mount->port->mp_MsgList.lh_Head->ln_Succ) {
        oz_device_publication_end();
        fprintf(stderr,"DOS unmount %s busy: queued packets\n",mount->name);return EBUSY;
    }
    RemDosEntry(mount->device);RemDosEntry(mount->volume);
    oz_device_publication_end();
    /* Keep port AND detached entries alive for previously resolved DeviceProc
     * references. Pump late packets with an explicit unmounted error. */
    mount->fs=NULL;
    retired_resources_release(mount);
    volume_event(IECLASS_DISKREMOVED);
    return 0;
}
int oz_dos_unmount(struct oz_mount *mount)
{
    if(!mount)return EINVAL;
    watches_pump(mount);
    /* Destruction, unlike storage detach, cannot leave live subscriptions
     * or notification reply messages pointing at the reclaimed endpoint. */
    if(mount->watches||mount->notify_outstanding)return EBUSY;
    int rc=oz_dos_detach(mount);if(rc)return rc;
    /* Only bounded callers which have joined all clients may destroy this. */
    FreeDosEntry(mount->volume);
    if(mount->borrowed_device)mount->device->dol_Task=NULL;
    else FreeDosEntry(mount->device);
    while(mount->handles){struct handle *h=mount->handles;mount->handles=h->next;free(h);}
    while(mount->locks){struct locked *l=mount->locks;mount->locks=l->next;free(l);}
    endpoint_port_free(mount,mount->timer_port);
    endpoint_port_free(mount,mount->notify_port);
    endpoint_port_free(mount,mount->port);free(mount);return 0;
}
