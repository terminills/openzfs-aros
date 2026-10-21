/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_vnops.h>

int
oz_file_limits_test(void)
{
    if (sizeof (rlim64_t) != 8 || zn_rlimit_fsize(0) ||
        zn_rlimit_fsize(MAXOFFSET_T) ||
        zn_rlimit_fsize((uint64_t)MAXOFFSET_T + 1) != EFBIG ||
        zn_rlimit_fsize(UINT64_MAX) != EFBIG) return (1);
    zfs_uio_t u;
    char bytes[10];
    struct iovec vec = {bytes, sizeof (bytes)};
    zfs_uio_iovec_init(&u, &vec, 1, MAXOFFSET_T - 5, UIO_SYSSPACE, 10, 0);
    zfs_uio_t original = u;
    if (zn_rlimit_fsize_uio(NULL, &u) || memcmp(&u, &original, sizeof (u))) return (2);
    zfs_uio_fault_disable(&u, B_TRUE);
    zfs_uio_fault_disable(&u, B_FALSE);
    if (memcmp(&u, &original, sizeof (u))) return (3);
    u.uio_loffset = MAXOFFSET_T;
    if (zn_rlimit_fsize_uio(NULL, &u) != EFBIG) return (4);
    u.uio_resid = 0;
    if (zn_rlimit_fsize_uio(NULL, &u)) return (5);
    u.uio_loffset = -1;
    if (zn_rlimit_fsize_uio(NULL, &u) != EINVAL) return (6);
    u.uio_loffset = 0; u.uio_resid = -1;
    if (zn_rlimit_fsize_uio(NULL, &u) != EINVAL ||
        zn_rlimit_fsize_uio(NULL, NULL) != EINVAL) return (7);
    return (0);
}
