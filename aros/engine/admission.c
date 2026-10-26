/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/task.h>
#include <resources/task.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>
#include <exec/resident.h>
#include "admission.h"
#include "packet-cred.h"
#include "../ui/support-api.h"
#include "../ui/partition-api.h"
#include "device-io.h"
#include "efi-native.h"
#include "../ui/gpt-api.h"
#include "../ui/zvol-api.h"
#include <zfs_prop.h>
#include <zfeature_common.h>
#include <sys/zfs_sysfs.h>
#include "../cli/session-cleanup.h"
#ifdef OZ_NATIVE_DISPATCHER
#include "control-file.h"
#include "ioctl-request.h"
#include "ioctl-copy.h"
#include "../ui/ioctl-api.h"
#include <sys/fs/zfs.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_native.h>
#include "zvol-native.h"
#include "../cli/stream-channel.h"
#include "../zpl/online-receive.h"
extern long zfsdev_ioctl_common(uint_t,zfs_cmd_t *,int);
#endif
#include <sys/dmu_objset.h>
#include <zfs_namecheck.h>
int oz_zpl_dataset_create(const char *, cred_t *);
int oz_zpl_dataset_create_options(const char *,const char *,cred_t *);
int oz_permission_list(struct oz_message *,cred_t *);
int oz_accounting_list(struct oz_message *,cred_t *);
int oz_user_property_change(struct oz_message *,cred_t *);
int oz_vdev_property_get(struct oz_message *,cred_t *);
int oz_permission_change(struct oz_message *,cred_t *);
static int (*lifecycle)(struct oz_message *,cred_t *,const struct oz_packet_security *);
static int (*handler_attach)(struct oz_handler_attach *,cred_t *);
void oz_admission_bind_handler(int (*handler)(struct oz_handler_attach *,cred_t *))
{handler_attach=handler;}
static int (*replication)(struct oz_message *,const struct oz_packet_security *);
static int (*key_load)(struct oz_message *,const struct oz_packet_security *);
static int (*dataset_create)(struct oz_message *,const struct oz_packet_security *);
void oz_admission_bind_create(int (*handler)(struct oz_message *,const struct oz_packet_security *))
{dataset_create=handler;}
static int (*mount_snapshot)(struct oz_mount_record *,ULONG,ULONG *);
static int (*mount_policy)(const char *,ULONG *,ULONG *);
static int (*mount_shares)(const char *,UQUAD *);
void oz_admission_bind_mount_shares(int (*handler)(const char *,UQUAD *))
{mount_shares=handler;}
void oz_admission_bind_mount_policy(int (*handler)(const char *,ULONG *,ULONG *))
{mount_policy=handler;}
void oz_admission_bind_mounts(int (*handler)(struct oz_mount_record *,ULONG,ULONG *))
{mount_snapshot=handler;}
void oz_admission_bind_key_load(int (*handler)(struct oz_message *,const struct oz_packet_security *))
{key_load=handler;}
void oz_admission_bind_replication(int (*handler)(struct oz_message *,const struct oz_packet_security *))
{replication=handler;}
void oz_admission_bind_lifecycle(int (*handler)(struct oz_message *,cred_t *,const struct oz_packet_security *))
{ lifecycle=handler; }

