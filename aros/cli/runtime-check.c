/* SPDX-License-Identifier: CDDL-1.0 */
/* Non-pool runtime checks. No global assigns, credentials or disk writes. */
#include "process.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

static int exact(BPTR handle,const char *wanted)
{
    char bytes[128];
    if(!Flush(handle)||Seek(handle,0,OFFSET_BEGINNING)==-1)return 0;
    LONG n=Read(handle,bytes,sizeof(bytes));
    return n==(LONG)strlen(wanted)&&!memcmp(bytes,wanted,n);
}
static int execution(void)
{
    char directory[96],path[128],bytes[8192];
    snprintf(directory,sizeof(directory),"RAM:OpenZFSExecCheck-%p",(void *)FindTask(NULL));
    BPTR lock=CreateDir((CONST_STRPTR)directory);
    if(!lock){puts("FAIL loader probe RAM directory");return 0;}
    UnLock(lock);snprintf(path,sizeof(path),"%s/probe",directory);
    BPTR input=Open((CONST_STRPTR)"PROGDIR:OpenZFSRuntimeCheck",MODE_OLDFILE);
    BPTR output=input?Open((CONST_STRPTR)path,MODE_NEWFILE):0;
    int ok=input&&output;LONG n=0;
    while(ok&&(n=Read(input,bytes,sizeof(bytes)))>0)
        if(Write(output,bytes,n)!=n)ok=0;
    if(n<0)ok=0;
    if(output&&!Close(output))ok=0;
    if(input&&!Close(input))ok=0;
    for(unsigned phase=0;phase<3&&ok;phase++) {
        int allowed=phase!=1;
        if(!SetProtection((CONST_STRPTR)path,allowed?0:FIBF_EXECUTE)){ok=0;break;}
        for(unsigned api=0;api<2;api++) {
            BPTR segment=api?NewLoadSeg((CONST_STRPTR)path,NULL):LoadSeg((CONST_STRPTR)path);
            LONG error=IoErr();
            int passed=allowed?segment!=0:!segment&&error==ERROR_READ_PROTECTED;
            if(segment)UnLoadSeg(segment);
            printf("%s %s execute=%s loaded=%d DOS=%ld\n",passed?"PASS":"FAIL",
                api?"NewLoadSeg":"LoadSeg",allowed?"on":"off",!!segment,(long)error);
            if(!passed)ok=0;
        }
    }
    if(!DeleteFile((CONST_STRPTR)path))ok=0;
    if(!DeleteFile((CONST_STRPTR)directory))ok=0;
    if(!ok)puts("FAIL loader execute protection; verify the matching DOS runtime patch");
    return ok;
}
static int streams(void)
{
    char directory[96],outname[112],errname[112];
    snprintf(directory,sizeof(directory),"RAM:OpenZFSCheck-%p",(void *)FindTask(NULL));
    BPTR lock=CreateDir((CONST_STRPTR)directory);
    if(!lock){printf("FAIL temporary RAM directory (DOS error %ld)\n",(long)IoErr());return 0;}
    UnLock(lock);
    snprintf(outname,sizeof(outname),"%s/stdout",directory);
    snprintf(errname,sizeof(errname),"%s/stderr",directory);
    BPTR output=Open((CONST_STRPTR)outname,MODE_NEWFILE);
    BPTR errors=Open((CONST_STRPTR)errname,MODE_NEWFILE);
    int ok=0;
    if(output&&errors){
        char *args[]={"OpenZFSRuntimeCheck","--stdio-child",NULL};
        struct oz_cli_result result;
        int error=oz_cli_spawn_exact("PROGDIR:OpenZFSRuntimeCheck",args,output,errors,&result);
        int outok=exact(output,"OpenZFS stdout probe\n");
        int errok=exact(errors,"OpenZFS stderr probe\n");
        ok=!error&&result.status==0&&outok&&errok;
        printf("%s redirected stdio: transport=%d child=%ld stdout=%d stderr=%d\n",
            ok?"PASS":"FAIL",error,result.status,outok,errok);
        /* A broken child runtime may already have closed inherited handles.
         * Do not close them again after the ownership check fails. */
        if(outok){if(!Close(output))ok=0;} output=0;
        if(errok){if(!Close(errors))ok=0;} errors=0;
    }
    if(output)Close(output);
    if(errors)Close(errors);
    if(!DeleteFile((CONST_STRPTR)outname))ok=0;
    if(!DeleteFile((CONST_STRPTR)errname))ok=0;
    if(!DeleteFile((CONST_STRPTR)directory)){
        printf("FAIL cleanup; inspect %s\n",directory);ok=0;
    }
    return ok;
}
int main(int argc,char **argv)
{
    if(argc==2&&!strcmp(argv[1],"--stdio-child")){
        if(isatty(STDOUT_FILENO)||isatty(STDERR_FILENO))return 20;
        if(fputs("OpenZFS stdout probe\n",stdout)<0||
            fputs("OpenZFS stderr probe\n",stderr)<0)return 20;
        return fflush(stdout)||fflush(stderr)?20:0;
    }
    if(argc!=1){puts("Usage: OpenZFSRuntimeCheck");return 20;}
    puts("OpenZFS runtime check (no pools or devices opened)");
    int ok=1;
    const char *names[]={"posixc.library","stdcio.library","z1.library","iffparse.library","muimaster.library"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++){
        struct Library *library=OpenLibrary((CONST_STRPTR)names[i],0);
        if(!library){printf("FAIL %s unavailable\n",names[i]);ok=0;}
        else {printf("INFO %s %u.%u available\n",names[i],library->lib_Version,library->lib_Revision);CloseLibrary(library);}
    }
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    int configured=secBase&&secIsConfigured();
    printf("%s security.library configured=%d\n",configured?"PASS":"FAIL",configured);
    if(secBase)CloseLibrary(secBase);
    if(!configured)ok=0;
    if(!streams())ok=0;
    if(!execution())ok=0;
    puts("Not tested: pipe disconnects, storage I/O, durability, GUI or pool operations.");
    puts(ok?"PASS checked runtime prerequisites only":"FAIL runtime prerequisites; review diagnostics above");
    return ok?0:20;
}
