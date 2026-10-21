/* Read-only diagnostic for the private two-disk QEMU fixture. */
#include <proto/exec.h>
#include <exec/io.h>
#include <devices/trackdisk.h>
#include <devices/scsidisk.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void note(const char *s) { while (*s) RawPutChar(*s++); RawPutChar('\n'); }
static uint64_t be(const unsigned char *p, unsigned n)
{ uint64_t v=0; while(n--) v=(v<<8)|*p++; return v; }

static int inquiry_cases(struct IOStdReq *io,unsigned unit)
{
    unsigned failures=0;
    for(unsigned test=0;test<14;test++) {
        UWORD data[64];unsigned char cmd[6]={0x12,0,0,0,96,0},saved[6];
        struct SCSICmd s={0};char line[180];unsigned expected=36;int reject=0;
        memset(data,0xa5,sizeof(data));
        s.scsi_Data=data;s.scsi_Length=96;s.scsi_Command=cmd;
        s.scsi_CmdLength=6;s.scsi_Flags=SCSIF_READ;
        switch(test) {
        case 1:cmd[4]=5;expected=5;break;
        case 2:cmd[4]=0;expected=0;s.scsi_Data=NULL;s.scsi_Length=0;break;
        case 3:s.scsi_CmdLength=5;reject=1;break;
        case 4:s.scsi_Flags=0;reject=1;break;
        case 5:s.scsi_Length=35;reject=1;break;
        case 6:s.scsi_Data=NULL;reject=1;break;
        case 7:cmd[1]=1;expected=6;break;
        case 8:cmd[1]=1;cmd[2]=0x80;expected=0;break;
        case 9:cmd[1]=1;cmd[2]=0x83;reject=1;break;
        case 10:cmd[2]=0x80;reject=1;break;
        case 11:cmd[5]=1;reject=1;break;
        case 12:cmd[3]=1;cmd[4]=0;break;
        case 13:cmd[1]=2;reject=1;break;
        }
        memcpy(saved,cmd,sizeof(cmd));s.scsi_Actual=123;s.scsi_CmdActual=123;
        io->io_Command=HD_SCSICMD;io->io_Data=&s;io->io_Length=sizeof(s);
        io->io_Error=0;io->io_Actual=0;io->io_Offset=0;
        int error=DoIO((struct IORequest *)io),ok;
        unsigned char *bytes=(unsigned char *)data;
        if(test==8)expected=4+bytes[3];
        if(reject) {expected=0;ok=error!=0&&!s.scsi_Actual&&!s.scsi_CmdActual;}
        else ok=!error&&!s.scsi_Status&&s.scsi_CmdActual==6&&s.scsi_Actual==expected;
        if(!reject&&test==8)ok=ok&&expected>4&&expected<=24&&bytes[0]==0&&bytes[1]==0x80&&bytes[2]==0;
        if(!reject&&test==7)ok=ok&&!memcmp(bytes,"\0\0\0\2\0\200",6);
        if(!reject&&(test==0||test==12))ok=ok&&bytes[0]==0&&bytes[1]==0&&bytes[4]==31&&
            !memcmp(bytes+8,"SATA    ",8)&&!memcmp(bytes+16,"QEMU HARDDISK",13);
        ok=ok&&!memcmp(cmd,saved,sizeof(cmd));
        for(unsigned i=expected;i<sizeof(data);i++)if(bytes[i]!=0xa5)ok=0;
        snprintf(line,sizeof(line),"INQUIRY unit=%u case=%u error=%d actual=%lu cmdactual=%u %s",
            unit,test,error,(unsigned long)s.scsi_Actual,s.scsi_CmdActual,ok?"PASS":"FAIL");note(line);
        failures+=!ok;
    }
    return failures;
}

static int boundaries(struct IOStdReq *io, unsigned unit)
{
    unsigned failures=0;
    for(unsigned test=0;test<10;test++) {
        UWORD data[32]; unsigned char cmd[16]={0x9e,0x10}, saved[16];
        struct SCSICmd s={0}; char line[160]; unsigned expected=0;
        memset(data,0xa5,sizeof(data)); cmd[13]=32;
        s.scsi_Command=cmd; s.scsi_CmdLength=16;
        s.scsi_Data=data; s.scsi_Length=32; s.scsi_Flags=SCSIF_READ;
        switch(test) {
        case 0: s.scsi_CmdLength=15; break;
        case 1: s.scsi_Length=31; break;
        case 2: cmd[1]=0x11; break;
        case 3: cmd[14]=1; break;
        case 4: cmd[9]=1; break;
        case 5: s.scsi_Flags=0; break;
        case 6: s.scsi_Data=NULL; break;
        case 7: cmd[13]=0; s.scsi_Data=NULL; s.scsi_Length=0; break;
        case 8: cmd[13]=12; s.scsi_Length=12; expected=12; break;
        case 9: cmd[13]=64; s.scsi_Length=64; expected=32; break;
        }
        memcpy(saved,cmd,sizeof(cmd));
        s.scsi_Actual=123; s.scsi_CmdActual=123;
        io->io_Command=HD_SCSICMD; io->io_Data=&s; io->io_Length=sizeof(s);
        io->io_Error=0; io->io_Actual=0; io->io_Offset=0;
        int error=DoIO((struct IORequest *)io), ok=1;
        if(test<7) ok=error!=0 && !s.scsi_Actual && !s.scsi_CmdActual;
        else ok=!error && !s.scsi_Status && s.scsi_Actual==expected && s.scsi_CmdActual==16;
        ok=ok && !memcmp(saved,cmd,sizeof(cmd));
        for(unsigned i=expected;i<sizeof(data);i++)
            if(((unsigned char *)data)[i]!=0xa5) ok=0;
        snprintf(line,sizeof(line),"BOUNDARY unit=%u case=%u error=%d actual=%lu cmdactual=%u %s",
            unit,test,error,(unsigned long)s.scsi_Actual,s.scsi_CmdActual,ok?"PASS":"FAIL");
        note(line); failures+=!ok;
    }
    return failures;
}

