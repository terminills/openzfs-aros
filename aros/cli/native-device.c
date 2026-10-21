/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzutil.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <proto/dos.h>
#include <dos/dos.h>
#include <limits.h>
#include "device-path.h"
#include "native-device.h"
#include "../ui/partition-api.h"
#include "../ui/admission-api.h"
#include "../../cmd/zpool/zpool_sector_quirks.h"
#include "../../cmd/zpool/zpool_util.h"

int oz_native_vdev_size(const char *name,uint64_t *bytes,int *whole)
{
    struct oz_device_path path;
    if(!bytes||!whole)return EINVAL;
    int error=oz_device_path_parse(name,&path);
    if(error)return error;
    uint64_t size=0;
    if(!path.partition) {
        struct oz_device_geometry geometry;
        error=oz_device_geometry_read(OZ_ADMISSION_PORT,path.device,path.unit,&geometry);
        if(!error)size=geometry.bytes;
    } else {
        struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
        if(!catalog)return ENOMEM;
        error=oz_partitions_read(OZ_ADMISSION_PORT,path.device,path.unit,catalog);
        if(!error&&(catalog->version!=OZ_PARTITION_CATALOG_VERSION||
            catalog->count>OZ_PARTITION_CATALOG_MAX))error=EPROTO;
        if(!error) {
            error=ENOENT;
            for(uint32_t i=0;i<catalog->count;i++) {
                const struct oz_partition_entry *entry=&catalog->entries[i];
                if(entry->position!=path.position)continue;
                error=EINVAL;
                if(!catalog->sector_size||entry->last_sector<entry->first_sector||
                    entry->last_sector>=catalog->disk_sectors)break;
                uint64_t sectors=entry->last_sector-entry->first_sector+1;
                if(sectors>UINT64_MAX/catalog->sector_size){error=EOVERFLOW;break;}
                size=sectors*catalog->sector_size;error=0;break;
            }
        }
        free(catalog);
    }
    if(error)return error;
    if(!size)return EINVAL;
    *bytes=size;*whole=!path.partition;return 0;
}

static int native_catalog(const struct oz_device_path *path,struct oz_partition_catalog *catalog)
{
    int error=oz_partitions_read(OZ_ADMISSION_PORT,path->device,path->unit,catalog);
    if(error==EIO) {
        /* An independently readable zero first sector is an unpartitioned
         * candidate, not proof of an entirely empty disk or write ownership. */
        struct oz_device_geometry geometry;
        error=oz_device_geometry_read(OZ_ADMISSION_PORT,path->device,path->unit,&geometry);
        unsigned char *sector=NULL;
        if(!error&&!(sector=malloc(geometry.sector_size)))error=ENOMEM;
        if(!error)error=oz_device_read_bytes(OZ_ADMISSION_PORT,path->device,path->unit,0,sector,geometry.sector_size);
        if(!error)for(unsigned i=0;i<geometry.sector_size;i++)if(sector[i]){error=EIO;break;}
        free(sector);
        if(!error){memset(catalog,0,sizeof(*catalog));catalog->version=OZ_PARTITION_CATALOG_VERSION;}
    }
    return error;
}
void oz_native_import_paths_free(char **paths,size_t count)
{
    while(count)free(paths[--count]);
    free(paths);
}
int oz_native_import_paths(char ***out,size_t *count)
{
    if(!out||!count)return EINVAL;
    *out=NULL;*count=0;
    struct oz_storage_inventory *inventory=malloc(sizeof(*inventory));
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    const size_t capacity=OZ_STORAGE_INVENTORY_MAX*(OZ_PARTITION_CATALOG_MAX+1);
    char **paths=calloc(capacity+1,sizeof(*paths));size_t used=0;
    int error=(!inventory||!catalog||!paths)?ENOMEM:oz_storage_inventory_read(OZ_ADMISSION_PORT,inventory);
    for(uint32_t i=0;!error&&i<inventory->count;i++) {
        const struct oz_storage_unit *unit=&inventory->units[i];
        /* HIDD RAID array, fixed disk and SSD; exclude optical/tape/unknown. */
        if(unit->type<1||unit->type>3)continue;
        char path[320];struct oz_device_path parsed;
        int n=snprintf(path,sizeof(path),"/AROS/%s/%u",unit->device,unit->unit);
        if(n<0||(size_t)n>=sizeof(path)){error=ENAMETOOLONG;break;}
        error=oz_device_path_parse(path,&parsed);if(error)break;
        error=native_catalog(&parsed,catalog);if(error)break;
        if(used>=capacity){error=EOVERFLOW;break;}
        paths[used]=strdup(path);if(!paths[used]){error=ENOMEM;break;}used++;
        for(uint32_t j=0;!error&&j<catalog->count;j++) {
            char child[352];
            n=snprintf(child,sizeof(child),"%s/partition/%u",path,catalog->entries[j].position);
            if(n<0||(size_t)n>=sizeof(child)){error=ENAMETOOLONG;break;}
            if(used>=capacity){error=EOVERFLOW;break;}
            paths[used]=strdup(child);if(!paths[used]){error=ENOMEM;break;}used++;
        }
    }
    free(catalog);free(inventory);
    if(error){oz_native_import_paths_free(paths,used);return error;}
    *out=paths;*count=used;return 0;
}

