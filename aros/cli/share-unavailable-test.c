/* SPDX-License-Identifier: CDDL-1.0 */
#include <libshare.h>
#include <errno.h>
#include <aros/debug.h>
#include "../../lib/libshare/libshare_impl.h"
int main(void)
{
    const sa_fstype_t *types[]={&libshare_nfs_type,&libshare_smb_type};
    struct sa_share_impl share={"test/files","TEST:","on"};
    for(unsigned i=0;i<2;i++) {
        const sa_fstype_t *type=types[i];
        errno=0;
        if(type->enable_share(&share)!=SA_NOT_SUPPORTED||errno!=ENOTSUP)goto fail;
        errno=0;
        if(type->disable_share(&share)!=SA_NOT_SUPPORTED||errno!=ENOTSUP)goto fail;
        errno=0;
        if(type->validate_shareopts("on")!=SA_NOT_SUPPORTED||errno!=ENOTSUP)goto fail;
        errno=0;
        if(type->is_shared(&share)!=B_FALSE||errno!=ENOTSUP)goto fail;
        errno=0;
        if(type->commit_shares()!=SA_NOT_SUPPORTED||errno!=ENOTSUP)goto fail;
    }
    bug("RESULT native CLI absent sharing PASS protocols=2 actions=5\n");
    return 0;
fail:
    bug("RESULT native CLI absent sharing FAIL\n");
    return 20;
}
