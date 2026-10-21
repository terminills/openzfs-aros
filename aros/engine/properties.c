/* SPDX-License-Identifier: CDDL-1.0 */
/* Exact property reads using the same upstream objects as zfs/zpool ioctls.
 * Not a replacement property database; unavailable values return errors. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dsl_prop.h>
#include <sys/dsl_dir.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_objset.h>
#include <sys/fs/zfs.h>
#include <zfs_prop.h>
#include <zfs_namecheck.h>
#include <libzutil.h>
#include <sys/zio_compress.h>
#include <sys/zfeature.h>
#include <sys/zfs_znode.h>
#ifdef OZ_NATIVE_DISPATCHER
#include <sys/zvol.h>
#include <sys/zfs_ioctl.h>
#include <zfs_deleg.h>
#include "ioctl-copy.h"
/* Upstream declares this only under _KERNEL. The full native dispatcher
 * links that complete zvol translation unit; this caller remains libzpool. */
extern int zvol_set_volsize(const char *,uint64_t);
extern int zvol_get_stats(objset_t *,nvlist_t *);
/* This userspace adapter does not include native kernel VFS declarations. */
extern long zfsdev_ioctl_common(uint_t,zfs_cmd_t *,int);
#endif
#include "../ui/api.h"
#include "cache-policy.h"

/* Upstream cursor and numeric identities are returned unchanged. Each request
 * selects one accounting metric; pages are live, not a frozen dataset view. */
int oz_accounting_list(struct oz_message *m,cred_t *caller)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;return ENOTSUP;
#else
    if(!caller)return EACCES;
    if(m->operation<67||m->operation>69||m->flags||m->options[0]||m->job_id||
        dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    const char *metrics[]={"used","quota","objects","objquota"};
    const unsigned props[3][4]={
        {ZFS_PROP_USERUSED,ZFS_PROP_USERQUOTA,ZFS_PROP_USEROBJUSED,ZFS_PROP_USEROBJQUOTA},
        {ZFS_PROP_GROUPUSED,ZFS_PROP_GROUPQUOTA,ZFS_PROP_GROUPOBJUSED,ZFS_PROP_GROUPOBJQUOTA},
        {ZFS_PROP_PROJECTUSED,ZFS_PROP_PROJECTQUOTA,ZFS_PROP_PROJECTOBJUSED,ZFS_PROP_PROJECTOBJQUOTA}};
    unsigned metric=0;
    if(m->argument[0]){for(;metric<4;metric++)if(!strcmp(m->argument,metrics[metric]))break;}
    if(metric==4)return EINVAL;
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));
    zfs_useracct_t *records=calloc(OZ_MAX_ROWS,sizeof(*records));
    if(!zc||!records){free(zc);free(records);return ENOMEM;}
    strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
    zc->zc_objset_type=props[m->operation-67][metric];zc->zc_cookie=m->cursor;
    zc->zc_nvlist_dst=(uintptr_t)records;zc->zc_nvlist_dst_size=OZ_MAX_ROWS*sizeof(*records);
    struct oz_cred_scope *credentials=NULL;
    struct oz_ioctl_copy_scope *copy=NULL;
    struct oz_ioctl_range range={records,OZ_MAX_ROWS*sizeof(*records),OZ_IOCTL_OUTPUT};
    int error=oz_cred_push(caller,&credentials);
    if(!error)error=oz_ioctl_copy_enter(&range,1,&copy);
    if(!error)error=zfsdev_ioctl_common(ZFS_IOC_USERSPACE_MANY-ZFS_IOC_FIRST,zc,FKIOCTL);
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    m->row_count=0;m->next_cursor=0;
    if(!error&&(zc->zc_nvlist_dst_size>range.size||zc->zc_nvlist_dst_size%sizeof(*records)))error=EOVERFLOW;
    unsigned count=error?0:zc->zc_nvlist_dst_size/sizeof(*records);
    for(unsigned i=0;!error&&i<count;i++){
        struct oz_row *r=&m->rows[i];memset(r,0,sizeof(*r));
        strcpy(r->kind,"accounting");
        snprintf(r->name,sizeof(r->name),"%s:%llu",m->operation==67?"user":m->operation==68?"group":"project",(unsigned long long)records[i].zu_rid);
        if(!memchr(records[i].zu_domain,0,sizeof(records[i].zu_domain))||
            strlcpy(r->parent,records[i].zu_domain,sizeof(r->parent))>=sizeof(r->parent)){error=ENAMETOOLONG;break;}
        strcpy(r->state,metrics[metric]);strcpy(r->value,metric<2?"bytes":"objects");
        r->used=records[i].zu_space;r->total=zc->zc_guid;m->row_count++;
    }
    if(!error&&count==OZ_MAX_ROWS){
        if(zc->zc_cookie==m->cursor)error=EIO;
        else m->next_cursor=zc->zc_cookie;
    }
    if(error){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
    if(error)snprintf(m->detail,sizeof(m->detail),"Authenticated accounting query errno=%d",error);
    else if(metric==1||metric==3)
        snprintf(m->detail,sizeof(m->detail),"Authenticated accounting metric=%s units=%s; default quota: query usage metric; errno=0",
            metrics[metric],metric<2?"bytes":"objects");
    else snprintf(m->detail,sizeof(m->detail),"Authenticated accounting metric=%s default-quota=%llu units=%s errno=0",
        metrics[metric],(unsigned long long)zc->zc_guid,metric<2?"bytes":"objects");
    free(records);free(zc);return error;
#endif
}

/* One row per stored permission, including ancestor records. These are not
 * flattened effective rights. Pages are live reads, not retained snapshots. */
int oz_permission_list(struct oz_message *m,cred_t *caller)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;return ENOTSUP;
#else
    if(!caller)return EACCES;
    if(m->operation!=79||m->flags||m->argument[0]||m->options[0]||m->job_id||
        dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));if(!zc)return ENOMEM;
    strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
    struct oz_cred_scope *credentials=NULL;int error=oz_cred_push(caller,&credentials);
    char *packed=NULL;size_t capacity=65536;nvlist_t *acl=NULL;
    while(!error){
        packed=malloc(capacity);if(!packed){error=ENOMEM;break;}
        zc->zc_nvlist_dst=(uintptr_t)packed;zc->zc_nvlist_dst_size=capacity;
        struct oz_ioctl_range range={packed,capacity,OZ_IOCTL_OUTPUT};
        struct oz_ioctl_copy_scope *copy=NULL;
        error=oz_ioctl_copy_enter(&range,1,&copy);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_GET_FSACL-ZFS_IOC_FIRST,zc,FKIOCTL);
        if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
        if(error==ENOMEM&&zc->zc_nvlist_dst_size>capacity&&zc->zc_nvlist_dst_size<=128UL*1024*1024){
            capacity=zc->zc_nvlist_dst_size;free(packed);packed=NULL;error=0;continue;
        }
        if(!error){
            if(zc->zc_nvlist_dst_size>capacity)error=EOVERFLOW;
            else error=nvlist_unpack(packed,zc->zc_nvlist_dst_size,&acl,0);
        }
        break;
    }
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    uint64_t index=0;m->row_count=0;m->next_cursor=0;
    for(nvpair_t *source=NULL;!error&&acl&&(source=nvlist_next_nvpair(acl,source));){
        nvlist_t *principals=NULL;error=nvpair_value_nvlist(source,&principals);
        for(nvpair_t *who=NULL;!error&&(who=nvlist_next_nvpair(principals,who));){
            nvlist_t *permissions=NULL;error=nvpair_value_nvlist(who,&permissions);
            const char *key=nvpair_name(who);
            if(!error&&(strlen(key)<3||zfs_deleg_verify_nvlist(principals)))error=EINVAL;
            for(nvpair_t *perm=NULL;!error&&(perm=nvlist_next_nvpair(permissions,perm));){
                if(index++<m->cursor)continue;
                if(m->row_count==OZ_MAX_ROWS){m->next_cursor=m->cursor+m->row_count;goto done;}
                struct oz_row *r=&m->rows[m->row_count];memset(r,0,sizeof(*r));
                strcpy(r->kind,"permission");
                char type=key[0]>='A'&&key[0]<='Z'?key[0]-'A'+'a':key[0];
                const char *label=type=='u'?"user":type=='g'?"group":type=='e'?"everyone":type=='s'?"set":type=='c'?"creator":NULL;
                if(!label){error=EINVAL;break;}
                int n=snprintf(r->name,sizeof(r->name),"%s%s%s",label,key[3]?":":"",key+3);
                if(n<0||(size_t)n>=sizeof(r->name)||
                    strlcpy(r->parent,nvpair_name(source),sizeof(r->parent))>=sizeof(r->parent)||
                    strlcpy(r->value,nvpair_name(perm),sizeof(r->value))>=sizeof(r->value)){error=ENAMETOOLONG;break;}
                snprintf(r->state,sizeof(r->state),"%s/%s",key[1]==ZFS_DELEG_LOCAL?"local":key[1]==ZFS_DELEG_DESCENDENT?"descendants":"definition",
                    key[0]>='A'&&key[0]<='Z'?"set-reference":"direct");
                m->row_count++;
            }
        }
    }
    if(!error&&m->cursor>index)error=EINVAL;
