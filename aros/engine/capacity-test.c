/* SPDX-License-Identifier: CDDL-1.0 */
/* Read-only capacity and high-LBA I/O on fresh sparse QEMU fixtures. */
#include "device-io.h"
#include "partitions.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <exec/io.h>
#include <devices/scsidisk.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef OZ_CAPACITY_TIB
#define OZ_CAPACITY_TIB 3
#endif
#if OZ_CAPACITY_TIB != 3 && OZ_CAPACITY_TIB != 50
#error Unsupported disposable capacity fixture
#endif
static int capacity_commands(void)
{
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    int opened=0,error=0;
    if(!io){error=ENOMEM;goto done;}
    if(OpenDevice((CONST_STRPTR)"ata.device",0,(struct IORequest *)io,0)){
        error=EIO;goto done;
    }
    opened=1;
    for(unsigned test=0;test<10;test++){
        unsigned char command[16]={0x9e,0x10},saved[16];
        UWORD data[32];struct SCSICmd s={0};unsigned expected=0;
        memset(data,0xa5,sizeof(data));command[13]=32;
        s.scsi_Command=command;s.scsi_CmdLength=16;
        s.scsi_Data=data;s.scsi_Length=32;s.scsi_Flags=SCSIF_READ;
        switch(test){
        case 0:s.scsi_CmdLength=15;break;
        case 1:s.scsi_Length=31;break;
        case 2:command[1]=0x11;break;
        case 3:command[14]=1;break;
        case 4:command[9]=1;break;
        case 5:s.scsi_Flags=0;break;
        case 6:s.scsi_Data=NULL;break;
        case 7:command[13]=0;s.scsi_Data=NULL;s.scsi_Length=0;break;
        case 8:command[13]=12;s.scsi_Length=12;expected=12;break;
        case 9:command[13]=64;s.scsi_Length=64;expected=32;break;
        }
        memcpy(saved,command,sizeof(saved));
        s.scsi_Actual=123;s.scsi_CmdActual=123;
        io->io_Command=HD_SCSICMD;io->io_Data=&s;io->io_Length=sizeof(s);
        io->io_Error=0;io->io_Actual=0;io->io_Offset=0;
        int rc=DoIO((struct IORequest *)io);
        int ok=test<7?(rc!=0&&!s.scsi_Actual&&!s.scsi_CmdActual):
            (!rc&&!s.scsi_Status&&s.scsi_Actual==expected&&s.scsi_CmdActual==16);
        ok=ok&&!memcmp(command,saved,sizeof(saved));
        for(unsigned i=expected;i<sizeof(data);i++)
            if(((unsigned char *)data)[i]!=0xa5)ok=0;
        char line[140];snprintf(line,sizeof(line),
            "Native capacity command case=%u rc=%d actual=%lu command=%u %s\n",
            test,rc,(unsigned long)s.scsi_Actual,s.scsi_CmdActual,ok?"PASS":"FAIL");
        for(char *p=line;*p;p++)RawPutChar(*p);
        if(!ok)error=EIO;
    }
done:
    if(opened)CloseDevice((struct IORequest *)io);
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    return error;
}
int main(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-capacity-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    struct oz_device *disk=NULL;
    const uint64_t size=UINT64_C(1)*OZ_CAPACITY_TIB*1024*1024*1024*1024;
    int rc=oz_device_open("ata.device",0,0,&disk),line=0;
#define CHECK(x) do{if(!(x)){line=__LINE__;goto done;}}while(0)
    CHECK(!rc&&disk);
    CHECK(oz_device_size(disk)==size&&oz_device_sector(disk)==512);
    unsigned char bytes[512];memset(bytes,0xaa,sizeof(bytes));
    CHECK(!oz_device_transfer(disk,bytes,sizeof(bytes),size-sizeof(bytes),0));
    for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==0x5a);
    struct oz_partition_catalog catalog;
    CHECK(!oz_partition_catalog_read("ata.device",1,&catalog));
    CHECK(catalog.disk_sectors==(UINT64_C(3)*1024*1024*1024*1024)/512&&catalog.sector_size==512&&catalog.count==1);
    CHECK(catalog.entries[0].first_sector==UINT64_C(4294969344));
    CHECK(catalog.entries[0].last_sector==UINT64_C(6442446847));
    CHECK(!strcmp(catalog.entries[0].name,"OpenZFS-large"));
    CHECK(oz_device_transfer(disk,bytes,sizeof(bytes),size,0)==EINVAL);
    CHECK(oz_device_transfer(disk,bytes,sizeof(bytes),size-sizeof(bytes),1)==EROFS);
    oz_device_close(disk);disk=NULL;
    CHECK(!oz_device_open_range("ata.device",0,0,size-4096,4096,&disk));
    CHECK(oz_device_size(disk)==4096);
    CHECK(!oz_device_transfer(disk,bytes,sizeof(bytes),4096-sizeof(bytes),0));
    for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==0x5a);
    CHECK(!capacity_commands());
done:
    oz_device_close(disk);
    char text[180];snprintf(text,sizeof(text),
        "RESULT native capacity %s size=%llu last-sector=1 range=1 bounds=1 readonly=1 large-gpt=1 rc=%d line=%d\n",
        line?"FAIL":"PASS",(unsigned long long)size,rc,line);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return line?20:0;
}
