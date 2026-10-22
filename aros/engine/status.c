/* SPDX-License-Identifier: CDDL-1.0 */
/* Read-only adapter over upstream SPA's owned statistics nvlist. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/zap.h>
#include <sys/fs/zfs.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"
#ifdef OZ_STATUS_TRACE
#include <proto/exec.h>
static void status_trace(const char *stage)
{
    const char *prefix="OpenZFS status stage=";
    while(*prefix)RawPutChar(*prefix++);
    while(*stage)RawPutChar(*stage++);
    RawPutChar('\n');
}
#else
#define status_trace(stage) ((void)0)
#endif
/* Same state/aux interpretation as upstream zpool_state_to_name. */
static const char *health(uint64_t state,uint64_t aux)
{
    switch(state) {
    case VDEV_STATE_CLOSED:case VDEV_STATE_OFFLINE:return "OFFLINE";
    case VDEV_STATE_REMOVED:return "REMOVED";
    case VDEV_STATE_CANT_OPEN:
        return aux==VDEV_AUX_CORRUPT_DATA||aux==VDEV_AUX_BAD_LOG?"FAULTED":
            aux==VDEV_AUX_SPLIT_POOL?"SPLIT":"UNAVAIL";
    case VDEV_STATE_FAULTED:return "FAULTED";
    case VDEV_STATE_DEGRADED:return "DEGRADED";
    case VDEV_STATE_HEALTHY:return "ONLINE";
    default:return "UNKNOWN";
    }
}
struct status_page {struct oz_message *m;uint64_t index,load_guid;int full;};
/* Read-only imports do not execute upstream's initialization restart path.
 * Read its persisted ZAP fields into our owned response, never into live vdev
 * state. A saved physical offset is not a runtime bytes-done counter. */
static int stored_initialization(spa_t *spa,nvlist_t *nv,unsigned depth)
{
    if(depth>32)return EOVERFLOW;
    uint64_t guid;int error=nvlist_lookup_uint64(nv,ZPOOL_CONFIG_GUID,&guid);
    if(error)return error;
    vdev_t *vd=spa_lookup_by_guid(spa,guid,B_FALSE);
    if(vd&&vd->vdev_leaf_zap) {
        uint64_t state=0,time=0,offset=0;
        error=zap_lookup(spa->spa_meta_objset,vd->vdev_leaf_zap,
            VDEV_LEAF_ZAP_INITIALIZE_STATE,sizeof(state),1,&state);
        if(error&&error!=ENOENT)return error;
        if(!error&&state!=VDEV_INITIALIZE_NONE) {
            error=zap_lookup(spa->spa_meta_objset,vd->vdev_leaf_zap,
                VDEV_LEAF_ZAP_INITIALIZE_ACTION_TIME,sizeof(time),1,&time);
            if(error&&error!=ENOENT)return error;
            error=zap_lookup(spa->spa_meta_objset,vd->vdev_leaf_zap,
                VDEV_LEAF_ZAP_INITIALIZE_LAST_OFFSET,sizeof(offset),1,&offset);
            if(error&&error!=ENOENT)return error;
            fnvlist_add_uint64(nv,"aros_initialize_stored_state",state);
            fnvlist_add_uint64(nv,"aros_initialize_stored_time",time);
            fnvlist_add_uint64(nv,"aros_initialize_stored_offset",offset);
        }
    }
    nvlist_t **children;uint_t count;
    error=nvlist_lookup_nvlist_array(nv,ZPOOL_CONFIG_CHILDREN,&children,&count);
    if(error==ENOENT)return 0;
    if(error)return error;
    for(unsigned i=0;i<count;i++) {
        error=stored_initialization(spa,children[i],depth+1);
        if(error)return error;
    }
    return 0;
}
/* libzpool deliberately does not activate shared spares/L2ARC on RO imports.
 * Preserve that behavior, but report their stored configuration explicitly
 * instead of making them disappear or inventing ONLINE health/counters. */
