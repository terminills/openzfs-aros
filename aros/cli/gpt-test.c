/* SPDX-License-Identifier: CDDL-1.0 */
#include "../../lib/libzfs/libzfs_impl.h"
#include "../ui/partition-api.h"
#include <errno.h>
#include <aros/debug.h>
int oz_upstream_gpt_test(void)
{
    libzfs_handle_t *hdl=libzfs_init();
    if(!hdl)return errno?errno:EIO;
    int error=0;struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    nvlist_t *config=NULL,*root=NULL,*leaf=NULL,*ignored=NULL;
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(catalog);
    CHECK(zpool_label_disk(hdl,NULL,"ata.device/00")==-1);
    CHECK(libzfs_errno(hdl)==EZFS_LABELFAILED);
    CHECK(zpool_label_disk(hdl,NULL,"ata.device/0/partition/0")==-1);
    CHECK(!zpool_label_disk(hdl,NULL,"ata.device/0"));
    CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
    struct oz_partition_entry *data=NULL;
    for(uint32_t i=0;i<catalog->count;i++)if(catalog->entries[i].position==0)data=&catalog->entries[i];
    bug("GPT CLI catalog count=%u data-start=%llu\n",catalog->count,
        (unsigned long long)(data?data->first_sector:0));
    CHECK(catalog->count==2&&data&&data->first_sector==2048);
    CHECK(!strncmp(data->name,"zfs-",4));
    CHECK(!zpool_relabel_disk(hdl,"/AROS/ata.device/0","test expand"));
    CHECK(zpool_relabel_disk(hdl,"/AROS/ata.device/0/partition/0","test invalid")==-1);
    CHECK(libzfs_errno(hdl)==EZFS_NOCAP);
    zpool_handle_t pool={0};pool.zpool_start_block=4096;
    CHECK(!zpool_label_disk(hdl,&pool,"/AROS/ata.device/0"));
    CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
    data=NULL;
    for(uint32_t i=0;i<catalog->count;i++)if(catalog->entries[i].position==0)data=&catalog->entries[i];
    CHECK(data&&data->first_sector==4096);
    /* A pool without a cached offset must traverse its real configuration,
     * skipping non-whole-disk children and inspecting native GPT partition 0. */
    config=fnvlist_alloc();root=fnvlist_alloc();leaf=fnvlist_alloc();ignored=fnvlist_alloc();
    fnvlist_add_uint64(leaf,ZPOOL_CONFIG_WHOLE_DISK,1);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,"/AROS/ata.device/0/partition/0");
    fnvlist_add_uint64(ignored,ZPOOL_CONFIG_WHOLE_DISK,0);
    const nvlist_t *children[]={ignored,leaf};
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,children,2);
    fnvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
    pool.zpool_start_block=0;pool.zpool_config=config;
    CHECK(!zpool_label_disk(hdl,&pool,"ata.device/0"));
    CHECK(pool.zpool_start_block==4096);
    CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
    data=NULL;
    for(uint32_t i=0;i<catalog->count;i++)if(catalog->entries[i].position==0)data=&catalog->entries[i];
    CHECK(data&&data->first_sector==4096);
    /* Missing configuration follows upstream's default, not an invented LBA. */
    pool.zpool_start_block=0;pool.zpool_config=NULL;
    CHECK(!zpool_label_disk(hdl,&pool,"ata.device/0"));
    CHECK(pool.zpool_start_block==2048);
    CHECK(!oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",0,catalog));
    data=NULL;
    for(uint32_t i=0;i<catalog->count;i++)if(catalog->entries[i].position==0)data=&catalog->entries[i];
    CHECK(data&&data->first_sector==2048);
done:
    nvlist_free(config);nvlist_free(root);nvlist_free(leaf);nvlist_free(ignored);
    free(catalog);libzfs_fini(hdl);
    bug("RESULT upstream GPT CLI %s short=1 full=1 reject=1 cached-start=1 config-start=1 fallback=1 relabel=1 line=%d\n",error?"FAIL":"PASS",error);
    return error;
#undef CHECK
}
