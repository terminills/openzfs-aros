/* SPDX-License-Identifier: CDDL-1.0 */
/* Private QEMU diagnostic: suspend AFTER the async worker owns namespace.
 * No injected error return: the real TXG waiter must observe suspension.
 * Link wrappers belong only to the standalone disposable-pool test. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/dsl_pool.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <proto/dos.h>
#include <proto/exec.h>

static spa_t *probe_spa;
static unsigned armed,entered,release_wait,returned,wait_flags;
extern int __real_txg_wait_synced_flags(dsl_pool_t *,uint64_t,txg_wait_flag_t);
extern void __real_txg_wait_synced(dsl_pool_t *,uint64_t);

static int intercept(dsl_pool_t *dp,uint64_t *txg,txg_wait_flag_t flags)
{
    if(dp->dp_spa!=probe_spa||!spa_namespace_held()||
        curthread!=probe_spa->spa_async_thread||
        !__atomic_exchange_n(&armed,0,__ATOMIC_ACQ_REL))return 0;
    __atomic_store_n(&wait_flags,flags,__ATOMIC_RELEASE);
    __atomic_store_n(&entered,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&release_wait,__ATOMIC_ACQUIRE))Delay(1);
    /* Suspension prevents further syncing. Account for an already-running
     * TXG so neither path accidentally observes an already-synced target. */
    *txg=spa_last_synced_txg(probe_spa)+TXG_DEFER_SIZE+2;
    return 1;
}
int __wrap_txg_wait_synced_flags(dsl_pool_t *dp,uint64_t txg,txg_wait_flag_t flags)
{
    int active=intercept(dp,&txg,flags);
    int error=__real_txg_wait_synced_flags(dp,txg,flags);
    if(active)__atomic_store_n(&returned,1,__ATOMIC_RELEASE);
    return error;
}
void __wrap_txg_wait_synced(dsl_pool_t *dp,uint64_t txg)
{
    int active=intercept(dp,&txg,TXG_WAIT_NONE);
    __real_txg_wait_synced(dp,txg);
    if(active)__atomic_store_n(&returned,1,__ATOMIC_RELEASE);
}
static int idle(spa_t *spa)
{
    mutex_enter(&spa->spa_async_lock);
    int result=spa->spa_async_thread==NULL;
    mutex_exit(&spa->spa_async_lock);
    return result;
}
int oz_async_config_suspend_test(void)
{
    spa_t *spa=NULL;
    int error=spa_open("qemu_mirror",&spa,FTAG);
    if(error)return error;
    spa_async_suspend(spa);
    probe_spa=spa;entered=release_wait=returned=wait_flags=0;
    __atomic_store_n(&armed,1,__ATOMIC_RELEASE);
    spa_async_request(spa,SPA_ASYNC_CONFIG_UPDATE);
    spa_async_resume(spa);
    txg_wait_synced(spa->spa_dsl_pool,0);
    unsigned polls=0;
    while(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE)&&polls++<1000)Delay(1);
    if(!__atomic_load_n(&entered,__ATOMIC_ACQUIRE))error=ETIMEDOUT;
    if(!error) {
        zio_suspend(spa,NULL,ZIO_SUSPEND_IOERR);
        __atomic_store_n(&release_wait,1,__ATOMIC_RELEASE);
        polls=0;
        while((!__atomic_load_n(&returned,__ATOMIC_ACQUIRE)||!idle(spa))&&polls++<250)Delay(1);
        if(!__atomic_load_n(&returned,__ATOMIC_ACQUIRE)||!idle(spa))error=EBUSY;
        else if(!(wait_flags&TXG_WAIT_SUSPEND))error=EINVAL;
        else if(!spa_namespace_tryenter(FTAG))error=EDEADLK;
        else {
            spa_namespace_exit(FTAG);
            mutex_enter(&spa->spa_async_lock);
            if(!(spa->spa_async_tasks&SPA_ASYNC_CONFIG_UPDATE))error=ENOENT;
            mutex_exit(&spa->spa_async_lock);
        }
    }
    /* Always resume, including the negative control's blocked waiter. */
    __atomic_store_n(&armed,0,__ATOMIC_RELEASE);
    __atomic_store_n(&release_wait,1,__ATOMIC_RELEASE);
    int resume=zio_resume(spa);
    if(!error)error=resume;
    txg_wait_kick(spa->spa_dsl_pool);
    polls=0;
    while(polls++<1000) {
        mutex_enter(&spa->spa_async_lock);
        int done=spa->spa_async_thread==NULL&&
            !(spa->spa_async_tasks&SPA_ASYNC_CONFIG_UPDATE);
        mutex_exit(&spa->spa_async_lock);
        if(done)break;
        Delay(1);
    }
    if(polls>=1000&&!error)error=ETIMEDOUT;
    probe_spa=NULL;
    spa_close(spa,FTAG);
    return error;
}
