/* Native injection test: compile the production fsync body under a private
 * name. Only descriptor lookup and handler replies are controlled here;
 * Flush, DoPkt and errno translation are real AROS services. No disk IO. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include "__fdesc.h"
int oz_test_fsync(int);
static struct FileHandle handle;
static fcb control;
static fdesc descriptor;
static struct MsgPort *handler_port;
static int handler_ready;
static LONG reply_error;
static LONG reply_success;
static unsigned requests;
fdesc *__getfdesc(int fd){return fd==73?&descriptor:NULL;}
static void *handler(void *unused)
{
    (void)unused;
    struct MsgPort *port=CreateMsgPort();
    handler_port=port;__atomic_store_n(&handler_ready,1,__ATOMIC_RELEASE);
    if(!port)return NULL;
    for(;;) {
        WaitPort(port);struct Message *msg;
        while((msg=GetMsg(port))) {
            struct DosPacket *packet=(void *)msg->mn_Node.ln_Name;
            if(packet->dp_Type==ACTION_DIE) {
                ReplyPkt(packet,DOSTRUE,0);DeleteMsgPort(port);return NULL;
            }
            requests++;
            if(packet->dp_Type!=ACTION_FLUSH)
                ReplyPkt(packet,DOSFALSE,ERROR_ACTION_NOT_KNOWN);
            else ReplyPkt(packet,reply_success,reply_error);
        }
    }
}
int main(void)
{
    int error=0;pthread_t thread;
    if(pthread_create(&thread,NULL,handler,NULL))return 20;
    while(!__atomic_load_n(&handler_ready,__ATOMIC_ACQUIRE))Delay(1);
    if(!handler_port){pthread_join(thread,NULL);return 20;}
    handle.fh_Type=handler_port;
    control.handle=MKBADDR(&handle);control.flags=O_WRONLY;
    descriptor.fcb=&control;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    errno=0;CHECK(oz_test_fsync(-1)==-1&&errno==EBADF&&requests==0);
    control.flags=O_RDONLY;
    errno=0;CHECK(oz_test_fsync(73)==-1&&errno==EBADF&&requests==0);
    control.flags=O_WRONLY;handle.fh_Type=NULL;
    errno=0;CHECK(oz_test_fsync(73)==-1&&errno==EBADF&&requests==0);
    handle.fh_Type=handler_port;
    const LONG errors[]={ERROR_ACTION_NOT_KNOWN,0,ERROR_DISK_WRITE_PROTECTED,ERROR_OBJECT_NOT_FOUND};
    /* The existing AROS translator preserves unmapped DOS errors above
     * MAX_ERRNO; it does not currently map write protection to EROFS. */
    const int expected[]={ENOSYS,EIO,MAX_ERRNO+ERROR_DISK_WRITE_PROTECTED,ENOENT};
    for(unsigned i=0;i<4;i++) {
        reply_error=errors[i];reply_success=DOSFALSE;errno=0;
        int rc=oz_test_fsync(73),saved=errno;
        char line[128];snprintf(line,sizeof(line),"Native fsync case=%u result=%d errno=%d expected=%d requests=%u\n",i,rc,saved,expected[i],requests);
        for(char *p=line;*p;p++)RawPutChar(*p);
        CHECK(rc==-1&&saved==expected[i]&&requests==i+1);
    }
    reply_error=0;reply_success=DOSTRUE;
    CHECK(oz_test_fsync(73)==0&&requests==5);
done:
    DoPkt(handler_port,ACTION_DIE,0,0,0,0,0);pthread_join(thread,NULL);
    char line[100];snprintf(line,sizeof(line),"RESULT native fsync error paths %s line=%d\n",error?"FAIL":"PASS",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error?20:0;
}