done:
    if(error){m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));}
    nvlist_free(acl);free(packed);free(zc);return error;
#endif
}

/* Typed native frontend; upstream validates the ACL and caller's authority. */
static int oz_permission_change_one(struct oz_message *m,cred_t *caller)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;return ENOTSUP;
#else
    if(!caller)return EACCES;
    if((m->operation!=65&&m->operation!=66)||m->flags!=OZ_CONFIRMED||
        dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    char who=0;uint64_t id=0;const char *digits=NULL,*setname=NULL;
    if(!strncmp(m->argument,"user:",5)){who='u';digits=m->argument+5;}
    else if(!strncmp(m->argument,"group:",6)){who='g';digits=m->argument+6;}
    else if(!strcmp(m->argument,"everyone"))who='e';
    else if(!strcmp(m->argument,"creator"))who='c';
    else if(!strncmp(m->argument,"set:",4)){
        who='s';setname=m->argument+4;
        if(permset_namecheck(setname,NULL,NULL))return EINVAL;
    }
    else return EINVAL;
    if(digits){
        if(!*digits)return EINVAL;
        for(;*digits;digits++){
            if(*digits<'0'||*digits>'9')return EINVAL;
            id=id*10+(*digits-'0');if(id>UINT32_MAX)return ERANGE;
        }
    }
    char text[sizeof(m->options)];strcpy(text,m->options);
    char *rest=text,*token,*permissions=NULL;int scope=3,seen=0,error=0,all=0;
    while((token=strsep(&rest,";"))!=NULL){
        if(!strncmp(token,"permissions=",12)&&!permissions)permissions=token+12;
        else if(!strcmp(token,"all=on")&&!all)all=1;
        else if(!strncmp(token,"scope=",6)&&!seen++){
            if(!strcmp(token+6,"local"))scope=1;
            else if(!strcmp(token+6,"descendants"))scope=2;
            else if(strcmp(token+6,"both"))return EINVAL;
        }else return EINVAL;
    }
    if(all?(m->operation!=66||permissions!=NULL):(!permissions||!*permissions))return EINVAL;
    if((who=='s'||who=='c')&&seen)return EINVAL;
    nvlist_t *acl=fnvlist_alloc(),*perms=fnvlist_alloc(),*sets=fnvlist_alloc();
    rest=permissions;
    while((token=strsep(&rest,","))!=NULL){
        nvlist_t *list=*token=='@'?sets:perms;
        if(!*token||nvlist_exists(list,token)){error=EINVAL;break;}
        fnvlist_add_boolean(list,token);
    }
    for(unsigned indirect=0;indirect<2&&!error;indirect++){
        nvlist_t *list=indirect?sets:perms;
        if(!all&&nvlist_empty(list))continue;
        char type=indirect?who-'a'+'A':who;
        for(int bit=1;bit<=2;bit*=2)if((scope&bit)||who=='s'||who=='c'){
            char key[ZFS_MAX_DELEG_NAME];
            zfs_deleg_whokey(key,(zfs_deleg_who_type_t)type,
                (who=='s'||who=='c')?ZFS_DELEG_NA:bit==1?ZFS_DELEG_LOCAL:ZFS_DELEG_DESCENDENT,
                who=='s'?(void *)setname:(void *)&id);
            if(all)fnvlist_add_boolean(acl,key);
            else fnvlist_add_nvlist(acl,key,list);
            if(who=='s'||who=='c')break;
        }
    }
    if(!error&&zfs_deleg_verify_nvlist(acl))error=EINVAL;
    char *packed=NULL;size_t size=0;zfs_cmd_t *zc=NULL;
    if(!error)error=nvlist_pack(acl,&packed,&size,NV_ENCODE_NATIVE,0);
    if(!error){zc=calloc(1,sizeof(*zc));if(!zc)error=ENOMEM;}
    if(!error&&strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name))>=sizeof(zc->zc_name))error=ENAMETOOLONG;
    struct oz_cred_scope *credentials=NULL;struct oz_ioctl_copy_scope *copy=NULL;
    if(!error)error=oz_cred_push(caller,&credentials);
    if(!error){
        zc->zc_perm_action=m->operation==66;
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
        struct oz_ioctl_range range={packed,size,OZ_IOCTL_INPUT};
        error=oz_ioctl_copy_enter(&range,1,&copy);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_SET_FSACL-ZFS_IOC_FIRST,zc,FKIOCTL);
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    free(zc);free(packed);fnvlist_free(sets);fnvlist_free(perms);fnvlist_free(acl);return error;
#endif
}
struct oz_permission_walk {struct oz_message *request;cred_t *caller;uint64_t completed;};
static int oz_permission_revoke_child(const char *name,void *arg)
{
    struct oz_permission_walk *walk=arg;
    if(strlcpy(walk->request->target,name,sizeof(walk->request->target))>=sizeof(walk->request->target))return ENAMETOOLONG;
    int error=oz_permission_change_one(walk->request,walk->caller);
    if(!error)walk->completed++;
    return error;
}
int oz_permission_change(struct oz_message *m,cred_t *caller)
{
    if(!(m->flags&OZ_RECURSIVE))return oz_permission_change_one(m,caller);
    if(m->operation!=66||m->flags!=(OZ_CONFIRMED|OZ_RECURSIVE)||
        dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    struct oz_message *child=malloc(sizeof(*child));if(!child)return ENOMEM;
    *child=*m;child->flags=OZ_CONFIRMED;
    struct oz_permission_walk walk={child,caller,0};
    /* Upstream visits children before parents, outside the config lock.
     * Each SET_FSACL rechecks caller authority. No atomic subtree promise. */
    int error=dmu_objset_find(m->target,oz_permission_revoke_child,&walk,DS_FIND_CHILDREN);
    snprintf(m->detail,sizeof(m->detail),"Authenticated recursive permission revoke completed=%llu errno=%d%s%s",
        (unsigned long long)walk.completed,error,error?" stopped-at=":"",error?child->target:"");
    free(child);return error;
}
/* Native pool policy writes. The property catalogue supplies types/index
 * spellings; upstream SPA validates values and owns the MOS transaction. */
int oz_pool_property_change(struct oz_message *m,cred_t *caller)
{
    int error=secpolicy_zfs(caller);if(error)return error;
    if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
    const char *equal=strchr(m->argument,'=');
    if(!equal||equal==m->argument)return EINVAL;
    size_t length=equal-m->argument;char name[ZAP_MAXNAMELEN];
    if(length>=sizeof(name))return ENAMETOOLONG;
    memcpy(name,m->argument,length);name[length]=0;
    const char *value=equal+1;uint64_t number=0;
    if(!strncmp(name,"feature@",8)) {
        spa_feature_t feature;
        if(zfeature_lookup_name(name+8,&feature))return ENOTSUP;
        if(strcmp(value,"enabled"))return EINVAL;
        spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
        if(!spa_writeable(spa))error=EROFS;
        else if(spa_suspended(spa))error=EAGAIN;
        struct oz_cred_scope *scope=NULL;nvlist_t *props=fnvlist_alloc();
        fnvlist_add_uint64(props,name,0);
        if(!error)error=oz_cred_push(caller,&scope);
        if(!error)error=spa_prop_set(spa,props);
        if(scope)VERIFY0(oz_cred_pop(scope));
        fnvlist_free(props);spa_close(spa,FTAG);return error;
    }
    zpool_prop_t prop=zpool_name_to_prop(name);
    int user=prop==ZPOOL_PROP_INVAL&&zfs_prop_user(name);
    if(prop==ZPOOL_PROP_INVAL&&!user)return ENOTSUP;
    if(!user&&(zpool_prop_readonly(prop)||zpool_prop_setonce(prop)))return EINVAL;
    /* Compatibility files and automatic device actions
     * need their own native adapters. Never report a
     * generic property write as proof those facilities are operational. */
    if(!user&&prop!=ZPOOL_PROP_COMMENT&&prop!=ZPOOL_PROP_DELEGATION&&
        prop!=ZPOOL_PROP_LISTSNAPS&&prop!=ZPOOL_PROP_FAILUREMODE&&
        prop!=ZPOOL_PROP_ASHIFT&&prop!=ZPOOL_PROP_DEDUP_TABLE_QUOTA&&
        prop!=ZPOOL_PROP_BOOTFS&&prop!=ZPOOL_PROP_CACHEFILE&&
        prop!=ZPOOL_PROP_MULTIHOST)return ENOTSUP;
    if(prop==ZPOOL_PROP_CACHEFILE&&!value[0]) {
        error=oz_cache_default_check();if(error)return error;
    }
    /* libzfs normally enforces the pool boundary before SPA resolves the
     * dataset object number. Typed callers bypass libzfs, so preserve that
     * boundary here. Empty clears the property, as in upstream libzfs.
     * Managing this metadata does not install an AROS ZFS boot loader. */
    if(prop==ZPOOL_PROP_BOOTFS&&value[0]) {
        size_t pool_length=strlen(m->target);
        if(dataset_namecheck(value,NULL,NULL)||strchr(value,'#')||
            strncmp(value,m->target,pool_length)||
            (value[pool_length]!='/'&&value[pool_length]!='\0'))return EINVAL;
    }
    zprop_type_t type=user?PROP_TYPE_STRING:zpool_prop_get_type(prop);
    if(type==PROP_TYPE_INDEX) {
        if(zpool_prop_string_to_index(prop,value,&number))return EINVAL;
    } else if(type==PROP_TYPE_NUMBER) {
        if(!strcmp(value,"none"))number=0;
        else if(prop==ZPOOL_PROP_DEDUP_TABLE_QUOTA&&!strcmp(value,"auto"))number=UINT64_MAX;
        else {
            zfs_num_error_t parsed=zfs_nicestrtonum_parse(value,&number,NULL);
            if(parsed!=ZFS_NUM_OK)return parsed==ZFS_NUM_TOO_LARGE?ERANGE:EINVAL;
            if(prop==ZPOOL_PROP_DEDUP_TABLE_QUOTA&&!number)return EINVAL;
        }
        if(prop==ZPOOL_PROP_ASHIFT&&number&&(number<ASHIFT_MIN||number>ASHIFT_MAX))return EINVAL;
    }
    spa_t *spa=NULL;error=spa_open(m->target,&spa,FTAG);if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa)&&prop!=ZPOOL_PROP_FAILUREMODE)error=EAGAIN;
    nvlist_t *props=NULL;struct oz_cred_scope *scope=NULL;
    if(!error)error=nvlist_alloc(&props,NV_UNIQUE_NAME,KM_SLEEP);
    if(!error)error=type==PROP_TYPE_STRING?nvlist_add_string(props,name,value):nvlist_add_uint64(props,name,number);
    if(!error)error=oz_cred_push(caller,&scope);
    if(!error)error=spa_prop_set(spa,props);
    if(!error&&prop==ZPOOL_PROP_CACHEFILE) {
        /* Cache policy is not a MOS property. Mirror upstream's dedicated
         * ioctl path rather than reporting success after spa_prop_set alone.
         * Upstream posts cache-write failures and schedules retries. */
        spa_namespace_enter(FTAG);
        spa_configfile_set(spa,props,B_FALSE);
        spa_write_cachefile(spa,B_FALSE,B_TRUE,B_FALSE);
        spa_namespace_exit(FTAG);
    }
    if(scope)VERIFY0(oz_cred_pop(scope));
    nvlist_free(props);spa_close(spa,FTAG);return error;
}
static int put_text(char *dst,size_t size,const char *src)
{
    if(strlen(src)>=size)return ENAMETOOLONG;
    strcpy(dst,src);return 0;
}
/* One authenticated upstream VDEV_GET_PROPS request. A null property asks for
 * the dynamic strings stored in the vdev property ZAP. */
