/* SPDX-License-Identifier: CDDL-1.0 */
/* Standalone ABI probe: no pools, files, devices or child processes opened. */
#include "errno-check.h"
#include <aros/debug.h>

int main(void)
{
    const int entry_errno = errno;
    const int values[] = {0, EINVAL, EACCES};
    int ok = 1;
    bug("RESULT OpenZFS errno ABI BEGIN\n");
    for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); ++i) {
        errno = values[i];
        int probe = oz_errno_check();
        int restored = errno == values[i];
        ok = ok && probe && restored;
        bug("RESULT OpenZFS errno ABI case=%u accessor-writer=%d restored=%d\n",
            i, probe, restored);
    }
    errno = entry_errno;
    bug("RESULT OpenZFS errno ABI %s cases=3\n", ok ? "PASS" : "FAIL");
    errno = entry_errno;
    return ok ? 0 : 20;
}