struct recursive_snapshot_create_context {
    const char *snapshot;
    nvlist_t *snapshots;
    unsigned count;
};
static int recursive_snapshot_create_collect(const char *dataset,void *opaque)
{
    struct recursive_snapshot_create_context *context=opaque;
    if(strchr(dataset,'@'))return 0;
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(snprintf(name,sizeof(name),"%s@%s",dataset,context->snapshot)>=sizeof(name))return ENAMETOOLONG;
    fnvlist_add_boolean(context->snapshots,name);context->count++;
    return 0;
}
static int recursive_snapshot_create(const char *filesystem,const char *snapshot)
{
#ifdef OZ_NATIVE_DISPATCHER
    struct recursive_snapshot_create_context context={.snapshot=snapshot,.snapshots=fnvlist_alloc()};
    int error=dmu_objset_find(filesystem,recursive_snapshot_create_collect,&context,DS_FIND_CHILDREN);
    if(!error&&!context.count)error=ENOENT;
    nvlist_t *input=NULL;char *packed=NULL,*output=NULL;zfs_cmd_t *zc=NULL;
    struct oz_ioctl_copy_scope *copy=NULL;
    if(!error) {
        input=fnvlist_alloc();fnvlist_add_nvlist(input,"snaps",context.snapshots);
        size_t packed_size=0;error=nvlist_pack(input,&packed,&packed_size,NV_ENCODE_NATIVE,0);
        size_t output_size=1024*1024;
        zc=calloc(1,sizeof(*zc));output=calloc(1,output_size);
        if(!zc||!output)error=ENOMEM;
        if(!error) {
            size_t pool_length=strcspn(filesystem,"/");
            if(pool_length>=sizeof(zc->zc_name))error=ENAMETOOLONG;
            else {
                snprintf(zc->zc_name,sizeof(zc->zc_name),"%.*s",(int)pool_length,filesystem);
                zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=packed_size;
                zc->zc_nvlist_dst=(uintptr_t)output;zc->zc_nvlist_dst_size=output_size;
                struct oz_ioctl_range ranges[]={{packed,packed_size,OZ_IOCTL_INPUT},
                    {output,output_size,OZ_IOCTL_OUTPUT}};
                error=oz_ioctl_copy_enter(ranges,2,&copy);
                if(!error)error=zfsdev_ioctl_common(ZFS_IOC_SNAPSHOT-ZFS_IOC_FIRST,zc,FKIOCTL);
            }
        }
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    free(output);free(zc);free(packed);if(input)fnvlist_free(input);
    fnvlist_free(context.snapshots);
    return error;
#else
    (void)filesystem;(void)snapshot;
    return ENOTSUP;
#endif
}
static int snapshot_create(const char *target,cred_t *caller,int recursive)
{
    if(!caller)return EACCES;
    if(dataset_namecheck(target,NULL,NULL))return EINVAL;
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(name,target,sizeof(name))>=sizeof(name))return ENAMETOOLONG;
    char *at=strchr(name,'@');
    if(!at||!at[1])return EINVAL;
    *at=0;
    int error=zfs_secpolicy_snapshot_perms(name,caller);
    if(error)return error;
    spa_t *spa=NULL;
    error=spa_open(name,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    spa_close(spa,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    error=recursive?recursive_snapshot_create(name,at+1):dmu_objset_snapshot_one(name,at+1);
    VERIFY0(oz_cred_pop(scope));
    return error;
}

struct oz_admitted {
    struct oz_message request;
    cred_t *caller;
    struct oz_packet_security *security;
    int executed;
};

void oz_admitted_free(struct oz_admitted *job)
{
    if (!job) return;
    crfree(job->caller);
    oz_packet_security_end(job->security);
    memset(job, 0, sizeof(*job));
    free(job);
}

int oz_admitted_capture(struct DosPacket *packet, struct MsgPort *service,
    struct oz_admitted **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!packet || packet->dp_Type != OZ_ADMISSION_ACTION ||
        !packet->dp_Arg1 || packet->dp_Arg2 != sizeof(struct oz_message))
        return EINVAL;
    struct oz_packet_security *security = NULL;
    int error = oz_packet_security_begin(packet, service, &security);
    if (error) return error;
    struct oz_admitted *job = calloc(1, sizeof(*job));
    if (!job) { oz_packet_security_end(security); return ENOMEM; }
    memcpy(&job->request, (const void *)packet->dp_Arg1, sizeof(job->request));
    struct oz_message *m = &job->request;
    int key_file=m->operation==62||m->operation==64;
    struct oz_import_policy import_policy;
    if (oz_validate(m, m->detail, sizeof(m->detail)) != OZ_OK) error = EINVAL;
    else if(m->operation>=67&&m->operation<=69) {
        if(m->flags||m->options[0]||m->job_id)error=EINVAL;
    }
    else if(m->operation==79) {
        if(m->flags||m->argument[0]||m->options[0]||m->job_id)error=EINVAL;
    }
    else if(m->operation==80) {
        if(m->flags||!m->argument[0]||!m->options[0]||
            (m->cursor&&strcmp(m->options,"all"))||m->job_id)error=EINVAL;
    }
    else if(m->operation==65||m->operation==66) {
        if((m->flags!=OZ_CONFIRMED&&!(m->operation==66&&m->flags==(OZ_CONFIRMED|OZ_RECURSIVE)))||!m->argument[0]||!m->options[0]||m->cursor||m->job_id)error=EINVAL;
    }
    else if(m->operation==64&&!strcmp(m->options,"inherit=on")) {
        if(!key_load||m->flags!=OZ_CONFIRMED||m->argument[0]||m->cursor||m->job_id)error=ENOTSUP;
    }
    else if(m->operation==64) {
        /* The credential-aware key adapter validates the bounded property
         * grammar before accessing the replacement key file. */
        if(!key_load||m->flags!=OZ_CONFIRMED||!m->argument[0]||m->cursor||m->job_id)error=ENOTSUP;
    }
    else if(m->operation==38) {
        int deferred=!strcmp(m->options,"defer=on");
        int recursive=m->flags==(OZ_CONFIRMED|OZ_RECURSIVE);
        if(!lifecycle||(m->flags!=OZ_CONFIRMED&&!recursive)||m->argument[0]||m->cursor||m->job_id||
            (m->options[0]&&!deferred)||(deferred&&!strchr(m->target,'@')))error=ENOTSUP;
    }
    /* Only bound, credential-aware operations are admitted for now.
     * Other operations must acquire their own policy adapters before enabling. */
    else if ((!(key_file&&key_load) && !(m->operation==63&&lifecycle) && !((m->operation==60||m->operation==61||m->operation==78)&&replication) && m->operation != 37 && m->operation != 50 && m->operation!=41 && m->operation!=42 && !((m->operation==19||m->operation==22||m->operation==23||m->operation==29||m->operation==32||m->operation==33)&&lifecycle) &&
        !((m->operation==17||m->operation==21||m->operation==20||m->operation==14||m->operation==10||m->operation==9||m->operation==11||m->operation==13||m->operation==12||m->operation==5||m->operation==6||m->operation==7||m->operation==8||m->operation==15||m->operation==16||m->operation==18||m->operation==24||m->operation==26||m->operation==38||m->operation==39||m->operation==43||m->operation==44||m->operation==51||m->operation==52||m->operation==53||m->operation==54||m->operation==55||m->operation==56)&&lifecycle)) ||
        (m->operation==33?(m->flags&~(OZ_CONFIRMED|OZ_DRYRUN|OZ_RAW)):
        (m->flags != OZ_CONFIRMED && !(m->operation==62&&m->flags==(OZ_CONFIRMED|OZ_DRYRUN)) && !((m->operation==5||m->operation==9||m->operation==11||m->operation==13||m->operation==26||m->operation==29||m->operation==61) && m->flags==(OZ_CONFIRMED|OZ_FORCE)) &&
        !((m->operation==39||m->operation==42||m->operation==50||m->operation==55||m->operation==56)&&
            m->flags==(OZ_CONFIRMED|OZ_RECURSIVE)))) ||
        ((key_file||m->operation==60||m->operation==61||m->operation==21||m->operation==20||m->operation==14||m->operation==10||m->operation==11||m->operation==13||m->operation==12||m->operation==22||m->operation==32||m->operation==15||m->operation==16||m->operation==18||m->operation==39||m->operation==41||m->operation==42||m->operation==43||m->operation==51||m->operation==52||m->operation==55||m->operation==56)?!m->argument[0]:(m->operation!=37&&m->operation!=23&&m->operation!=17&&m->argument[0]!=0)) ||
        ((m->operation==21||m->operation==20||m->operation==14)?!m->options[0]:m->operation==7?!oz_parse_import_policy(m->options,&import_policy):
        /* The bound send adapter validates its complete option grammar before
         * opening output. Do not duplicate its former from-only grammar here. */
        (m->operation==60||m->operation==61||m->operation==37||m->operation==5||m->operation==15||m->operation==16)?0:m->operation==43?(strcmp(m->options,"readonly=on")&&strcmp(m->options,"readonly=off")):
        (m->operation==11||m->operation==13)?(m->options[0]&&strcmp(m->options,"rebuild=on")):
        m->operation==42?(m->options[0]&&strcmp(m->options,"received=on")):
        m->operation==24?(m->options[0]&&strcmp(m->options,"scrub_restart=on")&&strcmp(m->options,"scrub_restart=off")):m->options[0]!=0) || (m->cursor&&m->operation!=33) || m->job_id)
        error = ENOTSUP;
    if (!error) {
        job->caller = oz_packet_security_cred(security);
        crhold(job->caller);
        job->security=security;security=NULL;
        memset(&m->msg, 0, sizeof(m->msg));
        m->msg.mn_Length = sizeof(*m);
        memset(m->reply_name, 0, sizeof(m->reply_name));
        memset(m->rows, 0, sizeof(m->rows));
        memset(m->capability_words, 0, sizeof(m->capability_words));
        memset(m->detail, 0, sizeof(m->detail));
        m->row_count = m->next_cursor = m->engine_available = 0;
        m->status = OZ_BUSY;
        *out = job;
    } else oz_admitted_free(job);
    oz_packet_security_end(security);
    return error;
}

const struct oz_packet_security *oz_admitted_security(const struct oz_admitted *job)
{ return job?job->security:NULL; }

int oz_admitted_cancel(struct oz_admitted *job, cred_t *caller)
{
    if (!job) return EINVAL;
    if (!caller || (secpolicy_zfs(caller) &&
        crgetuid(caller) != crgetuid(job->caller))) return EACCES;
    if (job->executed) return EALREADY;
    /* All transitions occur on the owning service task. This terminal flag
     * prevents the pump from dispatching the retained operation afterward. */
    job->executed = 1;
    job->request.status = OZ_CANCELLED;
    snprintf(job->request.detail, sizeof(job->request.detail),
        "Request cancelled before execution; no operation was dispatched");
    return 0;
}

int oz_admitted_execute(struct oz_admitted *job)
{
    if (!job) return EINVAL;
    if (job->executed) return EALREADY;
    job->executed = 1;
    /* Re-evaluate upstream permissions at execution: a grant might have been
     * revoked since admission. Identity is the retained native snapshot. */
    int error = (job->request.operation>=67&&job->request.operation<=69)?oz_accounting_list(&job->request,job->caller):
        job->request.operation==79?oz_permission_list(&job->request,job->caller):
        job->request.operation==80?oz_vdev_property_get(&job->request,job->caller):
        (job->request.operation==65||job->request.operation==66)?
        oz_permission_change(&job->request,job->caller):
        (job->request.operation==62||job->request.operation==64)?
        (key_load?key_load(&job->request,job->security):ENOTSUP):job->request.operation==63?
        (lifecycle?lifecycle(&job->request,job->caller,job->security):ENOTSUP):
        (job->request.operation==60||job->request.operation==61||job->request.operation==78)?
        (replication?replication(&job->request,job->security):ENOTSUP):
        (job->request.operation==41||job->request.operation==42)?
        oz_user_property_change(&job->request,job->caller):
        (job->request.operation==17||job->request.operation==21||job->request.operation==20||job->request.operation==14||job->request.operation==10||job->request.operation==9||job->request.operation==11||job->request.operation==13||job->request.operation==12||job->request.operation==5||job->request.operation==6||job->request.operation==7||job->request.operation==8||job->request.operation==15||job->request.operation==16||job->request.operation==18||job->request.operation==24||job->request.operation==26||job->request.operation==38||job->request.operation==39||
        job->request.operation==19||job->request.operation==22||job->request.operation==23||job->request.operation==29||job->request.operation==33||job->request.operation==32||job->request.operation==43||job->request.operation==44||job->request.operation==51||job->request.operation==52||job->request.operation==53||job->request.operation==54||job->request.operation==55||job->request.operation==56)?
        (lifecycle?lifecycle(&job->request,job->caller,job->security):ENOTSUP):job->request.operation==50?
        snapshot_create(job->request.target,job->caller,(job->request.flags&OZ_RECURSIVE)!=0):
        dataset_create?dataset_create(&job->request,job->security):
        job->request.argument[0]?ENOTSUP:oz_zpl_dataset_create_options(job->request.target,job->request.options,job->caller);
    job->request.status = error ? OZ_ENGINE_ERROR : OZ_OK;
    if((job->request.operation!=20&&job->request.operation!=21&&job->request.operation!=29&&job->request.operation!=66&&job->request.operation!=80&&!(job->request.operation>=67&&job->request.operation<=69)&&
        !(job->request.operation==38&&(job->request.flags&OZ_RECURSIVE)&&!strchr(job->request.target,'@'))&&
        !(job->request.operation==42&&(job->request.flags&OZ_RECURSIVE)))||!job->request.detail[0])
    snprintf(job->request.detail, sizeof(job->request.detail),
        "Authenticated %s returned errno=%d",
        (job->request.operation>=67&&job->request.operation<=69)?"accounting query":job->request.operation==79?"permission list":job->request.operation==65?"permission grant":job->request.operation==66?"permission revoke":
        job->request.operation==64?"key replacement":job->request.operation==62?((job->request.flags&OZ_DRYRUN)?"key verification (not loaded)":"key load"):job->request.operation==63?"key unload":job->request.operation==60?"stream send":job->request.operation==61?"stream receive":job->request.operation==78?"interrupted receive abort":job->request.operation==19?"resilver restart (acceptance, not repair completion)":
        job->request.operation==21?"TRIM control (query status for completion)":job->request.operation==20?"initialization control (query status for completion)":job->request.operation==17?"pool/device clear (query health afterward)":job->request.operation==14?"mirror split (destination exported)":job->request.operation==10?"device removal accepted (query status for completion)":job->request.operation==9?"pool add (query status for new topology)":job->request.operation==11?"device attach (query status for resilver completion)":job->request.operation==13?"device replace (query status for resilver completion)":job->request.operation==12?"device detach (query status for remaining redundancy)":job->request.operation==22?"pool checkpoint control (query status for discard completion)":job->request.operation==23?"pool reguid":job->request.operation==29?"native label clear":job->request.operation==33?"pool history (live; raw mode preserves full records)":job->request.operation==32?"pool property set":job->request.operation==6?"pool destruction":job->request.operation==26?"pool sync":job->request.operation==24?"pool reopen (query status for health)":
        job->request.operation==15?"device online (not resilver completion)":job->request.operation==16?"device offline":job->request.operation==5?"pool creation":job->request.operation==7?"pool import":job->request.operation==18?"scrub control (not completion)":job->request.operation==8?"pool export":job->request.operation==51?"bookmark creation":
        job->request.operation==38&&strchr(job->request.target,'#')?"bookmark destruction":
        job->request.operation==53?"clone promotion":job->request.operation==54?"snapshot rollback":
        job->request.operation==52?"snapshot clone":job->request.operation==55?"snapshot hold":job->request.operation==56?"snapshot release":
        job->request.operation==41?(strchr(job->request.argument,':')?"user property set":"dataset property set"):
        job->request.operation==42?(strchr(job->request.argument,':')?"user property inherit":"dataset property inherit"):
        job->request.operation==80?"vdev property query":job->request.operation==50?"snapshot creation":job->request.operation==38?
        "dataset destruction":job->request.operation==39?"dataset rename":
        job->request.operation==43?"dataset mount":job->request.operation==44?"dataset unmount":"dataset creation",error);
    if(job->request.operation==61&&error==ETXTBSY)
        snprintf(job->request.detail,sizeof(job->request.detail),
            "Receive refused: target differs from the incremental base or raw snapshot chain. "
            "Writable mounting can rebuild accounting after raw receive. "
            "Check the base snapshot and local changes; Force may discard changes and snapshots. errno=%d",error);
    return error;
}

int oz_admitted_result(struct oz_admitted *job, cred_t *caller,
    struct oz_message *out)
{
    if (!job || !out) return EINVAL;
    if (!caller || (secpolicy_zfs(caller) &&
        crgetuid(caller) != crgetuid(job->caller))) return EACCES;
    if (!job->executed) return EAGAIN;
    memcpy(out, &job->request, sizeof(*out));
    return 0;
}

struct oz_admission_server {
    struct MsgPort *port;
#ifdef OZ_NATIVE_DISPATCHER
    struct MsgPort *completed;
    struct oz_ioctl_job *ioctls[OZ_ADMISSION_SLOTS];
#endif
    char name[96];
    uint64_t epoch, next;
    struct { uint64_t number; struct oz_admitted *job; } slots[OZ_ADMISSION_SLOTS];
    struct { uint64_t number; uid_t owner; struct oz_session_cleanup *cleanup; }
        sessions[OZ_ADMISSION_SLOTS];
    struct Library *task_resource;
    struct Hook exit_hook;
    struct SignalSemaphore watch_lock;
    struct { struct Task *task; int retired; } watch[OZ_ADMISSION_SLOTS];
};
#ifdef OZ_NATIVE_DISPATCHER
struct oz_ioctl_job {
    struct Message done;
    struct Message owner_message;
    int (*owner_function)(void *);
    void *owner_argument;
    int owner_result;
    struct oz_admission_server *server;
    struct DosPacket *packet;
    void *destination;
    struct oz_ioctl_wire *snapshot;
    size_t length;
    struct oz_packet_security *security;
    struct oz_session_cleanup *session;
    struct oz_stream_endpoint endpoint;
    pthread_t thread;
    unsigned slot;
    uint64_t number;
    unsigned long command; /* immutable identity, not the worker's reply data */
    int error;
};
static pthread_once_t owner_once=PTHREAD_ONCE_INIT;
static pthread_key_t owner_key;
static int owner_key_error;
static void owner_key_init(void)
{owner_key_error=pthread_key_create(&owner_key,NULL);}
static int receive_owner_call(int (*function)(void *),void *argument)
{
    int error=pthread_once(&owner_once,owner_key_init);
    if(error||owner_key_error)return error?error:owner_key_error;
    struct oz_ioctl_job *job=pthread_getspecific(owner_key);
    if(!job||job->command!=ZFS_IOC_RECV_NEW||!function)return EACCES;
    struct MsgPort *reply=CreateMsgPort();
    if(!reply)return ENOMEM;
    job->owner_function=function;job->owner_argument=argument;
    job->owner_message.mn_ReplyPort=reply;
    job->owner_message.mn_Length=sizeof(job->owner_message);
    PutMsg(job->server->completed,&job->owner_message);
    WaitPort(reply);VERIFY3P(GetMsg(reply),==,&job->owner_message);
    DeleteMsgPort(reply);
    job->owner_function=NULL;job->owner_argument=NULL;
    return job->owner_result;
}
static void ioctl_job_free(struct oz_ioctl_job *job)
{
    if(job->session)oz_session_cleanup_release(job->session);
    oz_packet_security_end(job->security);
    if(job->snapshot){oz_ioctl_wire_clear(job->snapshot,job->length);free(job->snapshot);}
    free(job);
}
static void *ioctl_worker(void *arg)
{
    struct oz_ioctl_job *job=arg;
    job->error=pthread_once(&owner_once,owner_key_init);
    if(!job->error)job->error=owner_key_error;
    if(!job->error)job->error=pthread_setspecific(owner_key,job);
    if(job->error){PutMsg(job->server->completed,&job->done);return NULL;}
    zfs_file_t *stream=NULL;int fd=-1;
    if(job->snapshot->command==ZFS_IOC_SEND_NEW||job->snapshot->command==ZFS_IOC_RECV_NEW||job->snapshot->command==ZFS_IOC_DIFF) {
        job->error=job->snapshot->command==ZFS_IOC_RECV_NEW?
            zfs_file_from_reader(oz_stream_read,&job->endpoint,&stream):
            zfs_file_from_writer(oz_stream_write,&job->endpoint,&stream);
        if(!job->error)job->error=oz_session_file_insert(job->session,stream,&fd);
        if(!job->error)job->error=oz_ioctl_execute_stream(job->snapshot,job->length,
            job->security,job->session,fd);
        if(fd>0)(void)oz_session_file_close(job->session,fd);
        if(stream)zfs_file_close(stream);
    } else if(job->snapshot->command==ZFS_IOC_WAIT||job->snapshot->command==ZFS_IOC_WAIT_FS)
        job->error=oz_ioctl_execute_wait(job->snapshot,job->length,
            job->security,job->session);
    else job->error=oz_ioctl_execute(job->snapshot,job->length,
        job->security,job->session);
    VERIFY0(pthread_setspecific(owner_key,NULL));
    PutMsg(job->server->completed,&job->done);
    return NULL;
}
static int ioctl_job_start(struct oz_admission_server *s,struct DosPacket *p)
{
    size_t length=(size_t)p->dp_Arg2;
    int query=p->dp_Type==OZ_IOCTL_ACTION;
    if(!p->dp_Arg1||(query?p->dp_Arg3!=0:(p->dp_Arg3!=OZ_STREAM_READ&&p->dp_Arg3!=OZ_STREAM_WRITE))||length<sizeof(struct oz_ioctl_wire)||
        length>oz_ioctl_request_limit())return EINVAL;
    unsigned slot;
    for(slot=0;slot<OZ_ADMISSION_SLOTS&&s->ioctls[slot];slot++){}
    if(slot==OZ_ADMISSION_SLOTS)return EBUSY;
    if(s->next==UINT64_MAX)return EOVERFLOW;
    struct oz_ioctl_job *job=calloc(1,sizeof(*job));
    if(!job)return ENOMEM;
    job->server=s;job->packet=p;job->destination=(void *)p->dp_Arg1;
    job->length=length;job->slot=slot;
    job->endpoint=(struct oz_stream_endpoint){p->dp_Port,(uint64_t)(uintptr_t)p->dp_Link};
    int error=oz_packet_security_begin(p,s->port,&job->security);
    if(!error&&!(job->snapshot=malloc(length)))error=ENOMEM;
    if(!error) {
        memcpy(job->snapshot,job->destination,length);
        /* Only audited queries and estimate/send/receive operations. None
         * mutates the native mount registry. Never admit by namespace alone. */
        if(query) {
            /* Classify only our immutable copy, never the client's buffer.
             * Queries run off the DOS owner task: dataset locks can wait. */
            switch(job->snapshot->command) {
            case ZFS_IOC_POOL_STATS: case ZFS_IOC_POOL_CONFIGS:
            case ZFS_IOC_OBJSET_STATS: case ZFS_IOC_OBJSET_ZPLPROPS:
            case ZFS_IOC_DATASET_LIST_NEXT: case ZFS_IOC_SNAPSHOT_LIST_NEXT:
            case ZFS_IOC_POOL_GET_PROPS: case ZFS_IOC_GET_FSACL:
            case ZFS_IOC_SPACE_WRITTEN: case ZFS_IOC_GET_HOLDS:
            case ZFS_IOC_GET_BOOKMARKS: case ZFS_IOC_GET_BOOKMARK_PROPS:
            case ZFS_IOC_OBJ_TO_STATS: case ZFS_IOC_NEXT_OBJ:
                break;
            default: error=EBUSY;break;
            }
        } else if(job->snapshot->command!=ZFS_IOC_WAIT&&job->snapshot->command!=ZFS_IOC_WAIT_FS&&job->snapshot->command!=ZFS_IOC_SEND_SPACE&&job->snapshot->command!=ZFS_IOC_SEND_NEW&&
            job->snapshot->command!=ZFS_IOC_RECV_NEW&&job->snapshot->command!=ZFS_IOC_DIFF)error=ENOTSUP;
        else if(p->dp_Arg3!=(job->snapshot->command==ZFS_IOC_RECV_NEW?OZ_STREAM_READ:OZ_STREAM_WRITE))error=EINVAL;
    }
    if(!error)error=oz_admission_session_acquire(s,&job->snapshot->session,
        oz_packet_security_cred(job->security),&job->session);
    if(!error) {
        job->number=++s->next;job->command=job->snapshot->command;
        s->ioctls[slot]=job;
        error=pthread_create(&job->thread,NULL,ioctl_worker,job);
        if(error)s->ioctls[slot]=NULL;
    }
    if(error)ioctl_job_free(job);
    return error;
}
static void ioctl_jobs_complete(struct oz_admission_server *s)
{
    struct Message *message;
    while((message=GetMsg(s->completed))) {
        struct oz_ioctl_job *owner_job=NULL;
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
            if(s->ioctls[i]&&message==&s->ioctls[i]->owner_message)owner_job=s->ioctls[i];
        if(owner_job) {
            /* Only a retained service-owned receive worker can submit this
             * private message. No public packet contains a callback pointer.
             * Do not pump DOS between suspend, receive-end, and resume. */
            struct oz_cred_scope *scope=NULL;
            int error=owner_job->command!=ZFS_IOC_RECV_NEW||!owner_job->owner_function?
                EACCES:oz_cred_push(oz_packet_security_cred(owner_job->security),&scope);
            if(!error)error=owner_job->owner_function(owner_job->owner_argument);
            if(scope)VERIFY0(oz_cred_pop(scope));
            owner_job->owner_result=error;
            ReplyMsg(message);
            continue;
        }
        struct oz_ioctl_job *job=(void *)message;
        VERIFY0(pthread_join(job->thread,NULL));
        if(!job->error)memcpy(job->destination,job->snapshot,job->length);
        ReplyPkt(job->packet,job->error?DOSFALSE:DOSTRUE,job->error);
        s->ioctls[job->slot]=NULL;
        ioctl_job_free(job);
    }
}
#endif

/* Runs in the removing task's context: no CRT, engine calls or callbacks.
 * The resource serializes hook removal against active invocations. */
AROS_UFH3(static IPTR, session_task_event,
    AROS_UFHA(struct Hook *, hook, A0),
    AROS_UFHA(struct Task *, task, A2),
    AROS_UFHA(struct TaskNotifyMsg *, message, A1))
{
    AROS_USERFUNC_INIT
    struct oz_admission_server *s=hook->h_Data;
    if(message->tnm_Action==TNA_REMOVED) {
        int wake=0;
        ObtainSemaphore(&s->watch_lock);
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
            if(s->watch[i].task==task) {s->watch[i].retired=1;wake=1;}
        ReleaseSemaphore(&s->watch_lock);
        if(wake)Signal(s->port->mp_SigTask,1UL<<s->port->mp_SigBit);
    }
    return 0;
    AROS_USERFUNC_EXIT
}

static void session_remove(struct oz_admission_server *s,unsigned i)
{
    ObtainSemaphore(&s->watch_lock);
    memset(&s->watch[i],0,sizeof(s->watch[i]));
    ReleaseSemaphore(&s->watch_lock);
    struct oz_session_cleanup *cleanup=s->sessions[i].cleanup;
    memset(&s->sessions[i],0,sizeof(s->sessions[i]));
    oz_session_cleanup_close(cleanup);
}

static void session_reap(struct oz_admission_server *s)
{
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
        ObtainSemaphore(&s->watch_lock);
        int retired=s->watch[i].retired;
        ReleaseSemaphore(&s->watch_lock);
        if(retired&&s->sessions[i].cleanup)session_remove(s,i);
    }
}

