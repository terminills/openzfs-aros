/* SPDX-License-Identifier: CDDL-1.0 */
#include "preferences.h"
#include "settings-file.h"
#include <stdlib.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static int file_error(LONG e)
{
    switch(e){
    case ERROR_OBJECT_NOT_FOUND:case ERROR_DIR_NOT_FOUND:return ENOENT;
    case ERROR_OBJECT_EXISTS:case ERROR_OBJECT_IN_USE:return EBUSY;
    case ERROR_DISK_WRITE_PROTECTED:return EROFS;
    case ERROR_READ_PROTECTED:case ERROR_WRITE_PROTECTED:case ERROR_DELETE_PROTECTED:return EACCES;
    case ERROR_DISK_FULL:return ENOSPC;
    default:return EIO;
    }
}
static int load_one(const char *path,void *out,const struct oz_settings_codec *codec)
{
    BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);
    if(!file)return file_error(IoErr());
    void *candidate=calloc(1,codec->size);
    int error=candidate?codec->read(file,candidate):ENOMEM;
    if(!Close(file)&&!error)error=file_error(IoErr());
    if(!error)memcpy(out,candidate,codec->size);
    free(candidate);
    return error;
}
static int verify_lock(BPTR lock,const struct oz_settings_codec *codec,int directory)
{
    if(!codec->verify)return 0;
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(!fib)return ENOMEM;
    int error=Examine(lock,fib)?codec->verify(fib,directory):file_error(IoErr());
    FreeDosObject(DOS_FIB,fib);return error;
}
/* A replacement must not silently acquire the writer's default permissions
 * or ownership. All changes remain subject to the filesystem's caller checks. */
