/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_vnops.h>

/* Native AROS has no implemented RLIMIT_FSIZE provider. Enforce the actual
 * signed 64-bit offset boundary here; do not read the service task's Linux
 * limits or pretend a caller-specific limit was obtained. */
int
zn_rlimit_fsize(uint64_t end)
{
    return (end > (uint64_t)MAXOFFSET_T ? EFBIG : 0);
}

int
zn_rlimit_fsize_uio(znode_t *zp, zfs_uio_t *uio)
{
    (void)zp;
    if (!uio || zfs_uio_offset(uio) < 0 || zfs_uio_resid(uio) < 0)
        return (EINVAL);
    if (zfs_uio_resid(uio) && (uint64_t)zfs_uio_offset(uio) >= (uint64_t)MAXOFFSET_T)
        return (EFBIG);
    /* The upstream write loop clips n to MAXOFFSET_T - woff, allowing a
     * partial write up to the limit. Do not alter residual/vector state. */
    return (0);
}
