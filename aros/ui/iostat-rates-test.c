/* SPDX-License-Identifier: CDDL-1.0 */
#include "iostat-rates.h"
#include "iostat-refresh.h"
#include <aros/debug.h>
#include <proto/dos.h>
int oz_iostat_rates_test(void)
{
    struct oz_rate_history *h=calloc(1,sizeof(*h));
    struct oz_row r={0};char text[256];int error=0;
    if(!h)return 1;
    strcpy(r.kind,"io-bytes");strcpy(r.name,"disk0");strcpy(r.state,"ONLINE");
    strcpy(r.value,"type=disk role=data guid=42 counters=cumulative-since-vdev-load load-guid=91");
    r.total=1000000000;r.used=100;r.available=200;
#define SAMPLE(needle) do {oz_rate_format(h,"pool",&r,text,sizeof(text)); \
    if(!strstr(text,needle)){bug("I/O rate FAIL line=%d: %s\n",__LINE__,text);error=1;goto done;}}while(0)
    SAMPLE("baseline");
    r.total+=2000000000;r.used+=2000;r.available+=4000;
    SAMPLE("read=1000.00 write=2000.00 bytes/s; sample=2000000000 ns");
    SAMPLE("no elapsed time");
    r.total++;r.used=1;SAMPLE("counters reset");
    r.total=1;SAMPLE("counters reset");
    strcpy(r.value,"type=disk role=data guid=42 counters=cumulative-since-vdev-load load-guid=92");
    r.total=4000000000;r.used=5000;SAMPLE("baseline");
    strcpy(r.state,"UNAVAILABLE");SAMPLE("unavailable");
    strcpy(r.state,"ONLINE");r.total++;SAMPLE("baseline");
    strcpy(r.kind,"io-ops");SAMPLE("baseline");
    r.total+=1000000000;r.used+=25;r.available+=50;SAMPLE("read=25.00 write=50.00 operations/s");
    r.value[0]=0;r.total++;SAMPLE("unavailable");
    oz_rate_reset(h);
    strcpy(r.value,"load-guid=wrong");SAMPLE("unavailable");
    strcpy(r.value,"type=disk load-guid=-1");SAMPLE("unavailable");
    strcpy(r.value,"type=disk load-guid=18446744073709551616");SAMPLE("unavailable");
    strcpy(r.value,"type=disk guid=42 load-guid=93");r.used=0;r.available=0;r.total=1;SAMPLE("baseline");
    r.used=~(UQUAD)0;r.available=~(UQUAD)0;r.total=2;SAMPLE("interval read=");
    if(strstr(text,"inf")||strstr(text,"nan")){error=1;goto done;}
    oz_rate_reset(h);
    for(unsigned i=0;i<OZ_RATE_SAMPLES+1;i++){
        snprintf(r.name,sizeof(r.name),"disk%u",i);SAMPLE("baseline");
    }
    strcpy(r.name,"disk0");r.total++;SAMPLE("baseline");
    struct oz_refresh refresh;
    struct oz_io_view view={0};static struct oz_message request;
    strcpy(view.pool,"pool");strcpy(view.options,"histogram=latency");view.cursor=24;
    if(!oz_io_view_matches(&view,35,"pool","","histogram=latency")||
        oz_io_view_matches(&view,35,"pool","","")||
        oz_io_view_matches(&view,35,"pool","","histogram=size")||
        oz_io_view_matches(&view,35,"other","","histogram=latency")||
        oz_io_view_matches(&view,35,"pool","unexpected","histogram=latency")||
        oz_io_view_matches(&view,4,"pool","","histogram=latency")){error=1;goto done;}
    memset(&request,0xff,sizeof(request));oz_io_view_request(&view,&request);
    if(request.operation!=35||request.cursor!=24||request.flags||request.argument[0]||
        strcmp(request.target,"pool")||strcmp(request.options,"histogram=latency")){
        error=1;goto done;
    }
    bug("RESULT native I/O page refresh PASS options=1 cursor=1 edited-query-refused=1 clean-request=1\n");
    if(!oz_refresh_editor_matches(35,"pool","pool","","")||
        oz_refresh_editor_matches(35,"pool","pool","","histogram=latency")||
        oz_refresh_editor_matches(35,"pool","pool","","histogram=size")||
        oz_refresh_editor_matches(35,"pool","pool","unexpected","")||
        oz_refresh_editor_matches(35,"pool","other","","")||
        oz_refresh_editor_matches(35,"","","","")||
        oz_refresh_editor_matches(4,"pool","pool","","")){error=1;goto done;}
    bug("RESULT native I/O refresh editor PASS options=1 argument=1 pool=1 operation=1\n");
    if(!oz_refresh_arm(&refresh,"pool",100)||oz_refresh_due(&refresh,101,0)||
        oz_refresh_due(&refresh,2000100,1)||!oz_refresh_due(&refresh,2000100,0)||
        oz_refresh_due(&refresh,2000100,0)||!oz_refresh_due(&refresh,9000000,0)||
        oz_refresh_due(&refresh,10,0)||oz_refresh_due(&refresh,2000009,0)||
        !oz_refresh_due(&refresh,2000010,0)){error=1;goto done;}
    oz_refresh_stop(&refresh);
    if(oz_refresh_due(&refresh,9999999,0)||oz_refresh_arm(&refresh,"",100)||
        oz_refresh_arm(&refresh,"pool",~(UQUAD)0)){error=1;goto done;}
    bug("RESULT native I/O refresh scheduler PASS busy=1 rollback=1 no-catchup=1 stop=1\n");
    bug("RESULT native I/O rate arithmetic PASS delta=1 reset=1 generation=1 unavailable=1 eviction=1 overflow=1\n");
done:
    free(h);return error;
#undef SAMPLE
}
