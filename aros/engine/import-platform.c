/* SPDX-License-Identifier: CDDL-1.0 */
/* Admitted native-device and regular-file discovery hooks. Upstream libzutil owns
 * label parsing, pool/txg grouping and configuration reconstruction. */
#include <sys/zfs_context.h>
#include <sys/stat.h>
#include <sys/vdev_impl.h>
#include <sys/spa.h>
#include <libzutil.h>
#include "../../lib/libzutil/zutil_import.h"
#include <fcntl.h>
#include "import-platform.h"
#include <sys/aros_vdev_policy.h>
#ifdef OZ_IMPORT_DIAGNOSTICS
#include <proto/exec.h>
#define IMPORT_NOTE(...) do { char line[512];snprintf(line,sizeof(line),__VA_ARGS__); \
    for(const char *p=line;*p;p++)RawPutChar(*p);RawPutChar('\n'); }while(0)
#else
#define IMPORT_NOTE(...) ((void)0)
#endif

static int check_tree(struct oz_import_paths *allowed,nvlist_t *tree,
    unsigned depth,unsigned *nodes)
{
    const char *type,*path;
    if(depth>16||++*nodes>256||nvlist_lookup_string(tree,ZPOOL_CONFIG_TYPE,&type))
        return EINVAL;
    IMPORT_NOTE("Import tree depth=%u type=%s has-path=%d",depth,type,nvlist_exists(tree,ZPOOL_CONFIG_PATH));
    boolean_t disk=!strcmp(type,VDEV_TYPE_DISK);
    boolean_t leaf=!strcmp(type,VDEV_TYPE_FILE)||disk;
    boolean_t distributed=!strcmp(type,VDEV_TYPE_DRAID_SPARE);
    boolean_t absent=!strcmp(type,VDEV_TYPE_MISSING)||!strcmp(type,VDEV_TYPE_HOLE)||!strcmp(type,VDEV_TYPE_INDIRECT);
    if(!leaf&&!absent&&!distributed&&strcmp(type,VDEV_TYPE_ROOT)&&strcmp(type,VDEV_TYPE_MIRROR)&&
        strcmp(type,VDEV_TYPE_RAIDZ)&&strcmp(type,VDEV_TYPE_DRAID)&&
        strcmp(type,VDEV_TYPE_SPARE)&&strcmp(type,VDEV_TYPE_REPLACING))return ENOTSUP;
    if(leaf) {
        if(nvlist_lookup_string(tree,ZPOOL_CONFIG_PATH,&path))return EINVAL;
        if(disk&&strncmp(path,"/AROS/",6))return ENOTSUP;
        unsigned i;
        /* Do not stat/open a path before it has been explicitly admitted. */
        for(i=0;i<allowed->count;i++)if(!strcmp(path,allowed->paths[i]))break;
        if(i==allowed->count)return EACCES;
    } else if(distributed) {
        /* Upstream resolves this synthetic name by top-level GUID and spare
         * index. It never opens it as a file/device. Real descendants still
         * require exact caller-admitted paths above. */
        uint64_t guid,id;
        if(nvlist_lookup_string(tree,ZPOOL_CONFIG_PATH,&path)||
            nvlist_lookup_uint64(tree,ZPOOL_CONFIG_TOP_GUID,&guid)||!guid||
            nvlist_lookup_uint64(tree,ZPOOL_CONFIG_SPARE_ID,&id)||id>=100||
            strncmp(path,"draid",5)||path[5]<'1'||path[5]>'3'||path[6]!='-')return EINVAL;
        const char *p=path+7;
        if(*p<'0'||*p>'9')return EINVAL;
        while(*p>='0'&&*p<='9')p++;
        if(*p++!='-'||*p<'0'||*p>'9')return EINVAL;
        while(*p>='0'&&*p<='9')p++;
        if(*p)return EINVAL;
    } else if(nvlist_exists(tree,ZPOOL_CONFIG_PATH))return EINVAL;
    const char *arrays[]={ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
    for(unsigned a=0;a<3;a++) {
        if(!nvlist_exists(tree,arrays[a]))continue;
        nvlist_t **children;uint_t count;
        if(nvlist_lookup_nvlist_array(tree,arrays[a],&children,&count)||count>32)return EINVAL;
        IMPORT_NOTE("Import tree type=%s array=%s count=%u",type,arrays[a],count);
        /* Upstream emits an empty children array for an indirect vdev.
         * It has no external path and must not contain real descendants. */
        if((leaf||absent||distributed)&&count)return EINVAL;
        for(unsigned i=0;i<count;i++) {
            int error=check_tree(allowed,children[i],depth+1,nodes);
            if(error)return error;
        }
    }
    return 0;
}
int oz_import_config_check(struct oz_import_paths *allowed,nvlist_t *config)
{
    nvlist_t *tree;unsigned nodes=0;
    if(!allowed||!allowed->paths||!allowed->count||allowed->count>32||!config)
        return EINVAL;
    for(unsigned i=0;i<allowed->count;i++)
        if(!allowed->paths[i]||!allowed->paths[i][0])return EINVAL;
    if(nvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,&tree))return EINVAL;
    return check_tree(allowed,tree,0,&nodes);
}
static nvlist_t *refresh_config(void *context,nvlist_t *config)
{
    struct oz_import_paths *allowed=context;
    int error=oz_import_config_check(allowed,config);
    if(error){if(allowed)allowed->rejected=error;return NULL;}
    uint64_t guid;struct oz_vdev_policy *policy=NULL;
    error=nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&guid);
    if(!error)error=oz_vdev_policy_begin(guid,allowed->paths,allowed->count,&policy);
    if(error){allowed->rejected=error;return NULL;}
    nvlist_t *refreshed=spa_tryimport(config);
    unsigned denied=0;
    oz_vdev_policy_end(policy,NULL,&denied);
    if(denied){allowed->rejected=EACCES;nvlist_free(refreshed);return NULL;}
    if(refreshed&&(error=oz_import_config_check(allowed,refreshed))) {
        allowed->rejected=error;nvlist_free(refreshed);return NULL;
    }
    return refreshed;
}
static int pool_active(void *unused,const char *name,uint64_t guid,boolean_t *active)
{
    (void)unused;(void)name;
    *active=B_FALSE;
    /* This engine is the service's actual pool namespace. Do not query a
     * nonexistent Linux /dev/zfs, or report every pool inactive. */
    spa_namespace_enter(FTAG);
    for(spa_t *spa=spa_next(NULL);spa;spa=spa_next(spa))
        if(spa_guid(spa)==guid){*active=B_TRUE;break;}
    spa_namespace_exit(FTAG);
    return 0;
}
pool_config_ops_t oz_aros_config_ops={refresh_config,pool_active};
