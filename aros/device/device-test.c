/* SPDX-License-Identifier: CDDL-1.0 */
/* Included by the isolated native command fixture; never uses real storage. */
#include <exec/io.h>
#include <exec/interrupts.h>
#include <aros/asmcall.h>
#include <exec/errors.h>
#include <exec/memory.h>
#include <devices/trackdisk.h>
#include <devices/newstyle.h>
#include <devices/scsidisk.h>
#include <exec/execbase.h>
#include <proto/execlock.h>
#include <resources/execlock.h>
#include <aros/types/spinlock_s.h>
#include "mapping.h"
#ifdef OZ_TEST_BACKING_MEDIA_CHANGE
#include "../engine/device-io.h"
static unsigned media_notifications;
static void media_test_notify(void)
{__atomic_add_fetch(&media_notifications,1,__ATOMIC_RELEASE);}
#endif
static AROS_INTH1(device_change_callback, volatile ULONG *, count)
{
    AROS_INTFUNC_INIT
    __atomic_add_fetch(count,1,__ATOMIC_SEQ_CST);
    return 0;
    AROS_INTFUNC_EXIT
}
#if defined(OZ_TEST_ZVOL_RESIZE_API) || defined(OZ_TEST_ZVOL_READONLY_POLICY)
static int device_resize_api(unsigned operation,const char *target,const char *argument,
    unsigned flags,int expected,const char *value)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=flags;strcpy(m->target,target);strcpy(m->argument,argument);
    if(operation==40){
        struct oz_client client;free(out);out=NULL;
        if(!oz_client_open(&client)){free(m);return 1;}
        int failed=oz_submit(&client,m)!=OZ_OK;
        if(!failed)while(!(out=oz_poll(&client)))Delay(1);
        if(!failed){
            bug("Manager zvol get status=%u rows=%u value=%s\n",(unsigned)out->status,
                (unsigned)out->row_count,out->row_count?out->rows[0].value:"none");
            failed=out->status!=OZ_OK||out->row_count!=1||!value||strcmp(out->rows[0].value,value);
        }
        if(out)oz_message_free(out);
        oz_client_close(&client);free(m);return failed;
    }
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket),failed=0;
    if(!error){
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(error)failed=1;
        else{
            const char *found=strstr(out->detail,"errno=");char match[32];snprintf(match,sizeof(match),"errno=%d",expected);
            bug("Manager zvol property operation=%u expected=%d status=%u detail=%s\n",operation,expected,(unsigned)out->status,out->detail);
            failed=out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK);
            if(expected)failed|=!found||strcmp(found,match);
            if(value)failed|=out->row_count!=1||strcmp(out->rows[0].value,value);
        }
        if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))failed=1;
    }else failed=!expected||error!=expected;
    free(m);free(out);return failed;
}
#endif
static int device_mapping_test(struct IOStdReq *io,unsigned unit,unsigned count,
    const char *name,UQUAD bytes,ULONG flags)
{
    struct oz_device_mapping m;
    memset(&m,0xa5,sizeof(m));
    io->io_Command=OZCMD_GETMAPPING;io->io_Data=&m;io->io_Length=sizeof(m)-1;
    if(DoIO((struct IORequest *)io)!=IOERR_BADLENGTH||io->io_Actual)return 1;
    for(unsigned i=0;i<sizeof(m);i++)if(((UBYTE *)&m)[i]!=0xa5)return 2;
    io->io_Length=sizeof(m);
    if(DoIO((struct IORequest *)io)||io->io_Actual!=sizeof(m))return 3;
    if(m.version!=OZ_MAPPING_VERSION||m.structure_size!=sizeof(m)||m.unit!=unit||
       m.unit_count!=count||m.flags!=flags||m.bytes!=bytes||
       m.sector_size!=(bytes?512:0)||strcmp(m.dataset,name))return 4;
    return 0;
}
static int device_safe_eject_test(struct IOStdReq *io,struct MsgPort *port)
{
    io->io_Command=TD_CHANGENUM;
    if(DoIO((struct IORequest *)io))return 1;
    ULONG generation=io->io_Actual;
    struct oz_device_eject request={0,sizeof(request),generation};
    io->io_Command=OZCMD_EJECTMAPPING;io->io_Data=&request;io->io_Length=sizeof(request);
    if(DoIO((struct IORequest *)io)!=IOERR_BADLENGTH||io->io_Actual)return 2;
    request.version=OZ_EJECT_VERSION;io->io_Length--;
    if(DoIO((struct IORequest *)io)!=IOERR_BADLENGTH||io->io_Actual)return 3;
    io->io_Length=sizeof(request);request.expected_change=generation+1;
    if(DoIO((struct IORequest *)io)!=TDERR_DiskChanged||io->io_Actual)return 4;
    request.expected_change=generation;
    struct IOStdReq *peer=(void *)CreateIORequest(port,sizeof(*peer));
    if(!peer)return 5;
    if(OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)peer,0)){
        DeleteIORequest((struct IORequest *)peer);return 6;}
    int error=DoIO((struct IORequest *)io);
    CloseDevice((struct IORequest *)peer);DeleteIORequest((struct IORequest *)peer);
    if(error!=IOERR_UNITBUSY||io->io_Actual)return 7;
    io->io_Command=TD_CHANGESTATE;
    if(DoIO((struct IORequest *)io)||io->io_Actual)return 8;
    io->io_Command=OZCMD_EJECTMAPPING;
    if(DoIO((struct IORequest *)io)||io->io_Actual!=1)return 9;
    if(DoIO((struct IORequest *)io)!=TDERR_DiskChanged||io->io_Actual)return 10;
    io->io_Command=TD_CHANGENUM;
    if(DoIO((struct IORequest *)io)||io->io_Actual!=generation+1)return 11;
    bug("RESULT native zvol guarded eject PASS malformed=1 stale=1 busy=1 preserved=1 released=1 generation=1 absent-refused=1\n");
    return 0;
}
static int device_scsi_capacity_test(struct IOStdReq *io,UQUAD size)
{
    UBYTE command[16]={0x9e,0x10,0,0,0,0,0,0,0,0,0,0,0,32,0,0};
    UBYTE data[104],sense[18];struct SCSICmd scsi={0};
    scsi.scsi_Command=command;scsi.scsi_CmdLength=16;
    scsi.scsi_Data=(UWORD *)data;scsi.scsi_Length=32;
    scsi.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    scsi.scsi_SenseData=sense;scsi.scsi_SenseLength=sizeof(sense);
    io->io_Command=HD_SCSICMD;io->io_Data=&scsi;io->io_Length=sizeof(scsi);
    memset(data,0xa5,sizeof(data));
    if(DoIO((struct IORequest *)io)||scsi.scsi_Status||scsi.scsi_Actual!=32||scsi.scsi_CmdActual!=16)return 1;
    UQUAD last=0;for(unsigned i=0;i<8;i++)last=(last<<8)|data[i];
    if(last!=size/512-1||data[8]||data[9]||data[10]!=2||data[11]||data[32]!=0xa5)return 2;
    /* Both caller length and CDB allocation length bound the returned bytes. */
    memset(data,0xa5,sizeof(data));command[13]=12;
    if(DoIO((struct IORequest *)io)||scsi.scsi_Actual!=12||data[12]!=0xa5)return 3;
    memset(data,0xa5,sizeof(data));scsi.scsi_Length=4;
    if(DoIO((struct IORequest *)io)||scsi.scsi_Actual!=4||data[4]!=0xa5)return 4;
    scsi.scsi_Length=32;command[13]=0;scsi.scsi_Data=NULL;
    if(DoIO((struct IORequest *)io)||scsi.scsi_Actual)return 5;
    command[13]=32;
    if(DoIO((struct IORequest *)io)!=IOERR_BADADDRESS||scsi.scsi_Actual)return 10;
    scsi.scsi_Data=(UWORD *)data;scsi.scsi_CmdLength=1;
    if(DoIO((struct IORequest *)io)!=HFERR_BadStatus||sense[12]!=0x24)return 11;
    scsi.scsi_CmdLength=16;scsi.scsi_Flags=SCSIF_AUTOSENSE;
    if(DoIO((struct IORequest *)io)!=HFERR_BadStatus||sense[12]!=0x24)return 12;
    scsi.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    scsi.scsi_Data=(UWORD *)data;command[13]=32;command[1]=0x11;
    if(DoIO((struct IORequest *)io)!=HFERR_BadStatus||scsi.scsi_Status!=2||sense[2]!=5||sense[12]!=0x24)return 6;
    command[0]=0xff;
    if(DoIO((struct IORequest *)io)!=HFERR_BadStatus||sense[12]!=0x20)return 7;
    memset(command,0,sizeof(command));command[0]=0x25;scsi.scsi_CmdLength=10;
    if(DoIO((struct IORequest *)io)||scsi.scsi_Status||scsi.scsi_Actual!=8)return 8;
    UQUAD small=0;for(unsigned i=0;i<4;i++)small=(small<<8)|data[i];
    UQUAD expected=size/512-1;if(expected>0xffffffffULL)expected=0xffffffffULL;
    if(small!=expected||data[4]||data[5]||data[6]!=2||data[7])return 9;
    /* Match HDToolBox's 36-byte and the native inspector's 96-byte requests. */
    memset(command,0,sizeof(command));command[0]=0x12;scsi.scsi_CmdLength=6;
    for(unsigned allocation=36;allocation<=96;allocation+=60){
        memset(data,0xa5,sizeof(data));command[4]=allocation;scsi.scsi_Length=allocation;
        if(DoIO((struct IORequest *)io)||scsi.scsi_Status||scsi.scsi_CmdActual!=6||scsi.scsi_Actual!=36)return 13;
        if(data[0]||data[4]!=31||memcmp(data+8,"OpenZFS ",8)||memcmp(data+16,"AROS zvol       ",16)||
           (memcmp(data+32,"1.9 ",4)&&memcmp(data+32,"1.10",4))||data[36]!=0xa5)return 14;
    }
    memset(data,0xa5,sizeof(data));command[4]=4;
    if(DoIO((struct IORequest *)io)||scsi.scsi_Actual!=4||data[4]!=0xa5)return 15;
    command[1]=1;command[2]=0xff;command[4]=36;
    if(DoIO((struct IORequest *)io)!=HFERR_BadStatus||scsi.scsi_Status!=2||sense[12]!=0x24)return 16;
    bug("RESULT native zvol inquiry PASS standard36=1 inspector96=1 short-reply=1 unknown-VPD-rejected=1\n");
    return 0;
}
#ifdef OZ_TEST_ZVOL_UNMAP
static int device_unmap_test(struct IOStdReq *io,UQUAD size,int readonly)
{
    UBYTE c[16]={0x12,1,0,0,64,0},reply[64],sense[18],params[40]={0};
    static UBYTE bytes[24576];
    struct SCSICmd s={0};s.scsi_Command=c;s.scsi_CmdLength=6;
    s.scsi_Data=(UWORD *)reply;s.scsi_Length=sizeof(reply);s.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    s.scsi_SenseData=sense;s.scsi_SenseLength=sizeof(sense);
#define UCHECK(x) do{if(!(x)){bug("RESULT native zvol UNMAP FAIL line=%d\n",__LINE__);return 1;}}while(0)
#define UIO() (io->io_Command=HD_SCSICMD,io->io_Data=&s,io->io_Length=sizeof(s),DoIO((struct IORequest *)io))
    UCHECK(!UIO());
#ifdef OZ_TEST_ZVOL_UNMAP_UNSUPPORTED
    UCHECK(s.scsi_Actual==5&&reply[3]==1&&reply[4]==0);
    c[2]=0xb2;UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x24);
    memset(c,0,sizeof(c));c[0]=0x9e;c[1]=0x10;c[13]=32;s.scsi_CmdLength=16;
    UCHECK(!UIO()&&s.scsi_Actual==32&&!reply[14]);
    memset(c,0,sizeof(c));c[0]=0x42;s.scsi_CmdLength=10;s.scsi_Flags=SCSIF_AUTOSENSE;
    UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x20);
    bug("RESULT native zvol UNMAP compatibility PASS old-service=1 unadvertised=1 refused=1\n");