int check_device(const char *path,boolean_t force,boolean_t isspare,
    boolean_t iswholedisk)
{
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    if(error){errno=error;return -1;}
    if(!g_zfs||!!iswholedisk==!!parsed.partition){errno=EINVAL;return -1;}
    /* Force can override exported pool membership, never a DOS unit claim. */
    error=oz_device_dos_claim_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit);
    if(error){errno=error;return -1;}
    /* Same upstream pool-membership policy as the file/FreeBSD path, with
     * native exact-extent reads instead of POSIX open on an invented /dev.
     * This is not an exclusive write lease or a foreign-filesystem probe. */
    if(check_file_generic(path,force,isspare))return -1;
    if(parsed.partition)return 0;
    /* Whole-disk labels alone do not reveal pools on GPT/MBR children.
     * Inspect each actual partition extent before accepting this preflight. */
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog){errno=ENOMEM;return -1;}
    error=native_catalog(&parsed,catalog);
    if(!error) {
        for(uint32_t i=0;i<catalog->count;i++) {
            char child[320];
            int n=snprintf(child,sizeof(child),"%s/partition/%u",path,catalog->entries[i].position);
            if(n<0||(size_t)n>=sizeof(child)){error=ENAMETOOLONG;break;}
            if(check_file_generic(child,force,isspare)){free(catalog);return -1;}
        }
    }
    free(catalog);
    if(error){errno=error;return -1;}
    return 0;
}

boolean_t check_sector_size_database(char *path,int *sector_size)
{
    if(!sector_size){errno=EINVAL;return B_FALSE;}
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    struct oz_device_inquiry inquiry;
    if(!error)error=oz_device_inquiry_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,&inquiry);
    if(error){errno=error;return B_FALSE;}
    /* A partition shares the physical device's sector quirks. Never replace
     * logical geometry with a guessed model, or modify ashift on no match. */
    for(size_t i=0;i<sizeof(vdev_disk_database)/sizeof(vdev_disk_database[0]);i++) {
        if(!memcmp(inquiry.data+8,vdev_disk_database[i].id,24)) {
            *sector_size=vdev_disk_database[i].sector_size;
            return B_TRUE;
        }
    }
    return B_FALSE;
}

static int directory_component(const char *path)
{
    BPTR lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(!lock) {
        int error=__stdc_ioerr2errno(IoErr());
        return error?error:EIO;
    }
    struct FileInfoBlock info;
    int error=0;
    if(!Examine(lock,&info)) {
        error=__stdc_ioerr2errno(IoErr());if(!error)error=EIO;
    } else if(info.fib_DirEntryType<=0)error=ENOTDIR;
    UnLock(lock);
    return error;
}

