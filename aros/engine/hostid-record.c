/* SPDX-License-Identifier: CDDL-1.0 */
#define IFFParseBase OZHostidIFFBase
#include "hostid-record.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <prefs/prefhdr.h>
#include <errno.h>
struct Library *OZHostidIFFBase;
#define ID_OZHI MAKE_ID('O','Z','H','I')
/* Version 1: FORM PREF, zero PRHD (6 bytes), OZHI (BE version=1, BE id).
 * Exact size/chunks: no trailing data, duplicate fields or implicit defaults.
 * Identity zero is unprovisioned, never a valid saved record. */
int oz_hostid_record_read(BPTR file,uint32_t *identity)
{
    if(!file||!identity)return EINVAL;
    if(Seek(file,0,OFFSET_END)==-1)return EIO;
    LONG length=Seek(file,0,OFFSET_BEGINNING);
    if(length!=42)return EINVAL;
    OZHostidIFFBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!OZHostidIFFBase)return ENOSYS;
    struct IFFHandle *iff=AllocIFF();
    int error=ENOMEM,opened=0;unsigned root=0,header=0,value=0;uint32_t id=0;
    if(iff){
        iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_READ)?EINVAL:0;opened=!error;
        while(!error){
            LONG rc=ParseIFF(iff,IFFPARSE_STEP);
            if(rc==IFFERR_EOF)break;
            if(rc==IFFERR_EOC)continue;
            if(rc){error=EINVAL;break;}
            struct ContextNode *cn=CurrentChunk(iff);
            if(!cn){error=EINVAL;break;}
            if(iff->iff_Depth==1){
                if(root++||cn->cn_ID!=ID_FORM||cn->cn_Type!=ID_PREF||cn->cn_Size!=34)error=EINVAL;
            }else if(iff->iff_Depth!=2||cn->cn_Type!=ID_PREF)error=EINVAL;
            else if(cn->cn_ID==ID_PRHD){
                unsigned char h[6];
                if(header++||value||cn->cn_Size!=6||ReadChunkBytes(iff,h,6)!=6)error=EINVAL;
                else for(unsigned i=0;i<6;i++)if(h[i])error=ENOTSUP;
            }else if(cn->cn_ID==ID_OZHI){
                unsigned char bytes[8];
                if(!header||value++||cn->cn_Size!=8||ReadChunkBytes(iff,bytes,8)!=8)error=EINVAL;
                else if(bytes[0]||bytes[1]||bytes[2]||bytes[3]!=1)error=ENOTSUP;
                else {for(unsigned i=4;i<8;i++)id=(id<<8)|bytes[i];if(!id)error=EINVAL;}
            }else error=EINVAL;
        }
        if(!error&&(root!=1||header!=1||value!=1))error=EINVAL;
        if(opened)CloseIFF(iff);
        FreeIFF(iff);
    }
    CloseLibrary(OZHostidIFFBase);OZHostidIFFBase=NULL;
    if(!error)*identity=id;
    return error;
}
int oz_hostid_record_write(BPTR file,uint32_t identity)
{
    if(!file||!identity)return EINVAL;
    if(Seek(file,0,OFFSET_END)==-1)return EIO;
    LONG length=Seek(file,0,OFFSET_BEGINNING);
    if(length<0)return EIO;
    if(length)return EEXIST;
    OZHostidIFFBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!OZHostidIFFBase)return ENOSYS;
    struct IFFHandle *iff=AllocIFF();
    int error=ENOMEM,opened=0;
    if(iff){
        unsigned char header[6]={0},bytes[8]={0,0,0,1};
        for(int i=7;i>=4;i--){bytes[i]=identity;identity>>=8;}
        iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_WRITE)?EIO:0;opened=!error;
        if(!error&&PushChunk(iff,ID_PREF,ID_FORM,34))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_PRHD,6))error=EIO;
        if(!error&&WriteChunkBytes(iff,header,6)!=6)error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_OZHI,8))error=EIO;
        if(!error&&WriteChunkBytes(iff,bytes,8)!=8)error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(opened)CloseIFF(iff);
        FreeIFF(iff);
    }
    CloseLibrary(OZHostidIFFBase);OZHostidIFFBase=NULL;
    if(!error&&!Flush(file))error=EIO;
    return error;
}
