/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REPORT_COPY_H
#define OZ_REPORT_COPY_H
#include <errno.h>
#include <fcntl.h>
#include <stdint.h>
#include <unistd.h>

/* Copy the completed report from its open descriptor, not a reopened name.
 * Positioned reads preserve the pager cursor. Never truncate a destination.
 * Partial output remains on error; success is not a power-loss guarantee. */
static int oz_report_copy_new(int source,const char *path,uint64_t *copied)
{
    _Static_assert(sizeof(off_t)>=8,"Report export requires 64-bit offsets");
    if(source<0||!path||!*path||!copied)return EINVAL;
    *copied=0;
    int output=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(output<0)return errno;
    char buffer[32768];off_t offset=0;int error=0;
    for(;;){
        ssize_t got=pread(source,buffer,sizeof(buffer),offset);
        if(got<0&&errno==EINTR)continue;
        if(got<0){error=errno;break;}
        if(!got)break;
        if((uint64_t)offset>INT64_MAX-(uint64_t)got){error=EOVERFLOW;break;}
        size_t written=0;
        while(written<(size_t)got){
            ssize_t n=write(output,buffer+written,(size_t)got-written);
            if(n<0&&errno==EINTR)continue;
            if(n<=0){error=n<0?errno:EIO;break;}
            written+=(size_t)n;*copied+=(uint64_t)n;
        }
        if(error)break;
        offset+=got;
    }
    if(close(output)&&!error)error=errno;
    return error;
}
#endif
