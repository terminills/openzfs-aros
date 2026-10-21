/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzutil.h>
#include <errno.h>
#include <time.h>
#include <aros/debug.h>
#include <proto/exec.h>
#include <dos/dos.h>
static double elapsed(const struct timespec *a,const struct timespec *b)
{return (double)(b->tv_sec-a->tv_sec)+(double)(b->tv_nsec-a->tv_nsec)/1e9;}
#define CHECK(x) do{if(!(x)){bug("Native device wait failure line=%d errno=%d\n",__LINE__,errno);return EIO;}}while(0)
int oz_upstream_device_wait_test(int denied)
{
    if(denied) {
        CHECK(zpool_label_disk_wait("/AROS/ata.device/0",500)==EACCES);
        bug("Public native device wait PASS denied=1\n");return 0;
    }
    CHECK(zpool_label_disk_wait(NULL,100)==EINVAL);
    CHECK(zpool_label_disk_wait("",100)==EINVAL);
    CHECK(zpool_label_disk_wait("/AROS/ata.device/0",-1)==EINVAL);
    CHECK(zpool_label_disk_wait("/AROS/ata.device/-1",100)==EINVAL);
    CHECK(zpool_label_disk_wait("/AROS/ata.device/1/partition/0",500)==EIO);
    CHECK(zpool_label_disk_wait("/AROS/ata.device/0",0)==ENODEV);
    const char *ready[]={"/AROS/ata.device/0","/AROS/ata.device/1",
        "/AROS/ata.device/0/partition/2","RAM:OZPublicPool/disk0"};
    struct timespec before,after;
    for(unsigned i=0;i<4;i++) {
        CHECK(!clock_gettime(CLOCK_MONOTONIC,&before));
        CHECK(!zpool_label_disk_wait(ready[i],1000));
        CHECK(!clock_gettime(CLOCK_MONOTONIC,&after));
        CHECK(elapsed(&before,&after)>=0.045&&elapsed(&before,&after)<2.0);
    }
    CHECK(!zpool_disk_wait("/AROS/ata.device/0"));
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&before));
    CHECK(zpool_label_disk_wait("/AROS/openzfs-absent.device/0",80)==ENODEV);
    CHECK(!clock_gettime(CLOCK_MONOTONIC,&after));
    CHECK(elapsed(&before,&after)>=0.075&&elapsed(&before,&after)<2.0);
    SetSignal(SIGBREAKF_CTRL_C,SIGBREAKF_CTRL_C);
    CHECK(zpool_label_disk_wait("/AROS/ata.device/0",1000)==EINTR);
    CHECK(!(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C));
    bug("Public native device wait PASS denied=0 ready=4 settle=1 timeout=1 break=1 errors=1\n");
    return 0;
}
