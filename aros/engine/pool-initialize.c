/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/fs/zfs.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"
#include "split-config.h"

int oz_pool_initialize(struct oz_message *m,cred_t *caller)
{
    m->detail[0]=0;m->row_count=0;
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(m->flags!=OZ_CONFIRMED||pool_namecheck(m->target,NULL,NULL)||
        m->topology.group_count||m->topology.device_count||m->topology.ashift)
        return EINVAL;
    uint64_t command;
    if(!strcmp(m->argument,"start"))command=POOL_INITIALIZE_START;
    else if(!strcmp(m->argument,"suspend"))command=POOL_INITIALIZE_SUSPEND;
    else if(!strcmp(m->argument,"cancel"))command=POOL_INITIALIZE_CANCEL;
    else if(!strcmp(m->argument,"uninitialize"))command=POOL_INITIALIZE_UNINIT;
    else return EINVAL;
    uint64_t guids[32];unsigned count;
    /* Same strict guids=decimal,... grammar, without split topology rules. */
    error=oz_split_guids(m->options,guids,&count);if(error)return error;
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
        int failures=spa_vdev_initialize(spa,devices,command,errors);
        if(failures) {
            /* Upstream can already have accepted other devices. Never retry
             * the complete request automatically or hide partial failures. */
            error=EINVAL;
            size_t used=snprintf(m->detail,sizeof(m->detail),
                "Initialization request has %d device error(s); other devices may have changed. ",failures);
            for(nvpair_t *p=nvlist_next_nvpair(errors,NULL);p;p=nvlist_next_nvpair(errors,p)) {
                int64_t code=fnvpair_value_int64(p);
                if(used<sizeof(m->detail))used+=snprintf(m->detail+used,sizeof(m->detail)-used,
                    "GUID %s error=%lld; ",nvpair_name(p),(long long)code);
                if(m->row_count<OZ_MAX_ROWS) {
                    struct oz_row *r=&m->rows[m->row_count++];memset(r,0,sizeof(*r));
                    strcpy(r->kind,"initialize-error");strlcpy(r->name,nvpair_name(p),sizeof(r->name));
                    strlcpy(r->parent,m->target,sizeof(r->parent));strcpy(r->state,"ERROR");
                    snprintf(r->value,sizeof(r->value),"error=%lld",(long long)code);
                }
            }
            /* The complete <=32-device error list fits the detail buffer;
             * rows are a bounded convenience view, not the full error set. */
            if(used<sizeof(m->detail))snprintf(m->detail+used,sizeof(m->detail)-used,
                "Authenticated initialization control returned errno=%d",error);
        }
    }
    if(scope)VERIFY0(oz_cred_pop(scope));
    fnvlist_free(devices);fnvlist_free(errors);
close:
    spa_close(spa,FTAG);return error;
}
