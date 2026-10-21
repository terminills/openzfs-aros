/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PARTITION_API_H
#define OZ_PARTITION_API_H
#include "../platform/partitions.h"
#include <exec/types.h>
#include "admission-api.h"
#define OZ_PARTITION_ACTION 0x4f5a5054L
#define OZ_PARTITION_QUERY_VERSION 1
#define OZ_DEVICE_GEOMETRY_ACTION 0x4f5a5044L
#define OZ_DEVICE_INQUIRY_ACTION 0x4f5a5049L
#define OZ_DEVICE_DOS_CLAIM_ACTION 0x4f5a504aL
/* Root-authorized snapshot only: EBUSY for a known DOS claim; zero is not
 * exclusive ownership or permission to write. Uses oz_partition_request. */
int oz_device_dos_claim_request(const char *,void *,ULONG);
int oz_device_dos_claim_read(const char *,const char *,int32_t);
#define OZ_STORAGE_INVENTORY_ACTION 0x4f5a5354L
#define OZ_STORAGE_INVENTORY_MAX 64
struct oz_storage_unit {
    uint32_t unit,type,removable,reserved;
    char device[256],model[128],serial[128];
};
struct oz_storage_inventory {
    uint32_t version,count;
    struct oz_storage_unit units[OZ_STORAGE_INVENTORY_MAX];
};
struct oz_storage_inventory_query {
    uint32_t version,reserved;
    struct oz_storage_inventory inventory;
};
int oz_storage_inventory_native(struct oz_storage_inventory *);
int oz_storage_inventory_request(const char *,void *,ULONG);
int oz_storage_inventory_read(const char *,struct oz_storage_inventory *);
#define OZ_DEVICE_READ_ACTION 0x4f5a5052L
#define OZ_DEVICE_READ_MAX (1024UL * 1024UL)
struct oz_partition_request {
    uint32_t version,reserved;
    int32_t unit;
    uint32_t reserved2;
    char device[256];
};
struct oz_partition_query {
    struct oz_partition_request request;
    struct oz_partition_catalog catalog;
};
struct oz_device_geometry {
    uint32_t version,sector_size;
    uint64_t bytes;
};
struct oz_device_geometry_query {
    struct oz_partition_request request;
    struct oz_device_geometry geometry;
};
struct oz_device_inquiry {
    uint32_t version,length;
    unsigned char data[96];
};
struct oz_device_inquiry_query {
    struct oz_partition_request request;
    struct oz_device_inquiry inquiry;
};
int oz_device_inquiry_request(const char *,void *,ULONG);
int oz_device_inquiry_read(const char *,const char *,int32_t,struct oz_device_inquiry *);
#define OZ_DEVICE_HANDLE_OPEN 0x4f5a504fL
#define OZ_DEVICE_HANDLE_READ 0x4f5a5048L
/* Read-only open. Descriptor lifetime belongs to the authenticated session;
 * close with oz_control_close, or close the session. No write lease implied. */
struct oz_device_handle_open {
    struct oz_ticket session;
    struct oz_partition_request request;
    struct oz_device_geometry geometry;
    LONG descriptor;
    ULONG reserved;
};
struct oz_device_handle_read {
    struct oz_ticket session;
    uint64_t offset;
    LONG descriptor;
    ULONG count;
};
int oz_device_handle_open(const char *,const struct oz_ticket *,const char *,int32_t,
    LONG *,struct oz_device_geometry *);
int oz_device_handle_read(const char *,const struct oz_ticket *,LONG,uint64_t,void *,ULONG);
/* Sector-aligned, exact-length, read-only queries. Payload follows this header.
 * Each request opens the specified device once, with fresh caller credentials.
 * This is not a persistent identity token or permission to write/import. */
struct oz_device_read_query {
    struct oz_partition_request request;
    uint64_t offset;
    uint32_t count,reserved;
};
int oz_device_read_request(const char *,void *,ULONG);
int oz_device_read_bytes(const char *,const char *,int32_t,uint64_t,void *,ULONG);
/* Geometry is not a persistent hardware identifier or write lease. Blank
 * disks can answer this query even when no partition catalog is readable. */
int oz_device_geometry_request(const char *,void *,ULONG);
int oz_device_geometry_read(const char *,const char *,int32_t,
    struct oz_device_geometry *);
/* Read-only native root-authorized discovery; never grants write ownership.
 * Catalog is ignored on input and remains unchanged on any service error. */
int oz_partitions_request(const char *,void *,ULONG);
int oz_partitions_read(const char *,const char *,int32_t,
    struct oz_partition_catalog *);
#endif
