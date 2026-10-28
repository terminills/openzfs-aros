/* SPDX-License-Identifier: CDDL-1.0 */
/* Diagnostic executable test only, on a read-only ISO member fixture. */
#include <proto/dos.h>
#include <dos/dostags.h>
#include <aros/debug.h>
#include <stdio.h>
#include <string.h>
#include "zdb-fixture-paths.h"
#ifdef OZ_ZDB_DEVICE
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#endif
#ifdef OZ_ZDB_L2ARC
static int check_l2arc(void)
{
    const char *names[]={"little","big","missing"
#ifdef OZ_ZDB_L2ARC_LOGS
        ,"log-little","log-big","log-bad","log-little"
#endif
    };
    unsigned iso_count=sizeof(names)/sizeof(names[0]),count=iso_count;
#ifdef OZ_ZDB_L2ARC_DEVICE
    count++;
#endif
    for(unsigned pass=0;pass<count;pass++){
        char command[160];
        if(pass<iso_count)snprintf(command,sizeof(command),"C:ZdbAudit %s /SYS/zdb-l2arc/%s >RAM:zdb-l2arc.log",
            pass<3?"-l":"-lll",names[pass]);
#ifdef OZ_ZDB_L2ARC_DEVICE
        struct Library *secBase=NULL;ULONG previous=0;
        if(pass==iso_count){
            secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);if(!secBase)return 20;
            previous=secGetTaskOwner(FindTask(NULL));
            struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
            if(secLoginA(root)!=secOWNER_SYSTEM){CloseLibrary(secBase);return 20;}
            strcpy(command,"C:ZdbAudit -l /AROS/ata.device/1 >RAM:zdb-l2arc.log");
        }
#endif
        LONG status=SystemTags((CONST_STRPTR)command,NP_StackSize,65536,TAG_DONE);
#ifdef OZ_ZDB_L2ARC_DEVICE
        if(secBase){
            struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
            if(secLogoutA(logout)!=previous)status=20;
            CloseLibrary(secBase);
        }
#endif
        bug("RESULT native zdb l2arc pass=%u exit=%ld\n",pass,(long)status);
        FILE *report=fopen("RAM:zdb-l2arc.log","r");if(!report)return 20;
        char line[1024];unsigned lines=0;
        while(fgets(line,sizeof(line),report)){bug("ZDBL2[%u] %s",pass,line);lines++;}
        LONG expected=pass<iso_count&&!strcmp(names[pass],"log-bad")?1:0;
        int failed=ferror(report)||!lines||status!=expected;fclose(report);
        if(failed)return 20;
    }
    bug("RESULT native zdb l2arc PASS headers=3 storage=readonly-iso\n");
#ifdef OZ_ZDB_L2ARC_LOGS
    bug("RESULT native zdb l2arc PASS logs=4 endian=2 corrupt=1 recovery=1\n");
#endif
    return 0;
}
#endif
#ifdef OZ_ZDB_DEVICE
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
static int check_device(void)
{
#if defined(OZ_ZDB_NO_SECURITY) || defined(OZ_ZDB_MISSING_SECURITY)
    LONG status=SystemTags((CONST_STRPTR)"C:OpenZFSZdbUserTest >RAM:zdb-device.log",
        NP_StackSize,65536,TAG_DONE);
    bug("RESULT native zdb device pass=0 exit=%ld\n",(long)status);
    FILE *report=fopen("RAM:zdb-device.log","r");
    if(!report)return 20;
    char line[1024];unsigned lines=0;
    while(fgets(line,sizeof(line),report)){bug("ZDBDEV[0] %s",line);lines++;}
    int failed=ferror(report)||!lines||status!=1;
    fclose(report);
    if(failed)return 20;
#ifdef OZ_ZDB_MISSING_SECURITY
    bug("RESULT native zdb device PASS missing-security=1\n");
#else
    bug("RESULT native zdb device PASS no-security=1\n");
#endif
    return 0;
#else
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    ULONG previous=secGetTaskOwner(FindTask(NULL));
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    int failed=0;
    for(unsigned pass=0;pass<3&&!failed;pass++) {
        ULONG expected=secOWNER_SYSTEM;
        ULONG logged=secLoginA(root);
        if(logged!=expected)failed=1;
        if(!failed) {
#ifdef OZ_ZDB_PARTITIONS
            const char *command=pass==1?
                "C:OpenZFSZdbUserTest >RAM:zdb-device.log":pass==0?
                "C:ZdbAudit -l /AROS/ata.device/0/partition/0 >RAM:zdb-device.log":
#ifdef OZ_ZDB_NATIVE_POOL
                "C:ZdbAudit -l /AROS/ata.device/0/partition/0 >RAM:zdb-device.log";
#else
                "C:ZdbAudit -l " OZ_ZDB_SECOND_PATH " >RAM:zdb-device.log";
#endif
#else
            const char *command=pass==1?
                "C:OpenZFSZdbUserTest >RAM:zdb-device.log":
                "C:ZdbAudit -l /AROS/ata.device/0 >RAM:zdb-device.log";
#endif
            LONG status=SystemTags((CONST_STRPTR)command,NP_StackSize,65536,TAG_DONE);
            bug("RESULT native zdb device pass=%u exit=%ld\n",pass,(long)status);
            FILE *report=fopen("RAM:zdb-device.log","r");
            if(!report)failed=1;
            else {
                char line[1024];unsigned lines=0;
                while(fgets(line,sizeof(line),report)) {bug("ZDBDEV[%u] %s",pass,line);lines++;}
                failed=ferror(report)||!lines;
                fclose(report);
            }
            if(status!=(pass==1?1:0))failed=1;
        }
        if(logged&&secLogoutA(logout)!=previous)failed=1;
        if(secGetTaskOwner(FindTask(NULL))!=previous)failed=1;
    }
    CloseLibrary(secBase);
    if(failed)return 20;
    bug("RESULT native zdb device PASS root-denied-root=1\n");
    return 0;
#endif
}
#endif

