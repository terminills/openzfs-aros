/* Exercise the actual ADT DiskInfo formatter, not a duplicate calculation. */
#include <exec/types.h>
#include <stdio.h>
#include <string.h>
#include "support.h"
int main(void)
{
    const struct {ULONG blocks,total,bytes,percent;} cases[]={
        {0,0,512,100}, {0,1,512,0}, {1,1,512,100},
        {1,2,512,50}, {1,3,512,33},
        {1737661391U,1737661400U,32768,99},
        {9,1737661400U,32768,0},
        {4294967295U,4294967295U,4096,100},
        {2147483647U,4294967295U,4096,49}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        char buffer[128],suffix[16];
        ULONG got=FormatBlocksSized((STRPTR)buffer,sizeof(buffer),cases[i].blocks,
            cases[i].total,cases[i].bytes,TRUE);
        snprintf(suffix,sizeof(suffix),", %u%%)",(unsigned)cases[i].percent);
        if(got!=cases[i].percent||!strstr(buffer,suffix)) {
            printf("FAIL DiskInfo capacity case=%u expected=%u got=%u text=%s\n",
                i,(unsigned)cases[i].percent,(unsigned)got,buffer);
            return 20;
        }
    }
    puts("PASS DiskInfo capacity boundary cases=9");
    return 0;
}
