/* SPDX-License-Identifier: CDDL-1.0 */
/* Read-only initialization probe. Does not create or import any pool. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/tunables.h>
#include <stdio.h>
static int count_tunable(const zfs_tunable_t *tunable, void *arg)
{
    (void)tunable;
    (*(unsigned *)arg)++;
    return 0;
}
int main(void)
{
    if (!freopen("SYS:engine-init-errors.log", "w", stderr)) return 20;
    setvbuf(stderr,NULL,_IONBF,0);
    unsigned tunables=0;
    zfs_tunable_iter(count_tunable,&tunables);
    printf("Upstream tunable registry: %u entries\n",tunables);
    /* PARAM_CALL tunables are intentionally absent from upstream libzpool's
     * registry. Check a PARAM entry, not the kernel-only ARC limit setter. */
    const zfs_tunable_t *entry=zfs_tunable_lookup("zfs_arc_meta_balance");
    char value[64];
    if (!tunables || !entry || zfs_tunable_get(entry,value,sizeof(value))) {
        printf("FAIL upstream tunable lookup/read\n");
        return 20;
    }
    printf("Upstream tunable %s=%s\n",entry->zt_name,value);
    printf("OpenZFS upstream engine: starting read-only initialization\n");
    fflush(stdout);
    kernel_init(SPA_MODE_READ);
    printf("OpenZFS upstream engine: initialized; shutting down\n");
    fflush(stdout);
    kernel_fini();
    printf("RESULT engine initialization and shutdown passed\n");
    return 0;
}
