/* SPDX-License-Identifier: CDDL-1.0 */
#include "title.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <pthread.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

static pthread_mutex_t title_lock;
static pthread_once_t title_once=PTHREAD_ONCE_INIT;
static int title_error;
static void initialize_lock(void){title_error=pthread_mutex_init(&title_lock,NULL);}
static int prepare(void)
{
    int error=pthread_once(&title_once,initialize_lock);
    return error?error:title_error;
}
static struct Task *owner;
static struct CommandLineInterface *owner_cli;
/* GetProgramName reports truncation when length == capacity - 1, so give
 * its read buffer one spare byte beyond the 255-byte CLI title plus NUL. */
static char original[257];
static int exit_registered;

static void write_title(const char *text)
{
    /* Same public CLI BSTR representation and 255-byte bound as AROS DOS
     * SetProgramName. That API only targets the calling process; progress
     * workers instead update the CLI captured by their owning command.
     * No task name pointer is replaced or borrowed from a temporary buffer. */
    size_t length=strlen(text);
    CopyMem((APTR)text,AROS_BSTR_ADDR(owner_cli->cli_CommandName),length);
    AROS_BSTR_setstrlen(owner_cli->cli_CommandName,length);
}
int oz_cli_title_restore(void)
{
    int prepared=prepare();if(prepared)return prepared;
    pthread_mutex_lock(&title_lock);
    int error=0;
    if(owner_cli) {
        if(FindTask(NULL)!=owner)error=EPERM;
        else {write_title(original);owner_cli=NULL;owner=NULL;}
    }
    pthread_mutex_unlock(&title_lock);
    return error;
}
static void restore_at_exit(void){(void)oz_cli_title_restore();}
void zfs_setproctitle_init(int argc,char **argv,char **envp)
{
    (void)argc;(void)argv;(void)envp;
    int prepared=prepare();if(prepared){errno=prepared;return;}
    pthread_mutex_lock(&title_lock);
    if(owner_cli){pthread_mutex_unlock(&title_lock);return;}
    struct Task *task=FindTask(NULL);
    struct CommandLineInterface *cli=task->tc_Node.ln_Type==NT_PROCESS?Cli():NULL;
    int error=0;
    if(!cli||!cli->cli_CommandName)error=ENOTSUP;
    else if(!GetProgramName((STRPTR)original,sizeof(original)))error=EIO;
    else if(!exit_registered&&atexit(restore_at_exit))error=ENOMEM;
    else {exit_registered=1;owner=task;owner_cli=cli;}
    pthread_mutex_unlock(&title_lock);
    if(error)errno=error;
}
void zfs_setproctitle(const char *format,...)
{
    if(!format){errno=EINVAL;return;}
    int prepared=prepare();if(prepared){errno=prepared;return;}
    char text[256];va_list args;
    va_start(args,format);int count=vsnprintf(text,sizeof(text),format,args);va_end(args);
    if(count<0){errno=EILSEQ;return;}
    pthread_mutex_lock(&title_lock);
    if(owner_cli)write_title(text);
    else errno=ENOTSUP;
    pthread_mutex_unlock(&title_lock);
}
