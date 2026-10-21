/* Native Exec IPC acceptance test; only addresses the dedicated test port. */
#include "../ui/api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
static int failures;
static unsigned pools;
static const char *poolname;
static void request(struct oz_client *c, ULONG op, ULONG expected, ULONG version)
{
    static struct oz_message m;
    memset(&m,0,sizeof(m));m.operation=op;m.version=version;
    m.flags=OZ_CONFIRMED|OZ_DRYRUN; /* actual manager defaults */
    if (oz_submit(c,&m)!=OZ_OK) {failures++;puts("FAIL submit");return;}
    struct oz_message *r;
    while (!(r=oz_poll(c))) Delay(1);
    int valid=r->status==expected;
    if (expected==OZ_OK) {
        valid=valid && r->engine_available && r->row_count==(op==3?pools:0) &&
            r->capability_words[0]==((1UL<<1)|(1UL<<2)|(1UL<<3)|(1UL<<7)|(1UL<<31));
        if (op==3 && pools) valid=valid && !strcmp(r->rows[0].name,poolname);
    }
    printf("%s op=%lu status=%lu engine=%lu rows=%lu: %s\n",
        valid?"PASS":"FAIL",(unsigned long)op,(unsigned long)r->status,
        (unsigned long)r->engine_available,(unsigned long)r->row_count,r->detail);
    if (!valid)failures++;
    oz_message_free(r);
}
static void import_request(struct oz_client *c,const char *path,ULONG flags,ULONG expected)
{
    static struct oz_message m; memset(&m,0,sizeof(m));
    m.operation=7;m.flags=flags;
    snprintf(m.target,sizeof(m.target),"%s",poolname);
    snprintf(m.argument,sizeof(m.argument),"%s",path);
    strcpy(m.options,"readonly=on");
    if(oz_submit(c,&m)!=OZ_OK){failures++;return;}
    struct oz_message *r;while(!(r=oz_poll(c)))Delay(1);
    int valid=r->status==expected;
    printf("%s import flags=%lu expected=%lu got=%lu: %s\n",valid?"PASS":"FAIL",
        (unsigned long)flags,(unsigned long)expected,(unsigned long)r->status,r->detail);
    if(!valid)failures++;
    if(r->status==OZ_OK)pools=1;
    oz_message_free(r);
}
static void datasets(struct oz_client *c)
{
    static struct oz_message m;memset(&m,0,sizeof(m));
    m.operation=36;m.flags=OZ_RECURSIVE|OZ_DRYRUN|OZ_CONFIRMED;
    snprintf(m.target,sizeof(m.target),"%s",poolname);
    if(oz_submit(c,&m)!=OZ_OK){failures++;return;}
    struct oz_message *r;while(!(r=oz_poll(c)))Delay(1);
    char child[200];snprintf(child,sizeof(child),"%s/payload",poolname);
    int root=0,payload=0,files=0;
    char filesystem[200];snprintf(filesystem,sizeof(filesystem),"%s/files",poolname);
    for(unsigned i=0;i<r->row_count;i++) {
        if(!strcmp(r->rows[i].name,poolname))root++;
        if(!strcmp(r->rows[i].name,child))payload++;
        if(!strcmp(r->rows[i].name,filesystem))files++;
        printf("dataset %s | %s | %s\n",r->rows[i].name,r->rows[i].kind,r->rows[i].value);
    }
    int valid=r->status==OZ_OK && r->row_count==3 && root==1 && payload==1 && files==1 &&
        r->capability_words[1]==((1UL<<(36-32))|(1UL<<(40-32))|(1UL<<(43-32))|(1UL<<(44-32)));
    printf("%s native recursive dataset enumeration: %s\n",valid?"PASS":"FAIL",r->detail);
    if(!valid)failures++;
    oz_message_free(r);
}
int main(int argc,char **argv)
{
    char fixture_pool[160]={0},fixture_path[512]={0};
    if(argc==2 && !strcmp(argv[1],"FIXTURE")) {
        FILE *f=fopen("SYS:import-fixture.log","r");char line[1024];int passed=0;
        if(!f)return 20;
        while(fgets(line,sizeof(line),f)) {
            sscanf(line,"Creating upstream mirror %159s",fixture_pool);
            sscanf(line,"Exported upstream XDR configuration: %511s",fixture_path);
            if(strstr(line,"RESULT native mirror data roundtrip PASS"))passed=1;
        }
        fclose(f);
        if(!passed || !fixture_pool[0] || !fixture_path[0])return 20;
    }
    struct oz_client c;
    if (!oz_client_open(&c))return 20;
    c.server_name=OZ_NATIVE_TEST_PORT;
    request(&c,1,OZ_OK,OZ_VERSION);
    request(&c,2,OZ_OK,OZ_VERSION);
    request(&c,3,OZ_OK,OZ_VERSION);
    if(argc==3 || fixture_pool[0]) {
        poolname=fixture_pool[0]?fixture_pool:argv[1];
        const char *path=fixture_pool[0]?fixture_path:argv[2];
        import_request(&c,path,0,OZ_BAD_REQUEST);
        import_request(&c,path,OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
        import_request(&c,"/Work/export.nvlist",OZ_CONFIRMED,OZ_ENGINE_ERROR);
        request(&c,3,OZ_OK,OZ_VERSION);
        import_request(&c,path,OZ_CONFIRMED,OZ_OK);
        request(&c,3,OZ_OK,OZ_VERSION);
        datasets(&c);
        import_request(&c,path,OZ_CONFIRMED,OZ_ENGINE_ERROR);
        request(&c,3,OZ_OK,OZ_VERSION);
    }
    request(&c,34,OZ_UNSUPPORTED,OZ_VERSION);
    request(&c,1,OZ_BAD_VERSION,99);
    request(&c,3,OZ_OK,OZ_VERSION);
    oz_client_close(&c);
    Forbid();
    struct MsgPort *p=FindPort((CONST_STRPTR)OZ_NATIVE_TEST_PORT);
    if(p)Signal(p->mp_SigTask,SIGBREAKF_CTRL_C);
    Permit();
    printf("RESULT native management IPC failures=%d\n",failures);
    return failures?20:0;
}
