/* Private post-admission fault diagnostic; never linked into a release. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/dsl_pool.h>
#include <sys/txg.h>
#include <sys/zio.h>
#include <sys/zfs_file.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <proto/dos.h>
#include <stdio.h>
#include <proto/exec.h>
#include "zpl-fixture.h"
#include "device-io.h"
#include "partition-io.h"
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_removal.h>

static unsigned armed, calls, hits, real_calls, real_errors;
static unsigned target_failures, other_failures;
static zfs_file_t *failed_file;
int __real_zfs_file_fsync(zfs_file_t *, int);
int __real_oz_async_config_suspend_test(void);

static void arm_commit_fault(zfs_file_t *target)
{
    calls = hits = real_calls = real_errors = 0;
    __atomic_store_n(&failed_file, target, __ATOMIC_RELEASE);
#ifdef OZ_TEST_SINGLE_LEAF_FLUSH
    __atomic_store_n(&armed, 4, __ATOMIC_RELEASE);
#else
    __atomic_store_n(&armed, target ? 4 : 1, __ATOMIC_RELEASE);
#endif
}

static int disarm_commit_fault(const char *phase)
{
    __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
#ifdef OZ_TEST_SINGLE_LEAF_FLUSH
    unsigned failed = __atomic_load_n(&hits, __ATOMIC_ACQUIRE);
    unsigned healthy = __atomic_load_n(&real_calls, __ATOMIC_ACQUIRE);
    unsigned errors = __atomic_load_n(&real_errors, __ATOMIC_ACQUIRE);
    int error = failed && healthy && !errors ? 0 : EIO;
    char line[160];
    snprintf(line, sizeof(line), "DIAGNOSTIC persistent leaf %s failures=%u healthy=%u healthy-errors=%u error=%d\n",
        phase, failed, healthy, errors, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
    return error;
#else
    (void)phase;
    return 0;
#endif
}

#ifdef OZ_TEST_STRICT_FLUSH
static int transaction_flush_failure(spa_t *spa, const char *name,
    zfs_file_t *target_file)
{
    objset_t *os = NULL;
    int error = dmu_objset_create(name, DMU_OST_OTHER, 0, NULL, NULL, NULL);
    if (error) return error;
    error = dmu_objset_own(name, DMU_OST_OTHER, B_FALSE, B_FALSE, FTAG, &os);
    if (error) return error;
    txg_wait_synced(spa_get_dsl(spa), 0);
    dmu_tx_t *tx = dmu_tx_create(os);
    dmu_tx_hold_bonus(tx, DMU_NEW_OBJECT);
    dmu_tx_hold_write(tx, DMU_NEW_OBJECT, 0, sizeof(uint64_t));
    error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) { dmu_tx_abort(tx); dmu_objset_disown(os, B_FALSE, FTAG); return error; }
    uint64_t target = dmu_tx_get_txg(tx);
    uint64_t object = dmu_object_alloc(os, DMU_OT_PLAIN_FILE_CONTENTS,
        4096, DMU_OT_NONE, 0, tx);
    uint64_t expected = UINT64_C(0x51a20519fedcba98), actual = 0;
    arm_commit_fault(target_file);
    dmu_write(os, object, 0, sizeof(expected), &expected, tx, 0);
    dmu_tx_commit(tx);
    txg_wait_kick(spa_get_dsl(spa));
    unsigned polls = 0;
    while (!spa_suspended(spa) && polls++ < 1000) Delay(1);
    int suspended = spa_suspended(spa);
    uint64_t stopped = spa_last_synced_txg(spa);
    unsigned failures = __atomic_load_n(&hits, __ATOMIC_ACQUIRE);
    error = disarm_commit_fault("TXG");
    if (!suspended || stopped >= target || !failures) error = EIO;
    int resume = suspended ? zio_resume(spa) : 0;
    if (!error) error = resume;
    int synced = txg_wait_synced_flags(spa_get_dsl(spa), target, TXG_WAIT_SUSPEND);
    if (!error) error = synced;
    if (!error) error = dmu_read(os, object, 0, sizeof(actual), &actual, 0);
    if (!error && actual != expected) error = EIO;
    char line[256];
    snprintf(line, sizeof(line), "DIAGNOSTIC TXG flush suspended=%d failures=%u target=%llu stopped=%llu resume=%d synced=%d payload=%d error=%d\n",
        suspended, failures, (unsigned long long)target, (unsigned long long)stopped,
        resume, synced, actual == expected, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
    dmu_objset_disown(os, B_FALSE, FTAG);
    return error;
}
#endif

#ifdef OZ_TEST_ZIL_FLUSH
static int synchronous_flush_failure(spa_t *spa)
{
    struct oz_zpl *fs = NULL;
    struct oz_zpl_info info = {0};
    const char *name = "qemu_mirror/zilflush";
    int error = oz_zpl_fixture_create(name);
    if (!error) error = oz_zpl_open_writable(name, &fs);
    if (!error) error = oz_zpl_create_as(fs, oz_zpl_root(fs), "probe", 0, kcred, &info);
    if (error) { if (fs) oz_zpl_close(fs); return error; }
    txg_wait_synced(spa_get_dsl(spa), 0);
    uint64_t expected = UINT64_C(0x51a20519aabbccdd), actual = 0;
    size_t written = 0;
    arm_commit_fault(NULL);
    int failed = oz_zpl_write_as(fs, info.object, 0, &expected,
        sizeof(expected), &written, kcred);
    error = disarm_commit_fault("ZPL");
    unsigned failures = __atomic_load_n(&hits, __ATOMIC_ACQUIRE);
    int suspended = spa_suspended(spa);
    int resume = suspended ? zio_resume(spa) : 0;
    int synced = txg_wait_synced_flags(spa_get_dsl(spa), 0, TXG_WAIT_SUSPEND);
    if ((failed != EIO && failed != ESHUTDOWN) || !failures || !suspended || resume || synced)
        error = EIO;
    /* Do not treat the failed write's byte count as durable success. Retry
     * through the same mounted adapter after recovery and check its result. */
    int retried = oz_zpl_write_as(fs, info.object, 0, &expected,
        sizeof(expected), &written, kcred);
    if (!error && (retried || written != sizeof(expected))) error = EIO;
    if (!error) error = oz_zpl_read(fs, info.object, 0, &actual, sizeof(actual));
    if (!error && actual != expected) error = EIO;
    char line[256];
    snprintf(line, sizeof(line), "DIAGNOSTIC ZIL flush failed=%d failures=%u suspended=%d resume=%d synced=%d retry=%d payload=%d error=%d\n",
        failed, failures, suspended, resume, synced, retried, actual == expected, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
    int closed = oz_zpl_close(fs);
    return error ? error : closed;
}
#endif

