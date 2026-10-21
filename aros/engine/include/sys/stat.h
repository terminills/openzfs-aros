/* File-vdev bootstrap: never issue Linux block ioctls on AROS devices. */
#ifndef OPENZFS_AROS_STAT_H
#define OPENZFS_AROS_STAT_H
#include <sys/types.h>
/* Skip the Linux SPL stat shim while retaining the SDK's actual ABI. */
#include <aros/posixc/sys/stat.h>
#include <errno.h>
static inline int fstat64_blk(int fd, struct stat64 *st)
{
    if(fstat64(fd,st)<0)return -1;
    if(!S_ISREG(st->st_mode)){errno=ENOTSUP;return -1;}
    return 0;
}
#endif
