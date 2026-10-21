/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream asynchronous scrub control; caller owns management authorization. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/fs/zfs.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"

int oz_resilver_restart(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(m->flags!=OZ_CONFIRMED||m->argument[0]||m->options[0]||m->cursor||
        m->topology.group_count||m->topology.device_count||m->topology.ashift||
        pool_namecheck(m->target,NULL,NULL))return EINVAL;
    spa_t *spa=NULL;
    error=spa_open(m->target,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa))error=EAGAIN;
    else {
        struct oz_cred_scope *scope=NULL;
        error=oz_cred_push(caller,&scope);
        /* Upstream checks resilver_defer and whether any leaf needs repair.
         * A successful no-op is not proof that a resilver took place. */
        if(!error)error=spa_scan(spa,POOL_SCAN_RESILVER);
        if(scope)VERIFY0(oz_cred_pop(scope));
    }
    spa_close(spa,FTAG);return error;
}
int oz_scan_control(const char *pool,const char *action)
{
    if(pool_namecheck(pool,NULL,NULL))return EINVAL;
    if(strcmp(action,"start")&&strcmp(action,"stop")&&
        strcmp(action,"pause")&&strcmp(action,"resume"))return EINVAL;
    spa_t *spa=NULL;int rc=spa_open(pool,&spa,FTAG);
    if(rc)return rc;
    if(!spa_writeable(spa))rc=EROFS;
    else if(!strcmp(action,"start"))rc=spa_scan(spa,POOL_SCAN_SCRUB);
    else if(!strcmp(action,"stop"))rc=spa_scan_stop(spa);
    else rc=spa_scrub_pause_resume(spa,!strcmp(action,"pause")?
        POOL_SCRUB_PAUSE:POOL_SCRUB_NORMAL);
    spa_close(spa,FTAG);return rc;
}
int oz_scan_status(const char *pool,char *out,size_t size)
{
    spa_t *spa=NULL;int rc=spa_open(pool,&spa,FTAG);
    pool_scan_stat_t scan={0};
    if(!rc) {
        rc=spa_scan_get_stats(spa,&scan);
        /* ENOENT here means no scan history, not failure to find the pool. */
        if(rc==ENOENT)rc=0;
        spa_close(spa,FTAG);
    }
    if(rc){snprintf(out,size,"Scan statistics unavailable: errno=%d",rc);return rc;}
    const char *function=scan.pss_func==POOL_SCAN_NONE?"none":
        scan.pss_func==POOL_SCAN_SCRUB?"scrub":scan.pss_func==POOL_SCAN_RESILVER?"resilver":"other";
    const char *state=scan.pss_state==DSS_NONE?"none":scan.pss_state==DSS_FINISHED?"finished":
        scan.pss_state==DSS_CANCELED?"canceled":scan.pss_state==DSS_SCANNING?
        (scan.pss_pass_scrub_pause?"paused":"scanning"):"unknown";
    snprintf(out,size,"scan=%s state=%s examined=%llu to-examine=%llu issued=%llu errors=%llu start=%llu end=%llu",
        function,state,(unsigned long long)scan.pss_examined,(unsigned long long)scan.pss_to_examine,
        (unsigned long long)scan.pss_issued,(unsigned long long)scan.pss_errors,
        (unsigned long long)scan.pss_start_time,(unsigned long long)scan.pss_end_time);
    return 0;
}