int __wrap_zfs_file_fsync(zfs_file_t *file, int flags)
{
    unsigned mode = __atomic_load_n(&armed, __ATOMIC_ACQUIRE);
    if (mode) {
        unsigned call = __atomic_add_fetch(&calls, 1, __ATOMIC_RELAXED);
        if (mode == 4) {
            zfs_file_t *none = NULL;
            __atomic_compare_exchange_n(&failed_file, &none, file, 0,
                __ATOMIC_ACQ_REL, __ATOMIC_ACQUIRE);
        }
        if (mode == 1 || (mode == 2 && call == 1) ||
            (mode == 4 && file == __atomic_load_n(&failed_file, __ATOMIC_ACQUIRE))) {
            __atomic_add_fetch(&hits, 1, __ATOMIC_RELAXED);
            if(file==__atomic_load_n(&failed_file,__ATOMIC_ACQUIRE))
                __atomic_add_fetch(&target_failures,1,__ATOMIC_RELAXED);
            else
                __atomic_add_fetch(&other_failures,1,__ATOMIC_RELAXED);
            return EIO;
        }
        int error = __real_zfs_file_fsync(file, flags);
        __atomic_add_fetch(&real_calls, 1, __ATOMIC_RELAXED);
        if (error) __atomic_add_fetch(&real_errors, 1, __ATOMIC_RELAXED);
        return error;
    }
    return __real_zfs_file_fsync(file, flags);
}

