/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SIMPLE_POOLS_H
#define OZ_SIMPLE_POOLS_H
#include <stdint.h>
#include <stdio.h>
#include <string.h>

static void oz_simple_bytes(char *out, size_t size, uint64_t bytes)
{
    static const char *units[]={"B","KiB","MiB","GiB","TiB","PiB","EiB"};
    uint64_t divisor=1;unsigned unit=0;
    while(unit<6 && bytes/divisor>=1024){divisor*=1024;unit++;}
    if(!unit)snprintf(out,size,"%llu B",(unsigned long long)bytes);
    else snprintf(out,size,"%llu.%llu %s",(unsigned long long)(bytes/divisor),
        (unsigned long long)((bytes%divisor)*10/divisor),units[unit]);
}

static void oz_simple_pool_row(char *out,size_t size,const char *name,
    const char *state,uint64_t used,uint64_t available,uint64_t total)
{
    char u[40],a[40],t[40];
    if(!strcmp(state,"UNAVAILABLE")||used>total||available!=total-used){
        snprintf(out,size,"%.159s : %.47s\n  Capacity unavailable.\n",name,state);
        return;
    }
    oz_simple_bytes(u,sizeof(u),used);oz_simple_bytes(a,sizeof(a),available);
    oz_simple_bytes(t,sizeof(t),total);
    snprintf(out,size,"%.159s : %.47s\n  Size: %s   Allocated: %s   Free: %s\n",name,state,t,u,a);
}
#endif
