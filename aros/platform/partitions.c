/* SPDX-License-Identifier: CDDL-1.0 */
#include "partitions.h"
#include "../engine/device-io.h"
#include <proto/exec.h>
#include <proto/partition.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int oz_partition_catalog_read(const char *device,int32_t unit,
    struct oz_partition_catalog *output)
{
    if(!device||!*device||strlen(device)>255||unit<0||!output)return EINVAL;
    struct PartitionBase *PartitionBase=(struct PartitionBase *)OpenLibrary(
        (CONST_STRPTR)"partition.library",3);
    if(!PartitionBase)return ENOSYS;
    struct PartitionHandle *root=OpenRootPartition((CONST_STRPTR)device,unit);
    if(!root){CloseLibrary((struct Library *)PartitionBase);return ENODEV;}
    struct oz_partition_catalog *catalog=calloc(1,sizeof(*catalog));
    int error=catalog?0:ENOMEM,opened=0;
    if(error)goto done;
    catalog->version=OZ_PARTITION_CATALOG_VERSION;
    catalog->sector_size=root->dg.dg_SectorSize;
    catalog->disk_sectors=root->dg.dg_TotalSectors;
    /* Saturation is unknown capacity, not permission to clamp or trust a
     * partition header's self-reported size. Query the actual device. */
    if(catalog->disk_sectors>=0xfffffff0U) {
        struct oz_device *disk=NULL;
        error=oz_device_open(device,(unsigned)unit,0,&disk);
        if(error)goto done;
        if(oz_device_sector(disk)!=catalog->sector_size)error=EIO;
        else catalog->disk_sectors=oz_device_size(disk)/catalog->sector_size;
        oz_device_close(disk);
        if(error)goto done;
    }
    if(catalog->sector_size<512||catalog->sector_size>4096||
       (catalog->sector_size&(catalog->sector_size-1))||
       !catalog->disk_sectors) {
        error=ENOTSUP;goto done;
    }
    if(OpenPartitionTable(root)){error=EIO;goto done;}
    opened=1;
    ULONG table_type=0;
    struct TagItem table_tags[]={{PTT_TYPE,(IPTR)&table_type},{TAG_DONE,0}};
    /* Unlike GetPartitionAttrs, the table query returns true on success. */
    if(!GetPartitionTableAttrs(root,table_tags)||!root->table||
       table_type<PHPTT_RDB||table_type>PHPTT_GPT){error=ENOTSUP;goto done;}
    catalog->table_type=table_type;
    struct Node *node;
    for(node=root->table->list.lh_Head;node->ln_Succ;node=node->ln_Succ) {
        if(catalog->count==OZ_PARTITION_CATALOG_MAX){error=E2BIG;goto done;}
        struct oz_partition_entry *entry=&catalog->entries[catalog->count];
        struct PartitionType type={0};ULONG position=UINT32_MAX;
        UQUAD first=UINT64_MAX,last=UINT64_MAX;
        struct TagItem tags[]={{PT_STARTBLOCK,(IPTR)&first},{PT_ENDBLOCK,(IPTR)&last},
            {PT_POSITION,(IPTR)&position},{PT_TYPE,(IPTR)&type},
            {PT_NAME,(IPTR)entry->name},{TAG_DONE,0}};
        if(GetPartitionAttrs((struct PartitionHandle *)node,tags)||
           first>last||last>=catalog->disk_sectors||position==UINT32_MAX||
           type.id_len>sizeof(entry->type)||!memchr(entry->name,0,sizeof(entry->name))) {
            error=EIO;goto done;
        }
        entry->first_sector=first;entry->last_sector=last;
        entry->position=position;entry->type_length=type.id_len;
        memcpy(entry->type,type.id,type.id_len);
        /* Siblings describe disjoint portions of this virtual disk. A
         * container may contain a nested table, but may not overlap a sibling.
         * Reject the entire catalog rather than exposing ambiguous extents. */
        for(uint32_t i=0;i<catalog->count;i++) {
            const struct oz_partition_entry *other=&catalog->entries[i];
            if(first<=other->last_sector&&other->first_sector<=last) {
                error=EIO;goto done;
            }
        }
        catalog->count++;
    }
    memcpy(output,catalog,sizeof(*catalog));
done:
    free(catalog);
    if(opened)ClosePartitionTable(root);
    CloseRootPartition(root);CloseLibrary((struct Library *)PartitionBase);
    return error;
}
