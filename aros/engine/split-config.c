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
                /* The current adapter accepts file-backed leaves only. */
                if(nvlist_lookup_string(members[j],ZPOOL_CONFIG_TYPE,&type)||
                    strcmp(type,VDEV_TYPE_FILE)) {error=ENOTSUP;goto done;}
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
