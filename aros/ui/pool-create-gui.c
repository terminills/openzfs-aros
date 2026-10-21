/* Isolated GUI fixture. Only the real manager may create aros_gui_created.
 * No mock engine or fabricated file rows. */
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
#include <errno.h>
struct Library *IconBase;
#ifdef OZ_GUI_PUBLIC_SERVICE
#define GUI_AUTH_PORT OZ_ADMISSION_PORT
#define GUI_SERVICE_PORT OZ_PORT
#define GUI_MANAGER_COMMAND "C:OpenZFSManagerNative"
#else
#define GUI_AUTH_PORT OZ_ADMISSION_TEST_PORT
#define GUI_SERVICE_PORT OZ_NATIVE_TEST_PORT
#define GUI_MANAGER_COMMAND "C:OpenZFSManagerNative TEST-NATIVE"
#endif
static void note(const char *s)
{for(;*s;s++)RawPutChar(*s);RawPutChar('\n');}
#ifndef OZ_GUI_NATIVE_COLD
static void dump(const char *path)
{
    FILE *f=fopen(path,"r");if(!f){note("GUI saved log missing");return;}
    note(path);int c;while((c=fgetc(f))!=EOF)RawPutChar(c);fclose(f);
}
#endif
static int operation(unsigned op,const char *target,const char *arg,const char *options)
{
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return 0;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"%s",target);
    snprintf(m->argument,sizeof(m->argument),"%s",arg?arg:"");
    snprintf(m->options,sizeof(m->options),"%s",options?options:"");
#ifdef OZ_GUI_NATIVE_COLD
    if(op==7){m->topology.device_count=1;
        strcpy(m->topology.devices[0].path,"/AROS/ata.device/3/partition/0");}
