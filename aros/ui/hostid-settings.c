/* SPDX-License-Identifier: CDDL-1.0 */
#include "hostid-settings.h"
#include "settings-file.h"
#include "api.h"
#include "admission-api.h"
#include "../engine/hostid-record.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <string.h>
static int read_record(BPTR f,void *p){return oz_hostid_record_read(f,p);}
static int write_record(BPTR f,const void *p){return oz_hostid_record_write(f,*(const uint32_t *)p);}
static int equal_record(const void *a,const void *b){return *(const uint32_t *)a==*(const uint32_t *)b;}
static int trusted(const struct FileInfoBlock *f,int directory)
{
    if(f->fib_OwnerUID!=0xffff||
        (f->fib_Protection&(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE)))return EACCES;
    return (directory?(f->fib_DirEntryType==ST_ROOT||f->fib_DirEntryType==ST_USERDIR):f->fib_DirEntryType==ST_FILE)?0:EINVAL;
}
static const struct oz_settings_codec codec={sizeof(uint32_t),read_record,write_record,equal_record,trusted};
static int check_parent(BPTR parent,char *path,size_t size)
{
    struct FileInfoBlock *f=AllocDosObject(DOS_FIB,NULL);
    if(!f)return ENOMEM;
    int error=Examine(parent,f)?trusted(f,1):EIO;
    FreeDosObject(DOS_FIB,f);
    if(!error&&!NameFromLock(parent,(STRPTR)path,size))error=EIO;
    if(!error&&!AddPart((STRPTR)path,(CONST_STRPTR)"OpenZFSHost.prefs",size))error=ENAMETOOLONG;
    /* Refuse final soft links before the generic transaction helper can
     * follow them. The protected parent remains locked for publication. */
    if(!error){
        static const char name[]="OpenZFSHost.prefs";
        union {IPTR align;unsigned char bytes[AROS_BSTR_MEMSIZE4LEN(sizeof(name)-1)];} storage;
        BSTR bname=MKBADDR(storage.bytes);
        memcpy(AROS_BSTR_ADDR(bname),name,sizeof(name)-1);AROS_BSTR_setstrlen(bname,sizeof(name)-1);
        struct FileLock *fl=BADDR(parent);
        BPTR child=(BPTR)DoPkt(fl->fl_Task,ACTION_LOCATE_OBJECT,(IPTR)parent,(IPTR)bname,SHARED_LOCK,0,0);
        if(child)UnLock(child);
        else {LONG e=IoErr();if(e!=ERROR_OBJECT_NOT_FOUND)error=e==ERROR_IS_SOFT_LINK?ELOOP:EACCES;}
    }
    return error;
}
int oz_hostid_settings_apply(uint32_t identity,unsigned *published)
{
    if(published)*published=0;
    if(!identity)return EINVAL;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return ENOTSUP;
    int error=!secIsConfigured()?ENOTSUP:secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM?EPERM:0;
    BPTR parents[2]={0,0};char paths[2][768];
    const char *volumes[]={"ENVARC:","ENV:"};
    for(unsigned i=0;i<2&&!error;i++){
        parents[i]=Lock((CONST_STRPTR)volumes[i],SHARED_LOCK);
        error=parents[i]?check_parent(parents[i],paths[i],sizeof(paths[i])):EACCES;
    }
    ULONG previous=0;int changed=0;
    if(!error){
        previous=secGetDefProtection(FindTask(NULL));
        struct TagItem tags[]={{secT_DefProtection,0},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags))error=EACCES;else changed=1;
    }
    BPTR update=0;char update_path[768];
    if(!error){
        if(!NameFromLock(parents[1],(STRPTR)update_path,sizeof(update_path))||
            !AddPart((STRPTR)update_path,(CONST_STRPTR)"OpenZFSHost.update",sizeof(update_path)))error=EIO;
        else{
            /* Atomic handler operation: only one writer creates this name.
             * Publish BEFORE observing startup; startup publishes its port
             * BEFORE checking this marker. Never delete someone else's marker. */
            update=CreateDir((CONST_STRPTR)update_path);
            if(!update){LONG e=IoErr();error=e==ERROR_OBJECT_EXISTS||e==ERROR_OBJECT_IN_USE?EBUSY:EACCES;}
        }
    }
    if(!error){
        /* Check in lifecycle order so Starting -> ready renaming cannot
         * slip between the observations. Forbid alone is not an SMP lock. */
        Forbid();int running=FindPort((CONST_STRPTR)OZ_PORT ".Starting")!=NULL||
            FindPort((CONST_STRPTR)OZ_PORT)!=NULL||
            FindPort((CONST_STRPTR)OZ_ADMISSION_PORT)!=NULL;Permit();
        if(running)error=EBUSY;
    }
    for(unsigned i=0;i<2&&!error;i++){
        int wrote=0;error=oz_settings_store(paths[i],&identity,&wrote,&codec);
        if(wrote&&published)*published|=i?OZ_HOSTID_CURRENT:OZ_HOSTID_SAVED;
    }
    if(update){UnLock(update);if(!DeleteFile((CONST_STRPTR)update_path)&&!error)error=EIO;}
    if(changed){struct TagItem tags[]={{secT_DefProtection,previous},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags)&&!error)error=EIO;}
    for(unsigned i=0;i<2;i++)if(parents[i])UnLock(parents[i]);
    CloseLibrary(secBase);return error;
}
