/* SPDX-License-Identifier: CDDL-1.0 */
/* Per-pool file-open admission, independent of the opening worker thread.
 * This is pathname admission, not file identity or exclusive-write ownership. */
#include <sys/zfs_context.h>
#include <sys/aros_vdev_policy.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include "device-io.h"
struct oz_vdev_policy {
    struct oz_vdev_policy *next;
    uint64_t guid;
    uint64_t pending_guid;
    char name[256];
    unsigned count,accepted,rejected;
    char *paths[32];
    char *pending_path;
};
static pthread_mutex_t policy_lock=PTHREAD_MUTEX_INITIALIZER;
static struct oz_vdev_policy *policies;
static void destroy(struct oz_vdev_policy *p)
{for(unsigned i=0;i<p->count;i++)free(p->paths[i]);free(p);}
static int begin(uint64_t guid,const char *name,char *const *paths,unsigned count,
    struct oz_vdev_policy **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if((!guid&&(!name||!name[0]))||(name&&strlen(name)>=256)||!paths||!count||count>32)return EINVAL;
    struct oz_vdev_policy *p=calloc(1,sizeof(*p));if(!p)return ENOMEM;
    p->guid=guid;p->count=count;
    if(name)strlcpy(p->name,name,sizeof(p->name));
    for(unsigned i=0;i<count;i++) {
        if(!paths[i]||paths[i][0]!='/') {destroy(p);return EINVAL;}
        if(!strncmp(paths[i],"/AROS/",6)) {
            struct oz_device *device=NULL;
            int error=oz_device_bound_open(paths[i],0,&device);
            oz_device_close(device);
            /* ENODEV is an explicitly bound unavailable member. ENOENT
             * remains unadmitted, not implicit degraded-recovery permission. */
            if(error&&error!=ENODEV){destroy(p);return EINVAL;}
        }
        p->paths[i]=strdup(paths[i]);if(!p->paths[i]){destroy(p);return ENOMEM;}
    }
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *q=policies;q;q=q->next)if((guid&&(q->guid==guid||q->pending_guid==guid))||
        (name&&q->name[0]&&!strcmp(name,q->name))) {
        pthread_mutex_unlock(&policy_lock);destroy(p);return EBUSY;
    }
    p->next=policies;policies=p;
    pthread_mutex_unlock(&policy_lock);*out=p;return 0;
}
int oz_vdev_policy_begin(uint64_t guid,char *const *paths,unsigned count,
    struct oz_vdev_policy **out)
{return begin(guid,NULL,paths,count,out);}
int oz_vdev_policy_begin_named(const char *name,char *const *paths,unsigned count,
    struct oz_vdev_policy **out)
{return begin(0,name,paths,count,out);}
struct oz_vdev_extension {
    struct oz_vdev_extension *next;
    struct oz_vdev_policy *policy;
    char *path;
};
int oz_vdev_policy_extend(uint64_t guid,const char *name,const char *path,struct oz_vdev_extension **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!path||path[0]!='/')return EINVAL;
    if(!strncmp(path,"/AROS/",6)) {
        /* Expansion may admit only an already reserved, available native
         * extent. Unlike degraded import, an unavailable member is invalid.
         * Do not hold policy_lock while consulting the device registry. */
        struct oz_device *device=NULL;
        int bound=oz_device_bound_open(path,0,&device);
        oz_device_close(device);
        if(bound)return EINVAL;
    }
    int error=0;struct oz_vdev_extension *changes=NULL;
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *p=policies;p;p=p->next)if((guid&&(p->guid==guid||p->pending_guid==guid))||
        (name&&p->name[0]&&!strcmp(name,p->name))) {
        if(p->pending_path){error=EBUSY;break;}
        unsigned i;for(i=0;i<p->count;i++)if(!strcmp(path,p->paths[i]))break;
        if(i<p->count)continue;
        if(p->count==32){error=ENOSPC;break;}
        struct oz_vdev_extension *e=calloc(1,sizeof(*e));
        if(!e){error=ENOMEM;break;}
        e->path=strdup(path);
        if(!e->path){free(e);error=ENOMEM;break;}
        e->policy=p;e->next=changes;changes=e;
    }
    if(!error)for(struct oz_vdev_extension *e=changes;e;e=e->next)
        e->policy->paths[e->policy->count++]=e->path;
    pthread_mutex_unlock(&policy_lock);
    if(error)while(changes){struct oz_vdev_extension *e=changes;changes=e->next;free(e->path);free(e);}
    else *out=changes;
    return error;
}
/* Same service-task transaction; policies cannot be removed before completion. */
void oz_vdev_policy_extend_end(struct oz_vdev_extension *changes,int error)
{
    pthread_mutex_lock(&policy_lock);
    while(changes) {
        struct oz_vdev_extension *e=changes;changes=e->next;
        struct oz_vdev_policy *p=e->policy;
        if(error)for(unsigned i=0;i<p->count;i++)if(p->paths[i]==e->path) {
            free(e->path);p->paths[i]=p->paths[--p->count];break;
        }
        free(e);
    }
    pthread_mutex_unlock(&policy_lock);
}
int oz_vdev_policy_check(uint64_t guid,const char *path)
{return oz_vdev_policy_check_named(guid,NULL,path);}
struct oz_vdev_repath {
    struct oz_vdev_repath *next;
    struct oz_vdev_policy *policy;
    unsigned index;
    char *replacement;
};
int oz_vdev_policy_repath_begin(uint64_t guid,const char *name,const char *oldpath,
    const char *newpath,struct oz_vdev_repath **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if((!guid&&(!name||!name[0]))||!oldpath||!newpath||oldpath[0]!='/'||
        newpath[0]!='/'||!strcmp(oldpath,newpath))return EINVAL;
    if(!strncmp(newpath,"/AROS/",6)) {
        struct oz_device *device=NULL;
        int error=oz_device_bound_open(newpath,0,&device);
        oz_device_close(device);
        if(error)return error;
    }
    int error=0;
    struct oz_vdev_repath *changes=NULL;
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *p=policies;p;p=p->next)if((guid&&(p->guid==guid||p->pending_guid==guid))||
        (name&&p->name[0]&&!strcmp(name,p->name))) {
        if(p->pending_path||p->pending_guid){error=EBUSY;break;}
        unsigned old_index=p->count;
        for(unsigned i=0;i<p->count;i++) {
            if(!strcmp(newpath,p->paths[i])){error=EEXIST;break;}
            if(!strcmp(oldpath,p->paths[i])) {
                if(old_index!=p->count){error=EINVAL;break;}
                old_index=i;
            }
        }
        if(error)break;
        if(old_index==p->count){error=ENOENT;break;}
        struct oz_vdev_repath *r=calloc(1,sizeof(*r));
        if(!r){error=ENOMEM;break;}
        r->replacement=strdup(newpath);
        if(!r->replacement){free(r);error=ENOMEM;break;}
        r->policy=p;r->index=old_index;r->next=changes;changes=r;
    }
    if(!error&&!changes)error=ENOENT;
    if(!error)for(struct oz_vdev_repath *r=changes;r;r=r->next)
        r->policy->pending_path=r->replacement;
    pthread_mutex_unlock(&policy_lock);
    if(error)while(changes){struct oz_vdev_repath *r=changes;changes=r->next;free(r->replacement);free(r);}
    else *out=changes;
    return error;
}
void oz_vdev_policy_repath_end(struct oz_vdev_repath *changes,int error)
{
    pthread_mutex_lock(&policy_lock);
    while(changes) {
        struct oz_vdev_repath *r=changes;changes=r->next;
        ASSERT3P(r->policy->pending_path,==,r->replacement);
        if(!error) {
            free(r->policy->paths[r->index]);
            r->policy->paths[r->index]=r->replacement;
        } else free(r->replacement);
        r->policy->pending_path=NULL;
        free(r);
    }
    pthread_mutex_unlock(&policy_lock);
}
int oz_vdev_policy_reguid_begin(uint64_t old,uint64_t next)
{
    if(!old||!next)return EINVAL;
    if(old==next)return EEXIST;
    int error=0;
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *p=policies;p;p=p->next)
        if(p->guid==next||p->pending_guid==next||
            (p->guid==old&&(p->pending_guid||p->pending_path))){error=EBUSY;break;}
    if(!error)for(struct oz_vdev_policy *p=policies;p;p=p->next)
        if(p->guid==old)p->pending_guid=next;
    pthread_mutex_unlock(&policy_lock);
    return error;
}
void oz_vdev_policy_reguid_end(uint64_t old,uint64_t next,int error)
{
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *p=policies;p;p=p->next)
        if(p->guid==old&&p->pending_guid==next) {
            if(!error)p->guid=next;
            p->pending_guid=0;
        }
    pthread_mutex_unlock(&policy_lock);
}
int oz_vdev_policy_check_named(uint64_t guid,const char *name,const char *path)
{
    int error=0;
    pthread_mutex_lock(&policy_lock);
    for(struct oz_vdev_policy *p=policies;p;p=p->next)if((guid&&(p->guid==guid||p->pending_guid==guid))||
        (name&&p->name[0]&&!strcmp(name,p->name))) {
        int allowed=path&&p->pending_path&&!strcmp(path,p->pending_path);
        for(unsigned i=0;path&&i<p->count;i++)if(!strcmp(path,p->paths[i])){allowed=1;break;}
        if(!allowed){p->rejected++;error=EACCES;}else p->accepted++;
    }
    pthread_mutex_unlock(&policy_lock);
    return error;
}
void oz_vdev_policy_end(struct oz_vdev_policy *p,unsigned *accepted,unsigned *rejected)
{
    if(!p)return;
    pthread_mutex_lock(&policy_lock);
    struct oz_vdev_policy **link=&policies;
    while(*link&&*link!=p)link=&(*link)->next;
    ASSERT3P(*link,==,p);*link=p->next;
    if(accepted)*accepted=p->accepted;
    if(rejected)*rejected=p->rejected;
    pthread_mutex_unlock(&policy_lock);destroy(p);
}
