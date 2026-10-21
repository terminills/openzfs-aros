/* SPDX-License-Identifier: CDDL-1.0 */
/* Native Exec block I/O. Callers must identify/authorize each device explicitly.
 * No discovery, formatting, partition guessing or fallback to another unit.
 * Close requires all callers to have completed their synchronous operations. */
#include "device-io.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <devices/scsidisk.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
#include <aros/debug.h>
struct oz_device {
    struct IOStdReq *opened;
    uint64_t size,base;
    unsigned sector;
    int writable;
    unsigned read64,write64;
    struct oz_device *writer_next;
    int registered;
    unsigned unit;
    int revoked;
    int media_changed;
    int change_known;
    ULONG change_number;
    unsigned references;
    char name[256];
};
/* Service-local writer and admitted-reader reservations, keyed by actual
 * opened device/unit pointers, not path spelling. External raw users and DOS
 * mount ownership remain outside this registry; unbound readonly inspection does
 * not imply a coherent snapshot. Registered readers remain protected until
 * their last retained handle is released, even after the path is unbound. */
static pthread_mutex_t writers_lock;
static pthread_once_t writers_once=PTHREAD_ONCE_INIT;
static int writers_error;
static struct oz_device *writers;
struct oz_device_binding {
    struct oz_device_binding *next;
    struct oz_device *device;
    char path[512];
    char pending_path[512];
};
static struct oz_device_binding *bindings;
/* Lifecycle fields are protected by writers_lock. Stop is also sampled by
 * the polling worker without that lock. The owner alone may stop/join it. */
static pthread_t media_thread;
static struct Task *media_owner;
static unsigned media_state;
static int media_stop;
static void (*media_notify)(void);
/* Serialize this service's write chunks before attempting the global DOS
 * publication lock. Otherwise independent ZIO workers reject each other as
 * EBUSY, even though neither has a conflicting filesystem claim. This lock
 * never guards reference lifetime and is not held while waiting for DOS. */
static pthread_mutex_t write_io_lock;
static pthread_once_t write_io_once=PTHREAD_ONCE_INIT;
static int write_io_error;
static void write_io_init(void){write_io_error=pthread_mutex_init(&write_io_lock,NULL);}
int oz_device_publication_begin(void)
{
    int error=pthread_once(&write_io_once,write_io_init);
    if(!error)error=write_io_error;
    if(!error)error=pthread_mutex_lock(&write_io_lock);
    if(error)return error;
    /* A queued raw chunk cannot depend on a DOS packet: it only checks the
     * list and performs synchronous block-device I/O. Never wait for an
     * external DOS owner, which could be a client of our mounted handler. */
    if(!AttemptLockDosList(LDF_ALL|LDF_ENTRY|LDF_WRITE)) {
        pthread_mutex_unlock(&write_io_lock);return EBUSY;
    }
    return 0;
}
void oz_device_publication_end(void)
{
    UnLockDosList(LDF_ALL|LDF_ENTRY|LDF_WRITE);
    pthread_mutex_unlock(&write_io_lock);
}
int oz_device_startup_publication_begin(void)
{
    int error=pthread_once(&write_io_once,write_io_init);
    if(!error)error=write_io_error;
    if(!error)error=pthread_mutex_lock(&write_io_lock);
    if(error)return error;
    if(!AttemptLockDosList(LDF_ALL|LDF_READ)) {
        pthread_mutex_unlock(&write_io_lock);return EBUSY;
    }
    if(!AttemptLockDosList(LDF_ENTRY|LDF_WRITE)) {
        UnLockDosList(LDF_ALL|LDF_READ);
        pthread_mutex_unlock(&write_io_lock);return EBUSY;
    }
    return 0;
}
void oz_device_startup_publication_end(void)
{
    UnLockDosList(LDF_ENTRY|LDF_WRITE);
    UnLockDosList(LDF_ALL|LDF_READ);
    pthread_mutex_unlock(&write_io_lock);
}
static void writers_init(void){writers_error=pthread_mutex_init(&writers_lock,NULL);}
static int reserve_writer(struct oz_device *d)
{
    if(!d->writable)return 0;
    int error=pthread_once(&writers_once,writers_init);
    if(!error)error=writers_error;
    if(!error)error=pthread_mutex_lock(&writers_lock);
    if(error)return error;
    for(struct oz_device *p=writers;p;p=p->writer_next)
        if(p->opened->io_Device==d->opened->io_Device&&p->opened->io_Unit==d->opened->io_Unit&&
           d->base<p->base+p->size&&p->base<d->base+d->size){error=EBUSY;break;}
    if(!error){d->writer_next=writers;writers=d;d->registered=1;}
    pthread_mutex_unlock(&writers_lock);return error;
}
/* Reject a recognizable DOS filesystem claim on this unit, including a
 * configured device whose handler has not started yet. Reserve the whole
 * unit: repartitioning changes every child's interpretation of its extent.
 * This is a collision check, NOT an exclusive lease. DOS entries can appear
 * after unlocking and opaque/custom startup messages cannot be resolved here.
 * Do not use a successful check as authorization for general disk writes. */