#ifdef OZ_TEST_AUX_FLUSH
#ifdef OZ_TEST_LOG_FLUSH
extern uint64_t oz_test_zil_flush_fallbacks;
static int log_flush_failure(spa_t *spa, const char *path,
    struct oz_device_binding *binding)
{
    nvlist_t *root=fnvlist_alloc(),*leaf=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,path);
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_ASHIFT,12);
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_IS_LOG,1);
    const nvlist_t *leaves[]={leaf};
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,leaves,1);
    int error=spa_vdev_add(spa,root,B_FALSE);
    fnvlist_free(root);fnvlist_free(leaf);
    if(error){oz_device_unbind(binding);return error;}
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    vdev_t *log=NULL;
    for(uint64_t i=0;i<spa->spa_root_vdev->vdev_children;i++) {
        vdev_t *vd=spa->spa_root_vdev->vdev_child[i];
        if(vd->vdev_islog&&vd->vdev_path&&!strcmp(vd->vdev_path,path))log=vd;
    }
    vdev_file_t *vf=log?log->vdev_tsd:NULL;
    uint64_t guid=log?log->vdev_guid:0;
    zfs_file_t *file=vf?vf->vf_file:NULL;
    if(!file||log->vdev_aux)error=EIO;
    spa_config_exit(spa,SCL_ALL,FTAG);
    struct oz_zpl *fs=NULL;
    struct oz_zpl_info info={0};
    if(!error)error=oz_zpl_fixture_create("qemu_mirror/slogflush");
    if(!error)error=oz_zpl_open_writable("qemu_mirror/slogflush",&fs);
    if(!error)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"probe",0,kcred,&info);
    txg_wait_synced(spa_get_dsl(spa),0);
    uint64_t expected=UINT64_C(0x51a20519ccddeeff),actual=0;
    size_t written=0;
    int result=error;
    uint64_t fallback_before=__atomic_load_n(&oz_test_zil_flush_fallbacks,__ATOMIC_ACQUIRE);
    if(!error) {
        calls=hits=real_calls=real_errors=0;
        target_failures=other_failures=0;
        __atomic_store_n(&failed_file,file,__ATOMIC_RELEASE);
#ifdef OZ_TEST_LOG_DATA_FLUSH
        __atomic_store_n(&armed,1,__ATOMIC_RELEASE);
#else
        __atomic_store_n(&armed,4,__ATOMIC_RELEASE);
#endif
        result=oz_zpl_write_as(fs,info.object,0,&expected,sizeof(expected),&written,kcred);
        __atomic_store_n(&armed,0,__ATOMIC_RELEASE);
    }
    unsigned failures=__atomic_load_n(&hits,__ATOMIC_ACQUIRE);
    unsigned healthy=__atomic_load_n(&real_calls,__ATOMIC_ACQUIRE);
    unsigned healthy_errors=__atomic_load_n(&real_errors,__ATOMIC_ACQUIRE);
    uint64_t fallbacks=__atomic_load_n(&oz_test_zil_flush_fallbacks,__ATOMIC_ACQUIRE)-fallback_before;
    int suspended=spa_suspended(spa);
    int resumed=suspended?zio_resume(spa):0;
    int synced=txg_wait_synced_flags(spa_get_dsl(spa),0,TXG_WAIT_SUSPEND);
#ifdef OZ_TEST_LOG_DATA_FLUSH
    unsigned log_failures=__atomic_load_n(&target_failures,__ATOMIC_ACQUIRE);
    unsigned data_failures=__atomic_load_n(&other_failures,__ATOMIC_ACQUIRE);
    if(!error&&((result!=EIO&&result!=ESHUTDOWN)||!log_failures||!data_failures||
        !suspended||resumed||synced||!fallbacks))error=EIO;
    int retry=fs?oz_zpl_write_as(fs,info.object,0,&expected,sizeof(expected),&written,kcred):EIO;
    if(!error&&(retry||written!=sizeof(expected)))error=EIO;
    char combined[200];
    snprintf(combined,sizeof(combined),"DIAGNOSTIC combined log-data result=%d log-failures=%u data-failures=%u suspended=%d retry=%d error=%d\n",
        result,log_failures,data_failures,suspended,retry,error);
    for(char *p=combined;*p;p++)RawPutChar(*p);
