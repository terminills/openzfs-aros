/* SPDX-License-Identifier: CDDL-1.0 */
#include "efi-native.h"
#include "device-io.h"
#include "../platform/partitions.h"
#include <pthread.h>
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>

struct efi_handle {int id,writable;struct oz_device *disk;unsigned unit;char name[256];};
static struct efi_handle handles[16];
static unsigned next_id=1;
static pthread_mutex_t lock;
static pthread_once_t once=PTHREAD_ONCE_INIT;
static int lock_error;
static struct Task *guard_owner;
static int guard_id;
static void init_lock(void){lock_error=pthread_mutex_init(&lock,NULL);}
static int enter(void)
{
    int error=pthread_once(&once,init_lock);
    if(!error)error=lock_error;
    if(!error)error=pthread_mutex_lock(&lock);
    if(!error&&guard_owner&&guard_owner!=FindTask(NULL)) {
        pthread_mutex_unlock(&lock);error=EBUSY;
    }
    return error;
}
static int failure(int error){errno=error;return -1;}
static struct efi_handle *lookup(int id)
{
    for(unsigned i=0;i<16;i++)if(id>0&&handles[i].id==id)return &handles[i];
    return NULL;
}
int oz_efi_open(const char *name,unsigned unit,int writable)
{
    if(!name||!*name||strlen(name)>255||unit>INT_MAX)return failure(EINVAL);
    int error=enter();if(error)return failure(error);
    struct efi_handle *h=NULL;
    for(unsigned i=0;i<16;i++)if(!handles[i].id){h=&handles[i];break;}
    for(unsigned i=0;i<16;i++)
        if(handles[i].id&&handles[i].unit==unit&&!strcmp(handles[i].name,name)&&
           (writable||handles[i].writable)){error=EBUSY;break;}
    if(!error&&(!h||next_id>INT_MAX))error=EMFILE;
    if(!error)error=oz_device_open(name,unit,writable,&h->disk);
    int id=-1;
    if(!error){h->id=next_id++;h->unit=unit;h->writable=!!writable;strcpy(h->name,name);id=h->id;}
    pthread_mutex_unlock(&lock);
    return error?failure(error):id;
}
int oz_efi_close(int id)
{
    int error=enter();if(error)return error;
    struct efi_handle *h=lookup(id);error=h?0:EBADF;
    if(h&&guard_id==id)error=EBUSY;
    if(h&&!error){oz_device_close(h->disk);memset(h,0,sizeof(*h));}
    pthread_mutex_unlock(&lock);return error;
}
int oz_efi_guard_begin(int id)
{
    int error=enter();if(error)return error;
    struct efi_handle *h=lookup(id);
    if(!h)error=EBADF;
    else if(guard_owner)error=EBUSY;
    else if(!h->writable)error=EROFS;
    /* Never wait for DOS while owning the EFI mutex: another task may own
     * DOS and be trying to inspect an EFI handle. A shared DOS lock is NOT
     * sufficient on AROS, where AddDosEntry itself uses LDF_ALL|LDF_READ. */
    else if(!AttemptLockDosList(LDF_ALL|LDF_WRITE))error=EBUSY;
    else {
        error=oz_device_dos_claim(h->name,h->unit);
        if(error)UnLockDosList(LDF_ALL|LDF_WRITE);
        else {guard_owner=FindTask(NULL);guard_id=id;}
    }
    pthread_mutex_unlock(&lock);return error;
}
int oz_efi_guard_end(int id)
{
    int error=enter();if(error)return error;
    if(!guard_owner||guard_id!=id)error=EINVAL;
    else {
        UnLockDosList(LDF_ALL|LDF_WRITE);
        guard_owner=NULL;guard_id=0;
    }
    pthread_mutex_unlock(&lock);return error;
}
int oz_efi_disk_info(int id,uint64_t *sectors,unsigned *sector)
{
    if(!sectors||!sector)return failure(EINVAL);
    int error=enter();if(error)return failure(error);
    struct efi_handle *h=lookup(id);error=h?0:EBADF;
    if(h){*sector=oz_device_sector(h->disk);*sectors=oz_device_size(h->disk)/ *sector;}
    pthread_mutex_unlock(&lock);return error?failure(error):0;
}
int oz_efi_transfer(int id,uint64_t lba,void *data,size_t count,int write)
{
    int error=enter();if(error)return failure(error);
    struct efi_handle *h=lookup(id);error=h?0:EBADF;
    if(h) {
        unsigned sector=oz_device_sector(h->disk);
        if(lba>UINT64_MAX/sector)error=EOVERFLOW;
        else error=oz_device_transfer(h->disk,data,count,lba*sector,write);
        if(!error&&write)error=oz_device_flush(h->disk);
    }
    pthread_mutex_unlock(&lock);return error?failure(error):0;
}
int oz_efi_rescan(int id)
{
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    if(!catalog)return failure(ENOMEM);
    int error=enter();if(error){free(catalog);return failure(error);}
    struct efi_handle *h=lookup(id);error=h?0:EBADF;
    /* Native partition paths open a fresh partition.library root on each
     * lookup. Verify the actual newly written table, not a fake udev event. */
    if(h)error=oz_partition_catalog_read(h->name,h->unit,catalog);
    pthread_mutex_unlock(&lock);free(catalog);
    return error?failure(error):0;
}
