/* Native AROS execution of upstream nvpair allocation, nesting and XDR. */
#include <sys/nvpair.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int failures;
#define CHECK(x) do { int ok=!!(x); printf("%s %s\n",ok?"PASS":"FAIL",#x); if(!ok) failures++; } while(0)
int main(int argc,char **argv) {
    nvlist_t *pool=NULL,*leaf=NULL,*decoded=NULL,*got=NULL;
    char *packed=NULL;const char *path=NULL;size_t bytes=0;uint64_t number=0;
    CHECK(nvlist_alloc(&pool,NV_UNIQUE_NAME,0)==0);
    CHECK(nvlist_alloc(&leaf,NV_UNIQUE_NAME,0)==0);
    if(!pool || !leaf)return 20;
    CHECK(nvlist_add_string(leaf,"path","Work:disk files/vdev0")==0);
    CHECK(nvlist_add_uint64(leaf,"guid",UINT64_C(0xfedcba9876543210))==0);
    CHECK(nvlist_add_nvlist(pool,"vdev",leaf)==0);
    CHECK(nvlist_pack(pool,&packed,&bytes,NV_ENCODE_XDR,0)==0);
    CHECK(bytes>0);
    if(packed){
        CHECK(nvlist_unpack(packed,bytes,&decoded,0)==0);
        if(decoded){
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
        nvlist_t *bad=NULL;CHECK(nvlist_unpack(packed,bytes/2,&bad,0)!=0);nvlist_free(bad);
    }
    nvlist_free(decoded);nvlist_free(leaf);nvlist_free(pool);free(packed);
    printf("RESULT upstream nvpair failures=%d\n",failures);return failures?20:0;
}