#endif
    struct oz_ticket ticket={0};int e=oz_admission_submit(GUI_AUTH_PORT,m,&ticket);
    if(!e) {
        do {e=oz_admission_query(GUI_AUTH_PORT,&ticket,m);
            if(e==EAGAIN||(!e&&m->status==OZ_BUSY))Delay(1);
        }while(e==EAGAIN||(!e&&m->status==OZ_BUSY));
        if(!e){note(m->detail);e=m->status!=OZ_OK;}
        int release=oz_admission_release(GUI_AUTH_PORT,&ticket);
        if(release)e=release;
    }
    free(m);return !e;
}
static int verify_created_pool(void)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=GUI_SERVICE_PORT;m->operation=4;
    strcpy(m->target,"aros_gui_created");
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok) {
        while(!(m=oz_poll(&client)))Delay(1);
        unsigned mirrors=0,files=0;
#ifdef OZ_GUI_DRAID
        unsigned draids=0,spares=0;
        ok=m->status==OZ_OK&&m->row_count==6;
#else
#ifdef OZ_GUI_NATIVE_PARTITION
        ok=m->status==OZ_OK&&m->row_count==2;
#else
        ok=m->status==OZ_OK&&m->row_count==4;
#endif
#endif
        for(unsigned i=0;i<m->row_count;i++) {
            struct oz_row *r=&m->rows[i];
            mirrors+=!strcmp(r->kind,"mirror");files+=!strcmp(r->kind,"file");
#ifdef OZ_GUI_DRAID
            if(!strcmp(r->kind,"draid")){draids++;if(!strstr(r->value,"parity=1"))ok=0;}
            spares+=!strcmp(r->kind,"dspare");
#endif
#ifdef OZ_GUI_NATIVE_PARTITION
            if(i==1 && (strcmp(r->kind,"disk") ||
                strcmp(r->name,"/AROS/ata.device/3/partition/0")))ok=0;
#endif
            if(strcmp(r->state,"ONLINE")||!strstr(r->value,
                "read-errors=0 write-errors=0 checksum-errors=0"))ok=0;
            note(r->name);note(r->value);
        }
#ifdef OZ_GUI_DRAID
        ok=ok&&draids==1&&spares==1&&mirrors==0&&files==3;
#elif defined(OZ_GUI_NATIVE_PARTITION)
        ok=ok&&mirrors==0&&files==0;
#else
        ok=ok&&mirrors==1&&files==2;
#endif
        note(m->detail);oz_message_free(m);
    }
    oz_client_close(&client);return ok;
}
#ifndef OZ_GUI_NATIVE_PARTITION
static int prepare_files(void)
{
    BPTR dir=CreateDir((CONST_STRPTR)"RAM:OZGuiCreate");if(!dir)return 0;
    UnLock(dir);
    void *zero=calloc(1,1024*1024);if(!zero)return 0;
    int ok=1;
#ifdef OZ_GUI_DRAID
    unsigned members=3,megabytes=128;
#else
    unsigned members=2,megabytes=64;
#endif
    for(unsigned i=0;i<members&&ok;i++) {
        char path[64];snprintf(path,sizeof(path),"RAM:OZGuiCreate/disk%u",i);
        BPTR f=Open((CONST_STRPTR)path,MODE_NEWFILE);if(!f){ok=0;break;}
        for(unsigned n=0;n<megabytes&&ok;n++)ok=Write(f,zero,1024*1024)==1024*1024;
        if(!Close(f))ok=0;
    }
    free(zero);return ok;
}
#endif
#ifndef OZ_GUI_NATIVE_COLD
static int file_and_icon(const char *name,const char *text)
{
    BPTR f=Open((CONST_STRPTR)name,MODE_NEWFILE);if(!f)return 0;
    int ok=Write(f,(APTR)text,strlen(text))==(LONG)strlen(text);
    if(!Close(f))ok=0;
    struct DiskObject *icon=GetDefDiskObject(WBPROJECT);
    if(!icon)return 0;
    if(!PutDiskObject((CONST_STRPTR)name,icon))ok=0;
    FreeDiskObject(icon);return ok;
}
#endif
static int verify_file(const char *name,const char *text)
{
    char data[256];
    BPTR f=Open((CONST_STRPTR)name,MODE_OLDFILE);if(!f)return 0;
    LONG n=Read(f,data,sizeof(data));
    int ok=n==(LONG)strlen(text)&&!memcmp(data,text,strlen(text));
    if(!Close(f))ok=0;
    if(ok)note("GUI create exact DOS file readback PASS");
    return ok;
}
#ifndef OZ_GUI_NATIVE_COLD
static int browse_prompt(void)
{
    struct Window *w=OpenWindowTags(NULL,WA_Left,700,WA_Top,650,
        WA_Width,560,WA_Height,100,WA_Title,(IPTR)"Created pool: browse OZCREATED, then close here",
        WA_Flags,WFLG_DRAGBAR|WFLG_DEPTHGADGET|WFLG_CLOSEGADGET,
        WA_IDCMP,IDCMP_CLOSEWINDOW,TAG_DONE);
    if(!w)return 0;
    struct IntuiText it={1,0,JAM1,10,22,NULL,(STRPTR)"Close volume drawers before closing this window.",NULL};
    PrintIText(w->RPort,&it,0,0);
    int finished=0;
    while(!finished){WaitPort(w->UserPort);struct IntuiMessage *msg;
        while((msg=(struct IntuiMessage *)GetMsg(w->UserPort))) {
            if(msg->Class==IDCMP_CLOSEWINDOW)finished=1;
            ReplyMsg((struct Message *)msg);
        }
    }
    CloseWindow(w);return 1;
}
#endif
int main(void)
{
    BPTR marker=Lock((CONST_STRPTR)"SYS:Security/openzfs-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    IconBase=OpenLibrary((CONST_STRPTR)"icon.library",0);
    if(!secBase||!IconBase)return 20;
    ULONG previous=secGetTaskOwner(NULL);
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(secLoginA(login)!=secOWNER_SYSTEM)return 20;
    int ok=0;
#define CHECK(x) do {if(!(x)){char line[100];snprintf(line,sizeof(line),"RESULT GUI pool creation FAIL line=%d IoErr=%ld",__LINE__,(long)IoErr());note(line);goto done;}}while(0)
#ifdef OZ_GUI_PUBLIC_SERVICE
    note("Public GUI create service bootstrap");
    CHECK(SystemTags((CONST_STRPTR)"Run >T:admission-service.log C:OpenZFSAdmissionServiceHarness WRITABLE",TAG_DONE)==0);
#endif
    int ready=0;
    for(unsigned i=0;i<600&&!ready;i++) {
        Forbid();ready=FindPort((CONST_STRPTR)GUI_AUTH_PORT)!=NULL;Permit();
        if(!ready)Delay(5);
    }
    CHECK(ready);
#ifdef OZ_GUI_NATIVE_COLD
    CHECK(operation(7,"aros_gui_created",NULL,"readonly=on"));
    CHECK(verify_created_pool());
    CHECK(operation(43,"aros_gui_created/files","OZCREATED","readonly=on"));
    CHECK(verify_file("OZCREATED:CreatedByManager.txt","This file is on the upstream pool created in the native OpenZFS Manager.\n"));
    BPTR denied=Open((CONST_STRPTR)"OZCREATED:must-not-exist",MODE_NEWFILE);
    LONG denied_error=IoErr();
    if(denied)Close(denied);
    CHECK(!denied&&denied_error==ERROR_DISK_WRITE_PROTECTED);
    struct DiskObject *icon=GetDiskObject((CONST_STRPTR)"OZCREATED:CreatedByManager.txt");
    CHECK(icon!=NULL);FreeDiskObject(icon);
    CHECK(operation(44,"aros_gui_created/files",NULL,NULL));
    CHECK(operation(8,"aros_gui_created",NULL,NULL));
    ok=1;note("RESULT GUI-created native pool cold import PASS exact-bytes=1 icon=1 readonly-denied=1 export=1");
#else
#ifndef OZ_GUI_NATIVE_PARTITION
    CHECK(prepare_files());
#endif
    CHECK(SystemTags((CONST_STRPTR)"Run >T:wanderer.log SYS:System/Wanderer/Wanderer",SYS_Asynch,FALSE,TAG_DONE)==0);
#ifdef OZ_GUI_DRAID
    note("GUI create READY: pool=aros_gui_created draid1:1d:3c:1s RAM:OZGuiCreate/disk0 disk1 disk2 ashift=12");
#elif defined(OZ_GUI_NATIVE_PARTITION)
    note("GUI create READY: pool=aros_gui_created stripe /AROS/ata.device/3/partition/0 ashift=12");
#else
    note("GUI create READY: pool=aros_gui_created mirror RAM:OZGuiCreate/disk0 RAM:OZGuiCreate/disk1 ashift=12");
#endif
    CHECK(SystemTags((CONST_STRPTR)GUI_MANAGER_COMMAND,SYS_Asynch,FALSE,NP_StackSize,262144,TAG_DONE)==0);
    dump("T:OpenZFS-Manager.log");dump("T:OpenZFS-Response.txt");
    CHECK(verify_created_pool());
    CHECK(operation(37,"aros_gui_created/files",NULL,NULL));
    CHECK(operation(43,"aros_gui_created/files","OZCREATED","readonly=off"));
    CHECK(file_and_icon("OZCREATED:CreatedByManager.txt","This file is on the upstream pool created in the native OpenZFS Manager.\n"));
    CHECK(SetComment((CONST_STRPTR)"OZCREATED:CreatedByManager.txt",(CONST_STRPTR)"Real upstream pool, native DOS handler"));
    note("GUI create MOUNTED volume=OZCREATED file=CreatedByManager.txt");
    CHECK(browse_prompt());
    CHECK(verify_file("OZCREATED:CreatedByManager.txt","This file is on the upstream pool created in the native OpenZFS Manager.\n"));
    CHECK(verify_created_pool());
    CHECK(operation(44,"aros_gui_created/files",NULL,NULL));
    CHECK(operation(8,"aros_gui_created",NULL,NULL));
#ifdef OZ_GUI_NATIVE_PARTITION
    ok=1;note("RESULT GUI native partition pool creation PASS (visual and cold verification separate)");
#else
    CHECK(operation(8,"qemu_mirror",NULL,NULL));
#ifdef OZ_GUI_DRAID
    note("RESULT GUI dRAID pool creation PASS parity=1 files=3 distributed-spares=1 exact-DOS-bytes=1 export=1");
#endif
    ok=1;note("RESULT GUI pool creation PASS (visual verification separate; RAM files are not cold-persistence proof)");
#endif
#endif
done: ;
    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    if(secLogoutA(logout)!=previous)ok=0;
    CloseLibrary(IconBase);CloseLibrary(secBase);
    return ok?0:20;
#undef CHECK
}
