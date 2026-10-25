/* SPDX-License-Identifier: CDDL-1.0 */
/* Single service-task adapter. No device discovery or implicit pool imports. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/aros_vdev_policy.h>
#include <sys/spa_checkpoint.h>
#include <sys/fs/zfs.h>
#include <sys/vdev.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_file.h>
#include <sys/vdev_removal.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include "../zpl/lifecycle.h"
#include <sys/zil.h>
#include <sys/txg.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_scan.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_crypt.h>
#include <sys/zio_crypt.h>
#include <sys/dsl_userhold.h>
#include <zfs_namecheck.h>
#include <fcntl.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/security.h>
#include "../ui/api.h"
#include "zpl-fixture.h"
#include "dos-handler.h"
#include "device-io.h"
#include "partition-io.h"
#include "../cli/device-path.h"
#include "admission.h"
#include "packet-cred.h"
#include "replication.h"
#include "stream-file.h"
#include "stream-output.h"
#include "passphrase.h"
#include "cache-policy.h"
#include "hostid-policy.h"
#include "snapshot-unmount.h"
#ifdef OZ_NATIVE_DISPATCHER
#include <sys/zfs_ioctl_native.h>
#include "control-file.h"
#include "ioctl-copy.h"
/* Complete native dispatcher linked into this libzpool-context adapter. */
extern long zfsdev_ioctl_common(uint_t,zfs_cmd_t *,int);
#endif
static int ready;
static struct Task *engine_owner;
static int fixture_writes;
extern boolean_t spa_mode_readable_spacemaps;
int oz_property_get(struct oz_message *);
int oz_property_get_all(struct oz_message *);
int oz_pool_property_change(struct oz_message *,cred_t *);
int oz_pool_history(struct oz_message *,cred_t *);
int oz_pool_status(struct oz_message *,int *);
int oz_scan_control(const char *,const char *);
int oz_scan_status(const char *,char *,size_t);
int oz_zpl_dataset_create(const char *,cred_t *);
int oz_pool_create(struct oz_message *,cred_t *,const struct oz_packet_security *);
int oz_pool_add(struct oz_message *,cred_t *,const struct oz_packet_security *);
int oz_pool_split(struct oz_message *,cred_t *);
int oz_pool_clear(struct oz_message *,cred_t *);
int oz_pool_initialize(struct oz_message *,cred_t *);
int oz_pool_trim(struct oz_message *,cred_t *);
int oz_resilver_restart(struct oz_message *,cred_t *);
void oz_pool_split_release(const char *);
int oz_pool_split_source_busy(const char *);
int oz_pool_attach(struct oz_message *,cred_t *,const struct oz_packet_security *,int);
int oz_pool_import_files(struct oz_message *,cred_t *,const struct oz_packet_security *);
void oz_pool_import_release(const char *);
int oz_zpl_layout_test(void);
/* Detached endpoints remain valid for late DeviceProc packets. Keep their
 * ports in a compact tombstone list, while removing closed filesystems from
 * the active metadata registry. All endpoint ports borrow one service-task
 * signal, including notification/timer replies. */
struct retired_mount {struct retired_mount *next;struct oz_mount *mount;};
static struct mount_entry {struct oz_zpl *fs;struct oz_mount *mount;struct retired_mount *retirement;char dataset[256],name[32];int authenticated,cleanup_pending;} *mounts;
static struct retired_mount *retired_mounts;
static unsigned mount_count;
static int mount_signal=-1;
void *oz_mount_registry_calloc(size_t,size_t);
void *oz_mount_registry_realloc(void *,size_t);
static void mount_entry_remove(unsigned index,int retain_endpoint)
{
    VERIFY3U(index,<,mount_count);
    struct mount_entry *entry=&mounts[index];
    if(retain_endpoint) {
        VERIFY3P(entry->mount,!=,NULL);
        VERIFY3P(entry->retirement,!=,NULL);
        entry->retirement->mount=entry->mount;
        entry->retirement->next=retired_mounts;
        retired_mounts=entry->retirement;
    } else free(entry->retirement);
    if(index+1<mount_count)
        memmove(&mounts[index],&mounts[index+1],(mount_count-index-1)*sizeof(*mounts));
    memset(&mounts[mount_count-1],0,sizeof(*mounts));
    if(!--mount_count){free(mounts);mounts=NULL;}
}
static int native_mount_snapshot(struct oz_mount_record *records,ULONG capacity,ULONG *count)
{
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    *count=0;
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending)++*count;
    if(*count>OZ_MOUNTS_LIMIT)return E2BIG;
    if(*count>capacity)return ENOSPC;
    unsigned n=0;
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending) {
        struct oz_mount_record *r=&records[n++];
        strlcpy(r->dataset,mounts[i].dataset,sizeof(r->dataset));
        snprintf(r->mountpoint,sizeof(r->mountpoint),"%s:",mounts[i].name);
        r->flags=oz_zpl_readonly(mounts[i].fs)?OZ_MOUNT_READONLY:0;
    }
    return 0;
}
static int mounted_index(const char *dataset)
{
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending&&!strcmp(mounts[i].dataset,dataset))return i;
    return -1;
}
static int mount_cleanup_pending(const char *dataset)
{
    for(unsigned i=0;i<mount_count;i++)
        if(mounts[i].fs&&mounts[i].cleanup_pending&&!strcmp(mounts[i].dataset,dataset))return 1;
    return 0;
}
static int native_mount_policy(const char *dataset,ULONG *supported,ULONG *enabled)
{
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    int index=mounted_index(dataset);
    if(index<0)return ENOENT;
    uint32_t values=0;
    int error=oz_zpl_mount_policy(mounts[index].fs,&values);
    if(!error){*supported=OZ_POLICY_KNOWN;*enabled=values;}
    return error;
}
static int native_mount_shares(const char *dataset,UQUAD *object)
{
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    int index=mounted_index(dataset);
    if(index<0)return ENOENT;
    uint64_t value=0;
    int error=oz_zpl_shares_object(mounts[index].fs,&value);
    if(!error)*object=value;
    return error;
}
static int mounted_subtree(const char *dataset)
{
    size_t n=strlen(dataset);
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&!strncmp(mounts[i].dataset,dataset,n)&&
        (!mounts[i].dataset[n]||mounts[i].dataset[n]=='/'||mounts[i].dataset[n]=='@'))return 1;
    return 0;
}
/* Serialized service task only: keep native DOS publication and retained
 * device bindings consistent with an upstream CLI export. */
static int suspended_export_check(const char *name)
{
    /* Do not enter upstream txg_wait_synced for a pool already suspended.
     * This is a preflight, not a guarantee against a subsequent I/O failure. */
    spa_namespace_enter(FTAG);
    spa_t *spa=spa_lookup(name);
    int error=spa&&spa_suspended(spa)?EAGAIN:0;
    spa_namespace_exit(FTAG);
    return error;
}
static int suspended_dataset_check(const char *name)
{
    /* Closing a writable ZPL mount can wait for a final TXG.  Once the pool
     * is suspended that wait cannot make progress until explicit readmission,
     * so refuse before withdrawing the DOS namespace.  spa_open accepts a
     * dataset name and gives us the owning pool without a second parser. */
    spa_t *spa=NULL;
    int error=spa_open(name,&spa,FTAG);
    if(!error) {
        if(spa_suspended(spa))error=EAGAIN;
        spa_close(spa,FTAG);
    }
    return error;
}
int oz_cli_pool_export_check(const char *name,cred_t *caller)
{
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(pool_namecheck(name,NULL,NULL))return EINVAL;
    error=suspended_export_check(name);if(error)return error;
    return mounted_subtree(name)?EBUSY:oz_pool_split_source_busy(name);
}
void oz_cli_pool_export_done(const char *name)
{
    oz_pool_import_release(name);
    oz_pool_split_release(name);
}
static int mounted_snapshot_rename(const char *name,int recursive,int preflight)
{
    const char *at=strchr(name,'@');size_t base=(size_t)(at-name);
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs) {
        const char *mounted=mounts[i].dataset,*snap=strchr(mounted,'@');
        if(!snap||strcmp(snap+1,at+1)||strncmp(mounted,name,base))continue;
        if(snap==mounted+base||(recursive&&mounted[base]=='/')) {
            if(mounts[i].cleanup_pending)return EBUSY;
            int error=preflight?oz_dos_detach_check(mounts[i].mount):EBUSY;
            if(error)return error;
        }
    }
    return 0;
}
int oz_cli_dataset_rename_check(const char *source,const char *dest,int flags)
{
    int recursive=flags&1;
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    if(dataset_namecheck(source,NULL,NULL)||dataset_namecheck(dest,NULL,NULL))return EINVAL;
    if(strchr(source,'#')||strchr(dest,'#'))return ENOTSUP;
    int snapshot=strchr(source,'@')!=NULL;
    if(snapshot!=(strchr(dest,'@')!=NULL))return EINVAL;
    /* Check every source before upstream detaches any snapshot. This catches
     * retained handles without partial withdrawal; detach still rechecks for
     * newly queued packets. Nounmount must preserve existing publication. */
    if(snapshot) {
        int error=mounted_snapshot_rename(dest,recursive,0);
        return error?error:mounted_snapshot_rename(source,recursive,!(flags&2));
    }
    if(recursive)return EINVAL;
    return mounted_subtree(source)||mounted_subtree(dest)?EBUSY:0;
}
struct receive_input {struct oz_stream_file *native;struct oz_dos_stream *local;};
static int receive_reader(void *arg,void *buffer,size_t length,size_t *actual)
{
    struct receive_input *input=arg;
    return input->local?oz_dos_stream_read(input->local,buffer,length,actual):
        oz_stream_file_read(input->native,buffer,length,actual);
}

static int send_writer(void *arg,const void *buffer,size_t length)
{
    const char *bytes=buffer;
    while(length){
        size_t actual=0;int error=oz_stream_file_write(arg,bytes,length,&actual);
        if(error)return error;
        if(!actual)return EIO;
        bytes+=actual;length-=actual;
    }
    return 0;
}

static void key_clear(void *buffer,size_t length)
{
    volatile unsigned char *p=buffer;
    while(length--)*p++=0;
}
static int key_hex_digit(unsigned char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}

static int passphrase_file(struct receive_input *stream,int change,
    uint64_t salt,uint64_t iterations,uint8_t key[WRAPPING_KEY_LEN])
{
    /* libzfs reads the first line, strips LF only, and uses strlen for the
     * KDF. Existing keys have no new-passphrase length restriction. Grow by
     * copying then wiping, not realloc, which may leave secrets in freed RAM. */
    size_t capacity=512,used=0;char *line=malloc(capacity);
    if(!line)return ENOMEM;
    int error=0,done=0,saw_input=0;uint8_t chunk[256]={0};
    while(!error&&!done){
        size_t actual=0;error=receive_reader(stream,chunk,sizeof(chunk),&actual);
        if(error)break;
        if(actual>sizeof(chunk)){error=EIO;break;}
        if(!actual)break;
        saw_input=1;
        uint8_t *lf=memchr(chunk,'\n',actual);
        if(lf){actual=(size_t)(lf-chunk);done=1;}
        if(actual>SIZE_MAX-used-1){error=EOVERFLOW;break;}
        if(change&&used+actual>512){error=EINVAL;break;}
        size_t need=used+actual+1;
        if(need>capacity){
            size_t next=capacity>SIZE_MAX/2?need:capacity*2;
            if(next<need)next=need;
            char *grown=malloc(next);
            if(!grown){error=ENOMEM;break;}
            memcpy(grown,line,used);key_clear(line,capacity);free(line);
            line=grown;capacity=next;
        }
        memcpy(line+used,chunk,actual);used+=actual;
        key_clear(chunk,sizeof(chunk));
    }
    if(!error&&(!saw_input||(change&&used<8)))error=EINVAL;
    if(!error){line[used]=0;error=oz_passphrase_derive(line,strlen(line),salt,iterations,key);}
    key_clear(chunk,sizeof(chunk));key_clear(line,capacity);free(line);
    return error;
}

struct create_key_context { const struct oz_message *request; const struct oz_packet_security *security; };
int oz_zpl_dataset_create_prepared(const char *,const char *,cred_t *,
    int (*)(nvlist_t *,nvlist_t *,void *),void *);
