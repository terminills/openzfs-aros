/* SPDX-License-Identifier: CDDL-1.0 */
/* Explicit resident mapping. Code/base stay resident until reboot. */
#include <exec/devices.h>
#include <exec/execbase.h>
#include <exec/errors.h>
#include <exec/io.h>
#include <exec/interrupts.h>
#include <exec/memory.h>
#include <exec/semaphores.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <devices/scsidisk.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/execlock.h>
#include <resources/execlock.h>
#include <aros/types/spinlock_s.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <aros/libcall.h>
#include <aros/debug.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stddef.h>
#include "../ui/zvol-api.h"
#include "mapping.h"
#include "mountfile.h"
#define ZDEV_UNITS 32
struct zdev {
    struct Device device;
    struct Unit units[ZDEV_UNITS];
    unsigned unit_count;
    ULONG opens[ZDEV_UNITS],generation[ZDEV_UNITS];
    int remapping[ZDEV_UNITS];
    char names[ZDEV_UNITS][256];
    struct SignalSemaphore lock;
    struct List queue;
    struct List changes;
    struct Task *worker;
    struct Library *security;
    ULONG signal,pending;
    struct IOStdReq *active;
    int cancelled;
};
static int unit_index(struct zdev *b,const struct Unit *unit)
{
    for(unsigned i=0;i<b->unit_count;i++)if(unit==&b->units[i])return (int)i;
    return -1;
}
static int authorized(struct zdev *b)
{
    struct Library *secBase=b->security;
    return secIsConfigured()&&secGetTaskOwner(FindTask(NULL))==secOWNER_SYSTEM;
}
static int publish(struct zdev *b)
{
    struct Library *ExecLockBase=OpenResource((CONST_STRPTR)"execlock.resource");
    /* Prepare exactly the metadata AddDevice prepares, outside the lock.
     * AddDevice cannot be nested here: its list spinlock is not recursive. */
    b->device.dd_Library.lib_Node.ln_Type=NT_DEVICE;
    b->device.dd_Library.lib_Flags|=LIBF_CHANGED;
    SumLibrary(&b->device.dd_Library);
    if(ExecLockBase){
        if(!ObtainSystemLock(&SysBase->DeviceList,SPINLOCK_MODE_WRITE,LOCKF_FORBID))return -1;
    }else Forbid(); /* Classic uniprocessor Exec has no execlock.resource. */
    int available=FindName(&SysBase->DeviceList,(CONST_STRPTR)"openzfs.device")==NULL;
    if(available)Enqueue(&SysBase->DeviceList,&b->device.dd_Library.lib_Node);
    if(ExecLockBase)ReleaseSystemLock(&SysBase->DeviceList,LOCKF_FORBID);
    else Permit();
    return available;
}
#ifdef OZ_DEVICE_STARTUP_TEST
static int startup_barrier(void)
{
    /* Private fixture owns this port until all four messages are replied. */
    struct MsgPort *gate=FindPort((CONST_STRPTR)"OpenZFSDevice.StartupTest");
    struct MsgPort *reply=CreateMsgPort();
    if(!gate||!reply){if(reply)DeleteMsgPort(reply);return 0;}
    struct Message ready={0};
    ready.mn_ReplyPort=reply;ready.mn_Length=sizeof(ready);
    PutMsg(gate,&ready);WaitPort(reply);
    int ok=GetMsg(reply)==&ready;DeleteMsgPort(reply);return ok;
}
#endif
AROS_LH3(void,zopen,AROS_LHA(struct IORequest *,io,A1),
    AROS_LHA(IPTR,unit,D0),AROS_LHA(ULONG,flags,D1),struct zdev *,b,1,zvoldev)
{
    AROS_LIBFUNC_INIT
    io->io_Error=IOERR_OPENFAIL;
    if(unit>=b->unit_count||flags||io->io_Message.mn_Length<sizeof(struct IOStdReq)||!authorized(b))return;
    ObtainSemaphore(&b->lock);
    if(!b->remapping[unit]&&b->device.dd_Library.lib_OpenCnt!=65535){
        ++b->opens[unit];
        ++b->device.dd_Library.lib_OpenCnt;io->io_Unit=&b->units[unit];io->io_Error=0;
    }
    ReleaseSemaphore(&b->lock);
    AROS_LIBFUNC_EXIT
}
AROS_LH1(BPTR,zclose,AROS_LHA(struct IORequest *,io,A1),struct zdev *,b,2,zvoldev)
{
    AROS_LIBFUNC_INIT
#ifdef OZ_DEVICE_TRACE
    bug("ZDEV close request=%p base=%p\n",io,b);
#endif
    ObtainSemaphore(&b->lock);
    int unit=unit_index(b,io->io_Unit);
    if(unit>=0&&b->opens[unit]&&b->device.dd_Library.lib_OpenCnt){
        --b->opens[unit];--b->device.dd_Library.lib_OpenCnt;
    }
    io->io_Unit=NULL;ReleaseSemaphore(&b->lock);
#ifdef OZ_DEVICE_TRACE
    bug("ZDEV closed request=%p\n",io);
#endif
    return BNULL;
    AROS_LIBFUNC_EXIT
}
AROS_LH0(BPTR,zexpunge,struct zdev *,b,3,zvoldev)
{
    AROS_LIBFUNC_INIT
    (void)b;return BNULL; /* Never unload code while a device vector may run. */
    AROS_LIBFUNC_EXIT
}
AROS_LH0(IPTR,zreserved,struct zdev *,b,4,zvoldev)
{
    AROS_LIBFUNC_INIT
    (void)b;return 0;
    AROS_LIBFUNC_EXIT
}
AROS_LH1(void,zbegin,AROS_LHA(struct IOStdReq *,io,A1),struct zdev *,b,5,zvoldev)
{
    AROS_LIBFUNC_INIT
    if(io->io_Message.mn_Length<sizeof(*io)||unit_index(b,io->io_Unit)<0||!authorized(b)){
        io->io_Error=IOERR_OPENFAIL;
        if(!(io->io_Flags&IOF_QUICK))ReplyMsg(&io->io_Message);
        return;
    }
    ObtainSemaphore(&b->lock);
    if(b->remapping[unit_index(b,io->io_Unit)]){
        ReleaseSemaphore(&b->lock);io->io_Error=IOERR_UNITBUSY;io->io_Actual=0;
        if(!(io->io_Flags&IOF_QUICK))ReplyMsg(&io->io_Message);
        return;
    }
    if(io->io_Command==TD_ADDCHANGEINT||io->io_Command==TD_REMCHANGEINT){
        struct Node *n;int found=0;
        for(n=b->changes.lh_Head;n->ln_Succ;n=n->ln_Succ)
            if(n==&io->io_Message.mn_Node){found=1;break;}
        /* A registration is retained, not replied onto the caller's port.
         * The same request is reused by trackdisk clients for removal. */
        if(io->io_Command==TD_ADDCHANGEINT&&found){ReleaseSemaphore(&b->lock);return;}
        io->io_Actual=0;io->io_Error=0;
        io->io_Flags&=~IOF_QUICK;
        if(io->io_Command==TD_ADDCHANGEINT){
            struct Interrupt *interrupt=io->io_Data;
            if(!interrupt||io->io_Length<sizeof(*interrupt)||!interrupt->is_Code||
               interrupt->is_Node.ln_Pri < -32||interrupt->is_Node.ln_Pri > 32||
               interrupt->is_Node.ln_Pri%16)
                io->io_Error=IOERR_BADLENGTH;
            else{
                io->io_Message.mn_Node.ln_Type=NT_MESSAGE;
                AddTail(&b->changes,&io->io_Message.mn_Node);
                ReleaseSemaphore(&b->lock);return;
            }
        }else if(found)Remove(&io->io_Message.mn_Node);
        else io->io_Error=IOERR_NOCMD;
        ReleaseSemaphore(&b->lock);ReplyMsg(&io->io_Message);return;
    }
#ifdef OZ_DEVICE_TRACE
    bug("ZDEV begin command=%u request=%p worker=%p pending=%lu\n",io->io_Command,io,b->worker,(unsigned long)b->pending);
#endif
    io->io_Flags&=~IOF_QUICK;io->io_Message.mn_Node.ln_Type=NT_MESSAGE;
    AddTail(&b->queue,&io->io_Message.mn_Node);++b->pending;
    Signal(b->worker,b->signal);ReleaseSemaphore(&b->lock);
    AROS_LIBFUNC_EXIT
}
AROS_LH1(LONG,zabort,AROS_LHA(struct IOStdReq *,io,A1),struct zdev *,b,6,zvoldev)
{
    AROS_LIBFUNC_INIT
    if(!authorized(b))return IOERR_OPENFAIL;
    int removed=0,active=0;struct Node *node;
    ObtainSemaphore(&b->lock);
    for(node=b->changes.lh_Head;node->ln_Succ;node=node->ln_Succ)
        if(node==&io->io_Message.mn_Node){Remove(node);removed=1;break;}
    for(node=b->queue.lh_Head;node->ln_Succ;node=node->ln_Succ)
        if(node==&io->io_Message.mn_Node){Remove(node);--b->pending;removed=1;break;}
    if(b->active==io){b->cancelled=1;active=1;}
    ReleaseSemaphore(&b->lock);
    if(removed){io->io_Actual=0;io->io_Error=IOERR_ABORTED;ReplyMsg(&io->io_Message);}
    return removed||active?0:IOERR_NOCMD;
    AROS_LIBFUNC_EXIT
}
static const APTR vectors[]={
    (APTR)&AROS_SLIB_ENTRY(zopen,zvoldev,1),(APTR)&AROS_SLIB_ENTRY(zclose,zvoldev,2),
    (APTR)&AROS_SLIB_ENTRY(zexpunge,zvoldev,3),(APTR)&AROS_SLIB_ENTRY(zreserved,zvoldev,4),
    (APTR)&AROS_SLIB_ENTRY(zbegin,zvoldev,5),(APTR)&AROS_SLIB_ENTRY(zabort,zvoldev,6),(APTR)-1};
