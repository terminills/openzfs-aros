/* SPDX-License-Identifier: CDDL-1.0 */
#include <exec/types.h>
#include <exec/libraries.h>
#include <exec/memory.h>
#include <aros/libcall.h>
#include <aros/symbolsets.h>
#include <proto/exec.h>
#include <setjmp.h>

/* The real service owns the upstream engine, admission workers and DOS
 * endpoints. Clients continue to use authenticated IPC, never raw engine
 * pointers across task boundaries. The library reference spans its run. */
extern int oz_service_main(int, char **);
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

AROS_LH1(LONG, OpenZFSRunService, AROS_LHA(ULONG, mode, D0),
    struct Library *, OpenZFSBase, 5, OpenZFS)
{
    AROS_LIBFUNC_INIT
    (void)OpenZFSBase;
    if(mode>1||FindTask(NULL)!=owner)return 20;
    /* Global upstream state must never be initialized by two callers, nor
     * reused after teardown without unloading and reinitializing this module. */
    if(__atomic_exchange_n(&claimed,1,__ATOMIC_ACQ_REL))return 10;
    char *argv[]={"OpenZFSServiceNative","WRITABLE",NULL};
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
        result=__posixc_nixmain(oz_service_main,mode?2:1,argv);
    }
    __stdc_set_exitjmp(state->saved,state->discard);
    __stdc_set_errorptr(previous);
    FreeVec(state);
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
