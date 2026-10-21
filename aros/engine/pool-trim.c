/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/fs/zfs.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"
#include "split-config.h"

static int trim_options(const char *text,uint64_t *guids,unsigned *count,
    uint64_t *rate,boolean_t *partial,boolean_t *secure)
{
    char copy[1024];unsigned seen=0;
    if(strlcpy(copy,text,sizeof(copy))>=sizeof(copy))return EINVAL;
    *rate=0;*partial=B_FALSE;*secure=B_FALSE;*count=0;
    for(char *p=copy;p;) {
        char *next=strchr(p,';');if(next)*next++=0;
        unsigned bit;int error=0;
        if(!strncmp(p,"guids=",6)){bit=1;error=oz_split_guids(p,guids,count);}
        else if(!strncmp(p,"rate=",5)) {
            bit=2;const char *s=p+5;uint64_t value=0;
            if(!*s)return EINVAL;
            for(;*s;s++){
                if(*s<'0'||*s>'9')return EINVAL;
                unsigned digit=*s-'0';
                if(value>(UINT64_MAX-digit)/10)return ERANGE;
                value=value*10+digit;
            }
            *rate=value;
        } else if(!strncmp(p,"partial=",8)){
            bit=4;
            if(!strcmp(p+8,"on"))*partial=B_TRUE;
            else if(strcmp(p+8,"off"))return EINVAL;
        } else if(!strncmp(p,"secure=",7)){
            bit=8;
            if(!strcmp(p+7,"on"))*secure=B_TRUE;
            else if(strcmp(p+7,"off"))return EINVAL;
        } else return EINVAL;
        if(error)return error;
        if(seen&bit)return EINVAL;
        seen|=bit;p=next;
    }
    return (seen&1)?0:EINVAL;
}

int oz_pool_trim(struct oz_message *m,cred_t *caller)
{
    m->detail[0]=0;m->row_count=0;
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(m->flags!=OZ_CONFIRMED||pool_namecheck(m->target,NULL,NULL)||
        m->topology.group_count||m->topology.device_count||m->topology.ashift)
        return EINVAL;
    uint64_t command;
    if(!strcmp(m->argument,"start"))command=POOL_TRIM_START;
    else if(!strcmp(m->argument,"suspend"))command=POOL_TRIM_SUSPEND;
    else if(!strcmp(m->argument,"cancel"))command=POOL_TRIM_CANCEL;
    else return EINVAL;
    uint64_t guids[32],rate;unsigned count;boolean_t partial,secure;
    /* Same strict guids=decimal,... grammar, without split topology rules. */
    error=trim_options(m->options,guids,&count,&rate,&partial,&secure);if(error)return error;
    if(command!=POOL_TRIM_START&&(rate||partial||secure))return EINVAL;
    spa_t *spa=NULL;
    error=spa_open(m->target,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa)){error=EROFS;goto close;}
    if(spa_suspended(spa)){error=EAGAIN;goto close;}
    nvlist_t *devices=fnvlist_alloc(),*errors=fnvlist_alloc();
    for(unsigned i=0;i<count;i++) {
        char name[32];snprintf(name,sizeof(name),"%llu",(unsigned long long)guids[i]);
        fnvlist_add_uint64(devices,name,guids[i]);
    }
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(!error) {
        int failures=spa_vdev_trim(spa,devices,command,rate,partial,secure,errors);
        if(failures) {
            /* Upstream can already have accepted other devices. Never retry
             * the complete request automatically or hide partial failures. */
            error=EINVAL;
            size_t used=snprintf(m->detail,sizeof(m->detail),
                "TRIM request has %d device error(s); other devices may have changed. ",failures);
            for(nvpair_t *p=nvlist_next_nvpair(errors,NULL);p;p=nvlist_next_nvpair(errors,p)) {
                int64_t code=fnvpair_value_int64(p);
                if(used<sizeof(m->detail))used+=snprintf(m->detail+used,sizeof(m->detail)-used,
                    "GUID %s error=%lld; ",nvpair_name(p),(long long)code);
                if(m->row_count<OZ_MAX_ROWS) {
                    struct oz_row *r=&m->rows[m->row_count++];memset(r,0,sizeof(*r));
                    strcpy(r->kind,"trim-error");strlcpy(r->name,nvpair_name(p),sizeof(r->name));
                    strlcpy(r->parent,m->target,sizeof(r->parent));strcpy(r->state,"ERROR");
                    snprintf(r->value,sizeof(r->value),"error=%lld",(long long)code);
                }
            }
            /* The complete <=32-device error list fits the detail buffer;
             * rows are a bounded convenience view, not the full error set. */
            if(used<sizeof(m->detail))snprintf(m->detail+used,sizeof(m->detail)-used,
                "Authenticated TRIM control returned errno=%d",error);
        }
    }
    if(scope)VERIFY0(oz_cred_pop(scope));
    fnvlist_free(devices);fnvlist_free(errors);
close:
    spa_close(spa,FTAG);return error;
}