#else
    UCHECK(s.scsi_Actual==7&&reply[3]==3&&reply[4]==0&&reply[5]==0xb0&&reply[6]==0xb2);
    c[2]=0xb0;UCHECK(!UIO()&&s.scsi_Actual==64&&reply[3]==60&&reply[26]==0&&reply[27]==255&&reply[31]==16);
    c[2]=0xb2;UCHECK(!UIO()&&s.scsi_Actual==8&&reply[5]==0x80&&reply[6]==2);
    memset(c,0,sizeof(c));c[0]=0x9e;c[1]=0x10;c[13]=32;s.scsi_CmdLength=16;
    UCHECK(!UIO()&&s.scsi_Actual==32&&reply[14]==0x80);
    memset(c,0,sizeof(c));c[0]=0x42;c[8]=40;s.scsi_CmdLength=10;
    s.scsi_Data=(UWORD *)params;s.scsi_Length=sizeof(params);s.scsi_Flags=SCSIF_AUTOSENSE;
    params[1]=38;params[3]=32;
    /* Two descriptors: first spans partial edge blocks, second has zero length. */
    params[14]=8;params[15]=1;params[19]=32;
    if(readonly){
        UCHECK(UIO()==HFERR_BadStatus&&sense[2]==7&&sense[12]==0x27&&!s.scsi_Actual);
        io->io_Command=CMD_READ;io->io_Data=bytes;io->io_Length=sizeof(bytes);io->io_Offset=1048576;
        memset(bytes,0xa5,sizeof(bytes));UCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(bytes));
        for(unsigned i=0;i<sizeof(bytes);i++)UCHECK(bytes[i]==(i>=8192&&i<16384?0:(UBYTE)(i*13+7)));
        return 0;
    }
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(UBYTE)(i*13+7);
    io->io_Command=CMD_WRITE;io->io_Data=bytes;io->io_Length=sizeof(bytes);io->io_Offset=1048576;
    UCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(bytes));
    /* A bad second descriptor must not discard the otherwise valid first. */
    UQUAD bad=size/512;for(unsigned i=0;i<8;i++)params[31-i]=(UBYTE)(bad>>(8*i));params[35]=1;
    UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x21&&!s.scsi_Actual);
    io->io_Command=CMD_READ;io->io_Data=bytes;io->io_Length=sizeof(bytes);io->io_Offset=1048576;
    UCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(bytes));
    for(unsigned i=0;i<sizeof(bytes);i++)UCHECK(bytes[i]==(UBYTE)(i*13+7));
    memset(params+24,0,16);
    params[3]=31;UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x26);params[3]=32;
    c[1]=1;UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x24);c[1]=0;
    s.scsi_Length=39;UCHECK(UIO()==HFERR_BadStatus&&sense[12]==0x1a);s.scsi_Length=40;
    UCHECK(!UIO()&&!s.scsi_Status&&s.scsi_Actual==40&&s.scsi_CmdActual==10);
    io->io_Command=CMD_READ;io->io_Data=bytes;io->io_Length=sizeof(bytes);io->io_Offset=1048576;memset(bytes,0xa5,sizeof(bytes));
    UCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(bytes));
    for(unsigned i=0;i<sizeof(bytes);i++)UCHECK(bytes[i]==(i>=8192&&i<16384?0:(UBYTE)(i*13+7)));
    bug("RESULT native zvol UNMAP PASS VPD=1 full-block-zero=1 edges-preserved=1 invalid-list-no-write=1 malformed=3\n");
