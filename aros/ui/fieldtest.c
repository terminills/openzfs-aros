/* Check the catalogue consumed by both the native UI and IPC validator. */
#include "api.h"
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>
int main(void)
{
    static struct oz_message m;
    const unsigned operations[]={7,31,36,40};
    int failures=0;char why[256];
    for(unsigned i=0;i<sizeof(operations)/sizeof(operations[0]);i++) {
        memset(&m,0,sizeof(m));m.magic=OZ_MAGIC;m.version=OZ_VERSION;
        m.msg.mn_Length=sizeof(m);
        m.operation=operations[i];m.flags=OZ_CONFIRMED;
        const struct oz_operation *op=oz_find(m.operation);
        if(!op||!op->target_label[0]||
            (m.operation!=36&&!op->argument_label[0])){failures++;continue;}
        if(oz_validate(&m,why,sizeof(why))!=OZ_BAD_REQUEST||strcmp(why,"Target is required"))failures++;
        strcpy(m.target,"example/dataset");
        ULONG expected=m.operation==36?OZ_OK:OZ_BAD_REQUEST;
        if(oz_validate(&m,why,sizeof(why))!=expected)failures++;
        if(m.operation!=36&&strcmp(why,"Argument is required"))failures++;
        if(m.operation!=36)strcpy(m.argument,"example-argument");
        if(oz_validate(&m,why,sizeof(why))!=OZ_OK)failures++;
        printf("field operation=%u target=%s argument=%s failures=%d\n",
            operations[i],op->target_label,op->argument_label,failures);
    }
    memset(&m,0,sizeof(m));m.magic=OZ_MAGIC;m.version=OZ_VERSION;
    m.msg.mn_Length=sizeof(m);m.operation=3;
    if(!oz_find(3)->target_label[0]||oz_validate(&m,why,sizeof(why))!=OZ_OK)failures++;
    strcpy(m.target,"example");
    if(oz_validate(&m,why,sizeof(why))!=OZ_OK)failures++;
    const struct {
        unsigned operation;
        const char *reason;
    } unavailable[]={
        {45,"Unavailable on AROS: no native NFS or SMB export provider is installed"},
        {46,"Unavailable on AROS: no native NFS or SMB export provider is installed"},
        {71,"Not applicable on AROS: FreeBSD jail namespaces are not present"},
        {72,"Not applicable on AROS: FreeBSD jail namespaces are not present"},
        {73,"Not applicable on AROS: Linux user namespaces are not present"},
        {74,"Not applicable on AROS: Linux user namespaces are not present"}
    };
    for(unsigned i=0;i<sizeof(unavailable)/sizeof(unavailable[0]);i++) {
        memset(&m,0,sizeof(m));m.magic=OZ_MAGIC;m.version=OZ_VERSION;
        m.msg.mn_Length=sizeof(m);m.operation=unavailable[i].operation;
        m.flags=OZ_CONFIRMED;strcpy(m.target,"example/dataset");
        strcpy(m.argument,"platform-namespace");
        if(!oz_find(m.operation)||!oz_operation_unavailable(m.operation)||
            oz_validate(&m,why,sizeof(why))!=OZ_UNSUPPORTED||
            strcmp(why,unavailable[i].reason))failures++;
    }
    if(oz_operation_unavailable(50)!=NULL||!strstr(oz_find(50)->help,"Authenticated upstream snapshot"))failures++;
    unsigned routes[OZ_ROUTE_UNAVAILABLE+1]={0};
    for(unsigned i=0;i<oz_operation_count;i++) {
        memset(&m,0,sizeof(m));m.operation=oz_operations[i].id;
        enum oz_route route=oz_request_route(&m);
        if(route==OZ_ROUTE_INVALID||route>OZ_ROUTE_UNAVAILABLE)failures++;
        else routes[route]++;
    }
    memset(&m,0,sizeof(m));m.operation=7;m.topology.device_count=1;
    if(oz_request_route(&m)!=OZ_ROUTE_AUTHENTICATED||
        routes[OZ_ROUTE_PUBLIC]!=10||routes[OZ_ROUTE_AUTHENTICATED]!=49||
        routes[OZ_ROUTE_PROJECT]!=17||routes[OZ_ROUTE_UNAVAILABLE]!=6)failures++;
    memset(&m,0,sizeof(m));m.operation=29;strcpy(m.target,"/AROS/nvme.device/0/partition/1");
    if(oz_request_route(&m)!=OZ_ROUTE_AUTHENTICATED)failures++;
    strcpy(m.target,"Work:pools/file.vdev");
    if(oz_request_route(&m)!=OZ_ROUTE_PROJECT)failures++;
    memset(&m,0,sizeof(m));m.magic=OZ_MAGIC;m.version=OZ_VERSION;
    m.msg.mn_Length=sizeof(m);m.operation=17;m.flags=OZ_CONFIRMED;
    strcpy(m.target,"example");
    if(oz_request_route(&m)!=OZ_ROUTE_AUTHENTICATED)failures++;
    strcpy(m.options,"recovery=safe;assessment=on");
    if(oz_request_route(&m)!=OZ_ROUTE_PROJECT||oz_validate(&m,why,sizeof(why))!=OZ_OK)failures++;
    strcpy(m.argument,"123");
    if(oz_validate(&m,why,sizeof(why))==OZ_OK)failures++;
    m.argument[0]=0;m.flags|=OZ_DRYRUN;
    if(oz_validate(&m,why,sizeof(why))==OZ_OK)failures++;
    printf("RESULT field schema failures=%d\n",failures);
    bug("RESULT native operation catalogue %s unavailable=6 snapshot=implemented\n",
        failures?"FAIL":"PASS");
    return failures?20:0;
}
