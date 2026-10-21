/* SPDX-License-Identifier: CDDL-1.0 */
/* Private harness only. Seed in-memory statistics, never corrupt disk data. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <proto/exec.h>
#include "../ui/api.h"
int oz_pool_clear(struct oz_message *,cred_t *);
int oz_pool_clear_test(int write)
{
    struct oz_message *m=calloc(1,sizeof(*m));
    if(!m)return ENOMEM;
    m->operation=17;m->flags=OZ_CONFIRMED;
    strcpy(m->target,"qemu_mirror");
    int error=0;
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if(!reader){free(m);return ENOMEM;}
    if(oz_pool_clear(m,reader)!=EPERM)error=EIO;
    oz_cred_release(reader);
    if(!write) {
        if(oz_pool_clear(m,kcred)!=EROFS)error=EIO;
        goto done;
    }
    spa_t *spa=NULL;
    if(!error)error=spa_open(m->target,&spa,FTAG);
    if(error)goto done;
    vdev_t *mirror=spa->spa_root_vdev->vdev_child[0];
    if(mirror->vdev_children!=2){error=EINVAL;goto close;}
    vdev_t *a=mirror->vdev_child[0],*b=mirror->vdev_child[1];
    error=vdev_degrade(spa,a->vdev_guid,VDEV_AUX_ERR_EXCEEDED);
    if(error)goto close;
    if(!a->vdev_degraded||a->vdev_state!=VDEV_STATE_DEGRADED){error=EIO;goto close;}
    mutex_enter(&a->vdev_stat_lock);
    a->vdev_stat.vs_read_errors=7;a->vdev_stat.vs_write_errors=8;
    a->vdev_stat.vs_checksum_errors=9;
    mutex_exit(&a->vdev_stat_lock);
    mutex_enter(&b->vdev_stat_lock);b->vdev_stat.vs_read_errors=11;
    mutex_exit(&b->vdev_stat_lock);
    snprintf(m->argument,sizeof(m->argument),"%llu",(unsigned long long)a->vdev_guid);
    error=oz_pool_clear(m,kcred);
    if(!error&&(a->vdev_stat.vs_read_errors||a->vdev_stat.vs_write_errors||
        a->vdev_stat.vs_checksum_errors||b->vdev_stat.vs_read_errors!=11||
        a->vdev_degraded||a->vdev_state!=VDEV_STATE_HEALTHY))error=EIO;
    if(!error) {
        strcpy(m->argument,"18446744073709551616");
        if(oz_pool_clear(m,kcred)!=EINVAL)error=EIO;
        strcpy(m->argument,"0");if(oz_pool_clear(m,kcred)!=EINVAL)error=EIO;
        strcpy(m->argument,"1");if(oz_pool_clear(m,kcred)!=ENODEV)error=EIO;
        m->argument[0]=0;m->flags|=OZ_FORCE;
        if(oz_pool_clear(m,kcred)!=EINVAL)error=EIO;
        m->flags=OZ_CONFIRMED;strcpy(m->options,"rewind=on");
        if(oz_pool_clear(m,kcred)!=EINVAL)error=EIO;
        m->options[0]=0;
    }
    if(!error)error=oz_pool_clear(m,kcred);
    if(!error&&b->vdev_stat.vs_read_errors)error=EIO;
close:
    spa_close(spa,FTAG);
done:
    free(m);
    char line[100];snprintf(line,sizeof(line),"Native upstream pool/device clear write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
