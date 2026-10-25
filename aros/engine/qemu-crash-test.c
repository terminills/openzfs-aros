/* SPDX-License-Identifier: CDDL-1.0 */
/* Private VM only: freeze TXG persistence, commit ZIL, await a hard power cut. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu_objset.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <proto/exec.h>
#include "zpl-fixture.h"
#include "device-io.h"
#include "partition-io.h"
#include <sys/vdev_impl.h>
#include <sys/zil_impl.h>
#include <sys/vdev_file.h>
#include <sys/zfs_file.h>
static zfs_file_t *trace_log_file;
static uint64_t trace_hash(const void *bytes,size_t count)
{
    const unsigned char *p=bytes;uint64_t h=UINT64_C(14695981039346656037);
    while(count--){h^=*p++;h*=UINT64_C(1099511628211);}return h;
}
int __real_zfs_file_pwrite(zfs_file_t *,const void *,size_t,loff_t,uint8_t,ssize_t *);
int __wrap_zfs_file_pwrite(zfs_file_t *file,const void *bytes,size_t count,
    loff_t offset,uint8_t ashift,ssize_t *resid)
{
    int traced=file==__atomic_load_n(&trace_log_file,__ATOMIC_ACQUIRE);
    uint64_t before_hash=traced?trace_hash(bytes,count):0;
    int error=__real_zfs_file_pwrite(file,bytes,count,offset,ashift,resid);
    if(traced) {
        /* Only this owned diagnostic's selected SLOG. Do not turn a failed
         * readback into success or alter the actual write result. */
        unsigned char *copy=malloc(count);
        ssize_t left=0;int readback=ENOMEM,match=0;uint64_t actual_hash=0;
        if(copy) {
            readback=zfs_file_pread(file,copy,count,offset,&left);
            match=!readback&&!left&&!memcmp(bytes,copy,count);
            if(!readback&&!left)actual_hash=trace_hash(copy,count);
            free(copy);
        }
        char line[320];
        snprintf(line,sizeof(line),"crash SLOG write offset=%llu size=%llu buffer=%p error=%d readback=%d match=%d before=%llx after=%llx actual=%llx\n",
            (unsigned long long)offset,(unsigned long long)count,bytes,error,readback,match,
            (unsigned long long)before_hash,(unsigned long long)trace_hash(bytes,count),
            (unsigned long long)actual_hash);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    return error;
}
static const char *dataset="qemu_mirror/mountlife";
static const char *payload_name="hello.txt";
static const char before[]="before-crash-sync";
static const char after[]="after--crash-sync";
/* Linker wrapping is restricted to this diagnostic executable. No fault
 * injection switch or alternate replay implementation enters the service. */
static int inject_setattr_failure;
static unsigned partial_failures;
int __real_zil_replay_checked(objset_t *,void *,zil_replay_func_t *const [],boolean_t *);
static int reject_setattr(void *arg,void *record,boolean_t byteswap)
{
    (void)arg;(void)record;(void)byteswap;partial_failures++;return EIO;
}
int __wrap_zil_replay_checked(objset_t *os,void *arg,
    zil_replay_func_t *const vector[],boolean_t *replayed)
{
    if(!inject_setattr_failure)return __real_zil_replay_checked(os,arg,vector,replayed);
    zil_replay_func_t *copy[TX_MAX_TYPE];
    memcpy(copy,vector,sizeof(copy));copy[TX_SETATTR]=reject_setattr;
    return __real_zil_replay_checked(os,arg,copy,replayed);
}
static int reject_replay(void *arg,void *record,boolean_t byteswap)
{
    (void)record;(void)byteswap;(*(unsigned *)arg)++;return EIO;
}
static void report(const char *label,int result)
{
    char text[160];snprintf(text,sizeof(text),"%s=%d\n",label,result);
    for(char *p=text;*p;p++)RawPutChar(*p);
}
static int slog_prepare(spa_t *spa)
{
    nvlist_t *root=fnvlist_alloc(),*leaf=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,"/AROS/ata.device/3/partition/0");
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_ASHIFT,12);
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_IS_LOG,1);
    const nvlist_t *leaves[]={leaf};
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,leaves,1);
    int error=spa_vdev_add(spa,root,B_FALSE);
    fnvlist_free(root);fnvlist_free(leaf);
    if(!error)error=oz_zpl_fixture_create(dataset);
    nvlist_t *config=NULL;
    if(!error)error=spa_get_stats("qemu_mirror",&config,NULL,0);
    char *packed=NULL;size_t length=0;
    if(!error)error=nvlist_pack(config,&packed,&length,NV_ENCODE_XDR,0);
    if(!error&&length>1024*1024)error=EOVERFLOW;
    if(!error) {
        report("SLOG_CONFIG_BEGIN",(int)length);
        const char hex[]="0123456789abcdef";
        for(size_t offset=0;offset<length;offset+=32) {
            const char *prefix="SLOG_CONFIG ";
            for(const char *p=prefix;*p;p++)RawPutChar(*p);
            size_t n=length-offset;if(n>32)n=32;
            for(size_t j=0;j<n;j++) {
                unsigned char b=packed[offset+j];
                RawPutChar(hex[b>>4]);RawPutChar(hex[b&15]);
            }
            RawPutChar('\n');
        }
        report("SLOG_CONFIG_END",0);
    }
    free(packed);if(config)nvlist_free(config);
    return error;
}
/* Private diagnostic snapshots, not a service logging knob. Take them only
 * between completed namespace operations; preserve all recovery assertions. */
