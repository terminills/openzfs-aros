/* SPDX-License-Identifier: CDDL-1.0 */
/* Private SFS image; optional OpenZFS members are disposable qcow2 overlays. */
#include "startup-settings.h"
#include "admission-api.h"
#include "support-api.h"
#include "../engine/hostid-record.h"
#include "hostid-settings.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <proto/kernel.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include <dos/dostags.h>
#include <dos/var.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
APTR KernelBase;
#if defined(OZ_STARTUP_MMP) && !defined(OZ_STARTUP_HOSTID_EMPTY)
/* Native x86_64 little-endian file-vdev observation. The upstream layout in
 * uberblock_impl.h has timestamp at byte 32, MMP magic/delay/config at
 * 176/184/192; vdev_impl.h places the 128KiB ring in each 256KiB label.
 * Read actual label bytes, not a successful property command as evidence. */
static int mmp_sample(uint64_t *stamp,uint64_t *sequence)
{
    BPTR f=Open((CONST_STRPTR)"RAM:default-cache.vdev",MODE_OLDFILE);
    if(!f)return 0;
    int found=0;*stamp=0;*sequence=0;
    for(unsigned label=0;label<4;label++){
        LONG base=label<2?(LONG)(label*262144):(LONG)(128*1024*1024-(4-label)*262144);
        for(unsigned slot=0;slot<128;slot++){
            uint64_t words[25];
            if(Seek(f,base+131072+slot*1024,OFFSET_BEGINNING)==-1||Read(f,words,sizeof(words))!=sizeof(words)){Close(f);return 0;}
            if(words[0]!=UINT64_C(0xbab10c)||words[22]!=UINT64_C(0xa11cea11)||!words[23]||!(words[24]&2))continue;
            uint64_t seq=(words[24]>>32)&0xffff;
            /* Normal TXG uberblocks also carry MMP config, but always use
             * sequence zero (uberblock.c). Heartbeat writes start at one. */
            if(!seq)continue;
            if(!found||words[4]>*stamp||(words[4]==*stamp&&seq>*sequence)){*stamp=words[4];*sequence=seq;}
            found=1;
        }
    }
    Close(f);return found;
}
#endif
/* Use one kernel debug call per result. RawPutChar loops permit another
 * CPU's security diagnostics to split a result in the middle of a word. */
static void result_line(const char *line)
{
    if(KernelBase)KrnPrintf("%s",line);
    else for(const char *p=line;*p;p++)RawPutChar(*p);
}
#ifdef OZ_CLI_LAUNCH_CYCLES
/* Exercise the packaged executables without changing pool properties or
 * retrying failures. A successful SystemTags return alone is not evidence
 * that the intended command ran: require the exact property output too. */
static int cli_launch_cycles(void)
{
    for(unsigned i=0;i<OZ_CLI_LAUNCH_CYCLES;i++){
        const char *command=(i&1)?
            "C:zfs get -H -o value name qemu_mirror":
            "C:zpool get -H -o value name qemu_mirror";
        BPTR output=Open((CONST_STRPTR)"RAM:launch-output",MODE_NEWFILE);
        if(!output)return 0;
        SetIoErr(0);
        LONG rc=SystemTags((CONST_STRPTR)command,SYS_Output,output,SYS_Error,output,TAG_DONE);
        LONG error=IoErr();
        LONG flushed=Flush(output),closed=Close(output);
        char data[512]={0},line[256];LONG count=-1,extra=-1;
        output=Open((CONST_STRPTR)"RAM:launch-output",MODE_OLDFILE);
        if(output){count=Read(output,data,sizeof(data)-1);if(count>=0){char byte;extra=Read(output,&byte,1);}Close(output);}
        int exact=count==12&&extra==0&&!memcmp(data,"qemu_mirror\n",12);
        snprintf(line,sizeof(line),"CLI launch %u/%u command=%s rc=%ld ioerr=%ld bytes=%ld exact=%d flush=%ld close=%ld\n",
            i+1,(unsigned)OZ_CLI_LAUNCH_CYCLES,command,(long)rc,(long)error,(long)count,exact,(long)flushed,(long)closed);
        result_line(line);
        if(rc||!flushed||!closed||!exact){result_line(data);result_line("\n");return 0;}
    }
    result_line("RESULT native CLI launch cycles PASS exact-output=1 no-retries=1\n");
    return 1;
}
#endif
#ifdef OZ_ENCRYPTED_CREATE_GUI
#ifndef OZ_ENCRYPTED_KEY_FORMAT
#define OZ_ENCRYPTED_KEY_FORMAT 0
#endif
static const char *encrypted_format(void)
{ return OZ_ENCRYPTED_KEY_FORMAT==1?"raw":OZ_ENCRYPTED_KEY_FORMAT==2?"hex":"passphrase"; }
static int encrypted_fixture_key(const char *path,int wrong)
{
    unsigned char bytes[65];LONG n;
    if(OZ_ENCRYPTED_KEY_FORMAT==0){
        const char *text=wrong?"Public incorrect fixture passphrase\n":"Public disposable encrypted GUI fixture passphrase\n";
        n=(LONG)strlen(text);memcpy(bytes,text,n);
    }else if(OZ_ENCRYPTED_KEY_FORMAT==1){
        n=32;for(int i=0;i<32;i++)bytes[i]=(unsigned char)(i+1+wrong);
    }else{
        const char digits[]="0123456789abcdef";n=65;
        for(int i=0;i<32;i++){unsigned b=i+1+wrong;bytes[2*i]=digits[b>>4];bytes[2*i+1]=digits[b&15];}bytes[64]='\n';
    }
    BPTR f=Open((CONST_STRPTR)path,MODE_NEWFILE);if(!f)return 0;
    int ok=Write(f,bytes,n)==n;if(!Close(f))ok=0;
    return ok&&SetOwner((STRPTR)path,secOWNER_SYSTEM)&&SetProtection((CONST_STRPTR)path,0);
}
/* The cold-read dataset is created only by the UI. API authorization checks
 * use a separate name: absent after rejections, destroyed after the positive
 * delegated control. */
