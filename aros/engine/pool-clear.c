/* SPDX-License-Identifier: CDDL-1.0 */
/* Native adaptation of zfs_ioc_clear's ZPOOL_NO_REWIND path. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/zio.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"
int oz_pool_import_readmit(spa_t *);
int oz_pool_import_identity_check(spa_t *);

/* Shared by Manager clear and the real upstream ioctl. The caller holds a
 * SPA reference and has checked configuration privilege and MMP activity.
 * Never resume I/O through a stale native binding merely because its path
 * has reappeared. All target/label checks precede vdev_clear/zio_resume. */
int oz_pool_clear_readmit(spa_t *spa,uint64_t guid)
{
    int error=oz_pool_import_identity_check(spa);
    if(error||!spa_suspended(spa))return error;
    spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
    if(guid&&!spa_lookup_by_guid(spa,guid,B_TRUE))error=ENODEV;
    else error=oz_pool_import_readmit(spa);
    spa_config_exit(spa,SCL_CONFIG,FTAG);
    return error;
}

int oz_pool_clear(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(m->flags!=OZ_CONFIRMED||pool_namecheck(m->target,NULL,NULL)||
        m->options[0]||m->topology.group_count||m->topology.device_count||
        m->topology.ashift)return EINVAL;
    uint64_t guid=0;
    if(m->argument[0]) {
        for(const char *p=m->argument;*p;p++) {
            if(*p<'0'||*p>'9'||guid>(UINT64_MAX-(*p-'0'))/10)return EINVAL;
            guid=guid*10+(*p-'0');
        }
        if(!guid)return EINVAL;
    }
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);if(error)return error;
    spa_t *spa=NULL;
    /* Only an already admitted pool: no discovery, implicit import or rewind.
     * Reject readonly before changing the missing-log/open-failure state. */
    spa_namespace_enter(FTAG);
    spa=spa_lookup(m->target);
    if(!spa)error=ENOENT;
    else if(!spa_writeable(spa))error=EROFS;
    else {
        if(spa_get_log_state(spa)==SPA_LOG_MISSING)
            spa_set_log_state(spa,SPA_LOG_CLEAR);
        spa->spa_last_open_failed=0;
    }
    spa_namespace_exit(FTAG);
    if(error)goto done;
    error=spa_open(m->target,&spa,FTAG);
    if(error)goto done;
    if(spa_multihost(spa)&&spa_suspended(spa)&&spa_mmp_remote_host_activity(spa)) {
        error=EREMOTEIO;goto close;
    }
    error=oz_pool_clear_readmit(spa,guid);
    if(error)goto close;
    spa_vdev_state_enter(spa,SCL_NONE);
    vdev_t *vd=guid?spa_lookup_by_guid(spa,guid,B_TRUE):NULL;
    if(guid&&!vd) {
        error=ENODEV;
        (void)spa_vdev_state_exit(spa,NULL,error);
        goto close;
    }
    vdev_clear(spa,vd);
    (void)spa_vdev_state_exit(spa,spa_suspended(spa)?NULL:spa->spa_root_vdev,0);
    if(zio_resume(spa)!=0)error=EIO;
close:
    spa_close(spa,FTAG);
done:
    VERIFY0(oz_cred_pop(scope));
    return error;
}
