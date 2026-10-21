/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/exec.h>
#include <stdio.h>
#include <string.h>
#include "../ui/partition-api.h"
int main(void)
{
    static struct oz_storage_inventory c;char line[600];
    int error=oz_storage_inventory_native(&c);unsigned disks=0;
    for(unsigned i=0;i<c.count;i++){
        struct oz_storage_unit *r=&c.units[i];
        if(!strcmp(r->device,"ata.device")&&r->unit<2)disks|=1U<<r->unit;
        snprintf(line,sizeof(line),"Inventory %s/%lu type=%lu model=%s serial=%s\n",
            r->device,(unsigned long)r->unit,(unsigned long)r->type,r->model,r->serial);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    snprintf(line,sizeof(line),"RESULT HIDD inventory %s count=%u version=%u ata-mask=%u errno=%d\n",
        !error&&c.version==1&&disks==3?"PASS":"FAIL",c.count,c.version,disks,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error||c.version!=1||disks!=3?20:0;
}
