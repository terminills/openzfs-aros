/* SPDX-License-Identifier: CDDL-1.0 */
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <errno.h>
#include <string.h>
#include <aros/debug.h>
_Static_assert(sizeof(off_t) == 8, "Native positioned I/O requires 64-bit offsets");
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI positioned IO FAIL line=%d errno=%d\n", __LINE__, errno); return 20; } } while (0)
int main(void)
{
    /* Only a small private RAM file; no physical disks or large allocation. */
    const char *path = "RAM:cli-positioned-io";
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, "abcdefgh", 8) == 8);
    CHECK(lseek(fd, 3, SEEK_SET) == 3);
    char buf[9] = {0};
    CHECK(pwrite64(fd, "XY", 2, 5) == 2);
    CHECK(lseek(fd, 0, SEEK_CUR) == 3);
    CHECK(pread64(fd, buf, 8, 0) == 8);
    CHECK(strcmp(buf, "abcdeXYh") == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 3);
    CHECK(pread64(fd, buf, 1, (off_t)UINT64_C(0x100000000)) == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 3);
    errno = 0;
    CHECK(pread64(fd, buf, 1, -1) == -1 && errno == EINVAL);
    CHECK(lseek(fd, 0, SEEK_CUR) == 3);
    errno = 0;
    CHECK(pwrite64(fd, "Z", 1, -1) == -1 && errno == EINVAL);
    CHECK(lseek(fd, 0, SEEK_CUR) == 3);
    CHECK(close(fd) == 0);
    CHECK(unlink(path) == 0);
    bug("RESULT native CLI positioned IO PASS\n");
    return 0;
}
