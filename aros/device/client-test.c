/* SPDX-License-Identifier: CDDL-1.0 */
/* Private native subprocess fixture; no service or real-device discovery. */
#include <proto/exec.h>
#include <exec/io.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <stdlib.h>
#include <string.h>
struct client_message { struct Message message; ULONG index,phase,error; };
int main(int argc,char **argv)
{
    if(argc!=3)return 20;
    unsigned index=(unsigned)atoi(argv[1]);if(index>=4)return 20;
    struct MsgPort *parent=FindPort((CONST_STRPTR)argv[2]);
    struct MsgPort *reply=CreateMsgPort(),*port=CreateMsgPort();
    if(!parent||!reply||!port)return 20;
    struct client_message result={0};
    result.message.mn_ReplyPort=reply;result.message.mn_Length=sizeof(result);
    result.index=index;
    struct IOStdReq *ios[32]={0};unsigned opened=0;
    const ULONG chunk=8192;
    unsigned char *data=AllocVec(32*chunk,MEMF_PUBLIC);
    if(!data)result.error=1;
    for(unsigned i=0;i<32&&!result.error;i++){
        ios[i]=(void *)CreateIORequest(port,sizeof(*ios[i]));
        if(!ios[i]||OpenDevice((CONST_STRPTR)"openzfs.device",i,(struct IORequest *)ios[i],0))result.error=2;
        else opened++;
    }
    PutMsg(parent,&result.message);WaitPort(reply);GetMsg(reply);
    for(unsigned round=0;round<8&&!result.error;round++){
        for(unsigned i=0;i<32;i++){
            memset(data+i*chunk,1+index*37+i+round,chunk);
            ios[i]->io_Command=CMD_WRITE;ios[i]->io_Data=data+i*chunk;
            ios[i]->io_Length=chunk;ios[i]->io_Offset=655360+index*32768+(round%4)*chunk;
            SendIO((struct IORequest *)ios[i]);
        }
        for(unsigned i=0;i<32;i++)if(WaitIO((struct IORequest *)ios[i])||ios[i]->io_Actual!=chunk)result.error=3;
        /* Drain every submitted request even when one operation fails. */
        if(result.error)break;
        for(unsigned i=0;i<32;i++){ios[i]->io_Command=CMD_UPDATE;SendIO((struct IORequest *)ios[i]);}
        for(unsigned i=0;i<32;i++)if(WaitIO((struct IORequest *)ios[i]))result.error=4;
        if(result.error)break;
        memset(data,0,32*chunk);
        for(unsigned i=0;i<32;i++){ios[i]->io_Command=CMD_READ;SendIO((struct IORequest *)ios[i]);}
        for(unsigned i=0;i<32;i++){
            if(WaitIO((struct IORequest *)ios[i])||ios[i]->io_Actual!=chunk)result.error=5;
            for(unsigned n=0;n<chunk;n++)if(data[i*chunk+n]!=(unsigned char)(1+index*37+i+round))result.error=6;
        }
        if(GetMsg(port))result.error=7;
    }
    for(unsigned i=0;i<32;i++)if(ios[i]){
        if(i<opened)CloseDevice((struct IORequest *)ios[i]);
        DeleteIORequest((struct IORequest *)ios[i]);
    }
    if(data)FreeVec(data);
    DeleteMsgPort(port);
    result.phase=1;PutMsg(parent,&result.message);WaitPort(reply);GetMsg(reply);
    DeleteMsgPort(reply);return result.error?20:0;
}
