/* SPDX-License-Identifier: CDDL-1.0 */
#include "device-path.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#ifdef __AROS__
#include <proto/exec.h>
#endif
static void report(const char *s)
{
    puts(s);
#ifdef __AROS__
    while(*s)RawPutChar(*s++);
    RawPutChar('\n');
#endif
}
#define CHECK(x) do {if(!(x)){printf("line=%d\n",__LINE__);report("RESULT native device paths FAIL");return 20;}}while(0)
int main(void)
{
    static const char *bad[]={"", "/AROS/", "/AROS/.device/0",
        "/AROS/ata.device/", "/AROS/ata.device/-1", "/AROS/ata.device/+1",
        "/AROS/ata.device/00", "/AROS/ata.device/2147483648",
        "/AROS/ata.device/999999999999999999999999", "/AROS/ata.device/0/",
        "/AROS/ata.device/0/partition/", "/AROS/ata.device/0/partition/01",
        "/AROS/ata.device/0/partition/4294967295", "/AROS/ata.device/0/partition/-1",
        "/AROS/ata.device/0/partition/0/extra", "/AROS/ata.device/0/partition/0x1",
        "/AROS/SYS:ata.device/0", "/AROS/ata.device /0", "/AROS/../ata.device/0",
        "/aros/ata.device/0", "/dev/nvme0n1p1", "Work:disk1", "Work:disk-part1"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(bad[0]);i++) {
        struct oz_device_path out,old;memset(&out,0xa5,sizeof(out));old=out;
        CHECK(oz_device_path_parse(bad[i],&out)!=0&&!memcmp(&out,&old,sizeof(out)));
        char *copy=zfs_strip_partition(bad[i]);CHECK(copy&&!strcmp(copy,bad[i]));free(copy);
        CHECK(zfs_strip_path(bad[i])==bad[i]);
        char buffer[256],before[256];memset(buffer,0xcc,sizeof(buffer));
        size_t length=strlen(bad[i]);CHECK(length<sizeof(buffer));memcpy(buffer,bad[i],length+1);
        memcpy(before,buffer,sizeof(buffer));CHECK(zfs_append_partition(buffer,sizeof(buffer))==-1);
        CHECK(!memcmp(buffer,before,sizeof(buffer)));
    }
    const char *path="/AROS/nvme.device/2147483647/partition/4294967294";
    struct oz_device_path parsed;
    CHECK(!oz_device_path_parse(path,&parsed));
    CHECK(!strcmp(parsed.device,"nvme.device")&&parsed.unit==2147483647&&
        parsed.position==4294967294U&&parsed.partition);
    char *copy=zfs_strip_partition(path);CHECK(copy&&!strcmp(copy,"/AROS/nvme.device/2147483647"));free(copy);
    CHECK(!strcmp(zfs_strip_path(path),"nvme.device/2147483647/partition/4294967294"));
    const char *whole="/AROS/ata.device/0",*part="/AROS/ata.device/0/partition/0";
    for(size_t n=0;n<=strlen(part)+2;n++) {
        char buffer[128],before[128];memset(buffer,0x5a,sizeof(buffer));strcpy(buffer,whole);
        memcpy(before,buffer,sizeof(buffer));
        int result=zfs_append_partition(buffer,n);
        if(n<=strlen(part))CHECK(result==-1&&!memcmp(buffer,before,sizeof(buffer)));
        else CHECK(result==(int)strlen(part)&&!strcmp(buffer,part));
    }
    char already[128];strcpy(already,part);
    CHECK(zfs_append_partition(already,sizeof(already))==-1&&!strcmp(already,part));
    CHECK(!oz_device_path_parse(whole,&parsed)&&!parsed.partition&&parsed.unit==0);
    CHECK(oz_device_path_parse(NULL,&parsed)==EINVAL&&oz_device_path_parse(whole,NULL)==EINVAL);
    CHECK(zfs_strip_path(NULL)==NULL&&zfs_strip_partition(NULL)==NULL);
    CHECK(zfs_append_partition(NULL,0)==-1);
    char longest[320];memcpy(longest,"/AROS/",6);memset(longest+6,'a',248);
    memcpy(longest+254,".device/0",10);
    CHECK(!oz_device_path_parse(longest,&parsed)&&strlen(parsed.device)==255);
    memset(longest+6,'a',249);memcpy(longest+255,".device/0",10);
    struct oz_device_path previous=parsed;
    CHECK(oz_device_path_parse(longest,&parsed)==ENAMETOOLONG&&
        !memcmp(&parsed,&previous,sizeof(parsed)));
    report("RESULT native device paths PASS grammar=1 bounds=1 unchanged=1 file-names=1");return 0;
}
