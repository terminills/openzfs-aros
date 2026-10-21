/* SPDX-License-Identifier: CDDL-1.0 */
#include "stream-channel.h"
#include <proto/exec.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#define STREAM_MAGIC 0x4f5a5354UL
struct stream_message {
    struct Message message;
    uint32_t magic,direction,count,actual;
    uint64_t operation;
    int32_t error;
    unsigned char bytes[OZ_STREAM_CHUNK];
};
static int transfer(struct oz_stream_endpoint *endpoint,unsigned direction,
    void *bytes,size_t count,size_t *actual)
{
    if(!actual)return EINVAL;
    *actual=0;
    if(!endpoint||!endpoint->peer||!endpoint->operation||(!bytes&&count))return EINVAL;
    if(!count)return 0;
    struct MsgPort *reply=CreateMsgPort();
    if(!reply)return ENOMEM;
    struct stream_message *m=calloc(1,sizeof(*m));
    if(!m){DeleteMsgPort(reply);return ENOMEM;}
    int error=0;
    while(*actual<count) {
        size_t chunk=count-*actual;
        if(chunk>OZ_STREAM_CHUNK)chunk=OZ_STREAM_CHUNK;
        m->message.mn_ReplyPort=reply;m->message.mn_Length=sizeof(*m);
        m->magic=STREAM_MAGIC;m->operation=endpoint->operation;
        m->direction=direction;m->count=chunk;m->actual=0;m->error=0;
        memset(m->bytes,0,sizeof(m->bytes));
        if(direction==OZ_STREAM_WRITE)memcpy(m->bytes,(char *)bytes+*actual,chunk);
        PutMsg(endpoint->peer,&m->message);
        /* An in-flight message cannot be freed on an observation timeout. */
        WaitPort(reply);
        struct Message *returned=GetMsg(reply);
        if(returned!=&m->message)abort(); /* private unpublished reply port */
        if(m->message.mn_Length!=sizeof(*m)||m->magic!=STREAM_MAGIC||m->operation!=endpoint->operation||
            m->direction!=direction||m->count!=chunk||m->actual>chunk||m->error<0) {
            error=EPROTO;break;
        }
        if(direction==OZ_STREAM_READ&&m->actual)
            memcpy((char *)bytes+*actual,m->bytes,m->actual);
        *actual+=m->actual;
        error=m->error;
        if(error||m->actual<chunk)break;
    }
    volatile unsigned char *wipe=(void *)m;
    for(size_t i=0;i<sizeof(*m);i++)wipe[i]=0;
    free(m);DeleteMsgPort(reply);return error;
}
int oz_stream_read(void *context,void *bytes,size_t count,size_t *actual)
{return transfer(context,OZ_STREAM_READ,bytes,count,actual);}
int oz_stream_write(void *context,const void *bytes,size_t count,size_t *actual)
{return transfer(context,OZ_STREAM_WRITE,(void *)bytes,count,actual);}
int oz_stream_dispatch(struct Message *message,uint64_t operation,unsigned direction,
    int (*io)(void *,unsigned,void *,size_t,size_t *),void *context)
{
    if(!message)return EINVAL;
    if(message->mn_Length!=sizeof(struct stream_message)) {
        ReplyMsg(message);return EPROTO;
    }
    struct stream_message *m=(void *)message;
    size_t actual=0;
    int error=EPROTO;
    if(io&&operation&&m->magic==STREAM_MAGIC&&m->operation==operation&&
        m->direction==direction&&(direction==OZ_STREAM_READ||direction==OZ_STREAM_WRITE)&&
        m->count&&m->count<=OZ_STREAM_CHUNK&&!m->actual&&!m->error) {
        error=io(context,direction,m->bytes,m->count,&actual);
        if(actual>m->count||error<0){error=EIO;actual=0;}
    }
    m->actual=actual;m->error=error;
    ReplyMsg(message);return error;
}
