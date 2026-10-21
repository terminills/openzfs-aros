/* SPDX-License-Identifier: CDDL-1.0 */
/* Private copied-mirror fixture; initializes only upstream-reported free space. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <proto/exec.h>
#include <unistd.h>
#include "../ui/api.h"
int oz_pool_initialize(struct oz_message *,cred_t *);
int oz_pool_status(struct oz_message *,int *);
int oz_pool_initialize_test(int write,int controls)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*status=calloc(1,sizeof(*status));
    if(!m||!status){free(m);free(status);return ENOMEM;}
    m->operation=20;m->flags=OZ_CONFIRMED;strcpy(m->target,"qemu_mirror");
    strcpy(m->argument,"start");strcpy(m->options,"guids=1");
    int error=0;spa_t *spa=NULL;const char *stage="authorization";
    cred_t *reader=oz_cred_create(77,77,88,0,NULL);
    if(!reader){error=ENOMEM;goto done;}
    if(oz_pool_initialize(m,reader)!=EPERM)error=EIO;
    oz_cred_release(reader);
    if(!write) {
        if(oz_pool_initialize(m,kcred)!=EROFS)error=EIO;
        if(!error&&controls) {
            stage="cold suspended status";
            status->operation=4;strcpy(status->target,m->target);int load=0;
            error=oz_pool_status(status,&load);if(!error)error=load;
            unsigned rows=0;
            for(unsigned i=0;!error&&i<status->row_count;i++) {
                struct oz_row *r=&status->rows[i];
                if(strcmp(r->kind,"initialize"))continue;
                rows++;
                if(strcmp(r->state,"SUSPENDED")||r->total||r->used||
                    !strstr(r->value,"stored-only=readonly counters=unavailable"))error=EIO;
            }
            if(rows!=2)error=EIO;
            if(!error) {
                const char *pass="Native initialization cold suspended state PASS\n";
                for(const char *p=pass;*p;p++)RawPutChar(*p);
            }
        }
        goto done;
    }
    if(!error)error=spa_open(m->target,&spa,FTAG);
    if(error)goto done;
    vdev_t *mirror=spa->spa_root_vdev->vdev_child[0];
    if(mirror->vdev_children!=2){error=EINVAL;goto done;}
    vdev_t *a=mirror->vdev_child[0],*b=mirror->vdev_child[1];
    snprintf(m->options,sizeof(m->options),"guids=%llu,%llu",
        (unsigned long long)a->vdev_guid,(unsigned long long)b->vdev_guid);
    if(controls==2) {
        stage="cold writable suspended state";
        if(a->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            b->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            a->vdev_initialize_thread||b->vdev_initialize_thread){error=EIO;goto done;}
        goto start_initialization;
    }
    stage="cancel inactive";strcpy(m->argument,"cancel");
    if(oz_pool_initialize(m,kcred)!=EINVAL||m->row_count!=2)error=EIO;
    if(error)goto done;
start_initialization:
    stage="start";strcpy(m->argument,"start");
    if(!error)error=oz_pool_initialize(m,kcred);
    if(!error&&controls==1) {
        /* The dedicated runner throttles real QEMU writes, not engine code. */
        stage="suspend";strcpy(m->argument,"suspend");
        error=oz_pool_initialize(m,kcred);
        if(!error&&(a->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            b->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            a->vdev_initialize_thread||b->vdev_initialize_thread))error=EIO;
        if(!error) {
            stage="resume";strcpy(m->argument,"start");
            error=oz_pool_initialize(m,kcred);
        }
        if(!error) {
            stage="cancel";strcpy(m->argument,"cancel");
            error=oz_pool_initialize(m,kcred);
            if(!error&&(a->vdev_initialize_state!=VDEV_INITIALIZE_CANCELED||
                b->vdev_initialize_state!=VDEV_INITIALIZE_CANCELED||
                a->vdev_initialize_thread||b->vdev_initialize_thread))error=EIO;
        }
        if(error)goto done;
        const char *pass="Native initialization active suspend/resume/cancel PASS\n";
        for(const char *p=pass;*p;p++)RawPutChar(*p);
        stage="restart after cancel";strcpy(m->argument,"start");
        error=oz_pool_initialize(m,kcred);
    }
    int completed=0;
    for(unsigned tries=0;!error&&!completed&&tries<600;tries++) {
        completed=1;vdev_t *leaves[]={a,b};
        for(unsigned i=0;i<2;i++) {
            mutex_enter(&leaves[i]->vdev_initialize_lock);
            completed&=leaves[i]->vdev_initialize_state==VDEV_INITIALIZE_COMPLETE&&
                leaves[i]->vdev_initialize_thread==NULL;
            mutex_exit(&leaves[i]->vdev_initialize_lock);
        }
        if(!completed)usleep(100000);
    }
    if(!error&&!completed)error=ETIMEDOUT;
    if(!error) {
        stage="status";
        status->operation=4;strcpy(status->target,m->target);int load=0;
        error=oz_pool_status(status,&load);if(!error)error=load;
        unsigned rows=0;
        for(unsigned i=0;i<status->row_count;i++) {
            struct oz_row *r=&status->rows[i];
            if(strcmp(r->kind,"initialize"))continue;
            rows++;
            char diagnostic[512];snprintf(diagnostic,sizeof(diagnostic),
                "Initialize status name=%s state=%s done=%llu total=%llu progress=%lu %s\n",
                r->name,r->state,(unsigned long long)r->used,(unsigned long long)r->total,
                (unsigned long)r->progress_permille,r->value);
            for(const char *p=diagnostic;*p;p++)RawPutChar(*p);
            if(strcmp(r->state,"COMPLETE")||!r->used||!r->total||
                r->progress_permille!=1000||!strstr(r->value,"errors=0 "))error=EIO;
        }
        if(rows!=2)error=EIO;
    }
    if(error||controls==2)goto done;
    stage="partial uninitialize";strcpy(m->argument,"uninitialize");
    /* A failure must not conceal that the valid leaf has already changed. */
    snprintf(m->options,sizeof(m->options),"guids=%llu,1",
        (unsigned long long)a->vdev_guid);
    if(oz_pool_initialize(m,kcred)!=EINVAL||m->row_count!=1||
        strcmp(m->rows[0].name,"1")||
        !strstr(m->detail,"other devices may have changed")||
        !strstr(m->detail,"GUID 1 error=")||
        a->vdev_initialize_state!=VDEV_INITIALIZE_NONE||
        b->vdev_initialize_state!=VDEV_INITIALIZE_COMPLETE)error=EIO;
    if(error)goto done;
    stage="uninitialize";
    snprintf(m->options,sizeof(m->options),"guids=%llu,%llu",
        (unsigned long long)a->vdev_guid,(unsigned long long)b->vdev_guid);
    if(!error)error=oz_pool_initialize(m,kcred);
    if(!error&&(a->vdev_initialize_state!=VDEV_INITIALIZE_NONE||
        b->vdev_initialize_state!=VDEV_INITIALIZE_NONE))error=EIO;
    if(error)goto done;
    stage="32 device errors";
    strcpy(m->options,"guids=");
    for(unsigned i=1;i<=32;i++) {
        char guid[32];snprintf(guid,sizeof(guid),"%s%u",i==1?"":",",i);
        strlcat(m->options,guid,sizeof(m->options));
    }
    if(oz_pool_initialize(m,kcred)!=EINVAL||m->row_count!=OZ_MAX_ROWS||
        !strstr(m->detail,"32 device error(s)")||
        !strstr(m->detail,"Authenticated initialization control returned errno="))error=EIO;
    for(unsigned i=1;!error&&i<=32;i++) {
        char marker[40];snprintf(marker,sizeof(marker),"GUID %u error=",i);
        if(!strstr(m->detail,marker))error=EIO;
    }
    if(!error&&controls) {
        stage="persist suspended";
        snprintf(m->options,sizeof(m->options),"guids=%llu,%llu",
            (unsigned long long)a->vdev_guid,(unsigned long long)b->vdev_guid);
        strcpy(m->argument,"start");error=oz_pool_initialize(m,kcred);
        if(!error){strcpy(m->argument,"suspend");error=oz_pool_initialize(m,kcred);}
        if(!error&&(a->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            b->vdev_initialize_state!=VDEV_INITIALIZE_SUSPENDED||
            a->vdev_initialize_thread||b->vdev_initialize_thread))error=EIO;
    }
done:
    if(spa)spa_close(spa,FTAG);
    free(m);free(status);
    char line[160];snprintf(line,sizeof(line),"Native upstream initialization progress/uninitialize write=%d result=%d stage=%s\n",write,error,stage);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
