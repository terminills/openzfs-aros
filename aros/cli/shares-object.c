/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <errno.h>
#include <string.h>
#include "../../lib/libzfs/libzfs_impl.h"
#include "../ui/admission-api.h"
#include "../ui/mount-api.h"
int find_shares_object(differ_info_t *di)
{
    if(!di||!di->zhp||!di->ds){errno=EINVAL;return -1;}
    di->shares=0;
    UQUAD object=0;
    int error=oz_mount_shares_query(OZ_ADMISSION_PORT,di->ds,&object);
    if(error) {
        snprintf(di->errbuf,sizeof(di->errbuf),"cannot query shares object for '%s'",di->ds);
        zfs_error_aux(di->zhp->zfs_hdl,"%s",strerror(error));
        errno=error;
        return zfs_error(di->zhp->zfs_hdl,EZFS_DIFF,di->errbuf);
    }
    di->shares=object;return 0;
}
