/* Native IPC snapshot persistence test; exclusively uses NativeTest port. */
#include "../ui/api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static struct oz_client client;
static int failures;
static char pool[160],path[512],snapshot[256];
static struct oz_message *call(ULONG op,const char *target,const char *arg,
    const char *options,ULONG flags,ULONG expected)
{
    static struct oz_message m;memset(&m,0,sizeof(m));
    m.operation=op;m.flags=flags;
    snprintf(m.target,sizeof(m.target),"%s",target);
    snprintf(m.argument,sizeof(m.argument),"%s",arg);
    snprintf(m.options,sizeof(m.options),"%s",options);
    ULONG status=oz_submit(&client,&m);
    if(status!=OZ_OK){printf("FAIL submit status=%lu\n",(unsigned long)status);failures++;return NULL;}
    struct oz_message *r;while(!(r=oz_poll(&client)))Delay(1);
    printf("%s op=%lu expected=%lu status=%lu: %s\n",r->status==expected?"PASS":"FAIL",
        (unsigned long)op,(unsigned long)expected,(unsigned long)r->status,r->detail);
    if(r->status!=expected)failures++;
    if(r->status==OZ_TIMEOUT) {
        puts("STOP: unknown mutation outcome; no further requests or retries.");
        oz_message_free(r);oz_client_close(&client);exit(20);
    }
    return r;
}
static void command(ULONG op,const char *target,const char *arg,const char *options,
    ULONG flags,ULONG expected)
{
    struct oz_message *r=call(op,target,arg,options,flags,expected);
    if(r)oz_message_free(r);
}
static void listing(int expect_snapshot)
{
    struct oz_message *r=call(36,pool,"","snapshots=on",OZ_RECURSIVE,OZ_OK);
    if(!r)return;
    unsigned found=0;
    for(unsigned i=0;i<r->row_count;i++) {
        printf("row %s | %s | %s\n",r->rows[i].kind,r->rows[i].name,r->rows[i].value);
        if(!strcmp(r->rows[i].name,snapshot) && !strcmp(r->rows[i].kind,"snapshot"))found++;
    }
    int ok=found==(unsigned)expect_snapshot && r->row_count==(unsigned)(3+expect_snapshot);
    printf("%s snapshot listing expected=%d found=%u rows=%lu\n",ok?"PASS":"FAIL",
        expect_snapshot,found,(unsigned long)r->row_count);
    if(!ok)failures++;
    oz_message_free(r);
}
int main(int argc,char **argv)
{
    int verify=argc==2&&!strcmp(argv[1],"VERIFY");
    /* Keep proof independent of the observed hosted StdC Output ownership
     * anomaly. Preserve stdout too; this is not a runtime-library fix. */
    BPTR proof=Open((CONST_STRPTR)(verify?"SYS:verify-result.txt":"SYS:write-result.txt"),MODE_NEWFILE);
    if(!proof)return 20;
    if(Write(proof,"START\n",6)!=6){Close(proof);return 20;}
    FILE *f=fopen("SYS:import-fixture.log","r");char line[1024];int passed=0;
    if(!f)return 20;
    while(fgets(line,sizeof(line),f)) {
        sscanf(line,"Creating upstream mirror %159s",pool);
        sscanf(line,"Exported upstream XDR configuration: %511s",path);
        if(strstr(line,"RESULT native mirror data roundtrip PASS"))passed=1;
    }
    fclose(f);
    if(!passed||!pool[0]||!path[0]||!oz_client_open(&client))return 20;
    client.server_name=OZ_NATIVE_TEST_PORT;
    snprintf(snapshot,sizeof(snapshot),"%s/payload@api_snapshot",pool);
    command(7,pool,path,"readonly=on",OZ_CONFIRMED,OZ_OK);
    if(verify) {
        listing(1);
        command(50,snapshot,"","",OZ_CONFIRMED,OZ_UNSUPPORTED);
    } else {
        listing(0);
        command(50,snapshot,"","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        listing(0);
        command(8,pool,"","",OZ_CONFIRMED,OZ_OK);
        command(7,pool,path,"readonly=off",OZ_CONFIRMED,OZ_OK);
        command(50,snapshot,"","",0,OZ_BAD_REQUEST);
        command(50,snapshot,"","",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        listing(0);
        command(50,snapshot,"","",OZ_CONFIRMED,OZ_OK);
        command(50,snapshot,"","",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        listing(1);
        command(8,pool,"","",OZ_CONFIRMED,OZ_OK);
        struct oz_message *r=call(3,"","","",0,OZ_OK);
        if(r){if(r->row_count){failures++;puts("FAIL exported pool still listed");}oz_message_free(r);}
    }
    char result[512];
    int length=snprintf(result,sizeof(result),"RESULT snapshot %s pool=%s failures=%d\n",
        verify?"verify":"write",pool,failures);
    if(length<0||length>=(int)sizeof(result)||Write(proof,result,length)!=length)failures++;
    if(!Close(proof))failures++;
    oz_client_close(&client);
    Forbid();struct MsgPort *p=FindPort((CONST_STRPTR)OZ_NATIVE_TEST_PORT);
    if(p)Signal(p->mp_SigTask,SIGBREAKF_CTRL_C);
    Permit();
    printf("RESULT native snapshot %s failures=%d\n",verify?"cold verification":"write",failures);
    return failures?20:0;
}
