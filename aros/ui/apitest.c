#include "api.h"
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
static int failures;
ULONG __stack=131072;
static ULONG run(struct oz_client *c,struct oz_message *m,ULONG expected,const char *label) {
    ULONG rc=oz_submit(c,m);struct oz_message *reply=NULL;
    if(rc==OZ_OK){while(!(reply=oz_poll(c)))Delay(1);rc=reply->status;
        if(rc!=OZ_TIMEOUT && (reply->operation!=m->operation || strcmp(reply->target,m->target) || strcmp(reply->argument,m->argument) || strcmp(reply->options,m->options) || memcmp(&reply->topology,&m->topology,sizeof(m->topology)))) {
            printf("FAIL request fields changed in transit\n");failures++;
        }
        oz_message_free(reply);}
    printf("%s %s expected=%lu got=%lu\n",rc==expected?"PASS":"FAIL",label,(unsigned long)expected,(unsigned long)rc);
    if(rc!=expected)failures++;
    return rc;
}
int main(int argc,char **argv) {
    struct oz_client c;static struct oz_message m;unsigned i;
    if(!oz_client_open(&c))return 20;
    memset(&m,0,sizeof(m));m.operation=1;
    if(argc>1&&!strcmp(argv[1],"ABSENT"))run(&c,&m,OZ_NO_SERVICE,"service absent");
    else if(argc>1&&!strcmp(argv[1],"TIMEOUT")){c.server_name=OZ_TEST_PORT;run(&c,&m,OZ_TIMEOUT,"nonresponding service");}
    else {
        run(&c,&m,OZ_OK,"hello");m.operation=2;
        /* Only run mutation routing checks against the explicit no-engine build. */
        if(oz_submit(&c,&m)==OZ_OK) {
            struct oz_message *r;while(!(r=oz_poll(&c)))Delay(1);
            if(r->status!=OZ_OK || !strstr(r->detail,"engine.available=false")) {oz_message_free(r);oz_client_close(&c);return 20;}
            oz_message_free(r);
        } else {oz_client_close(&c);return 20;}
        m.version=99;run(&c,&m,OZ_BAD_VERSION,"version mismatch");m.version=OZ_VERSION;
        m.operation=9999;run(&c,&m,OZ_UNSUPPORTED,"unknown opcode");
        for(i=2;i<oz_operation_count;i++) {
            m.operation=oz_operations[i].id;m.flags=OZ_CONFIRMED;
            snprintf(m.target,sizeof(m.target),"testpool/test");snprintf(m.argument,sizeof(m.argument),"test-argument");
            snprintf(m.options,sizeof(m.options),"dryrun=true");
            run(&c,&m,oz_operation_unavailable(m.operation)?OZ_UNSUPPORTED:OZ_NO_ENGINE,oz_operations[i].key);
        }
        m.operation=6;m.target[0]=0;run(&c,&m,OZ_BAD_REQUEST,"missing required target");
        snprintf(m.target,sizeof(m.target),"testpool");m.flags=0;run(&c,&m,OZ_BAD_REQUEST,"unconfirmed mutation");
        m.operation=5;m.flags=OZ_CONFIRMED|OZ_DRYRUN;m.argument[0]=m.options[0]=0;
        m.topology.group_count=1;m.topology.device_count=2;m.topology.ashift=12;
        m.topology.groups[0].layout=OZ_MIRROR;
        strcpy(m.topology.devices[0].path,"Work:disks/first disk");
        strcpy(m.topology.devices[1].path,"Work:disks/second disk");
        run(&c,&m,OZ_NO_ENGINE,"typed mirror with spaces survives IPC");
        m.topology.device_count=1;run(&c,&m,OZ_BAD_REQUEST,"incomplete mirror");m.topology.device_count=2;
        strcpy(m.topology.devices[1].path,m.topology.devices[0].path);run(&c,&m,OZ_BAD_REQUEST,"duplicate path");
        strcpy(m.topology.devices[1].path,"Work:disks/second disk");
        m.topology.groups[0].layout=OZ_RAIDZ2;run(&c,&m,OZ_BAD_REQUEST,"incomplete RAIDZ2");
        m.topology.device_count=3;strcpy(m.topology.devices[2].path,"Work:disks/third disk");
        run(&c,&m,OZ_NO_ENGINE,"typed RAIDZ2");
        m.topology.devices[2].group=8;run(&c,&m,OZ_BAD_REQUEST,"invalid group index");m.topology.devices[2].group=0;
        m.topology.groups[0].role=OZ_LOG;run(&c,&m,OZ_BAD_REQUEST,"RAIDZ log rejected");m.topology.groups[0].role=OZ_DATA;
        m.topology.group_count=9;run(&c,&m,OZ_BAD_REQUEST,"group count bound");m.topology.group_count=1;
        m.topology.ashift=8;run(&c,&m,OZ_BAD_REQUEST,"invalid ashift");m.topology.ashift=12;
        strcpy(m.argument,"ambiguous devices");run(&c,&m,OZ_BAD_REQUEST,"mixed raw and typed layout rejected");m.argument[0]=0;
        m.operation=6;run(&c,&m,OZ_BAD_REQUEST,"layout on wrong operation rejected");
    }
    oz_client_close(&c);printf("RESULT failures=%d\n",failures);return failures?20:0;
}