#endif
#undef UIO
#undef UCHECK
    return 0;
}
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_CAPACITY
static int device_clients_test(void)
{
    struct client_message { struct Message message; ULONG index,phase,error; };
    struct MsgPort *port=CreateMsgPort();if(!port)return 20;
    char name[80];snprintf(name,sizeof(name),"OpenZFS.DeviceClients.%p",FindTask(NULL));
    port->mp_Node.ln_Name=name;AddPort(port);
    for(unsigned i=0;i<4;i++){
        char args[100];snprintf(args,sizeof(args),"%u %s\n",i,name);
        BPTR seg=LoadSeg((CONST_STRPTR)"C:OpenZFSDeviceClientTest");
        if(!seg||!CreateNewProcTags(NP_Seglist,seg,NP_FreeSeglist,TRUE,NP_Cli,TRUE,
            NP_CommandName,(IPTR)"C:OpenZFSDeviceClientTest",NP_Name,(IPTR)"OpenZFS device client test",
            NP_Arguments,(IPTR)args,NP_CopyVars,TRUE,NP_StackSize,131072,TAG_DONE))return 20;
    }
    unsigned error=0;
    for(unsigned phase=0;phase<2;phase++){
        struct client_message *messages[4]={0};unsigned seen=0;
        for(unsigned i=0;i<4;i++){
            WaitPort(port);messages[i]=(void *)GetMsg(port);
            struct client_message *m=messages[i];
            if(!m||m->message.mn_Length!=sizeof(*m)||m->index>=4||m->phase!=phase){error=1;continue;}
            if(seen&(1U<<m->index))error=1;
            seen|=1U<<m->index;if(m->error)error=1;
        }
        if(seen!=15)error=1;
        /* All four processes have opened all units before any starts I/O. */
        for(unsigned i=0;i<4;i++)if(messages[i])ReplyMsg(&messages[i]->message);
    }
    RemPort(port);DeleteMsgPort(port);
    if(!error)bug("RESULT native zvol clients PASS processes=4 units=32 rounds=8 flushes=1024 drained=1\n");
    return error?20:0;
}
static int device_capacity_test(void)
{
#define CAPCHECK(x) do{if(!(x)){bug("RESULT native zvol capacity FAIL line=%d dos=%ld\n",__LINE__,(long)IoErr());return 20;}}while(0)
    char names[33][64],sizes[32][16],command[4096]="";
    char *args[36]={"C:OpenZFSDevice"};
    for(unsigned i=0;i<33;i++){
        snprintf(names[i],sizeof(names[i]),"qemu_mirror/device-cap-%02u",i);
        args[i+1]=names[i];
        if(i<32)snprintf(sizes[i],sizeof(sizes[i]),"%uM",i+1);
    }
    args[34]="WRITABLE";args[35]=NULL;
    CAPCHECK(!run(args,20,NULL)); /* 33-unit admission must fail. */
    for(unsigned i=0;i<31;i++)RUN(0,NULL,"C:zfs","create","-s","-V",sizes[i],names[i]);
    args[33]="WRITABLE";args[34]=NULL;
    CAPCHECK(!run(args,20,NULL)); /* Last open fails; preceding 31 must close. */
    RUN(0,NULL,"C:zfs","destroy",names[0]);
    RUN(0,NULL,"C:zfs","create","-s","-V",sizes[0],names[0]);
    RUN(0,NULL,"C:zfs","create","-s","-V",sizes[31],names[31]);
    for(unsigned i=0;i<32;i++){
        size_t used=strlen(command);
        CAPCHECK(snprintf(command+used,sizeof(command)-used," %s",names[i])<(int)(sizeof(command)-used));
    }
    strcat(command," WRITABLE\n");
    BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE),output=Open((CONST_STRPTR)"T:zvol-capacity.log",MODE_NEWFILE);
    BPTR errors=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);CAPCHECK(input&&output&&errors);
    BPTR segment=LoadSeg((CONST_STRPTR)"C:OpenZFSDevice");CAPCHECK(segment);
    CAPCHECK(CreateNewProcTags(NP_Seglist,segment,NP_FreeSeglist,TRUE,NP_Cli,TRUE,
        NP_CommandName,(IPTR)"C:OpenZFSDevice",NP_Name,(IPTR)"C:OpenZFSDevice",
        NP_Arguments,(IPTR)command,NP_Input,input,NP_CloseInput,TRUE,
        NP_Output,output,NP_CloseOutput,TRUE,NP_Error,errors,NP_CloseError,TRUE,
        NP_CopyVars,TRUE,NP_StackSize,262144,TAG_DONE));
    struct MsgPort *port=CreateMsgPort();CAPCHECK(port);
    struct IOStdReq *ios[32]={0};unsigned char data[512];
    for(unsigned i=0;i<32;i++){
        ios[i]=(void *)CreateIORequest(port,sizeof(*ios[i]));CAPCHECK(ios[i]);
        int opened=0;
        for(unsigned attempt=0;attempt<500;attempt++){
            if(!OpenDevice((CONST_STRPTR)"openzfs.device",i,(struct IORequest *)ios[i],0)){opened=1;break;}
            Delay(1);
        }
        CAPCHECK(opened);
        struct DriveGeometry geometry={0};struct IOStdReq *io=ios[i];
        io->io_Command=TD_GETGEOMETRY;io->io_Data=&geometry;io->io_Length=sizeof(geometry);
        CAPCHECK(!DoIO((struct IORequest *)io)&&geometry.dg_TotalSectors==(i+1)*2048);
        memset(data,0x40+i,sizeof(data));io->io_Command=CMD_WRITE;io->io_Data=data;io->io_Length=sizeof(data);io->io_Offset=512;
        CAPCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(data));
        io->io_Command=CMD_UPDATE;CAPCHECK(!DoIO((struct IORequest *)io));
    }
    struct IOStdReq *bad=(void *)CreateIORequest(port,sizeof(*bad));CAPCHECK(bad);
    CAPCHECK(OpenDevice((CONST_STRPTR)"openzfs.device",32,(struct IORequest *)bad,0)==IOERR_OPENFAIL);
    DeleteIORequest((struct IORequest *)bad);
    /* Separate buffers keep every outstanding request valid until WaitIO.
     * The driver has one worker: this tests queue/completion isolation across
     * units, not parallel service execution or multiple submitting tasks. */
    const ULONG chunk=131072,rounds=8;
    unsigned char *queued=AllocVec(32*chunk,MEMF_PUBLIC);
    CAPCHECK(queued);
    for(unsigned round=0;round<rounds;round++){
        for(unsigned i=0;i<32;i++){
            memset(queued+i*chunk,1+i+round*32,chunk);
            ios[i]->io_Command=CMD_WRITE;ios[i]->io_Data=queued+i*chunk;
            ios[i]->io_Length=chunk;ios[i]->io_Offset=4096+(round%4)*chunk;
            SendIO((struct IORequest *)ios[i]);
        }
        for(unsigned i=0;i<32;i++)
            CAPCHECK(!WaitIO((struct IORequest *)ios[i])&&ios[i]->io_Actual==chunk);
        for(unsigned i=0;i<32;i++){
            ios[i]->io_Command=CMD_UPDATE;SendIO((struct IORequest *)ios[i]);
        }
        for(unsigned i=0;i<32;i++)CAPCHECK(!WaitIO((struct IORequest *)ios[i]));
        memset(queued,0,32*chunk);
        for(unsigned i=0;i<32;i++){
            ios[i]->io_Command=CMD_READ;SendIO((struct IORequest *)ios[i]);
        }
        for(unsigned i=0;i<32;i++){
            CAPCHECK(!WaitIO((struct IORequest *)ios[i])&&ios[i]->io_Actual==chunk);
            unsigned char expected=(unsigned char)(1+i+round*32);
            for(unsigned n=0;n<chunk;n++)CAPCHECK(queued[i*chunk+n]==expected);
        }
        CAPCHECK(!GetMsg(port));
    }
    FreeVec(queued);
    CAPCHECK(!device_clients_test());
    /* The readonly probe below must not retain the just-freed I/O buffer. */
    ios[31]->io_Data=data;ios[31]->io_Length=sizeof(data);ios[31]->io_Offset=512;
    bug("RESULT native zvol queued capacity PASS units=32 rounds=8 write-bytes=33554432 read-bytes=33554432 flushes=256 drained=1\n");
    RUN(0,NULL,"C:zfs","set","readonly=on",names[31]);
    ios[31]->io_Command=CMD_WRITE;CAPCHECK(DoIO((struct IORequest *)ios[31])==TDERR_WriteProt);
    /* Read and eject evens first, then verify that every odd unit survives. */
    for(unsigned parity=0;parity<2;parity++)for(unsigned i=parity;i<32;i+=2){
        struct IOStdReq *io=ios[i];memset(data,0,sizeof(data));
        io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=sizeof(data);io->io_Offset=512;
        CAPCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(data));
        for(unsigned n=0;n<sizeof(data);n++)CAPCHECK(data[n]==0x40+i);
        io->io_Command=TD_EJECT;CAPCHECK(!DoIO((struct IORequest *)io));
        io->io_Command=TD_CHANGESTATE;CAPCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==1);
        CloseDevice((struct IORequest *)io);DeleteIORequest((struct IORequest *)io);
        RUN(0,NULL,"C:zfs","destroy",names[i]);
    }
    CAPCHECK(!GetMsg(port));DeleteMsgPort(port);
    bug("RESULT native zvol capacity PASS units=32 geometry=32 distinct-data=32 boundary=1 partial-startup=1 readonly=1 independent-eject=32 cleanup=1\n");
#undef CAPCHECK
    return 0;
}
#endif
static int device_test(void)
{
#ifdef OZ_TEST_ZVOL_DEVICE_CAPACITY
    return device_capacity_test();
#endif
#define DCHECK(x) do{if(!(x)){LONG dos_error=IoErr();bug("RESULT native zvol device FAIL line=%d error=%d actual=%lu dos=%ld\n",__LINE__,io?(int)io->io_Error:0,io?(unsigned long)io->io_Actual:0,(long)dos_error);return 20;}}while(0)
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    unsigned char *data=AllocVec(131072,MEMF_PUBLIC|MEMF_CLEAR);
    unsigned char *expected=AllocVec(131072,MEMF_PUBLIC);
    DCHECK(io&&data&&expected);
    for(unsigned i=0;i<131072;i++)expected[i]=(unsigned char)(i*7+i/512);
#ifndef OZ_TEST_ZVOL_DEVICE_COLD
    RUN(0,NULL,"C:zfs","create","-s","-V","6G","-b","8192","qemu_mirror/device-volume");
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_MULTI
    RUN(0,NULL,"C:zfs","create","-s","-V","3T","qemu_mirror/device-second");
    RUN(20,NULL,"C:OpenZFSDevice","qemu_mirror/device-volume","qemu_mirror/device-volume");
    RUN(20,NULL,"C:OpenZFSDevice","qemu_mirror/device-volume","qemu_mirror/absent-volume");
#endif
    unsigned launches=1;
#ifdef OZ_TEST_ZVOL_DEVICE_EMPTY
    launches=0; /* Parent proved EMPTY before starting the service. */
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_RACE
    launches=4;
    struct MsgPort *gate=CreateMsgPort();DCHECK(gate);
    gate->mp_Node.ln_Name="OpenZFSDevice.StartupTest";AddPort(gate);
#endif
    for(unsigned launch=0;launch<launches;launch++){
    char logname[64];snprintf(logname,sizeof(logname),"T:zvol-device-%u.log",launch);
    BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE);
    BPTR output=Open((CONST_STRPTR)logname,MODE_NEWFILE);
    BPTR errors=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);
    DCHECK(input&&output&&errors);
    DCHECK(SystemTags((CONST_STRPTR)
#if defined(OZ_TEST_ZVOL_DEVICE_EMPTY)
        "C:OpenZFSDevice EMPTY",
#elif defined(OZ_TEST_ZVOL_DEVICE_READONLY) || defined(OZ_TEST_ZVOL_DEVICE_COLD)
        "C:OpenZFSDevice qemu_mirror/device-volume",
#elif defined(OZ_TEST_ZVOL_DEVICE_MULTI)
        "C:OpenZFSDevice qemu_mirror/device-volume qemu_mirror/device-second WRITABLE",
#else
        "C:OpenZFSDevice qemu_mirror/device-volume WRITABLE",
#endif
        SYS_Asynch,TRUE,SYS_Input,input,SYS_Output,output,SYS_Error,errors,
        NP_StackSize,262144,TAG_DONE)!=-1);
    }
#ifdef OZ_TEST_ZVOL_DEVICE_RACE
    struct Message *ready[4]={0};unsigned waiting=0;
    for(unsigned attempt=0;attempt<500&&waiting<4;attempt++){
        struct Message *m;
        while(waiting<4&&(m=GetMsg(gate)))ready[waiting++]=m;
        if(waiting<4)Delay(1);
    }
    DCHECK(waiting==4);
    for(unsigned i=0;i<4;i++)ReplyMsg(ready[i]);
    RemPort(gate);DeleteMsgPort(gate);