struct oz_admission_server *oz_admission_server_open(const char *name)
{
    if(!name||!name[0]||strlen(name)>=96){errno=EINVAL;return NULL;}
    struct oz_admission_server *s=calloc(1,sizeof(*s));
    if(!s)return NULL;
    int error=random_get_bytes((uint8_t *)&s->epoch,sizeof(s->epoch));
    if(error||!s->epoch){free(s);errno=error?error:EIO;return NULL;}
    s->port=CreateMsgPort();
    if(!s->port){free(s);errno=ENOMEM;return NULL;}
#ifdef OZ_NATIVE_DISPATCHER
    s->completed=CreateMsgPort();
    if(!s->completed){DeleteMsgPort(s->port);free(s);errno=ENOMEM;return NULL;}
    oz_receive_bind_owner(receive_owner_call);
#endif
    InitSemaphore(&s->watch_lock);
    struct Library *TaskResBase=OpenResource((CONST_STRPTR)"task.resource");
    /* Resource initialization fills only Node, not Library version fields.
     * Verify the resident corresponding to this base before calling new LVOs. */
    const struct Resident *resident=FindResident((CONST_STRPTR)"task.resource");
    unsigned version=0,revision=0;
    int hooks=TaskResBase&&resident&&resident->rt_Type==NT_RESOURCE&&
        ((struct Node *)TaskResBase)->ln_Name==(char *)resident->rt_Name&&
        resident->rt_IdString&&sscanf((const char *)resident->rt_IdString,
            "task.resource %u.%u",&version,&revision)==2&&
        (version>1||(version==1&&revision>=4));
    if(hooks) {
        s->exit_hook.h_Entry=(HOOKFUNC)(void (*)(void))session_task_event;
        s->exit_hook.h_Data=s;
        if(AddTaskNotifyHook(&s->exit_hook))s->task_resource=TaskResBase;
    }
    if(!s->task_resource) {
        const char *why="OpenZFS sessions unavailable: task.resource 1.4 lifecycle hooks required\n";
        while(*why)RawPutChar(*why++);
    }
    strcpy(s->name,name);s->port->mp_Node.ln_Name=s->name;
    Forbid();
    if(FindPort((CONST_STRPTR)name)) {
        Permit();
        if(s->task_resource)RemTaskNotifyHook(&s->exit_hook);
#ifdef OZ_NATIVE_DISPATCHER
        DeleteMsgPort(s->completed);
#endif
        DeleteMsgPort(s->port);free(s);errno=EEXIST;return NULL;
    }
    AddPort(s->port);Permit();
    return s;
}

