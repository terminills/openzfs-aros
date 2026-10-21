/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_MOUNTFILE_H
#define OZ_MOUNTFILE_H
#include "mapping.h"
#include <errno.h>
#include <stdio.h>
#include <string.h>
#define OZ_MOUNTFILE_SIZE 1024
static int oz_mountfile_count(const char *s,UQUAD *out)
{
    UQUAD n=0;
    if(!s||!*s)return EINVAL;
    for(;*s;s++){
        if(*s<'0'||*s>'9'||n>((UQUAD)-1-(*s-'0'))/10)return EINVAL;
        n=n*10+(*s-'0');
    }
    if(!n)return EINVAL;
    *out=n;return 0;
}
/* Query snapshot only, not a reservation or proof of filesystem size support.
 * On error the output remains unchanged. Zero requested means whole device. */
static int oz_mountfile_format(const struct oz_device_mapping *m,int ffs,
    UQUAD requested,char *out,size_t size)
{
    if(!m||!out||m->version!=OZ_MAPPING_VERSION||m->structure_size!=sizeof(*m)||
       m->unit>=32||m->unit_count<=m->unit||m->unit_count>32||
       (m->flags&~(OZ_MAPPING_PRESENT|OZ_MAPPING_READONLY))||
       !(m->flags&OZ_MAPPING_PRESENT)||m->sector_size!=512||!m->bytes||m->bytes%512)return ENXIO;
    UQUAD sectors=m->bytes/512;
    if(requested){if(requested>sectors)return EFBIG;sectors=requested;}
    ULONG track=1;
    while(sectors>0x80000000ULL&&!(sectors&1)&&track<65536){sectors/=2;track*=2;}
    if(sectors>0x80000000ULL)return EOVERFLOW;
    char text[OZ_MOUNTFILE_SIZE];
    int n=snprintf(text,sizeof(text),"FileSystem = L:%s-handler\nDevice = openzfs.device\nUnit = %lu\n"
        "Flags = 0\nLowCyl = 0\nHighCyl = %lu\nSurfaces = 1\nBlocksPerTrack = %lu\n"
        "BlockSize = 512\nBuffers = 64\nBufMemType = 1\nStacksize = 65536\n"
        "Priority = 5\nGlobVec = -1\nDosType = 0x%08lx\nActivate = 1\n",
        ffs?"afs":"fat",(unsigned long)m->unit,(unsigned long)(sectors-1),(unsigned long)track,
        ffs?0x444f5301UL:0x46415400UL);
    if(n<0||(size_t)n>=sizeof(text)||(size_t)n>=size)return ENOSPC;
    memcpy(out,text,(size_t)n+1);return 0;
}
#endif