#endif
    int opened=0;
    for(unsigned i=0;i<500;i++){
        if(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0)){opened=1;break;}
        Delay(1);
    }
    DCHECK(opened);
#ifdef OZ_TEST_ZVOL_DEVICE_EMPTY
    CloseDevice((struct IORequest *)io);
    for(unsigned slot=0;slot<32;slot++){
        DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",slot,(struct IORequest *)io,0));
        DCHECK(!device_mapping_test(io,slot,32,"",0,0));
        io->io_Command=TD_CHANGENUM;DCHECK(!DoIO((struct IORequest *)io)&&!io->io_Actual);
        io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
        DCHECK(DoIO((struct IORequest *)io)==TDERR_DiskChanged);
        CloseDevice((struct IORequest *)io);
    }
    RUN(10,"resident device already registered","C:OpenZFSDevice","EMPTY");
    RUN(0,"unit=31 dataset= state=absent bytes=0 sector=0","C:OpenZFSDevice","LIST");
    RUN(0,"mode=readonly","C:OpenZFSDevice","REMAP","31","qemu_mirror/device-volume");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",31,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,31,32,"qemu_mirror/device-volume",6ULL<<30,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    for(unsigned byte=0;byte<512;byte++)DCHECK(!data[byte]);
    io->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt);
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
    CloseDevice((struct IORequest *)io);
    RUN(0,"mode=writable-requested","C:OpenZFSDevice","REMAP","0","qemu_mirror/device-volume","WRITABLE");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    bug("RESULT native zvol empty startup PASS absent=32 boundary-slot=31 readonly=1 first-writable=1 duplicate-refused=1\n");
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_RACE
    unsigned rejected=0;
    for(unsigned attempt=0;attempt<500;attempt++){
        rejected=0;
        for(unsigned launch=0;launch<launches;launch++){
            char logname[64],bytes[256]={0};snprintf(logname,sizeof(logname),"T:zvol-device-%u.log",launch);
            BPTR log=Open((CONST_STRPTR)logname,MODE_OLDFILE);
            if(log){LONG n=Read(log,bytes,sizeof(bytes)-1);Close(log);
                if(n>0&&strstr(bytes,"resident device already registered"))rejected++;}
        }
        if(rejected==launches-1)break;
        Delay(1);
    }
    DCHECK(rejected==3);
    struct Library *ExecLockBase=OpenResource((CONST_STRPTR)"execlock.resource");
    DCHECK(ExecLockBase&&ObtainSystemLock(&SysBase->DeviceList,SPINLOCK_MODE_READ,LOCKF_FORBID));
    unsigned registrations=0;
    for(struct Node *node=SysBase->DeviceList.lh_Head;node->ln_Succ;node=node->ln_Succ)
        if(node->ln_Name&&!strcmp(node->ln_Name,"openzfs.device"))registrations++;
    ReleaseSystemLock(&SysBase->DeviceList,LOCKF_FORBID);
    DCHECK(registrations==1);
    bug("RESULT native zvol startup race PASS launches=4 barrier=4 rejected=3 registered=1\n");
#endif
#ifndef OZ_TEST_ZVOL_DEVICE_RACE
    RUN(10,"resident device already registered","C:OpenZFSDevice","qemu_mirror/device-volume");
#endif
    struct IOStdReq *other=(void *)CreateIORequest(port,sizeof(*other));
    unsigned invalid_unit=1;
#ifdef OZ_TEST_ZVOL_DEVICE_EMPTY
    invalid_unit=32;
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_MULTI
    invalid_unit=2;
#endif
    DCHECK(other&&OpenDevice((CONST_STRPTR)"openzfs.device",invalid_unit,(struct IORequest *)other,0)==IOERR_OPENFAIL);
    DeleteIORequest((struct IORequest *)other);
    RUN(0,"HighCyl = 12582911","C:OpenZFSDevice","MOUNTFILE","0","FFS");
    DCHECK(strstr(last_command_output,"FileSystem = L:afs-handler\n")&&
        strstr(last_command_output,"BlocksPerTrack = 1\n")&&
        strstr(last_command_output,"DosType = 0x444f5301\n"));
#ifdef OZ_TEST_ZVOL_DEVICE_MULTI
    struct IOStdReq *second=(void *)CreateIORequest(port,sizeof(*second));DCHECK(second);
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",1,(struct IORequest *)second,0));
    struct DriveGeometry second_geometry={0};
    second->io_Command=TD_GETGEOMETRY;second->io_Data=&second_geometry;second->io_Length=sizeof(second_geometry);
    DCHECK(!DoIO((struct IORequest *)second)&&second_geometry.dg_TotalSectors==0xffffffffUL);
    DCHECK(!device_scsi_capacity_test(second,3ULL<<40));
    RUN(0,"HighCyl = 1610612735","C:OpenZFSDevice","MOUNTFILE","1","FAT");
    DCHECK(strstr(last_command_output,"BlocksPerTrack = 4\n")&&
        strstr(last_command_output,"Unit = 1\n"));
    memset(data,0x69,512);
    second->io_Command=TD_WRITE64;second->io_Data=data;second->io_Length=512;
    second->io_Offset=0xfffffe00UL;second->io_Actual=767;
    DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual==512);
    second->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)second));
    memset(data,0,512);second->io_Command=TD_READ64;second->io_Actual=767;
    DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual==512);
    for(unsigned n=0;n<512;n++)DCHECK(data[n]==0x69);
    bug("RESULT native zvol large capacity PASS bytes=3298534883328 capacity16=1 capacity10-saturated=1 last-sector-io=1\n");
    memset(data,0xa6,131072);
    second->io_Command=CMD_WRITE;second->io_Data=data;second->io_Length=131072;second->io_Offset=512;
    DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual==131072);
    second->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)second));
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/device-second");
    second->io_Command=TD_PROTSTATUS;DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual);
    second->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)second)==TDERR_WriteProt);
#endif
    struct DriveGeometry geometry={0};
    unsigned mapped_count=1;
#ifdef OZ_TEST_ZVOL_DEVICE_EMPTY
    mapped_count=32;
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_MULTI
    mapped_count=2;
    DCHECK(!device_mapping_test(second,1,2,"qemu_mirror/device-second",3ULL<<40,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
#endif
    ULONG mapping_flags=OZ_MAPPING_PRESENT;
#if defined(OZ_TEST_ZVOL_DEVICE_COLD) || defined(OZ_TEST_ZVOL_DEVICE_READONLY)
    mapping_flags|=OZ_MAPPING_READONLY;
#endif
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",6ULL<<30,mapping_flags));
    RUN(0,"unit=0 dataset=qemu_mirror/device-volume state=","C:OpenZFSDevice","LIST");
    io->io_Command=TD_GETGEOMETRY;io->io_Data=&geometry;io->io_Length=sizeof(geometry);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==sizeof(geometry));
    DCHECK(geometry.dg_SectorSize==512&&geometry.dg_TotalSectors==12582912);
    DCHECK(!device_scsi_capacity_test(io,6ULL<<30));
    bug("RESULT native zvol SCSI capacity PASS capacity10=1 capacity16=1 short-replies=1 invalid-rejected=1\n");
    struct NSDeviceQueryResult query={0};
    io->io_Command=NSCMD_DEVICEQUERY;io->io_Data=&query;io->io_Length=sizeof(query);
    DCHECK(!DoIO((struct IORequest *)io)&&query.DeviceType==NSDEVTYPE_TRACKDISK&&query.SupportedCommands);
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
    expected[0]=0x32; /* Writer's last exact-read pattern. */
    goto filesystem_mount;
#endif
#ifdef OZ_TEST_ZVOL_DEVICE_READONLY
    io->io_Command=TD_PROTSTATUS;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual);
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=131072;io->io_Offset=0;
    memset(data,0xa7,131072);DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==131072);
    for(unsigned i=0;i<131072;i++)DCHECK(!data[i]);
    io->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt&&!io->io_Actual);
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
    CloseDevice((struct IORequest *)io);DeleteIORequest((struct IORequest *)io);DeleteMsgPort(port);
    FreeVec(data);FreeVec(expected);
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/device-volume");
    bug("RESULT native zvol device default readonly PASS read=1 write-denied=1 eject=1\n");
    return 0;
