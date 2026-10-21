/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>

/* Native equivalent of upstream zfs_write_simple in zfs_vnops_os.c.
 * No Linux reclaim-recursion marker is needed by the native userspace engine.
 * This internal helper deliberately uses kcred like upstream; public requests
 * must instead carry authenticated sender credentials through admission. */
int
zfs_write_simple(znode_t *zp, const void *data, size_t length,
    int64_t position, size_t *residual)
{
    if (!zp || (!data && length)) return (EINVAL);
    if (position < 0) return (EINVAL);
    if (length > INT64_MAX - position) return (EOVERFLOW);
    struct iovec vec = {(void *)data, length};
    zfs_uio_t uio;
    zfs_uio_iovec_init(&uio, &vec, 1, position, UIO_SYSSPACE, length, 0);
    int error = zfs_write(zp, &uio, 0, kcred);
    if (!error) {
        if (residual) *residual = zfs_uio_resid(&uio);
        else if (zfs_uio_resid(&uio)) error = EIO;
    }
    return (error);
}