static UWORD commands[]={CMD_READ,CMD_WRITE,CMD_UPDATE,CMD_CLEAR,TD_FORMAT,
    TD_READ64,TD_WRITE64,TD_FORMAT64,NSCMD_TD_READ64,NSCMD_TD_WRITE64,
    NSCMD_TD_FORMAT64,TD_GETGEOMETRY,TD_PROTSTATUS,TD_CHANGESTATE,TD_CHANGENUM,
    TD_GETDRIVETYPE,TD_MOTOR,TD_EJECT,TD_ADDCHANGEINT,TD_REMCHANGEINT,NSCMD_DEVICEQUERY,HD_SCSICMD,OZCMD_GETMAPPING,OZCMD_SETMAPPING,OZCMD_EJECTMAPPING,0};
static int cancelled(struct zdev *b)
{ObtainSemaphore(&b->lock);int c=b->cancelled;ReleaseSemaphore(&b->lock);return c;}
static int call(struct oz_zvol_request *q)
{int e=oz_zvol_request(OZ_ADMISSION_PORT,q,sizeof(*q)+(q->operation==OZ_ZVOL_DISCARD?0:q->count));return e?e:q->engine_error;}
static void init(struct oz_zvol_request *q,struct oz_ticket session,LONG fd,ULONG op)
{memset(q,0,sizeof(*q));q->session=session;q->version=OZ_ZVOL_VERSION;q->descriptor=fd;q->operation=op;}
static BYTE ioerror(int e)
{
    if(!e)return 0;
    if(e==EROFS||e==EACCES||e==EPERM)return TDERR_WriteProt;
    if(e==ENOENT||e==ENODEV||e==ENXIO||e==EBADF)return TDERR_DiskChanged;
    if(e==EINVAL||e==EOVERFLOW)return IOERR_BADLENGTH;
    return TDERR_NotSpecified;
}
#define ZDEV_UNMAP_DESCRIPTORS 255
#define ZDEV_UNMAP_SECTORS 0x7fffffUL
static UQUAD be_number(const UBYTE *p,unsigned n)
{UQUAD v=0;while(n--)v=(v<<8)|*p++;return v;}
static void put_be(UBYTE *p,UQUAD v,unsigned n)
{while(n){p[--n]=(UBYTE)v;v>>=8;}}
static void scsi_error(struct IOStdReq *io,struct SCSICmd *s,UBYTE key,UBYTE asc)
{
    s->scsi_Status=2;io->io_Error=HFERR_BadStatus;
    if((s->scsi_Flags&SCSIF_AUTOSENSE)&&s->scsi_SenseData){
        UBYTE sense[18]={0x70,0,0,0,0,0,0,10,0,0,0,0,0,0,0,0,0,0};
        sense[2]=key;sense[12]=asc;ULONG n=s->scsi_SenseLength;if(n>sizeof(sense))n=sizeof(sense);
        CopyMem(sense,s->scsi_SenseData,n);s->scsi_SenseActual=n;
    }
}
/* Bounded SCSI UNMAP only, never arbitrary command passthrough. Validate all
 * descriptors before the first mutation; execution errors can be partial. */