#endif
    /* Both low offsets and the high io_Actual word must reach distinct blocks. */
    for(unsigned high=0;high<2;high++){
        memcpy(data,expected,131072);data[0]=(unsigned char)(0x31+high);
        io->io_Command=NSCMD_TD_WRITE64;io->io_Data=data;io->io_Length=131072;
        io->io_Offset=512;io->io_Actual=high;
        DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==131072);
        io->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)io));
    }
    for(unsigned high=0;high<2;high++){
        memset(data,0,131072);expected[0]=(unsigned char)(0x31+high);
        io->io_Command=TD_READ64;io->io_Data=data;io->io_Length=131072;
        io->io_Offset=512;io->io_Actual=high;
        DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==131072&&!memcmp(data,expected,131072));
    }
    io->io_Command=CMD_READ;io->io_Offset=1;io->io_Length=512;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH&&!io->io_Actual);
    io->io_Command=TD_READ64;io->io_Offset=0x80000000UL;io->io_Actual=1;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH&&!io->io_Actual);
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/device-volume");
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",6ULL<<30,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    io->io_Command=TD_PROTSTATUS;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual);
    io->io_Command=CMD_WRITE;io->io_Offset=512;io->io_Length=512;io->io_Data=data;
    DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt&&!io->io_Actual);
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror/device-volume");
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    DCHECK(secBase&&secIsConfigured());
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);DCHECK(secLoginA(reader)==((77UL<<16)|88));
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=512;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_OPENFAIL);
    struct oz_device_mapping denied_mapping;
    memset(&denied_mapping,0xa5,sizeof(denied_mapping));
    io->io_Command=OZCMD_GETMAPPING;io->io_Data=&denied_mapping;io->io_Length=sizeof(denied_mapping);
    DCHECK(DoIO((struct IORequest *)io)==IOERR_OPENFAIL);
    for(unsigned n=0;n<sizeof(denied_mapping);n++)DCHECK(((UBYTE *)&denied_mapping)[n]==0xa5);
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;
    other=(void *)CreateIORequest(port,sizeof(*other));
    DCHECK(other&&OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)other,0)==IOERR_OPENFAIL);
    DeleteIORequest((struct IORequest *)other);
    secLogoutA(quiet);DCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    /* Real outstanding requests; each owns a distinct buffer. Accept a race
     * with completed I/O, but require at least one observed cancellation. */
    struct IOStdReq *async[8]={0};unsigned aborted=0;
    for(unsigned i=0;i<8;i++){
        async[i]=(void *)CreateIORequest(port,sizeof(*io));DCHECK(async[i]);
        DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)async[i],0));
        async[i]->io_Data=AllocVec(131072,MEMF_PUBLIC);DCHECK(async[i]->io_Data);
        async[i]->io_Command=CMD_READ;async[i]->io_Length=131072;async[i]->io_Offset=512;
    }
    for(unsigned i=0;i<8;i++)SendIO((struct IORequest *)async[i]);
    for(unsigned i=8;i>0;i--)(void)AbortIO((struct IORequest *)async[i-1]);
    for(unsigned i=0;i<8;i++){
        LONG e=WaitIO((struct IORequest *)async[i]);
        DCHECK((e==0&&async[i]->io_Actual==131072)||
            (e==IOERR_ABORTED&&async[i]->io_Actual<=131072));
        if(e==IOERR_ABORTED)++aborted;
        FreeVec(async[i]->io_Data);CloseDevice((struct IORequest *)async[i]);
        DeleteIORequest((struct IORequest *)async[i]);
    }
    DCHECK(aborted&&!GetMsg(port));
    bug("Native zvol async requests=8 aborted=%u replies-drained=1\n",aborted);
#ifdef OZ_TEST_ZVOL_DEVICE_FILESYSTEM
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
filesystem_mount: ;
#endif
    /* A bounded 64 MiB DOS view of the disposable zvol, not a host device. */
    RUN(20,NULL,"C:OpenZFSDevice","MOUNTFILE","32","FAT");
    RUN(20,NULL,"C:OpenZFSDevice","MOUNTFILE","0","UNKNOWN");
    RUN(20,NULL,"C:OpenZFSDevice","MOUNTFILE","0","FAT","0");
    RUN(20,NULL,"C:OpenZFSDevice","MOUNTFILE","0","FAT","18446744073709551616");
    RUN(20,"view exceeds mapped capacity","C:OpenZFSDevice","MOUNTFILE","0","FAT","18446744073709551615");
    RUN(0,"HighCyl = 131071","C:OpenZFSDevice","MOUNTFILE","0",
#ifdef OZ_TEST_ZVOL_DEVICE_AFS
        "FFS",
#else
        "FAT",
#endif
        "131072");
    const char *mount=last_command_output;
    DCHECK(strstr(mount,"Device = openzfs.device\nUnit = 0\n")&&
        strstr(mount,"BlocksPerTrack = 1\n")&&strstr(mount,"Activate = 1\n"));
    BPTR spec=Open((CONST_STRPTR)"T:OZBLOCK",MODE_NEWFILE);DCHECK(spec);
    DCHECK(Write(spec,(APTR)mount,strlen(mount))==(LONG)strlen(mount));Close(spec);
    RUN(0,NULL,"C:Mount","T:OZBLOCK");
    bug("RESULT native zvol generated mountfile PASS actual-mount=1 invalid-refused=1 blocks=131072\n");
#ifndef OZ_TEST_ZVOL_DEVICE_COLD
    /* DOS booleans are -1/0. Exec TRUE (1) is not accepted by FAT Inhibit. */
    DCHECK(Inhibit((CONST_STRPTR)"OZBLOCK:",DOSTRUE));
    DCHECK(Format((CONST_STRPTR)"OZBLOCK:",(CONST_STRPTR)"ZvolTest",
#ifdef OZ_TEST_ZVOL_DEVICE_AFS
        ID_FFS_DISK
#else
        ID_MSDOS_DISK
#endif
        ));
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    for(unsigned i=0;i<64;i+=16)bug("Zvol FAT BPB %u: %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x %02x\n",i,data[i],data[i+1],data[i+2],data[i+3],data[i+4],data[i+5],data[i+6],data[i+7],data[i+8],data[i+9],data[i+10],data[i+11],data[i+12],data[i+13],data[i+14],data[i+15]);
    bug("Zvol FAT signature %02x %02x\n",data[510],data[511]);
    /* Format writes metadata; uninhibit asks FAT to discover the new volume. */
    DCHECK(Inhibit((CONST_STRPTR)"OZBLOCK:",FALSE));
    BPTR file=Open((CONST_STRPTR)"OZBLOCK:payload.bin",MODE_NEWFILE);DCHECK(file);
    DCHECK(Write(file,expected,131072)==131072&&Flush(file));Close(file);
    DCHECK(Inhibit((CONST_STRPTR)"OZBLOCK:",DOSTRUE));
    DCHECK(Inhibit((CONST_STRPTR)"OZBLOCK:",FALSE));
#else
    BPTR file;
#endif
    file=Open((CONST_STRPTR)"OZBLOCK:payload.bin",MODE_OLDFILE);DCHECK(file);
    memset(data,0,131072);DCHECK(Read(file,data,131072)==131072&&!memcmp(data,expected,131072));Close(file);
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
    file=Open((CONST_STRPTR)"OZBLOCK:refused.bin",MODE_NEWFILE);
    LONG denied=IoErr();if(file)Close(file);
    DCHECK(!file&&denied==ERROR_DISK_WRITE_PROTECTED);
#ifndef OZ_TEST_ZVOL_DEVICE_AFS
    /* Check FAT admission, not just the device's eventual write rejection. */
    struct InfoData protection_info;
    BPTR protection_lock=Lock((CONST_STRPTR)"OZBLOCK:",ACCESS_READ);
    DCHECK(protection_lock);
    LONG protection_ok=Info(protection_lock,&protection_info);
    UnLock(protection_lock);
    DCHECK(protection_ok&&protection_info.id_DiskState==ID_WRITE_PROTECTED);
    file=Open((CONST_STRPTR)"OZBLOCK:payload.bin",MODE_READWRITE);
    denied=IoErr();if(file)Close(file);
    DCHECK(!file&&denied==ERROR_DISK_WRITE_PROTECTED);
    BPTR refused_dir=CreateDir((CONST_STRPTR)"OZBLOCK:refused-dir");
    denied=IoErr();if(refused_dir)UnLock(refused_dir);
    DCHECK(!refused_dir&&denied==ERROR_DISK_WRITE_PROTECTED);
    DCHECK(!Rename((CONST_STRPTR)"OZBLOCK:payload.bin",
        (CONST_STRPTR)"OZBLOCK:renamed.bin")&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    DCHECK(!DeleteFile((CONST_STRPTR)"OZBLOCK:payload.bin")&&
        IoErr()==ERROR_DISK_WRITE_PROTECTED);
    DCHECK(!SetProtection((CONST_STRPTR)"OZBLOCK:payload.bin",0)&&
        IoErr()==ERROR_DISK_WRITE_PROTECTED);
    file=Open((CONST_STRPTR)"OZBLOCK:payload.bin",MODE_OLDFILE);DCHECK(file);
    memset(data,0,131072);
    DCHECK(Read(file,data,131072)==131072&&!memcmp(data,expected,131072));
    Close(file);
    bug("RESULT native cold FAT protection PASS info=1 mutations-denied=6 exact-reread=1\n");
#endif
#endif
    CloseDevice((struct IORequest *)io);
    RUN(20,"eject refused","C:OpenZFSDevice","EJECT","0");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    file=Open((CONST_STRPTR)"OZBLOCK:payload.bin",MODE_OLDFILE);DCHECK(file);
    memset(data,0,131072);DCHECK(Read(file,data,131072)==131072&&!memcmp(data,expected,131072));Close(file);
    bug("RESULT native zvol mounted eject refusal PASS handler-only=1 data-preserved=1\n");
    DCHECK(Inhibit((CONST_STRPTR)"OZBLOCK:",DOSTRUE));
    /* Exercise the handler protocol directly; older fixture Mount commands
     * do not implement the newer SHUTDOWN switch. No public user volume. */
    struct MsgPort *filesystem=DeviceProc((CONST_STRPTR)"OZBLOCK:");DCHECK(filesystem);
    struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
    struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)"OZBLOCK",LDF_DEVICES);
    int own_entry=entry&&entry->dol_Task==filesystem;
    UnLockDosList(LDF_DEVICES|LDF_READ);DCHECK(own_entry);
    /* Withdraw only the fixture's node before the handler exits. The older
     * AFS fixture leaves dn_Task pointing at its dead port on ACTION_DIE. */
    DCHECK(RemDosEntry(entry));
    DCHECK(DoPkt(filesystem,ACTION_DIE,0,0,0,0,0));
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
    bug("RESULT native cold filesystem on zvol PASS exact-read=1 write-denied=1 shutdown=1\n");
