/* Private QEMU fixture: real authenticated mount and DOS files, no mock rows. */
#include "admission-api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <proto/intuition.h>
#include <proto/icon.h>
#include <libraries/security.h>
#include <workbench/workbench.h>
#include <workbench/icon.h>
#include <intuition/intuition.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
struct Library *IconBase;
static void note(const char *s)
{for(;*s;s++)RawPutChar(*s);RawPutChar('\n');}
static int operation(unsigned op,const char *target,const char *arg,const char *options)
{
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return 0;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"%s",target);
    snprintf(m->argument,sizeof(m->argument),"%s",arg?arg:"");
    snprintf(m->options,sizeof(m->options),"%s",options?options:"");
    struct oz_ticket ticket;int e=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!e) {
        do {e=oz_admission_query(OZ_ADMISSION_TEST_PORT,&ticket,m);if(!e&&m->status==OZ_BUSY)Delay(1);}
        while(!e&&m->status==OZ_BUSY);
        if(!e){note(m->detail);e=m->status!=OZ_OK;}
        int release=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);
        if(release)e=release;
    }
    free(m);return !e;
}
static int put(const char *name,const char *text)
{
    BPTR f=Open((CONST_STRPTR)name,MODE_NEWFILE);if(!f)return 0;
    LONG n=Write(f,(APTR)text,strlen(text));LONG closed=Close(f);
    return n==(LONG)strlen(text)&&closed;
}
static int export_fixture(void)
{
    /* Private legacy fixture transport only, not the production pool API. */
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=OZ_NATIVE_TEST_PORT;m->operation=8;m->flags=OZ_CONFIRMED;
    strcpy(m->target,"qemu_mirror");
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok){while(!(m=oz_poll(&client)))Delay(1);ok=m->status==OZ_OK;
        note(m->detail);oz_message_free(m);}
    oz_client_close(&client);return ok;
}
static int icon(const char *name,LONG type)
{
    struct DiskObject *object=GetDefDiskObject(type);if(!object)return 0;
    int ok=PutDiskObject((CONST_STRPTR)name,object);FreeDiskObject(object);return ok;
}
static int delete_when_idle(const char *name)
{
    /* Detail/icon identification can briefly open a file during refresh.
     * Honour the handler's in-use refusal; never force deletion. */
    for(unsigned retry=0;retry<100;retry++) {
        if(DeleteFile((CONST_STRPTR)name)) {
            if(retry){char line[80];snprintf(line,sizeof(line),
                "Wanderer fixture delete recovered after %u in-use retries",retry);note(line);}
            return 1;
        }
        LONG error=IoErr();char diagnostic[192];
        snprintf(diagnostic,sizeof(diagnostic),"Wanderer delete attempt=%u path=%s IoErr=%ld",retry,name,(long)error);
        note(diagnostic);
        if(error!=ERROR_OBJECT_IN_USE)return 0;
        Delay(5);
    }
    return 0;
}
static void prompt(const char *text,const char *button)
{
    /* A separate window: a requester attached to the desktop can inhibit
     * precisely the Wanderer interactions this fixture is meant to test. */
    struct Window *w=OpenWindowTags(NULL,WA_Left,700,WA_Top,650,
        WA_Width,560,WA_Height,115,WA_Title,(IPTR)button,
        WA_Flags,WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET,
        WA_IDCMP,IDCMP_CLOSEWINDOW,TAG_DONE);
    if(!w){note("RESULT Wanderer fixture control window FAIL");exit(20);}
    char lines[512];snprintf(lines,sizeof(lines),"%s\nClose this control window to continue.",text);
    WORD y=20;
    for(char *line=strtok(lines,"\n");line;line=strtok(NULL,"\n"),y+=13) {
        struct IntuiText it={1,0,JAM1,10,y,NULL,(STRPTR)line,NULL};
        PrintIText(w->RPort,&it,0,0);
    }
    int finished=0;
    while(!finished) {
        WaitPort(w->UserPort);
        struct IntuiMessage *msg;
        while((msg=(struct IntuiMessage *)GetMsg(w->UserPort))) {
            if(msg->Class==IDCMP_CLOSEWINDOW)finished=1;
            ReplyMsg((struct Message *)msg);
        }
    }
    CloseWindow(w);
}
int main(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:Security/openzfs-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    int ready=0;
    for(unsigned i=0;i<600&&!ready;i++) {
        Forbid();ready=FindPort((CONST_STRPTR)OZ_ADMISSION_TEST_PORT)!=NULL;Permit();
        if(!ready)Delay(5);
    }
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    IconBase=OpenLibrary((CONST_STRPTR)"icon.library",0);
    if(!ready||!secBase||!IconBase)return 20;
    ULONG previous=secGetTaskOwner(NULL);
    struct TagItem login[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(secLoginA(login)!=((77UL<<16)|88))return 20;
    const char *ds="qemu_mirror/delegated-root/wanderer";int ok=0;
#define CHECK(x) do {if(!(x)){char line[90];snprintf(line,sizeof(line),"RESULT Wanderer fixture FAIL line=%d IoErr=%ld",__LINE__,(long)IoErr());note(line);goto done;}}while(0)
    /* Start the desktop inside the test user's login, not the unauthenticated
     * startup shell. The production handler must not bypass MuFS for a GUI. */
    CHECK(SystemTags((CONST_STRPTR)"Run >T:wanderer.log SYS:System/Wanderer/Wanderer",
        SYS_Asynch,FALSE,TAG_DONE)==0);
    Delay(100);
    CHECK(operation(37,ds,NULL,NULL));
    CHECK(operation(43,ds,"OZBROWSE","readonly=off"));
    BPTR drawer=CreateDir((CONST_STRPTR)"OZBROWSE:Documents");CHECK(drawer);UnLock(drawer);
    CHECK(put("OZBROWSE:ReadMe.txt","Real upstream OpenZFS mounted through the AROS DOS handler.\n"));
    CHECK(put("OZBROWSE:Documents/Before.txt","Visible before the live update.\n"));
    CHECK(put("OZBROWSE:Documents/Iconless.txt","Use Show All Files to see this entry.\n"));
    CHECK(icon("OZBROWSE:disk",WBDISK));
    CHECK(icon("OZBROWSE:Documents",WBDRAWER));
    CHECK(icon("OZBROWSE:ReadMe.txt",WBPROJECT));
    CHECK(icon("OZBROWSE:Documents/Before.txt",WBPROJECT));
    CHECK(SetComment((CONST_STRPTR)"OZBROWSE:Documents/Before.txt",(CONST_STRPTR)"OpenZFS live refresh fixture"));
    note("Wanderer fixture READY volume=OZBROWSE uid=77 gid=88");
    prompt("Open OZBROWSE and its Documents drawer.\nEnable Show All Files to include Iconless.txt.\nCapture the initial listing, then update.","Update files");
    CHECK(Rename((CONST_STRPTR)"OZBROWSE:Documents/Before.txt",(CONST_STRPTR)"OZBROWSE:Documents/After.txt"));
    CHECK(Rename((CONST_STRPTR)"OZBROWSE:Documents/Before.txt.info",(CONST_STRPTR)"OZBROWSE:Documents/After.txt.info"));
    CHECK(delete_when_idle("OZBROWSE:Documents/Iconless.txt"));
    CHECK(put("OZBROWSE:Documents/New.txt","Created while the drawer was open.\n"));
    CHECK(icon("OZBROWSE:Documents/New.txt",WBPROJECT));
    note("Wanderer fixture UPDATED expected=After.txt,New.txt");
    prompt("Check the open drawer refreshed: After.txt and New.txt.\nBefore.txt and Iconless.txt should be gone.\nClose the OpenZFS drawers before continuing.","Finish and unmount");
    CHECK(operation(44,ds,NULL,NULL));
    CHECK(export_fixture());
    ok=1;note("RESULT Wanderer fixture DOS operations PASS (visual verification separate)");
done:;
    if(!ok) {
        FILE *log=fopen("T:admission-service.log","r");
        if(log){note("Wanderer fixture service failure log:");int c;
            while((c=fgetc(log))!=EOF)RawPutChar(c);
            fclose(log);}
    }
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    if(secLogoutA(logout)!=previous)ok=0;
    CloseLibrary(IconBase);CloseLibrary(secBase);
    return ok?0:20;
}
