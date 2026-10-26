/* SPDX-License-Identifier: CDDL-1.0 */
/* Explicit file or native-device import. File imports retain locks; writable
 * imports hand off to upstream open handles and retain parent directory locks.
 * Backing files must be dedicated to ZFS, without external writers/renames. */
#include <sys/zfs_context.h>
#include <sys/spa_impl.h>
#include <sys/vdev_impl.h>
#include <sys/vdev_draid.h>
#include <sys/abd.h>
#include <sys/zio_checksum.h>
#include <sys/stat.h>
#include <sys/aros_vdev_policy.h>
#include <zfs_namecheck.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>
#include <fcntl.h>
#include "../ui/api.h"
#include "import-platform.h"
#include "cache-policy.h"
#include "packet-cred.h"
#include "stream-file.h"
#include <libraries/security.h>
#include "device-io.h"
#include "partition-io.h"
#include "../cli/device-path.h"
struct imported_files {
    struct imported_files *next;
    char name[256];unsigned count;
    uint64_t guid;
    BPTR locks[OZ_MAX_DEVICES];
    struct oz_device_binding *native[OZ_MAX_DEVICES];
    unsigned char missing[OZ_MAX_DEVICES];
    char names[OZ_MAX_DEVICES][258];char *paths[OZ_MAX_DEVICES];
    struct oz_vdev_policy *policy;
};
static struct imported_files *imports;
/* Service owner only. A matching name with stale retained identity is not an
 * absent admission: recovery must fail rather than silently skip it. */
int oz_pool_import_identity_check(spa_t *spa)
{
    for(struct imported_files *p=imports;p;p=p->next)
        if(!strcmp(p->name,spa_name(spa))&&p->guid!=spa_guid(spa))return ESTALE;
    return 0;
}
/* Common native/CLI reguid adapter. The service owner serializes import,
 * clear and GUID changes. Publish the new retained identity only after the
 * upstream mutation succeeds; no allocation or new device admission occurs. */
int oz_pool_change_guid(spa_t *spa,const uint64_t *requested)
{
    if(!spa_writeable(spa))return EROFS;
    if(spa_suspended(spa))return EAGAIN;
    int error=oz_pool_import_identity_check(spa);
    if(error)return error;
    uint64_t old=spa_guid(spa),guid;
    if(requested)guid=*requested;
    else {
        spa_namespace_enter(FTAG);
        guid=spa_generate_guid(NULL);
        spa_namespace_exit(FTAG);
    }
    error=oz_vdev_policy_reguid_begin(old,guid);
    if(!error) {
        error=spa_change_guid(spa,&guid);
        oz_vdev_policy_reguid_end(old,guid,error);
        if(!error)for(struct imported_files *p=imports;p;p=p->next)
            if(p->guid==old&&!strcmp(p->name,spa_name(spa)))p->guid=spa_guid(spa);
    }
    return error;
}
struct readmit_reader {
    struct oz_device *device;
    uint64_t pool,member;
    int error;
};
static int readmit_label(void *arg,void *buffer,size_t size,uint64_t offset)
{
    struct readmit_reader *r=arg;
    int error=oz_device_transfer(r->device,buffer,size,offset,0);
    if(error){r->error=error;return -1;}
    /* Reject conflicting decodable labels, not only the first usable copy.
     * Upstream will additionally validate labels/uberblocks on vdev reopen. */
    nvlist_t *config=NULL;
    if(size==sizeof(vdev_phys_t)&&!nvlist_unpack(((vdev_phys_t *)buffer)->vp_nvlist,
        sizeof(((vdev_phys_t *)buffer)->vp_nvlist),&config,0)) {
        uint64_t pool=0,member=0,state=0;
        if(nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&pool)||
           nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&member)||
           nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_STATE,&state)||
           pool!=r->pool||member!=r->member||state!=POOL_STATE_ACTIVE)r->error=ESTALE;
        nvlist_free(config);
    }
    return 0;
}
static vdev_t *readmit_leaf(vdev_t *vd,const char *path)
{
    if(!vd->vdev_children)return vd->vdev_path&&!strcmp(vd->vdev_path,path)?vd:NULL;
    for(uint64_t i=0;i<vd->vdev_children;i++){
        vdev_t *found=readmit_leaf(vd->vdev_child[i],path);if(found)return found;
    }
    return NULL;
}
/* The service owner serializes configuration requests. The upstream ioctl
 * has already checked config privilege and holds SPA, but no config lock:
 * spa_vdev_setpath takes its own writer lock and waits for config sync.
 * A same-extent alias keeps the exact admitted handle. A genuinely moved
 * device requires an offline, closed leaf and matching ZFS label identity;
 * it stays offline until a separate upstream online/reopen validates it.
 * File-backed relocation needs native file-lock identity and is not admitted
 * here. Labels are not permission to write a DOS-owned or reserved extent. */
