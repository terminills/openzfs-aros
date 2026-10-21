/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzutil.h>
#include <errno.h>
#include <stdlib.h>
#include <time.h>
#include <pthread.h>
#include <proto/exec.h>
#include <proto/dos.h>

static int milliseconds(uint64_t *out)
{
    struct timespec now;
    if(clock_gettime(CLOCK_MONOTONIC,&now))return errno?errno:EIO;
    if(now.tv_sec<0||now.tv_nsec<0||now.tv_nsec>=1000000000)return EIO;
    if((uint64_t)now.tv_sec>(UINT64_MAX-999)/1000)return EOVERFLOW;
    *out=(uint64_t)now.tv_sec*1000+(uint64_t)now.tv_nsec/1000000;
    return 0;
}

int zpool_label_disk_wait(const char *path,int timeout_ms)
{
    if(!path||!*path||timeout_ms<0)return EINVAL;
    uint64_t start=0,stable=0,now=0;
    int seen=0,error=milliseconds(&start);
    if(error)return error;
    for(;;) {
        pthread_testcancel();
        if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C)return EINTR;
        errno=0;
        char *resolved=zfs_get_underlying_path(path);
        int query_error=resolved?0:(errno?errno:EIO);
        free(resolved);
        if(query_error&&query_error!=ENOENT&&query_error!=ENODEV)return query_error;
        error=milliseconds(&now);
        if(error)return error;
        if(now<start)return EIO;
        /* Like upstream's non-udev implementation, require 50 ms of successful
         * resolution. A failed observation resets the settle interval. These
         * are completed read-only queries, not a hardware identity guarantee.
         * A stalled device/service I/O itself is not preempted by this loop. */
        if(query_error)seen=0;
        else if(!seen){stable=now;seen=1;}
        else if(now-stable>=50&&now-start<=(uint64_t)timeout_ms)return 0;
        if(now-start>=(uint64_t)timeout_ms)return ENODEV;
        Delay(1);
    }
}

int zpool_disk_wait(const char *path)
{
    return zpool_label_disk_wait(path,DISK_LABEL_WAIT);
}
