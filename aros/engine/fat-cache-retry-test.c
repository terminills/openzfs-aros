/* Native regression against actual FAT cache.c; only AccessDisk is injected.
 * No device/file IO and no user volumes. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include "cache.h"
struct fixture { unsigned attempts, fail_at; unsigned char disk[64*512]; };
LONG AccessDisk(BOOL write,ULONG num,ULONG count,ULONG size,UBYTE *data,APTR arg)
{
    struct fixture *f=arg;
    if(size!=512||num+count>64)return ERROR_BAD_NUMBER;
    if(write) {
        if(++f->attempts==f->fail_at)return ERROR_WRITE_PROTECTED;
        memcpy(f->disk+num*size,data,count*size);
    } else memcpy(data,f->disk+num*size,count*size);
    return 0;
}
static unsigned dirty_count(struct Cache *c)
{
    unsigned n=0;
    for(struct MinNode *p=c->dirty_list.mlh_Head;p->mln_Succ;p=p->mln_Succ) {
        if(++n>2)return 99;
    }
    return n;
}
int main(void)
{
    int error=0;struct fixture fixture={0};
    struct Cache *cache=Cache_CreateCache(&fixture,4,2,512,SysBase,DOSBase);
    if(!cache)return 20;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    for(unsigned fail=1;fail<=2;fail++) {
        memset(fixture.disk,0,sizeof(fixture.disk));
        fixture.attempts=0;fixture.fail_at=fail;
        for(unsigned i=0;i<2;i++) {
            UBYTE *data=NULL;APTR block=Cache_GetBlock(cache,i*32,&data);
            CHECK(block&&data);memset(data,0x31+i+fail,512);
            Cache_MarkBlockDirty(cache,block);Cache_FreeBlock(cache,block);
        }
        CHECK(dirty_count(cache)==2);
        CHECK(!Cache_Flush(cache)&&IoErr()!=0);
        CHECK(fixture.attempts==fail&&dirty_count(cache)==3-fail);
        fixture.fail_at=0;
        CHECK(Cache_Flush(cache)&&IoErr()==0&&dirty_count(cache)==0);
        CHECK(fixture.attempts==3);
        for(unsigned i=0;i<2;i++)for(unsigned j=0;j<512;j++)
            CHECK(fixture.disk[i*32*512+j]==0x31+i+fail);
    }
done:
    fixture.fail_at=0;Cache_DestroyCache(cache);
    char line[100];snprintf(line,sizeof(line),"RESULT native FAT dirty retry %s line=%d\n",error?"FAIL":"PASS",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error?20:0;
}