int oz_pool_vdev_setpath(spa_t *spa,uint64_t guid,const char *newpath)
{
    if(!spa_writeable(spa))return EROFS;
    if(spa_suspended(spa))return EAGAIN;
    int error=oz_pool_import_identity_check(spa);
    if(error)return error;
    vdev_t *vd=spa_lookup_by_guid(spa,guid,B_TRUE);
    if(!vd||!vd->vdev_path)return ENOENT;
    if(!vd->vdev_ops->vdev_op_leaf)return ENOTSUP;
    struct oz_device_path parsed;
    if(!newpath||strlen(newpath)>=sizeof(((struct imported_files *)0)->names[0]))return EINVAL;
    if(strncmp(vd->vdev_path,"/AROS/",6))return ENOTSUP;
    if((error=oz_device_path_parse(newpath,&parsed)))return error;
    struct imported_files *owner=NULL;unsigned slot=0;
    for(struct imported_files *p=imports;p;p=p->next) {
        if(strcmp(p->name,spa_name(spa))||p->guid!=spa_guid(spa))continue;
        for(unsigned i=0;i<p->count;i++)if(!strcmp(p->paths[i],vd->vdev_path)) {
            if(owner)return EINVAL;
            owner=p;slot=i;
        }
    }
    if(!owner||!owner->native[slot])return ENOENT;
    if(!strcmp(newpath,vd->vdev_path))return 0;
    struct oz_device *old=NULL,*candidate=NULL;
    struct oz_device_binding *replacement=NULL;
    struct oz_vdev_repath *policy=NULL;
    int alias=0;
    error=oz_device_binding_snapshot(owner->native[slot],&old);
    /* An explicitly absent retained member can be moved only while offline. */
    if(error==ENODEV&&owner->missing[slot])error=0;
    if(!error)error=parsed.partition?
        oz_device_open_partition(parsed.device,parsed.unit,parsed.position,0,&candidate):
        oz_device_open(parsed.device,parsed.unit,0,&candidate);
    if(!error&&old) {
        error=oz_device_binding_repath_begin(owner->native[slot],old,candidate,newpath);
        if(!error)alias=1;
        else if(error==ESTALE)error=0; /* Different extent: check offline identity below. */
    }
    if(!error&&!alias) {
        if(!vd->vdev_offline||vd->vdev_tsd||vd->vdev_top->vdev_removing)error=EBUSY;
        oz_device_close(candidate);candidate=NULL;
        if(!error)error=parsed.partition?
            oz_device_open_partition(parsed.device,parsed.unit,parsed.position,1,&candidate):
            oz_device_open(parsed.device,parsed.unit,1,&candidate);
        if(!error&&oz_device_size(candidate)<vd->vdev_psize)error=ENOSPC;
        nvlist_t *config=NULL;int labels=0;
        if(!error) {
            struct readmit_reader reader={candidate,spa_guid(spa),guid,0};
            int rc=zpool_read_label_from(oz_device_size(candidate),readmit_label,&reader,&config,&labels);
            error=reader.error?reader.error:rc?ENOMEM:!config||!labels?ENODEV:0;
        }
        nvlist_free(config);
        if(!error)error=oz_device_bind_writable(newpath,candidate,&replacement);
    }
    if(!error)error=oz_vdev_policy_repath_begin(spa_guid(spa),spa_name(spa),
        owner->paths[slot],newpath,&policy);
    if(!error)error=spa_vdev_setpath(spa,guid,newpath);
    oz_vdev_policy_repath_end(policy,error);
    if(alias)VERIFY0(oz_device_binding_repath_end(owner->native[slot],error));
    if(!error) {
        if(replacement) {
            oz_device_unbind(owner->native[slot]);
            owner->native[slot]=replacement;replacement=NULL;
        }
        strcpy(owner->names[slot],newpath);
        owner->missing[slot]=0;
    }
    oz_device_unbind(replacement);
    oz_device_close(candidate);oz_device_close(old);
    return error;
}
/* Serialized confirmed clear only, with SPA held and config reader lock.
 * No path discovery or revived stale references. A partial admission failure
 * never calls zio_resume; independently validated bindings may remain fresh. */