ULONG oz_admission_server_signals(struct oz_admission_server *s)
{
    if(!s)return 0;
    ULONG signals=1UL<<s->port->mp_SigBit;
#ifdef OZ_NATIVE_DISPATCHER
    signals|=1UL<<s->completed->mp_SigBit;
#endif
    return signals;
}

int oz_admission_session_acquire(struct oz_admission_server *s,
    const struct oz_ticket *ticket,cred_t *caller,struct oz_session_cleanup **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!s||!ticket||!caller)return EINVAL;
    if(FindTask(NULL)!=s->port->mp_SigTask)return EDEADLK;
    if(ticket->epoch!=s->epoch)return ENOENT;
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
        if(s->sessions[i].cleanup&&s->sessions[i].number==ticket->number) {
            if(s->sessions[i].owner!=crgetuid(caller))return EACCES;
            int error=oz_session_cleanup_hold(s->sessions[i].cleanup);
            if(!error)*out=s->sessions[i].cleanup;
            return error;
        }
    }
    return ENOENT;
}

static int server_packet(struct oz_admission_server *s,struct DosPacket *p)
{
    /* Cancellation only changes queued metadata. Allow it while a storage
     * worker is active, but never interrupt or free that worker. */
    if(p->dp_Type==OZ_ADMISSION_CANCEL) {
        if(!p->dp_Arg1||p->dp_Arg2||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_ticket ticket=*(const struct oz_ticket *)p->dp_Arg1;
        cred_t *caller=oz_packet_security_cred(context);
        error=ENOENT;
        if(ticket.epoch==s->epoch&&ticket.number) {
            for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
                if(s->slots[i].job&&s->slots[i].number==ticket.number) {
                    error=oz_admitted_cancel(s->slots[i].job,caller);break;
                }
#ifdef OZ_NATIVE_DISPATCHER
                if(s->ioctls[i]&&s->ioctls[i]->number==ticket.number) {
                    cred_t *owner=oz_packet_security_cred(s->ioctls[i]->security);
                    error=secpolicy_zfs(caller)&&crgetuid(caller)!=crgetuid(owner)?EACCES:EBUSY;
                    break;
                }
#endif
            }
        }
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_HANDLER_ATTACH) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_handler_attach)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        cred_t *caller=oz_packet_security_cred(context);
        error=secpolicy_zfs(caller);
        if(!error) {
            struct oz_handler_attach q=*(struct oz_handler_attach *)p->dp_Arg1;
            if(q.version!=OZ_HANDLER_VERSION||(q.flags&~OZ_HANDLER_WRITABLE)||
                !q.device||q.endpoint||!memchr(q.dataset,0,sizeof(q.dataset))||
                !memchr(q.name,0,sizeof(q.name)))error=EINVAL;
            else error=handler_attach?handler_attach(&q,caller):ENOTSUP;
            if(!error)((struct oz_handler_attach *)p->dp_Arg1)->endpoint=q.endpoint;
        }
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_ADMISSION_LIST){
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_message)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        cred_t *caller=oz_packet_security_cred(context);
        struct oz_message *out=calloc(1,sizeof(*out));
        if(!out){oz_packet_security_end(context);return ENOMEM;}
        out->magic=OZ_MAGIC;out->version=OZ_VERSION;out->operation=76;
        out->msg.mn_Length=sizeof(*out);out->status=OZ_OK;
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++){
            struct oz_admitted *job=s->slots[i].job;
            if(!job||(secpolicy_zfs(caller)&&crgetuid(caller)!=crgetuid(job->caller)))continue;
            struct oz_row *row=&out->rows[out->row_count++];
            strcpy(row->kind,"admission-ticket");
            snprintf(row->name,sizeof(row->name),"%llu:%llu",(unsigned long long)s->epoch,(unsigned long long)s->slots[i].number);
            snprintf(row->parent,sizeof(row->parent),"operation=%lu owner=%lu",(unsigned long)job->request.operation,(unsigned long)crgetuid(job->caller));
            strcpy(row->state,job->request.status==OZ_CANCELLED?"cancelled":job->executed?"completed":"queued");
            if(job->executed)snprintf(row->value,sizeof(row->value),"status=%lu",(unsigned long)job->request.status);
            else strcpy(row->value,"result unavailable");
        }
