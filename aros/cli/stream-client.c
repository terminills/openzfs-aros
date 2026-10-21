/* SPDX-License-Identifier: CDDL-1.0 */
#include "stream-channel.h"
#include "../ui/ioctl-api.h"
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>

int oz_ioctl_stream_request(const char *name,void *storage,size_t length,
    unsigned direction,int (*io)(void *,unsigned,void *,size_t,size_t *),void *context)
{
    if(!name||!*name||!storage||length<sizeof(struct oz_ioctl_wire)||!io||
        (direction!=OZ_STREAM_READ&&direction!=OZ_STREAM_WRITE))return EINVAL;
    struct MsgPort *reply=CreateMsgPort();
    if(!reply)return ENOMEM;
    struct StandardPacket packet={0};
    packet.sp_Msg.mn_Node.ln_Name=(void *)&packet.sp_Pkt;
    packet.sp_Msg.mn_Length=sizeof(packet);
    packet.sp_Pkt.dp_Link=&packet.sp_Msg;
    packet.sp_Pkt.dp_Type=OZ_IOCTL_STREAM_ACTION;
    packet.sp_Pkt.dp_Arg1=(IPTR)storage;
    packet.sp_Pkt.dp_Arg2=length;
    packet.sp_Pkt.dp_Arg3=direction;
    Forbid();
    struct MsgPort *server=FindPort((CONST_STRPTR)name);
    if(server)SendPkt(&packet.sp_Pkt,server,reply);
    Permit();
    int error=ENOENT,stream_error=0;
    if(server)for(;;) {
        WaitPort(reply);
        struct Message *message=GetMsg(reply);
        if(message==&packet.sp_Msg) {
            error=(int)packet.sp_Pkt.dp_Res2;
            if(!error&&packet.sp_Pkt.dp_Res1!=DOSTRUE)error=EIO;
            if(!error)error=stream_error;
            break;
        }
        /* Identity correlates with this retained packet, not a public name.
         * Server admission must authenticate before sending any stream data. */
        int next=oz_stream_dispatch(message,(uint64_t)(uintptr_t)&packet.sp_Msg,
            direction,stream_error?NULL:io,context);
        if(next&&!stream_error)stream_error=next;
        /* Even after I/O failure, wait for final acknowledgment: a mutation
         * may have progressed and server callbacks can still be in flight. */
    }
    DeleteMsgPort(reply);return error;
}
