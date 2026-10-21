/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <errno.h>
#include "../../lib/libzfs/libzfs_impl.h"

const char *libzfs_error_init(int error)
{
    switch(error) {
    case ENOENT: case ENXIO:
        return "The resident OpenZFS service is not available.";
    case EACCES: case EPERM:
        return "The resident OpenZFS service denied this native caller.";
    case ENOTSUP: case EPROTO:
        return "The resident OpenZFS service does not support this client protocol.";
    case ENOMEM:
        return "Not enough memory to initialize the OpenZFS client.";
    case EINVAL:
        return "An OpenZFS client initialization setting is invalid.";
    default:
        return "Failed to initialize the native OpenZFS client.";
    }
}

int zfs_mount_delegation_check(void)
{
    /* Unlike a root-only Unix mount syscall, native mount RPCs support
     * delegation. They authenticate the packet caller and check the dataset
     * mount grant on every attach/detach. This dataset-free UI precheck must
     * not reject delegated non-root callers; it does not authorize a mount. */
    return 0;
}

int zfs_destroy_snaps_nvl_os(libzfs_handle_t *hdl,nvlist_t *snaps)
{
    if(!hdl||!snaps){errno=EINVAL;return -1;}
    /* No client-side unmount: the next upstream lzc_destroy_snaps call enters
     * the real dispatcher. After its credential check, zfs_ioc_destroy_snaps
     * invokes zfsctl_snapshot_unmount on the owner task. The native handler
     * preserves busy DOS locks/handles even for MNT_FORCE. Duplicating that
     * here would detach before the destroy permission check. */
    return 0;
}

void zpool_disable_volume_os(const char *name)
{
    /* Native DOS detach and zvol minor lifetime belong to the resident
     * engine. There is no per-client devfs namespace to disable after a
     * successful zfs_unmount. This is the same optional post-unmount hook as
     * on Linux/FreeBSD, not an implementation of a native zvol .device. */
    (void)name;
}

void zpool_disable_datasets_os(zpool_handle_t *pool,boolean_t force)
{
    /* Called only after the common code successfully unmounts the datasets.
     * Native endpoints belong to the resident handler, not a client devfs.
     * As on Linux/FreeBSD there is no additional OS teardown at this point.
     * In particular this hook must not detach busy endpoints or bypass the
     * credential checks already performed by the native unmount operation. */
    (void)pool;(void)force;
}
