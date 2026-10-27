/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable fixture account, authenticated in the actual debugger caller. */
#include <proto/exec.h>
#include <proto/security.h>
#include <proto/openzfs.h>
#include <libraries/security.h>
#include <aros/debug.h>
struct Library *OpenZFSBase;
int main(void)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    ULONG previous=secGetTaskOwner(FindTask(NULL));
#ifndef OZ_ZDB_NO_SECURITY
    struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
#endif
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    ULONG logged=0;
#ifdef OZ_ZDB_NO_SECURITY
    int allowed=!secIsConfigured();
    if(allowed)bug("RESULT native zdb actual caller security-unconfigured=1\n");
#else
    logged=secLoginA(login);
    int allowed=logged==((77UL<<16)|88)&&secGetTaskOwner(FindTask(NULL))==logged;
    if(allowed)bug("RESULT native zdb actual caller uid=77 gid=88\n");
#endif
    LONG status=20;
    if(allowed) {
        OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",2);
        if(OpenZFSBase) {
#ifdef OZ_ZDB_NATIVE_POOL
            char *args[]={"zdb","-P","-e","-p","/AROS/ata.device/0/partition/0",
                "-p","/AROS/ata.device/0/partition/2","-bcc","topo_source",NULL};
            status=OpenZFSRunDebugger(9,args);
#else
            char *args[]={"zdb","-l",
#ifdef OZ_ZDB_PARTITIONS
                "/AROS/ata.device/0/partition/0",
#else
                "/AROS/ata.device/0",
#endif
                NULL};
            status=OpenZFSRunDebugger(3,args);
#endif
            CloseLibrary(OpenZFSBase);
        }
    }
    if(logged&&secLogoutA(logout)!=previous)status=20;
    if(secGetTaskOwner(FindTask(NULL))!=previous)status=20;
    CloseLibrary(secBase);
    return status;
}
