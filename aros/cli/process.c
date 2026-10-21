/* SPDX-License-Identifier: CDDL-1.0 */
#include "process.h"
#include "arguments.h"
#include <dos/dostags.h>
#include <proto/dos.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <unistd.h>
#include <limits.h>
#include <proto/exec.h>
#include <dos/dosextens.h>
#include <dos/var.h>
/* dos/envpolicy.h additions: allow an older SDK, but fail closed if the
 * running DOS does not implement this opt-in process policy. */
#ifndef NP_EnvLocalOnly
#define NP_EnvLocalOnly (NP_Dummy + 27)
#endif
#ifndef PRF_ENVLOCALONLY
#define PRF_ENVLOCALONLY (1L << 22)
#endif

struct child_context {
    BPTR segment;
    char *arguments;
    ULONG argument_size;
    char **names, **values;
    size_t count;
    int setup_error;
    struct oz_cli_result *result;
};

/* No C-runtime calls here: this is an entry trampoline, not a CRT program. */
static LONG run_environment_child(void)
{
    struct child_context *context = FindTask(NULL)->tc_UserData;
    if (!(((struct Process *)FindTask(NULL))->pr_Flags & PRF_ENVLOCALONLY)) {
        context->setup_error = ENOTSUP;
        return -1;
    }
    for (size_t i = 0; i < context->count; ++i) {
        if (!SetVar((CONST_STRPTR)context->names[i],
            (CONST_STRPTR)context->values[i], -1, GVF_LOCAL_ONLY)) {
            context->result->dos_error = IoErr();
            context->setup_error = EIO;
            return -1;
        }
    }
    LONG status = RunCommand(context->segment, 262144,
        (STRPTR)context->arguments, context->argument_size);
    if (status == -1) {
        context->result->dos_error = IoErr();
        context->setup_error = EIO;
    }
    return status;
}

static long read_output(void *context, char *buffer, size_t size)
{
    return Read(*(BPTR *)context, buffer, size);
}

int oz_cli_read_lines(BPTR handle, size_t limit, char ***lines, int *count)
{
    if (!lines || !count) return EINVAL;
    *lines = NULL; *count = 0;
    if (!handle) return EINVAL;
    return oz_cli_collect(read_output, &handle, limit, lines, count);
}

int oz_cli_capture_env_exact(const char *path,char *const argv[],char *const env[],
    BPTR errors,size_t limit,char ***lines,int *count,struct oz_cli_result *result)
{
    if(!lines||!count||!result)return EINVAL;
    *lines=NULL;*count=0;result->status=-1;result->dos_error=0;
    if(!path||!*path||!argv||!errors)return EINVAL;
    char temporary[]="T:openzfs-output-XXXXXX";
    int reserved=mkstemp(temporary);
    if(reserved<0)return errno;
    /* Keep the exclusive-created file open throughout the child lifetime.
     * Open the same file without truncation for native DOS stream sharing. */
    BPTR output=Open((CONST_STRPTR)temporary,MODE_OLDFILE);
    int error=0;
    if(!output){result->dos_error=IoErr();error=EIO;}
    if(!error)error=oz_cli_spawn_env_exact(path,argv,env,output,errors,result);
    if(!error&&(!Flush(output)||Seek(output,0,OFFSET_BEGINNING)==-1)) {
        result->dos_error=IoErr();error=EIO;
    }
    if(!error)error=oz_cli_read_lines(output,limit,lines,count);
    if(output&&!Close(output)&&!error){result->dos_error=IoErr();error=EIO;}
    if(close(reserved)&&!error)error=errno;
    if(unlink(temporary)&&!error)error=errno;
    if(error){oz_cli_free_lines(*lines,*count);*lines=NULL;*count=0;}
    return error;
}

