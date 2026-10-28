/* SPDX-License-Identifier: CDDL-1.0 */
#include "report-page.h"
#include <stdio.h>
#include <stdlib.h>

#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL line %d\n",__LINE__);return 20;} }while(0)
int main(void)
{
    char path[]="/tmp/openzfs-report-page-XXXXXX";
    int fd=mkstemp(path);CHECK(fd>=0);
    struct oz_report_page page={-1,-1,1};
    char bytes[257];
    for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(char)('A'+i%26);
    CHECK(write(fd,bytes,sizeof(bytes))==sizeof(bytes));CHECK(!close(fd));
    for(unsigned capacity=2;capacity<=258;++capacity){
        char result[258];size_t offset=0;
        CHECK(!oz_report_page_open(&page,path));
        do {
            CHECK(!oz_report_page_next(&page,result,capacity));
            size_t n=strlen(result);CHECK(n<=sizeof(bytes)-offset);
            CHECK(!memcmp(bytes+offset,result,n));offset+=n;
        }while(!page.eof);
        CHECK(offset==sizeof(bytes));
        CHECK(!oz_report_page_next(&page,result,capacity)&&!result[0]);
        oz_report_page_close(&page);
    }
    fd=open(path,O_WRONLY|O_TRUNC);CHECK(fd>=0);CHECK(!close(fd));
    CHECK(!oz_report_page_open(&page,path));
    CHECK(!oz_report_page_next(&page,bytes,sizeof(bytes))&&page.eof&&!bytes[0]);
    oz_report_page_close(&page);
    fd=open(path,O_WRONLY);CHECK(fd>=0);CHECK(write(fd,"a\0b",3)==3);CHECK(!close(fd));
    CHECK(!oz_report_page_open(&page,path));
    CHECK(oz_report_page_next(&page,bytes,2)==EILSEQ&&page.fd==-1);
    CHECK(!oz_report_page_open(&page,path));
    CHECK(oz_report_page_next(&page,bytes,sizeof(bytes))==EILSEQ&&page.fd==-1);
    CHECK(oz_report_page_next(&page,bytes,sizeof(bytes))==EINVAL);
    CHECK(!unlink(path));
    CHECK(oz_report_page_open(&page,path)==ENOENT);
    puts("PASS report pages: capacities=257 exact boundaries, long lines, empty, NUL refusal, missing file and closed descriptor");
    return 0;
}