#ifdef OZ_NATIVE_DISPATCHER
static int vdev_property_fetch(struct oz_message *m,cred_t *caller,uint64_t guid,
    const char *property,vdev_prop_t prop,nvlist_t **output)
{
    nvlist_t *input=fnvlist_alloc(),*wanted=fnvlist_alloc();
    fnvlist_add_uint64(input,ZPOOL_VDEV_PROPS_GET_VDEV,guid);
    if(property) {
        fnvlist_add_uint64(wanted,property,prop);
        fnvlist_add_nvlist(input,ZPOOL_VDEV_PROPS_GET_PROPS,wanted);
    }
    nvlist_free(wanted);
    char *packed=NULL;size_t packed_size=0;
    int error=nvlist_pack(input,&packed,&packed_size,NV_ENCODE_NATIVE,0);nvlist_free(input);
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));void *reply=malloc(65536);
    if(!zc||!reply)error=ENOMEM;
    struct oz_cred_scope *credentials=NULL;struct oz_ioctl_copy_scope *copy=NULL;
    struct oz_ioctl_range ranges[2]={{packed,packed_size,OZ_IOCTL_INPUT},{reply,65536,OZ_IOCTL_OUTPUT}};
    if(!error)error=oz_cred_push(caller,&credentials);
    if(!error)error=oz_ioctl_copy_enter(ranges,2,&copy);
    if(!error) {
        strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=packed_size;
        zc->zc_nvlist_dst=(uintptr_t)reply;zc->zc_nvlist_dst_size=65536;
        error=zfsdev_ioctl_common(ZFS_IOC_VDEV_GET_PROPS-ZFS_IOC_FIRST,zc,FKIOCTL);
        if(!error&&(!zc->zc_nvlist_dst_filled||zc->zc_nvlist_dst_size>65536))error=EOVERFLOW;
        if(!error)error=nvlist_unpack(reply,zc->zc_nvlist_dst_size,output,0);
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    free(zc);free(reply);free(packed);return error;
}
static int vdev_property_get_one(struct oz_message *m,cred_t *caller,uint64_t guid)
{
    vdev_prop_t prop=vdev_name_to_prop(m->options);
    if(prop==VDEV_PROP_INVAL&&!zfs_prop_user(m->options))return EINVAL;
    if(prop==VDEV_PROP_INVAL)prop=VDEV_PROP_USERPROP;
    nvlist_t *output=NULL,*entry=NULL;
    int error=vdev_property_fetch(m,caller,guid,m->options,prop,&output);
    uint64_t number=0,source=0;const char *text=NULL;char numeric[32];
    int missing_property=0;
    if(!error){error=nvlist_lookup_nvlist(output,m->options,&entry);missing_property=error==ENOENT;}
    if(missing_property) {
        error=0;source=ZPROP_SRC_DEFAULT;
        if(prop==VDEV_PROP_USERPROP)text="-";
        else if(vdev_prop_get_type(prop)==PROP_TYPE_STRING) {
            text=vdev_prop_default_string(prop);if(!text)text="-";
        } else number=vdev_prop_default_numeric(prop);
    } else if(!error) {
        error=nvlist_lookup_uint64(entry,ZPROP_SOURCE,&source);
        if(!error&&nvlist_lookup_string(entry,ZPROP_VALUE,&text))
            error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&number);
    }
    if(!error&&!text) {
        if(!error&&prop!=VDEV_PROP_USERPROP&&vdev_prop_get_type(prop)==PROP_TYPE_INDEX)
            error=vdev_prop_index_to_string(prop,number,&text);
        else if(!error){snprintf(numeric,sizeof(numeric),"%llu",(unsigned long long)number);text=numeric;}
    }
    m->row_count=0;m->next_cursor=0;m->detail[0]=0;
    if(!error) {
        struct oz_row *r=&m->rows[0];memset(r,0,sizeof(*r));strcpy(r->kind,"property");
        error=put_text(r->name,sizeof(r->name),m->options);
        if(!error)error=put_text(r->parent,sizeof(r->parent),m->target);
        snprintf(r->state,sizeof(r->state),"source=%llu",(unsigned long long)source);
        if(!error&&strlen(text)>=sizeof(r->value)) {
            error=put_text(m->detail,sizeof(m->detail),text);
            if(!error){strcpy(r->kind,"property-long");strcpy(r->value,"Full value in response detail");r->total=strlen(text);}
        } else if(!error)error=put_text(r->value,sizeof(r->value),text);
        if(!error)m->row_count=1;
    }
    nvlist_free(output);return error;
}
struct vdev_all_context {
    struct oz_message *message;
    cred_t *caller;
    uint64_t guid,index;
    int error,full;
    char failed[64];
};
static int vdev_all_add(struct vdev_all_context *context,const char *name)
{
    uint64_t index=context->index++;
    /* Cursors are opaque catalogue positions, not row counts. Skipped pages
     * must not reissue every earlier upstream property query. Inapplicable
     * catalogue entries still consume a position, keeping later tokens stable
     * within the same live view. */
    if(index<context->message->cursor)return 0;
    struct oz_message query={0};query.operation=80;
    if(strlcpy(query.target,context->message->target,sizeof(query.target))>=sizeof(query.target)||
       strlcpy(query.argument,context->message->argument,sizeof(query.argument))>=sizeof(query.argument)||
       strlcpy(query.options,name,sizeof(query.options))>=sizeof(query.options))return ENAMETOOLONG;
    int error=vdev_property_get_one(&query,context->caller,context->guid);if(error)return error;
    if(query.row_count!=1)return EPROTO;
    if(context->message->row_count==OZ_MAX_ROWS) {
        context->message->next_cursor=index;context->full=1;return 0;
    }
    context->message->rows[context->message->row_count++]=query.rows[0];return 0;
}
static int vdev_all_registered(int property,void *opaque)
{
    struct vdev_all_context *context=opaque;
    if(context->full||context->error)return property;
    const char *name=vdev_prop_to_name(property);
    if(!name)context->error=EINVAL;
    else {
        context->error=vdev_all_add(context,name);
        /* Upstream's exact formatter returns -1 for an indexed value that is
         * not applicable to this vdev type (for example autosit on a mirror
         * leaf). `zpool get -v all` omits such entries; a real errno remains
         * fatal so a partial page is never reported as complete. */
        if(context->error==-1||context->error==ENOENT||context->error==ENOTSUP)context->error=0;
        else if(context->error)strlcpy(context->failed,name,sizeof(context->failed));
    }
    return context->full||context->error?property:ZPROP_CONT;
}
static int vdev_property_get_all(struct oz_message *m,cred_t *caller,uint64_t guid)
{
    struct vdev_all_context context={.message=m,.caller=caller,.guid=guid};
    nvlist_t *stored=NULL;m->row_count=0;m->next_cursor=0;m->detail[0]=0;
    (void)zprop_iter_common(vdev_all_registered,&context,B_FALSE,B_TRUE,ZFS_TYPE_VDEV);
    int error=context.error;
    if(!context.full&&!error)error=vdev_property_fetch(m,caller,guid,NULL,VDEV_PROP_INVAL,&stored);
    for(nvpair_t *pair=context.full||error?NULL:nvlist_next_nvpair(stored,NULL);
        pair&&!context.full&&!error;pair=nvlist_next_nvpair(stored,pair)) {
        const char *name=nvpair_name(pair);
        if(zfs_prop_user(name)) {
            error=vdev_all_add(&context,name);
            if(error)strlcpy(context.failed,name,sizeof(context.failed));
        }
    }
    /* The catalogue cursor is opaque, but it still must name this live view.
     * Equality is the valid exhausted position; anything beyond the number
     * of candidates is malformed or stale and must not look like EOF. */
    if(!context.full&&!error&&m->cursor>context.index)error=EINVAL;
    if(error) {
        m->row_count=0;m->next_cursor=0;memset(m->rows,0,sizeof(m->rows));
        snprintf(m->detail,sizeof(m->detail),"Authenticated vdev all-property query stopped at %s errno=%d",
            context.failed[0]?context.failed:"setup",error);
    } else snprintf(m->detail,sizeof(m->detail),
        "Authenticated vdev all-property live page rows=%u errno=0; property-long rows require an exact query",
        (unsigned)m->row_count);
    nvlist_free(stored);return error;
}
#endif
int oz_vdev_property_get(struct oz_message *m,cred_t *caller)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;return ENOTSUP;
#else
    int all=!strcmp(m->options,"all");
    if(m->operation!=80||m->flags||m->job_id||!m->argument[0]||!m->options[0]||
       (m->cursor&&!all))return EINVAL;
    if(strspn(m->argument,"0123456789")!=strlen(m->argument))return EINVAL;
    errno=0;uint64_t guid=strtoull(m->argument,NULL,10);
    if(errno||!guid)return EINVAL;
    return all?vdev_property_get_all(m,caller,guid):vdev_property_get_one(m,caller,guid);
