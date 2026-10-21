/* SPDX-License-Identifier: CDDL-1.0 */
/* Private observation only: preserve every production call and result. */
#include <sys/zfs_context.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <sys/zil.h>
#include <proto/exec.h>
#include <stdio.h>

int __real_dmu_objset_own(const char *,dmu_objset_type_t,boolean_t,boolean_t,const void *,objset_t **);
int __real_zil_replay_checked(objset_t *,void *,zil_replay_func_t *const [TX_MAX_TYPE],boolean_t *);

static void report(const char *stage,objset_t *os,int error,int replayed)
{
    char name[ZFS_MAX_DATASET_NAME_LEN],line[384];
    dsl_dataset_name(dmu_objset_ds(os),name);
    if(strcmp(name,"qemu_mirror/raw-received"))return;
    snprintf(line,sizeof(line),"RAW MOUNT TRACE %s error=%d replayed=%d flags=%llu users=%d objects=%d projects=%d upgrade-objects=%d upgrade-projects=%d zil-flags=%llu zil-hole=%d\n",
        stage,error,replayed,(unsigned long long)os->os_phys->os_flags,
        !!dmu_objset_userspace_present(os),!!dmu_objset_userobjspace_present(os),
        !!dmu_objset_projectquota_present(os),!!dmu_objset_userobjspace_upgradable(os),
        !!dmu_objset_projectquota_upgradable(os),
        (unsigned long long)os->os_phys->os_zil_header.zh_flags,
        !!BP_IS_HOLE(&os->os_phys->os_zil_header.zh_log));
    for(const char *p=line;*p;p++)RawPutChar(*p);
}

int __wrap_dmu_objset_own(const char *name,dmu_objset_type_t type,
    boolean_t readonly,boolean_t decrypt,const void *tag,objset_t **out)
{
    if(!strcmp(name,"qemu_mirror/raw-received")){
        objset_t *before=NULL;
        if(!dmu_objset_hold(name,FTAG,&before)){
            report(readonly?"before-own-readonly":"before-own-writable",before,0,-1);
            dmu_objset_rele(before,FTAG);
        }
    }
    int error=__real_dmu_objset_own(name,type,readonly,decrypt,tag,out);
    if(!error)report("after-own",*out,0,-1);
    return error;
}

int __wrap_zil_replay_checked(objset_t *os,void *arg,
    zil_replay_func_t *const vector[TX_MAX_TYPE],boolean_t *replayed)
{
    report("before-replay",os,0,-1);
    int error=__real_zil_replay_checked(os,arg,vector,replayed);
    report("after-replay",os,error,error?-1:*replayed);
    return error;
}
