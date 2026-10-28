/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int mode,reads,writes;
static ssize_t fault_pread(int fd,void *buffer,size_t size,off_t offset)
{
    ++reads;
    if(mode==1&&reads==1){errno=EINTR;return -1;}
    if(mode==3){errno=EIO;return -1;}
    return pread(fd,buffer,size>7?7:size,offset);
}
static ssize_t fault_write(int fd,const void *buffer,size_t size)
{
    ++writes;
    if(mode==1&&writes==1){errno=EINTR;return -1;}
    if(mode==2&&writes==2){errno=ENOSPC;return -1;}
    if(mode==4)return 0;
    return write(fd,buffer,size>3?3:size);
}
static int fault_close(int fd)
{
    int result=close(fd);if(mode==5){errno=EIO;return -1;}return result;
}
#define pread fault_pread
#define write fault_write
#define close fault_close
#include "report-copy.h"
#undef pread
#undef write
#undef close
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL copy fault line=%d\n",__LINE__);return 20;} }while(0)
int main(void)
{
    char dir[]="/tmp/openzfs-report-copy-XXXXXX",source[256],dest[256];
    CHECK(mkdtemp(dir));snprintf(source,sizeof(source),"%s/source",dir);
    int fd=open(source,O_RDWR|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    const char bytes[]="complete report output\n";
    CHECK(write(fd,bytes,sizeof(bytes)-1)==sizeof(bytes)-1);
    CHECK(lseek(fd,4,SEEK_SET)==4);
    const int errors[]={0,0,ENOSPC,EIO,EIO,EIO};
    for(mode=1;mode<=5;++mode){
        snprintf(dest,sizeof(dest),"%s/case-%d",dir,mode);reads=writes=0;
        uint64_t copied=0;
        CHECK(oz_report_copy_new(fd,dest,&copied)==errors[mode]);
        size_t expected=mode==2?3:(mode==3||mode==4)?0:sizeof(bytes)-1;
        CHECK(copied==expected&&lseek(fd,0,SEEK_CUR)==4);
        char actual[64]={0};int output=open(dest,O_RDONLY);CHECK(output>=0);
        CHECK(read(output,actual,sizeof(actual))==(ssize_t)expected);
        CHECK(!memcmp(actual,bytes,expected));CHECK(!close(output));
        CHECK(oz_report_copy_new(fd,dest,&copied)==EEXIST);
        CHECK(!unlink(dest));
    }
    CHECK(!close(fd));CHECK(!unlink(source));CHECK(!rmdir(dir));
    puts("PASS report copy faults: interrupted and short I/O, read error, ENOSPC, zero write, close failure, partial retention");
    return 0;
}
