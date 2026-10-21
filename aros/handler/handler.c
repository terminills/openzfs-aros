/* SPDX-License-Identifier: CDDL-1.0 */
#define __NOLIBBASE__
#include <exec/memory.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include "../ui/admission-api.h"
#include "../ui/handler-api.h"

static int copy_bstr(char *out,ULONG capacity,BSTR in)
{
    if(!in)return 0;
    const char *text=(const char *)AROS_BSTR_ADDR(in);
    ULONG length;
#ifdef AROS_FAST_BSTR
    /* A native handler has no executable CRT/libc startup. */
    for(length=0;length<capacity&&text[length];length++);
#else
    length=AROS_BSTR_strlen(in);
#endif
    if(!length||length>=capacity)return 0;
    for(ULONG i=0;i<length;i++)out[i]=text[i];
    out[length]=0;return 1;
}
static LONG startup_error(LONG error)
{
    switch(error) {
    case EPERM:case EACCES:return ERROR_WRITE_PROTECTED;
    case ENOMEM:return ERROR_NO_FREE_STORE;
    case ENOENT:case ENXIO:case ENODEV:return ERROR_DEVICE_NOT_MOUNTED;
    case EEXIST:case EBUSY:return ERROR_OBJECT_IN_USE;
    case EROFS:return ERROR_DISK_WRITE_PROTECTED;
    case EINVAL:case ENAMETOOLONG:return ERROR_BAD_NUMBER;
    default:return ERROR_ACTION_NOT_KNOWN;
    }
}

LONG openzfs_handler(struct ExecBase *SysBase)
{
    struct Process *process=(struct Process *)FindTask(NULL);
    struct MsgPort *port=&process->pr_MsgPort;
    WaitPort(port);
    struct Message *message=GetMsg(port);
    struct DosPacket *startup=(void *)message->mn_Node.ln_Name;
    struct DosLibrary *DOSBase=(void *)OpenLibrary((CONST_STRPTR)"dos.library",0);
    if(!DOSBase) {
        /* No DOS calls needed to return the startup failure. */
        struct MsgPort *reply=startup->dp_Port;
        startup->dp_Res1=DOSFALSE;startup->dp_Res2=ERROR_INVALID_RESIDENT_LIBRARY;
        startup->dp_Port=port;PutMsg(reply,startup->dp_Link);return RETURN_FAIL;
    }
    LONG error=ERROR_BAD_NUMBER;
    struct oz_handler_attach query={0};
    query.version=OZ_HANDLER_VERSION;
    query.device=BADDR(startup->dp_Arg3);
    struct FileSysStartupMsg *fssm=(IPTR)startup->dp_Arg2>=64?BADDR(startup->dp_Arg2):NULL;
    struct DosEnvec *env=fssm?BADDR(fssm->fssm_Environ):NULL;
    if(query.device&&fssm&&env&&env->de_TableSize>=DE_CONTROL&&
        env->de_DosType==0x5a465301&&!(fssm->fssm_Flags&~OZ_HANDLER_WRITABLE)&&
        copy_bstr(query.name,sizeof(query.name),query.device->dol_Name)&&
        copy_bstr(query.dataset,sizeof(query.dataset),(BSTR)env->de_Control)) {
        query.flags=fssm->fssm_Flags;
        struct MsgPort *reply=CreateMsgPort();
        error=ERROR_NO_FREE_STORE;
        if(reply) {
            struct StandardPacket request={0};
            request.sp_Msg.mn_Node.ln_Name=(void *)&request.sp_Pkt;
            request.sp_Msg.mn_Length=sizeof(request);
            request.sp_Pkt.dp_Link=&request.sp_Msg;
            request.sp_Pkt.dp_Type=OZ_HANDLER_ATTACH;
            request.sp_Pkt.dp_Arg1=(SIPTR)&query;
            request.sp_Pkt.dp_Arg2=sizeof(query);
            Forbid();
            struct MsgPort *server=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT);
            if(server)SendPkt(&request.sp_Pkt,server,reply);
            Permit();
            error=ERROR_DEVICE_NOT_MOUNTED;
            if(server) {
                WaitPort(reply);
                if(GetMsg(reply)==&request.sp_Msg&&request.sp_Pkt.dp_Res1==DOSTRUE&&
                    !request.sp_Pkt.dp_Res2&&query.endpoint)error=0;
                else error=startup_error(request.sp_Pkt.dp_Res2);
            }
            DeleteMsgPort(reply);
        }
    }
    ReplyPkt(startup,error?DOSFALSE:DOSTRUE,error);
    if(error){CloseLibrary((struct Library *)DOSBase);return RETURN_FAIL;}
    /* RunHandler returns this process port; GetDeviceProc prefers the engine
     * endpoint published in the adopted node. Retain the process port too for
     * direct/fallback users of RunHandler's result.
     * Preserve dp_Port: it identifies the original caller for MuFS/ACLs.
     * Remain resident after detach because old DeviceProc references may
     * still address this bootstrap port. The engine likewise stays resident. */
    for(;;) {
        WaitPort(port);
        while((message=GetMsg(port)))PutMsg(query.endpoint,message);
    }
}
