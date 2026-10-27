/* SPDX-License-Identifier: CDDL-1.0 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <exec/tasks.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <setjmp.h>

/* The real service owns the upstream engine, admission workers and DOS
 * endpoints. Clients continue to use authenticated IPC, never raw engine
 * pointers across task boundaries. The library reference spans its run. */
extern int oz_service_main(int, char **);
extern int oz_zdb_main(int, char **);
/* SDK posixc vector 5: the same startup wrapper used by -nix executables.
 * A library has no executable startup path to activate Unix path translation. */
extern int __posixc_nixmain(int (*entry)(int,char **),int,char **);
extern int *__stdc_set_errorptr(int *);
extern void __stdc_set_exitjmp(jmp_buf,jmp_buf);
static unsigned claimed;
static struct Task *owner;

static int init_library(struct Library *base)
{
    (void)base;
    owner=FindTask(NULL);
    return owner!=NULL;
}
ADD2INITLIB(init_library,0);

static int open_library(struct Library *base)
{
    (void)base;
    /* This is the engine host ABI, not a per-client libc context. All other
     * tasks use the admission port; they cannot borrow the owner's runtime. */
    return FindTask(NULL)==owner;
}
ADD2OPENLIB(open_library,0);

static LONG run_entry(int (*entry)(int,char **),int argc,char **argv)
{
    /* Unlike an executable CRT, library initialization has not supplied an
     * exit-status slot. nixmain returns zero without one, even after a real
     * authorization failure. Keep the slot alive for the whole service run. */
    int status=20;
    /* nixmain installs an exit target but does not restore it on return.
     * Keep saved buffers outside this function's automatic longjmp state,
     * and restore the old target before returning or closing the module. */
    struct exit_state {jmp_buf saved,discard;} *state=AllocVec(sizeof(*state),MEMF_ANY);
    if(!state)return 20;
    int *previous=__stdc_set_errorptr(&status);
    jmp_buf guard;
    volatile LONG result=20;
    if(setjmp(guard)==0){
        __stdc_set_exitjmp(guard,state->saved);
        result=__posixc_nixmain(entry,argc,argv);
    }
    __stdc_set_exitjmp(state->saved,state->discard);
    __stdc_set_errorptr(previous);
    FreeVec(state);
    return result;
}

AROS_LH1(LONG, OpenZFSRunService, AROS_LHA(ULONG, mode, D0),
    struct Library *, OpenZFSBase, 5, OpenZFS)
{
    AROS_LIBFUNC_INIT
    (void)OpenZFSBase;
    if(mode>1||FindTask(NULL)!=owner)return 20;
    /* Service and debugger share a single one-shot claim. A debugger must
     * never initialize or finalize the active service's upstream globals. */
    if(__atomic_exchange_n(&claimed,1,__ATOMIC_ACQ_REL))return 10;
    char *argv[]={"OpenZFSServiceNative","WRITABLE",NULL};
    return run_entry(oz_service_main,mode?2:1,argv);
    AROS_LIBFUNC_EXIT
}

static IPTR run_debugger(IPTR argc,IPTR argv)
{
    return run_entry(oz_zdb_main,(int)argc,(char **)argv);
}

AROS_LH2(LONG, OpenZFSRunDebugger, AROS_LHA(LONG, argc, D0),
    AROS_LHA(APTR, arguments, A0),
    struct Library *, OpenZFSBase, 6, OpenZFS)
{
    AROS_LIBFUNC_INIT
    (void)OpenZFSBase;
    char **argv=arguments;
    if(FindTask(NULL)!=owner||argc<1||argc>1024||!argv)return 20;
    for(LONG i=0;i<argc;i++)if(!argv[i])return 20;
    if(argv[argc])return 20;
    if(__atomic_exchange_n(&claimed,1,__ATOMIC_ACQ_REL))return 10;
    /* dump_label alone keeps four 256-KiB labels on its stack. Supply the
     * stack here, not as a shell-only convention: callers may use this ABI
     * with a normal AROS stack. The owner/runtime context does not change. */
    const ULONG bytes=4UL*1024*1024;
    APTR memory=AllocVec(bytes,MEMF_ANY);
    if(!memory)return 20;
    struct StackSwapStruct stack={memory,(char *)memory+bytes,(char *)memory+bytes};
    struct StackSwapArgs args={{(IPTR)argc,(IPTR)argv}};
    LONG result=(LONG)NewStackSwap(&stack,(APTR)run_debugger,&args);
    FreeVec(memory);
    return result;
    AROS_LIBFUNC_EXIT
}

static int close_library(struct Library *base)
{
    /* Reload produces fresh upstream globals. A running service retains its
     * own open reference, so another opener cannot expunge its active engine. */
    base->lib_Flags|=LIBF_DELEXP;
    return 1;
}
ADD2CLOSELIB(close_library,0);
