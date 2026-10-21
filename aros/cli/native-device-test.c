/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzutil.h>
#include <errno.h>
#include <aros/debug.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <proto/dos.h>
#include <unistd.h>
#include "../../cmd/zpool/zpool_util.h"
#include "../ui/partition-api.h"
static int relative_file_test(void)
{
    char *saved=getcwd(NULL,0);
    if(!saved)return EIO;
    int error=0;
    if(chdir("RAM:OZPublicPool")){free(saved);return EIO;}
    char *before=getcwd(NULL,0);
    BPTR original=Lock("RAM:OZPublicPool/disk0",SHARED_LOCK);
    const char *paths[]={"disk0","./disk0","../OZPublicPool/disk0",
        "././disk0",":OZPublicPool/disk0"};
    if(!before||!original)error=EIO;
    for(unsigned i=0;!error&&i<sizeof(paths)/sizeof(paths[0]);i++) {
        char *resolved=zfs_get_underlying_path(paths[i]);
        BPTR actual=resolved?Lock((CONST_STRPTR)resolved,SHARED_LOCK):BNULL;
        char *after=getcwd(NULL,0);
        if(!actual||SameLock(original,actual)!=LOCK_SAME||!after||strcmp(before,after)) {
            bug("Native relative resolution failed input=%s output=%s errno=%d\n",paths[i],resolved?resolved:"NULL",errno);
            error=EIO;
        }
        if(actual)UnLock(actual);
        free(after);free(resolved);
    }
    const char *absent[]={"OpenZFS.Missing.Relative","OpenZFS.Missing.Relative/../disk0",""};
    for(unsigned i=0;!error&&i<sizeof(absent)/sizeof(absent[0]);i++) {
        errno=0;
        char *missing=zfs_get_underlying_path(absent[i]);
        if(missing||errno!=ENOENT) {
            bug("Native relative missing returned=%s errno=%d\n",missing?missing:"NULL",errno);
            error=EIO;
        }
        free(missing);
    }
    const char *nondirs[]={"disk0/","disk0/.","disk0/../disk0"};
    for(unsigned i=0;!error&&i<sizeof(nondirs)/sizeof(nondirs[0]);i++) {
        errno=0;
        char *resolved=zfs_get_underlying_path(nondirs[i]);
        if(resolved||errno!=ENOTDIR) {
            bug("Native relative nondirectory input=%s returned=%s errno=%d\n",nondirs[i],resolved?resolved:"NULL",errno);
            error=EIO;
        }
        free(resolved);
    }
    char *after=getcwd(NULL,0);
    if(!after||!before||strcmp(before,after))error=EIO;
    free(after);
    if(original)UnLock(original);
    free(before);
    if(chdir(saved))error=EIO;
    free(saved);
    if(!error)bug("Public native relative file identity PASS paths=5 missing=3 nondirs=3 cwd=1\n");
    return error;
}
int oz_upstream_device_test(int denied)
{
    int sector=12345;
    errno=0;
    boolean_t match=check_sector_size_database("/AROS/ata.device/0",&sector);
    if(denied) {
        if(match||errno!=EACCES||sector!=12345)return EIO;
    } else {
        BPTR flag=Lock("SYS:openzfs-sector-quirk-test",SHARED_LOCK);
        int expected=flag!=BNULL;if(flag)UnLock(flag);
        if(match!=expected||sector!=(expected?8192:12345)) {
            bug("Sector quirk failed match=%d sector=%d expected=%d errno=%d\n",match,sector,expected,errno);
            struct oz_device_inquiry inquiry;
            if(!oz_device_inquiry_read(OZ_ADMISSION_PORT,"ata.device",0,&inquiry)) {
                bug("Native INQUIRY model bytes:");
                for(unsigned i=8;i<32;i++)bug(" %02x",inquiry.data[i]);
                bug("\n");
            }
            return EIO;
        }
        sector=12345;
        if(check_sector_size_database("/AROS/ata.device/0/partition/0",&sector)!=expected||
           sector!=(expected?8192:12345))return EIO;
        sector=12345;errno=0;
        if(check_sector_size_database("/AROS/openzfs-absent.device/0",&sector)||errno!=ENODEV||sector!=12345)return EIO;
        if(check_sector_size_database(NULL,&sector)||sector!=12345)return EIO;
        bug("Public native sector quirk PASS match=%d partition=1 absent=1 unchanged=1\n",expected);
    }
    for(unsigned i=0;i<16;i++) {
        errno=0;
        boolean_t first=zfs_dev_is_whole_disk("/AROS/ata.device/0");
        if(denied) {
            if(first||errno!=EACCES)return EIO;
        } else {
            if(!first||!zfs_dev_is_whole_disk("/AROS/ata.device/1"))return EIO;
        }
    }
    if(!denied) {
        if(zfs_dev_is_whole_disk("/AROS/ata.device/0/partition/0")||
           zfs_dev_is_whole_disk("RAM:disk0")||zfs_dev_is_whole_disk("/dev/sda")||
           zfs_dev_is_whole_disk("/AROS/ata.device/-1")||zfs_dev_is_whole_disk(NULL))return EIO;
        errno=0;
        if(zfs_dev_is_whole_disk("/AROS/openzfs-absent.device/0")||errno!=ENODEV)return EIO;
    }
    errno=0;
    char *underlying=zfs_get_underlying_path("/AROS/ata.device/0");
    if(denied) {
        if(underlying||errno!=EACCES){free(underlying);return EIO;}
    } else {
        if(!underlying)return EIO;
        int valid=!strcmp(underlying,"/AROS/ata.device/0");free(underlying);
        if(!valid)return EIO;
        const char *partitions[]={"/AROS/ata.device/0/partition/0","/AROS/ata.device/0/partition/2"};
        for(unsigned i=0;i<2;i++) {
            underlying=zfs_get_underlying_path(partitions[i]);
            if(!underlying)return EIO;
            valid=!strcmp(underlying,"/AROS/ata.device/0");free(underlying);
            if(!valid)return EIO;
        }
        errno=0;underlying=zfs_get_underlying_path("/AROS/ata.device/0/partition/1");
        if(underlying||errno!=ENOENT){free(underlying);return EIO;}
        errno=0;underlying=zfs_get_underlying_path("/AROS/ata.device/1/partition/0");
        if(underlying||errno!=EIO){free(underlying);return EIO;}
        const char *file="RAM:OZPublicPool/disk0";
        struct stat64 original,resolved;
        if(stat64(file,&original)){bug("Native underlying input stat failed errno=%d\n",errno);return EIO;}
        underlying=zfs_get_underlying_path(file);
        if(!underlying){bug("Native underlying resolution failed path=%s errno=%d\n",file,errno);return EIO;}
        bug("Native underlying resolved file=%s\n",underlying);
        valid=!stat64(underlying,&resolved)&&S_ISREG(resolved.st_mode)&&
            resolved.st_size==original.st_size;
        BPTR source_lock=Lock((CONST_STRPTR)file,SHARED_LOCK);
        BPTR resolved_lock=Lock((CONST_STRPTR)underlying,SHARED_LOCK);
        valid=valid&&source_lock&&resolved_lock&&SameLock(source_lock,resolved_lock)==LOCK_SAME;
        if(resolved_lock)UnLock(resolved_lock);
        if(source_lock)UnLock(source_lock);
        char *posix=zfs_get_underlying_path("/RAM/OZPublicPool/disk0");
        valid=valid&&posix&&!strcmp(posix,underlying);free(posix);
        free(underlying);if(!valid){bug("Native underlying resolved stat mismatch errno=%d\n",errno);return EIO;}
        errno=0;underlying=zfs_get_underlying_path("RAM:OpenZFS.Missing.Underlying");
        if(underlying||errno!=ENOENT){bug("Native underlying missing returned=%p errno=%d\n",underlying,errno);free(underlying);return EIO;}
        if(relative_file_test())return EIO;
    }
    bug("Public upstream native device predicate PASS denied=%d cycles=16 underlying=1\n",denied);
    return 0;
}
