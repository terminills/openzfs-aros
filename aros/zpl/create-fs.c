/* SPDX-License-Identifier: CDDL-1.0 */
/*
 * Portions Copyright (c) 2005, 2010, Oracle and/or its affiliates.
 * Portions Copyright (c) 2012, 2018 by Delphix.
 *
 * Native adaptation of zfs_create_fs in module/os/linux/zfs/zfs_znode_os.c.
 * Keep upstream master-node/SA/unlinked/root initialization and use the
 * ported zfs_acl_ids_create/zfs_mknode, replacing only Linux inode/cache
 * scaffolding with a temporary native znode. No fixture files or public ACL.
 */
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/zfs_znode.h>
#include <sys/zfs_dir.h>
#include <zfs_comutil.h>
#include <sys/zfs_acl.h>
#include <sys/zfs_sa.h>
#include <sys/zap.h>
#include <zfs_namecheck.h>
#include <sys/u8_textprep.h>
#include <libzutil.h>
#ifdef OZ_NATIVE_DISPATCHER
#include <sys/zfs_ioctl_impl.h>
#include "../engine/ioctl-copy.h"
#endif

void zfs_create_fs(objset_t *os,cred_t *cr,nvlist_t *zplprops,dmu_tx_t *tx)
{
    uint64_t version=zfs_zpl_version_map(spa_version(dmu_objset_spa(os)));
    uint64_t norm=0,sense=ZFS_CASE_SENSITIVE,utf8=0;
    VERIFY0(zap_create_claim(os,MASTER_NODE_OBJ,DMU_OT_MASTER_NODE,DMU_OT_NONE,0,tx));
    /* The upstream ioctl validates these creation-only properties before
     * its sync callback. Preserve them, including the requested older ZPL
     * version, instead of silently creating a default-only filesystem. */
    nvpair_t *elem=NULL;
    while(zplprops && (elem=nvlist_next_nvpair(zplprops,elem))) {
        uint64_t value;const char *name=nvpair_name(elem);
        VERIFY3U(nvpair_type(elem),==,DATA_TYPE_UINT64);
        VERIFY0(nvpair_value_uint64(elem,&value));
        if(!strcmp(name,zfs_prop_to_name(ZFS_PROP_VERSION)))version=MIN(version,value);
        else VERIFY0(zap_update(os,MASTER_NODE_OBJ,name,8,1,&value,tx));
        if(!strcmp(name,zfs_prop_to_name(ZFS_PROP_NORMALIZE)))norm=value;
        else if(!strcmp(name,zfs_prop_to_name(ZFS_PROP_CASE)))sense=value;
        else if(!strcmp(name,zfs_prop_to_name(ZFS_PROP_UTF8ONLY)))utf8=value;
    }
    VERIFY3U(version,!=,0);
    VERIFY0(zap_update(os,MASTER_NODE_OBJ,ZPL_VERSION_STR,8,1,&version,tx));
    uint64_t sa=0;
    if(version>=ZPL_VERSION_SA) {
        sa=zap_create(os,DMU_OT_SA_MASTER_NODE,DMU_OT_NONE,0,tx);
        VERIFY0(zap_add(os,MASTER_NODE_OBJ,ZFS_SA_ATTRS,8,1,&sa,tx));
    }
    uint64_t unlinked=zap_create(os,DMU_OT_UNLINKED_SET,DMU_OT_NONE,0,tx);
    VERIFY0(zap_add(os,MASTER_NODE_OBJ,ZFS_UNLINKED_SET,8,1,&unlinked,tx));
    zfsvfs_t *fs=kmem_zalloc(sizeof(*fs),KM_SLEEP);
    znode_t *root=kmem_zalloc(sizeof(*root),KM_SLEEP),*created=NULL;
    fs->z_os=os;fs->z_parent=fs;fs->z_version=version;
    fs->z_use_fuids=USE_FUIDS(version,os);fs->z_use_sa=USE_SA(version,os);
    fs->z_case=sense;fs->z_norm=norm;fs->z_utf8=utf8;
    if(sense==ZFS_CASE_INSENSITIVE || sense==ZFS_CASE_MIXED)
        fs->z_norm|=U8_TEXTPREP_TOUPPER;
    rw_init(&fs->z_fuid_lock,NULL,RW_DEFAULT,NULL);
    root->z_zfsvfs=fs;root->z_is_sa=fs->z_use_sa;
    VERIFY0(sa_setup(os,sa,zfs_attr_table,ZPL_END,&fs->z_attr_table));
    vattr_t va={.va_mask=ATTR_MODE|ATTR_UID|ATTR_GID,.va_mode=S_IFDIR|0755,
        .va_uid=crgetuid(cr),.va_gid=crgetgid(cr)};
    zfs_acl_ids_t ids;
    VERIFY0(zfs_acl_ids_create(root,IS_ROOT_NODE,&va,cr,NULL,&ids,NULL));
    zfs_mknode(root,&va,tx,cr,IS_ROOT_NODE,&created,&ids);
    VERIFY3P(created,==,root);
    VERIFY0(zap_add(os,MASTER_NODE_OBJ,ZFS_ROOT_OBJ,8,1,&root->z_id,tx));
    zfs_acl_ids_free(&ids);
    sa_handle_destroy(root->z_sa_hdl);
    rw_destroy(&fs->z_fuid_lock);
    kmem_free(root,sizeof(*root));kmem_free(fs,sizeof(*fs));
}

