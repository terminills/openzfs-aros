/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <sys/zfs_project.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include "../ui/project-api.h"

/* Public AROS/clib2 descriptor extension, not a private fdesc layout. */
extern int __get_default_file(int, long *);

int
oz_project_ioctl(int fd, unsigned long command, void *arg)
{
    zfsxattr_t *attributes=arg;
    if(command!=ZFS_IOC_FSGETXATTR&&command!=ZFS_IOC_FSSETXATTR){errno=ENOTTY;return -1;}
    if(!attributes){errno=EFAULT;return -1;}
    struct oz_project_request request={.version=OZ_PROJECT_VERSION,
        .operation=command==ZFS_IOC_FSGETXATTR?OZ_PROJECT_GET:OZ_PROJECT_SET};
    if(request.operation==OZ_PROJECT_SET){
        if(attributes->fsx_xflags&~FS_XFLAG_PROJINHERIT || attributes->fsx_extsize ||
            attributes->fsx_cowextsize){errno=ENOTSUP;return -1;}
        request.project=attributes->fsx_projid;
        request.inherit=!!(attributes->fsx_xflags&FS_XFLAG_PROJINHERIT);
    }
    struct stat st;
    if(fstat(fd,&st))return -1;
    if(!S_ISREG(st.st_mode)&&!S_ISDIR(st.st_mode)){errno=ENOTSUP;return -1;}
    long native=0;
    if(__get_default_file(fd,&native)){errno=EBADF;return -1;}
    BPTR lock=S_ISDIR(st.st_mode)?DupLock((BPTR)native):DupLockFromFH((BPTR)native);
    if(!lock){errno=EIO;return -1;}
    struct MsgPort *port=((struct FileLock *)BADDR(lock))->fl_Task;
    SIPTR result=DoPkt(port,OZ_ACTION_PROJECT,(SIPTR)lock,(SIPTR)&request,sizeof(request),0,0);
    LONG dos_error=IoErr();
    UnLock(lock);
    if(result!=DOSTRUE){
        errno=request.engine_error?request.engine_error:
            dos_error==ERROR_ACTION_NOT_KNOWN?ENOTSUP:EIO;
        return -1;
    }
    if(request.operation==OZ_PROJECT_GET){
        memset(attributes,0,sizeof(*attributes));
        attributes->fsx_projid=request.project;
        attributes->fsx_xflags=request.inherit?FS_XFLAG_PROJINHERIT:0;
    }
    return 0;
}
