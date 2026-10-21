/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_ioctl.h>
#include <sys/zfs_ioctl_impl.h>
#include <proto/exec.h>
#include "control-file.h"
static struct Task *owner;
static unsigned live_files;

int zfsdev_attach(void)
{
    if(owner)return EBUSY;
    owner=FindTask(NULL);
    return 0;
}
void zfsdev_detach(void)
{
    ASSERT(owner==FindTask(NULL));
    ASSERT3U(__atomic_load_n(&live_files,__ATOMIC_ACQUIRE),==,0);
    owner=NULL;
}
static void release_state(void *state)
{
    zfsdev_state_destroy_state(state);
    unsigned previous=__atomic_fetch_sub(&live_files,1,__ATOMIC_RELEASE);
    ASSERT3U(previous,>,0);
}
void zfsdev_private_set_state(void *private,zfsdev_state_t *state)
{
    /* File is still unpublished and has exactly its constructor reference. */
    VERIFY0(oz_file_set_private(private,state,release_state));
    __atomic_fetch_add(&live_files,1,__ATOMIC_RELAXED);
}
zfsdev_state_t *zfsdev_private_get_state(void *private)
{return zfs_file_private(private);}
static int control_read(void *arg,void *buffer,size_t length,size_t *done)
{
    (void)arg;(void)buffer;(void)length;
    *done=0;
    return ENOTSUP; /* Control descriptors have no byte-stream data. */
}
int oz_control_file_open(zfs_file_t **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!owner)return ENODEV;
    if(owner!=FindTask(NULL))return EDEADLK;
    zfs_file_t *file=NULL;
    /* Reader context must be non-null and outlive all file references. */
    int error=zfs_file_from_reader(control_read,&live_files,&file);
    if(error)return error;
    mutex_enter(&zfsdev_state_lock);
    error=zfsdev_state_init(file);
    mutex_exit(&zfsdev_state_lock);
    if(error)zfs_file_close(file);else *out=file;
    return error;
}
int oz_control_start(void)
{
    if(owner)return EBUSY;
    zfsdev_state_global_init();
    int error=zfsdev_attach();
    if(error)zfsdev_state_global_fini();
    return error;
}
int oz_control_stop(void)
{
    if(!owner)return ENODEV;
    if(owner!=FindTask(NULL))return EDEADLK;
    if(__atomic_load_n(&live_files,__ATOMIC_ACQUIRE))return EBUSY;
    zfsdev_detach();
    zfsdev_state_global_fini();
    return 0;
}
