/* SPDX-License-Identifier: CDDL-1.0 */
/* Test-only authentication against the disposable QEMU security fixture. */
#include <proto/exec.h>
#include <proto/security.h>
#include <proto/openzfs.h>
#include <libraries/security.h>
#include <aros/debug.h>
struct Library *OpenZFSBase;
int main(int argc,char **argv)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    ULONG previous=secGetTaskOwner(FindTask(NULL));
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    ULONG logged=secLoginA(login);
    LONG status=20;
    if(logged==secOWNER_SYSTEM&&secGetTaskOwner(FindTask(NULL))==logged) {
        bug("RESULT native zdb actual caller root=1\n");
        OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",2);
        if(OpenZFSBase) {
            status=OpenZFSRunDebugger(argc,argv);
            CloseLibrary(OpenZFSBase);
        }
    }
    if(logged&&secLogoutA(logout)!=previous)status=20;
    if(secGetTaskOwner(FindTask(NULL))!=previous)status=20;
    CloseLibrary(secBase);
    return status;
}
