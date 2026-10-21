/* SPDX-License-Identifier: CDDL-1.0 */
#include "preferences.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/iffparse.h>
#include <prefs/prefhdr.h>
#include <errno.h>
#include <string.h>

/* Called only by the owning GUI/test task, never a backend worker. */
struct Library *IFFParseBase;
#define ID_OZUI MAKE_ID('O','Z','U','I')
static unsigned get16(const unsigned char *p){return ((unsigned)p[0]<<8)|p[1];}
static void put16(unsigned char *p,unsigned v){p[0]=(unsigned char)(v>>8);p[1]=(unsigned char)v;}
static int valid(const struct oz_preferences *p)
{return p&&p->category<8&&p->width>=320&&p->width<=16384&&p->height>=200&&p->height<=16384&&p->view_mode<=OZ_UI_ADVANCED;}
void oz_preferences_default(struct oz_preferences *p)
{if(p){p->category=0;p->width=780;p->height=550;p->view_mode=OZ_UI_SIMPLE;}}

int oz_preferences_read(BPTR file,struct oz_preferences *out)
{
    if(!file||!out)return EINVAL;
    if(Seek(file,0,OFFSET_END)==-1)return EIO;
    LONG length=Seek(file,0,OFFSET_BEGINNING);
    if(length<0)return EIO;
    if(length<46||length>4096)return EINVAL;
    IFFParseBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!IFFParseBase)return ENOSYS;
    struct IFFHandle *iff=AllocIFF();int error=ENOMEM,opened=0;
    struct oz_preferences candidate={0};unsigned root=0,header=0,settings=0;
    if(iff){
        iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_READ)?EINVAL:0;opened=!error;
        while(!error){
            LONG rc=ParseIFF(iff,IFFPARSE_STEP);
            if(rc==IFFERR_EOF)break;
            if(rc==IFFERR_EOC)continue;
            if(rc){error=EINVAL;break;}
            struct ContextNode *cn=CurrentChunk(iff);
            if(!cn||cn->cn_Size<0){error=EINVAL;break;}
            if(iff->iff_Depth==1){
                if(root++||cn->cn_ID!=ID_FORM||cn->cn_Type!=ID_PREF||cn->cn_Size!=length-8)error=EINVAL;
            }else if(iff->iff_Depth!=2||!root||cn->cn_Type!=ID_PREF||
                cn->cn_ID==ID_FORM||cn->cn_ID==ID_LIST||cn->cn_ID==ID_CAT){error=EINVAL;}
            else if(cn->cn_ID==ID_PRHD){
                unsigned char bytes[6];
                if(header++||settings||cn->cn_Size!=sizeof(bytes)||
                    ReadChunkBytes(iff,bytes,sizeof(bytes))!=sizeof(bytes))error=EINVAL;
                else for(unsigned i=0;i<sizeof(bytes);i++)if(bytes[i])error=ENOTSUP;
            }else if(cn->cn_ID==ID_OZUI){
                unsigned char bytes[12];
                if(!header||settings++||cn->cn_Size!=sizeof(bytes)||
                    ReadChunkBytes(iff,bytes,sizeof(bytes))!=sizeof(bytes))error=EINVAL;
                else if((get16(bytes)!=1&&get16(bytes)!=2)||get16(bytes+2)||
                    (get16(bytes)==1&&get16(bytes+6)))error=ENOTSUP;
                else{
                    candidate.category=get16(bytes+4);candidate.width=get16(bytes+8);
                    candidate.height=get16(bytes+10);
                    /* Existing v1 users retain the original advanced view. */
                    candidate.view_mode=get16(bytes)==1?OZ_UI_ADVANCED:get16(bytes+6);
                    if(!valid(&candidate))error=EINVAL;
                }
            } /* Unknown bounded leaf chunks are reserved for extensions. */
        }
        if(!error&&(root!=1||header!=1||settings!=1))error=EINVAL;
        if(opened)CloseIFF(iff);
        FreeIFF(iff);
    }
    CloseLibrary(IFFParseBase);IFFParseBase=NULL;
    if(!error)*out=candidate;
    return error;
}

int oz_preferences_write(BPTR file,const struct oz_preferences *p)
{
    if(!file||!valid(p))return EINVAL;
    IFFParseBase=OpenLibrary((CONST_STRPTR)"iffparse.library",39);
    if(!IFFParseBase)return ENOSYS;
    struct IFFHandle *iff=AllocIFF();int error=ENOMEM,opened=0;
    if(iff){
        unsigned char header[6]={0},bytes[12]={0};
        put16(bytes,2);put16(bytes+4,p->category);put16(bytes+6,p->view_mode);
        put16(bytes+8,p->width);put16(bytes+10,p->height);
        iff->iff_Stream=(IPTR)file;InitIFFasDOS(iff);
        error=OpenIFF(iff,IFFF_WRITE)?EIO:0;opened=!error;
        if(!error&&PushChunk(iff,ID_PREF,ID_FORM,38))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_PRHD,sizeof(header)))error=EIO;
        if(!error&&WriteChunkBytes(iff,header,sizeof(header))!=sizeof(header))error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PushChunk(iff,ID_PREF,ID_OZUI,sizeof(bytes)))error=EIO;
        if(!error&&WriteChunkBytes(iff,bytes,sizeof(bytes))!=sizeof(bytes))error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(!error&&PopChunk(iff))error=EIO;
        if(opened)CloseIFF(iff);
        FreeIFF(iff);
    }
    CloseLibrary(IFFParseBase);IFFParseBase=NULL;
    return error;
}