int oz_pool_import_readmit(spa_t *spa)
{
    int error=oz_pool_import_identity_check(spa);
    for(struct imported_files *p=imports;p&&!error;p=p->next){
        if(strcmp(p->name,spa_name(spa))||p->guid!=spa_guid(spa))continue;
        for(unsigned i=0;i<p->count&&!error;i++){
            if(!p->native[i])continue;
            struct oz_device *old=NULL,*fresh=NULL;
            const char *stage="binding-snapshot";
            error=oz_device_binding_snapshot(p->native[i],&old);
            if(!error&&!oz_device_media_invalid(old)){oz_device_close(old);continue;}
            vdev_t *vd=NULL;
            if(!error){stage="vdev-path";vd=readmit_leaf(spa->spa_root_vdev,p->paths[i]);
                if(!vd)error=ENODEV;}
            struct oz_device_path path;
            if(!error){stage="path-parse";error=oz_device_path_parse(p->paths[i],&path);}
            if(!error){stage="device-open";error=path.partition?
                oz_device_open_partition(path.device,path.unit,path.position,0,&fresh):
                oz_device_open(path.device,path.unit,0,&fresh);}
            nvlist_t *config=NULL;int labels=0;
            if(!error){
                stage="member-labels";
                struct readmit_reader reader={fresh,spa_guid(spa),vd->vdev_guid,0};
                int rc=zpool_read_label_from(oz_device_size(fresh),readmit_label,&reader,&config,&labels);
                error=reader.error?reader.error:rc?ENOMEM:!config||!labels?ENODEV:0;
            }
            nvlist_free(config);
            if(!error){stage="binding-replace";error=oz_device_binding_replace(p->native[i],old,fresh);}
            if(error){
                char diagnostic[768];snprintf(diagnostic,sizeof(diagnostic),
                    "OpenZFS readmission failed stage=%s errno=%d path=%s\n",
                    stage,error,p->paths[i]);
                for(const char *c=diagnostic;*c;c++)RawPutChar(*c);
            }
            oz_device_close(fresh);oz_device_close(old);
        }
    }
    return error;
}
static void release(struct imported_files *p)
{
    if(p->policy)oz_vdev_policy_end(p->policy,NULL,NULL);
    for(unsigned i=0;i<p->count;i++) {
        oz_device_unbind(p->native[i]);
        if(p->locks[i])UnLock(p->locks[i]);
    }
    free(p);
}
/* Service task only, after successful export or kernel_fini (NULL: all). */
void oz_pool_import_release(const char *name)
{
    struct imported_files **link=&imports;
    while(*link) {
        struct imported_files *p=*link;
        if(!name||!strcmp(name,p->name)){*link=p->next;release(p);}
        else link=&p->next;
    }
}
static int pin_file(struct imported_files *p,unsigned i,const char *path,
    const struct oz_packet_security *security,int readonly)
{
    if(!security)return EACCES;
    const char *start=path,*rest;char native[256],canonical[256];size_t volume;
    if(path[0]=='/') {
        const char *slash=strchr(path+1,'/');
        if(!slash||slash==path+1||!slash[1]||strchr(path,':')||!strncmp(path,"/AROS/",6))return ENOTSUP;
        start=path+1;volume=slash-start;rest=slash+1;
    } else {
        const char *colon=strchr(path,':');
        if(!colon||colon==path||!colon[1]||memchr(path,'/',colon-path))return ENOTSUP;
        volume=colon-path;rest=colon+1;
    }
    memcpy(native,start,volume);native[volume]=0;
    native[volume]=':';strcpy(native+volume+1,rest);
    int error=oz_stream_path_lock(security,native,&p->locks[i]);
    if(error&&error!=ENOENT)return error;
    if(!p->locks[i]) {
        /* Degraded recovery may name an absent member, but only beneath an
         * existing, pinned directory on an admitted native filesystem. Never
         * create it, scan its siblings, or mistake permission failures for
         * absence. Upstream import enforces the remaining redundancy. */
        char *separator=strrchr(native,'/');
        if(!separator)separator=strchr(native,':');
        if(!separator||!separator[1]||strchr(separator+1,':'))return EINVAL;
        char leaf[256];strcpy(leaf,separator+1);separator[1]=0;
        error=oz_stream_path_lock(security,native,&p->locks[i]);if(error)return error;
        struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
        if(!fib)return ENOMEM;
        int directory=Examine(p->locks[i],fib)&&fib->fib_DirEntryType>0;
        FreeDosObject(DOS_FIB,fib);if(!directory)return ENOTDIR;
        if(!NameFromLock(p->locks[i],(STRPTR)canonical,sizeof(canonical))||
            !AddPart((STRPTR)canonical,(CONST_STRPTR)leaf,sizeof(canonical)))return ENAMETOOLONG;
        p->missing[i]=1;
    } else {
        error=oz_stream_lock_access(security,p->locks[i],secAt_Read|(readonly?0:secAt_Write));if(error)return error;
        for(unsigned j=0;j<i;j++)if(p->locks[j]&&!p->missing[j]&&SameLock(p->locks[i],p->locks[j])==LOCK_SAME)return EEXIST;
        if(!NameFromLock(p->locks[i],(STRPTR)canonical,sizeof(canonical)))return ENAMETOOLONG;
    }
    char *colon=strchr(canonical,':');if(!colon)return EINVAL;
    *colon='/';snprintf(p->names[i],sizeof(p->names[i]),"/%s",canonical);p->paths[i]=p->names[i];
    /* For an absent member the checked, pinned parent supplies canonical
     * identity; retain the exact requested leaf. Keeping an assign spelling
     * here would disagree with the surviving member's canonical label path.
     * This admits no siblings, case-folded leaf matches or label-chosen paths. */
    for(unsigned j=0;j<i;j++)if(!strcmp(p->paths[i],p->paths[j]))return EEXIST;
    if(p->missing[i])return 0;
    /* A native FAT read/write open requires an exclusive lock. Validate under
     * the shared identity lock here; SPA later opens with the requested mode. */
    int fd=open(p->paths[i],O_RDONLY);struct stat64 st;
    if(fd<0)return errno?errno:EIO;
    if(fstat64(fd,&st))error=errno?errno:EIO;
    else if(!S_ISREG(st.st_mode))error=ENOTSUP;
    else if(st.st_size<SPA_MINDEVSIZE||(st.st_size&511))error=EINVAL;
    close(fd);return error;
}
static int pin_native(struct imported_files *p,unsigned i,const char *path,int readonly)
{
    /* Explicit root-authorized paths only. Writable handles reserve their
     * admitted extent until final engine close; every write excludes DOS
     * publication and revokes on a newly recognized filesystem claim.
     * As with file backing, external raw users must remain excluded by the
     * operator. Never remove/inhibit a foreign mount to admit this request. */
    if(strlen(path)>=sizeof(p->names[i]))return ENAMETOOLONG;
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    if(error)return error;
    struct oz_device *device=NULL;
    /* Probe the unit separately: partition.library's root-open failure is
     * not specific enough to distinguish missing media from catalog failure. */
    error=oz_device_open(parsed.device,parsed.unit,0,&device);
    if(error==ENODEV) {
        /* Retain explicit absence, not permission to open a future device.
         * Upstream decides whether the surviving topology is sufficient. */
        error=oz_device_bind_unavailable(path,&p->native[i]);
        if(!error){strcpy(p->names[i],path);p->paths[i]=p->names[i];p->missing[i]=1;}
        return error;
    }
    if(!error&&parsed.partition) {
        oz_device_close(device);device=NULL;
        error=oz_device_open_partition(parsed.device,parsed.unit,parsed.position,!readonly,&device);
        /* ENOENT specifically means a valid catalog lacked this position.
         * Corrupt/unreadable catalogs remain errors, not absent members. */
        if(error==ENOENT) {
            error=oz_device_bind_unavailable(path,&p->native[i]);
            if(!error){strcpy(p->names[i],path);p->paths[i]=p->names[i];p->missing[i]=1;}
            return error;
        }
    } else if(!error&&!readonly) {
        oz_device_close(device);device=NULL;
        error=oz_device_open(parsed.device,parsed.unit,1,&device);
    }
    if(!error&&oz_device_size(device)<SPA_MINDEVSIZE)error=EINVAL;
    if(!error)error=readonly?oz_device_bind_readonly(path,device,&p->native[i]):
        oz_device_bind_writable(path,device,&p->native[i]);
    oz_device_close(device);
    if(!error){strcpy(p->names[i],path);p->paths[i]=p->names[i];}
    return error;
}
static int writable_handoff(struct imported_files *p)
{
    BPTR parents[OZ_MAX_DEVICES]={0};int error=0;
    /* Acquire every parent before relinquishing any file lock. Existing
     * missing-member locks already pin parents. The backing files must not
     * be renamed/replaced by other tasks during this non-atomic handoff. */
    for(unsigned i=0;i<p->count;i++)if(!p->missing[i]&&!p->native[i]) {
        parents[i]=ParentDir(p->locks[i]);
        if(!parents[i]){error=EIO;break;}
    }
    for(unsigned i=0;i<p->count;i++)if(parents[i]) {
        if(error)UnLock(parents[i]);
        else {UnLock(p->locks[i]);p->locks[i]=parents[i];}
    }
    return error;
}
/* A read-only TRYIMPORT owns temporary bindings, never a published pool.
 * The selected path list is separate from the label-derived topology. */