static int capacity(struct IOStdReq *io, unsigned unit, unsigned bits, uint64_t sectors)
{
    unsigned char command[16]={0}, saved[16], sense[32]={0};
    UWORD data[32]; struct SCSICmd s={0}; char line[256];
    unsigned length=bits==10?8:32, cmdlen=bits==10?10:16;
    memset(data,0xa5,sizeof(data));
    command[0]=bits==10?0x25:0x9e;
    if(bits==16) { command[1]=0x10; command[13]=32; }
    memcpy(saved,command,sizeof(command));
    s.scsi_Command=command; s.scsi_CmdLength=cmdlen;
    s.scsi_Data=data; s.scsi_Length=length;
    s.scsi_Flags=SCSIF_READ|SCSIF_AUTOSENSE;
    s.scsi_SenseData=sense; s.scsi_SenseLength=sizeof(sense);
    io->io_Command=HD_SCSICMD; io->io_Data=&s; io->io_Length=sizeof(s);
    io->io_Actual=0; io->io_Error=0; io->io_Offset=0;
    int error=DoIO((struct IORequest *)io);
    unsigned char *bytes=(unsigned char *)data;
    uint64_t last=be(bytes,bits==10?4:8);
    unsigned sector=(unsigned)be(bytes+(bits==10?4:8),4);
    uint64_t expected=sectors-1;
    if(bits==10 && expected>UINT32_MAX) expected=UINT32_MAX;
    int intact=!memcmp(command,saved,sizeof(command)), guard=1;
    for(unsigned i=length;i<sizeof(data);i++) if(bytes[i]!=0xa5) guard=0;
    int ok=!error && !s.scsi_Status && s.scsi_Actual==length &&
        s.scsi_CmdActual==cmdlen && io->io_Actual==sizeof(s) && intact && guard &&
        last==expected && sector==512;
    snprintf(line,sizeof(line),"CAPACITY unit=%u bits=%u error=%d status=%u actual=%lu cmdactual=%u unchanged=%d guard=%d last=%llu sector=%u %s",
        unit,bits,error,s.scsi_Status,(unsigned long)s.scsi_Actual,s.scsi_CmdActual,
        intact,guard,(unsigned long long)last,sector,ok?"PASS":"FAIL");
    note(line); return !ok;
}

int main(void)
{
    unsigned failures=0; char line[160];
    note("BEGIN read-only AHCI capacity diagnostic");
    for(unsigned unit=0;unit<2;unit++) {
        uint64_t sectors=unit?UINT64_C(6442450944):UINT64_C(1048576);
        struct MsgPort *port=CreateMsgPort();
        struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
        if(!io || OpenDevice((CONST_STRPTR)"ahci.device",unit,(struct IORequest *)io,0)) {
            note("FAIL opening private AHCI fixture"); failures++;
        } else {
            struct DriveGeometry geometry={0};
            io->io_Command=TD_GETGEOMETRY; io->io_Data=&geometry; io->io_Length=sizeof(geometry);
            int error=DoIO((struct IORequest *)io);
            int ok=!error && geometry.dg_SectorSize==512 &&
                geometry.dg_TotalSectors==(unit?UINT32_MAX:sectors);
            snprintf(line,sizeof(line),"GEOMETRY unit=%u error=%d sectors=%lu sector=%lu %s",unit,error,
                (unsigned long)geometry.dg_TotalSectors,(unsigned long)geometry.dg_SectorSize,ok?"PASS":"FAIL");
            note(line); failures+=!ok;
            failures+=capacity(io,unit,10,sectors);
            failures+=capacity(io,unit,16,sectors);
            failures+=boundaries(io,unit);
            failures+=inquiry_cases(io,unit);
            CloseDevice((struct IORequest *)io);
        }
        if(io) DeleteIORequest((struct IORequest *)io);
        if(port) DeleteMsgPort(port);
    }
    note(failures?"RESULT AHCI capacity FAIL":"RESULT AHCI capacity PASS");
    return failures?20:0;
}
