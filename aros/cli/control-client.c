/* SPDX-License-Identifier: CDDL-1.0 */
/* Native client bridge. No engine code or privileged identity substitution. */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <proto/exec.h>
#include "../../lib/libzfs_core/libzfs_core_impl.h"
#include "../ui/ioctl-api.h"
#include "stream-channel.h"
static int estimate_no_io(void *context,unsigned direction,void *bytes,size_t size,size_t *actual)
{
    (void)context;(void)direction;(void)bytes;(void)size;
    *actual=0;return EPROTO;
}
static int stream_io(void *context,unsigned direction,void *bytes,size_t size,size_t *actual)
{
    *actual=0;if(direction!=OZ_STREAM_WRITE&&direction!=OZ_STREAM_READ)return EPROTO;
    ssize_t wrote;
    do{wrote=direction==OZ_STREAM_READ?read(*(int *)context,bytes,size):
        write(*(int *)context,bytes,size);}while(wrote<0&&errno==EINTR);
    /* AROS encodes unmapped DOS errors as MAX_ERRNO + IoErr(). Those are
     * not portable errno values and must not enter the upstream engine. */
    if(wrote<0){int error=errno;return error>0&&error<MAX_ERRNO?error:EIO;}
    *actual=(size_t)wrote;return 0;
}

static pthread_mutex_t connection_lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t connection_idle=PTHREAD_COND_INITIALIZER;
static unsigned concurrent_streams;
static struct oz_ticket session;
static unsigned references;
static boolean_t connected;
static uint64_t connection_generation;
struct history_thread { uint64_t generation; LONG descriptor; };
static pthread_key_t history_key;
static pthread_once_t history_once=PTHREAD_ONCE_INIT;
static int history_key_error;
static void history_thread_close(void *value)
{
    struct history_thread *h=value;
    pthread_mutex_lock(&connection_lock);
    if(connected&&h->generation==connection_generation&&h->descriptor>0)
        (void)oz_control_close(OZ_ADMISSION_PORT,&session,h->descriptor);
    pthread_mutex_unlock(&connection_lock);free(h);
}
static void history_initialize(void)
{history_key_error=pthread_key_create(&history_key,history_thread_close);}
/* Called under connection_lock. A new session invalidates old thread slots;
 * descriptors are never reused within one session. Thread exit releases its
 * slot; session close releases all remaining slots including the main task. */
static int history_descriptor(LONG *out)
{
    int e=pthread_once(&history_once,history_initialize);
    if(e||history_key_error)return e?e:history_key_error;
    struct history_thread *h=pthread_getspecific(history_key);
    if(!h) {
        h=calloc(1,sizeof(*h));if(!h)return ENOMEM;
        e=pthread_setspecific(history_key,h);
        if(e){free(h);return e;}
    }
    if(h->generation!=connection_generation){h->descriptor=0;h->generation=connection_generation;}
    if(!h->descriptor) {
        LONG fd=-1;e=oz_control_open(OZ_ADMISSION_PORT,&session,&fd);
        if(e)return e;
        h->descriptor=fd;
    }
    *out=h->descriptor;return 0;
}
static int result(int error){if(error){errno=error;return -1;}return 0;}