static int cli_import_prepare(const void *bytes,size_t size,cred_t *caller,
    const struct oz_packet_security *security,const char *name,uint64_t expected_guid,int readonly,void **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);if(error)return error;
    nvlist_t *config=NULL,*admission=NULL;char **paths=NULL;uint_t count=0;
    error=nvlist_unpack((char *)bytes,size,&config,0);
    if(error)return error;
    error=nvlist_lookup_nvlist(config,OZ_IMPORT_ADMISSION,&admission);
    if(!error)error=nvlist_lookup_string_array(admission,OZ_IMPORT_PATHS,&paths,&count);
    if(!error&&(!count||count>OZ_MAX_DEVICES))error=EINVAL;
    for(unsigned i=0;!error&&i<count;i++) {
        if(!paths[i][0]||strlen(paths[i])>=256)error=EINVAL;
        for(unsigned j=0;!error&&j<i;j++)if(!strcmp(paths[i],paths[j]))error=EEXIST;
    }
    struct oz_import_paths allowed={.paths=paths,.count=count};
    /* Discovery authorizes candidates, not simultaneous writable ownership
     * of every scanned extent. Whole units overlap their partitions and may
     * belong to unrelated pools. Pin only config-referenced admitted leaves. */
    char *selected[OZ_MAX_DEVICES];unsigned selected_count=0;
    if(!error)error=oz_import_config_select(&allowed,config,selected,&selected_count);
    if(!error){paths=selected;count=selected_count;allowed.paths=paths;allowed.count=count;}
    uint64_t guid=0;
    if(!error)error=nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&guid);
    if(!error&&expected_guid&&guid!=expected_guid)error=EINVAL;
    struct imported_files *p=NULL;
    if(!error){p=calloc(1,sizeof(*p));if(!p)error=ENOMEM;}
    if(!error) {
        p->count=count;
        p->guid=guid;if(name)strcpy(p->name,name);
        for(unsigned i=0;!error&&i<count;i++)
            error=!strncmp(paths[i],"/AROS/",6)?pin_native(p,i,paths[i],readonly):pin_file(p,i,paths[i],security,readonly);
    }
    if(!error) {
        allowed.paths=p->paths;
        error=oz_import_config_check(&allowed,config);
    }
    if(!error)error=oz_vdev_policy_begin(guid,p->paths,p->count,&p->policy);
    if(!error&&!readonly)error=writable_handoff(p);
    nvlist_free(config);
    if(error){if(p)release(p);return error;}
    *out=p;return 0;
}
int oz_cli_tryimport_begin(const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out)
{return cli_import_prepare(bytes,size,caller,security,NULL,0,1,out);}
int oz_cli_tryimport_end(void *scope)
{
    struct imported_files *p=scope;unsigned denied=0;
    if(p) {
        if(p->policy){oz_vdev_policy_end(p->policy,NULL,&denied);p->policy=NULL;}
        release(p);
    }
    return denied?EACCES:0;
}
/* Native default is available only on protected persistent ENVARC storage. */
static int cli_cache_policy(nvlist_t *props)
{
    const char *cache=NULL;
    int error=nvlist_lookup_string(props,"cachefile",&cache);
    if(error)return error==ENOENT?oz_cache_default_check():error;
    if(!cache[0])return oz_cache_default_check();
    if(!strcmp(cache,"none"))return 0;
    if(cache[0]!='/')return EINVAL;
    const char *leaf=strrchr(cache,'/')+1;
    if(!leaf[0]||!strcmp(leaf,".")||!strcmp(leaf,".."))return EINVAL;
    return 0;
}
int oz_cli_import_begin(const char *name,uint64_t guid,void *bytes,size_t *size,
    const void *property_bytes,size_t property_size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    *out=NULL;
    int error=secpolicy_zfs(caller);if(error)return error;
    if(!guid||pool_namecheck(name,NULL,NULL))return EINVAL;
    spa_namespace_enter(FTAG);boolean_t exists=spa_lookup(name)!=NULL;
    spa_namespace_exit(FTAG);if(exists)return EEXIST;
    nvlist_t *props=NULL,*config=NULL;uint64_t readonly=0;
    error=property_size?nvlist_unpack((char *)property_bytes,property_size,&props,0):
        nvlist_alloc(&props,NV_UNIQUE_NAME,0);
    if(!error) {
        error=nvlist_lookup_uint64(props,"readonly",&readonly);
        if(error==ENOENT)error=0;
        if(!error&&readonly>1)error=EINVAL;
    }
    if(!error)error=cli_cache_policy(props);
    if(!error&&!readonly&&!(spa_mode_global&SPA_MODE_WRITE))error=EROFS;
    nvlist_free(props);if(error)return error;
    error=cli_import_prepare(bytes,*size,caller,security,name,guid,readonly,out);
    if(!error)error=nvlist_unpack(bytes,*size,&config,0);
    if(!error) {
        (void)nvlist_remove_all(config,OZ_IMPORT_ADMISSION);
        /* Repack in the already bounded private snapshot. Admission metadata
         * must not become persistent pool configuration. */
        char *packed=bytes;
        error=nvlist_pack(config,&packed,size,NV_ENCODE_NATIVE,0);
    }
    nvlist_free(config);
    if(error&&*out){oz_cli_tryimport_end(*out);*out=NULL;}
    return error;
}
int oz_cli_import_end(void *scope,int error)
{
    struct imported_files *p=scope;
    if(!p)return error;
    /* A successful SPA import can precede a failed output-nvlist copy. Never
     * drop active device reservations merely because the reply was too small. */
    spa_namespace_enter(FTAG);spa_t *spa=spa_lookup(p->name);
    boolean_t imported=spa&&(!p->guid||spa_guid(spa)==p->guid);
    /* Create reservations precede SPA GUID allocation. Retain the assigned
     * identity, not zero, so later explicit readmission can match this pool. */
    if(imported)p->guid=spa_guid(spa);
    spa_namespace_exit(FTAG);
    if(imported) {
        p->next=imports;imports=p;
        return error==ENOMEM?EOVERFLOW:error;
    }
    int denied=oz_cli_tryimport_end(p);
    return error?error:denied?denied:EIO;
}
static int create_paths(nvlist_t *tree,char **paths,unsigned *count,unsigned depth,unsigned *nodes)
{
    if(depth>8||++*nodes>128)return E2BIG;
    const char *type;
    if(nvlist_lookup_string(tree,ZPOOL_CONFIG_TYPE,&type))return EINVAL;
    if(!strcmp(type,VDEV_TYPE_DISK)||!strcmp(type,VDEV_TYPE_FILE)) {
        const char *path;
        if(*count==OZ_MAX_DEVICES||nvlist_lookup_string(tree,ZPOOL_CONFIG_PATH,&path)||
            !path[0]||strlen(path)>=256)return EINVAL;
        for(unsigned i=0;i<*count;i++)if(!strcmp(paths[i],path))return EEXIST;
        paths[(*count)++]=(char *)path;
    } else if(strcmp(type,VDEV_TYPE_ROOT)&&strcmp(type,VDEV_TYPE_MIRROR)&&
        strcmp(type,VDEV_TYPE_RAIDZ)&&strcmp(type,VDEV_TYPE_DRAID))return ENOTSUP;
    const char *arrays[]={ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
    for(unsigned a=0;a<3;a++)if(nvlist_exists(tree,arrays[a])) {
        nvlist_t **children;uint_t n;
        if(nvlist_lookup_nvlist_array(tree,arrays[a],&children,&n)||n>OZ_MAX_DEVICES)return EINVAL;
        for(unsigned i=0;i<n;i++){int error=create_paths(children[i],paths,count,depth+1,nodes);if(error)return error;}
    }
    return 0;
}
/* Verify the embedded label checksum before trusting destructive-reuse
 * metadata. No spa exists for this candidate yet, so use the upstream label
 * checksum function with its offset verifier (no template is required).
 * As in zio_checksum_error_impl, restore the borrowed buffer afterwards. */
static int destroyed_label_checksum(vdev_phys_t *label,uint64_t offset)
{
    zio_eck_t saved=label->vp_zbt;
    boolean_t swapped=saved.zec_magic==BSWAP_64(ZEC_MAGIC);
    if(saved.zec_magic!=ZEC_MAGIC&&!swapped)return ECKSUM;
    zio_cksum_t expected=saved.zec_cksum,actual;
    ZIO_SET_CHECKSUM(&label->vp_zbt.zec_cksum,offset,0,0,0);
    if(swapped){
        byteswap_uint64_array(&label->vp_zbt.zec_cksum,sizeof(zio_cksum_t));
        byteswap_uint64_array(&expected,sizeof(expected));
    }
    abd_t *abd=abd_get_from_buf(label,sizeof(*label));
    zio_checksum_table[ZIO_CHECKSUM_LABEL].ci_func[swapped](abd,sizeof(*label),NULL,&actual);
    abd_free(abd);label->vp_zbt=saved;
    return ZIO_CHECKSUM_EQUAL(actual,expected)?0:ECKSUM;
}

/* New pool members may reuse an explicitly destroyed member. Require every label to
 * agree under the retained native reservation or checked file identity lock,
 * rather than trusting client preflight or accepting arbitrary nonzero media.
 * Explicit force-create also permits coherently exported members, but never
 * members belonging to a pool in the live namespace. */
static int destroyed_backing(struct oz_device *device,int fd,uint64_t bytes,int allow_exported)
{
    uint64_t size=P2ALIGN_TYPED(bytes,sizeof(vdev_label_t),uint64_t);
    if(size<VDEV_LABELS*sizeof(vdev_label_t))return EINVAL;
    vdev_phys_t *label=malloc(sizeof(*label));if(!label)return ENOMEM;
    uint64_t pool=0,member=0,first_state=0;int error=0;
    for(unsigned l=0;l<VDEV_LABELS&&!error;l++){
        uint64_t offset=l*sizeof(vdev_label_t)+(l<VDEV_LABELS/2?0:size-VDEV_LABELS*sizeof(vdev_label_t))+VDEV_SKIP_SIZE;
        if(device)error=oz_device_transfer(device,label,sizeof(*label),offset,0);
        else if(lseek64(fd,offset,SEEK_SET)!=(off64_t)offset||
            read(fd,label,sizeof(*label))!=(ssize_t)sizeof(*label))error=EIO;
        nvlist_t *config=NULL;
        if(!error)error=destroyed_label_checksum(label,offset);
        if(!error)error=nvlist_unpack(label->vp_nvlist,sizeof(label->vp_nvlist),&config,0);
        uint64_t state=0,current_pool=0,current_member=0;
        if(!error&&(nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_STATE,&state)||
            nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&current_pool)||
            nvlist_lookup_uint64(config,ZPOOL_CONFIG_GUID,&current_member)||
            (state!=POOL_STATE_DESTROYED&&(!allow_exported||state!=POOL_STATE_EXPORTED))||
            !current_pool||!current_member))error=EEXIST;
        if(!error&&l&&(pool!=current_pool||member!=current_member||first_state!=state))error=EEXIST;
        if(!error){pool=current_pool;member=current_member;first_state=state;}
        nvlist_free(config);
    }
    if(!error&&allow_exported){
        /* A readonly import can leave EXPORTED labels on its members. The
         * live namespace, not the label state, decides whether it is in use.
         * The service owner serializes admission and the ensuing create. */
        spa_namespace_enter(FTAG);
        if(spa_by_guid(pool,0)!=NULL)error=EBUSY;
        spa_namespace_exit(FTAG);
    }
    free(label);return error;
}
int oz_destroyed_file_backing(int fd,uint64_t size)
{
    if(fd<0)return EBADF;
    return destroyed_backing(NULL,fd,size,0);
}
int oz_exported_file_backing(int fd,uint64_t size)
{
    if(fd<0)return EBADF;
    return destroyed_backing(NULL,fd,size,1);
}
static int blank_backing(struct imported_files *p,unsigned i,unsigned char *buffer,int reuse_destroyed)
{
    if(p->missing[i])return ENODEV;
    struct oz_device *device=NULL;int fd=-1,error=0;uint64_t size=0;
    if(p->native[i]) {
        error=oz_device_bound_open(p->paths[i],0,&device);
        if(!error)size=oz_device_size(device);
    } else {
        struct stat64 st;
        fd=open(p->paths[i],O_RDONLY);
        if(fd<0)error=errno?errno:EIO;
        else if(fstat64(fd,&st))error=errno?errno:EIO;
        else if(!S_ISREG(st.st_mode))error=ENOTSUP;
        else size=st.st_size;
    }
    if(!error&&(size<SPA_MINDEVSIZE||(size&511)))error=EINVAL;
    for(uint64_t offset=0;!error&&offset<size;) {
        size_t n=MIN(size-offset,1024*1024);
        if(device)error=oz_device_transfer(device,buffer,n,offset,0);
        else if(read(fd,buffer,n)!=(ssize_t)n)error=EIO;
        for(size_t b=0;!error&&b<n;b++)if(buffer[b])error=EEXIST;
        offset+=n;
    }
    if(error==EEXIST&&reuse_destroyed)error=destroyed_backing(device,fd,size,reuse_destroyed==2);
    if(fd>=0)close(fd);
    oz_device_close(device);return error;
}
static int create_begin(const char *name,const void *bytes,size_t size,
    const void *property_bytes,size_t property_size,cred_t *caller,const struct oz_packet_security *security,void **out,int force)
{
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    *out=NULL;int error=secpolicy_zfs(caller);if(error)return error;
    if(!(spa_mode_global&SPA_MODE_WRITE))return EROFS;
    if(pool_namecheck(name,NULL,NULL))return EINVAL;
    spa_namespace_enter(FTAG);boolean_t exists=spa_lookup(name)!=NULL;
    spa_namespace_exit(FTAG);if(exists)return EEXIST;
    nvlist_t *root=NULL,*props=NULL,*config=NULL;
    error=property_size?nvlist_unpack((char *)property_bytes,property_size,&props,0):
        nvlist_alloc(&props,NV_UNIQUE_NAME,0);
    if(!error)error=cli_cache_policy(props);
    /* Upstream creates the live SPA under tname while keeping name on disk.
     * Reservations and export cleanup must follow that live namespace. */
    const char *active_name=name;
    if(!error&&nvlist_exists(props,"tname")) {
        error=nvlist_lookup_string(props,"tname",&active_name);
        if(!error&&(strlen(active_name)>=sizeof(((struct imported_files *)0)->name)||
            pool_namecheck(active_name,NULL,NULL)))error=EINVAL;
        if(!error) {
            spa_namespace_enter(FTAG);exists=spa_lookup(active_name)!=NULL;
            spa_namespace_exit(FTAG);if(exists)error=EEXIST;
        }
    }
    if(!error)error=nvlist_unpack((char *)bytes,size,&root,0);
    char *paths[OZ_MAX_DEVICES];unsigned count=0,nodes=0;
    if(!error)error=create_paths(root,paths,&count,0,&nodes);
    if(!error&&!count)error=EINVAL;
    if(!error)error=nvlist_alloc(&config,NV_UNIQUE_NAME,0);
    if(!error)error=nvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
    struct oz_import_paths allowed={.paths=paths,.count=count};
    if(!error)error=oz_import_config_check(&allowed,config);
    struct imported_files *p=NULL;unsigned char *buffer=NULL;
    if(!error){p=calloc(1,sizeof(*p));buffer=malloc(1024*1024);if(!p||!buffer)error=ENOMEM;}
    if(!error) {
        p->count=count;strcpy(p->name,active_name);
        for(unsigned i=0;!error&&i<count;i++) {
            error=!strncmp(paths[i],"/AROS/",6)?pin_native(p,i,paths[i],0):pin_file(p,i,paths[i],security,0);
            if(!error)error=blank_backing(p,i,buffer,force?2:1);
        }
    }
    if(!error){allowed.paths=p->paths;error=oz_import_config_check(&allowed,config);}
    if(!error)error=oz_vdev_policy_begin_named(active_name,p->paths,p->count,&p->policy);
    if(!error)error=writable_handoff(p);
    free(buffer);nvlist_free(config);nvlist_free(root);nvlist_free(props);
    if(error){if(p)release(p);return error;}
    *out=p;return 0;
}
int oz_cli_create_begin(const char *name,const void *bytes,size_t size,
    const void *properties,size_t property_size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    return create_begin(name,bytes,size,properties,property_size,caller,security,out,0);
}
int oz_cli_create_request(const char *name,void *bytes,size_t *size,
    const void *properties,size_t property_size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    *out=NULL;
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);if(error)return error;
    nvlist_t *root=NULL;boolean_t force=B_FALSE;
    error=nvlist_unpack(bytes,*size,&root,0);
    if(!error&&nvlist_exists(root,OZ_CREATE_FORCE)){
        error=nvlist_lookup_boolean_value(root,OZ_CREATE_FORCE,&force);
        if(!error)error=nvlist_remove_all(root,OZ_CREATE_FORCE);
        if(!error){
            char *packed=bytes;
            error=nvlist_pack(root,&packed,size,NV_ENCODE_NATIVE,0);
        }
    }
    nvlist_free(root);
    if(!error)error=create_begin(name,bytes,*size,properties,property_size,caller,security,out,force);
    return error;
}
int oz_cli_create_force_begin(const char *name,const void *bytes,size_t size,
    const void *properties,size_t property_size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    return create_begin(name,bytes,size,properties,property_size,caller,security,out,1);
}
struct cli_add_scope {
    struct imported_files *files;
    struct oz_vdev_extension *extensions[OZ_MAX_DEVICES];
};
void oz_cli_add_end(void *scope,int error)
{
    struct cli_add_scope *s=scope;if(!s)return;
    for(unsigned i=0;i<OZ_MAX_DEVICES;i++)oz_vdev_policy_extend_end(s->extensions[i],error);
    if(s->files){
        if(error)release(s->files);
        else{s->files->next=imports;imports=s->files;}
    }
    free(s);
}
/* A distributed spare is an existing in-pool mapping, never a new backing
 * path. Keep this admission separate from blank external-member admission. */
