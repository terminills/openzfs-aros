/* SPDX-License-Identifier: CDDL-1.0 */
#include <proto/dos.h>
#include <aros/debug.h>
#include <string.h>
#include "process.h"
static int run(const char *path,const char *arg,long status,const char *needle)
{
    BPTR out=Open((CONST_STRPTR)"RAM:whole-command-output",MODE_NEWFILE);
    if(!out)return 1;
    char *args[]={(char *)path,(char *)arg,NULL};
    struct oz_cli_result result={0};
    int error=oz_cli_spawn_exact(path,args,out,out,&result);
    Close(out);
    out=Open((CONST_STRPTR)"RAM:whole-command-output",MODE_OLDFILE);
    char buffer[16384];LONG bytes=out?Read(out,buffer,sizeof(buffer)-1):-1;
    if(out)Close(out);
    if(bytes<0)bytes=0;
    buffer[bytes]=0;
    bug("Whole CLI path=%s arg=%s transport=%d status=%ld dos=%ld bytes=%ld\n",
        path,arg,error,result.status,result.dos_error,bytes);
    bug("Whole CLI output begin\n%s\nWhole CLI output end\n",buffer);
    DeleteFile((CONST_STRPTR)"RAM:whole-command-output");
    return error||result.status!=status||!bytes||(needle&&!strstr(buffer,needle));
}
int main(void)
{
    const char *paths[]={"C:zfs","C:zpool"};
    for(unsigned repeat=0;repeat<2;repeat++)for(unsigned i=0;i<2;i++) {
        if(run(paths[i],"--help",0,"usage:")||
           run(paths[i],"--version",1,"zfs-")||
           run(paths[i],"list",1,"The resident OpenZFS service is not available.")) {
            bug("RESULT native whole CLI FAIL iteration=%u command=%u\n",repeat,i);
            return 20;
        }
    }
    bug("RESULT native whole CLI PASS commands=2 repetitions=2 absent-service=1\n");
    return 0;
}