boolean_t zfs_dev_is_whole_disk(const char *path)
{
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    if(error){errno=error;return B_FALSE;}
    if(parsed.partition)return B_FALSE;
    struct oz_device_geometry geometry;
    error=oz_device_geometry_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,&geometry);
    if(error){errno=error;return B_FALSE;}
    /* The service verified an actual direct-access unit using fresh caller
     * credentials. This answers geometry, not permission to label/use it. */
    return B_TRUE;
}

char *zfs_get_underlying_path(const char *path)
{
    if(!path){errno=EINVAL;return NULL;}
    /* Ordinary files use real DOS canonicalization. Never apply native
     * partition suffix rules to file names or manufacture a device alias. */
    if(strncmp(path,"/AROS/",6)) {
        /* Explicit AROS volume names and POSIX /volume/file spellings resolve
         * through DOS locks. This CRT's realpath walker fails on an existing
         * RAM: file in both spellings. NameFromLock avoids cwd changes and
         * returns the real native canonical name, not an invented alias.
         * Relative POSIX components are translated to DOS path syntax;
         * Lock performs the traversal, including parent components, rather
         * than lexically erasing a possibly nonexistent directory. */
        const char *colon=strchr(path,':');
        {
            size_t length=strlen(path);
            if(!length){errno=ENOENT;return NULL;}
            if(colon&&memchr(path,'/',colon-path)){errno=EINVAL;return NULL;}
            if(length>SIZE_MAX-2){errno=ENAMETOOLONG;return NULL;}
            char *native=malloc(length+2),*resolved=malloc(PATH_MAX);
            if(!native||!resolved){free(native);free(resolved);errno=ENOMEM;return NULL;}
            if(path[0]=='/') {
                const char *slash=strchr(path+1,'/');
                size_t volume=slash?(size_t)(slash-path-1):length-1;
                if(!volume||colon){free(native);free(resolved);errno=EINVAL;return NULL;}
                memcpy(native,path+1,volume);native[volume]=':';
                strcpy(native+volume+1,slash?slash+1:"");
            } else if(colon) memcpy(native,path,length+1);
            else {
                size_t used=0;
                int separator=0;
                for(const char *p=path;*p;) {
                    const char *end=strchr(p,'/');
                    if(!end)end=p+strlen(p);
                    size_t component=end-p;
                    if(component&&!(component==1&&p[0]=='.')) {
                        if(separator)native[used++]='/';
                        if(component==2&&p[0]=='.'&&p[1]=='.') {
                            native[used++]='/';separator=0;
                        } else {
                            memcpy(native+used,p,component);used+=component;
                            separator=1;
                            if(*end=='/') {
                                native[used]=0;
                                int error=directory_component(native);
                                if(error){free(native);free(resolved);errno=error;return NULL;}
                            }
                        }
                    }
                    p=*end?end+1:end;
                }
                native[used]=0;
            }
            BPTR lock=Lock((CONST_STRPTR)native,SHARED_LOCK);
            int error=lock?0:__stdc_ioerr2errno(IoErr());
            if(lock) {
                if(!NameFromLock(lock,(STRPTR)resolved,PATH_MAX)) {
                    error=__stdc_ioerr2errno(IoErr());if(!error)error=EIO;
                }
                UnLock(lock);
            }
            free(native);
            if(error||!lock){free(resolved);errno=error?error:EIO;return NULL;}
            return resolved;
        }
    }
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    struct oz_device_geometry geometry;
    if(!error)error=oz_device_geometry_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,&geometry);
    if(!error&&parsed.partition) {
        struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
        if(!catalog)error=ENOMEM;
        else {
            error=oz_partitions_read(OZ_ADMISSION_PORT,parsed.device,parsed.unit,catalog);
            if(!error) {
                int found=0;
                for(uint32_t i=0;i<catalog->count;i++)
                    if(catalog->entries[i].position==parsed.position)found++;
                if(found!=1)error=found?EIO:ENOENT;
            }
            free(catalog);
        }
    }
    if(error){errno=error;return NULL;}
    return zfs_strip_partition(path);
}
