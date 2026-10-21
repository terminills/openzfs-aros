/* SPDX-License-Identifier: CDDL-1.0 */
/* Service-owned persistent default. Does not load or import any pool. */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include "cache-policy.h"
static BPTR archive;
static char cache_path[512];
static int policy_error=ENOTSUP;
static int trusted(BPTR lock,int directory)
{
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(!fib)return ENOMEM;
    int error=0;
    if(!Examine(lock,fib))error=EIO;
    else if(directory?(fib->fib_DirEntryType!=ST_ROOT&&fib->fib_DirEntryType!=ST_USERDIR):
        fib->fib_DirEntryType!=ST_FILE)error=EINVAL;
    else if(fib->fib_OwnerUID!=0xffff||
        (fib->fib_Protection&(FIBF_GRP_WRITE|FIBF_GRP_DELETE|FIBF_OTR_WRITE|FIBF_OTR_DELETE)))error=EACCES;
    FreeDosObject(DOS_FIB,fib);return error;
}
int oz_cache_default_check(void)
{
    if(policy_error)return policy_error;
    int error=trusted(archive,1);if(error)return error;
    /* Locate one child without following a soft link to another object. */
    static const char name[]="OpenZFS.cache";
    void *storage=malloc(AROS_BSTR_MEMSIZE4LEN(sizeof(name)-1));
    if(!storage)return ENOMEM;
    BSTR bname=MKBADDR(storage);
    memcpy(AROS_BSTR_ADDR(bname),name,sizeof(name)-1);
    AROS_BSTR_setstrlen(bname,sizeof(name)-1);
    struct FileLock *parent=BADDR(archive);
    BPTR child=(BPTR)DoPkt(parent->fl_Task,ACTION_LOCATE_OBJECT,
        (IPTR)archive,(IPTR)bname,SHARED_LOCK,0,0);
    LONG dos_error=child?0:IoErr();free(storage);
    if(!child)return dos_error==ERROR_OBJECT_NOT_FOUND?0:
        dos_error==ERROR_IS_SOFT_LINK?ELOOP:EACCES;
    error=trusted(child,0);UnLock(child);return error;
}
void oz_cache_policy_stop(void)
{
    if(archive)UnLock(archive);
    archive=0;cache_path[0]=0;policy_error=ENOTSUP;
}
void oz_cache_policy_start(void)
{
    oz_cache_policy_stop();
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return;
    if(!secIsConfigured()||secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM)goto done;
    archive=Lock((CONST_STRPTR)"ENVARC:",SHARED_LOCK);
    if(!archive)goto done;
    char native[480];
    if(!NameFromLock(archive,(STRPTR)native,sizeof(native))){policy_error=EIO;goto done;}
    char *colon=strchr(native,':');
    if(!colon){policy_error=EINVAL;goto done;}
    /* Native key-file enforcement is indexed by DOS device name, not an
     * assign or display volume name. Resolve the retained lock's handler
     * against the live device list; never call DOS/security under its lock. */
    struct InfoData info;
    if(!Info(archive,&info))goto done;
    char device[256]="";
    struct FileLock *locked=BADDR(archive);
    struct DosList *dl=LockDosList(LDF_DEVICES|LDF_READ);
    while((dl=NextDosEntry(dl,LDF_DEVICES))) {
        if(dl->dol_Task!=locked->fl_Task)continue;
        size_t length=AROS_BSTR_strlen(dl->dol_Name);
        if(length&&length<sizeof(device)) {
            memcpy(device,AROS_BSTR_ADDR(dl->dol_Name),length);device[length]=0;
        }
        break;
    }
    UnLockDosList(LDF_DEVICES|LDF_READ);
    if(!device[0]||!secIsVolumeSecured((CONST_STRPTR)device,info.id_DiskType))goto done;
    policy_error=trusted(archive,1);if(policy_error)goto done;
    *colon='/';size_t n=strlen(native);
    snprintf(cache_path,sizeof(cache_path),"/%s%sOpenZFS.cache",native,n&&native[n-1]=='/'?"":"/");
    policy_error=oz_cache_default_check();
done:
    CloseLibrary(secBase);
    if(policy_error){if(archive)UnLock(archive);archive=0;cache_path[0]=0;}
}
const char *oz_cache_default_path(void){return cache_path;}
