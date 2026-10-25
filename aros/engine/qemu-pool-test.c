/* SPDX-License-Identifier: CDDL-1.0 */
/* Destructive test ONLY for explicit disposable QEMU disks or partitions. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/txg.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/filehandler.h>
#include <stdarg.h>
#include "device-io.h"
#include "partition-io.h"
#include "zpl-fixture.h"
int oz_dos_handler_test(struct oz_zpl *);
int oz_legacy_cold_test(boolean_t);
int oz_special_file_test(boolean_t);
int oz_mount_lifecycle_test(boolean_t);
int oz_dos_write_test(int);
int oz_uio_test(void);
int oz_import_topology_test(void);
int oz_replacement_recovery_test(void);
int oz_replacement_cold_test(int);
int oz_cred_test(void);
int oz_delegation_test(void);
int oz_fuid_test(void);
int oz_timestamps_test(void);
int oz_vdev_policy_test(void);
int oz_atime_persistence_test(boolean_t);
int oz_write_persistence_test(boolean_t);
int oz_packet_cred_test(void);
int oz_dio_test(void);
int oz_zpl_layout_test(void);
int oz_async_config_suspend_test(void);
int oz_file_limits_test(void);
int oz_acl_storage_test(void);
int oz_acl_core_test(void);
int oz_dio_read_test(objset_t *, uint64_t);
int oz_dio_write_test(objset_t *, uint64_t);
int oz_buffered_test(objset_t *, uint64_t);

struct taskq_probe {
    taskq_t *queue;
    unsigned started,completed,release,bad_member;
};

/* Real on-disk objects, not duplicate synthetic cache entries. Reopening
 * read-only removes the writer's cache and excludes sync-driven trimming. */
static void report(const char *format,...);
static int cache_pressure_test(void)
{
    report("Cache pressure begin files=320 readonly-passes=3\n");
    const char *dataset = "qemu_mirror/cachepressure";
    int error = oz_zpl_fixture_create(dataset);
    struct oz_zpl *fs = NULL;
    if (!error) error = oz_zpl_open_writable(dataset, &fs);
    for (unsigned i = 0; !error && i < 320; i++) {
        char name[32];
        snprintf(name, sizeof(name), "cache-%u", i);
        struct oz_zpl_info info;
        error = oz_zpl_create_as(fs, oz_zpl_root(fs), name, 0, kcred, &info);
        uint64_t payload = i + 0x12340000ULL;
        size_t written = 0;
        if (!error) error = oz_zpl_write_as(fs, info.object, 0, &payload,
            sizeof(payload), &written, kcred);
        if (!error && written != sizeof(payload)) error = EIO;
        if (!error && (i+1)%32 == 0)
            report("Cache pressure created=%u\n",i+1);
    }
    if (fs) {
        int close_error = oz_zpl_close(fs);
        if (!error) error = close_error;
        fs = NULL;
    }
    if (!error) error = oz_zpl_open(dataset, &fs);
    for (unsigned pass = 0; !error && pass < 3; pass++) {
        for (unsigned i = 0; !error && i < 320; i++) {
            char name[32];
            snprintf(name, sizeof(name), "cache-%u", i);
            struct oz_zpl_info info;
            error = oz_zpl_lookup_at(fs, oz_zpl_root(fs), name, &info);
            uint64_t payload = 0, cached = 0;
            if (!error) error = oz_zpl_read(fs, info.object, 0, &payload,
                sizeof(payload));
            if (!error && (payload != i + 0x12340000ULL ||
                info.size != sizeof(payload))) error = EIO;
            if (!error) error = oz_zpl_counter(fs, "cached_nodes", &cached);
            /* Lookup retains its directory and child while stat trims. */
            if (!error && cached > 258) error = EOVERFLOW;
            if (!error && (i+1)%64 == 0)
                report("Cache pressure pass=%u read=%u\n",pass+1,i+1);
        }
    }
    if (fs) {
        int close_error = oz_zpl_close(fs);
        if (!error) error = close_error;
    }
    return error;
}
static unsigned taskq_fail_spawn,taskq_failed_spawns;
extern kthread_t *__real_zk_thread_create_optional(const char *,void (*)(void *),
    void *,size_t,int);
