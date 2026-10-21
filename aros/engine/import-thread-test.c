/* Verify the actual upstream pool used by label discovery on native AROS. */
#include <thread_pool.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
static unsigned counts[128],bad_member;
static tpool_t *current;
static void note(const char *);
static void cleanup_probe(void *arg)
{(*(unsigned *)arg)++;}
static void job(void *arg)
{
    unsigned i=(unsigned)(uintptr_t)arg;
    if(i==0) {
        void *allocation=malloc(32);
        if(!allocation)__atomic_fetch_add(&bad_member,1,__ATOMIC_RELAXED);
        free(allocation);
        unsigned cleaned=0;
        pthread_cleanup_push(cleanup_probe,&cleaned);
        pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,NULL);
        pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
        pthread_cleanup_pop(1);
        if(cleaned!=1)__atomic_fetch_add(&bad_member,1,__ATOMIC_RELAXED);
    }
    int member=tpool_member(current);
    __atomic_fetch_add(&counts[i],1,__ATOMIC_RELAXED);
    if(!member)__atomic_fetch_add(&bad_member,1,__ATOMIC_RELAXED);
}
static void note(const char *s)
{for(;*s;s++)RawPutChar(*s);RawPutChar('\n');}
static void *progress(void *unused)
{
    (void)unused;Delay(250);
    unsigned total=0;
    for(unsigned i=0;i<128;i++)total+=__atomic_load_n(&counts[i],__ATOMIC_RELAXED);
    char line[80];snprintf(line,sizeof(line),"Native import thread watchdog completed=%u",total);note(line);
    return NULL;
}
int main(void)
{
    note("Native upstream import thread pool starting");
    unsigned cleaned=0;
    pthread_cleanup_push(cleanup_probe,&cleaned);
    pthread_setcanceltype(PTHREAD_CANCEL_DEFERRED,NULL);
    pthread_setcancelstate(PTHREAD_CANCEL_ENABLE,NULL);
    pthread_cleanup_pop(1);
    if(cleaned!=1){note("RESULT import thread pool FAIL native cleanup callback not invoked");return 20;}
    pthread_t watcher;
    if(pthread_create(&watcher,NULL,progress,NULL))return 20;
    for(unsigned round=0;round<8;round++) {
        pthread_attr_t attr;pthread_attr_init(&attr);
        current=tpool_create(1,4,0,round&1?&attr:NULL);
        pthread_attr_destroy(&attr);
        if(!current){char line[80];snprintf(line,sizeof(line),"RESULT import thread pool FAIL create round=%u errno=%d",round,errno);note(line);return 20;}
        tpool_suspend(current);
        if(!tpool_suspended(current)){note("RESULT import thread pool FAIL suspend");return 20;}
        for(unsigned i=0;i<128;i++)if(tpool_dispatch(current,job,(void *)(uintptr_t)i)) {
            note("RESULT import thread pool FAIL dispatch");return 20;
        }
        for(unsigned i=0;i<128;i++)if(__atomic_load_n(&counts[i],__ATOMIC_RELAXED)!=round) {
            note("RESULT import thread pool FAIL suspended execution");return 20;
        }
        tpool_resume(current);tpool_wait(current);
        int ok=!__atomic_load_n(&bad_member,__ATOMIC_RELAXED);
        for(unsigned i=0;i<128;i++)if(__atomic_load_n(&counts[i],__ATOMIC_RELAXED)!=round+1)ok=0;
        tpool_destroy(current);current=NULL;
        if(!ok){note("RESULT import thread pool FAIL accounting");return 20;}
        char line[80];snprintf(line,sizeof(line),"Native import thread round=%u jobs=128 PASS",round);note(line);
    }
    pthread_join(watcher,NULL);
    note("RESULT import thread pool PASS rounds=8 jobs=1024");return 0;
}
