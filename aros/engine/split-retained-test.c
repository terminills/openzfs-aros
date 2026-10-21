/* SPDX-License-Identifier: CDDL-1.0 */
/* Private link-only fault fixture. Never part of a normal service build.
 * Execute an actual upstream split, but retain its SPA and report EIO as if
 * its final export failed. No upstream on-disk work is replaced by a stub. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
int __real_spa_vdev_split_mirror(spa_t *,const char *,nvlist_t *,nvlist_t *,boolean_t);
int __wrap_spa_vdev_split_mirror(spa_t *spa,const char *name,nvlist_t *config,
    nvlist_t *props,boolean_t export)
{
    if(strcmp(name,"cli_split_new"))
        return __real_spa_vdev_split_mirror(spa,name,config,props,export);
    int error=__real_spa_vdev_split_mirror(spa,name,config,props,B_FALSE);
    return error?error:EIO;
}
