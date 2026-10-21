/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable native QEMU fixture. Only the GUI may create gui-created. */
#include "api.h"
#include "admission-api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dostags.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
static void note(const char *s)
{for(;*s;s++)RawPutChar(*s);RawPutChar('\n');}
static void dump(const char *path)
{
    FILE *f=fopen(path,"r");if(!f){note("GUI saved log missing");return;}
    note(path);int c;while((c=fgetc(f))!=EOF)RawPutChar(c);fclose(f);
}
static int request(unsigned operation,const char *target,int expected)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=OZ_NATIVE_TEST_PORT;
    m->operation=operation;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"%s",target);
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok) {
        while(!(m=oz_poll(&client)))Delay(1);
        ok=m->status==OZ_OK&&(expected<0||(expected==0&&m->row_count==0)||
            (m->row_count==1&&!strcmp(m->rows[0].name,target)));
        note(m->detail);oz_message_free(m);
    }
    oz_client_close(&client);return ok;
}
static int checked_property(const char *dataset,const char *property,const char *value,const char *source)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=OZ_NATIVE_TEST_PORT;
    m->operation=40;
    snprintf(m->target,sizeof(m->target),"%s",dataset);
    snprintf(m->argument,sizeof(m->argument),"%s",property);
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok) {
        while(!(m=oz_poll(&client)))Delay(1);
        ok=m->status==OZ_OK&&m->row_count==1&&
            !strcmp(m->rows[0].name,property)&&
            !strcmp(m->rows[0].value,value)&&
            !strcmp(m->rows[0].state,source);
        oz_message_free(m);
    }
    oz_client_close(&client);
    return ok;
}
static int checked_bookmark(void)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=OZ_NATIVE_TEST_PORT;m->operation=36;
    strcpy(m->target,"qemu_mirror/delegated-root/gui-created");
    strcpy(m->options,"bookmarks=on");
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok) {
        while(!(m=oz_poll(&client)))Delay(1);
        unsigned long long guid=0,txg=0,created=0;
        ok=m->status==OZ_OK&&m->row_count==1&&
            !strcmp(m->rows[0].kind,"bookmark")&&
            !strcmp(m->rows[0].name,"qemu_mirror/delegated-root/gui-created#ui")&&
            sscanf(m->rows[0].value,"guid=%llu createtxg=%llu creation=%llu",&guid,&txg,&created)==3&&guid&&txg&&created;
        oz_message_free(m);
    }
    oz_client_close(&client);return ok;
}
static int no_property(const char *dataset,const char *property)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m||!oz_client_open(&client)){free(m);return 0;}
    client.server_name=OZ_NATIVE_TEST_PORT;m->operation=40;
    snprintf(m->target,sizeof(m->target),"%s",dataset);snprintf(m->argument,sizeof(m->argument),"%s",property);
    int ok=oz_submit(&client,m)==OZ_OK;free(m);
    if(ok){
        while(!(m=oz_poll(&client)))Delay(1);
        /* Upstream omits this read-only property when no receive is pending;
         * dsl_prop_get returns ENOENT, not a literal '-' property value. */
        int error=0;const char *p=strstr(m->detail,"errno=");if(p)sscanf(p,"errno=%d",&error);
        ok=m->status==OZ_ENGINE_ERROR&&error==ENOENT&&!m->row_count;
        note(m->detail);oz_message_free(m);
    }
    oz_client_close(&client);return ok;
}
static int stream_mount(const char *dataset,int mount,const char *name,int writable)
{
    struct oz_message m={0},out={0};struct oz_ticket ticket;
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=mount?43:44;m.flags=OZ_CONFIRMED;
    snprintf(m.target,sizeof(m.target),"%s",dataset);
    if(mount){snprintf(m.argument,sizeof(m.argument),"%s",name);strcpy(m.options,writable?"readonly=off":"readonly=on");}
    for(unsigned attempt=0;attempt<100;attempt++){
        int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,&m,&ticket);
        if(error)return 0;
        do{error=oz_admission_query(OZ_ADMISSION_TEST_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        int ok=!error&&out.status==OZ_OK,reported=0;
        const char *field=strstr(out.detail,"errno=");
        if(field)sscanf(field,"errno=%d",&reported);
        char diagnostic[256];snprintf(diagnostic,sizeof(diagnostic),"GUI %s dataset=%s attempt=%u transport=%d status=%lu errno=%d",
            mount?"mount":"unmount",dataset,attempt,error,(unsigned long)out.status,reported);note(diagnostic);
        int released=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);
        if(released)return 0;
        if(ok)return 1;
        /* Only retry an explicitly busy unmount in this private fixture.
         * Live desktop requests/locks may drain when the service pumps them. */
        if(mount||error||reported!=EBUSY)return 0;
        Delay(1);
    }
    return 0;
}
static int receive_mount(const char *dataset,int mount)
{return stream_mount(dataset,mount,"OZGUISTREAM",0);}
static int receive_file(int prepare)
{
    const char *dataset=prepare?"qemu_mirror/stream-artifacts":"qemu_mirror/gui-received";
    if(!receive_mount(dataset,1))return 0;
    BPTR input=Open((CONST_STRPTR)(prepare?"OZGUISTREAM:full.zfs":"OZGUISTREAM:hello.txt"),MODE_OLDFILE);
    BPTR output=prepare?Open((CONST_STRPTR)"RAM:OpenZFS-gui-receive.zfs",MODE_NEWFILE):0;
    char bytes[4096];LONG n=0;int ok=input&&(!prepare||output);
    if(ok&&prepare){while((n=Read(input,bytes,sizeof(bytes)))>0)if(Write(output,bytes,n)!=n){ok=0;break;}if(n<0)ok=0;}
    else if(ok){
        const char *expected="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
        n=Read(input,bytes,sizeof(bytes));ok=n==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)n);
        char diagnostic[128];snprintf(diagnostic,sizeof(diagnostic),"GUI received file read count=%ld expected=%lu match=%d",
            (long)n,(unsigned long)strlen(expected),ok);note(diagnostic);
    }
    if(!input)note("GUI stream input open FAIL");
    if(input&&!Close(input)){note("GUI stream input close FAIL");ok=0;}
    if(output&&!Close(output))ok=0;
    if(!receive_mount(dataset,0))ok=0;
    return ok;
}
static int resume_files(void)
{
    const char *dataset="qemu_mirror/resume-pending";
    if(!receive_mount(dataset,1))return 0;
    const char *names[]={"OZGUISTREAM:large.bin","OZGUISTREAM:embedded.bin","OZGUISTREAM:hello.txt"};
    const char *hello="Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";
    int ok=1;unsigned char bytes[4096];
    for(unsigned f=0;ok&&f<3;f++){
        BPTR input=Open((CONST_STRPTR)names[f],MODE_OLDFILE);
        size_t expected=f==0?1048576:f==1?4096:strlen(hello),total=0;LONG n=0;
        if(!input){ok=0;break;}
        while(ok&&(n=Read(input,bytes,sizeof(bytes)))>0){
            if(total+(size_t)n>expected){ok=0;break;}
            for(LONG i=0;ok&&i<n;i++){
                size_t at=total+(size_t)i;
                unsigned char wanted=f==0?(unsigned char)(((at%4096)*73+((at%4096)>>3)*19+1)&255):f==1?'A':hello[at];
                if(bytes[i]!=wanted)ok=0;
            }
            total+=(size_t)n;
        }
        if(n<0||total!=expected)ok=0;
        if(!Close(input))ok=0;
        char diagnostic[192];snprintf(diagnostic,sizeof(diagnostic),"GUI resumed file %s bytes=%lu expected=%lu match=%d",
            names[f],(unsigned long)total,(unsigned long)expected,ok);note(diagnostic);
    }
    if(!receive_mount(dataset,0))ok=0;
    return ok;
}
static int aborted_files(void)
{
    const char *datasets[]={"qemu_mirror/abort-api-incremental","qemu_mirror/abort-api-incremental@base"};
    for(unsigned i=0;i<2;i++){
        if(!receive_mount(datasets[i],1))return 0;
        BPTR input=Open((CONST_STRPTR)"OZGUISTREAM:payload.bin",MODE_OLDFILE);
        int ok=input!=0;size_t total=0;unsigned char bytes[4096];LONG n=0;
        while(ok&&(n=Read(input,bytes,sizeof(bytes)))>0){
            total+=(size_t)n;if(total>524288){ok=0;break;}
            for(LONG j=0;j<n;j++)if(bytes[j]!='A'){ok=0;break;}
        }
        if(n<0||total!=524288)ok=0;
        if(input&&!Close(input))ok=0;
        if(!receive_mount(datasets[i],0))ok=0;
        if(!ok)return 0;
    }
    return 1;
}
static int gui_passphrase;
static const char *gui_key_dataset="qemu_mirror/encrypted-api-change";
static int gui_key_file(const char *path,unsigned delta)
{
    unsigned char bytes[32];
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)(i+1+delta);
    BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);
    if(!file)return 0;
    const char *phrase=delta==2?"AROS OpenZFS fixture passphrase":"AROS replacement passphrase";
    const void *data=gui_passphrase?(const void *)phrase:bytes;
    LONG length=gui_passphrase?(LONG)strlen(phrase):(LONG)sizeof(bytes);
    int ok=Write(file,data,length)==length;
    if(!Close(file))ok=0;
    return ok;
}
/* Verification only: this fixture never issues key replacement (64). */
static int gui_key_request(unsigned op,const char *path,int expected)
{
    struct oz_message m={0},out={0};struct oz_ticket ticket;
    m.magic=OZ_MAGIC;m.version=OZ_VERSION;m.msg.mn_Length=sizeof(m);
    m.operation=op;m.flags=OZ_CONFIRMED;
    strcpy(m.target,gui_key_dataset);
    if(path)snprintf(m.argument,sizeof(m.argument),"%s",path);
    int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,&m,&ticket);
    if(error)return 0;
    do{error=oz_admission_query(OZ_ADMISSION_TEST_PORT,&ticket,&out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    int released=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket),reported=-1;
    const char *field=strstr(out.detail,"errno=");if(field)sscanf(field,"errno=%d",&reported);
    note(out.detail);
    return !error&&!released&&reported==expected&&out.status==(expected?OZ_ENGINE_ERROR:OZ_OK);
}
static int gui_key_verify(void)
{
    if(!gui_key_request(63,NULL,0)||
        !gui_key_request(62,"RAM:gui-old.key",EACCES)||
        !gui_key_request(62,"RAM:gui-new.key",0))return 0;
    const char *ds=gui_key_dataset;
    if(!stream_mount(ds,1,"OZGUIKEY",0))return 0;
    const char *expected="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    char bytes[128];BPTR file=Open((CONST_STRPTR)"OZGUIKEY:hello.txt",MODE_OLDFILE);
    int ok=0;
    if(file){LONG n=Read(file,bytes,sizeof(bytes));ok=n==(LONG)strlen(expected)&&!memcmp(bytes,expected,strlen(expected));if(!Close(file))ok=0;}
    if(!stream_mount(ds,0,"OZGUIKEY",0))ok=0;
    if(!gui_key_request(63,NULL,0))ok=0;
    return ok;
}
int main(int argc,char **argv)
{
    int promote=argc==2&&!strcmp(argv[1],"PROMOTE");
    int bookmark=argc==2&&!strcmp(argv[1],"BOOKMARK");
    int space=argc==2&&!strcmp(argv[1],"SPACE");
    int compression=argc==2&&!strcmp(argv[1],"COMPRESSION");
    int recordsize=argc==2&&!strcmp(argv[1],"RECORDSIZE");
    int export=argc==2&&!strcmp(argv[1],"EXPORT");
    int resumesend=argc==2&&!strcmp(argv[1],"RESUMESEND");
    int abortreceive=argc==2&&!strcmp(argv[1],"ABORTRECEIVE");
    gui_passphrase=argc==2&&!strcmp(argv[1],"PASSPHRASE");
    if(gui_passphrase)gui_key_dataset="qemu_mirror/encrypted-passphrase";
    int encryption=gui_passphrase||(argc==2&&!strcmp(argv[1],"ENCRYPTION"));
    int ownsend=resumesend||(argc==2&&!strcmp(argv[1],"OWNSEND")),ownmounted=0;
    int send=ownsend||(argc==2&&!strcmp(argv[1],"SEND"));
    int receive=send||(argc==2&&!strcmp(argv[1],"RECEIVE"));
    if(argc!=1&&!promote&&!bookmark&&!space&&!compression&&!recordsize&&!export&&!receive&&!abortreceive&&!encryption)return 20;
    BPTR marker=Lock((CONST_STRPTR)"SYS:Security/openzfs-test-only",ACCESS_READ);
    if(!marker)return 20;
    UnLock(marker);
    int ready=0;
    for(unsigned i=0;i<600&&!ready;i++) {
        Forbid();ready=FindPort((CONST_STRPTR)OZ_ADMISSION_TEST_PORT)!=NULL;Permit();
        if(!ready)Delay(5);
    }
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!ready||!secBase||!secIsConfigured()){note("GUI fixture unavailable");if(secBase)CloseLibrary(secBase);return 20;}
    ULONG previous=secGetTaskOwner(NULL);
    int root=export||receive||abortreceive||encryption;
    struct TagItem tags[]={{secT_UserID,(IPTR)(root?"oztestroot":"ozreader")},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    int logged=secLoginA(tags)==(root?secOWNER_SYSTEM:((77UL<<16)|88)),ok=0;
    if(logged) {
        if(export||ownsend||abortreceive||encryption){
            /* The service harness creates/destroys bounded test endpoints
             * before becoming ready. Start desktop consumers only afterward. */
            /* Asynchronous System owns its input/output/error handles. Never
             * let it inherit handles needed by this launcher or Manager. */
            BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE);
            BPTR output=Open((CONST_STRPTR)"T:wanderer.log",MODE_NEWFILE);
            BPTR errors=Open((CONST_STRPTR)"NIL:",MODE_NEWFILE);
            if(!input||!output||!errors||SystemTags((CONST_STRPTR)"SYS:System/Wanderer/Wanderer",
                SYS_Asynch,TRUE,SYS_Input,input,SYS_Output,output,SYS_Error,errors,
                NP_StackSize,262144,TAG_DONE)==-1){
                if(input)Close(input);if(output)Close(output);if(errors)Close(errors);
                note("GUI desktop launch FAIL");goto logout;
            }
        }
        if(receive&&!ownsend&&!receive_file(1)){note("GUI receive input preparation FAIL");goto logout;}
        if(ownsend){
            if(!stream_mount("qemu_mirror/direct-stream-policy",1,"OZGUIOUT",1)){
                note("GUI own-volume mount FAIL");goto logout;
            }
            ownmounted=1;
            BPTR exists=Lock((CONST_STRPTR)"OZGUIOUT:gui.zfs",ACCESS_READ);
            if(exists){UnLock(exists);note("GUI own-volume output unexpectedly exists");goto logout;}
            note("GUI own-volume output absent: send to OZGUIOUT:gui.zfs, then receive it");
        }
        if(send&&!ownsend){
            /* Remove only our newly prepared private fixture file. The public
             * send API must now create it, without any pre-created output. */
            if(!DeleteFile((CONST_STRPTR)"RAM:OpenZFS-gui-receive.zfs")){
                note("GUI absent output preparation FAIL");goto logout;
            }
            note("GUI send fixture: output absent; send must create it before GUI receive");
        }
        if(encryption&&(!gui_key_file("RAM:gui-old.key",2)||!gui_key_file("RAM:gui-new.key",3))){note("GUI key preparation FAIL");goto logout;}
        note(root?"GUI fixture launching real manager as authenticated root":"GUI fixture launching real manager as MuFS UID77 GID88");
        LONG result=SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative TEST-NATIVE",
            SYS_Asynch,FALSE,NP_StackSize,262144,TAG_DONE);
        note("GUI fixture manager returned");
        dump("T:OpenZFS-Manager.log");dump("T:OpenZFS-Response.txt");
        if(encryption){
            ok=result==0&&gui_key_verify();
            note(ok?"GUI replacement key and exact DOS content PASS":"GUI replacement key and exact DOS content FAIL");
            if(!request(8,"qemu_mirror",-1))ok=0;
            goto logout;
        }
        if(abortreceive){
            ok=result==0&&aborted_files()&&
                no_property("qemu_mirror/abort-api-new","guid")&&
                no_property("qemu_mirror/abort-api-incremental/%recv","guid")&&
                no_property("qemu_mirror/abort-api-incremental","receive_resume_token");
            note(ok?"GUI aborted receives preserve exact DOS head and snapshot bytes PASS":"GUI aborted receives preserve exact DOS head and snapshot bytes FAIL");
            if(!request(8,"qemu_mirror",-1))ok=0;
            goto logout;
        }
        if(receive){
            if(resumesend){
                ok=result==0&&request(36,"qemu_mirror/resume-pending@second",1)&&resume_files()&&
                    no_property("qemu_mirror/resume-pending","receive_resume_token");
                note(ok?"GUI token continuation exact DOS bytes and cleared state PASS":"GUI token continuation exact DOS bytes and cleared state FAIL");
            }else{
                ok=result==0&&request(36,"qemu_mirror/gui-received@ui",1)&&receive_file(0);
                note(ok?"GUI authenticated receive exact DOS bytes PASS":"GUI authenticated receive exact DOS bytes FAIL");
            }
            if(ownmounted){
                if(!stream_mount("qemu_mirror/direct-stream-policy",0,"OZGUIOUT",0))ok=0;
                else ownmounted=0;
            }
            if(!request(8,"qemu_mirror",-1))ok=0;
            if(!ownsend)DeleteFile((CONST_STRPTR)"RAM:OpenZFS-gui-receive.zfs");
            goto logout;
        }
        if(export) {
            ok=result==0&&request(3,"",0);
            note(ok?"GUI authenticated export absent pool PASS":"GUI authenticated export absent pool FAIL");
            goto logout;
        }
        ok=result==0&&request(36,"qemu_mirror/delegated-root/gui-created",1);
        int property;
        if(recordsize) {
            property=checked_property("qemu_mirror/delegated-root","recordsize",
                "65536","qemu_mirror/delegated-root")&&
                checked_property("qemu_mirror/delegated-root/gui-created","recordsize",
                "65536","qemu_mirror/delegated-root");
            note(property?"GUI recordsize inherited value/source PASS":"GUI recordsize inherited value/source FAIL");
        } else if(compression) {
            property=checked_property("qemu_mirror/delegated-root","compression",
                "lz4","qemu_mirror/delegated-root")&&
                checked_property("qemu_mirror/delegated-root/gui-created","compression",
                "lz4","qemu_mirror/delegated-root");
            note(property?"GUI compression inherited value/source PASS":"GUI compression inherited value/source FAIL");
        } else if(space) {
            property=checked_property("qemu_mirror/delegated-root/gui-created","quota",
                "16777216","qemu_mirror/delegated-root/gui-created");
            note(property?"GUI quota value/source PASS":"GUI quota value/source FAIL");
        } else if(bookmark) {
            property=checked_bookmark();
            note(property?"GUI bookmark identity PASS":"GUI bookmark identity FAIL");
        } else if(promote) {
            property=checked_property("qemu_mirror/delegated-root/gui-created","origin",
                "qemu_mirror/delegated-root/gui-promoted@ui","computed");
            if(!request(36,"qemu_mirror/delegated-root/gui-promoted@ui",1))ok=0;
            note(property?"GUI promoted origin PASS":"GUI promoted origin FAIL");
        } else {
            property=checked_property("qemu_mirror/delegated-root/gui-created","org.aros:gui",
                "parent","qemu_mirror/delegated-root");
            note(property?"GUI inherited property value/source PASS":"GUI inherited property value/source FAIL");
        }
        if(!property)ok=0;
        /* Export flushes the private pool for independent host zdb inspection.
         * This fixture request is not a public authenticated export API. */
        if(!request(8,"qemu_mirror",-1))ok=0;
logout: ;
        if(ownmounted&&!stream_mount("qemu_mirror/direct-stream-policy",0,"OZGUIOUT",0))ok=0;
        struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        if(secLogoutA(logout)!=previous||secGetTaskOwner(NULL)!=previous)ok=0;
    }
    CloseLibrary(secBase);
    note(ok?"RESULT authenticated native GUI PASS":"RESULT authenticated native GUI FAIL");
    return ok?0:20;
}