static void log_snapshot(const char *where)
{
    objset_t *os=NULL;
    int error=dmu_objset_hold(dataset,FTAG,&os);
    if(error) { report("crash log snapshot hold",error);return; }
    zilog_t *zil=os->os_zil;
    if(zil) {
        uint64_t seq,committed,restart;unsigned suspended,replay;
        mutex_enter(&zil->zl_issuer_lock);
        mutex_enter(&zil->zl_lock);
        seq=zil->zl_lr_seq;committed=zil->zl_commit_lr_seq;
        restart=zil->zl_restart_txg;suspended=zil->zl_suspend;
        replay=zil->zl_replay;
        mutex_exit(&zil->zl_lock);
        mutex_exit(&zil->zl_issuer_lock);
        zil_kstat_values_t stats={0};
        /* This userspace fixture does not always attach per-dataset sums. */
        if(zil->zl_sums)zil_kstat_values_update(&stats,zil->zl_sums);
        char text[384];
        snprintf(text,sizeof(text),
            "crash log %s seq=%llu committed=%llu restart=%llu suspend=%u replay=%u stats=%u errors=%llu crashes=%llu\n",
            where,(unsigned long long)seq,(unsigned long long)committed,
            (unsigned long long)restart,suspended,replay,zil->zl_sums!=NULL,
            (unsigned long long)stats.zil_commit_error_count.value.ui64,
            (unsigned long long)stats.zil_commit_crash_count.value.ui64);
        for(char *p=text;*p;p++)RawPutChar(*p);
    }
    dmu_objset_rele(os,FTAG);
}
static int namespace_stage(struct oz_zpl *fs)
{
    uint64_t root=oz_zpl_root(fs);struct oz_zpl_info file,dir,temp;
    int error=oz_zpl_create_as(fs,root,"crash-file",0,kcred,&file);
    size_t written=0;
    if(!error)error=oz_zpl_write_as(fs,file.object,0,after,sizeof(after),&written,kcred);
    if(!error&&written!=sizeof(after))error=EIO;
    if(!error)error=oz_zpl_mkdir_as(fs,root,"crash-dir",0,kcred,&dir);
    if(!error)error=oz_zpl_create_as(fs,root,"crash-exchange",0,kcred,&temp);
    if(!error)error=oz_zpl_write_as(fs,temp.object,0,after,sizeof(after),&written,kcred);
    if(!error&&written!=sizeof(after))error=EIO;
    if(!error)error=oz_zpl_create_as(fs,dir.object,"crash-exchange",0,kcred,&temp);
    if(!error)error=oz_zpl_write_as(fs,temp.object,0,before,sizeof(before),&written,kcred);
    if(!error&&written!=sizeof(before))error=EIO;
    if(!error)error=oz_zpl_exchange_as(fs,root,"crash-exchange",dir.object,"crash-exchange",kcred);
    report("crash exchange staged",error);
    if(!error)error=oz_zpl_create_as(fs,root,"crash-whiteout",0,kcred,&temp);
    if(!error)error=oz_zpl_write_as(fs,temp.object,0,after,sizeof(after),&written,kcred);
    if(!error&&written!=sizeof(after))error=EIO;
    if(!error)error=oz_zpl_whiteout_as(fs,root,"crash-whiteout",dir.object,"whiteout-moved",kcred);
    report("crash whiteout staged",error);
    if(!error)error=oz_zpl_link_as(fs,root,file.object,"crash-alias",kcred);
    if(!error)error=oz_zpl_link_as(fs,root,file.object,"crash-kept-alias",kcred);
    if(!error)log_snapshot("after-links");
    if(!error)error=oz_zpl_rename_as(fs,root,"crash-file",dir.object,"moved",kcred);
    if(!error)log_snapshot("after-rename");
    if(!error)error=oz_zpl_remove_as(fs,root,"crash-alias",0,kcred);
    if(!error)error=oz_zpl_symlink_as(fs,root,"crash-soft","crash-dir/moved",0,kcred);
    if(!error)error=oz_zpl_create_as(fs,root,"crash-gone",0,kcred,&temp);
    if(!error)error=oz_zpl_remove_as(fs,root,"crash-gone",0,kcred);
    if(!error)error=oz_zpl_mkdir_as(fs,root,"crash-gone-dir",0,kcred,&temp);
    if(!error)error=oz_zpl_remove_as(fs,root,"crash-gone-dir",1,kcred);
    if(!error)error=oz_zpl_remove_as(fs,root,"crash-old",0,kcred);
    if(!error)error=oz_zpl_remove_as(fs,root,"crash-old-dir",1,kcred);
    if(!error)error=oz_zpl_resize_as(fs,file.object,64,kcred);
    if(!error)error=oz_zpl_set_owner(fs,file.object,77,88,kcred);
    if(!error)error=oz_zpl_set_mtime(fs,file.object,1234567890,123000000,kcred);
    if(!error)error=oz_zpl_comment_store(fs,file.object,"replayed comment",kcred);
    /* Metadata setters enqueue records; only this explicit sync promises
     * power-loss durability for the complete staged namespace. */
    if(!error)error=oz_zpl_sync(fs);
    if(!error)log_snapshot("after-namespace-sync");
    report("crash namespace staged",error);return error;
}
static int namespace_verify(struct oz_zpl *fs)
{
    uint64_t root=oz_zpl_root(fs);struct oz_zpl_info file,dir,link,temp;
    int error=oz_zpl_lookup_at(fs,root,"crash-dir",&dir);
    if(!error&&!dir.directory)error=EIO;
    if(!error)error=oz_zpl_lookup_at(fs,dir.object,"moved",&file);
    char bytes[64],target[80];
    if(!error)error=oz_zpl_read_as(fs,file.object,0,bytes,sizeof(bytes),kcred);
    if(!error) {
        report("crash namespace bytes match",!memcmp(bytes,after,sizeof(after)));
        report("crash namespace protection present",file.has_native_protection);
        report("crash namespace protection",file.native_protection);
        report("crash namespace size",file.size);
        report("crash namespace uid",file.mapped_uid);
        report("crash namespace gid",file.mapped_gid);
        report("crash namespace mtime",file.mtime);
        report("crash namespace mtime nanos",file.mtime_nsec);
    }
    if(!error&&(memcmp(bytes,after,sizeof(after))||!file.has_native_protection||file.native_protection||
        file.size!=64||file.mapped_uid!=77||file.mapped_gid!=88||
        file.mtime!=1234567890||file.mtime_nsec!=123000000))error=EIO;
    for(unsigned i=sizeof(after);i<sizeof(bytes)&&!error;i++)if(bytes[i])error=EIO;
    if(!error)error=oz_zpl_comment_get(fs,file.object,target,sizeof(target));
    report("crash namespace metadata",error);
    if(!error&&strcmp(target,"replayed comment"))error=EIO;
    report("crash namespace comment",error);
    if(!error)error=oz_zpl_lookup_at(fs,root,"crash-kept-alias",&temp);
    if(!error&&temp.object!=file.object)error=EIO;
    if(!error)error=oz_zpl_lookup_at(fs,root,"crash-soft",&link);
    if(!error&&!link.symlink)error=EIO;
    if(!error)error=oz_zpl_readlink_as(fs,link.object,target,sizeof(target),kcred);
    if(!error&&strcmp(target,"crash-dir/moved"))error=EIO;
    if(!error)error=oz_zpl_lookup_at(fs,root,"crash-exchange",&temp);
    if(!error)error=oz_zpl_read_as(fs,temp.object,0,bytes,sizeof(before),kcred);
    if(!error&&(temp.size!=sizeof(before)||memcmp(bytes,before,sizeof(before))))error=EIO;
    if(!error)error=oz_zpl_lookup_at(fs,dir.object,"crash-exchange",&temp);
    if(!error)error=oz_zpl_read_as(fs,temp.object,0,bytes,sizeof(after),kcred);
    if(!error&&(temp.size!=sizeof(after)||memcmp(bytes,after,sizeof(after))))error=EIO;
    report("crash exchange recovered",error);
    if(!error)error=oz_zpl_lookup_at(fs,root,"crash-whiteout",&temp);
    if(!error&&(temp.special!=OZ_SPECIAL_CHARACTER||temp.size))error=EIO;
    if(!error)error=oz_zpl_lookup_at(fs,dir.object,"whiteout-moved",&temp);
    if(!error)error=oz_zpl_read_as(fs,temp.object,0,bytes,sizeof(after),kcred);
    if(!error&&(temp.size!=sizeof(after)||memcmp(bytes,after,sizeof(after))))error=EIO;
    report("crash whiteout recovered",error);
    const char *gone[]={"crash-file","crash-alias","crash-gone","crash-gone-dir","crash-old","crash-old-dir"};
    for(unsigned i=0;i<sizeof(gone)/sizeof(gone[0])&&!error;i++)
        if(oz_zpl_lookup_at(fs,root,gone[i],&temp)!=ENOENT)error=EIO;
    report("crash namespace recovered",error);return error;
}
int main(int argc,char **argv)
{
    if(argc!=2||(strcmp(argv[1],"STAGE")&&strcmp(argv[1],"RECOVER")&&
        strcmp(argv[1],"PARTIAL")&&strcmp(argv[1],"RESUME")&&strcmp(argv[1],"VERIFY")))return 20;
    int stage=!strcmp(argv[1],"STAGE");
    int partial=!strcmp(argv[1],"PARTIAL");
    int resume=!strcmp(argv[1],"RESUME");
    int recover=!strcmp(argv[1],"RECOVER")||partial;
    int slog=0;
    int slog_option=open("/SYS/slog-test",O_RDONLY);
    if(slog_option>=0) {
        char value[3];ssize_t n=read(slog_option,value,sizeof(value));close(slog_option);
        if(n!=2||value[0]!='1'||value[1]!='\n')return 20;
        slog=1;dataset="qemu_mirror/slogcrash";
    } else if(errno!=ENOENT)return 20;
    unsigned legacy=0;
    char legacy_dataset[80];
    int selection=open("/SYS/legacy-version",O_RDONLY);
    if(selection>=0) {
        char value[3];ssize_t n=read(selection,value,sizeof(value));close(selection);
        if(n!=2||value[0]<'1'||value[0]>'4'||value[1]!='\n')return 20;
        if(slog)return 20;
        legacy=value[0]-'0';
        snprintf(legacy_dataset,sizeof(legacy_dataset),"qemu_mirror/legacy-cold-%u",legacy);
        dataset=legacy_dataset;payload_name="payload";
    } else if(errno!=ENOENT)return 20;
    int fd=open("/SYS/export.nvlist",O_RDONLY);if(fd<0)return 20;
    struct stat st;int error=fstat(fd,&st);
    if(error||st.st_size<=0||st.st_size>1024*1024){close(fd);return 20;}
    char *bytes=malloc(st.st_size);if(!bytes){close(fd);return 20;}
    if(read(fd,bytes,st.st_size)!=st.st_size){close(fd);free(bytes);return 20;}
    close(fd);nvlist_t *config=NULL;
    error=nvlist_unpack(bytes,st.st_size,&config,0);free(bytes);if(error)return 20;
    if(setenv("OPENZFS_QEMU_DEVICE_IO","1",1)||setenv("ZFS_HOSTID","0x51a20519",1))return 20;
    spa_config_path="/RAM/nonexistent-openzfs.cache";
    kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);
    struct oz_device_binding *slog_binding=NULL;
    if(slog) {
        struct oz_device *device=NULL;
        error=oz_device_open("ata.device",3,0,&device);
        if(!error&&oz_device_size(device)!=512ULL*1024*1024)error=EINVAL;
        oz_device_close(device);device=NULL;
        if(!error)error=oz_device_open_partition("ata.device",3,0,1,&device);
        if(!error&&oz_device_size(device)!=256ULL*1024*1024)error=EINVAL;
        if(!error)error=oz_device_bind_writable("/AROS/ata.device/3/partition/0",device,&slog_binding);
        oz_device_close(device);
        report("crash dedicated SLOG binding",error);
    }
    nvlist_t *props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    if(!error)error=spa_import("qemu_mirror",config,props,0);
    report("crash spa_import",error);
    spa_t *spa=NULL;struct oz_zpl *fs=NULL;
    if(!error)error=spa_open("qemu_mirror",&spa,FTAG);
    if(!error&&slog&&stage)error=slog_prepare(spa);
    objset_t *os=NULL;
    if(!error)error=dmu_objset_hold(dataset,FTAG,&os);
    if(!error) {
        uint64_t version=0;
        if(legacy) {
            error=zap_lookup(os,MASTER_NODE_OBJ,ZPL_VERSION_STR,8,1,&version);
            if(!error&&version!=legacy)error=EIO;
            report("crash legacy version",version);
        }
        report("crash ZIL replay needed",!!(os->os_zil_header.zh_flags&ZIL_REPLAY_NEEDED));
        if((recover||resume)&&!(os->os_zil_header.zh_flags&ZIL_REPLAY_NEEDED))error=EIO;
        if(!stage&&!recover&&!resume&&(os->os_zil_header.zh_flags&ZIL_REPLAY_NEEDED))error=EIO;
        dmu_objset_rele(os,FTAG);
    }
    if(!error&&recover) {
        /* Deliberate callback failure must not destroy the only durable copy
         * of an acknowledged write. Exercise the real upstream parser. */
        error=dmu_objset_own(dataset,DMU_OST_ZFS,B_FALSE,B_FALSE,FTAG,&os);
        if(!error) {
            zilog_t *log=zil_open(os,NULL,NULL);
            zil_replay_func_t *vector[TX_MAX_TYPE];
            for(unsigned i=0;i<TX_MAX_TYPE;i++)vector[i]=reject_replay;
            unsigned calls=0;boolean_t replayed=B_FALSE;
            int rejected=zil_replay_checked(os,&calls,vector,&replayed);
            if(rejected!=EIO||!calls||!replayed||
                !(os->os_zil_header.zh_flags&ZIL_REPLAY_NEEDED)||
                BP_IS_HOLE(&os->os_zil_header.zh_log))error=EIO;
            zil_close(log);dmu_objset_disown(os,B_FALSE,FTAG);
        }
        report("crash failed replay retained log",error);
    }
    if(!error&&recover) {
        /* Real namespace callbacks run through deletions before SETATTR
         * fails. The mount must unwind without discarding their retry log. */
        inject_setattr_failure=1;
        int rejected=oz_zpl_open_writable(dataset,&fs);
        inject_setattr_failure=0;
        report("crash partial replay returned",rejected);
        if(rejected!=EIO||!partial_failures||fs)error=EIO;
        if(!error)error=dmu_objset_hold(dataset,FTAG,&os);
        if(!error) {
            if(!(os->os_zil_header.zh_flags&ZIL_REPLAY_NEEDED)||
                BP_IS_HOLE(&os->os_zil_header.zh_log)||
                !os->os_zil_header.zh_replay_seq)error=EIO;
            uint64_t root=0,queue=0,count=0,entry=0;
            if(!error)error=zap_lookup(os,MASTER_NODE_OBJ,ZFS_ROOT_OBJ,8,1,&root);
            if(!error&&zap_lookup(os,root,"crash-old",8,1,&entry)!=ENOENT)error=EIO;
            if(!error)error=zap_lookup(os,MASTER_NODE_OBJ,ZFS_UNLINKED_SET,8,1,&queue);
            if(!error)error=zap_count(os,queue,&count);
            if(!error&&!count)error=EIO;
            dmu_objset_rele(os,FTAG);
        }
        report("crash partial replay cleanup",error);
    }
    if(!error&&partial) {
        const char *ready="CRASH READY partial replay persisted without export\n";
        for(const char *p=ready;*p;p++)RawPutChar(*p);
        for(;;)Wait(0);
    }
    if(!error)error=oz_zpl_open_writable(dataset,&fs);
    report("crash writer open",error);
    uint64_t object=0,size=0;
    if(!error)error=oz_zpl_lookup(fs,payload_name,&object,&size);
    if(!error&&stage) {
        size_t written=0;
        error=oz_zpl_write_as(fs,object,0,before,sizeof(before),&written,kcred);
        if(!error&&written!=sizeof(before))error=EIO;
        struct oz_zpl_info old;
        if(!error)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"crash-old",0,kcred,&old);
        if(!error)error=oz_zpl_mkdir_as(fs,oz_zpl_root(fs),"crash-old-dir",0,kcred,&old);
        if(!error)error=oz_zpl_sync(fs);
        /* Anchor the first log block before freezing, as upstream ztest does. */
        if(!error)error=dmu_objset_hold(dataset,FTAG,&os);
        if(!error) {
            if(BP_IS_HOLE(&os->os_zil_header.zh_log))error=EIO;
            if(!error&&slog) {
                uint64_t id=DVA_GET_VDEV(&os->os_zil_header.zh_log.blk_dva[0]);
                spa_config_enter(spa,SCL_STATE_ALL,FTAG,RW_READER);
                if(id>=spa->spa_root_vdev->vdev_children||
                    !spa->spa_root_vdev->vdev_child[id]->vdev_islog)error=EIO;
                if(!error) {
                    vdev_t *vd=spa->spa_root_vdev->vdev_child[id];
                    if(vd->vdev_children||!vd->vdev_tsd)error=EIO;
                    else __atomic_store_n(&trace_log_file,
                        ((vdev_file_t *)vd->vdev_tsd)->vf_file,__ATOMIC_RELEASE);
                }
                spa_config_exit(spa,SCL_STATE_ALL,FTAG);
                report("crash dedicated SLOG anchor",error);
            }
            dmu_objset_rele(os,FTAG);
        }
        if(!error) {
            spa_freeze(spa);
            report("crash TXG frozen",0);
            error=oz_zpl_write_as(fs,object,0,after,sizeof(after),&written,kcred);
            if(!error&&written!=sizeof(after))error=EIO;
            if(!error)error=namespace_stage(fs);
        }
        if(!error) {
            const char *ready="CRASH READY committed ZIL without clean close\n";
            for(const char *p=ready;*p;p++)RawPutChar(*p);
            for(;;)Wait(0); /* Harness kills only this owned VM, not guest cleanup. */
        }
    }
    if(!error&&!stage) {
        char readback[sizeof(after)];
        error=oz_zpl_read_as(fs,object,0,readback,sizeof(readback),kcred);
        if(!error&&memcmp(readback,after,sizeof(after)))error=EIO;
        report("crash recovered committed bytes",error);
        if(!error)error=namespace_verify(fs);
    }
    int close=oz_zpl_close(fs);if(!error)error=close;
    if(spa)spa_close(spa,FTAG);
    if(!error)error=spa_export("qemu_mirror",NULL,B_FALSE,B_FALSE);
    if(!error&&slog_binding)oz_device_unbind(slog_binding);
    fnvlist_free(props);nvlist_free(config);kernel_fini();
    const char *result=error?"RESULT crash recovery FAIL\n":"RESULT crash recovery PASS\n";
    for(const char *p=result;*p;p++)RawPutChar(*p);
    return error?20:0;
}
