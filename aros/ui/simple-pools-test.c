/* SPDX-License-Identifier: CDDL-1.0 */
#include "simple-pools.h"
#include <assert.h>
int main(void)
{
    char text[512],small[2];
    oz_simple_bytes(text,sizeof(text),0);assert(!strcmp(text,"0 B"));
    oz_simple_bytes(text,sizeof(text),1023);assert(!strcmp(text,"1023 B"));
    oz_simple_bytes(text,sizeof(text),1536);assert(!strcmp(text,"1.5 KiB"));
    oz_simple_bytes(text,sizeof(text),UINT64_MAX);assert(!strcmp(text,"15.9 EiB"));
    oz_simple_pool_row(text,sizeof(text),"tank","ONLINE",1024,3072,4096);
    assert(strstr(text,"4.0 KiB")&&strstr(text,"1.0 KiB")&&strstr(text,"3.0 KiB"));
    oz_simple_pool_row(text,sizeof(text),"tank","DEGRADED",1024,3072,4096);
    assert(strstr(text,"DEGRADED"));
    oz_simple_pool_row(text,sizeof(text),"tank","UNAVAILABLE",0,0,0);
    assert(strstr(text,"Capacity unavailable")&&!strstr(text,"0 B"));
    oz_simple_pool_row(text,sizeof(text),"tank","ONLINE",2,0,1);
    assert(strstr(text,"Capacity unavailable"));
    oz_simple_pool_row(text,sizeof(text),"tank","ONLINE",1,1,1);
    assert(strstr(text,"Capacity unavailable"));
    oz_simple_pool_row(small,sizeof(small),"tank","ONLINE",0,0,0);
    assert(small[1]==0);
    oz_simple_dataset_row(text,sizeof(text),"filesystem","tank/files","writable mount WORK:",1536,2048,1);
    assert(strstr(text,"tank/files (filesystem)")&&strstr(text,"WORK:")&&strstr(text,"1.5 KiB"));
    assert(!strstr(text,"used=")&&!strstr(text,"progress="));
    assert(strstr(text,"Used: 2.0 KiB"));
    oz_simple_dataset_row(text,sizeof(text),"volume","tank/disk","writable, not mounted",0,0,1);
    assert(strstr(text,"Available: 0 B")&&strstr(text,"not mounted"));
    assert(strstr(text,"Used: 0 B"));
    oz_simple_dataset_row(text,sizeof(text),"filesystem","old/files","read-only",1536,0,0);
    assert(!strstr(text,"Used:")&&strstr(text,"Available: 1.5 KiB"));
    oz_simple_dataset_row(text,sizeof(text),"snapshot","tank/files@save","read-only",0,2048,1);
    assert(strstr(text,"read-only")&&!strstr(text,"Available:"));
    oz_simple_dataset_row(small,sizeof(small),"filesystem","tank","read-only",UINT64_MAX,UINT64_MAX,1);
    assert(small[1]==0);
    puts("Simple pool and dataset formatting PASS");return 0;
}
