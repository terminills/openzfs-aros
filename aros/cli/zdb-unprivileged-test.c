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
    struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    ULONG logged=secLoginA(login);
    LONG status=20;
    if(logged==((77UL<<16)|88)&&secGetTaskOwner(FindTask(NULL))==logged) {
        bug("RESULT native zdb actual caller uid=77 gid=88\n");
        OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",2);
        if(OpenZFSBase) {
            char *args[]={"zdb","-l","/AROS/ata.device/0",NULL};
            status=OpenZFSRunDebugger(3,args);
            CloseLibrary(OpenZFSBase);
        }
    }
    if(logged&&secLogoutA(logout)!=previous)status=20;
    if(secGetTaskOwner(FindTask(NULL))!=previous)status=20;
    CloseLibrary(secBase);
    return status;
}
