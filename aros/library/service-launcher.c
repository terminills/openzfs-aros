/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/exec.h>
#include <proto/openzfs.h>
#include <stdio.h>
#include <string.h>
struct Library *OpenZFSBase;
int main(int argc,char **argv)
{
    if(argc<1||argc>2||(argc==2&&strcmp(argv[1],"WRITABLE"))){
        fprintf(stderr,"Invalid startup mode. Use WRITABLE or no argument.\n");
        return 20;
    }
    OpenZFSBase=OpenLibrary((CONST_STRPTR)"openzfs.library",1);
    if(!OpenZFSBase){fprintf(stderr,"Cannot open openzfs.library version 1.\n");return 20;}
    LONG result=OpenZFSRunService(argc==2);
    CloseLibrary(OpenZFSBase);
    return (int)result;
}
