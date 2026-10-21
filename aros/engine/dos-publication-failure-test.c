/* SPDX-License-Identifier: CDDL-1.0 */
/* Private fixture: fail DOS publication and detached-close cleanup. */
#include <errno.h>
#include <string.h>
#include <aros/debug.h>
#include <proto/dos.h>
#include "zpl-fixture.h"
#include "dos-handler.h"
static struct oz_zpl *failed,*detached;
static int injected,detach_injected;
int __real_oz_dos_mount_shared(struct oz_zpl *,const char *,struct oz_mount **,int,int);
int __wrap_oz_dos_mount_shared(struct oz_zpl *fs,const char *name,struct oz_mount **out,int auth,int signal)
{
    if(!injected&&!strcmp(name,"OZPUBFAIL")) {
        injected=1;failed=fs;*out=NULL;
        bug("DOS PUBLICATION FIXTURE registration errno=17\n");
        return EEXIST;
    }
    int error=__real_oz_dos_mount_shared(fs,name,out,auth,signal);
    if(!error&&!detach_injected&&!strcmp(name,"OZCLOSEFAIL")) {
        detach_injected=1;detached=fs;
        bug("DOS RETIRED CLOSE FIXTURE armed sentinel=RAM:openzfs-retired-close-failure\n");
    }
    return error;
}
int __real_oz_zpl_close(struct oz_zpl *);
int __wrap_oz_zpl_close(struct oz_zpl *fs)
{
    if(fs&&fs==failed) {
        failed=NULL;
        bug("DOS PUBLICATION FIXTURE cleanup errno=5\n");
        return EIO;
    }
    if(fs&&fs==detached) {
        BPTR sentinel=Lock((CONST_STRPTR)"RAM:openzfs-retired-close-failure",ACCESS_READ);
        if(sentinel) {
            UnLock(sentinel);
            bug("DOS RETIRED CLOSE FIXTURE cleanup errno=5 sentinel=present\n");
            return EIO;
        }
        detached=NULL;
    }
    return __real_oz_zpl_close(fs);
}
