/* SPDX-License-Identifier: CDDL-1.0 */
#include "title.h"
#include <pthread.h>
#include <proto/dos.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>
#include <stdint.h>
#define CHECK(x) do{if(!(x)){bug("RESULT native CLI title FAIL line=%d\n",__LINE__);return 20;}}while(0)
static void *worker(void *arg)
{
    (void)arg;
    zfs_setproctitle("sending %s (%d%%)","pool/files",42);
    return (void *)(uintptr_t)oz_cli_title_restore();
}
int main(void)
{
    char saved[257],current[257],longname[512];
    CHECK(GetProgramName((STRPTR)saved,sizeof(saved)));
    for(unsigned i=0;i<16;i++) {
        zfs_setproctitle_init(0,NULL,NULL);
        pthread_t thread;void *status=NULL;
        CHECK(!pthread_create(&thread,NULL,worker,NULL));
        CHECK(!pthread_join(thread,&status)&&status==(void *)(uintptr_t)EPERM);
        CHECK(GetProgramName((STRPTR)current,sizeof(current))&&!strcmp(current,"sending pool/files (42%)"));
        memset(longname,'x',sizeof(longname)-1);longname[sizeof(longname)-1]=0;
        zfs_setproctitle("%s",longname);
        CHECK(GetProgramName((STRPTR)current,sizeof(current))&&strlen(current)==255);
        CHECK(!oz_cli_title_restore());
        CHECK(GetProgramName((STRPTR)current,sizeof(current))&&!strcmp(saved,current));
        errno=0;zfs_setproctitle("uninitialized");CHECK(errno==ENOTSUP);
    }
    bug("RESULT native CLI title PASS runs=16 worker=1 bounds=1 restore=1\n");
    return 0;
}
