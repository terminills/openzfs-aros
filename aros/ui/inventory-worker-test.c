/* SPDX-License-Identifier: CDDL-1.0 */
/* Native Exec transport/lifetime test, not a substitute for service policy tests. */
#include "admission-worker.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <aros/debug.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int oz_inventory_worker_test(void)
{
    const char *name="OpenZFS.Private.InventoryWorkerTest";
    struct oz_storage_inventory *out=malloc(sizeof(*out)),*saved=malloc(sizeof(*saved));
    struct MsgPort *port=CreateMsgPort();
    if(!out||!saved||!port){free(out);free(saved);if(port)DeleteMsgPort(port);return ENOMEM;}
    port->mp_Node.ln_Name=(char *)name;
    Forbid();int exists=FindPort((CONST_STRPTR)name)!=NULL;
    if(!exists)AddPort(port);
    Permit();
    if(exists){DeleteMsgPort(port);free(out);free(saved);return EEXIST;}
    int failed=0;
    for(unsigned mode=0;mode<12;mode++) {
        struct oz_inventory_worker *worker=NULL;
        memset(out,0xa5,sizeof(*out));*saved=*out;
        int error=oz_inventory_worker_start(name,&worker);
        if(error){failed=1;break;}
        struct Message *message=NULL;
        /* Keep the reply withheld until the worker is provably in flight. */
        while(!(message=GetMsg(port)))Delay(1);
        int operation=123;
        failed|=oz_inventory_worker_poll(worker,out,&operation)!=EAGAIN;
        failed|=operation!=123||memcmp(out,saved,sizeof(*out))!=0;
        failed|=oz_inventory_worker_free(worker)!=EBUSY;
        struct DosPacket *packet=(void *)message->mn_Node.ln_Name;
        failed|=packet->dp_Type!=OZ_STORAGE_INVENTORY_ACTION;
        failed|=packet->dp_Arg2!=sizeof(struct oz_storage_inventory_query);
        struct oz_storage_inventory_query *query=(void *)packet->dp_Arg1;
        if(!mode){query->inventory.version=1;query->inventory.count=1;
            strcpy(query->inventory.units[0].device,"test.device");
            strcpy(query->inventory.units[0].model,"lifetime fixture");
            query->inventory.units[0].unit=7;
        } else if(mode==2){query->inventory.version=1;query->inventory.count=OZ_STORAGE_INVENTORY_MAX+1;
        } else if(mode>=3){query->inventory.version=1;query->inventory.count=1;
            struct oz_storage_unit *u=&query->inventory.units[0];
            strcpy(u->device,"test.device");
            switch(mode){
            case 3:memset(u->device,'x',sizeof(u->device));break;
            case 4:memset(u->model,'x',sizeof(u->model));break;
            case 5:memset(u->serial,'x',sizeof(u->serial));break;
            case 6:u->reserved=1;break;
            case 7:u->unit=UINT32_MAX;break;
            case 8:u->removable=2;break;
            case 9:u->device[0]=0;break;
            case 10:query->inventory.version=2;break;
            case 11:query->inventory.count=2;query->inventory.units[1]=*u;break;
            }
        }
        ReplyPkt(packet,mode==1?DOSFALSE:DOSTRUE,mode==1?EACCES:0);
        while((error=oz_inventory_worker_poll(worker,out,&operation))==EAGAIN)Delay(1);
        failed|=error!=0||operation!=(mode==1?EACCES:mode?EPROTO:0);
        if(mode)failed|=memcmp(out,saved,sizeof(*out))!=0;
        else failed|=out->count!=1||out->units[0].unit!=7||strcmp(out->units[0].device,"test.device");
        failed|=oz_inventory_worker_free(worker)!=0;
    }
    RemPort(port);DeleteMsgPort(port);
    /* Missing endpoint must complete rather than leave a retained worker. */
    struct oz_inventory_worker *worker=NULL;
    int error=oz_inventory_worker_start(name,&worker),operation=0;
    failed|=error!=0;
    if(!error){
        *out=*saved;
        while((error=oz_inventory_worker_poll(worker,out,&operation))==EAGAIN)Delay(1);
        failed|=error!=0||operation!=ENOENT||memcmp(out,saved,sizeof(*out))!=0;
        failed|=oz_inventory_worker_free(worker)!=0;
    }
    free(out);free(saved);
    bug("RESULT native inventory worker %s delayed=1 busy-free=1 success=1 denied=1 malformed=10 absent=1 unchanged=1\n",failed?"FAIL":"PASS");
    return failed?EIO:0;
}