#endif
}
/* libzfs size validation plus zfs_check_settable's large-block gate. The
 * existing mounted ZPL callback applies this upstream DSL property live. */
static int recordsize_property_change(struct oz_message *m,cred_t *caller,const char *value)
{
    if(strchr(m->target,'@'))return EINVAL;
    int inherit=m->operation==42;uint64_t number=0;
    if(!inherit) {
        zfs_num_error_t parsed=zfs_nicestrtonum_parse(value,&number,NULL);
        if(parsed!=ZFS_NUM_OK)return parsed==ZFS_NUM_TOO_LARGE?ERANGE:EINVAL;
        if(number<SPA_MINBLOCKSIZE||number>SPA_MAXBLOCKSIZE||!ISP2(number))return EINVAL;
    }
    int error=oz_dataset_property_perms(m->target,"recordsize",caller);
    if(error)return error;
    objset_t *os=NULL;error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    spa_t *spa=dmu_objset_spa(os);
    if(dmu_objset_type(os)!=DMU_OST_ZFS)error=EINVAL;
    if(!error&&!spa_writeable(spa))error=EROFS;
    if(!error&&!inherit&&number>SPA_OLD_MAXBLOCKSIZE) {
        extern uint_t zfs_max_recordsize;
        if(number>zfs_max_recordsize)error=ERANGE;
        else if(!spa_feature_is_enabled(spa,SPA_FEATURE_LARGE_BLOCKS))error=ENOTSUP;
    }
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;error=oz_cred_push(caller,&scope);
    if(!error) {
        error=inherit?dsl_prop_inherit(m->target,"recordsize",ZPROP_SRC_INHERITED):
            dsl_prop_set_int(m->target,"recordsize",ZPROP_SRC_LOCAL,number);
        VERIFY0(oz_cred_pop(scope));
    }
    return error;
}
/* Filesystem switches use upstream DSL callbacks. Native zvol open/status/write
 * boundaries resolve effective readonly from DSL, including parent changes;
 * other filesystem-only switches remain unsupported for volumes. */
static int filesystem_switch_property_change(struct oz_message *m,cred_t *caller,
    const char *property,const char *value)
{
    if(strchr(m->target,'@'))return EINVAL;
    int inherit=m->operation==42;uint64_t number=0;
    if(!inherit&&zfs_prop_string_to_index(zfs_name_to_prop(property),value,&number))return EINVAL;
    int error=oz_dataset_property_perms(m->target,property,caller);
    if(error)return error;
    objset_t *os=NULL;error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    if(dmu_objset_type(os)!=DMU_OST_ZFS&&
        !(dmu_objset_type(os)==DMU_OST_ZVOL&&!strcmp(property,"readonly")))error=ENOTSUP;
    if(!error&&!spa_writeable(dmu_objset_spa(os)))error=EROFS;
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;error=oz_cred_push(caller,&scope);
    if(!error) {
        error=inherit?dsl_prop_inherit(m->target,property,ZPROP_SRC_INHERITED):
            dsl_prop_set_int(m->target,property,ZPROP_SRC_LOCAL,number);
        VERIFY0(oz_cred_pop(scope));
    }
    return error;
}
/* Follow zfs_check_settable and zfs_prop_set_special's compression path.
 * Parsing comes from the upstream index table, including compression levels.
 * Feature enablement is an explicit pool-admin operation, never implicit here. */