static int configured_aux(const char *name,nvlist_t *root,uint64_t *load_guid)
{
    spa_t *spa;int error=spa_open(name,&spa,FTAG);if(error)return error;
    spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
    *load_guid=spa_load_guid(spa);
    if(!spa_writeable(spa)) {
        error=stored_initialization(spa,root,0);
        spa_aux_vdev_t *aux[]={&spa->spa_spares,&spa->spa_l2cache};
        const char *keys[]={ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
        for(unsigned i=0;i<2&&!error;i++) {
            if(!aux[i]->sav_config)continue;
            nvlist_t **items;uint_t count;
            error=nvlist_lookup_nvlist_array(root,keys[i],&items,&count);
            if(error==ENOENT){
                error=nvlist_lookup_nvlist_array(aux[i]->sav_config,keys[i],&items,&count);
                if(!error)error=nvlist_add_nvlist_array(root,keys[i],(const nvlist_t *const *)items,count);
                if(!error)error=nvlist_lookup_nvlist_array(root,keys[i],&items,&count);
            }
            if(!error) {
                for(unsigned j=0;j<count;j++){
                    uint64_t *raw;uint_t fields;
                    /* spa_get_stats supplies live stats for an in-tree
                     * replacement spare even on readonly libzpool import.
                     * Do not replace those with saved configuration. */
                    if(i==0&&!nvlist_lookup_uint64_array(items[j],ZPOOL_CONFIG_VDEV_STATS,&raw,&fields)&&
                        VDEV_STAT_VALID(vs_aux,fields)&&((vdev_stat_t *)raw)->vs_aux==VDEV_AUX_SPARED)continue;
                    fnvlist_add_boolean(items[j],"aros_configured_only");
                }
            }
        }
    }
    spa_config_exit(spa,SCL_CONFIG,FTAG);spa_close(spa,FTAG);return error;
}
static int device_rows(nvlist_t *nv,const char *parent,const char *role,
    unsigned depth,struct status_page *page)
{
    if(depth>32)return EOVERFLOW;
    const char *type,*path=NULL;uint64_t guid;
    if(nvlist_lookup_string(nv,ZPOOL_CONFIG_TYPE,&type)||
        nvlist_lookup_uint64(nv,ZPOOL_CONFIG_GUID,&guid))return EINVAL;
    (void)nvlist_lookup_string(nv,ZPOOL_CONFIG_PATH,&path);
    char name[160];int n;
    if(!depth)n=snprintf(name,sizeof(name),"%s",page->m->target);
    else if(path)n=snprintf(name,sizeof(name),"%s",path);
    else n=snprintf(name,sizeof(name),"%s-%016llx",type,(unsigned long long)guid);
    if(n<0||(size_t)n>=sizeof(name)||strlen(type)>=sizeof(page->m->rows[0].kind))return ENAMETOOLONG;
    if(page->m->operation==35) {
        vdev_stat_t sample={0};uint64_t *raw;uint_t count;
        boolean_t inactive=nvlist_exists(nv,"aros_configured_only");
        if(!inactive) {
            if(nvlist_lookup_uint64_array(nv,ZPOOL_CONFIG_VDEV_STATS,&raw,&count)||
                !VDEV_STAT_VALID(vs_bytes,count))return EINVAL;
            memcpy(&sample,raw,MIN(sizeof(sample),(size_t)count*sizeof(uint64_t)));
            if(sample.vs_timestamp<0)return EINVAL;
        }
        if(!strcmp(page->m->options,"histogram=latency")||!strcmp(page->m->options,"histogram=size")) {
            int sizes=!strcmp(page->m->options,"histogram=size");
            const char *latency_keys[]={ZPOOL_CONFIG_VDEV_TOT_R_LAT_HISTO,ZPOOL_CONFIG_VDEV_TOT_W_LAT_HISTO,
                ZPOOL_CONFIG_VDEV_DISK_R_LAT_HISTO,ZPOOL_CONFIG_VDEV_DISK_W_LAT_HISTO,
                ZPOOL_CONFIG_VDEV_SYNC_R_LAT_HISTO,ZPOOL_CONFIG_VDEV_SYNC_W_LAT_HISTO,
                ZPOOL_CONFIG_VDEV_ASYNC_R_LAT_HISTO,ZPOOL_CONFIG_VDEV_ASYNC_W_LAT_HISTO,
                ZPOOL_CONFIG_VDEV_SCRUB_LAT_HISTO,ZPOOL_CONFIG_VDEV_TRIM_LAT_HISTO,
                ZPOOL_CONFIG_VDEV_REBUILD_LAT_HISTO};
            const char *size_keys[]={ZPOOL_CONFIG_VDEV_SYNC_IND_R_HISTO,ZPOOL_CONFIG_VDEV_SYNC_IND_W_HISTO,
                ZPOOL_CONFIG_VDEV_ASYNC_IND_R_HISTO,ZPOOL_CONFIG_VDEV_ASYNC_IND_W_HISTO,
                ZPOOL_CONFIG_VDEV_IND_SCRUB_HISTO,ZPOOL_CONFIG_VDEV_IND_TRIM_HISTO,ZPOOL_CONFIG_VDEV_IND_REBUILD_HISTO,
                ZPOOL_CONFIG_VDEV_SYNC_AGG_R_HISTO,ZPOOL_CONFIG_VDEV_SYNC_AGG_W_HISTO,
                ZPOOL_CONFIG_VDEV_ASYNC_AGG_R_HISTO,ZPOOL_CONFIG_VDEV_ASYNC_AGG_W_HISTO,
                ZPOOL_CONFIG_VDEV_AGG_SCRUB_HISTO,ZPOOL_CONFIG_VDEV_AGG_TRIM_HISTO,ZPOOL_CONFIG_VDEV_AGG_REBUILD_HISTO};
            const char **keys=sizes?size_keys:latency_keys;
            unsigned metrics=sizes?ARRAY_SIZE(size_keys):ARRAY_SIZE(latency_keys);
            unsigned expected=sizes?VDEV_RQ_HISTO_BUCKETS:VDEV_L_HISTO_BUCKETS;
            nvlist_t *extended=NULL;
            if(!inactive)(void)nvlist_lookup_nvlist(nv,ZPOOL_CONFIG_VDEV_STATS_EX,&extended);
            for(unsigned metric=0;metric<metrics;metric++) {
                uint64_t *buckets=NULL;uint_t count=0;
                int unavailable=!extended||nvlist_lookup_uint64_array(extended,keys[metric],&buckets,&count);
                if(!unavailable&&count!=expected)return EINVAL;
                for(unsigned bucket=0;bucket<(unavailable?1:count);bucket++) {
                    uint64_t index=page->index++;
                    if(index<page->m->cursor)continue;
                    if(page->m->row_count==OZ_MAX_ROWS){page->m->next_cursor=index;page->full=1;return ECANCELED;}
                    struct oz_row *r=&page->m->rows[page->m->row_count++];
                    strcpy(r->kind,sizes?"io-size":"io-latency");strcpy(r->name,name);strcpy(r->parent,parent);
                    strcpy(r->state,unavailable?"UNAVAILABLE":health(sample.vs_state,sample.vs_aux));
                    /* Upstream HISTO folds zero into bucket zero and saturates
                     * the last bucket. Counts are cumulative, never rates. */
                    r->used=unavailable?0:buckets[bucket];
                    r->available=bucket?1ULL<<bucket:0;
                    r->total=bucket+1==expected?UINT64_MAX:(1ULL<<(bucket+1))-1;
                    snprintf(r->value,sizeof(r->value),"metric=%s bucket=%u unit=%s counts=%s guid=%llu load-guid=%llu",
                        keys[metric],bucket,sizes?"bytes":"ns",unavailable?"unavailable":"cumulative",
                        (unsigned long long)guid,(unsigned long long)page->load_guid);
                }
            }
            goto children;
        }
        for(unsigned metric=0;metric<2;metric++) {
            uint64_t index=page->index++;
            if(index<page->m->cursor)continue;
            if(page->m->row_count==OZ_MAX_ROWS) {
                page->m->next_cursor=index;page->full=1;return ECANCELED;
            }
            struct oz_row *r=&page->m->rows[page->m->row_count++];
            strcpy(r->kind,metric?"io-ops":"io-bytes");
            strcpy(r->name,name);strcpy(r->parent,parent);
            strcpy(r->state,inactive?"UNAVAILABLE":health(sample.vs_state,sample.vs_aux));
            r->used=metric?sample.vs_ops[ZIO_TYPE_READ]:sample.vs_bytes[ZIO_TYPE_READ];
            r->available=metric?sample.vs_ops[ZIO_TYPE_WRITE]:sample.vs_bytes[ZIO_TYPE_WRITE];
            r->total=sample.vs_timestamp;
            snprintf(r->value,sizeof(r->value),"type=%s role=%s guid=%llu counters=%s load-guid=%llu",
                type,role,(unsigned long long)guid,inactive?"unavailable":"cumulative-since-vdev-load",
                (unsigned long long)page->load_guid);
        }
        goto children;
    }
    uint64_t index=page->index++;
    if(index>=page->m->cursor) {
        if(page->m->row_count==OZ_MAX_ROWS) {
            page->m->next_cursor=index;page->full=1;return ECANCELED;
        }
        struct oz_row *r=&page->m->rows[page->m->row_count++];
        strcpy(r->kind,depth?type:"pool");strcpy(r->name,name);strcpy(r->parent,parent);
        if(nvlist_exists(nv,"aros_configured_only")) {
            strcpy(r->state,"INACTIVE");
            snprintf(r->value,sizeof(r->value),"role=%s guid=%llu configured-only=readonly statistics=unavailable",role,(unsigned long long)guid);
            return 0; /* Auxiliary entries are leaves. No backing device opened. */
        }
        uint64_t *raw;uint_t count;
        if(nvlist_lookup_uint64_array(nv,ZPOOL_CONFIG_VDEV_STATS,&raw,&count)||
            !VDEV_STAT_VALID(vs_checksum_errors,count))return EINVAL;
        const vdev_stat_t *vs=(const vdev_stat_t *)raw;
        /* Upstream zpool status gives SPARED precedence over the auxiliary
         * handle's open state; its active in-tree instance owns the I/O. */
        strcpy(r->state,!strcmp(role,"spare")&&
            vs->vs_aux==VDEV_AUX_SPARED?"INUSE":health(vs->vs_state,vs->vs_aux));
        r->used=vs->vs_alloc;r->total=vs->vs_space;
        r->available=vs->vs_space>vs->vs_alloc?vs->vs_space-vs->vs_alloc:0;
        uint64_t parity=0;
        (void)nvlist_lookup_uint64(nv,ZPOOL_CONFIG_NPARITY,&parity);
        snprintf(r->value,sizeof(r->value),
            "role=%s guid=%llu aux=%llu read-errors=%llu write-errors=%llu checksum-errors=%llu parity=%llu",
            role,(unsigned long long)guid,(unsigned long long)vs->vs_aux,
            (unsigned long long)vs->vs_read_errors,(unsigned long long)vs->vs_write_errors,
            (unsigned long long)vs->vs_checksum_errors,(unsigned long long)parity);
    }
    uint64_t *init_raw;uint_t init_count;
    if(!nvlist_lookup_uint64_array(nv,ZPOOL_CONFIG_VDEV_STATS,&init_raw,&init_count)&&
        VDEV_STAT_VALID(vs_initialize_action_time,init_count)) {
        vdev_stat_t snapshot={0};
        memcpy(&snapshot,init_raw,MIN(sizeof(snapshot),(size_t)init_count*sizeof(uint64_t)));
        uint64_t stored_state=0,stored_time=0,stored_offset=0;
        boolean_t stored=!nvlist_lookup_uint64(nv,"aros_initialize_stored_state",&stored_state);
        if(stored) {
            (void)nvlist_lookup_uint64(nv,"aros_initialize_stored_time",&stored_time);
            (void)nvlist_lookup_uint64(nv,"aros_initialize_stored_offset",&stored_offset);
            snapshot.vs_initialize_state=stored_state;
            snapshot.vs_initialize_action_time=stored_time;
            snapshot.vs_initialize_bytes_done=0;snapshot.vs_initialize_bytes_est=0;
        }
        const vdev_stat_t *vs=&snapshot;
        if(vs->vs_initialize_state!=VDEV_INITIALIZE_NONE) {
            index=page->index++;
            if(index>=page->m->cursor) {
                if(page->m->row_count==OZ_MAX_ROWS) {
                    page->m->next_cursor=index;page->full=1;return ECANCELED;
                }
                struct oz_row *r=&page->m->rows[page->m->row_count++];
                strcpy(r->kind,"initialize");strcpy(r->name,name);strcpy(r->parent,parent);
                const char *state="UNKNOWN";
                switch(vs->vs_initialize_state) {
                case VDEV_INITIALIZE_ACTIVE:state="ACTIVE";break;
                case VDEV_INITIALIZE_CANCELED:state="CANCELED";break;
                case VDEV_INITIALIZE_SUSPENDED:state="SUSPENDED";break;
                case VDEV_INITIALIZE_COMPLETE:state="COMPLETE";break;
                }
                strcpy(r->state,state);r->used=vs->vs_initialize_bytes_done;
                r->total=vs->vs_initialize_bytes_est;
                r->available=r->total>r->used?r->total-r->used:0;
                /* The byte total is an estimate. Match upstream zpool:
                 * confirmed completion, not estimated byte equality, is 100%. */
                r->progress_permille=vs->vs_initialize_state==VDEV_INITIALIZE_COMPLETE?1000:
                    (r->total?(r->used>=r->total?1000:
                    (ULONG)((long double)r->used*1000/r->total)):0);
                if(stored)snprintf(r->value,sizeof(r->value),
                    "guid=%llu stored-only=readonly counters=unavailable action-time=%llu next-offset=%llu",
                    (unsigned long long)guid,(unsigned long long)stored_time,
                    (unsigned long long)stored_offset);
                else snprintf(r->value,sizeof(r->value),"guid=%llu errors=%llu action-time=%llu",
                    (unsigned long long)guid,(unsigned long long)vs->vs_initialize_errors,
                    (unsigned long long)vs->vs_initialize_action_time);
            }
        }
    }
children: ;
    const char *keys[]={ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
    const char *roles[]={"data","spare","cache"};
    for(unsigned a=0;a<3;a++) {
        nvlist_t **children;uint_t count;
        int rc=nvlist_lookup_nvlist_array(nv,keys[a],&children,&count);
        if(rc==ENOENT)continue;
        if(rc)return rc;
        for(unsigned i=0;i<count;i++) {
            const char *child_role=roles[a];uint64_t islog=0;
            if(!a) {
                child_role=depth?role:"data";
                (void)nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_IS_LOG,&islog);
                if(islog)child_role="log";
                else {
                    const char *bias;
                    if(!nvlist_lookup_string(children[i],ZPOOL_CONFIG_ALLOCATION_BIAS,&bias))child_role=bias;
                }
            }
            rc=device_rows(children[i],name,child_role,depth+1,page);
            if(rc)return rc;
        }
    }
    return 0;
}
int oz_pool_status(struct oz_message *m,int *load_error)
{
    nvlist_t *config=NULL,*root=NULL;
    if(pool_namecheck(m->target,NULL,NULL)){*load_error=EINVAL;return EINVAL;}
    /* This interface reports imported namespace entries only. */
    status_trace("namespace-enter");
    spa_namespace_enter(FTAG);
    status_trace("namespace-held");
    boolean_t exists=spa_lookup(m->target)!=NULL;
    spa_namespace_exit(FTAG);
    if(!exists){*load_error=ENOENT;return ENOENT;}
    status_trace("stats-enter");
    *load_error=spa_get_stats(m->target,&config,NULL,0);
    status_trace("stats-returned");
    int rc=*load_error;
    if(config) {
        rc=nvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,&root);
        uint64_t load_guid=0;
        if(!rc){status_trace("aux-enter");rc=configured_aux(m->target,root,&load_guid);status_trace("aux-returned");}
        if(!rc) {
            struct status_page page={.m=m,.load_guid=load_guid};
            rc=device_rows(root,"","pool",0,&page);
            if(rc==ECANCELED&&page.full)rc=0;
            /* Equality is the exhausted live status position. A cursor past
             * every topology/metric row is malformed or stale, not EOF. */
            if(!rc&&!page.full&&m->cursor>page.index)rc=EINVAL;
            if(!rc&&m->cursor==0&&m->row_count&&
                nvlist_exists(config,ZPOOL_CONFIG_SUSPENDED))strcpy(m->rows[0].state,"SUSPENDED");
        }
    }
    nvlist_free(config);status_trace("done");return rc;
}
