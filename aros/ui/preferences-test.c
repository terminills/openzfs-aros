/* SPDX-License-Identifier: CDDL-1.0 */
#include "preferences.h"
#include "startup-settings.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
static const unsigned char golden[46]={
    'F','O','R','M',0,0,0,38,'P','R','E','F',
    'P','R','H','D',0,0,0,6,0,0,0,0,0,0,
    'O','Z','U','I',0,0,0,12,0,2,0,0,0,5,0,0,4,0,3,0};
static int read_case(const unsigned char *bytes,unsigned length,int accepted)
{
    BPTR f=Open((CONST_STRPTR)"RAM:OpenZFS-test.prefs",MODE_NEWFILE);
    if(!f)return 0;
    int ok=Write(f,bytes,length)==(LONG)length;
    if(!Close(f))ok=0;
    if(!ok)return 0;
    f=Open((CONST_STRPTR)"RAM:OpenZFS-test.prefs",MODE_OLDFILE);
    if(!f)return 0;
    struct oz_preferences p={7,800,600},before=p;
    int rc=oz_preferences_read(f,&p);
    if(!Close(f))return 0;
    return accepted?(!rc&&p.category==5&&p.width==1024&&p.height==768&&
        p.view_mode==(bytes[35]==1?OZ_UI_ADVANCED:bytes[41])):
        (rc!=0&&!memcmp(&p,&before,sizeof(p)));
}
int main(void)
{
    int error=0,logged=0;BPTR file=0;unsigned char bytes[128];
    struct Library *secBase=NULL;
    struct oz_preferences p;
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(!oz_startup_settings_test());
    oz_preferences_default(&p);CHECK(p.category==0&&p.width==780&&p.height==550&&p.view_mode==OZ_UI_SIMPLE);
    p=(struct oz_preferences){5,1024,768};
    file=Open((CONST_STRPTR)"RAM:OpenZFS-test.prefs",MODE_NEWFILE);CHECK(file);
    CHECK(!oz_preferences_write(file,&p));CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"RAM:OpenZFS-test.prefs",MODE_OLDFILE);CHECK(file);
    CHECK(Read(file,bytes,sizeof(bytes))==sizeof(golden));
    CHECK(!memcmp(bytes,golden,sizeof(golden)));CHECK(Close(file));file=0;
    CHECK(read_case(golden,sizeof(golden),1));
    memcpy(bytes,golden,46);bytes[35]=1;CHECK(read_case(bytes,46,1));
    memcpy(bytes,golden,46);bytes[41]=1;CHECK(read_case(bytes,46,1));
    bytes[41]=2;CHECK(read_case(bytes,46,0));
    for(unsigned n=0;n<sizeof(golden);n++)CHECK(read_case(golden,n,0));
    static const unsigned corrupt[]={0,7,8,16,19,20,21,22,26,33,34,35,36,38,40,42,44};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);i++){
        memcpy(bytes,golden,sizeof(golden));bytes[corrupt[i]]^=0x80;
        CHECK(read_case(bytes,sizeof(golden),0));
    }
    memcpy(bytes,golden,sizeof(golden));bytes[39]=8;CHECK(read_case(bytes,46,0));
    memcpy(bytes,golden,sizeof(golden));bytes[42]=0;CHECK(read_case(bytes,46,0));
    memcpy(bytes,golden,sizeof(golden));bytes[44]=0;CHECK(read_case(bytes,46,0));
    /* An extra complete file or trailing data must not be silently ignored. */
    memcpy(bytes,golden,46);bytes[46]=0;CHECK(read_case(bytes,47,0));
    memcpy(bytes+46,golden,46);CHECK(read_case(bytes,92,0));
    /* Duplicate known chunks cannot override validated settings. */
    memcpy(bytes,golden,46);memcpy(bytes+46,golden+12,14);bytes[7]=52;
    CHECK(read_case(bytes,60,0));
    memcpy(bytes,golden,46);memcpy(bytes+46,golden+26,20);bytes[7]=58;
    CHECK(read_case(bytes,66,0));
    /* Bounded unknown leaf chunk: forward-compatible, with IFF odd padding. */
    memcpy(bytes,golden,46);memcpy(bytes+46,"EXTN\0\0\0\1x\0",10);bytes[7]=48;
    CHECK(read_case(bytes,56,1));
    file=Open((CONST_STRPTR)"RAM:OpenZFS-invalid.prefs",MODE_NEWFILE);CHECK(file);
    p.category=8;CHECK(oz_preferences_write(file,&p)==EINVAL);
    CHECK(Seek(file,0,OFFSET_CURRENT)==0);CHECK(Close(file));file=0;
    const char *path="RAM:OpenZFS-managed.prefs";
    int published=0,recovered=0;struct oz_preferences loaded;
    p=(struct oz_preferences){5,1024,768};
    CHECK(!oz_preferences_store(path,&p,&published)&&published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!recovered&&!memcmp(&p,&loaded,sizeof(p)));
    p=(struct oz_preferences){2,800,600,OZ_UI_ADVANCED};published=0;
    CHECK(!oz_preferences_store(path,&p,&published)&&published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!recovered&&!memcmp(&p,&loaded,sizeof(p)));
    CHECK(SetProtection((CONST_STRPTR)path,FIBF_DELETE));
    struct oz_preferences changed={3,900,700};published=0;
    CHECK(oz_preferences_store(path,&changed,&published)==EACCES&&!published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!recovered&&!memcmp(&p,&loaded,sizeof(p)));
    CHECK(SetProtection((CONST_STRPTR)path,0));
    CHECK(SetProtection((CONST_STRPTR)path,FIBF_WRITE));
    CHECK(oz_preferences_store(path,&changed,&published)==EACCES&&!published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!memcmp(&p,&loaded,sizeof(p)));
    CHECK(SetProtection((CONST_STRPTR)path,0));
    changed.category=8;published=0;
    CHECK(oz_preferences_store(path,&changed,&published)==EINVAL&&!published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!memcmp(&p,&loaded,sizeof(p)));
    /* An occupied transaction is not ours to overwrite or clean up. */
    BPTR transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-managed.prefs.txn");CHECK(transaction);
    UnLock(transaction);published=0;
    CHECK(oz_preferences_store(path,&p,&published)==EBUSY&&!published);
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&!recovered);
    /* Model an interruption between the two renames. Read-only fallback
     * recovers the prior valid settings; a save must still refuse collision. */
    CHECK(Rename((CONST_STRPTR)path,(CONST_STRPTR)"RAM:OpenZFS-managed.prefs.txn/previous"));
    CHECK(!oz_preferences_load(path,&loaded,&recovered)&&recovered&&!memcmp(&p,&loaded,sizeof(p)));
    CHECK(oz_preferences_store(path,&p,&published)==EBUSY&&!published);
    CHECK(Rename((CONST_STRPTR)"RAM:OpenZFS-managed.prefs.txn/previous",(CONST_STRPTR)path));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:OpenZFS-managed.prefs.txn"));
    transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-directory.prefs");CHECK(transaction);UnLock(transaction);
    CHECK(oz_preferences_store("RAM:OpenZFS-directory.prefs",&p,&published)==EINVAL&&!published);
    CHECK(DeleteFile((CONST_STRPTR)"RAM:OpenZFS-directory.prefs"));
    /* Exercise actual caller credentials, not only owner protection bits. */
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
    struct TagItem root_tags[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem reader_tags[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout_tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    ULONG previous_owner=secGetTaskOwner(FindTask(NULL));
    CHECK(secLoginA(root_tags)==secOWNER_SYSTEM);logged=1;
    static struct oz_startup_settings boot,boot_read;
    const char *boot_path="ENV:OpenZFS-1111222233334444.prefs";
    boot.guid=UINT64_C(0x1111222233334444);boot.flags=5;boot.count=1;
    strcpy(boot.pool,"trusted_boot");strcpy(boot.paths[0],"RAM:member");
    unsigned boot_published=0;
    CHECK(!oz_startup_settings_apply(&boot,0,&boot_published)&&boot_published==1);
    CHECK(secSetProtection((CONST_STRPTR)boot_path,FIBF_OTR_READ));
    CHECK(!oz_startup_settings_trusted(boot.guid,&boot_read)&&boot_read.guid==boot.guid);
    static const LONG unsafe[]={FIBF_GRP_WRITE,FIBF_GRP_DELETE,FIBF_OTR_WRITE,FIBF_OTR_DELETE};
    for(unsigned i=0;i<sizeof(unsafe)/sizeof(unsafe[0]);i++){
        CHECK(secSetProtection((CONST_STRPTR)boot_path,unsafe[i]));boot_read.flags=99;
        CHECK(oz_startup_settings_trusted(boot.guid,&boot_read)==EACCES&&boot_read.flags==99);
    }
    CHECK(secSetProtection((CONST_STRPTR)boot_path,FIBF_OTR_READ));
    CHECK(SetOwner((STRPTR)boot_path,(77UL<<16)|88));
    CHECK(oz_startup_settings_trusted(boot.guid,&boot_read)==EACCES);
    ULONG saved_default=secGetDefProtection(FindTask(NULL));
    CHECK(oz_startup_settings_apply_trusted(&boot,0,&boot_published)==EACCES&&!boot_published);
    CHECK(secGetDefProtection(FindTask(NULL))==saved_default);
    CHECK(SetOwner((STRPTR)boot_path,secOWNER_SYSTEM));
    transaction=CreateDir((CONST_STRPTR)"ENV:OpenZFS-1111222233334444.prefs.txn");CHECK(transaction);UnLock(transaction);
    CHECK(oz_startup_settings_trusted(boot.guid,&boot_read)==EBUSY);
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-1111222233334444.prefs.txn"));
    CHECK(secSetProtection((CONST_STRPTR)boot_path,FIBF_OTR_WRITE));
    CHECK(oz_startup_settings_apply_trusted(&boot,0,&boot_published)==EACCES&&!boot_published);
    CHECK(secSetProtection((CONST_STRPTR)boot_path,FIBF_OTR_READ));
    CHECK(!oz_startup_settings_apply_trusted(&boot,1,&boot_published)&&boot_published==3);
    CHECK(!oz_startup_settings_trusted(boot.guid,&boot_read));
    boot.guid++;
    struct TagItem permissive[]={{secT_DefProtection,FIBF_OTR_WRITE|FIBF_OTR_READ|FIBF_OTR_DELETE},{TAG_DONE,0}};
    CHECK(secSetDefProtectionA(permissive));
    CHECK(!oz_startup_settings_apply_trusted(&boot,1,&boot_published)&&boot_published==3);
    CHECK(secGetDefProtection(FindTask(NULL))==permissive[0].ti_Data);
    CHECK(!oz_startup_settings_trusted(boot.guid,&boot_read));
    /* Real AROS Startup-Sequence uses Copy ALL NOPRO. Verify that a valid
     * archived record cannot acquire trust merely by being copied to ENV. */
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-1111222233334445.prefs"));
    CHECK(SystemTags((CONST_STRPTR)"C:Copy ENVARC:OpenZFS-1111222233334445.prefs ENV:OpenZFS-1111222233334445.prefs NOPRO NOREQ QUIET",TAG_DONE)==0);
    boot_read.flags=99;
    CHECK(oz_startup_settings_trusted(boot.guid,&boot_read)==EACCES&&boot_read.flags==99);
    CHECK(oz_startup_settings_restore_trusted(boot.guid,&boot_published)==EACCES&&!boot_published);
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-1111222233334445.prefs"));
    CHECK(!oz_startup_settings_restore_trusted(boot.guid,&boot_published)&&boot_published==1);
    CHECK(secGetDefProtection(FindTask(NULL))==permissive[0].ti_Data);
    CHECK(!oz_startup_settings_trusted(boot.guid,&boot_read)&&boot_read.guid==boot.guid&&boot_read.flags==boot.flags);
    CHECK(secSetProtection((CONST_STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs",FIBF_OTR_WRITE));
    CHECK(oz_startup_settings_restore_trusted(boot.guid,&boot_published)==EACCES&&!boot_published);
    CHECK(!oz_startup_settings_trusted(boot.guid,&boot_read));
    CHECK(secSetProtection((CONST_STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs",0));
    CHECK(SetOwner((STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs",(77UL<<16)|88));
    CHECK(oz_startup_settings_restore_trusted(boot.guid,&boot_published)==EACCES&&!boot_published);
    CHECK(SetOwner((STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs",secOWNER_SYSTEM));
    transaction=CreateDir((CONST_STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs.txn");CHECK(transaction);UnLock(transaction);
    CHECK(oz_startup_settings_restore_trusted(boot.guid,&boot_published)==EBUSY&&!boot_published);
    CHECK(DeleteFile((CONST_STRPTR)"ENVARC:OpenZFS-1111222233334445.prefs.txn"));
    CHECK(!oz_startup_settings_restore_trusted(boot.guid,&boot_published)&&boot_published==1);
    const char *copy_result="Native startup NOPRO copy unsafe metadata refused PASS\n";
    for(const char *s=copy_result;*s;s++)RawPutChar(*s);
    permissive[0].ti_Data=saved_default;CHECK(secSetDefProtectionA(permissive));
    transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-private");CHECK(transaction);UnLock(transaction);
    CHECK(secSetProtection((CONST_STRPTR)"RAM:OpenZFS-private",0));
    CHECK(!oz_preferences_store("RAM:OpenZFS-private/settings.prefs",&p,&published)&&published);
    CHECK(secSetProtection((CONST_STRPTR)"RAM:OpenZFS-private/settings.prefs",0));
    CHECK(SetComment((CONST_STRPTR)"RAM:OpenZFS-private/settings.prefs",(CONST_STRPTR)"Preserve preference metadata"));
    CHECK(!oz_preferences_store("RAM:OpenZFS-private/settings.prefs",&p,&published)&&published);
    transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-shared");CHECK(transaction);UnLock(transaction);
    CHECK(secSetProtection((CONST_STRPTR)"RAM:OpenZFS-shared",FIBF_OTR_READ|FIBF_OTR_WRITE|FIBF_OTR_EXECUTE|FIBF_OTR_DELETE));
    CHECK(secLogoutA(logout_tags)==previous_owner);logged=0;
    CHECK(secLoginA(reader_tags)==((77UL<<16)|88));logged=1;
    CHECK(oz_startup_settings_trusted(boot.guid,&boot_read)==EPERM);
    CHECK(oz_startup_settings_apply_trusted(&boot,1,&boot_published)==EPERM&&!boot_published);
    CHECK(oz_startup_settings_restore_trusted(boot.guid,&boot_published)==EPERM&&!boot_published);
    loaded=changed;struct oz_preferences before=loaded;
    int denied_read=oz_preferences_load("RAM:OpenZFS-private/settings.prefs",&loaded,&recovered);
    {char diagnostic[128];snprintf(diagnostic,sizeof(diagnostic),"Preference foreign read errno=%d owner=%08lx\n",denied_read,(unsigned long)secGetTaskOwner(FindTask(NULL)));
        for(const char *s=diagnostic;*s;s++)RawPutChar(*s);}
    CHECK(denied_read==EACCES);
    CHECK(!memcmp(&loaded,&before,sizeof(loaded)));
    CHECK(oz_preferences_store("RAM:OpenZFS-private/settings.prefs",&p,&published)==EACCES&&!published);
    transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-shared/reader");CHECK(transaction);UnLock(transaction);
    CHECK(!oz_preferences_store("RAM:OpenZFS-shared/reader/settings.prefs",&p,&published)&&published);
    CHECK(!oz_preferences_load("RAM:OpenZFS-shared/reader/settings.prefs",&loaded,NULL)&&!memcmp(&p,&loaded,sizeof(p)));
    CHECK(secSetProtection((CONST_STRPTR)"RAM:OpenZFS-shared/reader/settings.prefs",FIBF_GRP_READ|FIBF_SCRIPT));
    changed=(struct oz_preferences){3,900,700};
    CHECK(!oz_preferences_store("RAM:OpenZFS-shared/reader/settings.prefs",&changed,&published)&&published);
    CHECK(!oz_preferences_load("RAM:OpenZFS-shared/reader/settings.prefs",&loaded,NULL)&&!memcmp(&changed,&loaded,sizeof(changed)));
    transaction=Lock((CONST_STRPTR)"RAM:OpenZFS-shared/reader/settings.prefs",SHARED_LOCK);CHECK(transaction);
    struct FileInfoBlock *metadata=AllocDosObject(DOS_FIB,NULL);
    int metadata_ok=metadata&&Examine(transaction,metadata)&&metadata->fib_OwnerUID==77&&
        metadata->fib_OwnerGID==88&&metadata->fib_Protection==(FIBF_GRP_READ|FIBF_SCRIPT);
    if(metadata)FreeDosObject(DOS_FIB,metadata);
    UnLock(transaction);CHECK(metadata_ok);
    CHECK(secLogoutA(logout_tags)==previous_owner);logged=0;
    CHECK(secLoginA(root_tags)==secOWNER_SYSTEM);logged=1;
    CHECK(!oz_preferences_load("RAM:OpenZFS-private/settings.prefs",&loaded,NULL)&&!memcmp(&p,&loaded,sizeof(p)));
    transaction=Lock((CONST_STRPTR)"RAM:OpenZFS-private/settings.prefs",SHARED_LOCK);CHECK(transaction);
    metadata=AllocDosObject(DOS_FIB,NULL);
    metadata_ok=metadata&&Examine(transaction,metadata)&&metadata->fib_Protection==0&&
        !strcmp((const char *)metadata->fib_Comment,"Preserve preference metadata");
    if(metadata)FreeDosObject(DOS_FIB,metadata);
    UnLock(transaction);CHECK(metadata_ok);
    /* A privileged writer must retain the prior non-root owner, not claim it. */
    CHECK(!oz_preferences_store("RAM:OpenZFS-shared/reader/settings.prefs",&p,&published)&&published);
    transaction=Lock((CONST_STRPTR)"RAM:OpenZFS-shared/reader/settings.prefs",SHARED_LOCK);CHECK(transaction);
    metadata=AllocDosObject(DOS_FIB,NULL);
    metadata_ok=metadata&&Examine(transaction,metadata)&&metadata->fib_OwnerUID==77&&
        metadata->fib_OwnerGID==88&&metadata->fib_Protection==(FIBF_GRP_READ|FIBF_SCRIPT);
    if(metadata)FreeDosObject(DOS_FIB,metadata);
    UnLock(transaction);CHECK(metadata_ok);
    transaction=Lock((CONST_STRPTR)"RAM:OpenZFS-private/settings.prefs.txn",SHARED_LOCK);
    if(transaction)UnLock(transaction);
    CHECK(!transaction&&IoErr()==ERROR_OBJECT_NOT_FOUND);
done:
    if(file)Close(file);
    if(logged){struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};secLogoutA(tags);}
    if(secBase)CloseLibrary(secBase);
    char line[100];snprintf(line,sizeof(line),"Native IFF preferences line=%d\nRESULT native IFF preferences %s\n",error,error?"FAIL":"PASS");
    for(const char *s=line;*s;s++)RawPutChar(*s);
    return error?20:0;
}
