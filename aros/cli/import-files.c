/* SPDX-License-Identifier: CDDL-1.0 */
/* Shared by the resident engine and upstream CLI. No SPA/DMU dependencies:
 * upstream libzutil owns label parsing and configuration reconstruction. */
#include <sys/zfs_context.h>
#include <sys/stat.h>
#include <sys/vdev_impl.h>
#include <libzutil.h>
#include "../../lib/libzutil/zutil_import.h"
#include <fcntl.h>
#ifdef OZ_NATIVE_LABEL_CLIENT
#include "native-label.h"
#else
#include "../engine/device-io.h"
struct engine_label_reader {struct oz_device *device;int error;};
static int engine_label_bytes(void *arg,void *out,size_t count,uint64_t offset)
{
    struct engine_label_reader *r=arg;
    if(r->error)return r->error;
    return r->error=oz_device_transfer(r->device,out,count,offset,0);
}
#endif
#ifdef OZ_IMPORT_DIAGNOSTICS
#include <proto/exec.h>
#define IMPORT_NOTE(...) do { char line[512];snprintf(line,sizeof(line),__VA_ARGS__); \
    for(const char *p=line;*p;p++)RawPutChar(*p);RawPutChar('\n'); }while(0)
#else
#define IMPORT_NOTE(...) ((void)0)
#endif

static void accept_label(rdsk_node_t *node,nvlist_t *config,int labels,int error)
{
    uint64_t guid=0;
    if(error||!labels||!config||nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&guid)||
        (node->rn_vdev_guid&&node->rn_vdev_guid!=guid)) {
        nvlist_free(config);return;
    }
    node->rn_config=config;node->rn_num_labels=labels;
    /* Never open additional paths merely because a label names them. */
}
void zpool_open_func(void *arg)
{
    rdsk_node_t *node=arg;
#ifdef OZ_NATIVE_LABEL_CLIENT
    /* Client-only RPC transport: the engine must not wait on its own port. */
    if(!strncmp(node->rn_name,"/AROS/",6)) {
        nvlist_t *config=NULL;int labels=0;
        int error=oz_native_read_label_session(node->rn_name,node->rn_native_session,&config,&labels);
        IMPORT_NOTE("Native label hook path=%s error=%d labels=%d config=%d",node->rn_name,error,labels,config!=NULL);
        if(error==EACCES||error==EPERM) {
            pthread_mutex_lock(node->rn_lock);
            node->rn_hdl->lpc_open_access_error=B_TRUE;
            pthread_mutex_unlock(node->rn_lock);
        }
        accept_label(node,config,labels,error);return;
    }
#else
    if(!strncmp(node->rn_name,"/AROS/",6)) {
        nvlist_t *config=NULL;int labels=0;
        struct engine_label_reader reader={0};
        int error=oz_device_bound_open(node->rn_name,0,&reader.device);
        if(!error) {
            int result=zpool_read_label_from(oz_device_size(reader.device),
                engine_label_bytes,&reader,&config,&labels);
            error=reader.error?reader.error:(result?(errno?errno:ENOMEM):0);
            oz_device_close(reader.device);
        }
        accept_label(node,config,labels,error);return;
    }
#endif
    struct stat64 st;
    /* Only explicit regular-file candidates are supported here. Device
     * discovery must use native identity/partition policy, not guessed paths. */
    int stat_error=stat64(node->rn_name,&st);
    IMPORT_NOTE("Label hook path=%s stat=%d errno=%d regular=%d size=%llu",node->rn_name,stat_error,errno,
        stat_error?0:S_ISREG(st.st_mode),(unsigned long long)(stat_error?0:st.st_size));
    if(stat_error||!S_ISREG(st.st_mode)||st.st_size<SPA_MINDEVSIZE)return;
    int fd=open(node->rn_name,O_RDONLY);
    IMPORT_NOTE("Label hook open=%d errno=%d",fd,errno);
    if(fd<0) {
        if(errno==EACCES) {
            pthread_mutex_lock(node->rn_lock);
            node->rn_hdl->lpc_open_access_error=B_TRUE;
            pthread_mutex_unlock(node->rn_lock);
        }
        return;
    }
    /* Revalidate the opened object instead of trusting only the path stat. */
    if(fstat64(fd,&st)||!S_ISREG(st.st_mode)||st.st_size<SPA_MINDEVSIZE) {
        close(fd);return;
    }
    nvlist_t *config=NULL;int labels=0;
    int error=zpool_read_label(fd,&config,&labels);
    IMPORT_NOTE("Label hook read=%d labels=%d config=%d",error,labels,config!=NULL);
    close(fd);
    accept_label(node,config,labels,error);
}

#ifdef OZ_NATIVE_LABEL_CLIENT
const char *const *zpool_default_search_paths(size_t *count)
{
    static const char *const paths[]={"/AROS",NULL};
    *count=1;return paths;
}
#endif

int zpool_find_import_blkid(libpc_handle_t *hdl,pthread_mutex_t *lock,avl_tree_t **cache)
{
#ifdef OZ_NATIVE_LABEL_CLIENT
    size_t count;
    const char *const *paths=zpool_default_search_paths(&count);
    return zpool_find_import_scan(hdl,lock,cache,paths,count);
#else
    (void)lock;*cache=NULL;
    hdl->lpc_error=LPC_BADPATH;
    snprintf(hdl->lpc_desc,sizeof(hdl->lpc_desc),"AROS discovery requires explicit regular-file paths; automatic device enumeration is not implemented");
    return ENOTSUP;
#endif
}

void update_vdev_config_dev_strs(nvlist_t *nv)
{
    const char *type;
    if(!nvlist_lookup_string(nv,ZPOOL_CONFIG_TYPE,&type)&&!strcmp(type,VDEV_TYPE_FILE)) {
        (void)nvlist_remove_all(nv,ZPOOL_CONFIG_DEVID);
        (void)nvlist_remove_all(nv,ZPOOL_CONFIG_PHYS_PATH);
    }
}

void update_vdevs_config_dev_sysfs_path(nvlist_t *config)
{
    /* Like upstream FreeBSD: no Linux sysfs namespace on AROS. */
    (void)config;
}
