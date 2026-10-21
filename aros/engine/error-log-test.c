/* SPDX-License-Identifier: CDDL-1.0 */
/* Private fixture only. Generate real upstream persistent error records;
 * never manufacture the ioctl response or replace its copy/lookup code. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/txg.h>
#include <aros/debug.h>

int __real_spa_get_errlog(spa_t *,void *,uint64_t *);
int __wrap_spa_get_errlog(spa_t *spa,void *out,uint64_t *count)
{
    static int seeded;
    if(!seeded&&!strcmp(spa_name(spa),"qemu_mirror")) {
        txg_wait_synced(spa_get_dsl(spa),0);
        objset_t *os=NULL;uint64_t root=0,entry=0;
        int error=dmu_objset_hold("qemu_mirror/error-log",FTAG,&os);
        if(error)return error;
        error=zap_lookup(os,MASTER_NODE_OBJ,ZFS_ROOT_OBJ,8,1,&root);
        if(!error)error=zap_lookup(os,root,"evidence",8,1,&entry);
        if(!error) {
            zbookmark_phys_t bookmark={.zb_objset=dmu_objset_id(os),
                .zb_object=ZFS_DIRENT_OBJ(entry),.zb_level=0,.zb_blkid=0};
            for(unsigned i=0;i<2&&!error;i++) {
                uint64_t birth=0;
                zbookmark_err_phys_t probe={.zb_object=bookmark.zb_object,.zb_level=0,.zb_blkid=i};
                error=find_birth_txg(os->os_dsl_dataset,&probe,&birth);
                if(!error&&!birth)error=EIO;
                if(!error) {
                    bookmark.zb_blkid=i;spa_log_error(spa,&bookmark,birth);
                    if(!i)spa_log_error(spa,&bookmark,birth); /* duplicate must collapse */
                    bug("ERROR LOG FIXTURE block=%u birth=%llu\n",i,(unsigned long long)birth);
                }
            }
            if(!error) {
                seeded=1;
                bug("ERROR LOG FIXTURE recorded objset=%llu object=%llu blocks=2 duplicate=1\n",
                    (unsigned long long)bookmark.zb_objset,(unsigned long long)bookmark.zb_object);
            }
        }
        dmu_objset_rele(os,FTAG);
        if(error)return error;
        txg_wait_synced(spa_get_dsl(spa),0);
    }
    return __real_spa_get_errlog(spa,out,count);
}
