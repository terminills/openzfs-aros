/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <libraries/security.h>
#include "packet-cred.h"
#include "stream-file.h"

struct oz_stream_file {
    BPTR handle;
    uint32_t access;
};

static int object_access(const struct oz_packet_security *caller,BPTR lock,
    uint32_t access,int directory)
{
    if(!caller)return EACCES;
    if(!lock||!access||(access&~(directory?(secAt_Execute|secAt_Write):(secAt_Read|secAt_Write))))return EINVAL;
    struct FileLock *native=BADDR(lock);
    struct MsgPort *handler=native->fl_Task;
    if(!handler||!handler->mp_SigTask)return EIO;
    /* Even Examine would synchronously deadlock on our own DOS endpoint. */
    if(handler->mp_SigTask==FindTask(NULL))return EDEADLK;
    struct FileInfoBlock *info=AllocDosObject(DOS_FIB,NULL);
    if(!info)return ENOMEM;
    int error=0;
    if(!Examine(lock,info))error=EIO;
    else if(directory?(info->fib_DirEntryType!=ST_ROOT&&
        info->fib_DirEntryType!=ST_USERDIR&&info->fib_DirEntryType!=ST_LINKDIR):
        info->fib_DirEntryType!=ST_FILE)error=ENOTSUP;
    else {
        uint32_t owner=((uint32_t)info->fib_OwnerUID<<16)|info->fib_OwnerGID;
        error=oz_packet_security_access_on(caller,handler,owner,
            (uint32_t)info->fib_Protection,access,NULL);
    }
    FreeDosObject(DOS_FIB,info);return error;
}

int oz_stream_lock_access(const struct oz_packet_security *caller,BPTR lock,uint32_t access)
{return object_access(caller,lock,access,0);}

int oz_stream_directory_access(const struct oz_packet_security *caller,BPTR lock,uint32_t access)
{return object_access(caller,lock,access,1);}

int oz_stream_child_lock(const struct oz_packet_security *caller,BPTR parent,
    const char *name,BPTR *out)
{
    if(!out)return EINVAL;
    *out=0;
    if(!name||!name[0]||strpbrk(name,"/:"))return EINVAL;
    size_t length=strlen(name);
    if(length>255)return ENAMETOOLONG;
    int error=object_access(caller,parent,secAt_Execute,1);
    if(error)return error;
    /* Locate exactly one component. Lock() would silently follow soft links
     * using the daemon's identity and skip authorization of the new path. */
    void *storage=malloc(AROS_BSTR_MEMSIZE4LEN(length));
    if(!storage)return ENOMEM;
    BSTR bname=MKBADDR(storage);
    memcpy(AROS_BSTR_ADDR(bname),name,length);
    AROS_BSTR_setstrlen(bname,length);
    struct FileLock *native=BADDR(parent);
    BPTR child=(BPTR)DoPkt(native->fl_Task,ACTION_LOCATE_OBJECT,
        (IPTR)parent,(IPTR)bname,SHARED_LOCK,0,0);
    LONG dos_error=child?0:IoErr();
    free(storage);
    if(!child){
        if(dos_error==ERROR_OBJECT_NOT_FOUND)return ENOENT;
        if(dos_error==ERROR_IS_SOFT_LINK)return ELOOP;
        return EIO;
    }
    *out=child;return 0;
}