int lzc_open_os(void)
{
    pthread_mutex_lock(&connection_lock);
    int error=0;LONG fd=-1;
    if(!connected) {
        /* The opening process owns the task-exit lease. A short-lived worker
         * cannot silently establish a process-wide connection. Main-process
         * initialization must precede worker use. */
        if(FindTask(NULL)->tc_Node.ln_Type!=NT_PROCESS)error=EDEADLK;
        else if(connection_generation==UINT64_MAX)error=EOVERFLOW;
        else error=oz_session_open(OZ_ADMISSION_PORT,&session);
        if(!error){connected=B_TRUE;++connection_generation;}
    }
    if(!error)error=oz_control_open(OZ_ADMISSION_PORT,&session,&fd);
    if(!error)++references;
    else if(connected&&!references) {
        if(!oz_session_close(OZ_ADMISSION_PORT,&session))connected=B_FALSE;
    }
    pthread_mutex_unlock(&connection_lock);
    return error?result(error):(int)fd;
}
int lzc_close_os(int fd)
{
    pthread_mutex_lock(&connection_lock);
    while(concurrent_streams)pthread_cond_wait(&connection_idle,&connection_lock);
    int error=connected&&references?oz_control_close(OZ_ADMISSION_PORT,&session,fd):EBADF;
    if(!error && !--references) {
        error=oz_session_close(OZ_ADMISSION_PORT,&session);
        if(!error||error==ENOENT){connected=B_FALSE;memset(&session,0,sizeof(session));}
    }
    pthread_mutex_unlock(&connection_lock);return result(error);
}
int lzc_ioctl_fd_os(int fd,unsigned long request,zfs_cmd_t *command)
{
    if(fd<=0)return result(EBADF);
    if(!command||request>UINT32_MAX)return result(EINVAL);
    boolean_t history_read=request==ZFS_IOC_POOL_GET_HISTORY;
    boolean_t error_log=request==ZFS_IOC_ERROR_LOG;
    /* History reads use the existing bounded output region, not an input
     * string and never a caller pointer in the wire command. */
    size_t history=0;
    if(history_read) {
        if(!command->zc_history||!command->zc_history_len||
           command->zc_nvlist_src_size||command->zc_nvlist_conf_size||
           command->zc_nvlist_dst_size)return result(EINVAL);
    } else if(command->zc_history) {
        if(request!=ZFS_IOC_POOL_EXPORT&&request!=ZFS_IOC_POOL_DESTROY)return result(ENOTSUP);
        history=strnlen((const char *)(uintptr_t)command->zc_history,OZ_IOCTL_HISTORY_LIMIT);
        if(history==OZ_IOCTL_HISTORY_LIMIT)return result(E2BIG);
        ++history;
    }
    if((!history_read&&command->zc_history_len)||command->zc_share.z_exportdata||
       command->zc_share.z_sharedata||command->zc_share.z_sharetype||command->zc_share.z_sharemax)return result(ENOTSUP);
    uint64_t src=command->zc_nvlist_src_size,conf=command->zc_nvlist_conf_size,dst=command->zc_nvlist_dst_size;
    if(history_read)dst=command->zc_history_len;
    /* ERROR_LOG uses bookmark counts in zfs_cmd_t, unlike nvlist ioctls.
     * The transport always allocates and bounds bytes. */
    if(error_log) {
        if(dst>UINT64_MAX/sizeof(zbookmark_phys_t))return result(EOVERFLOW);
        dst*=sizeof(zbookmark_phys_t);
    }
#ifdef OZ_TEST_IMPORT_TINY_REPLY
    /* Test build only: exercise the real post-import output-buffer failure. */
    if(request==ZFS_IOC_POOL_IMPORT&&dst>1)dst=1;
#endif
    size_t length=sizeof(struct oz_ioctl_wire)+sizeof(*command);
    const size_t cap=128UL*1024*1024;
    if(src>cap-length)return result(E2BIG);
    length+=(size_t)src;
    if(conf>cap-length)return result(E2BIG);
    length+=(size_t)conf;
    if(history>cap-length)return result(E2BIG);
    length+=history;
    if(dst>cap-length)return result(E2BIG);
    length+=(size_t)dst;
    if((src&&!command->zc_nvlist_src)||(conf&&!command->zc_nvlist_conf)||(!history_read&&dst&&!command->zc_nvlist_dst))return result(EINVAL);
    struct oz_ioctl_wire *w=calloc(1,length);if(!w)return result(ENOMEM);
    w->magic=OZ_IOCTL_WIRE_MAGIC;w->version=OZ_IOCTL_WIRE_VERSION;
    w->command_size=sizeof(*command);w->command=request;w->descriptor=fd;
    w->src_size=src;w->conf_size=conf;w->history_size=history;w->dst_capacity=dst;
    zfs_cmd_t *copy=(void *)(w+1);*copy=*command;
    copy->zc_nvlist_dst_size=dst;
    copy->zc_nvlist_src=copy->zc_nvlist_conf=copy->zc_nvlist_dst=0;
    copy->zc_history=0;
    copy->zc_nvlist_dst_filled=B_FALSE;copy->zc_iflags=0;
    char *input=(void *)(copy+1),*config=input+src,*log=config+conf,*output=log+history;
    if(src)memcpy(input,(const void *)(uintptr_t)command->zc_nvlist_src,src);
    if(conf)memcpy(config,(const void *)(uintptr_t)command->zc_nvlist_conf,conf);
    if(history)memcpy(log,(const void *)(uintptr_t)command->zc_history,history);
    pthread_mutex_lock(&connection_lock);
    int error=connected&&references?0:EBADF;
    LONG history_fd=0;
    if(!error)error=history_descriptor(&history_fd);
    if(!error)w->history_descriptor=history_fd;
    int streamfd=-1;
    boolean_t diff=request==ZFS_IOC_DIFF;
    boolean_t waiting=request==ZFS_IOC_WAIT||request==ZFS_IOC_WAIT_FS;
    if(!error&&diff) {
        if(command->zc_cookie>INT_MAX)error=EBADF;
        else if((streamfd=dup((int)command->zc_cookie))<0)error=errno;
    }
    if(!error&&(request==ZFS_IOC_SEND_NEW||request==ZFS_IOC_RECV_NEW)) {
        nvlist_t *input_nvl=NULL;int32_t original_fd=-1;
        error=nvlist_unpack(input,src,&input_nvl,0);
        if(!error)error=nvlist_lookup_int32(input_nvl,request==ZFS_IOC_RECV_NEW?"input_fd":"fd",&original_fd);
        if(!error&&(streamfd=dup(original_fd))<0)error=errno;
        nvlist_free(input_nvl);
    }
    if(!error){
        w->session=session;
        /* Diff's consumer thread issues object-stat queries on this same
         * connection. Retain its lifetime but do not serialize those queries
         * behind stream delivery. Close waits until this request is replied. */
        if(diff||waiting){++concurrent_streams;pthread_mutex_unlock(&connection_lock);}
        error=(diff||request==ZFS_IOC_SEND_NEW||request==ZFS_IOC_RECV_NEW)?
            oz_ioctl_stream_request(OZ_ADMISSION_PORT,w,length,
                request==ZFS_IOC_RECV_NEW?OZ_STREAM_READ:OZ_STREAM_WRITE,stream_io,&streamfd):
            (waiting||request==ZFS_IOC_SEND_SPACE)?
            oz_ioctl_stream_request(OZ_ADMISSION_PORT,w,length,OZ_STREAM_WRITE,estimate_no_io,NULL):
            oz_ioctl_request(OZ_ADMISSION_PORT,w,length);
        if(diff||waiting){pthread_mutex_lock(&connection_lock);--concurrent_streams;pthread_cond_broadcast(&connection_idle);}
    }
    if(streamfd>=0)close(streamfd);
    pthread_mutex_unlock(&connection_lock);
    if(!error) {
        if(copy->zc_nvlist_src||copy->zc_nvlist_conf||copy->zc_nvlist_dst||copy->zc_iflags||copy->zc_history)error=EPROTO;
        else {
            if(error_log) {
                /* Upstream fills from the end and returns unused slots.
                 * Keep that count for libzfs; never interpret it as bytes. */
                if(copy->zc_nvlist_dst_size>command->zc_nvlist_dst_size)error=EPROTO;
                else if(!w->engine_error) {
                    if(!copy->zc_nvlist_dst_filled)error=EPROTO;
                    else if(dst)memcpy((void *)(uintptr_t)command->zc_nvlist_dst,output,dst);
                }
            } else if(history_read) {
                if(copy->zc_nvlist_dst_filled||copy->zc_history_len>dst)error=EPROTO;
                else if(!w->engine_error&&copy->zc_history_len)
                    memcpy((void *)(uintptr_t)command->zc_history,output,copy->zc_history_len);
                copy->zc_nvlist_dst_size=command->zc_nvlist_dst_size;
            } else if(copy->zc_nvlist_dst_filled) {
                /* Upstream reports the required nvlist size with ENOMEM.
                 * Preserve that size/error for zcmd_expand_dst_nvlist; there
                 * are no complete bytes to copy into the old allocation.
                 * Only the read-only queries below get this retry path
                 * (received properties can exceed the initial 256 KiB);
                 * a completed import must keep its non-retry overflow result. */
                if(copy->zc_nvlist_dst_size>dst){
                    if((request!=ZFS_IOC_DATASET_LIST_NEXT&&request!=ZFS_IOC_SNAPSHOT_LIST_NEXT&&
                        request!=ZFS_IOC_POOL_STATS&&request!=ZFS_IOC_EVENTS_NEXT&&
                        request!=ZFS_IOC_VDEV_GET_PROPS&&request!=ZFS_IOC_OBJSET_STATS&&
                        request!=ZFS_IOC_OBJSET_RECVD_PROPS)||
                        w->engine_error!=ENOMEM)error=EOVERFLOW;
                }
                else if(copy->zc_nvlist_dst_size)memcpy((void *)(uintptr_t)command->zc_nvlist_dst,output,copy->zc_nvlist_dst_size);
            }
            copy->zc_nvlist_src=command->zc_nvlist_src;copy->zc_nvlist_conf=command->zc_nvlist_conf;
            copy->zc_nvlist_dst=command->zc_nvlist_dst;
            copy->zc_history=command->zc_history;
            *command=*copy;
            if(!error)error=w->engine_error;
        }
    }
#ifdef OZ_IMPORT_DIAGNOSTICS
    /* Private diagnostic builds retain the exact failed transport/backend
     * errno before libzfs may terminate on EZFS_UNKNOWN. Never translate it. */
    if(error) {
        char line[384];snprintf(line,sizeof(line),"Native failed ioctl request=%lu target=%.255s error=%d engine=%d\n",
            request,command->zc_name,error,w->engine_error);
        for(const char *p=line;*p;p++)RawPutChar(*p);
    }
    if(request==ZFS_IOC_POOL_TRYIMPORT||request==ZFS_IOC_POOL_IMPORT||request==ZFS_IOC_POOL_STATS) {
        char line[192];snprintf(line,sizeof(line),"Native query ioctl request=%lu error=%d engine=%d conf=%llu dst=%llu returned=%llu filled=%d\n",
            request,error,w->engine_error,(unsigned long long)conf,(unsigned long long)dst,
            (unsigned long long)copy->zc_nvlist_dst_size,copy->zc_nvlist_dst_filled);
        for(const char *p=line;*p;p++)RawPutChar(*p);
    }
#endif
#ifdef OZ_TEST_IMPORT_TINY_REPLY
    if(request==ZFS_IOC_POOL_IMPORT) {
        const char *p=error==EOVERFLOW&&w->engine_error==EOVERFLOW?"TEST native import tiny reply EOVERFLOW\n":
            "RESULT native import tiny reply FAIL unexpected errno\n";
        while(*p)RawPutChar(*p++);
    }
#endif
    oz_ioctl_wire_clear(w,length);free(w);return result(error);
}
