/* Independent fresh-engine read of the snapshot's actual DMU object bytes. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/dmu.h>
#include <stdio.h>
int main(void)
{
    char line[1024],pool[160]={0},path[512]={0},snapshot[256];
    unsigned long long object=0;int passed=0,rc=0;
    FILE *f=fopen("/SYS/import-fixture.log","r");if(!f)return 20;
    while(fgets(line,sizeof(line),f)) {
        sscanf(line,"Creating upstream mirror %159s",pool);
        sscanf(line,"Exported upstream XDR configuration: %511s",path);
        sscanf(line,"dataset payload write=0 object=%llu bytes=16384",&object);
        if(strstr(line,"RESULT native mirror data roundtrip PASS"))passed=1;
    }
    fclose(f);
    if(!passed||!pool[0]||!path[0]||!object)return 20;
    f=fopen(path,"rb");if(!f)return 20;
    char packed[65536];size_t length=fread(packed,1,sizeof(packed),f);
    int valid=feof(f)&&!ferror(f);fclose(f);if(!valid)return 20;
    nvlist_t *config=NULL,*props=NULL;
    rc=nvlist_unpack(packed,length,&config,0);if(rc)return 20;
    props=fnvlist_alloc();fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"readonly",1);
    spa_config_path="";kernel_init(SPA_MODE_READ);
    rc=spa_import(pool,config,props,0);
    printf("fresh-engine import=%d\n",rc);
    snprintf(snapshot,sizeof(snapshot),"%s/payload@api_snapshot",pool);
    if(!rc) {
        objset_t *os=NULL;
        rc=dmu_objset_own(snapshot,DMU_OST_OTHER,B_TRUE,B_FALSE,FTAG,&os);
        if(!rc) {
            unsigned char bytes[16384];
            rc=dmu_read(os,object,0,sizeof(bytes),bytes,0);
            if(!rc)for(unsigned i=0;i<sizeof(bytes);i++) {
                if(bytes[i]!=(unsigned char)((i*37u)^(i>>5)^0xa7)){rc=EIO;break;}
            }
            dmu_objset_disown(os,B_FALSE,FTAG);
        }
    }
    kernel_fini();nvlist_free(config);nvlist_free(props);
    printf("RESULT cold snapshot object=%llu bytes=16384 compare=%d %s\n",object,rc,rc?"FAIL":"PASS");
    return rc?20:0;
}
