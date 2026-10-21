/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/openzfs.h>
#include <aros/debug.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <stdlib.h>
#include <string.h>
struct Library *OpenZFSBase;
int main(int argc,char **argv)
{
    OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",1);
    if(argc==2&&!strcmp(argv[1],"FOREIGN")){
        if(!OpenZFSBase)return 0;
        CloseLibrary(OpenZFSBase);return 20;
    }
    if(!OpenZFSBase)return 20;
    if(argc==2&&!strcmp(argv[1],"DENIED")){
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase){CloseLibrary(OpenZFSBase);return 20;}
        struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        int ok=secIsConfigured()&&secLoginA(login)==((77UL<<16)|88);
        if(ok)ok=OpenZFSRunService(1)==20&&OpenZFSRunService(1)==10;
        CloseLibrary(OpenZFSBase);CloseLibrary(secBase);
        /* Explicit libc exit after returning from and unloading the engine. */
        exit(ok?7:20);
    }
    int result=20;
    if(OpenZFSRunService(2)!=20||OpenZFSRunService((ULONG)-1)!=20)goto done;
    if(argc==2&&!strcmp(argv[1],"REOPEN")){result=0;goto done;}
    if(argc!=1)goto done;
    struct Library *second=OpenLibrary((CONST_STRPTR)"openzfs.library",1);
    if(!second)goto done;
    int same=second==OpenZFSBase;
    CloseLibrary(second);
    if(!same)goto done;
    if(SystemTags((CONST_STRPTR)"C:OpenZFSLibraryTest FOREIGN",TAG_DONE)!=0)goto done;
    CloseLibrary(OpenZFSBase);OpenZFSBase=NULL;
    /* This child is a different task. It can acquire the module only after
     * the last owner reference has closed and a fresh module initializes. */
    for(unsigned cycle=0;cycle<8;cycle++) {
        if(SystemTags((CONST_STRPTR)"C:OpenZFSLibraryTest REOPEN",TAG_DONE)!=0)return 20;
        if(SystemTags((CONST_STRPTR)"C:OpenZFSLibraryTest DENIED",TAG_DONE)!=7)return 20;
        /* Reacquire in the original task too: a child's denied start must
         * not retain ownership or leave the one-shot claim in a new load. */
        OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",1);
        if(!OpenZFSBase)return 20;
        int valid=OpenZFSRunService(2)==20;
        CloseLibrary(OpenZFSBase);OpenZFSBase=NULL;
        if(!valid)return 20;
        bug("RESULT shared engine lifecycle cycle=%u PASS\n",cycle+1);
    }
    bug("RESULT shared engine denied return PASS status=20 repeated=10 caller-exit=7\n");
    bug("RESULT shared engine library lifecycle PASS invalid-mode=2 nested-open=1 foreign-refused=1 reload-other-task=1\n");
    return 0;
done:
    CloseLibrary(OpenZFSBase);
    return result;
}
