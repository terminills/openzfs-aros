/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <stdint.h>
#include <dos/dosextens.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include "packet-cred.h"
#include "stream-file.h"
#include "stream-output.h"

struct oz_stream_output {
    const struct oz_packet_security *caller;
    BPTR parent,stage,file;
    struct MsgPort *handler;
    BSTR stage_name,leaf,payload;
    uint32_t owner,protection;
    int created,payload_created;
};
static BSTR bname(const char *text)
{
    size_t n=strlen(text);void *storage=malloc(AROS_BSTR_MEMSIZE4LEN(n));
    if(!storage)return 0;
    BSTR name=MKBADDR(storage);
    memcpy(AROS_BSTR_ADDR(name),text,n);AROS_BSTR_setstrlen(name,n);return name;
}
static int dos_errno(LONG error)
{
    switch(error){
    case ERROR_OBJECT_EXISTS:return EEXIST;
    case ERROR_OBJECT_NOT_FOUND:return ENOENT;
    case ERROR_OBJECT_IN_USE:return EBUSY;
    case ERROR_DISK_FULL:return ENOSPC;
    case ERROR_DISK_WRITE_PROTECTED:return EROFS;
    case ERROR_WRITE_PROTECTED:case ERROR_DELETE_PROTECTED:return EACCES;
    case ERROR_ACTION_NOT_KNOWN:return ENOTSUP;
    default:return EIO;
    }
}
int oz_stream_output_finish(struct oz_stream_output *out,int commit)
{
    if(!out)return EINVAL;
    int error=0,published=0;
    if(out->file){if(!Close(out->file))error=dos_errno(IoErr());out->file=0;}
    if(commit&&!error){
        error=oz_stream_directory_access(out->caller,out->parent,secAt_Execute|secAt_Write);
        if(!error&&!DoPkt(out->handler,ACTION_SET_PROTECT,0,(SIPTR)out->stage,(SIPTR)out->payload,out->protection,0))error=dos_errno(IoErr());
        if(!error&&!DoPkt(out->handler,ACTION_SET_OWNER,0,(SIPTR)out->stage,(SIPTR)out->payload,out->owner,0))error=dos_errno(IoErr());
        if(!error){
            if(!DoPkt(out->handler,ACTION_RENAME_OBJECT,(SIPTR)out->stage,(SIPTR)out->payload,(SIPTR)out->parent,(SIPTR)out->leaf,0))error=dos_errno(IoErr());
            else published=1;
        }
    }
    if(out->payload_created&&!published&&out->stage&&out->payload){
        /* Caller defaults may prohibit deletion. This is still our private,
         * unpublished scratch object, never the colliding destination. */
        if(!DoPkt(out->handler,ACTION_SET_PROTECT,0,(SIPTR)out->stage,(SIPTR)out->payload,0,0)&&!error)error=dos_errno(IoErr());
        if(!DoPkt(out->handler,ACTION_DELETE_OBJECT,(SIPTR)out->stage,(SIPTR)out->payload,0,0,0)){
            LONG rc=IoErr();if(!error&&rc!=ERROR_OBJECT_NOT_FOUND)error=dos_errno(rc);
        }
    }
    if(out->stage)UnLock(out->stage);
    if(out->created&&!DoPkt(out->handler,ACTION_DELETE_OBJECT,(SIPTR)out->parent,(SIPTR)out->stage_name,0,0,0)&&!error)error=dos_errno(IoErr());
    if(out->parent)UnLock(out->parent);
    if(out->stage_name)free(BADDR(out->stage_name));
    if(out->leaf)free(BADDR(out->leaf));
    if(out->payload)free(BADDR(out->payload));
    free(out);return error;
}
int oz_stream_output_begin(const struct oz_packet_security *caller,const char *path,
    struct oz_stream_output **result)
{
    if(!result)return EINVAL;
    *result=NULL;
    if(!caller)return EACCES;
    if(!path||strlen(path)>=4096)return EINVAL;
    const char *colon=strchr(path,':');
    if(!colon||strchr(colon+1,':'))return EINVAL;
    const char *slash=strrchr(colon+1,'/'),*leaf=slash?slash+1:colon+1;
    if(!leaf[0]||strlen(leaf)>255)return EINVAL;
    char parent[4096];size_t length=(size_t)(leaf-path);
    memcpy(parent,path,length);parent[length]=0;
    struct oz_stream_output *out=calloc(1,sizeof(*out));if(!out)return ENOMEM;
    out->caller=caller;int error=oz_stream_path_lock(caller,parent,&out->parent);
    if(!error)error=oz_stream_directory_access(caller,out->parent,secAt_Execute|secAt_Write);
    cred_t *cr=oz_packet_security_cred(caller);
    if(!error)error=oz_aros_owner_from_ids(crgetuid(cr),crgetgid(cr),&out->owner);
    if(!error)error=oz_packet_security_defaults(caller,&out->protection);
    if(!error){out->leaf=bname(leaf);out->payload=bname("stream");if(!out->leaf||!out->payload)error=ENOMEM;}
    struct Library *secBase=NULL;ULONG previous=0;int changed=0;
    if(!error){
        out->handler=((struct FileLock *)BADDR(out->parent))->fl_Task;
        secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase||!secIsConfigured()||(secGetTaskOwner(NULL)>>16)!=secROOT_UID)error=EACCES;
    }
    if(!error){
        previous=secGetDefProtection(NULL);
        struct TagItem tags[]={{secT_DefProtection,0},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags))error=EIO;else changed=1;
    }
    static unsigned serial;
    for(unsigned tries=0;!error&&!out->stage&&tries<64;tries++){
        char name[64];snprintf(name,sizeof(name),".ozsend-%08lx-%08x",(unsigned long)((uintptr_t)FindTask(NULL)&UINT32_MAX),++serial);
        if(out->stage_name)free(BADDR(out->stage_name));
        out->stage_name=bname(name);
        if(!out->stage_name){error=ENOMEM;break;}
        out->stage=(BPTR)DoPkt(out->handler,ACTION_CREATE_DIR,(SIPTR)out->parent,(SIPTR)out->stage_name,0,0,0);
        if(out->stage)out->created=1;
        else if(IoErr()!=ERROR_OBJECT_EXISTS)error=dos_errno(IoErr());
    }
    if(!error&&!out->stage)error=EEXIST;
    if(changed){struct TagItem tags[]={{secT_DefProtection,previous},{TAG_DONE,0}};
        if(!secSetDefProtectionA(tags)&&!error)error=EIO;}
    if(secBase)CloseLibrary(secBase);
    if(!error){
        /* Refuse handlers which did not preserve private native creation
         * metadata. No stream data exists yet. Do not chmod a shared object. */
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
        if(!fib)error=ENOMEM;
        else {if(!Examine(out->stage,fib)||fib->fib_Protection||fib->fib_OwnerUID!=secROOT_UID)error=ENOTSUP;FreeDosObject(DOS_FIB,fib);}
    }
    if(!error){
        struct FileHandle *fh=AllocDosObject(DOS_FILEHANDLE,NULL);
        if(!fh)error=ENOMEM;
        else if(!DoPkt(out->handler,ACTION_FINDOUTPUT,(SIPTR)MKBADDR(fh),(SIPTR)out->stage,(SIPTR)out->payload,0,0)){
            error=dos_errno(IoErr());FreeDosObject(DOS_FILEHANDLE,fh);
        }else {fh->fh_Type=out->handler;out->file=MKBADDR(fh);out->payload_created=1;}
    }
    if(error){oz_stream_output_finish(out,0);return error;}
    *result=out;return 0;
}
int oz_stream_output_write(void *arg,const void *bytes,size_t length)
{
    struct oz_stream_output *out=arg;
    if(!out||!out->file||(!bytes&&length))return EINVAL;
    const char *p=bytes;
    while(length){
        LONG chunk=length>INT32_MAX?INT32_MAX:(LONG)length;
        LONG count=Write(out->file,(APTR)p,chunk);
        if(count<=0)return count<0?dos_errno(IoErr()):EIO;
        p+=count;length-=(size_t)count;
    }
    return 0;
}
