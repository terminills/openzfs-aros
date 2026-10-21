/* SPDX-License-Identifier: CDDL-1.0 */
/* Native topology shared by the resident engine and userspace commands. */
#include <proto/exec.h>
#include <proto/processor.h>
#include <limits.h>

int aros_zfs_ncpus(void)
{
    APTR ProcessorBase=OpenResource((CONST_STRPTR)PROCESSORNAME);
    ULONG count=1;
    struct TagItem tags[]={{GCIT_NumberOfProcessors,(IPTR)&count},{TAG_DONE,0}};
    if(ProcessorBase!=NULL)GetCPUInfo(tags);
    /* A missing resource must not create zero workers. This preserves the
     * engine's single-worker fallback, not a guess based on the Linux host. */
    return count>0&&count<=INT_MAX?(int)count:1;
}
