/* SPDX-License-Identifier: CDDL-1.0 */
#include <libintl.h>
#include <string.h>
#include <stdio.h>
#ifdef __AROS__
#include <aros/debug.h>
extern long zpool_online_cpus(void);
#endif

int main(void)
{
    if (strcmp(gettext("pool unavailable"), "pool unavailable") ||
        strcmp(dgettext("zfs", "dataset unavailable"), "dataset unavailable") ||
        strcmp(ngettext("pool", "pools", 0), "pools") ||
        strcmp(ngettext("pool", "pools", 1), "pool") ||
        strcmp(ngettext("pool", "pools", 2), "pools") ||
        textdomain("zfs") == NULL)
        return 1;
    puts("AROS CLI no-NLS message selection PASS");
#ifdef __AROS__
    /* This native fixture runs exclusively in our two-vCPU private QEMU. */
    bug("CLI upstream userland version=%s\n", ZFS_META_ALIAS);
    long count = zpool_online_cpus();
    bug("CLI native online CPUs=%ld\n", count);
    if (count != 2) {
        bug("RESULT native CLI platform FAIL\n");
        return 1;
    }
    bug("RESULT native CLI platform PASS\n");
#endif
    return 0;
}
