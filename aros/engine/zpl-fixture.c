/* SPDX-License-Identifier: CDDL-1.0 */
/* Initial AROS ZPL boundary: read-only access plus disposable fixture creation.
 * Creation adapts the portable portions of upstream zfs_create_fs/zfs_mknode
 * (module/os/linux/zfs/zfs_znode_os.c). All on-disk allocation, SA registration,
 * ZAP encoding, checksums and pool transactions use the upstream engine.
 * This is NOT a complete vnode/ACL/ZIL adapter or a production formatter.
 * The deliberately public fixture contains root/nested regular files; writes through
 * the DOS frontend remain unsupported until their ZPL semantics are ported.
 */
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/dnode.h>
#include <sys/sa.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_sa.h>
#include <sys/zfs_znode.h>
#include <sys/zap.h>
#include <sys/txg.h>
#include <sys/stat.h>
#include <pthread.h>
#include "zpl-fixture.h"
#include "../zpl/read-mount.h"
struct oz_zpl {struct oz_read_mount *native;struct oz_zpl *failed_next;};
struct failed_mounts {struct oz_zpl *head;};
static pthread_once_t failed_once=PTHREAD_ONCE_INIT;
static pthread_key_t failed_key;
static int failed_key_error;
/* No global task-pointer registry: cleanup stays on the original owner,
 * including pthread teardown, and cannot match a recycled task address. */
