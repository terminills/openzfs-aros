/* SPDX-License-Identifier: CDDL-1.0 */
/* Private fixture only: concurrent engine calls, NOT transport authentication.
 * A start gate encourages overlap; it does not prove every SMP interleaving. */
#include <sys/dsl_destroy.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_pool.h>
#include <pthread.h>
static int count_expect(int snapshot,uint64_t expected)
{
    const char *name="qemu_mirror/count-limited";
    dsl_pool_t *dp=NULL;dsl_dir_t *dd=NULL;uint64_t value=UINT64_MAX;
    int error=dsl_pool_hold(name,FTAG,&dp);
    if(error)return error;
    error=dsl_dir_hold(dp,name,FTAG,&dd,NULL);
    if(!error) {
        error=snapshot?dsl_dir_get_snapshot_count(dd,&value):
            dsl_dir_get_filesystem_count(dd,&value);
        dsl_dir_rele(dd,FTAG);
    }
    dsl_pool_rele(dp,FTAG);
    return error?error:(value==expected?0:EIO);
}
struct count_gate {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned ready;
    int go;
};
struct count_contender {
    struct count_gate *gate;
    cred_t *cred;
    int snapshot, index, error;
};
static void *count_contend(void *arg)
{
    struct count_contender *c=arg;
    struct count_gate *g=c->gate;
    VERIFY0(pthread_mutex_lock(&g->mutex));
    g->ready++;
    VERIFY0(pthread_cond_broadcast(&g->cond));
    while(!g->go)VERIFY0(pthread_cond_wait(&g->cond,&g->mutex));
    VERIFY0(pthread_mutex_unlock(&g->mutex));
    if(g->go<0)return NULL;
    char name[128];
    snprintf(name,sizeof(name),"qemu_mirror/count-limited/%c",'a'+c->index);
    if(!c->snapshot)c->error=oz_zpl_dataset_create(name,c->cred);
    else {
        struct oz_cred_scope *scope=NULL;
        c->error=zfs_secpolicy_snapshot_perms("qemu_mirror/count-limited",c->cred);
        if(!c->error)c->error=oz_cred_push(c->cred,&scope);
        if(!c->error) {
            c->error=dmu_objset_snapshot_one("qemu_mirror/count-limited",c->index?"b":"a");
            VERIFY0(oz_cred_pop(scope));
        }
    }
    return NULL;
}
static int count_race_test(void)
{
    struct oz_message *request=calloc(1,sizeof(*request));
    cred_t *cred=oz_cred_create(77,77,88,0,NULL);
    if(!request||!cred){free(request);crfree(cred);return ENOMEM;}
    request->operation=41;
    strcpy(request->target,"qemu_mirror/count-limited");
    int error=0;
    for(int kind=0;kind<2&&!error;kind++) {
        snprintf(request->argument,sizeof(request->argument),"%s_limit=1",kind?"snapshot":"filesystem");
        error=oz_user_property_change(request,kcred);
        if(!error)error=count_expect(kind,0);
        for(int round=0;round<8&&!error;round++) {
            struct count_gate g={.mutex=PTHREAD_MUTEX_INITIALIZER,
                .cond=PTHREAD_COND_INITIALIZER};
            struct count_contender c[2]={{&g,cred,kind,0,EINPROGRESS},
                {&g,cred,kind,1,EINPROGRESS}};
            pthread_t threads[2];unsigned started=0;
            for(;started<2;started++) {
                error=pthread_create(&threads[started],NULL,count_contend,&c[started]);
                if(error)break;
            }
            VERIFY0(pthread_mutex_lock(&g.mutex));
            while(!error&&g.ready!=2)VERIFY0(pthread_cond_wait(&g.cond,&g.mutex));
            g.go=error?-1:1;
            VERIFY0(pthread_cond_broadcast(&g.cond));
            VERIFY0(pthread_mutex_unlock(&g.mutex));
            for(unsigned i=0;i<started;i++)VERIFY0(pthread_join(threads[i],NULL));
            VERIFY0(pthread_cond_destroy(&g.cond));
            VERIFY0(pthread_mutex_destroy(&g.mutex));
            if(error)break;
            if(!((c[0].error==0&&c[1].error==EDQUOT)||
                 (c[1].error==0&&c[0].error==EDQUOT)))error=EIO;
            if(!error)error=count_expect(kind,1);
            char notice[128];
            snprintf(notice,sizeof(notice),"Native count race kind=%d round=%d errors=%d,%d result=%d\n",
                kind,round,c[0].error,c[1].error,error);
            for(char *p=notice;*p;p++)RawPutChar(*p);
            /* Preserve failed objects for offline inspection. */
            if(error)break;
            char name[128];
            snprintf(name,sizeof(name),"qemu_mirror/count-limited%c%c",kind?'@':'/',c[0].error?'b':'a');
            struct oz_cred_scope *scope=NULL;
            error=oz_cred_push(kcred,&scope);
            if(!error) {
                error=kind?dsl_destroy_snapshot(name,B_FALSE):dsl_destroy_head(name);
                VERIFY0(oz_cred_pop(scope));
            }
            if(!error)error=count_expect(kind,0);
        }
    }
    crfree(cred);free(request);
    if(!error) {
        const char *p="Native concurrent engine count limits PASS rounds=16\n";
        while(*p)RawPutChar(*p++);
    }
    return error;
}
