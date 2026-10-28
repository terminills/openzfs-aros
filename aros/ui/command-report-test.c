/* SPDX-License-Identifier: CDDL-1.0 */
/* Private native fixture: exercise the real internal command capture path. */
#include "project-worker.c"
#include "report-page.h"
#define CHECK(c) do { if(!(c)){printf("FAIL command report line=%d\n",__LINE__);return 20;} }while(0)
int main(void)
{
    for(unsigned cycle=0;cycle<3;++cycle){
        struct oz_message response={0};
        char *args[]={"C:OpenZFSReportChild","large",NULL};
        CHECK(!command_capture(&response,args));
        CHECK(response.status==OZ_ENGINE_ERROR&&response.row_count==1&&!response.next_cursor);
        CHECK(!strcmp(response.rows[0].kind,"command-report"));
        CHECK(strstr(response.detail,"exit=23")&&strstr(response.detail,"Full output:"));
        struct oz_report_page page={-1,-1,1};
        CHECK(!oz_report_page_open(&page,response.rows[0].name));
        char text[4096];size_t total=0;unsigned pages=0;
        const char tail[]="\nreport-tail\n";
        do {
            CHECK(!oz_report_page_next(&page,text,sizeof(text)));++pages;
            for(size_t i=0;i<strlen(text);++i,++total){
                CHECK(total<131072+sizeof(tail)-1);
                CHECK(text[i]==(total<131072?(char)('A'+total%26):tail[total-131072]));
            }
        }while(!page.eof);
        CHECK(total==131072+sizeof(tail)-1&&pages>1);
        oz_report_page_close(&page);
        CHECK(!unlink(response.rows[0].name));
        printf("PASS command report cycle=%u bytes=%lu pages=%u exit=23 retained=1\n",cycle,(unsigned long)total,pages);
    }
    puts("PASS native command reports cycles=3 exact-read=1 no-command-rerun=1");
    return 0;
}