#ifdef OZ_NATIVE_DISPATCHER
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++){
            struct oz_ioctl_job *job=s->ioctls[i];
            if(!job)continue;
            cred_t *owner=oz_packet_security_cred(job->security);
            if(secpolicy_zfs(caller)&&crgetuid(caller)!=crgetuid(owner))continue;
            struct oz_row *row=&out->rows[out->row_count++];
            strcpy(row->kind,"service-worker");
            snprintf(row->name,sizeof(row->name),"%llu:%llu",(unsigned long long)s->epoch,(unsigned long long)job->number);
            snprintf(row->parent,sizeof(row->parent),"ioctl=%lu owner=%lu",job->command,(unsigned long)crgetuid(owner));
            strcpy(row->state,"in-flight");
            strcpy(row->value,(job->command==ZFS_IOC_WAIT||job->command==ZFS_IOC_WAIT_FS)?"wait":job->command==ZFS_IOC_SEND_NEW?"send":
                job->command==ZFS_IOC_RECV_NEW?"receive":job->command==ZFS_IOC_DIFF?"diff":
                job->command==ZFS_IOC_SEND_SPACE?"send estimate":"query");
        }
#endif
        snprintf(out->detail,sizeof(out->detail),"Visible admission requests and in-flight service workers: %lu. Released tickets and drained workers are absent. Only queued admission requests may be cancelled; no activity percentage.",(unsigned long)out->row_count);
        memcpy((void *)p->dp_Arg1,out,sizeof(*out));free(out);
        oz_packet_security_end(context);return 0;
    }
#ifdef OZ_NATIVE_DISPATCHER
    /* Keep mount/topology administration out of an active stream's lifetime.
     * DOS and zvol data I/O must still run, including output on our own pool.
     * A rejected control request is never queued or silently retried. */
    if(p->dp_Type!=OZ_SESSION_OPEN&&p->dp_Type!=OZ_SESSION_CLOSE&&
        p->dp_Type!=OZ_CONTROL_OPEN&&p->dp_Type!=OZ_CONTROL_CLOSE&&p->dp_Type!=OZ_ZVOL_ACTION) {
        for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
            if(s->ioctls[i]&&s->ioctls[i]->snapshot->command!=ZFS_IOC_WAIT&&s->ioctls[i]->snapshot->command!=ZFS_IOC_WAIT_FS)return EBUSY;
    }
