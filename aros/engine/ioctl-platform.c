/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <exec/memory.h>
#include <proto/exec.h>

uint64_t zfs_max_nvlist_src_size_os(void)
{
    if(zfs_max_nvlist_src_size)return zfs_max_nvlist_src_size;
    /* Match upstream's quarter-RAM/128-MiB default, using AROS memory rather
     * than Linux page counters or the host machine's memory capacity. */
    uint64_t memory=AvailMem(MEMF_TOTAL);
    return MIN(memory/4,UINT64_C(128)*1024*1024);
}

void zfs_ioctl_init_os(void)
{
    /* No AROS-only ioctl numbers yet. In particular, Linux user namespaces
     * and FreeBSD jail attachment must not be registered as supported here.
     * Common upstream ioctls are registered by zfs_ioctl_init(). */
}

void zfs_ioctl_update_mount_cache(const char *dataset)
{
    /* There is no separate mountpoint-property cache in the native handler.
     * It retains explicit DOS volume mounts, not paths copied from a Unix
     * mount table. Changing a dataset property must not rename an active DOS
     * volume. Property reads use the resident engine's current dataset state.
     * This hook does not implement libzfs mount/unmount or auto-mount policy. */
    (void)dataset;
}
