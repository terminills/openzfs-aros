#ifdef LIB_ZPOOL_BUILD
#include <sys/zfs_context.h>
#include "../engine/admission.h"
#include <proto/security.h>
#include <libraries/security.h>
#endif
#include "api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdio.h>
#include <string.h>
static void service_note(const char *text)
{
    printf("%s\n",text);fflush(stdout);
#ifdef LIB_ZPOOL_BUILD
    /* Lifecycle evidence must survive a launcher with no usable stdio sink. */
    for(const char *p=text;*p;p++)RawPutChar(*p);
    RawPutChar('\n');
#endif
}
int main(int argc,char **argv) {
    int drop=0,test_writes=0,test_native=0;
#ifdef OZ_SERVICE_TEST_MODES
    drop=argc==2&&!strcmp(argv[1],"TEST-DROP");
    test_writes=argc==2&&!strcmp(argv[1],"TEST-WRITE");
    test_native=argc==2&&!strcmp(argv[1],"TEST-NATIVE");
#endif
    if(argc<1||argc>2||(argc==2&&strcmp(argv[1],"WRITABLE")&&
        !drop&&!test_writes&&!test_native)){
        service_note("Invalid startup mode. Default is read-only; WRITABLE requires an authorized system identity.");return 20;
    }
    int public_writes=argc>1&&!strcmp(argv[1],"WRITABLE");
    int writes=test_writes||public_writes;
    if(public_writes){
#ifdef LIB_ZPOOL_BUILD
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        int authorized=secBase&&secIsConfigured()&&secGetTaskOwner(FindTask(NULL))==secOWNER_SYSTEM;
        if(secBase)CloseLibrary(secBase);
        if(!authorized){service_note("Writable startup denied: configured security.library and system task identity required.");return 20;}
#else
        service_note("Writable startup unavailable: no upstream engine linked.");return 20;
#endif
    }
    struct MsgPort *port=CreateMsgPort();struct oz_message *m;
    if(writes || test_native) {
        /* Preserve the launcher's error stream, including read-only ISO
         * boots. Test mode must not require a writable system volume. */
        setvbuf(stderr,NULL,_IONBF,0);
    }
    if(!port)return 20;
    const char *portname=drop?OZ_TEST_PORT:
        (test_writes || test_native?OZ_NATIVE_TEST_PORT:OZ_PORT);
    /* Reserve singleton ownership without advertising a request endpoint
     * before the engine can service requests. Slow initialization must not
     * consume the client's normal operation timeout. */
    char starting_name[128];
    int starting_length=snprintf(starting_name,sizeof(starting_name),"%s.Starting",portname);
    if(starting_length<0||(size_t)starting_length>=sizeof(starting_name)){
        DeleteMsgPort(port);return 20;
    }
    port->mp_Node.ln_Name=starting_name;
    Forbid();
    if(FindPort((CONST_STRPTR)portname)||FindPort((CONST_STRPTR)starting_name)){
        Permit();DeleteMsgPort(port);return 10;
    }
    AddPort(port);Permit();
    int available=oz_engine_start(writes);
    ULONG admission_signals=0;
#ifdef LIB_ZPOOL_BUILD
    struct oz_admission_server *admission=available&&!drop?
        oz_admission_server_open(strcmp(portname,OZ_PORT)?OZ_ADMISSION_TEST_PORT:OZ_ADMISSION_PORT):NULL;
    admission_signals=oz_admission_server_signals(admission);
    service_note(admission?"Native authenticated admission ready":"Native authenticated admission unavailable");
    if(public_writes&&(!available||!admission)){
        service_note("Writable startup failed: engine and authenticated admission are both required.");
        Forbid();RemPort(port);Permit();
        while((m=(struct oz_message*)GetMsg(port)))oz_message_free(m);
        DeleteMsgPort(port);
        if(admission)(void)oz_admission_server_close(admission);
        if(available)(void)oz_engine_stop();
        return 20;
    }
#endif
    Forbid();RemPort(port);port->mp_Node.ln_Name=(char*)portname;AddPort(port);Permit();
    char ready_note[160];snprintf(ready_note,sizeof(ready_note),"OpenZFS management API %d ready; engine %s%s",OZ_VERSION,available?"initialized":"unavailable",drop?"; TEST-DROP responses":"");
    service_note(ready_note);
    for(;;) {
        ULONG signals=Wait((1UL<<port->mp_SigBit)|SIGBREAKF_CTRL_C|oz_engine_signals()|admission_signals);
        oz_engine_pump();
#ifdef LIB_ZPOOL_BUILD
        oz_admission_server_pump(admission);
#endif
        if(signals&SIGBREAKF_CTRL_C) {
#ifdef LIB_ZPOOL_BUILD
            if(oz_admission_server_busy(admission)) {
                service_note("Service remains resident: active client sessions or admitted operations.");
            } else
#endif
            {
                if(oz_engine_stop())break;
                service_note("Service remains resident: mounted or retired DOS endpoints still require servicing.");
            }
        }
        while((m=(struct oz_message*)GetMsg(port))) {
            if(!strcmp(portname,OZ_PORT))oz_broker_process_public(m,drop);
            else oz_broker_process(m,drop);
        }
    }
#ifdef LIB_ZPOOL_BUILD
    (void)oz_admission_server_close(admission);
#endif
    Forbid();RemPort(port);Permit();
    while((m=(struct oz_message*)GetMsg(port)))oz_message_free(m);
    DeleteMsgPort(port);
    service_note("OpenZFS service engine shutdown complete");return 0;
}
