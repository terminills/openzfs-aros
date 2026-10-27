/* SPDX-License-Identifier: CDDL-1.0 */
#include "split-config.h"
#include <sys/fs/zfs.h>
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>

static nvlist_t *node(const char *type,uint64_t guid)
{
    nvlist_t *v=fnvlist_alloc();
    fnvlist_add_string(v,ZPOOL_CONFIG_TYPE,type);
    fnvlist_add_uint64(v,ZPOOL_CONFIG_GUID,guid);
    return v;
}
int main(void)
{
    uint64_t ids[32];unsigned n;
    const char *bad[]={"","guids=","guids=0","guids=-1","guids=+1",
        "guids=1,","guids=1,1","guids=1;2","guids=1, 2","guids=1x"};
    for(unsigned i=0;i<sizeof(bad)/sizeof(*bad);i++) {
        n=99;assert(oz_split_guids(bad[i],ids,&n)==EINVAL);assert(n==0);
    }
    assert(oz_split_guids("guids=18446744073709551616",ids,&n)==ERANGE);
    assert(!oz_split_guids("guids=18446744073709551615,2",ids,&n));
    assert(n==2&&ids[0]==UINT64_MAX&&ids[1]==2);
    char maximum[256]="guids=";
    for(unsigned i=1;i<=32;i++) {
        char part[12];snprintf(part,sizeof(part),"%s%u",i==1?"":",",i);
        strcat(maximum,part);
    }
    assert(!oz_split_guids(maximum,ids,&n)&&n==32&&ids[31]==32);
    strcat(maximum,",33");
    assert(oz_split_guids(maximum,ids,&n)==EINVAL&&n==0);
    nvlist_t *leaf[4],*mirror[2],*tops[4],*tree=node(VDEV_TYPE_ROOT,1),*out=NULL;
    for(unsigned i=0;i<4;i++){
        leaf[i]=node(VDEV_TYPE_FILE,101+i);
        char path[32];snprintf(path,sizeof(path),"/RAM/disk%u",i);
        fnvlist_add_string(leaf[i],ZPOOL_CONFIG_PATH,path);
    }
    for(unsigned i=0;i<2;i++) {
        mirror[i]=node(VDEV_TYPE_MIRROR,201+i);
        fnvlist_add_nvlist_array(mirror[i],ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf[i*2],2);
    }
    tops[0]=mirror[0];tops[1]=node(VDEV_TYPE_FILE,301);
    fnvlist_add_uint64(tops[1],ZPOOL_CONFIG_IS_LOG,1);
    tops[2]=mirror[1];tops[3]=node(VDEV_TYPE_HOLE,302);
    fnvlist_add_uint64(tops[3],ZPOOL_CONFIG_IS_HOLE,1);
    nvlist_t *log=tops[1];
    fnvlist_add_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,4);
    assert(!oz_split_guids("guids=104,102",ids,&n));
    assert(!oz_split_config(tree,ids,n,&out));
    /* The source remains a mirror, not replaced by our selected leaf. */
    nvlist_t **source_children;unsigned source_count;
    assert(!nvlist_lookup_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,&source_children,&source_count));
    assert(source_count==4&&!strcmp(fnvlist_lookup_string(source_children[0],ZPOOL_CONFIG_TYPE),VDEV_TYPE_MIRROR));
    nvlist_t *root=fnvlist_lookup_nvlist(out,ZPOOL_CONFIG_VDEV_TREE),**children;
    unsigned count;
    assert(!nvlist_lookup_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,&children,&count));
    assert(count==3); /* Interior log becomes hole; trailing hole omitted. */
    assert(fnvlist_lookup_uint64(children[0],ZPOOL_CONFIG_GUID)==102);
    assert(fnvlist_lookup_uint64(children[1],ZPOOL_CONFIG_IS_HOLE)==1);
    assert(fnvlist_lookup_uint64(children[2],ZPOOL_CONFIG_GUID)==104);
    nvlist_t *admitted=NULL;
    assert(!oz_split_config_admit(tree,out,&admitted));nvlist_free(admitted);admitted=NULL;
    fnvlist_add_string(children[1],ZPOOL_CONFIG_PATH,"/unadmitted");
    assert(oz_split_config_admit(tree,out,&admitted)==EINVAL&&!admitted);
    fnvlist_remove(children[1],ZPOOL_CONFIG_PATH);
    fnvlist_add_uint64(children[1],ZPOOL_CONFIG_IS_HOLE,0);
    assert(oz_split_config_admit(tree,out,&admitted)==EINVAL&&!admitted);
    fnvlist_add_uint64(children[1],ZPOOL_CONFIG_IS_HOLE,1);
    fnvlist_add_string(children[0],ZPOOL_CONFIG_PATH,"/wrong-member");
    assert(oz_split_config_admit(tree,out,&admitted)==EINVAL&&!admitted);
    fnvlist_add_string(children[0],ZPOOL_CONFIG_PATH,"/RAM/disk1");
    fnvlist_add_uint64(children[0],ZPOOL_CONFIG_GUID,104);
    assert(oz_split_config_admit(tree,out,&admitted)==EINVAL&&!admitted);
    fnvlist_add_uint64(children[0],ZPOOL_CONFIG_GUID,102);
    assert(!oz_split_config_admit(tree,out,&admitted));nvlist_free(admitted);admitted=NULL;
    nvlist_free(out);out=NULL;
    assert(oz_split_config(tree,ids,1,&out)==EINVAL&&!out); /* omitted mirror */
    ids[0]=101;ids[1]=102;
    assert(oz_split_config(tree,ids,2,&out)==EINVAL&&!out); /* same mirror */
    ids[0]=999;ids[1]=104;
    assert(oz_split_config(tree,ids,2,&out)==EINVAL&&!out);
    ids[0]=104;
    assert(oz_split_config(tree,ids,2,&out)==EINVAL&&!out);
    ids[0]=102;ids[1]=104;
    fnvlist_add_string(mirror[0],ZPOOL_CONFIG_TYPE,VDEV_TYPE_RAIDZ);
    fnvlist_add_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,4);
    assert(oz_split_config(tree,ids,2,&out)==ENOTSUP&&!out);
    fnvlist_add_string(mirror[0],ZPOOL_CONFIG_TYPE,VDEV_TYPE_MIRROR);
    nvlist_t *indirect=node(VDEV_TYPE_INDIRECT,401);
    fnvlist_add_uint64(indirect,ZPOOL_CONFIG_INDIRECT_OBJECT,42);
    fnvlist_add_uint64(indirect,ZPOOL_CONFIG_INDIRECT_BIRTHS,43);
    tops[1]=indirect;
    fnvlist_add_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,4);
    assert(!oz_split_config(tree,ids,2,&out));
    root=fnvlist_lookup_nvlist(out,ZPOOL_CONFIG_VDEV_TREE);
    assert(!nvlist_lookup_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,&children,&count));
    assert(count==3&&!strcmp(fnvlist_lookup_string(children[1],ZPOOL_CONFIG_TYPE),VDEV_TYPE_INDIRECT));
    /* Never trust caller-provided MOS object IDs, even with a correct GUID. */
    fnvlist_add_uint64(children[1],ZPOOL_CONFIG_INDIRECT_OBJECT,999);
    assert(!oz_split_config_admit(tree,out,&admitted));
    nvlist_t *admitted_root=fnvlist_lookup_nvlist(admitted,ZPOOL_CONFIG_VDEV_TREE),**admitted_children;
    unsigned admitted_count;
    assert(!nvlist_lookup_nvlist_array(admitted_root,ZPOOL_CONFIG_CHILDREN,&admitted_children,&admitted_count));
    assert(fnvlist_lookup_uint64(admitted_children[1],ZPOOL_CONFIG_INDIRECT_OBJECT)==42);
    assert(fnvlist_lookup_uint64(admitted_children[1],ZPOOL_CONFIG_INDIRECT_BIRTHS)==43);
    nvlist_free(admitted);admitted=NULL;
    fnvlist_add_uint64(children[1],ZPOOL_CONFIG_GUID,402);
    assert(oz_split_config_admit(tree,out,&admitted)==EINVAL&&!admitted);
    nvlist_free(out);out=NULL;
    /* Native disk selection must preserve the exact admitted path and GUID. */
    fnvlist_add_string(leaf[1],ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
    fnvlist_add_string(leaf[1],ZPOOL_CONFIG_PATH,"/AROS/ata.device/1/partition/0");
    fnvlist_add_nvlist_array(mirror[0],ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)leaf,2);
    fnvlist_add_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,4);
    assert(!oz_split_config(tree,ids,2,&out));
    root=fnvlist_lookup_nvlist(out,ZPOOL_CONFIG_VDEV_TREE);
    assert(!nvlist_lookup_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,&children,&count));
    assert(!strcmp(fnvlist_lookup_string(children[0],ZPOOL_CONFIG_TYPE),VDEV_TYPE_DISK));
    assert(!strcmp(fnvlist_lookup_string(children[0],ZPOOL_CONFIG_PATH),"/AROS/ata.device/1/partition/0"));
    assert(fnvlist_lookup_uint64(children[0],ZPOOL_CONFIG_GUID)==102);
    nvlist_free(out);out=NULL;
    fnvlist_add_string(leaf[1],ZPOOL_CONFIG_TYPE,VDEV_TYPE_RAIDZ);
    fnvlist_add_nvlist_array(mirror[0],ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)leaf,2);
    fnvlist_add_nvlist_array(tree,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,4);
    assert(oz_split_config(tree,ids,2,&out)==ENOTSUP&&!out);
    nvlist_free(tree);nvlist_free(indirect);nvlist_free(tops[3]);nvlist_free(log);
    for(unsigned i=0;i<4;i++)nvlist_free(leaf[i]);
    for(unsigned i=0;i<2;i++)nvlist_free(mirror[i]);
    puts("PASS explicit mirror split configuration and GUID validation");
    return 0;
}
