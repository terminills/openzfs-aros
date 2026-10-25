/* SPDX-License-Identifier: CDDL-1.0 */
/* RAM-backed API test, not storage durability qualification. */
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>
#include <aros/debug.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native sync FAIL line=%d errno=%d\n", __LINE__, errno); return 20; } } while (0)
int main(void)
{
    const char *path = "RAM:openzfs-sync-test";
    const char data[] = "OpenZFS positional sync fixture";
    char readback[sizeof(data)];
    bug("RESULT native sync BEGIN\n");
    errno = 0;
    int rc = fdatasync(-1), error = errno;
    CHECK(rc == -1 && error == EBADF);
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600);
    CHECK(fd >= 0);
    CHECK(write(fd, data, sizeof(data)) == sizeof(data));
    CHECK(lseek(fd, 7, SEEK_SET) == 7);
    CHECK(fdatasync(fd) == 0);
    CHECK(lseek(fd, 0, SEEK_CUR) == 7);
    CHECK(lseek(fd, 0, SEEK_SET) == 0);
    CHECK(read(fd, readback, sizeof(readback)) == sizeof(readback));
    CHECK(!memcmp(data, readback, sizeof(data)));
    CHECK(close(fd) == 0);
    errno = 0;
    rc = fdatasync(fd); error = errno;
    CHECK(rc == -1 && error == EBADF);
    CHECK(unlink(path) == 0);
    bug("RESULT native sync PASS invalid=1 closed=1 offset=1 data=1 cleanup=1 durability=untested\n");
    return 0;
}
