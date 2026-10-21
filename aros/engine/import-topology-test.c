/* SPDX-License-Identifier: CDDL-1.0 */
/* Native config-admission regression. No paths are opened by this test. */
#include <sys/zfs_context.h>
#include <sys/fs/zfs.h>
#include "import-platform.h"
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/dsl_scan.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/txg.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include "device-io.h"
#include "partition-io.h"

int oz_import_topology_test(void)
{
    char *paths[]={"/AROS/ata.device/0/partition/0",
        "/AROS/ata.device/1/partition/0"};
    struct oz_import_paths allowed={paths,2,0};
    nvlist_t *members[2]={fnvlist_alloc(),fnvlist_alloc()};
    nvlist_t *replacement=fnvlist_alloc(),*root=fnvlist_alloc();
    nvlist_t *config=fnvlist_alloc();
    for(unsigned i=0;i<2;i++) {
        fnvlist_add_string(members[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
        fnvlist_add_string(members[i],ZPOOL_CONFIG_PATH,paths[i]);
    }
    fnvlist_add_string(replacement,ZPOOL_CONFIG_TYPE,VDEV_TYPE_REPLACING);
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    int result=0;
    /* A replacement tree must be admitted, but neither the replacement's
     * children nor auxiliary arrays may smuggle in a third device path. */
    const char *arrays[]={NULL,ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,
        ZPOOL_CONFIG_L2CACHE};
    for(unsigned i=0;i<4;i++) {
        nvlist_t *node=fnvlist_dup(replacement);
        fnvlist_add_nvlist_array(node,ZPOOL_CONFIG_CHILDREN,
            (const nvlist_t *const *)members,2);
        if(i) {
            nvlist_t *bad=fnvlist_dup(members[1]);
            fnvlist_add_string(bad,ZPOOL_CONFIG_PATH,"/AROS/ata.device/2/partition/0");
            fnvlist_add_nvlist_array(node,arrays[i],(const nvlist_t *const *)&bad,1);
            nvlist_free(bad);
        }
        fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,
            (const nvlist_t *const *)&node,1);
        fnvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
        int error=oz_import_config_check(&allowed,config);
        nvlist_free(node);
        if(error!=(i?EACCES:0)){result=1+i;break;}
    }
    nvlist_free(config);nvlist_free(root);nvlist_free(replacement);
    nvlist_free(members[0]);nvlist_free(members[1]);
    return result;
}

static int replacement_payload(uint64_t *object,int write)
{
    const char *dataset="replacement_recovery/payload";
    int error=0;
    if(write) {
        /* AROS has no ambient kernel credential.  Prove the common DMU
         * boundary refuses an anonymous create before entering the explicit
         * internal scope used by this engine-owned fixture. */
        if(dmu_objset_create(dataset,DMU_OST_OTHER,0,NULL,NULL,NULL)!=EACCES)
            return EIO;
        struct oz_cred_scope *scope=NULL;
        error=oz_cred_push(kcred,&scope);
        if(!error)error=dmu_objset_create(dataset,DMU_OST_OTHER,0,NULL,NULL,NULL);
        int restored=scope?oz_cred_pop(scope):0;
        if(!error)error=restored;
    }
    objset_t *os=NULL;
    if(!error)error=dmu_objset_own(dataset,DMU_OST_OTHER,!write,B_FALSE,FTAG,&os);
    if(error)return error;
    if(!write&&!*object) {
        dmu_object_info_t info;
        do {
            error=dmu_object_next(os,object,B_FALSE,0);
            if(!error)error=dmu_object_info(os,*object,&info);
        } while(!error&&info.doi_type!=DMU_OT_PLAIN_FILE_CONTENTS);
        if(error){dmu_objset_disown(os,B_FALSE,FTAG);return error;}
    }
    unsigned char expected[4096],actual[4096];
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(i*71u)^(i>>4);
    if(write) {
        dmu_tx_t *tx=dmu_tx_create(os);
        dmu_tx_hold_bonus(tx,DMU_NEW_OBJECT);
        dmu_tx_hold_write(tx,DMU_NEW_OBJECT,0,1024*1024);
        error=dmu_tx_assign(tx,DMU_TX_WAIT);
        if(error)dmu_tx_abort(tx);
        else {
            *object=dmu_object_alloc(os,DMU_OT_PLAIN_FILE_CONTENTS,4096,DMU_OT_NONE,0,tx);
            for(unsigned offset=0;offset<1024*1024;offset+=sizeof(expected))
                dmu_write(os,*object,offset,sizeof(expected),expected,tx,0);
            dmu_tx_commit(tx);txg_wait_synced(dmu_objset_pool(os),0);
        }
    } else for(unsigned offset=0;!error&&offset<1024*1024;offset+=sizeof(actual)) {
        error=dmu_read(os,*object,offset,sizeof(actual),actual,0);
        if(!error&&memcmp(expected,actual,sizeof(actual)))error=EIO;
    }
    dmu_objset_disown(os,B_FALSE,FTAG);return error;
}

int oz_replacement_recovery_test(void)
{
    const char *native[]={"RAM:oz-replacement-old.vdev","RAM:oz-replacement-new.vdev"};
    char paths[2][256];char *admitted[]={paths[0],paths[1]};
    nvlist_t *leaf[2]={fnvlist_alloc(),fnvlist_alloc()},*root=fnvlist_alloc();
    nvlist_t *props=fnvlist_alloc(),*exported=NULL,*found=NULL;
    spa_t *spa=NULL;uint64_t object=0,guid=0;int error=0,active=0;
    unsigned char *zero=calloc(1,1024*1024);if(!zero){error=ENOMEM;goto done;}
    for(unsigned i=0;i<2&&!error;i++) {
        BPTR existing=Lock((CONST_STRPTR)native[i],ACCESS_READ);
        if(existing){UnLock(existing);error=EEXIST;break;}
        BPTR file=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!file){error=EIO;break;}
        for(unsigned n=0;n<128;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=EIO;break;}
        if(!Close(file))error=EIO;
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[250];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))error=EIO;
        if(lock)UnLock(lock);
        if(error)break;
        char *colon=strchr(canonical,':');if(!colon){error=EINVAL;break;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_FILE);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_PATH,paths[i]);
        fnvlist_add_uint64(leaf[i],ZPOOL_CONFIG_ASHIFT,12);
    }
    free(zero);if(error)goto done;
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf[0],1);
    fnvlist_add_string(props,"cachefile","none");
    error=spa_create("replacement_recovery",root,props,NULL,NULL);
    if(error)goto done;active=1;
    error=replacement_payload(&object,1);if(error)goto done;
    error=spa_open("replacement_recovery",&spa,FTAG);if(error)goto done;
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    guid=spa->spa_root_vdev->vdev_child[0]->vdev_guid;
    spa_config_exit(spa,SCL_ALL,FTAG);
    zfs_scan_suspend_progress=1;
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf[1],1);
    error=spa_vdev_attach(spa,guid,root,1,0);
    spa_close(spa,FTAG);spa=NULL;if(error)goto done;
    error=spa_export("replacement_recovery",&exported,B_FALSE,B_FALSE);
    if(error)goto done;active=0;
    /* This diagnostic runs before the main mirror is created: no other pool
     * is active. Drop all engine caches and queues before label rediscovery.
     * Guest RAM files persist; this is an engine restart, not a guest reboot. */
    kernel_fini();
    kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);
    /* Rediscover from real on-disk labels, not the saved export nvlist. */
    struct oz_import_paths allowed={admitted,2,0};
    libpc_handle_t handle={.lpc_ops=&oz_aros_config_ops,.lpc_lib_handle=&allowed};
    importargs_t args={.path=admitted,.paths=2,.poolname="replacement_recovery",.scan=B_TRUE};
    error=zpool_find_config(&handle,"replacement_recovery",&found,&args);
    if(error)goto done;
    error=spa_import("replacement_recovery",found,props,0);
    if(error)goto done;active=1;
    error=spa_open("replacement_recovery",&spa,FTAG);if(error)goto done;
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    if(strcmp(spa->spa_root_vdev->vdev_child[0]->vdev_ops->vdev_op_type,VDEV_TYPE_REPLACING))error=EIO;
    spa_config_exit(spa,SCL_ALL,FTAG);
    zfs_scan_suspend_progress=0;
    int finished=0;
    for(unsigned i=0;!error&&i<120;i++) {
        spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
        vdev_t *vd=spa->spa_root_vdev->vdev_child[0];
        finished=vd->vdev_ops->vdev_op_leaf&&vd->vdev_path&&!strcmp(vd->vdev_path,paths[1]);
        spa_config_exit(spa,SCL_ALL,FTAG);
        if(finished)break;Delay(50);
    }
    if(!error&&!finished)error=ETIMEDOUT;
    spa_close(spa,FTAG);spa=NULL;
    if(!error)error=replacement_payload(&object,0);