static int prepare_create_key(nvlist_t *props,nvlist_t *input,void *opaque)
{
    struct create_key_context *c=opaque;
    uint64_t encryption=0,format=0,salt=0,iterations=DEFAULT_PBKDF2_ITERATIONS;
    /* Keep the upstream "on" alias: dsl_crypto_params_create resolves it to
     * ZIO_CRYPT_ON_VALUE. A supplied key still requires actual encryption. */
    if(nvlist_lookup_uint64(props,"encryption",&encryption)||
        encryption==ZIO_CRYPT_INHERIT||encryption==ZIO_CRYPT_OFF||encryption>=ZIO_CRYPT_FUNCTIONS||
        nvlist_lookup_uint64(props,"keyformat",&format)||
        (format!=ZFS_KEYFORMAT_RAW&&format!=ZFS_KEYFORMAT_HEX&&format!=ZFS_KEYFORMAT_PASSPHRASE))return EINVAL;
    const char *path=c->request->argument;
    if(!strchr(path,':'))return EINVAL;
    if(nvlist_exists(props,"pbkdf2iters")){
        if(format!=ZFS_KEYFORMAT_PASSPHRASE||
            nvlist_lookup_uint64(props,"pbkdf2iters",&iterations)||
            iterations<MIN_PBKDF2_ITERATIONS||iterations>UINT32_MAX)return EINVAL;
    }
    int error=0;
    if(format==ZFS_KEYFORMAT_PASSPHRASE){
        error=random_get_bytes((uint8_t *)&salt,sizeof(salt));
        if(error)return error;
        if(!salt)return EIO;
        fnvlist_add_uint64(props,"pbkdf2salt",salt);
        fnvlist_add_uint64(props,"pbkdf2iters",iterations);
    }
    BPTR lock=0;struct receive_input stream={0};error=ENODEV;
    for(unsigned i=0;i<mount_count&&error==ENODEV;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending)
        error=oz_dos_stream_open(mounts[i].mount,c->security,path,&stream.local);
    if(error==ENODEV){
        error=oz_stream_path_lock(c->security,path,&lock);
        if(!error)error=oz_stream_file_open(c->security,&lock,secAt_Read,&stream.native);
    }
    uint8_t material[WRAPPING_KEY_LEN*2+1]={0},key[WRAPPING_KEY_LEN]={0};
    size_t used=0,limit=format==ZFS_KEYFORMAT_RAW?sizeof(key)+1:sizeof(material);
    if(!error&&format==ZFS_KEYFORMAT_PASSPHRASE)
        error=passphrase_file(&stream,1,salt,iterations,key);
    while(!error&&format!=ZFS_KEYFORMAT_PASSPHRASE&&used<limit){
        size_t actual=0;error=receive_reader(&stream,material+used,limit-used,&actual);
        if(error||!actual)break;
        if(actual>limit-used){error=EIO;break;}
        used+=actual;
        if(format==ZFS_KEYFORMAT_HEX){uint8_t *lf=memchr(material,'\n',used);if(lf){used=lf-material;break;}}
    }
    if(!error&&format==ZFS_KEYFORMAT_RAW){
        if(used!=sizeof(key))error=EINVAL;else memcpy(key,material,sizeof(key));
    }else if(!error&&format==ZFS_KEYFORMAT_HEX){
        if(used!=sizeof(key)*2)error=EINVAL;
        else for(size_t i=0;i<sizeof(key);i++){
            int hi=key_hex_digit(material[2*i]),lo=key_hex_digit(material[2*i+1]);
            if(hi<0||lo<0){error=EINVAL;break;}key[i]=(uint8_t)((hi<<4)|lo);
        }
    }
    key_clear(material,sizeof(material));
    if(stream.native){int e=oz_stream_file_close(stream.native);if(!error)error=e;}
    if(stream.local)oz_dos_stream_close(stream.local);
    if(lock)UnLock(lock);
    if(!error){
        /* Persist no source filename or secret in UI preferences. Explicit
         * later key loads supply their own path; upstream location is prompt. */
        fnvlist_add_string(props,"keylocation","prompt");
        nvlist_t *hidden=fnvlist_alloc();fnvlist_add_uint8_array(hidden,"wkeydata",key,sizeof(key));
        fnvlist_add_nvlist(input,"hidden_args",hidden);
        uint8_t *copy=NULL;uint_t n=0;
        if(!nvlist_lookup_uint8_array(hidden,"wkeydata",&copy,&n))key_clear(copy,n);
        fnvlist_free(hidden);
    }
    key_clear(key,sizeof(key));return error;
}
static int authenticated_create(struct oz_message *m,const struct oz_packet_security *security)
{
    if(!ready||!security)return EACCES;
    if(!fixture_writes)return EROFS;
    cred_t *caller=oz_packet_security_cred(security);
    if(!m->argument[0])return oz_zpl_dataset_create_prepared(m->target,m->options,caller,NULL,NULL);
    if(dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    char parent[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(parent,m->target,sizeof(parent))>=sizeof(parent))return ENAMETOOLONG;
    char *slash=strrchr(parent,'/');if(!slash)return EINVAL;*slash=0;
    int error=oz_dataset_create_perms(parent,caller);if(error)return error;
    spa_t *spa=NULL;error=spa_open(parent,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa))error=EROFS;else if(spa_suspended(spa))error=EAGAIN;
    spa_close(spa,FTAG);if(error)return error;
    struct create_key_context context={m,security};
    return oz_zpl_dataset_create_prepared(m->target,m->options,caller,prepare_create_key,&context);
}
static int key_change_options(const char *text,uint64_t *format,uint64_t *iters,
    char *location,size_t capacity)
{
    char options[sizeof(((struct oz_message *)0)->options)];
    unsigned seen=0;
    *format=*iters=0;location[0]=0;
    if(strlen(text)>=sizeof(options))return EINVAL;
    strcpy(options,text);
    for(char *item=options;*item;){
        char *end=strchr(item,';');if(end)*end=0;
        char *value=strchr(item,'=');if(!value||value==item||!value[1])return EINVAL;
        *value++=0;unsigned bit;
        if(!strcmp(item,"keyformat")){
            bit=1;
            if(!strcmp(value,"raw"))*format=ZFS_KEYFORMAT_RAW;
            else if(!strcmp(value,"hex"))*format=ZFS_KEYFORMAT_HEX;
            else if(!strcmp(value,"passphrase"))*format=ZFS_KEYFORMAT_PASSPHRASE;
            else return EINVAL;
        }else if(!strcmp(item,"pbkdf2iters")){
            bit=2;
            if(strspn(value,"0123456789")!=strlen(value))return EINVAL;
            errno=0;*iters=strtoull(value,NULL,10);
            if(errno||*iters<MIN_PBKDF2_ITERATIONS||*iters>UINT32_MAX)return EINVAL;
        }else if(!strcmp(item,"keylocation")){
            bit=4;
            if(strlen(value)>=capacity||!zfs_prop_valid_keylocation(value,B_TRUE))return EINVAL;
            strcpy(location,value);
        }else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;
        if(!end)break;
        item=end+1;if(!*item)return EINVAL;
    }
    return 0;
}
static int authenticated_key_file(struct oz_message *m,const struct oz_packet_security *security)
{
    if(!ready)return ENXIO;
    if(!security)return EACCES;
    int change=m->operation==64;
    int check_only=!change&&(m->flags&OZ_DRYRUN);
    int inherit=change&&!strcmp(m->options,"inherit=on");
    if((m->operation!=62&&!change)||
        (m->flags!=OZ_CONFIRMED&&m->flags!=(OZ_CONFIRMED|(!change?OZ_DRYRUN:0)))||
        (inherit?m->argument[0]!=0:(!m->argument[0]||(!change&&m->options[0]))))return ENOTSUP;
    if(dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    cred_t *caller=oz_packet_security_cred(security);
    int error=oz_dataset_property_perms(m->target,change?"change-key":"load-key",caller);
    if(error)return error;
    spa_t *spa=NULL;
    error=spa_open(m->target,&spa,FTAG);
    if(error)return error;
    if(spa_suspended(spa))error=EAGAIN;
    else if(change&&(!fixture_writes||!spa_writeable(spa)))error=EROFS;
    spa_close(spa,FTAG);
    if(error)return error;
    if(inherit){
        /* Upstream checks root/clone eligibility and both loaded keys;
         * never substitute FORCE_INHERIT or read a key under service identity. */
        dsl_crypto_params_t *params=NULL;
        error=dsl_crypto_params_create_nvlist(DCP_CMD_INHERIT,NULL,NULL,&params);
        if(!error)error=spa_keystore_change_key(m->target,params);
        if(params)dsl_crypto_params_free(params,!!error);
        return error;
    }
    uint64_t requested_format=0,requested_iters=0;
    char keylocation[sizeof(m->options)]={0};
    if(change){
        error=key_change_options(m->options,&requested_format,&requested_iters,keylocation,sizeof(keylocation));
        if(error)return error;
    }
    /* keyformat is computed from the upstream crypto object, not an ordinary
     * inherited DSL property. Do not guess a format from key-file contents. */
    objset_t *os=NULL;nvlist_t *props=NULL,*entry=NULL;uint64_t format=0,salt=0,iterations=0;
    error=dmu_objset_hold(m->target,FTAG,&os);
    if(!error){
        props=fnvlist_alloc();dmu_objset_stats(os,props);
        error=nvlist_lookup_nvlist(props,"keyformat",&entry);
        if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&format);
        if(!error&&format==ZFS_KEYFORMAT_PASSPHRASE){
            error=nvlist_lookup_nvlist(props,"pbkdf2salt",&entry);
            if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&salt);
            if(!error)error=nvlist_lookup_nvlist(props,"pbkdf2iters",&entry);
            if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&iterations);
        }
        dmu_objset_rele(os,FTAG);fnvlist_free(props);
    }
    if(error)return error;
    if(change&&requested_format)format=requested_format;
    if(format!=ZFS_KEYFORMAT_RAW&&format!=ZFS_KEYFORMAT_HEX&&format!=ZFS_KEYFORMAT_PASSPHRASE)return ENOTSUP;
    if(change){
        salt=iterations=0;
        if(requested_iters&&format!=ZFS_KEYFORMAT_PASSPHRASE)return EINVAL;
    }
    if(change&&format==ZFS_KEYFORMAT_PASSPHRASE){
        error=random_get_bytes((uint8_t *)&salt,sizeof(salt));
        if(error)return error;
        if(!salt)return EIO; /* upstream rejects a zero replacement salt */
        iterations=requested_iters?requested_iters:DEFAULT_PBKDF2_ITERATIONS;
    }
    BPTR lock=0;struct receive_input stream={0};
    error=ENODEV;
    for(unsigned i=0;i<mount_count&&error==ENODEV;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending)
        error=oz_dos_stream_open(mounts[i].mount,security,m->argument,&stream.local);
    if(error==ENODEV){
        error=oz_stream_path_lock(security,m->argument,&lock);
        if(!error)error=oz_stream_file_open(security,&lock,secAt_Read,&stream.native);
    }
    /* Match libzfs: raw consumes exact bytes through EOF; hex consumes the
     * first line and strips LF only. Reject excess length without allocating
     * an unbounded line. No key material enters retained request/job buffers. */
    uint8_t material[WRAPPING_KEY_LEN*2+1]={0},key[WRAPPING_KEY_LEN]={0};
    size_t used=0,limit=format==ZFS_KEYFORMAT_RAW?WRAPPING_KEY_LEN+1:sizeof(material);
    if(!error&&format==ZFS_KEYFORMAT_PASSPHRASE)
        error=passphrase_file(&stream,change,salt,iterations,key);
    while(!error&&format!=ZFS_KEYFORMAT_PASSPHRASE&&used<limit){
        size_t actual=0;error=receive_reader(&stream,material+used,limit-used,&actual);
        if(error||!actual)break;
        if(actual>limit-used){error=EIO;break;}
        used+=actual;
        if(format==ZFS_KEYFORMAT_HEX){
            uint8_t *lf=memchr(material,'\n',used);
            if(lf){used=(size_t)(lf-material);break;}
        }
    }
    if(!error&&format!=ZFS_KEYFORMAT_PASSPHRASE){
        if(format==ZFS_KEYFORMAT_RAW){
            if(used!=sizeof(key))error=EINVAL;
            else memcpy(key,material,sizeof(key));
        }else if(used!=WRAPPING_KEY_LEN*2)error=EINVAL;
        else for(size_t i=0;i<sizeof(key);i++){
            int hi=key_hex_digit(material[2*i]),lo=key_hex_digit(material[2*i+1]);
            if(hi<0||lo<0){error=EINVAL;break;}
            key[i]=(uint8_t)((hi<<4)|lo);
        }
    }
    key_clear(material,sizeof(material));
    if(stream.native){int rc=oz_stream_file_close(stream.native);if(!error)error=rc;}
    if(stream.local)oz_dos_stream_close(stream.local);
    if(lock)UnLock(lock);
    if(!error){
        nvlist_t *hidden=fnvlist_alloc();dsl_crypto_params_t *params=NULL;
        nvlist_t *newprops=change?fnvlist_alloc():NULL;
        if(newprops)fnvlist_add_uint64(newprops,"keyformat",format);
        if(newprops&&keylocation[0])fnvlist_add_string(newprops,"keylocation",keylocation);
        if(newprops&&format==ZFS_KEYFORMAT_PASSPHRASE){
            fnvlist_add_uint64(newprops,"pbkdf2salt",salt);
            fnvlist_add_uint64(newprops,"pbkdf2iters",iterations);
        }
        fnvlist_add_uint8_array(hidden,"wkeydata",key,WRAPPING_KEY_LEN);
        error=dsl_crypto_params_create_nvlist(change?DCP_CMD_NEW_KEY:DCP_CMD_NONE,newprops,hidden,&params);
        fnvlist_free(newprops);
        uint8_t *copy=NULL;uint_t count=0;
        if(!nvlist_lookup_uint8_array(hidden,"wkeydata",&copy,&count))key_clear(copy,count);
        fnvlist_free(hidden);
        if(!error)error=change?spa_keystore_change_key(m->target,params):
            spa_keystore_load_wkey(m->target,params,check_only?B_TRUE:B_FALSE);
        /* A successful check never transfers its wrapping key to the
         * keystore. Match zfs_ioc_load_key's noop ownership cleanup. */
        if(params)dsl_crypto_params_free(params,!!error||check_only);
    }
    key_clear(key,sizeof(key));
    return error;
}

static int send_request(const char *source,const char *from,unsigned flags,const char *token,const char *redact,
    cred_t *caller,int (*write)(void *,const void *,size_t),void *arg)
{
    return token?oz_send_stream_token(source,from,token,caller,write,arg):
        oz_send_stream_redacted(source,from,flags,redact,caller,write,arg);
}
static int authenticated_replication(struct oz_message *m,const struct oz_packet_security *security)
{
    if(!ready)return ENXIO;
    if(!security)return EACCES;
    if(m->operation==78){
        if(m->flags!=OZ_CONFIRMED||m->argument[0]||m->options[0])return ENOTSUP;
        if(!fixture_writes)return EROFS;
        if(dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
        if(mounted_subtree(m->target))return EBUSY;
        return oz_receive_abort(m->target,oz_packet_security_cred(security));
    }
    if(m->operation==60){
        if(m->flags!=OZ_CONFIRMED||!m->argument[0])return ENOTSUP;
        const char *from=NULL,*token=NULL,*redact=NULL;
        char options[sizeof(m->options)];
        memcpy(options,m->options,sizeof(options));options[sizeof(options)-1]=0;
        unsigned send_flags=0;
        int option_error=oz_send_options(options,&from,&send_flags,&token,&redact);
        if(option_error)return option_error;
        if(dataset_namecheck(m->target,NULL,NULL)||!strchr(m->target,'@'))return EINVAL;
        option_error=oz_send_redact_validate(m->target,redact);
        if(option_error)return option_error;
        cred_t *caller=oz_packet_security_cred(security);
        int error;
        if(token){struct oz_resume_send resume;
            error=oz_send_resume_prepare(m->target,from,token,caller,&resume);}
        else error=oz_send_perms(m->target,!!(send_flags&OZ_SEND_RAW),caller);
        if(error)return error;
        struct oz_dos_stream *local=NULL;
        error=ENODEV;
        for(unsigned i=0;i<mount_count&&error==ENODEV;i++)
            if(mounts[i].fs&&!mounts[i].cleanup_pending)error=oz_dos_stream_open_output(mounts[i].mount,security,m->argument,&local);
        if(error!=ENODEV){
            if(!error)error=send_request(m->target,from,send_flags,token,redact,caller,oz_dos_stream_write,local);
            if(local){int finish=oz_dos_stream_finish(local,!error);if(!error)error=finish;}
            return error;
        }
        error=0;
        BPTR lock=0;struct oz_stream_file *stream=NULL;
        if(!error)error=oz_stream_path_lock(security,m->argument,&lock);
        if(error==ENOENT){
            struct oz_stream_output *output=NULL;
            error=oz_stream_output_begin(security,m->argument,&output);
            if(!error){
                error=send_request(m->target,from,send_flags,token,redact,caller,oz_stream_output_write,output);
                int finish=oz_stream_output_finish(output,!error);
                if(!error)error=finish;
            }
            return error;
        }
        if(!error)error=oz_stream_file_open_empty_output(security,&lock,&stream);
        if(!error)error=send_request(m->target,from,send_flags,token,redact,caller,send_writer,stream);
        if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
        if(lock)UnLock(lock);
        return error;
    }
    if(m->operation!=61||!(m->flags&OZ_CONFIRMED)||
        (m->flags&~(OZ_CONFIRMED|OZ_FORCE))||!m->argument[0])return ENOTSUP;
    int force=!!(m->flags&OZ_FORCE);
    int resumable=0;
    if(m->options[0]){
        if(!strcmp(m->options,"resumable=on"))resumable=1;
        else if(strcmp(m->options,"resumable=off"))return EINVAL;
    }
    if(!fixture_writes)return EROFS;
    if(dataset_namecheck(m->target,NULL,NULL))return EINVAL;
    char dataset[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(dataset,m->target,sizeof(dataset))>=sizeof(dataset))return ENAMETOOLONG;
    char *snapshot=strchr(dataset,'@');
    if(!snapshot||snapshot==dataset||!snapshot[1])return EINVAL;
    *snapshot++=0;
    cred_t *caller=oz_packet_security_cred(security);
    int error=oz_receive_perms(dataset,force,caller);
    if(error)return error;
    /* The service can manage both writable and read-only imports. Check the
     * actual destination pool before opening or consuming the native input. */
    spa_t *spa=NULL;
    error=spa_open(dataset,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa))error=EAGAIN;
    spa_close(spa,FTAG);
    if(error)return error;
#ifndef OZ_NATIVE_DISPATCHER
    if(mounted_subtree(dataset))return EBUSY;
#endif
    BPTR lock=0;struct receive_input stream={0};zfs_file_t *input=NULL;
    error=ENODEV;
    for(unsigned i=0;i<mount_count&&error==ENODEV;i++)if(mounts[i].fs&&!mounts[i].cleanup_pending)
        error=oz_dos_stream_open(mounts[i].mount,security,m->argument,&stream.local);
    if(error==ENODEV){
        error=oz_stream_path_lock(security,m->argument,&lock);
        if(!error)error=oz_stream_file_open(security,&lock,secAt_Read,&stream.native);
    }
    if(!error)error=zfs_file_from_reader(receive_reader,&stream,&input);
    if(!error)error=oz_receive_stream_options(dataset,snapshot,input,caller,resumable,force);
    if(input)zfs_file_close(input);
    if(stream.native){int rc=oz_stream_file_close(stream.native);if(!error)error=rc;}
    if(stream.local)oz_dos_stream_close(stream.local);
    if(lock)UnLock(lock);
    return error;
}