static void scsi_unmap(struct zdev *b,struct IOStdReq *io,struct SCSICmd *s,
    UQUAD size,int readonly,struct oz_ticket session,LONG fd,struct oz_zvol_request *q)
{
    UBYTE *c=s->scsi_Command;
    if(s->scsi_CmdLength!=10){scsi_error(io,s,5,0x24);return;}
    if(c[1]||c[2]||c[3]||c[4]||c[5]||c[6]||c[9]||(s->scsi_Flags&SCSIF_READ)){
        scsi_error(io,s,5,0x24);return;
    }
    ULONG n=be_number(c+7,2);
    if(!n){io->io_Actual=sizeof(*s);return;}
    if(n<8||n>8+16*ZDEV_UNMAP_DESCRIPTORS||n>s->scsi_Length||!s->scsi_Data){
        scsi_error(io,s,5,0x1a);return;
    }
    UBYTE *data=AllocVec(n,MEMF_PUBLIC);if(!data){scsi_error(io,s,4,0x44);return;}
    CopyMem(s->scsi_Data,data,n);
    ULONG descriptors=be_number(data+2,2),total=0;
    if(be_number(data,2)!=n-2||descriptors!=n-8||(descriptors&15)||data[4]||data[5]||data[6]||data[7]){
        scsi_error(io,s,5,0x26);goto done;
    }
    for(ULONG pos=8;pos<n;pos+=16){
        UQUAD lba=be_number(data+pos,8);ULONG count=be_number(data+pos+8,4);
        if(data[pos+12]||data[pos+13]||data[pos+14]||data[pos+15]||count>ZDEV_UNMAP_SECTORS-total){
            scsi_error(io,s,5,0x26);goto done;
        }
        if(count&&(lba>size/512||count>size/512-lba)){scsi_error(io,s,5,0x21);goto done;}
        total+=count;
    }
    if(readonly){scsi_error(io,s,7,0x27);goto done;}
    for(ULONG pos=8;pos<n;pos+=16){
        ULONG count=be_number(data+pos+8,4);if(!count)continue;
        if(cancelled(b)){io->io_Error=IOERR_ABORTED;goto done;}
        init(q,session,fd,OZ_ZVOL_DISCARD);q->offset=be_number(data+pos,8)*512;q->count=count*512;
        int error=call(q);
        if(error){scsi_error(io,s,error==EROFS?7:4,error==EROFS?0x27:0x44);goto done;}
    }
    s->scsi_Actual=n;io->io_Actual=sizeof(*s);
done:FreeVec(data);
}
static void scsi_query(struct zdev *b,struct IOStdReq *io,UQUAD size,UQUAD block,
    int readonly,struct oz_ticket session,LONG fd,struct oz_zvol_request *q)
{
    if(!io->io_Data||io->io_Length<sizeof(struct SCSICmd)){io->io_Error=IOERR_BADLENGTH;return;}
    struct SCSICmd *s=io->io_Data;
    s->scsi_Actual=0;s->scsi_CmdActual=0;s->scsi_Status=0;s->scsi_SenseActual=0;
    if(!s->scsi_Command||!s->scsi_CmdLength){io->io_Error=IOERR_BADADDRESS;return;}
    UBYTE *c=s->scsi_Command,asc=0;
    int trim=0;
    if(c[0]==0x42||c[0]==0x9e||(c[0]==0x12&&s->scsi_CmdLength==6&&(c[1]&1))){
        init(q,session,fd,OZ_ZVOL_CAPABILITIES);int e=call(q);
        if(!e)trim=!!(q->flags&OZ_ZVOL_CAP_DISCARD);
        else if(e!=EINVAL&&e!=ENOTSUP){io->io_Error=ioerror(e);return;}
    }
    if(c[0]==0x42){
        s->scsi_CmdActual=s->scsi_CmdLength;
        if(!trim){scsi_error(io,s,5,0x20);return;}
        scsi_unmap(b,io,s,size,readonly,session,fd,q);return;
    }
    int wide=c[0]==0x9e,inquiry=c[0]==0x12;
    ULONG allocation=8;
    if(c[0]!=0x25&&!wide&&!inquiry)asc=0x20; /* INVALID COMMAND OPERATION CODE */
    else if(s->scsi_CmdLength!=(inquiry?6:(wide?16:10)))asc=0x24;
    else {
        if(!(s->scsi_Flags&SCSIF_READ))asc=0x24;
        if(inquiry){
            if((c[1]&~1)||c[5]||(!(c[1]&1)&&c[2])||
                ((c[1]&1)&&c[2]!=0&&(!trim||(c[2]!=0xb0&&c[2]!=0xb2))))asc=0x24;
            allocation=((ULONG)c[3]<<8)|c[4];
        }else if(wide){
            if(c[1]!=0x10||c[14]||c[15])asc=0x24;
            for(unsigned i=2;i<10;i++)if(c[i])asc=0x24;
            allocation=((ULONG)c[10]<<24)|((ULONG)c[11]<<16)|((ULONG)c[12]<<8)|c[13];
        }else for(unsigned i=1;i<10;i++)if(c[i])asc=0x24;
    }
    s->scsi_CmdActual=s->scsi_CmdLength;
    if(asc){
        s->scsi_Status=2;io->io_Error=HFERR_BadStatus;
        if((s->scsi_Flags&SCSIF_AUTOSENSE)&&s->scsi_SenseData){
            UBYTE sense[18]={0x70,0,5,0,0,0,0,10,0,0,0,0,0,0,0,0,0,0};sense[12]=asc;
            ULONG n=s->scsi_SenseLength;if(n>sizeof(sense))n=sizeof(sense);
            CopyMem(sense,s->scsi_SenseData,n);s->scsi_SenseActual=n;
        }
        return;
    }
    UBYTE bytes[64]={0};ULONG n;
    if(inquiry){
      if(c[1]&1){
        bytes[1]=c[2];
        if(!c[2]){n=trim?7:5;bytes[4]=0;if(trim){bytes[5]=0xb0;bytes[6]=0xb2;}}
        else if(c[2]==0xb2){n=8;bytes[5]=0x80;bytes[6]=2;}
        else{n=64;put_be(bytes+20,ZDEV_UNMAP_SECTORS,4);put_be(bytes+24,ZDEV_UNMAP_DESCRIPTORS,4);
            put_be(bytes+28,block/512,4);put_be(bytes+32,0x80000000UL,4);}
        put_be(bytes+2,n-4,2);
      }else{
        bytes[3]=2;bytes[4]=31; /* Standard 36-byte direct-access response. */
        CopyMem("OpenZFS ",bytes+8,8);CopyMem("AROS zvol       ",bytes+16,16);
        CopyMem("1.10",bytes+32,4);n=36;
      }
    }else{
        if(size<512){io->io_Error=TDERR_DiskChanged;return;}
        UQUAD last=size/512-1;unsigned width=wide?8:4;
        if(!wide&&last>0xffffffffULL)last=0xffffffffULL;
        for(unsigned i=0;i<width;i++)bytes[width-1-i]=(UBYTE)(last>>(8*i));
        bytes[width+2]=2; /* Big-endian 512-byte logical block length. */
        if(wide&&trim)bytes[14]=0x80; /* LBPME; do not claim LBPRZ for partial edges. */
        n=wide?32:8;
    }
    if(n>allocation)n=allocation;
    if(n>s->scsi_Length)n=s->scsi_Length;
    if(n&&!s->scsi_Data){io->io_Error=IOERR_BADADDRESS;return;}
    if(n)CopyMem(bytes,s->scsi_Data,n);
    s->scsi_Actual=n;io->io_Actual=sizeof(*s);
}
static void perform(struct zdev *b,struct IOStdReq *io,struct oz_ticket session,
    LONG *fd,struct oz_zvol_request *q)
{
    ULONG high=io->io_Actual;io->io_Actual=0;io->io_Error=0;
    if(cancelled(b)){io->io_Error=IOERR_ABORTED;return;}
    if(io->io_Command==NSCMD_DEVICEQUERY){
        if(!io->io_Data||io->io_Length<sizeof(struct NSDeviceQueryResult)){io->io_Error=IOERR_BADLENGTH;return;}
        struct NSDeviceQueryResult r={0,sizeof(r),NSDEVTYPE_TRACKDISK,0,commands};
        CopyMem(&r,io->io_Data,sizeof(r));io->io_Actual=sizeof(r);return;
    }
    if(io->io_Command==TD_CHANGESTATE){io->io_Actual=*fd<0;return;}
    if(io->io_Command==TD_CHANGENUM){io->io_Actual=b->generation[unit_index(b,io->io_Unit)];return;}
    if(io->io_Command==TD_GETDRIVETYPE){io->io_Actual=DRIVE_NEWSTYLE;return;}
    if(io->io_Command==OZCMD_GETMAPPING){
        if(!io->io_Data||io->io_Length<sizeof(struct oz_device_mapping)){io->io_Error=IOERR_BADLENGTH;return;}
        struct oz_device_mapping m={0};
        m.version=OZ_MAPPING_VERSION;m.structure_size=sizeof(m);
        m.unit=unit_index(b,io->io_Unit);m.unit_count=b->unit_count;
        strcpy(m.dataset,b->names[m.unit]);
        if(*fd>=0){
            init(q,session,*fd,OZ_ZVOL_INFO);int e=call(q);
            if(e){io->io_Error=ioerror(e);return;}
            m.flags=OZ_MAPPING_PRESENT|(q->readonly?OZ_MAPPING_READONLY:0);
            m.bytes=q->size;m.sector_size=512;
        }
        CopyMem(&m,io->io_Data,sizeof(m));io->io_Actual=sizeof(m);return;
    }
    if(*fd<0){io->io_Error=TDERR_DiskChanged;return;}
    if(io->io_Command==TD_EJECT){
#ifdef OZ_DEVICE_TRACE
        bug("ZDEV eject closing descriptor=%ld\n",(long)*fd);
#endif
        init(q,session,*fd,OZ_ZVOL_CLOSE);int e=call(q);if(!e){
            *fd=-1;
            ++b->generation[unit_index(b,io->io_Unit)];
            /* Task-context Cause delivers before the registration can be
             * removed by another task. Interrupt callbacks must not block. */
            ObtainSemaphore(&b->lock);
            struct Node *n;
            for(n=b->changes.lh_Head;n->ln_Succ;n=n->ln_Succ){
                struct IOStdReq *watch=(void *)n;
                if(watch->io_Unit==io->io_Unit)Cause((struct Interrupt *)watch->io_Data);
            }
            ReleaseSemaphore(&b->lock);
        }
#ifdef OZ_DEVICE_TRACE
        bug("ZDEV eject close result=%d\n",e);
#endif
        io->io_Error=ioerror(e);return;
    }
    if(io->io_Command==CMD_UPDATE){init(q,session,*fd,OZ_ZVOL_FLUSH);io->io_Error=ioerror(call(q));return;}
    if(io->io_Command==CMD_CLEAR||io->io_Command==TD_MOTOR)return;
    init(q,session,*fd,OZ_ZVOL_INFO);int error=call(q);
    if(error){io->io_Error=ioerror(error);return;}
    UQUAD size=q->size;int readonly=q->readonly;
    if(io->io_Command==HD_SCSICMD){UQUAD block=q->block_size;scsi_query(b,io,size,block,readonly,session,*fd,q);return;}
    if(io->io_Command==TD_PROTSTATUS){io->io_Actual=readonly;return;}
    if(io->io_Command==TD_GETGEOMETRY){
        if(!io->io_Data||io->io_Length<sizeof(struct DriveGeometry)){io->io_Error=IOERR_BADLENGTH;return;}
        UQUAD sectors=size/512;ULONG count=sectors>0xffffffffULL?0xffffffffUL:(ULONG)sectors;
        struct DriveGeometry g={512,count,count,1,1,1,MEMF_PUBLIC,DG_DIRECT_ACCESS,0,0};
        CopyMem(&g,io->io_Data,sizeof(g));io->io_Actual=sizeof(g);return;
    }
    int read=io->io_Command==CMD_READ||io->io_Command==TD_READ64||io->io_Command==NSCMD_TD_READ64;
    int write=io->io_Command==CMD_WRITE||io->io_Command==TD_FORMAT||io->io_Command==TD_WRITE64||
        io->io_Command==TD_FORMAT64||io->io_Command==NSCMD_TD_WRITE64||io->io_Command==NSCMD_TD_FORMAT64;
    if(!read&&!write){io->io_Error=IOERR_NOCMD;return;}
    int wide=io->io_Command==TD_READ64||io->io_Command==TD_WRITE64||io->io_Command==TD_FORMAT64||
        io->io_Command==NSCMD_TD_READ64||io->io_Command==NSCMD_TD_WRITE64||io->io_Command==NSCMD_TD_FORMAT64;
    UQUAD offset=io->io_Offset|(wide?(UQUAD)high<<32:0);
    if((offset&511)||(io->io_Length&511)||offset>size||io->io_Length>size-offset){io->io_Error=IOERR_BADLENGTH;return;}
    if(io->io_Length&&!io->io_Data){io->io_Error=IOERR_BADADDRESS;return;}
    if(write&&readonly){io->io_Error=TDERR_WriteProt;return;}
    while(io->io_Actual<io->io_Length){
        if(cancelled(b)){io->io_Error=IOERR_ABORTED;return;}
        ULONG n=io->io_Length-io->io_Actual;if(n>OZ_ZVOL_IO_MAX)n=OZ_ZVOL_IO_MAX;
        init(q,session,*fd,write?OZ_ZVOL_WRITE:OZ_ZVOL_READ);q->offset=offset+io->io_Actual;q->count=n;
        if(write)CopyMem((UBYTE *)io->io_Data+io->io_Actual,q+1,n);
        error=call(q);
        if(q->completed>n){io->io_Error=TDERR_NotSpecified;return;}
        if(read&&q->completed)CopyMem(q+1,(UBYTE *)io->io_Data+io->io_Actual,q->completed);
        io->io_Actual+=q->completed;
        if(error||q->completed!=n){io->io_Error=ioerror(error?error:EIO);return;}
    }
}
static void eject_mapping(struct zdev *b,struct IOStdReq *io,struct oz_ticket session,
    LONG *descriptor,struct oz_zvol_request *q)
{
    io->io_Actual=0;io->io_Error=0;
    if(!io->io_Data||io->io_Length!=sizeof(struct oz_device_eject)){
        io->io_Error=IOERR_BADLENGTH;return;
    }
    struct oz_device_eject request;CopyMem(io->io_Data,&request,sizeof(request));
    if(request.version!=OZ_EJECT_VERSION||request.structure_size!=sizeof(request)){
        io->io_Error=IOERR_BADLENGTH;return;
    }
    int unit=unit_index(b,io->io_Unit);
    ObtainSemaphore(&b->lock);
    int busy=b->opens[unit]!=1||b->pending!=1;
    for(struct Node *n=b->changes.lh_Head;n->ln_Succ;n=n->ln_Succ)
        if(((struct IOStdReq *)n)->io_Unit==io->io_Unit)busy=1;
    if(busy)io->io_Error=IOERR_UNITBUSY;
    else if(*descriptor<0||request.expected_change!=b->generation[unit])io->io_Error=TDERR_DiskChanged;
    else if(b->cancelled)io->io_Error=IOERR_ABORTED;
    else b->remapping[unit]=1;
    ReleaseSemaphore(&b->lock);
    if(io->io_Error)return;
    init(q,session,*descriptor,OZ_ZVOL_FLUSH);io->io_Error=ioerror(call(q));
    if(!io->io_Error){
        io->io_Command=TD_EJECT;perform(b,io,session,descriptor,q);
        io->io_Command=OZCMD_EJECTMAPPING;
        if(*descriptor<0)io->io_Actual=1;
    }
    ObtainSemaphore(&b->lock);b->remapping[unit]=0;ReleaseSemaphore(&b->lock);
}
static void remap(struct zdev *b,struct IOStdReq *io,struct oz_ticket *session,
    LONG *descriptors,struct oz_zvol_request *q)
{
    io->io_Actual=0;io->io_Error=0;
    int unit=unit_index(b,io->io_Unit);
    if(!io->io_Data||io->io_Length!=sizeof(struct oz_device_remap)){
        io->io_Error=IOERR_BADLENGTH;return;
    }
    struct oz_device_remap m;
    CopyMem(io->io_Data,&m,sizeof(m));
    if(m.version!=OZ_REMAP_VERSION||m.structure_size!=sizeof(m)||
       (m.flags&~OZ_REMAP_WRITABLE)||!m.dataset[0]||
       !memchr(m.dataset,0,sizeof(m.dataset))){
        io->io_Error=IOERR_BADLENGTH;return;
    }
    ObtainSemaphore(&b->lock);
    int busy=descriptors[unit]>=0||b->opens[unit]!=1||b->pending!=1;
    struct Node *n;
    for(n=b->changes.lh_Head;n->ln_Succ;n=n->ln_Succ)
        if(((struct IOStdReq *)n)->io_Unit==io->io_Unit)busy=1;
    for(unsigned i=0;i<b->unit_count;i++)
        if(descriptors[i]>=0&&!strcmp(m.dataset,b->names[i]))busy=1;
    if(busy)io->io_Error=IOERR_UNITBUSY;
    else if(m.expected_change!=b->generation[unit])io->io_Error=TDERR_DiskChanged;
    else if(b->cancelled)io->io_Error=IOERR_ABORTED;
    else b->remapping[unit]=1;
    ReleaseSemaphore(&b->lock);
    if(io->io_Error)return;
    int fresh=!session->number;
    int error=fresh?oz_session_open(OZ_ADMISSION_PORT,session):0;
    if(!error){
        init(q,*session,-1,OZ_ZVOL_OPEN);
        strcpy(q->name,m.dataset);
        q->flags=(m.flags&OZ_REMAP_WRITABLE)?OZ_ZVOL_WRITABLE:0;
        error=call(q);
        if(!error){
            descriptors[unit]=q->descriptor;
            strcpy(b->names[unit],m.dataset);
            ++b->generation[unit];
        }
    }
    if(error&&fresh&&session->number){
        if(!oz_session_close(OZ_ADMISSION_PORT,session))
            memset(session,0,sizeof(*session));
    }
    ObtainSemaphore(&b->lock);b->remapping[unit]=0;ReleaseSemaphore(&b->lock);
    io->io_Error=ioerror(error);
}
static int list_mappings(void)
{
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io){if(port)DeleteMsgPort(port);return 20;}
    unsigned count=1;int result=0;
    for(unsigned unit=0;unit<count;unit++){
        if(OpenDevice((CONST_STRPTR)"openzfs.device",unit,(struct IORequest *)io,0)){
            puts("OpenZFSDevice: cannot open mapped unit (device absent or access denied)");result=20;break;
        }
        struct oz_device_mapping m={0};
        io->io_Command=OZCMD_GETMAPPING;io->io_Data=&m;io->io_Length=sizeof(m);
        int error=DoIO((struct IORequest *)io);CloseDevice((struct IORequest *)io);
        if(error||io->io_Actual!=sizeof(m)||m.version!=OZ_MAPPING_VERSION||
           m.structure_size!=sizeof(m)||m.unit!=unit||!m.unit_count||m.unit_count>ZDEV_UNITS||
           (unit&&m.unit_count!=count)||!memchr(m.dataset,0,sizeof(m.dataset))){
            puts("OpenZFSDevice: mapping query failed or unsupported");result=20;break;
        }
        count=m.unit_count;
        printf("unit=%u dataset=%s state=%s bytes=%llu sector=%lu\n",unit,m.dataset,
            !(m.flags&OZ_MAPPING_PRESENT)?"absent":(m.flags&OZ_MAPPING_READONLY)?"readonly":"writable",
            (unsigned long long)m.bytes,(unsigned long)m.sector_size);
    }
    DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);return result;
}
static int mountfile_command(int argc,char **argv)
{
    /* Emit only a standard Mount description. The caller owns its output file
     * and explicitly chooses when to mount; this command never formats media. */
    if((argc!=4&&argc!=5)||!argv[2][0]||
       (strcmp(argv[3],"FFS")&&strcmp(argv[3],"FAT")))return 20;
    unsigned unit=0;
    for(const char *p=argv[2];*p;p++){
        if(*p<'0'||*p>'9')return 20;
        unit=unit*10+(*p-'0');if(unit>=ZDEV_UNITS)return 20;
    }
    UQUAD requested=0;
    if(argc==5&&oz_mountfile_count(argv[4],&requested))return 20;
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io){if(port)DeleteMsgPort(port);return 20;}
    struct oz_device_mapping m={0};
    int error=OpenDevice((CONST_STRPTR)"openzfs.device",unit,(struct IORequest *)io,0);
    if(!error){
        io->io_Command=OZCMD_GETMAPPING;io->io_Data=&m;io->io_Length=sizeof(m);
        error=DoIO((struct IORequest *)io);
        if(!error&&(io->io_Actual!=sizeof(m)||m.version!=OZ_MAPPING_VERSION||
           m.structure_size!=sizeof(m)||m.unit!=unit||!(m.flags&OZ_MAPPING_PRESENT)||
           m.sector_size!=512||!m.bytes||m.bytes%512))error=IOERR_BADLENGTH;
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);
    if(error){fprintf(stderr,"OpenZFSDevice: no usable mapping (device error %d)\n",error);return 20;}
    char text[OZ_MOUNTFILE_SIZE];
    error=oz_mountfile_format(&m,!strcmp(argv[3],"FFS"),requested,text,sizeof(text));
    if(error){fprintf(stderr,"OpenZFSDevice: %s\n",error==EFBIG?
        "view exceeds mapped capacity":"capacity cannot be represented exactly");return 20;}
    if(fputs(text,stdout)==EOF||fflush(stdout))return 20;
    return 0;
}
static int eject_command(int argc,char **argv)
{
    if(argc!=3||!argv[2][0])return 20;
    unsigned unit=0;
    for(const char *p=argv[2];*p;p++){
        if(*p<'0'||*p>'9')return 20;
        unit=unit*10+(*p-'0');if(unit>=ZDEV_UNITS)return 20;
    }
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io){if(port)DeleteMsgPort(port);return 20;}
    int released=0,error=OpenDevice((CONST_STRPTR)"openzfs.device",unit,(struct IORequest *)io,0);
    if(!error){
        io->io_Command=TD_CHANGENUM;error=DoIO((struct IORequest *)io);
        if(!error){
            struct oz_device_eject request={OZ_EJECT_VERSION,sizeof(request),io->io_Actual};
            io->io_Command=OZCMD_EJECTMAPPING;io->io_Data=&request;io->io_Length=sizeof(request);
            error=DoIO((struct IORequest *)io);released=io->io_Actual==1;
        }
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);
    if(error){printf("OpenZFSDevice: %s (device error %d)\n",
        released?"ejected; session cleanup failed":"eject refused or failed",error);return 20;}
    printf("OpenZFSDevice: ejected unit=%u\n",unit);return 0;
}
static int remap_command(int argc,char **argv)
{
    if((argc!=4&&argc!=5)||(argc==5&&strcmp(argv[4],"WRITABLE"))||
       !argv[2][0]||!argv[3][0]||strlen(argv[3])>=256)return 20;
    unsigned unit=0;
    for(const char *p=argv[2];*p;p++){
        if(*p<'0'||*p>'9')return 20;
        unit=unit*10+(*p-'0');if(unit>=ZDEV_UNITS)return 20;
    }
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io){if(port)DeleteMsgPort(port);return 20;}
    int error=OpenDevice((CONST_STRPTR)"openzfs.device",unit,(struct IORequest *)io,0);
    if(!error){
        io->io_Command=TD_CHANGENUM;
        error=DoIO((struct IORequest *)io);
        if(!error){
            struct oz_device_remap m={OZ_REMAP_VERSION,sizeof(m),
                argc==5?OZ_REMAP_WRITABLE:0,io->io_Actual,{0}};
            strcpy(m.dataset,argv[3]);
            io->io_Command=OZCMD_SETMAPPING;io->io_Data=&m;io->io_Length=sizeof(m);
            error=DoIO((struct IORequest *)io);
        }
        CloseDevice((struct IORequest *)io);
    }
    DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);
    if(error){printf("OpenZFSDevice: remap refused or failed (device error %d)\n",error);return 20;}
    printf("OpenZFSDevice: remapped unit=%u dataset=%s mode=%s\n",unit,argv[3],
        argc==5?"writable-requested":"readonly");
    return 0;
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"LIST"))return list_mappings();
    if(argc>1&&!strcmp(argv[1],"EJECT"))return eject_command(argc,argv);
    if(argc>1&&!strcmp(argv[1],"MOUNTFILE"))return mountfile_command(argc,argv);
    if(argc>1&&!strcmp(argv[1],"REMAP"))return remap_command(argc,argv);
    int empty=argc==2&&!strcmp(argv[1],"EMPTY");
    int writable=argc>1&&!strcmp(argv[argc-1],"WRITABLE");
    unsigned count=empty?ZDEV_UNITS:argc-1-writable;
    if(!count||count>ZDEV_UNITS){
        puts("OpenZFSDevice EMPTY | pool/volume [pool/volume ...] [WRITABLE]");
        puts("OpenZFSDevice LIST | REMAP unit pool/volume [WRITABLE]");
        puts("OpenZFSDevice EJECT unit (must be unused; no implicit unmount)");
        puts("OpenZFSDevice MOUNTFILE unit FFS|FAT [512-byte-block-count] >mountfile");
        puts("EMPTY publishes 32 absent slots; no import or mapping. Resident until reboot.");return 20;}
    for(unsigned i=0;!empty&&i<count;i++){
        if(!argv[i+1][0]||strlen(argv[i+1])>=256)return 20;
        for(unsigned j=0;j<i;j++)if(!strcmp(argv[i+1],argv[j+1]))return 20;
    }
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured()||secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM){if(secBase)CloseLibrary(secBase);return 20;}
    struct oz_ticket session={0};LONG descriptors[ZDEV_UNITS];
    for(unsigned i=0;i<ZDEV_UNITS;i++)descriptors[i]=-1;
    struct oz_zvol_request *q=AllocVec(sizeof(*q)+OZ_ZVOL_IO_MAX,MEMF_PUBLIC|MEMF_CLEAR);
    if(!q){CloseLibrary(secBase);return 20;}
    int error=empty?0:oz_session_open(OZ_ADMISSION_PORT,&session);
    for(unsigned i=0;!empty&&i<count&&!error;i++){
        init(q,session,-1,OZ_ZVOL_OPEN);strcpy(q->name,argv[i+1]);q->flags=writable?OZ_ZVOL_WRITABLE:0;
        error=call(q);if(!error)descriptors[i]=q->descriptor;
    }
    BYTE sig=-1;struct zdev *b=NULL;
    if(!error){sig=AllocSignal(-1);if(sig<0)error=ENOMEM;}
    if(!error){b=(void *)MakeLibrary(vectors,NULL,NULL,sizeof(*b),BNULL);if(!b)error=ENOMEM;}
    if(error){if(session.number)(void)oz_session_close(OZ_ADMISSION_PORT,&session);if(sig>=0)FreeSignal(sig);FreeVec(q);CloseLibrary(secBase);return 20;}
    memset((char *)b+sizeof(struct Device),0,sizeof(*b)-sizeof(struct Device));
    InitSemaphore(&b->lock);NewList(&b->queue);NewList(&b->changes);b->worker=FindTask(NULL);b->security=secBase;b->signal=1UL<<sig;
    b->unit_count=count;
    for(unsigned i=0;!empty&&i<count;i++)strcpy(b->names[i],argv[i+1]);
    b->device.dd_Library.lib_Node.ln_Name="openzfs.device";
    b->device.dd_Library.lib_Version=1;b->device.dd_Library.lib_Revision=10;
    b->device.dd_Library.lib_IdString="openzfs.device 1.10 resident zvol transport";