static int namespace_lock(const struct oz_packet_security *,const char *,BPTR *,unsigned *);
static int relative_lock(const struct oz_packet_security *caller,BPTR base,
    const char *path,BPTR *out,unsigned *budget,int namespaces)
{
    if(!out)return EINVAL;
    *out=0;
    if(!path)return EINVAL;
    if(strchr(path,':'))return EXDEV;
    size_t length=strlen(path);
    if(length>=4096)return ENAMETOOLONG;
    int error=object_access(caller,base,secAt_Execute,1);
    if(error)return error;
    if(((struct FileLock *)BADDR(base))->fl_Access!=SHARED_LOCK)return EBUSY;
    BPTR current=DupLock(base);
    if(!current)return EIO;
    char *pending=malloc(4096),*expanded=malloc(4096);
    if(!pending||!expanded){free(pending);free(expanded);UnLock(current);return ENOMEM;}
    memcpy(pending,path,length+1);
    int directory=1;
    while(!error&&pending[0]){
        if(pending[0]=='/'){
            error=object_access(caller,current,secAt_Execute,1);
            if(error)break;
            BPTR parent=ParentDir(current);
            if(!parent){error=IoErr()?EIO:ENOENT;break;}
            UnLock(current);current=parent;
            memmove(pending,pending+1,strlen(pending));
            directory=1;continue;
        }
        char *slash=strchr(pending,'/');
        size_t count=slash?(size_t)(slash-pending):strlen(pending);
        if(count>255){error=ENAMETOOLONG;break;}
        char component[256];memcpy(component,pending,count);component[count]=0;
        BPTR child=0;
        error=oz_stream_child_lock(caller,current,component,&child);
        if(error==ELOOP){
            if(!*budget)break;
            --*budget;
            struct FileLock *native=BADDR(current);
            LONG size=ReadLink(native->fl_Task,current,(CONST_STRPTR)component,
                (STRPTR)expanded,4096);
            if(size<0){error=size==-2?ENAMETOOLONG:EIO;break;}
            if(size==0||size>=4096){error=EINVAL;break;}
            expanded[size]=0;
            const char *rest=slash;
            if(rest&&expanded[size-1]=='/')rest++;
            size_t suffix=rest?strlen(rest):0;
            if((size_t)size+suffix>=4096){error=ENAMETOOLONG;break;}
            if(rest)memcpy(expanded+size,rest,suffix+1);
            if(strchr(expanded,':')){
                if(!namespaces){error=EXDEV;break;}
                BPTR resolved=0;
                error=namespace_lock(caller,expanded,&resolved,budget);
                if(!error){UnLock(current);current=resolved;directory=0;pending[0]=0;}
                break;
            }
            strcpy(pending,expanded);error=0;continue;
        }
        if(error)break;
        UnLock(current);current=child;
        directory=slash!=NULL;
        if(slash)memmove(pending,slash+1,strlen(slash));
        else pending[0]=0;
    }
    if(!error&&directory)error=object_access(caller,current,secAt_Execute,1);
    if(error)UnLock(current);else *out=current;
    free(expanded);free(pending);return error;
}

int oz_stream_relative_lock(const struct oz_packet_security *caller,BPTR base,
    const char *path,BPTR *out)
{
    unsigned budget=16;
    return relative_lock(caller,base,path,out,&budget,0);
}

/* Namespace locks are snapshotted under the DOS-list read lock so concurrent
 * removal cannot invalidate an assign while its lock is being duplicated.
 * Do not invoke GetDeviceProc: deferred assigns use privileged Lock() there. */
static int copy_namespace_lock(BPTR source,BPTR *out)
{
    if(!source)return EIO;
    struct FileLock *native=BADDR(source);
    if(!native->fl_Task||!native->fl_Task->mp_SigTask)return ENXIO;
    if(native->fl_Task->mp_SigTask==FindTask(NULL))return EDEADLK;
    if(native->fl_Access!=SHARED_LOCK)return EBUSY;
    *out=DupLock(source);return *out?0:EIO;
}

static int namespace_lock(const struct oz_packet_security *caller,const char *path,
    BPTR *out,unsigned *budget)
{
    *out=0;
    if(!caller)return EACCES;
    if(!path)return EINVAL;
    if(strlen(path)>=4096)return ENAMETOOLONG;
    const char *colon=strchr(path,':');
    if(!colon||colon==path)return EINVAL;
    size_t length=(size_t)(colon-path);
    if(length>31)return ENAMETOOLONG;
    char name[32];memcpy(name,path,length);name[length]=0;
    if(!strcasecmp(name,"PROGDIR")||strchr(name,'/'))return EINVAL;
    BPTR *roots=NULL;size_t count=0;char *redirect=NULL;
    int error=0;
    struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
    struct DosList *entry=FindDosEntry(list,(CONST_STRPTR)name,LDF_ALL);
    if(!entry)error=ENOENT;
    else if((entry->dol_Type==DLT_LATE&&!entry->dol_Lock)||entry->dol_Type==DLT_NONBINDING){
        const char *target=(const char *)entry->dol_misc.dol_assign.dol_AssignName;
        if(!target||strlen(target)>=4096)error=EINVAL;
        else {redirect=strdup(target);if(!redirect)error=ENOMEM;}
    } else if(entry->dol_Type==DLT_DIRECTORY||entry->dol_Type==DLT_LATE){
        count=1;
        for(struct AssignList *a=entry->dol_misc.dol_assign.dol_List;a;a=a->al_Next)count++;
        roots=calloc(count,sizeof(*roots));
        if(!roots)error=ENOMEM;
        else {
            error=copy_namespace_lock(entry->dol_Lock,&roots[0]);
            size_t i=1;
            for(struct AssignList *a=entry->dol_misc.dol_assign.dol_List;a&&!error;a=a->al_Next)
                error=copy_namespace_lock(a->al_Lock,&roots[i++]);
        }
    } else if(entry->dol_Type==DLT_DEVICE||entry->dol_Type==DLT_VOLUME){
        struct MsgPort *port=entry->dol_Task;
        if(!port||!port->mp_SigTask)error=ENXIO;
        else if(port->mp_SigTask==FindTask(NULL))error=EDEADLK;
        else {
            roots=calloc(1,sizeof(*roots));count=1;
            if(!roots)error=ENOMEM;
            else {
                IPTR storage=0;BSTR empty=MKBADDR(&storage);
                AROS_BSTR_setstrlen(empty,0);
                roots[0]=(BPTR)DoPkt(port,ACTION_LOCATE_OBJECT,0,(IPTR)empty,SHARED_LOCK,0,0);
                if(!roots[0])error=EIO;
            }
        }
    } else error=ENOTSUP;
    UnLockDosList(LDF_ALL|LDF_READ);
    if(!error&&redirect){
        if(!*budget)error=ELOOP;
        else {
            --*budget;
            BPTR base=0;error=namespace_lock(caller,redirect,&base,budget);
            if(!error)error=relative_lock(caller,base,colon+1,out,budget,1);
            if(base)UnLock(base);
        }
    } else if(!error){
        error=ENOENT;
        for(size_t i=0;i<count;i++){
            error=relative_lock(caller,roots[i],colon+1,out,budget,1);
            if(error!=ENOENT)break;
        }
    }
    if(roots){for(size_t i=0;i<count;i++)if(roots[i])UnLock(roots[i]);free(roots);}
    free(redirect);return error;
}

