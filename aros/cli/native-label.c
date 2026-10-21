/* SPDX-License-Identifier: CDDL-1.0 */
#include "native-label.h"
#include "device-path.h"
#include "../ui/partition-api.h"
#include "../ui/admission-api.h"
#include <errno.h>
#include <stdlib.h>

struct native_reader {
    struct oz_device_path path;
    struct oz_ticket session;
    LONG descriptor;
    uint64_t base,size;
    int error;
};
int oz_native_pool_in_use(libzfs_handle_t *hdl,const char *path,
    pool_state_t *state,char **name,boolean_t *inuse)
{
    if(!hdl||!state||!name||!inuse)return EINVAL;
    *name=NULL;*inuse=B_FALSE;
    nvlist_t *config=NULL;int labels=0;
    int error=oz_native_read_label(path,&config,&labels);
    if(error)return error;
    errno=0;
    return zpool_in_use_label(hdl,config,state,name,inuse)?(errno?errno:EIO):0;
}
static int read_label_bytes(void *arg,void *out,size_t count,uint64_t offset)
{
    struct native_reader *r=arg;
    if(r->error)return r->error;
    if(offset>r->size||count>r->size-offset||count>OZ_DEVICE_READ_MAX||
       offset>UINT64_MAX-r->base)return r->error=EINVAL;
    return r->error=oz_device_handle_read(OZ_ADMISSION_PORT,&r->session,
        r->descriptor,r->base+offset,out,(ULONG)count);
}
int oz_native_read_label(const char *path,nvlist_t **config,int *labels)
{return oz_native_read_label_session(path,NULL,config,labels);}
int oz_native_read_label_session(const char *path,const struct oz_ticket *session,
    nvlist_t **config,int *labels)
{
    if(!config||!labels)return EINVAL;
    *config=NULL;*labels=0;
    struct native_reader r={.descriptor=-1};
    int opened=0;
    int error=oz_device_path_parse(path,&r.path);
    struct oz_device_geometry geometry;
    if(!error&&session)r.session=*session;
    else if(!error){error=oz_session_open(OZ_ADMISSION_PORT,&r.session);opened=!error;}
    if(!error)error=oz_device_handle_open(OZ_ADMISSION_PORT,&r.session,
        r.path.device,r.path.unit,&r.descriptor,&geometry);
    if(error)goto done;
    r.size=geometry.bytes;
    if(r.path.partition) {
        struct oz_partition_catalog *c=malloc(sizeof(*c));
        if(!c){error=ENOMEM;goto done;}
        error=oz_partitions_read(OZ_ADMISSION_PORT,r.path.device,r.path.unit,c);
        if(!error&&(c->sector_size!=geometry.sector_size||
           c->disk_sectors!=geometry.bytes/geometry.sector_size))error=EIO;
        unsigned found=0;
        for(uint32_t i=0;!error&&i<c->count;i++) {
            const struct oz_partition_entry *p=&c->entries[i];
            if(p->position!=r.path.position)continue;
            if(++found!=1||p->first_sector>p->last_sector||p->last_sector>=c->disk_sectors)error=EIO;
            else {
                r.base=p->first_sector*c->sector_size;
                r.size=(p->last_sector-p->first_sector+1)*c->sector_size;
            }
        }
        if(!error&&!found)error=ENOENT;
        free(c);
        if(error)goto done;
    }
    int result=zpool_read_label_from(r.size,read_label_bytes,&r,config,labels);
    error=r.error?r.error:(result?(errno?errno:ENOMEM):0);
done:
    if(r.descriptor>0){int closed=oz_control_close(OZ_ADMISSION_PORT,&r.session,r.descriptor);if(!error)error=closed;}
    if(opened){int closed=oz_session_close(OZ_ADMISSION_PORT,&r.session);if(!error)error=closed;}
    /* A denied/disappeared device must not masquerade as a blank disk or
     * leave a partial configuration that could influence write admission. */
    if(error){nvlist_free(*config);*config=NULL;*labels=0;}
    return error;
}