#endif
    if(p->dp_Type==OZ_ZVOL_ACTION) {
#ifndef OZ_NATIVE_DISPATCHER
        return ENOTSUP;
#else
        if(!p->dp_Arg1||p->dp_Arg2<(IPTR)sizeof(struct oz_zvol_request)||
           p->dp_Arg2>(IPTR)(sizeof(struct oz_zvol_request)+OZ_ZVOL_IO_MAX)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        struct oz_zvol_request q;memcpy(&q,(void *)p->dp_Arg1,sizeof(q));
        int io=q.operation==OZ_ZVOL_READ||q.operation==OZ_ZVOL_WRITE;
        int discard=q.operation==OZ_ZVOL_DISCARD;
        struct oz_session_cleanup *session=NULL;zfs_file_t *file=NULL;
        void *bytes=NULL;struct oz_cred_scope *scope=NULL;
        cred_t *caller=oz_packet_security_cred(context);
        if(q.version!=OZ_ZVOL_VERSION||q.operation<OZ_ZVOL_OPEN||q.operation>OZ_ZVOL_CAPABILITIES||
           q.completed||q.size||q.block_size||q.readonly||q.engine_error||
           !memchr(q.name,0,sizeof(q.name))||
           ((io||discard)?(!q.count||(io&&q.count>OZ_ZVOL_IO_MAX)||(q.count&511)||(q.offset&511)):(q.count||q.offset))||
           p->dp_Arg2!=(IPTR)(sizeof(q)+(io?q.count:0))||
           (q.operation==OZ_ZVOL_OPEN?(q.descriptor!=-1||!q.name[0]||q.flags>OZ_ZVOL_WRITABLE):
             (q.descriptor<=0||q.name[0]||q.flags)))error=EINVAL;
        else if((error=secpolicy_zfs(caller))!=0){}
        else error=oz_admission_session_acquire(s,&q.session,caller,&session);
        if(!error&&io&&!(bytes=calloc(1,q.count)))error=ENOMEM;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error){
            if(q.operation==OZ_ZVOL_OPEN){
                char path[sizeof(q.name)+5];snprintf(path,sizeof(path),"ZVOL:%s",q.name);
                if((q.flags&OZ_ZVOL_WRITABLE)&&!(spa_mode_global&SPA_MODE_WRITE))q.engine_error=EROFS;
                else q.engine_error=zfs_file_open(path,q.flags?O_RDWR:O_RDONLY,0,caller,&file);
                if(!q.engine_error){
                    int descriptor=-1;
                    q.engine_error=oz_session_file_insert(session,file,&descriptor);
                    if(!q.engine_error)q.descriptor=descriptor;
                }
            }else{
                q.engine_error=oz_session_file_get(session,q.descriptor,&file);
                if(!q.engine_error&&!file->f_zvol)q.engine_error=EBADF;
                if(!q.engine_error){
                    if(io){
                        if(q.operation==OZ_ZVOL_WRITE)memcpy(bytes,(char *)p->dp_Arg1+sizeof(q),q.count);
                        size_t done=0;
                        q.engine_error=oz_zvol_io(file->f_zvol,q.offset,bytes,q.count,q.operation==OZ_ZVOL_WRITE,&done);
                        q.completed=done;
                        if(q.operation==OZ_ZVOL_READ&&done)memcpy((char *)p->dp_Arg1+sizeof(q),bytes,done);
                    }else if(q.operation==OZ_ZVOL_CAPABILITIES){
                        q.flags=OZ_ZVOL_CAP_DISCARD;
                    }else if(discard){
                        q.engine_error=oz_zvol_discard(file->f_zvol,q.offset,q.count);
                        if(!q.engine_error)q.completed=q.count;
                    }else if(q.operation==OZ_ZVOL_INFO){
                        uint64_t size=0,block=0;boolean_t readonly=B_TRUE;
                        q.engine_error=oz_zvol_info(file->f_zvol,&size,&block,&readonly);
                        if(!q.engine_error){q.size=size;q.block_size=block;q.readonly=readonly;}
                    }else if(q.operation==OZ_ZVOL_FLUSH)q.engine_error=oz_zvol_flush(file->f_zvol);
                    else q.engine_error=oz_session_file_close(session,q.descriptor);
                }
            }
            memcpy((void *)p->dp_Arg1,&q,sizeof(q));
        }
        if(scope)(void)oz_cred_pop(scope);
        if(bytes){memset(bytes,0,q.count);free(bytes);}
        if(file)zfs_file_close(file);
        if(session)oz_session_cleanup_release(session);
        oz_packet_security_end(context);return error;
#endif
    }
    if(p->dp_Type==OZ_GPT_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_gpt_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        struct oz_gpt_query q;memcpy(&q,(const void *)p->dp_Arg1,sizeof(q));
        int fd=-1,changed=0;
        if(q.version!=OZ_GPT_VERSION||q.reserved||q.changed||q.unit<0||
           !q.device[0]||!memchr(q.device,0,sizeof(q.device))||
           !memchr(q.name,0,sizeof(q.name))||!q.bytes||!q.sector_size||
           (q.operation!=OZ_GPT_LABEL&&q.operation!=OZ_GPT_EXPAND)||
           q.flags!=(q.operation==OZ_GPT_LABEL?(OZ_GPT_CONFIRMED|OZ_GPT_REPLACE_TABLE):OZ_GPT_CONFIRMED)||
           (q.operation==OZ_GPT_EXPAND&&(q.start||q.name[0])))error=EINVAL;
        else if((error=secpolicy_zfs(oz_packet_security_cred(context)))!=0) {}
        else if(!(spa_mode_global&SPA_MODE_WRITE))error=EROFS;
        else if((fd=oz_efi_open(q.device,q.unit,1))<0)error=errno?errno:EIO;
        if(!error) {
            uint64_t sectors;unsigned sector;
            if(oz_efi_disk_info(fd,&sectors,&sector))error=errno?errno:EIO;
            else if(sector!=q.sector_size||sectors!=q.bytes/sector||q.bytes%sector)error=ESTALE;
        }
        if(!error&&q.operation==OZ_GPT_LABEL) {
            error=oz_efi_label(fd,q.start,q.name);if(!error)changed=1;
        } else if(!error)error=oz_efi_expand(fd,&changed);
        if(fd>0){int close_error=oz_efi_close(fd);if(!error)error=close_error;}
        if(!error)((struct oz_gpt_query *)p->dp_Arg1)->changed=changed;
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_STORAGE_INVENTORY_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_storage_inventory_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        uint32_t header[2];memcpy(header,(void *)p->dp_Arg1,sizeof(header));
        struct oz_storage_inventory *result=NULL;
        if(header[0]!=1||header[1])error=EINVAL;
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else if(!(result=calloc(1,sizeof(*result))))error=ENOMEM;
        else error=oz_storage_inventory_native(result);
        if(!error)memcpy(&((struct oz_storage_inventory_query *)p->dp_Arg1)->inventory,result,sizeof(*result));
        free(result);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_DEVICE_HANDLE_OPEN||p->dp_Type==OZ_DEVICE_HANDLE_READ) {
        int opening=p->dp_Type==OZ_DEVICE_HANDLE_OPEN;
        size_t header=opening?sizeof(struct oz_device_handle_open):sizeof(struct oz_device_handle_read);
        if(!p->dp_Arg1||p->dp_Arg2<(IPTR)header||p->dp_Arg3||
           p->dp_Arg2>(IPTR)(header+(opening?0:OZ_DEVICE_READ_MAX)))return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);if(error)return error;
        struct oz_session_cleanup *session=NULL;
        struct oz_ticket ticket;memcpy(&ticket,(const void *)p->dp_Arg1,sizeof(ticket));
        if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else error=oz_admission_session_acquire(s,&ticket,oz_packet_security_cred(context),&session);
        zfs_file_t *file=NULL;
        if(!error&&opening) {
            struct oz_device_handle_open q;memcpy(&q,(const void *)p->dp_Arg1,sizeof(q));
            struct oz_device *device=NULL;
            if(q.reserved||q.descriptor!=-1||q.request.version!=OZ_PARTITION_QUERY_VERSION||
               q.request.reserved||q.request.reserved2||q.request.unit<0||!q.request.device[0]||
               !memchr(q.request.device,0,sizeof(q.request.device)))error=EINVAL;
            else error=oz_device_open(q.request.device,q.request.unit,0,&device);
            if(!error) {
                q.geometry=(struct oz_device_geometry){OZ_PARTITION_QUERY_VERSION,
                    oz_device_sector(device),oz_device_size(device)};
                error=oz_file_from_device(device,&file);
                if(!error)device=NULL;
            }
            if(!error){int fd;error=oz_session_file_insert(session,file,&fd);if(!error)q.descriptor=fd;}
            if(!error)memcpy((void *)p->dp_Arg1,&q,sizeof(q));
            oz_device_close(device);
        } else if(!error) {
            struct oz_device_handle_read q;memcpy(&q,(const void *)p->dp_Arg1,sizeof(q));
            if(q.descriptor<=0||!q.count||q.count>OZ_DEVICE_READ_MAX||
               p->dp_Arg2!=(IPTR)(sizeof(q)+q.count))error=EINVAL;
            else error=oz_session_file_get(session,q.descriptor,&file);
            if(!error&&!file->f_device)error=EBADF;
            void *bytes=NULL;
            if(!error&&!(bytes=calloc(1,q.count)))error=ENOMEM;
            if(!error)error=oz_device_transfer(file->f_device,bytes,q.count,q.offset,0);
            if(!error)memcpy((char *)p->dp_Arg1+sizeof(q),bytes,q.count);
            if(bytes){memset(bytes,0,q.count);free(bytes);}
        }
        if(file)zfs_file_close(file);
        if(session)oz_session_cleanup_release(session);
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_DEVICE_READ_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2<(IPTR)sizeof(struct oz_device_read_query)||
           p->dp_Arg2>(IPTR)(sizeof(struct oz_device_read_query)+OZ_DEVICE_READ_MAX)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_device_read_query query;
        memcpy(&query,(const void *)p->dp_Arg1,sizeof(query));
        struct oz_device *device=NULL;
        void *bytes=NULL;
        if(query.request.version!=OZ_PARTITION_QUERY_VERSION||
           query.request.reserved||query.request.reserved2||query.reserved||
           query.request.unit<0||!query.request.device[0]||
           !memchr(query.request.device,0,sizeof(query.request.device))||
           !query.count||query.count>OZ_DEVICE_READ_MAX||
           p->dp_Arg2!=(IPTR)(sizeof(query)+query.count))error=EINVAL;
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else error=oz_device_open(query.request.device,query.request.unit,0,&device);
        if(!error&&(query.offset>oz_device_size(device)||
           query.count>oz_device_size(device)-query.offset||
           query.offset%oz_device_sector(device)||query.count%oz_device_sector(device)))error=EINVAL;
        if(!error&&!(bytes=calloc(1,query.count)))error=ENOMEM;
        if(!error)error=oz_device_transfer(device,bytes,query.count,query.offset,0);
        /* Never expose partial or failed device reads. Transfer uses a private
         * buffer, not caller-controlled request memory, throughout DoIO. */
        if(!error)memcpy((char *)p->dp_Arg1+sizeof(query),bytes,query.count);
        if(bytes){memset(bytes,0,query.count);free(bytes);}
        oz_device_close(device);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_DEVICE_DOS_CLAIM_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_partition_request)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_partition_request query;
        memcpy(&query,(const void *)p->dp_Arg1,sizeof(query));
        struct oz_device *device=NULL;
        if(query.version!=OZ_PARTITION_QUERY_VERSION||query.reserved||query.reserved2||
           query.unit<0||!query.device[0]||!memchr(query.device,0,sizeof(query.device)))error=EINVAL;
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else error=oz_device_open(query.device,query.unit,0,&device);
        if(!error)error=oz_device_dos_claim(query.device,query.unit);
        oz_device_close(device);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_DEVICE_INQUIRY_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_device_inquiry_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_partition_request query;
        memcpy(&query,(const void *)p->dp_Arg1,sizeof(query));
        struct oz_device *device=NULL;
        if(query.version!=OZ_PARTITION_QUERY_VERSION||query.reserved||query.reserved2||
           query.unit<0||!query.device[0]||!memchr(query.device,0,sizeof(query.device)))error=EINVAL;
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else error=oz_device_open(query.device,query.unit,0,&device);
        struct oz_device_inquiry result={.version=OZ_PARTITION_QUERY_VERSION};
        unsigned length=0;
        if(!error)error=oz_device_inquiry(device,result.data,&length);
        if(!error) {
            result.length=length;
            memcpy(&((struct oz_device_inquiry_query *)p->dp_Arg1)->inquiry,&result,sizeof(result));
        }
        oz_device_close(device);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_DEVICE_GEOMETRY_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_device_geometry_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_partition_request query;
        memcpy(&query,(const void *)p->dp_Arg1,sizeof(query));
        struct oz_device *device=NULL;
        if(query.version!=OZ_PARTITION_QUERY_VERSION||query.reserved||query.reserved2||
           query.unit<0||!query.device[0]||!memchr(query.device,0,sizeof(query.device)))error=EINVAL;
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else error=oz_device_open(query.device,query.unit,0,&device);
        if(!error) {
            struct oz_device_geometry result={OZ_PARTITION_QUERY_VERSION,
                oz_device_sector(device),oz_device_size(device)};
            memcpy(&((struct oz_device_geometry_query *)p->dp_Arg1)->geometry,&result,sizeof(result));
        }
        oz_device_close(device);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_PARTITION_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_partition_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_partition_request query;
        memcpy(&query,(const void *)p->dp_Arg1,sizeof(query));
        struct oz_partition_catalog *catalog=NULL;
        if(query.version!=OZ_PARTITION_QUERY_VERSION||query.reserved||query.reserved2||
           query.unit<0||!query.device[0]||!memchr(query.device,0,sizeof(query.device)))error=EINVAL;
        /* Raw device metadata is outside dataset ACLs. Never inherit the
         * service's privilege or accept an identity supplied in the request. */
        else if(crgetuid(oz_packet_security_cred(context))!=0)error=EACCES;
        else if(!(catalog=calloc(1,sizeof(*catalog))))error=ENOMEM;
        else error=oz_partition_catalog_read(query.device,query.unit,catalog);
        if(!error)memcpy(&((struct oz_partition_query *)p->dp_Arg1)->catalog,catalog,sizeof(*catalog));
        free(catalog);oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_MOUNT_SHARES_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_mount_shares_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_mount_shares_query query;
        memcpy(&query,(void *)p->dp_Arg1,sizeof(query));
        if(query.version!=OZ_MOUNTS_VERSION||query.object||query.reserved||
           !query.dataset[0]||!memchr(query.dataset,0,sizeof(query.dataset)))error=EINVAL;
        else if(!mount_shares)error=ENOTSUP;
        else error=mount_shares(query.dataset,&query.object);
        if(!error)memcpy((void *)p->dp_Arg1,&query,sizeof(query));
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_MOUNT_POLICY_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_mount_policy_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_mount_policy_query query;
        memcpy(&query,(void *)p->dp_Arg1,sizeof(query));
        if(query.version!=OZ_MOUNTS_VERSION||query.supported||query.enabled||query.reserved||
           !query.dataset[0]||!memchr(query.dataset,0,sizeof(query.dataset)))error=EINVAL;
        else if(!mount_policy)error=ENOTSUP;
        else error=mount_policy(query.dataset,&query.supported,&query.enabled);
        if(!error)memcpy((void *)p->dp_Arg1,&query,sizeof(query));
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_ENGINE_VERSION_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_engine_version_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_engine_version_query query;
        memcpy(&query,(void *)p->dp_Arg1,sizeof(query));
        const char empty[sizeof(query.engine)]={0};
        if(query.version!=OZ_SUPPORT_VERSION||query.reserved||
           memcmp(query.engine,empty,sizeof(empty)))error=EINVAL;
        else if(sizeof(ZFS_META_ALIAS)>sizeof(query.engine))error=EOVERFLOW;
        else {
            memcpy(query.engine,ZFS_META_ALIAS,sizeof(ZFS_META_ALIAS));
            memcpy((void *)p->dp_Arg1,&query,sizeof(query));
        }
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_HOSTID_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_hostid_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_hostid_query query;
        memcpy(&query,(void *)p->dp_Arg1,sizeof(query));
        if(query.version!=OZ_SUPPORT_VERSION||query.hostid)error=EINVAL;
        else {
            query.hostid=zone_get_hostid(NULL);
            memcpy((void *)p->dp_Arg1,&query,sizeof(query));
        }
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_MOUNT_CONTROL_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_mount_control)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_mount_control input;
        memcpy(&input,(void *)p->dp_Arg1,sizeof(input));
        if(input.version!=OZ_MOUNTS_VERSION||input.reserved||input.flags&~OZ_MOUNT_READONLY||
           !memchr(input.dataset,0,sizeof(input.dataset))||!memchr(input.volume,0,sizeof(input.volume))||
           !input.dataset[0]||(input.operation!=OZ_MOUNT_ATTACH&&input.operation!=OZ_MOUNT_DETACH)||
           (input.operation==OZ_MOUNT_DETACH&&(input.flags||input.volume[0])))error=EINVAL;
        else if(!lifecycle)error=ENOTSUP;
        else {
            struct oz_message *message=calloc(1,sizeof(*message));
            if(!message)error=ENOMEM;
            else {
                message->operation=input.operation==OZ_MOUNT_ATTACH?43:44;
                strcpy(message->target,input.dataset);strcpy(message->argument,input.volume);
                strcpy(message->options,input.flags&OZ_MOUNT_READONLY?"readonly=on":"readonly=off");
                error=lifecycle(message,oz_packet_security_cred(context),context);
                free(message);
            }
        }
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_SUPPORT_ACTION) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_support_query)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_support_query query;
        memcpy(&query,(void *)p->dp_Arg1,sizeof(query));
        if(query.version!=OZ_SUPPORT_VERSION||query.supported||
           !memchr(query.scope,0,sizeof(query.scope))||!memchr(query.name,0,sizeof(query.name))||
           !query.scope[0]||!query.name[0])error=EINVAL;
        else if(!strcmp(query.scope,ZFS_SYSFS_POOL_FEATURES)) {
            for(unsigned i=0;i<SPA_FEATURES;i++)
                if(spa_feature_table[i].fi_guid&&spa_feature_table[i].fi_zfs_mod_supported&&
                   !strcmp(query.name,spa_feature_table[i].fi_guid))query.supported=1;
        } else {
            zprop_desc_t *table=NULL;unsigned count=0;
            if(!strcmp(query.scope,ZFS_SYSFS_POOL_PROPERTIES)){table=zpool_prop_get_table();count=ZPOOL_NUM_PROPS;}
            else if(!strcmp(query.scope,ZFS_SYSFS_DATASET_PROPERTIES)){table=zfs_prop_get_table();count=ZFS_NUM_PROPS;}
            else if(!strcmp(query.scope,ZFS_SYSFS_VDEV_PROPERTIES)){table=vdev_prop_get_table();count=VDEV_NUM_PROPS;}
            else error=ENOTSUP;
            for(unsigned i=0;i<count;i++)
                if(table[i].pd_name&&table[i].pd_zfs_mod_supported&&!strcmp(query.name,table[i].pd_name))query.supported=1;
        }
        if(!error)memcpy((void *)p->dp_Arg1,&query,sizeof(query));
        oz_packet_security_end(context);return error;
    }
    if(p->dp_Type==OZ_MOUNTS_ACTION) {
        size_t length=(size_t)p->dp_Arg2;
        if(!p->dp_Arg1||p->dp_Arg3||length<sizeof(struct oz_mount_snapshot)||
           length>sizeof(struct oz_mount_snapshot)+OZ_MOUNTS_LIMIT*sizeof(struct oz_mount_record))return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_mount_snapshot input;
        memcpy(&input,(void *)p->dp_Arg1,sizeof(input));
        if(input.version!=OZ_MOUNTS_VERSION||input.count||input.reserved||
           input.capacity>OZ_MOUNTS_LIMIT||length!=sizeof(input)+(size_t)input.capacity*sizeof(struct oz_mount_record))error=EINVAL;
        else if(!mount_snapshot)error=ENOTSUP;
        else {
            struct oz_mount_snapshot *output=calloc(1,length);
            if(!output)error=ENOMEM;
            else {
                *output=input;
                error=mount_snapshot((void *)(output+1),input.capacity,&output->count);
                if(!error||error==ENOSPC)memcpy((void *)p->dp_Arg1,output,length);
                free(output);
            }
        }
        oz_packet_security_end(context);return error;
    }