int oz_device_dos_claim(const char *name,unsigned unit)
{
    if(!name||!*name||strlen(name)>255)return EINVAL;
    const ULONG flags=LDF_DEVICES|LDF_READ;
    struct DosList *list=LockDosList(flags);
    if(!list)return EBUSY;
    int error=0;
    struct DosList *entry=list;
    while((entry=NextDosEntry(entry,LDF_DEVICES))) {
        struct FileSysStartupMsg *startup=BADDR(entry->dol_misc.dol_handler.dol_Startup);
        if(!startup||!TypeOfMem(startup)||
           !TypeOfMem((char *)startup+sizeof(*startup)-1))continue;
        struct DosEnvec *env=BADDR(startup->fssm_Environ);
        if(!env||!TypeOfMem(env)||!TypeOfMem((char *)env+sizeof(*env)-1)||
           env->de_TableSize<DE_DOSTYPE||env->de_TableSize>255||
           !startup->fssm_Device)continue;
        const char *device=(const char *)AROS_BSTR_ADDR(startup->fssm_Device);
        if(!TypeOfMem((APTR)device))continue;
        /* Filesystem device BSTRs are NUL terminated. Bound the comparison
         * without retaining pointers outside the DOS-list read lock. */
        size_t n=0;
        while(n<256&&TypeOfMem((APTR)(device+n))&&device[n])n++;
        if(n==256||!TypeOfMem((APTR)(device+n)))continue;
        if(startup->fssm_Unit==unit&&!strcmp(device,name)) {
            error=EBUSY;break;
        }
    }
    UnLockDosList(flags);return error;
}
static int request_result(struct oz_device *d,unsigned command,void *buf,unsigned len,
    uint64_t offset,int check_length,ULONG *actual)
{
    /* A reported media change invalidates this handle's geometry and extent.
     * Never silently resume on newly inserted media under the old admission.
     * This does not detect an unreported change or cancel in-flight requests. */
    if(__atomic_load_n(&d->media_changed,__ATOMIC_ACQUIRE))return ENXIO;
    /* Each worker owns its own reply port/signal and request. Never share an
     * IOStdReq across ZIO threads or wait on another task's signal bit. */
    struct MsgPort *port=CreateMsgPort();if(!port)return ENOMEM;
    struct IOStdReq *io=CreateIORequest(port,sizeof(*io));
    if(!io){DeleteMsgPort(port);return ENOMEM;}
    io->io_Device=d->opened->io_Device;io->io_Unit=d->opened->io_Unit;
    io->io_Command=command;io->io_Data=buf;io->io_Length=len;
    io->io_Offset=(uint32_t)offset;io->io_Actual=(uint32_t)(offset>>32);
    int native=DoIO((struct IORequest *)io);
    int rc=native?(native==IOERR_NOCMD?ENOTSUP:EIO):0;
    if(native==TDERR_DiskChanged){
        __atomic_store_n(&d->media_changed,1,__ATOMIC_RELEASE);rc=ENXIO;
    }
    if(!rc && check_length && io->io_Actual!=len)rc=EIO;
    if(!rc&&actual)*actual=io->io_Actual;
#ifdef OZ_DEVICE_IO_DIAGNOSTIC
    if(rc)bug("OpenZFS native completion command=%u native=%d io_error=%d actual=%lu expected=%u offset=%llu check=%d\n",
        command,native,io->io_Error,(unsigned long)io->io_Actual,len,(unsigned long long)offset,check_length);
#endif
    DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);return rc;
}
static int request(struct oz_device *d,unsigned command,void *buf,unsigned len,
    uint64_t offset,int check_length)
{return request_result(d,command,buf,len,offset,check_length,NULL);}
static int check_media(struct oz_device *d)
{
    if(__atomic_load_n(&d->media_changed,__ATOMIC_ACQUIRE))return ENXIO;
    if(!d->change_known)return 0; /* Driver has no generation contract. */
    ULONG current=0;
    int error=request_result(d,TD_CHANGENUM,NULL,0,0,0,&current);
    if(!error&&current!=d->change_number){
        __atomic_store_n(&d->media_changed,1,__ATOMIC_RELEASE);error=ENXIO;
    }
    return error;
}
/* Saturated trackdisk geometry is not a capacity. Query the actual 64-bit
 * last LBA; never derive a larger size from CHS or silently truncate it. */
