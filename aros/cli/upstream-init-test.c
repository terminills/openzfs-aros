/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <errno.h>
#include <aros/debug.h>
#include <stdlib.h>
#include <string.h>
#include "../../lib/libzfs/libzfs_impl.h"
int aros_zfs_ncpus(void);

int oz_upstream_init_test(void)
{
    int cpus=aros_zfs_ncpus();
    if(cpus<1)return EIO;
    for(unsigned i=0;i<128;i++)if(aros_zfs_ncpus()!=cpus)return EIO;
    bug("Public native CPU topology PASS cpus=%d queries=128\n",cpus);
    if(!strstr(libzfs_error_init(ENOENT),"not available")||
       !strstr(libzfs_error_init(EACCES),"denied")||
       !strstr(libzfs_error_init(EPROTO),"protocol")||
       !strstr(libzfs_error_init(EINVAL),"invalid")||
       !strstr(libzfs_error_init(ENOMEM),"memory")||
       zfs_mount_delegation_check()!=0)return EIO;
    bug("Public native init diagnostics PASS missing=1 denied=1 protocol=1 settings=1 memory=1\n");
    for (unsigned i = 0; i < 80; ++i) {
        libzfs_handle_t *first = libzfs_init();
        if (!first) return errno ? errno : EIO;
        libzfs_handle_t *second = libzfs_init();
        if (!second) {
            int error = errno;
            libzfs_fini(first);
            return error ? error : EIO;
        }
        libzfs_fini(first);
        libzfs_fini(second);
    }
    bug("Public upstream libzfs init PASS cycles=80 simultaneous=2\n");
    const char *previous = getenv("ZFS_SENDRECV_MAX_NVLIST");
    char *saved = previous ? strdup(previous) : NULL;
    if (previous && !saved) return ENOMEM;
    int error = 0;
    const char *invalid[] = { "invalid", "1oops", "18446744073709551616" };
    for (unsigned i = 0; i < 80 && !error; ++i) {
        if (setenv("ZFS_SENDRECV_MAX_NVLIST", "1M", 1)) { error = errno; break; }
        libzfs_handle_t *survivor = libzfs_init();
        if (!survivor) { error = errno ? errno : EIO; break; }
        if (survivor->libzfs_max_nvlist != 1024 * 1024) error = EIO;
        if (!error && setenv("ZFS_SENDRECV_MAX_NVLIST", invalid[i % 3], 1)) error = errno;
        if (!error) {
            errno = 0;
            libzfs_handle_t *bad = libzfs_init();
            int reported = errno;
            if (bad) libzfs_fini(bad);
            if (bad || reported != EINVAL) {
                bug("Public upstream init invalid-setting failure errno=%d expected=%d\n", reported, EINVAL);
                error = EIO;
            }
        }
        libzfs_fini(survivor);
    }
    int restored = saved ? setenv("ZFS_SENDRECV_MAX_NVLIST", saved, 1) : unsetenv("ZFS_SENDRECV_MAX_NVLIST");
    free(saved);
    if (!error && restored) error = errno;
    if (error) return error;
    bug("Public upstream init failure cleanup PASS cycles=80 survivor=1 errno=EINVAL\n");
    return 0;
}
