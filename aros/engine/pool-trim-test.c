/* SPDX-License-Identifier: CDDL-1.0 */
/* Native admission/capability checks, NOT evidence that discard works. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <proto/exec.h>
#include "../ui/api.h"
int oz_pool_trim(struct oz_message *,cred_t *);
int oz_pool_trim_test(int write)
{
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return ENOMEM;
    int error=0;spa_t *spa=NULL;
    m->operation=21;m->flags=OZ_CONFIRMED;strcpy(m->target,"qemu_mirror");
    strcpy(m->argument,"start");strcpy(m->options,"guids=1");
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if(!reader){error=ENOMEM;goto done;}
    if(oz_pool_trim(m,reader)!=EPERM)error=EIO;
    oz_cred_release(reader);
    const char *bad[]={"", "rate=1", "guids=", "guids=0", "guids=1,1",
        "guids=1;", "guids=1;;rate=2", "guids=1;rate=-1", "guids=1;rate=1K",
        "guids=1;rate=1;rate=2", "guids=1;secure=yes", "guids=1;partial=yes",
        "guids=1;unknown=on", "guids=1;secure=on;secure=off"};
    for(unsigned i=0;!error&&i<sizeof(bad)/sizeof(*bad);i++) {
        strcpy(m->options,bad[i]);if(oz_pool_trim(m,kcred)!=EINVAL)error=EIO;
    }
    strcpy(m->options,"guids=1;rate=18446744073709551616");
    if(!error&&oz_pool_trim(m,kcred)!=ERANGE)error=EIO;
    strcpy(m->options,"guids=1;rate=1");strcpy(m->argument,"suspend");
    if(!error&&oz_pool_trim(m,kcred)!=EINVAL)error=EIO;
    strcpy(m->options,"guids=1");strcpy(m->argument,"start");
    if(!write) {
        if(!error&&oz_pool_trim(m,kcred)!=EROFS)error=EIO;
        goto done;
    }
    if(!error)error=spa_open(m->target,&spa,FTAG);if(error)goto done;
    vdev_t *mirror=spa->spa_root_vdev->vdev_child[0];
    if(mirror->vdev_children!=2){error=EINVAL;goto done;}
    vdev_t *a=mirror->vdev_child[0],*b=mirror->vdev_child[1];
    if(a->vdev_has_trim||b->vdev_has_trim){error=EIO;goto done;}
    const char *actions[]={"start","suspend","cancel"};
    for(unsigned i=0;!error&&i<3;i++) {
        strcpy(m->argument,actions[i]);
        snprintf(m->options,sizeof(m->options),"guids=%llu,%llu%s",
            (unsigned long long)a->vdev_guid,(unsigned long long)b->vdev_guid,
            i==0?";rate=1048576;partial=on;secure=off":"");
        if(oz_pool_trim(m,kcred)!=EINVAL||m->row_count!=2){error=EIO;break;}
        char expected[64];snprintf(expected,sizeof(expected),"error=%d",EOPNOTSUPP);
        for(unsigned j=0;j<2;j++)if(strcmp(m->rows[j].kind,"trim-error")||
            strcmp(m->rows[j].value,expected))error=EIO;
        if(a->vdev_trim_thread||b->vdev_trim_thread||
            a->vdev_trim_state!=VDEV_TRIM_NONE||b->vdev_trim_state!=VDEV_TRIM_NONE)error=EIO;
    }
done:
    if(spa)spa_close(spa,FTAG);free(m);
    char line[128];snprintf(line,sizeof(line),"Native TRIM validation/unsupported capability write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