int oz_cli_run_process(const char *path,char *const argv[],char *const env[],
    int flags,char ***lines,int *count)
{
    if(lines)*lines=NULL;
    if(count)*count=0;
    if(!path||!*path||!argv||!argv[0]||(flags&~7)||(lines&&!count))return -EINVAL;
    size_t argc=0;
    while(argv[argc])if(++argc>1024)return -E2BIG;
    char **helper_args=calloc(argc+4,sizeof(*helper_args));
    if(!helper_args)return -ENOMEM;
    const char *helper="SYS:Tools/OpenZFS/ExecHelper";
    helper_args[0]=(char *)helper;helper_args[1]=(flags&4)?"exact":"search";
    helper_args[2]=(char *)path;
    for(size_t i=0;i<argc;i++)helper_args[i+3]=argv[i];
    BPTR quiet=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);
    if(!quiet){free(helper_args);return -EIO;}
    BPTR output=(flags&1)?Output():quiet;
    BPTR errors=(flags&2)?ErrorOutput():quiet;
    struct oz_cli_result result;
    int error;
    if(!output||!errors)error=EBADF;
    else if(lines)error=oz_cli_capture_env_exact(helper,helper_args,env,errors,
        INT_MAX,lines,count,&result);
    else error=oz_cli_spawn_env_exact(helper,helper_args,env,output,errors,&result);
    if(!Close(quiet)&&!error)error=EIO;
    free(helper_args);
    if(error){if(lines){oz_cli_free_lines(*lines,*count);*lines=NULL;*count=0;}return -error;}
    return result.status;
}

/* NP_Synchronous waits past this callback and DOS child cleanup. The callback
 * must not call the parent's C runtime from the child's context. */
static void completed(IPTR code, IPTR data)
{
    struct oz_cli_result *result = (struct oz_cli_result *)data;
    result->status = (LONG)code;
}

int oz_cli_spawn_exact(const char *path, char *const argv[], BPTR output,
    BPTR errors, struct oz_cli_result *result)
{
    return oz_cli_spawn_env_exact(path, argv, NULL, output, errors, result);
}

int oz_cli_spawn_env_exact(const char *path, char *const argv[], char *const env[],
    BPTR output, BPTR errors, struct oz_cli_result *result)
{
    char *args = NULL;
    BPTR segment;
    struct Process *child;
    int error;
    struct child_context context = {0};
    if (!result) return EINVAL;
    result->status = -1;
    result->dos_error = 0;
    if (!path || !*path || !output || !errors) return EINVAL;
    error = oz_cli_arguments(argv, 65536, &args);
    if (error) return error;
    if (env) {
        size_t bytes = 0;
        while (env[context.count]) {
            if (context.count == 1024) { error = E2BIG; goto cleanup; }
            size_t length = strlen(env[context.count]);
            if (length >= 65536 - bytes) { error = E2BIG; goto cleanup; }
            bytes += length + 1;
            ++context.count;
        }
        context.names = calloc(context.count + 1, sizeof(char *));
        context.values = calloc(context.count + 1, sizeof(char *));
        if (!context.names || !context.values) { error = ENOMEM; goto cleanup; }
        for (size_t i = 0; i < context.count; ++i) {
            context.names[i] = strdup(env[i]);
            if (!context.names[i]) { error = ENOMEM; goto cleanup; }
            char *equal = strchr(context.names[i], '=');
            if (!equal || equal == context.names[i]) { error = EINVAL; goto cleanup; }
            *equal = '\0'; context.values[i] = equal + 1;
            for (size_t j = 0; j < i; ++j)
                if (!strcasecmp(context.names[i], context.names[j])) {
                    error = EINVAL; goto cleanup;
                }
        }
    }
    segment = LoadSeg((CONST_STRPTR)path);
    if (!segment) {
        result->dos_error = IoErr();
        error = ENOEXEC; goto cleanup;
    }
    context.segment = segment; context.arguments = args;
    context.argument_size = strlen(args); context.result = result;
    child = CreateNewProcTags(env ? NP_Entry : NP_Seglist,
        env ? (IPTR)run_environment_child : (IPTR)segment,
        NP_FreeSeglist, FALSE, NP_Cli, TRUE,
        NP_CommandName, (IPTR)path, NP_Name, (IPTR)path,
        NP_Arguments, (IPTR)args, NP_StackSize, 262144,
        NP_Input, Input(), NP_CloseInput, FALSE,
        NP_Output, output, NP_CloseOutput, FALSE,
        NP_Error, errors, NP_CloseError, FALSE,
        NP_CopyVars, env == NULL, NP_Synchronous, TRUE,
        env ? NP_EnvLocalOnly : TAG_IGNORE, TRUE,
        NP_UserData, (IPTR)&context,
        NP_ExitCode, (IPTR)completed, NP_ExitData, (IPTR)result,
        TAG_DONE);
    if (!child) result->dos_error = IoErr();
    /* The returned process pointer is not dereferenced: DOS owns its lifetime. */
    UnLoadSeg(segment);
    error = child ? context.setup_error : EIO;
cleanup:
    if (context.names)
        for (size_t i = 0; i < context.count; ++i) free(context.names[i]);
    free(context.names); free(context.values);
    free(args);
    return error;
}
