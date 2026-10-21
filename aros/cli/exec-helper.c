/* SPDX-License-Identifier: CDDL-1.0 */
/* Real child process: the native parent never invokes exec in its own CRT. */
#include <unistd.h>
#include <string.h>
#ifdef OZ_EXEC_TEST_DIAGNOSTICS
#include <errno.h>
#include <aros/debug.h>
#include <stdlib.h>
#endif
int main(int argc,char **argv)
{
    if(argc<4)return 255;
#ifdef OZ_EXEC_TEST_DIAGNOSTICS
    bug("CLI exec helper PATH=%s\n",getenv("PATH")?getenv("PATH"):"(unset)");
#endif
    if(!strcmp(argv[1],"exact"))execv(argv[2],argv+3);
    else if(!strcmp(argv[1],"search"))execvp(argv[2],argv+3);
#ifdef OZ_EXEC_TEST_DIAGNOSTICS
    bug("CLI exec helper returned errno=%d argc=%d\n",errno,argc);
#endif
    /* Match upstream's child _exit(-1) when exec fails. */
    return 255;
}
