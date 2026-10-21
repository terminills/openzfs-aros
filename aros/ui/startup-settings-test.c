/* SPDX-License-Identifier: CDDL-1.0 */
#include "startup-settings.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static int read_case(const unsigned char *bytes,unsigned length,int accepted)
{
    BPTR file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_NEWFILE);
    if(!file)return 0;
    int ok=Write(file,bytes,length)==(LONG)length;
    if(!Close(file))ok=0;
    if(!ok)return 0;
    struct oz_startup_settings *p=malloc(sizeof(*p)),*before=malloc(sizeof(*p));
    if(!p||!before){free(p);free(before);return 0;}
    memset(p,0xa5,sizeof(*p));*before=*p;
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_OLDFILE);
    if(!file){free(p);free(before);return 0;}
    int rc=oz_startup_settings_read(file,p);
    ok=Close(file)&&(accepted?(!rc&&p->guid==UINT64_C(0x0123456789abcdef)&&p->flags==5&&p->count==1&&
        !strcmp(p->pool,"tank")&&!strcmp(p->paths[0],"/AROS/ata.device/0/partition/0")):
        (rc&&!memcmp(p,before,sizeof(*p))));
    free(p);free(before);return ok;
}
int oz_startup_settings_test(void)
{
    int error=0;BPTR file=0;
    struct oz_startup_settings *p=calloc(1,sizeof(*p)),*loaded=calloc(1,sizeof(*p));
    unsigned char golden[562]={
        'F','O','R','M',0,0,2,42,'P','R','E','F',
        'P','R','H','D',0,0,0,6,0,0,0,0,0,0,
        'O','Z','B','T',0,0,2,16,
        0,1,0,5,1,35,69,103,137,171,205,239,0,1,0,0};
    unsigned char bytes[1200];
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(p&&loaded);
    strcpy((char *)golden+50,"tank");strcpy((char *)golden+306,"/AROS/ata.device/0/partition/0");
    p->guid=UINT64_C(0x0123456789abcdef);p->flags=5;p->count=1;
    strcpy(p->pool,"tank");strcpy(p->paths[0],"/AROS/ata.device/0/partition/0");
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_NEWFILE);CHECK(file);
    CHECK(!oz_startup_settings_write(file,p));CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_OLDFILE);CHECK(file);
    CHECK(Read(file,bytes,sizeof(bytes))==sizeof(golden)&&!memcmp(bytes,golden,sizeof(golden)));
    CHECK(Close(file));file=0;
    CHECK(read_case(golden,sizeof(golden),1));
    for(unsigned n=0;n<sizeof(golden);n++)CHECK(read_case(golden,n,0));
    static const unsigned corrupt[]={0,7,8,16,19,20,26,33,35,37,47,49,305,561};
    for(unsigned i=0;i<sizeof(corrupt)/sizeof(corrupt[0]);i++) {
        memcpy(bytes,golden,sizeof(golden));bytes[corrupt[i]]^=0x80;
        CHECK(read_case(bytes,sizeof(golden),0));
    }
    memcpy(bytes,golden,sizeof(golden));memset(bytes+38,0,8);CHECK(read_case(bytes,562,0));
    memcpy(bytes,golden,sizeof(golden));memset(bytes+50,'a',256);CHECK(read_case(bytes,562,0));
    memcpy(bytes,golden,sizeof(golden));bytes[306]='x';CHECK(read_case(bytes,562,0));
    memcpy(bytes,golden,sizeof(golden));bytes[562]=0;CHECK(read_case(bytes,563,0));
    memcpy(bytes+562,golden,sizeof(golden));CHECK(read_case(bytes,1124,0));
    memcpy(bytes,golden,sizeof(golden));memcpy(bytes+562,golden+26,536);
    bytes[6]=4;bytes[7]=66;CHECK(read_case(bytes,1098,0));
    /* Unknown bounded leaf chunk, including required odd padding. */
    memcpy(bytes,golden,sizeof(golden));memcpy(bytes+562,"EXTN\0\0\0\1x\0",10);
    bytes[7]=52;CHECK(read_case(bytes,572,1));
    p->count=32;for(unsigned i=0;i<32;i++)snprintf(p->paths[i],256,"/RAM/disk%u",i);
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_NEWFILE);CHECK(file);
    CHECK(!oz_startup_settings_write(file,p));CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-test.prefs",MODE_OLDFILE);CHECK(file);
    CHECK(!oz_startup_settings_read(file,loaded)&&loaded->count==32&&loaded->guid==p->guid);
    for(unsigned i=0;i<32;i++)CHECK(!strcmp(loaded->paths[i],p->paths[i]));
    CHECK(Close(file));file=0;
    /* Real DOS storage lifecycle, not just codec round trips. */
    const char *saved="RAM:OpenZFS-startup-saved.prefs";
    int published=-1,recovered=-1;
    CHECK(!oz_startup_settings_store(saved,p,&published)&&published==1);
    CHECK(!oz_startup_settings_load(saved,loaded,&recovered)&&!recovered&&loaded->count==32);
    p->flags=OZ_STARTUP_READONLY|OZ_STARTUP_MOUNT|OZ_STARTUP_ENABLED;
    CHECK(!oz_startup_settings_store(saved,p,&published)&&published==1);
    CHECK(!oz_startup_settings_load(saved,loaded,&recovered)&&loaded->flags==7);
    BPTR transaction=CreateDir((CONST_STRPTR)"RAM:OpenZFS-startup-saved.prefs.txn");
    CHECK(transaction);UnLock(transaction);
    CHECK(oz_startup_settings_store(saved,p,&published)==EBUSY&&!published);
    CHECK(Rename((CONST_STRPTR)saved,(CONST_STRPTR)"RAM:OpenZFS-startup-saved.prefs.txn/previous"));
    CHECK(!oz_startup_settings_load(saved,loaded,&recovered)&&recovered==1&&loaded->flags==7);
    CHECK(Rename((CONST_STRPTR)"RAM:OpenZFS-startup-saved.prefs.txn/previous",(CONST_STRPTR)saved));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:OpenZFS-startup-saved.prefs.txn"));
    /* Invalid replacement must preserve the last good 32-device record. */
    p->count=0;
    CHECK(oz_startup_settings_store(saved,p,&published)==EINVAL&&!published);
    CHECK(!oz_startup_settings_load(saved,loaded,&recovered)&&!recovered&&loaded->count==32);
    p->count=32;
    CHECK(DeleteFile((CONST_STRPTR)saved));
    /* Runner supplies private RAM-backed ENV/ENVARC, never host settings. */
    const char *env="ENV:OpenZFS-0123456789abcdef.prefs";
    const char *archive="ENVARC:OpenZFS-0123456789abcdef.prefs";
    unsigned mask=99;
    CHECK(!oz_startup_settings_apply(p,1,&mask)&&mask==3);
    CHECK(!oz_startup_settings_current(p->guid,loaded,&recovered)&&loaded->flags==7);
    p->flags=OZ_STARTUP_READONLY;
    CHECK(!oz_startup_settings_apply(p,0,&mask)&&mask==1);
    CHECK(!oz_startup_settings_current(p->guid,loaded,&recovered)&&loaded->flags==1);
    CHECK(!oz_startup_settings_load(archive,loaded,&recovered)&&loaded->flags==7);
    transaction=CreateDir((CONST_STRPTR)"ENV:OpenZFS-0123456789abcdef.prefs.txn");
    CHECK(transaction);UnLock(transaction);
    p->flags=OZ_STARTUP_ENABLED;
    CHECK(oz_startup_settings_apply(p,1,&mask)==EBUSY&&mask==2);
    CHECK(!oz_startup_settings_current(p->guid,loaded,&recovered)&&loaded->flags==1);
    CHECK(!oz_startup_settings_load(archive,loaded,&recovered)&&loaded->flags==4);
    CHECK(DeleteFile((CONST_STRPTR)"ENV:OpenZFS-0123456789abcdef.prefs.txn"));
    p->guid++;
    CHECK(!oz_startup_settings_store(env,p,&published));
    loaded->flags=99;
    CHECK(oz_startup_settings_current(p->guid-1,loaded,&recovered)==EINVAL&&loaded->flags==99);
    p->guid--;
    CHECK(DeleteFile((CONST_STRPTR)env));
    CHECK(oz_startup_settings_current(p->guid,loaded,&recovered)==ENOENT);
    /* No ENVARC fallback: an absent ENV record must not silently enable it. */
    CHECK(DeleteFile((CONST_STRPTR)archive));
    strcpy(p->paths[1],p->paths[0]);
    file=Open((CONST_STRPTR)"RAM:OpenZFS-startup-invalid.prefs",MODE_NEWFILE);CHECK(file);
    CHECK(oz_startup_settings_write(file,p)==EINVAL&&Seek(file,0,OFFSET_CURRENT)==0);
done:
    if(file)Close(file);
    free(p);free(loaded);
    char line[96];snprintf(line,sizeof(line),"RESULT native startup IFF %s line=%d\n",error?"FAIL":"PASS",error);
    for(const char *c=line;*c;c++)RawPutChar(*c);
    return error;
#undef CHECK
}