static void create_fs(objset_t *os,void *arg,cred_t *cr,dmu_tx_t *tx)
{
    VERIFY3P(cr,==,arg);
    zfs_create_fs(os,cr,NULL,tx);
}

int oz_zpl_dataset_create(const char *name,cred_t *cr)
{
    if(!cr)return EACCES;
    if(!name||dataset_namecheck(name,NULL,NULL)||!strchr(name,'/')||
        strchr(name,'@')||strchr(name,'#'))return EINVAL;
    char parent[ZFS_MAX_DATASET_NAME_LEN];
    if(strlcpy(parent,name,sizeof(parent))>=sizeof(parent))return ENAMETOOLONG;
    *strrchr(parent,'/')=0;
    int rc=oz_dataset_create_perms(parent,cr);
    if(rc)return rc;
    spa_t *spa=NULL;rc=spa_open(name,&spa,FTAG);
    if(rc)return rc;
    if(!spa_writeable(spa))rc=EROFS;
    spa_close(spa,FTAG);
    if(rc)return rc;
    struct oz_cred_scope *scope=NULL;
    rc=oz_cred_push(cr,&scope);
    if(rc)return rc;
    rc=dmu_objset_create(name,DMU_OST_ZFS,0,NULL,create_fs,cr);
    int pop_error=oz_cred_pop(scope);
    VERIFY0(pop_error);
    return rc;
}

/* Typed management adapter: parse spelling only. Upstream CREATE owns
 * permission checks, property combinations, inheritance and transactions. */