static int capacity16(struct oz_device *d,unsigned sector,uint64_t *size)
{
    unsigned char command[16]={0x9e,0x10,0,0,0,0,0,0,0,0,0,0,0,32,0,0};
    unsigned char sense[32]={0};UWORD data[16]={0};
    struct SCSICmd scsi={0};
    scsi.scsi_Data=data;scsi.scsi_Length=sizeof(data);
    scsi.scsi_Command=command;scsi.scsi_CmdLength=sizeof(command);
    scsi.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    scsi.scsi_SenseData=sense;scsi.scsi_SenseLength=sizeof(sense);
    int error=request(d,HD_SCSICMD,&scsi,sizeof(scsi),0,0);
    if(error)return error;
    if(scsi.scsi_Status||scsi.scsi_CmdActual!=sizeof(command)||
       scsi.scsi_Actual!=sizeof(data))return EIO;
    const unsigned char *b=(const unsigned char *)data;
    uint64_t last=0;unsigned logical=0;
    for(unsigned i=0;i<8;i++)last=(last<<8)|b[i];
    for(unsigned i=8;i<12;i++)logical=(logical<<8)|b[i];
    if(logical!=sector)return EIO;
    if(last==UINT64_MAX||last+1>UINT64_MAX/sector)return EOVERFLOW;
    *size=(last+1)*sector;return 0;
}
static int open_extent(const char *name,unsigned unit,int writable,
    uint64_t base,uint64_t length,int range,struct oz_device **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!name||!*name||strlen(name)>255||unit>INT32_MAX)return EINVAL;
    if(writable) {
        int error=oz_device_dos_claim(name,unit);
        if(error)return error;
    }
    struct oz_device *d=calloc(1,sizeof(*d));if(!d)return ENOMEM;
    strcpy(d->name,name);d->unit=unit;d->references=1;
    struct MsgPort *port=CreateMsgPort();
    if(!port){free(d);return ENOMEM;}
    d->opened=CreateIORequest(port,sizeof(*d->opened));
    if(!d->opened){DeleteMsgPort(port);free(d);return ENOMEM;}
    int open_error=OpenDevice((CONST_STRPTR)name,unit,(struct IORequest *)d->opened,0);
    if(open_error) {
        DeleteIORequest((struct IORequest *)d->opened);DeleteMsgPort(port);free(d);
        return open_error==IOERR_OPENFAIL?ENODEV:open_error==IOERR_UNITBUSY?EBUSY:EIO;
    }
    /* Open/CloseDevice use this handle; actual I/O uses worker-owned requests.
     * Dispose of the opening task's signal on that same task, not at close. */
    d->opened->io_Message.mn_ReplyPort=NULL;DeleteMsgPort(port);
    int rc=request_result(d,TD_CHANGENUM,NULL,0,0,0,&d->change_number);
    if(rc&&rc!=ENOTSUP)goto fail;
    d->change_known=!rc;
    struct DriveGeometry geometry;memset(&geometry,0,sizeof(geometry));
    rc=request(d,TD_GETGEOMETRY,&geometry,sizeof(geometry),0,0);
    if(rc)goto fail;
    if(geometry.dg_DeviceType!=DG_DIRECT_ACCESS || geometry.dg_SectorSize<512 ||
        geometry.dg_SectorSize>4096 || (geometry.dg_SectorSize&(geometry.dg_SectorSize-1)) ||
        !geometry.dg_TotalSectors) {
        rc=ENOTSUP;goto fail;
    }
    d->sector=geometry.dg_SectorSize;
    d->size=(uint64_t)geometry.dg_TotalSectors*d->sector;d->writable=writable;
    if(geometry.dg_TotalSectors>=0xfffffff0U) {
        rc=capacity16(d,d->sector,&d->size);if(rc)goto fail;
        if(d->size/d->sector<geometry.dg_TotalSectors){rc=EIO;goto fail;}
    }
    struct NSDeviceQueryResult query;memset(&query,0,sizeof(query));
    if(!request(d,NSCMD_DEVICEQUERY,&query,sizeof(query),0,0) &&
        query.SizeAvailable>=sizeof(query) && query.SupportedCommands) {
        /* Bounded traversal; unsupported 64-bit I/O must never wrap to 32 bits. */
        for(unsigned i=0;i<256 && query.SupportedCommands[i];i++) {
            unsigned cmd=query.SupportedCommands[i];
            if(cmd==NSCMD_TD_READ64)d->read64=cmd;
            if(cmd==NSCMD_TD_WRITE64)d->write64=cmd;
        }
    }
    if(range) {
        if(!length||base>d->size||length>d->size-base||base%d->sector||length%d->sector) {
            rc=EINVAL;goto fail;
        }
        d->base=base;d->size=length;
    }
    rc=check_media(d);if(rc)goto fail;
    rc=reserve_writer(d);if(rc)goto fail;
    *out=d;return 0;
