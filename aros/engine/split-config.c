/* SPDX-License-Identifier: CDDL-1.0 */
#include "split-config.h"
#include <sys/fs/zfs.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>

int oz_split_guids(const char *text,uint64_t *guids,unsigned *count)
{
    if(!text||!guids||!count)return EINVAL;
    *count=0;
    if(strncmp(text,"guids=",6))return EINVAL;
    const char *p=text+6;
    unsigned n=0;
    do {
        if(n==32||*p<'0'||*p>'9')return EINVAL;
        uint64_t value=0;
        while(*p>='0'&&*p<='9') {
            unsigned digit=*p++-'0';
            if(value>(UINT64_MAX-digit)/10)return ERANGE;
            value=value*10+digit;
        }
        if(!value)return EINVAL;
        for(unsigned i=0;i<n;i++)if(guids[i]==value)return EINVAL;
        guids[n++]=value;
        if(!*p)break;
        if(*p++!=',')return EINVAL;
    } while(1);
    *count=n;return 0;
}

int oz_split_config(nvlist_t *tree,const uint64_t *guids,unsigned count,
    nvlist_t **out)
{
    nvlist_t **children,**members,*root=NULL,*config=NULL;
    nvlist_t *selected[32]={0};
    unsigned children_count,member_count,used=0,keep=0;
    const char *type;
    int error=EINVAL;
    if(!out)return EINVAL;
    *out=NULL;
    if(!tree||!guids||!count||count>32)return EINVAL;
    for(unsigned i=0;i<count;i++) {
        if(!guids[i])return EINVAL;
        for(unsigned j=0;j<i;j++)if(guids[i]==guids[j])return EINVAL;
    }
    if(nvlist_lookup_string(tree,ZPOOL_CONFIG_TYPE,&type)||
        strcmp(type,VDEV_TYPE_ROOT)||
        nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&children_count)||
        !children_count||children_count>32)return EINVAL;
    for(unsigned i=0;i<children_count;i++) {
        uint64_t log=0,hole=0;
        (void)nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_IS_LOG,&log);
        (void)nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_IS_HOLE,&hole);
        if(log||hole) {
            selected[i]=fnvlist_alloc();
            fnvlist_add_string(selected[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_HOLE);
            fnvlist_add_uint64(selected[i],ZPOOL_CONFIG_IS_HOLE,1);
            continue;
        }
        if(nvlist_lookup_string(children[i],ZPOOL_CONFIG_TYPE,&type))goto done;
        keep=i+1;
        if(!strcmp(type,VDEV_TYPE_INDIRECT)) {
            error=nvlist_dup(children[i],&selected[i],0);
            if(error)goto done;
            error=EINVAL;continue;
        }
        if(strcmp(type,VDEV_TYPE_MIRROR)) {error=ENOTSUP;goto done;}
        if(nvlist_lookup_nvlist_array(children[i],ZPOOL_CONFIG_CHILDREN,&members,&member_count)||
            member_count<2)goto done;
        for(unsigned j=0;j<member_count;j++) {
            uint64_t guid;
            if(nvlist_lookup_uint64(members[j],ZPOOL_CONFIG_GUID,&guid))goto done;
            for(unsigned k=0;k<count;k++)if(guids[k]==guid) {
                if(selected[i]||(used&(1U<<k)))goto done;
                /* Admission checks the selected live leaf's path separately;
                 * native disk leaves need the same topology selection. */
                if(nvlist_lookup_string(members[j],ZPOOL_CONFIG_TYPE,&type)||
                    (strcmp(type,VDEV_TYPE_FILE)&&strcmp(type,VDEV_TYPE_DISK))) {error=ENOTSUP;goto done;}
                error=nvlist_dup(members[j],&selected[i],0);
                if(error)goto done;
                error=EINVAL;used|=1U<<k;
            }
        }
        if(!selected[i])goto done;
    }
    if(used!=(count==32?UINT32_MAX:((1U<<count)-1))||!keep)goto done;
    root=fnvlist_alloc();config=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)selected,keep);
    fnvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
    *out=config;config=NULL;error=0;
