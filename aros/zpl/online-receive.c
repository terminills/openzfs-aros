/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_vfsops.h>
#include <sys/zfs_ioctl_impl.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_recv.h>
#include "read-mount.h"
#include "online-receive.h"

static int (*owner_call)(int (*)(void *),void *);
void oz_receive_bind_owner(int (*call)(int (*)(void *),void *))
{ owner_call=call; }
struct receive_end {
    zfsvfs_t *fs;
    dmu_recv_cookie_t *cookie;
    int end_redacted,executed;
};
static int finish_receive(void *arg)
{
    struct receive_end *r=arg;
    /* Check before touching the cookie: a failed dispatch must still clean
     * up upstream's receive state exactly once. */
    if(!oz_read_mount_is_owner((struct oz_read_mount *)r->fs))return EDEADLK;
    r->executed=1;
    dsl_dataset_t *ds=dmu_objset_ds(r->fs->z_os);
    /* The receive cookie owns a temporary dataset whose redaction feature
     * may already be active. Only the live mount tells us whether this is
     * a transition from an ordinary namespace to a redacted one. */
    int end_redacted=r->end_redacted&&!dsl_get_redacted(ds);
    int error=zfs_suspend_fs(r->fs);
    int end_error=dmu_recv_end(r->cookie,r->fs);
    if(!error)error=end_redacted?zfs_end_fs(r->fs,ds):zfs_resume_fs(r->fs,ds);
    return error?error:end_error;
}
int oz_receive_end_owner(zfsvfs_t *fs,dmu_recv_cookie_t *cookie,int end_redacted)
{
    struct receive_end r={fs,cookie,end_redacted,0};
    int error=oz_read_mount_is_owner((struct oz_read_mount *)fs)?finish_receive(&r):
        owner_call?owner_call(finish_receive,&r):EDEADLK;
    if(!r.executed) {
        /* The live mount still owns the destination. NULL cannot hand off
         * that ownership; upstream rejects the swap and cleans the receive.
         * Keep the original dispatch error even if cleanup also fails. */
        (void)dmu_recv_end(cookie,NULL);
    }
    return error;
}
#ifdef OZ_NATIVE_DISPATCHER
int oz_receive_end_dataset(const char *name,dmu_recv_cookie_t *cookie,int end_redacted)
{
    zfsvfs_t *fs=NULL;
    if(getzfsvfs(name,&fs)!=0)return dmu_recv_end(cookie,NULL);
    int error=oz_receive_end_owner(fs,cookie,end_redacted);
    zfs_vfs_rele(fs);
    return error;
}
#endif
