/* SPDX-License-Identifier: CDDL-1.0 */
/* Read-only capacity and high-LBA I/O on fresh sparse QEMU fixtures. */
#include "device-io.h"
#include "partitions.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdint.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#ifndef OZ_CAPACITY_TIB
#define OZ_CAPACITY_TIB 3
#endif
#if OZ_CAPACITY_TIB != 3 && OZ_CAPACITY_TIB != 50
#error Unsupported disposable capacity fixture
#endif
int main(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-capacity-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    struct oz_device *disk=NULL;
    const uint64_t size=UINT64_C(1)*OZ_CAPACITY_TIB*1024*1024*1024*1024;
    int rc=oz_device_open("ata.device",0,0,&disk),line=0;
#define CHECK(x) do{if(!(x)){line=__LINE__;goto done;}}while(0)
    CHECK(!rc&&disk);
    CHECK(oz_device_size(disk)==size&&oz_device_sector(disk)==512);
    unsigned char bytes[512];memset(bytes,0xaa,sizeof(bytes));
    CHECK(!oz_device_transfer(disk,bytes,sizeof(bytes),size-sizeof(bytes),0));
    for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==0x5a);
    struct oz_partition_catalog catalog;
    CHECK(!oz_partition_catalog_read("ata.device",1,&catalog));
    CHECK(catalog.disk_sectors==(UINT64_C(3)*1024*1024*1024*1024)/512&&catalog.sector_size==512&&catalog.count==1);
    CHECK(catalog.entries[0].first_sector==UINT64_C(4294969344));
    CHECK(catalog.entries[0].last_sector==UINT64_C(6442446847));
    CHECK(!strcmp(catalog.entries[0].name,"OpenZFS-large"));
    CHECK(oz_device_transfer(disk,bytes,sizeof(bytes),size,0)==EINVAL);
    CHECK(oz_device_transfer(disk,bytes,sizeof(bytes),size-sizeof(bytes),1)==EROFS);
    oz_device_close(disk);disk=NULL;
    CHECK(!oz_device_open_range("ata.device",0,0,size-4096,4096,&disk));
    CHECK(oz_device_size(disk)==4096);
    CHECK(!oz_device_transfer(disk,bytes,sizeof(bytes),4096-sizeof(bytes),0));
    for(unsigned i=0;i<sizeof(bytes);i++)CHECK(bytes[i]==0x5a);
done:
    oz_device_close(disk);
    char text[180];snprintf(text,sizeof(text),
        "RESULT native capacity %s size=%llu last-sector=1 range=1 bounds=1 readonly=1 large-gpt=1 rc=%d line=%d\n",
        line?"FAIL":"PASS",(unsigned long long)size,rc,line);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return line?20:0;
}