struct recursive_snapshot_context {
    const char *snapshot;
    nvlist_t *snapshots;
    unsigned count;
};
static int recursive_snapshot_collect(const char *dataset,void *opaque)
{
    struct recursive_snapshot_context *context=opaque;
    if(strchr(dataset,'@'))return 0;
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(snprintf(name,sizeof(name),"%s@%s",dataset,context->snapshot)>=sizeof(name))return ENAMETOOLONG;
    objset_t *os=NULL;int error=dmu_objset_hold(name,FTAG,&os);
    if(error==ENOENT)return 0;
    if(error)return error;
    dmu_objset_rele(os,FTAG);
    fnvlist_add_boolean(context->snapshots,name);context->count++;
    return 0;
}
static int recursive_snapshot_destroy(struct oz_message *m)
{
    char filesystem[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(filesystem,m->target,sizeof(filesystem))>=sizeof(filesystem))return ENAMETOOLONG;
    char *snapshot=strchr(filesystem,'@');
    if(!snapshot||snapshot==filesystem||!snapshot[1])return EINVAL;
    *snapshot++=0;
    objset_t *target=NULL;int error=dmu_objset_hold(m->target,FTAG,&target);
    if(error)return error;
    dmu_objset_rele(target,FTAG);
    struct recursive_snapshot_context context={.snapshot=snapshot,.snapshots=fnvlist_alloc()};
    error=dmu_objset_find(filesystem,recursive_snapshot_collect,&context,DS_FIND_CHILDREN);
    if(!error&&!context.count)error=ENOENT;
#ifdef OZ_NATIVE_DISPATCHER
    nvlist_t *input=NULL;char *packed=NULL,*output=NULL;zfs_cmd_t *zc=NULL;
    struct oz_ioctl_copy_scope *copy=NULL;
    if(!error) {
        input=fnvlist_alloc();fnvlist_add_nvlist(input,"snaps",context.snapshots);
        if(!strcmp(m->options,"defer=on"))fnvlist_add_boolean(input,"defer");
        size_t packed_size=0;error=nvlist_pack(input,&packed,&packed_size,NV_ENCODE_NATIVE,0);
        size_t output_size=1024*1024;
        zc=calloc(1,sizeof(*zc));output=calloc(1,output_size);
        if(!zc||!output)error=ENOMEM;
        if(!error) {
            size_t pool_length=strcspn(filesystem,"/");
            if(pool_length>=sizeof(zc->zc_name))error=ENAMETOOLONG;
            else {
                snprintf(zc->zc_name,sizeof(zc->zc_name),"%.*s",(int)pool_length,filesystem);
                zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=packed_size;
                zc->zc_nvlist_dst=(uintptr_t)output;zc->zc_nvlist_dst_size=output_size;
                struct oz_ioctl_range ranges[]={{packed,packed_size,OZ_IOCTL_INPUT},
                    {output,output_size,OZ_IOCTL_OUTPUT}};
                error=oz_ioctl_copy_enter(ranges,2,&copy);
                if(!error)error=zfsdev_ioctl_common(ZFS_IOC_DESTROY_SNAPS-ZFS_IOC_FIRST,zc,FKIOCTL);
            }
        }
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    free(output);free(zc);free(packed);if(input)fnvlist_free(input);
#else
    if(!error)error=ENOTSUP;
#endif
    fnvlist_free(context.snapshots);
    return error;
}
struct recursive_dataset_destroy_context {
    cred_t *caller;
    char **datasets;
    unsigned count,capacity,snapshots;
    int error;
};
static int recursive_dataset_destroy_collect(const char *name,void *opaque)
{
    struct recursive_dataset_destroy_context *context=opaque;
    if(strchr(name,'@')) {
        context->snapshots++;
        if(!context->error)context->error=EEXIST;
        return 0;
    }
    if(context->error)return 0;
    int error=zfs_secpolicy_destroy_perms(name,context->caller);
    if(error){context->error=error;return 0;}
    if(context->count==context->capacity) {
        unsigned capacity=context->capacity?context->capacity*2:8;
        char **datasets=realloc(context->datasets,capacity*sizeof(*datasets));
        if(!datasets){context->error=ENOMEM;return 0;}
        context->datasets=datasets;context->capacity=capacity;
    }
    size_t length=strlen(name)+1;char *copy=malloc(length);
    if(!copy){context->error=ENOMEM;return 0;}
    memcpy(copy,name,length);context->datasets[context->count++]=copy;
    return 0;
}
static int recursive_dataset_destroy(struct oz_message *m,cred_t *caller)
{
    struct recursive_dataset_destroy_context context={.caller=caller};
    int error=dmu_objset_find(m->target,recursive_dataset_destroy_collect,&context,
        DS_FIND_CHILDREN|DS_FIND_SNAPSHOTS);
    if(!error)error=context.error;
    unsigned completed=0;
    if(error) {
        if(context.snapshots)snprintf(m->detail,sizeof(m->detail),
            "Authenticated recursive dataset destroy refused before mutation snapshots=%u completed=0 errno=%d",
            context.snapshots,error);
        else snprintf(m->detail,sizeof(m->detail),
            "Authenticated recursive dataset destroy preflight completed=0 errno=%d",error);
    } else {
        for(;completed<context.count;completed++) {
            error=dsl_destroy_head(context.datasets[completed]);
            if(error)break;
        }
        snprintf(m->detail,sizeof(m->detail),
            error?"Authenticated recursive dataset destroy stopped completed=%u errno=%d; inspect the live tree before retrying":"Authenticated recursive dataset destroy completed=%u errno=%d",
            completed,error);
    }
    for(unsigned i=0;i<context.count;i++)free(context.datasets[i]);
    free(context.datasets);return error;
}
static int dataset_lifecycle(struct oz_message *m,int authenticated,cred_t *caller)
{
    const char *name=m->target;
    int snapshot=strchr(name,'@')!=NULL;
    int deferred=m->operation==38&&!strcmp(m->options,"defer=on");
    int recursive=(m->flags&OZ_RECURSIVE)!=0;
    if(recursive&&(!authenticated||
        (snapshot?(m->operation!=38&&m->operation!=39):m->operation!=38)))return EINVAL;
    if(m->operation==38&&((m->options[0]&&!deferred)||(deferred&&!snapshot)))return EINVAL;
    if(dataset_namecheck(name,NULL,NULL)||strchr(name,'#')||
        (!strchr(name,'/')&&!snapshot))return EINVAL;
    if(mounted_subtree(name)&&!(authenticated&&m->operation==39&&snapshot))return EBUSY;
    if(m->operation==39) {
        const char *dest=m->argument;size_t n=strcspn(name,"/");
        int error=oz_cli_dataset_rename_check(name,dest,recursive);if(error)return error;
        if(snapshot) {
            size_t base=(size_t)(strchr(name,'@')-name);
            if(strncmp(name,dest,base+1))return EXDEV;
        } else {
            if(!strchr(dest,'/'))return EINVAL;
            if(strncmp(name,dest,n)||dest[n]!='/')return EXDEV;
        }
    }
    spa_t *spa=NULL;int rc=spa_open(name,&spa,FTAG);
    if(rc)return rc;
    if(!spa_writeable(spa))rc=EROFS;
    spa_close(spa,FTAG);
    if(rc)return rc;
    if(recursive) {
#ifdef OZ_NATIVE_DISPATCHER
        if(m->operation==38)return snapshot?recursive_snapshot_destroy(m):
            recursive_dataset_destroy(m,caller);
        /* Caller credentials are already scoped by authenticated lifecycle.
         * Use upstream recursive traversal and its per-snapshot detach path. */
        zfs_cmd_t *zc=calloc(1,sizeof(*zc));if(!zc)return ENOMEM;
        strlcpy(zc->zc_name,name,sizeof(zc->zc_name));
        strlcpy(zc->zc_value,m->argument,sizeof(zc->zc_value));zc->zc_cookie=1;
        rc=zfsdev_ioctl_common(ZFS_IOC_RENAME-ZFS_IOC_FIRST,zc,FKIOCTL);
        free(zc);return rc;
#else
        return ENOTSUP;
#endif
    }
    if(m->operation==39&&snapshot) {
        char filesystem[ZFS_MAX_DATASET_NAME_LEN];
        const char *at=strchr(name,'@');size_t length=(size_t)(at-name);
        if(length>=sizeof(filesystem))return ENAMETOOLONG;
        memcpy(filesystem,name,length);filesystem[length]=0;
        /* Authorization and writable-pool validation have already succeeded.
         * Single-snapshot rename follows upstream unmount semantics, but must
         * propagate busy DOS references instead of stranding an old name. */
        rc=zfsctl_snapshot_unmount(name,0);
        if(rc)return rc;
        return dsl_dataset_rename_snapshot(filesystem,at+1,strchr(m->argument,'@')+1,B_FALSE);
    }
    return m->operation==39?dsl_dir_rename(name,m->argument):
        snapshot?dsl_destroy_snapshot(name,deferred):dsl_destroy_head(name);
}
static int mount_dataset_mode(struct oz_message *,int,int);
static int unmount_dataset(const char *,int);
static int snapshot_holds(struct oz_message *,cred_t *);
static int snapshot_clone(struct oz_message *m)
{
    int rc=0;size_t poollen=strcspn(m->target,"/@");
    const char *at=strchr(m->target,'@');
    if(!at||!at[1]||dataset_namecheck(m->target,NULL,NULL)||
        dataset_namecheck(m->argument,NULL,NULL)||strchr(m->argument,'@')||
        strchr(m->argument,'#'))return EINVAL;
    if(strncmp(m->target,m->argument,poollen)||m->argument[poollen]!='/')return EXDEV;
    spa_t *spa=NULL;
    rc=spa_open(m->target,&spa,FTAG);
    if(!rc) {
        if(!spa_writeable(spa))rc=EROFS;
        spa_close(spa,FTAG);
    }
    return rc?rc:dsl_dataset_clone(m->argument,m->target);
}
static int authenticated_rollback(struct oz_message *m,cred_t *caller)
{
    char filesystem[ZFS_MAX_DATASET_NAME_LEN];
    if(dataset_namecheck(m->target,NULL,NULL)||!strchr(m->target,'@'))return EINVAL;
    int error=dmu_fsname(m->target,filesystem);
    if(error)return error;
    error=oz_dataset_rollback_perms(filesystem,caller);
    if(error)return error;
#ifdef OZ_NATIVE_DISPATCHER
    /* Use the same upstream rollback, mounted suspend/resume and permission
     * checks as C:zfs. Admission executes this on the native DOS owner. */
    nvlist_t *input=fnvlist_alloc();
    fnvlist_add_string(input,"target",m->target);
    char *packed=NULL;size_t size=0;
    error=nvlist_pack(input,&packed,&size,NV_ENCODE_NATIVE,0);
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));
    char *output=calloc(1,4096);
    if(!zc||!output)error=ENOMEM;
    struct oz_cred_scope *credentials=NULL;
    struct oz_ioctl_copy_scope *copy=NULL;
    if(!error)error=oz_cred_push(caller,&credentials);
    if(!error) {
        strlcpy(zc->zc_name,filesystem,sizeof(zc->zc_name));
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
        zc->zc_nvlist_dst=(uintptr_t)output;zc->zc_nvlist_dst_size=4096;
        struct oz_ioctl_range ranges[]={{packed,size,OZ_IOCTL_INPUT},
            {output,4096,OZ_IOCTL_OUTPUT}};
        error=oz_ioctl_copy_enter(ranges,2,&copy);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_ROLLBACK-ZFS_IOC_FIRST,zc,FKIOCTL);
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    free(output);free(zc);free(packed);fnvlist_free(input);
    return error;
#else
    /* The reduced dispatcher lacks the mounted ioctl path. Never hand NULL
     * ownership to upstream while a live mount owns the dataset. */
    if(mounted_subtree(filesystem))return EBUSY;
    spa_t *spa=NULL;
    error=spa_open(filesystem,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    if(error){spa_close(spa,FTAG);return error;}
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error){spa_close(spa,FTAG);return error;}
    /* zil_close may leave an allocated empty tail. Drain it through upstream
     * suspend/destroy before the rollback swaps objsets; synchronous destroy
     * traversal skips unclaimed ZIL blocks on a writable pool. Do not discard
     * an unreplayed log: zil_suspend explicitly refuses that state. NULL does
     * suspend+resume and leaves no long hold obstructing rollback ownership. */
    error=zil_suspend(filesystem,NULL);
    if(!error)txg_wait_synced(spa_get_dsl(spa),0);
    nvlist_t *result=fnvlist_alloc();
    /* Explicit target prevents a racing/newer snapshot from silently becoming
     * the rollback destination. Upstream checks intervening snapshots/bookmarks. */
    if(!error)error=dsl_dataset_rollback(filesystem,m->target,NULL,result);
    fnvlist_free(result);
    VERIFY0(oz_cred_pop(scope));
    spa_close(spa,FTAG);
    return error;
#endif
}
static int authenticated_promote(struct oz_message *m,cred_t *caller)
{
    if(dataset_namecheck(m->target,NULL,NULL)||strpbrk(m->target,"@#%"))return EINVAL;
    char origin[ZFS_MAX_DATASET_NAME_LEN],conflict[ZFS_MAX_DATASET_NAME_LEN]={0};
    int error=oz_dataset_promote_perms(m->target,caller,origin);
    if(error)return error;
    char *at=strchr(origin,'@');
    if(at)*at=0;
#ifdef OZ_NATIVE_DISPATCHER
    /* The upstream AROS promotion path preflights all published snapshots
     * and propagates native detach failures after authorization. */
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));
    if(!zc)return ENOMEM;
    strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(!error)error=zfsdev_ioctl_common(ZFS_IOC_PROMOTE-ZFS_IOC_FIRST,zc,FKIOCTL);
    strlcpy(conflict,zc->zc_string,sizeof(conflict));
    if(scope)VERIFY0(oz_cred_pop(scope));
    free(zc);