#ifdef OZ_ZDB_BAD_LABELS
static int check_bad_labels(void)
{
    const char *commands[]={
        "C:ZdbAudit -l /SYS/zdb-bad/one-bad >RAM:zdb-bad.log",
        "C:ZdbAudit -l /SYS/zdb-member >RAM:zdb-bad.log",
        "C:ZdbAudit -l /SYS/zdb-bad/all-bad >RAM:zdb-bad.log",
        "C:ZdbAudit -l /SYS/zdb-member >RAM:zdb-bad.log"
    };
    const LONG expected[]={1,0,2,0};
    for(unsigned pass=0;pass<4;pass++) {
        LONG status=SystemTags((CONST_STRPTR)commands[pass],NP_StackSize,65536,TAG_DONE);
        bug("RESULT native zdb damaged pass=%u exit=%ld\n",pass,(long)status);
        FILE *report=fopen("RAM:zdb-bad.log","r");
        if(!report)return 20;
        char line[1024];unsigned lines=0;
        while(fgets(line,sizeof(line),report)) {
            bug("ZDBBAD[%u] %s",pass,line);lines++;
        }
        int failed=ferror(report)!=0;
        fclose(report);
        if(status!=expected[pass]||failed||!lines)return 20;
    }
    bug("RESULT native zdb damaged PASS cases=4\n");
    return 0;
}
#endif

