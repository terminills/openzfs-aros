/* SPDX-License-Identifier: CDDL-1.0 */
/* Link only: never run this prerequisite probe on an AROS installation.
 * Explicit declarations test symbol availability even in SDKs whose headers
 * omit the extensions. They do not supply an implementation or import stub. */
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#if defined(OZ_PROBE_PWRITE)
extern ssize_t pwrite(int, const void *, size_t, off_t);
int main(void) { return pwrite(-1, "", 0, 0) == -1 ? 0 : 1; }
#elif defined(OZ_PROBE_PIPE2)
extern int pipe2(int [2], int);
int main(void) { int fd[2]; return pipe2(fd, O_CLOEXEC); }
#else
#error Select a prerequisite probe
#endif
