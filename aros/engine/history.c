/* SPDX-License-Identifier: CDDL-1.0 */
/* Upstream history storage/record decoding, bounded native response pages. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <sys/fs/zfs.h>
#include <zfs_namecheck.h>
#include <libzutil.h>
#include "../ui/api.h"

static int history_read(spa_t *spa,uint64_t *offset,uint64_t *size,char *bytes)
{
    if(!spa->spa_history)return ENOENT;
    dmu_buf_t *bonus=NULL;
    int error=dmu_bonus_hold(spa->spa_meta_objset,spa->spa_history,FTAG,&bonus);
    if(error)return error;
    mutex_enter(&spa->spa_history_lock);
    uint64_t end=((spa_history_phys_t *)bonus->db_data)->sh_eof;
    mutex_exit(&spa->spa_history_lock);dmu_buf_rele(bonus,FTAG);
    if(*offset>end)return EINVAL;
    /* Offset zero retains upstream's initial asynchronous-history sync. */
    return spa_history_get(spa,offset,size,bytes);
}
static void summary_text(char *dest,size_t size,const char *text)
{
    if(!text)text="";
    size_t length=strlen(text),copy=MIN(length,size-1);
    memcpy(dest,text,copy);dest[copy]=0;
    if(length>=size&&size>4)memcpy(dest+size-4,"...",4);
}
/* Keep arbitrary CLI offsets out of the upstream ring-position arithmetic,
 * as the Manager reader above already does. The logical EOF only increases. */
int oz_cli_history_check(const char *name,uint64_t offset,cred_t *caller)
{
    int error=secpolicy_zfs(caller);if(error)return error;
    if(pool_namecheck(name,NULL,NULL))return EINVAL;
    spa_t *spa=NULL;error=spa_open(name,&spa,FTAG);if(error)return error;
    dmu_buf_t *bonus=NULL;
    if(spa_suspended(spa))error=EAGAIN;
    else if(!spa->spa_history)error=ENOENT;
    else error=dmu_bonus_hold(spa->spa_meta_objset,spa->spa_history,FTAG,&bonus);
    if(!error) {
        mutex_enter(&spa->spa_history_lock);
        if(offset>((spa_history_phys_t *)bonus->db_data)->sh_eof)error=EINVAL;
        mutex_exit(&spa->spa_history_lock);
    }
    if(bonus)dmu_buf_rele(bonus,FTAG);
    spa_close(spa,FTAG);return error;
}
int oz_pool_history(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller);if(error)return error;
    if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
    spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
    if(spa_version(spa)<SPA_VERSION_ZPOOL_HISTORY)error=ENOTSUP;
    else if(spa_suspended(spa))error=EAGAIN;
    uint64_t offset=m->cursor;
    for(unsigned i=0;!error&&i<OZ_MAX_ROWS;i++) {
        struct oz_row *row=&m->rows[i];
        uint64_t size=(m->flags&OZ_RAW)?96:8;
        char head[96];error=history_read(spa,&offset,&size,head);
        if(error||!size)break;
        uint64_t start=offset-size;
        if(m->flags&OZ_RAW) {
            strcpy(row->kind,"history.raw");strcpy(row->state,"hex bytes");
            snprintf(row->name,sizeof(row->name),"%llu",(unsigned long long)start);
            summary_text(row->parent,sizeof(row->parent),m->target);
            for(unsigned n=0;n<size;n++)snprintf(row->value+n*2,3,"%02x",(unsigned char)head[n]);
            row->total=size;m->row_count++;m->next_cursor=offset;continue;
        }
        if(size!=8){error=EIO;break;}
        uint64_t length=0;for(unsigned n=0;n<8;n++)length|=(uint64_t)(unsigned char)head[n]<<(8*n);
        /* Raw mode can retrieve larger records without unbounded allocation. */
        if(!length||length>1024*1024){error=EOVERFLOW;break;}
        char *record=malloc(length+8);if(!record){error=ENOMEM;break;}
        memcpy(record,head,8);size=length;uint64_t before=offset;
        error=history_read(spa,&offset,&size,record+8);
        if(!error&&(size!=length||offset-before!=length))error=ESTALE;
        nvlist_t **records=NULL;uint_t count=0;uint64_t leftover=0;
        if(!error)error=zpool_history_unpack(record,length+8,&leftover,&records,&count);
        free(record);
        if(!error&&(leftover||count!=1))error=EIO;
        if(!error) {
            nvlist_t *nv=records[0];const char *name=NULL,*text=NULL,*dataset=NULL;
            (void)nvlist_lookup_string(nv,ZPOOL_HIST_INT_NAME,&name);
            (void)nvlist_lookup_string(nv,ZPOOL_HIST_CMD,&text);
            if(text&&!name)name="command";
            if(!text)(void)nvlist_lookup_string(nv,ZPOOL_HIST_INT_STR,&text);
            if(!name)(void)nvlist_lookup_string(nv,ZPOOL_HIST_IOCTL,&name);
            (void)nvlist_lookup_string(nv,ZPOOL_HIST_DSNAME,&dataset);
            strcpy(row->kind,"history");summary_text(row->name,sizeof(row->name),name?name:"record");
            summary_text(row->parent,sizeof(row->parent),dataset?dataset:m->target);
            summary_text(row->value,sizeof(row->value),text);
            snprintf(row->state,sizeof(row->state),"offset=%llu",(unsigned long long)start);
            (void)nvlist_lookup_uint64(nv,ZPOOL_HIST_TIME,&row->used);
            (void)nvlist_lookup_uint64(nv,ZPOOL_HIST_WHO,&row->available);
            (void)nvlist_lookup_uint64(nv,ZPOOL_HIST_TXG,&row->total);
            m->row_count++;m->next_cursor=offset;
        }
        for(unsigned n=0;n<count;n++)nvlist_free(records[n]);
        free(records);
    }
    if(!error&&m->next_cursor) {
        char probe;uint64_t end=offset,size=1;
        error=history_read(spa,&end,&size,&probe);
        if(!error&&!size)m->next_cursor=0;
    }
    if(error){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
    spa_close(spa,FTAG);return error;
}
