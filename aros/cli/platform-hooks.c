/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <libzutil.h>
#include <errno.h>
#include "../../cmd/zpool/zpool_util.h"
#include "platform-hooks.h"
#include <string.h>

boolean_t is_mpath_whole_disk(const char *path)
{
    /* The native catalogue admits exact device/unit/partition identities;
     * it does not expose Linux device-mapper multipath whole disks. */
    (void)path;
    return B_FALSE;
}

void update_vdev_config_dev_sysfs_path(nvlist_t *nv, const char *path,
    const char *key)
{
    /* AROS has no sysfs topology. As on upstream FreeBSD, leave the existing
     * vdev metadata untouched rather than invent a Linux enclosure path. */
    (void)nv;
    (void)path;
    (void)key;
}

int oz_pool_upgrade_notice(zpool_handle_t *pool,FILE *out)
{
    if(!pool||!out){errno=EINVAL;return -1;}
    char bootfs[ZPOOL_MAXPROPLEN];
    if(zpool_get_prop(pool,ZPOOL_PROP_BOOTFS,bootfs,sizeof(bootfs),NULL,B_FALSE))
        return -1;
    if(!strcmp(bootfs,"-"))return 0;
    return fprintf(out,"Pool '%s' has bootfs set to '%s'. Verify that the installed "
        "boot loader supports the enabled pool features before rebooting. "
        "No boot code was changed.\n",zpool_get_name(pool),bootfs)<0?-1:0;
}

void after_zpool_upgrade(zpool_handle_t *pool)
{
    if(oz_pool_upgrade_notice(pool,stdout))
        fprintf(stderr,"Unable to report boot-loader requirements after pool upgrade.\n");
}

int zpool_power_current_state(zpool_handle_t *pool,char *vdev)
{
    /* No native enclosure-slot control backend is registered. Do not infer
     * a slot's power state from drive visibility or fabricate a transition. */
    (void)pool;(void)vdev;
    errno=ENOTSUP;
    return -1;
}

int zpool_power(zpool_handle_t *pool,char *vdev,boolean_t turn_on)
{
    (void)pool;(void)vdev;(void)turn_on;
    errno=ENOTSUP;
    return ENOTSUP;
}
