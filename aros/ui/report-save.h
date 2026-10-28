/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REPORT_SAVE_H
#define OZ_REPORT_SAVE_H
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

/* Never truncate a pre-existing file or follow a destination symlink.
 * The caller chooses the destination; this is not a durability guarantee. */
static int oz_report_save_new(const char *path,const char *text)
{
    if(!path||!*path||!text)return EINVAL;
    int fd=open(path,O_WRONLY|O_CREAT|O_EXCL,0600);
    if(fd<0)return errno;
    size_t remaining=strlen(text);const char *cursor=text;int error=0;
    while(remaining){
        ssize_t count=write(fd,cursor,remaining);
        if(count<0&&errno==EINTR)continue;
        if(count<=0){error=count<0?errno:EIO;break;}
        cursor+=count;remaining-=(size_t)count;
    }
    if(close(fd)&&!error)error=errno;
    /* Keep an incomplete new file on error. Do not unlink by name after
     * closing: another task could have replaced that directory entry. */
    return error;
}
#endif
