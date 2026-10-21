/* SPDX-License-Identifier: CDDL-1.0 */
/* Private disposable-QEMU instrumentation, never linked by default. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_dir.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include "../zpl/read-mount.h"
#include "zpl-fixture.h"
#include <stdio.h>
#include <proto/exec.h>

int __real_spa_scan_range(spa_t *,pool_scan_func_t,uint64_t,uint64_t);
int __real_spa_scan_stop(spa_t *);
int __real_spa_wait(const char *,zpool_wait_activity_t,boolean_t *);
static int first_scrub=1;
static znode_t *held_delete;
static struct oz_read_mount *held_mount;
int __real_oz_read_mount_remove(struct oz_read_mount *,uint64_t,const char *,int,cred_t *);
int __real_dsl_dir_wait(dsl_dir_t *,dsl_dataset_t *,zfs_wait_activity_t,boolean_t *);
int __real_oz_mount_unpublish(zfsvfs_t *);
int __real_zfs_suspend_fs(zfsvfs_t *);

static void report(const char *line)
{
    while(*line)RawPutChar(*line++);
}

int __wrap_spa_scan_range(spa_t *spa,pool_scan_func_t func,
    uint64_t txgstart,uint64_t txgend)
{
    if(!strcmp(spa_name(spa),"qemu_mirror")){
        if(func==POOL_SCAN_SCRUB&&first_scrub){
            first_scrub=0;zfs_scan_suspend_progress=1;
            report("WAIT TEST holding first scrub progress\n");
        }
    }
    int error=__real_spa_scan_range(spa,func,txgstart,txgend);
    if(error)zfs_scan_suspend_progress=0;
    return error;
}

int __wrap_spa_scan_stop(spa_t *spa)
{
    if(!strcmp(spa_name(spa),"qemu_mirror"))zfs_scan_suspend_progress=0;
    return __real_spa_scan_stop(spa);
}

int __wrap_spa_wait(const char *pool,zpool_wait_activity_t activity,boolean_t *waited)
{
    int error=__real_spa_wait(pool,activity,waited);
    if(!strcmp(pool,"qemu_mirror")&&activity==ZPOOL_WAIT_SCRUB){
        char line[96];
        snprintf(line,sizeof(line),"WAIT TEST upstream result errno=%d waited=%d\n",error,error?-1:*waited);
        report(line);
    }
    return error;
}

/* Retain a real deleted node in the private fixture. The DOS removal itself
 * still reports the genuine EBUSY from its synchronous reclaim attempt.
 * A second independent DOS removal releases the reference and drains both.
 * No wait implementation, activity flag or completion result is fabricated. */
int __wrap_oz_read_mount_remove(struct oz_read_mount *mount,uint64_t parent,
    const char *name,int directory,cred_t *cr)
{
    objset_t *os=oz_read_mount_objset(mount);
    char dataset[ZFS_MAX_DATASET_NAME_LEN];dmu_objset_name(os,dataset);
    if(!strcmp(dataset,"qemu_mirror/cli-waitfs")) {
        if(!strcmp(name,"held-deleteq")) {
            if(held_delete)return EBUSY;
            struct oz_zpl_info info;
            int error=oz_read_mount_lookup(mount,parent,name,&info);
            if(error)return error;
            mutex_enter(&os->os_user_ptr_lock);
            zfsvfs_t *fs=dmu_objset_get_user(os);
            mutex_exit(&os->os_user_ptr_lock);
            if(!fs)return EIO;
            error=zfs_zget(fs,info.object,&held_delete);
            if(error)return error;
            held_mount=mount;
            report("FS WAIT TEST retaining real deleted node\n");
        } else if(!strcmp(name,"release-deleteq")&&held_delete) {
            if(held_mount!=mount)return EINVAL;
            oz_znode_rele(held_delete);held_delete=NULL;held_mount=NULL;
            report("FS WAIT TEST releasing real deleted node\n");
        }
    }
    return __real_oz_read_mount_remove(mount,parent,name,directory,cr);
}

int __wrap_dsl_dir_wait(dsl_dir_t *dd,dsl_dataset_t *ds,
    zfs_wait_activity_t activity,boolean_t *waited)
{
    int error=__real_dsl_dir_wait(dd,ds,activity,waited);
    char dataset[ZFS_MAX_DATASET_NAME_LEN];dsl_dataset_name(ds,dataset);
    if(!strcmp(dataset,"qemu_mirror/cli-waitfs")) {
        char line[100];snprintf(line,sizeof(line),
            "FS WAIT TEST upstream result errno=%d waited=%d\n",error,*waited);
        report(line);
    }
    return error;
}

int __wrap_oz_mount_unpublish(zfsvfs_t *fs)
{
    /* Permit the test mount to close while its genuine queue is pending.
     * Releasing the node does not reap it: production unpublish must cancel
     * the waiter before normal close drains the remaining unlinked set. */
    if(held_delete&&held_mount&&oz_read_mount_objset(held_mount)==fs->z_os) {
        oz_znode_rele(held_delete);held_delete=NULL;held_mount=NULL;
        report("FS WAIT TEST releasing reference for native unmount\n");
    }
    return __real_oz_mount_unpublish(fs);
}

int __wrap_zfs_suspend_fs(zfsvfs_t *fs)
{
    if(held_delete&&held_mount&&oz_read_mount_objset(held_mount)==fs->z_os) {
        oz_znode_rele(held_delete);held_delete=NULL;held_mount=NULL;
        report("FS WAIT TEST releasing reference for native suspend\n");
    }
    int error=__real_zfs_suspend_fs(fs);
    char line[96];snprintf(line,sizeof(line),"FS WAIT TEST native suspend errno=%d\n",error);
    report(line);
    return error;
}
