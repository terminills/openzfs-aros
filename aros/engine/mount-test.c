/* Structured management IPC -> real upstream ZPL -> ordinary AROS DOS. */
#include "../ui/api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
static int failures;
static struct oz_message *request(struct oz_client *c,ULONG op,const char *target,
    const char *arg,const char *options,ULONG flags,ULONG expected)
{
    static struct oz_message m;memset(&m,0,sizeof(m));m.operation=op;m.flags=flags;
    snprintf(m.target,sizeof(m.target),"%s",target);snprintf(m.argument,sizeof(m.argument),"%s",arg);
    snprintf(m.options,sizeof(m.options),"%s",options);
    if(oz_submit(c,&m)!=OZ_OK){failures++;return NULL;}
    struct oz_message *r;while(!(r=oz_poll(c)))Delay(1);
    printf("%s mount API op=%lu status=%lu expected=%lu: %s\n",
        r->status==expected?"PASS":"FAIL",(unsigned long)op,(unsigned long)r->status,(unsigned long)expected,r->detail);
    if(r->status!=expected)failures++;
    for(unsigned i=0;i<r->row_count;i++)printf("row %s=%s source=%s\n",r->rows[i].name,r->rows[i].value,r->rows[i].state);
    return r;
}
static void check(int okay,const char *what)
{
    printf("%s %s\n",okay?"PASS":"FAIL",what);if(!okay)failures++;
}
static void check_stats(struct oz_client *c,const char *dataset,
    unsigned long long reads,unsigned long long bytes)
{
    struct oz_message *r=request(c,36,dataset,"","",0,OZ_OK);
    char expected[100];snprintf(expected,sizeof(expected),
        " mount-reads=%llu mount-read-bytes=%llu",reads,bytes);
    int found=0;
    if(r)for(unsigned i=0;i<r->row_count;i++)
        if(!strcmp(r->rows[i].name,dataset)) {
            char *stats=strstr(r->rows[i].value," mount-reads=");
            if(stats&&!strcmp(stats,expected))found=1;
        }
    check(found,"management row exposes exact upstream mount read counters");
    oz_message_free(r);
}
int main(void)
{
    setvbuf(stdout,NULL,_IONBF,0);
    char pool[160]={0},path[512]={0},dataset[256],line[1024];int passed=0;
    FILE *f=fopen("SYS:import-fixture.log","r");if(!f)return 20;
    while(fgets(line,sizeof(line),f)) {
        sscanf(line,"Creating upstream mirror %159s",pool);
        sscanf(line,"Exported upstream XDR configuration: %511s",path);
        if(strstr(line,"RESULT native mirror data roundtrip PASS"))passed=1;
    }
    fclose(f);if(!passed||!pool[0]||!path[0])return 20;
    snprintf(dataset,sizeof(dataset),"%s/files",pool);
    struct oz_client c;if(!oz_client_open(&c))return 20;c.server_name=OZ_NATIVE_TEST_PORT;
#define REQ(op,t,a,o,fl,e) oz_message_free(request(&c,op,t,a,o,fl,e))
    REQ(7,pool,path,"readonly=on",OZ_CONFIRMED,OZ_OK);
    struct oz_message *property=request(&c,31,pool,"readonly","",0,OZ_OK);
    check(property&&property->row_count==1&&!strcmp(property->rows[0].value,"on"),"pool readonly comes from upstream enum");oz_message_free(property);
    property=request(&c,40,dataset,"used","",0,OZ_OK);
    check(property&&property->row_count==1&&strtoull(property->rows[0].value,NULL,10)>0,"dataset used comes from upstream statistics");oz_message_free(property);
    property=request(&c,40,dataset,"compression","",0,OZ_OK);
    /* Pinned upstream include/sys/zio.h sets DEFAULT to ON. */
    check(property&&property->row_count==1&&!strcmp(property->rows[0].value,"on")&&
        !strcmp(property->rows[0].state,"default"),"dataset default compression and source resolved upstream");oz_message_free(property);
    REQ(40,dataset,"not-a-real-property","",0,OZ_ENGINE_ERROR);
    REQ(40,dataset,"all","",0,OZ_UNSUPPORTED);
    REQ(43,dataset,"OZAPI","readonly=on",OZ_CONFIRMED|OZ_DRYRUN,OZ_UNSUPPORTED);
    REQ(43,dataset,"OZAPI","readonly=on",OZ_CONFIRMED,OZ_OK);
    if(failures){oz_client_close(&c);return 20;}
    REQ(43,dataset,"OZOTHER","readonly=on",OZ_CONFIRMED,OZ_ENGINE_ERROR);
    struct oz_message *r=request(&c,36,pool,"","",OZ_RECURSIVE,OZ_OK);
    int state=0;
    if(r)for(unsigned i=0;i<r->row_count;i++)if(!strcmp(r->rows[i].name,dataset)&&
        !strcmp(r->rows[i].state,"read-only mount OZAPI:"))state=1;
    check(state,"dataset row reflects real DOS mount");oz_message_free(r);
    check_stats(&c,dataset,0,0);
    const char expected[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    BPTR file=Open((CONST_STRPTR)"OZAPI:hello.txt",MODE_OLDFILE);
    check(file!=0,"Open through API-mounted handler");
    if(file) {
        REQ(44,dataset,"","",OZ_CONFIRMED,OZ_BUSY);
        char buf[128];LONG n=Read(file,buf,sizeof(buf));
        check(n==sizeof(expected)-1&&!memcmp(buf,expected,sizeof(expected)-1),"upstream ZPL bytes through DOS");
        check(Close(file),"Close mounted file");
    }
    check_stats(&c,dataset,1,sizeof(expected)-1);
    BPTR root=Lock((CONST_STRPTR)"OZAPI:",SHARED_LOCK);check(root!=0,"Lock mounted root");
    if(root) {
        struct InfoData info;
        check(Info(root,&info)&&info.id_DiskState==ID_WRITE_PROTECTED&&
            info.id_NumBlocks>info.id_NumBlocksUsed&&info.id_NumBlocksUsed>0,
            "Info reports upstream capacity and read-only state");
        REQ(44,dataset,"","",OZ_CONFIRMED,OZ_BUSY);
        struct FileInfoBlock fib;
        int entries=0;if(Examine(root,&fib))while(ExNext(root,&fib))entries++;
        check(entries==2&&IoErr()==ERROR_NO_MORE_ENTRIES,"Examine and ExNext mounted root");
        UnLock(root);
    }
    struct DevProc *old=GetDeviceProc((CONST_STRPTR)"OZAPI:",NULL);
    check(old!=NULL,"retain DeviceProc across detach");
    REQ(44,dataset,"","",OZ_CONFIRMED,OZ_OK);
    if(old) {
        LONG result=DoPkt(old->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0);
        check(!result&&IoErr()==ERROR_DEVICE_NOT_MOUNTED,"late packet gets device-not-mounted, not a stale freed port");
        FreeDeviceProc(old);
    }
    REQ(43,dataset,"OZAPI","readonly=on",OZ_CONFIRMED,OZ_OK);
    file=Open((CONST_STRPTR)"OZAPI:hello.txt",MODE_OLDFILE);
    check(file!=0,"reopen after detach/remount");if(file)Close(file);
    check_stats(&c,dataset,0,0);
    /* Leave this mount available for GUI/manual verification on private :94. */
    REQ(2,"","","",0,OZ_OK);
    oz_client_close(&c);printf("RESULT native mount API failures=%d\n",failures);return failures?20:0;
}