static int retry_failed_mounts(struct failed_mounts *pending)
{
    int first=0;
    struct oz_zpl **link=&pending->head;
    while(*link) {
        struct oz_zpl *fs=*link;
        int error=oz_read_mount_close(fs->native);
        if(error){if(!first)first=error;link=&fs->failed_next;}
        else {*link=fs->failed_next;free(fs);}
    }
    return first;
}
static void failed_mounts_destroy(void *value)
{
    struct failed_mounts *pending=value;
    int error=retry_failed_mounts(pending);
    if(error) {
        /* A loan outliving its owner is still a lifecycle defect. Never
         * disown/free its storage or transfer ownership to another task. */
        cmn_err(CE_WARN,"OpenZFS owner exiting with failed mount loans: %d",error);
        return;
    }
    free(pending);
}
static void failed_mounts_initialize(void)
{ failed_key_error=pthread_key_create(&failed_key,failed_mounts_destroy); }
static void failed_mounts_release_empty(struct failed_mounts *pending)
{
    /* Successful opens must not leave a destructor argument pointing back
     * into an otherwise unloadable engine library on a long-lived caller. */
    if(!pending->head&&!pthread_setspecific(failed_key,NULL))free(pending);
}
static int failed_mounts_get(struct failed_mounts **out)
{
    int error=pthread_once(&failed_once,failed_mounts_initialize);
    if(error||failed_key_error)return error?error:failed_key_error;
    struct failed_mounts *pending=pthread_getspecific(failed_key);
    if(!pending) {
        pending=calloc(1,sizeof(*pending));if(!pending)return ENOMEM;
        error=pthread_setspecific(failed_key,pending);
        if(error){free(pending);return error;}
    }
    *out=pending;
    return retry_failed_mounts(pending);
}
static const char fixture_text[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";

static int make_node(objset_t *os,sa_attr_type_t *attrs,uint64_t parent,
    int directory,uint64_t size,uint64_t links,uint64_t *object,dmu_tx_t *tx)
{
    uint64_t mode=(directory?S_IFDIR:S_IFREG)|0777,gen=dmu_tx_get_txg(tx);
    uint64_t uid=0,gid=0,flags=0,count=1;
    uint64_t stamp[2]={(uint64_t)gethrestime_sec(),0};
    zfs_ace_hdr_t ace={ACE_ACCESS_ALLOWED_ACE_TYPE,ACE_EVERYONE,ACE_ALL_PERMS};
    /* No invented wire layout: upstream SA constructs the ZPL bonus. */
    *object=directory?zap_create(os,DMU_OT_DIRECTORY_CONTENTS,DMU_OT_SA,
        DN_BONUS_SIZE(DNODE_MIN_SIZE),tx):dmu_object_alloc(os,
        DMU_OT_PLAIN_FILE_CONTENTS,4096,DMU_OT_SA,DN_BONUS_SIZE(DNODE_MIN_SIZE),tx);
    if(!parent)parent=*object;
    sa_handle_t *handle;int rc=sa_handle_get(os,*object,NULL,SA_HDL_PRIVATE,&handle);
    if(rc)return rc;
    sa_bulk_attr_t a[16];memset(a,0,sizeof(a));int n=0;
#define ADD(which,value,len) SA_ADD_BULK_ATTR(a,n,attrs[which],NULL,value,len)
    ADD(ZPL_MODE,&mode,8);ADD(ZPL_SIZE,&size,8);ADD(ZPL_GEN,&gen,8);
    ADD(ZPL_UID,&uid,8);ADD(ZPL_GID,&gid,8);ADD(ZPL_PARENT,&parent,8);
    ADD(ZPL_FLAGS,&flags,8);ADD(ZPL_ATIME,stamp,16);ADD(ZPL_MTIME,stamp,16);
    ADD(ZPL_CTIME,stamp,16);ADD(ZPL_CRTIME,stamp,16);ADD(ZPL_LINKS,&links,8);
    ADD(ZPL_DACL_COUNT,&count,8);ADD(ZPL_DACL_ACES,&ace,sizeof(ace));
#undef ADD
    rc=sa_replace_all_by_template(handle,a,n,tx);sa_handle_destroy(handle);return rc;
}
static void create_sync(objset_t *os,void *arg,cred_t *cr,dmu_tx_t *tx)
{
    (void)cr;int *result=arg;uint64_t version=ZPL_VERSION,root,file;
    *result=zap_create_claim(os,MASTER_NODE_OBJ,DMU_OT_MASTER_NODE,DMU_OT_NONE,0,tx);
    if(*result)return;
    *result=zap_add(os,MASTER_NODE_OBJ,ZPL_VERSION_STR,8,1,&version,tx);if(*result)return;
    uint64_t sa=zap_create(os,DMU_OT_SA_MASTER_NODE,DMU_OT_NONE,0,tx);
    *result=zap_add(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa,tx);if(*result)return;
    uint64_t unlinked=zap_create(os,DMU_OT_UNLINKED_SET,DMU_OT_NONE,0,tx);
    *result=zap_add(os,MASTER_NODE_OBJ,ZFS_UNLINKED_SET,8,1,&unlinked,tx);if(*result)return;
    sa_attr_type_t *attrs;
    *result=sa_setup(os,sa,zfs_attr_table,ZPL_END,&attrs);if(*result)return;
    *result=make_node(os,attrs,0,1,4,3,&root,tx);if(*result)return;
    *result=zap_add(os,MASTER_NODE_OBJ,ZFS_ROOT_OBJ,8,1,&root,tx);if(*result)return;
    *result=make_node(os,attrs,root,0,sizeof(fixture_text)-1,1,&file,tx);if(*result)return;
    dmu_write(os,file,0,sizeof(fixture_text)-1,fixture_text,tx,0);
    uint64_t dirent=file|((uint64_t)DT_REG<<60);
    *result=zap_add(os,root,"hello.txt",8,1,&dirent,tx);if(*result)return;
    uint64_t subdir;
    *result=make_node(os,attrs,root,1,3,2,&subdir,tx);if(*result)return;
    dirent=subdir|((uint64_t)DT_DIR<<60);
    *result=zap_add(os,root,"subdir",8,1,&dirent,tx);if(*result)return;
    *result=make_node(os,attrs,subdir,0,sizeof(fixture_text)-1,1,&file,tx);if(*result)return;
    dmu_write(os,file,0,sizeof(fixture_text)-1,fixture_text,tx,0);
    dirent=file|((uint64_t)DT_REG<<60);
    *result=zap_add(os,subdir,"nested.txt",8,1,&dirent,tx);
}
int oz_zpl_fixture_create(const char *dataset)
{
    int result=0;int rc=dmu_objset_create(dataset,DMU_OST_ZFS,0,NULL,create_sync,&result);
    return rc?rc:result;
}
int oz_zpl_fixture_create_crypto(const char *dataset,struct dsl_crypto_params *params)
{
    int result=0;int rc=dmu_objset_create(dataset,DMU_OST_ZFS,0,params,create_sync,&result);
    return rc?rc:result;
}
static int open_mode(const char *dataset,int readonly,struct oz_zpl **out)
{
    if(!out)return EINVAL;
    if(!dataset){*out=NULL;return EINVAL;}
    *out=NULL;
    struct failed_mounts *pending=NULL;
    int ready=failed_mounts_get(&pending);if(ready)return ready;
    struct oz_zpl *fs=calloc(1,sizeof(*fs));
    if(!fs){failed_mounts_release_empty(pending);return ENOMEM;}
    int rc=oz_read_mount_open_dataset(dataset,readonly,&fs->native);
    if(rc){
        int cleanup=oz_zpl_close(fs);
        /* A cache with borrowed references must retain its objset rather
         * than be freed underneath callbacks. Preserve the admission error. */
        if(cleanup) {
            fs->failed_next=pending->head;pending->head=fs;
            cmn_err(CE_WARN,"OpenZFS failed admission retained for owner retry: cleanup %d, error %d",cleanup,rc);
        }
        failed_mounts_release_empty(pending);
        return rc;
    }failed_mounts_release_empty(pending);*out=fs;return 0;
}
int oz_zpl_open(const char *dataset,struct oz_zpl **out)
{return open_mode(dataset,1,out);}
int oz_zpl_open_writable(const char *dataset,struct oz_zpl **out)
{return open_mode(dataset,0,out);}
int oz_zpl_sync(struct oz_zpl *fs)
{return fs?oz_read_mount_sync(fs->native):EINVAL;}
int oz_zpl_readonly(struct oz_zpl *fs)
{return !fs||oz_read_mount_readonly(fs->native);}
int oz_zpl_ended(struct oz_zpl *fs)
{return !fs||oz_read_mount_ended(fs->native);}
int oz_zpl_exec(struct oz_zpl *fs)
{return fs&&oz_read_mount_exec(fs->native);}
int oz_zpl_mount_policy(struct oz_zpl *fs,uint32_t *values)
{return fs?oz_read_mount_policy(fs->native,values):EINVAL;}
int oz_zpl_shares_object(struct oz_zpl *fs,uint64_t *object)
{return fs?oz_read_mount_shares_object(fs->native,object):EINVAL;}
int oz_zpl_protection_store(struct oz_zpl *fs,uint64_t object,uint32_t bits,cred_t *cr)
{return fs?oz_read_mount_protection_store(fs->native,object,bits,cr):EINVAL;}
int oz_zpl_comment_get(struct oz_zpl *fs,uint64_t object,char *buffer,size_t capacity)
{return fs?oz_read_mount_comment_get(fs->native,object,buffer,capacity):EINVAL;}
int oz_zpl_comment_store(struct oz_zpl *fs,uint64_t object,const char *comment,cred_t *cr)
{return fs?oz_read_mount_comment_store(fs->native,object,comment,cr):EINVAL;}
int oz_zpl_set_mtime(struct oz_zpl *fs,uint64_t object,int64_t seconds,uint32_t nanos,cred_t *cr)
{return fs?oz_read_mount_set_mtime(fs->native,object,seconds,nanos,cr):EINVAL;}
int oz_zpl_set_owner(struct oz_zpl *fs,uint64_t object,uint32_t uid,uint32_t gid,cred_t *cr)
{return fs?oz_read_mount_set_owner(fs->native,object,uid,gid,cr):EINVAL;}
int oz_zpl_project_get(struct oz_zpl *fs,uint64_t object,uint32_t *id,int *inherit,cred_t *cr)
{return fs?oz_read_mount_project_get(fs->native,object,id,inherit,cr):EINVAL;}
int oz_zpl_project_set(struct oz_zpl *fs,uint64_t object,uint32_t id,int inherit,cred_t *cr)
{return fs?oz_read_mount_project_set(fs->native,object,id,inherit,cr):EINVAL;}
int oz_zpl_close(struct oz_zpl *fs)
{
    if(!fs)return 0;
    int rc=oz_read_mount_close(fs->native);if(rc)return rc;
    free(fs);return 0;
}
int oz_zpl_lookup(struct oz_zpl *fs,const char *name,uint64_t *object,uint64_t *size)
{
    struct oz_zpl_info info;int rc=oz_zpl_lookup_at(fs,oz_zpl_root(fs),name,&info);
    if(!rc&&info.directory)rc=EISDIR;
    if(!rc){*object=info.object;*size=info.size;}return rc;
}
uint64_t oz_zpl_root(struct oz_zpl *fs){return fs?oz_read_mount_root(fs->native):0;}
int oz_zpl_counter(struct oz_zpl *fs,const char *name,uint64_t *out)
{return fs ? oz_read_mount_counter(fs->native,name,out) : EINVAL;}
void oz_zpl_space(struct oz_zpl *fs,uint64_t *referenced,uint64_t *available)
{
    uint64_t objects,freeobjects;
    dmu_objset_space(oz_read_mount_objset(fs->native),referenced,available,&objects,&freeobjects);
}
int oz_zpl_stat(struct oz_zpl *fs,uint64_t object,struct oz_zpl_info *info)
{
    return fs ? oz_read_mount_stat(fs->native,object,info) : EINVAL;
}
int oz_zpl_lookup_at(struct oz_zpl *fs,uint64_t parent,const char *name,struct oz_zpl_info *info)
{
    if(!name||strchr(name,'/')||strchr(name,':'))return ENOTSUP;
    int rc=oz_zpl_stat(fs,parent,info);if(rc)return rc;
    if(!*name)return 0;
    if(!info->directory)return ENOTDIR;
    return oz_read_mount_lookup(fs->native,parent,name,info);
}
int oz_zpl_same_name(struct oz_zpl *fs,const char *a,const char *b,int *same)
{ return fs?oz_read_mount_same_name(fs->native,a,b,same):EINVAL; }
int oz_zpl_nth(struct oz_zpl *fs,uint64_t directory,uint32_t index,char *name,size_t capacity,struct oz_zpl_info *info)
{
    int rc=oz_zpl_stat(fs,directory,info);if(rc)return rc;
    if(!info->directory)return ENOTDIR;
    zap_cursor_t cursor;zap_attribute_t *entry=zap_attribute_long_alloc();
    zap_cursor_init(&cursor,oz_read_mount_objset(fs->native),directory);
    /* DOS FileInfoBlock only has a 32-bit continuation key. Never truncate a
     * 64-bit upstream cursor: replay a stable read-only directory by index. */
    for(uint32_t i=0;;i++) {
        rc=zap_cursor_retrieve(&cursor,entry);if(rc||i==index)break;
        zap_cursor_advance(&cursor);
    }
    if(!rc&&(entry->za_integer_length!=8||entry->za_num_integers!=1))rc=EIO;
    if(!rc&&strlen(entry->za_name)>=capacity)rc=ENAMETOOLONG;
    if(!rc){strcpy(name,entry->za_name);rc=oz_zpl_stat(fs,ZFS_DIRENT_OBJ(entry->za_first_integer),info);}
    zap_cursor_fini(&cursor);zap_attribute_free(entry);return rc;
}
static int resolve(struct oz_zpl *fs,uint64_t parent,const char *path,struct oz_zpl_info *info,
    cred_t *cr,int authenticated,oz_path_policy_t policy,void *context)
{
    if(!path||strchr(path,':'))return EINVAL;
    int rc=oz_zpl_stat(fs,parent,info);if(rc)return rc;
    while(*path) {
        if(info->symlink)return OZ_ZPL_SOFTLINK;
        if(!info->directory)return ENOTDIR;
        if(authenticated) {
            rc=oz_zpl_access(fs,info->object,ACE_EXECUTE,cr);
            if(!rc&&policy)rc=policy(context,info->object,ACE_EXECUTE,cr);
            if(rc)return rc;
        }
        /* AROS slash at the start of a component means parent; a slash
         * after a named component is its separator. Never escape the root. */
        if(*path=='/') {
            if(info->object==oz_zpl_root(fs))return ENOENT;
            rc=oz_zpl_stat(fs,info->parent,info);path++;
        } else {
            const char *end=strchr(path,'/');size_t n=end?(size_t)(end-path):strlen(path);
            char name[256];if(n>=sizeof(name))return ENAMETOOLONG;
            memcpy(name,path,n);name[n]=0;
            rc=oz_zpl_lookup_at(fs,info->object,name,info);
            path+=n;if(*path=='/') {path++;if(!rc&&info->symlink)return OZ_ZPL_SOFTLINK;if(!rc&&!info->directory)return ENOTDIR;}
        }
        if(rc)return rc;
    }
    return 0;
}
int oz_zpl_resolve(struct oz_zpl *fs,uint64_t parent,const char *path,struct oz_zpl_info *info)
{return resolve(fs,parent,path,info,NULL,0,NULL,NULL);}
int oz_zpl_resolve_as(struct oz_zpl *fs,uint64_t parent,const char *path,
    struct oz_zpl_info *info,cred_t *cr)
{
    if(!fs||!path||!info)return EINVAL;
    memset(info,0,sizeof(*info));
    if(!cr)return EACCES;
    struct oz_zpl_info candidate;
    int rc=resolve(fs,parent,path,&candidate,cr,1,NULL,NULL);
    if(!rc)rc=oz_zpl_access(fs,candidate.object,ACE_READ_ATTRIBUTES,cr);
    if(!rc)*info=candidate;
    return rc;
}
int oz_zpl_resolve_policy(struct oz_zpl *fs,uint64_t parent,const char *path,
    struct oz_zpl_info *info,cred_t *cr,oz_path_policy_t policy,void *context)
{return oz_zpl_resolve_for(fs,parent,path,info,cr,policy,context,ACE_READ_ATTRIBUTES);}
int oz_zpl_resolve_for(struct oz_zpl *fs,uint64_t parent,const char *path,
    struct oz_zpl_info *info,cred_t *cr,oz_path_policy_t policy,void *context,uint32_t rights)
{
    if(!fs||!path||!info||!policy)return EINVAL;
    memset(info,0,sizeof(*info));
    if(!cr)return EACCES;
    struct oz_zpl_info candidate;
    int rc=resolve(fs,parent,path,&candidate,cr,1,policy,context);
    if(!rc)rc=oz_zpl_access(fs,candidate.object,rights,cr);
    if(!rc)rc=policy(context,candidate.object,rights,cr);
    if(!rc)*info=candidate;
    return rc;
}
int oz_zpl_access(struct oz_zpl *fs,uint64_t object,uint32_t requested,cred_t *cr)
{return fs?oz_read_mount_access(fs->native,object,requested,cr):EINVAL;}
int oz_zpl_resolve_property(struct oz_zpl *fs,uint64_t parent,const char *path,
    struct oz_zpl_info *info,cred_t *cr,oz_path_policy_t policy,void *context,uint32_t rights)
{
    if(!fs||!path||!info||!policy)return EINVAL;
    memset(info,0,sizeof(*info));
    if(!cr)return EACCES;
    struct oz_zpl_info candidate;
    int rc=resolve(fs,parent,path,&candidate,cr,1,policy,context);
    /* Check mutation rights, not the target's current native read bit:
     * otherwise an owner could not clear their own read protection. */
    if(!rc)rc=oz_zpl_access(fs,candidate.object,rights,cr);
    if(!rc)*info=candidate;
    return rc;
}
int oz_zpl_name(struct oz_zpl *fs,uint64_t object,char *name,size_t capacity)
{
    if(!capacity)return ENAMETOOLONG;
    if(object==oz_zpl_root(fs)){name[0]=0;return 0;}
    struct oz_zpl_info info;int rc=oz_zpl_stat(fs,object,&info);if(rc)return rc;
    zap_cursor_t cursor;zap_attribute_t *entry=zap_attribute_long_alloc();
    zap_cursor_init(&cursor,oz_read_mount_objset(fs->native),info.parent);
    while(!(rc=zap_cursor_retrieve(&cursor,entry))) {
        if(entry->za_integer_length==8&&entry->za_num_integers==1&&
            ZFS_DIRENT_OBJ(entry->za_first_integer)==object) {
            if(strlen(entry->za_name)>=capacity)rc=ENAMETOOLONG;
            else strcpy(name,entry->za_name);
            break;
        }
        zap_cursor_advance(&cursor);
    }
    zap_cursor_fini(&cursor);zap_attribute_free(entry);return rc;
}
int oz_zpl_create_as(struct oz_zpl *fs,uint64_t parent,const char *name,uint32_t protection,
    cred_t *cr,struct oz_zpl_info *info)
{return fs?oz_read_mount_create(fs->native,parent,name,protection,cr,info):EINVAL;}
int oz_zpl_mkdir_as(struct oz_zpl *fs,uint64_t parent,const char *name,uint32_t protection,
    cred_t *cr,struct oz_zpl_info *info)
{return fs?oz_read_mount_mkdir(fs->native,parent,name,protection,cr,info):EINVAL;}
int oz_zpl_replace_as(struct oz_zpl *fs,uint64_t parent,const char *source,const char *target,
    const struct oz_zpl_info *fresh,const struct oz_zpl_info *old,cred_t *cr,
    int (*check)(void *),void *context)
{return fs?oz_read_mount_replace(fs->native,parent,source,target,fresh,old,cr,check,context):EINVAL;}
int oz_zpl_remove_as(struct oz_zpl *fs,uint64_t parent,const char *name,int directory,cred_t *cr)
{return fs?oz_read_mount_remove(fs->native,parent,name,directory,cr):EINVAL;}
int oz_zpl_rename_as(struct oz_zpl *fs,uint64_t from,const char *oldname,uint64_t to,
    const char *newname,cred_t *cr)
{return fs?oz_read_mount_rename(fs->native,from,oldname,to,newname,cr):EINVAL;}
int oz_zpl_exchange_as(struct oz_zpl *fs,uint64_t from,const char *oldname,uint64_t to,
    const char *newname,cred_t *cr)
{return fs?oz_read_mount_exchange(fs->native,from,oldname,to,newname,cr):EINVAL;}
int oz_zpl_whiteout_as(struct oz_zpl *fs,uint64_t from,const char *oldname,uint64_t to,
    const char *newname,cred_t *cr)
{return fs?oz_read_mount_whiteout(fs->native,from,oldname,to,newname,cr):EINVAL;}
int oz_zpl_resize_as(struct oz_zpl *fs,uint64_t object,uint64_t size,cred_t *cr)
{return fs?oz_read_mount_resize(fs->native,object,size,cr):EINVAL;}
int oz_zpl_link_as(struct oz_zpl *fs,uint64_t parent,uint64_t object,const char *name,cred_t *cr)
{return fs?oz_read_mount_link(fs->native,parent,object,name,cr):EINVAL;}
int oz_zpl_symlink_as(struct oz_zpl *fs,uint64_t parent,const char *name,const char *target,uint32_t protection,cred_t *cr)
{return fs?oz_read_mount_symlink(fs->native,parent,name,target,protection,cr):EINVAL;}
int oz_zpl_readlink_as(struct oz_zpl *fs,uint64_t object,char *buffer,size_t size,cred_t *cr)
{return fs?oz_read_mount_readlink(fs->native,object,buffer,size,cr):EINVAL;}
int oz_zpl_write_as(struct oz_zpl *fs,uint64_t object,uint64_t offset,const void *buffer,
    size_t size,size_t *written,cred_t *cr)
{
    if(!written)return EINVAL;
    *written=0;
    return fs?oz_read_mount_write(fs->native,object,offset,buffer,size,written,cr):EINVAL;
}
int oz_zpl_rewrite_as(struct oz_zpl *fs,uint64_t object,uint64_t offset,
    uint64_t length,uint64_t flags,uint64_t argument,cred_t *cr)
{
    return fs?oz_read_mount_rewrite(fs->native,object,offset,length,flags,argument,cr):EINVAL;
}
int oz_zpl_read(struct oz_zpl *fs,uint64_t object,uint64_t offset,void *buffer,size_t size)
{
    return oz_read_mount_read(fs->native,object,offset,buffer,size);
}
int oz_zpl_read_as(struct oz_zpl *fs,uint64_t object,uint64_t offset,
    void *buffer,size_t size,cred_t *cr)
{
    int rc=oz_zpl_access(fs,object,ACE_READ_DATA,cr);
    return rc?rc:oz_zpl_read(fs,object,offset,buffer,size);
}
