/* SPDX-License-Identifier: CDDL-1.0 */
/* Private QEMU fixture helper only, never linked into the service/library.
 * The runner owns all three disposable disk overlays. The CLI test offlines
 * source member 0 (and 3 on return) before copying it. Member 1 is copied as a
 * wrong-identity negative control, not as an importable consistent snapshot.
 * No arbitrary caller path/unit, mount inhibition or partition writes. */
#include "device-io.h"
#include "partition-io.h"
#include <stdlib.h>
#include <errno.h>
#include <aros/debug.h>
int oz_test_repath_copy(unsigned source,unsigned destination)
{
    if(!((source<=1&&destination==3)||(source==3&&destination==0)))return EINVAL;
    struct oz_device *from=NULL,*to=NULL;
    void *buffer=malloc(1024*1024);
    int error=buffer?0:ENOMEM;
    if(!error)error=oz_device_open_partition("ata.device",source,0,0,&from);
    if(!error)error=oz_device_open_partition("ata.device",destination,0,1,&to);
    const uint64_t bytes=256ULL*1024*1024;
    if(!error&&(oz_device_size(from)!=bytes||oz_device_size(to)!=bytes))error=EINVAL;
    for(uint64_t offset=0;!error&&offset<bytes;offset+=1024*1024) {
        error=oz_device_transfer(from,buffer,1024*1024,offset,0);
        if(!error)error=oz_device_transfer(to,buffer,1024*1024,offset,1);
    }
    if(!error)error=oz_device_flush(to);
    oz_device_close(to);oz_device_close(from);free(buffer);
    bug("Native repath fixture copy source=%u destination=%u bytes=%llu errno=%d\n",
        source,destination,(unsigned long long)bytes,error);
    return error;
}
