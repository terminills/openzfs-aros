/* SPDX-License-Identifier: CDDL-1.0 */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <aros/debug.h>
/* Frozen SDK plus the appended native import stub under test. */
int pipe2(int [2], int);
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI pipe FAIL line=%d errno=%d\n", __LINE__, errno); return 20; } } while (0)
int main(void)
{
    int fd[2];
    for (unsigned i=0; i<64; i++) {
        int cloexec = (i & 1) ? O_CLOEXEC : 0;
        CHECK(pipe2(fd, cloexec) == 0);
        CHECK(fcntl(fd[0], F_GETFD) == (cloexec ? FD_CLOEXEC : 0));
        CHECK(fcntl(fd[1], F_GETFD) == (cloexec ? FD_CLOEXEC : 0));
        char data[4] = {0};
        CHECK(write(fd[1], "zfs", 3) == 3);
        CHECK(read(fd[0], data, 3) == 3 && !strcmp(data, "zfs"));
        CHECK(close(fd[1]) == 0);
        CHECK(read(fd[0], data, 1) == 0);
        CHECK(close(fd[0]) == 0);
    }
    CHECK(pipe(fd) == 0);
    CHECK(fcntl(fd[0], F_GETFD) == 0 && fcntl(fd[1], F_GETFD) == 0);
    CHECK(close(fd[0]) == 0 && close(fd[1]) == 0);
    fd[0]=123; fd[1]=456;
    errno=0; CHECK(pipe2(fd, O_APPEND) == -1 && errno == EINVAL);
    CHECK(fd[0] == 123 && fd[1] == 456);
    errno=0; CHECK(pipe2(fd, O_NONBLOCK) == -1 && errno == ENOTSUP);
    CHECK(fd[0] == 123 && fd[1] == 456);
    errno=0; CHECK(pipe2(NULL, 0) == -1 && errno == EFAULT);
    bug("RESULT native CLI pipe PASS runs=64 flags=1 io=1 eof=1 errors=1\n");
    return 0;
}
