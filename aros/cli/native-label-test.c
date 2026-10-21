/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/avl.h>
#include <libzutil.h>
#include "native-label.h"
#include "../ui/partition-api.h"
#include "../../lib/libzutil/zutil_import.h"
#include "../../lib/libzfs/libzfs_impl.h"
#include <aros/debug.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <dos/filehandler.h>
#include "../../cmd/zpool/zpool_util.h"
#define CHECK(x) do {if(!(x)){bug("Native label failure line=%d errno=%d\n",__LINE__,errno);return EIO;}}while(0)
static int preflight_with_dos_claim(const char *path,int unit,boolean_t whole)
{
    int claim=oz_device_dos_claim_read(OZ_ADMISSION_PORT,"ata.device",unit);
    if(claim&&claim!=EBUSY)return 0;
    errno=0;int rc=check_device(path,B_TRUE,B_FALSE,whole);
    return claim==EBUSY?(rc==-1&&errno==EBUSY):rc==0;
}
static int forced_dos_claim_test(void)
{
    struct DosList *entry=MakeDosEntry((CONST_STRPTR)"OpenZFSPreflightClaim",DLT_DEVICE);
    if(!entry)return ENOMEM;
    struct FileSysStartupMsg startup={0};struct DosEnvec env={0};
    env.de_TableSize=DE_DOSTYPE;env.de_DosType=0x444f5301;
    startup.fssm_Device=AROS_CONST_BSTR("ata.device");startup.fssm_Unit=0;
    startup.fssm_Environ=MKBADDR(&env);
    entry->dol_misc.dol_handler.dol_Startup=MKBADDR(&startup);
    if(!AddDosEntry(entry)){FreeDosEntry(entry);return EIO;}
    int valid=oz_device_dos_claim_read(OZ_ADMISSION_PORT,"ata.device",0)==EBUSY;
    for(unsigned force=0;force<2;force++) {
        errno=0;int rc=check_device("/AROS/ata.device/0",force,B_FALSE,B_TRUE);
        valid=valid&&rc==-1&&errno==EBUSY;
        errno=0;rc=check_device("/AROS/ata.device/0/partition/0",force,B_FALSE,B_FALSE);
        valid=valid&&rc==-1&&errno==EBUSY;
    }
    RemDosEntry(entry);FreeDosEntry(entry);
    if(valid)bug("Public native DOS preflight PASS inactive=1 whole=1 partition=1 force-denied=1\n");
    return valid?0:EIO;
}
static int retained_device_test(int denied)
{
    struct oz_ticket ticket={0};LONG fd=-1,control=-1;
    struct oz_device_geometry geometry={0};
    unsigned char first[512],second[512];int error=0;
    CHECK(!oz_session_open(OZ_ADMISSION_PORT,&ticket));
#define HANDLE_CHECK(x) do {if(!(x)){bug("Native retained handle FAIL line=%d\n",__LINE__);error=EIO;goto done;}}while(0)
    int opened=oz_device_handle_open(OZ_ADMISSION_PORT,&ticket,"ata.device",1,&fd,&geometry);
    if(denied){HANDLE_CHECK(opened==EACCES&&fd==-1);goto done;}
    HANDLE_CHECK(!opened&&fd>0&&geometry.bytes==256ULL*1024*1024&&geometry.sector_size==512);
    HANDLE_CHECK(!oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,fd,0,first,sizeof(first)));
    HANDLE_CHECK(!oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,fd,0,second,sizeof(second)));
    HANDLE_CHECK(!memcmp(first,second,sizeof(first)));
    memset(second,0xa5,sizeof(second));
    HANDLE_CHECK(oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,fd,geometry.bytes,second,sizeof(second))==EINVAL);
    for(unsigned i=0;i<sizeof(second);i++)HANDLE_CHECK(second[i]==0xa5);
    HANDLE_CHECK(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&control));
    HANDLE_CHECK(oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,control,0,second,sizeof(second))==EBADF);
    HANDLE_CHECK(!oz_control_close(OZ_ADMISSION_PORT,&ticket,fd));
    HANDLE_CHECK(oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,fd,0,second,sizeof(second))==EBADF);fd=-1;
    HANDLE_CHECK(!oz_device_handle_open(OZ_ADMISSION_PORT,&ticket,"ata.device",1,&fd,&geometry));
    HANDLE_CHECK(!oz_session_close(OZ_ADMISSION_PORT,&ticket));
    HANDLE_CHECK(oz_device_handle_read(OZ_ADMISSION_PORT,&ticket,fd,0,second,sizeof(second))==ENOENT);
    fd=control=-1;