kthread_t *__wrap_zk_thread_create_optional(const char *name,void (*func)(void *),
    void *arg,size_t stack,int state)
{
    if(!strcmp(name,"aros-failure-test")&&
        __atomic_load_n(&taskq_fail_spawn,__ATOMIC_ACQUIRE)) {
        __atomic_fetch_add(&taskq_failed_spawns,1,__ATOMIC_RELEASE);
        return NULL;
    }
    return __real_zk_thread_create_optional(name,func,arg,stack,state);
}
static void taskq_probe_job(void *arg)
{
    struct taskq_probe *p=arg;
    if(!taskq_member(p->queue,curthread)||taskq_of_curthread()!=p->queue)
        __atomic_fetch_add(&p->bad_member,1,__ATOMIC_RELAXED);
    __atomic_fetch_add(&p->started,1,__ATOMIC_RELEASE);
    while(!__atomic_load_n(&p->release,__ATOMIC_ACQUIRE))Delay(1);
    __atomic_fetch_add(&p->completed,1,__ATOMIC_RELEASE);
}
static int taskq_growth_test(void)
{
    for(unsigned round=0;round<16;round++) {
        struct taskq_probe p={0};taskq_ent_t entries[8];
        p.queue=taskq_create("aros-growth-test",8,minclsyspri,0,64,TASKQ_DYNAMIC);
        int error=p.queue->tq_nthreads!=1;
        for(unsigned i=0;i<16;i++) {
            if(i&1) {
                taskq_init_ent(&entries[i/2]);
                taskq_dispatch_ent(p.queue,taskq_probe_job,&p,0,&entries[i/2]);
            } else if(!taskq_dispatch(p.queue,taskq_probe_job,&p,TQ_SLEEP))error=1;
        }
        unsigned polls=0;
        while(__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)<8&&polls++<500)Delay(1);
        if(__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)!=8||p.completed)error=2;
        mutex_enter(&p.queue->tq_lock);
        if(p.queue->tq_nthreads!=8)error=3;
        mutex_exit(&p.queue->tq_lock);
        if(taskq_dispatch(p.queue,taskq_probe_job,&p,TQ_NOQUEUE)!=TASKQID_INVALID)error=5;
        __atomic_store_n(&p.release,1,__ATOMIC_RELEASE);
        taskq_wait(p.queue);
        if(!error&&(p.started!=16||p.completed!=16||p.bad_member))error=4;
        if(!error) {
            if(taskq_dispatch(p.queue,taskq_probe_job,&p,TQ_NOQUEUE)==TASKQID_INVALID)error=6;
            taskq_wait(p.queue);
            if(p.started!=17||p.completed!=17||p.bad_member)error=7;
        }
        taskq_destroy(p.queue);
        if(error)return error;
    }
    return 0;
}
static int taskq_failure_test(void)
{
    struct taskq_probe p={0};
    p.queue=taskq_create("aros-failure-test",8,minclsyspri,0,64,TASKQ_DYNAMIC);
    __atomic_store_n(&taskq_fail_spawn,1,__ATOMIC_RELEASE);
    int error=0;
    for(unsigned i=0;i<16;i++)
        if(!taskq_dispatch(p.queue,taskq_probe_job,&p,TQ_SLEEP))error=8;
    unsigned polls=0;
    while((!__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)||
        !__atomic_load_n(&taskq_failed_spawns,__ATOMIC_ACQUIRE))&&polls++<500)Delay(1);
    if(__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)!=1||
        !__atomic_load_n(&taskq_failed_spawns,__ATOMIC_ACQUIRE))error=9;
    __atomic_store_n(&p.release,1,__ATOMIC_RELEASE);
    taskq_wait(p.queue);
    if(p.completed!=16||p.bad_member)error=10;
    /* Wait drained spawn reservations as well as jobs. Reuse the same queue
     * after recovery, proving no leaked reservation prevents later growth. */
    __atomic_store_n(&taskq_fail_spawn,0,__ATOMIC_RELEASE);
    p.started=p.completed=p.release=0;
    for(unsigned i=0;i<16;i++)
        if(!taskq_dispatch(p.queue,taskq_probe_job,&p,TQ_SLEEP))error=11;
    polls=0;
    while(__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)<8&&polls++<500)Delay(1);
    if(__atomic_load_n(&p.started,__ATOMIC_ACQUIRE)!=8)error=12;
    __atomic_store_n(&p.release,1,__ATOMIC_RELEASE);
    taskq_wait(p.queue);
    if(p.completed!=16||p.bad_member)error=13;
    taskq_destroy(p.queue);
    return error;
}
static void report(const char *format,...)
{
    char buf[512];va_list ap;va_start(ap,format);
    vsnprintf(buf,sizeof(buf),format,ap);va_end(ap);
    Write(Output(),buf,strlen(buf));
    for(char *p=buf;*p;p++)RawPutChar(*p);
}
static int data(uint64_t *object,int writing)
{
    objset_t *os;int rc;
    unsigned char expected[16384],actual[16384];
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(i*37u)^(i>>5)^0xa7^((i>=3077&&i<9078)?0x5a:0);
    if(writing && (rc=dmu_objset_create("qemu_mirror/payload",DMU_OST_OTHER,0,NULL,NULL,NULL)))return rc;
    rc=dmu_objset_own("qemu_mirror/payload",DMU_OST_OTHER,!writing,B_FALSE,FTAG,&os);
    if(rc)return rc;
    if(writing) {
        dmu_tx_t *tx=dmu_tx_create(os);
        dmu_tx_hold_bonus(tx,DMU_NEW_OBJECT);
        dmu_tx_hold_write(tx,DMU_NEW_OBJECT,0,sizeof(expected));
        rc=dmu_tx_assign(tx,DMU_TX_WAIT);
        if(rc)dmu_tx_abort(tx);
        else {
            *object=dmu_object_alloc(os,DMU_OT_PLAIN_FILE_CONTENTS,4096,DMU_OT_NONE,0,tx);
            memset(actual,0,sizeof(actual));
            dmu_write(os,*object,0,sizeof(actual),actual,tx,0);
            dmu_tx_commit(tx);txg_wait_synced(dmu_objset_pool(os),0);
        }
        if(!rc){report("Direct write helper address=%p\n",oz_dio_write_test);rc=oz_dio_write_test(os,*object);report("Upstream native direct UIO write=%d\n",rc);}
        if(!rc){rc=oz_buffered_test(os,*object);report("Upstream buffered UIO partial-block write/read=%d\n",rc);}
    } else {
        rc=dmu_read(os,*object,0,sizeof(actual),actual,0);
        if(!rc && memcmp(expected,actual,sizeof(actual)))rc=EIO;
        if(!rc){rc=oz_dio_read_test(os,*object);report("Upstream native direct UIO read/compare=%d\n",rc);}
    }
    dmu_objset_disown(os,B_FALSE,FTAG);return rc;
}
static int large_device_test(unsigned unit)
{
    const uint64_t offsets[]={UINT64_C(0x100000),UINT64_C(0x100100000),
        UINT64_C(0x100000000)-512};
    unsigned char expected[1024],actual[1024];
    struct oz_device *d=NULL;int rc=oz_device_open("ata.device",unit,1,&d);
    if(!rc&&(oz_device_size(d)!=8ULL*1024*1024*1024||oz_device_sector(d)!=512))rc=EINVAL;
    /* Distinct data at equal low 32-bit offsets detects aliasing; the final
     * request crosses the 4 GiB boundary in one native transfer. */
    for(unsigned i=0;i<3&&!rc;i++) {
        for(unsigned j=0;j<sizeof(expected);j++)expected[j]=(j*37u)^(i*83u)^0xa7;
        rc=oz_device_transfer(d,expected,sizeof(expected),offsets[i],1);
    }
    if(!rc)rc=oz_device_flush(d);
    for(unsigned i=0;i<3&&!rc;i++) {
        for(unsigned j=0;j<sizeof(expected);j++)expected[j]=(j*37u)^(i*83u)^0xa7;
        rc=oz_device_transfer(d,actual,sizeof(actual),offsets[i],0);
        if(!rc&&memcmp(actual,expected,sizeof(actual)))rc=EIO;
        report("native large-device unit=%u offset=%llu comparison=%d\n",unit,
            (unsigned long long)offsets[i],rc);
    }
    memset(expected,0,sizeof(expected));
    for(unsigned i=0;i<3&&!rc;i++)rc=oz_device_transfer(d,expected,sizeof(expected),offsets[i],1);
    if(!rc)rc=oz_device_flush(d);
    for(unsigned i=0;i<3&&!rc;i++) {
        rc=oz_device_transfer(d,actual,sizeof(actual),offsets[i],0);
        if(!rc&&memcmp(actual,expected,sizeof(actual)))rc=EIO;
    }
    oz_device_close(d);report("native large-device unit=%u restored=%d\n",unit,rc);return rc;
}
int main(int argc,char **argv)
{
    if(argc==2&&(!strcmp(argv[1],"REPLACEMENT-STAGE")||!strcmp(argv[1],"REPLACEMENT-RECOVER")||!strcmp(argv[1],"REPLACEMENT-CRASH"))) {
        int recover=!strcmp(argv[1],"REPLACEMENT-RECOVER");
        int error=oz_replacement_cold_test(!strcmp(argv[1],"REPLACEMENT-CRASH")?2:recover);
        report("RESULT native cold replacement %s %s errno=%d\n",recover?"recover":"stage",error?"FAIL":"PASS",error);
        return error?20:0;
    }
    if(argc!=2 || (strcmp(argv[1],"DISPOSABLE")&&strcmp(argv[1],"DISPOSABLE-8G")&&strcmp(argv[1],"DISPOSABLE-PARTITION")&&strcmp(argv[1],"DISPOSABLE-RAIDZ1")&&strcmp(argv[1],"DISPOSABLE-RAIDZ2")&&strcmp(argv[1],"DISPOSABLE-RAIDZ3")&&strcmp(argv[1],"DISPOSABLE-AHCI3T")))return 20;
    int ahci_large=!strcmp(argv[1],"DISPOSABLE-AHCI3T");
    unsigned raidz=!strcmp(argv[1],"DISPOSABLE-RAIDZ3")?3:!strcmp(argv[1],"DISPOSABLE-RAIDZ2")?2:
        !strcmp(argv[1],"DISPOSABLE-RAIDZ1")?1:0;
    const char *layout=raidz==3?"raidz3":raidz==2?"raidz2":raidz==1?"raidz1":"mirror";
    unsigned count=raidz==3?4:raidz?3:2;
    const unsigned units[]={0,1,3,0}; /* ATA unit 2 is the readonly boot CD. */
    const char *devices[]={"ata.device","ata.device","ata.device","ahci.device"};
    if(ahci_large)devices[0]=devices[1]="ahci.device";
    int large=!strcmp(argv[1],"DISPOSABLE-8G");
    int partition=ahci_large||raidz||!strcmp(argv[1],"DISPOSABLE-PARTITION");
    uint64_t expected_size=large?8ULL*1024*1024*1024:256ULL*1024*1024;
    if(ahci_large)expected_size=(UINT64_C(3)<<40)-(UINT64_C(2)<<20);
    report("QEMU upstream pool test starting; handler access is a separate test\n");
    int layout_result = oz_zpl_layout_test();
    report("Native engine/ZPL shared-layout tests=%d\n", layout_result);
    if (layout_result) return 20;
    int limits_result = oz_file_limits_test();
    report("Native file-size limits/internal flags tests=%d\n", limits_result);
    if (limits_result) return 20;
    int acl_result = oz_acl_storage_test();
    report("Native upstream ACL storage helper tests=%d\n", acl_result);
    if (acl_result) return 20;
    int acl_core_result = oz_acl_core_test();
    report("Native upstream ACL conversion/core tests=%d\n", acl_core_result);
    if (acl_core_result) return 20;
    int uio_result = oz_uio_test();
    report("Native UIO scatter/gather/copy/skip/64-bit/bounds tests=%d\n", uio_result);
    if (uio_result) return 20;
    int cred_result = oz_cred_test();
    report("Native credential identity/groups/refcount/policy tests=%d\n", cred_result);
    if (cred_result) return 20;
    int fuid_result = oz_fuid_test();
    report("Native upstream FUID mapping/AVL/unmapped-identity tests=%d\n", fuid_result);
    if (fuid_result) return 20;
    int timestamp_result = oz_timestamps_test();
    report("Native ZPL timestamps/relatime/readonly/dirty-state tests=%d\n", timestamp_result);
    if (timestamp_result) return 20;
    if (oz_vdev_policy_test()) return 20;
    /* Refuse used disks: all four prospective label regions must be zero. */
    unsigned char *blank=malloc(256*1024);if(!blank)return 20;
    for(unsigned member=0;member<count;member++) {
        unsigned unit=units[member];
        struct oz_device *d=NULL;int rc=oz_device_open(devices[member],unit,0,&d);
        report("disk %s/%u raw-open=%d bytes=%llu\n",devices[member],unit,rc,
            (unsigned long long)(d?oz_device_size(d):0));
        if(partition&&!rc) {
            if(oz_device_size(d)!=(ahci_large?(UINT64_C(3)<<40):512ULL*1024*1024))rc=EINVAL;
            oz_device_close(d);d=NULL;
            if(!rc)rc=oz_device_open_partition(devices[member],unit,0,0,&d);
            report("disk %s/%u partition-open=%d bytes=%llu\n",devices[member],unit,rc,
                (unsigned long long)(d?oz_device_size(d):0));
        }
        if(!rc && oz_device_size(d)!=expected_size)rc=EINVAL;
        for(unsigned label=0;!rc && label<4;label++) {
            uint64_t off=label<2?label*256*1024ULL:oz_device_size(d)-(4-label)*256*1024ULL;
            rc=oz_device_transfer(d,blank,256*1024,off,0);
            for(unsigned j=0;!rc && j<256*1024;j++)if(blank[j])rc=EEXIST;
        }
        oz_device_close(d);report("disk %s/%u blank-label preflight=%d\n",devices[member],unit,rc);
        if(rc){free(blank);return 20;}
    }
    free(blank);
    if(large)for(unsigned unit=0;unit<2;unit++)if(large_device_test(unit))return 20;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1) || setenv("ZFS_HOSTID","0x51a20519",1))return 20;
    /* Retain one explicit writer admission per blank-checked member, as the
     * service does. Label probes and vdev opens borrow that same admission
     * rather than competing for overlapping independent writer reservations. */
    struct oz_device_binding *bindings[4]={0};
    for(unsigned i=0;i<count;i++) {
        struct oz_device *device=NULL;char path[80];
        snprintf(path,sizeof(path),"/AROS/%s/%u%s",devices[i],units[i],partition?"/partition/0":"");
        int error=partition?oz_device_open_partition(devices[i],units[i],0,1,&device):
            oz_device_open(devices[i],units[i],1,&device);
        if(!error)error=oz_device_bind_writable(path,device,&bindings[i]);
        oz_device_close(device);
        if(error){
            for(unsigned j=0;j<count;j++)oz_device_unbind(bindings[j]);
            report("Native fixture binding failed unit=%u errno=%d\n",units[i],error);
            struct DosList *list=LockDosList(LDF_DEVICES|LDF_READ),*entry=list;
            while(list&&(entry=NextDosEntry(entry,LDF_DEVICES))) {
                struct FileSysStartupMsg *s=BADDR(entry->dol_misc.dol_handler.dol_Startup);
                if(!s||!TypeOfMem(s)||!TypeOfMem((char *)s+sizeof(*s)-1))continue;
                struct DosEnvec *e=BADDR(s->fssm_Environ);
                if(!e||!TypeOfMem(e)||!TypeOfMem((char *)e+sizeof(*e)-1)||
                    e->de_TableSize<DE_DOSTYPE||e->de_TableSize>255)continue;
                report("Native fixture DOS claim candidate name=%s unit=%lu\n",
                    AROS_BSTR_ADDR(entry->dol_Name),(unsigned long)s->fssm_Unit);
            }
            if(list)UnLockDosList(LDF_DEVICES|LDF_READ);
            return 20;
        }
    }
    spa_config_path="/RAM/nonexistent-openzfs.cache";
    nvlist_t *leaves[4],*mirror=fnvlist_alloc(),*root=fnvlist_alloc(),*props=fnvlist_alloc(),*exported=NULL;
    for(unsigned i=0;i<count;i++) {
        leaves[i]=fnvlist_alloc();
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
        char path[80];snprintf(path,sizeof(path),"/AROS/%s/%u%s",devices[i],units[i],partition?"/partition/0":"");
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_PATH,path);
        fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,12);
    }
    const nvlist_t *mirror_view[]={mirror};
    fnvlist_add_string(mirror,ZPOOL_CONFIG_TYPE,raidz?VDEV_TYPE_RAIDZ:VDEV_TYPE_MIRROR);
    if(raidz)fnvlist_add_uint64(mirror,ZPOOL_CONFIG_NPARITY,raidz);
    fnvlist_add_nvlist_array(mirror,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)leaves,count);
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,mirror_view,1);
    fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"failmode",ZIO_FAILURE_MODE_CONTINUE);
    report("kernel_init read/write\n");kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);
    int topology_result=oz_import_topology_test();
    report("RESULT native import replacing admission %s case=%d\n",
        topology_result?"FAIL":"PASS",topology_result);
    if(topology_result){kernel_fini();return 20;}
    topology_result=oz_replacement_recovery_test();
    report("RESULT native unfinished replacement recovery %s engine-restart=1 errno=%d\n",
        topology_result?"FAIL":"PASS",topology_result);
    if(topology_result){kernel_fini();return 20;}
    int taskq_result=taskq_growth_test();
    report("Native dynamic taskq growth/concurrency/join tests=%d rounds=16 workers=8 jobs=16\n",taskq_result);
    if(taskq_result)return 20;
    taskq_result=taskq_failure_test();
    report("Native dynamic taskq optional-failure/recovery tests=%d refused=%u jobs=32\n",
        taskq_result,taskq_failed_spawns);
    if(taskq_result)return 20;
    int dio_result = oz_dio_test();
    report("Native direct-buffer descriptors/borrowed ABD tests=%d\n", dio_result);
    if (dio_result) { kernel_fini(); return 20; }
    int identity_result=oz_packet_cred_test();
    report("Native security packet identity conversion/capture=%d\n",identity_result);
    if(identity_result){kernel_fini();return 20;}
    /* This disposable diagnostic creates datasets directly, outside the
     * authenticated service dispatcher. Give only this fixture phase an
     * explicit credential scope; production must still reject absent CRED. */
    struct oz_cred_scope *fixture_scope=NULL;
    int fixture_error=oz_cred_push(kcred,&fixture_scope);
    if(fixture_error){kernel_fini();return 20;}
    /* Keep the historical fixture dataset name used by shared DOS tests;
     * topology is determined by the vdev config, never by that name. */
    report("spa_create layout=%s members=%u\n",layout,count);int rc=spa_create("qemu_mirror",root,props,NULL,NULL);
    report("spa_create=%d\n",rc);uint64_t object=0;
    if(!rc){rc=oz_async_config_suspend_test();report("Native suspended async config tests=%d\n",rc);}
    if(!rc){rc=data(&object,1);report("DMU write=%d object=%llu\n",rc,(unsigned long long)object);}
    if(!rc){rc=oz_zpl_fixture_create("qemu_mirror/files");report("ZPL fixture creation=%d\n",rc);}
    if(!rc){rc=cache_pressure_test();report("Native cache pressure files=320 readonly-passes=3 tests=%d\n",rc);}
    if(!rc){rc=oz_delegation_test();report("Native upstream dataset delegation policy=%d\n",rc);}
    if(!rc){rc=oz_atime_persistence_test(B_TRUE);report("Native SA atime flush/persistence=%d\n",rc);}
    if(!rc){rc=oz_write_persistence_test(B_TRUE);report("Native upstream vnode write/fsync/append=%d\n",rc);}
    if(!rc)rc=oz_mount_lifecycle_test(B_TRUE);
    if(!rc)rc=oz_dos_write_test(1);
    if(!rc)rc=oz_legacy_cold_test(B_TRUE);
    if(!rc)rc=oz_special_file_test(B_TRUE);
    if(!rc){rc=spa_export("qemu_mirror",&exported,B_FALSE,B_FALSE);report("export=%d\n",rc);}
    if(!rc && exported) {
        char *packed=NULL;size_t bytes=0;
        rc=nvlist_pack(exported,&packed,&bytes,NV_ENCODE_XDR,0);
        if(!rc && bytes>1024*1024)rc=EOVERFLOW;
        if(!rc) {
            report("EXPORT_XDR_BEGIN %llu\n",(unsigned long long)bytes);
            const char hex[]="0123456789abcdef";
            for(size_t start=0;start<bytes;start+=32) {
                char line[65];size_t count=bytes-start;if(count>32)count=32;
                for(size_t j=0;j<count;j++){unsigned char b=packed[start+j];line[j*2]=hex[b>>4];line[j*2+1]=hex[b&15];}
                line[count*2]=0;report("EXPORT_XDR %s\n",line);
            }
            report("EXPORT_XDR_END\n");
        }
        free(packed);
    }
    VERIFY0(oz_cred_pop(fixture_scope));
    kernel_fini();
    if(!rc && !exported)rc=EIO;
    if(!rc) {
        kernel_init(SPA_MODE_READ);
        nvlist_t *ip=fnvlist_alloc();fnvlist_add_string(ip,"cachefile","none");fnvlist_add_uint64(ip,"readonly",1);
        rc=spa_import("qemu_mirror",exported,ip,0);report("read-only import=%d\n",rc);
        if(!rc){rc=data(&object,0);report("DMU 16384-byte read/compare=%d\n",rc);}
        if(!rc) {
            struct oz_zpl *fs=NULL;uint64_t file=0,size=0;char buf[128]={0};
            rc=oz_zpl_open("qemu_mirror/files",&fs);report("ZPL reopen=%d\n",rc);
            if(!rc)rc=oz_zpl_lookup(fs,"hello.txt",&file,&size);
            if(!rc && size>=sizeof(buf))rc=EOVERFLOW;
            if(!rc)rc=oz_zpl_read(fs,file,0,buf,size);
            if(!rc && strcmp(buf,"OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n"))rc=EIO;
            report("ZPL file read/compare=%d bytes=%llu\n",rc,(unsigned long long)size);
            if(!rc) {
                rc=oz_dos_handler_test(fs);
                report("DOS handler Open/Read/Seek/Close repeated 3 times, missing file and readonly checks=%d\n",rc);
            }
            int close_rc=oz_zpl_close(fs);if(!rc)rc=close_rc;
        }
        fnvlist_free(ip);kernel_fini();
    }
    fnvlist_free(exported);fnvlist_free(root);fnvlist_free(mirror);fnvlist_free(props);
    for(unsigned i=0;i<count;i++)fnvlist_free(leaves[i]);
    for(unsigned i=0;i<count;i++)oz_device_unbind(bindings[i]);
    report("RESULT QEMU native %s %s %s\n",raidz==3?"four-device":raidz?"three-device":"two-device",layout,rc?"FAIL":"PASS");return rc?20:0;
}