static LONG encrypted_command(const char *command)
{
    BPTR log=Open((CONST_STRPTR)"RAM:crypt-command",MODE_NEWFILE);if(!log)return -1;
    LONG rc=SystemTags((CONST_STRPTR)command,SYS_Output,log,SYS_Error,log,TAG_DONE);
    LONG error=IoErr();
    Flush(log);Close(log);result_line(command);result_line("\n");
    char status[128];snprintf(status,sizeof(status),"CLI result rc=%ld ioerr=%ld\n",(long)rc,(long)error);result_line(status);
    log=Open((CONST_STRPTR)"RAM:crypt-command",MODE_OLDFILE);
    if(log){char data[512];LONG n;while((n=Read(log,data,sizeof(data)-1))>0){data[n]=0;result_line(data);}Close(log);}
    return rc;
}
static int encrypted_dataset_key(const char *dataset,unsigned operation,const char *path,int expected)
{
    static struct oz_message m,out;struct oz_ticket ticket;
    memset(&m,0,sizeof(m));memset(&out,0,sizeof(out));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=operation;m.flags=OZ_CONFIRMED;
    snprintf(m.target,sizeof(m.target),"%s",dataset);
    if(path)snprintf(m.argument,sizeof(m.argument),"%s",path);
    if(operation==64&&!path)strcpy(m.options,"inherit=on");
    int e=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);if(e)return 0;
    do{e=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(e==EAGAIN)Delay(1);}while(e==EAGAIN);
    int release=oz_admission_release(OZ_ADMISSION_PORT,&ticket),reported=-1;
    const char *field=strstr(out.detail,"errno=");if(field)sscanf(field,"errno=%d",&reported);
    result_line(out.detail);result_line("\n");
    return !e&&!release&&reported==expected&&out.status==(expected?OZ_ENGINE_ERROR:OZ_OK);
}
static int encrypted_key(unsigned operation,const char *path,int expected)
{ return encrypted_dataset_key("qemu_mirror/gui-encrypted",operation,path,expected); }
static int encrypted_dataset_property(const char *dataset,const char *property,const char *expected)
{
    char command[256],data[128];
    snprintf(command,sizeof(command),"C:zfs get -H -o value %s %s >RAM:crypt-property",property,dataset);
    if(SystemTags((CONST_STRPTR)command,TAG_DONE)!=0)return 0;
    BPTR f=Open((CONST_STRPTR)"RAM:crypt-property",MODE_OLDFILE);if(!f)return 0;
    LONG n=Read(f,data,sizeof(data));int closed=Close(f);size_t length=strlen(expected);
    return closed&&n==(LONG)length+1&&!memcmp(data,expected,length)&&data[length]=='\n';
}
static int encrypted_property(const char *property,const char *expected)
{ return encrypted_dataset_property("qemu_mirror/gui-encrypted",property,expected); }
static int encrypted_reject(const char *path,const char *format,int expected)
{
    static struct oz_message m,out;struct oz_ticket ticket;
    memset(&m,0,sizeof(m));memset(&out,0,sizeof(out));
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=37;m.flags=OZ_CONFIRMED;
    strcpy(m.target,"qemu_mirror/rejected-key");
    snprintf(m.argument,sizeof(m.argument),"%s",path);
    snprintf(m.options,sizeof(m.options),"encryption=on,keyformat=%s",format);
    int e=oz_admission_submit(OZ_ADMISSION_PORT,&m,&ticket);if(e)return 0;
    do{e=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&out);if(e==EAGAIN)Delay(1);}while(e==EAGAIN);
    int release=oz_admission_release(OZ_ADMISSION_PORT,&ticket),reported=-1;
    const char *field=strstr(out.detail,"errno=");if(field)sscanf(field,"errno=%d",&reported);
    result_line(out.detail);result_line("\n");
    return !e&&!release&&reported==expected&&out.status==(expected?OZ_ENGINE_ERROR:OZ_OK);
}
static int encrypted_rejected_absent(void)
{
    if(encrypted_command("C:zfs list qemu_mirror/rejected-key")!=1)return 0;
    BPTR f=Open((CONST_STRPTR)"RAM:crypt-command",MODE_OLDFILE);if(!f)return 0;
    char data[1024];LONG n=Read(f,data,sizeof(data)-1);int closed=Close(f);
    if(n<0)return 0;
    data[n]=0;
    return closed&&strstr(data,"dataset does not exist")!=NULL;
}
static int encrypted_malformed_cases(void)
{
    static const struct {const char *name,*format;unsigned length;int invalid_hex;} cases[]={
        {"raw-short","raw",31,0},{"raw-long","raw",33,0},
        {"hex-short","hex",63,0},{"hex-long","hex",65,0},{"hex-invalid","hex",64,1},
        {"passphrase-short","passphrase",7,0},{"passphrase-long","passphrase",513,0},
        {"passphrase-empty","passphrase",0,0}};
    unsigned char bytes[513];
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++){
        memset(bytes,'a',sizeof(bytes));if(cases[i].invalid_hex)bytes[31]='g';
        const char *path="OZBOOT:malformed.key";
        BPTR f=Open((CONST_STRPTR)path,MODE_NEWFILE);if(!f)return 0;
        int ok=Write(f,bytes,cases[i].length)==(LONG)cases[i].length;if(!Close(f))ok=0;
        if(!ok||!SetOwner((STRPTR)path,secOWNER_SYSTEM)||!SetProtection((CONST_STRPTR)path,0))return 0;
        result_line(cases[i].name);result_line("\n");
        if(!encrypted_reject(path,cases[i].format,EINVAL)||!encrypted_rejected_absent())return 0;
    }
    if(!encrypted_reject("OZBOOT:absent.key","raw",ENOENT)||!encrypted_rejected_absent())return 0;
    result_line("RESULT encrypted malformed creation PASS cases=9 dataset-absent-after-each=1\n");
    return 1;
}
#endif
int main(int argc,char **argv)
{
    int error=0,operation_error=0;LONG failure_ioerr=0;struct Library *secBase=NULL;unsigned published=0;
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");
    struct Process *process=(struct Process *)FindTask(NULL);
    APTR oldwindow=process->pr_WindowPtr;process->pr_WindowPtr=(APTR)-1;
    static struct oz_startup_settings record,loaded;
    int create=argc==2&&!strcmp(argv[1],"CREATE");
#define CHECK(x) do{if(!(x)){failure_ioerr=IoErr();error=__LINE__;goto done;}}while(0)
    CHECK(argc==2&&(create||!strcmp(argv[1],"READ")));
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
#if defined(OZ_STARTUP_COLD_COPY) || defined(OZ_STARTUP_HOSTID) || defined(OZ_ENCRYPTED_CREATE_GUI)
    ULONG previous_owner=secGetTaskOwner(FindTask(NULL));
#endif
    CHECK(secLoginA(login)==secOWNER_SYSTEM);
    CHECK(SystemTags((CONST_STRPTR)"C:Mount SYS:OZBOOT",TAG_DONE)==0);
    if(create){
        /* Follow native SFSformat's inhibit/format/uninhibit sequence so
         * the handler reloads the newly written volume metadata. */
        CHECK(Inhibit((CONST_STRPTR)"OZBOOT:",TRUE));
        CHECK(Format((CONST_STRPTR)"OZBOOT:",(CONST_STRPTR)"OZARCHIVE",0x53465300));
        CHECK(Inhibit((CONST_STRPTR)"OZBOOT:",FALSE));
        /* SFS opts into MuFS through a volume key file. Disposable test
         * marker only, not a real credential; blank directory lines retain
         * the isolated SYS:Security fixture instead of claiming its config. */
        const char marker[]="OpenZFS-disposable-startup-test-only\n\n\n";
        BPTR key=Open((CONST_STRPTR)"OZBOOT:.MultiUser.keyfile",MODE_NEWFILE);CHECK(key);
        CHECK(Write(key,marker,sizeof(marker)-1)==sizeof(marker)-1);CHECK(Close(key));
        CHECK(SetOwner((STRPTR)"OZBOOT:.MultiUser.keyfile",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"OZBOOT:.MultiUser.keyfile",0));
    }
    CHECK(secFSRendezVous());
    for(unsigned i=0;i<250&&!secIsVolumeSecured((CONST_STRPTR)"OZBOOT:",0x53465300);i++)Delay(1);
    CHECK(secIsVolumeSecured((CONST_STRPTR)"OZBOOT:",0x53465300));
    if(create){BPTR dir=CreateDir((CONST_STRPTR)"OZBOOT:Prefs");CHECK(dir);UnLock(dir);}
    BPTR archive=Lock((CONST_STRPTR)"OZBOOT:Prefs",SHARED_LOCK);CHECK(archive);
    CHECK(AssignLock((CONST_STRPTR)"ENVARC",archive));
    record.guid=UINT64_C(0x123456789abcdef0);record.flags=OZ_STARTUP_ENABLED|OZ_STARTUP_READONLY|OZ_STARTUP_MOUNT;
#ifdef OZ_STARTUP_COLD_HANDLER
    /* Separate pool import from Mount-driven dataset activation. */
    record.flags&=~OZ_STARTUP_MOUNT;
    if(create) {
        static const char mountfile[]="FileSystem = L:OpenZFS-Handler\nDevice = openzfs\nUnit = 0\nFlags = 0\n"
            "Surfaces = 1\nBlocksPerTrack = 1\nLowCyl = 0\nHighCyl = 0\n"
            "DosType = 0x5a465301\nControl = \"qemu_mirror/files\"\n"
            "StackSize = 262144\nActivate = 1\n";
        BPTR file=Open((CONST_STRPTR)"OZBOOT:OZCOLD",MODE_NEWFILE);CHECK(file);
        CHECK(Write(file,mountfile,sizeof(mountfile)-1)==sizeof(mountfile)-1);
        CHECK(Flush(file));CHECK(Close(file));
        CHECK(SetOwner((STRPTR)"OZBOOT:OZCOLD",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"OZBOOT:OZCOLD",0));
    }
#endif
    record.count=2;strcpy(record.pool,"cold_settings");
    strcpy(record.paths[0],"/AROS/ata.device/1/partition/0");
    strcpy(record.paths[1],"/AROS/ata.device/3/partition/0");
#ifdef OZ_STARTUP_COLD_POOL
#ifdef OZ_STARTUP_COLD_COPY
    if(!create){
        BPTR hint=Open((CONST_STRPTR)"OZBOOT:fixture-guid",MODE_OLDFILE);CHECK(hint);
        CHECK(Read(hint,&record.guid,sizeof(record.guid))==sizeof(record.guid));CHECK(Close(hint));CHECK(record.guid);
        CHECK(oz_startup_settings_trusted(record.guid,&loaded)==ENOENT);
        int absent;Forbid();absent=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)==NULL&&FindPort((CONST_STRPTR)OZ_PORT)==NULL;Permit();
        CHECK(absent);
        char prepare[128];snprintf(prepare,sizeof(prepare),"C:OpenZFSStartup %016llx PREPARE",(unsigned long long)record.guid);
        char check_record[128];snprintf(check_record,sizeof(check_record),"C:OpenZFSStartup %016llx CHECK",(unsigned long long)record.guid);
        CHECK(SystemTags((CONST_STRPTR)check_record,TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSStartup invalid CHECK",TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSStartup 0000000000000000 PREPARE",TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSStartup invalid PREPARE",TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSStartup 123456789abcdef0 PREPARE EXTRA",TAG_DONE)==1);
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        CHECK(secLogoutA(logout)==previous_owner);
        CHECK(secLoginA(reader)==((77UL<<16)|88));
        CHECK(SystemTags((CONST_STRPTR)prepare,TAG_DONE)==1);
        CHECK(secLogoutA(logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(oz_startup_settings_trusted(record.guid,&loaded)==ENOENT);
        CHECK(SystemTags((CONST_STRPTR)prepare,TAG_DONE)==0);
        Forbid();absent=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)==NULL&&FindPort((CONST_STRPTR)OZ_PORT)==NULL;Permit();
        CHECK(absent);
        CHECK(!oz_startup_settings_trusted(record.guid,&loaded));
        CHECK(SystemTags((CONST_STRPTR)check_record,TAG_DONE)==0);
        /* A separate disposable ENV record proves CHECK accepts a disabled
         * configuration without probing its deliberately nonexistent member,
         * and rejects a damaged IFF file without repairing it. */
        struct oz_startup_settings disabled=loaded;
        disabled.guid=record.guid^UINT64_C(0x8000000000000000);
        if(!disabled.guid)disabled.guid=1;
        disabled.flags&=~OZ_STARTUP_ENABLED;disabled.count=1;
        strcpy(disabled.paths[0],"/AROS/nonexistent-check-only.device/123/whole");
        unsigned check_published=0;
        CHECK(!oz_startup_settings_apply_trusted(&disabled,0,&check_published)&&check_published==1);
        char check_disabled[128],disabled_path[80];
        snprintf(check_disabled,sizeof(check_disabled),"C:OpenZFSStartup %016llx CHECK",(unsigned long long)disabled.guid);
        snprintf(disabled_path,sizeof(disabled_path),"ENV:OpenZFS-%016llx.prefs",(unsigned long long)disabled.guid);
        CHECK(SystemTags((CONST_STRPTR)check_disabled,TAG_DONE)==0);
        BPTR damaged=Open((CONST_STRPTR)disabled_path,MODE_READWRITE);CHECK(damaged);
        CHECK(Write(damaged,"BAD!",4)==4);CHECK(Close(damaged));
        CHECK(SystemTags((CONST_STRPTR)check_disabled,TAG_DONE)==1);
        damaged=Open((CONST_STRPTR)disabled_path,MODE_OLDFILE);CHECK(damaged);
        char magic[4];CHECK(Read(damaged,magic,4)==4&&!memcmp(magic,"BAD!",4));CHECK(Close(damaged));
        CHECK(DeleteFile((CONST_STRPTR)disabled_path));
        const char *damaged_pass="RESULT native startup check damaged PASS disabled=1 nonexistent-device=1 bad-IFF=1 unchanged=1 cleanup=1\n";
        result_line(damaged_pass);
        CHECK(secLogoutA(logout)==previous_owner);
        CHECK(secLoginA(reader)==((77UL<<16)|88));
        CHECK(SystemTags((CONST_STRPTR)check_record,TAG_DONE)==1);
        CHECK(secLogoutA(logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        Forbid();absent=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)==NULL&&FindPort((CONST_STRPTR)OZ_PORT)==NULL;Permit();
        CHECK(absent);
        const char *check_pass="RESULT native startup check PASS valid=1 missing=1 malformed=1 ordinary-user=1 service-absent=1\n";
        result_line(check_pass);
        CHECK(SystemTags((CONST_STRPTR)"C:Copy ENVARC: ENV: ALL NOPRO NOREQ",TAG_DONE)==0);
        CHECK(!oz_startup_settings_trusted(record.guid,&loaded));
        const char *pass="RESULT native startup prepare PASS service-absent-before=1 service-absent-after=1 protected-ENV=1\n";
        result_line(pass);
        const char *denied="RESULT native startup prepare rejection PASS malformed=3 ordinary-user=1 ENV-absent=1\n";
        result_line(denied);
    }
#endif
#ifdef OZ_STARTUP_HOSTID
    CHECK(SetVar((CONST_STRPTR)"ZFS_HOSTID",(CONST_STRPTR)"0x11223344",-1,GVF_LOCAL_ONLY));
#ifndef OZ_STARTUP_HOSTID_EMPTY
    if(create){
        const unsigned char host_pref[42]={
            'F','O','R','M',0,0,0,34,'P','R','E','F',
            'P','R','H','D',0,0,0,6,0,0,0,0,0,0,
            'O','Z','H','I',0,0,0,8,0,0,0,1,0x51,0xa2,0x05,0x19};
        BPTR host_file=Open((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs",MODE_NEWFILE);CHECK(host_file);
        CHECK(oz_hostid_record_write(host_file,0)==EINVAL);
        CHECK(!oz_hostid_record_write(host_file,0x51a20519));CHECK(Close(host_file));
        host_file=Open((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs",MODE_OLDFILE);CHECK(host_file);
        unsigned char encoded[43];LONG encoded_bytes=Read(host_file,encoded,sizeof(encoded));
        CHECK(encoded_bytes==sizeof(host_pref)&&!memcmp(encoded,host_pref,sizeof(host_pref)));
        uint32_t decoded=0;
        CHECK(!oz_hostid_record_read(host_file,&decoded)&&decoded==0x51a20519);
        CHECK(Close(host_file));
        CHECK(SetOwner((STRPTR)"ENVARC:OpenZFSHost.prefs",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs",0));
        CHECK(SetOwner((STRPTR)"ENV:",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"ENV:",0));
        /* Deterministically exercise the published startup reservation,
         * without timing a real service launch against filesystem writes. */
        struct MsgPort *starting_guard=CreateMsgPort();CHECK(starting_guard);
        starting_guard->mp_Node.ln_Name=OZ_PORT ".Starting";
        Forbid();AddPort(starting_guard);Permit();
        unsigned starting_published=99;
        int starting_error=oz_hostid_settings_apply(0x11223344,&starting_published);
        Forbid();RemPort(starting_guard);Permit();DeleteMsgPort(starting_guard);
        CHECK(starting_error==EBUSY&&starting_published==0);
        host_file=Open((CONST_STRPTR)"ENVARC:OpenZFSHost.prefs",MODE_OLDFILE);CHECK(host_file);
        CHECK(!oz_hostid_record_read(host_file,&decoded)&&decoded==0x51a20519);
        CHECK(Close(host_file));
        result_line("RESULT native hostid startup reservation PASS busy=1 unpublished=1 saved-unchanged=1\n");
#ifdef OZ_STARTUP_HOSTID_GUI
        CHECK(SystemTags((CONST_STRPTR)"C:Assign USBCLASSES: SYS:Classes/USB",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:AddUSBClasses QUIET",TAG_DONE)==0);
        Delay(100);
        result_line("RESULT native hostid GUI READY choose-Startup-save-51a20519-close-Manager\n");
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        const char *gui_identity_paths[]={"ENV:OpenZFSHost.prefs","ENVARC:OpenZFSHost.prefs"};
        for(unsigned i=0;i<2;i++){
            BPTR gui_identity_file=Open((CONST_STRPTR)gui_identity_paths[i],MODE_OLDFILE);CHECK(gui_identity_file);
            uint32_t gui_identity=0;
            CHECK(!oz_hostid_record_read(gui_identity_file,&gui_identity)&&gui_identity==0x51a20519);
            CHECK(Close(gui_identity_file));
        }
        result_line("RESULT native hostid GUI PASS saved=1 current=1\n");
#endif
        unsigned identity_published=99;
        CHECK(oz_hostid_settings_apply(0,&identity_published)==EINVAL&&identity_published==0);
        CHECK(!oz_hostid_settings_apply(0x51a20519,&identity_published)&&identity_published==3);
        result_line("RESULT native hostid provisioning PASS saved=1 current=1 zero-refused=1\n");
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 00000000 SAVE",TAG_DONE)==20);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 11223344",TAG_DONE)==20);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 51a20519 SAVE",TAG_DONE)==0);
        result_line("RESULT native hostid CLI PASS invalid=1 save-required=1 saved=1\n");
        struct TagItem identity_logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem identity_reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        CHECK(secLogoutA(identity_logout)==previous_owner);
        CHECK(secLoginA(identity_reader)==((77UL<<16)|88));
        CHECK(oz_hostid_settings_apply(0x11223344,&identity_published)==EPERM&&identity_published==0);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 11223344 SAVE",TAG_DONE)==20);
        CHECK(secLogoutA(identity_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        const char *identity_check_paths[]={"ENVARC:OpenZFSHost.prefs","ENV:OpenZFSHost.prefs"};
        for(unsigned i=0;i<2;i++){
            BPTR verified=Open((CONST_STRPTR)identity_check_paths[i],MODE_OLDFILE);CHECK(verified);
            uint32_t retained=0;CHECK(!oz_hostid_record_read(verified,&retained)&&retained==0x51a20519);
            CHECK(Close(verified));
        }
        result_line("RESULT native hostid authorization PASS ordinary-user=1 cli=1 unchanged=1\n");
        /* A real conflicting ENV transaction makes the second publication
         * fail, without injecting faults into production binaries. */
        BPTR identity_pending=CreateDir((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn");
        CHECK(identity_pending);UnLock(identity_pending);
        CHECK(oz_hostid_settings_apply(0x11223344,&identity_published)==EBUSY&&identity_published==OZ_HOSTID_SAVED);
        for(unsigned i=0;i<2;i++){
            BPTR verified=Open((CONST_STRPTR)identity_check_paths[i],MODE_OLDFILE);CHECK(verified);
            uint32_t retained=0;CHECK(!oz_hostid_record_read(verified,&retained)&&retained==(i?0x51a20519:0x11223344));
            CHECK(Close(verified));
        }
        identity_pending=Lock((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn",SHARED_LOCK);
        CHECK(identity_pending);UnLock(identity_pending);
        CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn"));
        CHECK(!oz_hostid_settings_apply(0x51a20519,&identity_published)&&identity_published==3);
        result_line("RESULT native hostid partial save PASS ENVARC-new=1 ENV-old=1 pending-preserved=1 recovered=1\n");
    }
    CHECK(SystemTags((CONST_STRPTR)"C:Copy ENVARC:OpenZFSHost.prefs ENV:OpenZFSHost.prefs NOPRO NOREQ",TAG_DONE)==0);
    CHECK(SetOwner((STRPTR)"ENV:",secOWNER_SYSTEM));
    CHECK(SetProtection((CONST_STRPTR)"ENV:",0));
    CHECK(SetOwner((STRPTR)"ENV:OpenZFSHost.prefs",secOWNER_SYSTEM));
    CHECK(SetProtection((CONST_STRPTR)"ENV:OpenZFSHost.prefs",0));
    if(create){
        unsigned char good_identity[42],bad_identity[42];
        BPTR identity_file=Open((CONST_STRPTR)"ENV:OpenZFSHost.prefs",MODE_OLDFILE);CHECK(identity_file);
        CHECK(Read(identity_file,good_identity,sizeof(good_identity))==sizeof(good_identity));CHECK(Close(identity_file));
        for(unsigned scenario=0;scenario<7;scenario++){
            BPTR update_guard=0;
            memcpy(bad_identity,good_identity,sizeof(bad_identity));
            if(scenario==0)bad_identity[0]='X';
            if(scenario==1)bad_identity[37]=2;
            if(scenario==2)memset(bad_identity+38,0,4);
            identity_file=Open((CONST_STRPTR)"ENV:OpenZFSHost.prefs",MODE_NEWFILE);CHECK(identity_file);
            CHECK(Write(identity_file,bad_identity,sizeof(bad_identity))==sizeof(bad_identity));CHECK(Close(identity_file));
            CHECK(SetOwner((STRPTR)"ENV:OpenZFSHost.prefs",scenario==3?(77UL<<16)|88:secOWNER_SYSTEM));
            if(scenario==4){
                BPTR pending_identity=CreateDir((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn");
                CHECK(pending_identity);UnLock(pending_identity);
            }
            if(scenario>=5){
                update_guard=CreateDir((CONST_STRPTR)"ENV:OpenZFSHost.update");CHECK(update_guard);
                unsigned blocked_publication=99;
                CHECK(oz_hostid_settings_apply(0x11223344,&blocked_publication)==EBUSY&&blocked_publication==0);
                if(scenario==5){UnLock(update_guard);update_guard=0;}
            }
            BPTR identity_log=Open((CONST_STRPTR)"RAM:hostid-rejected",MODE_NEWFILE);CHECK(identity_log);
            LONG refused=SystemTags((CONST_STRPTR)"C:OpenZFSServiceNative WRITABLE",
                SYS_Output,identity_log,SYS_Error,identity_log,TAG_DONE);
            CHECK(Flush(identity_log));CHECK(Close(identity_log));CHECK(refused==20);
            Forbid();int service_absent=FindPort((CONST_STRPTR)OZ_PORT)==NULL&&FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)==NULL;Permit();
            CHECK(service_absent);
            identity_log=Open((CONST_STRPTR)"RAM:hostid-rejected",MODE_OLDFILE);CHECK(identity_log);
            char identity_error[1024]={0};LONG identity_bytes=Read(identity_log,identity_error,sizeof(identity_error)-1);
            CHECK(Close(identity_log));CHECK(identity_bytes>0&&strstr(identity_error,"native host identity rejected"));
            identity_file=Open((CONST_STRPTR)"ENV:OpenZFSHost.prefs",MODE_OLDFILE);CHECK(identity_file);
            unsigned char retained_identity[43];LONG retained_bytes=Read(identity_file,retained_identity,sizeof(retained_identity));
            CHECK(Close(identity_file));CHECK(retained_bytes==42&&!memcmp(retained_identity,bad_identity,42));
            CHECK(SetOwner((STRPTR)"ENV:OpenZFSHost.prefs",secOWNER_SYSTEM));
            if(scenario==4)CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.prefs.txn"));
            if(update_guard)UnLock(update_guard);
            if(scenario>=5)CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSHost.update"));
        }
        identity_file=Open((CONST_STRPTR)"ENV:OpenZFSHost.prefs",MODE_NEWFILE);CHECK(identity_file);
        CHECK(Write(identity_file,good_identity,sizeof(good_identity))==sizeof(good_identity));CHECK(Close(identity_file));
        CHECK(SetOwner((STRPTR)"ENV:OpenZFSHost.prefs",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"ENV:OpenZFSHost.prefs",0));
        result_line("RESULT native hostid rejection PASS malformed=1 version=1 zero=1 foreign-owner=1 unchanged=1 ports-absent=1\n");
        result_line("RESULT native hostid transaction rejection PASS pending=1 recovered=1\n");
        result_line("RESULT native hostid update exclusion PASS writer-busy=1 stale-marker=1 held-marker=1 recovered=1\n");
    }
#endif
#endif
    CHECK(SystemTags((CONST_STRPTR)"Run >T:service.log C:OpenZFSServiceNative WRITABLE",TAG_DONE)==0);
    int ready=0;
    for(unsigned i=0;i<1500;i++){
        Forbid();ready=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();
        if(ready)break;
        Delay(1);
    }
    CHECK(ready);
#ifdef OZ_STARTUP_HOSTID
    ULONG actual_hostid=0;
#ifdef OZ_STARTUP_HOSTID_EMPTY
    CHECK(!oz_hostid_query(OZ_ADMISSION_PORT,&actual_hostid)&&actual_hostid==0);
    const char *identity_paths[]={"ENV:OpenZFSHost.prefs","ENVARC:OpenZFSHost.prefs"};
    for(unsigned i=0;i<2;i++){
        BPTR identity_lock=Lock((CONST_STRPTR)identity_paths[i],SHARED_LOCK);
        LONG identity_error=IoErr();
        if(identity_lock)UnLock(identity_lock);
        CHECK(!identity_lock&&identity_error==ERROR_OBJECT_NOT_FOUND);
    }
    result_line("RESULT native unprovisioned hostid PASS id=0 environment-ignored=1 no-files-created=1\n");
#else
    CHECK(!oz_hostid_query(OZ_ADMISSION_PORT,&actual_hostid)&&actual_hostid==0x51a20519UL);
    unsigned busy_published=99;
    CHECK(oz_hostid_settings_apply(0x11223344,&busy_published)==EBUSY&&busy_published==0);
    CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSHostIdentity 11223344 SAVE",TAG_DONE)==20);
    result_line("RESULT native persistent hostid PASS id=51a20519 environment-ignored=1\n");
#endif
#endif
    strcpy(record.pool,"qemu_mirror");
    if(create){
#ifdef OZ_STARTUP_DEFAULT_CACHE
        /* A new disposable backing file, never a user disk. No cachefile
         * option: this exercises the ordinary upstream create path. */
        BPTR member=Open((CONST_STRPTR)"RAM:default-cache.vdev",MODE_NEWFILE);CHECK(member);
        char *zero=calloc(1,1024*1024);CHECK(zero);
        int filled=1;
        for(unsigned i=0;i<128;i++)if(Write(member,zero,1024*1024)!=1024*1024){filled=0;break;}
        free(zero);CHECK(Flush(member));CHECK(Close(member));CHECK(filled);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool create -O mountpoint=none default_created /RAM/default-cache.vdev",TAG_DONE)==0);
        BPTR created_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);CHECK(created_cache);UnLock(created_cache);
#ifdef OZ_STARTUP_MMP
#ifdef OZ_STARTUP_HOSTID_EMPTY
        BPTR mmp_log=Open((CONST_STRPTR)"RAM:mmp-no-hostid",MODE_NEWFILE);CHECK(mmp_log);
        LONG mmp_refused=SystemTags((CONST_STRPTR)"C:zpool set multihost=on default_created",
            SYS_Output,mmp_log,SYS_Error,mmp_log,TAG_DONE);
        CHECK(Flush(mmp_log));CHECK(Close(mmp_log));CHECK(mmp_refused==1);
        mmp_log=Open((CONST_STRPTR)"RAM:mmp-no-hostid",MODE_OLDFILE);CHECK(mmp_log);
        char mmp_error[1024]={0};LONG mmp_bytes=Read(mmp_log,mmp_error,sizeof(mmp_error)-1);
        CHECK(Close(mmp_log));CHECK(mmp_bytes>0&&strstr(mmp_error,"requires a non-zero system hostid"));
        mmp_log=Open((CONST_STRPTR)"RAM:mmp-property",MODE_NEWFILE);CHECK(mmp_log);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -o value multihost default_created",SYS_Output,mmp_log,TAG_DONE)==0);
        CHECK(Flush(mmp_log));CHECK(Close(mmp_log));
        mmp_log=Open((CONST_STRPTR)"RAM:mmp-property",MODE_OLDFILE);CHECK(mmp_log);
        char mmp_property[16]={0};mmp_bytes=Read(mmp_log,mmp_property,sizeof(mmp_property)-1);
        CHECK(Close(mmp_log));CHECK(mmp_bytes>=4&&!memcmp(mmp_property,"off",3)&&(mmp_property[3]=='\n'||mmp_property[3]=='\r'));
        result_line("RESULT native MMP missing hostid PASS CLI-refused=1 explicit-diagnostic=1 property-off=1\n");
#else
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set multihost=on default_created",TAG_DONE)==0);
        uint64_t first_stamp=0,first_sequence=0,next_stamp=0,next_sequence=0;
        int first_found=0,advanced=0;
        for(unsigned i=0;i<20&&!first_found;i++){Delay(25);first_found=mmp_sample(&first_stamp,&first_sequence);}
        CHECK(first_found);
        for(unsigned i=0;i<20&&!advanced;i++){
            Delay(25);
            advanced=mmp_sample(&next_stamp,&next_sequence)&&
                (next_stamp>first_stamp||(next_stamp==first_stamp&&next_sequence>first_sequence));
        }
        CHECK(advanced);
        char heartbeat_observed[192];
        snprintf(heartbeat_observed,sizeof(heartbeat_observed),
            "Native MMP labels: first timestamp=%llu sequence=%llu next timestamp=%llu sequence=%llu\n",
            (unsigned long long)first_stamp,(unsigned long long)first_sequence,
            (unsigned long long)next_stamp,(unsigned long long)next_sequence);
        result_line(heartbeat_observed);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set multihost=off default_created",TAG_DONE)==0);
        result_line("RESULT native MMP heartbeat PASS enabled=1 label-sequence-advanced=1 disabled=1\n");
#endif
#endif
        CHECK(SystemTags((CONST_STRPTR)"C:zpool destroy default_created",TAG_DONE)==0);
        created_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);
        if(created_cache)UnLock(created_cache);
        CHECK(!created_cache&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        CHECK(DeleteFile((CONST_STRPTR)"RAM:default-cache.vdev"));
        const char *created="RESULT native default cache create PASS created=1 serialized=1 destroyed=1 cache-removed=1 cleanup=1\n";
        result_line(created);
        BPTR diagnostic=Open((CONST_STRPTR)"RAM:cache-default-command",MODE_NEWFILE);CHECK(diagnostic);
        LONG imported=SystemTags((CONST_STRPTR)"C:zpool import -N -d /AROS/ata.device/1/partition/0 -d /AROS/ata.device/3/partition/0 qemu_mirror",
            SYS_Output,diagnostic,SYS_Error,diagnostic,TAG_DONE);
        CHECK(Flush(diagnostic));CHECK(Close(diagnostic));CHECK(imported==0);
        BPTR default_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);CHECK(default_cache);UnLock(default_cache);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile=none qemu_mirror",TAG_DONE)==0);
        default_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);
        if(default_cache)UnLock(default_cache);
        CHECK(!default_cache);
        /* Recheck trust at each request, not only at service startup. These
         * objects live exclusively on this newly formatted SFS fixture. */
        BPTR wrong_type=CreateDir((CONST_STRPTR)"ENVARC:OpenZFS.cache");CHECK(wrong_type);
        UnLock(wrong_type);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile= qemu_mirror",TAG_DONE)==1);
        wrong_type=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);CHECK(wrong_type);
        struct FileInfoBlock *cache_info=AllocDosObject(DOS_FIB,NULL);CHECK(cache_info);
        int unchanged_dir=Examine(wrong_type,cache_info)&&cache_info->fib_DirEntryType==ST_USERDIR;
        FreeDosObject(DOS_FIB,cache_info);UnLock(wrong_type);CHECK(unchanged_dir);
        CHECK(DeleteFile((CONST_STRPTR)"ENVARC:OpenZFS.cache"));
        const char cache_marker[]="untrusted-cache-must-not-be-replaced";
        BPTR untrusted=Open((CONST_STRPTR)"ENVARC:OpenZFS.cache",MODE_NEWFILE);CHECK(untrusted);
        CHECK(Write(untrusted,cache_marker,sizeof(cache_marker))==sizeof(cache_marker));
        CHECK(Close(untrusted));
        CHECK(SetOwner((STRPTR)"ENVARC:OpenZFS.cache",(77UL<<16)|88));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile= qemu_mirror",TAG_DONE)==1);
        untrusted=Open((CONST_STRPTR)"ENVARC:OpenZFS.cache",MODE_OLDFILE);CHECK(untrusted);
        char cache_data[sizeof(cache_marker)+1];LONG cache_bytes=Read(untrusted,cache_data,sizeof(cache_data));
        CHECK(Close(untrusted));CHECK(cache_bytes==sizeof(cache_marker)&&!memcmp(cache_data,cache_marker,sizeof(cache_marker)));
        CHECK(SetOwner((STRPTR)"ENVARC:OpenZFS.cache",secOWNER_SYSTEM));
        CHECK(DeleteFile((CONST_STRPTR)"ENVARC:OpenZFS.cache"));
        BPTR policy_dir=Lock((CONST_STRPTR)"ENVARC:",SHARED_LOCK);CHECK(policy_dir);
        cache_info=AllocDosObject(DOS_FIB,NULL);CHECK(cache_info);
        CHECK(Examine(policy_dir,cache_info));LONG original_protection=cache_info->fib_Protection;
        FreeDosObject(DOS_FIB,cache_info);UnLock(policy_dir);
        CHECK(SetProtection((CONST_STRPTR)"OZBOOT:Prefs",original_protection|FIBF_GRP_WRITE));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile= qemu_mirror",TAG_DONE)==1);
        CHECK(SetProtection((CONST_STRPTR)"OZBOOT:Prefs",original_protection));
        default_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);
        if(default_cache)UnLock(default_cache);
        CHECK(!default_cache&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -o value cachefile qemu_mirror >RAM:cache-policy",TAG_DONE)==0);
        BPTR policy_file=Open((CONST_STRPTR)"RAM:cache-policy",MODE_OLDFILE);CHECK(policy_file);
        char policy_value[16];LONG policy_bytes=Read(policy_file,policy_value,sizeof(policy_value));CHECK(Close(policy_file));
        CHECK(policy_bytes>=5&&!memcmp(policy_value,"none",4)&&(policy_value[4]=='\n'||policy_value[4]=='\r'));
        result_line("RESULT native default cache rejection PASS directory=1 foreign-owner=1 writable-parent=1 unchanged=1 policy-none=1\n");
#ifdef OZ_STARTUP_CACHE_GUI
        /* Minimal cold fixture omits the stock USB class scan. Restore it
         * for human-equivalent tablet input to this isolated GUI test. */
        CHECK(SystemTags((CONST_STRPTR)"C:Assign USBCLASSES: SYS:Classes/USB",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:AddUSBClasses QUIET",TAG_DONE)==0);
        Delay(100);
        result_line("Native default cache GUI READY: Pools/set qemu_mirror cachefile=; confirm, then close Manager\n");
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        default_cache=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);
        CHECK(default_cache);UnLock(default_cache);
        result_line("RESULT native default cache GUI PASS reset=1 serialized=1\n");
#else
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile= qemu_mirror",TAG_DONE)==0);
#endif
#else
        CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -o cachefile=none -d /AROS/ata.device/1/partition/0 -d /AROS/ata.device/3/partition/0 qemu_mirror",TAG_DONE)==0);
#endif
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -p -o value guid qemu_mirror >RAM:guid.txt",TAG_DONE)==0);
        BPTR info=Open((CONST_STRPTR)"RAM:guid.txt",MODE_OLDFILE);CHECK(info);
        char value[80]={0};LONG bytes=Read(info,value,sizeof(value)-1);CHECK(Close(info));CHECK(bytes>0);
        char *end;record.guid=strtoull(value,&end,10);CHECK(record.guid&&(*end=='\n'||*end=='\r'));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs set mountpoint=none qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zfs set mountpoint=OZCOLD: qemu_mirror/files",TAG_DONE)==0);
#ifdef OZ_CLI_LAUNCH_CYCLES
        CHECK(cli_launch_cycles());
#endif
#ifdef OZ_ENCRYPTED_CREATE_GUI
        /* The baseline mirror intentionally has a minimal feature set.
         * Upgrade only our disposable COW copy, through the shipped CLI. */
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get feature@encryption qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set feature@encryption=enabled qemu_mirror",TAG_DONE)==0);
        CHECK(encrypted_fixture_key("OZBOOT:gui-create.key",0));
        CHECK(encrypted_fixture_key("OZBOOT:gui-wrong.key",1));
#ifndef OZ_MALFORMED_KEY_CYCLES
#define OZ_MALFORMED_KEY_CYCLES 1
#endif
        for(unsigned cycle=0;cycle<OZ_MALFORMED_KEY_CYCLES;cycle++){
            char progress[96];snprintf(progress,sizeof(progress),"Malformed-key launch context cycle=%u/%u\n",cycle+1,(unsigned)OZ_MALFORMED_KEY_CYCLES);result_line(progress);
            CHECK(encrypted_malformed_cases());
        }
        struct TagItem crypt_logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem crypt_reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
        int refused=encrypted_reject("OZBOOT:gui-create.key",encrypted_format(),EPERM);
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(refused&&encrypted_rejected_absent());
        result_line("RESULT encrypted unauthorized creation PASS uid=77 dataset-absent=1\n");
        CHECK(encrypted_command("C:zfs allow -u 77 create,mount,keylocation qemu_mirror")==0);
        CHECK(MakeLink((CONST_STRPTR)"OZBOOT:key-absolute",(SIPTR)"OZBOOT:gui-create.key",LINK_SOFT));
        CHECK(MakeLink((CONST_STRPTR)"OZBOOT:key-relative",(SIPTR)"gui-create.key",LINK_SOFT));
        CHECK(MakeLink((CONST_STRPTR)"OZBOOT:key-cycle",(SIPTR)"key-cycle",LINK_SOFT));
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
        refused=encrypted_reject("OZBOOT:gui-create.key",encrypted_format(),EACCES);
        int absolute_refused=encrypted_reject("OZBOOT:key-absolute",encrypted_format(),EACCES);
        int relative_refused=encrypted_reject("OZBOOT:key-relative",encrypted_format(),EACCES);
        int cycle_refused=encrypted_reject("OZBOOT:key-cycle",encrypted_format(),ELOOP);
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(refused&&absolute_refused&&relative_refused&&cycle_refused&&encrypted_rejected_absent());
        /* Changing only this disposable key file's owner must make the
         * delegated request succeed: distinguish file access from create ACL. */
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",(77UL<<16)|88));
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
        int delegated=encrypted_reject("OZBOOT:gui-create.key",encrypted_format(),0);
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",secOWNER_SYSTEM));
        CHECK(delegated);
        CHECK(encrypted_command("C:zfs list qemu_mirror/rejected-key")==0);
        CHECK(encrypted_command("C:zfs destroy qemu_mirror/rejected-key")==0);
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",(77UL<<16)|88));
        const char *key_aliases[]={"OZBOOT:key-absolute","OZBOOT:key-relative"};
        for(unsigned alias_index=0;alias_index<2;alias_index++){
            CHECK(secLogoutA(crypt_logout)==previous_owner);
            CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
            delegated=encrypted_reject(key_aliases[alias_index],encrypted_format(),0);
            CHECK(secLogoutA(crypt_logout)==previous_owner);
            CHECK(secLoginA(login)==secOWNER_SYSTEM);
            CHECK(delegated);
            CHECK(encrypted_command("C:zfs destroy qemu_mirror/rejected-key")==0);
            CHECK(encrypted_rejected_absent());
        }
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",secOWNER_SYSTEM));
        /* Retain link fixtures in the disposable archive for inspection:
         * this SFS-backed fixture returns ERROR_IS_SOFT_LINK from DeleteFile. Do
         * not let unrelated link cleanup mask the authorization verdict. */
        result_line("RESULT encrypted key link access PASS private-target-denied=2 owner-target-accepted=2 cycle-denied=1 datasets-cleaned=1 links-retained=3\n");
        CHECK(encrypted_command("C:zfs unallow -u 77 create,mount,keylocation qemu_mirror")==0);
        /* A successful unallow command is not itself proof of enforcement.
         * Keep the key readable by the same user so a file-access refusal
         * cannot mask a stale delegated create permission. */
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",(77UL<<16)|88));
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
        refused=encrypted_reject("OZBOOT:gui-create.key",encrypted_format(),EPERM);
        absolute_refused=encrypted_reject("OZBOOT:key-absolute",encrypted_format(),EPERM);
        relative_refused=encrypted_reject("OZBOOT:key-relative",encrypted_format(),EPERM);
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(SetOwner((STRPTR)"OZBOOT:gui-create.key",secOWNER_SYSTEM));
        CHECK(refused&&absolute_refused&&relative_refused);
        CHECK(encrypted_rejected_absent());
        result_line("RESULT encrypted delegation revocation PASS readable-key=1 direct-denied=1 linked-denied=2 dataset-absent=1\n");
        result_line("RESULT encrypted delegated file access PASS create-granted=1 private-key-denied=1 owner-key-accepted=1 cleaned=1 revoked=1\n");
        CHECK(SystemTags((CONST_STRPTR)"C:zfs list qemu_mirror/gui-encrypted",TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:IPrefs",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:Assign USBCLASSES: SYS:Classes/USB",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:AddUSBClasses QUIET",TAG_DONE)==0);
        Delay(100);
#ifdef OZ_KEY_PROMPT_GUI
        CHECK(SystemTags((CONST_STRPTR)"C:Mount DEVS:DOSDrivers/PIPE",TAG_DONE)==0);
        /* Upstream keylocation=prompt also accepts redirected key material.
         * The no-echo console helper must not break that non-TTY path. */
        CHECK(encrypted_command("C:zfs create -u -o encryption=on -o keyformat=passphrase -o keylocation=prompt qemu_mirror/rejected-key <OZBOOT:gui-create.key")==0);
        CHECK(encrypted_command("C:zfs get encryption,keyformat,keylocation qemu_mirror/rejected-key")==0);
        CHECK(encrypted_command("C:zfs destroy qemu_mirror/rejected-key")==0);
        CHECK(encrypted_rejected_absent());
        result_line("RESULT CLI redirected key PASS real-create=1 cleaned=1\n");
        BPTR prompt_console=Open((CONST_STRPTR)"CON:100/100/700/200/OpenZFS key prompt test/CLOSE",MODE_NEWFILE);
        CHECK(prompt_console);
        for(unsigned prompt_case=0;prompt_case<3;prompt_case++){
        BPTR prompt_errors=Open((CONST_STRPTR)"RAM:prompt-errors",MODE_NEWFILE);CHECK(prompt_errors);
        const char *prompt_cases[]={"cancel with Escape","mismatched confirmation","edited public-test-passphrase twice"};
        char prompt_result[160];snprintf(prompt_result,sizeof(prompt_result),"RESULT CLI key prompt READY case=%u %s\n",prompt_case,prompt_cases[prompt_case]);result_line(prompt_result);
        const char *prompt_command=prompt_case<2?
            "C:zfs create -u -o encryption=on -o keyformat=passphrase -o keylocation=prompt qemu_mirror/rejected-key":
            "C:zfs create -u -o encryption=on -o keyformat=passphrase -o keylocation=prompt qemu_mirror/cli-prompt";
        LONG prompt_rc=SystemTags((CONST_STRPTR)prompt_command,
            /* SystemTagList requires distinct handles: NULL output/error
             * asks the shell to open CONSOLE: on the input's handler. */
            SYS_Input,prompt_console,SYS_Output,0,SYS_Error,prompt_errors,NP_StackSize,1048576,TAG_DONE);
        snprintf(prompt_result,sizeof(prompt_result),"RESULT CLI key prompt command case=%u rc=%ld ioerr=%ld\n",prompt_case,(long)prompt_rc,(long)IoErr());result_line(prompt_result);
        CHECK(Flush(prompt_errors));CHECK(Seek(prompt_errors,0,OFFSET_BEGINNING)!=-1);
        char prompt_error_text[512];LONG prompt_got;
        while((prompt_got=Read(prompt_errors,prompt_error_text,sizeof(prompt_error_text)-1))>0){prompt_error_text[prompt_got]=0;result_line(prompt_error_text);}
        CHECK(prompt_got==0);CHECK(Close(prompt_errors));
        CHECK(prompt_rc==(prompt_case<2?1:0));
        if(prompt_case<2)CHECK(encrypted_rejected_absent());
        /* This handle is reused, not reopened: prove restored cooked echo
         * visually and a complete native line after success AND errors. */
        const char echo_prompt[]="\nMode restoration: type echo-check then Enter: ";
        CHECK(Write(prompt_console,echo_prompt,sizeof(echo_prompt)-1)==sizeof(echo_prompt)-1);
        result_line("RESULT CLI key prompt ECHO READY type echo-check; capture before Enter\n");
        char echo_line[64];LONG echo_bytes=Read(prompt_console,echo_line,sizeof(echo_line));
        CHECK(echo_bytes==11&&!memcmp(echo_line,"echo-check\n",11));
        snprintf(prompt_result,sizeof(prompt_result),"RESULT CLI key prompt case=%u PASS expected-status=1 restored-line=1\n",prompt_case);result_line(prompt_result);
        }
        /* console_handler's final ACTION_END goes through its shutdown
         * reply (DOSFALSE with error=0). Record both values, not a generic
         * filesystem Close() boolean assertion. */
        LONG prompt_closed=Close(prompt_console),prompt_close_error=IoErr();
        char prompt_result[128];
        snprintf(prompt_result,sizeof(prompt_result),"RESULT CLI prompt close rc=%ld ioerr=%ld\n",(long)prompt_closed,(long)prompt_close_error);result_line(prompt_result);
        CHECK(prompt_closed||prompt_close_error==0);
        CHECK(encrypted_command("C:zfs get encryption,keyformat,keylocation qemu_mirror/cli-prompt")==0);
        CHECK(encrypted_command("C:zfs destroy qemu_mirror/cli-prompt")==0);
        result_line("RESULT CLI key prompt CREATE PASS upstream-create=1 confirmed=1 cleaned=1\n");
#endif
        char ready_line[256];snprintf(ready_line,sizeof(ready_line),"RESULT encrypted creation GUI READY dataset=qemu_mirror/gui-encrypted key=OZBOOT:gui-create.key format=%s; create then close Manager\n",encrypted_format());result_line(ready_line);
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",NP_StackSize,262144,TAG_DONE)==0);
        CHECK(encrypted_property("encryption","aes-256-gcm"));
        CHECK(encrypted_property("keyformat",encrypted_format()));
        CHECK(encrypted_property("keylocation","prompt"));
        CHECK(encrypted_property("encryptionroot","qemu_mirror/gui-encrypted"));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs set mountpoint=OZCRYPT: qemu_mirror/gui-encrypted",TAG_DONE)==0);
        const char content[]="Encrypted GUI creation: persistent native AROS handler bytes.\n";
        BPTR cryptfile=Open((CONST_STRPTR)"OZCRYPT:proof.txt",MODE_NEWFILE);CHECK(cryptfile);
        CHECK(Write(cryptfile,content,sizeof(content)-1)==sizeof(content)-1);CHECK(Flush(cryptfile));CHECK(Close(cryptfile));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs unmount qemu_mirror/gui-encrypted",TAG_DONE)==0);
        /* Exercise packaged libzfs change-key with the dataset's existing
         * keyformat: passphrase KDF and exact raw/hex material all use the
         * upstream CLI, not a test-only key-change adapter. */
        {
            CHECK(encrypted_command("C:zfs change-key -o keylocation=prompt qemu_mirror/gui-encrypted <OZBOOT:gui-wrong.key")==0);
            CHECK(encrypted_key(63,NULL,0));
            CHECK(encrypted_key(62,"OZBOOT:gui-create.key",EACCES));
            CHECK(encrypted_property("keystatus","unavailable"));
            CHECK(encrypted_key(62,"OZBOOT:gui-wrong.key",0));
            CHECK(encrypted_command("C:zfs change-key -o keylocation=prompt qemu_mirror/gui-encrypted <OZBOOT:gui-create.key")==0);
            CHECK(encrypted_key(63,NULL,0));
            CHECK(encrypted_key(62,"OZBOOT:gui-wrong.key",EACCES));
            CHECK(encrypted_property("keystatus","unavailable"));
            CHECK(encrypted_key(62,"OZBOOT:gui-create.key",0));
            CHECK(encrypted_property("keylocation","prompt"));
            CHECK(encrypted_property("keyformat",encrypted_format()));
            result_line("RESULT CLI key rotation PASS rotations=2 retired-key-denied=2 active-key-loaded=2\n");
        }
        CHECK(SystemTags((CONST_STRPTR)"C:zfs mount qemu_mirror/gui-encrypted",TAG_DONE)==0);
        const char *child="qemu_mirror/gui-encrypted/child";
        CHECK(encrypted_command("C:zfs create -u -o mountpoint=OZCHILD: qemu_mirror/gui-encrypted/child")==0);
        CHECK(encrypted_dataset_property(child,"encryptionroot","qemu_mirror/gui-encrypted"));
        CHECK(encrypted_command("C:zfs mount qemu_mirror/gui-encrypted/child")==0);
        BPTR childfile=Open((CONST_STRPTR)"OZCHILD:proof.txt",MODE_NEWFILE);CHECK(childfile);
        CHECK(Write(childfile,content,sizeof(content)-1)==sizeof(content)-1);CHECK(Flush(childfile));CHECK(Close(childfile));
        CHECK(encrypted_command("C:zfs unmount qemu_mirror/gui-encrypted/child")==0);
        char child_change[256];
        snprintf(child_change,sizeof(child_change),"C:zfs change-key -o keyformat=%s -o keylocation=prompt qemu_mirror/gui-encrypted/child <OZBOOT:gui-wrong.key",encrypted_format());
        CHECK(encrypted_command(child_change)==0);
        CHECK(encrypted_dataset_property(child,"encryptionroot",child));
        CHECK(encrypted_dataset_key(child,63,NULL,0));
        CHECK(encrypted_dataset_key(child,62,"OZBOOT:gui-create.key",EACCES));
        CHECK(encrypted_dataset_property(child,"keystatus","unavailable"));
        CHECK(encrypted_dataset_key(child,64,NULL,EACCES));
        CHECK(encrypted_dataset_property(child,"encryptionroot",child));
        CHECK(encrypted_dataset_key(child,62,"OZBOOT:gui-wrong.key",0));
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(crypt_reader)==((77UL<<16)|88));
        refused=encrypted_dataset_key(child,64,NULL,EPERM);
        CHECK(secLogoutA(crypt_logout)==previous_owner);
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        CHECK(refused);
        CHECK(encrypted_dataset_property(child,"encryptionroot",child));
        result_line("RESULT encrypted inheritance authorization PASS unprivileged-denied=1 root-unchanged=1\n");
#ifdef OZ_KEY_INHERIT_GUI
        result_line("RESULT encrypted inheritance GUI READY target=qemu_mirror/gui-encrypted/child options=inherit=on argument=empty; submit then close Manager\n");
        CHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",NP_StackSize,262144,TAG_DONE)==0);
#else
        CHECK(encrypted_dataset_key(child,64,NULL,0));
#endif
        CHECK(encrypted_dataset_property(child,"encryptionroot","qemu_mirror/gui-encrypted"));
        CHECK(encrypted_dataset_key(child,64,NULL,EINVAL));
        CHECK(encrypted_command("C:zfs mount qemu_mirror/gui-encrypted/child")==0);
        childfile=Open((CONST_STRPTR)"OZCHILD:proof.txt",MODE_OLDFILE);CHECK(childfile);
        char child_data[sizeof(content)]={0};
        CHECK(Read(childfile,child_data,sizeof(child_data))==sizeof(content)-1);
        CHECK(!memcmp(child_data,content,sizeof(content)-1));CHECK(Read(childfile,child_data,1)==0);CHECK(Close(childfile));
        CHECK(encrypted_command("C:zfs unmount qemu_mirror/gui-encrypted/child")==0);
        result_line("RESULT encrypted child inheritance PASS independent-root=1 wrong-key-denied=1 inherited-again=1 exact-content=1\n");
        CHECK(encrypted_command("C:zfs get keystatus,canmount,mountpoint qemu_mirror/gui-encrypted")==0);
        CHECK(encrypted_property("mounted","yes"));
        cryptfile=Open((CONST_STRPTR)"OZCRYPT:proof.txt",MODE_OLDFILE);CHECK(cryptfile);
        char retained[sizeof(content)]={0};
        CHECK(Read(cryptfile,retained,sizeof(retained))==sizeof(content)-1);
        CHECK(!memcmp(retained,content,sizeof(content)-1));
        CHECK(Read(cryptfile,retained,1)==0);CHECK(Close(cryptfile));
        result_line("RESULT encrypted pre-rotation payload PASS exact-content=1 eof=1\n");
        CHECK(SystemTags((CONST_STRPTR)"C:zfs unmount qemu_mirror/gui-encrypted",TAG_DONE)==0);
        CHECK(encrypted_key(63,NULL,0));CHECK(encrypted_property("keystatus","unavailable"));
        result_line("RESULT encrypted creation GUI WRITE PASS real-properties=1 handler-write=1 unloaded=1\n");
#endif
#ifdef OZ_STARTUP_COLD_CACHE
#ifdef OZ_STARTUP_DEFAULT_CACHE
        BPTR cache=Open((CONST_STRPTR)"ENVARC:OpenZFS.cache",MODE_OLDFILE);CHECK(cache);
#else
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set cachefile=/OZARCHIVE/zpool.cache qemu_mirror",TAG_DONE)==0);
        BPTR cache=Open((CONST_STRPTR)"OZBOOT:zpool.cache",MODE_OLDFILE);CHECK(cache);
#endif
        BPTR saved=Open((CONST_STRPTR)"OZBOOT:zpool.saved",MODE_NEWFILE);CHECK(saved);
        char block[8192];LONG n;unsigned copied=0;
        while((n=Read(cache,block,sizeof(block)))>0){
            CHECK(copied<16*1024*1024);CHECK(Write(saved,block,n)==n);copied+=n;
        }
        CHECK(n==0&&copied);CHECK(Close(cache));CHECK(Flush(saved));CHECK(Close(saved));
        CHECK(SetOwner((STRPTR)"OZBOOT:zpool.saved",secOWNER_SYSTEM));
        CHECK(SetProtection((CONST_STRPTR)"OZBOOT:zpool.saved",0));
#endif
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
#ifdef OZ_STARTUP_COLD_CACHE
#ifdef OZ_STARTUP_DEFAULT_CACHE
        BPTR removed=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",ACCESS_READ);
#else
        BPTR removed=Lock((CONST_STRPTR)"OZBOOT:zpool.cache",ACCESS_READ);
#endif
        if(removed)UnLock(removed);
        CHECK(!removed);
        const char *cache_written="RESULT native startup cold cache CREATE PASS serialized=1 saved=1\n";
        result_line(cache_written);
#ifdef OZ_STARTUP_DEFAULT_CACHE
        CHECK(SystemTags((CONST_STRPTR)"C:Copy OZBOOT:zpool.saved ENVARC:OpenZFS.cache",TAG_DONE)==0);
        const char *default_pass="RESULT native default cache PASS import=1 disable=1 reset=1 persisted=1\n";
        result_line(default_pass);
#endif
#endif
        /* Fixture selector is not authority: the actual startup command must
         * still validate protected IFF metadata and the discovered pool GUID. */
        BPTR hint=Open((CONST_STRPTR)"OZBOOT:fixture-guid",MODE_NEWFILE);CHECK(hint);
        CHECK(Write(hint,&record.guid,sizeof(record.guid))==sizeof(record.guid));CHECK(Close(hint));
    }else{
        BPTR hint=Open((CONST_STRPTR)"OZBOOT:fixture-guid",MODE_OLDFILE);CHECK(hint);
        CHECK(Read(hint,&record.guid,sizeof(record.guid))==sizeof(record.guid));CHECK(Close(hint));CHECK(record.guid);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)==1);
#ifdef OZ_STARTUP_DEFAULT_CACHE
        BPTR persisted=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);CHECK(persisted);UnLock(persisted);
        const char *not_imported="RESULT native default cache startup PASS not-auto-imported=1\n";
        result_line(not_imported);
#endif
#ifdef OZ_STARTUP_COLD_CACHE
        CHECK(SystemTags((CONST_STRPTR)"C:zpool import -N -c /OZARCHIVE/zpool.saved -o cachefile=none -o readonly=on qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -o value readonly qemu_mirror >RAM:cache-readonly",TAG_DONE)==0);
        BPTR prop=Open((CONST_STRPTR)"RAM:cache-readonly",MODE_OLDFILE);CHECK(prop);
        char value[16]={0};LONG n=Read(prop,value,sizeof(value)-1);CHECK(Close(prop));
        CHECK(n>=3&&value[0]=='o'&&value[1]=='n'&&(value[2]=='\n'||value[2]=='\r'));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
        const char *cache_read="RESULT native startup cold cache READ PASS imported=1 readonly=1 exported=1\n";
        result_line(cache_read);
#ifdef OZ_STARTUP_DEFAULT_CACHE
        BPTR stale=Lock((CONST_STRPTR)"ENVARC:OpenZFS.cache",SHARED_LOCK);
        if(stale)UnLock(stale);
        CHECK(!stale&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        const char *cleaned="RESULT native default cache cleanup PASS stale-default-removed=1 pool-readonly=1\n";
        result_line(cleaned);
#endif
#endif
    }
#endif
    if(create){
        operation_error=oz_startup_settings_apply_trusted(&record,1,&published);
        CHECK(!operation_error&&published==3);
    }else{
#ifndef OZ_STARTUP_COLD_COPY
        CHECK(oz_startup_settings_trusted(record.guid,&loaded)==ENOENT);
#endif
#ifdef OZ_STARTUP_COLD_POOL
        char command[128];snprintf(command,sizeof(command),"C:OpenZFSStartup %016llx RESTORE",(unsigned long long)record.guid);
#ifdef OZ_STARTUP_COLD_COPY
        /* PREPARE and the stock copy already ran before service startup. */
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list -H qemu_mirror",TAG_DONE)==1);
        CHECK(!oz_startup_settings_trusted(record.guid,&loaded));
        snprintf(command,sizeof(command),"C:OpenZFSStartup %016llx",(unsigned long long)record.guid);
#endif
        CHECK(SystemTags((CONST_STRPTR)command,TAG_DONE)==0);
#else
        CHECK(!oz_startup_settings_restore_trusted(record.guid,&published)&&published==1);
#endif
    }
    CHECK(!oz_startup_settings_trusted(record.guid,&loaded));
    CHECK(loaded.guid==record.guid&&loaded.flags==record.flags&&loaded.count==2&&
        !strcmp(loaded.pool,record.pool)&&!strcmp(loaded.paths[0],record.paths[0])&&
        !strcmp(loaded.paths[1],record.paths[1]));
#ifdef OZ_STARTUP_COLD_POOL
    if(!create){
#ifdef OZ_STARTUP_COLD_HANDLER
        struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ);
        int already=FindDosEntry(list,(CONST_STRPTR)"OZCOLD",LDF_DEVICES)!=NULL;
        UnLockDosList(LDF_DEVICES|LDF_READ);CHECK(!already);
        CHECK(SystemTags((CONST_STRPTR)"C:Mount OZBOOT:OZCOLD",TAG_DONE)==0);
        const char *handler_pass="RESULT native startup cold handler PASS saved-mountfile=1 imported-unmounted=1 activated=1\n";
        result_line(handler_pass);
#endif
        BPTR file=Open((CONST_STRPTR)"OZCOLD:hello.txt",MODE_OLDFILE);CHECK(file);
        const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        char data[128];LONG count=Read(file,data,sizeof(data));CHECK(Close(file));
        CHECK(count==sizeof(expected)-1&&!memcmp(data,expected,sizeof(expected)-1));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs create qemu_mirror/startup-refused",TAG_DONE)==1);
        CHECK(SystemTags((CONST_STRPTR)"C:zfs unmount qemu_mirror/files",TAG_DONE)==0);
#ifdef OZ_ENCRYPTED_CREATE_GUI
        CHECK(encrypted_property("keystatus","unavailable"));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs mount qemu_mirror/gui-encrypted",TAG_DONE)==1);
        CHECK(encrypted_key(62,"OZBOOT:gui-wrong.key",EACCES));
        CHECK(encrypted_property("keystatus","unavailable"));
        CHECK(encrypted_key(62,"OZBOOT:gui-create.key",0));
        CHECK(SystemTags((CONST_STRPTR)"C:zfs mount qemu_mirror/gui-encrypted",TAG_DONE)==0);
        BPTR cryptfile=Open((CONST_STRPTR)"OZCRYPT:proof.txt",MODE_OLDFILE);CHECK(cryptfile);
        const char crypt_expected[]="Encrypted GUI creation: persistent native AROS handler bytes.\n";
        char crypt_data[128];LONG crypt_count=Read(cryptfile,crypt_data,sizeof(crypt_data));CHECK(Close(cryptfile));
        CHECK(crypt_count==sizeof(crypt_expected)-1&&!memcmp(crypt_data,crypt_expected,sizeof(crypt_expected)-1));
        CHECK(encrypted_dataset_property("qemu_mirror/gui-encrypted/child","encryptionroot","qemu_mirror/gui-encrypted"));
        CHECK(encrypted_dataset_key("qemu_mirror/gui-encrypted/child",64,NULL,EROFS));
        CHECK(encrypted_dataset_property("qemu_mirror/gui-encrypted/child","encryptionroot","qemu_mirror/gui-encrypted"));
        result_line("RESULT encrypted inheritance readonly PASS denied=1 root-unchanged=1\n");
        CHECK(encrypted_command("C:zfs mount qemu_mirror/gui-encrypted/child")==0);
        cryptfile=Open((CONST_STRPTR)"OZCHILD:proof.txt",MODE_OLDFILE);CHECK(cryptfile);
        crypt_count=Read(cryptfile,crypt_data,sizeof(crypt_data));
        CHECK(crypt_count==sizeof(crypt_expected)-1&&!memcmp(crypt_data,crypt_expected,sizeof(crypt_expected)-1));
        CHECK(Read(cryptfile,crypt_data,1)==0);CHECK(Close(cryptfile));
        CHECK(encrypted_command("C:zfs unmount qemu_mirror/gui-encrypted/child")==0);
        result_line("RESULT encrypted child inheritance COLD PASS parent-key-only=1 exact-content=1 eof=1\n");
        CHECK(SystemTags((CONST_STRPTR)"C:zfs unmount qemu_mirror/gui-encrypted",TAG_DONE)==0);
        CHECK(encrypted_key(63,NULL,0));
        result_line("RESULT encrypted creation GUI COLD PASS locked=1 mount-denied=1 wrong-key-denied=1 load=1 exact-handler-read=1 unloaded=1\n");
#endif
#ifdef OZ_STARTUP_DEFAULT_CACHE
        /* Last mutation on the disposable host archive: its SFS security
         * adapter currently refuses DeleteFile on soft links (DOS 233).
         * Retain the link/target as evidence, do not weaken protection to
         * clean them up. The pool is readonly, and remains so throughout. */
        const char link_marker[]="untrusted-cache-target-must-survive";
        BPTR link_target=Open((CONST_STRPTR)"OZBOOT:cache-link-target",MODE_NEWFILE);CHECK(link_target);
        CHECK(Write(link_target,link_marker,sizeof(link_marker))==sizeof(link_marker));CHECK(Close(link_target));
        CHECK(MakeLink((CONST_STRPTR)"ENVARC:OpenZFS.cache",(SIPTR)"OZBOOT:cache-link-target",LINK_SOFT));
        BPTR link_error=Open((CONST_STRPTR)"RAM:cache-softlink-error",MODE_NEWFILE);CHECK(link_error);
        LONG link_rc=SystemTags((CONST_STRPTR)"C:zpool set cachefile= qemu_mirror",
            SYS_Output,link_error,SYS_Error,link_error,TAG_DONE);
        CHECK(Flush(link_error));CHECK(Close(link_error));CHECK(link_rc==1);
        link_error=Open((CONST_STRPTR)"RAM:cache-softlink-error",MODE_OLDFILE);CHECK(link_error);
        char link_message[512]={0};LONG message_bytes=Read(link_error,link_message,sizeof(link_message)-1);
        CHECK(Close(link_error));CHECK(message_bytes>0&&strstr(link_message,"symbolic link rejected"));
        link_target=Open((CONST_STRPTR)"OZBOOT:cache-link-target",MODE_OLDFILE);CHECK(link_target);
        char link_data[sizeof(link_marker)+1];LONG link_bytes=Read(link_target,link_data,sizeof(link_data));
        CHECK(Close(link_target));CHECK(link_bytes==sizeof(link_marker)&&!memcmp(link_data,link_marker,sizeof(link_marker)));
        result_line("RESULT native default cache softlink PASS rejected=1 target-unchanged=1 retained=1 ELOOP=1\n");
#endif
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
        const char *pass="RESULT native startup cold pool PASS persisted=1 restored=1 imported=1 mounted=1 exact-DOS-read=1 readonly-denied=1\n";
        result_line(pass);
#ifdef OZ_STARTUP_COLD_COPY
        const char *copy_pass="RESULT native startup cold copy PASS secure-publish=1 stock-copy=1 ENV-import=1\n";
        result_line(copy_pass);
#endif
    }
#endif
    CHECK(AssignLock((CONST_STRPTR)"ENVARC",0));
#ifdef OZ_STARTUP_DEFAULT_CACHE
    /* The service intentionally pins its configured cache directory. Flush
     * this private archive rather than trying to unmount its live parent. */
    BPTR archive_flush=Lock((CONST_STRPTR)"OZBOOT:",SHARED_LOCK);CHECK(archive_flush);
    struct FileLock *archive_native=BADDR(archive_flush);
    LONG flushed=DoPkt(archive_native->fl_Task,ACTION_FLUSH,0,0,0,0,0);
    UnLock(archive_flush);CHECK(flushed);
#else
    CHECK(Inhibit((CONST_STRPTR)"OZBOOT:",TRUE));
#endif
done:
#ifdef OZ_STARTUP_DEFAULT_CACHE
    if(error){
        const char *logs[]={"RAM:cache-default-command","T:service.log"};
        for(unsigned i=0;i<2;i++){
            BPTR log=Open((CONST_STRPTR)logs[i],MODE_OLDFILE);
            if(log){char text[1024];LONG n;unsigned total=0;
                for(const char *p=logs[i];*p;p++)RawPutChar(*p);
                RawPutChar('\n');
                while(total<16384&&(n=Read(log,text,sizeof(text)))>0){
                    for(LONG k=0;k<n;k++)RawPutChar(text[k]);
                    total+=n;
                }
                Close(log);
            }
        }
    }
#endif
    {char line[200];snprintf(line,sizeof(line),"RESULT native startup cold %s %s line=%d ioerr=%ld operation=%d published=%u\n",
        create?"CREATE":"READ",error?"FAIL":"PASS",error,(long)failure_ioerr,operation_error,published);
        result_line(line);}
    if(secBase)CloseLibrary(secBase);
    process->pr_WindowPtr=oldwindow;return error?20:0;
}