done:
    nvlist_free(config);nvlist_free(root);
    for(unsigned i=0;i<32;i++)nvlist_free(selected[i]);
    return error;
}

int oz_split_config_admit(nvlist_t *source,nvlist_t *request,nvlist_t **out)
{
    nvlist_t *tree=NULL,**children=NULL,*clean=NULL,**expected=NULL;
    unsigned count=0,n=0,selected=0;uint64_t guids[32];const char *type;
    if(!out)return EINVAL;
    *out=NULL;
    if(!source||!request||nvlist_lookup_nvlist(request,ZPOOL_CONFIG_VDEV_TREE,&tree)||
        nvlist_lookup_string(tree,ZPOOL_CONFIG_TYPE,&type)||strcmp(type,VDEV_TYPE_ROOT)||
        nvlist_exists(tree,ZPOOL_CONFIG_SPARES)||nvlist_exists(tree,ZPOOL_CONFIG_L2CACHE)||
        nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&children,&count)||!count||count>32)
        return EINVAL;
    for(unsigned i=0;i<count;i++){
        if(nvlist_lookup_string(children[i],ZPOOL_CONFIG_TYPE,&type))return EINVAL;
        if(!strcmp(type,VDEV_TYPE_HOLE)||!strcmp(type,VDEV_TYPE_INDIRECT)){
            if(nvlist_exists(children[i],ZPOOL_CONFIG_PATH))return EINVAL;
        }else if(!strcmp(type,VDEV_TYPE_FILE)||!strcmp(type,VDEV_TYPE_DISK)){
            if(nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_GUID,&guids[selected++]))return EINVAL;
        }else return EINVAL;
        const char *arrays[]={ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
        for(unsigned a=0;a<3;a++)if(nvlist_exists(children[i],arrays[a])){
            nvlist_t **nested;unsigned size;
            if(nvlist_lookup_nvlist_array(children[i],arrays[a],&nested,&size)||size)return EINVAL;
        }
    }
    int error=oz_split_config(source,guids,selected,&clean);
    if(error)return error;
    tree=fnvlist_lookup_nvlist(clean,ZPOOL_CONFIG_VDEV_TREE);
    error=nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&expected,&n);
    if(error)goto done;
    if(n!=count){error=EINVAL;goto done;}
    for(unsigned i=0;i<n;i++){
        const char *want=fnvlist_lookup_string(expected[i],ZPOOL_CONFIG_TYPE);
        type=fnvlist_lookup_string(children[i],ZPOOL_CONFIG_TYPE);
        if(strcmp(type,want)){error=EINVAL;goto done;}
        if(!strcmp(type,VDEV_TYPE_HOLE)){
            uint64_t hole;
            if(nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_IS_HOLE,&hole)||hole!=1){error=EINVAL;goto done;}
        }else{
            uint64_t guid,expected_guid;
            if(nvlist_lookup_uint64(children[i],ZPOOL_CONFIG_GUID,&guid)||
                nvlist_lookup_uint64(expected[i],ZPOOL_CONFIG_GUID,&expected_guid)||guid!=expected_guid){error=EINVAL;goto done;}
            if(strcmp(type,VDEV_TYPE_INDIRECT)){
                const char *path,*expected_path;
                if(nvlist_lookup_string(children[i],ZPOOL_CONFIG_PATH,&path)||
                    nvlist_lookup_string(expected[i],ZPOOL_CONFIG_PATH,&expected_path)||strcmp(path,expected_path)){
                    error=EINVAL;goto done;
                }
            }
        }
        /* Volatile statistics are not persisted and need not consume the
         * caller's bounded config buffer. Keep trusted indirect object IDs. */
        (void)nvlist_remove_all(expected[i],ZPOOL_CONFIG_VDEV_STATS);
        (void)nvlist_remove_all(expected[i],ZPOOL_CONFIG_VDEV_STATS_EX);
    }
    *out=clean;clean=NULL;
done:
    nvlist_free(clean);return error;
}