#ifdef OZ_NATIVE_DISPATCHER
    if(p->dp_Type==OZ_IOCTL_ACTION) {
        size_t length=(size_t)p->dp_Arg2;
        if(!p->dp_Arg1||p->dp_Arg3||length<sizeof(struct oz_ioctl_wire)||length>oz_ioctl_request_limit())return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_ioctl_wire *snapshot=malloc(length);
        struct oz_session_cleanup *session=NULL;
        if(!snapshot)error=ENOMEM;
        else {
            memcpy(snapshot,(const void *)p->dp_Arg1,length);
            error=oz_admission_session_acquire(s,&snapshot->session,oz_packet_security_cred(context),&session);
            if(!error)error=oz_ioctl_execute(snapshot,length,context,session);
            if(!error)memcpy((void *)p->dp_Arg1,snapshot,length);
        }
        if(session)oz_session_cleanup_release(session);
        if(snapshot){oz_ioctl_wire_clear(snapshot,length);free(snapshot);}
        oz_packet_security_end(context);
        return error;
    }
#endif
    if(p->dp_Type==OZ_CONTROL_OPEN||p->dp_Type==OZ_CONTROL_CLOSE) {
#ifndef OZ_NATIVE_DISPATCHER
        return ENOTSUP;
#else
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_control_request)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        struct oz_control_request request;
        memcpy(&request,(const void *)p->dp_Arg1,sizeof(request));
        struct oz_session_cleanup *session=NULL;
        if(request.reserved || (p->dp_Type==OZ_CONTROL_OPEN?request.descriptor!=-1:request.descriptor<=0))error=EINVAL;
        else error=oz_admission_session_acquire(s,&request.session,oz_packet_security_cred(context),&session);
        if(!error && p->dp_Type==OZ_CONTROL_OPEN) {
            zfs_file_t *file=NULL;int descriptor=-1;
            error=oz_control_file_open(&file);
            if(!error) {
                error=oz_session_file_insert(session,file,&descriptor);
                zfs_file_close(file);
            }
            if(!error) {
                request.descriptor=descriptor;
                memcpy((void *)p->dp_Arg1,&request,sizeof(request));
            }
        } else if(!error)error=oz_session_file_close(session,request.descriptor);
        if(session)oz_session_cleanup_release(session);
        oz_packet_security_end(context);
        return error;
