/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <sys/dmu_objset.h>
#include <proto/exec.h>
#include "../ui/api.h"
#include "read-mount.h"
int oz_user_property_change(struct oz_message *,cred_t *);
/* Native DSL callback integration, separate from authenticated IPC coverage.
 * A synthetic cached znode makes relatime's time comparisons deterministic;
 * this is not a claim of DOS-file atime persistence. */
int oz_timestamp_policy_test(int write)
{
    const char *name="qemu_mirror/delegated-root";
    zfsvfs_t fs={0};struct oz_mount_policy *policy=NULL;
    int error=dmu_objset_own(name,DMU_OST_ZFS,!write,B_FALSE,&fs,&fs.z_os);
    if(error)return error;
    znode_t zp={.z_zfsvfs=&fs,.z_atime={100,0},.z_mtime={90,0},.z_ctime={90,0}};
    mutex_init(&zp.z_lock,NULL,MUTEX_DEFAULT,NULL);
    struct oz_message *m=calloc(1,sizeof(*m));
#define TS_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    TS_CHECK(m);m->operation=41;strcpy(m->target,name);
    TS_CHECK(!oz_mount_policy_open(&fs,!write,&policy));
    if(write) {
        strcpy(m->argument,"atime=on");TS_CHECK(!oz_user_property_change(m,kcred));
        strcpy(m->argument,"relatime=on");TS_CHECK(!oz_user_property_change(m,kcred));
        TS_CHECK(fs.z_atime&&fs.z_relatime);
        inode_timespec_t now={101,0};oz_znode_accessed_at(&zp,&now);
        TS_CHECK(zp.z_atime.tv_sec==100&&!zp.z_atime_dirty);
        now.tv_sec=86501;oz_znode_accessed_at(&zp,&now);
        TS_CHECK(zp.z_atime.tv_sec==86501&&zp.z_atime_dirty);zp.z_atime_dirty=B_FALSE;
        strcpy(m->argument,"relatime=off");TS_CHECK(!oz_user_property_change(m,kcred));
        now.tv_sec++;oz_znode_accessed_at(&zp,&now);
        TS_CHECK(!fs.z_relatime&&zp.z_atime.tv_sec==86502&&zp.z_atime_dirty);
        zp.z_atime_dirty=B_FALSE;
        strcpy(m->argument,"atime=off");TS_CHECK(!oz_user_property_change(m,kcred));
        now.tv_sec++;oz_znode_accessed_at(&zp,&now);
        TS_CHECK(!fs.z_atime&&zp.z_atime.tv_sec==86502&&!zp.z_atime_dirty);
        oz_mount_policy_close(policy);policy=NULL;
        TS_CHECK(!oz_mount_policy_open(&fs,B_TRUE,&policy));
        strcpy(m->argument,"atime=on");TS_CHECK(!oz_user_property_change(m,kcred));
        TS_CHECK(fs.z_atime&&fs.z_readonly);oz_znode_accessed_at(&zp,&now);
        TS_CHECK(zp.z_atime.tv_sec==86502&&!zp.z_atime_dirty);
    } else {
        TS_CHECK(!fs.z_atime&&!fs.z_relatime&&fs.z_readonly);
        inode_timespec_t now={999999,0};oz_znode_accessed_at(&zp,&now);
        TS_CHECK(zp.z_atime.tv_sec==100&&!zp.z_atime_dirty);
    }
done:
    if(policy)oz_mount_policy_close(policy);
    free(m);mutex_destroy(&zp.z_lock);dmu_objset_disown(fs.z_os,B_FALSE,&fs);
    char line[100];snprintf(line,sizeof(line),"Native timestamp callbacks write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef TS_CHECK
}