#else
    /* Reduced dispatcher still requires every affected volume unmounted. */
    if(mounted_subtree(m->target)||mounted_subtree(origin))return EBUSY;
    spa_t *spa=NULL;
    error=spa_open(m->target,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    struct oz_cred_scope *scope=NULL;
    if(!error)error=oz_cred_push(caller,&scope);
    if(!error)error=dsl_dataset_promote(m->target,conflict);
    if(scope)VERIFY0(oz_cred_pop(scope));
    spa_close(spa,FTAG);
#endif
    if(error&&conflict[0]) {
        m->row_count=1;
        strcpy(m->rows[0].kind,"conflict");
        strlcpy(m->rows[0].name,conflict,sizeof(m->rows[0].name));
        strcpy(m->rows[0].state,"snapshot name collision");
    }
    return error;
}
static int authenticated_bookmark(struct oz_message *m,cred_t *caller)
{
    int destroy=m->operation==38;
    const char *name=destroy?m->target:m->argument;
    if(bookmark_namecheck(name,NULL,NULL))return EINVAL;
    int error=oz_bookmark_perms(name,destroy,caller);
    /* Upstream treats bookmarks of a missing filesystem as already destroyed.
     * Stop here rather than racing creation of that unchecked filesystem. */
    if(destroy&&error==ENOENT)return 0;
    if(error)return error;
    nvlist_t *input=fnvlist_alloc(),*errors=fnvlist_alloc();
    if(destroy)fnvlist_add_boolean(input,name);
    else {
        fnvlist_add_string(input,name,m->target);
        if(dsl_bookmark_create_nvl_validate(input))error=EINVAL;
    }
    spa_t *spa=NULL;
    if(!error)error=spa_open(name,&spa,FTAG);
    if(!error&&!spa_writeable(spa))error=EROFS;
    struct oz_cred_scope *scope=NULL;
    if(!error)error=oz_cred_push(caller,&scope);
    if(!error)error=destroy?dsl_bookmark_destroy(input,errors):dsl_bookmark_create(input,errors);
    nvpair_t *pair=nvlist_next_nvpair(errors,NULL);
    if(!error&&pair)error=fnvpair_value_int32(pair);
    if(scope)VERIFY0(oz_cred_pop(scope));
    if(spa)spa_close(spa,FTAG);
    fnvlist_free(input);fnvlist_free(errors);
    return error;
}

struct native_label_identity {
    uint64_t state, vdev_guid, pool_guid;
    int valid;
};

static uint64_t native_label_offset(uint64_t size,unsigned label)
{
    return label*sizeof(vdev_label_t)+(label<VDEV_LABELS/2?
        0:size-VDEV_LABELS*sizeof(vdev_label_t));
}

/* The writer reservation is acquired before this policy check.  That rejects
 * every overlapping native pool binding owned by this service.  Namespace
 * inspection additionally prevents force from clearing an imported member,
 * including a read-only pool whose on-disk state still says EXPORTED. */
static int native_label_policy(nvlist_t *config,int force,int *l2cache,
    char *reason,size_t reason_size)
{
    uint64_t state=0,vdev_guid=0,pool_guid=0;
    if(nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&vdev_guid)||!vdev_guid||
       nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_STATE,&state)||
       state>POOL_STATE_L2CACHE)return EINVAL;
    if(state!=POOL_STATE_SPARE&&state!=POOL_STATE_L2CACHE&&
       (nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&pool_guid)||!pool_guid))
        return EINVAL;
    int resident=0;
    spa_namespace_enter(FTAG);
    if(state==POOL_STATE_SPARE||state==POOL_STATE_L2CACHE) {
        for(spa_t *spa=spa_next(NULL);spa;spa=spa_next(spa))
            if(spa->spa_state!=POOL_STATE_UNINITIALIZED&&spa->spa_root_vdev&&
               spa_lookup_by_guid(spa,vdev_guid,B_TRUE)){resident=1;break;}
    } else if(state==POOL_STATE_EXPORTED)resident=spa_by_guid(pool_guid,0)!=NULL;
    else if(state==POOL_STATE_ACTIVE)resident=spa_by_guid(pool_guid,vdev_guid)!=NULL;
    spa_namespace_exit(FTAG);
    if(resident) {
        snprintf(reason,reason_size,"Native label clear refused: label belongs to a currently imported pool member");
        return EBUSY;
    }
    if((state==POOL_STATE_ACTIVE||state==POOL_STATE_EXPORTED)&&!force) {
        snprintf(reason,reason_size,"Native label clear refused: %s pool membership requires Force",
            state==POOL_STATE_EXPORTED?"exported":"potentially-active");
        return EPERM;
    }
    if(state==POOL_STATE_L2CACHE)*l2cache=1;
    return 0;
}

static int native_label_clear(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller),written=0,valid=0,l2cache=0;
    struct oz_device_path path;
    struct oz_device *device=NULL;
    unsigned char *labels=NULL,*header=NULL;
    struct native_label_identity identities[VDEV_LABELS]={0};
    struct native_label_identity identity={0};
    if(error)return error;
    if((m->flags!=OZ_CONFIRMED&&m->flags!=(OZ_CONFIRMED|OZ_FORCE))||
       m->argument[0]||m->options[0]||m->cursor||m->job_id)return ENOTSUP;
    error=oz_device_path_parse(m->target,&path);
    if(error)return error;
    error=path.partition?
        oz_device_open_partition(path.device,path.unit,path.position,1,&device):
        oz_device_open(path.device,path.unit,1,&device);
    if(error)goto done;
    uint64_t size=P2ALIGN_TYPED(oz_device_size(device),sizeof(vdev_label_t),uint64_t);
    if(size<VDEV_LABEL_START_SIZE+VDEV_LABEL_END_SIZE){error=ENOSPC;goto done;}
    labels=malloc(VDEV_LABELS*sizeof(vdev_label_t));
    if(!labels){error=ENOMEM;goto done;}
    for(unsigned i=0;i<VDEV_LABELS;i++) {
        vdev_label_t *label=(vdev_label_t *)(labels+i*sizeof(*label));
        error=oz_device_transfer(device,label,sizeof(*label),native_label_offset(size,i),0);
        if(error)goto done;
        nvlist_t *config=NULL;
        if(nvlist_unpack(label->vl_vdev_phys.vp_nvlist,
            sizeof(label->vl_vdev_phys.vp_nvlist),&config,0))continue;
        uint64_t state=0,guid=0,pool_guid=0;
        int known=!nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&guid)&&guid&&
            !nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_STATE,&state)&&
            state<=POOL_STATE_L2CACHE;
        if(known&&state!=POOL_STATE_SPARE&&state!=POOL_STATE_L2CACHE)
            known=!nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&pool_guid)&&pool_guid;
        if(known) {
            struct native_label_identity current={state,guid,pool_guid,1};
            if(valid&&(current.state!=identity.state||current.vdev_guid!=identity.vdev_guid||
                current.pool_guid!=identity.pool_guid))error=EINVAL;
            else if(!valid)identity=current;
            identities[i]=current;
            valid++;
            if(!error)error=native_label_policy(config,m->flags&OZ_FORCE,&l2cache,
                    m->detail,sizeof(m->detail));
        }
        nvlist_free(config);
        if(error)goto done;
    }
    if(!valid){error=ENOENT;goto done;}
    for(unsigned i=0;i<VDEV_LABELS;i++)if(identities[i].valid) {
        unsigned char *label=labels+i*sizeof(vdev_label_t);
        memset(label,0,sizeof(vdev_label_t));
        error=oz_device_transfer(device,label+VDEV_SKIP_SIZE,
            sizeof(vdev_label_t)-VDEV_SKIP_SIZE,
            native_label_offset(size,i)+VDEV_SKIP_SIZE,1);
        if(error)goto done;
        written++;
    }
    if(l2cache) {
        unsigned sector=oz_device_sector(device);
        if(VDEV_LABEL_START_SIZE>size||sector>size-VDEV_LABEL_START_SIZE){error=ENOSPC;goto done;}
        header=malloc(sector);if(!header){error=ENOMEM;goto done;}
        error=oz_device_transfer(device,header,sector,VDEV_LABEL_START_SIZE,0);
        if(error)goto done;
        memset(header,0,MIN((size_t)SPA_MINBLOCKSIZE,(size_t)sector));
        error=oz_device_transfer(device,header,sector,VDEV_LABEL_START_SIZE,1);
        if(error)goto done;
    }
    error=oz_device_flush(device);if(error)goto done;
    for(unsigned i=0;i<VDEV_LABELS;i++)if(identities[i].valid) {
        unsigned char *label=labels+i*sizeof(vdev_label_t);
        error=oz_device_transfer(device,label,sizeof(vdev_label_t),native_label_offset(size,i),0);
        if(error)goto done;
        for(size_t n=VDEV_SKIP_SIZE;n<sizeof(vdev_label_t);n++)
            if(label[n]){error=EIO;goto done;}
    }
    if(l2cache) {
        unsigned sector=oz_device_sector(device);
        error=oz_device_transfer(device,header,sector,VDEV_LABEL_START_SIZE,0);
        if(error)goto done;
        for(size_t n=0;n<MIN((size_t)SPA_MINBLOCKSIZE,(size_t)sector);n++)
            if(header[n]){error=EIO;goto done;}
    }
    snprintf(m->detail,sizeof(m->detail),
        "Cleared and verified %d native ZFS label slots%s",written,
        l2cache?" and the L2ARC header":"");
done:
    if(error&&!m->detail[0]) {
        if(written)snprintf(m->detail,sizeof(m->detail),
            "Native label clear failed after %d label writes; inspect before retrying (errno=%d)",
            written,error);
        else snprintf(m->detail,sizeof(m->detail),
            "Native label clear failed before writes (errno=%d)",error);
    }
    free(header);free(labels);oz_device_close(device);
    return error;
}

