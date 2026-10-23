/* SPDX-License-Identifier: CDDL-1.0 */
#include "startup-settings.h"
#include "settings-file.h"
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
static int record_read(BPTR f,void *p){return oz_startup_settings_read(f,p);}
static int record_write(BPTR f,const void *p){return oz_startup_settings_write(f,p);}
static int record_equal(const void *a,const void *b)
{
    const struct oz_startup_settings *x=a,*y=b;
    if(x->guid!=y->guid||x->flags!=y->flags||x->count!=y->count||strcmp(x->pool,y->pool))return 0;
    for(unsigned i=0;i<x->count;i++)if(strcmp(x->paths[i],y->paths[i]))return 0;
    return 1;
}
static const struct oz_settings_codec record_codec={sizeof(struct oz_startup_settings),record_read,record_write,record_equal,NULL};
int oz_startup_settings_load(const char *path,struct oz_startup_settings *p,int *recovered)
{return oz_settings_load(path,p,recovered,&record_codec);}
int oz_startup_settings_store(const char *path,const struct oz_startup_settings *p,int *published)
{return oz_settings_store(path,p,published,&record_codec);}
static void record_path(char *path,size_t size,const char *volume,uint64_t guid)
{snprintf(path,size,"%s:OpenZFS-%08lx%08lx.prefs",volume,
    (unsigned long)(guid>>32),(unsigned long)(guid&UINT64_C(0xffffffff)));}
static int apply_record(const struct oz_startup_settings *p,int permanent,unsigned *published,const struct oz_settings_codec *codec)
{
    if(published)*published=0;
    if(!p||!p->guid||(permanent!=0&&permanent!=1))return EINVAL;
    char path[80];int wrote=0,error;
    if(permanent){
        record_path(path,sizeof(path),"ENVARC",p->guid);
        error=oz_settings_store(path,p,&wrote,codec);
        if(wrote&&published)*published|=OZ_STARTUP_PUBLISHED_ENVARC;
        if(error)return error;
    }
    record_path(path,sizeof(path),"ENV",p->guid);
    error=oz_settings_store(path,p,&wrote,codec);
    if(wrote&&published)*published|=OZ_STARTUP_PUBLISHED_ENV;
    return error;
}
int oz_startup_settings_apply(const struct oz_startup_settings *p,int permanent,unsigned *published)
{return apply_record(p,permanent,published,&record_codec);}
int oz_startup_settings_current(uint64_t guid,struct oz_startup_settings *p,int *recovered)
{
    if(recovered)*recovered=0;
    if(!guid||!p)return EINVAL;
    struct oz_startup_settings *candidate=malloc(sizeof(*candidate));
    if(!candidate)return ENOMEM;
    char path[80];int recovery=0;
    record_path(path,sizeof(path),"ENV",guid);
    int error=oz_startup_settings_load(path,candidate,&recovery);
    if(!error&&candidate->guid!=guid)error=EINVAL;
    if(!error){*p=*candidate;if(recovered)*recovered=recovery;}
    free(candidate);return error;
}
static int trusted_metadata(const struct FileInfoBlock *fib)
{
    return fib->fib_DirEntryType==ST_FILE&&fib->fib_OwnerUID==0xffff&&
        !(fib->fib_Protection&(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE));
}
static int verify_trusted(const struct FileInfoBlock *fib,int directory)
{
    if(fib->fib_OwnerUID!=0xffff||
        (fib->fib_Protection&(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE)))return EACCES;
    return (directory?fib->fib_DirEntryType>0:fib->fib_DirEntryType==ST_FILE)?0:EINVAL;
}
int oz_startup_settings_apply_trusted(const struct oz_startup_settings *p,int permanent,unsigned *published)
{
    if(published)*published=0;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOTSUP;
    int error=!secIsConfigured()?ENOTSUP:secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM?EPERM:0;
    ULONG previous=secGetDefProtection(FindTask(NULL));int changed=0;
    if(!error){struct TagItem tags[]={{secT_DefProtection,0},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags))error=EACCES;else changed=1;}
    static const struct oz_settings_codec trusted_codec={sizeof(struct oz_startup_settings),record_read,record_write,record_equal,verify_trusted};
    if(!error)error=apply_record(p,permanent,published,&trusted_codec);
    if(changed){struct TagItem tags[]={{secT_DefProtection,previous},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags)&&!error)error=EIO;}
    CloseLibrary(secBase);return error;
}
static int trusted_record(const char *volume,uint64_t guid,struct oz_startup_settings *out)
{
    if(!guid||!out)return EINVAL;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOTSUP;
    int error=!secIsConfigured()?ENOTSUP:
        secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM?EPERM:0;
    BPTR file=0;struct FileInfoBlock *before=NULL,*after=NULL;
    struct oz_startup_settings *candidate=NULL;
    char path[80],transaction[88];
    record_path(path,sizeof(path),volume,guid);
    if(!error)error=oz_settings_transaction_path(path,transaction,sizeof(transaction));
    if(!error){
        BPTR pending=Lock((CONST_STRPTR)transaction,SHARED_LOCK);
        if(pending){UnLock(pending);error=EBUSY;}
        else if(IoErr()!=ERROR_OBJECT_NOT_FOUND&&IoErr()!=ERROR_DIR_NOT_FOUND)error=EACCES;
    }
    if(!error){file=Open((CONST_STRPTR)path,MODE_OLDFILE);
        if(!file)error=(IoErr()==ERROR_OBJECT_NOT_FOUND||IoErr()==ERROR_DIR_NOT_FOUND)?ENOENT:EACCES;}
    if(!error){
        before=AllocDosObject(DOS_FIB,NULL);after=AllocDosObject(DOS_FIB,NULL);
        candidate=malloc(sizeof(*candidate));
        if(!before||!after||!candidate)error=ENOMEM;
    }
    if(!error&&(!ExamineFH(file,before)||!trusted_metadata(before)))error=EACCES;
    if(!error)error=oz_startup_settings_read(file,candidate);
    if(!error&&candidate->guid!=guid)error=EINVAL;
    if(!error&&(!ExamineFH(file,after)||!trusted_metadata(after)||
        before->fib_Protection!=after->fib_Protection||before->fib_OwnerGID!=after->fib_OwnerGID||
        before->fib_Size!=after->fib_Size||memcmp(&before->fib_Date,&after->fib_Date,sizeof(before->fib_Date))))error=EAGAIN;
    if(file&&!Close(file)&&!error)error=EIO;
    if(!error)*out=*candidate;
    if(before)FreeDosObject(DOS_FIB,before);
    if(after)FreeDosObject(DOS_FIB,after);
    free(candidate);CloseLibrary(secBase);return error;
}
int oz_startup_settings_trusted(uint64_t guid,struct oz_startup_settings *out)
{return trusted_record("ENV",guid,out);}
int oz_startup_settings_restore_trusted(uint64_t guid,unsigned *published)
{
    if(published)*published=0;
    struct oz_startup_settings *record=malloc(sizeof(*record));
    if(!record)return ENOMEM;
    int error=trusted_record("ENVARC",guid,record);
    if(!error)error=oz_startup_settings_apply_trusted(record,0,published);
    free(record);return error;
}
