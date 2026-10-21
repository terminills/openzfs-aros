/* SPDX-License-Identifier: CDDL-1.0 */
/* UI transport fixture only. Never authenticates or executes an operation. */
#include "admission-api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <stdio.h>
static void note(const char *s)
{puts(s);fflush(stdout);for(const char *p=s;*p;p++)RawPutChar(*p);RawPutChar('\n');}
int main(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-ui-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    struct MsgPort *port=CreateMsgPort();if(!port)return 20;
    const char *name="OpenZFS.Management.Admission.Delayed.Test.v1";
    port->mp_Node.ln_Name=(char *)name;
    Forbid();int exists=FindPort((CONST_STRPTR)name)!=NULL;
    if(!exists)AddPort(port);
    Permit();
    if(exists){DeleteMsgPort(port);return 20;}
    note("Delayed UI peer ready; no storage operations");
    WaitPort(port);struct Message *message=GetMsg(port);
    struct DosPacket *packet=(struct DosPacket *)message->mn_Node.ln_Name;
    note("Delayed UI peer holding packet; awaiting release marker");
    do {
        Delay(5);
        marker=Lock((CONST_STRPTR)"SYS:openzfs-ui-release",ACCESS_READ);
    }while(!marker);
    UnLock(marker);
    ReplyPkt(packet,DOSFALSE,ENOTSUP);
    Forbid();RemPort(port);Permit();
    while((message=GetMsg(port))) {
        packet=(struct DosPacket *)message->mn_Node.ln_Name;
        ReplyPkt(packet,DOSFALSE,ENOTSUP);
    }
    DeleteMsgPort(port);
    note("Delayed UI peer returned packet and closed");
    return 0;
}
