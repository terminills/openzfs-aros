/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_NATIVE_PARTITIONS_H
#define OZ_NATIVE_PARTITIONS_H
#include <stdint.h>
#define OZ_PARTITION_CATALOG_VERSION 1
#define OZ_PARTITION_CATALOG_MAX 128
struct oz_partition_entry {
    uint64_t first_sector,last_sector;
    uint32_t position,type_length;
    unsigned char type[32];
    char name[128];
};
struct oz_partition_catalog {
    uint32_t version,table_type,sector_size,count;
    uint64_t disk_sectors;
    struct oz_partition_entry entries[OZ_PARTITION_CATALOG_MAX];
};
/* Read immediate children of an explicitly named device's root table.
 * Positions are partition.library positions, not invented Unix disk names.
 * Output is unchanged on failure. No writable device admission is implied. */
int oz_partition_catalog_read(const char *,int32_t,struct oz_partition_catalog *);
#endif