#else
    if(!error&&(result||written!=sizeof(expected)||!failures||!healthy||
        healthy_errors||suspended||resumed||synced||!fallbacks))error=EIO;
#endif
    if(!error)error=oz_zpl_read(fs,info.object,0,&actual,sizeof(actual));
    if(!error&&actual!=expected)error=EIO;
    if(fs){int closed=oz_zpl_close(fs);if(!error)error=closed;}
    int removed=guid?spa_vdev_remove(spa,guid,B_FALSE):EIO;
    if(!removed) {
        txg_wait_synced(spa_get_dsl(spa),0);
        spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
        if(spa_lookup_by_guid(spa,guid,B_TRUE))removed=EBUSY;
        spa_config_exit(spa,SCL_ALL,FTAG);
    }
    if(!removed)oz_device_unbind(binding);
    if(!error)error=removed;
    char line[256];
    snprintf(line,sizeof(line),"DIAGNOSTIC log flush result=%d failures=%u healthy=%u healthy-errors=%u suspended=%d resume=%d sync=%d payload=%d remove=%d fallbacks=%llu error=%d\n",
        result,failures,healthy,healthy_errors,suspended,resumed,synced,actual==expected,removed,(unsigned long long)fallbacks,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
#endif
#ifdef OZ_TEST_ACTIVE_SPARE
static int active_spare_flush(spa_t *spa, const char *path, uint64_t guid)
{
    spa_config_enter(spa,SCL_ALL,FTAG,RW_READER);
    vdev_t *old=spa->spa_root_vdev;
    while(old->vdev_children)old=old->vdev_child[0];
    uint64_t old_guid=old->vdev_guid;
    spa_config_exit(spa,SCL_ALL,FTAG);
    nvlist_t *root=fnvlist_alloc(),*leaf=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,path);
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_ASHIFT,12);
    const nvlist_t *leaves[]={leaf};
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,leaves,1);
    int error=spa_vdev_attach(spa,old_guid,root,1,0);
    fnvlist_free(root);fnvlist_free(leaf);
    if(error)return error;
    txg_wait_synced(spa_get_dsl(spa),0);
    spa_config_enter(spa,SCL_STATE_ALL,FTAG,RW_READER);
    vdev_t *active=spa_lookup_by_guid(spa,guid,B_FALSE);
    vdev_file_t *vf=active?active->vdev_tsd:NULL;
    zfs_file_t *active_file=vf?vf->vf_file:NULL;
    int verified=active&&active->vdev_isspare&&!active->vdev_aux&&vf&&vf->vf_file;
    int observed=0,recovered=0;
    unsigned failures=0;
    if(verified) {
        calls=hits=real_calls=real_errors=0;
        __atomic_store_n(&failed_file,vf->vf_file,__ATOMIC_RELEASE);
        __atomic_store_n(&armed,4,__ATOMIC_RELEASE);
        zio_t *io=zio_root(spa,NULL,NULL,ZIO_FLAG_CANFAIL);
        zio_flush(io,active);
        observed=zio_wait(io);
        __atomic_store_n(&armed,0,__ATOMIC_RELEASE);
        failures=__atomic_load_n(&hits,__ATOMIC_ACQUIRE);
        io=zio_root(spa,NULL,NULL,ZIO_FLAG_CANFAIL);
        zio_flush(io,active);
        recovered=zio_wait(io);
    }
    spa_config_exit(spa,SCL_STATE_ALL,FTAG);
    int transaction=verified&&observed==EIO&&!recovered?
        transaction_flush_failure(spa,"qemu_mirror/activeflush",active_file):EIO;
    /* Cancel this temporary replacement, preserving the original mirror. */
    int detached=spa_vdev_detach(spa,guid,0,B_FALSE);
    if(!verified||observed!=EIO||!failures||recovered||detached||transaction)error=EIO;
    char line[200];
    snprintf(line,sizeof(line),"DIAGNOSTIC active spare verified=%d failures=%u parent=%d recovery=%d detach=%d txg=%d error=%d\n",
        verified,failures,observed,recovered,detached,transaction,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
#endif

static int auxiliary_flush_failure(spa_t *spa)
{
#ifndef OZ_TEST_LOG_FLUSH
#ifdef OZ_TEST_AUX_SPARE
    const char *kind="spare",*config_key=ZPOOL_CONFIG_SPARES;
    spa_aux_vdev_t *sav=&spa->spa_spares;
#else
    const char *kind="cache",*config_key=ZPOOL_CONFIG_L2CACHE;
    spa_aux_vdev_t *sav=&spa->spa_l2cache;
#endif
#endif
    const char *path="/AROS/ata.device/3/partition/0";
    struct oz_device *device=NULL;
    struct oz_device_binding *binding=NULL;
    int error=oz_device_open("ata.device",3,0,&device);
    if(!error&&oz_device_size(device)!=512ULL*1024*1024)error=EINVAL;
    oz_device_close(device);device=NULL;
    if(!error)error=oz_device_open_partition("ata.device",3,0,1,&device);
    if(!error&&oz_device_size(device)!=256ULL*1024*1024)error=EINVAL;
    if(!error)error=oz_device_bind_writable(path,device,&binding);
    oz_device_close(device);
    if(error)return error;
#ifdef OZ_TEST_LOG_FLUSH
    return log_flush_failure(spa,path,binding);
#else
    nvlist_t *root=fnvlist_alloc(),*leaf=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,NULL,0);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,path);
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_ASHIFT,12);
    const nvlist_t *leaves[]={leaf};
    fnvlist_add_nvlist_array(root,config_key,leaves,1);
    error=spa_vdev_add(spa,root,B_FALSE);
    fnvlist_free(root);fnvlist_free(leaf);
    if(error){oz_device_unbind(binding);return error;}
    txg_wait_synced(spa_get_dsl(spa),0);
    spa_config_enter(spa,SCL_ALL,FTAG,RW_WRITER);
    vdev_t *aux=sav->sav_count==1?sav->sav_vdevs[0]:NULL;
    vdev_file_t *vf=aux?aux->vdev_tsd:NULL;
    uint64_t guid=aux?aux->vdev_guid:0;
    uint64_t before=spa_last_synced_txg(spa);
    if(!vf||!vf->vf_file||aux->vdev_aux!=sav||
        spa_lookup_by_guid(spa,guid,B_FALSE)!=NULL)error=EIO;
    if(!error) {
        calls=hits=real_calls=real_errors=0;
        __atomic_store_n(&failed_file,vf->vf_file,__ATOMIC_RELEASE);
        __atomic_store_n(&armed,4,__ATOMIC_RELEASE);
        sav->sav_label_sync=B_TRUE;
        spa->spa_aux_sync_uber=B_TRUE;
        vdev_config_dirty(spa->spa_root_vdev);
    }
    spa_config_exit(spa,SCL_ALL,FTAG);
    if(!error) {
        txg_wait_kick(spa_get_dsl(spa));
        unsigned polls=0;
        while(!spa_suspended(spa)&&spa_last_synced_txg(spa)<=before&&polls++<1000)Delay(1);
    }
    uint64_t fault_synced=spa_last_synced_txg(spa);
    __atomic_store_n(&armed,0,__ATOMIC_RELEASE);
    unsigned failures=__atomic_load_n(&hits,__ATOMIC_ACQUIRE);
    unsigned healthy=__atomic_load_n(&real_calls,__ATOMIC_ACQUIRE);
    unsigned healthy_errors=__atomic_load_n(&real_errors,__ATOMIC_ACQUIRE);
    int suspended=spa_suspended(spa);
    int resumed=suspended?zio_resume(spa):0;
    int synced=txg_wait_synced_flags(spa_get_dsl(spa),0,TXG_WAIT_SUSPEND);
    /* Inactive auxiliary errors must not suspend healthy data devices.
     * Require an actual TXG advance while the fault was still armed. */
    if(!error&&(!failures||!healthy||healthy_errors||suspended||resumed||synced||
        fault_synced<=before))error=EIO;