done:
    zfs_scan_suspend_progress=0;
    if(spa)spa_close(spa,FTAG);
    if(active){int cleanup=spa_destroy("replacement_recovery");if(!error)error=cleanup;}
    if(!error)for(unsigned i=0;i<2;i++)if(!DeleteFile((CONST_STRPTR)native[i]))error=EIO;
    nvlist_free(found);nvlist_free(exported);nvlist_free(root);nvlist_free(props);
    nvlist_free(leaf[0]);nvlist_free(leaf[1]);return error;
}

/* Two separate guest boots, only the runner's fixed disposable GPT members. */
int oz_replacement_cold_test(int phase)
{
    int recover=phase==1,crash=phase==2;
    char *paths[]={"/AROS/ata.device/0/partition/0","/AROS/ata.device/1/partition/0"};
    struct oz_device_binding *bindings[2]={0};
    nvlist_t *leaf[2]={fnvlist_alloc(),fnvlist_alloc()},*root=fnvlist_alloc();
    nvlist_t *props=fnvlist_alloc(),*found=NULL;spa_t *spa=NULL;
    uint64_t object=0,guid=0;int error=0,initialized=0,active=0;
    unsigned char *blank=malloc(256*1024);if(!blank){error=ENOMEM;goto done;}
    for(unsigned i=0;i<2&&!error;i++) {
        struct oz_device *device=NULL;
        error=oz_device_open_partition("ata.device",i,0,1,&device);
        if(!error&&oz_device_size(device)!=256ULL*1024*1024)error=EINVAL;
        for(unsigned label=0;!error&&!recover&&label<4;label++) {
            uint64_t offset=label<2?label*256*1024ULL:oz_device_size(device)-(4-label)*256*1024ULL;
            error=oz_device_transfer(device,blank,256*1024,offset,0);
            for(unsigned n=0;!error&&n<256*1024;n++)if(blank[n])error=EEXIST;
        }
        if(!error)error=oz_device_bind_writable(paths[i],device,&bindings[i]);
        oz_device_close(device);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_PATH,paths[i]);
        fnvlist_add_uint64(leaf[i],ZPOOL_CONFIG_ASHIFT,12);
    }
    free(blank);if(error)goto done;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1)){error=EIO;goto done;}
    spa_config_path="/RAM/nonexistent-openzfs.cache";
    kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);initialized=1;
    fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    if(!recover) {
        fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf[0],1);
        error=spa_create("replacement_recovery",root,props,NULL,NULL);
        if(error)goto done;active=1;
        error=replacement_payload(&object,1);if(error)goto done;
        error=spa_open("replacement_recovery",&spa,FTAG);if(error)goto done;
        spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
        guid=spa->spa_root_vdev->vdev_child[0]->vdev_guid;
        spa_config_exit(spa,SCL_ALL,FTAG);
        zfs_scan_suspend_progress=1;
        fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf[1],1);
        error=spa_vdev_attach(spa,guid,root,1,0);
    } else {
        struct oz_import_paths allowed={paths,2,0};
        libpc_handle_t handle={.lpc_ops=&oz_aros_config_ops,.lpc_lib_handle=&allowed};
        importargs_t args={.path=paths,.paths=2,.poolname="replacement_recovery",.scan=B_TRUE};
        zfs_scan_suspend_progress=1;
        error=zpool_find_config(&handle,"replacement_recovery",&found,&args);
        if(!error)error=spa_import("replacement_recovery",found,props,0);
        if(error)goto done;active=1;
        error=spa_open("replacement_recovery",&spa,FTAG);
    }
    if(error)goto done;
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    if(strcmp(spa->spa_root_vdev->vdev_child[0]->vdev_ops->vdev_op_type,VDEV_TYPE_REPLACING))error=EIO;
    spa_config_exit(spa,SCL_ALL,FTAG);
    if(!error&&crash) {
        txg_wait_synced(spa->spa_dsl_pool,0);
        const char *marker="RESULT native cold replacement crash-ready PASS\n";
        while(*marker)RawPutChar(*marker++);
        /* The runner terminates this specific guest. No export, close,
         * kernel_fini or guest shutdown occurs after the marker. */
        for(;;)Delay(50);
    }
    if(!error&&recover) {
        zfs_scan_suspend_progress=0;
        int finished=0;
        for(unsigned i=0;i<120;i++) {
            spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
            vdev_t *vd=spa->spa_root_vdev->vdev_child[0];
            finished=vd->vdev_ops->vdev_op_leaf&&vd->vdev_path&&!strcmp(vd->vdev_path,paths[1]);
            spa_config_exit(spa,SCL_ALL,FTAG);
            if(finished)break;Delay(50);
        }
        if(!finished)error=ETIMEDOUT;
        if(!error)error=replacement_payload(&object,0);
    }
done:
    if(spa)spa_close(spa,FTAG);
    if(active){int cleanup=spa_export("replacement_recovery",NULL,B_FALSE,B_FALSE);if(!error)error=cleanup;}
    zfs_scan_suspend_progress=0;
    if(initialized)kernel_fini();
    for(unsigned i=0;i<2;i++)oz_device_unbind(bindings[i]);
    nvlist_free(found);nvlist_free(root);nvlist_free(props);
    nvlist_free(leaf[0]);nvlist_free(leaf[1]);return error;
}
