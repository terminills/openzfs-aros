/* SPDX-License-Identifier: CDDL-1.0 */
#include "partitions.h"
#include <proto/exec.h>
#include <libraries/uuid.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static void note(const char *text){while(*text)RawPutChar(*text++);}
int main(int argc,char **argv)
{
    const char *mode=argc==2?argv[1]:"gpt";
    int gpt=!strcmp(mode,"gpt"),mbr=!strcmp(mode,"mbr");
    int invalid=!strcmp(mode,"overlap")||!strcmp(mode,"out-of-bounds");
    const uuid_t expected_type=MAKE_UUID(0x6a898cc3,0x1dd2,0x11b2,0x99a6,0x080020736631ULL);
    struct oz_partition_catalog *catalog=malloc(sizeof(*catalog));
    struct oz_partition_catalog *saved=malloc(sizeof(*saved));
    int error=0;
#define CHECK(x) do {if(!(x)){char line[128];snprintf(line,sizeof(line),"Partition catalog FAIL line=%d error=%d\n",__LINE__,error);note(line);error=20;goto done;}}while(0)
    CHECK(catalog&&saved);
    CHECK(gpt||mbr||invalid);
    for(unsigned iteration=0;iteration<16;iteration++) {
        memset(catalog,0xa5,sizeof(*catalog));
        *saved=*catalog;
        error=oz_partition_catalog_read("ata.device",0,catalog);
        if(invalid) {
            CHECK(error==EIO);
            CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
            continue;
        }
        CHECK(!error);
        CHECK(catalog->version==1&&catalog->table_type==(gpt?4:2)&&catalog->sector_size==512&&
            catalog->disk_sectors==524288&&catalog->count==2);
        CHECK(catalog->entries[0].position==0&&catalog->entries[1].position==(gpt?1:2));
        CHECK(catalog->entries[0].first_sector==4097&&catalog->entries[0].last_sector==135168);
        CHECK(catalog->entries[1].first_sector==139265&&catalog->entries[1].last_sector==270336);
        if(gpt) {
            CHECK(!strcmp(catalog->entries[0].name,"OpenZFS-A")&&!strcmp(catalog->entries[1].name,"OpenZFS-B"));
            CHECK(catalog->entries[0].type_length==16&&catalog->entries[1].type_length==16);
            CHECK(!memcmp(catalog->entries[0].type,&expected_type,sizeof(expected_type)));
            CHECK(!memcmp(catalog->entries[0].type,catalog->entries[1].type,16));
        } else {
            CHECK(catalog->entries[0].type_length==1&&catalog->entries[1].type_length==1);
            CHECK(catalog->entries[0].type[0]==0xbf&&catalog->entries[1].type[0]==0xbf);
        }
        *saved=*catalog;
        error=oz_partition_catalog_read("ata.device",1,catalog);CHECK(error!=0);
        CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
        error=oz_partition_catalog_read("",0,catalog);CHECK(error==EINVAL);
        CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
        error=oz_partition_catalog_read("ata.device",-1,catalog);CHECK(error==EINVAL);
        CHECK(!memcmp(catalog,saved,sizeof(*catalog)));
    }
    error=0;
    {char line[160];snprintf(line,sizeof(line),
        "RESULT native partition catalog PASS mode=%s cycles=16 unchanged-output=1\n",mode);note(line);}
done:
    free(catalog);free(saved);
    if(error)note("RESULT native partition catalog FAIL\n");
    return error;
}
