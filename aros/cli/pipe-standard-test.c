/* SPDX-License-Identifier: CDDL-1.0 */
/* Audit existing pipe/fcntl semantics. This does NOT prove atomic CLOEXEC
 * creation or race freedom against concurrent exec; it is not pipe2 glue. */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <aros/debug.h>

#define CHECK(x) do { if (!(x)) { \
    bug("RESULT OpenZFS standard pipe FAIL line=%d errno=%d\n", \
        __LINE__, errno); return 20; } } while (0)

int main(void)
{
    bug("RESULT OpenZFS standard pipe BEGIN\n");
    for (unsigned i = 0; i < 16; ++i) {
        int fd[2];
        char data[4] = {0};
        CHECK(pipe(fd) == 0);
        CHECK(fcntl(fd[0], F_SETFD, FD_CLOEXEC) == 0);
        CHECK(fcntl(fd[1], F_SETFD, FD_CLOEXEC) == 0);
        CHECK(fcntl(fd[0], F_GETFD) == FD_CLOEXEC);
        CHECK(fcntl(fd[1], F_GETFD) == FD_CLOEXEC);
        CHECK(write(fd[1], "zfs", 3) == 3);
        CHECK(read(fd[0], data, 3) == 3 && !strcmp(data, "zfs"));
        CHECK(close(fd[1]) == 0);
        CHECK(read(fd[0], data, 1) == 0);
        CHECK(close(fd[0]) == 0);
    }
    bug("RESULT OpenZFS standard pipe PASS runs=16 flags=1 io=1 eof=1 atomic=untested\n");
    return 0;
}
