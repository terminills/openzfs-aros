/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

static int scenario, calls;
static ssize_t fault_write(int fd,const void *data,size_t size)
{
    ++calls;
    if(scenario==1&&calls==1){errno=EINTR;return -1;}
    if(scenario==2&&calls==2){errno=ENOSPC;return -1;}
    if(scenario==3)return 0;
    return write(fd,data,size>3?3:size);
}
static int fault_close(int fd)
{
    int rc=close(fd);
    if(scenario==4){errno=EIO;return -1;}
    return rc;
}
#define write fault_write
#define close fault_close
#include "report-save.h"
#undef write
#undef close

int main(void)
{
    char dir[]="/tmp/openzfs-report-fault-XXXXXX",path[256];
    if(!mkdtemp(dir))return 1;
    const char payload[]="report contents\n";
    const int errors[]={0,0,ENOSPC,EIO,EIO};
    int failed=0;
    for(scenario=1;scenario<=4;++scenario){
        snprintf(path,sizeof(path),"%s/case-%d",dir,scenario);calls=0;
        int error=oz_report_save_new(path,payload);
        char bytes[64]={0};int fd=open(path,O_RDONLY);
        ssize_t n=fd<0?-1:read(fd,bytes,sizeof(bytes));
        if(fd>=0)close(fd);
        size_t expected=scenario==2?3:scenario==3?0:strlen(payload);
        failed|=error!=errors[scenario]||n!=(ssize_t)expected;
        failed|=memcmp(bytes,payload,expected)!=0;
        /* Even an incomplete output is preserved and cannot be overwritten. */
        failed|=oz_report_save_new(path,"replacement")!=EEXIST;
        unlink(path);
    }
    rmdir(dir);
    puts(failed?"FAIL report save faults":"PASS report save faults: EINTR retry, short writes, ENOSPC, zero write, close failure, incomplete file retained");
    return failed?1:0;
}
