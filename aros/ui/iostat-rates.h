/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IOSTAT_RATES_H
#define OZ_IOSTAT_RATES_H
#include "api.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
/* Bounded UI history, not a pool-wide atomic snapshot. Evicted samples become
 * baselines, never guessed rates. Each metric/path/role/load has its own clock. */
#define OZ_RATE_SAMPLES 256
struct oz_rate_sample { char pool[256];struct oz_row row;int valid; };
struct oz_rate_history { struct oz_rate_sample samples[OZ_RATE_SAMPLES];unsigned next; };
static void oz_rate_reset(struct oz_rate_history *h){memset(h,0,sizeof(*h));}
static void oz_rate_format(struct oz_rate_history *h,const char *pool,
    const struct oz_row *r,char *text,size_t size)
{
    int bytes=!strcmp(r->kind,"io-bytes");
    if(!bytes&&strcmp(r->kind,"io-ops")){snprintf(text,size,"interval unavailable: not an I/O metric");return;}
    struct oz_rate_sample *previous=NULL;
    for(unsigned i=0;i<OZ_RATE_SAMPLES;i++){
        struct oz_rate_sample *s=&h->samples[i];
        if(!s->valid||strcmp(s->pool,pool)||strcmp(s->row.kind,r->kind)||strcmp(s->row.name,r->name))continue;
        if(!strcmp(r->state,"UNAVAILABLE")){s->valid=0;continue;}
        if(!strcmp(s->row.parent,r->parent)&&!strcmp(s->row.value,r->value))previous=s;
    }
    const char *generation=strstr(r->value," load-guid=");char *end=NULL;
    errno=0;
    unsigned long long load=generation&&generation[11]>='0'&&generation[11]<='9'?strtoull(generation+11,&end,10):0;
    if(errno==ERANGE)load=0;
    if(!strcmp(r->state,"UNAVAILABLE")||!load||!end||(*end&&*end!=' ')||!r->total){
        snprintf(text,size,"interval unavailable: no live generation/timestamp");return;
    }
    if(!previous){
        previous=&h->samples[h->next++%OZ_RATE_SAMPLES];
        snprintf(text,size,"interval baseline: sample again");
    }else if(r->total==previous->row.total){
        snprintf(text,size,"interval unavailable: no elapsed time");return;
    }else if(r->total<previous->row.total||r->used<previous->row.used||r->available<previous->row.available){
        snprintf(text,size,"interval baseline: counters reset");
    }else{
        unsigned long long dt=r->total-previous->row.total;
        long double read=(long double)(r->used-previous->row.used)*1000000000.0L/dt;
        long double write=(long double)(r->available-previous->row.available)*1000000000.0L/dt;
        snprintf(text,size,"interval read=%.2Lf write=%.2Lf %s/s; sample=%llu ns",
            read,write,bytes?"bytes":"operations",dt);
    }
    snprintf(previous->pool,sizeof(previous->pool),"%s",pool);
    previous->row=*r;previous->valid=1;
}
#endif