#else
    bug("RESULT native filesystem on zvol PASS format=1 write=1 flush=1 reopen=1 exact-read=1\n");
#endif
#endif
#ifdef OZ_TEST_ZVOL_MAPPING_GUI
    bug("Native live zvol mapping GUI READY: inspect 6 GiB writable and 3 TiB readonly units; close Manager to continue\n");
    DCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    DCHECK(!device_mapping_test(io,0,2,"qemu_mirror/device-volume",6ULL<<30,OZ_MAPPING_PRESENT));
    DCHECK(!device_mapping_test(second,1,2,"qemu_mirror/device-second",3ULL<<40,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    bug("RESULT native live zvol GUI continuation PASS mappings-unchanged=1\n");
#endif
    volatile ULONG change_count=0;
    io->io_Command=CMD_READ;io->io_Data=expected;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    struct Interrupt change={0};
    change.is_Node.ln_Type=NT_INTERRUPT;change.is_Data=(APTR)&change_count;
    change.is_Code=(VOID_FUNC)AROS_ASMSYMNAME(device_change_callback);
    struct IOStdReq *watch=(void *)CreateIORequest(port,sizeof(*watch));DCHECK(watch);
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)watch,0));
    watch->io_Command=TD_ADDCHANGEINT;watch->io_Data=&change;watch->io_Length=sizeof(change);
    SendIO((struct IORequest *)watch);DCHECK(!CheckIO((struct IORequest *)watch)&&!GetMsg(port));
    watch->io_Command=TD_REMCHANGEINT;DCHECK(!DoIO((struct IORequest *)watch)&&!GetMsg(port));
    watch->io_Command=TD_ADDCHANGEINT;SendIO((struct IORequest *)watch);
    DCHECK(!AbortIO((struct IORequest *)watch));
    DCHECK(WaitIO((struct IORequest *)watch)==IOERR_ABORTED&&!GetMsg(port));
    watch->io_Command=TD_ADDCHANGEINT;SendIO((struct IORequest *)watch);
    DCHECK(!CheckIO((struct IORequest *)watch));
#ifdef OZ_TEST_BACKING_MEDIA_CHANGE
    struct oz_device *backing_media=NULL,*error_media=NULL;
    struct oz_device_binding *media_binding=NULL,*rejected_binding=NULL;
    struct oz_device *reopened_media=NULL;
    DCHECK(!oz_device_open("openzfs.device",0,1,&backing_media));
    DCHECK(!oz_device_open("openzfs.device",0,0,&error_media));
    DCHECK(!oz_device_transfer(backing_media,expected,512,0,0));
    DCHECK(!oz_device_bind_writable("/AROS/test-media-change",backing_media,&media_binding));
    DCHECK(!oz_device_bound_open("/AROS/test-media-change",1,&reopened_media));
    oz_device_close(reopened_media);reopened_media=NULL;
    __atomic_store_n(&media_notifications,0,__ATOMIC_RELEASE);
    DCHECK(!oz_device_media_start(media_test_notify));
    DCHECK(oz_device_media_start(NULL)==EBUSY);
#endif
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
    for(unsigned ticks=0;!__atomic_load_n(&change_count,__ATOMIC_SEQ_CST)&&ticks<50;ticks++)Delay(1);
    DCHECK(__atomic_load_n(&change_count,__ATOMIC_SEQ_CST)==1&&!CheckIO((struct IORequest *)watch));
    watch->io_Command=TD_REMCHANGEINT;DCHECK(!DoIO((struct IORequest *)watch)&&!GetMsg(port));
    CloseDevice((struct IORequest *)watch);DeleteIORequest((struct IORequest *)watch);
    bug("RESULT native zvol change notification PASS retained=1 remove=1 abort=1 eject-callback=1 drained=1\n");
    io->io_Command=TD_CHANGESTATE;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==1);
    io->io_Command=CMD_READ;DCHECK(DoIO((struct IORequest *)io)==TDERR_DiskChanged);
#ifdef OZ_TEST_BACKING_MEDIA_CHANGE
    /* No adapter I/O or bound reopen after eject: the worker must observe
     * generation independently. Its join must release snapshot references. */
    for(unsigned tick=0;tick<150&&!__atomic_load_n(&media_notifications,__ATOMIC_ACQUIRE);tick++)Delay(1);
    DCHECK(__atomic_load_n(&media_notifications,__ATOMIC_ACQUIRE)>0);
    DCHECK(oz_device_media_invalid(backing_media));
    DCHECK(!oz_device_media_stop());
    DCHECK(!oz_device_media_stop());
    DCHECK(oz_device_bound_open("/AROS/test-media-change",1,&reopened_media)==ENXIO&&!reopened_media);
    DCHECK(oz_device_transfer(error_media,expected,512,0,0)==ENXIO);
    oz_device_close(error_media);error_media=NULL;
    DCHECK(oz_device_transfer(backing_media,expected,512,0,0)==ENXIO);
    DCHECK(oz_device_transfer(backing_media,expected,512,0,1)==ENXIO);
    DCHECK(oz_device_flush(backing_media)==ENXIO);
    unsigned char inquiry[96];unsigned inquiry_bytes=0;
    DCHECK(oz_device_inquiry(backing_media,inquiry,&inquiry_bytes)==ENXIO);
    DCHECK(oz_device_bound_open("/AROS/test-media-change",1,&reopened_media)==ENXIO&&!reopened_media);
    DCHECK(oz_device_bound_open("/AROS/test-media-change",0,&reopened_media)==ENXIO&&!reopened_media);
    DCHECK(oz_device_bind_writable("/AROS/test-media-changed-again",backing_media,&rejected_binding)==ENXIO&&!rejected_binding);
    oz_device_unbind(media_binding);media_binding=NULL;
    oz_device_close(backing_media);backing_media=NULL;
    DCHECK(!oz_device_media_start(NULL));DCHECK(!oz_device_media_stop());
    bug("RESULT native backing media change PASS read-before=1 eject=1 read-denied=1 write-denied=1 flush-denied=1 inquiry-denied=1 bound-reopen-denied=2 rebind-denied=1 generation-before-io=1 native-error=1 polling=1 joined=1 restart=1 notification=1\n");
#endif
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",0,0));
    RUN(0,"unit=0 dataset=qemu_mirror/device-volume state=absent bytes=0 sector=0","C:OpenZFSDevice","LIST");
    RUN(20,"no usable mapping","C:OpenZFSDevice","MOUNTFILE","0","FFS");
    bug("RESULT native zvol mapping PASS live=1 readonly=1 absent=1 short-buffer=1 list=1\n");
#ifdef OZ_TEST_ZVOL_DEVICE_MULTI
    memset(data,0,131072);
    second->io_Command=CMD_READ;second->io_Data=data;second->io_Length=131072;second->io_Offset=512;
    DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual==131072);
    for(unsigned i=0;i<131072;i++)DCHECK(data[i]==0xa6);
    RUN(1,NULL,"C:zfs","destroy","qemu_mirror/device-second");
    second->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)second));
    second->io_Command=TD_CHANGESTATE;DCHECK(!DoIO((struct IORequest *)second)&&second->io_Actual==1);
    CloseDevice((struct IORequest *)second);DeleteIORequest((struct IORequest *)second);
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/device-second");
    RUN(0,"unit=1 dataset=qemu_mirror/device-second state=absent bytes=0 sector=0","C:OpenZFSDevice","LIST");
    bug("RESULT native zvol multiple units PASS units=2 distinct-geometry=1 independent-data=1 readonly-isolated=1 eject-isolated=1 cleanup=1\n");
#endif
    /* Reuse an ejected resident unit after all original descriptors closed.
     * Default is readonly; a failed open must preserve absent mapping/name. */
#ifdef OZ_TEST_ZVOL_REMAP_GUI
    CloseDevice((struct IORequest *)io);
    bug("Native zvol remap GUI READY: remap unit 0 to qemu_mirror/device-volume; mode=%s; close Manager to verify\n",
#ifdef OZ_TEST_ZVOL_REMAP_GUI_WRITE
        "writable"
#else
        "readonly"
#endif
    );
    DCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
#ifdef OZ_TEST_ZVOL_EJECT_GUI
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",0,0));
    io->io_Command=TD_CHANGENUM;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==3);
    io->io_Command=CMD_READ;DCHECK(DoIO((struct IORequest *)io)==TDERR_DiskChanged);
    CloseDevice((struct IORequest *)io);
    RUN(0,"mode=writable-requested","C:OpenZFSDevice","REMAP","0","qemu_mirror/device-volume","WRITABLE");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    bug("RESULT native zvol GUI eject PASS absent=1 generation=3 io-refused=1 remappable=1\n");
#endif
#ifdef OZ_TEST_ZVOL_MOUNT_GUI
    RUN(0,"HighCyl = 131071","C:OpenZFSDevice","MOUNTFILE","0","FFS","131072");
    BPTR saved=Open((CONST_STRPTR)"T:OZBLOCK",MODE_OLDFILE);DCHECK(saved);
    char saved_text[1024];LONG saved_bytes=Read(saved,saved_text,sizeof(saved_text));Close(saved);
    DCHECK(saved_bytes==(LONG)strlen(last_command_output)&&
        !memcmp(saved_text,last_command_output,saved_bytes));
    BPTR transaction=Lock((CONST_STRPTR)"T:OZBLOCK.txn",SHARED_LOCK);
    if(transaction)UnLock(transaction);
    DCHECK(!transaction);
    bug("RESULT native zvol GUI mountfile PASS saved=1 exact-cli-match=1 transaction-clean=1\n");
