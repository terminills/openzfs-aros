/* SPDX-License-Identifier: CDDL-1.0 */
#include "hostid-policy.h"
#include "hostid-record.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <string.h>
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
int oz_hostid_policy_start(void)
{
    /* Once used by the production service, getenv can never override it. */
    oz_hostid_set_native(0);
    BPTR parent=Lock((CONST_STRPTR)"ENV:",SHARED_LOCK);
    if(!parent)return IoErr()==ERROR_OBJECT_NOT_FOUND||IoErr()==ERROR_DIR_NOT_FOUND?0:EACCES;
    static const char pending[]="OpenZFSHost.prefs.txn";
    union {IPTR align;unsigned char bytes[AROS_BSTR_MEMSIZE4LEN(sizeof(pending)-1)];} storage;
    BSTR bname=MKBADDR(storage.bytes);
    memcpy(AROS_BSTR_ADDR(bname),pending,sizeof(pending)-1);AROS_BSTR_setstrlen(bname,sizeof(pending)-1);
    struct FileLock *native=BADDR(parent);
    /* The service has already published Starting. A writer publishes this
     * marker before checking Starting/ready, and holds it through both file
     * publications. Any lookup result except absence must fail closed,
     * including an exclusive-lock conflict or a stale crash marker. */
    static const char updating[]="OpenZFSHost.update";
    memcpy(AROS_BSTR_ADDR(bname),updating,sizeof(updating)-1);AROS_BSTR_setstrlen(bname,sizeof(updating)-1);
    BPTR update=(BPTR)DoPkt(native->fl_Task,ACTION_LOCATE_OBJECT,(IPTR)parent,(IPTR)bname,SHARED_LOCK,0,0);
    LONG update_error=update?0:IoErr();
    if(update){UnLock(update);UnLock(parent);return EBUSY;}
    if(update_error!=ERROR_OBJECT_NOT_FOUND){UnLock(parent);return update_error==ERROR_OBJECT_IN_USE?EBUSY:EACCES;}
    memcpy(AROS_BSTR_ADDR(bname),pending,sizeof(pending)-1);AROS_BSTR_setstrlen(bname,sizeof(pending)-1);
    BPTR transaction=(BPTR)DoPkt(native->fl_Task,ACTION_LOCATE_OBJECT,(IPTR)parent,(IPTR)bname,SHARED_LOCK,0,0);
    LONG transaction_error=transaction?0:IoErr();
    if(transaction){UnLock(transaction);UnLock(parent);return EBUSY;}
    if(transaction_error!=ERROR_OBJECT_NOT_FOUND){UnLock(parent);return EACCES;}
    static const char name[]="OpenZFSHost.prefs";
    memcpy(AROS_BSTR_ADDR(bname),name,sizeof(name)-1);AROS_BSTR_setstrlen(bname,sizeof(name)-1);
    BPTR child=(BPTR)DoPkt(native->fl_Task,ACTION_LOCATE_OBJECT,(IPTR)parent,(IPTR)bname,SHARED_LOCK,0,0);
    LONG lookup_error=child?0:IoErr();
    if(!child){UnLock(parent);return lookup_error==ERROR_OBJECT_NOT_FOUND?0:lookup_error==ERROR_IS_SOFT_LINK?ELOOP:EACCES;}
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int error=!secBase?ENOTSUP:!secIsConfigured()?ENOTSUP:
        secGetTaskOwner(FindTask(NULL))!=secOWNER_SYSTEM?EPERM:0;
    if(!error)error=trusted(parent,1);
    if(!error)error=trusted(child,0);
    BPTR file=0;uint32_t identity=0;
    if(!error){file=OpenFromLock(child);if(file)child=0;else error=EIO;}
    if(!error)error=oz_hostid_record_read(file,&identity);
    if(file&&!Close(file)&&!error)error=EIO;
    if(child)UnLock(child);
    UnLock(parent);if(secBase)CloseLibrary(secBase);
    if(!error)oz_hostid_set_native(identity);
    return error;
}
