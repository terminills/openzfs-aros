/* SPDX-License-Identifier: CDDL-1.0 */
/* Test-only import prelude, followed by the unmodified production event loop.
 * This does not add native-device import privileges to the public API. */
#define OZ_SERVICE_TEST_MODES 1
#define main oz_service_main
#include "../ui/service.c"
#undef main
#include <sys/spa.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <sys/dsl_deleg.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_dataset.h>
#include <zfs_deleg.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <sys/zfeature.h>
int oz_zpl_dataset_create(const char *,cred_t *);
int oz_user_property_change(struct oz_message *,cred_t *);
#include "count-race-test.h"
int oz_timestamp_policy_test(int);
int oz_exec_policy_test(int);
int oz_encryption_test(int);
int oz_vdev_policy_test(void);
int oz_pool_clear_test(int);
int oz_pool_initialize_test(int,int);
int oz_pool_trim_test(int);
int oz_resilver_test(int);
int oz_send_receive_test(int);
int oz_advanced_stream_test(int);
int oz_bookmark_stream_test(int);
int oz_resumable_stream_test(int);
int oz_cold_resume_test(void);
int oz_incremental_resume_test(int);
int oz_replication_perms_test(int);
int oz_dos_stream_policy_test(int);
int oz_packet_delegated_create_test(const char *,int);
static void fixture_stage(const char *stage)
{
    const char *prefix="Admission fixture stage: ";
    while(*prefix)RawPutChar(*prefix++);
    while(*stage)RawPutChar(*stage++);
    RawPutChar('\n');
}
static int delegated_fixture(void)
{
    /* Explicit opt-in on disposable copied disks only. Production bookmark
     * requests must never silently upgrade a pool's on-disk feature set. */
    spa_t *spa=NULL;
    int error=spa_open("qemu_mirror",&spa,FTAG);
    if(error)return error;
    nvlist_t *features=fnvlist_alloc();
    fnvlist_add_uint64(features,"feature@bookmarks",0);
    error=spa_prop_set(spa,features);
    fnvlist_free(features);spa_close(spa,FTAG);
    if(error)return error;
    const char *dataset="qemu_mirror/delegated-root";
    error=oz_zpl_dataset_create(dataset,kcred);
    if(error)return error;
    /* Exercise the production setter against disabled features first. Only
     * this private fixture then explicitly enables them for positive tests. */
    error=spa_open("qemu_mirror",&spa,FTAG);
    if(error)return error;
    int disabled=!spa_feature_is_enabled(spa,SPA_FEATURE_LZ4_COMPRESS)&&
        !spa_feature_is_enabled(spa,SPA_FEATURE_ZSTD_COMPRESS)&&
        !spa_feature_is_enabled(spa,SPA_FEATURE_FS_SS_LIMIT)&&
        !spa_feature_is_enabled(spa,SPA_FEATURE_LARGE_BLOCKS);
    spa_close(spa,FTAG);
    if(!disabled)return EINVAL;
    struct oz_message *request=calloc(1,sizeof(*request));
    if(!request)return ENOMEM;
    request->operation=41;strcpy(request->target,dataset);
    strcpy(request->argument,"compression=lz4");
    error=oz_user_property_change(request,kcred)==ENOTSUP?0:EIO;
    strcpy(request->argument,"compression=zstd");
    if(!error)error=oz_user_property_change(request,kcred)==ENOTSUP?0:EIO;
    strcpy(request->argument,"filesystem_limit=0");
    if(!error)error=oz_user_property_change(request,kcred)==ENOTSUP?0:EIO;
    strcpy(request->argument,"snapshot_limit=0");
    if(!error)error=oz_user_property_change(request,kcred)==ENOTSUP?0:EIO;
    strcpy(request->argument,"recordsize=1M");
    if(!error)error=oz_user_property_change(request,kcred)==ENOTSUP?0:EIO;
    free(request);if(error)return error;
    const char *notice="Native compression disabled feature refusal PASS\n";
    while(*notice)RawPutChar(*notice++);
    notice="Native count-limit disabled feature refusal PASS\n";
    while(*notice)RawPutChar(*notice++);
    error=spa_open("qemu_mirror",&spa,FTAG);
    if(error)return error;
    features=fnvlist_alloc();
    fnvlist_add_uint64(features,"feature@lz4_compress",0);
    fnvlist_add_uint64(features,"feature@zstd_compress",0);
    fnvlist_add_uint64(features,"feature@filesystem_limits",0);
    fnvlist_add_uint64(features,"feature@large_blocks",0);
    error=spa_prop_set(spa,features);fnvlist_free(features);spa_close(spa,FTAG);
    if(error)return error;
    nvlist_t *acl=fnvlist_alloc(),*permissions=fnvlist_alloc();
    const char *names[]={"create","mount","snapshot","rename","destroy","userprop","clone","hold","release","rollback","promote","bookmark","quota","refquota","reservation","refreservation","compression","recordsize","readonly","atime","relatime"};
    for(unsigned i=0;i<sizeof(names)/sizeof(names[0]);i++)
        fnvlist_add_boolean(permissions,names[i]);
    fnvlist_add_nvlist(acl,"ul$77",permissions);
    fnvlist_add_nvlist(acl,"ud$77",permissions);
    error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(dataset,acl,B_FALSE);
    fnvlist_free(permissions);fnvlist_free(acl);
    /* Independent grant: HOLD must never imply RELEASE. This fixture is
     * outside delegated-root so its descendant grant cannot mask the test. */
    if(error)return error;
    dataset="qemu_mirror/hold-only";
    error=oz_zpl_dataset_create(dataset,kcred);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(kcred,&scope);
    if(error)return error;
    error=dmu_objset_snapshot_one(dataset,"test");
    VERIFY0(oz_cred_pop(scope));
    if(error)return error;
    acl=fnvlist_alloc();permissions=fnvlist_alloc();
    fnvlist_add_boolean(permissions,"hold");
    fnvlist_add_boolean(permissions,"bookmark");
    fnvlist_add_boolean(permissions,"userprop");
    fnvlist_add_nvlist(acl,"ul$77",permissions);
    error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(dataset,acl,B_FALSE);
    fnvlist_free(permissions);fnvlist_free(acl);
    if(!error) {
        error=oz_cred_push(kcred,&scope);
        if(!error) {
            error=dsl_dataset_clone("qemu_mirror/delegated-root/denied-promote","qemu_mirror/hold-only@test");
            VERIFY0(oz_cred_pop(scope));
        }
    }
    if(error)return error;
    dataset="qemu_mirror/count-limited";
    error=oz_zpl_dataset_create(dataset,kcred);
    if(error)return error;
    acl=fnvlist_alloc();permissions=fnvlist_alloc();
    const char *restricted[]={"create","mount","snapshot","destroy"};
    for(unsigned i=0;i<sizeof(restricted)/sizeof(*restricted);i++)
        fnvlist_add_boolean(permissions,restricted[i]);
    fnvlist_add_nvlist(acl,"ul$77",permissions);
    fnvlist_add_nvlist(acl,"ud$77",permissions);
    error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(dataset,acl,B_FALSE);
    fnvlist_free(permissions);fnvlist_free(acl);
    return error;
}
int main(int argc,char **argv)
{
    if(argc!=2||(strcmp(argv[1],"WRITE")&&strcmp(argv[1],"VERIFY")&&strcmp(argv[1],"COLD")&&strcmp(argv[1],"WRITE-CANCEL")&&strcmp(argv[1],"COLD-CANCEL")&&strcmp(argv[1],"WRITE-INITIALIZE")&&strcmp(argv[1],"VERIFY-INITIALIZE")&&strcmp(argv[1],"RESUME-INITIALIZE")&&strcmp(argv[1],"RESUME-REPLICATION")&&strcmp(argv[1],"CREATE")))return 20;
    int create_only=!strcmp(argv[1],"CREATE");
    int replication_resume=!strcmp(argv[1],"RESUME-REPLICATION");
    int initialize_controls=!strcmp(argv[1],"WRITE-INITIALIZE");
    int initialize_resume=!strcmp(argv[1],"RESUME-INITIALIZE");
    int cancel_test=!strcmp(argv[1],"WRITE-CANCEL")||!strcmp(argv[1],"COLD-CANCEL");
    int write=!strcmp(argv[1],"WRITE")||cancel_test||initialize_controls||initialize_resume||replication_resume||create_only;
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(!marker)return 20;
    fclose(marker);
    if(cancel_test) {
        /* Upstream's test-only progress pause makes cancellation deterministic.
         * Confined to this marked disposable harness; no production API knob. */
        extern int zfs_removal_suspend_progress;
        zfs_removal_suspend_progress=1;
        fixture_stage("test-only removal progress pause enabled");
    }
    /* This marked, isolated test fixture has an explicit empty-password root
     * account. File vdev ZIO tasks inherit native filesystem authority from
     * the daemon; POSIX engine credentials do not bypass MuFS backing-file ACLs.
     * Production service launch/authentication must be supplied by the OS. */
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem login_tags[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(secLoginA(login_tags)!=secOWNER_SYSTEM){CloseLibrary(secBase);return 20;}
    if(!strcmp(argv[1],"COLD")||!strcmp(argv[1],"COLD-CANCEL")) {
        /* Fresh engine, no saved config or imported baseline. The separate
         * authenticated client must discover the persistent file labels. */
        if(setenv("ZFS_HOSTID","0x51a20519",1))return 20;
        /* Upstream spa_export requires a write-capable engine even for an
         * explicitly read-only pool. Pool readonly=on still prevents writes;
         * the runner independently compares both member files afterward. */
        char *cold_args[]={"OpenZFSServiceNative","TEST-WRITE",NULL};
        return oz_service_main(2,cold_args);
    }
    int fd=open("/SYS/export.nvlist",O_RDONLY);if(fd<0)return 20;
    struct stat st;int error=fstat(fd,&st);
    if(error||st.st_size<=0||st.st_size>1024*1024){close(fd);return 20;}
    char *bytes=malloc(st.st_size);if(!bytes){close(fd);return 20;}
    if(read(fd,bytes,st.st_size)!=st.st_size){close(fd);free(bytes);return 20;}
    close(fd);nvlist_t *config=NULL;
    error=nvlist_unpack(bytes,st.st_size,&config,0);free(bytes);if(error)return 20;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1))return 20;
    fixture_stage("engine start");
    if(!oz_engine_start(1))return 20;
    if(oz_vdev_policy_test())return 20;
    fixture_stage("engine ready; importing baseline");
    nvlist_t *props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"readonly",!write);
    error=spa_import("qemu_mirror",config,props,0);
    fixture_stage("baseline import returned");
    nvlist_free(config);nvlist_free(props);
    if(error){oz_engine_stop();return 20;}
    if(create_only) {
        /* Keep the shared client's pool-control prerequisites, but do not make
         * replication, timestamp or exec-policy setup a prerequisite for the
         * pool-creation and mount-lifetime test it actually owns. */
        if((error=oz_pool_clear_test(1))!=0)return 20;
        if((error=oz_pool_trim_test(1))!=0)return 20;
        if((error=oz_resilver_test(1))!=0)return 20;
        fixture_stage("create-only service event loop");
        char *service_args[]={"OpenZFSServiceNative","TEST-WRITE",NULL};
        return oz_service_main(2,service_args);
    }
    if(replication_resume){
        error=oz_cold_resume_test();
        if(!error)error=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
        if(!error&&!oz_engine_stop())error=EBUSY;
        const char *result=error?"RESULT native replication resume FAIL\n":"RESULT native replication resume PASS\n";
        while(*result)RawPutChar(*result++);
        return error?20:0;
    }
    /* Full CREATE dispatch checks pool writability before delegation. Both
     * boots must refuse this retained reader request; the writable boot
     * specifically proves denial of authority after the sender has exited. */
    if(oz_packet_delegated_create_test("qemu_mirror/files/native-authority-denied",
        write?EPERM:EROFS))return 20;
    if(initialize_resume) {
        error=oz_pool_initialize_test(1,2);
        if(!error)error=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
        if(!error&&!oz_engine_stop())error=EBUSY;
        const char *result=error?"RESULT native initialization resume FAIL\n":"RESULT native initialization resume PASS\n";
        while(*result)RawPutChar(*result++);
        return error?20:0;
    }
    if((error=oz_pool_clear_test(write))!=0)return 20;
    if((error=oz_pool_trim_test(write))!=0)return 20;
    if((error=oz_resilver_test(write))!=0)return 20;
    /* Focused initialization fixtures, including older saved RO GUI sources,
     * do not promise replication datasets. Keep their test contract separate. */
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_send_receive_test(write))!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_replication_perms_test(write))!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_dos_stream_policy_test(write))!=0)return 20;
    if((error=oz_pool_initialize_test(write,initialize_controls||!strcmp(argv[1],"VERIFY-INITIALIZE")))!=0)return 20;
    fixture_stage("delegated fixture");
    if(write&&(error=delegated_fixture())!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_advanced_stream_test(write))!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_bookmark_stream_test(write))!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_resumable_stream_test(write))!=0)return 20;
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_incremental_resume_test(write))!=0)return 20;
    fixture_stage("count race test");
    if(!initialize_controls&&strcmp(argv[1],"VERIFY-INITIALIZE")&&
        (error=oz_encryption_test(write))!=0)return 20;
    if(write&&(error=count_race_test())!=0)return 20;
    fixture_stage("timestamp policy test");
    /* The full timestamp cold fixture depends on its client's write phase.
     * Focused exec clients deliberately do not perform those mutations. */
    if(!getenv("OPENZFS_EXEC_ONLY")&&(error=oz_timestamp_policy_test(write))!=0)return 20;
    fixture_stage("execution property test");
    if((error=oz_exec_policy_test(write))!=0)return 20;
    fixture_stage("service event loop");
    char *service_args[]={"OpenZFSServiceNative","TEST-WRITE",NULL};
    return oz_service_main(2,service_args);
}
