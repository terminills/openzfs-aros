/* SPDX-License-Identifier: CDDL-1.0 */
#include "../ui/startup-import.h"
#include "../ui/startup-settings.h"
#include <stdio.h>
#include <string.h>
int oz_startup_mount_pool(const char *,uint64_t);
int main(int argc,char **argv)
{
    if((argc!=2&&argc!=3)||(argc==3&&strcmp(argv[2],"RESTORE")&&strcmp(argv[2],"PREPARE")&&strcmp(argv[2],"CHECK"))||strlen(argv[1])!=16){
        fprintf(stderr,"Usage: OpenZFSStartup GUID-16-HEX [RESTORE|PREPARE|CHECK]\n");return 1;}
    uint64_t guid=0;
    for(unsigned i=0;i<16;i++){
        unsigned d;char c=argv[1][i];
        if(c>='0'&&c<='9')d=c-'0';else if(c>='a'&&c<='f')d=c-'a'+10;
        else if(c>='A'&&c<='F')d=c-'A'+10;else return 1;
        guid=(guid<<4)|d;
    }
    if(!guid)return 1;
    if(argc==3&&!strcmp(argv[2],"CHECK")) {
        static struct oz_startup_settings settings;
        int error=oz_startup_settings_trusted(guid,&settings);
        if(error){fprintf(stderr,"Startup check: errno=%d; no import attempted\n",error);return 1;}
        printf("Trusted startup record: pool=%s guid=%016llx members=%u enabled=%s readonly=%s mount=%s\n",
            settings.pool,(unsigned long long)settings.guid,(unsigned)settings.count,
            settings.flags&OZ_STARTUP_ENABLED?"yes":"no",
            settings.flags&OZ_STARTUP_READONLY?"yes":"no",
            settings.flags&OZ_STARTUP_MOUNT?"yes":"no");
        printf("Record validation only; devices not probed, no service started or pool imported\n");
        return 0;
    }
    if(argc==3){
        unsigned published=0;
        int error=oz_startup_settings_restore_trusted(guid,&published);
        if(error){fprintf(stderr,"Startup restore: errno=%d published=%u\n",error,published);return 1;}
        if(!strcmp(argv[2],"PREPARE")){
            printf("Startup ENV record prepared; no pool import\n");return 0;
        }
    }
    static struct oz_startup_import_result result;
    int error=oz_startup_import_trusted(OZ_ADMISSION_PORT,guid,&result);
    if(error){
        fprintf(stderr,"Startup import: errno=%d status=%lu imported=%d release=%d ticket=%llu:%llu %s\n",
            error,(unsigned long)result.status,result.imported,result.release_error,
            (unsigned long long)result.ticket.epoch,(unsigned long long)result.ticket.number,result.detail);
        return 1;
    }
    if(result.skipped){printf("Startup record disabled; no import\n");return 0;}
    if(!result.imported)return 1;
    printf("Startup imported %s\n",result.pool);
    if(!result.mount_requested){printf("Startup mounting disabled\n");return 0;}
    error=oz_startup_mount_pool(result.pool,guid);
    if(error)fprintf(stderr,"Startup mounting failed; pool remains imported; inspect partial mounts\n");
    return error?1:0;
}
