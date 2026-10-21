/* SPDX-License-Identifier: CDDL-1.0 */
#include "partition-io.h"
#include "../platform/partitions.h"
#include <stdlib.h>
#include <errno.h>
int oz_device_open_partition(const char *name,unsigned unit,uint32_t position,
    int writable,struct oz_device **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(unit>INT32_MAX)return EINVAL;
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog)return ENOMEM;
    int error=oz_partition_catalog_read(name,unit,catalog);
    const struct oz_partition_entry *selected=NULL;
    if(!error) {
        for(uint32_t i=0;i<catalog->count;i++)if(catalog->entries[i].position==position) {
            if(selected){error=EIO;break;}
            selected=&catalog->entries[i];
        }
        if(!error&&!selected)error=ENOENT;
    }
    if(!error) {
        uint64_t base=selected->first_sector*catalog->sector_size;
        uint64_t length=(selected->last_sector-selected->first_sector+1)*catalog->sector_size;
        struct oz_device *device=NULL;
        error=oz_device_open_range(name,unit,writable,base,length,&device);
        if(!error&&oz_device_sector(device)!=catalog->sector_size)error=EIO;
        if(error)oz_device_close(device);else *out=device;
    }
    free(catalog);return error;
}
