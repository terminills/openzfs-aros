/* SPDX-License-Identifier: CDDL-1.0 */
/* Explicit absent capabilities, following upstream FreeBSD's SMB provider.
 * Replace these tables when native export-server integration is available.
 * Neither storing a dataset property nor having an SMB client is an export.
 */
#include <libshare.h>
#include <errno.h>
#include <stdio.h>
#include "../../lib/libshare/libshare_impl.h"

static int unavailable(void)
{
    fputs("AROS OpenZFS: no native NFS/SMB export provider is installed.\n", stderr);
    errno=ENOTSUP;
    return SA_NOT_SUPPORTED;
}
static int change(sa_share_impl_t share)
{
    (void)share;
    return unavailable();
}
static int validate(const char *options)
{
    (void)options;
    return unavailable();
}
static boolean_t active(sa_share_impl_t share)
{
    (void)share;
    errno=ENOTSUP;
    return B_FALSE;
}
/* Even commit must not claim success. libshare's public wrapper discards its
 * return value, so the diagnostic is also emitted to stderr. No files change.
 */
const sa_fstype_t libshare_nfs_type={
    .enable_share=change,.disable_share=change,.is_shared=active,
    .validate_shareopts=validate,.commit_shares=unavailable
};
const sa_fstype_t libshare_smb_type={
    .enable_share=change,.disable_share=change,.is_shared=active,
    .validate_shareopts=validate,.commit_shares=unavailable
};