static int preserve_metadata(const char *path,const struct FileInfoBlock *old)
{
    BPTR lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(!lock)return file_error(IoErr());
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    int error=0;
    if(!fib)error=ENOMEM;
    else if(!Examine(lock,fib))error=file_error(IoErr());
    UnLock(lock);
    if(!error&&(fib->fib_OwnerUID!=old->fib_OwnerUID||fib->fib_OwnerGID!=old->fib_OwnerGID)){
        ULONG owner=((ULONG)old->fib_OwnerUID<<16)|old->fib_OwnerGID;
        if(!SetOwner((STRPTR)path,owner))error=file_error(IoErr());
    }
    if(!error&&fib->fib_Protection!=old->fib_Protection){
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        LONG ok=secBase?secSetProtection((CONST_STRPTR)path,old->fib_Protection):
            SetProtection((CONST_STRPTR)path,old->fib_Protection);
        if(!ok)error=file_error(IoErr());
        if(secBase)CloseLibrary(secBase);
    }
    if(!error&&strcmp((const char *)fib->fib_Comment,(const char *)old->fib_Comment)&&
        !SetComment((CONST_STRPTR)path,(CONST_STRPTR)old->fib_Comment))error=file_error(IoErr());
    if(!error){
        lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
        if(!lock)error=file_error(IoErr());
        else{
            if(!Examine(lock,fib))error=file_error(IoErr());
            else if(fib->fib_OwnerUID!=old->fib_OwnerUID||fib->fib_OwnerGID!=old->fib_OwnerGID||
                fib->fib_Protection!=old->fib_Protection||
                strcmp((const char *)fib->fib_Comment,(const char *)old->fib_Comment))error=EACCES;
            UnLock(lock);
        }
    }
    if(fib)FreeDosObject(DOS_FIB,fib);
    return error;
}
int oz_settings_load(const char *path,void *out,int *recovered,const struct oz_settings_codec *codec)
{
    if(recovered)*recovered=0;
    if(!path||!out||strlen(path)>900)return EINVAL;
    int error=load_one(path,out,codec);
    if(error!=ENOENT)return error;
    char previous[1024];snprintf(previous,sizeof(previous),"%s.txn/previous",path);
    error=load_one(previous,out,codec);
    if(!error&&recovered)*recovered=1;
    return error;
}
int oz_settings_store(const char *path,const void *p,int *published,const struct oz_settings_codec *codec)
{
    if(published)*published=0;
    if(!path||!p||!strchr(path,':')||strlen(path)>900)return EINVAL;
    char stage[1024],next[1024],previous[1024];
    snprintf(stage,sizeof(stage),"%s.txn",path);
    snprintf(next,sizeof(next),"%s.txn/next",path);
    snprintf(previous,sizeof(previous),"%s.txn/previous",path);
    /* Atomic creation claims a fresh private staging namespace. Existing
     * transactions belong to another writer or need explicit recovery. */
    BPTR lock=CreateDir((CONST_STRPTR)stage);
    if(!lock)return file_error(IoErr());
    int error=verify_lock(lock,codec,1);
    UnLock(lock);
    if(error){DeleteFile((CONST_STRPTR)stage);return error;}
    int created=0,moved=0,committed=0;
    BPTR file=Open((CONST_STRPTR)next,MODE_NEWFILE);
    if(!file)error=file_error(IoErr());
    else{
        created=1;error=codec->write(file,p);
        if(!error&&!Flush(file))error=file_error(IoErr());
        if(!Close(file)&&!error)error=file_error(IoErr());
    }
    if(!error){
        void *check=calloc(1,codec->size);
        error=check?load_one(next,check,codec):ENOMEM;
        if(!error&&!codec->equal(check,p))error=EIO;
        free(check);
    }
    if(!error){
        lock=Lock((CONST_STRPTR)path,SHARED_LOCK);
        if(lock){
            struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
            if(!fib)error=ENOMEM;
            else{
                if(!Examine(lock,fib))error=file_error(IoErr());
                else if(fib->fib_DirEntryType!=ST_FILE)error=EINVAL;
                else if(codec->verify)error=codec->verify(fib,0);
                /* Rename may permit delete-protected files. Replacement
                 * must not publish first and discover protection only when
                 * deleting the saved previous version. */
                if(!error&&(fib->fib_Protection&(FIBF_DELETE|FIBF_WRITE)))error=EACCES;
                if(!error)error=preserve_metadata(next,fib);
                FreeDosObject(DOS_FIB,fib);
            }
            UnLock(lock);
            if(!error){
                if(!Rename((CONST_STRPTR)path,(CONST_STRPTR)previous))error=file_error(IoErr());
                else moved=1;
            }
        }else if(IoErr()!=ERROR_OBJECT_NOT_FOUND)error=file_error(IoErr());
    }
    if(!error){
        lock=Lock((CONST_STRPTR)next,SHARED_LOCK);
        if(!lock)error=file_error(IoErr());
        else{error=verify_lock(lock,codec,0);UnLock(lock);}
    }
    if(!error){
        if(!Rename((CONST_STRPTR)next,(CONST_STRPTR)path))error=file_error(IoErr());
        else{committed=1;created=0;if(published)*published=1;}
    }
    if(error&&moved&&!committed){
        /* A failed rollback retains the previous version in the transaction;
         * load can recover it. Never delete the sole saved copy. */
        if(Rename((CONST_STRPTR)previous,(CONST_STRPTR)path))moved=0;
    }
    if(created&&!DeleteFile((CONST_STRPTR)next)&&!error)error=file_error(IoErr());
    if(committed&&moved){
        if(!DeleteFile((CONST_STRPTR)previous)){if(!error)error=file_error(IoErr());}
        else moved=0;
    }
    if(!moved&&!DeleteFile((CONST_STRPTR)stage)&&!error)error=file_error(IoErr());
    return error;
}
static int pref_read(BPTR f,void *p){return oz_preferences_read(f,p);}
static int pref_write(BPTR f,const void *p){return oz_preferences_write(f,p);}
static int pref_equal(const void *a,const void *b)
{
    const struct oz_preferences *x=a,*y=b;
    return x->category==y->category&&x->width==y->width&&x->height==y->height&&x->view_mode==y->view_mode;
}
static const struct oz_settings_codec pref_codec={sizeof(struct oz_preferences),pref_read,pref_write,pref_equal,NULL};
int oz_preferences_load(const char *path,struct oz_preferences *p,int *recovered)
{return oz_settings_load(path,p,recovered,&pref_codec);}
int oz_preferences_store(const char *path,const struct oz_preferences *p,int *published)
{return oz_settings_store(path,p,published,&pref_codec);}
