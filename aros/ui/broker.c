#include "api.h"
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>

/* Runs on the owning service task. Client transferred this AllocVec message
 * through PutMsg; reply delivery transfers it back, otherwise we release it. */
static void broker_process(struct oz_message *m,int drop,int public_endpoint)
{
    if(m->msg.mn_Length!=sizeof(*m)){FreeVec(m);return;}
    if(!memchr(m->reply_name,0,sizeof(m->reply_name))){oz_message_free(m);return;}
    m->status=oz_validate(m,m->detail,sizeof(m->detail));
    if(m->status==OZ_OK&&public_endpoint){
        const struct oz_operation *op=oz_find(m->operation);
        if(op&&op->mutation){
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"This operation requires authenticated admission; no storage operation was executed.");
        }
    }
    if(m->status==OZ_OK)oz_engine_dispatch(m);
    printf("request=%lu operation=%lu status=%lu\n",(unsigned long)m->id,
        (unsigned long)m->operation,(unsigned long)m->status);fflush(stdout);
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;
    Forbid();
    struct MsgPort *reply=drop?NULL:FindPort((CONST_STRPTR)m->reply_name);
    if(reply)PutMsg(reply,&m->msg);
    Permit();
    if(!reply)oz_message_free(m);
}
/* Legacy fixture tests intentionally exercise the historical engine adapter.
 * The normal public service must use the admission-only mutation boundary. */
void oz_broker_process(struct oz_message *m,int drop){broker_process(m,drop,0);}
void oz_broker_process_public(struct oz_message *m,int drop){broker_process(m,drop,1);}