static int attach_begin(const char *name,uint64_t target,int replacing,
    const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out,int force)
{
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    *out=NULL;
    int error=secpolicy_zfs(caller);if(error)return error;
    nvlist_t *root=NULL,**children=NULL;uint_t count=0;const char *type=NULL,*path=NULL;
    error=nvlist_unpack((char *)bytes,size,&root,0);if(error)return error;
    boolean_t synthetic=nvlist_lookup_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,&children,&count)==0&&
        count==1&&nvlist_lookup_string(children[0],ZPOOL_CONFIG_TYPE,&type)==0&&
        !strcmp(type,VDEV_TYPE_DRAID_SPARE);
    if(!synthetic){nvlist_free(root);return (force?oz_cli_add_force_begin:oz_cli_add_begin)(name,bytes,size,caller,security,out);}
    if(!replacing||!target||pool_namecheck(name,NULL,NULL))error=EINVAL;
    if(!error&&(!(spa_mode_global&SPA_MODE_WRITE)))error=EROFS;
    if(!error&&(nvlist_lookup_string(root,ZPOOL_CONFIG_TYPE,&type)||strcmp(type,VDEV_TYPE_ROOT)||
        nvlist_lookup_string(children[0],ZPOOL_CONFIG_PATH,&path)))error=EINVAL;
    /* Do not admit nested children, auxiliary arrays, or supplied identity
     * overrides. Upstream resolves the named spare from the live inventory. */
    for(nvpair_t *p=NULL;!error&&(p=nvlist_next_nvpair(root,p));)
        if(strcmp(nvpair_name(p),ZPOOL_CONFIG_TYPE)&&strcmp(nvpair_name(p),ZPOOL_CONFIG_CHILDREN))error=EINVAL;
    for(nvpair_t *p=NULL;!error&&(p=nvlist_next_nvpair(children[0],p));) {
        const char *key=nvpair_name(p);
        if(!strcmp(key,ZPOOL_CONFIG_TYPE)||!strcmp(key,ZPOOL_CONFIG_PATH))continue;
        uint64_t ashift;
        if(!strcmp(key,ZPOOL_CONFIG_IS_LOG)) {
            if(nvpair_value_uint64(p,&ashift)||ashift)error=EINVAL;
            continue;
        }
        if(strcmp(key,ZPOOL_CONFIG_ASHIFT)||nvpair_value_uint64(p,&ashift)||ashift<9||ashift>16)error=EINVAL;
    }
    spa_t *spa=NULL;
    if(!error)error=spa_open(name,&spa,FTAG);
    if(!error) {
        if(!spa_writeable(spa))error=EROFS;
        else if(spa_suspended(spa))error=EAGAIN;
        else {
            spa_config_enter(spa,SCL_CONFIG,FTAG,RW_READER);
            vdev_t *old=spa_lookup_by_guid(spa,target,B_FALSE);
            error=ENODEV;
            for(int i=0;old&&i<spa->spa_spares.sav_count;i++) {
                vdev_t *spare=spa->spa_spares.sav_vdevs[i];
                if(spare->vdev_ops==&vdev_draid_spare_ops&&spare->vdev_path&&
                    !strcmp(path,spare->vdev_path)&&
                    old->vdev_top==vdev_draid_spare_get_parent(spare)){error=0;break;}
            }
            spa_config_exit(spa,SCL_CONFIG,FTAG);
        }
        spa_close(spa,FTAG);
    }
    nvlist_free(root);return error;
}
int oz_cli_attach_begin(const char *name,uint64_t target,int replacing,
    const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    return attach_begin(name,target,replacing,bytes,size,caller,security,out,0);
}
int oz_cli_attach_request(const char *name,uint64_t target,int replacing,
    void *bytes,size_t *size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    *out=NULL;
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);if(error)return error;
    nvlist_t *root=NULL;boolean_t force=B_FALSE;
    error=nvlist_unpack(bytes,*size,&root,0);
    if(!error&&nvlist_exists(root,OZ_ATTACH_FORCE)){
        error=nvlist_lookup_boolean_value(root,OZ_ATTACH_FORCE,&force);
        if(!error)error=nvlist_remove_all(root,OZ_ATTACH_FORCE);
        if(!error){char *packed=bytes;error=nvlist_pack(root,&packed,size,NV_ENCODE_NATIVE,0);}
    }
    nvlist_free(root);
    if(!error)error=attach_begin(name,target,replacing,bytes,*size,caller,security,out,force);
    return error;
}
static int add_begin(const char *name,const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out,int force)
{
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    *out=NULL;int error=secpolicy_zfs(caller);if(error)return error;
    if(!(spa_mode_global&SPA_MODE_WRITE))return EROFS;
    if(pool_namecheck(name,NULL,NULL))return EINVAL;
    spa_t *spa=NULL;error=spa_open(name,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa))error=EAGAIN;
    nvlist_t *root=NULL,*config=NULL;char *paths[OZ_MAX_DEVICES];unsigned count=0,nodes=0;
    struct cli_add_scope *s=NULL;unsigned char *buffer=NULL;
    if(!error)error=nvlist_unpack((char *)bytes,size,&root,0);
    if(!error)error=create_paths(root,paths,&count,0,&nodes);
    if(!error&&!count)error=EINVAL;
    if(!error)error=nvlist_alloc(&config,NV_UNIQUE_NAME,0);
    if(!error)error=nvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
    struct oz_import_paths allowed={.paths=paths,.count=count};
    if(!error)error=oz_import_config_check(&allowed,config);
    if(!error){s=calloc(1,sizeof(*s));buffer=malloc(1024*1024);if(!s||!buffer)error=ENOMEM;}
    if(!error){s->files=calloc(1,sizeof(*s->files));if(!s->files)error=ENOMEM;}
    if(!error){
        struct imported_files *p=s->files;p->count=count;p->guid=spa_guid(spa);strcpy(p->name,name);
        for(unsigned i=0;!error&&i<count;i++){
            error=!strncmp(paths[i],"/AROS/",6)?pin_native(p,i,paths[i],0):pin_file(p,i,paths[i],security,0);
            if(!error)error=blank_backing(p,i,buffer,force?2:1);
        }
        if(!error){allowed.paths=p->paths;error=oz_import_config_check(&allowed,config);}
        if(!error)error=writable_handoff(p);
        for(unsigned i=0;!error&&i<count;i++)
            error=oz_vdev_policy_extend(p->guid,p->name,p->paths[i],&s->extensions[i]);
    }
    free(buffer);nvlist_free(config);nvlist_free(root);spa_close(spa,FTAG);
    if(error){oz_cli_add_end(s,error);return error;}
    *out=s;return 0;
}
int oz_cli_add_begin(const char *name,const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    return add_begin(name,bytes,size,caller,security,out,0);
}
int oz_cli_add_request(const char *name,void *bytes,size_t *size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    *out=NULL;
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);if(error)return error;
    nvlist_t *root=NULL;boolean_t force=B_FALSE;
    error=nvlist_unpack(bytes,*size,&root,0);
    if(!error&&nvlist_exists(root,OZ_ADD_FORCE)){
        error=nvlist_lookup_boolean_value(root,OZ_ADD_FORCE,&force);
        if(!error)error=nvlist_remove_all(root,OZ_ADD_FORCE);
        if(!error){char *packed=bytes;error=nvlist_pack(root,&packed,size,NV_ENCODE_NATIVE,0);}
    }
    nvlist_free(root);
    if(!error)error=add_begin(name,bytes,*size,caller,security,out,force);
    return error;
}
int oz_cli_add_force_begin(const char *name,const void *bytes,size_t size,cred_t *caller,const struct oz_packet_security *security,void **out)
{
    return add_begin(name,bytes,size,caller,security,out,1);
}
int oz_pool_import_files(struct oz_message *m,cred_t *caller,const struct oz_packet_security *security)
{
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);if(error)return error;
    char why[128];
    if(pool_namecheck(m->target,NULL,NULL)||m->flags!=OZ_CONFIRMED||m->argument[0]||
        oz_validate_import_files(&m->topology,why,sizeof(why))!=OZ_OK)return EINVAL;
    struct oz_import_policy policy;
    if(!oz_parse_import_policy(m->options,&policy))return EINVAL;
    int readonly=policy.readonly,destroyed=policy.destroyed,rewind=policy.checkpoint;
    UQUAD expected_guid=policy.expected_guid;
    if(!readonly&&!(spa_mode_global&SPA_MODE_WRITE))return EROFS;
    spa_namespace_enter(FTAG);boolean_t exists=spa_lookup(m->target)!=NULL;
    spa_namespace_exit(FTAG);if(exists)return EEXIST;
    struct imported_files *p=calloc(1,sizeof(*p));if(!p)return ENOMEM;
    p->count=m->topology.device_count;strcpy(p->name,m->target);
    struct oz_cred_scope *scope=NULL;nvlist_t *config=NULL,*props=NULL;
    const char *stage="credentials";
    error=oz_cred_push(caller,&scope);if(error)goto done;
    stage="pin-files";
    for(unsigned i=0;i<p->count&&!error;i++) {
        const char *path=m->topology.devices[i].path;
        error=!strncmp(path,"/AROS/",6)?pin_native(p,i,path,readonly):pin_file(p,i,path,security,readonly);
    }
    if(error)goto done;
    struct oz_import_paths allowed={.paths=p->paths,.count=p->count};
    libpc_handle_t h={.lpc_ops=&oz_aros_config_ops,.lpc_lib_handle=&allowed};
    importargs_t args={.path=p->paths,.paths=p->count,.poolname=m->target,.scan=B_TRUE};
    stage="discovery";
    error=zpool_find_config(&h,m->target,&config,&args);
    if(error){if(allowed.rejected)error=allowed.rejected;goto done;}
    /* libzutil discovery also returns destroyed pools. Upstream zpool's CLI
     * filters those unless recovery was explicitly requested (-D). Normal
     * native import must not silently resurrect a successfully destroyed pool. */
    stage="pool-state";
    uint64_t state;
    error=nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_STATE,&state);
    if(!error&&((state==POOL_STATE_DESTROYED)!=destroyed))error=ENOENT;
    if(error)goto done;
    uint64_t guid;
    stage="path-policy";
    error=nvlist_lookup_uint64(config,ZPOOL_CONFIG_POOL_GUID,&guid);
    if(!error)p->guid=guid;
    if(!error&&expected_guid&&guid!=expected_guid){stage="expected-guid";error=EINVAL;}
    if(!error)error=oz_vdev_policy_begin(guid,p->paths,p->count,&p->policy);
    if(error)goto done;
    props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"readonly",readonly);
    if(!readonly) {
        stage="writable-lock-handoff";error=writable_handoff(p);if(error)goto done;
    }
    stage="spa-import";
    if(policy.recovery!=OZ_IMPORT_CURRENT) {
        nvlist_t *load=fnvlist_alloc();
        fnvlist_add_uint32(load,ZPOOL_LOAD_REWIND_POLICY,ZPOOL_DO_REWIND|
            (policy.recovery==OZ_IMPORT_EXTREME?ZPOOL_EXTREME_REWIND:0));
        fnvlist_add_nvlist(config,ZPOOL_LOAD_POLICY,load);fnvlist_free(load);
    }
    error=spa_import(m->target,config,props,rewind?ZFS_IMPORT_CHECKPOINT:0);
    if(!error){p->next=imports;imports=p;p=NULL;}
done:
    if(error) {
        char diagnostic[128];snprintf(diagnostic,sizeof(diagnostic),
            "OpenZFS import failed stage=%s errno=%d\n",stage,error);
        for(const char *c=diagnostic;*c;c++)RawPutChar(*c);
    }
    nvlist_free(config);nvlist_free(props);
    if(p)release(p);
    if(scope)VERIFY0(oz_cred_pop(scope));
    return error;
}