static int authenticated_lifecycle(struct oz_message *m,cred_t *caller,const struct oz_packet_security *security)
{
    if(!ready)return ENXIO;
    if(!caller)return EACCES;
    if(m->operation==63){
        if(m->flags!=OZ_CONFIRMED||m->argument[0]||m->options[0])return ENOTSUP;
        if(dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
        int error=oz_dataset_property_perms(m->target,"load-key",caller);
        if(error)return error;
        if(mounted_subtree(m->target))return EBUSY;
        spa_t *spa=NULL;
        error=spa_open(m->target,&spa,FTAG);
        if(error)return error;
        if(spa_suspended(spa))error=EAGAIN;
        spa_close(spa,FTAG);
        if(error)return error;
        /* Keystore state is memory-only: read-only pools also permit unload.
         * Upstream additionally checks inherited users and active key refs. */
        return spa_keystore_unload_wkey(m->target);
    }
    if(m->operation==5)return oz_pool_create(m,caller,security);
    if(m->operation==9)return oz_pool_add(m,caller,security);
    if(m->operation==20)return oz_pool_initialize(m,caller);
    if(m->operation==21)return oz_pool_trim(m,caller);
    if(m->operation==19)return oz_resilver_restart(m,caller);
    if(m->operation==17)return oz_pool_clear(m,caller);
    if(m->operation==14) {
        int error=secpolicy_zfs(caller);if(error)return error;
        if(mounted_subtree(m->target))return EBUSY;
        return oz_pool_split(m,caller);
    }
    if(m->operation==11||m->operation==13)return oz_pool_attach(m,caller,security,m->operation==13);
    if(m->operation==7)return oz_pool_import_files(m,caller,security);
    if(m->operation==32)return oz_pool_property_change(m,caller);
    if(m->operation==33)return oz_pool_history(m,caller);
    if(m->operation==22) {
        int error=secpolicy_zfs(caller);if(error)return error;
        if(pool_namecheck(m->target,NULL,NULL)||
            (strcmp(m->argument,"create")&&strcmp(m->argument,"discard")))return EINVAL;
        spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
        if(!spa_writeable(spa))error=EROFS;
        else if(spa_suspended(spa))error=EAGAIN;
        struct oz_cred_scope *scope=NULL;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error)error=!strcmp(m->argument,"create")?spa_checkpoint(m->target):spa_checkpoint_discard(m->target);
        if(scope)VERIFY0(oz_cred_pop(scope));
        spa_close(spa,FTAG);return error;
    }
    if(m->operation==23) {
        int error=secpolicy_zfs(caller);if(error)return error;
        if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
        uint64_t guid=0,*requested=NULL;
        if(m->argument[0]) {
            for(const char *p=m->argument;*p;p++)if(*p<'0'||*p>'9')return EINVAL;
            errno=0;guid=strtoull(m->argument,NULL,10);
            if(errno==ERANGE||!guid)return EINVAL;
            requested=&guid;
        }
        spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
        if(!spa_writeable(spa))error=EROFS;
        else if(spa_suspended(spa))error=EAGAIN;
        struct oz_cred_scope *scope=NULL;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error)error=oz_pool_change_guid(spa,requested);
        if(scope)VERIFY0(oz_cred_pop(scope));
        spa_close(spa,FTAG);return error;
    }
    if(m->operation==29)return native_label_clear(m,caller);
    if(m->operation==24) {
        int error=secpolicy_zfs(caller);if(error)return error;
        if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
        spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);
        if(error)return error;
        if(spa_suspended(spa))error=EAGAIN;
        struct oz_cred_scope *scope=NULL;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error) {
            /* Same state/config exclusion and scan behavior as the upstream
             * ZFS_IOC_POOL_REOPEN. Reopen success is not a health verdict. */
            spa_vdev_state_enter(spa,SCL_NONE);
            spa->spa_scrub_reopen=(!strcmp(m->options,"scrub_restart=off")&&
                dsl_scan_scrubbing(spa->spa_dsl_pool));
            vdev_reopen(spa->spa_root_vdev);
            spa->spa_scrub_reopen=B_FALSE;
            (void)spa_vdev_state_exit(spa,NULL,0);
        }
        if(scope)VERIFY0(oz_cred_pop(scope));
        spa_close(spa,FTAG);return error;
    }
    if(m->operation==26) {
        /* Match ZFS_IOC_POOL_SYNC: no privileged policy, but reject read-only
         * and suspended pools before waiting. A native identity is still
         * required by admission. Mounted datasets are explicitly allowed. */
        if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
        spa_t *spa=NULL;int error=spa_open(m->target,&spa,FTAG);
        if(error)return error;
        if(spa_suspended(spa))error=EAGAIN;
        else if(!spa_writeable(spa))error=EROFS;
        struct oz_cred_scope *scope=NULL;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error) {
            if(m->flags&OZ_FORCE) {
                spa_config_enter(spa,SCL_CONFIG,FTAG,RW_WRITER);
                vdev_config_dirty(spa->spa_root_vdev);
                spa_config_exit(spa,SCL_CONFIG,FTAG);
            }
            txg_wait_synced(spa_get_dsl(spa),0);
        }
        if(scope)VERIFY0(oz_cred_pop(scope));
        spa_close(spa,FTAG);return error;
    }
    if(m->operation==10||m->operation==12||m->operation==15||m->operation==16) {
        int error=secpolicy_zfs(caller);if(error)return error;
        uint64_t vdev_flags=0;
        if(m->options[0]) {
            if(m->operation==15&&!strcmp(m->options,"expand=on"))vdev_flags=ZFS_ONLINE_EXPAND;
            else if(m->operation==16&&!strcmp(m->options,"temporary=on"))vdev_flags=ZFS_OFFLINE_TEMPORARY;
            else return EINVAL;
        }
        if(pool_namecheck(m->target,NULL,NULL)||!m->argument[0])return EINVAL;
        int cancel=m->operation==10&&!strcmp(m->argument,"cancel");
        uint64_t guid=0;
        if(!cancel) {
            for(const char *p=m->argument;*p;p++)if(*p<'0'||*p>'9')return EINVAL;
            errno=0;guid=strtoull(m->argument,NULL,10);
            if(errno==ERANGE||!guid)return EINVAL;
        }
        spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
        if(!spa_writeable(spa))error=EROFS;
        else if(spa_suspended(spa))error=EAGAIN;
        if(!error&&!cancel) {
            spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
            vdev_t *vd=spa_lookup_by_guid(spa,guid,B_TRUE);
            if(!vd)error=ENODEV;
            else if(m->operation!=10&&!vd->vdev_ops->vdev_op_leaf)error=ENOTSUP;
            spa_config_exit(spa,SCL_CONFIG,FTAG);
        }
        struct oz_cred_scope *scope=NULL;
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error) {
            vdev_state_t state;
            /* No force or spare replacement flags.
             * Upstream still applies pool properties such as autoexpand.
             * Upstream refuses an offline that destroys redundancy. Online
             * acceptance is not proof that any ensuing resilver finished. */
            error=cancel?spa_vdev_remove_cancel(spa):m->operation==10?spa_vdev_remove(spa,guid,B_FALSE):
                m->operation==12?spa_vdev_detach(spa,guid,0,B_FALSE):
                m->operation==15?vdev_online(spa,guid,vdev_flags,&state):vdev_offline(spa,guid,vdev_flags);
        }
        if(scope)VERIFY0(oz_cred_pop(scope));
        spa_close(spa,FTAG);return error;
    }
    if(m->operation==18) {
        int error=secpolicy_zfs(caller);
        if(error)return error;
        struct oz_cred_scope *scope=NULL;
        error=oz_cred_push(caller,&scope);
        if(error)return error;
        error=oz_scan_control(m->target,m->argument);
        VERIFY0(oz_cred_pop(scope));
        return error;
    }
    if(m->operation==6||m->operation==8) {
        int error=secpolicy_zfs(caller);
        if(error)return error;
        if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
        error=suspended_export_check(m->target);if(error)return error;
        if(mounted_subtree(m->target))return EBUSY;
        if((error=oz_pool_split_source_busy(m->target)))return error;
        if(m->operation==6) {
            spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);
            if(error)return error;
            if(!spa_writeable(spa))error=EROFS;
            spa_close(spa,FTAG);if(error)return error;
        }
        struct oz_cred_scope *scope=NULL;
        error=oz_cred_push(caller,&scope);
        if(error)return error;
        /* Read-only imports may also be exported. Never force or implicitly
         * detach DOS endpoints; the caller must unmount first. */
        error=m->operation==6?spa_destroy(m->target):spa_export(m->target,NULL,B_FALSE,B_FALSE);
        if(!error){oz_pool_import_release(m->target);oz_pool_split_release(m->target);}
        VERIFY0(oz_cred_pop(scope));
        return error;
    }
    if(m->operation==51||(m->operation==38&&strchr(m->target,'#')))
        return authenticated_bookmark(m,caller);
    if(m->operation==54)return authenticated_rollback(m,caller);
    if(m->operation==53)return authenticated_promote(m,caller);
    int error=m->operation==39?
        zfs_secpolicy_rename_perms(m->target,m->argument,caller):
        m->operation==38?zfs_secpolicy_destroy_perms(m->target,caller):
        (m->operation==43||m->operation==44)?oz_dataset_mount_perms(m->target,caller):
        m->operation==52?oz_snapshot_clone_perms(m->target,m->argument,caller):
        (m->operation==55||m->operation==56)?oz_snapshot_hold_perms(m->target,m->operation==56,caller):ENOTSUP;
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    /* Same service task owns the mount table and runs both transports.
     * Preserve mounted-subtree, same-pool, name and readonly checks. */
    error=m->operation==43?mount_dataset_mode(m,1,!strcmp(m->options,"readonly=off")):
        m->operation==44?unmount_dataset(m->target,1):
        m->operation==52?snapshot_clone(m):
        (m->operation==55||m->operation==56)?snapshot_holds(m,caller):dataset_lifecycle(m,1,caller);
    VERIFY0(oz_cred_pop(scope));
    return error;
}
struct recursive_snapshot_hold_context {
    const char *snapshot;
    const char *tag;
    nvlist_t *holds;
    cred_t *caller;
    int release;
    unsigned count;
};
struct recursive_snapshot_hold_query_context {
    struct oz_message *message;
    const char *snapshot;
    uint64_t index;
    unsigned snapshots;
    int full;
};
static int snapshot_hold_row(struct oz_message *m,const char *snapshot,
    const char *tag,uint64_t created)
{
    if(strlen(tag)>=sizeof(m->rows[0].name)||
       strlen(snapshot)>=sizeof(m->rows[0].parent))return ENAMETOOLONG;
    struct oz_row *r=&m->rows[m->row_count++];
    strcpy(r->kind,"hold");strcpy(r->name,tag);strcpy(r->parent,snapshot);
    strcpy(r->state,"persistent");
    snprintf(r->value,sizeof(r->value),"created-unix=%llu",(unsigned long long)created);
    return 0;
}
static int recursive_snapshot_hold_query_collect(const char *dataset,void *opaque)
{
    struct recursive_snapshot_hold_query_context *context=opaque;
    if(strchr(dataset,'@')||context->full)return 0;
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(snprintf(name,sizeof(name),"%s@%s",dataset,context->snapshot)>=sizeof(name))return ENAMETOOLONG;
    objset_t *os=NULL;int error=dmu_objset_hold(name,FTAG,&os);
    if(error==ENOENT)return 0;
    if(error)return error;
    dmu_objset_rele(os,FTAG);context->snapshots++;
    nvlist_t *holds=fnvlist_alloc();error=dsl_dataset_get_holds(name,holds);
    for(nvpair_t *p=nvlist_next_nvpair(holds,NULL);p&&!error;p=nvlist_next_nvpair(holds,p)) {
        if(context->index++<context->message->cursor)continue;
        if(context->message->row_count==OZ_MAX_ROWS) {
            context->message->next_cursor=context->index-1;context->full=1;break;
        }
        error=snapshot_hold_row(context->message,name,nvpair_name(p),fnvpair_value_uint64(p));
    }
    fnvlist_free(holds);return error;
}
static int recursive_snapshot_hold_collect(const char *dataset,void *opaque)
{
    struct recursive_snapshot_hold_context *context=opaque;
    if(strchr(dataset,'@'))return 0;
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(snprintf(name,sizeof(name),"%s@%s",dataset,context->snapshot)>=sizeof(name))return ENAMETOOLONG;
    objset_t *os=NULL;int error=dmu_objset_hold(name,FTAG,&os);
    if(error==ENOENT)return 0;
    if(error)return error;
    dmu_objset_rele(os,FTAG);
    error=oz_snapshot_hold_perms(name,context->release,context->caller);
    if(error)return error;
    if(context->release) {
        nvlist_t *current=fnvlist_alloc();error=dsl_dataset_get_holds(name,current);
        int exists=!error&&nvlist_exists(current,context->tag);fnvlist_free(current);
        if(error)return error;
        if(!exists)return ENOENT;
        nvlist_t *tags=fnvlist_alloc();fnvlist_add_boolean(tags,context->tag);
        fnvlist_add_nvlist(context->holds,name,tags);fnvlist_free(tags);
    } else fnvlist_add_string(context->holds,name,context->tag);
    context->count++;
    return 0;
}
static int snapshot_holds(struct oz_message *m,cred_t *caller)
{
    if(!strchr(m->target,'@')||dataset_namecheck(m->target,NULL,NULL))return EINVAL;
    nvlist_t *holds=fnvlist_alloc();int rc=0;
    if(m->operation==57) {
        if(m->flags&OZ_RECURSIVE) {
            char filesystem[ZFS_MAX_DATASET_NAME_LEN];
            if(strlcpy(filesystem,m->target,sizeof(filesystem))>=sizeof(filesystem))rc=ENAMETOOLONG;
            char *snapshot=rc?NULL:strchr(filesystem,'@');
            if(!rc) {
                *snapshot++=0;
                struct recursive_snapshot_hold_query_context context={.message=m,.snapshot=snapshot};
                rc=dmu_objset_find(filesystem,recursive_snapshot_hold_query_collect,&context,DS_FIND_CHILDREN);
                if(!rc&&!context.snapshots)rc=ENOENT;
                /* Equality is the exhausted global hold position. A cursor
                 * beyond every live hold is malformed or stale, not EOF. */
                if(!rc&&!context.full&&m->cursor>context.index)rc=EINVAL;
            }
        } else {
            rc=dsl_dataset_get_holds(m->target,holds);
            uint64_t index=0;
            for(nvpair_t *p=nvlist_next_nvpair(holds,NULL);p&&!rc;p=nvlist_next_nvpair(holds,p)) {
                if(index++<m->cursor)continue;
                if(m->row_count==OZ_MAX_ROWS){m->next_cursor=index-1;break;}
                rc=snapshot_hold_row(m,m->target,nvpair_name(p),fnvpair_value_uint64(p));
            }
            if(!rc&&!m->next_cursor&&m->cursor>index)rc=EINVAL;
        }
    } else {
        spa_t *spa=NULL;rc=spa_open(m->target,&spa,FTAG);
        if(!rc){if(!spa_writeable(spa))rc=EROFS;spa_close(spa,FTAG);}
        if(!rc) {
            nvlist_t *errors=fnvlist_alloc();
            if(m->flags&OZ_RECURSIVE) {
                if(!caller)rc=ENOTSUP;
                char filesystem[ZFS_MAX_DATASET_NAME_LEN];
                if(!rc&&strlcpy(filesystem,m->target,sizeof(filesystem))>=sizeof(filesystem))rc=ENAMETOOLONG;
                char *snapshot=rc?NULL:strchr(filesystem,'@');
                if(!rc) {
                    *snapshot++=0;
                    struct recursive_snapshot_hold_context context={.snapshot=snapshot,.tag=m->argument,
                        .holds=holds,.caller=caller,.release=m->operation==56};
                    rc=dmu_objset_find(filesystem,recursive_snapshot_hold_collect,&context,DS_FIND_CHILDREN);
                    if(!rc&&!context.count)rc=ENOENT;
                }
            } else if(m->operation==55)fnvlist_add_string(holds,m->target,m->argument);
            else {
                nvlist_t *tags=fnvlist_alloc();fnvlist_add_boolean(tags,m->argument);
                fnvlist_add_nvlist(holds,m->target,tags);fnvlist_free(tags);
            }
            if(!rc&&m->operation==55) {
                rc=dsl_dataset_user_hold(holds,0,errors);
            } else {
                if(!rc)rc=dsl_dataset_user_release(holds,errors);
            }
            /* Upstream bulk APIs can report per-snapshot errors separately. */
            nvpair_t *error=nvlist_next_nvpair(errors,NULL);
            /* Release reports missing tags as snapshot#tag, not just the
             * snapshot. Treat any per-snapshot error as request failure. */
            if(!rc&&error)rc=fnvpair_value_int32(error);
            fnvlist_free(errors);
        }
    }
    fnvlist_free(holds);
    if(rc){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
    return rc;
}
static int bookmark_list(struct oz_message *m)
{
    if(dataset_namecheck(m->target,NULL,NULL)||strpbrk(m->target,"@#%"))return EINVAL;
    nvlist_t *props=fnvlist_alloc(),*bookmarks=fnvlist_alloc();
    fnvlist_add_boolean(props,"guid");
    fnvlist_add_boolean(props,"createtxg");
    fnvlist_add_boolean(props,"creation");
    int error=dsl_get_bookmarks(m->target,props,bookmarks);
    uint64_t index=0;
    for(nvpair_t *pair=nvlist_next_nvpair(bookmarks,NULL);pair&&!error;pair=nvlist_next_nvpair(bookmarks,pair)) {
        if(index++<m->cursor)continue;
        if(m->row_count==OZ_MAX_ROWS){m->next_cursor=index-1;break;}
        struct oz_row *row=&m->rows[m->row_count];
        if(strlen(m->target)+1+strlen(nvpair_name(pair))>=sizeof(row->name)||
            strlen(m->target)>=sizeof(row->parent)){error=ENAMETOOLONG;break;}
        nvlist_t *values=fnvpair_value_nvlist(pair);
        uint64_t guid=fnvlist_lookup_uint64(fnvlist_lookup_nvlist(values,"guid"),ZPROP_VALUE);
        uint64_t txg=fnvlist_lookup_uint64(fnvlist_lookup_nvlist(values,"createtxg"),ZPROP_VALUE);
        uint64_t creation=fnvlist_lookup_uint64(fnvlist_lookup_nvlist(values,"creation"),ZPROP_VALUE);
        strcpy(row->kind,"bookmark");strcpy(row->parent,m->target);
        snprintf(row->name,sizeof(row->name),"%s#%s",m->target,nvpair_name(pair));
        strcpy(row->state,"persistent");
        snprintf(row->value,sizeof(row->value),"guid=%llu createtxg=%llu creation=%llu",
            (unsigned long long)guid,(unsigned long long)txg,(unsigned long long)creation);
        m->row_count++;
    }
    fnvlist_free(bookmarks);fnvlist_free(props);
    /* Equality is the exhausted live bookmark position. A larger cursor is
     * malformed or stale and must not silently resemble an empty dataset. */
    if(!error&&!m->next_cursor&&m->cursor>index)error=EINVAL;
    if(error){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
    return error;
}
ULONG oz_engine_signals(void)
{
    ULONG mask=0;
    for(unsigned i=0;i<mount_count;i++)mask|=oz_dos_signals(mounts[i].mount);
    for(struct retired_mount *r=retired_mounts;r;r=r->next)mask|=oz_dos_signals(r->mount);
    return mask;
}
static void retry_failed_publications(void)
{
    if(FindTask(NULL)!=engine_owner)return;
    for(unsigned i=0;i<mount_count;) {
        if(mounts[i].cleanup_pending&&mounts[i].fs) {
            if(!oz_zpl_close(mounts[i].fs)) {
                int retain_endpoint=mounts[i].mount!=NULL;
                mounts[i].fs=NULL;
                mount_entry_remove(i,retain_endpoint);
                continue;
            }
        }
        i++;
    }
}
void oz_engine_pump(void)
{
    retry_failed_publications();
    for(unsigned i=0;i<mount_count;) {
        if(mounts[i].mount) {
            oz_dos_pump(mounts[i].mount);
            /* Ended mounts reject I/O while their old references drain. Retain
             * late-packet endpoints just as ordinary unmount does. Busy teardown
             * is retried on later service events, never forced through. */
            if(mounts[i].fs && oz_zpl_ended(mounts[i].fs) &&
                !oz_dos_detach(mounts[i].mount)) {
                mounts[i].cleanup_pending=1;
                if(!oz_zpl_close(mounts[i].fs)) {
                    mounts[i].fs=NULL;
                    mount_entry_remove(i,1);
                    continue;
                }
            }
        }
        i++;
    }
    for(struct retired_mount *r=retired_mounts;r;r=r->next)oz_dos_pump(r->mount);
}
static int mount_dataset_mode_existing(struct oz_message *m,int authenticated,int writable,struct DosList *existing)
{
    retry_failed_publications();
    if(dataset_namecheck(m->target,NULL,NULL))return EINVAL;
    if(mount_cleanup_pending(m->target))return EBUSY;
    if(mounted_index(m->target)>=0)return EEXIST;
    if(!m->argument[0]||strlen(m->argument)>=32||
        strspn(m->argument,"abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")!=strlen(m->argument))return EINVAL;
    /* A caller may hold the DOS publication lock while awaiting this reply.
     * Reject that known contention before opening writable storage: closing
     * a failed writable mount can sync a TXG, whose raw writes need the lock
     * held by the waiting caller. This is only a preflight; mount_impl still
     * checks publication immediately before inserting the DOS entries. */
    int publication=existing?oz_device_startup_publication_begin():oz_device_publication_begin();
    if(publication)return publication;
    if(existing)oz_device_startup_publication_end();else oz_device_publication_end();
    if(mount_signal<0){mount_signal=AllocSignal(-1);if(mount_signal<0)return ENOMEM;}
    if(mount_count>=INT_MAX||mount_count>=SIZE_MAX/sizeof(*mounts)-1)return ENOMEM;
    struct retired_mount *retirement=oz_mount_registry_calloc(1,sizeof(*retirement));
    if(!retirement)return ENOMEM;
    struct mount_entry *grown=oz_mount_registry_realloc(mounts,(mount_count+1)*sizeof(*mounts));
    if(!grown){free(retirement);return ENOMEM;}
    mounts=grown;unsigned i=mount_count;memset(&mounts[i],0,sizeof(mounts[i]));
    struct oz_zpl *fs=NULL;struct oz_mount *mount=NULL;
    fprintf(stderr,"mount: ZPL open %s\n",m->target);
    int rc=writable?oz_zpl_open_writable(m->target,&fs):oz_zpl_open(m->target,&fs);
    fprintf(stderr,"mount: ZPL open rc=%d, registering DOS %s\n",rc,m->argument);
    if(!rc)rc=existing?oz_dos_mount_adopt(fs,m->argument,existing,&mount,authenticated,mount_signal):
        oz_dos_mount_shared(fs,m->argument,&mount,authenticated,mount_signal);
    fprintf(stderr,"mount: DOS registration rc=%d\n",rc);
    if(rc){
        int cleanup=oz_zpl_close(fs);
        if(cleanup) {
            /* DOS publication never succeeded. Keep the already reserved
             * registry slot solely for owner cleanup, not mounted queries.
             * Pool/subtree guards still see the owned filesystem until it
             * drains. Preserve the original publication error. */
            mounts[i].fs=fs;mounts[i].cleanup_pending=1;
            mounts[i].retirement=retirement;
            strcpy(mounts[i].dataset,m->target);strcpy(mounts[i].name,m->argument);
            mount_count++;
            fprintf(stderr,"mount: retained failed DOS publication cleanup=%d original=%d\n",cleanup,rc);
        } else free(retirement);
        return rc;
    }
    mounts[i].fs=fs;mounts[i].mount=mount;mounts[i].retirement=retirement;mounts[i].authenticated=authenticated;
    strcpy(mounts[i].dataset,m->target);strcpy(mounts[i].name,m->argument);mount_count++;return 0;
}
static int mount_dataset_mode(struct oz_message *m,int authenticated,int writable)
{return mount_dataset_mode_existing(m,authenticated,writable,NULL);}
static int native_handler_attach(struct oz_handler_attach *q,cred_t *caller)
{
    if(!ready)return ENXIO;
    if(FindTask(NULL)!=engine_owner)return EDEADLK;
    int error=secpolicy_zfs(caller);
    if(!error)error=oz_dataset_mount_perms(q->dataset,caller);
    if(error)return error;
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return ENOMEM;
    strlcpy(m->target,q->dataset,sizeof(m->target));
    strlcpy(m->argument,q->name,sizeof(m->argument));
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(!error)error=mount_dataset_mode_existing(m,1,(q->flags&OZ_HANDLER_WRITABLE)!=0,q->device);
    if(scope)VERIFY0(oz_cred_pop(scope));
    if(!error)q->endpoint=q->device->dol_Task;
    free(m);return error;
}
static int unmount_dataset(const char *dataset,int authenticated)
{
    int i=mounted_index(dataset);if(i<0)return ENOENT;
    if(mounts[i].authenticated&&!authenticated)return EPERM;
    int rc=suspended_dataset_check(dataset);
    if(!rc)rc=oz_dos_detach(mounts[i].mount);
    if(!rc) {
        /* The DOS namespace is already detached. Hide this entry from active
         * queries while a failed close is retried by the owner-task pump. */
        mounts[i].cleanup_pending=1;
        rc=oz_zpl_close(mounts[i].fs);
        if(!rc){mounts[i].fs=NULL;mount_entry_remove(i,1);}
    }
    return rc;
}
int oz_snapshot_unmount_preflight(const char *filesystem)
{
    if(!filesystem||dataset_namecheck(filesystem,NULL,NULL)||strpbrk(filesystem,"@#%"))return EINVAL;
    if(!ready||FindTask(NULL)!=engine_owner)return ENODEV;
    size_t length=strlen(filesystem);
    for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&
        !strncmp(mounts[i].dataset,filesystem,length)&&mounts[i].dataset[length]=='@') {
        if(mounts[i].cleanup_pending)return EBUSY;
        int error=oz_dos_detach_check(mounts[i].mount);
        if(error)return error;
    }
    return 0;
}
int zfsctl_snapshot_unmount(const char *name,int flags)
{
    if(!name||dataset_namecheck(name,NULL,NULL)||!strchr(name,'@')||
        (flags&~MNT_FORCE))return EINVAL;
    if(!ready)return ENXIO;
    /* DOS ports and the mount registry belong to this task. Never manipulate
     * them from an async engine worker or bypass their busy-handle checks. */
    if(FindTask(NULL)!=engine_owner)return EDEADLK;
    if(mounted_index(name)<0)return 0;
    return unmount_dataset(name,1);
}
struct dataset_page { struct oz_message *m; uint64_t index; int full; };
static int dataset_row(const char *name,void *arg)
{
    struct dataset_page *page=arg;struct oz_message *m=page->m;
    if(page->index++<m->cursor)return 0;
    if(m->row_count==OZ_MAX_ROWS){m->next_cursor=page->index-1;page->full=1;return ECANCELED;}
    if(strlen(name)>=sizeof(m->rows[0].name))return ENAMETOOLONG;
    objset_t *os;int rc=dmu_objset_hold(name,FTAG,&os);
    if(rc)return rc;
    struct oz_row *r=&m->rows[m->row_count];
    dmu_objset_type_t type=dmu_objset_type(os);
    uint64_t referenced,available,objects,freeobjects;
    dmu_objset_space(os,&referenced,&available,&objects,&freeobjects);
    snprintf(r->kind,sizeof(r->kind),"%s",strchr(name,'@')?"snapshot":type==DMU_OST_ZFS?"filesystem":
        type==DMU_OST_ZVOL?"volume":"DMU dataset");
    snprintf(r->name,sizeof(r->name),"%s",name);
    /* Snapshots belong to their dataset, not the dataset's containing
     * directory. This also gives pool-root snapshots their correct parent. */
    const char *parent_end=strchr(name,'@');
    if(!parent_end)parent_end=strrchr(name,'/');
    if(parent_end)snprintf(r->parent,sizeof(r->parent),"%.*s",(int)(parent_end-name),name);
    int mounted=mounted_index(name);
    if(mounted>=0)snprintf(r->state,sizeof(r->state),"%s mount %s:",
        oz_zpl_readonly(mounts[mounted].fs)?"read-only":"writable",mounts[mounted].name);
    else snprintf(r->state,sizeof(r->state),"%s, not mounted",
        !strchr(name,'@') && spa_writeable(dmu_objset_spa(os))?"writable":"read-only");
    r->used=dsl_get_used(os->os_dsl_dataset);
    snprintf(r->value,sizeof(r->value),"used=%llu referenced=%llu available=%llu objects=%llu",
        (unsigned long long)r->used,(unsigned long long)referenced,
        (unsigned long long)available,(unsigned long long)objects);
    if(mounted>=0) {
        uint64_t reads=0,nread=0;
        int stats=oz_zpl_counter(mounts[mounted].fs,"reads",&reads);
        if(!stats)stats=oz_zpl_counter(mounts[mounted].fs,"nread",&nread);
        size_t length=strlen(r->value);
        if(stats)snprintf(r->value+length,sizeof(r->value)-length,
            " mount-read-stats unavailable errno=%d",stats);
        else snprintf(r->value+length,sizeof(r->value)-length,
            " mount-reads=%llu mount-read-bytes=%llu",
            (unsigned long long)reads,(unsigned long long)nread);
    }
    r->available=available; /* used is upstream accounting, not referenced */
    dmu_objset_rele(os,FTAG);m->row_count++;return 0;
}
/* Diagnostic file-backed import boundary, not a generic device scanner.
 * Only the owned fixture directory named by export.nvlist may supply vdevs. */
static int fixture_tree(nvlist_t *tree, const char *dir, unsigned depth,
    unsigned *leaves)
{
    const char *type,*path; nvlist_t **children; uint_t count;
    if (depth>8 || nvlist_lookup_string(tree,ZPOOL_CONFIG_TYPE,&type)) return EINVAL;
    if (!strcmp(type,VDEV_TYPE_FILE)) {
        if (nvlist_lookup_string(tree,ZPOOL_CONFIG_PATH,&path)) return EINVAL;
        size_t n=strlen(dir);
        if (strncmp(path,dir,n) || strncmp(path+n,"/disk",5) ||
            path[n+5]<'0' || path[n+5]>'4' || path[n+6]) return EINVAL;
        struct stat st;
        if (stat(path,&st) || !S_ISREG(st.st_mode)) return EINVAL;
        return ++*leaves<=5?0:EINVAL;
    }
    if (strcmp(type,VDEV_TYPE_ROOT) && strcmp(type,VDEV_TYPE_MIRROR) &&
        strcmp(type,VDEV_TYPE_RAIDZ)) return ENOTSUP;
    if (nvlist_exists(tree,ZPOOL_CONFIG_L2CACHE) ||
        nvlist_exists(tree,ZPOOL_CONFIG_SPARES)) return ENOTSUP;
    if (nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&count) ||
        !count || count>5) return EINVAL;
    for (uint_t i=0;i<count;i++) {
        int rc=fixture_tree(children[i],dir,depth+1,leaves);
        if (rc) return rc;
    }
    return 0;
}
static int import_fixture(struct oz_message *m)
{
    const char prefix[]="/SYS/OpenZFS-test-";
    size_t n=sizeof(prefix)-1;
    if (strncmp(m->argument,prefix,n) || strlen(m->argument)!=n+8+14 ||
        strcmp(m->argument+n+8,"/export.nvlist")) return EINVAL;
    for (size_t i=n;i<n+8;i++)
        if (!strchr("0123456789abcdef",m->argument[i])) return EINVAL;
    char dir[64]; memcpy(dir,m->argument,n+8); dir[n+8]=0;
    nvlist_t *config=NULL,*tree=NULL,*props=NULL;
    char *packed=NULL; const char *name=NULL; struct stat st;
    int fd=open(m->argument,O_RDONLY),rc=0;
    if(fd<0)return errno?errno:EIO;
    if(fstat(fd,&st) || !S_ISREG(st.st_mode) || st.st_size<=0 ||
        st.st_size>1024*1024) {rc=EINVAL;goto done;}
    packed=malloc(st.st_size);
    if(!packed){rc=ENOMEM;goto done;}
    if(read(fd,packed,st.st_size)!=st.st_size){rc=EIO;goto done;}
    rc=nvlist_unpack(packed,st.st_size,&config,0);
    if(rc)goto done;
    if(nvlist_lookup_string(config,ZPOOL_CONFIG_POOL_NAME,&name) ||
        strcmp(name,m->target) || strlen(name)>=sizeof(m->rows[0].name) ||
        nvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,&tree)) {rc=EINVAL;goto done;}
    unsigned leaves=0;
    rc=fixture_tree(tree,dir,0,&leaves);
    if(rc || !leaves){if(!rc)rc=EINVAL;goto done;}
    spa_namespace_enter(FTAG);
    boolean_t exists=spa_lookup(name)!=NULL;
    spa_namespace_exit(FTAG);
    if(exists){rc=EEXIST;goto done;}
    props=fnvlist_alloc();
    fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"readonly",strcmp(m->options,"readonly=off")!=0);
    rc=spa_import(m->target,config,props,0);
