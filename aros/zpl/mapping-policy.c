/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vnops.h>

/* AROS DOS exposes byte I/O, not a shared file pager. The inspected ADT
 * compiler/linuxso/lso_mem.c rejects file MAP_SHARED; private file mappings
 * are copies populated via pread. They are not writable aliases of this
 * filesystem's ARC. No native entry point may increment z_mapcnt until a
 * real pager/coherency implementation replaces this policy.
 *
 * These are NOT successful page-cache operations. The only supported state
 * is absence of a separate file mapping cache. Unexpected mapped-page paths
 * fail explicitly instead of discarding dirty data or reporting fake flushes.
 */
boolean_t
zn_has_cached_data(znode_t *zp, uint64_t start, uint64_t end)
{
    VERIFY(zp != NULL);
    VERIFY3U(zp->z_mapcnt, ==, 0);
    return (B_FALSE);
}

int
mappedread(znode_t *zp, int nbytes, zfs_uio_t *uio)
{
    return (ENOTSUP);
}

void
update_pages(znode_t *zp, int64_t start, int len, objset_t *os)
{
    panic("AROS OpenZFS: unsupported shared file-page update");
}

void
zn_flush_cached_data(znode_t *zp, boolean_t sync)
{
    panic("AROS OpenZFS: unsupported shared file-page flush");
}