int oz_stream_path_lock(const struct oz_packet_security *caller,const char *path,BPTR *out)
{
    if(!out)return EINVAL;
    unsigned budget=16;
    return namespace_lock(caller,path,out,&budget);
}

int oz_stream_file_open(const struct oz_packet_security *caller,BPTR *lock,
    uint32_t access,struct oz_stream_file **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!lock)return EINVAL;
    int error=oz_stream_lock_access(caller,*lock,access);
    if(error)return error;
    struct oz_stream_file *file=calloc(1,sizeof(*file));
    if(!file)return ENOMEM;
    file->handle=OpenFromLock(*lock);
    if(!file->handle){free(file);return EIO;}
    *lock=0;file->access=access;*out=file;return 0;
}

int oz_stream_file_read(struct oz_stream_file *file,void *buffer,size_t length,
    size_t *actual)
{
    if(!actual)return EINVAL;
    *actual=0;
    if(!file||(!buffer&&length)||length>INT32_MAX)return EINVAL;
    if(!(file->access&secAt_Read))return EACCES;
    if(!length)return 0;
    LONG count=Read(file->handle,buffer,(LONG)length);
    if(count<0)return EIO;
    *actual=(size_t)count;return 0;
}

int oz_stream_file_open_empty_output(const struct oz_packet_security *caller,
    BPTR *lock,struct oz_stream_file **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!lock)return EINVAL;
    int error=oz_stream_lock_access(caller,*lock,secAt_Write);
    if(error)return error;
    if(!ChangeMode(CHANGE_LOCK,*lock,EXCLUSIVE_LOCK))
        return IoErr()==ERROR_OBJECT_IN_USE?EBUSY:ENOTSUP;
    struct oz_stream_file *file=NULL;
    error=oz_stream_file_open(caller,lock,secAt_Write,&file);
    if(error)return error;
    /* Test EOF, not 32-bit fib_Size, which can wrap for a large native file.
     * This is an internal emptiness check; no byte is returned to the caller.
     * OpenFromLock preserves the exclusive object and starts at offset zero. */
    char byte;
    LONG count=Read(file->handle,&byte,1);
    if(count){oz_stream_file_close(file);return count<0?EIO:EEXIST;}
    *out=file;return 0;
}

int oz_stream_file_write(struct oz_stream_file *file,const void *buffer,
    size_t length,size_t *actual)
{
    if(!actual)return EINVAL;
    *actual=0;
    if(!file||(!buffer&&length)||length>INT32_MAX)return EINVAL;
    if(!(file->access&secAt_Write))return EACCES;
    if(!length)return 0;
    LONG count=Write(file->handle,(APTR)buffer,(LONG)length);
    if(count<0)return EIO;
    *actual=(size_t)count;return 0;
}

int oz_stream_file_close(struct oz_stream_file *file)
{
    if(!file)return EINVAL;
    int error=Close(file->handle)?0:EIO;
    free(file);return error;
}
