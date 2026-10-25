/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ERRNO_CHECK_H
#define OZ_ERRNO_CHECK_H

#include <errno.h>
#include <unistd.h>

/* Exercise both the caller's errno accessor and a POSIX library writer.
 * This cannot make an incompatible runtime safe: an invalid ABI may fault.
 * Run it in an isolated guest before storage qualification. No file is opened.
 */
static int oz_errno_check(void)
{
    int saved = errno;
    volatile int *slot = &errno;
    errno = EDOM;
    int ok = *slot == EDOM && errno == EDOM;
    errno = ERANGE;
    ok = ok && *slot == ERANGE && errno == ERANGE;
    errno = 0;
    int result = close(-1);
    ok = ok && result == -1 && errno == EBADF && *slot == EBADF;
    errno = saved;
    return ok;
}

#endif