done:
    close(fd);free(packed);nvlist_free(config);nvlist_free(props);
    return rc;
}
static unsigned native_media_removed(vdev_t *vd)
{
    if(!vd)return 0;
    unsigned changed=0;
    for(unsigned i=0;i<vd->vdev_children;i++)changed+=native_media_removed(vd->vdev_child[i]);
    if((vd->vdev_ops==&vdev_file_ops||vd->vdev_ops==&vdev_disk_ops)&&
        vd->vdev_state!=VDEV_STATE_REMOVED){
        vdev_file_t *file=vd->vdev_tsd;
        if(file&&file->vf_file&&oz_device_media_invalid(file->vf_file->f_device)){
            vd->vdev_remove_wanted=B_TRUE;changed++;
        }
    }
    return changed;
}
static void native_media_notify(void)
{
    /* The monitor owns no device-registry lock/reference here. A busy
     * namespace can be retried on the next poll; never block export on us. */
    if(!spa_namespace_tryenter(FTAG))return;
    for(spa_t *spa=spa_next(NULL);spa;spa=spa_next(spa)){
        if(spa_state(spa)!=POOL_STATE_ACTIVE)continue;
        if(!spa_config_tryenter(spa,SCL_ALL,FTAG,RW_READER))continue;
        unsigned changed=native_media_removed(spa->spa_root_vdev);
        for(int i=0;i<spa->spa_l2cache.sav_count;i++)changed+=native_media_removed(spa->spa_l2cache.sav_vdevs[i]);
        for(int i=0;i<spa->spa_spares.sav_count;i++)changed+=native_media_removed(spa->spa_spares.sav_vdevs[i]);
        if(changed)spa_async_remove_notify(spa);
        spa_config_exit(spa,SCL_ALL,FTAG);
    }
    spa_namespace_exit(FTAG);
}
int oz_engine_start(int writes)
{
    int layout_error = oz_zpl_layout_test();
    if (layout_error) {
        fprintf(stderr, "OpenZFS engine/ZPL ABI mismatch at layout field %d\n", layout_error);
        return (0);
    }
    if (!ready) {
        int identity_error=oz_hostid_policy_start();
        if(identity_error){
            fprintf(stderr,"OpenZFS native host identity rejected: %d\n",identity_error);
            return 0;
        }
        /* Empty cache path cannot name any existing pool cache. Explicit
         * import support must be added before the service opens storage. */
        spa_config_path = "";
        /* Upstream libzpool otherwise skips space maps on read-only import,
         * leaving allocation counters at zero. Management must report actual
         * usage in both modes. This enables reads, never write permission. */
        spa_mode_readable_spacemaps = B_TRUE;
        fixture_writes=writes;
        kernel_init(SPA_MODE_READ|(writes?SPA_MODE_WRITE:0));
        /* Resolve the default only AFTER libzpool startup: cached pools must
         * never appear in the namespace without native device admission. */
        oz_cache_policy_start();
        spa_config_path=(char *)oz_cache_default_path();
        zfs_init();
        int media_error=oz_device_media_start(native_media_notify);
        if(media_error){
            fprintf(stderr,"OpenZFS native media monitor startup failed: %d\n",media_error);
            kernel_fini();zfs_fini();oz_cache_policy_stop();return 0;
        }
#ifdef OZ_NATIVE_DISPATCHER
        int error=zfs_ioctl_native_init();
        if(!error) {
            error=oz_control_start();
            if(error)VERIFY0(zfs_ioctl_native_fini());
        }
        if(error) {
            fprintf(stderr,"OpenZFS native dispatcher startup failed: %d\n",error);
            VERIFY0(oz_device_media_stop());
            kernel_fini();zfs_fini();oz_cache_policy_stop();return 0;
        }
#endif
        engine_owner=FindTask(NULL);
        ready = 1;
    }
    oz_admission_bind_lifecycle(authenticated_lifecycle);
    oz_admission_bind_replication(authenticated_replication);
    oz_admission_bind_key_load(authenticated_key_file);
    oz_admission_bind_create(authenticated_create);
    oz_admission_bind_mounts(native_mount_snapshot);
    oz_admission_bind_handler(native_handler_attach);
    oz_admission_bind_mount_policy(native_mount_policy);
    oz_admission_bind_mount_shares(native_mount_shares);
    return ready;
}
int oz_engine_stop(void)
{
    /* Do not free task-owned ports while a DOS client might reference one.
     * Resident after the first mount until a proven retirement protocol lands. */
    if(mount_count||retired_mounts)return 0;
#ifdef OZ_NATIVE_DISPATCHER
    if(ready) {
        if(engine_owner!=FindTask(NULL))return 0;
        /* Final file references own upstream on-exit/event objects. */
        if(oz_control_stop()!=0)return 0;
        VERIFY0(zfs_ioctl_native_fini());
    }
#endif
    if(mount_signal>=0){FreeSignal(mount_signal);mount_signal=-1;}
    free(mounts);mounts=NULL;
    if (ready) { VERIFY0(oz_device_media_stop()); kernel_fini(); zfs_fini(); oz_pool_import_release(NULL); oz_pool_split_release(NULL); oz_cache_policy_stop(); ready = 0; engine_owner=NULL; }
    return 1;
}
struct recursive_property_context {
    struct oz_message *message;
    uint64_t index;
    int full;
};
static int recursive_property_collect(const char *dataset,void *opaque)
{
    struct recursive_property_context *context=opaque;
    if(strchr(dataset,'@')||context->full)return 0;
    struct oz_message query={0};query.operation=40;
    if(strlcpy(query.target,dataset,sizeof(query.target))>=sizeof(query.target)||
       strlcpy(query.argument,context->message->argument,sizeof(query.argument))>=sizeof(query.argument))
        return ENAMETOOLONG;
    int error=oz_property_get(&query);if(error)return error;
    if(query.row_count!=1)return EPROTO;
    /* Exact queries carry the full long value in detail. Recursive pages
     * cannot carry many such values, so retain the explicit property-long
     * placeholder and byte count; callers can issue an exact query for bytes. */
    if(context->index++<context->message->cursor)return 0;
    if(context->message->row_count==OZ_MAX_ROWS) {
        context->message->next_cursor=context->index-1;context->full=1;return 0;
    }
    context->message->rows[context->message->row_count++]=query.rows[0];
    return 0;
}
static int recursive_property_get(struct oz_message *m)
{
    if(strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    struct recursive_property_context context={.message=m};
    int error=dmu_objset_find(m->target,recursive_property_collect,&context,DS_FIND_CHILDREN);
    /* A token equal to the live dataset count is an exhausted page. A token
     * beyond it is malformed or stale and must not silently resemble EOF. */
    if(!error&&!context.full&&m->cursor>context.index)error=EINVAL;
    return error;
}
struct recursive_all_property_context {
    struct oz_message *message;
    uint64_t dataset;
    uint32_t wanted_dataset;
    uint32_t property_cursor;
    int finished_dataset;
    int done;
};
static int recursive_all_property_collect(const char *dataset,void *opaque)
{
    struct recursive_all_property_context *context=opaque;
    if(strchr(dataset,'@')||context->done)return 0;
    if(context->dataset>UINT32_MAX)return EOVERFLOW;
    uint32_t ordinal=(uint32_t)context->dataset++;
    if(ordinal<context->wanted_dataset)return 0;
    if(ordinal>context->wanted_dataset) {
        if(!context->finished_dataset)return EINVAL;
        context->message->next_cursor=(uint64_t)ordinal<<32;
        context->done=1;return 0;
    }
    struct oz_message query={0};query.operation=40;
    if(strlcpy(query.target,dataset,sizeof(query.target))>=sizeof(query.target) ||
       strlcpy(query.argument,"all",sizeof(query.argument))>=sizeof(query.argument))
        return ENAMETOOLONG;
    query.cursor=context->property_cursor;
    int error=oz_property_get_all(&query);if(error)return error;
    if(!query.row_count)return context->property_cursor?EINVAL:EIO;
    if(query.next_cursor) {
        if(query.next_cursor<=query.cursor||query.next_cursor>UINT32_MAX)return EIO;
        context->message->next_cursor=((uint64_t)ordinal<<32)|query.next_cursor;
        context->done=1;
    } else context->finished_dataset=1;
    context->message->row_count=query.row_count;
    memcpy(context->message->rows,query.rows,sizeof(query.rows));
    return 0;
}
static int recursive_all_property_get(struct oz_message *m)
{
    if(strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    struct recursive_all_property_context context={.message=m,
        .wanted_dataset=(uint32_t)(m->cursor>>32),
        .property_cursor=(uint32_t)m->cursor};
    int error=dmu_objset_find(m->target,recursive_all_property_collect,&context,DS_FIND_CHILDREN);
    /* The only exhausted global position is dataset-count with property zero.
     * Larger ordinals, or a property cursor attached to that end position,
     * are outside the current live view. Per-dataset bounds are checked by
     * oz_property_get_all(). */
    if(!error&&!context.done&&!m->row_count&&
       (context.wanted_dataset>context.dataset||
        (context.wanted_dataset==context.dataset&&context.property_cursor)))
        error=EINVAL;
    return error;
}
void oz_engine_dispatch(struct oz_message *m)
{
    m->engine_available=ready;
    m->row_count=0; m->next_cursor=0; m->job_id=0;
    memset(m->rows,0,sizeof(m->rows));
    memset(m->capability_words,0,sizeof(m->capability_words));
    m->capability_words[0]=(1UL<<1)|(1UL<<2)|(ready?((1UL<<3)|(1UL<<4)|(1UL<<7)|(1UL<<31)):0);
    if(ready)m->capability_words[0]|=(1UL<<5)|(1UL<<8)|(1UL<<18)|(1UL<<24); /* authenticated admission */
    if(ready&&fixture_writes)m->capability_words[0]|=(1UL<<6)|(1UL<<15)|(1UL<<16)|(1UL<<26);
    if(ready)m->capability_words[1]=(1UL<<(36-32))|(1UL<<(40-32))|(1UL<<(43-32))|(1UL<<(44-32));
    if(ready && fixture_writes){m->capability_words[0]|=(1UL<<8)|(1UL<<18);m->capability_words[1]|=(1UL<<(37-32))|(1UL<<(38-32))|(1UL<<(39-32))|(1UL<<(50-32))|(1UL<<(52-32));}
    if(ready)m->capability_words[1]|=1UL<<(57-32);
    if(ready&&fixture_writes)m->capability_words[1]|=(1UL<<(55-32))|(1UL<<(56-32));
    if(ready&&fixture_writes)m->capability_words[1]|=1UL<<(61-32);
    if(ready&&fixture_writes)m->capability_words[2]|=1UL<<(78-64);
    if(ready)m->capability_words[1]|=1UL<<(60-32);
    if(ready)m->capability_words[1]|=1UL<<(63-32);
    if(ready)m->capability_words[1]|=1UL<<(62-32);
    if(ready&&fixture_writes)m->capability_words[2]|=1UL<<(64-64);
    if(ready&&fixture_writes)m->capability_words[1]|=1UL<<(32-32);
    if(ready)m->capability_words[1]|=(1UL<<(33-32))|(1UL<<(35-32));
    if(ready&&fixture_writes)m->capability_words[0]|=(1UL<<23)|(1UL<<19);
    if(ready&&fixture_writes)m->capability_words[0]|=1UL<<22;
    if(ready&&fixture_writes)m->capability_words[0]|=1UL<<12;
    if(ready&&fixture_writes)m->capability_words[0]|=(1UL<<11)|(1UL<<13);
    if(ready&&fixture_writes)m->capability_words[0]|=1UL<<9;
    if(ready&&fixture_writes)m->capability_words[0]|=1UL<<10;
    if(ready&&fixture_writes)m->capability_words[0]|=(1UL<<14)|(1UL<<17)|(1UL<<20)|(1UL<<21);
#ifdef OZ_NATIVE_DISPATCHER
    if(ready&&fixture_writes)m->capability_words[2]|=(1UL<<(65-64))|(1UL<<(66-64));
    if(ready)m->capability_words[2]|=1UL<<(79-64);
    if(ready)m->capability_words[2]|=1UL<<(80-64);
    if(ready)m->capability_words[2]|=(1UL<<(67-64))|(1UL<<(68-64))|(1UL<<(69-64));
#endif
    /* These authenticated admission controls operate on retained requests,
     * not pool data, and remain available in read-only engine mode. */
    if(ready)m->capability_words[2]|=(1UL<<(76-64))|(1UL<<(77-64));
    if(ready&&fixture_writes) {
        /* A writable service owns all three audited transport endpoints.
         * Keep the stable route catalogue authoritative instead of requiring
         * every newly wired operation to duplicate a bit in this legacy
         * read-only capability construction.  Conditional routes (explicit
         * import and labelclear) still have at least one supported form. */
        for(unsigned i=0;i<oz_operation_count;i++) {
            struct oz_message probe={0};probe.operation=oz_operations[i].id;
            enum oz_route route=oz_request_route(&probe);
            if(route!=OZ_ROUTE_INVALID&&route!=OZ_ROUTE_UNAVAILABLE&&
               probe.operation<128)
                m->capability_words[probe.operation/32]|=1UL<<(probe.operation%32);
        }
    }
    if (m->operation==1 || m->operation==2) {
        char operations[2048]="";size_t used=0;
        for(unsigned i=0;i<oz_operation_count;i++) {
            unsigned id=oz_operations[i].id;
            if(id>=128||!(m->capability_words[id/32]&(1UL<<(id%32))))continue;
            int n=snprintf(operations+used,sizeof(operations)-used,"%s%s",used?",":"",oz_operations[i].key);
            if(n<0||(size_t)n>=sizeof(operations)-used)break;
            used+=(size_t)n;
        }
        m->status=OZ_OK;
        snprintf(m->detail,sizeof(m->detail),
            "api.version=%u\nengine.available=%s\nengine=upstream OpenZFS 2.4.4\n"
            "engine.mode=%s\nengine.operations=%s\n"
            "Pool create: authenticated root, typed layout including dRAID, pre-provisioned blank files or explicit native partitions; optional tname=temporary, no force.\n"
            "Pool export: authenticated root admission, unmount first; no force/options.\n"
            "Explicit member import: root admission, topology.devices without groups/ashift, readonly=on/off; destroyed=on selects destroyed pools only. Dedicated files or admitted native extents required; no external writers.\n"
            "Pool set: root admission; comment, delegation, listsnapshots, failmode, ashift, dedup_table_quota, bootfs, explicit cachefile and user properties. Bootfs metadata does not install a boot loader.\n"
            "Scrub: authenticated root admission; start/pause/resume/stop. Query pool.status for completion.\n"
            "Legacy fixture import: target=pool, argument=/SYS/OpenZFS-test-XXXXXXXX/export.nvlist, "
            "options=readonly=on; TEST-WRITE also allows readonly=off. "
            "Confirmation required, dry-run unsupported.\n"
            "Mount: target=ZPL dataset, argument=DOS name, options=readonly=on. "
            "Unmount: target=dataset only. Dynamic mount registry; retired endpoints retained and service stays resident afterward. "
            "No automatic physical-device discovery.",
            (unsigned)OZ_VERSION,ready?"true":"false",fixture_writes?"write-capable":"read-only",
            operations);
    } else if (!ready) {
        m->status=OZ_NO_ENGINE;
        snprintf(m->detail,sizeof(m->detail),"Native engine is not initialized.");
    } else if(m->operation==57||((m->operation==55||m->operation==56)&&fixture_writes)) {
        int query=m->operation==57;
        if(m->options[0]||(query?(m->argument[0]||(m->flags&~(OZ_CONFIRMED|OZ_DRYRUN|OZ_RECURSIVE))):
            (!m->argument[0]||m->cursor||m->flags!=OZ_CONFIRMED))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Snapshot holds: query accepts target, cursor and optional recursive flag; hold/release require confirmation and one tag. No temporary, options or mutation dry-run. Nothing executed.");return;
        }
        int rc=snapshot_holds(m,NULL);
        m->status=rc==EBUSY?OZ_BUSY:rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream snapshot %s errno=%d; rows=%lu. Hold pages are a live view. Mutations remain fixture-only, without general caller authorization.",query?"holds":m->operation==55?"hold":"release",rc,(unsigned long)m->row_count);
    } else if(m->operation==18 && fixture_writes) {
        if(m->flags!=OZ_CONFIRMED||m->options[0]||m->cursor||
            (strcmp(m->argument,"start")&&strcmp(m->argument,"pause")&&
             strcmp(m->argument,"resume")&&strcmp(m->argument,"stop"))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Scrub requires a confirmed pool and start/pause/resume/stop action; no options, dry-run or cursor. Nothing executed.");return;
        }
        int rc=oz_scan_control(m->target,m->argument);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream scrub %s returned errno=%d. Success accepts control, not completion; query pool.status. General management authorization remains unavailable.",m->argument,rc);
    } else if(m->operation==4||m->operation==35) {
        if(!m->target[0]||m->argument[0]||(m->options[0]&&
            !(m->operation==35&&(!strcmp(m->options,"histogram=latency")||!strcmp(m->options,"histogram=size"))))||
            (m->flags&~(OZ_CONFIRMED|OZ_DRYRUN))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Status requires one imported pool; no arguments/options or mutation flags.");return;
        }
        int load=0,rc=oz_pool_status(m,&load);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        if(rc){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
        if(m->operation==35) {
            if(m->options[0]) {
                snprintf(m->detail,sizeof(m->detail),"Upstream %s histograms errno=%d, load errno=%d. Counts are cumulative; bounds are inclusive %s, final bucket saturates. Each page is live, not an atomic pool snapshot. UNAVAILABLE is not zero activity.",!strcmp(m->options,"histogram=size")?"request-size":"latency",rc,load,!strcmp(m->options,"histogram=size")?"bytes":"nanoseconds");
                return;
            }
            snprintf(m->detail,sizeof(m->detail),
                "Upstream cumulative I/O counters errno=%d, load errno=%d. "
                "io-bytes/io-ops: used=reads, available=writes, total=nanoseconds since vdev load. "
                "These are totals, not per-second rates. UNAVAILABLE rows have no runtime sample. "
                "Each page is live; do not sum parent and child counters. Reopen/import can reset counters.",rc,load);
            return;
        }
        snprintf(m->detail,sizeof(m->detail),
            "Upstream SPA/vdev statistics errno=%d, load errno=%d. "
            "Capacity fields are per-vdev allocated/space bytes, not dataset quota availability. "
            "Each page is a live statistics query; no device discovery or import performed.",rc,load);
        if(!rc) {
            size_t used=strlen(m->detail);
            m->detail[used++]='\n';
            (void)oz_scan_status(m->target,m->detail+used,sizeof(m->detail)-used);
            spa_t *spa=NULL;int cr=spa_open(m->target,&spa,FTAG);
            pool_checkpoint_stat_t checkpoint={0};
            if(!cr){cr=spa_checkpoint_get_stats(spa,&checkpoint);spa_close(spa,FTAG);}
            used=strlen(m->detail);
            if(cr==ZFS_ERR_NO_CHECKPOINT)snprintf(m->detail+used,sizeof(m->detail)-used,"\ncheckpoint=none");
            else if(cr)snprintf(m->detail+used,sizeof(m->detail)-used,"\ncheckpoint=unknown errno=%d",cr);
            else snprintf(m->detail+used,sizeof(m->detail)-used,"\ncheckpoint=%s space=%llu since=%llu",
                checkpoint.pcs_state==CS_CHECKPOINT_EXISTS?"exists":"discarding",
                (unsigned long long)checkpoint.pcs_space,(unsigned long long)checkpoint.pcs_start_time);
            pool_removal_stat_t removal={0};spa=NULL;
            int rr=spa_open(m->target,&spa,FTAG);
            if(!rr) {
                spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
                rr=spa_removal_get_stats(spa,&removal);
                spa_config_exit(spa,SCL_CONFIG,FTAG);spa_close(spa,FTAG);
            }
            used=strlen(m->detail);
            if(rr==ENOENT)snprintf(m->detail+used,sizeof(m->detail)-used,"\nremoval=none");
            else if(rr)snprintf(m->detail+used,sizeof(m->detail)-used,"\nremoval=unknown errno=%d",rr);
            else snprintf(m->detail+used,sizeof(m->detail)-used,"\nremoval=%s vdev=%llu copied=%llu total=%llu",
                removal.prs_state==DSS_SCANNING?"running":removal.prs_state==DSS_FINISHED?"finished":
                removal.prs_state==DSS_CANCELED?"canceled":"unknown",
                (unsigned long long)removal.prs_removing_vdev,(unsigned long long)removal.prs_copied,
                (unsigned long long)removal.prs_to_copy);
        }
    } else if(m->operation==31 || m->operation==40) {
        int recursive=m->operation==40&&(m->flags&OZ_RECURSIVE);
        int all=!strcmp(m->argument,"all");
        if(!m->target[0]||!m->argument[0]||strchr(m->argument,',')||
            m->options[0]||(m->cursor&&!recursive&&!all)||
            (m->flags&~(OZ_CONFIRMED|OZ_DRYRUN|(m->operation==40?OZ_RECURSIVE:0)))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Get requires one exact property or all in Argument and no options. All-property and recursive views paginate live. Nothing modified.");return;
        }
        /* Admission may leave a success diagnostic in the request buffer.
         * Property adapters own this response field: exact long values place
         * their bytes here, all-property failures retain their own detail,
         * and every other result is formatted below. Never report a stale
         * validation message as the explanation for an engine error. */
        m->detail[0]=0;
        int rc=recursive&&all?recursive_all_property_get(m):
            all?oz_property_get_all(m):recursive?recursive_property_get(m):oz_property_get(m);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        if(rc){m->row_count=0;memset(m->rows,0,sizeof(m->rows));}
        if(!(all&&rc&&m->detail[0])&&
            (all||recursive||rc||!m->row_count||strcmp(m->rows[0].kind,"property-long")))
            snprintf(m->detail,sizeof(m->detail),"Upstream %sproperty lookup errno=%d; rows=%lu. Numeric values use upstream raw units; enum values use upstream names.%s",
                recursive&&all?"recursive all ":all?"all ":recursive?"recursive ":"",rc,(unsigned long)m->row_count,
                all?" Pages are live; property-long rows require an exact query.":
                recursive?" Pages are live; property-long rows require an exact query.":"");
    } else if(m->operation==43 || m->operation==44) {
        if(!m->target[0]||m->flags!=OZ_CONFIRMED||
            (m->operation==43?strcmp(m->options,"readonly=on")!=0:(m->argument[0]||m->options[0]))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Confirmed read-only mount requires dataset, DOS name and readonly=on; unmount takes only dataset. No force/dry-run supported.");
            return;
        }
        int rc=m->operation==43?mount_dataset_mode(m,0,0):unmount_dataset(m->target,0);
        m->status=rc==EBUSY?OZ_BUSY:rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"DOS %s errno=%d. %s",
            m->operation==43?"mount":"unmount",rc,
            rc?"Operation did not complete.":m->operation==43?
            "Real read-only ZPL file/directory access is active. Writes remain unsupported.":
            "Storage detached; endpoint retained to answer late DOS packets safely.");
    } else if((m->operation==38||m->operation==39) && fixture_writes) {
        int deferred=m->operation==38&&!strcmp(m->options,"defer=on");
        if(m->flags!=OZ_CONFIRMED||m->cursor||
            (m->operation==38?(m->argument[0]||(m->options[0]&&!deferred)||
                (deferred&&!strchr(m->target,'@'))):(!m->argument[0]||m->options[0]))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Confirmed dataset lifecycle only; rename requires destination. Snapshot destroy alone accepts defer=on. No force, recursive or dry-run. Nothing executed.");return;
        }
        int rc=dataset_lifecycle(m,0,NULL);
        m->status=rc==EBUSY?OZ_BUSY:rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream dataset %s errno=%d. Mounted subtrees are rejected; no implicit unmount. General management authorization remains unavailable.",m->operation==39?"rename":"destroy",rc);
    } else if(m->operation==37 && fixture_writes) {
        if(m->flags!=OZ_CONFIRMED||m->argument[0]||m->options[0]||m->cursor) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Create requires a confirmed new filesystem name, no arguments/options/dry-run/cursor. Nothing executed.");return;
        }
        int rc=oz_zpl_dataset_create(m->target,kcred);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream filesystem creation errno=%d. No implicit mount. Default properties only; general management authorization remains unavailable.",rc);
    } else if (m->operation==52 && fixture_writes) {
        if(!m->target[0]||!m->argument[0]||m->options[0]||m->cursor||m->flags!=OZ_CONFIRMED) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Clone requires confirmed source snapshot in Target and destination dataset in Argument. Options/dry-run unsupported; nothing executed.");
            return;
        }
        int rc=snapshot_clone(m);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream snapshot clone returned errno=%d. Destination is not mounted; general management authorization remains unavailable.",rc);
    } else if ((m->operation==8 || m->operation==50) && fixture_writes) {
        int rc=0;
        if(!m->target[0] || m->argument[0] || m->options[0] || m->flags!=OZ_CONFIRMED) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Only a confirmed target without flags/options is supported. Nothing executed.");
            return;
        }
        if(m->operation==8) {
            size_t n=strlen(m->target);
            for(unsigned i=0;i<mount_count;i++)if(mounts[i].fs&&!strncmp(mounts[i].dataset,m->target,n)&&
                (!mounts[i].dataset[n]||mounts[i].dataset[n]=='/'||mounts[i].dataset[n]=='@'))rc=EBUSY;
            if(!rc)rc=oz_pool_split_source_busy(m->target);
            if(!rc)rc=spa_export(m->target,NULL,B_FALSE,B_FALSE);
        }
        else {
            char name[sizeof(m->target)];snprintf(name,sizeof(name),"%s",m->target);
            char *at=strchr(name,'@');
            if(!at || !at[1] || dataset_namecheck(name,NULL,NULL))rc=EINVAL;
            else {
                *at=0;
                spa_t *spa=NULL;
                rc=spa_open(name,&spa,FTAG);
                if(!rc) {
                    if(!spa_writeable(spa))rc=EROFS;
                    spa_close(spa,FTAG);
                }
                if(!rc)rc=dmu_objset_snapshot_one(name,at+1);
            }
        }
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream %s returned errno=%d.",
            m->operation==8?"pool export":"snapshot creation",rc);
    } else if(m->operation==36&&!strcmp(m->options,"bookmarks=on")) {
        if(m->argument[0]||(m->flags&~(OZ_CONFIRMED|OZ_DRYRUN))) {
            m->status=OZ_UNSUPPORTED;
            strcpy(m->detail,"Bookmark list requires one filesystem, no argument or recursion.");return;
        }
        int rc=bookmark_list(m);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Upstream bookmark enumeration errno=%d; live paginated view, not a namespace snapshot.",rc);
    } else if (m->operation==36) {
        if(!m->target[0] || m->argument[0] || (m->options[0] && strcmp(m->options,"snapshots=on")) ||
            (m->flags & ~(OZ_CONFIRMED|OZ_DRYRUN|OZ_RECURSIVE))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Dataset list requires a pool/dataset target; only recursive and safety flags supported.");
            return;
        }
        struct dataset_page page={.m=m};
        int rc;
        if(strchr(m->target,'@')) {
            /* dmu_objset_find traverses filesystem roots, not snapshot
             * names. Validate the leaf even on a page beyond its one row. */
            page.index=m->cursor;
            rc=dataset_row(m->target,&page);
            if(m->cursor){m->row_count=0;memset(m->rows,0,sizeof(m->rows));}
        } else rc=dmu_objset_find(m->target,dataset_row,&page,
            (m->flags&OZ_RECURSIVE?DS_FIND_CHILDREN:0)|(m->options[0]?DS_FIND_SNAPSHOTS:0));
        if(rc==ECANCELED && page.full)rc=0;
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        if(rc){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
        snprintf(m->detail,sizeof(m->detail),
            "Upstream dataset enumeration: errno=%d, rows=%lu. "
            "Referenced bytes are not used bytes; mount state is reported per row. "
            "Pagination is a live namespace view, not a consistent snapshot.",
            rc,(unsigned long)m->row_count);
    } else if (m->operation==7) {
        if(m->topology.device_count) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Explicit file import requires authenticated admission; no import executed.");
            return;
        }
        if (!m->target[0] || !m->argument[0] ||
            (strcmp(m->options,"readonly=on") && (!fixture_writes || strcmp(m->options,"readonly=off"))) ||
            m->flags!=OZ_CONFIRMED) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),
                "Only confirmed fixture import with readonly=on is supported. "
                "Dry-run/force/recursive/raw/resumable are not implemented. Nothing executed.");
            return;
        }
        int rc=import_fixture(m);
        m->status=rc?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),
            "Upstream fixture import (%s): errno=%d. %s",m->options,rc,
            rc?"Import did not complete successfully.":"Pool loaded with the requested access mode.");
    } else if (m->operation==3) {
        /* The UI sends its confirmation/dry-run safety flags with queries.
         * This operation is intrinsically read-only; neither changes it. */
        if (m->argument[0] || m->options[0] ||
            (m->flags & ~(OZ_CONFIRMED|OZ_DRYRUN))) {
            m->status=OZ_UNSUPPORTED;
            snprintf(m->detail,sizeof(m->detail),"Pool list accepts only an optional exact pool name.");
            return;
        }
        spa_t *spa=NULL; uint64_t index=0;
        spa_namespace_enter(FTAG);
        while ((spa=spa_next(spa))!=NULL) {
            if (m->target[0] && strcmp(m->target,spa_name(spa))) continue;
            if (index++ < m->cursor) continue;
            if (m->row_count==OZ_MAX_ROWS) { m->next_cursor=index-1; break; }
            struct oz_row *row=&m->rows[m->row_count++];
            snprintf(row->kind,sizeof(row->kind),"pool");
            snprintf(row->name,sizeof(row->name),"%s",spa_name(spa));
            snprintf(row->state,sizeof(row->state),"%s",spa_state_to_name(spa));
        }
        spa_namespace_exit(FTAG);
        /* Query owned upstream snapshots after dropping the namespace lock.
         * A pool may disappear between enumeration and sampling: report the
         * error, never invent zero capacity or retain a stale numeric sample. */
        unsigned failed=0;
        for(unsigned i=0;i<m->row_count;i++) {
            struct oz_row *row=&m->rows[i];
            nvlist_t *config=NULL,*root=NULL;uint64_t *raw=NULL;uint_t count=0;
            int error=spa_get_stats(row->name,&config,NULL,0);
            if(!error)error=nvlist_lookup_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,&root);
            if(!error)error=nvlist_lookup_uint64_array(root,ZPOOL_CONFIG_VDEV_STATS,&raw,&count);
            vdev_stat_t sample={0};
            if(!error&&!VDEV_STAT_VALID(vs_space,count))error=EINVAL;
            if(!error) {
                memcpy(&sample,raw,MIN(sizeof(sample),(size_t)count*sizeof(uint64_t)));
                if(sample.vs_alloc>sample.vs_space)error=EINVAL;
            }
            if(error) {
                failed++;row->total=row->used=row->available=0;
                snprintf(row->state,sizeof(row->state),"UNAVAILABLE");
                snprintf(row->value,sizeof(row->value),"Capacity unavailable: errno=%d",error);
            } else {
                row->total=sample.vs_space;row->used=sample.vs_alloc;
                row->available=sample.vs_space-sample.vs_alloc;
                snprintf(row->value,sizeof(row->value),
                    "size=%llu allocated=%llu free=%llu bytes",
                    (unsigned long long)row->total,(unsigned long long)row->used,
                    (unsigned long long)row->available);
            }
            nvlist_free(config);
        }
        m->status=failed?OZ_ENGINE_ERROR:OZ_OK;
        snprintf(m->detail,sizeof(m->detail),
            "%lu imported pool(s) in this page; upstream SPA namespace queried. "
            "Pool bytes are raw capacity, not dataset available space. "
            "Statistics failures=%u. Pagination is a live namespace view.",
            (unsigned long)m->row_count,failed);
    } else {
        m->status=OZ_UNSUPPORTED;
        if(!fixture_writes&&(m->operation==8||m->operation==18||m->operation==37||m->operation==38||m->operation==39||m->operation==50||m->operation==52||m->operation==55||m->operation==56))
            snprintf(m->detail,sizeof(m->detail),
                "This operation is unavailable in read-only management mode. "
                "Writable management is currently restricted to explicit test fixtures. Nothing executed.");
        else snprintf(m->detail,sizeof(m->detail),
                "Operation %lu is not wired to the native engine. Nothing executed.",
                (unsigned long)m->operation);
    }
}
