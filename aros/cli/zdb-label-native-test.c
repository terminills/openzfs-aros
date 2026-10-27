/* SPDX-License-Identifier: CDDL-1.0 */
/* Diagnostic executable test only, on a read-only ISO member fixture. */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>

int main(void)
{
    bug("RESULT native zdb labels BEGIN\n");
    for (unsigned run = 0; run < 2; ++run) {
        LONG status = SystemTags((CONST_STRPTR)
            "C:ZdbAudit -l /SYS/zdb-member >RAM:zdb-labels.log",
            /* dump_label holds all four 256-KiB vdev labels on its stack,
             * in addition to the caller and decoder frames. */
#ifdef OZ_ZDB_SHARED
            /* The public library entry must provide its own large stack. */
            NP_StackSize, 65536, TAG_DONE);
#else
            NP_StackSize, 4194304, TAG_DONE);
#endif
        FILE *report = fopen("RAM:zdb-labels.log", "r");
        int name = 0, labels = 0, failed = 0;
        char line[1024];
        bug("RESULT native zdb labels run=%u exit=%ld\n", run, (long)status);
        if (!report) return 20;
        while (fgets(line, sizeof(line), report)) {
            bug("ZDB[%u] %s", run, line);
            name |= strstr(line, "name: 'topo_source'") != NULL;
            labels |= strstr(line, "labels = 0 1 2 3 ") != NULL;
            failed |= strstr(line, "failed to ") != NULL;
        }
        failed |= ferror(report) != 0;
        fclose(report);
        if (status || !name || !labels || failed) {
            bug("RESULT native zdb labels FAIL run=%u name=%d labels=%d errors=%d\n",
                run, name, labels, failed);
            return 20;
        }
    }
#ifdef OZ_ZDB_SHARED
    LONG lifecycle=SystemTags((CONST_STRPTR)
        "C:OpenZFSZdbLifetimeTest >RAM:zdb-lifetime.log",
        NP_StackSize,65536,TAG_DONE);
    if(lifecycle!=7){
        bug("RESULT native zdb labels FAIL lifecycle-exit=%ld\n",(long)lifecycle);
        return 20;
    }
    bug("RESULT shared zdb caller exit PASS status=7\n");
#endif
    bug("RESULT native zdb labels PASS runs=2 storage=readonly-iso\n");
    return 0;
}
