/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <proto/exec.h>
#include <pthread.h>
#include "zpl-fixture.h"
int oz_dos_property_test(struct oz_zpl *);

struct foreign_close {
    struct oz_zpl *fs;
    int result;
};
static void *foreign_close_task(void *argument)
{
    struct foreign_close *test = argument;
    test->result = oz_zpl_close(test->fs);
    return NULL;
}
static int owner_close_test(struct oz_zpl **fs, boolean_t write)
{
    struct foreign_close test = {*fs, -1};
    pthread_t worker;
    int error = pthread_create(&worker, NULL, foreign_close_task, &test);
    if (!error) {
        error = pthread_join(worker, NULL);
        /* Negative control may have freed the mount. Never reuse it. */
        if (!test.result) *fs = NULL;
        if (!error && test.result != EDEADLK) error = EIO;
    }
    char text[128];
    snprintf(text, sizeof (text),
        "Native foreign mount close write=%d errno=%d result=%d\n",
        write, test.result, error);
    for (char *p = text; *p; p++) RawPutChar(*p);
    return error;
}

int oz_mount_lifecycle_test(boolean_t write)
{
    const char *dataset="qemu_mirror/mountlife";
    struct oz_zpl *fs=NULL;
    int error=write?oz_zpl_fixture_create(dataset):0;
    if(!error)error=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    if(!error)error=owner_close_test(&fs,write);
    uint64_t object=0,size=0;
    if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
    if(!error&&write) {
        struct oz_zpl_info owned;
        error=oz_zpl_resolve(fs,oz_zpl_root(fs),"subdir/nested.txt",&owned);
        if(!error&&oz_zpl_set_owner(fs,owned.object,77,88,NULL)!=EACCES)error=EIO;
        if(!error)error=oz_zpl_set_owner(fs,owned.object,77,88,kcred);
        if(oz_zpl_protection_store(fs,object,0,NULL)!=EACCES)error=EIO;
        if(!error)error=oz_zpl_protection_store(fs,object,0x81234567,kcred);
        if(!error)error=oz_zpl_sync(fs);
        if(!error)error=oz_dos_property_test(fs);
        int close=oz_zpl_close(fs);
        if(!close)fs=NULL;
        if(!error)error=close;
        if(!error)error=oz_zpl_open(dataset,&fs);
    }
    struct oz_zpl_info info;
    if(!error)error=oz_zpl_stat(fs,object,&info);
    if(!error&&(!info.has_native_protection||info.native_protection!=0x81234567))error=EIO;
    if(!error&&oz_zpl_protection_store(fs,object,0,kcred)!=EROFS)error=EIO;
    if(!error)error=oz_dos_property_test(fs);
    if(!error)error=oz_zpl_sync(fs);
    int close=oz_zpl_close(fs);if(!error)error=close;
    if(!close)fs=NULL;
    if(!error&&write) {
        for(unsigned pass=0;pass<2&&!error;pass++) {
            error=oz_zpl_open_writable(dataset,&fs);
            if(!error)error=oz_zpl_stat(fs,object,&info);
            if(!error&&(!info.has_native_protection||info.native_protection!=0x81234567))error=EIO;
            if(!error)error=oz_zpl_protection_store(fs,object,0x81234567,kcred);
            if(!error)error=oz_zpl_sync(fs);
            close=oz_zpl_close(fs);
            if(!close)fs=NULL;
            if(!error)error=close;
        }
        char reopen[96];snprintf(reopen,sizeof(reopen),"Native repeated writable reopen result=%d\n",error);
        for(char *p=reopen;*p;p++)RawPutChar(*p);
    }
    if(!error&&!write) {
        struct oz_zpl *denied=NULL;
        if(oz_zpl_open_writable(dataset,&denied)!=EROFS||denied)error=EIO;
        if(denied)oz_zpl_close(denied);
    }
    char text[100];snprintf(text,sizeof(text),"Native writable mount lifecycle write=%d result=%d\n",write,error);
    for(char *p=text;*p;p++)RawPutChar(*p);
    return error;
}
