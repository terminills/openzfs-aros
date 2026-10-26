/* SPDX-License-Identifier: CDDL-1.0 */
#include "simple-health.h"
#include <assert.h>
int main(void)
{
    char text[512],small[2];uint64_t value=42;
    assert(oz_health_counter("role=data read-errors=0 write-errors=3","read-errors",&value)&&value==0);
    assert(oz_health_counter("read-errors=18446744073709551615","read-errors",&value)&&value==UINT64_MAX);
    const char *bad[]={"", "not-read-errors=3", "read-errors=", "read-errors=-1",
        "read-errors=+1", "read-errors=1x", "read-errors=18446744073709551616",
        "read-errors=1 read-errors=1", "read-errors=0 read-errors=2", "read-errors=1\n"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++){
        value=42;assert(!oz_health_counter(bad[i],"read-errors",&value)&&value==42);
    }
    oz_simple_health_row(text,sizeof(text),"pool","tank","ONLINE",
        "role=pool guid=7 read-errors=0 write-errors=0 checksum-errors=0 parity=0");
    assert(strstr(text,"tank (pool): ONLINE")&&strstr(text,"Errors: read 0, write 0, checksum 0"));
    assert(!strstr(text,"guid=")&&!strstr(text,"progress="));
    oz_simple_health_row(text,sizeof(text),"disk","member","DEGRADED",
        "checksum-errors=9 write-errors=2 read-errors=18446744073709551615");
    assert(strstr(text,"DEGRADED")&&strstr(text,"read 18446744073709551615, write 2, checksum 9"));
    oz_simple_health_row(text,sizeof(text),"pool","tank","ONLINE",
        "read-errors=0 write-errors=0 checksum-errors=5");
    assert(strstr(text,"checksum 5")); /* ONLINE does not erase historic errors. */
    oz_simple_health_row(text,sizeof(text),"spare","standby","INACTIVE",
        "configured-only=readonly statistics=unavailable");
    assert(strstr(text,"INACTIVE")&&strstr(text,"counters unavailable")&&!strstr(text,"read 0"));
    oz_simple_health_row(text,sizeof(text),"pool","tank","SUSPENDED","read-errors=0 write-errors=0");
    assert(strstr(text,"SUSPENDED")&&strstr(text,"counters unavailable"));
    oz_simple_health_row(small,sizeof(small),"disk","long-name","FAULTED","");
    assert(small[1]==0);
    puts("Simple health formatting PASS counters=exact missing=unavailable overflow=refused states=preserved");
    return 0;
}