static int compression_property_change(struct oz_message *m,cred_t *caller,const char *value)
{
    if(strchr(m->target,'@'))return EINVAL;
    uint64_t number=0;
    int inherit=m->operation==42;
    if(!inherit&&(!value||zfs_prop_string_to_index(ZFS_PROP_COMPRESSION,value,&number)))return EINVAL;
    int error=oz_dataset_property_perms(m->target,"compression",caller);
    if(error)return error;
    objset_t *os=NULL;
    error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    spa_t *spa=dmu_objset_spa(os);
    if(dmu_objset_type(os)!=DMU_OST_ZFS&&dmu_objset_type(os)!=DMU_OST_ZVOL)error=EINVAL;
    if(!error&&!spa_writeable(spa))error=EROFS;
    if(!error&&!inherit) {
        uint64_t algo=ZIO_COMPRESS_ALGO(number),version=spa_version(spa);
        if(algo>=ZIO_COMPRESS_GZIP_1&&algo<=ZIO_COMPRESS_GZIP_9&&version<SPA_VERSION_GZIP_COMPRESSION)error=ENOTSUP;
        if(algo==ZIO_COMPRESS_ZLE&&version<SPA_VERSION_ZLE_COMPRESSION)error=ENOTSUP;
        if(algo==ZIO_COMPRESS_LZ4&&!spa_feature_is_enabled(spa,SPA_FEATURE_LZ4_COMPRESS))error=ENOTSUP;
        if(algo==ZIO_COMPRESS_ZSTD&&!spa_feature_is_enabled(spa,SPA_FEATURE_ZSTD_COMPRESS))error=ENOTSUP;
    }
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    nvlist_t *props=NULL;
    if(!inherit) {
        error=nvlist_alloc(&props,NV_UNIQUE_NAME,KM_SLEEP);
        if(!error)error=nvlist_add_uint64(props,"compression",number);
        if(error){nvlist_free(props);return error;}
    }
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(!error) {
        if(inherit)error=dsl_prop_inherit(m->target,"compression",ZPROP_SRC_INHERITED);
        else {
            error=dsl_dataset_set_compression(m->target,ZPROP_SRC_LOCAL,number);
            if(!error)error=dsl_props_set(m->target,ZPROP_SRC_LOCAL,props);
        }
        VERIFY0(oz_cred_pop(scope));
    }
    nvlist_free(props);return error;
}
/* Space properties use the same special setters as zfs_prop_set_special,
 * never a generic DSL property write that omits accounting/limit checks.
 * Numeric syntax is shared with upstream libzfs; 'none' disables the limit.
 * Additional built-ins remain separate integration work. */
static int space_property_change(struct oz_message *m,cred_t *caller,
    const char *property,const char *value)
{
    zfs_prop_t prop=zfs_name_to_prop(property);
    int count_limit=prop==ZFS_PROP_FILESYSTEM_LIMIT||prop==ZFS_PROP_SNAPSHOT_LIMIT;
    if(prop!=ZFS_PROP_QUOTA&&prop!=ZFS_PROP_REFQUOTA&&
        prop!=ZFS_PROP_RESERVATION&&prop!=ZFS_PROP_REFRESERVATION&&!count_limit)return ENOTSUP;
    if(strchr(m->target,'@'))return EINVAL;
    if(m->operation!=41)return EINVAL; /* these properties are not inheritable */
    if(!value||!*value)return EINVAL;
    uint64_t number=count_limit?UINT64_MAX:0;
    if(strcmp(value,"none")) {
        zfs_num_error_t parsed=zfs_nicestrtonum_parse(value,&number,NULL);
        if(parsed!=ZFS_NUM_OK)return parsed==ZFS_NUM_TOO_LARGE?ERANGE:EINVAL;
        /* Match zprop_parse_value: quota/refquota must use the explicit
         * 'none' spelling to disable a limit. Numeric zero (including a
         * fraction truncated to zero bytes) must not silently disable it. */
        if(!number&&(prop==ZFS_PROP_QUOTA||prop==ZFS_PROP_REFQUOTA))return EINVAL;
    }
    int error=oz_dataset_property_perms(m->target,zfs_prop_to_name(prop),caller);
    if(error)return error;
    objset_t *os=NULL;
    error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    dmu_objset_type_t type=dmu_objset_type(os);
    if(type!=DMU_OST_ZFS&&type!=DMU_OST_ZVOL)error=EINVAL;
    else if(!zfs_prop_valid_for_type(prop,type==DMU_OST_ZFS?ZFS_TYPE_FILESYSTEM:ZFS_TYPE_VOLUME,B_FALSE))error=EINVAL;
    if(!error&&!spa_writeable(dmu_objset_spa(os)))error=EROFS;
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    nvlist_t *props=NULL;
    if(count_limit) {
        error=nvlist_alloc(&props,NV_UNIQUE_NAME,KM_SLEEP);
        if(!error)error=nvlist_add_uint64(props,property,number);
        if(error){nvlist_free(props);return error;}
    }
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error){nvlist_free(props);return error;}
    switch(prop) {
    case ZFS_PROP_QUOTA:error=dsl_dir_set_quota(m->target,ZPROP_SRC_LOCAL,number);break;
    case ZFS_PROP_REFQUOTA:error=dsl_dataset_set_refquota(m->target,ZPROP_SRC_LOCAL,number);break;
    case ZFS_PROP_RESERVATION:error=dsl_dir_set_reservation(m->target,ZPROP_SRC_LOCAL,number);break;
    case ZFS_PROP_REFRESERVATION:error=dsl_dataset_set_refreservation(m->target,ZPROP_SRC_LOCAL,number);break;
    case ZFS_PROP_FILESYSTEM_LIMIT:
    case ZFS_PROP_SNAPSHOT_LIMIT:
        if(number!=UINT64_MAX)error=dsl_dir_activate_fs_ss_limit(m->target);
        if(!error)error=dsl_props_set(m->target,ZPROP_SRC_LOCAL,props);
        break;
    default:error=ENOTSUP;break;
    }
    VERIFY0(oz_cred_pop(scope));
    nvlist_free(props);
    return error;
}
/* volsize is stored by upstream zvol code, not by generic DSL property
 * assignment. Do not inherit it or silently accept a filesystem target. */
static int volume_size_property_change(struct oz_message *m,cred_t *caller,const char *value)
{
#ifdef OZ_NATIVE_DISPATCHER
    if(m->operation!=41||strchr(m->target,'@')||!value||!*value)return EINVAL;
    uint64_t number=0;
    zfs_num_error_t parsed=zfs_nicestrtonum_parse(value,&number,NULL);
    if(parsed!=ZFS_NUM_OK)return parsed==ZFS_NUM_TOO_LARGE?ERANGE:EINVAL;
    if(!number)return EINVAL;
    int error=oz_dataset_property_perms(m->target,"volsize",caller);
    if(error)return error;
    objset_t *os=NULL;error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    if(dmu_objset_type(os)!=DMU_OST_ZVOL)error=EINVAL;
    else if(!spa_writeable(dmu_objset_spa(os)))error=EROFS;
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;error=oz_cred_push(caller,&scope);
    if(!error){error=zvol_set_volsize(m->target,number);VERIFY0(oz_cred_pop(scope));}
    return error;
#else
    return ENOTSUP;
#endif
}
static int accounting_default_property(zfs_prop_t prop)
{
    return prop==ZFS_PROP_DEFAULTUSERQUOTA||prop==ZFS_PROP_DEFAULTGROUPQUOTA||
        prop==ZFS_PROP_DEFAULTPROJECTQUOTA||prop==ZFS_PROP_DEFAULTUSEROBJQUOTA||
        prop==ZFS_PROP_DEFAULTGROUPOBJQUOTA||prop==ZFS_PROP_DEFAULTPROJECTOBJQUOTA;
}
/* Marshal the same typed SET_PROP values as libzfs, with numeric AROS IDs.
 * The dispatcher owns quota feature, target, privilege and transaction checks. */