#ifdef OZ_ZDB_POOL
static int check_pool(void)
{
    const char *commands[]={
#ifdef OZ_ZDB_NATIVE_POOL
        "C:OpenZFSZdbRootTest -P -e -p /AROS/ata.device/0/partition/0 -p " OZ_ZDB_SECOND_PATH " -bcc topo_source >RAM:zdb-pool.log",
        "C:OpenZFSZdbRootTest -P -e -p /AROS/ata.device/0/partition/0 -p " OZ_ZDB_SECOND_PATH " -dddd topo_source >RAM:zdb-pool.log",
        "C:OpenZFSZdbRootTest -P -e -p /AROS/ata.device/0/partition/0 -p /AROS/ata.device/0/partition/0 -p " OZ_ZDB_SECOND_PATH " -bcc topo_source >RAM:zdb-pool.log"
#else
        "C:ZdbAudit -P -e -p /SYS/zdb-pool -bcc topo_source >RAM:zdb-pool.log",
        "C:ZdbAudit -P -e -p /SYS/zdb-pool -dddd topo_source >RAM:zdb-pool.log"
#endif
    };
    for(unsigned pass=0;pass<sizeof(commands)/sizeof(commands[0]);pass++) {
#ifdef OZ_ZDB_NATIVE_POOL
        if(pass==2) {
            LONG status=SystemTags((CONST_STRPTR)
                "C:OpenZFSZdbRootTest -e -p /AROS/ata.device/0/partition/0 -p " OZ_ZDB_MISSING_PATH " -bcc topo_source >RAM:zdb-missing.log",
                NP_StackSize,65536,TAG_DONE);
            bug("RESULT native zdb partial admission exit=%ld\n",(long)status);
            FILE *missing=fopen("RAM:zdb-missing.log","r");
            if(!missing)return 20;
            char line[1024];unsigned lines=0;
            while(fgets(line,sizeof(line),missing)){bug("ZDBMISSING %s",line);lines++;}
            int failed=ferror(missing)||!lines||status!=1;
            fclose(missing);
            if(failed){bug("RESULT native zdb labels FAIL partial-admission\n");return 20;}
        }
#endif
        bug("RESULT native zdb pool BEGIN pass=%u\n",pass);
        LONG status=SystemTags((CONST_STRPTR)commands[pass],NP_StackSize,65536,TAG_DONE);
        FILE *report=fopen("RAM:zdb-pool.log","r");
        bug("RESULT native zdb pool exit pass=%u status=%ld\n",pass,(long)status);
        if(!report){bug("RESULT native zdb labels FAIL missing-pool-report=%u\n",pass);return 20;}
        unsigned char bytes[128];char hex[257];
        unsigned long offset=0;size_t count;
        static const char digits[]="0123456789abcdef";
        while((count=fread(bytes,1,sizeof(bytes),report))!=0) {
            for(size_t i=0;i<count;i++) {
                hex[i*2]=digits[bytes[i]>>4];hex[i*2+1]=digits[bytes[i]&15];
            }
            hex[count*2]=0;
            bug("ZDBDATA[%u] offset=%lu bytes=%lu %s\n",pass,offset,(unsigned long)count,hex);
            offset+=count;
        }
        int failed=ferror(report)!=0;
        fclose(report);
        if(status||failed||!offset) {
            bug("RESULT native zdb labels FAIL pool-pass=%u\n",pass);return 20;
        }
        bug("RESULT native zdb pool report pass=%u bytes=%lu\n",pass,offset);
    }
    bug("RESULT native zdb pool PASS block-and-objects=1\n");
    return 0;
}
#endif

int main(void)
{
    bug("RESULT native zdb labels BEGIN\n");
    for (unsigned run = 0; run < 2; ++run) {
        LONG status = SystemTags((CONST_STRPTR)
            "C:ZdbAudit -l /SYS/zdb-member >RAM:zdb-labels.log",
            /* dump_label holds all four 256-KiB vdev labels on its stack,
             * in addition to the caller and decoder frames. */
#ifdef OZ_ZDB_SHARED
            /* The public library entry must provide its own large stack. */
            NP_StackSize, 65536, TAG_DONE);
#else
            NP_StackSize, 4194304, TAG_DONE);
#endif
        FILE *report = fopen("RAM:zdb-labels.log", "r");
        int name = 0, labels = 0, failed = 0;
        char line[1024];
        bug("RESULT native zdb labels run=%u exit=%ld\n", run, (long)status);
        if (!report) return 20;
        while (fgets(line, sizeof(line), report)) {
            bug("ZDB[%u] %s", run, line);
            name |= strstr(line, "name: 'topo_source'") != NULL;
            labels |= strstr(line, "labels = 0 1 2 3 ") != NULL;
            failed |= strstr(line, "failed to ") != NULL;
        }
        failed |= ferror(report) != 0;
        fclose(report);
        if (status || !name || !labels || failed) {
            bug("RESULT native zdb labels FAIL run=%u name=%d labels=%d errors=%d\n",
                run, name, labels, failed);
            return 20;
        }
    }
#ifdef OZ_ZDB_SHARED
    LONG lifecycle=SystemTags((CONST_STRPTR)
        "C:OpenZFSZdbLifetimeTest >RAM:zdb-lifetime.log",
        NP_StackSize,65536,TAG_DONE);
    if(lifecycle!=7){
        bug("RESULT native zdb labels FAIL lifecycle-exit=%ld\n",(long)lifecycle);
        return 20;
    }
    bug("RESULT shared zdb caller exit PASS status=7\n");
#endif
#ifdef OZ_ZDB_POOL
    if(check_pool())return 20;
#endif
#ifdef OZ_ZDB_BAD_LABELS
    if(check_bad_labels()){
        bug("RESULT native zdb labels FAIL damaged-labels\n");return 20;
    }
#endif
#ifdef OZ_ZDB_DEVICE
    if(check_device()){
        bug("RESULT native zdb labels FAIL native-device\n");return 20;
    }
#endif
#ifdef OZ_ZDB_L2ARC
    if(check_l2arc()){
        bug("RESULT native zdb labels FAIL l2arc\n");return 20;
    }
#endif
    bug("RESULT native zdb labels PASS runs=2 storage=readonly-iso\n");
    return 0;
}