#endif
#ifdef OZ_TEST_ZVOL_REMAP_GUI_WRITE
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",6ULL<<30,OZ_MAPPING_PRESENT));
    /* Distinct bytes must reach the backing through the GUI-created handle. */
    memset(expected,0x5c,512);
    io->io_Command=CMD_WRITE;io->io_Data=expected;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    io->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)io));
#else
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",
        6ULL<<30,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
#endif
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
#ifdef OZ_TEST_ZVOL_REMAP_GUI_WRITE
    bug("RESULT native zvol GUI writable remap PASS write=1 flush=1 exact-read=1 eject=1\n");
#else
    bug("RESULT native zvol GUI remap PASS readonly=1 exact-read=1 eject=1\n");
#endif
#endif
    struct oz_device_remap replacement={OZ_REMAP_VERSION,sizeof(replacement),0,0,{0}};
    strcpy(replacement.dataset,"qemu_mirror/device-volume");
    io->io_Command=TD_CHANGENUM;DCHECK(!DoIO((struct IORequest *)io));
    ULONG generation=io->io_Actual;
#ifdef OZ_TEST_ZVOL_EJECT_GUI
    DCHECK(generation==5);
#elif defined(OZ_TEST_ZVOL_REMAP_GUI)
    DCHECK(generation==3);
#elif defined(OZ_TEST_ZVOL_DEVICE_EMPTY)
    DCHECK(generation==2);
#else
    DCHECK(generation==1);
#endif
    io->io_Command=OZCMD_SETMAPPING;io->io_Data=&replacement;io->io_Length=sizeof(replacement);
    replacement.version=0;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH);
    replacement.version=OZ_REMAP_VERSION;
    io->io_Length=sizeof(replacement)-1;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH);
    io->io_Length=sizeof(replacement);
    DCHECK(DoIO((struct IORequest *)io)==TDERR_DiskChanged);
    replacement.expected_change=generation;
    struct IOStdReq *remap_peer=(void *)CreateIORequest(port,sizeof(*remap_peer));DCHECK(remap_peer);
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)remap_peer,0));
    DCHECK(DoIO((struct IORequest *)io)==IOERR_UNITBUSY);
    CloseDevice((struct IORequest *)remap_peer);DeleteIORequest((struct IORequest *)remap_peer);
    strcpy(replacement.dataset,"qemu_mirror/missing-zvol");
    DCHECK(DoIO((struct IORequest *)io)!=0);
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",0,0));
    strcpy(replacement.dataset,"qemu_mirror/device-volume");
    io->io_Command=OZCMD_SETMAPPING;io->io_Data=&replacement;io->io_Length=sizeof(replacement);
    DCHECK(!DoIO((struct IORequest *)io)&&!io->io_Actual);
    DCHECK(DoIO((struct IORequest *)io)==IOERR_UNITBUSY);
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",
        6ULL<<30,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    io->io_Command=TD_CHANGENUM;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==generation+1);
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    io->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt);
    DCHECK(!device_safe_eject_test(io,port));
    io->io_Command=TD_CHANGENUM;DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==generation+2);
    bug("RESULT native zvol remap PASS stale=1 busy=1 failed-open=1 readonly=1 exact-read=1 session-reopen=1 generation=1\n");
    CloseDevice((struct IORequest *)io);
    RUN(0,"mode=readonly","C:OpenZFSDevice","REMAP","0","qemu_mirror/device-volume");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",
        6ULL<<30,OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    RUN(20,"eject refused","C:OpenZFSDevice","EJECT","0");
    CloseDevice((struct IORequest *)io);
    RUN(0,"ejected unit=0","C:OpenZFSDevice","EJECT","0");
    RUN(20,"eject refused","C:OpenZFSDevice","EJECT","0");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",0,0));
    bug("RESULT native zvol eject CLI PASS busy-refused=1 released=1 absent-refused=1\n");
#ifndef OZ_TEST_ZVOL_DEVICE_COLD
    CloseDevice((struct IORequest *)io);
    RUN(0,"mode=writable-requested","C:OpenZFSDevice","REMAP","0","qemu_mirror/device-volume","WRITABLE");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/device-volume",6ULL<<30,OZ_MAPPING_PRESENT));
    io->io_Command=CMD_WRITE;io->io_Data=expected;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    /* EJECT itself must flush; do not precede this with CMD_UPDATE. */
    CloseDevice((struct IORequest *)io);
    RUN(0,"ejected unit=0","C:OpenZFSDevice","EJECT","0");
    RUN(0,"mode=readonly","C:OpenZFSDevice","REMAP","0","qemu_mirror/device-volume");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    memset(data,0,512);io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
    bug("RESULT native zvol writable eject PASS write=1 eject=1 readonly-remap=1 exact-read=1\n");
    RUN(0,NULL,"C:zfs","create","-s","-V","16M","-b","8192","qemu_mirror/remap-target");
    CloseDevice((struct IORequest *)io);
    RUN(0,"dataset=qemu_mirror/remap-target","C:OpenZFSDevice","REMAP","0",
        "qemu_mirror/remap-target","WRITABLE");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/remap-target",16ULL<<20,OZ_MAPPING_PRESENT));
    io->io_Command=CMD_WRITE;io->io_Data=expected;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    io->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)io));
    memset(data,0,512);io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    /* No filesystem owns this disposable zvol: exercise live size changes
     * through upstream CLI while the resident raw-device handle stays open. */
#ifdef OZ_TEST_ZVOL_UNMAP
    DCHECK(!device_unmap_test(io,16ULL<<20,0));
#ifndef OZ_TEST_ZVOL_UNMAP_UNSUPPORTED
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/remap-target");
    DCHECK(!device_unmap_test(io,16ULL<<20,1));
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror/remap-target");
    bug("RESULT native zvol UNMAP readonly PASS sense-write-protected=1 no-transfer=1\n");
#endif
#endif
#ifdef OZ_TEST_ZVOL_RESIZE_API
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=0",OZ_CONFIRMED,EINVAL,NULL));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=1537",OZ_CONFIRMED,EINVAL,NULL));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=garbage",OZ_CONFIRMED,EINVAL,NULL));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=18446744073709551616",OZ_CONFIRMED,ERANGE,NULL));
    DCHECK(!device_resize_api(42,"qemu_mirror/remap-target","volsize",OZ_CONFIRMED,EINVAL,NULL));
    DCHECK(!device_resize_api(41,"qemu_mirror","volsize=32M",OZ_CONFIRMED,EINVAL,NULL));
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/remap-target");
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=32M",OZ_CONFIRMED,EROFS,NULL));
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror/remap-target");
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        DCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);DCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=device_resize_api(41,"qemu_mirror/remap-target","volsize=32M",OZ_CONFIRMED,EPERM,NULL);
        secLogoutA(quiet);DCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
        DCHECK(!denied);
    }
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/remap-target",16ULL<<20,OZ_MAPPING_PRESENT));
#ifdef OZ_TEST_ZVOL_RESIZE_GUI
    bug("Native zvol resize GUI READY: set qemu_mirror/remap-target volsize=32M, then close Manager\n");
    DCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=32M",OZ_CONFIRMED,0,NULL));
#endif
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","volsize",0,0,"33554432"));
    bug("RESULT native Manager zvol resize PASS malformed=4 inherit-denied=1 filesystem-denied=1 readonly-denied=1 unauthorized-denied=1 changed=1 exact-get=1\n");
#else
    RUN(0,NULL,"C:zfs","set","volsize=32M","qemu_mirror/remap-target");
#endif
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/remap-target",32ULL<<20,OZ_MAPPING_PRESENT));
    DCHECK(!device_scsi_capacity_test(io,32ULL<<20));
    struct DriveGeometry resized={0};
    io->io_Command=TD_GETGEOMETRY;io->io_Data=&resized;io->io_Length=sizeof(resized);
    DCHECK(!DoIO((struct IORequest *)io)&&resized.dg_SectorSize==512&&resized.dg_TotalSectors==65536);
    io->io_Command=CMD_WRITE;io->io_Data=expected;io->io_Length=512;io->io_Offset=24UL<<20;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    io->io_Command=CMD_UPDATE;DCHECK(!DoIO((struct IORequest *)io));
    memset(data,0,512);io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=24UL<<20;
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    RUN(0,NULL,"C:zfs","set","volsize=16M","qemu_mirror/remap-target");
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/remap-target",16ULL<<20,OZ_MAPPING_PRESENT));
    DCHECK(!device_scsi_capacity_test(io,16ULL<<20));
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=24UL<<20;
    DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH&&!io->io_Actual);
    io->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)io)==IOERR_BADLENGTH&&!io->io_Actual);
    io->io_Command=CMD_READ;io->io_Offset=0;memset(data,0,512);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    RUN(0,NULL,"C:zfs","set","volsize=32M","qemu_mirror/remap-target");
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=24UL<<20;
    memset(data,0xa5,512);DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    for(unsigned i=0;i<512;i++)DCHECK(!((UBYTE *)data)[i]);
    bug("RESULT native zvol live resize PASS growth=1 capacity=1 geometry=1 new-range-io=1 shrink-bounds=1 prefix-preserved=1 regrown-zero=1\n");
#ifdef OZ_TEST_ZVOL_READONLY_POLICY
    struct oz_ticket readonly_session={0};
    struct {struct oz_zvol_request q;unsigned char bytes[512];} readonly_packet;