int oz_zpl_dataset_create_prepared(const char *name,const char *options,cred_t *cr,
    int (*prepare)(nvlist_t *,nvlist_t *,void *),void *context)
{
    if(!name||!options||!cr)return EINVAL;
#ifndef OZ_NATIVE_DISPATCHER
    return (*options||prepare)?ENOTSUP:oz_zpl_dataset_create(name,cr);
#else
    if(strlen(options)>=512)return E2BIG;
    char text[512];strcpy(text,options);
    nvlist_t *in=fnvlist_alloc(),*props=fnvlist_alloc();
    int type=DMU_OST_ZFS,type_seen=0;
    char *rest=text,*token;int error=0;
    while(*options&&(token=strsep(&rest,","))!=NULL) {
        char *value=strchr(token,'=');
        if(!value||value==token||!value[1]){error=EINVAL;break;}
        *value++=0;
        if(!strcmp(token,"type")) {
            if(type_seen++){error=EINVAL;break;}
            if(!strcmp(value,"volume"))type=DMU_OST_ZVOL;
            else if(strcmp(value,"filesystem")){error=EINVAL;break;}
            continue;
        }
        if(nvlist_exists(props,token)){error=EINVAL;break;}
        uint64_t number=0;
        if(!strcmp(token,"volsize")||!strcmp(token,"volblocksize")||
            !strcmp(token,"refreservation")||!strcmp(token,"reservation")||
            !strcmp(token,"quota")||!strcmp(token,"refquota")||
            !strcmp(token,"recordsize")||!strcmp(token,"pbkdf2iters")) {
            if(!strcmp(token,"pbkdf2iters")&&!prepare){error=ENOTSUP;break;}
            int unlimited=!strcmp(value,"none")&&(!strcmp(token,"quota")||
                !strcmp(token,"refquota")||!strcmp(token,"reservation")||!strcmp(token,"refreservation"));
            zfs_num_error_t parsed=unlimited?ZFS_NUM_OK:zfs_nicestrtonum_parse(value,&number,NULL);
            if(parsed!=ZFS_NUM_OK){error=EINVAL;break;}
            /* libzfs_valid_proplist checks this before the CREATE ioctl.
             * Our typed caller bypasses libzfs, so retain the same structural
             * size check; upstream still enforces feature/tunable limits. */
            if((!strcmp(token,"recordsize")||!strcmp(token,"volblocksize"))&&
                (number<SPA_MINBLOCKSIZE||number>SPA_MAXBLOCKSIZE||!ISP2(number))) {
                error=EINVAL;break;
            }
        } else if(!strcmp(token,"encryption")||!strcmp(token,"keyformat")) {
            if(!prepare){error=ENOTSUP;break;}
            zfs_prop_t prop=zfs_name_to_prop(token);
            if(zfs_prop_string_to_index(prop,value,&number)){error=EINVAL;break;}
        } else if(!strcmp(token,"normalization")) {
            if(!strcmp(value,"none"))number=0;
            else if(!strcmp(value,"formC"))number=U8_TEXTPREP_NFC;
            else if(!strcmp(value,"formD"))number=U8_TEXTPREP_NFD;
            else if(!strcmp(value,"formKC"))number=U8_TEXTPREP_NFKC;
            else if(!strcmp(value,"formKD"))number=U8_TEXTPREP_NFKD;
            else {error=EINVAL;break;}
        } else if(!strcmp(token,"utf8only")) {
            if(!strcmp(value,"on"))number=1;
            else if(strcmp(value,"off")){error=EINVAL;break;}
        } else if(!strcmp(token,"casesensitivity")) {
            if(!strcmp(value,"sensitive"))number=ZFS_CASE_SENSITIVE;
            else if(!strcmp(value,"insensitive"))number=ZFS_CASE_INSENSITIVE;
            else if(!strcmp(value,"mixed"))number=ZFS_CASE_MIXED;
            else {error=EINVAL;break;}
        } else {error=ENOTSUP;break;}
        fnvlist_add_uint64(props,token,number);
    }
    if(!error) {
        /* Refuse mixed filesystem/volume options before CREATE can mutate
         * anything. Upstream validates sizes, alignment and reservations. */
        if(type==DMU_OST_ZVOL) {
            if(!nvlist_exists(props,"volsize")||nvlist_exists(props,"normalization")||
                nvlist_exists(props,"utf8only")||nvlist_exists(props,"casesensitivity")||
                nvlist_exists(props,"quota")||nvlist_exists(props,"refquota")||nvlist_exists(props,"recordsize"))error=EINVAL;
        } else if(nvlist_exists(props,"volsize")||nvlist_exists(props,"volblocksize"))error=EINVAL;
    }
    if(!error&&prepare)error=prepare(props,in,context);
    if(!error){fnvlist_add_int32(in,"type",type);fnvlist_add_nvlist(in,"props",props);}
    char *packed=NULL,*output=NULL;size_t size=0;zfs_cmd_t *zc=NULL;
    if(!error)error=nvlist_pack(in,&packed,&size,NV_ENCODE_NATIVE,0);
    if(!error) {
        zc=calloc(1,sizeof(*zc));output=malloc(65536);
        if(!zc||!output)error=ENOMEM;
    }
    if(!error&&strlcpy(zc->zc_name,name,sizeof(zc->zc_name))>=sizeof(zc->zc_name))error=ENAMETOOLONG;
    struct oz_cred_scope *scope=NULL;struct oz_ioctl_copy_scope *copy=NULL;
    if(!error)error=oz_cred_push(cr,&scope);
    if(!error) {
        zc->zc_nvlist_src=(uintptr_t)packed;zc->zc_nvlist_src_size=size;
        zc->zc_nvlist_dst=(uintptr_t)output;zc->zc_nvlist_dst_size=65536;
        struct oz_ioctl_range ranges[]={{packed,size,OZ_IOCTL_INPUT},{output,65536,OZ_IOCTL_OUTPUT}};
        error=oz_ioctl_copy_enter(ranges,2,&copy);
        if(!error)error=zfsdev_ioctl_common(ZFS_IOC_CREATE-ZFS_IOC_FIRST,zc,FKIOCTL);
    }
    if(copy)VERIFY0(oz_ioctl_copy_leave(copy));
    if(scope)VERIFY0(oz_cred_pop(scope));
    /* The prepared input may contain hidden wrapping-key bytes. */
    if(packed){volatile unsigned char *p=(void *)packed;for(size_t i=0;i<size;i++)p[i]=0;}
    nvlist_t *hidden=NULL;uint8_t *key=NULL;uint_t keylen=0;
    if(!nvlist_lookup_nvlist(in,"hidden_args",&hidden)&&
        !nvlist_lookup_uint8_array(hidden,"wkeydata",&key,&keylen)){
        volatile uint8_t *p=key;while(keylen--)*p++=0;
    }
    free(zc);free(output);free(packed);fnvlist_free(in);fnvlist_free(props);
    return error;
#endif
}
int oz_zpl_dataset_create_options(const char *name,const char *options,cred_t *cr)
{return oz_zpl_dataset_create_prepared(name,options,cr,NULL,NULL);}