static int accounting_property_change(struct oz_message *m,cred_t *caller,const char *property,const char *value)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;(void)property;(void)value;return ENOTSUP;
#else
    if(m->operation!=41||!value||!*value||strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    uint64_t number=0,id=0;
    if(strcmp(value,"none")){
        zfs_num_error_t parsed=zfs_nicestrtonum_parse(value,&number,NULL);
        if(parsed!=ZFS_NUM_OK)return parsed==ZFS_NUM_TOO_LARGE?ERANGE:EINVAL;
    }
    zfs_prop_t prop=zfs_name_to_prop(property);
    nvlist_t *props=fnvlist_alloc();int error=0;
    if(accounting_default_property(prop))fnvlist_add_uint64(props,property,number);
    else {
        unsigned type=0;
        for(;type<ZFS_NUM_USERQUOTA_PROPS;type++)
            if(!strncmp(property,zfs_userquota_prop_prefixes[type],strlen(zfs_userquota_prop_prefixes[type])))break;
        if(type==ZFS_NUM_USERQUOTA_PROPS||!(type&1)){error=EINVAL;goto done;}
        const char *p=property+strlen(zfs_userquota_prop_prefixes[type]);
        if(!*p){error=EINVAL;goto done;}
        for(;*p;p++){
            if(*p<'0'||*p>'9'){error=EINVAL;goto done;}
            id=id*10+(*p-'0');if(id>UINT32_MAX){error=ERANGE;goto done;}
        }
        char encoded[96];snprintf(encoded,sizeof(encoded),"%s%llx-",zfs_userquota_prop_prefixes[type],(unsigned long long)id);
        uint64_t values[]={type,id,number};fnvlist_add_uint64_array(props,encoded,values,3);
    }
    char *packed=NULL;size_t size=0;zfs_cmd_t *zc=calloc(1,sizeof(*zc));
    struct oz_cred_scope *credentials=NULL;struct oz_ioctl_copy_scope *copy=NULL;
    if(!zc){error=ENOMEM;goto done;}
    error=nvlist_pack(props,&packed,&size,NV_ENCODE_NATIVE,0);
    if(!error)error=oz_cred_push(caller,&credentials);
    if(!error){
        strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
        struct oz_ioctl_range range={packed,size,OZ_IOCTL_INPUT};error=oz_ioctl_copy_enter(&range,1,&copy);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_SET_PROP-ZFS_IOC_FIRST,zc,FKIOCTL);
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    free(zc);free(packed);
done:
    fnvlist_free(props);return error;
#endif
}
/* Engine policy properties have native DMU/ARC consumers. Use the real ioctl
 * setters so upstream permission, readonly-pool and special-property checks
 * remain authoritative, rather than merely storing a DSL value. */
static int engine_policy_property_change(struct oz_message *m,cred_t *caller,
    const char *property,const char *value)
{
#ifndef OZ_NATIVE_DISPATCHER
    (void)m;(void)caller;(void)property;(void)value;return ENOTSUP;
#else
    zfs_prop_t prop=zfs_name_to_prop(property);uint64_t number=0;
    if(strchr(m->target,'@')||strchr(m->target,'#'))return EINVAL;
    if(m->operation==41&&(!value||zfs_prop_string_to_index(prop,value,&number)))return EINVAL;
    int error=oz_dataset_property_perms(m->target,property,caller);
    if(error)return error;
    objset_t *os=NULL;error=dmu_objset_hold(m->target,FTAG,&os);
    if(error)return error;
    dmu_objset_type_t type=dmu_objset_type(os);
    if(type!=DMU_OST_ZFS&&type!=DMU_OST_ZVOL)error=EINVAL;
    else if(!zfs_prop_valid_for_type(prop,type==DMU_OST_ZFS?ZFS_TYPE_FILESYSTEM:ZFS_TYPE_VOLUME,B_FALSE))error=EINVAL;
    /* Match libzfs's extra encrypted-dataset copies validation. */
    if(!error&&m->operation==41&&prop==ZFS_PROP_COPIES&&number>2&&
        dmu_objset_ds(os)->ds_dir->dd_crypto_obj)error=EINVAL;
    dmu_objset_rele(os,FTAG);
    if(error)return error;
    zfs_cmd_t *zc=calloc(1,sizeof(*zc));if(!zc)return ENOMEM;
    nvlist_t *props=fnvlist_alloc();char *packed=NULL;size_t size=0;
    strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
    if(m->operation==41){
        fnvlist_add_uint64(props,property,number);
        error=nvlist_pack(props,&packed,&size,NV_ENCODE_NATIVE,0);
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
    }else strlcpy(zc->zc_value,property,sizeof(zc->zc_value));
    struct oz_cred_scope *credentials=NULL;struct oz_ioctl_copy_scope *copy=NULL;
    if(!error)error=oz_cred_push(caller,&credentials);
    if(!error&&packed){
        struct oz_ioctl_range range={packed,size,OZ_IOCTL_INPUT};
        error=oz_ioctl_copy_enter(&range,1,&copy);
    }
    if(!error)error=zfsdev_ioctl_common((m->operation==41?ZFS_IOC_SET_PROP:ZFS_IOC_INHERIT_PROP)-ZFS_IOC_FIRST,zc,FKIOCTL);
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(credentials)VERIFY0(oz_cred_pop(credentials));
    free(zc);free(packed);fnvlist_free(props);return error;
#endif
}
/* User properties retain USERPROP delegation. All built-ins are dispatched
 * separately so each can acquire its upstream validation and special setter. */
