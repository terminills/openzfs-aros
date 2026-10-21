/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <sys/stat.h>
#include <errno.h>
#include <unistd.h>
#include "../../cmd/zpool/zpool_util.h"

int zfs_dev_flush(int fd)
{
    struct stat64 st;
    if(fstat64(fd,&st))return -1;
    /* Native Exec device descriptors are service capabilities, not POSIX
     * descriptors. Do not pretend a DOS file flush invalidates a device's
     * cache. The file path uses the CRT's Flush + ACTION_FLUSH boundary. */
    if(!S_ISREG(st.st_mode)){errno=ENOTSUP;return -1;}
    return fsync(fd);
}

int check_file(const char *path,boolean_t force,boolean_t isspare)
{
    if(!path||!*path||!g_zfs){errno=EINVAL;return -1;}
    struct stat64 st;
    if(stat64(path,&st))return -1;
    if(!S_ISREG(st.st_mode)){errno=EINVAL;return -1;}
    /* Use real upstream label and active-pool checks. No label clearing or
     * create occurs here; the caller must still perform its actual operation. */
    return check_file_generic(path,force,isspare);
}
