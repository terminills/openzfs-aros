/* SPDX-License-Identifier: CDDL-1.0 */
/* Negative QEMU fixture, never installed in a distribution. Exercise the
 * production RuntimeCheck entry point while simulating a POSIX writer that
 * fails to update the caller's errno. Use only --errno-serial/--errno-only.
 */
#include <unistd.h>
static int fault_close(int descriptor)
{
    (void)descriptor;
    return -1;
}
#define close fault_close
#include "runtime-check.c"
