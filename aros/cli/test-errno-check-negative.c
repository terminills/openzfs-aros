/* SPDX-License-Identifier: CDDL-1.0 */
#include <errno.h>
#include <unistd.h>
#include <stdio.h>

static int broken_close(int descriptor)
{
    (void)descriptor;
    /* Simulate a POSIX writer that does not update the caller's errno. */
    return -1;
}
#define close broken_close
#include "errno-check.h"
#undef close

int main(void)
{
    errno=EACCES;
    if(oz_errno_check() || errno!=EACCES) return 1;
    puts("PASS mismatched errno writer rejected and saved errno restored");
    return 0;
}