fail:
    oz_device_close(d);return rc;
}
int oz_device_open(const char *name,unsigned unit,int writable,struct oz_device **out)
{return open_extent(name,unit,writable,0,0,0,out);}
int oz_device_open_range(const char *name,unsigned unit,int writable,
    uint64_t base,uint64_t length,struct oz_device **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!length)return EINVAL;
    return open_extent(name,unit,writable,base,length,1,out);
}
int oz_device_retain(struct oz_device *d)
{
    if(!d)return EINVAL;
    /* Caller already owns a reference: this cannot resurrect a freed handle. */
    unsigned old=__atomic_load_n(&d->references,__ATOMIC_RELAXED);
    for(;;) {
        if(!old)return EINVAL;
        if(old==~0U)return EOVERFLOW;
        if(__atomic_compare_exchange_n(&d->references,&old,old+1,0,
            __ATOMIC_RELAXED,__ATOMIC_RELAXED))return 0;
    }
}
static int bind_device(const char *path,struct oz_device *d,int writable,
    struct oz_device_binding **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!path||strncmp(path,"/AROS/",6)||!path[6]||strlen(path)>=512)return EINVAL;
    if(d&&!!d->writable!=!!writable)return EROFS;
    if(writable&&(!d||!d->registered))return EINVAL;
    if(d&&__atomic_load_n(&d->revoked,__ATOMIC_ACQUIRE))return EBUSY;
    if(d){int error=check_media(d);if(error)return error;}
    struct oz_device_binding *b=calloc(1,sizeof(*b));if(!b)return ENOMEM;
    strcpy(b->path,path);b->device=d;
    int error=pthread_once(&writers_once,writers_init);
    if(!error)error=writers_error;
    if(!error)error=pthread_mutex_lock(&writers_lock);
    if(error){free(b);return error;}
    for(struct oz_device_binding *p=bindings;p;p=p->next) {
        if(!strcmp(p->path,path)||!strcmp(p->pending_path,path)){error=EEXIST;break;}
        /* One admitted writable handle cannot be published under a second
         * path and then mistaken for a separately owned pool member. */
        if(writable&&p->device==d){error=EBUSY;break;}
    }
    if(!error&&d)for(struct oz_device *p=writers;p;p=p->writer_next)
        if(p!=d&&(p->writable||writable)&&p->opened->io_Device==d->opened->io_Device&&
           p->opened->io_Unit==d->opened->io_Unit&&
           d->base<p->base+p->size&&p->base<d->base+d->size){error=EBUSY;break;}
    if(!error&&d)error=oz_device_retain(d);
    if(!error&&d&&!d->registered) {
        d->writer_next=writers;writers=d;d->registered=1;
    }
    if(!error){b->next=bindings;bindings=b;*out=b;}
    pthread_mutex_unlock(&writers_lock);
    if(error)free(b);
    return error;
}
int oz_device_bind_readonly(const char *path,struct oz_device *d,
    struct oz_device_binding **out)
{
    if(!d){if(out)*out=NULL;return EINVAL;}
    return bind_device(path,d,0,out);
}
int oz_device_bind_writable(const char *path,struct oz_device *d,
    struct oz_device_binding **out)
{
    if(!d){if(out)*out=NULL;return EINVAL;}
    return bind_device(path,d,1,out);
}
int oz_device_bind_unavailable(const char *path,struct oz_device_binding **out)
{return bind_device(path,NULL,0,out);}
int oz_device_bound_open(const char *path,int writable,struct oz_device **out)
{
    if(!out)return EINVAL;
    *out=NULL;if(!path)return EINVAL;
    int error=pthread_once(&writers_once,writers_init);
    if(!error)error=writers_error;
    if(!error)error=pthread_mutex_lock(&writers_lock);
    if(error)return error;
    error=ENOENT;
    for(struct oz_device_binding *p=bindings;p;p=p->next)if(!strcmp(p->path,path)||
        (p->pending_path[0]&&!strcmp(p->pending_path,path))) {
        if(writable&&(!p->device||!p->device->writable))error=EROFS;
        else if(!p->device)error=ENODEV;
        else if(__atomic_load_n(&p->device->media_changed,__ATOMIC_ACQUIRE))error=ENXIO;
        else if(writable&&__atomic_load_n(&p->device->revoked,__ATOMIC_ACQUIRE))error=EBUSY;
        else error=oz_device_retain(p->device);
        if(!error)*out=p->device;
        break;
    }
    pthread_mutex_unlock(&writers_lock);
    /* Device I/O must run outside the binding registry lock. Retain first
     * so a concurrent unbind cannot free the handle during this check. */
    if(!error&&*out){
        error=check_media(*out);
        if(error){oz_device_close(*out);*out=NULL;}
    }
    return error;
}
void oz_device_unbind(struct oz_device_binding *b)
{
    if(!b)return;
    if(pthread_mutex_lock(&writers_lock))return;
    struct oz_device_binding **p=&bindings;
    while(*p&&*p!=b)p=&(*p)->next;
    if(*p!=b){pthread_mutex_unlock(&writers_lock);return;}
    *p=b->next;
    pthread_mutex_unlock(&writers_lock);
    oz_device_close(b->device);free(b);
}
int oz_device_binding_snapshot(struct oz_device_binding *b,struct oz_device **out)
{
    if(!b||!out)return EINVAL;
    *out=NULL;
    int error=pthread_mutex_lock(&writers_lock);if(error)return error;
    if(!b->device)error=ENODEV;
    else if(!(error=oz_device_retain(b->device)))*out=b->device;
    pthread_mutex_unlock(&writers_lock);return error;
}
int oz_device_binding_repath_begin(struct oz_device_binding *b,struct oz_device *expected,
    struct oz_device *candidate,const char *path)
{
    if(!b||!expected||!candidate||!path||strncmp(path,"/AROS/",6)||
        !path[6]||strlen(path)>=sizeof(b->path))return EINVAL;
    /* Compare opened kernel identity and exact geometry, not device spelling.
     * The candidate grants no write rights and is never substituted for the
     * admitted handle. Caller has separately resolved this canonical path. */
    if(expected->opened->io_Device!=candidate->opened->io_Device||
        expected->opened->io_Unit!=candidate->opened->io_Unit||
        expected->base!=candidate->base||expected->size!=candidate->size||
        expected->sector!=candidate->sector)return ESTALE;
    int error=check_media(expected);if(error)return error;
    error=check_media(candidate);if(error)return error;
    error=pthread_mutex_lock(&writers_lock);if(error)return error;
    if(b->device!=expected)error=ESTALE;
    else if(b->pending_path[0]||__atomic_load_n(&expected->revoked,__ATOMIC_ACQUIRE))error=EBUSY;
    else if(!strcmp(b->path,path))error=EEXIST;
    for(struct oz_device_binding *p=bindings;!error&&p;p=p->next)
        if(p!=b&&(!strcmp(p->path,path)||!strcmp(p->pending_path,path)))error=EEXIST;
    if(!error)strcpy(b->pending_path,path);
    pthread_mutex_unlock(&writers_lock);return error;
}
int oz_device_binding_repath_end(struct oz_device_binding *b,int error)
{
    if(!b)return EINVAL;
    int lock_error=pthread_mutex_lock(&writers_lock);if(lock_error)return lock_error;
    if(!b->pending_path[0]){pthread_mutex_unlock(&writers_lock);return EINVAL;}
    if(!error)strcpy(b->path,b->pending_path);
    b->pending_path[0]=0;
    pthread_mutex_unlock(&writers_lock);return 0;
}
int oz_device_binding_replace(struct oz_device_binding *b,struct oz_device *old,
    struct oz_device *fresh)
{
    if(!b||!old||!fresh||old==fresh||fresh->registered||fresh->writable)return EINVAL;
    if(!oz_device_media_invalid(old)||old->revoked)return EBUSY;
    if(strcmp(old->name,fresh->name)||old->unit!=fresh->unit||
       old->base!=fresh->base||old->size!=fresh->size||old->sector!=fresh->sector)return ESTALE;
    int error=check_media(fresh);if(error)return error;
    if(old->writable&&(error=oz_device_dos_claim(fresh->name,fresh->unit)))return error;
    error=pthread_mutex_lock(&writers_lock);if(error)return error;
    if(b->pending_path[0])error=EBUSY;
    else if(b->device!=old||!old->registered)error=ESTALE;
    for(struct oz_device *p=writers;!error&&p;p=p->writer_next)
        if(p!=old&&(p->writable||old->writable)&&
           p->opened->io_Device==fresh->opened->io_Device&&p->opened->io_Unit==fresh->opened->io_Unit&&
           fresh->base<p->base+p->size&&p->base<fresh->base+fresh->size)error=EBUSY;
    if(!error)error=oz_device_retain(fresh);
    if(!error){
        struct oz_device **p=&writers;while(*p&&*p!=old)p=&(*p)->writer_next;
        if(*p)*p=old->writer_next;
        old->registered=0;old->writer_next=NULL;
        fresh->writable=old->writable;fresh->registered=1;
        fresh->writer_next=writers;writers=fresh;b->device=fresh;
    }
    pthread_mutex_unlock(&writers_lock);
    if(!error)oz_device_close(old); /* Release only the binding's reference. */
    return error;
}
void oz_device_close(struct oz_device *d)
{
    if(!d)return;
    if(__atomic_sub_fetch(&d->references,1,__ATOMIC_ACQ_REL))return;
    if(d->registered) {
        /* Close requires callers to have finished all synchronous I/O. */
        if(pthread_mutex_lock(&writers_lock))return;
        struct oz_device **link=&writers;
        while(*link&&*link!=d)link=&(*link)->writer_next;
        if(*link==d)*link=d->writer_next;
        d->registered=0;
        pthread_mutex_unlock(&writers_lock);
    }
    CloseDevice((struct IORequest *)d->opened);
    DeleteIORequest((struct IORequest *)d->opened);free(d);
}
uint64_t oz_device_size(const struct oz_device *d){return d->size;}
unsigned oz_device_sector(const struct oz_device *d){return d->sector;}
int oz_device_media_invalid(const struct oz_device *d)
{return d?__atomic_load_n(&d->media_changed,__ATOMIC_ACQUIRE):0;}
static int poll_media(int *invalid)
{
    *invalid=0;
    int error=pthread_mutex_lock(&writers_lock);if(error)return error;
    size_t count=0,used=0;
    for(struct oz_device *d=writers;d;d=d->writer_next)count++;
    struct oz_device **snapshot=count?calloc(count,sizeof(*snapshot)):NULL;
    if(count&&!snapshot){pthread_mutex_unlock(&writers_lock);return ENOMEM;}
    for(struct oz_device *d=writers;d;d=d->writer_next)
        if((d->change_known||oz_device_media_invalid(d))&&!oz_device_retain(d))snapshot[used++]=d;
    pthread_mutex_unlock(&writers_lock);
    /* Unbind/close may proceed while a device query waits. Each snapshot
     * reference protects the handle through its final synchronous request. */
    for(size_t i=0;i<used;i++){
        int rc=check_media(snapshot[i]);
        if(rc==ENXIO)*invalid=1;
        if(rc&&rc!=ENXIO&&!error)error=rc;
        oz_device_close(snapshot[i]);
    }
    free(snapshot);return error;
}
static void *media_worker(void *unused)
{
    (void)unused;unsigned reports=0;
    while(!__atomic_load_n(&media_stop,__ATOMIC_ACQUIRE)){
        int invalid=0,error=poll_media(&invalid);
        if(invalid&&media_notify)media_notify();
        if(error&&reports++<8)bug("OpenZFS media monitor query error=%d\n",error);
        for(unsigned i=0;i<10&&!__atomic_load_n(&media_stop,__ATOMIC_ACQUIRE);i++)Delay(5);
    }
    return NULL;
}
int oz_device_media_start(void (*notify)(void))
{
    int error=pthread_once(&writers_once,writers_init);
    if(!error)error=writers_error;
    if(!error)error=pthread_mutex_lock(&writers_lock);
    if(error)return error;
    if(media_state)error=EBUSY;
    else{
        __atomic_store_n(&media_stop,0,__ATOMIC_RELEASE);
        media_notify=notify;
        error=pthread_create(&media_thread,NULL,media_worker,NULL);
        if(!error){media_owner=FindTask(NULL);media_state=1;}
    }
    pthread_mutex_unlock(&writers_lock);return error;
}
int oz_device_media_stop(void)
{
    int error=pthread_once(&writers_once,writers_init);
    if(!error)error=writers_error;
    if(!error)error=pthread_mutex_lock(&writers_lock);
    if(error)return error;
    if(!media_state){pthread_mutex_unlock(&writers_lock);return 0;}
    if(media_owner!=FindTask(NULL)||media_state!=1){pthread_mutex_unlock(&writers_lock);return EPERM;}
    media_state=2;__atomic_store_n(&media_stop,1,__ATOMIC_RELEASE);
    pthread_mutex_unlock(&writers_lock);
    error=pthread_join(media_thread,NULL);
    if(error)return error; /* Never claim quiescence or free live state. */
    error=pthread_mutex_lock(&writers_lock);if(error)return error;
    media_state=0;media_owner=NULL;media_notify=NULL;
    pthread_mutex_unlock(&writers_lock);return 0;
}
static int transfer_error(struct oz_device *d,const char *stage,int error,
    uint64_t offset,size_t count,int writing)
{
    static unsigned reports;
    if(__atomic_fetch_add(&reports,1,__ATOMIC_RELAXED)<32)
        bug("OpenZFS native I/O failure stage=%s error=%d device=%s unit=%u write=%d offset=%llu count=%llu base=%llu size=%llu sector=%u\n",
            stage,error,d->name,d->unit,writing,(unsigned long long)offset,
            (unsigned long long)count,(unsigned long long)d->base,
            (unsigned long long)d->size,d->sector);
    return error;
}
int oz_device_transfer(struct oz_device *d,void *buf,size_t count,uint64_t off,int write)
{
    if(!d || (!buf&&count))return EINVAL;
    if(write&&!d->writable)return transfer_error(d,"readonly",EROFS,off,count,write);
    if(off>d->size || count>d->size-off)return transfer_error(d,"bounds",EINVAL,off,count,write);
    if(off%d->sector || count%d->sector)return transfer_error(d,"alignment",EINVAL,off,count,write);
    if(write) {
        int error=pthread_once(&write_io_once,write_io_init);
        if(!error)error=write_io_error;
        if(error)return error;
    }
    /* Bounds above are relative to this handle. The base was checked against
     * physical capacity at open, so translation cannot overflow or escape. */
    off+=d->base;
    unsigned char *p=buf;
    while(count) {
        unsigned n=count>1024*1024?1024*1024:(unsigned)count;
        unsigned cmd=write?CMD_WRITE:CMD_READ;
        if(off+n>UINT64_C(0x100000000)) {
            cmd=write?d->write64:d->read64;if(!cmd)return EOVERFLOW;
        }
        if(write) {
            int error=pthread_mutex_lock(&write_io_lock);if(error)return error;
            if(__atomic_load_n(&d->revoked,__ATOMIC_ACQUIRE)) {
                pthread_mutex_unlock(&write_io_lock);return transfer_error(d,"revoked",EBUSY,off,n,write);
            }
            /* We inspect namespace ownership, never mutate it. Shared list
             * AND entry locks exclude RemDosEntry's exclusive list lock and
             * AddDosEntry's exclusive entry lock, while independent admission
             * readers (including other processes) can perform data I/O.
             * Exclusive entry ownership here spuriously failed those writers
             * and suspended otherwise healthy pools. Acquire both together;
             * never wait behind a DOS caller that may depend on this I/O. */
            if(!AttemptLockDosList(LDF_ALL|LDF_ENTRY|LDF_READ)) {
                pthread_mutex_unlock(&write_io_lock);return transfer_error(d,"dos-publication-contention",EBUSY,off,n,write);
            }
            int claim=oz_device_dos_claim(d->name,d->unit);
            if(claim) {
                /* Do not silently resume writes if the claim disappears.
                 * The caller must close, resolve ownership, then reopen. */
                if(claim==EBUSY)__atomic_store_n(&d->revoked,1,__ATOMIC_RELEASE);
                UnLockDosList(LDF_ALL|LDF_ENTRY|LDF_READ);
                pthread_mutex_unlock(&write_io_lock);
                return transfer_error(d,"dos-claim",claim,off,n,write);
            }
        }
        int rc=request(d,cmd,p,n,off,1);
        if(write){UnLockDosList(LDF_ALL|LDF_ENTRY|LDF_READ);pthread_mutex_unlock(&write_io_lock);}
        if(rc)return transfer_error(d,"device-request",rc,off,n,write);
        p+=n;off+=n;count-=n;
    }
    return 0;
}
int oz_device_flush(struct oz_device *d)
{
    if(!d)return EINVAL;
    if(!d->writable)return 0; /* This handle cannot have submitted writes. */
    return request(d,CMD_UPDATE,NULL,0,0,0);
}

int oz_device_inquiry(struct oz_device *d,unsigned char out[96],unsigned *length)
{
    if(!d||!out||!length)return EINVAL;
    unsigned char command[6]={0x12,0,0,0,96,0},sense[32]={0};
    UWORD data[48]={0};
    struct SCSICmd scsi={0};
    scsi.scsi_Data=data;scsi.scsi_Length=sizeof(data);
    scsi.scsi_Command=command;scsi.scsi_CmdLength=sizeof(command);
    scsi.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    scsi.scsi_SenseData=sense;scsi.scsi_SenseLength=sizeof(sense);
    int error=request(d,HD_SCSICMD,&scsi,sizeof(scsi),0,0);
    if(!error&&(scsi.scsi_Status||scsi.scsi_CmdActual!=sizeof(command)||
       scsi.scsi_Actual<36||scsi.scsi_Actual>sizeof(data)))error=EIO;
    const unsigned char *bytes=(const unsigned char *)data;
    if(!error&&((bytes[0]&0xe0)||(bytes[0]&0x1f)!=0||bytes[4]<31))error=EIO;
    if(!error){memcpy(out,data,sizeof(data));*length=scsi.scsi_Actual;}
    return error;
}
