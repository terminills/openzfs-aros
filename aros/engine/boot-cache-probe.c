/* SPDX-License-Identifier: CDDL-1.0 */
/* Prove the synthetic cache is visible through the exact loader pathname. */
#include <sys/nvpair.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <aros/debug.h>
int main(void)
{
    char bytes[8192];nvlist_t *cache=NULL,*pool=NULL;const char *name=NULL;
    FILE *f=fopen("/boot/zfs/zpool.cache","rb");
    if(!f){bug("RESULT boot cache probe FAIL open errno=%d\n",errno);return 20;}
    size_t n=fread(bytes,1,sizeof(bytes),f);int failed=ferror(f)||!feof(f);
    if(fclose(f))failed=1;
    if(failed||nvlist_unpack(bytes,n,&cache,0)){
        bug("RESULT boot cache probe FAIL read/unpack bytes=%lu\n",(unsigned long)n);return 20;
    }
    failed=nvlist_lookup_nvlist(cache,"unadmitted_probe",&pool)||
        nvlist_lookup_string(pool,"name",&name)||strcmp(name,"unadmitted_probe");
    nvlist_free(cache);
    bug("RESULT boot cache probe %s visible=1 decoded=1\n",failed?"FAIL":"PASS");
    return failed?20:0;
}
