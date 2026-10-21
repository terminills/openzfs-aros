/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/zfs_onexit.h>
#include <sys/fm/util.h>
#include <aros/debug.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include "../engine/control-file.h"
#include "../engine/file-scope.h"
#include "session-cleanup.h"
#include "stream-channel.h"
#include "../ui/ioctl-api.h"
#include <dos/dosextens.h>
#include <proto/dos.h>
#define CHECK(x) do {if(!(x)){bug("RESULT native control files FAIL line=%d\n",__LINE__);return 20;}}while(0)
static void count(void *arg){++*(unsigned *)arg;}
struct channel_trial {
    struct oz_stream_endpoint endpoint;
    struct Message completed;
    unsigned char *data,*backing;
    size_t length,capacity,used,actual;
    unsigned direction,calls,overreport;
    int callback_error,error;
};
static int channel_io(void *arg,unsigned direction,void *data,size_t size,size_t *actual)
{
    struct channel_trial *t=arg;++t->calls;
    if(t->overreport){*actual=size+1;return 0;}
    *actual=MIN(size,t->capacity-t->used);
    if(direction==OZ_STREAM_READ)memcpy(data,t->backing+t->used,*actual);
    else memcpy(t->backing+t->used,data,*actual);
    t->used+=*actual;return t->callback_error;
}
static void *channel_worker(void *arg)
{
    struct channel_trial *t=arg;
    t->error=t->direction==OZ_STREAM_READ?
        oz_stream_read(&t->endpoint,t->data,t->length,&t->actual):
        oz_stream_write(&t->endpoint,t->data,t->length,&t->actual);
    PutMsg(t->endpoint.peer,&t->completed);
    return NULL;
}
static int stream_channels(void)
{
    struct MsgPort *port=CreateMsgPort();CHECK(port);
    for(unsigned direction=OZ_STREAM_READ;direction<=OZ_STREAM_WRITE;direction++) {
        for(unsigned mode=0;mode<6;mode++) {
            struct channel_trial t={0};
            t.endpoint=(struct oz_stream_endpoint){port,123};
            t.direction=direction;t.length=3*OZ_STREAM_CHUNK+19;
            t.capacity=(mode==1||mode==2)?7:t.length;
            if(mode==5)t.capacity=0;
            t.callback_error=mode==2?ENOSPC:0;t.overreport=mode==4;
            t.data=calloc(1,t.length);t.backing=calloc(1,t.length);
            CHECK(t.data&&t.backing);
            unsigned char *source=direction==OZ_STREAM_READ?t.backing:t.data;
            for(size_t i=0;i<t.length;i++)source[i]=(unsigned char)(i*37+11);
            pthread_t worker;CHECK(!pthread_create(&worker,NULL,channel_worker,&t));
            for(;;) {
                WaitPort(port);struct Message *message=GetMsg(port);
                if(message==&t.completed)break;
                int error=oz_stream_dispatch(message,mode==3?124:123,direction,channel_io,&t);
                CHECK(error==(mode==2?ENOSPC:mode==3?EPROTO:mode==4?EIO:0));
            }
            CHECK(!pthread_join(worker,NULL));
            CHECK(t.error==(mode==2?ENOSPC:mode==3?EPROTO:mode==4?EIO:0));
            size_t expected=mode==0?t.length:mode<3?7:0;
            CHECK(t.actual==expected&&t.used==expected);
            CHECK(t.calls==(mode==0?4:mode==3?0:1));
            CHECK(!memcmp(t.data,t.backing,expected));
            free(t.data);free(t.backing);
        }
    }
    DeleteMsgPort(port);
    bug("RESULT native stream channel PASS directions=2 multichunk=1 short=1 error-progress=1 wrong-operation=1 invalid-count=1 zero-progress=1\n");
    return 0;
}
struct stream_server_trial {
    struct channel_trial *trial;
    struct MsgPort *notify;
    struct Message ready;
    int started,deny;
};
static void *stream_server(void *arg)
{
    struct stream_server_trial *s=arg;
    struct MsgPort *port=CreateMsgPort();
    if(port) {
        port->mp_Node.ln_Name="OpenZFS.Private.Stream.Test";
        Forbid();
        if(!FindPort(port->mp_Node.ln_Name)){AddPort(port);s->started=1;}
        Permit();
    }
    PutMsg(s->notify,&s->ready);
    if(s->started) {
        WaitPort(port);
        struct StandardPacket *p=(void *)GetMsg(port);
        struct channel_trial *t=s->trial;
        int error=EPROTO;
        if(p->sp_Msg.mn_Length==sizeof(*p)&&p->sp_Pkt.dp_Type==OZ_IOCTL_STREAM_ACTION&&
            p->sp_Pkt.dp_Arg2==sizeof(struct oz_ioctl_wire)&&p->sp_Pkt.dp_Arg3==t->direction) {
            t->endpoint=(struct oz_stream_endpoint){p->sp_Pkt.dp_Port,(uint64_t)(uintptr_t)p->sp_Pkt.dp_Link};
            error=s->deny?EACCES:t->direction==OZ_STREAM_READ?
                oz_stream_read(&t->endpoint,t->data,t->length,&t->actual):
                oz_stream_write(&t->endpoint,t->data,t->length,&t->actual);
        }
        t->error=error;
        ReplyPkt(&p->sp_Pkt,error?DOSFALSE:DOSTRUE,error);
        Forbid();RemPort(port);Permit();
    }
    if(port)DeleteMsgPort(port);
    return NULL;
}
static int stream_clients(void)
{
    struct MsgPort *notify=CreateMsgPort();CHECK(notify);
    struct oz_ioctl_wire wire={0};
    struct channel_trial missing={0};
    CHECK(oz_ioctl_stream_request("OpenZFS.Absent.Stream.Test",&wire,sizeof(wire),
        OZ_STREAM_READ,channel_io,&missing)==ENOENT&&!missing.calls);
    for(unsigned direction=OZ_STREAM_READ;direction<=OZ_STREAM_WRITE;direction++)
        for(unsigned mode=0;mode<3;mode++) {
            struct channel_trial t={0};
            t.direction=direction;t.length=2*OZ_STREAM_CHUNK+23;
            t.capacity=mode==1?7:t.length;t.callback_error=mode==1?ENOSPC:0;
            t.data=calloc(1,t.length);t.backing=calloc(1,t.length);CHECK(t.data&&t.backing);
            unsigned char *source=direction==OZ_STREAM_READ?t.backing:t.data;
            for(size_t i=0;i<t.length;i++)source[i]=(unsigned char)(i*13+5);
            struct stream_server_trial s={.trial=&t,.notify=notify,.deny=mode==2};
            pthread_t worker;CHECK(!pthread_create(&worker,NULL,stream_server,&s));
            WaitPort(notify);CHECK(GetMsg(notify)==&s.ready&&s.started);
            int error=oz_ioctl_stream_request("OpenZFS.Private.Stream.Test",&wire,sizeof(wire),
                direction,channel_io,&t);
            CHECK(!pthread_join(worker,NULL));
            CHECK(error==(mode==1?ENOSPC:mode==2?EACCES:0)&&t.error==error);
            size_t expected=mode==0?t.length:mode==1?7:0;
            CHECK(t.actual==expected&&t.used==expected&&!memcmp(t.data,t.backing,expected));
            CHECK(t.calls==(mode==0?3:mode==1?1:0));
            free(t.data);free(t.backing);
        }
    DeleteMsgPort(notify);
    bug("RESULT native stream client PASS directions=2 multichunk=1 partial-error=1 final-refusal=1 absent=1\n");
    return 0;
}
struct stream_sink {
    unsigned char bytes[64];size_t used,capacity;int error,overreport;
};
static int write_sink(void *arg,const void *bytes,size_t length,size_t *done)
{
    struct stream_sink *s=arg;
    if(s->overreport){*done=length+1;return 0;}
    *done=MIN(length,s->capacity-s->used);
    memcpy(s->bytes+s->used,bytes,*done);s->used+=*done;
    return s->error;
}
static int read_source(void *arg,void *bytes,size_t length,size_t *done)
{
    struct stream_sink *s=arg;
    *done=MIN(length,s->capacity-s->used);
    memcpy(bytes,s->bytes+s->used,*done);s->used+=*done;
    return s->error;
}
static int stream_files(void)
{
    struct stream_sink sink={.capacity=64};unsigned released=0;
    zfs_file_t *file=NULL;unsigned char bytes[16];memset(bytes,0x5a,sizeof(bytes));
    CHECK(zfs_file_from_writer(NULL,&sink,&file)==EINVAL&&!file);
    CHECK(zfs_file_from_writer(write_sink,NULL,&file)==EINVAL&&!file);
    CHECK(zfs_file_from_writer(write_sink,&sink,NULL)==EINVAL);
    CHECK(!zfs_file_from_writer(write_sink,&sink,&file));
    CHECK(!oz_file_set_private(file,&released,count));
    CHECK(zfs_file_off(file)==0);
    ssize_t resid=-1;
    CHECK(!zfs_file_write(file,bytes,sizeof(bytes),&resid)&&!resid);
    CHECK(sink.used==16&&!memcmp(sink.bytes,bytes,16)&&zfs_file_off(file)==16);
    sink.capacity=20;
    CHECK(!zfs_file_write(file,bytes,8,&resid)&&resid==4&&zfs_file_off(file)==20);
    CHECK(zfs_file_write(file,bytes,8,NULL)==EIO&&zfs_file_off(file)==20);
    sink.capacity=22;sink.error=ENOSPC;
    CHECK(zfs_file_write(file,bytes,8,&resid)==ENOSPC&&resid==6&&zfs_file_off(file)==22);
    sink.error=0;sink.overreport=1;
    CHECK(zfs_file_write(file,bytes,8,&resid)==EIO&&resid==8&&zfs_file_off(file)==22);
    sink.overreport=0;
    CHECK(zfs_file_read(file,bytes,1,NULL)==EBADF);
    CHECK(zfs_file_pread(file,bytes,1,0,NULL)==EBADF);
    CHECK(zfs_file_pwrite(file,bytes,1,0,0,NULL)==ESPIPE);
    loff_t offset=0;zfs_file_attr_t attr;
    CHECK(zfs_file_seek(file,&offset,SEEK_SET)==ESPIPE);
    CHECK(zfs_file_getattr(file,&attr)==ENOTSUP&&zfs_file_fsync(file,0)==ENOTSUP);
    file->f_stream_offset=INT64_MAX;
    CHECK(zfs_file_write(file,bytes,1,&resid)==EOVERFLOW&&resid==1&&sink.used==22);
    file->f_stream_offset=22;
    struct oz_file_binding binding={17,file};struct oz_file_scope *scope=NULL;
    CHECK(!oz_file_scope_enter(&binding,1,&scope));zfs_file_close(file);
    zfs_file_t *held=zfs_file_get(17);CHECK(held&&!released);
    CHECK(!oz_file_scope_leave(scope)&&!released&&!zfs_file_get(17));
    zfs_file_put(held);CHECK(released==1);
    sink.used=0;sink.capacity=16;
    CHECK(!zfs_file_from_reader(read_source,&sink,&file));
    CHECK(!zfs_file_read(file,bytes,8,&resid)&&!resid&&zfs_file_off(file)==8);
    CHECK(!zfs_file_read(file,bytes,16,&resid)&&resid==8&&zfs_file_off(file)==16);
    CHECK(!zfs_file_read(file,bytes,1,&resid)&&resid==1&&zfs_file_off(file)==16);
    CHECK(zfs_file_seek(file,&offset,SEEK_SET)==ESPIPE);
    zfs_file_close(file);
    bug("RESULT native sequential streams PASS exact=1 short=1 error=1 invalid-count=1 overflow=1 retained=1 reader-offset=1\n");
    return 0;
}
static void free_event(nvlist_t *event,nvlist_t *detector)
{nvlist_free(event);nvlist_free(detector);}
struct worker {zfs_file_t *file;int error;};
static void *finish_control(void *arg)
{
    struct worker *w=arg;zfs_file_t *file=NULL;
    if(oz_control_file_open(&file)!=EDEADLK||file||oz_control_stop()!=EDEADLK)w->error=EIO;
    zfs_zevent_fd_rele(w->file);
    return NULL;
}
int main(void)
{
    CHECK(!stream_clients());
    CHECK(!stream_channels());
    CHECK(!stream_files());
    uint64_t original_limit=zfs_max_nvlist_src_size;
    zfs_max_nvlist_src_size=0;
    CHECK(zfs_max_nvlist_src_size_os()==MIN((uint64_t)AvailMem(MEMF_TOTAL)/4,UINT64_C(128)*1024*1024));
    zfs_max_nvlist_src_size=8192;
    CHECK(zfs_max_nvlist_src_size_os()==8192);
    zfs_max_nvlist_src_size=original_limit;
    unsigned callbacks=0;
    zfs_file_t *file=NULL;
    CHECK(oz_control_file_open(&file)==ENODEV && !file);
    for(unsigned round=0;round<16;round++) {
        fm_init();
        CHECK(oz_control_start()==0);
        CHECK(oz_control_start()==EBUSY);
        struct oz_session_cleanup *session=NULL;
        CHECK(oz_session_cleanup_create(&session)==0);
        int descriptor=-1;minor_t minor=0,event_minor=0;
        int opened=oz_control_file_open(&file);
        if(opened)bug("control open error=%d round=%u\n",opened,round);
        CHECK(opened==0);
        CHECK(zfsdev_getminor(file,&minor)==0 && minor>0);
        zfs_file_t *others[8];minor_t minors[8];
        for(unsigned i=0;i<8;i++) {
            CHECK(oz_control_file_open(&others[i])==0);
            CHECK(zfsdev_getminor(others[i],&minors[i])==0 && minors[i]!=minor);
            for(unsigned j=0;j<i;j++)CHECK(minors[i]!=minors[j]);
        }
        for(unsigned i=0;i<8;i++) {
            zfs_file_close(others[i]);
            CHECK(zfsdev_get_state(minors[i],ZST_ALL)==NULL);
        }
        CHECK(oz_session_file_insert(session,file,&descriptor)==0);
        zfs_file_close(file);
        struct oz_file_scope *scope=NULL;
        CHECK(oz_session_file_scope(session,&scope)==0);
        zfs_file_t *held=zfs_onexit_fd_hold(descriptor,&minor);
        CHECK(held!=NULL);
        CHECK(zfs_onexit_add_cb(minor,count,&callbacks,NULL)==0);
        zfs_zevent_t *stream=NULL;
        zfs_file_t *events=zfs_zevent_fd_hold(descriptor,&event_minor,&stream);
        CHECK(events==held && minor==event_minor && stream!=NULL);
        nvlist_t *event=NULL,*result=NULL;
        CHECK(nvlist_alloc(&event,NV_UNIQUE_NAME,0)==0);
        CHECK(nvlist_add_uint64(event,"aros-test-round",round)==0);
        CHECK(zfs_zevent_post(event,NULL,free_event)==0);
        uint64_t size=1<<20,dropped=0,value=UINT64_MAX;
        CHECK(zfs_zevent_next(stream,&result,&size,&dropped)==0 && !dropped);
        CHECK(nvlist_lookup_uint64(result,"aros-test-round",&value)==0 && value==round);
        nvlist_free(result);result=NULL;
        CHECK(zfs_zevent_next(stream,&result,&size,&dropped)==ENOENT && !result);
        if(round==0){
            /* Exercise upstream's bounded queue with the real retained
             * control cursor parked on the event that will be evicted. */
            for(unsigned sequence=0;sequence<600;sequence++){
                event=NULL;
                CHECK(!nvlist_alloc(&event,NV_UNIQUE_NAME,0));
                CHECK(!nvlist_add_uint64(event,"aros-overflow-sequence",sequence));
                CHECK(!zfs_zevent_post(event,NULL,free_event));
            }
            size=1<<20;dropped=0;
            CHECK(!zfs_zevent_next(stream,&result,&size,&dropped)&&dropped>0);
            CHECK(!nvlist_lookup_uint64(result,"aros-overflow-sequence",&value)&&value==88);
            nvlist_free(result);result=NULL;
            zfs_zevent_t *independent=NULL;zfs_zevent_init(&independent);
            CHECK(independent);
            size=1<<20;dropped=UINT64_MAX;
            CHECK(!zfs_zevent_next(independent,&result,&size,&dropped)&&!dropped);
            CHECK(!nvlist_lookup_uint64(result,"aros-overflow-sequence",&value)&&value==88);
            nvlist_free(result);result=NULL;
            for(unsigned sequence=89;sequence<600;sequence++){
                size=1<<20;dropped=UINT64_MAX;
                CHECK(!zfs_zevent_next(stream,&result,&size,&dropped)&&!dropped);
                CHECK(!nvlist_lookup_uint64(result,"aros-overflow-sequence",&value)&&value==sequence);
                nvlist_free(result);result=NULL;
            }
            CHECK(zfs_zevent_next(stream,&result,&size,&dropped)==ENOENT&&!result);
            uint_t drained=0;zfs_zevent_drain_all(&drained);CHECK(drained==512);
            CHECK(zfs_zevent_next(independent,&result,&size,&dropped)==ENOENT&&!result);
            zfs_zevent_destroy(independent);
            bug("RESULT native event overflow PASS published=600 retained=512 ordered=1 drop-notice=1 reset=1 independent=1 drain=512\n");
        }
        oz_session_cleanup_close(session);
        CHECK(oz_file_scope_leave(scope)==0);
        CHECK(!zfs_onexit_fd_hold(descriptor,&event_minor));
        CHECK(oz_control_stop()==EBUSY && callbacks==round);
        zfs_onexit_fd_rele(held);
        CHECK(oz_control_stop()==EBUSY && callbacks==round);
        struct worker w={.file=events};pthread_t thread;
        CHECK(pthread_create(&thread,NULL,finish_control,&w)==0);
        CHECK(pthread_join(thread,NULL)==0 && !w.error);
        CHECK(callbacks==round+1 && zfsdev_get_state(minor,ZST_ALL)==NULL);
        CHECK(zfs_onexit_add_cb(minor,count,&callbacks,NULL)==EBADF);
        CHECK(oz_control_stop()==0);
        fm_fini();
    }
    bug("RESULT native control files PASS cycles=16 callbacks=16 events=16 retained=1 threads=16 minors=144\n");
    return 0;
}
