/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <libzutil.h>
#include "../../lib/libzutil/zutil_import.h"
#include <aros/debug.h>

/* Called only after the disposable public mirror has been exported. */
int oz_upstream_import_files_test(const char *directory)
{
    pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    libpc_handle_t hdl={0};
    uint64_t pool_guid=0,first_vdev=0;
    int error=0;
    for(unsigned i=0;i<2&&!error;i++) {
        char path[512];snprintf(path,sizeof(path),"%s/disk%u",directory,i);
        rdsk_node_t node={.rn_name=path,.rn_hdl=&hdl,.rn_lock=&lock};
        zpool_open_func(&node);
        uint64_t guid=0,pool=0;const char *name=NULL;
        if(!node.rn_config||node.rn_num_labels<1||
           nvlist_lookup_uint64(node.rn_config,ZPOOL_CONFIG_GUID,&guid)||
           nvlist_lookup_uint64(node.rn_config,ZPOOL_CONFIG_POOL_GUID,&pool)||
           nvlist_lookup_string(node.rn_config,ZPOOL_CONFIG_POOL_NAME,&name)||
           strcmp(name,"public_mirror")||!guid||!pool)error=EIO;
        if(!error&&i&&(pool!=pool_guid||guid==first_vdev))error=EIO;
        if(!i){pool_guid=pool;first_vdev=guid;}
        nvlist_free(node.rn_config);node.rn_config=NULL;node.rn_num_labels=0;
        if(error)break;
        node.rn_vdev_guid=guid;
        zpool_open_func(&node);
        if(!node.rn_config||!node.rn_num_labels)error=EIO;
        nvlist_free(node.rn_config);node.rn_config=NULL;node.rn_num_labels=0;
        node.rn_vdev_guid=guid^1;
        if(!node.rn_vdev_guid)node.rn_vdev_guid=guid^2;
        zpool_open_func(&node);
        if(node.rn_config||node.rn_num_labels)error=EIO;
        nvlist_free(node.rn_config);
    }
    char missing[512];snprintf(missing,sizeof(missing),"%s/missing",directory);
    const char *rejected[]={directory,missing};
    for(unsigned i=0;i<2&&!error;i++) {
        rdsk_node_t node={.rn_name=(char *)rejected[i],.rn_hdl=&hdl,.rn_lock=&lock};
        zpool_open_func(&node);
        if(node.rn_config||node.rn_num_labels)error=EIO;
        nvlist_free(node.rn_config);
    }
    pthread_mutex_destroy(&lock);
    if(!error)bug("Public upstream file discovery PASS members=2 identity=1 mismatch=1 directory=1 missing=1\n");
    return error;
}
