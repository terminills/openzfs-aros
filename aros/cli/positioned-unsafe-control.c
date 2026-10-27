/* SPDX-License-Identifier: CDDL-1.0 */
/* Host-only negative control: deliberately NOT a runtime implementation. */
#include <unistd.h>
#include <sched.h>

ssize_t __wrap_pread(int fd, void *buf, size_t length, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    sched_yield();
    ssize_t result = read(fd, buf, length);
    sched_yield();
    if (lseek(fd, saved, SEEK_SET) < 0) return -1;
    return result;
}

ssize_t __wrap_pwrite(int fd, const void *buf, size_t length, off_t offset)
{
    off_t saved = lseek(fd, 0, SEEK_CUR);
    if (saved < 0 || lseek(fd, offset, SEEK_SET) < 0) return -1;
    sched_yield();
    ssize_t result = write(fd, buf, length);
    sched_yield();
    if (lseek(fd, saved, SEEK_SET) < 0) return -1;
    return result;
}
