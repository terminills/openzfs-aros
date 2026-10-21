/* SPDX-License-Identifier: CDDL-1.0 */
#define IFFParseBase OZStartupIFFBase
#include "startup-settings.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <prefs/prefhdr.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct Library *OZStartupIFFBase;
/* Owning startup/UI task only; this library base is not shared by workers. */
#define ID_OZBT MAKE_ID('O','Z','B','T')
#define PREFIX 272
#define MAX_PAYLOAD (PREFIX+256*OZ_STARTUP_PATHS)
static unsigned get16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static void put16(unsigned char *p,unsigned n){p[0]=n>>8;p[1]=n;}
static int string_valid(const char *s)
{
    const char *end=memchr(s,0,256);
    if(!end||end==s)return 0;
    for(const char *p=s;p<end;p++)if((unsigned char)*p<32||(unsigned char)*p==127)return 0;
    return 1;
}
static int valid(const struct oz_startup_settings *p)
{
    if(!p||!p->guid||(p->flags&~7)||!p->count||p->count>OZ_STARTUP_PATHS||!string_valid(p->pool))return 0;
    if(strpbrk(p->pool,"/@#"))return 0;
    for(unsigned i=0;i<p->count;i++) {
        if(!string_valid(p->paths[i])||(p->paths[i][0]!='/'&&!strchr(p->paths[i],':')))return 0;
        for(unsigned j=0;j<i;j++)if(!strcmp(p->paths[i],p->paths[j]))return 0;
    }
    return 1;
}
static int decode(const unsigned char *bytes,unsigned size,struct oz_startup_settings *p)
{
    if(size<PREFIX||get16(bytes)!=1||get16(bytes+14))return EINVAL;
    p->flags=get16(bytes+2);p->count=get16(bytes+12);
    if(p->count>OZ_STARTUP_PATHS||size!=PREFIX+256U*p->count)return EINVAL;
    for(unsigned i=0;i<8;i++)p->guid=(p->guid<<8)|bytes[4+i];
    memcpy(p->pool,bytes+16,256);
    for(unsigned i=0;i<p->count;i++)memcpy(p->paths[i],bytes+PREFIX+256*i,256);
    if(!valid(p))return EINVAL;
    /* Canonical zero padding: no hidden payload after any terminator. */
    for(unsigned i=0;i<=p->count;i++) {
        const char *s=i?p->paths[i-1]:p->pool;
        for(unsigned n=strlen(s)+1;n<256;n++)if(s[n])return EINVAL;
    }
    return 0;
}
int oz_startup_settings_read(BPTR file,struct oz_startup_settings *out)
{
    if(!file||!out)return EINVAL;
    if(Seek(file,0,OFFSET_END)==-1)return EIO;
    LONG length=Seek(file,0,OFFSET_BEGINNING);
    if(length<0)return EIO;
    if(length<34+PREFIX+256||length>16384)return EINVAL;
    OZStartupIFFBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!OZStartupIFFBase)return ENOSYS;
    struct IFFHandle *iff=AllocIFF();
    struct oz_startup_settings *p=calloc(1,sizeof(*p));
    unsigned char *bytes=malloc(MAX_PAYLOAD);
    int error=ENOMEM,opened=0;unsigned root=0,header=0,settings=0;
    if(iff&&p&&bytes) {
        iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_READ)?EINVAL:0;opened=!error;
        while(!error) {
            LONG rc=ParseIFF(iff,IFFPARSE_STEP);
            if(rc==IFFERR_EOF)break;
            if(rc==IFFERR_EOC)continue;
            if(rc){error=EINVAL;break;}
            struct ContextNode *cn=CurrentChunk(iff);
            if(!cn||cn->cn_Size<0){error=EINVAL;break;}
            if(iff->iff_Depth==1) {
                if(root++||cn->cn_ID!=ID_FORM||cn->cn_Type!=ID_PREF||cn->cn_Size!=length-8)error=EINVAL;
            } else if(iff->iff_Depth!=2||cn->cn_Type!=ID_PREF||cn->cn_ID==ID_FORM||cn->cn_ID==ID_LIST||cn->cn_ID==ID_CAT)error=EINVAL;
            else if(cn->cn_ID==ID_PRHD) {
                unsigned char h[6];
                if(header++||settings||cn->cn_Size!=6||ReadChunkBytes(iff,h,6)!=6)error=EINVAL;
                else for(unsigned i=0;i<6;i++)if(h[i])error=ENOTSUP;
            } else if(cn->cn_ID==ID_OZBT) {
                if(!header||settings++||cn->cn_Size>MAX_PAYLOAD||ReadChunkBytes(iff,bytes,cn->cn_Size)!=cn->cn_Size)error=EINVAL;
                else error=decode(bytes,cn->cn_Size,p);
            }
        }
        if(!error&&(root!=1||header!=1||settings!=1))error=EINVAL;
        if(opened)CloseIFF(iff);
    }
    if(iff)FreeIFF(iff);
    CloseLibrary(OZStartupIFFBase);OZStartupIFFBase=NULL;
    if(!error)*out=*p;
    free(p);free(bytes);return error;
}
int oz_startup_settings_write(BPTR file,const struct oz_startup_settings *p)
{
    if(!file||!valid(p))return EINVAL;
    unsigned size=PREFIX+256*p->count;
    unsigned char *bytes=calloc(1,size);if(!bytes)return ENOMEM;
    put16(bytes,1);put16(bytes+2,p->flags);put16(bytes+12,p->count);
    uint64_t guid=p->guid;for(int i=7;i>=0;i--){bytes[4+i]=guid;guid>>=8;}
    memcpy(bytes+16,p->pool,strlen(p->pool));
    for(unsigned i=0;i<p->count;i++)memcpy(bytes+PREFIX+256*i,p->paths[i],strlen(p->paths[i]));
    OZStartupIFFBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!OZStartupIFFBase){free(bytes);return ENOSYS;}
    struct IFFHandle *iff=AllocIFF();int error=ENOMEM,opened=0;
    if(iff) {
        unsigned char header[6]={0};iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_WRITE)?EIO:0;opened=!error;
        if(!error&&PushChunk(iff,ID_PREF,ID_FORM,26+size))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_PRHD,6))error=EIO;
        if(!error&&WriteChunkBytes(iff,header,6)!=6)error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_OZBT,size))error=EIO;
        if(!error&&WriteChunkBytes(iff,bytes,size)!=(LONG)size)error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(opened)CloseIFF(iff);
        FreeIFF(iff);
    }
    CloseLibrary(OZStartupIFFBase);OZStartupIFFBase=NULL;free(bytes);return error;
}
