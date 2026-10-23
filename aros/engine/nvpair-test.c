/* Native AROS execution of upstream nvpair allocation, nesting and XDR. */
#include <sys/nvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
static int failures;
static void serial(const char *text) { for(;*text;text++)RawPutChar(*text); }
#define CHECK(x) do { int ok=!!(x); printf("%s %s\n",ok?"PASS":"FAIL",#x); serial(ok?"nvpair PASS " #x "\n":"nvpair FAIL " #x "\n"); if(!ok) failures++; } while(0)
int main(int argc,char **argv) {
    nvlist_t *pool=NULL,*leaf=NULL,*decoded=NULL,*got=NULL;
    char *packed=NULL;const char *path=NULL;size_t bytes=0;uint64_t number=0;
    CHECK(nvlist_alloc(&pool,NV_UNIQUE_NAME,0)==0);
    CHECK(nvlist_alloc(&leaf,NV_UNIQUE_NAME,0)==0);
    if(!pool || !leaf){nvlist_free(pool);nvlist_free(leaf);serial("RESULT native nvpair XDR FAIL\n");return 20;}
    CHECK(nvlist_add_string(leaf,"path","Work:disk files/vdev0")==0);
    CHECK(nvlist_add_uint64(leaf,"guid",UINT64_C(0xfedcba9876543210))==0);
    CHECK(nvlist_add_nvlist(pool,"vdev",leaf)==0);
    uint64_t stats[]={0,UINT64_MAX,UINT64_C(0x100000000)};
    const char *names[]={"mirror","Work:disk files/vdev0",""};
    const nvlist_t *children[]={leaf,leaf};
    CHECK(nvlist_add_uint64_array(pool,"stats",stats,3)==0);
    CHECK(nvlist_add_string_array(pool,"names",names,3)==0);
    CHECK(nvlist_add_nvlist_array(pool,"children",children,2)==0);
    CHECK(nvlist_add_boolean_value(pool,"readonly",B_TRUE)==0);
    CHECK(nvlist_pack(pool,&packed,&bytes,NV_ENCODE_XDR,0)==0);
    CHECK(bytes>0);
    if(packed){
        CHECK(nvlist_unpack(packed,bytes,&decoded,0)==0);
        if(decoded){
            uint_t count=0;uint64_t *values=NULL;char **strings=NULL;
            nvlist_t **nodes=NULL;boolean_t readonly=B_FALSE;
            CHECK(nvlist_lookup_uint64_array(decoded,"stats",&values,&count)==0);
            CHECK(values&&count==3&&!memcmp(values,stats,sizeof(stats)));
            count=0;
            CHECK(nvlist_lookup_string_array(decoded,"names",&strings,&count)==0);
            CHECK(strings&&count==3&&!strcmp(strings[0],names[0])&&
                !strcmp(strings[1],names[1])&&!strcmp(strings[2],names[2]));
            count=0;
            CHECK(nvlist_lookup_nvlist_array(decoded,"children",&nodes,&count)==0);
            CHECK(nodes&&count==2);
            if(nodes&&count==2)for(unsigned i=0;i<2;i++){
                number=0;
                CHECK(nvlist_lookup_uint64(nodes[i],"guid",&number)==0);
                CHECK(number==UINT64_C(0xfedcba9876543210));
            }
            CHECK(nvlist_lookup_boolean_value(decoded,"readonly",&readonly)==0);
            CHECK(readonly==B_TRUE);
            CHECK(nvlist_lookup_nvlist(decoded,"vdev",&got)==0);
            if(got){
                CHECK(nvlist_lookup_string(got,"path",&path)==0);
                CHECK(path && !strcmp(path,"Work:disk files/vdev0"));
                CHECK(nvlist_lookup_uint64(got,"guid",&number)==0);
                CHECK(number==UINT64_C(0xfedcba9876543210));
            }
        }
        FILE *f=fopen(argc>1?argv[1]:"SYS:nvpair-aros.xdr","wb");CHECK(f!=NULL);
        if(f){CHECK(fwrite(packed,1,bytes,f)==bytes);CHECK(fclose(f)==0);}
        int truncated_rejected=1;
        for(size_t length=0;length<bytes;length++){
            nvlist_t *bad=NULL;
            if(nvlist_unpack(packed,length,&bad,0)==0)truncated_rejected=0;
            nvlist_free(bad);
        }
        CHECK(truncated_rejected);
    }
    nvlist_free(decoded);nvlist_free(leaf);nvlist_free(pool);free(packed);
    printf("RESULT upstream nvpair failures=%d\n",failures);
    const char *result=failures?"RESULT native nvpair XDR FAIL\n":"RESULT native nvpair XDR PASS\n";
    for(const char *p=result;*p;p++)RawPutChar(*p);
    return failures?20:0;
}