#endif
    }
    if(p->dp_Type==OZ_SESSION_OPEN||p->dp_Type==OZ_SESSION_CLOSE) {
        if(!p->dp_Arg1||p->dp_Arg2!=sizeof(struct oz_ticket)||p->dp_Arg3)return EINVAL;
        struct oz_packet_security *context=NULL;
        int error=oz_packet_security_begin(p,s->port,&context);
        if(error)return error;
        cred_t *caller=oz_packet_security_cred(context);
        if(p->dp_Type==OZ_SESSION_OPEN) {
            if(!s->task_resource) {oz_packet_security_end(context);return ENOTSUP;}
            unsigned i;
            for(i=0;i<OZ_ADMISSION_SLOTS&&s->sessions[i].cleanup;i++);
            if(i==OZ_ADMISSION_SLOTS||s->next==UINT64_MAX)error=EBUSY;
            else {
                error=oz_session_cleanup_create(&s->sessions[i].cleanup);
                if(!error) {
                    s->sessions[i].number=++s->next;
                    s->sessions[i].owner=crgetuid(caller);
                    /* Packet sender remains live through ReplyPkt. Publish
                     * the watch before replying; do not dereference it later. */
                    ObtainSemaphore(&s->watch_lock);
                    s->watch[i].task=p->dp_Port->mp_SigTask;
                    s->watch[i].retired=0;
                    ReleaseSemaphore(&s->watch_lock);
                    struct oz_ticket ticket={s->epoch,s->next};
                    memcpy((void *)p->dp_Arg1,&ticket,sizeof(ticket));
                }
            }
        } else {
            struct oz_ticket ticket;
            memcpy(&ticket,(const void *)p->dp_Arg1,sizeof(ticket));
            struct oz_session_cleanup *session=NULL;
            error=oz_admission_session_acquire(s,&ticket,caller,&session);
            if(!error) {
                for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
                    if(s->sessions[i].cleanup==session) {session_remove(s,i);break;}
                oz_session_cleanup_release(session);
            }
        }
        oz_packet_security_end(context);
        return error;
    }
    if(p->dp_Type==OZ_ADMISSION_ACTION) {
        if(!p->dp_Arg3)return EINVAL;
        struct oz_admitted *job=NULL;
        int error=oz_admitted_capture(p,s->port,&job);
        if(error)return error;
        unsigned i;
        for(i=0;i<OZ_ADMISSION_SLOTS&&s->slots[i].job;i++);
        if(i==OZ_ADMISSION_SLOTS||s->next==UINT64_MAX) {
            oz_admitted_free(job);return EBUSY;
        }
        s->slots[i].job=job;s->slots[i].number=++s->next;
        struct oz_ticket ticket={s->epoch,s->next};
        memcpy((void *)p->dp_Arg3,&ticket,sizeof(ticket));
        return 0;
    }
    if(p->dp_Type!=OZ_ADMISSION_QUERY&&p->dp_Type!=OZ_ADMISSION_RELEASE)return ENOTSUP;
    if(!p->dp_Arg1)return EINVAL;
    if(p->dp_Type==OZ_ADMISSION_QUERY&&(!p->dp_Arg2||p->dp_Arg3!=sizeof(struct oz_message)))return EINVAL;
    if(p->dp_Type==OZ_ADMISSION_RELEASE&&(p->dp_Arg2||p->dp_Arg3))return EINVAL;
    struct oz_packet_security *context=NULL;
    int error=oz_packet_security_begin(p,s->port,&context);
    if(error)return error;
    struct oz_ticket ticket;
    memcpy(&ticket,(void *)p->dp_Arg1,sizeof(ticket));
    unsigned i;
    for(i=0;i<OZ_ADMISSION_SLOTS;i++)
        if(s->slots[i].job&&s->slots[i].number==ticket.number)break;
    if(ticket.epoch!=s->epoch||i==OZ_ADMISSION_SLOTS)error=ENOENT;
    else {
        struct oz_admitted *job=s->slots[i].job;
        cred_t *caller=oz_packet_security_cred(context);
        if(secpolicy_zfs(caller)&&crgetuid(caller)!=crgetuid(job->caller))error=EACCES;
        else if(p->dp_Type==OZ_ADMISSION_QUERY)
            error=oz_admitted_result(job,caller,(void *)p->dp_Arg2);
        else if(!job->executed)error=EBUSY;
        else {oz_admitted_free(job);s->slots[i].job=NULL;}
    }
    oz_packet_security_end(context);
    return error;
}

#ifdef OZ_TEST_QUEUE_HOLD
/* Disposable test build only. Packaging rejects this marker symbol. */
int oz_test_admission_hold(const struct oz_admitted *job)
{
    return job->request.operation==41 &&
        !strcmp(job->request.target,"qemu_mirror/engine-policy") &&
        !strcmp(job->request.argument,"oz:cancel=must-not-execute");
}
#endif

void oz_admission_server_pump(struct oz_admission_server *s)
{
    if(!s)return;
#ifdef OZ_NATIVE_DISPATCHER
    ioctl_jobs_complete(s);
#endif
    session_reap(s);
    struct Message *message;
    while((message=GetMsg(s->port))) {
        struct StandardPacket *sp=(void *)message;
        if(message->mn_Length!=sizeof(*sp)||
            message->mn_Node.ln_Name!=(void *)&sp->sp_Pkt||sp->sp_Pkt.dp_Link!=message) {
            ReplyMsg(message);continue;
        }
        int error;
#ifdef OZ_NATIVE_DISPATCHER
        int active_ioctls=0;
        for(unsigned j=0;j<OZ_ADMISSION_SLOTS;j++)
            if(s->ioctls[j]&&s->ioctls[j]->snapshot->command!=ZFS_IOC_WAIT&&s->ioctls[j]->snapshot->command!=ZFS_IOC_WAIT_FS)active_ioctls=1;
        if(sp->sp_Pkt.dp_Type==OZ_IOCTL_STREAM_ACTION||
            (sp->sp_Pkt.dp_Type==OZ_IOCTL_ACTION&&active_ioctls)) {
            error=ioctl_job_start(s,&sp->sp_Pkt);
            if(!error)continue; /* worker retains packet until final reply */
        } else
#endif
        error=server_packet(s,&sp->sp_Pkt);
        ReplyPkt(&sp->sp_Pkt,error?DOSFALSE:DOSTRUE,error);
    }
    /* Ordinary management remains owner-task serialized. Threaded ioctl jobs
     * independently retain their packet and private input snapshot. */
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
        if(s->slots[i].job&&!s->slots[i].job->executed) {
#ifdef OZ_NATIVE_DISPATCHER
            int streaming=0;
            for(unsigned j=0;j<OZ_ADMISSION_SLOTS;j++)
                if(s->ioctls[j]&&s->ioctls[j]->snapshot->command!=ZFS_IOC_WAIT&&s->ioctls[j]->snapshot->command!=ZFS_IOC_WAIT_FS)streaming=1;
            if(streaming)continue;
#endif
#ifdef OZ_TEST_QUEUE_HOLD
            if(oz_test_admission_hold(s->slots[i].job))continue;
#endif
            (void)oz_admitted_execute(s->slots[i].job);
        }
}

int oz_admission_server_busy(struct oz_admission_server *s)
{
    if(!s)return 0;
#ifdef OZ_NATIVE_DISPATCHER
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)if(s->ioctls[i])return EBUSY;
#endif
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)
        if(s->sessions[i].cleanup||(s->slots[i].job&&!s->slots[i].job->executed))return EBUSY;
    return 0;
}

int oz_admission_server_close(struct oz_admission_server *s)
{
    if(!s)return 0;
    if(oz_admission_server_busy(s))return EBUSY;
    if(s->task_resource) {
        struct Library *TaskResBase=s->task_resource;
        RemTaskNotifyHook(&s->exit_hook);
    }
    Forbid();RemPort(s->port);Permit();
    struct Message *message;
    while((message=GetMsg(s->port))) {
        struct StandardPacket *sp=(void *)message;
        if(message->mn_Length==sizeof(*sp)&&message->mn_Node.ln_Name==(void *)&sp->sp_Pkt&&sp->sp_Pkt.dp_Link==message)
            ReplyPkt(&sp->sp_Pkt,DOSFALSE,ENOENT);
        else ReplyMsg(message);
    }
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)oz_admitted_free(s->slots[i].job);
#ifdef OZ_NATIVE_DISPATCHER
    DeleteMsgPort(s->completed);
#endif
    DeleteMsgPort(s->port);free(s);return 0;
}