#define RSET(op,desc) do{memset(&readonly_packet,0,sizeof(readonly_packet));readonly_packet.q.session=readonly_session;readonly_packet.q.version=OZ_ZVOL_VERSION;readonly_packet.q.operation=op;readonly_packet.q.descriptor=desc;}while(0)
#define RCALL(wanted) do{int e=oz_zvol_request(OZ_ADMISSION_PORT,&readonly_packet,sizeof(readonly_packet.q)+readonly_packet.q.count);DCHECK(!e&&readonly_packet.q.engine_error==(wanted));}while(0)
    DCHECK(!oz_session_open(OZ_ADMISSION_PORT,&readonly_session));
    RSET(OZ_ZVOL_OPEN,-1);readonly_packet.q.flags=OZ_ZVOL_WRITABLE;
    strcpy(readonly_packet.q.name,"qemu_mirror/remap-target");RCALL(0);
    LONG readonly_fd=readonly_packet.q.descriptor;DCHECK(readonly_fd>0);
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror");
    RUN(0,NULL,"C:zfs","inherit","readonly","qemu_mirror/remap-target");
    RUN(0,"on","C:zfs","get","-Hp","-o","value","readonly","qemu_mirror/remap-target");
    /* No INFO/status request before WRITE: the backend must enforce policy. */
    RSET(OZ_ZVOL_WRITE,readonly_fd);readonly_packet.q.count=512;
    memcpy(readonly_packet.bytes,expected,512);RCALL(EROFS);
    DCHECK(!readonly_packet.q.completed);
    RSET(OZ_ZVOL_OPEN,-1);readonly_packet.q.flags=OZ_ZVOL_WRITABLE;
    strcpy(readonly_packet.q.name,"qemu_mirror/remap-target");RCALL(EROFS);
    io->io_Command=TD_PROTSTATUS;int protection_error=DoIO((struct IORequest *)io);
    ULONG protection=io->io_Actual;
    io->io_Command=CMD_WRITE;io->io_Data=expected;io->io_Length=512;io->io_Offset=0;
    int write_error=DoIO((struct IORequest *)io);ULONG wrote=io->io_Actual;
    bug("Native inherited zvol readonly observed protection-error=%d protected=%lu write-error=%d wrote=%lu\n",
        protection_error,(unsigned long)protection,write_error,(unsigned long)wrote);
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror");
    DCHECK(!protection_error&&protection&&write_error==TDERR_WriteProt&&!wrote);
    RUN(0,"off","C:zfs","get","-Hp","-o","value","readonly","qemu_mirror/remap-target");
    RSET(OZ_ZVOL_WRITE,readonly_fd);readonly_packet.q.count=512;
    memcpy(readonly_packet.bytes,expected,512);RCALL(0);DCHECK(readonly_packet.q.completed==512);
    RSET(OZ_ZVOL_FLUSH,readonly_fd);RCALL(0);
    io->io_Command=TD_PROTSTATUS;DCHECK(!DoIO((struct IORequest *)io)&&!io->io_Actual);
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;memset(data,0,512);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        DCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);DCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=device_resize_api(41,"qemu_mirror/remap-target","readonly=on",OZ_CONFIRMED,EPERM,NULL);
        secLogoutA(quiet);DCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
        DCHECK(!denied);
    }
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","readonly",0,0,"off"));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","readonly=bogus",OZ_CONFIRMED,EINVAL,NULL));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","atime=on",OZ_CONFIRMED,ENOTSUP,NULL));
#ifdef OZ_TEST_ZVOL_READONLY_GUI
    bug("Native zvol readonly GUI READY: set qemu_mirror/remap-target readonly=on, then close Manager\n");
    DCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","readonly=on",OZ_CONFIRMED,0,NULL));
#endif
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","readonly",0,0,"on"));
    RSET(OZ_ZVOL_WRITE,readonly_fd);readonly_packet.q.count=512;
    memcpy(readonly_packet.bytes,expected,512);RCALL(EROFS);DCHECK(!readonly_packet.q.completed);
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","readonly=off",OZ_CONFIRMED,0,NULL));
    RSET(OZ_ZVOL_WRITE,readonly_fd);readonly_packet.q.count=512;
    memcpy(readonly_packet.bytes,expected,512);RCALL(0);DCHECK(readonly_packet.q.completed==512);
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror");
    DCHECK(!device_resize_api(42,"qemu_mirror/remap-target","readonly",OZ_CONFIRMED,0,NULL));
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","readonly",0,0,"on"));
    RSET(OZ_ZVOL_WRITE,readonly_fd);readonly_packet.q.count=512;
    memcpy(readonly_packet.bytes,expected,512);RCALL(EROFS);DCHECK(!readonly_packet.q.completed);
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror");
    io->io_Command=TD_PROTSTATUS;DCHECK(!DoIO((struct IORequest *)io)&&!io->io_Actual);
    DCHECK(!oz_session_close(OZ_ADMISSION_PORT,&readonly_session));
    bug("RESULT native zvol inherited readonly PASS property=on device-protected=1 write-denied=1 restored=1\n");
    bug("RESULT native zvol readonly boundaries PASS direct-write-denied=1 writable-open-denied=1 inherited-off-write=1 readback=1\n");
    bug("RESULT native Manager zvol readonly PASS set=1 get=1 inherit=1 write-policy=1 malformed-denied=1 filesystem-only-denied=1 unauthorized-denied=1\n");
#ifdef OZ_TEST_ZVOL_READONLY_GUI
    bug("RESULT native Manager zvol readonly GUI PASS actual-manager=1 independent-property=1 independent-write-refusal=1\n");
#endif
#undef RCALL
#undef RSET
#endif
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
#ifdef OZ_TEST_ZVOL_RESIZE_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native zvol resize retained PASS bytes=33554432 prefix=512 truncated-offset=25165824\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/remap-target");
#endif
    bug("RESULT native zvol new mapping PASS changed-dataset=1 geometry=1 write-flush-read=1 cleanup=1\n");
#endif
#ifdef OZ_TEST_ZVOL_RESIZE_COLD
    CloseDevice((struct IORequest *)io);
    RUN(0,"dataset=qemu_mirror/remap-target","C:OpenZFSDevice","REMAP","0","qemu_mirror/remap-target");
    DCHECK(!OpenDevice((CONST_STRPTR)"openzfs.device",0,(struct IORequest *)io,0));
    DCHECK(!device_mapping_test(io,0,mapped_count,"qemu_mirror/remap-target",32ULL<<20,
        OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY));
    DCHECK(!device_scsi_capacity_test(io,32ULL<<20));
#ifdef OZ_TEST_ZVOL_UNMAP
    DCHECK(!device_unmap_test(io,32ULL<<20,1));
    bug("RESULT native zvol UNMAP cold PASS exact-hole=1 exact-neighbors=1 readonly-denied=1 capabilities=1\n");
#endif
    io->io_Command=CMD_READ;io->io_Data=data;io->io_Length=512;io->io_Offset=0;memset(data,0,512);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512&&!memcmp(data,expected,512));
    io->io_Offset=24UL<<20;memset(data,0xa5,512);
    DCHECK(!DoIO((struct IORequest *)io)&&io->io_Actual==512);
    for(unsigned i=0;i<512;i++)DCHECK(!((UBYTE *)data)[i]);
    io->io_Command=CMD_WRITE;DCHECK(DoIO((struct IORequest *)io)==TDERR_WriteProt&&!io->io_Actual);
#ifdef OZ_TEST_ZVOL_RESIZE_API
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","volsize",0,0,"33554432"));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","volsize=16M",OZ_CONFIRMED,EROFS,NULL));
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","volsize",0,0,"33554432"));
    bug("RESULT native Manager zvol resize cold PASS exact-get=1 readonly-service-denied=1 unchanged=1\n");
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","readonly",0,0,"off"));
    DCHECK(!device_resize_api(41,"qemu_mirror/remap-target","readonly=on",OZ_CONFIRMED,EROFS,NULL));
    DCHECK(!device_resize_api(42,"qemu_mirror/remap-target","readonly",OZ_CONFIRMED,EROFS,NULL));
    DCHECK(!device_resize_api(40,"qemu_mirror/remap-target","readonly",0,0,"off"));
    bug("RESULT native Manager zvol readonly cold PASS set-denied=1 inherit-denied=1 property-unchanged=1\n");
#endif
    io->io_Command=TD_EJECT;DCHECK(!DoIO((struct IORequest *)io));
    bug("RESULT native zvol resize cold PASS capacity=33554432 prefix=512 truncated-zero=1 write-denied=1\n");
#endif
    bug("RESULT native zvol remap CLI PASS default-readonly=1 mapping-query=1 cleanup=1\n");
    bug("Zvol test cleanup close-device\n");CloseDevice((struct IORequest *)io);
    bug("Zvol test cleanup delete-request\n");DeleteIORequest((struct IORequest *)io);
    bug("Zvol test cleanup delete-port\n");DeleteMsgPort(port);
    bug("Zvol test cleanup free-buffers\n");FreeVec(data);FreeVec(expected);
    bug("Zvol test cleanup destroy-volume\n");
    trace_run=1;
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
    bug("RESULT native cold zvol device PASS readonly=1 eject=1\n");
#elif defined(OZ_TEST_ZVOL_DEVICE_KEEP)
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native zvol retained PASS dataset=qemu_mirror/device-volume\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/device-volume");
#endif
    trace_run=0;
#ifndef OZ_TEST_ZVOL_DEVICE_COLD
    bug("RESULT native zvol device PASS exec=1 geometry=1 query=1 chunked=1 offset64=1 bounds=1 readonly=1 eject=1 unauthorized=1 abort=1\n");
#endif
#undef DCHECK
    return 0;
}
