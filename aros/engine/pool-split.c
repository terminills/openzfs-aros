/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/aros_vdev_policy.h>
#include <zfs_namecheck.h>
#include "../ui/api.h"
#include "split-config.h"
#include "import-platform.h"
int oz_cli_pool_export_check(const char *,cred_t *);

/* Retain a named rule if upstream leaves the destination imported on an export
 * failure. Normal split success exports it before returning. Service task only. */
static struct split_guard {
    struct split_guard *next;
    char name[256];
    char source[256];
    struct oz_vdev_policy *policy;
} *guards;
/* A retained destination borrows the source's native bindings and exclusive
 * file reservations. They cannot be released until that destination unloads. */
int oz_pool_split_source_busy(const char *source)
{
    for(struct split_guard *g=guards;g;g=g->next)
        if(!strcmp(source,g->source))return EBUSY;
    return 0;
}
/* Upstream CLI already supplies a split configuration. Validate its leaf
 * identities against the live source before authorizing the new pool name.
 * Existing source bindings remain retained; this admits no new backing. */
int oz_cli_split_begin(const char *source,const char *destination,
    const void *bytes,size_t size,cred_t *caller,void **out)
{
    *out=NULL;
    int error=oz_cli_pool_export_check(source,caller);
    if(error)return error;
    if(pool_namecheck(destination,NULL,NULL)||!strcmp(source,destination))return EINVAL;
    spa_t *spa=NULL;nvlist_t *config=NULL,*tree=NULL;struct split_guard *guard=NULL;
    error=spa_open(source,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa))error=EAGAIN;
    spa_namespace_enter(FTAG);
    if(!error&&spa_lookup(destination))error=EEXIST;
    spa_namespace_exit(FTAG);
    if(!error)error=nvlist_unpack((char *)bytes,size,&config,0);
    if(!error)error=nvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,&tree);
    nvlist_t **children=NULL;unsigned count=0,path_count=0;char *paths[32];
    if(!error)error=nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&count);
    if(!error&&(!count||count>32||nvlist_exists(tree,ZPOOL_CONFIG_SPARES)||
        nvlist_exists(tree,ZPOOL_CONFIG_L2CACHE)))error=EINVAL;
    spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
    for(unsigned i=0;!error&&i<count;i++) {
        const char *type,*path;uint64_t guid;
        if(nvlist_lookup_string(children[i],ZPOOL_CONFIG_TYPE,&type)){error=EINVAL;break;}
        if(!strcmp(type,VDEV_TYPE_HOLE)||!strcmp(type,VDEV_TYPE_INDIRECT)) {
            /* These require their own exact topology validation, not a
             * caller-supplied object silently excluded from path admission. */
            error=ENOTSUP;break;
        }
        if((strcmp(type,VDEV_TYPE_FILE)&&strcmp(type,VDEV_TYPE_DISK))||
            nvlist_exists(children[i],ZPOOL_CONFIG_CHILDREN)||
            nvlist_lookup_string(children[i],ZPOOL_CONFIG_PATH,&path)||
            nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_GUID,&guid)) {error=EINVAL;break;}
        vdev_t *vd=spa_lookup_by_guid(spa,guid,B_FALSE);
        if(!vd||vd->vdev_children||!vd->vdev_path||strcmp(path,vd->vdev_path)||
            strcmp(type,vd->vdev_ops->vdev_op_type)){error=EINVAL;break;}
        for(unsigned j=0;j<path_count;j++)if(!strcmp(path,paths[j]))error=EINVAL;
        if(!error)error=oz_vdev_policy_check_named(spa->spa_config_guid,source,path);
        if(!error)paths[path_count++]=(char *)path;
    }
    spa_config_exit(spa,SCL_CONFIG,FTAG);
    struct oz_import_paths allowed={.paths=paths,.count=path_count};
    if(!error)error=oz_import_config_check(&allowed,config);
    if(!error){guard=calloc(1,sizeof(*guard));if(!guard)error=ENOMEM;}
    if(!error){strlcpy(guard->name,destination,sizeof(guard->name));
        strlcpy(guard->source,source,sizeof(guard->source));
        error=oz_vdev_policy_begin_named(destination,paths,path_count,&guard->policy);}
    nvlist_free(config);spa_close(spa,FTAG);
    if(error){if(guard){oz_vdev_policy_end(guard->policy,NULL,NULL);free(guard);}return error;}
    *out=guard;return 0;
}
int oz_cli_split_end(void *scope,int error)
{
    struct split_guard *guard=scope;if(!guard)return error;
    spa_namespace_enter(FTAG);int retained=spa_lookup(guard->name)!=NULL;spa_namespace_exit(FTAG);
    if(retained){guard->next=guards;guards=guard;return error?error:EBUSY;}
    oz_vdev_policy_end(guard->policy,NULL,NULL);free(guard);return error;
}
void oz_pool_split_release(const char *name)
{
    struct split_guard **link=&guards;
    while(*link) {
        struct split_guard *g=*link;
        if(!name||!strcmp(name,g->name)) {
            *link=g->next;oz_vdev_policy_end(g->policy,NULL,NULL);free(g);
        } else link=&g->next;
    }
}
int oz_pool_split(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller);
    if(error)return error;
    uint64_t guids[32];unsigned count;
    if(m->flags!=OZ_CONFIRMED||pool_namecheck(m->target,NULL,NULL)||
        pool_namecheck(m->argument,NULL,NULL)||!strcmp(m->target,m->argument)||
        m->topology.group_count||m->topology.device_count||m->topology.ashift)
        return EINVAL;
    if((error=oz_split_guids(m->options,guids,&count)))return error;
    spa_t *spa=NULL;nvlist_t *source=NULL,*config=NULL,*tree=NULL;
    struct oz_cred_scope *scope=NULL;
    struct split_guard *guard=calloc(1,sizeof(*guard));
    if(!guard)return ENOMEM;
    strlcpy(guard->name,m->argument,sizeof(guard->name));
    strlcpy(guard->source,m->target,sizeof(guard->source));
    error=spa_open(m->target,&spa,FTAG);if(error)goto done;
    if(!spa_writeable(spa)){error=EROFS;goto done;}
    if(spa_suspended(spa)){error=EAGAIN;goto done;}
    spa_namespace_enter(FTAG);
    error=spa_lookup(m->argument)?EEXIST:0;
    spa_namespace_exit(FTAG);
    if(error)goto done;
    spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
    source=spa_config_generate(spa,NULL,-1ULL,B_TRUE);
    spa_config_exit(spa,SCL_CONFIG,FTAG);
    error=nvlist_lookup_nvlist(source,ZPOOL_CONFIG_VDEV_TREE,&tree);
    if(!error)error=oz_split_config(tree,guids,count,&config);
    if(error)goto done;
    nvlist_t **children;unsigned child_count,path_count=0;
    char *paths[32];
    tree=fnvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE);
    error=nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&child_count);
    if(error)goto done;
    for(unsigned i=0;i<child_count;i++) {
        const char *type=fnvlist_lookup_string(children[i],ZPOOL_CONFIG_TYPE);
        if(!strcmp(type,VDEV_TYPE_HOLE)||!strcmp(type,VDEV_TYPE_INDIRECT))continue;
        const char *path;
        if(nvlist_lookup_string(children[i],ZPOOL_CONFIG_PATH,&path)){error=EINVAL;goto done;}
        error=oz_vdev_policy_check_named(spa->spa_config_guid,spa_name(spa),path);
        if(error)goto done;
        paths[path_count++]=(char *)path;
    }
    error=oz_vdev_policy_begin_named(m->argument,paths,path_count,&guard->policy);
    if(error)goto done;
    error=oz_cred_push(caller,&scope);
    if(error)goto done;
    /* The destination stays exported: no implicit mounting or discovery. */
    error=spa_vdev_split_mirror(spa,m->argument,config,NULL,B_TRUE);
    spa_namespace_enter(FTAG);
    int retained=spa_lookup(m->argument)!=NULL;
    spa_namespace_exit(FTAG);
    if(retained) {
        guard->next=guards;guards=guard;guard=NULL;
        if(!error)error=EBUSY;
    }
done:
    if(scope)VERIFY0(oz_cred_pop(scope));
    if(guard){oz_vdev_policy_end(guard->policy,NULL,NULL);free(guard);}
    nvlist_free(source);nvlist_free(config);
    if(spa)spa_close(spa,FTAG);
    return error;
}
