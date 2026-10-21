/* SPDX-License-Identifier: CDDL-1.0 */
/* Native OS implementation of the upstream libzfs GPT entrypoints. The
 * caller has already selected/provisioned and checked the device, as for the
 * upstream OS entrypoints. This does not grant arbitrary raw-device ownership. */
#include "../../lib/libzfs/libzfs_impl.h"
#include <sys/vdev_disk.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <libraries/partition.h>
#include "device-path.h"
#include "../ui/gpt-api.h"
#include "../ui/partition-api.h"

static uint64_t start_from_config(nvlist_t *config)
{
    nvlist_t **children;uint_t count;
    if(!config)return 0;
    if(!nvlist_lookup_nvlist_array(config,ZPOOL_CONFIG_CHILDREN,&children,&count)) {
        for(uint_t i=0;i<count;i++) {
            uint64_t start=start_from_config(children[i]);
            if(start)return start;
        }
        return 0;
    }
    uint64_t whole=0;const char *path=NULL;
    struct oz_device_path parsed;
    if(nvlist_lookup_uint64(config,ZPOOL_CONFIG_WHOLE_DISK,&whole)||!whole||
       nvlist_lookup_string(config,ZPOOL_CONFIG_PATH,&path)||
       oz_device_path_parse(path,&parsed))return 0;
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog)return 0;
    uint64_t start=0;
    if(!oz_partitions_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,catalog)&&
       catalog->table_type==PHPTT_GPT)
        for(uint32_t i=0;i<catalog->count;i++)
            if(catalog->entries[i].position==0){start=catalog->entries[i].first_sector;break;}
    free(catalog);return start;
}
static int prepare(const char *path,struct oz_gpt_query *q)
{
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    if(error||parsed.partition)return error?error:EINVAL;
    struct oz_device_geometry geometry;
    error=oz_device_geometry_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,&geometry);
    if(error)return error;
    memset(q,0,sizeof(*q));q->version=OZ_GPT_VERSION;
    q->unit=parsed.unit;strcpy(q->device,parsed.device);
    q->sector_size=geometry.sector_size;q->bytes=geometry.bytes;
    return 0;
}
int zpool_label_disk(libzfs_handle_t *hdl,zpool_handle_t *pool,const char *name)
{
    if(!hdl){errno=EINVAL;return -1;}
    char path[320],message[512];
    snprintf(message,sizeof(message),"cannot label '%s'",name?name:"(null)");
    int n=name?snprintf(path,sizeof(path),"%s%s",name[0]=='/'?"":"/AROS/",name):-1;
    struct oz_gpt_query query;
    int error=n<0?EINVAL:(size_t)n>=sizeof(path)?ENAMETOOLONG:prepare(path,&query);
    if(!error) {
        uint64_t start=0;
        if(pool) {
            start=pool->zpool_start_block;
            if(!start) {
                nvlist_t *root=NULL;
                if(pool->zpool_config)nvlist_lookup_nvlist(pool->zpool_config,ZPOOL_CONFIG_VDEV_TREE,&root);
                start=start_from_config(root);
            }
        }
        if(!start||start==(uint64_t)MAXOFFSET_T)start=NEW_START_BLOCK;
        if(pool)pool->zpool_start_block=start;
        query.start=start;query.operation=OZ_GPT_LABEL;
        query.flags=OZ_GPT_CONFIRMED|OZ_GPT_REPLACE_TABLE;
        /* Labels are descriptive, not secrets or device identity. Native
         * partition resolution uses positions/GUIDs, never Linux udev names. */
        snprintf(query.name,sizeof(query.name),"zfs-%016llx",
            (unsigned long long)(((uint64_t)rand()<<32)|(uint64_t)rand()));
        error=oz_gpt_request(OZ_ADMISSION_PORT,&query);
    }
    if(!error)return 0; /* service has flushed and independently rescanned */
    errno=error;
    zfs_error_aux(hdl,"native GPT label failed: %s",strerror(error));
    return zfs_error(hdl,EZFS_LABELFAILED,message);
}
int zpool_relabel_disk(libzfs_handle_t *hdl,const char *path,const char *message)
{
    if(!hdl){errno=EINVAL;return -1;}
    struct oz_gpt_query query;
    int error=prepare(path,&query);
    if(!error) {
        query.operation=OZ_GPT_EXPAND;query.flags=OZ_GPT_CONFIRMED;
        error=oz_gpt_request(OZ_ADMISSION_PORT,&query);
    }
    if(!error)return 0;
    errno=error;
    zfs_error_aux(hdl,"native GPT expansion failed: %s",strerror(error));
    return zfs_error(hdl,EZFS_NOCAP,message?message:"cannot expand native disk");
}
