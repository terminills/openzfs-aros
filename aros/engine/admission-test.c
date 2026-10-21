/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include "admission.h"
static const char endpoint[]="OpenZFS.Admission.QEMU.v1";
struct client {struct Message done;struct MsgPort *done_port;const char *dataset;int error;};
static ULONG login(struct Library *secBase,const char *user)
{
    struct TagItem tags[]={{secT_UserID,(IPTR)user},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    return secLoginA(tags);
}
static ULONG logout(struct Library *secBase)
{
    struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    return secLogoutA(tags);
}
static int result(const struct oz_ticket *ticket,struct oz_message *out)
{
    int error;
    do { error=oz_admission_query(endpoint,ticket,out);if(error==EAGAIN)Delay(1); } while(error==EAGAIN);
    return error;
}
static void *client_task(void *arg)
{
    struct client *c=arg;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    ULONG previous=secBase?secGetTaskOwner(FindTask(NULL)):0;
    int logged=0;
    struct oz_ticket ticket={0},slots[OZ_ADMISSION_SLOTS];
#define CHECK(expr) do {if(!(expr)){c->error=__LINE__;goto done;}} while(0)
    CHECK(secBase&&m&&out);
    CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=37;snprintf(m->target,sizeof(m->target),"%s",c->dataset);
    CHECK(oz_admission_submit(endpoint,m,&ticket)==EINVAL&&!ticket.number);
    m->flags=OZ_CONFIRMED;
    CHECK(oz_admission_submit("OpenZFS.Absent.Test",m,&ticket)==ENOENT);
    CHECK(oz_admission_submit(endpoint,m,&ticket)==0&&ticket.number&&ticket.epoch);
    CHECK(result(&ticket,out)==0&&out->status==OZ_OK&&!strcmp(out->target,c->dataset));
    struct oz_ticket stale=ticket;stale.epoch^=1;
    CHECK(oz_admission_query(endpoint,&stale,out)==ENOENT);
    CHECK(logout(secBase)==previous);logged=0;
    CHECK(oz_admission_query(endpoint,&ticket,out)==EACCES);
    CHECK(oz_admission_release(endpoint,&ticket)==EACCES);
    CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
    CHECK(result(&ticket,out)==0&&out->status==OZ_OK);
    CHECK(oz_admission_release(endpoint,&ticket)==0);
    CHECK(oz_admission_query(endpoint,&ticket,out)==ENOENT);
    CHECK(logout(secBase)==previous);logged=0;
    CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++) {
        CHECK(oz_admission_submit(endpoint,m,&slots[i])==0);
        CHECK(result(&slots[i],out)==0&&out->status==OZ_ENGINE_ERROR);
    }
    CHECK(oz_admission_submit(endpoint,m,&ticket)==EBUSY&&!ticket.number);
    for(unsigned i=0;i<OZ_ADMISSION_SLOTS;i++)CHECK(oz_admission_release(endpoint,&slots[i])==0);
    CHECK(oz_admission_submit(endpoint,m,&ticket)==0&&ticket.number>slots[OZ_ADMISSION_SLOTS-1].number);
    CHECK(result(&ticket,out)==0&&out->status==OZ_ENGINE_ERROR);
    CHECK(oz_admission_release(endpoint,&ticket)==0);
done:
    if(logged&&logout(secBase)!=previous)c->error=EIO;
    free(m);free(out);if(secBase)CloseLibrary(secBase);
    PutMsg(c->done_port,&c->done);return NULL;
#undef CHECK
}
int oz_admission_endpoint_test(const char *dataset)
{
    struct oz_admission_server *server=oz_admission_server_open(endpoint);
    if(!server)return errno?errno:EIO;
    struct oz_admission_server *duplicate=oz_admission_server_open(endpoint);
    if(duplicate){oz_admission_server_close(duplicate);oz_admission_server_close(server);return EIO;}
    if(errno!=EEXIST){oz_admission_server_close(server);return EIO;}
    struct MsgPort *done=CreateMsgPort();
    if(!done){oz_admission_server_close(server);return ENOMEM;}
    struct client c={.done_port=done,.dataset=dataset};
    pthread_t thread;int error=pthread_create(&thread,NULL,client_task,&c);
    if(!error) {
        for(;;) {
            Wait((1UL<<done->mp_SigBit)|oz_admission_server_signals(server));
            oz_admission_server_pump(server);
            if(GetMsg(done))break;
        }
        int joined=pthread_join(thread,NULL);error=joined?joined:c.error;
    }
    int closed=oz_admission_server_close(server);if(!error)error=closed;
    DeleteMsgPort(done);
    char line[100];snprintf(line,sizeof(line),"Native authenticated admission endpoint=%d\n",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
