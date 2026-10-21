/* SPDX-License-Identifier: CDDL-1.0 */
#include "preferences.h"
#include "startup-settings.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static void report(const char *s){while(*s)RawPutChar(*s++);}
static int launch(unsigned phase){
    char line[100];snprintf(line,sizeof(line),"Native preferences GUI phase %u ready\n",phase);report(line);
    return SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE);
}
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
int main(int argc,char **argv){
    int error=0,recovered=0;struct oz_preferences p,a;BPTR file=0,lock=0;
    struct Library *secBase=NULL;
    if(argc==2&&!strcmp(argv[1],"SIMPLE")){
        CHECK(oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)==ENOENT);
        CHECK(oz_preferences_load("ENVARC:OpenZFSManager.prefs",&p,NULL)==ENOENT);
        /* Fresh Simple view: switch Advanced, Use, close. */
        CHECK(!launch(6));
        CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)&&p.view_mode==OZ_UI_ADVANCED);
        CHECK(oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)==ENOENT);
        report("Native Simple GUI Advanced Use session only PASS\n");
        /* Reopened Advanced: switch Simple, Save, close. */
        CHECK(!launch(7));
        CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)&&p.view_mode==OZ_UI_SIMPLE);
        CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.view_mode==OZ_UI_SIMPLE);
        CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSManager.prefs"));
        report("Native Simple GUI Simple Save both stores PASS\n");
        /* Archive fallback restores Simple. Inspect and close unchanged. */
        CHECK(!launch(8));
        CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.view_mode==OZ_UI_SIMPLE);
        report("Native Simple GUI archive fallback PASS\n");
        goto done;
    }
    if(argc==2&&!strcmp(argv[1],"STARTUP")){
        static struct oz_startup_settings initial,loaded;
        secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase&&secIsConfigured());
        struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        CHECK(secLoginA(login)==secOWNER_SYSTEM);
        struct TagItem permissive[]={{secT_DefProtection,FIBF_OTR_READ|FIBF_OTR_WRITE|FIBF_OTR_DELETE},{TAG_DONE,0}};
        CHECK(secSetDefProtectionA(permissive));
        initial.guid=UINT64_C(0x0123456789abcdef);initial.count=2;initial.flags=OZ_STARTUP_READONLY;
        strcpy(initial.pool,"gui_pool");strcpy(initial.paths[0],"RAM:member-one");strcpy(initial.paths[1],"RAM:member-two");
        unsigned published=0;
        CHECK(!oz_startup_settings_apply_trusted(&initial,0,&published)&&published==1);
        /* Open Startup, enter GUID, Load, enable, Save for boot, close both. */
        CHECK(!launch(0));
        CHECK(!oz_startup_settings_current(initial.guid,&loaded,&recovered)&&!recovered);
        CHECK(loaded.flags==5&&loaded.count==2&&!strcmp(loaded.pool,"gui_pool"));
        CHECK(!strcmp(loaded.paths[0],initial.paths[0])&&!strcmp(loaded.paths[1],initial.paths[1]));
        CHECK(!oz_startup_settings_load("ENVARC:OpenZFS-0123456789abcdef.prefs",&loaded,&recovered));
        CHECK(loaded.guid==initial.guid&&loaded.flags==5&&loaded.count==2);
        CHECK(!oz_startup_settings_trusted(initial.guid,&loaded));
        lock=Lock((CONST_STRPTR)"ENVARC:OpenZFS-0123456789abcdef.prefs",SHARED_LOCK);CHECK(lock);
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
        int secure=fib&&Examine(lock,fib)&&fib->fib_OwnerUID==0xffff&&
            !(fib->fib_Protection&(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE));
        if(fib)FreeDosObject(DOS_FIB,fib);
        UnLock(lock);lock=0;CHECK(secure);
        report("Native startup GUI secure Save for boot PASS\n");
        report("Native startup GUI Load and Save actual records PASS\n");goto done;
    }
    if(argc==2&&!strcmp(argv[1],"FAILURES"))goto failure_cases;
    /* A fresh RAM-only session. Phase 1: select Encryption, Use, close. */
    CHECK(oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)==ENOENT);
    CHECK(oz_preferences_load("ENVARC:OpenZFSManager.prefs",&p,NULL)==ENOENT);
    CHECK(!launch(1));
    CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&p,&recovered)&&!recovered&&p.category==5);
    CHECK(oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)==ENOENT);
    report("Native preferences GUI Use persisted session only PASS\n");
    /* Phase 2: loaded Encryption, Save, close. */
    CHECK(!launch(2));
    CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.category==5);
    CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)&&p.category==5);
    CHECK(a.width==p.width&&a.height==p.height);
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSManager.prefs"));
    report("Native preferences GUI Save persisted both PASS\n");
    /* Phase 3: ENVARC fallback, select Datasets, Use, close. */
    CHECK(!launch(3));
    CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&p,NULL)&&p.category==2);
    CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.category==5);
    report("Native preferences GUI archive fallback and session override PASS\n");
    goto done;
failure_cases:
    p=(struct oz_preferences){5,780,550,OZ_UI_ADVANCED};int published=0;
    CHECK(!oz_preferences_store("ENVARC:OpenZFSManager.prefs",&p,&published)&&published);
    file=Open((CONST_STRPTR)"ENV:OpenZFSManager.prefs",MODE_NEWFILE);CHECK(file);
    CHECK(Write(file,"invalid",7)==7);CHECK(Close(file));file=0;
    /* Phase 4: defaults plus warning. Close without Use or Save. */
    CHECK(!launch(4));
    file=Open((CONST_STRPTR)"ENV:OpenZFSManager.prefs",MODE_OLDFILE);CHECK(file);
    char bytes[8]={0};CHECK(Read(file,bytes,sizeof(bytes))==7&&!strcmp(bytes,"invalid"));
    CHECK(Close(file));file=0;
    CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.category==5);
    report("Native preferences GUI invalid input left untouched PASS\n");
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFSManager.prefs"));
    lock=CreateDir((CONST_STRPTR)"ENV:OpenZFSManager.prefs.txn");CHECK(lock);UnLock(lock);lock=0;
    CHECK(!oz_preferences_store("ENV:OpenZFSManager.prefs.txn/previous",&p,&published)&&published);
    /* Phase 5: backup loaded and warning. Try Use; busy transaction must
     * remain intact. Close after observing the failed-save feedback. */
    CHECK(!launch(5));
    CHECK(!oz_preferences_load("ENV:OpenZFSManager.prefs",&a,&recovered)&&recovered&&a.category==5);
    lock=Lock((CONST_STRPTR)"ENV:OpenZFSManager.prefs",SHARED_LOCK);
    CHECK(!lock&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    CHECK(!oz_preferences_load("ENVARC:OpenZFSManager.prefs",&a,NULL)&&a.category==5);
    report("Native preferences GUI backup loaded without overwriting transaction PASS\n");
done:
    if(secBase)CloseLibrary(secBase);
    if(file)Close(file);
    if(lock)UnLock(lock);
    {char line[120];snprintf(line,sizeof(line),"Native preferences GUI line=%d\nRESULT native preferences GUI %s\n",error,error?"FAIL":"PASS");report(line);}
    return error?20:0;
}