static int single_property_change(struct oz_message *m,cred_t *caller)
{
    if(!caller)return EACCES;
    if(dataset_namecheck(m->target,NULL,NULL))return EINVAL;
    if(m->operation!=41&&m->operation!=42)return ENOTSUP;
    if(m->options[0]) {
        if(m->operation!=42||strcmp(m->options,"received=on"))return EINVAL;
#ifndef OZ_NATIVE_DISPATCHER
        return ENOTSUP;
#else
        /* Match zfs inherit -S, including special property handling and
         * delegated permissions. Do not emulate it with a local DSL setter. */
        if(m->flags!=OZ_CONFIRMED||m->cursor||m->job_id||
            strchr(m->argument,'='))return EINVAL;
        zfs_cmd_t *zc=calloc(1,sizeof(*zc));if(!zc)return ENOMEM;
        strlcpy(zc->zc_name,m->target,sizeof(zc->zc_name));
        strlcpy(zc->zc_value,m->argument,sizeof(zc->zc_value));
        zc->zc_cookie=1;
        struct oz_cred_scope *credentials=NULL;
        int error=oz_cred_push(caller,&credentials);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_INHERIT_PROP-ZFS_IOC_FIRST,zc,FKIOCTL);
        if(credentials)VERIFY0(oz_cred_pop(credentials));
        free(zc);return error;
#endif
    }
    char property[sizeof(m->argument)];
    strlcpy(property,m->argument,sizeof(property));
    char *value=strchr(property,'=');
    if(m->operation==41) {
        if(!value)return EINVAL;
        *value++=0; /* remaining equals/semicolons are literal value bytes */
    } else if(value)return EINVAL;
    if(zfs_prop_userquota(property)||accounting_default_property(zfs_name_to_prop(property)))
        return accounting_property_change(m,caller,property,value);
    if(!strcmp(property,"compression"))return compression_property_change(m,caller,value);
    if(!strcmp(property,"recordsize"))return recordsize_property_change(m,caller,value);
    if(!strcmp(property,"volsize"))return volume_size_property_change(m,caller,value);
    if(!strcmp(property,"copies")||!strcmp(property,"primarycache")||
        !strcmp(property,"secondarycache")||!strcmp(property,"logbias")||
        !strcmp(property,"redundant_metadata")||!strcmp(property,"checksum")||
        !strcmp(property,"dedup")||!strcmp(property,"sync")||!strcmp(property,"dnodesize"))
        return engine_policy_property_change(m,caller,property,value);
    if(!strcmp(property,"readonly")||!strcmp(property,"atime")||!strcmp(property,"relatime")||!strcmp(property,"exec"))
        return filesystem_switch_property_change(m,caller,property,value);
    if(!zfs_prop_user(property))return space_property_change(m,caller,property,value);
    int error=oz_dataset_userprop_perms(m->target,caller);
    if(error)return error;
    spa_t *spa=NULL;
    error=spa_open(m->target,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    spa_close(spa,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    if(m->operation==42)error=dsl_prop_inherit(m->target,property,ZPROP_SRC_INHERITED);
    else {
        nvlist_t *props=NULL;
        error=nvlist_alloc(&props,NV_UNIQUE_NAME,KM_SLEEP);
        if(!error)error=nvlist_add_string(props,property,value);
        if(!error)error=dsl_props_set(m->target,ZPROP_SRC_LOCAL,props);
        nvlist_free(props);
    }
    VERIFY0(oz_cred_pop(scope));
    return error;
}
struct recursive_inherit_context {
    const struct oz_message *request;
    cred_t *caller;
    uint64_t completed;
    zfs_prop_t property;
};
static int recursive_inherit_one(const char *dataset,void *opaque)
{
    struct recursive_inherit_context *context=opaque;
    if(strchr(dataset,'@'))return 0;
    if(context->property!=ZPROP_INVAL) {
        objset_t *os=NULL;int error=dmu_objset_hold(dataset,FTAG,&os);
        if(error)return error;
        dmu_objset_type_t type=dmu_objset_type(os);
        int valid=(type==DMU_OST_ZFS||type==DMU_OST_ZVOL)&&
            zfs_prop_valid_for_type(context->property,
                type==DMU_OST_ZFS?ZFS_TYPE_FILESYSTEM:ZFS_TYPE_VOLUME,B_FALSE);
        dmu_objset_rele(os,FTAG);
        /* Match `zfs inherit -r`: a built-in property that does not apply to
         * one descendant type is skipped rather than aborting the walk. */
        if(!valid)return 0;
    }
    struct oz_message request={0};request.operation=42;request.flags=OZ_CONFIRMED;
    if(strlcpy(request.target,dataset,sizeof(request.target))>=sizeof(request.target)||
       strlcpy(request.argument,context->request->argument,sizeof(request.argument))>=sizeof(request.argument)||
       strlcpy(request.options,context->request->options,sizeof(request.options))>=sizeof(request.options))
        return ENAMETOOLONG;
    int error=single_property_change(&request,context->caller);
    if(!error)context->completed++;
    return error;
}
int oz_user_property_change(struct oz_message *m,cred_t *caller)
{
    if(m->operation!=42||!(m->flags&OZ_RECURSIVE))
        return single_property_change(m,caller);
    int error=0;
    if(!caller)error=EACCES;
    if(!error&&(dataset_namecheck(m->target,NULL,NULL)||strchr(m->target,'@')||strchr(m->target,'#')))error=EINVAL;
    zfs_prop_t property=zfs_name_to_prop(m->argument);
    int received=!strcmp(m->options,"received=on");
    if(!error&&property==ZPROP_INVAL&&!zfs_prop_user(m->argument))error=ENOTSUP;
    if(!error&&property!=ZPROP_INVAL&&(zfs_prop_readonly(property)||
        (!received&&!zfs_prop_inheritable(property))||
        (received&&(property==ZFS_PROP_VOLSIZE||property==ZFS_PROP_VERSION))))error=EINVAL;
    struct recursive_inherit_context context={
        .request=m,.caller=caller,.property=property
    };
    if(!error)error=dmu_objset_find(m->target,recursive_inherit_one,&context,DS_FIND_CHILDREN);
    snprintf(m->detail,sizeof(m->detail),
        "Authenticated recursive property inherit completed=%llu errno=%d%s",
        (unsigned long long)context.completed,error,
        error?(context.completed?"; earlier changes remain, inspect before retrying":"; nothing changed"):"");
    return error;
}
int oz_property_get(struct oz_message *m)
{
    int pool=m->operation==31,rc=0;
    nvlist_t *props=NULL,*entry=NULL;spa_t *spa=NULL;objset_t *os=NULL;
    uint64_t number=0,source_number=0;const char *text=NULL,*source=NULL;
    char value_buf[ZAP_MAXVALUELEN],source_buf[ZFS_MAX_DATASET_NAME_LEN];
    zfs_prop_t zp=ZPROP_INVAL;zpool_prop_t pp=ZPOOL_PROP_INVAL;
    int numeric=0;
    if(pool) {
        if(!strncmp(m->argument,"feature@",8)) {
            spa_feature_t feature;
            if(zfeature_lookup_name(m->argument+8,&feature))return ENOTSUP;
            rc=spa_open(m->target,&spa,FTAG);if(rc)return rc;
            const char *state=spa_feature_is_active(spa,feature)?"active":
                spa_feature_is_enabled(spa,feature)?"enabled":"disabled";
            struct oz_row *row=&m->rows[0];
            strcpy(row->kind,"property");strcpy(row->state,"upstream");
            rc=put_text(row->name,sizeof(row->name),m->argument);
            if(!rc)rc=put_text(row->parent,sizeof(row->parent),m->target);
            if(!rc){strcpy(row->value,state);m->row_count=1;}
            spa_close(spa,FTAG);return rc;
        }
        pp=zpool_name_to_prop(m->argument);if(pp==ZPOOL_PROP_INVAL&&!zfs_prop_user(m->argument))return ENOTSUP;
        rc=spa_open(m->target,&spa,FTAG);if(rc)return rc;
        props=fnvlist_alloc();rc=spa_prop_get(spa,props);spa_close(spa,FTAG);
    } else {
        zp=zfs_name_to_prop(m->argument);
        if(zp==ZPROP_INVAL&&!zfs_prop_user(m->argument))return ENOTSUP;
        rc=dmu_objset_hold(m->target,FTAG,&os);if(rc)return rc;
        rc=dsl_prop_get_all(os,&props);
        if(!rc)dmu_objset_stats(os,props);
        if(!rc&&(zp==ZFS_PROP_VOLSIZE||zp==ZFS_PROP_VOLBLOCKSIZE)) {
            dmu_objset_stats_t stats={0};dmu_objset_fast_stat(os,&stats);
            if(dmu_objset_type(os)!=DMU_OST_ZVOL)rc=EINVAL;
            else if(stats.dds_inconsistent)rc=EBUSY;
#ifdef OZ_NATIVE_DISPATCHER
            else rc=zvol_get_stats(os,props);
#else
            else rc=ENOTSUP;
#endif
        }
        if(!rc&&(accounting_default_property(zp)||zp==ZFS_PROP_VERSION||zp==ZFS_PROP_NORMALIZE||
            zp==ZFS_PROP_UTF8ONLY||zp==ZFS_PROP_CASE)) {
            /* These live in the ZPL master node, not the DSL property ZAP.
             * Match upstream OBJSET_ZPLPROPS rather than returning DSL defaults. */
            dmu_objset_stats_t stats={0};dmu_objset_fast_stat(os,&stats);
            if(dmu_objset_type(os)!=DMU_OST_ZFS)rc=EINVAL;
            else if(stats.dds_inconsistent)rc=EBUSY;
            else rc=zfs_get_zplprop(os,zp,&number);
            if(!rc){(void)nvlist_remove_all(props,m->argument);
                dsl_prop_nvlist_add_uint64(props,zp,number);}
        }
        if(!rc&&zp==ZFS_PROP_ORIGIN&&!nvlist_exists(props,m->argument)) {
            /* Origin is computed dataset metadata, not an inheritable DSL
             * property. An independent filesystem legitimately has none. */
            dmu_objset_stats_t stats={0};
            dmu_objset_fast_stat(os,&stats);
            dsl_prop_nvlist_add_string(props,ZFS_PROP_ORIGIN,
                stats.dds_origin[0]?stats.dds_origin:"-");
        }
        dmu_objset_rele(os,FTAG);
    }
    if(rc)goto done;
    rc=nvlist_lookup_nvlist(props,m->argument,&entry);
    if(!rc) {
        if(!nvlist_lookup_uint64(entry,ZPROP_VALUE,&number))numeric=1;
        else if(nvlist_lookup_string(entry,ZPROP_VALUE,&text)){rc=ENOTSUP;goto done;}
        if(pool) {
            if(nvlist_lookup_uint64(entry,ZPROP_SOURCE,&source_number))source="computed";
            else if(source_number==ZPROP_SRC_DEFAULT)source="default";
            else if(source_number==ZPROP_SRC_LOCAL)source="local";
            else source="upstream";
        } else if(nvlist_lookup_string(entry,ZPROP_SOURCE,&source))source="computed";
        else if(!*source)source="default";
    } else if(rc==ENOENT&&pool&&pp!=ZPOOL_PROP_INVAL) {
        /* Match libzfs zpool_get_prop_string/int: unset registered properties
         * have upstream defaults, not ENOENT. Unknown user properties still
         * remain absent. In particular split pools can lack a label comment. */
        source="default";rc=0;
        if(zpool_prop_get_type(pp)==PROP_TYPE_STRING) {
            text=zpool_prop_default_string(pp);if(!text)text="-";
        } else {number=zpool_prop_default_numeric(pp);numeric=1;}
    } else if(rc==ENOENT&&!pool) {
        source_buf[0]=0;
        if(zp==ZPROP_INVAL||zfs_prop_get_type(zp)==PROP_TYPE_STRING) {
            rc=dsl_prop_get(m->target,m->argument,1,sizeof(value_buf),value_buf,source_buf);
            text=value_buf;
        } else {
            rc=dsl_prop_get_integer(m->target,m->argument,&number,source_buf);numeric=1;
        }
        source=*source_buf?source_buf:"default";
    }
    if(rc)goto done;
    if(numeric) {
        int index=pool?(pp!=ZPOOL_PROP_INVAL&&zpool_prop_get_type(pp)==PROP_TYPE_INDEX):
            zp!=ZPROP_INVAL&&zfs_prop_get_type(zp)==PROP_TYPE_INDEX;
        if(index) {
            rc=pool?zpool_prop_index_to_string(pp,number,&text):zfs_prop_index_to_string(zp,number,&text);
            if(rc)goto done;
        } else {snprintf(value_buf,sizeof(value_buf),"%llu",(unsigned long long)number);text=value_buf;}
    }
    struct oz_row *row=&m->rows[0];
    strcpy(row->kind,"property");
    rc=put_text(row->name,sizeof(row->name),m->argument);
    if(!rc)rc=put_text(row->parent,sizeof(row->parent),m->target);
    if(!rc)rc=put_text(row->state,sizeof(row->state),source);
    if(!rc&&strlen(text)>=sizeof(row->value)) {
        /* One exact-property query has the full detail field available.
         * Never return a truncated resume token as a usable property value. */
        rc=put_text(m->detail,sizeof(m->detail),text);
        if(!rc){strcpy(row->kind,"property-long");
            strcpy(row->value,"Full value in response detail");
            row->total=strlen(text);}
    } else if(!rc)rc=put_text(row->value,sizeof(row->value),text);
    if(!rc)m->row_count=1;
done:
    nvlist_free(props);return rc;
}
struct property_all_context {
    struct oz_message *message;
    uint64_t index;
    zfs_type_t type;
    int pool;
    int error;
    int full;
    char failed[64];
};
static int property_all_add(struct property_all_context *context,const char *name)
{
    uint64_t index=context->index++;
    /* The token names a live catalogue candidate. Count before fetching so a
     * later page can skip earlier registered, dynamic and feature candidates
     * without repeating their potentially expensive upstream lookups. */
    if(index<context->message->cursor)return 0;
    struct oz_message query={0};query.operation=context->pool?31:40;
    if(strlcpy(query.target,context->message->target,sizeof(query.target))>=sizeof(query.target)||
       strlcpy(query.argument,name,sizeof(query.argument))>=sizeof(query.argument))return ENAMETOOLONG;
    int error=oz_property_get(&query);if(error)return error;
    if(query.row_count!=1)return EPROTO;
    if(context->message->row_count==OZ_MAX_ROWS) {
        context->message->next_cursor=index;context->full=1;return 0;
    }
    /* Long values keep an explicit unusable placeholder. The exact-property
     * request remains the only transport whose detail field contains bytes. */
    context->message->rows[context->message->row_count++]=query.rows[0];
    return 0;
}
static int property_all_registered(int property,void *opaque)
{
    struct property_all_context *context=opaque;
    if(context->full||context->error)return property;
    if(!context->pool&&!zfs_prop_valid_for_type(property,context->type,B_FALSE))return ZPROP_CONT;
    const char *name=context->pool?zpool_prop_to_name(property):zfs_prop_to_name(property);
    if(!name)context->error=EINVAL;
    else {
        context->error=property_all_add(context,name);
        /* The exact adapter deliberately leaves some upstream properties
         * unavailable. `all` lists the retrievable set instead of making one
         * unavailable registered entry hide every usable property. */
        if(context->error==ENOENT||context->error==ENOTSUP)context->error=0;
        else if(context->error)strlcpy(context->failed,name,sizeof(context->failed));
    }
    return context->full||context->error?property:ZPROP_CONT;
}
int oz_property_get_all(struct oz_message *m)
{
    int pool=m->operation==31,error=0;nvlist_t *props=NULL;spa_t *spa=NULL;objset_t *os=NULL;
    struct property_all_context context={.message=m,.pool=pool,
        .type=pool?ZFS_TYPE_POOL:ZFS_TYPE_DATASET};
    if(pool) {
        error=spa_open(m->target,&spa,FTAG);
        if(!error){props=fnvlist_alloc();error=spa_prop_get(spa,props);}
        if(spa)spa_close(spa,FTAG);
    } else {
        error=dmu_objset_hold(m->target,FTAG,&os);
        if(!error) {
            dmu_objset_type_t type=dmu_objset_type(os);
            if(type==DMU_OST_ZFS)context.type=ZFS_TYPE_FILESYSTEM;
            else if(type==DMU_OST_ZVOL)context.type=ZFS_TYPE_VOLUME;
            else error=EINVAL;
        }
        if(!error)error=dsl_prop_get_all(os,&props);
        if(os)dmu_objset_rele(os,FTAG);
    }
    if(error)goto done;
    (void)zprop_iter_common(property_all_registered,&context,B_FALSE,B_TRUE,
        pool?ZFS_TYPE_POOL:ZFS_TYPE_DATASET);
    error=context.error;
    /* User properties are dynamic and therefore not present in the registered
     * property tables. Append their live nvlist order to the global cursor. */
    for(nvpair_t *pair=context.full||error?NULL:nvlist_next_nvpair(props,NULL);
        pair&&!context.full&&!error;pair=nvlist_next_nvpair(props,pair)) {
        const char *name=nvpair_name(pair);
        if(zfs_prop_user(name)) {
            error=property_all_add(&context,name);
            if(error)strlcpy(context.failed,name,sizeof(context.failed));
        }
    }
    /* spa_prop_get() does not contain feature@ entries. Upstream `zpool get
     * all` expands every supported feature from spa_feature_table instead, so
     * do the same and let the exact adapter derive disabled/enabled/active. */
    for(spa_feature_t feature=0;pool&&!context.full&&!error&&feature<SPA_FEATURES;feature++) {
        char name[ZAP_MAXNAMELEN];
        int length=snprintf(name,sizeof(name),"feature@%s",spa_feature_table[feature].fi_uname);
        if(length<0||(size_t)length>=sizeof(name))error=ENAMETOOLONG;
        else error=property_all_add(&context,name);
        if(error)strlcpy(context.failed,name,sizeof(context.failed));
    }
done:
    /* Preserve cursor==candidate-count as an explicit exhausted page, while
     * rejecting tokens outside the current live catalogue. */
    if(!context.full&&!error&&m->cursor>context.index)error=EINVAL;
    if(error)snprintf(m->detail,sizeof(m->detail),
        "All-property enumeration stopped at %s errno=%d",
        context.failed[0]?context.failed:"setup",error);
    nvlist_free(props);return error;
}
