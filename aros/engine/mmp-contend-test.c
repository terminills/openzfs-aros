/* SPDX-License-Identifier: CDDL-1.0 */
/* Two private guests, distinct host IDs, host-created shared disposable disks. */
#include "hostid-record.h"
#include "../ui/support-api.h"
#include "../ui/admission-api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/kernel.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <stdlib.h>
#ifndef OZ_MMP_TYPED_CYCLES
#define OZ_MMP_TYPED_CYCLES 1
#endif
int oz_mmp_raw_property(const char *,int *);
APTR KernelBase;
static void report(const char *s){KrnPrintf("%s",s);}
static int property_is(const char *expected)
{
    LONG result=SystemTags((CONST_STRPTR)"C:zpool get -H -o value multihost qemu_mirror >RAM:mmp-readback",TAG_DONE);
    char diagnostic[1024];
    snprintf(diagnostic,sizeof(diagnostic),"MMP readback command=%ld ioerr=%ld\n",(long)result,(long)IoErr());report(diagnostic);
    if(result)return 0;
    BPTR file=Open((CONST_STRPTR)"RAM:mmp-readback",MODE_OLDFILE);
    if(!file){report("MMP readback open failed\n");return 0;}
    char value[768]={0};LONG count=Read(file,value,sizeof(value)-1);
    if(!Close(file))return 0;
    snprintf(diagnostic,sizeof(diagnostic),"MMP readback count=%ld value=[%s] expected=[%s]\n",(long)count,value,expected);report(diagnostic);
    return count==(LONG)strlen(expected)&&!strcmp(value,expected);
}
static int typed_property(const char *value)
{
    static struct oz_message request,response;
    memset(&request,0,sizeof(request));
    request.magic=OZ_MAGIC;request.version=OZ_VERSION;
    request.msg.mn_Length=sizeof(request);request.operation=32;request.flags=OZ_CONFIRMED;
    strcpy(request.target,"qemu_mirror");
    snprintf(request.argument,sizeof(request.argument),"multihost=%s",value);
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,&request,&ticket);
    if(!error){
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,&response);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){
            report(response.detail);report("\n");
            if(response.status!=OZ_OK){
                const char *number=strstr(response.detail,"errno=");
                error=number?atoi(number+6):EIO;
                if(!error)error=EIO;
            }
        }
    }
    if(ticket.number){int e=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=e;}
    return error;
}
int main(int argc,char **argv)
{
    int stall=argc==2&&!strcmp(argv[1],"STALL");
    int failure=0,owner=stall||(argc==2&&!strcmp(argv[1],"OWNER"));
    int recovery=argc==2&&!strcmp(argv[1],"RECOVERY");
    int nohost=argc==2&&!strcmp(argv[1],"NOHOST");
    int zeroset=argc==2&&!strcmp(argv[1],"ZEROSET");
    struct Library *secBase=NULL;
    KernelBase=OpenResource((CONST_STRPTR)"kernel.resource");if(!KernelBase)return 20;
#define CHECK(x) do {if(!(x)){failure=__LINE__;goto done;}}while(0)
    CHECK(argc==2&&(owner||recovery||nohost||zeroset||!strcmp(argv[1],"CONTENDER")));
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    CHECK(secLoginA(login)==secOWNER_SYSTEM);
    CHECK(SetOwner((STRPTR)"ENV:",secOWNER_SYSTEM));CHECK(SetProtection((CONST_STRPTR)"ENV:",0));
    uint32_t identity=(nohost||zeroset)?0:owner?0x51a20519:0x51a20520;
    if(identity){
    BPTR pref=Open((CONST_STRPTR)"ENV:OpenZFSHost.prefs",MODE_NEWFILE);CHECK(pref);
    CHECK(!oz_hostid_record_write(pref,identity));CHECK(Close(pref));
    CHECK(SetOwner((STRPTR)"ENV:OpenZFSHost.prefs",secOWNER_SYSTEM));
    CHECK(SetProtection((CONST_STRPTR)"ENV:OpenZFSHost.prefs",0));
    }
    CHECK(SystemTags((CONST_STRPTR)"Run >T:service.log C:OpenZFSServiceNative WRITABLE",TAG_DONE)==0);
    int ready=0;for(unsigned i=0;i<1500&&!ready;i++){
        Forbid();ready=FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();if(!ready)Delay(1);
    }
    CHECK(ready);ULONG actual=0;CHECK(!oz_hostid_query(OZ_ADMISSION_PORT,&actual)&&actual==identity);
    BPTR output=Open((CONST_STRPTR)"RAM:import-result",MODE_NEWFILE);CHECK(output);
    LONG imported=SystemTags((CONST_STRPTR)"C:zpool import -f -N -o cachefile=none -d /AROS/ata.device/1/partition/0 -d /AROS/ata.device/3/partition/0 qemu_mirror",
        SYS_Output,output,SYS_Error,output,TAG_DONE);
    CHECK(Flush(output));CHECK(Close(output));
    output=Open((CONST_STRPTR)"RAM:import-result",MODE_OLDFILE);CHECK(output);
    char diagnostic[4096]={0};LONG n=Read(output,diagnostic,sizeof(diagnostic)-1);CHECK(Close(output));CHECK(n>=0);
    report(diagnostic);
    if(owner){
        CHECK(imported==0);
        for(unsigned cycle=0;cycle<OZ_MMP_TYPED_CYCLES;cycle++){
            char progress[80];snprintf(progress,sizeof(progress),"MMP typed cycle %u/%u\n",cycle+1,OZ_MMP_TYPED_CYCLES);report(progress);
            CHECK(typed_property("invalid")==EINVAL);
            CHECK(!typed_property("on"));
            CHECK(property_is("on\n"));
            CHECK(!typed_property("off"));
            CHECK(property_is("off\n"));
        }
        report("RESULT native MMP TYPED PASS on=1 off=1 invalid-refused=1 cli-readback=1\n");
        CHECK(SystemTags((CONST_STRPTR)"C:zpool set multihost=on qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool sync qemu_mirror",TAG_DONE)==0);
        Delay(250);report("RESULT native MMP OWNER READY hostid=51a20519\n");
        if(stall){
            for(unsigned i=0;i<24;i++){
                Delay(250);
                CHECK(SystemTags((CONST_STRPTR)"C:zpool status qemu_mirror >RAM:mmp-status",TAG_DONE)==0);
                BPTR status=Open((CONST_STRPTR)"RAM:mmp-status",MODE_OLDFILE);CHECK(status);
                char text[4096]={0};LONG count=Read(status,text,sizeof(text)-1);CHECK(Close(status));CHECK(count>0);
                if(strstr(text,"SUSPENDED")){
                    CHECK(strstr(text,"pool is suspended because multihost writes failed or were delayed"));
                    report(text);
                    report("RESULT native MMP STALL PASS pool-suspended=1\n");
                    /* Host removes throttles after observing the marker.
                     * Never count this delay itself as recovery evidence. */
                    Delay(500);
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool clear qemu_mirror",TAG_DONE)==0);
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool sync qemu_mirror",TAG_DONE)==0);
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool status qemu_mirror >RAM:mmp-recovered",TAG_DONE)==0);
                    status=Open((CONST_STRPTR)"RAM:mmp-recovered",MODE_OLDFILE);CHECK(status);
                    memset(text,0,sizeof(text));count=Read(status,text,sizeof(text)-1);CHECK(Close(status));CHECK(count>0);
                    report(text);
                    CHECK(strstr(text,"state: ONLINE")&&!strstr(text,"SUSPENDED"));
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -o value multihost qemu_mirror >RAM:mmp-enabled",TAG_DONE)==0);
                    status=Open((CONST_STRPTR)"RAM:mmp-enabled",MODE_OLDFILE);CHECK(status);
                    char property[16]={0};count=Read(status,property,sizeof(property)-1);CHECK(Close(status));
                    CHECK(count==3&&!strcmp(property,"on\n"));
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
                    CHECK(SystemTags((CONST_STRPTR)"C:zpool list qemu_mirror",TAG_DONE)==1);
                    report("RESULT native MMP STALL RECOVERY PASS cleared=1 synced=1 online=1 multihost=on exported=1\n");
                    goto done;
                }
                report("MMP stall observer alive\n");
            }
            CHECK(0);
        }
        for(unsigned i=0;i<600;i++){
            Delay(50);
            char beat[96];snprintf(beat,sizeof(beat),"MMP owner alive %u\n",i);report(beat);
        }
        CHECK(0); /* Host runner must finish before this diagnostic deadline. */
    }else if(zeroset){
        CHECK(imported==0);
        CHECK(typed_property("on")==ENOTSUP);
        int engine_error=0;
        CHECK(!oz_mmp_raw_property("qemu_mirror",&engine_error));
        CHECK(engine_error==ENOTSUP);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool get -H -o value multihost qemu_mirror >RAM:mmp-property",TAG_DONE)==0);
        BPTR property=Open((CONST_STRPTR)"RAM:mmp-property",MODE_OLDFILE);CHECK(property);
        char value[32]={0};LONG count=Read(property,value,sizeof(value)-1);CHECK(Close(property));
        CHECK(count==4&&!strcmp(value,"off\n"));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
        report("RESULT native MMP ZEROSET PASS transport=0 engine=ENOTSUP unchanged=off\n");
    }else if(nohost){
        CHECK(imported==1);
        CHECK(strstr(diagnostic,"hostid is not set"));
        CHECK(strstr(diagnostic,"C:OpenZFSHostIdentity <8-digit-hex-ID> SAVE"));
        CHECK(strstr(diagnostic,"as SYSTEM")&&strstr(diagnostic,"Restart OpenZFS"));
        CHECK(!strstr(diagnostic,"zgenhostid"));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list qemu_mirror",TAG_DONE)==1);
        report("RESULT native MMP NOHOST PASS identity=0 import-refused=1 absent-locally=1\n");
    }else if(recovery){
        CHECK(imported==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool sync qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool export qemu_mirror",TAG_DONE)==0);
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list qemu_mirror",TAG_DONE)==1);
        report("RESULT native MMP RECOVERY PASS stopped-owner=1 imported=1 synced=1 exported=1\n");
    }else{
        CHECK(imported==1);
        CHECK(strstr(diagnostic,"pool is imported on host")&&strstr(diagnostic,"hostid=51a20519"));
        CHECK(SystemTags((CONST_STRPTR)"C:zpool list qemu_mirror",TAG_DONE)==1);
        report("RESULT native MMP CONTENDER PASS active-owner=1 absent-locally=1 distinct-hostid=1\n");
    }
done:
    if(failure){char message[128];snprintf(message,sizeof(message),"RESULT native MMP %s FAIL line=%d ioerr=%ld\n",owner?"OWNER":zeroset?"ZEROSET":nohost?"NOHOST":recovery?"RECOVERY":"CONTENDER",failure,(long)IoErr());report(message);}
    if(secBase)CloseLibrary(secBase);
    return failure?20:0;
}