#ifdef OZ_DEVICE_STARTUP_TEST
    int published=startup_barrier()?publish(b):-1;
#else
    int published=publish(b);
#endif
    if(published!=1){
        if(session.number)(void)oz_session_close(OZ_ADMISSION_PORT,&session);
        ULONG bytes=b->device.dd_Library.lib_NegSize+b->device.dd_Library.lib_PosSize;
        FreeMem((UBYTE *)b-b->device.dd_Library.lib_NegSize,bytes);
        FreeSignal(sig);FreeVec(q);CloseLibrary(secBase);
        if(!published){puts("OpenZFSDevice: resident device already registered");return 10;}
        puts("OpenZFSDevice: publication failed");return 20;
    }
    for(unsigned i=0;i<count;i++)bug("OpenZFS device ready unit=%u dataset=%s writable=%u\n",i,b->names[i],writable);
    for(;;){
        ULONG signals=Wait(b->signal|SIGBREAKF_CTRL_C);
        for(;;){
            ObtainSemaphore(&b->lock);struct IOStdReq *io=(void *)RemHead(&b->queue);
            b->active=io;b->cancelled=0;ReleaseSemaphore(&b->lock);
            if(!io)break;
#ifdef OZ_DEVICE_TRACE
            bug("ZDEV perform command=%u request=%p\n",io->io_Command,io);
#endif
            int unit=unit_index(b,io->io_Unit);
            if(unit<0){io->io_Error=IOERR_OPENFAIL;io->io_Actual=0;}
            else if(io->io_Command==OZCMD_SETMAPPING)remap(b,io,&session,descriptors,q);
            else if(io->io_Command==OZCMD_EJECTMAPPING)eject_mapping(b,io,session,&descriptors[unit],q);
            else perform(b,io,session,&descriptors[unit],q);
            /* Eject must release the session as well as its volume descriptor.
             * Keep device vectors resident, but do not pin the engine forever. */
            unsigned present=0;
            for(unsigned i=0;i<count;i++)if(descriptors[i]>=0)present++;
            if(!present&&session.number){
                int e=oz_session_close(OZ_ADMISSION_PORT,&session);
                if(!e)memset(&session,0,sizeof(session));
                else if(!io->io_Error)io->io_Error=ioerror(e);
            }
            ObtainSemaphore(&b->lock);b->active=NULL;--b->pending;ReleaseSemaphore(&b->lock);
#ifdef OZ_DEVICE_TRACE
            bug("ZDEV reply command=%u request=%p error=%d\n",io->io_Command,io,io->io_Error);
#endif
            ReplyMsg(&io->io_Message);
        }
        if(signals&SIGBREAKF_CTRL_C)bug("OpenZFS device remains resident; TD_EJECT releases each unit backing volume. Reboot to unload.\n");
    }
}