done:
    if(control>0)oz_control_close(OZ_ADMISSION_PORT,&ticket,control);
    if(fd>0)oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);
    int closed=oz_session_close(OZ_ADMISSION_PORT,&ticket);
    if(!error&&closed&&closed!=ENOENT)error=closed;
    if(!error)bug("Public native retained device PASS denied=%d lifetime=1\n",denied);
    return error;
#undef HANDLE_CHECK
}
static int native_policy_test(const char *path)
{
    libzfs_handle_t *hdl=libzfs_init();
    if(!hdl)return errno?errno:EIO;
    libzfs_handle_t *saved_global=g_zfs;g_zfs=hdl;
    pool_state_t state=POOL_STATE_UNINITIALIZED;
    char *name=NULL;boolean_t inuse=B_FALSE;
    int error=oz_native_pool_in_use(hdl,path,&state,&name,&inuse);
    int valid=!error&&inuse&&state==POOL_STATE_EXPORTED&&name&&!strcmp(name,"qemu_mirror");
    if(!valid)bug("Native policy phase=exported path=%s error=%d state=%d inuse=%d name=%s\n",
        path,error,state,inuse,name?name:"(none)");
    free(name);name=NULL;
    /* Pool-membership policy still independently exercises force. Native
     * write preflight additionally rejects a currently mounted DOS unit. */
    valid=valid&&check_file_generic(path,B_FALSE,B_FALSE)==-1&&
        check_file_generic(path,B_TRUE,B_FALSE)==0;
    valid=valid&&preflight_with_dos_claim(path,3,B_FALSE);
    valid=valid&&check_device("/AROS/ata.device/3",B_FALSE,B_FALSE,B_TRUE)==-1&&
        preflight_with_dos_claim("/AROS/ata.device/3",3,B_TRUE);
    int saved=hdl->libzfs_fd;
    hdl->libzfs_fd=-1;
    error=oz_native_pool_in_use(hdl,path,&state,&name,&inuse);
    int failed_query=check_device(path,B_TRUE,B_FALSE,B_FALSE);
    hdl->libzfs_fd=saved;
    if(!error||inuse||name)bug("Native policy phase=broken-query path=%s error=%d inuse=%d name=%s\n",
        path,error,inuse,name?name:"(none)");
    valid=valid&&error&&!inuse&&!name&&failed_query==-1;
    free(name);name=NULL;
    error=oz_native_pool_in_use(hdl,"/AROS/ata.device/1",&state,&name,&inuse);
    if(error||inuse||name)bug("Native policy phase=blank error=%d inuse=%d name=%s\n",
        error,inuse,name?name:"(none)");
    valid=valid&&!error&&!inuse&&!name;
    free(name);name=NULL;
    error=oz_native_pool_in_use(hdl,"/AROS/openzfs-absent.device/0",&state,&name,&inuse);
    if(error!=ENODEV||inuse||name)bug("Native policy phase=missing error=%d inuse=%d name=%s\n",
        error,inuse,name?name:"(none)");
    valid=valid&&error==ENODEV&&!inuse&&!name;
    free(name);
    g_zfs=saved_global;libzfs_fini(hdl);
    CHECK(valid);
    bug("Public native device preflight PASS exported=1 force=1 query-failure=1 whole-children=1\n");
    bug("Public upstream native in-use policy PASS exported=1 query-failure=1 blank=1 missing=1\n");
    return 0;
}
static int native_read_repeat_test(const char *path,uint64_t expected_guid)
{
    /* Alternate the labeled and blank devices. Stop on the first failed
     * read: this is repetition for coverage, not retry/error suppression. */
    for(unsigned i=0;i<32;i++) {
        nvlist_t *config=NULL;int labels=0;uint64_t guid=0;
        int error=oz_native_read_label(path,&config,&labels);
        int valid=!error&&config&&labels==4&&
            !nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&guid)&&guid==expected_guid;
        nvlist_free(config);config=NULL;
        if(!valid)bug("Native read repetition cycle=%u phase=labeled error=%d labels=%d guid=%llu\n",
            i,error,labels,(unsigned long long)guid);
        CHECK(valid);
        error=oz_native_read_label("/AROS/ata.device/1",&config,&labels);
        valid=!error&&!config&&!labels;
        nvlist_free(config);
        if(!valid)bug("Native read repetition cycle=%u phase=blank error=%d labels=%d\n",i,error,labels);
        CHECK(valid);
    }
    bug("Public native read repetition PASS cycles=32 alternating=1 identity=1\n");
    return 0;
}
static int auxiliary_policy_test(const char *path)
{
    /* Mutate only an in-memory copy of the real label; no disk writes.
     * Exercise both auxiliary states against a failed namespace query. */
    const uint64_t states[]={POOL_STATE_SPARE,POOL_STATE_L2CACHE};
    for(unsigned i=0;i<2;i++) {
        libzfs_handle_t *hdl=libzfs_init();
        if(!hdl)return errno?errno:EIO;
        nvlist_t *config=NULL;int labels=0;
        int error=oz_native_read_label(path,&config,&labels);
        if(!error)error=nvlist_add_uint64(config,ZPOOL_CONFIG_POOL_STATE,states[i]);
        if(error){nvlist_free(config);libzfs_fini(hdl);return error;}
        int saved=hdl->libzfs_fd;hdl->libzfs_fd=-1;
        pool_state_t state=POOL_STATE_UNINITIALIZED;
        char *name=NULL;boolean_t inuse=B_FALSE;
        int result=zpool_in_use_label(hdl,config,&state,&name,&inuse);
        hdl->libzfs_fd=saved;
        int valid=result==-1&&!inuse&&!name;
        if(!valid)bug("Native auxiliary query state=%llu result=%d inuse=%d\n",
            (unsigned long long)states[i],result,inuse);
        free(name);libzfs_fini(hdl);
        CHECK(valid);
    }
    bug("Public upstream auxiliary query failure PASS spare=1 cache=1\n");
    return 0;
}
static int candidate_path(nvlist_t *tree,uint64_t wanted,const char *path)
{
    uint64_t guid=0;
    if(!nvlist_lookup_uint64(tree,ZPOOL_CONFIG_GUID,&guid)&&guid==wanted) {
        const char *actual=NULL;
        return !nvlist_lookup_string(tree,ZPOOL_CONFIG_PATH,&actual)&&!strcmp(actual,path);
    }
    nvlist_t **children;uint_t count;
    if(!nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&count))
        for(uint_t i=0;i<count;i++)if(candidate_path(children[i],wanted,path))return 1;
    return 0;
}
static int scanner_core(const char *path,int denied,uint64_t guid,uint64_t pool_guid)
{
    pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    libpc_handle_t hdl={0};avl_tree_t *cache=NULL;
    const char *paths[]={path,path,"/AROS/ata.device/1"};
    /* Metadata preview, like zdb: no tryimport/activation/exclusive ownership
     * claim. Use the actual scanner, thread pool and configuration assembler. */
    importargs_t args={.can_be_active=B_TRUE};
    CHECK(!zpool_find_import_scan(&hdl,&lock,&cache,paths,sizeof(paths)/sizeof(paths[0]))&&cache);
    CHECK(avl_numnodes(cache)==(!strcmp(path,paths[2])?1:2));
    bug("Native scanner candidate=%s\n",((rdsk_node_t *)avl_first(cache))->rn_name);
    nvlist_t *pools=zpool_find_import_impl(&hdl,&args,&lock,cache);
    int valid=0;
    if(denied)valid=(!pools||nvlist_empty(pools))&&hdl.lpc_open_access_error;
    else {
        /* Import results allow duplicate pool names; like zpool import,
         * iterate pairs instead of using unique-name nvlist lookup. */
        unsigned matches=0;
        for(nvpair_t *p=NULL;pools&&(p=nvlist_next_nvpair(pools,p));) {
            nvlist_t *pool=NULL,*tree=NULL;uint64_t found=0;
            if(!strcmp(nvpair_name(p),"qemu_mirror")&&
                !nvpair_value_nvlist(p,&pool)&&
                !nvlist_lookup_uint64(pool,ZPOOL_CONFIG_POOL_GUID,&found)&&found==pool_guid&&
                !nvlist_lookup_nvlist(pool,ZPOOL_CONFIG_VDEV_TREE,&tree)&&candidate_path(tree,guid,path))
                matches++;
        }
        valid=matches==1;
    }
    if(!valid) {
        bug("Native scanner result pools=%p empty=%d access=%d error=%d detail=%s\n",pools,pools?nvlist_empty(pools):1,hdl.lpc_open_access_error,hdl.lpc_error,hdl.lpc_desc);
        for(nvpair_t *p=NULL;pools&&(p=nvlist_next_nvpair(pools,p));)
            bug("Native scanner pool key=%s type=%d\n",nvpair_name(p),nvpair_type(p));
    }
    nvlist_free(pools);
    CHECK(valid);
    const char *invalid[]={"/AROS/ata.device/-1"};cache=NULL;
    CHECK(zpool_find_import_scan(&hdl,&lock,&cache,invalid,1)==EINVAL&&!cache);
    pthread_mutex_destroy(&lock);
    bug("Public upstream native scanner PASS denied=%d explicit=1 duplicate=1 malformed=1\n",denied);
    return 0;
}
static int default_scanner_test(const char *path,int denied,uint64_t guid,uint64_t pool_guid)
{
    pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    libpc_handle_t hdl={0};avl_tree_t *cache=NULL;
    size_t count=0;
    const char *const *defaults=zpool_default_search_paths(&count);
    CHECK(count==1&&defaults&&!strcmp(defaults[0],"/AROS")&&!defaults[1]);
    int error=zpool_find_import_blkid(&hdl,&lock,&cache);
    if(denied) {
        CHECK(error==EACCES&&!cache&&hdl.lpc_open_access_error);
        pthread_mutex_destroy(&lock);
        bug("Public native default discovery PASS denied=1 no-cache=1\n");
        return 0;
    }
    CHECK(!error&&cache);
    unsigned target=0,optical=0;
    for(rdsk_node_t *node=avl_first(cache);node;node=AVL_NEXT(cache,node)) {
        if(!strcmp(node->rn_name,path))target++;
        if(!strncmp(node->rn_name,"/AROS/ata.device/2",18))optical++;
    }
    unsigned candidates=avl_numnodes(cache);
    importargs_t args={.can_be_active=B_TRUE};
    nvlist_t *pools=zpool_find_import_impl(&hdl,&args,&lock,cache);
    unsigned matches=0;
    for(nvpair_t *pair=NULL;pools&&(pair=nvlist_next_nvpair(pools,pair));) {
        nvlist_t *pool=NULL,*tree=NULL;uint64_t found=0;
        if(!strcmp(nvpair_name(pair),"qemu_mirror")&&!nvpair_value_nvlist(pair,&pool)&&
           !nvlist_lookup_uint64(pool,ZPOOL_CONFIG_POOL_GUID,&found)&&found==pool_guid&&
           !nvlist_lookup_nvlist(pool,ZPOOL_CONFIG_VDEV_TREE,&tree)&&candidate_path(tree,guid,path))matches++;
    }
    nvlist_free(pools);pthread_mutex_destroy(&lock);
    CHECK(target==1&&!optical&&candidates>=5&&matches==1);
    CHECK(getenv("ZPOOL_IMPORT_PATH")==NULL);
    char resolved[320];
    CHECK(!zfs_resolve_shortname("ata.device/3/partition/0",resolved,sizeof(resolved))&&!strcmp(resolved,path));
    CHECK(zfs_resolve_shortname("ata.device/3/partition/0",resolved,4)==ENAMETOOLONG);
    CHECK(zfs_resolve_shortname("ata.device/-1",resolved,sizeof(resolved))!=0);
    bug("Public native default discovery PASS registered=1 partitions=1 optical-excluded=1 pool=1 shortname=1 bounded=1\n");
    return 0;
}
static int scanner_test(const char *path,int denied,uint64_t guid,uint64_t pool_guid)
{
    if(denied)return scanner_core(path,denied,guid,pool_guid);
    struct oz_ticket held[OZ_ADMISSION_SLOTS];unsigned count=0;
    int error=0;
    while(count<OZ_ADMISSION_SLOTS) {
        error=oz_session_open(OZ_ADMISSION_PORT,&held[count]);
        if(error)break;
        count++;
    }
    if(!error) {
        struct oz_ticket extra;
        error=oz_session_open(OZ_ADMISSION_PORT,&extra);
        if(!error)oz_session_close(OZ_ADMISSION_PORT,&extra);
    }
    if(error!=EBUSY||!count)error=EIO;
    else {
        pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
        libpc_handle_t hdl={0};avl_tree_t *cache=NULL;
        const char *paths[]={path};
        error=zpool_find_import_scan(&hdl,&lock,&cache,paths,1);
        if(!error) {
            importargs_t args={.can_be_active=B_TRUE};
            nvlist_t *pools=zpool_find_import_impl(&hdl,&args,&lock,cache);
            if(pools||hdl.lpc_error!=LPC_BADPATH)error=EIO;
            nvlist_free(pools);
        }
        pthread_mutex_destroy(&lock);
        int closed=oz_session_close(OZ_ADMISSION_PORT,&held[--count]);
        if(!error)error=closed;
        if(!error)error=scanner_core(path,denied,guid,pool_guid);
    }
    while(count){int closed=oz_session_close(OZ_ADMISSION_PORT,&held[--count]);if(!error)error=closed;}
    if(!error)bug("Public native scanner session budget PASS one-slot=1 parent-owned=1 cleanup=1 exhausted-error=1\n");
    return error;
}
int oz_upstream_native_label_test(int denied)
{
    libzfs_handle_t *handle=libzfs_init();CHECK(handle);
    libzfs_handle_t *saved_global=g_zfs;g_zfs=handle;
    errno=0;
    int admitted=check_device("/AROS/ata.device/1",B_TRUE,B_FALSE,B_TRUE);
    int preflight_error=errno;
    int valid=denied?(admitted==-1&&preflight_error==EACCES):
        preflight_with_dos_claim("/AROS/ata.device/1",1,B_TRUE);
    if(!denied) {
        valid=valid&&!forced_dos_claim_test();
        valid=valid&&preflight_with_dos_claim("/AROS/ata.device/0/partition/0",0,B_FALSE);
        errno=0;
        valid=valid&&check_device("/AROS/ata.device/0/partition/0",B_TRUE,B_FALSE,B_TRUE)==-1&&errno==EINVAL;
        errno=0;
        valid=valid&&check_device("/AROS/openzfs-absent.device/0",B_TRUE,B_FALSE,B_TRUE)==-1&&errno==ENODEV;
        errno=0;
        valid=valid&&check_device("RAM:disk0",B_TRUE,B_FALSE,B_TRUE)==-1&&errno==EINVAL;
    }
    g_zfs=saved_global;libzfs_fini(handle);CHECK(valid);
    if(denied)bug("Public native device preflight PASS denied=1 force-denied=1\n");
    else bug("Public native device preflight PASS denied=0 blank=1 type=1 missing=1 malformed=1\n");
    CHECK(!retained_device_test(denied));
    nvlist_t *config=NULL;int labels=99;
    int error=oz_native_read_label("/AROS/ata.device/1",&config,&labels);
    pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
    libpc_handle_t hdl={0};
    rdsk_node_t node={.rn_name="/AROS/ata.device/1",.rn_hdl=&hdl,.rn_lock=&lock};
    if(denied) {
        CHECK(error==EACCES&&!config&&!labels);
        zpool_open_func(&node);
        CHECK(!node.rn_config&&!node.rn_num_labels&&hdl.lpc_open_access_error);
        CHECK(!scanner_test("/AROS/ata.device/1",1,0,0));
        CHECK(!default_scanner_test(NULL,1,0,0));
        bug("Public upstream native labels PASS denied=1 access-flag=1\n");
        return 0;
    }
    CHECK(!error&&!config&&!labels);
    const char *blank[]={"/AROS/ata.device/0","/AROS/ata.device/0/partition/0","/AROS/ata.device/0/partition/2"};
    for(unsigned i=0;i<3;i++) {
        labels=99;
        CHECK(!oz_native_read_label(blank[i],&config,&labels));
        CHECK(!config&&!labels);
    }
    CHECK(oz_native_read_label("/AROS/ata.device/0/partition/1",&config,&labels)==ENOENT&&!config&&!labels);
    CHECK(oz_native_read_label("/AROS/openzfs-absent.device/0",&config,&labels)==ENODEV&&!config&&!labels);
    CHECK(oz_native_read_label("/AROS/ata.device/-1",&config,&labels)==EINVAL&&!config&&!labels);
    CHECK(oz_native_read_label("RAM:disk0",&config,&labels)==EINVAL&&!config&&!labels);
    zpool_open_func(&node);
    CHECK(!node.rn_config&&!node.rn_num_labels&&!hdl.lpc_open_access_error);
    BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-label-test-only",SHARED_LOCK);
    if(marker) {
        UnLock(marker);
        /* A private copy of a previously verified QEMU-created GPT pool disk,
         * attached at IDE index 3. Never import it or follow label paths. */
        const char *path="/AROS/ata.device/3/partition/0";
        uint64_t expected_guid=0,expected_pool=0;
        for(unsigned i=0;i<4;i++) {
            error=oz_native_read_label(path,&config,&labels);
            if(error)bug("Native label read path=%s error=%d\n",path,error);
            CHECK(!error&&config&&labels==4);
            uint64_t guid=0,pool=0;const char *name=NULL;
            CHECK(!nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&guid)&&guid);
            CHECK(!nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&pool)&&pool);
            CHECK(!nvlist_lookup_string(config,ZPOOL_CONFIG_POOL_NAME,&name)&&!strcmp(name,"qemu_mirror"));
            if(i)CHECK(guid==expected_guid&&pool==expected_pool);
            expected_guid=guid;expected_pool=pool;
            nvlist_free(config);config=NULL;
        }
        node.rn_name=(char *)path;node.rn_vdev_guid=expected_guid;
        zpool_open_func(&node);
        CHECK(node.rn_config&&node.rn_num_labels==4&&!hdl.lpc_open_access_error);
        nvlist_free(node.rn_config);node.rn_config=NULL;node.rn_num_labels=0;
        node.rn_vdev_guid=expected_guid^1;
        if(!node.rn_vdev_guid)node.rn_vdev_guid=expected_guid^2;
        zpool_open_func(&node);
        CHECK(!node.rn_config&&!node.rn_num_labels);
        CHECK(!oz_native_read_label("/AROS/ata.device/3",&config,&labels)&&!config&&!labels);
        CHECK(!scanner_test(path,0,expected_guid,expected_pool));
        CHECK(!default_scanner_test(path,0,expected_guid,expected_pool));
        CHECK(!native_policy_test(path));
        CHECK(!auxiliary_policy_test(path));
        CHECK(!native_read_repeat_test(path,expected_guid));
        bug("Public upstream native labeled discovery PASS labels=4 repeats=4 identity=1 hook=1 mismatch=1 extent=1 pool=%llu vdev=%llu\n",(unsigned long long)expected_pool,(unsigned long long)expected_guid);
    }
    bug("Public upstream native labels PASS denied=0 blank=4 missing=1 malformed=1 hook=1\n");
    return 0;
}
