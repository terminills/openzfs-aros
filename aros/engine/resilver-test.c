/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zfeature.h>
#include <proto/exec.h>
#include "../ui/api.h"
int oz_resilver_restart(struct oz_message *,cred_t *);
int oz_resilver_test(int write)
{
    int error=0;spa_t *spa=NULL;
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return ENOMEM;
    m->operation=19;m->flags=OZ_CONFIRMED;strcpy(m->target,"qemu_mirror");
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if(!reader){error=ENOMEM;goto done;}
    if(oz_resilver_restart(m,reader)!=EPERM)error=EIO;
    oz_cred_release(reader);
    if(error)goto done;
    m->flags|=OZ_FORCE;
    if(oz_resilver_restart(m,kcred)!=EINVAL){error=EIO;goto done;}
    m->flags=OZ_CONFIRMED;strcpy(m->argument,"start");
    if(oz_resilver_restart(m,kcred)!=EINVAL){error=EIO;goto done;}
    m->argument[0]=0;strcpy(m->options,"force=on");
    if(oz_resilver_restart(m,kcred)!=EINVAL){error=EIO;goto done;}
    m->options[0]=0;
    if(!write){if(oz_resilver_restart(m,kcred)!=EROFS)error=EIO;goto done;}
    error=spa_open(m->target,&spa,FTAG);if(error)goto done;
    if(!spa_feature_is_enabled(spa,SPA_FEATURE_RESILVER_DEFER)) {
        if(oz_resilver_restart(m,kcred)!=ENOTSUP){error=EIO;goto done;}
        nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props,"feature@resilver_defer",0);
        error=spa_prop_set(spa,props);fnvlist_free(props);if(error)goto done;
    }
    /* This fixture starts healthy: explicitly establish the no-work case.
     * A subsequent dirty-leaf test is needed to prove real repair restart. */
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    int needed=vdev_resilver_needed(spa->spa_root_vdev,NULL,NULL);
    spa_config_exit(spa,SCL_ALL,FTAG);
    if(needed){error=EBUSY;goto done;}
    error=oz_resilver_restart(m,kcred);
done:
    if(spa)spa_close(spa,FTAG);free(m);
    char line[128];snprintf(line,sizeof(line),"Native resilver restart validation/no-work write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
