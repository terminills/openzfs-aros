/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/exec.h>
#include <proto/openzfs.h>
#include <stdio.h>
struct Library *OpenZFSBase;
int main(int argc,char **argv)
{
    OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",2);
    if(!OpenZFSBase){
        fprintf(stderr,"Cannot open openzfs.library version 2; stop the active service before offline debugging.\n");
        return 20;
    }
    LONG result=OpenZFSRunDebugger(argc,argv);
    CloseLibrary(OpenZFSBase);
    return (int)result;
}
