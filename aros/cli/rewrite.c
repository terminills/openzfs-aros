/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <stdint.h>
#include <sys/stat.h>
#include <sys/fs/zfs.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "../ui/rewrite-api.h"

/* Public AROS/clib2 descriptor extension, not a private fdesc layout. */
extern int __get_default_file(int, long *);

int
oz_rewrite_ioctl(int fd, unsigned long command, void *arg)
{
    zfs_rewrite_args_t *arguments=arg;
    if(command!=ZFS_IOC_REWRITE){errno=ENOTTY;return -1;}
    if(!arguments){errno=EFAULT;return -1;}
    struct stat st;
    if(fstat(fd,&st))return -1;
    if(!S_ISREG(st.st_mode)){errno=ENOTSUP;return -1;}
    long native=0;
    if(__get_default_file(fd,&native)){errno=EBADF;return -1;}
    struct FileHandle *fh=BADDR((BPTR)native);
    if(!fh||!fh->fh_Type){errno=EBADF;return -1;}
    struct oz_rewrite_request request={.version=OZ_REWRITE_VERSION,
        .offset=arguments->off,.length=arguments->len,
        .flags=arguments->flags,.argument=arguments->arg};
    SIPTR result=DoPkt(fh->fh_Type,OZ_ACTION_REWRITE,fh->fh_Arg1,
        (SIPTR)&request,sizeof(request),0,0);
    LONG dos_error=IoErr();
    if(result!=DOSTRUE){
        errno=request.engine_error?request.engine_error:
            dos_error==ERROR_ACTION_NOT_KNOWN?ENOTSUP:EIO;
        return -1;
    }
    return 0;
}
