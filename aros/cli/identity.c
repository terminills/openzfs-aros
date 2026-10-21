/* SPDX-License-Identifier: CDDL-1.0 */
/* CLI preflight identity only. Service packets still authenticate every
 * operation independently; these values grant no engine authority. */
#include <sys/types.h>
#include <unistd.h>
#include <errno.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>

static uid_t native_uid(void)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase){errno=ENOSYS;return (uid_t)-1;}
    if(!secIsConfigured()){CloseLibrary(secBase);errno=EACCES;return (uid_t)-1;}
    ULONG owner=secGetTaskOwner(FindTask(NULL));
    CloseLibrary(secBase);
    UWORD uid=owner>>16;
    return uid==secROOT_UID?0:uid==secNOBODY_UID?65535:uid;
}
/* security.library exposes the task owner, not independent real/effective
 * POSIX UIDs. As in packet credentials, do not invent a different real UID. */
uid_t getuid(void){return native_uid();}
uid_t geteuid(void){return native_uid();}
