/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_impl.h>
#include "../cli/session-cleanup.h"
#include "replication.h"

#define CHECK(x) do { if (!(x)) return EIO; } while (0)
struct worker { proc_t *a,*b; struct oz_caller_scope *foreign; int abandon; };
static void *worker(void *arg)
{
    struct worker *w=arg;
    struct oz_caller_scope *a=NULL,*b=NULL;
    int error=0;
    if(oz_caller_capture() || zfs_proc_is_caller(w->a) ||
        oz_caller_leave(w->foreign)!=EINVAL)error=EIO;
    if(!error)error=oz_caller_enter(w->a,&a);
    if(!error&&(!zfs_proc_is_caller(w->a)||zfs_proc_is_caller(w->b)))error=EIO;
    if(!error)error=oz_caller_enter(w->b,&b);
    if(!error&&(zfs_proc_is_caller(w->a)||!zfs_proc_is_caller(w->b)))error=EIO;
    /* Thread exit must release nested scopes even without explicit leaves. */
    if(w->abandon)return (void *)(uintptr_t)error;
    if(b&&oz_caller_leave(b))error=EIO;
    if(a&&oz_caller_leave(a))error=EIO;
    if(zfs_proc_is_caller(w->a)||oz_caller_capture())error=EIO;
    return (void *)(uintptr_t)error;
}
int oz_caller_test(void)
{
    struct oz_session_cleanup *sa=NULL,*sb=NULL;
    CHECK(!oz_session_cleanup_create(&sa));
    CHECK(!oz_session_cleanup_create(&sb));
    proc_t *a=oz_session_cleanup_caller(sa),*b=oz_session_cleanup_caller(sb);
    CHECK(a&&b&&a!=b&&!oz_caller_capture()&&!zfs_proc_is_caller(NULL));
    struct oz_caller_scope *outer=NULL,*inner=NULL;
    CHECK(oz_caller_enter(NULL,&outer)==EACCES&&!outer);
    CHECK(!oz_caller_enter(a,&outer));
    proc_t *captured=oz_caller_capture();CHECK(captured==a);
    CHECK(!oz_caller_enter(b,&inner));
    CHECK(!zfs_proc_is_caller(captured)&&zfs_proc_is_caller(b));
    CHECK(oz_caller_leave(outer)==EINVAL);
    CHECK(!oz_caller_leave(inner)&&zfs_proc_is_caller(captured));
    struct worker w[16];pthread_t threads[16];unsigned started=0;int error=0;
    for(;started<16;started++) {
        w[started]=(struct worker){a,b,outer,started&1};
        error=pthread_create(&threads[started],NULL,worker,&w[started]);
        if(error)break;
    }
    for(unsigned i=0;i<started;i++) {
        void *result=NULL;
        if(pthread_join(threads[i],&result)||result)error=EIO;
    }
    CHECK(!error&&started==16);
    oz_session_cleanup_close(sa);oz_session_cleanup_close(sb);
    CHECK(zfs_proc_is_caller(captured)); /* scope/capture retain identity */
    CHECK(!oz_caller_leave(outer)&&!zfs_proc_is_caller(captured));
    CHECK(!oz_caller_enter(captured,&outer)&&zfs_proc_is_caller(captured));
    CHECK(!oz_caller_leave(outer));oz_caller_release(captured);
    CHECK(!oz_caller_capture());
    return 0;
}
struct observation { const char *snapshot; proc_t *a,*b; unsigned calls; int fail,error; };
static int observe_output(void *arg,const void *bytes,size_t length)
{
    (void)bytes;(void)length;
    struct observation *o=arg;
    dsl_pool_t *dp;dsl_dataset_t *ds=NULL;
    int error=dsl_pool_hold(o->snapshot,FTAG,&dp);
    if(!error) {
        error=dsl_dataset_hold(dp,o->snapshot,FTAG,&ds);
        if(!error) {
            mutex_enter(&ds->ds_sendstream_lock);
            dmu_sendstatus_t *status=list_head(&ds->ds_sendstreams);
            if(!status||status->dss_proc!=o->a||!zfs_proc_is_caller(status->dss_proc))error=EIO;
            struct oz_caller_scope *scope=NULL;
            if(!error)error=oz_caller_enter(o->b,&scope);
            if(!error&&zfs_proc_is_caller(status->dss_proc))error=EIO;
            if(scope&&oz_caller_leave(scope))error=EIO;
            if(!error&&!zfs_proc_is_caller(status->dss_proc))error=EIO;
            mutex_exit(&ds->ds_sendstream_lock);
            dsl_dataset_rele(ds,FTAG);
        }
        dsl_pool_rele(dp,FTAG);
    }
    ++o->calls;
    if(error)o->error=error;
    return error ? error : o->fail ? ENOSPC : 0;
}
int oz_send_caller_test(const char *snapshot)
{
    proc_t *a=NULL,*b=NULL;
    CHECK(!oz_caller_create(&a)&&!oz_caller_create(&b));
    struct oz_caller_scope *scope=NULL;CHECK(!oz_caller_enter(a,&scope));
    for(int fail=0;fail<2;fail++) {
        struct observation observation={snapshot,a,b,0,fail,0};
        int error=oz_send_stream(snapshot,NULL,kcred,observe_output,&observation);
        CHECK(error==(fail?ENOSPC:0)&&observation.calls&&!observation.error);
        dsl_pool_t *dp;dsl_dataset_t *ds;
        CHECK(!dsl_pool_hold(snapshot,FTAG,&dp));
        CHECK(!dsl_dataset_hold(dp,snapshot,FTAG,&ds));
        mutex_enter(&ds->ds_sendstream_lock);
        boolean_t empty=list_is_empty(&ds->ds_sendstreams);
        mutex_exit(&ds->ds_sendstream_lock);
        dsl_dataset_rele(ds,FTAG);dsl_pool_rele(dp,FTAG);
        CHECK(empty&&zfs_proc_is_caller(a));
    }
    CHECK(!oz_caller_leave(scope));oz_caller_release(a);oz_caller_release(b);
    return 0;
}
