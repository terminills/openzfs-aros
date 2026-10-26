/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SIMPLE_HEALTH_H
#define OZ_SIMPLE_HEALTH_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* Parse the existing status row's exact space-separated decimal field.
 * Missing, duplicate, malformed or overflowing counters are unavailable,
 * never zero. No errno/strtoull ABI dependency is needed for display parsing. */
static int oz_health_counter(const char *text,const char *key,uint64_t *out)
{
    size_t length=strlen(key);int found=0;uint64_t value=0;
    while(*text){
        while(*text==' ')text++;
        const char *end=text+strcspn(text," ");
        if((size_t)(end-text)>length&&!memcmp(text,key,length)&&text[length]=='='){
            if(found++)return 0;
            const char *digit=text+length+1;
            if(digit==end)return 0;
            for(;digit<end;digit++){
                if(*digit<'0'||*digit>'9')return 0;
                unsigned next=(unsigned)(*digit-'0');
                if(value>(UINT64_MAX-next)/10)return 0;
                value=value*10+next;
            }
        }
        text=end;
    }
    if(!found)return 0;
    *out=value;return 1;
}

static void oz_simple_health_row(char *out,size_t size,const char *kind,
    const char *name,const char *state,const char *metadata)
{
    uint64_t reads,writes,checksums;
    if(!oz_health_counter(metadata,"read-errors",&reads)||
       !oz_health_counter(metadata,"write-errors",&writes)||
       !oz_health_counter(metadata,"checksum-errors",&checksums)){
        snprintf(out,size,"%.159s (%.23s): %.47s\n  Error counters unavailable; see Advanced.\n",name,kind,state);
        return;
    }
    snprintf(out,size,"%.159s (%.23s): %.47s\n  Errors: read %llu, write %llu, checksum %llu\n",
        name,kind,state,(unsigned long long)reads,(unsigned long long)writes,
        (unsigned long long)checksums);
}
#endif