#ifdef OZ_TEST_ACTIVE_SPARE
    if(!error)error=active_spare_flush(spa,path,guid);
#endif
    int removed=guid?spa_vdev_remove(spa,guid,B_FALSE):EIO;
    if(!error)error=removed;
    if(!removed)oz_device_unbind(binding);
    char line[256];
    snprintf(line,sizeof(line),"DIAGNOSTIC auxiliary %s failures=%u healthy=%u healthy-errors=%u suspended=%d resume=%d sync=%d remove=%d error=%d\n",
        kind,failures,healthy,healthy_errors,suspended,resumed,synced,removed,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#endif
}
#endif

int __wrap_oz_async_config_suspend_test(void)
{
    int error = __real_oz_async_config_suspend_test();
    if (error) return error;
    spa_t *spa = NULL;
    error = spa_open("qemu_mirror", &spa, FTAG);
    if (error) return error;
    txg_wait_synced(spa_get_dsl(spa), 0);
    spa_config_enter(spa, SCL_STATE_ALL, FTAG, RW_READER);
    zio_t *root = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
    __atomic_store_n(&armed, 1, __ATOMIC_RELEASE);
    zio_flush(root, spa->spa_root_vdev);
    int observed = zio_wait(root);
    __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
    unsigned count = __atomic_load_n(&hits, __ATOMIC_ACQUIRE);
    calls = hits = real_calls = real_errors = 0;
    root = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
    __atomic_store_n(&armed, 2, __ATOMIC_RELEASE);
    zio_flush(root, spa->spa_root_vdev);
    int single = zio_wait(root);
    __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
    unsigned single_hits = hits, single_real = real_calls;
    unsigned single_errors = real_errors;
    calls = hits = real_calls = real_errors = 0;
    root = zio_root(spa, NULL, NULL, ZIO_FLAG_CANFAIL);
    __atomic_store_n(&armed, 3, __ATOMIC_RELEASE);
    zio_flush(root, spa->spa_root_vdev);
    int recovered = zio_wait(root);
    __atomic_store_n(&armed, 0, __ATOMIC_RELEASE);
    spa_config_exit(spa, SCL_STATE_ALL, FTAG);
    char line[160];
    snprintf(line, sizeof(line), "DIAGNOSTIC runtime flush injected=EIO hits=%u parent=%d recovery=%d\n",
        count, observed, recovered);
    for (char *p = line; *p; p++) RawPutChar(*p);
    snprintf(line, sizeof(line), "DIAGNOSTIC runtime flush single hits=%u real=%u errors=%u parent=%d recovery-leaves=%u recovery-errors=%u\n",
        single_hits, single_real, single_errors, single, real_calls, real_errors);
    for (char *p = line; *p; p++) RawPutChar(*p);
    /* Reproduce the current upstream DONT_PROPAGATE behavior, not a
     * durability acceptance. A zero parent error is the defect under test. */
#ifdef OZ_TEST_STRICT_FLUSH
    error = count >= 2 && observed == EIO && single == EIO && recovered == 0 ? 0 : EIO;
#else
    error = count >= 2 && observed == 0 && single == 0 && recovered == 0 ? 0 : EIO;
#endif
    if (single_hits != 1 || single_real < 1 || single_errors ||
        real_calls < 2 || real_errors) error = EIO;
#ifdef OZ_TEST_STRICT_FLUSH
    if (!error) error = transaction_flush_failure(spa,"qemu_mirror/flushprobe",NULL);
#endif
#ifdef OZ_TEST_ZIL_FLUSH
    if (!error) error = synchronous_flush_failure(spa);
#endif
#ifdef OZ_TEST_AUX_FLUSH
    if (!error) error = auxiliary_flush_failure(spa);
#endif
    spa_close(spa, FTAG);
    return error;
}
