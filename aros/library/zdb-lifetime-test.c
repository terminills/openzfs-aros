/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/exec.h>
#include <proto/openzfs.h>
#include <aros/debug.h>
#include <stdlib.h>
struct Library *OpenZFSBase;
int main(void)
{
    char *args[]={"zdb","-l","/SYS/zdb-member",NULL};
    char *bad[]={NULL};
    for(unsigned cycle=0;cycle<2;cycle++) {
        OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",2);
        if(!OpenZFSBase)return 20;
        int ok=OpenZFSRunDebugger(0,args)==20 &&
            OpenZFSRunDebugger(1025,args)==20 &&
            OpenZFSRunDebugger(1,NULL)==20 &&
            OpenZFSRunDebugger(1,bad)==20 &&
            OpenZFSRunDebugger(2,args)==20 &&
            OpenZFSRunService(2)==20;
        if(ok)ok=OpenZFSRunDebugger(3,args)==0;
        if(ok)ok=OpenZFSRunDebugger(3,args)==10 && OpenZFSRunService(0)==10;
        CloseLibrary(OpenZFSBase);OpenZFSBase=NULL;
        if(!ok){bug("RESULT shared zdb lifecycle FAIL cycle=%u\n",cycle);return 20;}
        bug("RESULT shared zdb lifecycle PASS cycle=%u invalid=5 repeated=10 service=10\n",cycle);
    }
    /* Detect a stale exit target into the library or its freed large stack. */
    exit(7);
}
