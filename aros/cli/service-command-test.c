/* SPDX-License-Identifier: CDDL-1.0 */
/* Real upstream binaries, against the parent's disposable imported pool. */
#include <proto/dos.h>
#include <proto/exec.h>
#include <aros/debug.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dostags.h>
#include <dos/dosextens.h>
#include "process.h"
#include "../ui/api.h"
#include "../ui/admission-api.h"
#include "../ui/partition-api.h"
#include "../ui/mount-api.h"
#include "../ui/startup-settings.h"
#include "../ui/iostat-rates.h"
#include "../ui/zvol-api.h"
#include "../ui/project-api.h"
#include "../ui/rewrite-api.h"
#include "../ui/project-worker.h"
#ifdef OZ_TEST_USB_MEMBERS
#define OZ_TEST_PRIMARY_DEVICE "usbscsi.device"
#elif defined(OZ_TEST_AHCI_MEMBERS)
#define OZ_TEST_PRIMARY_DEVICE "ahci.device"
#else
#define OZ_TEST_PRIMARY_DEVICE "ata.device"
#endif
#define OZ_TEST_PRIMARY0 "/AROS/" OZ_TEST_PRIMARY_DEVICE "/0/partition/0"
#define OZ_TEST_PRIMARY1 "/AROS/" OZ_TEST_PRIMARY_DEVICE "/1/partition/0"
#define OZ_TEST_PRIMARY_ASSIGN0 "path=" OZ_TEST_PRIMARY0
#ifdef OZ_TEST_HISTORY_RACE
#include <pthread.h>
#endif
int oz_iostat_rates_test(void);
int oz_history_wire_test(const char *,unsigned,unsigned);
#ifdef OZ_TEST_HISTORY_RACE
int oz_history_wire_race_test(const char *,int (*)(void *,unsigned *,unsigned *),void *);
#endif
static int run(char *const [],long,const char *);
static int trace_run;
static int send_to_file(char *const argv[],const char *path)
{
    BPTR output=Open((CONST_STRPTR)path,MODE_NEWFILE);
    BPTR errors=Open((CONST_STRPTR)"RAM:openzfs-cli-stream-errors",MODE_NEWFILE);
    if(!output||!errors){if(output)Close(output);if(errors)Close(errors);return 1;}
    if(IsInteractive(output)){Close(output);Close(errors);return 1;}
    struct oz_cli_result result={0};
    int error=oz_cli_spawn_exact(argv[0],argv,output,errors,&result);
    if(!error&&(result.status||result.dos_error))error=1;
    if(!Flush(output))error=1;
    if(!Close(output))error=1;
    Close(errors);
    errors=Open((CONST_STRPTR)"RAM:openzfs-cli-stream-errors",MODE_OLDFILE);
    if(errors){char text[2048];LONG got=Read(errors,text,sizeof(text)-1);Close(errors);
        if(got>=0){text[got]=0;bug("Native CLI send output: %s\n",text);}}
    bug("Native CLI stream status=%ld dos=%ld error=%d output=%s\n",result.status,result.dos_error,error,path);
    return error;
}
static int receive_from_file_expected(char *const argv[],const char *path,long expected_status)
{
    BPTR input=Open((CONST_STRPTR)path,MODE_OLDFILE);
    BPTR output=Open((CONST_STRPTR)"RAM:openzfs-cli-receive-output",MODE_NEWFILE);
    if(!input||!output){if(input)Close(input);if(output)Close(output);return 1;}
    BPTR previous=SelectInput(input);
    struct oz_cli_result result={0};
    int error=oz_cli_spawn_exact(argv[0],argv,output,output,&result);
    SelectInput(previous);Close(input);Close(output);
    if(!error&&(result.status!=expected_status||result.dos_error))error=1;
    output=Open((CONST_STRPTR)"RAM:openzfs-cli-receive-output",MODE_OLDFILE);
    if(output){char text[2048];LONG got=Read(output,text,sizeof(text)-1);Close(output);
        if(got>=0){text[got]=0;bug("Native CLI receive output: %s\n",text);}}
    bug("Native CLI receive status=%ld dos=%ld error=%d\n",result.status,result.dos_error,error);
    return error;
}
static int receive_from_file(char *const argv[],const char *path)
{
    return receive_from_file_expected(argv,path,0);
}
struct pipeline_completion {
    struct Message message;
    struct MsgPort *port;
    BPTR writer,errors;
    LONG status;
};
static void pipeline_sender_done(IPTR code,IPTR data)
{
    struct pipeline_completion *done=(void *)data;
    done->status=(LONG)code;
    /* Close writer before notifying: receiver must observe EOF. No CRT here. */
    Close(done->writer);Close(done->errors);
    PutMsg(done->port,&done->message);
}
#ifndef OZ_TEST_PIPE_LEGACY
static int pipe_partial_close(void)
{
    unsigned char *bytes=malloc(65536),readback[256];
    struct MsgPort *reply=CreateMsgPort();
    BPTR writer=Open((CONST_STRPTR)"PIPE:openzfs-partial-close",MODE_NEWFILE);
    BPTR reader=writer?Open((CONST_STRPTR)"PIPE:openzfs-partial-close",MODE_OLDFILE):0;
    if(!bytes||!reply||!writer||!reader){
        if(reader)Close(reader);if(writer)Close(writer);if(reply)DeleteMsgPort(reply);free(bytes);return 1;
    }
    memset(bytes,0x5a,65536);
    struct FileHandle *fh=BADDR(writer);
    struct StandardPacket packet={0};
    packet.sp_Msg.mn_Node.ln_Name=(void *)&packet.sp_Pkt;
    packet.sp_Msg.mn_Length=sizeof(packet);
    packet.sp_Pkt.dp_Link=&packet.sp_Msg;
    packet.sp_Pkt.dp_Type=ACTION_WRITE;
    packet.sp_Pkt.dp_Arg1=fh->fh_Arg1;
    packet.sp_Pkt.dp_Arg2=(IPTR)bytes;
    packet.sp_Pkt.dp_Arg3=65536;
    SendPkt(&packet.sp_Pkt,fh->fh_Type,reply);
    LONG first=Read(reader,readback,sizeof(readback));
    Close(reader);
    /* Keep packet/buffer alive until the pending write is actually replied. */
    WaitPort(reply);struct Message *done=GetMsg(reply);
    IPTR written=packet.sp_Pkt.dp_Res1,error=packet.sp_Pkt.dp_Res2;
    LONG next=Write(writer,bytes,1),next_error=IoErr();
    int failed=done!=&packet.sp_Msg||first!=sizeof(readback)||
        memcmp(bytes,readback,sizeof(readback))||written<first||written>=65536||
        error!=0||next!=-1||next_error!=ERROR_INVALID_LOCK;
    bug("Native pipe partial-close first=%ld written=%ld error=%ld next=%ld next-error=%ld\n",
        (long)first,(long)written,(long)error,(long)next,(long)next_error);
    Close(writer);DeleteMsgPort(reply);free(bytes);
    if(!failed)bug("RESULT native pipe partial close PASS short-write=1 next-write-failed=1\n");
    return failed;
}
#endif
static int cli_pipeline(int close_reader)
{
    const char *name="PIPE:openzfs-disposable-cli-pipeline";
    BPTR writer=Open((CONST_STRPTR)name,MODE_NEWFILE);
    BPTR reader=writer?Open((CONST_STRPTR)name,MODE_OLDFILE):0;
    BPTR errors=Open((CONST_STRPTR)"RAM:openzfs-pipeline-send-errors",MODE_NEWFILE);
    BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE);
    BPTR segment=LoadSeg((CONST_STRPTR)"C:zfs");
    struct MsgPort *port=CreateMsgPort();
    if(!writer||!reader||!errors||!input||!segment||!port) {
        if(reader)Close(reader);if(writer)Close(writer);if(errors)Close(errors);
        if(input)Close(input);if(segment)UnLoadSeg(segment);if(port)DeleteMsgPort(port);
        return 1;
    }
    struct pipeline_completion done={0};
    done.port=port;done.writer=writer;done.errors=errors;
    done.message.mn_Length=sizeof(done.message);
    struct Process *child=CreateNewProcTags(NP_Seglist,segment,NP_FreeSeglist,TRUE,
        NP_Cli,TRUE,NP_CommandName,(IPTR)"C:zfs",NP_Name,(IPTR)"OpenZFS pipeline sender",
        NP_Arguments,(IPTR)"send qemu_mirror/gui-crypt@raw\n",NP_StackSize,262144,
        NP_Input,input,NP_CloseInput,TRUE,NP_Output,writer,NP_CloseOutput,FALSE,
        NP_Error,errors,NP_CloseError,FALSE,NP_CopyVars,TRUE,
        NP_ExitCode,(IPTR)pipeline_sender_done,NP_ExitData,(IPTR)&done,TAG_DONE);
    if(!child){Close(reader);Close(writer);Close(errors);Close(input);UnLoadSeg(segment);DeleteMsgPort(port);return 1;}
    if(close_reader) {
        unsigned char bytes[8192];LONG first=Read(reader,bytes,sizeof(bytes));
        Close(reader);reader=0;
        /* Bounded observation, not cancellation of any pending request. */
        Delay(50);
        struct Message *message=GetMsg(port);
        if(message) {
            int failed=message!=&done.message||first<=0||done.status==0;
#ifdef OZ_TEST_PIPE_LEGACY
            failed=1; /* Legacy policy must allow replacement-reader recovery. */
#endif
            bug("Native CLI reader-close writer-completed=1 status=%ld first=%ld\n",done.status,first);
            DeleteMsgPort(port);return failed;
        }
        /* Native PIPE may wait for a replacement reader. Recover this test
         * without freeing the live sender's completion storage. */
        reader=Open((CONST_STRPTR)name,MODE_OLDFILE);
        if(!reader) {
            bug("RESULT native zvol transport FAIL reader-close recovery-open\n");
            for(;;)Delay(50); /* host test runner terminates disposable guest */
        }
        unsigned long drained=0;LONG got;
        while((got=Read(reader,bytes,sizeof(bytes)))>0)drained+=(unsigned long)got;
        Close(reader);WaitPort(port);message=GetMsg(port);
        bug("Native CLI reader-close writer-completed=0 recovered=1 status=%ld drained=%lu read=%ld\n",done.status,drained,got);
        DeleteMsgPort(port);
#ifdef OZ_TEST_PIPE_LEGACY
        return message!=&done.message||first<=0||done.status!=0||!drained||got!=0;
#else
        return 1;
#endif
    }
    BPTR output=Open((CONST_STRPTR)"RAM:openzfs-pipeline-receive-errors",MODE_NEWFILE);
    BPTR previous=SelectInput(reader);
    char *args[]={"C:zfs","receive","-u","qemu_mirror/cli-pipeline",NULL};
    struct oz_cli_result result={0};
    int error=output?oz_cli_spawn_exact(args[0],args,output,output,&result):ENOMEM;
    SelectInput(previous);Close(reader);if(output)Close(output);
    bug("Native CLI pipeline receiver status=%ld dos=%ld error=%d\n",result.status,result.dos_error,error);
    BPTR receiver_log=Open((CONST_STRPTR)"RAM:openzfs-pipeline-receive-errors",MODE_OLDFILE);
    if(receiver_log){char text[2048];LONG got=Read(receiver_log,text,sizeof(text)-1);Close(receiver_log);
        if(got>=0){text[got]=0;bug("Native CLI pipeline receiver diagnostic: %s\n",text);}}
    WaitPort(port);struct Message *message=GetMsg(port);
    if(message!=&done.message)error=EIO;
    bug("Native CLI pipeline sender status=%ld\n",done.status);
    const char *logs[]={"RAM:openzfs-pipeline-send-errors","RAM:openzfs-pipeline-receive-errors"};
    for(unsigned i=0;i<2;i++) {
        BPTR log=Open((CONST_STRPTR)logs[i],MODE_OLDFILE);
        if(log){char text[2048];LONG got=Read(log,text,sizeof(text)-1);Close(log);
            if(got>=0){text[got]=0;bug("Native CLI pipeline %s: %s\n",logs[i],text);}}
    }
    DeleteMsgPort(port);
    return error||result.status||result.dos_error||done.status;
}
static int command_worker_test(ULONG operation,const char *target,const char *action,const char *options,ULONG flags,ULONG status,const char *needle)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=flags;
    snprintf(m->target,sizeof(m->target),"%s",target);
    snprintf(m->argument,sizeof(m->argument),"%s",action);
    snprintf(m->options,sizeof(m->options),"%s",options);
    struct oz_project_worker *worker=NULL;
    int error=oz_project_worker_start(m,&worker);
    if(!error){
        do{error=oz_project_worker_poll(worker,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){
            bug("Project worker action=%s options=%s status=%lu: %s\n",action,options,(unsigned long)out->status,out->detail);
            error=out->status!=status||(needle&&!strstr(out->detail,needle));
            if(!error&&needle&&!strcmp(needle,"Full output: ")){
                char path[128];const char *start=strstr(out->detail,needle)+strlen(needle);
                size_t length=strcspn(start,"\n");
                if(!length||length>=sizeof(path))error=EINVAL;
                else {
                    memcpy(path,start,length);path[length]=0;
                    BPTR report=Open((CONST_STRPTR)path,MODE_OLDFILE);
                    char **lines=NULL;int count=0;
                    error=report?oz_cli_read_lines(report,262144,&lines,&count):EIO;
                    for(unsigned n=0;!error&&n<128;n++){
                        char expected[64];snprintf(expected,sizeof(expected),"+\tOZDIFF:/bulk-%03u",n);
                        int found=0;for(int i=0;i<count;i++)if(!strcmp(lines[i],expected))found++;
                        if(found!=1)error=EIO;
                    }
                    oz_cli_free_lines(lines,count);
                    if(report&&!Close(report)&&!error)error=EIO;
                    if(!DeleteFile((CONST_STRPTR)path)&&!error)error=EIO;
                }
            }
            int released=oz_project_worker_free(worker);if(!error)error=released;
        }
    }
    free(m);free(out);return error;
}
static int project_worker_test(const char *target,const char *action,const char *options,ULONG flags,ULONG status,const char *needle)
{
    return command_worker_test(70,target,action,options,flags,status,needle);
}
static int live_iostat_rates(void)
{
    struct oz_rate_history *history=calloc(1,sizeof(*history));
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    if(!history||!m){free(history);free(m);return 1;}
    if(!oz_client_open(&client)){free(history);free(m);return 1;}
    int error=0;unsigned rows=0,positive=0;UQUAD writes[8]={0};
    oz_rate_reset(history);
    for(unsigned pass=0;pass<2&&!error;pass++){
        memset(m,0,sizeof(*m));m->operation=35;strcpy(m->target,"qemu_mirror");
        error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error){
            error=out->status!=OZ_OK||out->row_count!=8||out->next_cursor;
            for(unsigned i=0;i<out->row_count&&!error;i++){
                char text[256];oz_rate_format(history,m->target,&out->rows[i],text,sizeof(text));
                bug("Live I/O sample %u %s %s: %s\n",pass,out->rows[i].kind,out->rows[i].name,text);
                if(!strstr(text,pass?"interval read=":"baseline"))error=1;
                if(!pass)writes[i]=out->rows[i].available;
                else if(!strcmp(out->rows[i].kind,"io-bytes")&&
                    !strncmp(out->rows[i].name,"/AROS/",6)&&out->rows[i].available>writes[i])positive++;
                if(pass)rows++;
            }
        }
        if(out){oz_message_free(out);out=NULL;}Delay(2);
        if(!pass&&!error){
            char *create[]={"C:zfs","create","-u","qemu_mirror/rate-sample",NULL};
            char *sync[]={"C:zpool","sync","qemu_mirror",NULL};
            error=run(create,0,NULL)||run(sync,0,NULL);
        }
    }
    if(!error){
        char *destroy[]={"C:zfs","destroy","qemu_mirror/rate-sample",NULL};
        error=positive!=2||run(destroy,0,NULL);
    }
    if(!error)bug("RESULT native live I/O rates PASS rows=%u generation=1 paired-samples=1 positive-write-leaves=%u\n",rows,positive);
    oz_client_close(&client);free(history);free(m);return error;
}
static int live_histograms(int sizes)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    int error=0;unsigned rows=0,pages=0;UQUAD positive=0;
    unsigned buckets=sizes?25:37,metrics=sizes?14:11;
    m->operation=35;strcpy(m->target,"qemu_mirror");strcpy(m->options,sizes?"histogram=size":"histogram=latency");
    do {
        error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error) {
            error=out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS;
            for(unsigned i=0;i<out->row_count&&!error;i++) {
                struct oz_row *r=&out->rows[i];unsigned bucket=rows%buckets;
                UQUAD low=bucket?1ULL<<bucket:0;
                UQUAD high=bucket+1==buckets?~(UQUAD)0:(1ULL<<(bucket+1))-1;
                char wanted[32];snprintf(wanted,sizeof(wanted)," bucket=%u unit=%s",bucket,sizes?"bytes":"ns");
                error=strcmp(r->kind,sizes?"io-size":"io-latency")||!strcmp(r->state,"UNAVAILABLE")||
                    !strstr(r->value,wanted)||!strstr(r->value,"counts=cumulative")||
                    r->available!=low||r->total!=high;
                if(!strncmp(r->name,"/AROS/",6)&&r->used)positive++;
                rows++;
            }
            if(out->next_cursor&&out->next_cursor!=rows)error=1;
            m->cursor=out->next_cursor;
        }
        if(out){oz_message_free(out);out=NULL;}
        if(++pages>128)error=1;
    }while(!error&&m->cursor);
    if(!error)error=rows!=4*metrics*buckets||!positive||pages<2;
    if(!error) {
        m->cursor=0;strcpy(m->options,"histogram=invalid");
        error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error)error=out->status!=OZ_UNSUPPORTED||out->row_count||out->next_cursor;
        if(out)oz_message_free(out);
    }
    bug("RESULT native %s histograms %s rows=%u pages=%u positive-leaf-buckets=%llu\n",
        sizes?"size":"latency",error?"FAIL":"PASS",rows,pages,(unsigned long long)positive);
    oz_client_close(&client);free(m);return error;
}
static int gui_property(void)
{
    struct oz_client client;
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->operation=40;
    strcpy(m->target,"qemu_mirror/cli-files");
    strcpy(m->argument,"test:origin");
    int error=oz_submit(&client,m)!=OZ_OK;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error)error=out->status!=OZ_OK||out->row_count!=1||
        strcmp(out->rows[0].name,"test:origin")||
        strcmp(out->rows[0].value,"upstream-cli");
    free(m);if(out)oz_message_free(out);oz_client_close(&client);
    return error;
}
static int gui_version(void)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    unsigned supported=0,advertised=0;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->operation=1;int error=oz_submit(&client,m)!=OZ_OK;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    char expected[32];snprintf(expected,sizeof(expected),"api.version=%u\n",(unsigned)OZ_VERSION);
    if(!error)error=out->status!=OZ_OK||out->version!=OZ_VERSION||!strstr(out->detail,expected);
    if(!error) {
        for(unsigned id=0;id<128;id++)
            if(out->capability_words[id/32]&(1UL<<(id%32)))advertised++;
        for(unsigned i=0;i<oz_operation_count;i++) {
            unsigned id=oz_operations[i].id;
            int available=oz_operation_unavailable(id)==NULL;
            int bit=id<128&&(out->capability_words[id/32]&(1UL<<(id%32)))!=0;
            if(available)supported++;
            if(available!=bit||(available&&!strstr(out->detail,oz_operations[i].key)))error=1;
        }
        if(advertised!=supported)error=1;
    }
    if(out){oz_message_free(out);out=NULL;}
    if(!error){
        m->version=OZ_VERSION-1;
        error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error)error=out->status!=OZ_BAD_VERSION;
    }
    if(!error) {
        bug("RESULT native management API version PASS wire=%u advertised=%u old-version-rejected=1\n",(unsigned)OZ_VERSION,(unsigned)OZ_VERSION);
        bug("RESULT native management capability catalogue PASS supported=%u unavailable=%u exact=1\n",supported,oz_operation_count-supported);
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);return error;
}
static const char *expected_data="CLI data";
static int dos_file(void)
{
    int error=0;
    BPTR file=Open((CONST_STRPTR)"OZCLI:caf\xc3\xa9",MODE_NEWFILE);
    if(!file)error=1;
    else {if(Write(file,expected_data,8)!=8||!Flush(file))error=1;if(!Close(file))error=1;}
    if(!error) {
        file=Open((CONST_STRPTR)"OZCLI:CAFE\xcc\x81",MODE_OLDFILE);
        if(!file)error=1;
        else {char bytes[9]={0};if(Read(file,bytes,sizeof(bytes))!=8||memcmp(bytes,expected_data,8))error=1;if(!Close(file))error=1;}
    }
    return error;
}
static int readonly_file(void)
{
    BPTR file=Open((CONST_STRPTR)"OZCLI:CAFE\xcc\x81",MODE_OLDFILE);
    if(!file)return 1;
    char bytes[9]={0};
    int error=Read(file,bytes,sizeof(bytes))!=8||memcmp(bytes,expected_data,8);
    if(!Close(file))error=1;
    file=Open((CONST_STRPTR)"OZCLI:denied-import",MODE_NEWFILE);
    LONG denied=IoErr();
    if(file){Close(file);return 1;}
    return error||(denied!=ERROR_DISK_WRITE_PROTECTED&&denied!=ERROR_WRITE_PROTECTED);
}
/* Reimport stays on the already validated native management path. This test
 * proves CLI export, not yet CLI discovery/import. */
static int reimport(void)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=7;m->flags=OZ_CONFIRMED;
    strcpy(m->target,"qemu_mirror");strcpy(m->options,"readonly=off");
    m->topology.device_count=2;
    for(unsigned i=0;i<2;i++)snprintf(m->topology.devices[i].path,256,
        "/AROS/" OZ_TEST_PRIMARY_DEVICE "/%u/partition/0",i);
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){bug("CLI test reimport: %s\n",out->detail);error=out->status!=OZ_OK;}
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        if(!error)error=released;
    }
    free(m);free(out);
    return error;
}
#ifdef OZ_TEST_NATIVE_LABELCLEAR
struct native_label_snapshot { unsigned char pads[4][16*1024]; };

static int native_label_snapshot(unsigned unit,struct native_label_snapshot *saved,int after)
{
    struct oz_ticket session={0};struct oz_device_geometry geometry;
    struct oz_partition_catalog catalog;LONG descriptor=-1;
    unsigned char *label=malloc(256*1024);int error=!label;
    if(!error)error=oz_session_open(OZ_ADMISSION_PORT,&session);
    if(!error)error=oz_device_handle_open(OZ_ADMISSION_PORT,&session,
        "ata.device",unit,&descriptor,&geometry);
    if(!error)error=oz_partitions_read(OZ_ADMISSION_PORT,"ata.device",unit,&catalog);
    const struct oz_partition_entry *part=NULL;
    for(uint32_t i=0;!error&&i<catalog.count;i++)if(catalog.entries[i].position==0) {
        if(part)error=EIO;else part=&catalog.entries[i];
    }
    if(!error&&!part)error=ENOENT;
    uint64_t base=0,size=0;
    if(!error) {
        if(catalog.sector_size!=geometry.sector_size||
           catalog.disk_sectors!=geometry.bytes/geometry.sector_size||
           part->first_sector>part->last_sector)error=EIO;
        else {
            base=part->first_sector*catalog.sector_size;
            size=(part->last_sector-part->first_sector+1)*catalog.sector_size;
            size-=size%(256*1024);
            if(size<5*1024*1024)error=ENOSPC;
        }
    }
    for(unsigned i=0;!error&&i<4;i++) {
        uint64_t offset=i<2?i*(256*1024):size-(4-i)*(256*1024);
        error=oz_device_handle_read(OZ_ADMISSION_PORT,&session,descriptor,
            base+offset,label,256*1024);
        if(error)break;
        if(after) {
            if(memcmp(label,saved->pads[i],16*1024))error=EIO;
            for(unsigned n=16*1024;!error&&n<256*1024;n++)if(label[n])error=EIO;
        } else {
            memcpy(saved->pads[i],label,16*1024);
            unsigned nonzero=0;
            for(unsigned n=16*1024;n<256*1024;n++)nonzero|=label[n];
            if(!nonzero)error=ENOENT;
        }
    }
    if(descriptor>=0){int closed=oz_control_close(OZ_ADMISSION_PORT,&session,descriptor);if(!error)error=closed;}
    if(session.number){int closed=oz_session_close(OZ_ADMISSION_PORT,&session);if(!error)error=closed;}
    free(label);return error;
}

static int manager_native_labelclear(const char *path,ULONG flags,ULONG status,
    const char *detail)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=29;m->flags=flags;strcpy(m->target,path);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        bug("Manager native labelclear path=%s status=%lu detail=%s\n",path,
            (unsigned long)out->status,out->detail);
        if(!error&&(out->status!=status||(detail&&!strstr(out->detail,detail))))error=EIO;
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
    }
    free(m);free(out);return error;
}

static int native_labelclear_test(void)
{
    const char *first=OZ_TEST_PRIMARY0;
    const char *second=OZ_TEST_PRIMARY1;
    struct native_label_snapshot *before=calloc(2,sizeof(*before));
    int error=!before;
    if(!error)error=run((char *const []){"C:zpool","list","-H","qemu_mirror",NULL},0,"qemu_mirror");
    if(!error)error=manager_native_labelclear(first,OZ_CONFIRMED|OZ_FORCE,OZ_ENGINE_ERROR,"errno=16");
    if(!error)error=run((char *const []){"C:zpool","export","qemu_mirror",NULL},0,NULL);
    if(!error)error=run((char *const []){"C:zpool","list","-H","qemu_mirror",NULL},1,NULL);
    if(!error&&(manager_native_labelclear(first,OZ_CONFIRMED,OZ_ENGINE_ERROR,"exported pool membership requires Force")||
       native_label_snapshot(0,&before[0],0)||native_label_snapshot(1,&before[1],0)||
       manager_native_labelclear(first,OZ_CONFIRMED|OZ_FORCE,OZ_OK,"Cleared and verified 4")||
       manager_native_labelclear(second,OZ_CONFIRMED|OZ_FORCE,OZ_OK,"Cleared and verified 4")||
       native_label_snapshot(0,&before[0],1)||native_label_snapshot(1,&before[1],1)||
       run((char *const []){"C:zpool","import","-d",(char *)first,"-d",(char *)second,NULL},0,"no pools available")))error=1;
    free(before);
    if(!error)bug("RESULT native raw labelclear PASS active-refused=1 exported-force=1 labels=8 pads-preserved=8 regions-zero=8 import-absent=1\n");
    return error;
}
#endif
static char last_command_output[16384];
#ifdef OZ_TEST_SPLIT_RETAINED
static int manager_pool_busy(unsigned operation)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=OZ_CONFIRMED;
    strcpy(m->target,"cli_split_source");
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){
            char detail[32];snprintf(detail,sizeof(detail),"errno=%d",EBUSY);
            bug("Manager retained-source operation=%u status=%d: %s\n",operation,out->status,out->detail);
            error=out->status!=OZ_ENGINE_ERROR||!strstr(out->detail,detail);
        }
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        if(!error)error=released;
    }
    free(m);free(out);return error;
}
#endif
static int manager_rename_flags(const char *source,const char *dest,int expected,ULONG flags)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=39;m->flags=OZ_CONFIRMED|flags;
    strcpy(m->target,source);strcpy(m->argument,dest);
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){
            char detail[80];snprintf(detail,sizeof(detail),"Authenticated dataset rename returned errno=%d",expected);
            bug("Manager rename status=%d: %s\n",out->status,out->detail);
            error=out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||strcmp(out->detail,detail);
        }
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        if(!error)error=released;
    }
    free(m);free(out);return error;
}
static int manager_rename(const char *source,const char *dest,int expected)
{return manager_rename_flags(source,dest,expected,0);}
#ifdef OZ_TEST_NATIVE_REPATH_RAM
static int repath_ram_ready;
static int repath_copy_cli(const char *source,const char *dest)
{
    BPTR from=Open((CONST_STRPTR)source,MODE_OLDFILE);
    BPTR to=Open((CONST_STRPTR)dest,MODE_NEWFILE);
    unsigned char *a=malloc(65536),*b=malloc(65536);int error=!from||!to||!a||!b;
    LONG got=0;
    while(!error&&(got=Read(from,a,65536))>0)if(Write(to,a,got)!=got)error=1;
    if(got<0)error=1;
    if(to){if(!Flush(to))error=1;if(!Close(to))error=1;to=0;}
    if(from){Close(from);from=0;}
    if(!error){from=Open((CONST_STRPTR)source,MODE_OLDFILE);to=Open((CONST_STRPTR)dest,MODE_OLDFILE);error=!from||!to;}
    while(!error&&(got=Read(from,a,65536))>0)
        if(Read(to,b,got)!=got||memcmp(a,b,got))error=1;
    if(got<0||(!error&&Read(to,b,1)!=0))error=1;
    if(from)Close(from);if(to)Close(to);free(a);free(b);
    bug("Native repath CLI copy source=%s destination=%s byte-compare=%d\n",source,dest,!error);
    return error;
}
#endif
static int run(char *const argv[],long expected,const char *needle)
{
    if(trace_run)bug("Zvol cleanup command opening RAM output\n");
    BPTR out=Open((CONST_STRPTR)"RAM:service-command-output",MODE_NEWFILE);
    if(trace_run)bug("Zvol cleanup command RAM output=%lx\n",(unsigned long)out);
    if(!out)return 1;
    struct oz_cli_result result={0};
    const char *executable=argv[0];
#ifdef OZ_TEST_NATIVE_REPATH_RAM
    if(repath_ram_ready) {
        if(!strcmp(executable,"C:zpool"))executable="RAM:repath-zpool";
        if(!strcmp(executable,"C:zfs"))executable="RAM:repath-zfs";
        bug("Native repath executable=%s\n",executable);
    }
#endif
    int error=oz_cli_spawn_exact(executable,argv,out,out,&result);
    if(trace_run)bug("Zvol cleanup command spawn returned=%d\n",error);
    Close(out);
    out=Open((CONST_STRPTR)"RAM:service-command-output",MODE_OLDFILE);
    char buffer[16384];LONG bytes=out?Read(out,buffer,sizeof(buffer)-1):-1;
    if(out)Close(out);
    if(bytes<0)bytes=0;
    buffer[bytes]=0;
    memcpy(last_command_output,buffer,(size_t)bytes+1);
    for(unsigned i=0;argv[i];i++)bug("Service CLI argv[%u]=%s\n",i,argv[i]);
    bug("Service CLI transport=%d status=%ld dos=%ld bytes=%ld\n%s\n",
        error,result.status,result.dos_error,(long)bytes,buffer);
    DeleteFile((CONST_STRPTR)"RAM:service-command-output");
    return error||result.status!=expected||(needle&&!strstr(buffer,needle));
}
#define RUN(expected,needle,...) do { char *args[]={__VA_ARGS__,NULL}; \
    if(run(args,expected,needle)){bug("RESULT native service CLI FAIL line=%d\n",__LINE__);return 20;} } while(0)

#ifdef OZ_TEST_MOUNT_REGISTRY_ALLOC
static int mount_registry_allocation_test(void)
{
    static char *datasets[]={
        "qemu_mirror/mount-registry-calloc",
        "qemu_mirror/mount-registry-realloc"
    };
    static char *volumes[]={"OZMEMCALLOC","OZMEMREALLOC"};
    static char *sentinels[]={
        "RAM:openzfs-mount-registry-calloc-failure",
        "RAM:openzfs-mount-registry-realloc-failure"
    };
    for(unsigned i=0;i<2;i++) {
        RUN(0,NULL,"C:zfs","create","-u","-o",i?"mountpoint=OZMEMREALLOC:":"mountpoint=OZMEMCALLOC:",datasets[i]);
        BPTR sentinel=Open((CONST_STRPTR)sentinels[i],MODE_NEWFILE);
        if(!sentinel||!Close(sentinel))return 1;
        RUN(1,NULL,"C:zfs","mount",datasets[i]);
        if(!DeleteFile((CONST_STRPTR)sentinels[i]))return 1;
        struct DosList *list=LockDosList(LDF_ALL|LDF_READ);
        int published=FindDosEntry(list,(CONST_STRPTR)volumes[i],LDF_ALL)!=NULL;
        UnLockDosList(LDF_ALL|LDF_READ);
        if(published)return 1;
        RUN(0,"no","C:zfs","get","-H","-o","value","mounted",datasets[i]);
        RUN(0,NULL,"C:zfs","mount",datasets[i]);
        char path[32];snprintf(path,sizeof(path),"%s:",volumes[i]);
        struct DevProc *mounted=GetDeviceProc((CONST_STRPTR)path,NULL);
        if(!mounted)return 1;
        FreeDeviceProc(mounted);
        RUN(0,NULL,"C:zfs","unmount",datasets[i]);
        RUN(0,NULL,"C:zfs","destroy",datasets[i]);
    }
    bug("RESULT native mount registry allocation recovery PASS calloc=1 realloc=1 unpublished=2 retry=2 cleanup=2\n");
    return 0;
}
#endif

#ifdef OZ_TEST_ERROR_LOG
int oz_error_log_wire_test(unsigned long long,int);
int oz_error_log_denied_test(void);
static int error_log_test(void)
{
    const char *dataset="qemu_mirror/error-log";
#ifdef OZ_TEST_ERROR_LOG_HEAD
    RUN(0,NULL,"C:zpool","set","feature@head_errlog=enabled","qemu_mirror");
    RUN(0,"active","C:zpool","get","-H","-o","value","feature@head_errlog","qemu_mirror");
#endif
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZERR:",
        "-o","recordsize=4096","-o","compression=off",dataset);
    RUN(0,NULL,"C:zfs","mount",dataset);
    BPTR file=Open((CONST_STRPTR)"OZERR:evidence",MODE_NEWFILE);
    if(!file)return 20;
    char data[8192];memset(data,'E',sizeof(data));
    int good=Write(file,data,sizeof(data))==sizeof(data)&&Flush(file);
    if(!Close(file)||!good)return 20;
    RUN(0,NULL,"C:zfs","get","-H","-o","value","objsetid",dataset);
    char *end=NULL;unsigned long long dsobj=strtoull(last_command_output,&end,10);
    if(!dsobj||!end||strcmp(end,"\n"))return 20;
    int seeded=0;
#ifdef OZ_TEST_ERROR_LOG_SEEDED
    seeded=1;
#endif
    if(oz_error_log_wire_test(dsobj,seeded))return 20;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);
    int denied=secLoginA(reader)==((77UL<<16)|88)?oz_error_log_denied_test():EACCES;
    secLogoutA(quiet);int restored=secLoginA(root)==secOWNER_SYSTEM;
    CloseLibrary(secBase);
    if(denied||!restored)return 20;
    RUN(0,seeded?"evidence":"No known data errors","C:zpool","status","-v","qemu_mirror");
    if(command_worker_test(82,"qemu_mirror","","",0,OZ_OK,
        seeded?"evidence":"No known data errors")||
        command_worker_test(82,"qemu_mirror","","",OZ_FORCE,OZ_ENGINE_ERROR,"before command execution")||
        command_worker_test(82,"qemu_mirror","","clear=on",0,OZ_ENGINE_ERROR,"before command execution"))return 20;
    bug("RESULT native Manager error details PASS seeded=%d upstream-output=1 force-denied=1 options-denied=1\n",seeded);
#ifdef OZ_TEST_ERROR_LOG_GUI
    bug("Native error log GUI READY: Pools/error details target=qemu_mirror; all flags off; read affected-file report, then close Manager\n");
    if(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE))return 20;
    RUN(0,seeded?"evidence":"No known data errors","C:zpool","status","-v","qemu_mirror");
    bug("RESULT native Manager error GUI lifecycle PASS seeded=%d actual-manager=1 unchanged-log=1 visual-verification=separate\n",seeded);
#endif
#ifdef OZ_TEST_ERROR_LOG_HEAD
    /* Track the same block through snapshot/clone sharing, then COW.  Error
     * records remain synthetic; all dataset and block identities are real. */
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/error-log@affected");
    RUN(0,NULL,"C:zfs","clone","-o","mountpoint=none","-o","canmount=off",
        "qemu_mirror/error-log@affected","qemu_mirror/error-log-clone");
    RUN(0,"qemu_mirror/error-log@affected:/evidence","C:zpool","status","-v","qemu_mirror");
    if(!strstr(last_command_output,"qemu_mirror/error-log-clone:/evidence")||
        !strstr(last_command_output,"OZERR:/evidence"))return 20;
    file=Open((CONST_STRPTR)"OZERR:evidence",MODE_READWRITE);
    if(!file)return 20;
    memset(data,'R',sizeof(data));
    good=Write(file,data,sizeof(data))==sizeof(data)&&Flush(file);
    if(!Close(file)||!good)return 20;
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    RUN(0,"qemu_mirror/error-log@affected:/evidence","C:zpool","status","-v","qemu_mirror");
    if(!strstr(last_command_output,"qemu_mirror/error-log-clone:/evidence")||
        strstr(last_command_output,"OZERR:/evidence"))return 20;
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/error-log-clone");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/error-log@affected");
    bug("RESULT native head error attribution PASS snapshot=1 clone=1 rewritten-head-excluded=1\n");
#endif
    if(seeded) {
        RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
        RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
        RUN(0,"No known data errors","C:zpool","status","-v","qemu_mirror");
    }
    RUN(0,NULL,"C:zfs","unmount",dataset);
    RUN(0,NULL,"C:zfs","destroy",dataset);
    bug("RESULT native CLI error log PASS seeded=%d status-path=1 cleanup=1\n",seeded);
#ifdef OZ_TEST_ERROR_LOG_HEAD
    bug("RESULT native head error log PASS active=1 birth-aware=1 scrub-cleared=1\n");
#endif
    return 0;
}
#endif
#ifdef OZ_TEST_RECEIVED_PROPERTIES
int oz_received_wire_test(const char *,const char *);
int oz_received_large_wire_test(const char *);
#ifdef OZ_TEST_RECEIVED_EXPANSION
static int received_expansion_test(void)
{
    const char *source="qemu_mirror/cli-props-large-source";
    const char *target="qemu_mirror/cli-props-large-received";
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",source);
    /* 96 real user values exceed libzfs's initial 256 KiB destination.
     * Batches stay below the native command argument limit. */
    for(unsigned first=0;first<96;first+=8) {
        char values[8][3120];
        char *args[12]={"C:zfs","set"};
        for(unsigned i=0;i<8;i++) {
            int prefix=snprintf(values[i],sizeof(values[i]),"org.aros:large%03u=",first+i);
            memset(values[i]+prefix,'a'+(first+i)%26,3072);
            values[i][prefix+3072]=0;args[2+i]=values[i];
        }
        args[10]=(char *)source;args[11]=NULL;
        if(run(args,0,NULL))return 20;
    }
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-props-large-source@base");
    char *send[]={"C:zfs","send","-p","qemu_mirror/cli-props-large-source@base",NULL};
    char *receive[]={"C:zfs","receive","-u",(char *)target,NULL};
    if(send_to_file(send,"RAM:received-large.stream")||
        receive_from_file(receive,"RAM:received-large.stream")||
        oz_received_large_wire_test(target))return 20;
    /* A single property's CLI lookup still fetches the entire received tree.
     * Check both ends byte-for-byte, not just an exit status or prefix. */
    for(unsigned i=0;i<2;i++) {
        RUN(0,NULL,"C:zfs","get","-H","-o","received",
            i?"org.aros:large095":"org.aros:large000",target);
        unsigned n=0;while(last_command_output[n]==(i?'r':'a'))n++;
        if(n!=3072||strcmp(last_command_output+n,"\n"))return 20;
    }
    RUN(0,NULL,"C:zfs","destroy","-r",target);
    RUN(0,NULL,"C:zfs","destroy","-r",source);
    if(!DeleteFile((CONST_STRPTR)"RAM:received-large.stream"))return 20;
    bug("RESULT native CLI received expansion PASS properties=96 value-bytes=3072 initial-capacity=262144 exact-readbacks=2 cleanup=1\n");
    return 0;
}
#endif
static int manager_received_restore(const char *target,const char *property,
    const char *options,ULONG flags,int success)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=42;m->flags=flags;
    strcpy(m->target,target);strcpy(m->argument,property);strcpy(m->options,options);
    struct oz_ticket ticket={0};
    int rc=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket),accepted=!rc;
    if(accepted) {
        do {rc=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(rc==EAGAIN)Delay(1);}while(rc==EAGAIN);
        bug("Manager received restore property=%s options=%s flags=%lu rc=%d status=%lu detail=%s\n",
            property,options,(unsigned long)flags,rc,(unsigned long)out->status,out->detail);
    } else bug("Manager received restore admission property=%s options=%s flags=%lu rc=%d\n",
        property,options,(unsigned long)flags,rc);
    int passed=success?(!rc&&out->status==OZ_OK):(rc||out->status!=OZ_OK);
    if(accepted&&oz_admission_release(OZ_ADMISSION_PORT,&ticket))passed=0;
    free(m);free(out);return !passed;
}
static int received_properties_test(void)
{
    const char *source="qemu_mirror/cli-props-source";
    const char *target="qemu_mirror/cli-props-received";
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",
        "-o","compression=gzip","-o","org.aros:received=sender",source);
    RUN(0,"gzip\t-","C:zfs","get","-H","-o","value,received","compression",source);
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-props-source@base");
    char *send[]={"C:zfs","send","-p","qemu_mirror/cli-props-source@base",NULL};
    char *receive[]={"C:zfs","receive","-u",(char *)target,NULL};
    if(send_to_file(send,"RAM:received-props.stream") ||
        receive_from_file(receive,"RAM:received-props.stream"))return 20;
    RUN(0,"gzip\tgzip","C:zfs","get","-H","-o","value,received","compression",target);
    RUN(0,"sender\tsender","C:zfs","get","-H","-o","value,received","org.aros:received",target);
    if(oz_received_wire_test(target,source))return 20;
    RUN(0,NULL,"C:zfs","set","compression=off","org.aros:received=local",target);
    RUN(0,"off\tgzip","C:zfs","get","-H","-o","value,received","compression",target);
    RUN(0,"local\tsender","C:zfs","get","-H","-o","value,received","org.aros:received",target);
    RUN(0,NULL,"C:zfs","inherit","-S","compression",target);
    RUN(0,NULL,"C:zfs","inherit","-S","org.aros:received",target);
    RUN(0,"gzip\treceived\tgzip","C:zfs","get","-H","-o","value,source,received","compression",target);
    RUN(0,"sender\treceived\tsender","C:zfs","get","-H","-o","value,source,received","org.aros:received",target);
    RUN(0,NULL,"C:zfs","set","compression=off","org.aros:received=local",target);
    if(manager_received_restore(target,"compression","received=on",0,0)||
        manager_received_restore(target,"compression","received=wrong",OZ_CONFIRMED,0)||
        manager_received_restore(target,"compression","received=on",OZ_CONFIRMED|OZ_FORCE,0))return 20;
    RUN(0,"off\tgzip","C:zfs","get","-H","-o","value,received","compression",target);
    if(manager_received_restore(target,"compression","received=on",OZ_CONFIRMED,1)||
        manager_received_restore(target,"org.aros:received","received=on",OZ_CONFIRMED,1))return 20;
    RUN(0,"gzip\treceived\tgzip","C:zfs","get","-H","-o","value,source,received","compression",target);
    RUN(0,"sender\treceived\tsender","C:zfs","get","-H","-o","value,source,received","org.aros:received",target);
    bug("RESULT native Manager received restore PASS native=1 user=1 unconfirmed=1 bad-option=1 force-denied=1 CLI-readback=1\n");
#ifdef OZ_TEST_RECEIVED_GUI
    RUN(0,NULL,"C:zfs","set","compression=off","org.aros:received=local",target);
    bug("Native received GUI READY: Datasets/inherit target=qemu_mirror/cli-props-received options=received=on; restore compression and org.aros:received, disable dry-run, confirm each, then close Manager\n");
    if(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE))return 20;
    RUN(0,"gzip\treceived\tgzip","C:zfs","get","-H","-o","value,source,received","compression",target);
    RUN(0,"sender\treceived\tsender","C:zfs","get","-H","-o","value,source,received","org.aros:received",target);
    bug("RESULT native Manager received GUI PASS actual-manager=1 native=1 user=1 CLI-readback=2\n");
#endif
    RUN(0,NULL,"C:zfs","destroy","-r",target);
    RUN(0,NULL,"C:zfs","destroy","-r",source);
    if(!DeleteFile((CONST_STRPTR)"RAM:received-props.stream"))return 20;
    bug("RESULT native CLI received properties PASS stream-props=1 native=1 user=1 local-override=1 inherit-received=1\n");
#ifdef OZ_TEST_RECEIVED_EXPANSION
    if(received_expansion_test())return 20;
#endif
    return 0;
}
#endif
#ifdef OZ_TEST_NATIVE_REPATH
int oz_test_repath_copy(unsigned,unsigned);
static int native_repath(void)
{
#ifdef OZ_TEST_NATIVE_REPATH_RAM
    if(repath_copy_cli("C:zpool","RAM:repath-zpool")||repath_copy_cli("C:zfs","RAM:repath-zfs"))return 20;
    repath_ram_ready=1;
#endif
    const char *old=OZ_TEST_PRIMARY0;
    const char *moved="/AROS/ata.device/3/partition/0";
    const char *assignment="path=/AROS/ata.device/3/partition/0";
    RUN(0,NULL,"C:zpool","get","-H","-o","value","guid","qemu_mirror",old);
    char guid[32];size_t n=strcspn(last_command_output,"\r\n");
    if(!n||n>=sizeof(guid)||strspn(last_command_output,"0123456789")!=n)return 20;
    memcpy(guid,last_command_output,n);guid[n]=0;
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:","-o","normalization=formC",
        "-o","utf8only=on","-o","casesensitivity=insensitive","qemu_mirror/repath");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/repath");
    if(dos_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/repath");
    RUN(0,NULL,"C:zpool","set",OZ_TEST_PRIMARY_ASSIGN0,"qemu_mirror",guid);
    if(command_worker_test(81,"qemu_mirror",guid,OZ_TEST_PRIMARY_ASSIGN0,
        OZ_CONFIRMED,OZ_OK,"exit=0")||
        command_worker_test(81,"qemu_mirror",guid,assignment,0,OZ_ENGINE_ERROR,"before command execution"))return 20;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    RUN(1,"permission denied","C:zpool","set",assignment,"qemu_mirror",guid);
    if(command_worker_test(81,"qemu_mirror",guid,assignment,OZ_CONFIRMED,
        OZ_ENGINE_ERROR,"permission denied"))return 20;
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(1,NULL,"C:zpool","set",assignment,"qemu_mirror",guid); /* Online leaf. */
    if(command_worker_test(81,"qemu_mirror",guid,assignment,OZ_CONFIRMED,
        OZ_ENGINE_ERROR,NULL))return 20;
    RUN(0,old,"C:zpool","get","-H","-o","value","path","qemu_mirror",guid);
    bug("RESULT native Manager path guards PASS unchanged-path=1 unconfirmed=1 unauthorized=1 online-denied=1 readback=1\n");
    RUN(0,NULL,"C:zpool","offline","qemu_mirror",guid);
    RUN(1,NULL,"C:zpool","set",assignment,"qemu_mirror",guid); /* Blank target. */
    if(oz_test_repath_copy(1,3))return 20;
#ifdef OZ_TEST_NATIVE_REPATH_RAM
    BPTR cd_probe=LoadSeg((CONST_STRPTR)"C:zpool");LONG cd_error=IoErr();
    bug("Native repath CD launch probe loaded=%d dos=%ld\n",cd_probe!=0,(long)cd_error);
    if(cd_probe)UnLoadSeg(cd_probe);
#endif
    RUN(1,"backing device identity changed or does not match","C:zpool","set",assignment,"qemu_mirror",guid);
    RUN(0,old,"C:zpool","get","-H","-o","value","path","qemu_mirror",guid);
    if(oz_test_repath_copy(0,3))return 20;
    RUN(0,NULL,"C:zpool","set",assignment,"qemu_mirror",guid);
    RUN(0,moved,"C:zpool","get","-H","-o","value","path","qemu_mirror",guid);
    RUN(0,"OFFLINE","C:zpool","status","-P","qemu_mirror");
    RUN(0,NULL,"C:zpool","online","qemu_mirror",guid);
    RUN(0,NULL,"C:zpool","wait","-t","resilver","qemu_mirror");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",moved,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(1,"read-only","C:zpool","set",OZ_TEST_PRIMARY_ASSIGN0,"qemu_mirror",guid);
    RUN(0,moved,"C:zpool","status","-P","qemu_mirror");
    if(strstr(last_command_output,old))return 20;
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/repath");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/repath");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",moved,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    expected_data="Moved IO";
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/repath");
    if(dos_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/repath");
    /* Matching label GUIDs do not establish freshness of a stale clone.
     * Copy the now-offline current member back to the unbound original test
     * extent, simulating the same up-to-date medium moving back. */
    RUN(0,NULL,"C:zpool","offline","qemu_mirror",guid);
    if(oz_test_repath_copy(3,0))return 20;
    RUN(0,NULL,"C:zpool","set",OZ_TEST_PRIMARY_ASSIGN0,"qemu_mirror",guid);
    RUN(0,NULL,"C:zpool","online","qemu_mirror",guid);
    RUN(0,NULL,"C:zpool","wait","-t","resilver","qemu_mirror");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",old,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/repath");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/repath");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/repath");
    expected_data="CLI data";
    bug("RESULT native CLI repath PASS online-denied=1 blank-denied=1 wrong-member-denied=1 moved=1 offline-preserved=1 reimport=3 DOS-read=2 restored=1\n");
    bug("RESULT native CLI repath guards PASS unauthorized=1 readonly=1 post-move-write=1 fresh-return-copy=1\n");
    return 0;
}
#endif
#ifdef OZ_TEST_ZVOL_DEVICE
#include "../device/device-test.c"
#endif
#ifdef OZ_TEST_ZVOL_TRANSPORT
#ifdef OZ_TEST_PERMISSION_COLD
static int permission_list_boundaries(void)
{
    static const struct {const char *target,*argument,*options;ULONG flags;UQUAD cursor,job;int admission,engine;} cases[]={
        {"qemu_mirror","","",0,0,0,0,0},
        {"qemu_mirror/named-permissions/first","","",0,5,0,0,0},
        {"qemu_mirror/named-permissions/first","","",0,6,0,0,EINVAL},
        {"qemu_mirror/named-permissions/first","","",0,~(UQUAD)0,0,0,EINVAL},
        {"qemu_mirror/missing-acl-fixture","","",0,0,0,0,ENOENT},
        {"qemu_mirror/named-permissions/first@owned","","",0,0,0,0,EINVAL},
        {"qemu_mirror","user:77","",0,0,0,EINVAL,0},
        {"qemu_mirror","","scope=local",0,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_CONFIRMED,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_DRYRUN,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_FORCE,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_RECURSIVE,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_RAW,0,0,EINVAL,0},
        {"qemu_mirror","","",OZ_RESUMABLE,0,0,EINVAL,0},
        {"qemu_mirror","","",0,0,1,EINVAL,0}
    };
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    int failed=0;
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0])&&!failed;i++){
        memset(m,0,sizeof(*m));memset(out,0xa5,sizeof(*out));
        m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);m->operation=79;
        strcpy(m->target,cases[i].target);strcpy(m->argument,cases[i].argument);strcpy(m->options,cases[i].options);
        m->flags=cases[i].flags;m->cursor=cases[i].cursor;m->job_id=cases[i].job;
        struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
        failed=error!=cases[i].admission;
        if(!error){
            do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
            char detail[96];snprintf(detail,sizeof(detail),"Authenticated permission list returned errno=%d",cases[i].engine);
            failed=failed||error||out->status!=(cases[i].engine?OZ_ENGINE_ERROR:OZ_OK)||strcmp(out->detail,detail)||out->row_count||out->next_cursor;
            int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);failed=failed||released;
        }
        bug("Permission boundary case=%u admission=%d failed=%d\n",i,error,failed);
    }
    free(m);free(out);
    if(!failed)bug("RESULT native permission list boundaries PASS cases=15 empty=1 end=1 invalid-cursor=2 missing=1 snapshot=1 invalid-fields=9\n");
    return failed;
}
static int permission_list_test(unsigned expected_rows)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    unsigned found=0,pages=0,rows=0;int error=0;uint32_t extra=0;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);m->operation=79;
    strcpy(m->target,"qemu_mirror/named-permissions/first");
    do {
        struct oz_ticket ticket={0};
        error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
        if(!error){
            do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
            if(!error&&out->status!=OZ_OK)error=1;
            if(!error)for(unsigned i=0;i<out->row_count;i++){
                struct oz_row *r=&out->rows[i];
                bug("Permission row %s %s %s %s\n",r->parent,r->name,r->state,r->value);
                if(strcmp(r->kind,"permission")){error=1;break;}
                rows++;
                if(!strcmp(r->parent,"qemu_mirror/named-permissions")){
                    for(unsigned n=0;n<30;n++){
                        char principal[32];snprintf(principal,sizeof(principal),"user:%u",1000+n);
                        if(!strcmp(r->name,principal)&&!strcmp(r->state,"local/direct")&&!strcmp(r->value,"mount")){
                            if(extra&(1U<<n))error=1;
                            extra|=1U<<n;
                        }
                    }
                    if(!strcmp(r->name,"set:@base")&&!strcmp(r->value,"mount")&&!strcmp(r->state,"definition/direct"))found|=1;
                    if(!strcmp(r->name,"set:@outer")&&!strcmp(r->value,"@base")&&!strcmp(r->state,"definition/set-reference"))found|=2;
                    if(!strcmp(r->name,"user:77")&&!strcmp(r->value,"@outer")&&!strcmp(r->state,"local/set-reference"))found|=4;
                    if(!strcmp(r->name,"user:77")&&!strcmp(r->value,"@outer")&&!strcmp(r->state,"descendants/set-reference"))found|=8;
                }
                if(!strcmp(r->parent,m->target)&&!strcmp(r->name,"user:77")&&!strcmp(r->value,"snapshot"))found|=16;
            }
            if(!error&&out->next_cursor&&out->next_cursor<=m->cursor)error=1;
            m->cursor=out->next_cursor;
            int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
        }
        if(++pages>16)error=1;
    }while(!error&&m->cursor);
    free(m);free(out);
    error=error||found!=31||rows!=expected_rows;
    if(expected_rows==35)error=error||pages!=2||extra!=0x3fffffffU;
    if(!error)bug("RESULT native permission list PASS structured=1 ancestor-source=1 sets=1 scopes=1 acquired-grant=1\n");
    return error;
}
/* CLI output may end directly after its final record. Match whole lines. */
static int permission_line(const char *line)
{
    size_t n=strlen(line);const char *p=last_command_output;
    while((p=strstr(p,line))!=NULL){
        if((p==last_command_output||p[-1]=='\n')&&(!p[n]||p[n]=='\n'||(p[n]=='\r'&&p[n+1]=='\n')))return 1;
        p++;
    }
    return 0;
}
#endif
#endif /* OZ_TEST_ZVOL_TRANSPORT: request helpers are also used by pool tests. */
static int manager_permission_expect(unsigned operation,const char *target,const char *argument,const char *options,int expected,ULONG flags,const char *expected_detail)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=flags;
    strcpy(m->argument,argument);
    strcpy(m->target,target);strcpy(m->options,options);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error){
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){
            char detail[40];snprintf(detail,sizeof(detail),"errno=%d",expected);
            bug("Manager permission/create op=%u target=%s options=%s status=%d: %s\n",operation,target,options,out->status,out->detail);
            const char *result=strstr(out->detail,"errno=");
            error=out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||
                (expected_detail?strcmp(out->detail,expected_detail):(!result||strcmp(result,detail)));
        }
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
    }
    free(m);free(out);return error;
}
static int manager_permission_flags(unsigned operation,const char *target,const char *argument,const char *options,int expected,ULONG flags)
{ return manager_permission_expect(operation,target,argument,options,expected,flags,
    operation==66&&(flags&OZ_RECURSIVE)&&!expected?
        "Authenticated recursive permission revoke completed=3 errno=0":NULL); }
static int manager_permission_or_create(unsigned operation,const char *target,const char *argument,const char *options,int expected)
{ return manager_permission_flags(operation,target,argument,options,expected,OZ_CONFIRMED); }
static int manager_admission_refused(unsigned operation,const char *target,const char *options,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m));if(!m)return 1;
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=OZ_CONFIRMED;
    strcpy(m->target,target);strcpy(m->options,options);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error)oz_admission_release(OZ_ADMISSION_PORT,&ticket);
    free(m);return error!=expected;
}
static int manager_hold_exists(const char *snapshot,const char *tag,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=57;strcpy(m->target,snapshot);
    int error=0,found=0;
    do {
        error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error&&out->status!=OZ_OK)error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"hold")&&
            !strcmp(out->rows[i].parent,snapshot)&&!strcmp(out->rows[i].name,tag))found=1;
        m->cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
    }while(!error&&m->cursor);
    free(m);oz_client_close(&client);return error||found!=expected;
}
static int manager_recursive_hold_rows(const char snapshots[3][128],const char *base_tag)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=57;m->flags=OZ_RECURSIVE;strcpy(m->target,snapshots[0]);
    unsigned char seen[3][9]={{0}};unsigned total=0,pages=0;UQUAD cursor=0;int error=0;
    do {
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];int dataset=-1,tag=-1;
            for(unsigned j=0;j<3;j++)if(!strcmp(r->parent,snapshots[j]))dataset=j;
            if(!strcmp(r->name,base_tag))tag=8;
            else if(strlen(r->name)==8&&!strncmp(r->name,"query-",6)&&
                r->name[6]>='0'&&r->name[6]<='9'&&r->name[7]>='0'&&r->name[7]<='9')
                tag=(r->name[6]-'0')*10+r->name[7]-'0';
            if(dataset<0||tag<0||tag>=9||seen[dataset][tag]++||
                strcmp(r->kind,"hold")||strcmp(r->state,"persistent")||
                strncmp(r->value,"created-unix=",13))error=1;
            total++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
    }while(!error&&cursor);
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<9;j++)if(seen[i][j]!=1)error=1;
    free(m);oz_client_close(&client);return error||pages!=2||total!=27;
}
static int manager_hold_cursor_case(const char *snapshot,ULONG flags,UQUAD cursor,
    ULONG expected_status)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=57;m->flags=flags;m->cursor=cursor;strcpy(m->target,snapshot);
    int error=oz_submit(&client,m)!=OZ_OK;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error) {
        bug("Snapshot hold cursor boundary recursive=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            !!(flags&OZ_RECURSIVE),(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        error=out->status!=expected_status||out->row_count||out->next_cursor||
            (expected_status==OZ_ENGINE_ERROR&&!strstr(out->detail,"errno=22"));
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);return error;
}
static int property_cursor_case(struct oz_client *client,struct oz_message *m,
    UQUAD cursor,ULONG expected_status)
{
    struct oz_message *out=NULL;m->cursor=cursor;
    int error=oz_submit(client,m)!=OZ_OK;
    if(!error)while(!(out=oz_poll(client)))Delay(1);
    if(!error) {
        bug("Property cursor boundary op=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            (unsigned)m->operation,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        error=out->status!=expected_status||out->row_count||out->next_cursor||
            (expected_status==OZ_ENGINE_ERROR&&!strstr(out->detail,"errno=22"));
    }
    if(out)oz_message_free(out);return error;
}
static int property_catalogue_cursor_probe(struct oz_client *client,struct oz_message *m,
    UQUAD cursor,int *relation)
{
    struct oz_message *out=NULL;m->cursor=cursor;
    int error=oz_submit(client,m)!=OZ_OK;
    if(!error)while(!(out=oz_poll(client)))Delay(1);
    if(!error) {
        bug("Property catalogue cursor probe op=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            (unsigned)m->operation,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        if(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS)*relation=-1;
        else if(out->status==OZ_OK&&!out->row_count&&!out->next_cursor)*relation=0;
        else if(out->status==OZ_ENGINE_ERROR&&!out->row_count&&!out->next_cursor&&
            strstr(out->detail,"errno=22"))*relation=1;
        else error=1;
    }
    if(out)oz_message_free(out);return error;
}
static int property_catalogue_end(struct oz_client *client,struct oz_message *m,
    UQUAD known_below,UQUAD *end_out)
{
    UQUAD low=known_below,high=low+1;int relation=0;
    for(unsigned probes=0;probes<64;probes++) {
        if(property_catalogue_cursor_probe(client,m,high,&relation))return 1;
        if(!relation){*end_out=high;return 0;}
        if(relation>0)break;
        low=high;
        if(high>(~(UQUAD)0-1)/2)high=~(UQUAD)0;
        else high=high*2+1;
    }
    if(relation<=0)return 1;
    while(high-low>1) {
        UQUAD middle=low+(high-low)/2;
        if(property_catalogue_cursor_probe(client,m,middle,&relation))return 1;
        if(!relation){*end_out=middle;return 0;}
        if(relation<0)low=middle;else high=middle;
    }
    return 1;
}
static int recursive_property_query_test(void)
{
#define RPCHECK(x) do{if(!(x)){bug("RESULT native recursive property query FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *root="qemu_mirror/recursive-property";char child[128];
    RUN(0,NULL,"C:zfs","set","test:recursive=base","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",root);
    for(unsigned i=0;i<26;i++) {
        snprintf(child,sizeof(child),"%s/child-%02u",root,i);
        RUN(0,NULL,"C:zfs","create","-u",child);
    }
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    RPCHECK(m&&oz_client_open(&client));
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=40;m->flags=OZ_RECURSIVE;strcpy(m->target,root);strcpy(m->argument,"mountpoint");
    unsigned char seen[27]={0};unsigned total=0,pages=0,inherited=0;UQUAD cursor=0;int error=0;
    do {
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];int index=!strcmp(r->parent,root)?0:-1;
            for(unsigned j=0;j<26&&index<0;j++) {
                snprintf(child,sizeof(child),"%s/child-%02u",root,j);
                if(!strcmp(r->parent,child))index=j+1;
            }
            if(index<0||seen[index]++||strcmp(r->kind,"property")||
                strcmp(r->name,"mountpoint")||strcmp(r->value,"none")||strcmp(r->state,root))error=1;
            if(index>0)inherited++;
            total++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
    }while(!error&&cursor);
    for(unsigned i=0;i<27;i++)if(seen[i]!=1)error=1;
    RPCHECK(!error&&pages==2&&total==27&&inherited==26);
    RPCHECK(!property_cursor_case(&client,m,27,OZ_OK));
    RPCHECK(!property_cursor_case(&client,m,28,OZ_ENGINE_ERROR));
    RPCHECK(!property_cursor_case(&client,m,~(UQUAD)0,OZ_ENGINE_ERROR));
    char assignment[400]="test:long=";memset(assignment+10,'x',300);assignment[310]=0;
    RUN(0,NULL,"C:zfs","set",assignment,root);
    memset(m,0,sizeof(*m));m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=40;m->flags=OZ_RECURSIVE;strcpy(m->target,root);strcpy(m->argument,"test:long");
    memset(seen,0,sizeof(seen));total=pages=0;cursor=0;error=0;
    do {
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];int index=!strcmp(r->parent,root)?0:-1;
            for(unsigned j=0;j<26&&index<0;j++) {
                snprintf(child,sizeof(child),"%s/child-%02u",root,j);
                if(!strcmp(r->parent,child))index=j+1;
            }
            if(index<0||seen[index]++||strcmp(r->kind,"property-long")||
                strcmp(r->name,"test:long")||strcmp(r->value,"Full value in response detail")||
                strcmp(r->state,root)||r->total!=300)error=1;
            total++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
    }while(!error&&cursor);
    for(unsigned i=0;i<27;i++)if(seen[i]!=1)error=1;
    RPCHECK(!error&&pages==2&&total==27);
    RUN(0,NULL,"C:zfs","set","test:recursive=local",root);
    for(unsigned i=0;i<26;i++) {
        snprintf(child,sizeof(child),"%s/child-%02u",root,i);
        RUN(0,NULL,"C:zfs","set","test:recursive=local",child);
    }
    RPCHECK(!manager_permission_expect(42,root,"quota","",EINVAL,OZ_CONFIRMED|OZ_RECURSIVE,
        "Authenticated recursive property inherit completed=0 errno=22; nothing changed"));
    RPCHECK(!manager_permission_expect(42,root,"test:recursive","",0,OZ_CONFIRMED|OZ_RECURSIVE,
        "Authenticated recursive property inherit completed=27 errno=0"));
    memset(m,0,sizeof(*m));m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=40;m->flags=OZ_RECURSIVE;strcpy(m->target,root);strcpy(m->argument,"test:recursive");
    memset(seen,0,sizeof(seen));total=pages=inherited=0;cursor=0;error=0;
    do {
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];int index=!strcmp(r->parent,root)?0:-1;
            for(unsigned j=0;j<26&&index<0;j++) {
                snprintf(child,sizeof(child),"%s/child-%02u",root,j);
                if(!strcmp(r->parent,child))index=j+1;
            }
            if(index<0||seen[index]++||strcmp(r->kind,"property")||
                strcmp(r->name,"test:recursive")||strcmp(r->value,"base")||strcmp(r->state,"qemu_mirror"))error=1;
            inherited++;total++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
    }while(!error&&cursor);
    for(unsigned i=0;i<27;i++)if(seen[i]!=1)error=1;
    RPCHECK(!error&&pages==2&&total==27&&inherited==27);
    free(m);oz_client_close(&client);
    char detail[128];snprintf(detail,sizeof(detail),"Authenticated recursive dataset destroy completed=27 errno=0");
    RPCHECK(!manager_permission_expect(38,root,"","",0,OZ_CONFIRMED|OZ_RECURSIVE,detail));
    RUN(1,NULL,"C:zfs","list","-H",root);
    RUN(0,NULL,"C:zfs","inherit","test:recursive","qemu_mirror");
    bug("RESULT native recursive property query PASS datasets=27 pages=2 unique=27 inherited=26 long-placeholders=27 cursor-end=1 invalid-cursor=2 errno-detail=2 cleanup=27\n");
    bug("RESULT native recursive property inherit PASS datasets=27 pages=2 inherited=27 preflight=1 cleanup=27\n");
#undef RPCHECK
    return 0;
}
static int all_property_rows(unsigned operation,const char *target,
    unsigned *rows_out,unsigned *pages_out,unsigned *features_out)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    char names[256][64];unsigned names_count=0,rows=0,pages=0,features=0;
    int user=operation==40?0:1,long_value=operation==40?0:1,guid=operation==31?0:1,error=0;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;strcpy(m->target,target);strcpy(m->argument,"all");
    UQUAD cursor=0,last_page_cursor=0,catalogue_end=0;
    do {
        UQUAD page_cursor=cursor;
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(out)bug("Property all page op=%u page=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            operation,pages,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];
            if((strcmp(r->kind,"property")&&strcmp(r->kind,"property-long"))||
                strcmp(r->parent,target)||!r->name[0]||!r->state[0]||names_count>=256)error=1;
            for(unsigned n=0;!error&&n<names_count;n++)if(!strcmp(names[n],r->name))error=1;
            if(error)break;
            strcpy(names[names_count++],r->name);
            if(!strcmp(r->name,"test:all"))user=!strcmp(r->kind,"property")&&!strcmp(r->value,"visible");
            if(!strcmp(r->name,"test:all-long"))long_value=!strcmp(r->kind,"property-long")&&r->total==300;
            if(!strcmp(r->name,"guid"))guid=1;
            if(!strncmp(r->name,"feature@",8))features++;
            rows++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(!cursor)last_page_cursor=page_cursor;
        if(out){oz_message_free(out);out=NULL;}
        if(pages>32)error=1;
    }while(!error&&cursor);
    if(!error)error=!user||!long_value||!guid||!rows||
        (operation==40&&(rows<=OZ_MAX_ROWS||pages<2))||(operation==31&&!features);
    if(!error)error=property_catalogue_end(&client,m,last_page_cursor,&catalogue_end);
    if(!error)error=property_cursor_case(&client,m,catalogue_end,OZ_OK);
    if(!error)error=property_cursor_case(&client,m,catalogue_end+1,OZ_ENGINE_ERROR);
    if(!error)error=property_cursor_case(&client,m,~(UQUAD)0,OZ_ENGINE_ERROR);
    if(error)bug("Property all verdict op=%u rows=%u pages=%u user=%d long=%d guid=%d features=%u names=%u\n",
        operation,rows,pages,user,long_value,guid,features,names_count);
    free(m);oz_client_close(&client);
    *rows_out=rows;*pages_out=pages;*features_out=features;return error;
}
struct recursive_all_seen {
    char parent[128];
    char name[64];
};
static int recursive_all_property_rows(const char *target,unsigned *rows_out,unsigned *pages_out)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    struct recursive_all_seen *seen=calloc(512,sizeof(*seen));
    char expected[3][128];unsigned rows=0,pages=0,seen_count=0;
    unsigned dataset_rows[3]={0},user[3]={0},long_value[3]={0};int error=0;
    if(!m||!seen){free(m);free(seen);return 1;}
    snprintf(expected[0],sizeof(expected[0]),"%s",target);
    snprintf(expected[1],sizeof(expected[1]),"%s/child",target);
    snprintf(expected[2],sizeof(expected[2]),"%s/child/grandchild",target);
    if(!oz_client_open(&client)){free(m);free(seen);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=40;m->flags=OZ_RECURSIVE;strcpy(m->target,target);strcpy(m->argument,"all");
    UQUAD cursor=0;
    do {
        m->cursor=cursor;error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        pages++;
        if(out)bug("Recursive property all page=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            pages,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];int dataset=-1;
            for(unsigned d=0;d<3;d++)if(!strcmp(r->parent,expected[d]))dataset=d;
            if(dataset<0||(strcmp(r->kind,"property")&&strcmp(r->kind,"property-long"))||
                !r->name[0]||!r->state[0]||seen_count>=512)error=1;
            for(unsigned n=0;!error&&n<seen_count;n++)
                if(!strcmp(seen[n].parent,r->parent)&&!strcmp(seen[n].name,r->name))error=1;
            if(error)break;
            strcpy(seen[seen_count].parent,r->parent);strcpy(seen[seen_count++].name,r->name);
            dataset_rows[dataset]++;
            if(!strcmp(r->name,"test:all"))
                user[dataset]=!strcmp(r->kind,"property")&&!strcmp(r->value,"visible");
            if(!strcmp(r->name,"test:all-long"))
                long_value[dataset]=!strcmp(r->kind,"property-long")&&r->total==300;
            rows++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out?out->next_cursor:0;
        if(out){oz_message_free(out);out=NULL;}
        if(pages>32)error=1;
    }while(!error&&cursor);
    for(unsigned d=0;!error&&d<3;d++)
        if(dataset_rows[d]<=OZ_MAX_ROWS||!user[d]||!long_value[d])error=1;
    if(!error&&pages<7)error=1;
    if(!error)error=property_cursor_case(&client,m,(UQUAD)3<<32,OZ_OK);
    if(!error)error=property_cursor_case(&client,m,(UQUAD)4<<32,OZ_ENGINE_ERROR);
    if(!error)error=property_cursor_case(&client,m,((UQUAD)3<<32)|1,OZ_ENGINE_ERROR);
    if(!error)error=property_cursor_case(&client,m,~(UQUAD)0,OZ_ENGINE_ERROR);
    if(error)bug("Recursive property all verdict rows=%u pages=%u seen=%u datasets=%u/%u/%u user=%u/%u/%u long=%u/%u/%u\n",
        rows,pages,seen_count,dataset_rows[0],dataset_rows[1],dataset_rows[2],
        user[0],user[1],user[2],long_value[0],long_value[1],long_value[2]);
    free(m);free(seen);oz_client_close(&client);
    *rows_out=rows;*pages_out=pages;return error;
}
static int all_property_query_test(void)
{
#define APCHECK(x) do{if(!(x)){bug("RESULT native property all FAIL line=%d\n",__LINE__);return 20;}}while(0)
    RUN(0,NULL,"C:zfs","set","test:all=visible","qemu_mirror");
    char assignment[400]="test:all-long=";unsigned prefix=strlen(assignment);
    memset(assignment+prefix,'y',300);assignment[prefix+300]=0;
    RUN(0,NULL,"C:zfs","set",assignment,"qemu_mirror");
    unsigned dataset_rows=0,dataset_pages=0,ignored=0,pool_rows=0,pool_pages=0,features=0;
    APCHECK(!all_property_rows(40,"qemu_mirror",&dataset_rows,&dataset_pages,&ignored));
    APCHECK(!all_property_rows(31,"qemu_mirror",&pool_rows,&pool_pages,&features));
    const char *recursive_root="qemu_mirror/all-properties";
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",recursive_root);
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/all-properties/child");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/all-properties/child/grandchild");
    unsigned recursive_rows=0,recursive_pages=0;
    APCHECK(!recursive_all_property_rows(recursive_root,&recursive_rows,&recursive_pages));
    RUN(0,NULL,"C:zfs","destroy","-r",recursive_root);
    RUN(0,NULL,"C:zfs","inherit","test:all","qemu_mirror");
    RUN(0,NULL,"C:zfs","inherit","test:all-long","qemu_mirror");
    bug("RESULT native property all PASS dataset-rows=%u dataset-pages=%u user=1 long=1 pool-rows=%u pool-pages=%u guid=1 features=%u cursor-end=2 invalid-cursor=4 errno-detail=4 cleanup=2\n",
        dataset_rows,dataset_pages,pool_rows,pool_pages,features);
    bug("RESULT native recursive property all PASS datasets=3 rows=%u pages=%u unique=%u user=3 long=3 cursor-end=1 invalid-cursor=3 errno-detail=3 cleanup=3\n",
        recursive_rows,recursive_pages,recursive_rows);
#undef APCHECK
    return 0;
}
static int deferred_snapshot_destroy_test(void)
{
#define DCHECK(x) do{if(!(x)){bug("RESULT native deferred snapshot destroy FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *dataset="qemu_mirror/deferred-destroy";
    const char *snapshot="qemu_mirror/deferred-destroy@held";
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",dataset);
    RUN(0,NULL,"C:zfs","snapshot",snapshot);
    DCHECK(!manager_permission_or_create(55,snapshot,"defer-test","",0));
    DCHECK(!manager_permission_or_create(38,snapshot,"","",EBUSY));
    DCHECK(!manager_permission_or_create(38,snapshot,"","defer=on",0));
    RUN(0,"on","C:zfs","get","-H","-o","value","defer_destroy",snapshot);
    RUN(0,snapshot,"C:zfs","list","-H","-t","snapshot",snapshot);
    DCHECK(!manager_admission_refused(38,snapshot,"defer=off",ENOTSUP));
    DCHECK(!manager_admission_refused(38,dataset,"defer=on",ENOTSUP));
    DCHECK(!manager_admission_refused(38,"qemu_mirror/deferred-destroy#held","defer=on",ENOTSUP));
    DCHECK(!manager_permission_or_create(56,snapshot,"defer-test","",0));
    RUN(1,NULL,"C:zfs","list","-H","-t","snapshot",snapshot);
    RUN(0,NULL,"C:zfs","destroy",dataset);
    bug("RESULT native deferred snapshot destroy PASS held=1 marked=1 retained=1 release-destroyed=1 invalid=3 cleanup=1\n");
#undef DCHECK
    return 0;
}
static int recursive_snapshot_destroy_test(void)
{
#define RCHECK(x) do{if(!(x)){bug("RESULT native recursive snapshot destroy FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *datasets[]={"qemu_mirror/recursive-destroy",
        "qemu_mirror/recursive-destroy/child","qemu_mirror/recursive-destroy/child/grandchild"};
    char batch[3][128],keep[3][128];
    for(unsigned i=0;i<3;i++) {
        RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",datasets[i]);
        snprintf(batch[i],sizeof(batch[i]),"%s@batch",datasets[i]);
        snprintf(keep[i],sizeof(keep[i]),"%s@keep",datasets[i]);
        RUN(0,NULL,"C:zfs","snapshot",batch[i]);RUN(0,NULL,"C:zfs","snapshot",keep[i]);
    }
    RCHECK(manager_permission_flags(38,batch[0],"","defer=on",0,
        OZ_CONFIRMED|OZ_RECURSIVE)==ENOTSUP);
    RCHECK(!manager_permission_or_create(55,batch[2],"recursive-blocker","",0));
    RCHECK(!manager_permission_flags(38,batch[0],"","",EBUSY,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++)RUN(0,batch[i],"C:zfs","list","-H","-t","snapshot",batch[i]);
    RCHECK(!manager_permission_or_create(56,batch[2],"recursive-blocker","",0));
    RCHECK(!manager_permission_flags(38,batch[0],"","",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++) {
        RUN(1,NULL,"C:zfs","list","-H","-t","snapshot",batch[i]);
        RUN(0,keep[i],"C:zfs","list","-H","-t","snapshot",keep[i]);
    }
    for(int i=2;i>=0;i--) {
        RUN(0,NULL,"C:zfs","destroy",keep[i]);RUN(0,NULL,"C:zfs","destroy",datasets[i]);
    }
    bug("RESULT native recursive snapshot destroy PASS snapshots=3 atomic-busy=1 retained=3 keep=3 invalid=1 cleanup=1\n");
#undef RCHECK
    return 0;
}
static int recursive_dataset_destroy_test(void)
{
#define RDCHECK(x) do{if(!(x)){bug("RESULT native recursive dataset destroy FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *datasets[]={"qemu_mirror/recursive-dataset-destroy",
        "qemu_mirror/recursive-dataset-destroy/child",
        "qemu_mirror/recursive-dataset-destroy/child/grandchild"};
    const char *snapshot="qemu_mirror/recursive-dataset-destroy/child@safety";
    const char *keep="qemu_mirror/recursive-dataset-keep";
    for(unsigned i=0;i<3;i++)RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",datasets[i]);
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",keep);
    RDCHECK(!manager_permission_or_create(43,datasets[1],"RDESTBUSY","readonly=off",0));
    RDCHECK(!manager_permission_flags(38,datasets[0],"","",EBUSY,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++)RUN(0,datasets[i],"C:zfs","list","-H",datasets[i]);
    RDCHECK(!manager_permission_or_create(44,datasets[1],"","",0));
    RUN(0,NULL,"C:zfs","snapshot",snapshot);
    RDCHECK(manager_permission_flags(38,datasets[0],"","defer=on",0,
        OZ_CONFIRMED|OZ_RECURSIVE)==ENOTSUP);
    char detail[192];snprintf(detail,sizeof(detail),
        "Authenticated recursive dataset destroy refused before mutation snapshots=1 completed=0 errno=%d",EEXIST);
    RDCHECK(!manager_permission_expect(38,datasets[0],"","",EEXIST,
        OZ_CONFIRMED|OZ_RECURSIVE,detail));
    for(unsigned i=0;i<3;i++)RUN(0,datasets[i],"C:zfs","list","-H",datasets[i]);
    RUN(0,snapshot,"C:zfs","list","-H","-t","snapshot",snapshot);
    RUN(0,NULL,"C:zfs","destroy",snapshot);
    snprintf(detail,sizeof(detail),"Authenticated recursive dataset destroy completed=3 errno=0");
    RDCHECK(!manager_permission_expect(38,datasets[0],"","",0,
        OZ_CONFIRMED|OZ_RECURSIVE,detail));
    for(unsigned i=0;i<3;i++)RUN(1,NULL,"C:zfs","list","-H",datasets[i]);
    RUN(0,keep,"C:zfs","list","-H",keep);RUN(0,NULL,"C:zfs","destroy",keep);
    bug("RESULT native recursive dataset destroy PASS datasets=3 mounted-preflight=1 snapshot-preflight=1 retained=6 destroyed=3 keep=1 invalid=1 progress-detail=1 cleanup=1\n");
#undef RDCHECK
    return 0;
}
static int recursive_snapshot_create_test(void)
{
#define SCHECK(x) do{if(!(x)){bug("RESULT native recursive snapshot create FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *datasets[]={"qemu_mirror/recursive-create",
        "qemu_mirror/recursive-create/child","qemu_mirror/recursive-create/child/grandchild"};
    char batch[3][128],collision[3][128];
    for(unsigned i=0;i<3;i++) {
        RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",datasets[i]);
        snprintf(batch[i],sizeof(batch[i]),"%s@batch",datasets[i]);
        snprintf(collision[i],sizeof(collision[i]),"%s@collision",datasets[i]);
    }
    SCHECK(!manager_permission_flags(50,batch[0],"","",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++)RUN(0,batch[i],"C:zfs","list","-H","-t","snapshot",batch[i]);
    RUN(0,NULL,"C:zfs","snapshot",collision[1]);
    SCHECK(!manager_permission_flags(50,collision[0],"","",EEXIST,OZ_CONFIRMED|OZ_RECURSIVE));
    RUN(1,NULL,"C:zfs","list","-H","-t","snapshot",collision[0]);
    RUN(0,collision[1],"C:zfs","list","-H","-t","snapshot",collision[1]);
    RUN(1,NULL,"C:zfs","list","-H","-t","snapshot",collision[2]);
    SCHECK(!manager_permission_flags(38,batch[0],"","",0,OZ_CONFIRMED|OZ_RECURSIVE));
    RUN(0,NULL,"C:zfs","destroy",collision[1]);
    for(int i=2;i>=0;i--)RUN(0,NULL,"C:zfs","destroy",datasets[i]);
    bug("RESULT native recursive snapshot create PASS snapshots=3 atomic-collision=1 retained=1 absent=2 cleanup=1\n");
#undef SCHECK
    return 0;
}
static int recursive_snapshot_holds_test(void)
{
#define HCHECK(x) do{if(!(x)){bug("RESULT native recursive snapshot holds FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *datasets[]={"qemu_mirror/recursive-holds",
        "qemu_mirror/recursive-holds/child","qemu_mirror/recursive-holds/child/grandchild"};
    char snapshots[3][128];const char *tag="recursive-tag";
    for(unsigned i=0;i<3;i++) {
        RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",datasets[i]);
        snprintf(snapshots[i],sizeof(snapshots[i]),"%s@held",datasets[i]);
        RUN(0,NULL,"C:zfs","snapshot",snapshots[i]);
    }
    HCHECK(!manager_permission_or_create(55,snapshots[1],tag,"",0));
    HCHECK(!manager_permission_flags(55,snapshots[0],tag,"",EEXIST,OZ_CONFIRMED|OZ_RECURSIVE));
    HCHECK(!manager_hold_exists(snapshots[0],tag,0)&&!manager_hold_exists(snapshots[1],tag,1)&&
        !manager_hold_exists(snapshots[2],tag,0));
    HCHECK(!manager_permission_or_create(56,snapshots[1],tag,"",0));
    HCHECK(!manager_permission_flags(55,snapshots[0],tag,"",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++)HCHECK(!manager_hold_exists(snapshots[i],tag,1));
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<8;j++) {
        char query_tag[16];snprintf(query_tag,sizeof(query_tag),"query-%02u",j);
        HCHECK(!manager_permission_or_create(55,snapshots[i],query_tag,"",0));
    }
    HCHECK(!manager_recursive_hold_rows(snapshots,tag));
    HCHECK(!manager_hold_cursor_case(snapshots[1],0,9,OZ_OK));
    HCHECK(!manager_hold_cursor_case(snapshots[1],0,10,OZ_ENGINE_ERROR));
    HCHECK(!manager_hold_cursor_case(snapshots[1],0,~(UQUAD)0,OZ_ENGINE_ERROR));
    HCHECK(!manager_hold_cursor_case(snapshots[0],OZ_RECURSIVE,27,OZ_OK));
    HCHECK(!manager_hold_cursor_case(snapshots[0],OZ_RECURSIVE,28,OZ_ENGINE_ERROR));
    HCHECK(!manager_hold_cursor_case(snapshots[0],OZ_RECURSIVE,~(UQUAD)0,OZ_ENGINE_ERROR));
    for(unsigned i=0;i<3;i++)for(unsigned j=0;j<8;j++) {
        char query_tag[16];snprintf(query_tag,sizeof(query_tag),"query-%02u",j);
        HCHECK(!manager_permission_or_create(56,snapshots[i],query_tag,"",0));
    }
    HCHECK(!manager_permission_or_create(56,snapshots[1],tag,"",0));
    HCHECK(!manager_permission_flags(56,snapshots[0],tag,"",ENOENT,OZ_CONFIRMED|OZ_RECURSIVE));
    HCHECK(!manager_hold_exists(snapshots[0],tag,1)&&!manager_hold_exists(snapshots[1],tag,0)&&
        !manager_hold_exists(snapshots[2],tag,1));
    HCHECK(!manager_permission_or_create(55,snapshots[1],tag,"",0));
    HCHECK(!manager_permission_flags(56,snapshots[0],tag,"",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned i=0;i<3;i++)HCHECK(!manager_hold_exists(snapshots[i],tag,0));
    HCHECK(!manager_permission_flags(38,snapshots[0],"","",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(int i=2;i>=0;i--)RUN(0,NULL,"C:zfs","destroy",datasets[i]);
    bug("RESULT native recursive snapshot holds PASS snapshots=3 hold-atomic=1 queries=27 pages=2 cursor-end=2 invalid-cursor=4 errno-detail=4 release-preflight=1 release-all=1 cleanup=1\n");
#undef HCHECK
    return 0;
}
static int bookmark_pagination_test(void)
{
#define BCHECK(x) do{if(!(x)){bug("RESULT native bookmark pagination FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *dataset="qemu_mirror/bookmark-page";
    const char *snapshot="qemu_mirror/bookmark-page@source";
    RUN(0,NULL,"C:zpool","set","feature@bookmarks=enabled","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none",dataset);
    RUN(0,NULL,"C:zfs","snapshot",snapshot);
    for(unsigned i=0;i<27;i++) {
        char bookmark[128];snprintf(bookmark,sizeof(bookmark),"%s#page-%02u",dataset,i);
        BCHECK(!manager_permission_or_create(51,snapshot,bookmark,"",0));
    }
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    BCHECK(m&&oz_client_open(&client));
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=36;strcpy(m->target,dataset);strcpy(m->options,"bookmarks=on");
    unsigned char seen[27]={0};unsigned rows=0,pages=0;UQUAD cursor=0;
    do {
        m->cursor=cursor;BCHECK(oz_submit(&client,m)==OZ_OK);
        while(!(out=oz_poll(&client)))Delay(1);
        BCHECK(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS);
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];unsigned ordinal=0;char extra=0;
            BCHECK(!strcmp(r->kind,"bookmark")&&!strcmp(r->parent,dataset)&&
                !strcmp(r->state,"persistent")&&
                !strncmp(r->name,dataset,strlen(dataset))&&
                sscanf(r->name+strlen(dataset),"#page-%u%c",&ordinal,&extra)==1&&
                ordinal<27&&!seen[ordinal]++&&strstr(r->value,"guid=")&&
                strstr(r->value," createtxg=")&&strstr(r->value," creation="));
            rows++;
        }
        BCHECK(!out->next_cursor||out->next_cursor>cursor);
        cursor=out->next_cursor;oz_message_free(out);out=NULL;pages++;
    }while(cursor);
    BCHECK(rows==27&&pages==2);
    for(unsigned i=0;i<27;i++)BCHECK(seen[i]==1);
    static const struct {UQUAD cursor;ULONG status;} boundaries[]={
        {27,OZ_OK},{28,OZ_ENGINE_ERROR},{~(UQUAD)0,OZ_ENGINE_ERROR}};
    for(unsigned i=0;i<sizeof(boundaries)/sizeof(boundaries[0]);i++) {
        m->cursor=boundaries[i].cursor;BCHECK(oz_submit(&client,m)==OZ_OK);
        while(!(out=oz_poll(&client)))Delay(1);
        bug("Bookmark cursor boundary cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            (unsigned long long)m->cursor,(unsigned)out->status,(unsigned)out->row_count,
            (unsigned long long)out->next_cursor,out->detail);
        BCHECK(out->status==boundaries[i].status&&!out->row_count&&!out->next_cursor&&
            (boundaries[i].status==OZ_OK||strstr(out->detail,"errno=22")));
        oz_message_free(out);out=NULL;
    }
    free(m);oz_client_close(&client);
    for(unsigned i=0;i<27;i++) {
        char bookmark[128];snprintf(bookmark,sizeof(bookmark),"%s#page-%02u",dataset,i);
        BCHECK(!manager_permission_or_create(38,bookmark,"","",0));
    }
    RUN(0,NULL,"C:zfs","destroy",snapshot);RUN(0,NULL,"C:zfs","destroy",dataset);
    bug("RESULT native bookmark pagination PASS bookmarks=27 pages=2 unique=27 cursor-end=1 invalid-cursor=2 errno-detail=2 cleanup=29\n");
#undef BCHECK
    return 0;
}
static int status_cursor_boundary_test(void)
{
#define STCHECK(x) do{if(!(x)){bug("RESULT native status cursor boundaries FAIL line=%d\n",__LINE__);return 20;}}while(0)
    struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;struct oz_client client;
    STCHECK(m&&oz_client_open(&client));
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    strcpy(m->target,"qemu_mirror");
    static const struct {ULONG operation;const char *options;} views[]={
        {4,""},{35,""},{35,"histogram=latency"},{35,"histogram=size"}};
    for(unsigned view=0;view<sizeof(views)/sizeof(views[0]);view++) {
        m->operation=views[view].operation;m->cursor=0;
        strcpy(m->options,views[view].options);
        UQUAD end=0;unsigned pages=0;
        do {
            STCHECK(oz_submit(&client,m)==OZ_OK);
            while(!(out=oz_poll(&client)))Delay(1);
            STCHECK(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS);
            end+=out->row_count;
            STCHECK(!out->next_cursor||out->next_cursor==end);
            m->cursor=out->next_cursor;oz_message_free(out);out=NULL;
            STCHECK(++pages<128);
        } while(m->cursor);
        const UQUAD cursors[]={end,end+1,~(UQUAD)0};
        const ULONG statuses[]={OZ_OK,OZ_ENGINE_ERROR,OZ_ENGINE_ERROR};
        for(unsigned i=0;i<sizeof(cursors)/sizeof(cursors[0]);i++) {
            m->cursor=cursors[i];STCHECK(oz_submit(&client,m)==OZ_OK);
            while(!(out=oz_poll(&client)))Delay(1);
            bug("Status cursor boundary op=%u options=%s live=%llu pages=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
                (unsigned)m->operation,m->options,(unsigned long long)end,pages,(unsigned long long)m->cursor,
                (unsigned)out->status,(unsigned)out->row_count,
                (unsigned long long)out->next_cursor,out->detail);
            STCHECK(out->status==statuses[i]&&!out->row_count&&!out->next_cursor&&
                (statuses[i]==OZ_OK||strstr(out->detail,"errno=22")));
            oz_message_free(out);out=NULL;
        }
    }
    free(m);oz_client_close(&client);
    bug("RESULT native status cursor boundaries PASS views=4 paginated=2 cursor-end=4 invalid-cursor=8 errno-detail=8\n");
#undef STCHECK
    return 0;
}
static int history_cursor_boundary_test(void)
{
#define HCCHECK(x) do{if(!(x)){bug("RESULT native history cursor boundaries FAIL line=%d\n",__LINE__);return 20;}}while(0)
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    HCCHECK(m&&out);
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=33;m->flags=OZ_RAW;strcpy(m->target,"qemu_mirror");
    UQUAD cursor=0,end=0;unsigned rows=0,pages=0;
    do {
        m->cursor=cursor;struct oz_ticket ticket={0};
        HCCHECK(!oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket));
        int error;do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        bug("History raw page=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            pages+1,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        HCCHECK(!error&&!released);
        HCCHECK(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS);
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];char *tail=NULL;
            errno=0;UQUAD start=strtoull(r->name,&tail,10);
            HCCHECK(!errno&&tail!=r->name&&!*tail&&!strcmp(r->kind,"history.raw")&&
                !strcmp(r->parent,m->target)&&r->total&&r->total<=96&&start==end);
            end=start+r->total;rows++;
        }
        HCCHECK(!out->next_cursor||out->next_cursor==end);
        cursor=out->next_cursor;memset(out,0,sizeof(*out));
        HCCHECK(++pages<1024);
    } while(cursor);
    HCCHECK(rows&&end&&end<~(UQUAD)0);
    const ULONG flags[]={OZ_RAW,0};
    for(unsigned view=0;view<sizeof(flags)/sizeof(flags[0]);view++) {
        m->flags=flags[view];
        const UQUAD cursors[]={end,end+1,~(UQUAD)0};
        const ULONG statuses[]={OZ_OK,OZ_ENGINE_ERROR,OZ_ENGINE_ERROR};
        for(unsigned i=0;i<sizeof(cursors)/sizeof(cursors[0]);i++) {
            m->cursor=cursors[i];struct oz_ticket ticket={0};
            HCCHECK(!oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket));
            int error;do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
            bug("History cursor boundary raw=%u end=%llu cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
                !!(m->flags&OZ_RAW),(unsigned long long)end,(unsigned long long)m->cursor,
                (unsigned)out->status,(unsigned)out->row_count,
                (unsigned long long)out->next_cursor,out->detail);
            int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
            HCCHECK(!error&&!released);
            HCCHECK(out->status==statuses[i]&&!out->row_count&&!out->next_cursor&&
                (statuses[i]==OZ_OK||strstr(out->detail,"errno=22")));
            memset(out,0,sizeof(*out));
        }
    }
    free(m);free(out);
    bug("RESULT native history cursor boundaries PASS views=2 raw-bytes=%llu raw-rows=%u raw-pages=%u cursor-end=2 invalid-cursor=4 errno-detail=4\n",
        (unsigned long long)end,rows,pages);
#undef HCCHECK
    return 0;
}
static int manager_history_unsupported(const char *pool,ULONG flags)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=33;m->flags=flags;strcpy(m->target,pool);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        char detail[40];snprintf(detail,sizeof(detail),"errno=%d",ENOTSUP);
        if(!error) {
            bug("Legacy history raw=%u status=%u rows=%u next=%llu detail=%s\n",
                !!(flags&OZ_RAW),(unsigned)out->status,(unsigned)out->row_count,
                (unsigned long long)out->next_cursor,out->detail);
            error=out->status!=OZ_ENGINE_ERROR||out->row_count||out->next_cursor||
                !strstr(out->detail,detail);
        }
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
    }
    free(m);free(out);return error;
}
#ifdef OZ_TEST_HISTORY_MEGARECORD
#define OZ_HISTORY_MEGARECORD_BYTES (1024U * 1024U + 4096U)
#define OZ_HISTORY_MEGARECORD_PREFIX "history-megarecord-v1-"
static unsigned history_megarecord_matches(const unsigned char *record,size_t length)
{
    const size_t prefix=strlen(OZ_HISTORY_MEGARECORD_PREFIX);unsigned matches=0;
    if(length<OZ_HISTORY_MEGARECORD_BYTES+1)return 0;
    for(size_t start=0;start+OZ_HISTORY_MEGARECORD_BYTES<length;start++) {
        if(memcmp(record+start,OZ_HISTORY_MEGARECORD_PREFIX,prefix))continue;
        size_t i=prefix;
        for(;i<OZ_HISTORY_MEGARECORD_BYTES;i++)
            if(record[start+i]!=(unsigned char)('a'+i%23))break;
        if(i==OZ_HISTORY_MEGARECORD_BYTES&&!record[start+i])matches++;
    }
    return matches;
}
static int manager_history_request(struct oz_message *m,struct oz_message *out)
{
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);
        if(!error)error=released;
    }
    if(error)bug("Manager history megarecord admission transport errno=%d\n",error);
    return error;
}
static int manager_history_megarecord(const char *pool)
{
#define MHCHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    unsigned summary_pages=0,raw_pages=0,large_records=0,matches=0;
    unsigned char header[8],*large=NULL;size_t header_used=0,record_used=0;
    uint64_t record_length=0,cursor=0,boundary=0,position=0;int error=0;
    MHCHECK(m&&out);m->magic=OZ_MAGIC;m->version=OZ_VERSION;
    m->msg.mn_Length=sizeof(*m);m->operation=33;strcpy(m->target,pool);
    for(;;) {
        m->cursor=cursor;MHCHECK(!manager_history_request(m,out));
        summary_pages++;
        bug("Manager history megarecord summary page=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            summary_pages,(unsigned long long)cursor,(unsigned)out->status,
            (unsigned)out->row_count,(unsigned long long)out->next_cursor,out->detail);
        if(out->status==OZ_ENGINE_ERROR) {
            char detail[32];snprintf(detail,sizeof(detail),"errno=%d",EOVERFLOW);
            MHCHECK(!out->row_count&&!out->next_cursor&&strstr(out->detail,detail));
            boundary=cursor;memset(out,0,sizeof(*out));break;
        }
        MHCHECK(out->status==OZ_OK&&out->row_count&&out->next_cursor>cursor);
        cursor=out->next_cursor;memset(out,0,sizeof(*out));
        MHCHECK(summary_pages<256);
    }
    m->flags=OZ_RAW;cursor=position=boundary;
    while(!matches) {
        m->cursor=cursor;MHCHECK(!manager_history_request(m,out));
        MHCHECK(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS);
        for(unsigned r=0;r<out->row_count;r++) {
            struct oz_row *row=&out->rows[r];char *tail=NULL;
            errno=0;uint64_t start=strtoull(row->name,&tail,10);
            MHCHECK(!errno&&tail!=row->name&&!*tail&&start==position&&
                !strcmp(row->kind,"history.raw")&&row->total&&row->total<=96&&
                strlen(row->value)==row->total*2);
            for(unsigned n=0;n<row->total;n++) {
                const char *digits="0123456789abcdef";
                const char *hi=strchr(digits,row->value[n*2]);
                const char *lo=strchr(digits,row->value[n*2+1]);
                MHCHECK(hi&&lo);unsigned char byte=(unsigned char)((hi-digits)*16+(lo-digits));
                if(header_used<sizeof(header)) {
                    header[header_used++]=byte;
                    if(header_used==sizeof(header)) {
                        record_length=0;
                        for(unsigned b=0;b<sizeof(header);b++)record_length|=(uint64_t)header[b]<<(8*b);
                        MHCHECK(record_length&&record_length<2U*1024U*1024U);
                        if(record_length>1024U*1024U) {
                            MHCHECK(!large);large=malloc((size_t)record_length);MHCHECK(large);
                            large_records++;
                        }
                        record_used=0;
                    }
                } else {
                    if(large)large[record_used]=byte;
                    record_used++;
                    if(record_used==record_length) {
                        if(large) {
                            matches=history_megarecord_matches(large,(size_t)record_length);
                            free(large);large=NULL;
                        }
                        header_used=0;record_used=0;record_length=0;
                    }
                }
                position++;
            }
        }
        MHCHECK(!out->next_cursor||out->next_cursor==position);
        cursor=out->next_cursor;memset(out,0,sizeof(*out));raw_pages++;
        MHCHECK(raw_pages<1024&&(matches||cursor));
    }
    MHCHECK(large_records==1&&matches==1&&!large&&record_length==0&&!record_used);
done:
    free(out);free(large);free(m);
    bug("RESULT native Manager history megarecord %s bytes=%u summary-eoverflow=%u raw-exact=%u summary-pages=%u raw-pages=%u line=%d\n",
        error?"FAIL":"PASS",OZ_HISTORY_MEGARECORD_BYTES,!error,matches==1,summary_pages,raw_pages,error);
    return error?20:0;
#undef MHCHECK
}
#endif
#ifdef OZ_TEST_HISTORY_RACE
#define OZ_HISTORY_RACE_RECORDS 240
struct history_race_state {
    pthread_mutex_t lock;
    unsigned completed,done;
    int error;
    char dataset[256];
};
static int history_race_snapshot(void *context,unsigned *completed,unsigned *done)
{
    struct history_race_state *state=context;
    int error=pthread_mutex_lock(&state->lock);if(error)return error;
    *completed=state->completed;*done=state->done;error=state->error;
    int unlocked=pthread_mutex_unlock(&state->lock);
    return error?error:unlocked;
}
static void *history_race_writer(void *context)
{
    struct history_race_state *state=context;
    BPTR output=Open((CONST_STRPTR)"RAM:history-race-writer.log",MODE_NEWFILE);
    int error=output?0:EIO;
    for(unsigned record=0;record<OZ_HISTORY_RACE_RECORDS&&!error;record++) {
        char assignment[6400];
        int prefix=snprintf(assignment,sizeof(assignment),
            "test:history-race=history-race-record-%03u-",record);
        if(prefix<0||(size_t)prefix+6001>sizeof(assignment)){error=EOVERFLOW;break;}
        memset(assignment+prefix,'a'+record%26,6000);assignment[prefix+6000]=0;
        char *args[]={"C:zfs","set",assignment,state->dataset,NULL};
        struct oz_cli_result result={0};
        error=oz_cli_spawn_exact(args[0],args,output,output,&result);
        if(!error&&(result.status||result.dos_error))error=EIO;
        int locked=pthread_mutex_lock(&state->lock);
        if(locked){error=error?error:locked;break;}
        if(error)state->error=error;else state->completed++;
        int unlocked=pthread_mutex_unlock(&state->lock);
        if(unlocked){error=error?error:unlocked;break;}
    }
    if(output&&!Close(output)&&!error)error=EIO;
    if(!pthread_mutex_lock(&state->lock)) {
        if(error&&!state->error)state->error=error;
        state->done=1;(void)pthread_mutex_unlock(&state->lock);
    }
    return NULL;
}
static int history_concurrent_overwrite(const char *pool,const char *dataset)
{
    struct history_race_state state={0};
    if(strlen(dataset)>=sizeof(state.dataset))return 20;
    strcpy(state.dataset,dataset);
    int error=pthread_mutex_init(&state.lock,NULL);pthread_t writer;
    int initialized=!error,created=0;
    if(!error){error=pthread_create(&writer,NULL,history_race_writer,&state);created=!error;}
    int reader=error?error:oz_history_wire_race_test(pool,history_race_snapshot,&state);
    if(created){int joined=pthread_join(writer,NULL);if(!error)error=joined;}
    unsigned completed=0,done=0;int writer_error=0,destroyed=0;
    if(initialized) {
        writer_error=history_race_snapshot(&state,&completed,&done);
        destroyed=pthread_mutex_destroy(&state.lock);
    }
    if(!error)error=reader?reader:writer_error?writer_error:destroyed;
    if(!error&&(completed!=OZ_HISTORY_RACE_RECORDS||!done))error=EIO;
    return error?20:0;
}
#endif
static int legacy_history_pool(void)
{
#define LHCHECK(x) do{if(!(x)){bug("RESULT native legacy pool history FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *native="RAM:cli-legacy-history.vdev";char canonical[256],path[258];
    char *zero=calloc(1,1024*1024);LHCHECK(zero);
    BPTR file=Open((CONST_STRPTR)native,MODE_NEWFILE);LHCHECK(file);
    int error=0;for(unsigned n=0;n<128;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=1;break;}
    free(zero);if(!Close(file))error=1;LHCHECK(!error);
    BPTR lock=Lock((CONST_STRPTR)native,ACCESS_READ);LHCHECK(lock);
    int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);LHCHECK(named);
    char *colon=strchr(canonical,':');LHCHECK(colon);*colon='/';snprintf(path,sizeof(path),"/%s",canonical);
    RUN(0,NULL,"C:zpool","create","-d","-o","version=1","-o","cachefile=none",
        "-O","mountpoint=none","cli_legacy_history",path);
    RUN(0,NULL,"C:zpool","get","-H","-o","value","version","cli_legacy_history");
    LHCHECK(strtoull(last_command_output,NULL,10)==1);
    LHCHECK(!manager_history_unsupported("cli_legacy_history",0));
    LHCHECK(!manager_history_unsupported("cli_legacy_history",OZ_RAW));
    RUN(0,NULL,"C:zpool","destroy","cli_legacy_history");
    LHCHECK(DeleteFile((CONST_STRPTR)native));
    bug("RESULT native legacy pool history PASS version=1 summary-unsupported=1 raw-unsupported=1 empty-errors=2 errno-detail=2 cleanup=1\n");
#undef LHCHECK
    return 0;
}
#ifdef OZ_TEST_ZVOL_TRANSPORT
static int manager_volume_create_target(const char *target,const char *options,int expected)
{ return manager_permission_or_create(37,target,"",options,expected); }
static int manager_volume_create(const char *options,int expected)
{ return manager_volume_create_target("qemu_mirror/transport-volume",options,expected); }
static int accounting_values_test(unsigned operation,const char *metric,unsigned expected_rows,uint64_t expected_value,uint64_t expected_default)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;strcpy(m->argument,metric);strcpy(m->target,"qemu_mirror/accounting");
    unsigned rows=0,pages=0;uint32_t seen=0;int error=0,positive=0;
    const char *units=(!strcmp(metric,"objects")||!strcmp(metric,"objquota"))?"objects":"bytes";
    const char *principal=operation==67?"user:":operation==68?"group:":"project:";
    do{
        struct oz_ticket ticket={0};error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
        if(!error){
            do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
            if(!error&&out->status!=OZ_OK){bug("Accounting query failed: %s\n",out->detail);error=1;}
            if(!error){
                char detail[192];snprintf(detail,sizeof(detail),"Authenticated accounting metric=%s default-quota=%llu units=%s errno=0",metric,(unsigned long long)expected_default,units);
                if(!strcmp(metric,"quota")||!strcmp(metric,"objquota"))snprintf(detail,sizeof(detail),"Authenticated accounting metric=%s units=%s; default quota: query usage metric; errno=0",metric,units);
                if(strcmp(out->detail,detail))error=1;
            }
            for(unsigned i=0;!error&&i<out->row_count;i++){
                struct oz_row *r=&out->rows[i];
                if(strcmp(r->kind,"accounting")||strcmp(r->state,metric)||strcmp(r->value,units)||r->parent[0]||
                    (expected_value!=UINT64_MAX&&r->used!=expected_value)||r->total!=expected_default||strncmp(r->name,principal,strlen(principal)))error=1;
                if(r->used)positive=1;
                if(expected_rows!=~0U){
                    unsigned id=0;char extra=0;
                    if(sscanf(r->name+strlen(principal),"%u%c",&id,&extra)!=1||id<1000||id>=1000+expected_rows)error=1;
                    else {uint32_t bit=1U<<(id-1000);if(seen&bit)error=1;seen|=bit;}
                }
                rows++;
            }
            m->cursor=out->next_cursor;
            int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
        }
        if(++pages>4)error=1;
    }while(!error&&m->cursor);
    if((expected_rows!=~0U&&rows!=expected_rows)||(expected_rows==30&&(seen!=0x3fffffffU||pages!=2))||
        (expected_value==UINT64_MAX&&!positive))error=1;
    bug("Accounting op=%u metric=%s rows=%u pages=%u error=%d\n",operation,metric,rows,pages,error);
    free(m);free(out);return error;
}
static int accounting_test(unsigned operation,const char *metric,unsigned expected_rows,uint64_t expected_value)
{return accounting_values_test(operation,metric,expected_rows,expected_value,0);}
static int accounting_matrix(void)
{
    if(accounting_test(67,"quota",30,1048576)||accounting_test(68,"quota",1,2097152)||
        accounting_test(69,"quota",1,3145728)||accounting_test(67,"objquota",1,7)||
        accounting_test(68,"objquota",1,8)||accounting_test(69,"objquota",1,9))return 1;
    for(unsigned op=67;op<=69;op++){
        if(accounting_values_test(op,"used",~0U,UINT64_MAX,(uint64_t)(op-63)*1048576)||
            accounting_values_test(op,"objects",~0U,UINT64_MAX,op+34))return 1;
    }
    return 0;
}
static int accounting_default_reads(void)
{
    const char *names[]={"defaultuserquota","defaultgroupquota","defaultprojectquota",
        "defaultuserobjquota","defaultgroupobjquota","defaultprojectobjquota"};
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m));
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    int error=0;
    for(unsigned i=0;i<6&&!error;i++){
        memset(m,0,sizeof(*m));m->operation=40;strcpy(m->target,"qemu_mirror/accounting");strcpy(m->argument,names[i]);
        error=oz_submit(&client,m)!=OZ_OK;struct oz_message *out=NULL;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        if(!error){
            char value[32];snprintf(value,sizeof(value),"%llu",(unsigned long long)(i<3?(uint64_t)(i+4)*1048576:i+98));
            error=out->status!=OZ_OK||out->row_count!=1||strcmp(out->rows[0].name,names[i])||strcmp(out->rows[0].value,value);
        }
        if(out)oz_message_free(out);
    }
    free(m);oz_client_close(&client);return error;
}
#ifdef OZ_TEST_PERMISSION_AUTHORIZE
static int cold_permission_authorization(void)
{
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    int retained=manager_permission_or_create(50,"qemu_mirror/named-permissions/first@cold","","",0);
    int removed=manager_permission_or_create(50,"qemu_mirror/named-permissions/second@cold-denied","","",EPERM);
    int denied=manager_volume_create_target("qemu_mirror/named-permissions/cold-denied","type=volume,volsize=2M",EPERM);
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    if(retained||removed||denied)return 20;
    RUN(0,NULL,"C:zfs","list","qemu_mirror/named-permissions/first@cold");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/second@cold-denied");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/cold-denied");
    if(manager_permission_or_create(65,"qemu_mirror/named-permissions","set:@base","permissions=create",0))return 20;
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    int created=manager_volume_create_target("qemu_mirror/named-permissions/cold-created","type=volume,volsize=2M",0);
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    if(created)return 20;
    RUN(0,"volume","C:zfs","get","-H","-o","value","type","qemu_mirror/named-permissions/cold-created");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/cold-created");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/first@cold");
    if(manager_permission_or_create(66,"qemu_mirror/named-permissions","set:@base","permissions=create",0))return 20;
    for(unsigned n=0;n<30;n++){
        char principal[32];snprintf(principal,sizeof(principal),"user:%u",1000+n);
        if(manager_permission_or_create(65,"qemu_mirror/named-permissions",principal,"permissions=mount;scope=local",0))return 20;
    }
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
#ifdef OZ_TEST_PERMISSION_LIST_GUI
    bug("Permission list GUI READY: UID77; list qemu_mirror/named-permissions/first, inspect both pages, close Manager\n");
    int gui_listed=SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE);
    if(gui_listed)return 20;
#endif
    int listed=permission_list_test(35);
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    if(listed)return 20;
    for(unsigned n=0;n<30;n++){
        char principal[32];snprintf(principal,sizeof(principal),"user:%u",1000+n);
        if(manager_permission_or_create(66,"qemu_mirror/named-permissions",principal,"permissions=mount;scope=local",0))return 20;
    }
    bug("RESULT native permission list pagination PASS ordinary-user=1 pages=2 rows=35 unique-extra-grants=30\n");
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    CloseLibrary(secBase);
    bug("RESULT native permissions cold authorization PASS retained-allowed=1 removed-denied=1 changed-set-denied=1 regrant-allowed=1\n");
    return 0;
}
#endif
#ifdef OZ_TEST_RAW_RESUME_COLD
static int cold_raw_resume(void)
{
#define RCHECK(x) do{if(!(x)){bug("RESULT native raw resume cold FAIL line=%d\n",__LINE__);return 20;}}while(0)
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/gui-crypt");
    RUN(0,"1-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-pending");
    char token[1024];size_t n=strcspn(last_command_output,"\r\n");
    RCHECK(n>2&&n<sizeof(token));
    memcpy(token,last_command_output,n);token[n]=0;
    char *cold_send[]={"C:zfs","send","-t",token,NULL};
    char *cold_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/raw-pending",NULL};
    RCHECK(!send_to_file(cold_send,"RAM:openzfs-cold-rest.stream"));
    RCHECK(!receive_from_file(cold_receive,"RAM:openzfs-cold-rest.stream"));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-pending");
    RCHECK(last_command_output[0]=='-'&&strspn(last_command_output+1,"\r\n")==strlen(last_command_output+1));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-pending");
    unsigned char key[32],expected[8192],actual[8192];
    for(unsigned i=0;i<sizeof(key);i++)key[i]=(unsigned char)(i+1);
    BPTR file=Open((CONST_STRPTR)"RAM:openzfs-cold-raw.key",MODE_NEWFILE);RCHECK(file);
    RCHECK(Write(file,key,sizeof(key))==sizeof(key));RCHECK(Close(file));
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cold-raw.key","qemu_mirror/raw-pending");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZRESUME:","qemu_mirror/raw-pending");
    for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(unsigned char)(i*37u+i/251u)^0x5a;
    file=Open((CONST_STRPTR)"OZRESUME:resume-payload",MODE_OLDFILE);RCHECK(file);
    for(unsigned i=0;i<128;i++){
        RCHECK(Read(file,actual,sizeof(actual))==sizeof(actual));
        RCHECK(!memcmp(actual,expected,sizeof(actual)));
    }
    RCHECK(Read(file,actual,1)==0);RCHECK(Close(file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/raw-pending");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/raw-pending");
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cold-rest.stream"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cold-raw.key"));
    bug("RESULT native raw resume cold PASS persisted-token=1 locked=1 original-key=1 exact-MiB=1 cleared=1 CLI=1\n");
#undef RCHECK
    return 0;
}
#endif
static int zvol_transport_test(void)
{
    struct oz_ticket session={0};LONG fd=-1,control=-1;
    static struct {struct oz_zvol_request q;unsigned char bytes[24576];} packet;
    unsigned char expected[512];for(unsigned i=0;i<512;i++)expected[i]=(unsigned char)(i*7);
#define ZCHECK(x) do{if(!(x)){bug("RESULT native zvol transport FAIL line=%d\n",__LINE__);return 20;}}while(0)
#ifndef OZ_TEST_ZVOL_DISCARD_COLD
    ZCHECK(SystemTags((CONST_STRPTR)"C:Mount DEVS:DOSDrivers/PIPE",TAG_DONE)==0);
    {
        struct oz_client client;ZCHECK(oz_client_open(&client));
        struct oz_message *request=calloc(1,sizeof(*request)),*response=NULL;
        ZCHECK(request);request->operation=3;
        ZCHECK(oz_submit(&client,request)==OZ_OK);
        while(!(response=oz_poll(&client)))Delay(1);
        ZCHECK(response->status==OZ_OK);
        unsigned found=0;char size[32]={0};
        for(unsigned i=0;i<response->row_count;i++)if(!strcmp(response->rows[i].name,"qemu_mirror")) {
            struct oz_row *row=&response->rows[i];found++;
            ZCHECK(row->total&&row->used&&row->available&&
                row->used<=row->total&&row->available==row->total-row->used);
            ZCHECK(strstr(row->value,"allocated=")&&strstr(row->value,"free="));
            snprintf(size,sizeof(size),"%llu",(unsigned long long)row->total);
        }
        ZCHECK(found==1);oz_message_free(response);free(request);oz_client_close(&client);
        RUN(0,size,"C:zpool","list","-H","-p","-o","size","qemu_mirror");
        bug("RESULT native Manager pool capacity PASS upstream-size=1 allocated=1 free=1\n");
#ifdef OZ_TEST_POOL_LIST_GUI
        bug("Native pool capacity GUI READY: query Pools/list, save response and close Manager\n");
        ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        bug("RESULT native pool capacity GUI closed\n");
#endif
    }
    /* Promotion changes snapshot ancestry, not live filesystem contents.
     * Retain real DOS file handles on both sides across the Manager call. */
    {
        RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZPROMOSRC:","qemu_mirror/promo-source");
        RUN(0,NULL,"C:zfs","mount","qemu_mirror/promo-source");
        BPTR f=Open((CONST_STRPTR)"OZPROMOSRC:stable",MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,"original",8)==8);ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/promo-source@base");
        RUN(0,NULL,"C:zfs","clone","-u","-o","mountpoint=OZPROMOCLONE:","qemu_mirror/promo-source@base","qemu_mirror/promo-clone");
        RUN(0,NULL,"C:zfs","mount","qemu_mirror/promo-clone");
        RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/promo-clone@base");
        ZCHECK(!manager_permission_or_create(53,"qemu_mirror/promo-clone","","",EEXIST));
        RUN(0,"qemu_mirror/promo-source@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/promo-clone");
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/promo-clone@base");
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-source@base","OZPROMOSNAP","readonly=on",0));
        f=Open((CONST_STRPTR)"OZPROMOSNAP:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(!manager_permission_or_create(53,"qemu_mirror/promo-clone","","",EBUSY));
        RUN(1,"busy","C:zfs","promote","qemu_mirror/promo-clone");
        char snap_bytes[9];ZCHECK(Read(f,snap_bytes,sizeof(snap_bytes))==8&&!memcmp(snap_bytes,"original",8));
        ZCHECK(Close(f));
        BPTR source=Open((CONST_STRPTR)"OZPROMOSRC:stable",MODE_OLDFILE);ZCHECK(source);
        BPTR clone=Open((CONST_STRPTR)"OZPROMOCLONE:stable",MODE_OLDFILE);ZCHECK(clone);
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=manager_permission_or_create(53,"qemu_mirror/promo-clone","","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/promo-clone","user:77","permissions=promote,mount",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        denied=manager_permission_or_create(53,"qemu_mirror/promo-clone","","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/promo-source","user:77","permissions=promote",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int promoted=manager_permission_or_create(53,"qemu_mirror/promo-clone","","",0);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!promoted);
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/promo-clone","user:77","permissions=promote,mount",0));
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/promo-source","user:77","permissions=promote",0));
        CloseLibrary(secBase);
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-clone@base","OZPROMOSNAP","readonly=on",0));
        f=Open((CONST_STRPTR)"OZPROMOSNAP:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,snap_bytes,sizeof(snap_bytes))==8&&!memcmp(snap_bytes,"original",8));ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","promote","qemu_mirror/promo-source");
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-source@base","OZPROMOSNAP","readonly=on",0));
        RUN(0,NULL,"C:zfs","promote","qemu_mirror/promo-clone");
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-clone@base","OZPROMOSNAP","readonly=on",0));
        f=Open((CONST_STRPTR)"OZPROMOSNAP:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,snap_bytes,sizeof(snap_bytes))==8&&!memcmp(snap_bytes,"original",8));ZCHECK(Close(f));
        ZCHECK(!manager_permission_or_create(44,"qemu_mirror/promo-clone@base","","",0));
        bug("RESULT native published snapshot promotion PASS busy=2 Manager-detach=1 CLI-detach=2 remount-read=2\n");
        char bytes[9];
        ZCHECK(Read(source,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(source));
        ZCHECK(Read(clone,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(clone));
        RUN(0,"qemu_mirror/promo-clone@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/promo-source");
        f=Open((CONST_STRPTR)"OZPROMOCLONE:independent",MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,"clone",5)==5);ZCHECK(Close(f));
        BPTR absent=Lock((CONST_STRPTR)"OZPROMOSRC:independent",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
#ifdef OZ_TEST_PROMOTION_GUI
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-clone@base","OZPROMOSNAP","readonly=on",0));
        bug("Native published snapshot promotion GUI READY: Snapshots/promote qemu_mirror/promo-source; confirm then close Manager\n");
        ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
        RUN(0,"qemu_mirror/promo-source@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/promo-clone");
        ZCHECK(!manager_permission_or_create(43,"qemu_mirror/promo-source@base","OZPROMOSNAP","readonly=on",0));
        f=Open((CONST_STRPTR)"OZPROMOSNAP:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,snap_bytes,sizeof(snap_bytes))==8&&!memcmp(snap_bytes,"original",8));ZCHECK(Close(f));
        ZCHECK(!manager_permission_or_create(44,"qemu_mirror/promo-source@base","","",0));
        bug("RESULT native published snapshot promotion GUI PASS mounted-before=1 detached=1 remount-read=1\n");
        f=Open((CONST_STRPTR)"OZPROMOCLONE:independent",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==5&&!memcmp(bytes,"clone",5));ZCHECK(Close(f));
        f=Open((CONST_STRPTR)"OZPROMOSRC:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(f));
        bug("RESULT native Manager mounted promotion GUI PASS actual-manager=1 ancestry=1 preserved-files=2\n");
#endif
        RUN(0,NULL,"C:zfs","unmount","qemu_mirror/promo-source");
        RUN(0,NULL,"C:zfs","unmount","qemu_mirror/promo-clone");
#ifdef OZ_TEST_PROMOTION_GUI
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/promo-clone");
        RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/promo-source");
#else
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/promo-source");
        RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/promo-clone");
#endif
        bug("RESULT native Manager mounted promotion PASS collision-refused=1 retained-handles=2 ancestry=1 independent-write=1\n");
        bug("RESULT native Manager promotion authority PASS ungranted-denied=1 clone-only-denied=1 both-granted=1 revoked=2 snapshot-busy-preserved=1\n");
    }
    /* Production-path online receive, with no retained-node test hooks.
     * Repeat the mounted objset swap and verify exact native DOS bytes. */
    RUN(0,NULL,"C:zfs","create","-u","-o","atime=off","-o","mountpoint=OZONRECV:","qemu_mirror/cli-online-recv");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-online-recv");
    BPTR stable=Open((CONST_STRPTR)"OZONRECV:stable",MODE_NEWFILE);ZCHECK(stable);
    ZCHECK(Write(stable,"original",8)==8);ZCHECK(Close(stable));
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-online-recv@base");
    for(unsigned cycle=0;cycle<2;cycle++) {
        char payload[80];snprintf(payload,sizeof(payload),"online receive cycle %u exact DOS payload",cycle);
        BPTR f=Open((CONST_STRPTR)"OZONRECV:payload",MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,payload,strlen(payload))==(LONG)strlen(payload));ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-online-recv@end");
        char *send[]={"C:zfs","send","-i","qemu_mirror/cli-online-recv@base",
            "qemu_mirror/cli-online-recv@end",NULL};
        ZCHECK(!send_to_file(send,"RAM:online-receive.stream"));
        RUN(0,NULL,"C:zfs","rollback","-r","qemu_mirror/cli-online-recv@base");
        BPTR absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        char *receive[]={"C:zfs","receive","-F","qemu_mirror/cli-online-recv",NULL};
        BPTR input=Open((CONST_STRPTR)"RAM:online-receive.stream",MODE_OLDFILE);ZCHECK(input);
        ZCHECK(Seek(input,0,OFFSET_END)>=0);
        LONG length=Seek(input,0,OFFSET_CURRENT);ZCHECK(length>1024);
        ZCHECK(Seek(input,0,OFFSET_BEGINNING)>=0);
        BPTR cut=Open((CONST_STRPTR)"RAM:online-receive-cut.stream",MODE_NEWFILE);ZCHECK(cut);
        char chunk[4096];LONG remaining=length-512;
        while(remaining>0) {
            LONG n=remaining>(LONG)sizeof(chunk)?(LONG)sizeof(chunk):remaining;
            ZCHECK(Read(input,chunk,n)==n);ZCHECK(Write(cut,chunk,n)==n);remaining-=n;
        }
        ZCHECK(Close(input));ZCHECK(Close(cut));
        ZCHECK(!receive_from_file_expected(receive,"RAM:online-receive-cut.stream",1));
        stable=Open((CONST_STRPTR)"OZONRECV:stable",MODE_OLDFILE);ZCHECK(stable);
        ZCHECK(Read(stable,chunk,sizeof(chunk))==8&&!memcmp(chunk,"original",8));
        ZCHECK(Read(stable,chunk,1)==0);ZCHECK(Close(stable));
        absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-online-recv");
        ZCHECK(!receive_from_file(receive,"RAM:online-receive.stream"));
        f=Open((CONST_STRPTR)"OZONRECV:payload",MODE_OLDFILE);ZCHECK(f);
        char bytes[80];ZCHECK(Read(f,bytes,sizeof(bytes))==(LONG)strlen(payload));
        ZCHECK(!memcmp(bytes,payload,strlen(payload)));
        ZCHECK(Read(f,bytes,1)==0);ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-online-recv");
        ZCHECK(!manager_permission_or_create(54,"qemu_mirror/cli-online-recv@base","","",EEXIST));
        f=Open((CONST_STRPTR)"OZONRECV:payload",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==(LONG)strlen(payload));
        ZCHECK(!memcmp(bytes,payload,strlen(payload)));ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-online-recv@end");
        ZCHECK(!manager_permission_or_create(54,"qemu_mirror/cli-online-recv@missing","","",ESRCH));
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=manager_permission_or_create(54,"qemu_mirror/cli-online-recv@base","","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/cli-online-recv","user:77","permissions=rollback",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int allowed=manager_permission_or_create(54,"qemu_mirror/cli-online-recv@base","","",0);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!allowed);
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/cli-online-recv","user:77","permissions=rollback",0));
        CloseLibrary(secBase);
        absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    }
    {
        BPTR f=Open((CONST_STRPTR)"OZONRECV:local-change",MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,"keep-local",10)==10);ZCHECK(Close(f));
        ZCHECK(!manager_permission_expect(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",ETXTBSY,OZ_CONFIRMED,
            "Receive refused: target differs from the incremental base or raw snapshot chain. "
            "Writable mounting can rebuild accounting after raw receive. "
            "Check the base snapshot and local changes; Force may discard changes and snapshots. errno=26"));
        char bytes[16];f=Open((CONST_STRPTR)"OZONRECV:local-change",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==10&&!memcmp(bytes,"keep-local",10));ZCHECK(Close(f));
        f=Open((CONST_STRPTR)"OZONRECV:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(f));
        BPTR absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        ZCHECK(manager_permission_flags(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",0,OZ_FORCE)==EINVAL);
        ZCHECK(manager_permission_flags(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",0,OZ_CONFIRMED|OZ_FORCE|OZ_RAW)==ENOTSUP);
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/cli-online-recv","user:77","permissions=receive:append,mount,create",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int forced=manager_permission_flags(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",0,OZ_CONFIRMED|OZ_FORCE);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(forced==EPERM);
        f=Open((CONST_STRPTR)"OZONRECV:local-change",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==10&&!memcmp(bytes,"keep-local",10));ZCHECK(Close(f));
        absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/cli-online-recv","user:77","permissions=receive",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        forced=manager_permission_flags(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",0,OZ_CONFIRMED|OZ_FORCE);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!forced);
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/cli-online-recv","user:77","permissions=receive,receive:append,mount,create",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        forced=manager_permission_flags(61,"qemu_mirror/cli-online-recv@revoked","RAM:online-receive.stream","",0,OZ_CONFIRMED|OZ_FORCE);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(forced==EPERM);
        CloseLibrary(secBase);
        absent=Lock((CONST_STRPTR)"OZONRECV:local-change",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        const char *incoming="online receive cycle 1 exact DOS payload";
        char received[80];f=Open((CONST_STRPTR)"OZONRECV:payload",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,received,sizeof(received))==(LONG)strlen(incoming)&&!memcmp(received,incoming,strlen(incoming)));ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-online-recv@end");
        ZCHECK(!manager_permission_or_create(54,"qemu_mirror/cli-online-recv@base","","",0));
        bug("RESULT native Manager mounted receive noforce PASS modified-refused=1 local-preserved=1 baseline-preserved=1\n");
        bug("RESULT native Manager mounted receive force PASS explicit=1 invalid-flags-refused=2 local-discarded=1 incoming-exact=1 rollback=1\n");
        bug("RESULT native Manager forced receive delegation PASS append-denied=1 local-preserved=1 receive-granted=1 revoked-denied=1\n");
    }
    /* Upstream stable ZFS_ERR_STREAM_TRUNCATED = 1040. */
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive-cut.stream","",1040));
    {
        char bytes[9];BPTR f=Open((CONST_STRPTR)"OZONRECV:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(f));
        BPTR absent=Lock((CONST_STRPTR)"OZONRECV:payload",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-online-recv");
    }
#ifdef OZ_TEST_RECEIVE_GUI
#ifdef OZ_TEST_FORCE_RECEIVE_GUI
    {
        BPTR f=Open((CONST_STRPTR)"OZONRECV:gui-local-change",MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,"discard",7)==7);ZCHECK(Close(f));
    }
    bug("Native forced receive GUI READY: select Force and inspect destructive warning before confirming mounted receive\n");
#endif
    bug("Native mounted receive GUI READY: Replication/receive qemu_mirror/cli-online-recv@end from RAM:online-receive.stream; confirm then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#ifdef OZ_TEST_FORCE_RECEIVE_GUI
    {
        BPTR absent=Lock((CONST_STRPTR)"OZONRECV:gui-local-change",ACCESS_READ);
        ZCHECK(!absent&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        bug("RESULT native Manager forced receive GUI PASS actual-manager=1 local-discarded=1\n");
    }
#endif
#else
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=manager_permission_or_create(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/cli-online-recv","user:77","permissions=receive",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        denied=manager_permission_or_create(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/cli-online-recv","user:77","permissions=mount,create",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int received=manager_permission_or_create(61,"qemu_mirror/cli-online-recv@end","RAM:online-receive.stream","",0);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!received);
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/cli-online-recv","user:77","permissions=receive,mount,create",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        denied=manager_permission_or_create(61,"qemu_mirror/cli-online-recv@after-revoke","RAM:online-receive.stream","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied);
        CloseLibrary(secBase);
        bug("RESULT native Manager mounted receive delegation PASS ungranted-denied=1 partial-denied=1 granted=1 revoked-denied=1\n");
    }
#endif
    {
        const char *payload="online receive cycle 1 exact DOS payload";
        char bytes[80];BPTR f=Open((CONST_STRPTR)"OZONRECV:payload",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==(LONG)strlen(payload)&&!memcmp(bytes,payload,strlen(payload)));ZCHECK(Close(f));
        f=Open((CONST_STRPTR)"OZONRECV:stable",MODE_OLDFILE);ZCHECK(f);
        ZCHECK(Read(f,bytes,sizeof(bytes))==8&&!memcmp(bytes,"original",8));ZCHECK(Close(f));
        RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-online-recv");
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-online-recv@end");
        ZCHECK(!manager_permission_or_create(54,"qemu_mirror/cli-online-recv@base","","",0));
        bug("RESULT native Manager mounted receive PASS incremental=1 exact-files=2 resumed-wait=1 rollback=1\n");
        bug("RESULT native Manager mounted receive recovery PASS truncated=1 baseline-preserved=1 retry=1\n");
#ifdef OZ_TEST_RECEIVE_GUI
        bug("RESULT native Manager mounted receive GUI PASS actual-manager=1 exact-files=2 resumed-wait=1 rollback=1\n");
#endif
    }
#ifdef OZ_TEST_ROLLBACK_GUI
    stable=Open((CONST_STRPTR)"OZONRECV:gui-rollback",MODE_NEWFILE);ZCHECK(stable);
    ZCHECK(Write(stable,"discard",7)==7);ZCHECK(Close(stable));
    bug("Native mounted rollback GUI READY: Snapshots/rollback qemu_mirror/cli-online-recv@base; confirm then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    BPTR gone_gui=Lock((CONST_STRPTR)"OZONRECV:gui-rollback",ACCESS_READ);
    ZCHECK(!gone_gui&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    stable=Open((CONST_STRPTR)"OZONRECV:stable",MODE_OLDFILE);ZCHECK(stable);
    char stable_bytes[9];ZCHECK(Read(stable,stable_bytes,sizeof(stable_bytes))==8);
    ZCHECK(!memcmp(stable_bytes,"original",8));ZCHECK(Close(stable));
    bug("RESULT native Manager mounted rollback GUI PASS actual-manager=1 discarded=1 baseline-preserved=1\n");
#endif
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-online-recv@base");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-online-recv");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-online-recv");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:online-receive.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:online-receive-cut.stream"));
    bug("RESULT native mounted receive recovery PASS truncated=2 preserved=2 retried=2\n");
    bug("RESULT native Manager mounted rollback PASS cycles=2 newer-refused=2 missing-refused=2 DOS-restored=2\n");
    bug("RESULT native Manager rollback delegation PASS denied=2 granted=2 revoked=2\n");
    bug("RESULT native mounted incremental receive PASS cycles=2 exact-DOS=1 resumed-wait=1\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/engine-policy");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/engine-policy/child");
    {
        struct oz_message *request=calloc(1,sizeof(*request)),*response=calloc(1,sizeof(*response));
        ZCHECK(request&&response);
        request->magic=OZ_MAGIC;request->version=OZ_VERSION;request->msg.mn_Length=sizeof(*request);
        request->operation=41;request->flags=OZ_CONFIRMED;
        strcpy(request->target,"qemu_mirror/engine-policy");strcpy(request->argument,"copies=2");
        struct oz_ticket ticket={0};ZCHECK(!oz_admission_submit(OZ_ADMISSION_PORT,request,&ticket));
        int error;do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,response);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        ZCHECK(!error&&response->status==OZ_OK);
        ZCHECK(oz_admission_cancel(OZ_ADMISSION_PORT,&ticket)==EALREADY);
        struct oz_ticket stale=ticket;stale.epoch^=1;
        ZCHECK(oz_admission_cancel(OZ_ADMISSION_PORT,&stale)==(stale.epoch?ENOENT:EINVAL));
        ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,response));
        char identity[80];snprintf(identity,sizeof(identity),"%llu:%llu",(unsigned long long)ticket.epoch,(unsigned long long)ticket.number);
        int found=0;for(unsigned i=0;i<response->row_count;i++)if(!strcmp(response->rows[i].name,identity)){
            found=1;ZCHECK(!strcmp(response->rows[i].state,"completed")&&!strcmp(response->rows[i].value,"status=0"));
        }
        ZCHECK(found);
        memset(request,0,sizeof(*request));request->magic=OZ_MAGIC;request->version=OZ_VERSION;
        request->msg.mn_Length=sizeof(*request);request->operation=76;
        struct oz_project_worker *listing=NULL;ZCHECK(!oz_project_worker_start(request,&listing));
        do{error=oz_project_worker_poll(listing,response);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        ZCHECK(!error&&response->status==OZ_OK);ZCHECK(!oz_project_worker_free(listing));
        found=0;for(unsigned i=0;i<response->row_count;i++)if(!strcmp(response->rows[i].name,identity))found=1;
        ZCHECK(found);
        request->operation=77;request->flags=OZ_CONFIRMED;
        const char *cancel_targets[]={"", "1", "0:1", "1:0", "1:2:3", "-1:2",
            "1:18446744073709551616", " 1:2", "1:2 ", identity};
        for(unsigned c=0;c<sizeof(cancel_targets)/sizeof(cancel_targets[0]);c++) {
            snprintf(request->target,sizeof(request->target),"%s",cancel_targets[c]);
            listing=NULL;ZCHECK(!oz_project_worker_start(request,&listing));
            do{error=oz_project_worker_poll(listing,response);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
            ZCHECK(!error&&response->status==OZ_ENGINE_ERROR);
            char expected_error[48];snprintf(expected_error,sizeof(expected_error),"errno=%d",
                c+1==sizeof(cancel_targets)/sizeof(cancel_targets[0])?EALREADY:EINVAL);
            ZCHECK(strstr(response->detail,expected_error));
            ZCHECK(!oz_project_worker_free(listing));
        }
#ifdef OZ_TEST_ADMISSION_LIST_GUI
        bug("Native admission list GUI READY: Advanced/requests, all flags off, submit then close Manager\n");
        ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#endif
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        ZCHECK(oz_admission_cancel(OZ_ADMISSION_PORT,&ticket)==EACCES);
        ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,response));
        for(unsigned i=0;i<response->row_count;i++)ZCHECK(strcmp(response->rows[i].name,identity));
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
        ZCHECK(!oz_admission_release(OZ_ADMISSION_PORT,&ticket));
        ZCHECK(oz_admission_cancel(OZ_ADMISSION_PORT,&ticket)==ENOENT);
        ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,response));
        for(unsigned i=0;i<response->row_count;i++)ZCHECK(strcmp(response->rows[i].name,identity));
        free(request);free(response);
        bug("RESULT native admission list PASS retained=1 completed=1 worker=1 foreign-hidden=1 released-absent=1\n");
    }
#ifdef OZ_TEST_QUEUE_HOLD
    {
        struct oz_message *request=calloc(1,sizeof(*request)),*response=calloc(1,sizeof(*response));
        ZCHECK(request&&response);
        request->magic=OZ_MAGIC;request->version=OZ_VERSION;request->msg.mn_Length=sizeof(*request);
        request->operation=41;request->flags=OZ_CONFIRMED;
        strcpy(request->target,"qemu_mirror/engine-policy");
        strcpy(request->argument,"oz:cancel=must-not-execute");
        struct oz_ticket ticket={0};ZCHECK(!oz_admission_submit(OZ_ADMISSION_PORT,request,&ticket));
        ZCHECK(oz_admission_query(OZ_ADMISSION_PORT,&ticket,response)==EAGAIN);
        request->operation=77;request->argument[0]=0;
        snprintf(request->target,sizeof(request->target),"%llu:%llu",(unsigned long long)ticket.epoch,(unsigned long long)ticket.number);
#ifdef OZ_TEST_QUEUE_GUI
        bug("Queued cancellation GUI READY ticket=%s: Advanced/cancel-job, confirm, close Manager\n",request->target);
        ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
        struct oz_project_worker *cancel=NULL;ZCHECK(!oz_project_worker_start(request,&cancel));
        int error;do{error=oz_project_worker_poll(cancel,response);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        ZCHECK(!error&&response->status==OZ_OK);ZCHECK(!oz_project_worker_free(cancel));
#endif
        ZCHECK(!oz_admission_query(OZ_ADMISSION_PORT,&ticket,response)&&response->status==OZ_CANCELLED);
        ZCHECK(oz_admission_cancel(OZ_ADMISSION_PORT,&ticket)==EALREADY);
        ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,response));
        int found=0;for(unsigned i=0;i<response->row_count;i++)if(!strcmp(response->rows[i].name,request->target)){
            ZCHECK(!strcmp(response->rows[i].state,"cancelled"));found=1;
        }
        ZCHECK(found);ZCHECK(!oz_admission_release(OZ_ADMISSION_PORT,&ticket));
        ZCHECK(oz_admission_query(OZ_ADMISSION_PORT,&ticket,response)==ENOENT);
        free(request);free(response);
        RUN(0,"-","C:zfs","get","-H","-o","value","oz:cancel","qemu_mirror/engine-policy");
        bug("RESULT native queued cancellation PASS worker=1 cancelled=1 retained=1 released=1 property-absent=1\n");
    }
#endif
    RUN(0,"disabled","C:zpool","get","-H","-o","value","feature@large_dnode","qemu_mirror");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy","dnodesize=auto","",ENOTSUP));
    RUN(0,"legacy","C:zfs","get","-H","-o","value","dnodesize","qemu_mirror/engine-policy");
    RUN(0,NULL,"C:zpool","set","feature@large_dnode=enabled","qemu_mirror");
    bug("RESULT native Manager dnodesize feature PASS disabled-refused=1 unchanged=1 enabled=1\n");
    static const char *dnode_values[]={"legacy","auto","1k","2k","4k","8k","16k"};
    for(unsigned i=0;i<7;i++){
        char setting[48];snprintf(setting,sizeof(setting),"dnodesize=%s",dnode_values[i]);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy",setting,"",0));
        RUN(0,dnode_values[i],"C:zfs","get","-H","-o","value","dnodesize","qemu_mirror/engine-policy");
    }
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/engine-policy@dnode-type");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy@dnode-type","dnodesize=auto","",EINVAL));
    ZCHECK(!manager_permission_or_create(42,"qemu_mirror/engine-policy@dnode-type","dnodesize","",EINVAL));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/engine-policy@dnode-type");
    RUN(0,NULL,"C:zfs","create","-s","-V","1M","qemu_mirror/dnode-volume");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/dnode-volume","dnodesize=auto","",EINVAL));
    ZCHECK(!manager_permission_or_create(42,"qemu_mirror/dnode-volume","dnodesize","",EINVAL));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/dnode-volume");
    bug("RESULT native Manager dnodesize values PASS values=7 snapshot-refused=2 volume-refused=2 cleanup=1\n");
    static const char *policy_names[]={"copies","primarycache","secondarycache","logbias","redundant_metadata","checksum","dedup","sync","dnodesize"};
    static const char *policy_values[]={"2","metadata","none","throughput","most","sha256","verify","always","auto"};
    for(unsigned i=0;i<9;i++){
        char setting[96];snprintf(setting,sizeof(setting),"%s=%s",policy_names[i],policy_values[i]);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy",setting,"",0));
        RUN(0,policy_values[i],"C:zfs","get","-H","-o","value",policy_names[i],"qemu_mirror/engine-policy");
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy/child",setting,"",0));
        RUN(0,"local","C:zfs","get","-H","-o","source",policy_names[i],"qemu_mirror/engine-policy/child");
        ZCHECK(!manager_permission_or_create(42,"qemu_mirror/engine-policy/child",policy_names[i],"",0));
        RUN(0,"inherited from qemu_mirror/engine-policy","C:zfs","get","-H","-o","source",policy_names[i],"qemu_mirror/engine-policy/child");
        RUN(0,policy_values[i],"C:zfs","get","-H","-o","value",policy_names[i],"qemu_mirror/engine-policy/child");
        snprintf(setting,sizeof(setting),"%s=invalid",policy_names[i]);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/engine-policy",setting,"",EINVAL));
        RUN(0,policy_values[i],"C:zfs","get","-H","-o","value",policy_names[i],"qemu_mirror/engine-policy");
    }
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=manager_permission_or_create(41,"qemu_mirror/engine-policy","copies=1","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==0xffffffffUL);ZCHECK(!denied);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/engine-policy","user:77","permissions=copies",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int allowed=manager_permission_or_create(41,"qemu_mirror/engine-policy/child","copies=1","",0);
        int inherit_allowed=manager_permission_or_create(42,"qemu_mirror/engine-policy/child","copies","",0);
        int other_denied=manager_permission_or_create(41,"qemu_mirror/engine-policy/child","logbias=latency","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==0xffffffffUL);ZCHECK(!allowed&&!inherit_allowed&&!other_denied);
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/engine-policy","user:77","permissions=copies",0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        denied=manager_permission_or_create(41,"qemu_mirror/engine-policy/child","copies=1","",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==0xffffffffUL);CloseLibrary(secBase);ZCHECK(!denied);
    }
    RUN(0,"2","C:zfs","get","-H","-o","value","copies","qemu_mirror/engine-policy/child");
    bug("RESULT native Manager engine policy delegation PASS set=1 inherit=1 other-denied=1 revoked-denied=1\n");
#ifdef OZ_TEST_ENGINE_POLICY_GUI
    bug("Native engine policy GUI READY: set checksum=fletcher4 on qemu_mirror/engine-policy/child using Manager, then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    RUN(0,"fletcher4","C:zfs","get","-H","-o","value","checksum","qemu_mirror/engine-policy/child");
    RUN(0,"local","C:zfs","get","-H","-o","source","checksum","qemu_mirror/engine-policy/child");
    bug("RESULT native Manager engine policy GUI PASS checksum=fletcher4 source=local CLI-verified=1\n");
#endif
    RUN(0,NULL,"C:zfs","set","mountpoint=OZPOLICY:","qemu_mirror/engine-policy/child");
    unsigned char policy_data[8192],policy_readback[8192];
    for(unsigned i=0;i<sizeof(policy_data);i++)policy_data[i]=(unsigned char)(i*17+i/71);
    for(unsigned file=0;file<2;file++){
        const char *path=file?"OZPOLICY:duplicate":"OZPOLICY:payload";
        BPTR handle=Open((CONST_STRPTR)path,MODE_NEWFILE);ZCHECK(handle);
        for(unsigned block=0;block<16;block++)ZCHECK(Write(handle,policy_data,sizeof(policy_data))==sizeof(policy_data));
        ZCHECK(Flush(handle));ZCHECK(Close(handle));
        handle=Open((CONST_STRPTR)path,MODE_OLDFILE);ZCHECK(handle);
        for(unsigned block=0;block<16;block++){
            ZCHECK(Read(handle,policy_readback,sizeof(policy_readback))==sizeof(policy_readback));
            ZCHECK(!memcmp(policy_data,policy_readback,sizeof(policy_data)));
        }
        ZCHECK(Read(handle,policy_readback,1)==0);ZCHECK(Close(handle));
    }
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/engine-policy/child");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/engine-policy");
    bug("RESULT native Manager engine policy PASS properties=9 set=18 inherit=9 invalid=9 unchanged=9 unauthorized=1 DOS-KiB=256 cleanup=1\n");
#ifdef OZ_TEST_WAIT_PENDING
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    struct oz_message *wait_request=calloc(1,sizeof(*wait_request)),*wait_reply=calloc(1,sizeof(*wait_reply));
    ZCHECK(wait_request&&wait_reply);
    wait_request->magic=OZ_MAGIC;wait_request->version=OZ_VERSION;
    wait_request->msg.mn_Length=sizeof(*wait_request);wait_request->operation=30;
    strcpy(wait_request->target,"qemu_mirror");strcpy(wait_request->options,"activities=scrub");
    struct oz_project_worker *wait_worker=NULL;
    ZCHECK(!oz_project_worker_start(wait_request,&wait_worker));
    Delay(100);
    ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    struct oz_message *live=calloc(1,sizeof(*live));ZCHECK(live);
    ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,live));
    char wait_identity[160]={0};
    for(unsigned i=0;i<live->row_count;i++)if(!strcmp(live->rows[i].kind,"service-worker")&&!strcmp(live->rows[i].value,"wait")){
        ZCHECK(!strcmp(live->rows[i].state,"in-flight"));strcpy(wait_identity,live->rows[i].name);
    }
    ZCHECK(wait_identity[0]);
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase);
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,live));
        for(unsigned i=0;i<live->row_count;i++)ZCHECK(strcmp(live->rows[i].name,wait_identity));
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
    }
    RUN(0,"scrub in progress","C:zpool","status","qemu_mirror");
    ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    RUN(0,NULL,"C:zpool","scrub","-s","qemu_mirror");
    int wait_error;
    do{wait_error=oz_project_worker_poll(wait_worker,wait_reply);if(wait_error==EAGAIN)Delay(1);}while(wait_error==EAGAIN);
    ZCHECK(!wait_error&&wait_reply->status==OZ_OK);
    ZCHECK(!oz_project_worker_free(wait_worker));free(wait_request);free(wait_reply);
    ZCHECK(!oz_admission_list(OZ_ADMISSION_PORT,live));
    for(unsigned i=0;i<live->row_count;i++)ZCHECK(strcmp(live->rows[i].name,wait_identity));
    free(live);
    bug("RESULT native live request list PASS wait-visible=1 in-flight=1 drained-absent=1\n");
    bug("RESULT native live request identity PASS foreign-hidden=1\n");
    bug("RESULT native pending wait PASS pending=1 concurrent-status=1 stop=1 drained=1\n");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZWAITFS:","qemu_mirror/cli-waitfs");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-waitfs");
    const char *wait_files[]={"OZWAITFS:held-deleteq","OZWAITFS:release-deleteq"};
    for(unsigned i=0;i<2;i++) {
        BPTR f=Open((CONST_STRPTR)wait_files[i],MODE_NEWFILE);ZCHECK(f);
        ZCHECK(Write(f,"wait",4)==4);ZCHECK(Close(f));
    }
    ZCHECK(!DeleteFile((CONST_STRPTR)wait_files[0]));
    BPTR gone=Lock((CONST_STRPTR)wait_files[0],ACCESS_READ);
    ZCHECK(!gone&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    wait_request=calloc(1,sizeof(*wait_request));wait_reply=calloc(1,sizeof(*wait_reply));
    ZCHECK(wait_request&&wait_reply);
    wait_request->magic=OZ_MAGIC;wait_request->version=OZ_VERSION;
    wait_request->msg.mn_Length=sizeof(*wait_request);wait_request->operation=49;
    strcpy(wait_request->target,"qemu_mirror/cli-waitfs");
    strcpy(wait_request->options,"activities=deleteq");wait_worker=NULL;
    ZCHECK(!oz_project_worker_start(wait_request,&wait_worker));
    Delay(100);ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    RUN(0,"qemu_mirror/cli-waitfs","C:zfs","list","-H","qemu_mirror/cli-waitfs");
    ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    ZCHECK(DeleteFile((CONST_STRPTR)wait_files[1]));
    for(unsigned i=0;i<500;i++) {
        wait_error=oz_project_worker_poll(wait_worker,wait_reply);
        if(wait_error!=EAGAIN)break;
        Delay(1);
    }
    ZCHECK(!wait_error&&wait_reply->status==OZ_OK&&strstr(wait_reply->detail,"exit=0"));
    ZCHECK(!oz_project_worker_free(wait_worker));free(wait_request);free(wait_reply);
    /* Repeat through the same external CLI path, this time ending the
     * mount while the real deletion queue is still pending. */
    BPTR cancel_file=Open((CONST_STRPTR)wait_files[0],MODE_NEWFILE);ZCHECK(cancel_file);
    ZCHECK(Close(cancel_file));ZCHECK(!DeleteFile((CONST_STRPTR)wait_files[0]));
    wait_request=calloc(1,sizeof(*wait_request));wait_reply=calloc(1,sizeof(*wait_reply));
    ZCHECK(wait_request&&wait_reply);
    wait_request->magic=OZ_MAGIC;wait_request->version=OZ_VERSION;
    wait_request->msg.mn_Length=sizeof(*wait_request);wait_request->operation=49;
    strcpy(wait_request->target,"qemu_mirror/cli-waitfs");
    strcpy(wait_request->options,"activities=deleteq");wait_worker=NULL;
    ZCHECK(!oz_project_worker_start(wait_request,&wait_worker));
    Delay(100);ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-waitfs");
    for(unsigned i=0;i<500;i++) {
        wait_error=oz_project_worker_poll(wait_worker,wait_reply);
        if(wait_error!=EAGAIN)break;
        Delay(1);
    }
    ZCHECK(!wait_error&&wait_reply->status==OZ_ENGINE_ERROR&&
        strstr(wait_reply->detail,"error waiting"));
    ZCHECK(!oz_project_worker_free(wait_worker));free(wait_request);free(wait_reply);
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-waitfs");
    RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-waitfs");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-waitfs@before");
    cancel_file=Open((CONST_STRPTR)wait_files[0],MODE_NEWFILE);ZCHECK(cancel_file);
    ZCHECK(Close(cancel_file));ZCHECK(!DeleteFile((CONST_STRPTR)wait_files[0]));
    wait_request=calloc(1,sizeof(*wait_request));wait_reply=calloc(1,sizeof(*wait_reply));
    ZCHECK(wait_request&&wait_reply);
    wait_request->magic=OZ_MAGIC;wait_request->version=OZ_VERSION;
    wait_request->msg.mn_Length=sizeof(*wait_request);wait_request->operation=49;
    strcpy(wait_request->target,"qemu_mirror/cli-waitfs");
    strcpy(wait_request->options,"activities=deleteq");wait_worker=NULL;
    ZCHECK(!oz_project_worker_start(wait_request,&wait_worker));
    Delay(100);ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    RUN(0,NULL,"C:zfs","rollback","qemu_mirror/cli-waitfs@before");
    for(unsigned i=0;i<500;i++) {
        wait_error=oz_project_worker_poll(wait_worker,wait_reply);
        if(wait_error!=EAGAIN)break;
        Delay(1);
    }
    ZCHECK(!wait_error&&wait_reply->status==OZ_ENGINE_ERROR&&
        strstr(wait_reply->detail,"error waiting"));
    ZCHECK(!oz_project_worker_free(wait_worker));free(wait_request);free(wait_reply);
    RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-waitfs");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-waitfs@before");
    bug("RESULT native pending filesystem rollback PASS pending=1 interrupted=1 rollback=1 resumed-wait=1\n");
    /* Preserve a real incremental stream, rewind its mounted source, and
     * receive it back while an independent CLI waits on a held deletion. */
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-waitfs@recvbase");
    const char received_payload[]="native receive resumes the mounted filesystem";
    BPTR received=Open((CONST_STRPTR)"OZWAITFS:received-data",MODE_NEWFILE);ZCHECK(received);
    ZCHECK(Write(received,received_payload,sizeof(received_payload))==sizeof(received_payload));
    ZCHECK(Close(received));
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-waitfs@recvend");
    char *wait_send[]={"C:zfs","send","-i","qemu_mirror/cli-waitfs@recvbase",
        "qemu_mirror/cli-waitfs@recvend",NULL};
    ZCHECK(!send_to_file(wait_send,"RAM:pending-wait-receive.stream"));
    RUN(0,NULL,"C:zfs","rollback","-r","qemu_mirror/cli-waitfs@recvbase");
    gone=Lock((CONST_STRPTR)"OZWAITFS:received-data",ACCESS_READ);
    ZCHECK(!gone&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    cancel_file=Open((CONST_STRPTR)wait_files[0],MODE_NEWFILE);ZCHECK(cancel_file);
    ZCHECK(Close(cancel_file));ZCHECK(!DeleteFile((CONST_STRPTR)wait_files[0]));
    wait_request=calloc(1,sizeof(*wait_request));wait_reply=calloc(1,sizeof(*wait_reply));
    ZCHECK(wait_request&&wait_reply);
    wait_request->magic=OZ_MAGIC;wait_request->version=OZ_VERSION;
    wait_request->msg.mn_Length=sizeof(*wait_request);wait_request->operation=49;
    strcpy(wait_request->target,"qemu_mirror/cli-waitfs");
    strcpy(wait_request->options,"activities=deleteq");wait_worker=NULL;
    ZCHECK(!oz_project_worker_start(wait_request,&wait_worker));
    Delay(100);ZCHECK(oz_project_worker_poll(wait_worker,wait_reply)==EAGAIN);
    char *wait_receive[]={"C:zfs","receive","-F","qemu_mirror/cli-waitfs",NULL};
    ZCHECK(!receive_from_file(wait_receive,"RAM:pending-wait-receive.stream"));
    for(unsigned i=0;i<500;i++) {
        wait_error=oz_project_worker_poll(wait_worker,wait_reply);
        if(wait_error!=EAGAIN)break;
        Delay(1);
    }
    ZCHECK(!wait_error&&wait_reply->status==OZ_ENGINE_ERROR&&
        strstr(wait_reply->detail,"error waiting"));
    ZCHECK(!oz_project_worker_free(wait_worker));free(wait_request);free(wait_reply);
    RUN(0,NULL,"C:zfs","wait","qemu_mirror/cli-waitfs");
    received=Open((CONST_STRPTR)"OZWAITFS:received-data",MODE_OLDFILE);ZCHECK(received);
    char received_bytes[sizeof(received_payload)+1];
    ZCHECK(Read(received,received_bytes,sizeof(received_bytes))==sizeof(received_payload));
    ZCHECK(!memcmp(received_bytes,received_payload,sizeof(received_payload)));
    ZCHECK(Read(received,received_bytes,1)==0);ZCHECK(Close(received));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-waitfs@recvend");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-waitfs@recvbase");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:pending-wait-receive.stream"));
    bug("RESULT native pending filesystem receive PASS pending=1 interrupted=1 received=1 resumed-wait=1 exact-DOS=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-waitfs");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-waitfs");
    bug("RESULT native pending filesystem wait PASS pending=1 concurrent-list=1 released=1 drained=1\n");
    bug("RESULT native pending filesystem cancel PASS unmount=1 interrupted=1 remount=1\n");
#endif
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","qemu_mirror");
    bug("RESULT native CLI pool wait PASS idle=1 scrub=1 all=1\n");
    RUN(0,NULL,"C:zfs","wait","-t","deleteq","qemu_mirror");
    RUN(0,NULL,"C:zfs","wait","qemu_mirror");
    RUN(1,"does not exist","C:zfs","wait","qemu_mirror/absent-wait-target");
    bug("RESULT native CLI filesystem wait PASS deleteq=1 all=1 absent-refused=1\n");
    ZCHECK(!command_worker_test(30,"qemu_mirror","","activities=scrub,resilver",0,OZ_OK,"exit=0"));
    ZCHECK(!command_worker_test(30,"qemu_mirror","","",0,OZ_OK,"exit=0"));
    const char *bad_wait_options[]={"activities=","activities=scrub,","activities=scrub,scrub","activities=bogus","shell=on"};
    for(unsigned i=0;i<sizeof(bad_wait_options)/sizeof(bad_wait_options[0]);i++)
        ZCHECK(!command_worker_test(30,"qemu_mirror","",bad_wait_options[i],0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(30,"qemu_mirror","","",OZ_FORCE,OZ_ENGINE_ERROR,"before command execution"));
    bug("RESULT native Manager wait PASS selected=1 all=1 malformed=6\n");
    ZCHECK(!command_worker_test(49,"qemu_mirror","","activities=deleteq",0,OZ_OK,"exit=0"));
    ZCHECK(!command_worker_test(49,"qemu_mirror","","",0,OZ_OK,"exit=0"));
    const char *bad_fs_wait[]={"activities=","activities=deleteq,","activities=deleteq,deleteq","activities=scrub","shell=on"};
    for(unsigned i=0;i<sizeof(bad_fs_wait)/sizeof(bad_fs_wait[0]);i++)
        ZCHECK(!command_worker_test(49,"qemu_mirror","",bad_fs_wait[i],0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(49,"qemu_mirror","","",OZ_FORCE,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(49,"qemu_mirror","unexpected","",0,OZ_ENGINE_ERROR,"before command execution"));
    bug("RESULT native Manager filesystem wait PASS selected=1 all=1 malformed=7\n");
    ZCHECK(!command_worker_test(27,"qemu_mirror","","type=ddt",OZ_CONFIRMED,OZ_OK,"exit=0"));
    ZCHECK(!command_worker_test(27,"qemu_mirror","","type=brt",OZ_CONFIRMED,OZ_OK,"exit=0"));
    ZCHECK(!command_worker_test(27,"qemu_mirror","","",OZ_CONFIRMED,OZ_OK,"exit=0"));
    ZCHECK(!command_worker_test(27,"qemu_mirror","","type=bogus",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(27,"qemu_mirror","","",0,OZ_ENGINE_ERROR,"before command execution"));
    const char *bad_prune_options[]={"","percent=0","percent=101","percent=-1","days=0","days=18446744073709551615","percent=1;days=1"};
    for(unsigned i=0;i<sizeof(bad_prune_options)/sizeof(bad_prune_options[0]);i++)
        ZCHECK(!command_worker_test(28,"qemu_mirror","",bad_prune_options[i],OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(28,"qemu_mirror","","percent=100",0,OZ_ENGINE_ERROR,"before command execution"));
    RUN(0,NULL,"C:zpool","set","feature@fast_dedup=enabled","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZPRUNE:","-o","dedup=on","-o","compression=off","-o","recordsize=128K","qemu_mirror/cli-prune");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-prune");
    unsigned char *prune_bytes=malloc(128*1024),*prune_read=malloc(128*1024);ZCHECK(prune_bytes&&prune_read);
    BPTR prune_file;
    const char *prune_paths[]={"OZPRUNE:payload","OZPRUNE:duplicate"};
    for(unsigned file=0;file<2;file++){
        prune_file=Open((CONST_STRPTR)prune_paths[file],MODE_NEWFILE);ZCHECK(prune_file);
        for(unsigned block=0;block<4;block++){
            for(unsigned j=0;j<128*1024;j++)prune_bytes[j]=(unsigned char)(j*17+block*29+1);
            ZCHECK(Write(prune_file,prune_bytes,128*1024)==128*1024);
        }
        ZCHECK(Close(prune_file));
    }
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    /* Scrub waits for fast-dedup logs to flush before walking the DDT.
     * Pruning deliberately skips entries still present in those logs. */
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,"DDT entries ","C:zpool","status","-D","qemu_mirror");
    const char *prune_count=strstr(last_command_output,"DDT entries ");ZCHECK(prune_count);
    char *prune_count_end=NULL;
    unsigned long long prune_before=strtoull(prune_count+12,&prune_count_end,10);
    ZCHECK(prune_before>=4&&prune_count_end&&*prune_count_end==',');
    bug("DDT prune fixture entries-before=%llu\n",prune_before);
    Delay(100); /* Ensure unique entries are older than the pruning cutoff. */
    /* Well-formed but older than the Unix epoch: upstream must reject this
     * without leaving its active-prune state set for subsequent requests. */
    ZCHECK(!command_worker_test(28,"qemu_mirror","","days=999999",OZ_CONFIRMED,OZ_ENGINE_ERROR,"invalid argument"));
    ZCHECK(!command_worker_test(28,"qemu_mirror","","percent=100",OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    /* Pruning itself logs cleared DDT physical records. Flush those too
     * before interpreting aggregate table+log entry counts. */
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,"DDT entries ","C:zpool","status","-D","qemu_mirror");
    prune_count=strstr(last_command_output,"DDT entries ");ZCHECK(prune_count);
    unsigned long long prune_shared=strtoull(prune_count+12,&prune_count_end,10);
    ZCHECK(prune_shared>=4&&prune_shared<=prune_before&&prune_count_end&&*prune_count_end==',');
    bug("DDT prune fixture shared-entries-retained=%llu\n",prune_shared);
    prune_file=Open((CONST_STRPTR)"OZPRUNE:duplicate",MODE_OLDFILE);ZCHECK(prune_file);
    for(unsigned block=0;block<4;block++){
        for(unsigned j=0;j<128*1024;j++)prune_bytes[j]=(unsigned char)(j*17+block*29+1);
        ZCHECK(Read(prune_file,prune_read,128*1024)==128*1024);
        ZCHECK(!memcmp(prune_bytes,prune_read,128*1024));
    }
    ZCHECK(Close(prune_file));ZCHECK(DeleteFile((CONST_STRPTR)"OZPRUNE:duplicate"));
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    Delay(100);
    ZCHECK(!command_worker_test(28,"qemu_mirror","","percent=100",OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,"no DDT entries","C:zpool","status","-D","qemu_mirror");
    ZCHECK(!command_worker_test(28,"qemu_mirror","","days=999999",OZ_CONFIRMED,OZ_ENGINE_ERROR,"invalid argument"));
    ZCHECK(!command_worker_test(28,"qemu_mirror","","days=1",OZ_CONFIRMED,OZ_OK,"exit=0"));
    bug("RESULT native DDT prune recovery PASS populated-invalid=1 valid-after-error=1 empty-invalid=1 valid-after-empty-error=1\n");
    prune_file=Open((CONST_STRPTR)"OZPRUNE:payload",MODE_OLDFILE);ZCHECK(prune_file);
    for(unsigned block=0;block<4;block++){
        for(unsigned j=0;j<128*1024;j++)prune_bytes[j]=(unsigned char)(j*17+block*29+1);
        ZCHECK(Read(prune_file,prune_read,128*1024)==128*1024);
        ZCHECK(!memcmp(prune_bytes,prune_read,128*1024));
    }
    ZCHECK(Close(prune_file));free(prune_bytes);free(prune_read);
    bug("RESULT native DDT prune shared PASS duplicate-written=1 shared-retained=1 duplicate-read=1 duplicate-deleted=1 unique-pruned=1 survivor-read=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-prune");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-prune");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZDIFF:","qemu_mirror/cli-diff");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-diff");
    const char *diff_names[]={"OZDIFF:diff-modified","OZDIFF:diff-old","OZDIFF:diff-removed"};
    for(unsigned i=0;i<3;i++){
        BPTR file=Open((CONST_STRPTR)diff_names[i],MODE_NEWFILE);ZCHECK(file);
        ZCHECK(Write(file,"before",6)==6);ZCHECK(Close(file));
    }
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-diff@before");
    BPTR diff_file=Open((CONST_STRPTR)"OZDIFF:diff-added",MODE_NEWFILE);ZCHECK(diff_file);
    ZCHECK(Write(diff_file,"added",5)==5);ZCHECK(Close(diff_file));
    diff_file=Open((CONST_STRPTR)"OZDIFF:diff-modified",MODE_OLDFILE);ZCHECK(diff_file);
    ZCHECK(Write(diff_file,"modified-content",16)==16);ZCHECK(Close(diff_file));
    ZCHECK(Rename((CONST_STRPTR)"OZDIFF:diff-old",(CONST_STRPTR)"OZDIFF:diff-new"));
    ZCHECK(DeleteFile((CONST_STRPTR)"OZDIFF:diff-removed"));
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-diff@after");
    RUN(0,"diff-added","C:zfs","diff","-H","qemu_mirror/cli-diff@before","qemu_mirror/cli-diff@after");
    ZCHECK(strstr(last_command_output,"+\tOZDIFF:/diff-added\n"));
    ZCHECK(strstr(last_command_output,"M\tOZDIFF:/diff-modified\n"));
    ZCHECK(strstr(last_command_output,"-\tOZDIFF:/diff-removed\n"));
    ZCHECK(strstr(last_command_output,"R\tOZDIFF:/diff-old\tOZDIFF:/diff-new\n"));
    RUN(0,"diff-added","C:zfs","diff","-H","qemu_mirror/cli-diff@before");
    ZCHECK(strstr(last_command_output,"R\tOZDIFF:/diff-old\tOZDIFF:/diff-new\n"));
    RUN(0,"diff-added","C:zfs","diff","-H","qemu_mirror/cli-diff@before","qemu_mirror/cli-diff");
    ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff@before","qemu_mirror/cli-diff@after","scripted=on",0,OZ_OK,"diff-added"));
    ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff@before","","",0,OZ_OK,"diff-added"));
    const char *bad_diff_options[]={"scripted=on;scripted=on","scripted=on;","shell=on","timestamps=off"};
    for(unsigned i=0;i<sizeof(bad_diff_options)/sizeof(bad_diff_options[0]);i++)
        ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff@before","",bad_diff_options[i],0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff@before","","",OZ_FORCE,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff","","",0,OZ_ENGINE_ERROR,"before command execution"));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase&&secIsConfigured());
        struct TagItem diff_quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem diff_reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem diff_root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_reader)==((77UL<<16)|88));
        int prefetch_denied=command_worker_test(27,"qemu_mirror","","type=ddt",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL);
        int prune_denied=command_worker_test(28,"qemu_mirror","","percent=100",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL);
        int denied=command_worker_test(58,"qemu_mirror/cli-diff@before","","",0,OZ_ENGINE_ERROR,NULL);
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_root)==secOWNER_SYSTEM);ZCHECK(!denied&&!prefetch_denied&&!prune_denied);
        RUN(0,NULL,"C:zfs","allow","-u","77","diff","qemu_mirror/cli-diff");
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_reader)==((77UL<<16)|88));
        int delegated=command_worker_test(58,"qemu_mirror/cli-diff@before","","scripted=on",0,OZ_OK,"diff-added");
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_root)==secOWNER_SYSTEM);ZCHECK(!delegated);
        RUN(0,NULL,"C:zfs","unallow","-u","77","diff","qemu_mirror/cli-diff");
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_reader)==((77UL<<16)|88));
        denied=command_worker_test(58,"qemu_mirror/cli-diff@before","","",0,OZ_ENGINE_ERROR,NULL);
        secLogoutA(diff_quiet);ZCHECK(secLoginA(diff_root)==secOWNER_SYSTEM);ZCHECK(!denied);
        CloseLibrary(secBase);
    }
#ifdef OZ_TEST_DIFF_GUI
    bug("Diff GUI READY: compare qemu_mirror/cli-diff@before to qemu_mirror/cli-diff@after; close Manager to continue\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#endif
    for(unsigned n=0;n<128;n++){
        char path[64];snprintf(path,sizeof(path),"OZDIFF:bulk-%03u",n);
        BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);ZCHECK(file);ZCHECK(Close(file));
    }
    ZCHECK(!command_worker_test(58,"qemu_mirror/cli-diff@after","","scripted=on",0,OZ_OK,"Full output: "));
    bug("RESULT native Manager diff report PASS overflow=1 exact-paths=128 user-cleanup=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-diff");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-diff@before");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-diff@after");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-diff");
    bug("RESULT native CLI diff PASS snapshots=2 added-path=1 cleanup=1\n");
    bug("RESULT native CLI diff changes PASS added=1 modified=1 renamed=1 removed=1\n");
    bug("RESULT native CLI diff live PASS implicit=1 explicit-filesystem=1 cleanup=1\n");
    bug("RESULT native Manager diff worker PASS snapshots=1 live=1 malformed=6 cleanup=1\n");
    bug("RESULT native Manager diff delegation PASS denied=1 diff-only=1 revoked=1 cleanup=1\n");
    bug("RESULT native Manager prefetch PASS ddt=1 brt=1 both=1 invalid=1 unconfirmed=1 nonroot-denied=1\n");
    bug("RESULT native Manager ddtprune PASS populated=1 entries-after=0 exact-KiB=512 days-empty=1 invalid=7 unconfirmed=1 nonroot-denied=1 cleanup=1\n");
#endif
#define ZSET(op,desc) do{memset(&packet.q,0,sizeof(packet.q));packet.q.session=session;packet.q.version=OZ_ZVOL_VERSION;packet.q.operation=op;packet.q.descriptor=desc;}while(0)
#define ZCALL(wanted) do{int e=oz_zvol_request(OZ_ADMISSION_PORT,&packet,sizeof(packet.q)+(packet.q.operation==OZ_ZVOL_DISCARD?0:packet.q.count));ZCHECK(!e&&packet.q.engine_error==(wanted));}while(0)
#ifdef OZ_TEST_ZVOL_DISCARD_COLD
#ifdef OZ_TEST_ENCRYPTION_COLD
    unsigned char cold_key[32],cold_bytes[512];
    for(unsigned i=0;i<sizeof(cold_key);i++)cold_key[i]=(unsigned char)(i+1);
    BPTR cold_file=Open((CONST_STRPTR)"RAM:openzfs-cli-test.key",MODE_NEWFILE);ZCHECK(cold_file);
    ZCHECK(Write(cold_file,cold_key,sizeof(cold_key))==sizeof(cold_key));ZCHECK(Close(cold_file));
    cold_key[0]^=0x55;
    cold_file=Open((CONST_STRPTR)"RAM:openzfs-cli-new.key",MODE_NEWFILE);ZCHECK(cold_file);
    ZCHECK(Write(cold_file,cold_key,sizeof(cold_key))==sizeof(cold_key));ZCHECK(Close(cold_file));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(-1,"Incorrect key","C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","load-key","-n","qemu_mirror/cli-encrypted");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","load-key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-encrypted");
    cold_file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_OLDFILE);ZCHECK(cold_file);
    ZCHECK(Read(cold_file,cold_bytes,sizeof(cold_bytes))==sizeof(cold_bytes));
    ZCHECK(!memcmp(cold_bytes,expected,sizeof(expected)));ZCHECK(Close(cold_file));
    ZCHECK(!Open((CONST_STRPTR)"OZCRYPT:denied",MODE_NEWFILE));
    RUN(-1,NULL,"C:zfs","change-key","-o","keylocation=file:///RAM/openzfs-cli-test.key","qemu_mirror/cli-encrypted");
    RUN(0,"file:///RAM/openzfs-cli-new.key","C:zfs","get","-H","-o","value","keylocation","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/cli-encrypted");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(-1,"Incorrect key","C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","load-key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/cli-encrypted");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-test.key"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-new.key"));
    bug("RESULT native CLI encryption cold PASS locked=1 old-key-denied=1 verify-only=1 new-key=1 exact-read=1 readonly=1 unload=1\n");
    const char *cold_formats[]={"hex","passphrase","raw"};
    for(unsigned variant=0;variant<3;variant++){
        char dataset[96],hex_key[65];
        const char *phrase="Public AROS encrypted creation fixture";
        for(unsigned i=0;i<sizeof(cold_key);i++){cold_key[i]=(unsigned char)(i+1);snprintf(hex_key+2*i,3,"%02x",cold_key[i]);}
        const void *material=variant==0?(const void *)hex_key:variant==1?(const void *)phrase:(const void *)cold_key;
        size_t size=variant==0?64:variant==1?strlen(phrase):sizeof(cold_key);
        snprintf(dataset,sizeof(dataset),"qemu_mirror/api-format-%s",cold_formats[variant]);
        cold_file=Open((CONST_STRPTR)"RAM:openzfs-api-format.key",MODE_NEWFILE);ZCHECK(cold_file);
        ZCHECK(Write(cold_file,material,size)==size);ZCHECK(Close(cold_file));
        RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus",dataset);
        RUN(0,cold_formats[variant],"C:zfs","get","-H","-o","value","keyformat",dataset);
        RUN(0,"prompt","C:zfs","get","-H","-o","value","keylocation",dataset);
        RUN(0,NULL,"C:zfs","load-key","-n","-L","file:///RAM/openzfs-api-format.key",dataset);
        RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus",dataset);
        RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-api-format.key",dataset);
        if(variant<2){
            RUN(0,NULL,"C:zfs","mount",dataset);
            cold_file=Open((CONST_STRPTR)"OZCRYPT:format-payload",MODE_OLDFILE);ZCHECK(cold_file);
            ZCHECK(Read(cold_file,cold_bytes,sizeof(cold_bytes))==sizeof(cold_bytes));
            ZCHECK(!memcmp(cold_bytes,expected,sizeof(expected)));ZCHECK(Close(cold_file));
            ZCHECK(!Open((CONST_STRPTR)"OZCRYPT:denied",MODE_NEWFILE));
            RUN(0,NULL,"C:zfs","unmount",dataset);
        }else{
            ZCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
            ZSET(OZ_ZVOL_OPEN,-1);strcpy(packet.q.name,dataset);ZCALL(0);fd=packet.q.descriptor;
            ZSET(OZ_ZVOL_READ,fd);packet.q.offset=512;packet.q.count=sizeof(expected);
            memset(packet.bytes,0,sizeof(expected));ZCALL(0);
            ZCHECK(packet.q.completed==sizeof(expected)&&!memcmp(packet.bytes,expected,sizeof(expected)));
            ZSET(OZ_ZVOL_WRITE,fd);packet.q.offset=512;packet.q.count=sizeof(expected);
            memcpy(packet.bytes,expected,sizeof(expected));ZCALL(EROFS);ZCHECK(!packet.q.completed);
            ZSET(OZ_ZVOL_CLOSE,fd);ZCALL(0);ZCHECK(!oz_session_close(OZ_ADMISSION_PORT,&session));
        }
        RUN(0,NULL,"C:zfs","unload-key",dataset);
        ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-api-format.key"));
    }
    bug("RESULT native API encryption cold PASS hex=1 passphrase=1 volume=1 verify-only=3 exact-read=3 readonly=3\n");
    cold_file=Open((CONST_STRPTR)"RAM:openzfs-raw-recovery.key",MODE_NEWFILE);ZCHECK(cold_file);
    ZCHECK(Write(cold_file,cold_key,sizeof(cold_key))==sizeof(cold_key));ZCHECK(Close(cold_file));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-raw-recovery.key","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/raw-received");
    cold_file=Open((CONST_STRPTR)"OZRAW:incremental-payload",MODE_OLDFILE);ZCHECK(cold_file);
    ZCHECK(Read(cold_file,cold_bytes,sizeof(cold_bytes))==sizeof(cold_bytes));ZCHECK(Close(cold_file));
    for(unsigned i=0;i<sizeof(cold_bytes);i++)ZCHECK(cold_bytes[i]==(unsigned char)(expected[i]^0xa5));
    ZCHECK(!Open((CONST_STRPTR)"OZRAW:denied",MODE_NEWFILE));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","list","-t","snapshot","qemu_mirror/raw-received@raw","qemu_mirror/raw-received@next");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-recovery.key"));
    bug("RESULT native raw receive cold PASS locked=1 original-key=1 incremental-bytes=1 snapshots=2 readonly=1\n");
#endif
#ifdef OZ_TEST_PROJECT_COLD
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/project-packets");
    RUN(0,"1003 P","C:zfs","project","-d","OZPROJECT:");
    RUN(0,"1003 P","C:zfs","project","OZPROJECT:child");
    BPTR cold_project_lock=Lock((CONST_STRPTR)"OZPROJECT:child",SHARED_LOCK);ZCHECK(cold_project_lock);
    struct MsgPort *cold_project_port=((struct FileLock *)BADDR(cold_project_lock))->fl_Task;
    struct oz_project_request cold_project={.version=OZ_PROJECT_VERSION,.operation=OZ_PROJECT_GET};
    ZCHECK(DoPkt(cold_project_port,OZ_ACTION_PROJECT,(SIPTR)cold_project_lock,(SIPTR)&cold_project,sizeof(cold_project),0,0)==DOSTRUE);
    ZCHECK(cold_project.project==1003&&cold_project.inherit==1);
    UnLock(cold_project_lock);
    RUN(-1,"Read-only file system","C:zfs","project","-p","1004","OZPROJECT:child");
    RUN(0,"1003 P","C:zfs","project","OZPROJECT:child");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/project-packets");
    bug("RESULT native project cold PASS root=1 child=1 inheritance=1 packet-readback=1 readonly=1\n");
#endif
#ifdef OZ_TEST_ACCOUNTING_COLD
#ifdef OZ_TEST_ACCOUNTING_GUI
    bug("Accounting GUI READY: qemu_mirror/accounting; inspect user quota pages and group/project usage, then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#endif
    ZCHECK(!accounting_matrix());
    ZCHECK(!accounting_default_reads());
    RUN(0,"4194304","C:zfs","get","-Hp","-o","value","defaultuserquota","qemu_mirror/accounting");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userquota@1000=2M","",EROFS));
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","defaultuserquota=8M","",EROFS));
    RUN(0,"102","C:zfs","get","-Hp","-o","value","defaultgroupobjquota","qemu_mirror/accounting");
    RUN(0,"3145728","C:zfs","get","-Hp","-o","value","projectquota@1000","qemu_mirror/accounting");
    RUN(0,"1000\t1048576\t7","C:zfs","userspace","-Hnp","-o","name,quota,objquota","qemu_mirror/accounting");
    for(unsigned id=1000;id<1030;id++){
        char entry[48];snprintf(entry,sizeof(entry),"%u\t1048576\t",id);
        ZCHECK(strstr(last_command_output,entry));
    }
    RUN(0,"1000\t2097152\t8","C:zfs","groupspace","-Hnp","-o","name,quota,objquota","qemu_mirror/accounting");
    RUN(0,"1000\t3145728\t9","C:zfs","projectspace","-Hp","-o","name,quota,objquota","qemu_mirror/accounting");
    bug("RESULT native accounting CLI PASS single=1 user-rows=30 group=1 project=1 objectquotas=3\n");
    bug("RESULT native accounting cold PASS metrics=12 defaults=6 cli-readback=3 readonly-query=1\n");
#endif
#ifdef OZ_TEST_PERMISSION_COLD
    ZCHECK(!permission_list_boundaries());
    ZCHECK(!permission_list_test(5));
    RUN(0,"Permission sets:","C:zfs","allow","qemu_mirror/named-permissions");
    ZCHECK(strstr(last_command_output,"\t@base mount\n")&&strstr(last_command_output,"\t@outer @base\n"));
    ZCHECK(strstr(last_command_output,"Local+Descendent permissions:\n"));
    ZCHECK(permission_line("\tuser ozreader @outer")||permission_line("\tuser 77 @outer"));
    ZCHECK(!strstr(last_command_output,"Create time permissions:")&&!strstr(last_command_output,"create,")&&!strstr(last_command_output,",create"));
    RUN(0,NULL,"C:zfs","allow","qemu_mirror/named-permissions/first");
    ZCHECK(permission_line("\tuser ozreader snapshot")||permission_line("\tuser 77 snapshot"));
    RUN(0,NULL,"C:zfs","allow","qemu_mirror/named-permissions/second");
    ZCHECK(!strstr(last_command_output,"snapshot")&&!strstr(last_command_output,"Create time permissions:"));
    RUN(0,NULL,"C:zfs","list","qemu_mirror/named-permissions/first@owned");
    RUN(0,NULL,"C:zfs","list","qemu_mirror/named-permissions/first@retained");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/second@denied");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/third");
    bug("RESULT native permissions cold PASS sets=1 acquired-grant=1 removed-creator=1 refused-objects-absent=1\n");
#endif
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror","user:77","permissions=create,mount",EROFS));
    ZCHECK(!manager_permission_or_create(66,"qemu_mirror","user:77","permissions=create,mount",EROFS));
    ZCHECK(!manager_permission_or_create(66,"qemu_mirror","user:77","all=on",EROFS));
    bug("RESULT native permission revoke all readonly PASS refused=1\n");
    bug("RESULT native Manager permission readonly PASS allow-denied=1 unallow-denied=1\n");
    ZCHECK(!manager_volume_create_target("qemu_mirror/readonly-create-refused","type=volume,volsize=2M",EROFS));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/readonly-create-refused");
    bug("RESULT native Manager volume readonly creation PASS refused=1 absent=1\n");
    ZCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
    ZSET(OZ_ZVOL_OPEN,-1);strcpy(packet.q.name,"qemu_mirror/transport-volume");ZCALL(0);fd=packet.q.descriptor;
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=sizeof(packet.bytes);memset(packet.bytes,0xa5,sizeof(packet.bytes));ZCALL(0);
    ZCHECK(packet.q.completed==sizeof(packet.bytes));
    for(unsigned i=0;i<sizeof(packet.bytes);i++)ZCHECK(packet.bytes[i]==(i>=8192&&i<16384?0:(unsigned char)(i*7)));
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.count=8192;ZCALL(EROFS);ZCHECK(!packet.q.completed);
    ZCHECK(!oz_session_close(OZ_ADMISSION_PORT,&session));
    bug("RESULT native zvol discard cold PASS zero-block=1 exact-neighbors=1 readonly-denied=1\n");
#else
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ZCHECK(secBase&&secIsConfigured());
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int create_denied=manager_volume_create("type=volume,volsize=2M,volblocksize=8K",EPERM);
    int bootfs_denied=manager_permission_or_create(32,"qemu_mirror","bootfs=","",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!create_denied);
    ZCHECK(!bootfs_denied);
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/bootfs-test");
    ZCHECK(!manager_permission_or_create(32,"qemu_mirror","bootfs=qemu_mirror/bootfs-test","",0));
    RUN(0,"qemu_mirror/bootfs-test","C:zpool","get","-Hp","-o","value","bootfs","qemu_mirror");
    ZCHECK(!manager_permission_or_create(32,"qemu_mirror","bootfs=qemu_mirror_other/bootfs-test","",EINVAL));
    ZCHECK(!manager_permission_or_create(32,"qemu_mirror","bootfs=qemu_mirror/missing-bootfs","",ENOENT));
    RUN(0,"qemu_mirror/bootfs-test","C:zpool","get","-Hp","-o","value","bootfs","qemu_mirror");
    ZCHECK(!manager_permission_or_create(32,"qemu_mirror","bootfs=","",0));
    RUN(0,"-","C:zpool","get","-Hp","-o","value","bootfs","qemu_mirror");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/bootfs-test");
    bug("RESULT native Manager bootfs PASS set=1 readback=1 clear=1 cross-pool-refused=1 missing-preserves=1 unprivileged-refused=1\n");
    ZCHECK(!manager_volume_create_target("qemu_mirror/create-limits",
        "quota=32M,refquota=16M,reservation=1M,refreservation=1M,recordsize=16K",0));
    RUN(0,"33554432","C:zfs","get","-Hp","-o","value","quota","qemu_mirror/create-limits");
    RUN(0,"16777216","C:zfs","get","-Hp","-o","value","refquota","qemu_mirror/create-limits");
    RUN(0,"1048576","C:zfs","get","-Hp","-o","value","reservation","qemu_mirror/create-limits");
    RUN(0,"1048576","C:zfs","get","-Hp","-o","value","refreservation","qemu_mirror/create-limits");
    RUN(0,"16384","C:zfs","get","-Hp","-o","value","recordsize","qemu_mirror/create-limits");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/create-limits");
    ZCHECK(!manager_volume_create_target("qemu_mirror/create-limits","quota=none,refreservation=none",0));
    RUN(0,"0","C:zfs","get","-Hp","-o","value","quota","qemu_mirror/create-limits");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/create-limits");
    ZCHECK(!manager_volume_create_target("qemu_mirror/create-limits","recordsize=3K",EINVAL));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/create-limits");
    /* Valid syntax, but exceeds this isolated pool: exercise upstream
     * post-create property failure and dataset cleanup, not parser refusal. */
    ZCHECK(!manager_volume_create_target("qemu_mirror/create-limits","reservation=1T",ENOSPC));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/create-limits");
    ZCHECK(!manager_volume_create_target("qemu_mirror/create-limits","reservation=none",0));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/create-limits");
    bug("RESULT native Manager creation cleanup PASS insufficient-space=1 absent=1 same-name-retry=1\n");
    bug("RESULT native Manager creation limits PASS quota=1 refquota=1 reservation=1 refreservation=1 recordsize=1 none=1 invalid-absent=1\n");
    /* Public deterministic test material, never a user key. Exercise the real
     * libzfs crypto CREATE path rather than the fixture's direct SPA helpers. */
    unsigned char crypto_key[32];for(unsigned i=0;i<sizeof(crypto_key);i++)crypto_key[i]=(unsigned char)(i+1);
    BPTR crypto_file=Open((CONST_STRPTR)"RAM:openzfs-cli-test.key",MODE_NEWFILE);ZCHECK(crypto_file);
    ZCHECK(Write(crypto_file,crypto_key,sizeof(crypto_key))==sizeof(crypto_key));ZCHECK(Close(crypto_file));
    RUN(0,NULL,"C:zpool","set","feature@encryption=enabled","qemu_mirror");
    ZCHECK(!manager_permission_or_create(37,"qemu_mirror/gui-crypt-bad","RAM:openzfs-cli-test.key",
        "encryption=aes-256-gcm",EINVAL));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/gui-crypt-bad");
    ZCHECK(!manager_permission_or_create(37,"qemu_mirror/gui-crypt-off","RAM:openzfs-cli-test.key",
        "encryption=off,keyformat=raw",EINVAL));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/gui-crypt-off");
    const unsigned bad_key_lengths[]={31,33,63,64,7,513};
    const char *bad_key_formats[]={"raw","raw","hex","hex","passphrase","passphrase"};
    for(unsigned i=0;i<6;i++){
        unsigned char bad_material[513];char bad_options[96];
        memset(bad_material,'a',sizeof(bad_material));if(i==3)bad_material[7]='g';
        BPTR bad_file=Open((CONST_STRPTR)"RAM:openzfs-invalid.key",MODE_NEWFILE);ZCHECK(bad_file);
        ZCHECK(Write(bad_file,bad_material,bad_key_lengths[i])==bad_key_lengths[i]);ZCHECK(Close(bad_file));
        snprintf(bad_options,sizeof(bad_options),"encryption=on,keyformat=%s",bad_key_formats[i]);
        ZCHECK(!manager_permission_or_create(37,"qemu_mirror/bad-key-material",
            "RAM:openzfs-invalid.key",bad_options,EINVAL));
        RUN(1,NULL,"C:zfs","list","qemu_mirror/bad-key-material");
        ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-invalid.key"));
    }
    ZCHECK(!manager_permission_or_create(37,"qemu_mirror/bad-key-material",
        "RAM:openzfs-cli-test.key","encryption=on,keyformat=raw",0));
    RUN(0,"available","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/bad-key-material");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/bad-key-material");
    bug("RESULT native encryption invalid material PASS raw-length=2 hex=2 passphrase-length=2 absent=6 valid-retry=1\n");
    const char *bad_iterations[]={
        "encryption=on,keyformat=passphrase,pbkdf2iters=99999",
        "encryption=on,keyformat=passphrase,pbkdf2iters=4294967296",
        "encryption=on,keyformat=raw,pbkdf2iters=350000"};
    for(unsigned i=0;i<3;i++){
        ZCHECK(!manager_permission_or_create(37,"qemu_mirror/gui-crypt-bad-iters",
            "RAM:openzfs-cli-test.key",bad_iterations[i],EINVAL));
        RUN(1,NULL,"C:zfs","list","qemu_mirror/gui-crypt-bad-iters");
    }
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int encrypted_create_denied=manager_permission_or_create(37,"qemu_mirror/gui-crypt-denied",
        "RAM:openzfs-cli-test.key","encryption=aes-256-gcm,keyformat=raw",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!encrypted_create_denied);
    RUN(1,NULL,"C:zfs","list","qemu_mirror/gui-crypt-denied");
#ifdef OZ_TEST_ENCRYPTED_CREATE_GUI
    bug("Native encrypted creation GUI READY: create qemu_mirror/gui-crypt with encryption=on,keyformat=raw and RAM:openzfs-cli-test.key; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_permission_or_create(37,"qemu_mirror/gui-crypt","RAM:openzfs-cli-test.key",
        "encryption=on,keyformat=raw",0));
#endif
    RUN(0,"aes-256-gcm","C:zfs","get","-H","-o","value","encryption","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/gui-crypt","copies=3","",EINVAL));
    RUN(0,"1","C:zfs","get","-H","-o","value","copies","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/gui-crypt","copies=2","",0));
    RUN(0,"2","C:zfs","get","-H","-o","value","copies","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/gui-crypt","copies=1","",0));
    bug("RESULT native Manager encrypted copies PASS copies3-refused=1 unchanged=1 copies2=1 restored=1\n");
    RUN(0,"prompt","C:zfs","get","-H","-o","value","keylocation","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZCRYPT:","qemu_mirror/gui-crypt");
    /* Upstream property postfix processing mounts the new mountpoint. */
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/gui-crypt");
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:api-payload",MODE_NEWFILE);ZCHECK(crypto_file);
    ZCHECK(Write(crypto_file,expected,sizeof(expected))==sizeof(expected));ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
    /* Multiple uncompressed records ensure truncation happens after progress,
     * not merely in the stream header. This is disposable fixture data. */
    RUN(0,NULL,"C:zfs","set","compression=off","qemu_mirror/gui-crypt");
    unsigned char resume_block[8192];
    for(unsigned i=0;i<sizeof(resume_block);i++)resume_block[i]=(unsigned char)(i*37u+i/251u);
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:resume-payload",MODE_NEWFILE);ZCHECK(crypto_file);
    for(unsigned i=0;i<128;i++)ZCHECK(Write(crypto_file,resume_block,sizeof(resume_block))==sizeof(resume_block));
    ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/gui-crypt@raw");
    RUN(0,"size\t","C:zfs","send","-nP","qemu_mirror/gui-crypt@raw");
    ZCHECK(strtoull(strstr(last_command_output,"size\t")+5,NULL,10)>1048576);
    /* Both stream output and input use DOS served by this same engine. */
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZSTREAM:","qemu_mirror/cli-stream-storage");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-stream-storage");
    char *cli_plain_send[]={"C:zfs","send","qemu_mirror/gui-crypt@raw",NULL};
    char *cli_plain_receive[]={"C:zfs","receive","-u","qemu_mirror/cli-plain-received",NULL};
    ZCHECK(!send_to_file(cli_plain_send,"OZSTREAM:plain.stream"));
    ZCHECK(!receive_from_file(cli_plain_receive,"OZSTREAM:plain.stream"));
    RUN(0,"off","C:zfs","get","-H","-o","value","encryption","qemu_mirror/cli-plain-received");
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/cli-plain-received");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZPLAINCLI:","qemu_mirror/cli-plain-received");
    unsigned char plain_read[8192];
    BPTR plain_lock=Lock((CONST_STRPTR)"OZPLAINCLI:resume-payload",SHARED_LOCK);ZCHECK(plain_lock);
    BPTR plain_file=OpenFromLock(plain_lock);
    if(!plain_file)UnLock(plain_lock);
    ZCHECK(plain_file);ZCHECK(!IsInteractive(plain_file));
    for(unsigned i=0;i<128;i++) {
        ZCHECK(Read(plain_file,plain_read,sizeof(plain_read))==sizeof(plain_read));
        ZCHECK(!memcmp(plain_read,resume_block,sizeof(plain_read)));
    }
    ZCHECK(Read(plain_file,plain_read,1)==0);ZCHECK(Close(plain_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-plain-received");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/gui-crypt");
    BPTR incremental_file=Open((CONST_STRPTR)"OZCRYPT:cli-incremental",MODE_NEWFILE);ZCHECK(incremental_file);
    for(unsigned i=0;i<16;i++)ZCHECK(Write(incremental_file,resume_block,sizeof(resume_block))==sizeof(resume_block));
    ZCHECK(Flush(incremental_file));ZCHECK(Close(incremental_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/gui-crypt@cli-incremental");
    char *cli_incremental_send[]={"C:zfs","send","-i","qemu_mirror/gui-crypt@raw","qemu_mirror/gui-crypt@cli-incremental",NULL};
    ZCHECK(!send_to_file(cli_incremental_send,"OZSTREAM:incremental.stream"));
    ZCHECK(!receive_from_file(cli_plain_receive,"OZSTREAM:incremental.stream"));
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-plain-received");
    incremental_file=Open((CONST_STRPTR)"OZPLAINCLI:cli-incremental",MODE_OLDFILE);ZCHECK(incremental_file);
    for(unsigned i=0;i<16;i++) {
        ZCHECK(Read(incremental_file,plain_read,sizeof(plain_read))==sizeof(plain_read));
        ZCHECK(!memcmp(plain_read,resume_block,sizeof(plain_read)));
    }
    ZCHECK(Read(incremental_file,plain_read,1)==0);ZCHECK(Close(incremental_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-plain-received");
    ZCHECK(DeleteFile((CONST_STRPTR)"OZSTREAM:incremental.stream"));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt@cli-incremental");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/gui-crypt");
    ZCHECK(DeleteFile((CONST_STRPTR)"OZCRYPT:cli-incremental"));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    bug("RESULT native CLI incremental stream PASS own-volume=1 exact-128KiB=1\n");
    /* Interrupt an actual CLI stream after multiple uncompressed records,
     * then use the upstream receive token to resume it through the CLI. */
    BPTR stream_source=Open((CONST_STRPTR)"OZSTREAM:plain.stream",MODE_OLDFILE);ZCHECK(stream_source);
    BPTR stream_cut=Open((CONST_STRPTR)"OZSTREAM:cut.stream",MODE_NEWFILE);ZCHECK(stream_cut);
    for(unsigned i=0;i<64;i++) {
        ZCHECK(Read(stream_source,plain_read,sizeof(plain_read))==sizeof(plain_read));
        ZCHECK(Write(stream_cut,plain_read,sizeof(plain_read))==sizeof(plain_read));
    }
    ZCHECK(Close(stream_source));ZCHECK(Flush(stream_cut));ZCHECK(Close(stream_cut));
    char *cli_resume_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/cli-resumed",NULL};
    ZCHECK(!receive_from_file_expected(cli_resume_receive,"OZSTREAM:cut.stream",1));
    RUN(0,NULL,"C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-resumed");
    char *cli_resume_token=strdup(last_command_output);ZCHECK(cli_resume_token);
    cli_resume_token[strcspn(cli_resume_token,"\r\n")]=0;
    ZCHECK(strlen(cli_resume_token)>16&&strcmp(cli_resume_token,"-"));
    char *cli_resume_send[]={"C:zfs","send","-t",cli_resume_token,NULL};
    ZCHECK(!send_to_file(cli_resume_send,"OZSTREAM:resume.stream"));
    free(cli_resume_token);
    ZCHECK(!receive_from_file(cli_resume_receive,"OZSTREAM:resume.stream"));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-resumed");
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/cli-resumed");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZRESUMECLI:","qemu_mirror/cli-resumed");
    plain_file=Open((CONST_STRPTR)"OZRESUMECLI:resume-payload",MODE_OLDFILE);ZCHECK(plain_file);
    for(unsigned i=0;i<128;i++) {
        ZCHECK(Read(plain_file,plain_read,sizeof(plain_read))==sizeof(plain_read));
        ZCHECK(!memcmp(plain_read,resume_block,sizeof(plain_read)));
    }
    ZCHECK(Read(plain_file,plain_read,1)==0);ZCHECK(Close(plain_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-resumed");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-resumed");
    ZCHECK(DeleteFile((CONST_STRPTR)"OZSTREAM:cut.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"OZSTREAM:resume.stream"));
    bug("RESULT native CLI resumed stream PASS truncated=1 token=1 own-volume=1 exact-MiB=1\n");
#ifndef OZ_TEST_PIPE_LEGACY
    ZCHECK(!pipe_partial_close());
#endif
    for(unsigned pipeline_run=0;pipeline_run<8;pipeline_run++) {
    ZCHECK(!cli_pipeline(0));
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/cli-pipeline");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZPIPECLI:","qemu_mirror/cli-pipeline");
    plain_file=Open((CONST_STRPTR)"OZPIPECLI:resume-payload",MODE_OLDFILE);ZCHECK(plain_file);
    for(unsigned i=0;i<128;i++) {
        ZCHECK(Read(plain_file,plain_read,sizeof(plain_read))==sizeof(plain_read));
        ZCHECK(!memcmp(plain_read,resume_block,sizeof(plain_read)));
    }
    ZCHECK(Read(plain_file,plain_read,1)==0);ZCHECK(Close(plain_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-pipeline");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-pipeline");
    bug("Native CLI pipeline completed run=%u\n",pipeline_run+1);
    }
    bug("RESULT native CLI pipeline PASS concurrent=1 exact-MiB=1\n");
    bug("RESULT native CLI pipeline repeat PASS runs=8 exact-MiB=1 reuse-pipe=1\n");
    ZCHECK(!cli_pipeline(1));
#ifdef OZ_TEST_PIPE_LEGACY
    bug("RESULT native CLI pipeline legacy PASS reader-reopen=1 sender-success=1\n");
#else
    bug("RESULT native CLI pipeline reader-close PASS sender-failed=1\n");
#endif
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-plain-received");
    ZCHECK(DeleteFile((CONST_STRPTR)"OZSTREAM:plain.stream"));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-stream-storage");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-stream-storage");
    bug("RESULT native CLI plain stream PASS modern-receive=1 own-volume=1 encryption-off=1 exact-MiB=1\n");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/gui-crypt");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@raw","RAM:openzfs-raw.stream","raw=on",0));
    char *cli_raw_send[]={"C:zfs","send","-w","qemu_mirror/gui-crypt@raw",NULL};
    ZCHECK(!send_to_file(cli_raw_send,"RAM:openzfs-cli-raw.stream"));
    RUN(0,"size\t","C:zfs","send","-nP","-w","qemu_mirror/gui-crypt@raw");
    ZCHECK(strtoull(strstr(last_command_output,"size\t")+5,NULL,10)>1048576);
    BPTR delegated_stream=Open((CONST_STRPTR)"RAM:openzfs-delegated-raw.stream",MODE_NEWFILE);ZCHECK(delegated_stream);ZCHECK(Close(delegated_stream));
    ZCHECK(SetOwner((CONST_STRPTR)"RAM:openzfs-delegated-raw.stream",(77UL<<16)|88));
    RUN(0,NULL,"C:zfs","allow","-u","ozreader","send:raw","qemu_mirror/gui-crypt");
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int plaintext_denied=manager_permission_or_create(60,"qemu_mirror/gui-crypt@raw",
        "RAM:openzfs-delegated-raw.stream","raw=off",EPERM);
    int delegated_raw=manager_permission_or_create(60,"qemu_mirror/gui-crypt@raw",
        "RAM:openzfs-delegated-raw.stream","raw=on",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!plaintext_denied&&!delegated_raw);
    RUN(0,NULL,"C:zfs","unallow","-u","ozreader","send:raw","qemu_mirror/gui-crypt");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-delegated-raw.stream"));
    bug("RESULT native raw send delegation PASS raw-grant=1 plaintext-denied=1\n");
    char *cli_raw_receive[]={"C:zfs","receive","-u","qemu_mirror/raw-received",NULL};
    ZCHECK(!receive_from_file(cli_raw_receive,"RAM:openzfs-cli-raw.stream"));
    RUN(0,"aes-256-gcm","C:zfs","get","-H","-o","value","encryption","qemu_mirror/raw-received");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/raw-received");
    /* Verification must not dirty the receive target (including atime)
     * before a non-forced incremental receive against its base snapshot. */
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZRAW:","qemu_mirror/raw-received");
    unsigned char raw_read[512];
    BPTR raw_file=Open((CONST_STRPTR)"OZRAW:api-payload",MODE_OLDFILE);ZCHECK(raw_file);
    ZCHECK(Read(raw_file,raw_read,sizeof(raw_read))==sizeof(raw_read));ZCHECK(!memcmp(raw_read,expected,sizeof(expected)));ZCHECK(Close(raw_file));
    unsigned char cli_stream_read[8192];
    raw_file=Open((CONST_STRPTR)"OZRAW:resume-payload",MODE_OLDFILE);ZCHECK(raw_file);
    for(unsigned i=0;i<128;i++) {
        ZCHECK(Read(raw_file,cli_stream_read,sizeof(cli_stream_read))==sizeof(cli_stream_read));
        ZCHECK(!memcmp(cli_stream_read,resume_block,sizeof(cli_stream_read)));
    }
    ZCHECK(Read(raw_file,cli_stream_read,1)==0);ZCHECK(Close(raw_file));
    bug("RESULT native CLI raw stream PASS locked-source=1 upstream-send=1 CLI-receive=1 original-key=1 exact-MiB=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror/raw-received");
    BPTR resume_input=Open((CONST_STRPTR)"RAM:openzfs-cli-raw.stream",MODE_OLDFILE);ZCHECK(resume_input);
    BPTR resume_cut=Open((CONST_STRPTR)"RAM:openzfs-raw-cut.stream",MODE_NEWFILE);ZCHECK(resume_cut);
    unsigned char resume_bytes[8192];
    for(unsigned i=0;i<64;i++){
        ZCHECK(Read(resume_input,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(Write(resume_cut,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
    }
    ZCHECK(Close(resume_input));ZCHECK(Close(resume_cut));
    /* Upstream stable ZFS_ERR_STREAM_TRUNCATED = 1040. */
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-resumed@raw",
        "RAM:openzfs-raw-cut.stream","resumable=on",1040));
    RUN(0,"1-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-resumed");
    char raw_resume_options[1024];size_t raw_token_len=strcspn(last_command_output,"\r\n");
    ZCHECK(raw_token_len>2&&raw_token_len+8<sizeof(raw_resume_options));
    snprintf(raw_resume_options,sizeof(raw_resume_options),"resume=%.*s",(int)raw_token_len,last_command_output);
    RUN(0,"size\t","C:zfs","send","-nP","-t",raw_resume_options+7);
    ZCHECK(strtoull(strstr(last_command_output,"size\t")+5,NULL,10)>0);
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/gui-crypt@wrong-resume");
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@wrong-resume",
        "RAM:openzfs-wrong-resume.stream",raw_resume_options,ESTALE));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt@wrong-resume");
    BPTR wrong_resume=Lock((CONST_STRPTR)"RAM:openzfs-wrong-resume.stream",SHARED_LOCK);
    if(wrong_resume)UnLock(wrong_resume);
    ZCHECK(!wrong_resume);
    delegated_stream=Open((CONST_STRPTR)"RAM:openzfs-raw-continuation.stream",MODE_NEWFILE);
    ZCHECK(delegated_stream);ZCHECK(Close(delegated_stream));
    ZCHECK(SetOwner((CONST_STRPTR)"RAM:openzfs-raw-continuation.stream",(77UL<<16)|88));
    RUN(0,NULL,"C:zfs","allow","-u","ozreader","send:raw","qemu_mirror/gui-crypt");
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int raw_resume_result=manager_permission_or_create(60,"qemu_mirror/gui-crypt@raw",
        "RAM:openzfs-raw-continuation.stream",raw_resume_options,0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!raw_resume_result);
    RUN(0,NULL,"C:zfs","unallow","-u","ozreader","send:raw","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-resumed@raw",
        "RAM:openzfs-raw-continuation.stream","resumable=on",0));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-resumed");
    ZCHECK(last_command_output[0]=='-'&&
        strspn(last_command_output+1,"\r\n")==strlen(last_command_output+1));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-resumed");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/raw-resumed");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZRESUME:","qemu_mirror/raw-resumed");
    BPTR resumed_file=Open((CONST_STRPTR)"OZRESUME:resume-payload",MODE_OLDFILE);ZCHECK(resumed_file);
    for(unsigned i=0;i<128;i++){
        ZCHECK(Read(resumed_file,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(!memcmp(resume_bytes,resume_block,sizeof(resume_bytes)));
    }
    ZCHECK(Read(resumed_file,resume_bytes,1)==0);ZCHECK(Close(resumed_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/raw-resumed");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/raw-resumed");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-resumed@raw");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-resumed");
    char *cli_raw_resume_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/cli-raw-resumed",NULL};
    ZCHECK(!receive_from_file_expected(cli_raw_resume_receive,"RAM:openzfs-raw-cut.stream",1));
    RUN(0,"1-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-raw-resumed");
    char *raw_cli_token=strdup(last_command_output);ZCHECK(raw_cli_token);
    raw_cli_token[strcspn(raw_cli_token,"\r\n")]=0;
    char *cli_raw_resume_send[]={"C:zfs","send","-t",raw_cli_token,NULL};
    ZCHECK(!send_to_file(cli_raw_resume_send,"RAM:openzfs-cli-raw-resume.stream"));
    free(raw_cli_token);
    ZCHECK(!receive_from_file(cli_raw_resume_receive,"RAM:openzfs-cli-raw-resume.stream"));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-raw-resumed");
    ZCHECK(last_command_output[0]=='-'&&strspn(last_command_output+1,"\r\n")==strlen(last_command_output+1));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/gui-crypt");
    RUN(0,"aes-256-gcm","C:zfs","get","-H","-o","value","encryption","qemu_mirror/cli-raw-resumed");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-raw-resumed");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/cli-raw-resumed");
    RUN(0,NULL,"C:zfs","set","mountpoint=OZRAWCLIRESUME:","qemu_mirror/cli-raw-resumed");
    resumed_file=Open((CONST_STRPTR)"OZRAWCLIRESUME:resume-payload",MODE_OLDFILE);ZCHECK(resumed_file);
    for(unsigned i=0;i<128;i++) {
        ZCHECK(Read(resumed_file,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(!memcmp(resume_bytes,resume_block,sizeof(resume_bytes)));
    }
    ZCHECK(Read(resumed_file,resume_bytes,1)==0);ZCHECK(Close(resumed_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-raw-resumed");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/cli-raw-resumed");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-raw-resumed");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-raw.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-raw-resume.stream"));
    bug("RESULT native CLI raw resume PASS truncated=1 source-locked=1 receiver-locked=1 original-key=1 exact-MiB=1 cleared=1\n");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-cut.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-continuation.stream"));
    bug("RESULT native raw resume PASS truncated=1 token=1 delegated=1 locked=1 exact-MiB=1 cleared=1\n");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw.stream"));
    bug("RESULT native raw encrypted replication PASS source-locked=1 receive-locked=1 original-key=1 exact-DOS=1\n");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/gui-crypt");
    unsigned char api_crypto_read[512];
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:api-payload",MODE_OLDFILE);ZCHECK(crypto_file);
    ZCHECK(Read(crypto_file,api_crypto_read,sizeof(api_crypto_read))==sizeof(api_crypto_read));
    ZCHECK(!memcmp(api_crypto_read,expected,sizeof(expected)));ZCHECK(Close(crypto_file));
    for(unsigned i=0;i<sizeof(raw_read);i++)raw_read[i]=expected[i]^0xa5;
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:incremental-payload",MODE_NEWFILE);ZCHECK(crypto_file);
    ZCHECK(Write(crypto_file,raw_read,sizeof(raw_read))==sizeof(raw_read));ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
    for(unsigned i=0;i<sizeof(resume_block);i++)resume_block[i]^=0x5a;
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:resume-payload",MODE_NEWFILE);ZCHECK(crypto_file);
    for(unsigned i=0;i<128;i++)ZCHECK(Write(crypto_file,resume_block,sizeof(resume_block))==sizeof(resume_block));
    ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/gui-crypt@next");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/gui-crypt");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@next","RAM:openzfs-raw-incremental.stream",
        "from=qemu_mirror/gui-crypt@raw;raw=on",0));
    RUN(0,"size\t","C:zfs","send","-nP","-w","-i","qemu_mirror/gui-crypt@raw","qemu_mirror/gui-crypt@next");
    ZCHECK(strtoull(strstr(last_command_output,"size\t")+5,NULL,10)>1048576);
    bug("RESULT native CLI send estimate PASS ordinary=1 raw-locked=1 raw-resume=1 raw-incremental=1\n");
    resume_input=Open((CONST_STRPTR)"RAM:openzfs-raw-incremental.stream",MODE_OLDFILE);ZCHECK(resume_input);
    resume_cut=Open((CONST_STRPTR)"RAM:openzfs-raw-cut.stream",MODE_NEWFILE);ZCHECK(resume_cut);
    for(unsigned i=0;i<64;i++){
        ZCHECK(Read(resume_input,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(Write(resume_cut,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
    }
    ZCHECK(Close(resume_input));ZCHECK(Close(resume_cut));
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-received@next",
        "RAM:openzfs-raw-cut.stream","resumable=on",1040));
    RUN(0,"1-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-received");
    raw_token_len=strcspn(last_command_output,"\r\n");
    ZCHECK(raw_token_len>2&&raw_token_len+48<sizeof(raw_resume_options));
    snprintf(raw_resume_options,sizeof(raw_resume_options),"resume=%.*s",(int)raw_token_len,last_command_output);
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@next",
        "RAM:openzfs-no-base.stream",raw_resume_options,EINVAL));
    wrong_resume=Lock((CONST_STRPTR)"RAM:openzfs-no-base.stream",SHARED_LOCK);
    if(wrong_resume)UnLock(wrong_resume);
    ZCHECK(!wrong_resume);
    snprintf(raw_resume_options,sizeof(raw_resume_options),"from=qemu_mirror/gui-crypt@raw;resume=%.*s",(int)raw_token_len,last_command_output);
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@next",
        "RAM:openzfs-raw-continuation.stream",raw_resume_options,0));
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-received@next",
        "RAM:openzfs-raw-continuation.stream","resumable=on",0));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-received");
    ZCHECK(last_command_output[0]=='-'&&strspn(last_command_output+1,"\r\n")==strlen(last_command_output+1));
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/raw-received");
    /* Keep verification reads from dirtying the new incremental base. */
    RUN(0,NULL,"C:zfs","set","atime=off","qemu_mirror/raw-received");
#ifndef OZ_TEST_RAW_MOUNT_WRITABLE
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/raw-received");
#else
    bug("RAW MOUNT DIAGNOSTIC before mount\n");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@next","qemu_mirror/raw-received");
#endif
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/raw-received");
#ifdef OZ_TEST_RAW_MOUNT_WRITABLE
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RAW MOUNT DIAGNOSTIC after mount before DOS reads\n");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@next","qemu_mirror/raw-received");
#endif
    raw_file=Open((CONST_STRPTR)"OZRAW:incremental-payload",MODE_OLDFILE);ZCHECK(raw_file);
    ZCHECK(Read(raw_file,api_crypto_read,sizeof(api_crypto_read))==sizeof(api_crypto_read));ZCHECK(Close(raw_file));
    ZCHECK(!memcmp(raw_read,api_crypto_read,sizeof(raw_read)));
    resumed_file=Open((CONST_STRPTR)"OZRAW:resume-payload",MODE_OLDFILE);ZCHECK(resumed_file);
    for(unsigned i=0;i<128;i++){
        ZCHECK(Read(resumed_file,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(!memcmp(resume_bytes,resume_block,sizeof(resume_bytes)));
    }
    ZCHECK(Read(resumed_file,resume_bytes,1)==0);ZCHECK(Close(resumed_file));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-cut.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-continuation.stream"));
    bug("RESULT native raw incremental resume PASS truncated=1 from-required=1 locked=1 exact-MiB=1 cleared=1\n");
#ifdef OZ_TEST_RAW_MOUNT_WRITABLE
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RAW MOUNT DIAGNOSTIC after DOS reads\n");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@next","qemu_mirror/raw-received");
#endif
    /* Raw incremental finalization must also resume an already published,
     * unlocked receiver, not only the unmounted/locked receive path above. */
    RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/gui-crypt");
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:mounted-raw-payload",MODE_NEWFILE);ZCHECK(crypto_file);
    const char *mounted_raw="mounted encrypted raw incremental exact DOS bytes";
    ZCHECK(Write(crypto_file,mounted_raw,strlen(mounted_raw))==(LONG)strlen(mounted_raw));ZCHECK(Close(crypto_file));
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/gui-crypt@mounted");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/gui-crypt");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/gui-crypt");
    ZCHECK(!manager_permission_or_create(60,"qemu_mirror/gui-crypt@mounted","RAM:openzfs-mounted-raw.stream",
        "from=qemu_mirror/gui-crypt@next;raw=on",0));
#ifdef OZ_TEST_RAW_MOUNT_FORCE
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-received@mounted","RAM:openzfs-mounted-raw.stream","",ETXTBSY));
    ZCHECK(!manager_permission_flags(61,"qemu_mirror/raw-received@mounted","RAM:openzfs-mounted-raw.stream","",0,OZ_CONFIRMED|OZ_FORCE));
#else
    ZCHECK(!manager_permission_or_create(61,"qemu_mirror/raw-received@mounted","RAM:openzfs-mounted-raw.stream","",0));
#endif
    raw_file=Open((CONST_STRPTR)"OZRAW:mounted-raw-payload",MODE_OLDFILE);ZCHECK(raw_file);
    char mounted_bytes[80];
    ZCHECK(Read(raw_file,mounted_bytes,sizeof(mounted_bytes))==(LONG)strlen(mounted_raw));
    ZCHECK(!memcmp(mounted_bytes,mounted_raw,strlen(mounted_raw)));ZCHECK(Close(raw_file));
    raw_file=Open((CONST_STRPTR)"OZRAW:incremental-payload",MODE_OLDFILE);ZCHECK(raw_file);
    ZCHECK(Read(raw_file,api_crypto_read,sizeof(api_crypto_read))==sizeof(api_crypto_read));ZCHECK(Close(raw_file));
    ZCHECK(!memcmp(raw_read,api_crypto_read,sizeof(raw_read)));
#ifdef OZ_TEST_RAW_MOUNT_FORCE
    bug("RESULT native Manager writable mounted raw receive PASS ordinary-refused=1 explicit-force=1 prior-bytes=1\n");
#endif
    RUN(0,NULL,"C:zfs","wait","qemu_mirror/raw-received");
    RUN(0,"available","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/raw-received");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-mounted-raw.stream"));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-received@mounted");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt@mounted");
    bug("RESULT native Manager mounted raw receive PASS locked-source=1 mounted-target=1 exact-DOS=1 key-retained=1 wait=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/raw-received");
#ifndef OZ_TEST_ENCRYPTION_KEEP
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-received@next");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-received@raw");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/raw-received");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt@next");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt@raw");
#else
    char *pending_send[]={"C:zfs","send","-w","qemu_mirror/gui-crypt@next",NULL};
    ZCHECK(!send_to_file(pending_send,"RAM:openzfs-raw-pending.stream"));
    resume_input=Open((CONST_STRPTR)"RAM:openzfs-raw-pending.stream",MODE_OLDFILE);ZCHECK(resume_input);
    resume_cut=Open((CONST_STRPTR)"RAM:openzfs-raw-cut.stream",MODE_NEWFILE);ZCHECK(resume_cut);
    for(unsigned i=0;i<64;i++){
        ZCHECK(Read(resume_input,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
        ZCHECK(Write(resume_cut,resume_bytes,sizeof(resume_bytes))==sizeof(resume_bytes));
    }
    ZCHECK(Close(resume_input));ZCHECK(Close(resume_cut));
    char *pending_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/raw-pending",NULL};
    ZCHECK(!receive_from_file_expected(pending_receive,"RAM:openzfs-raw-cut.stream",1));
    RUN(0,"1-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/raw-pending");
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-pending.stream"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-cut.stream"));
#endif
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-raw-incremental.stream"));
    bug("RESULT native raw incremental PASS source-locked=1 receive-locked=1 original-key=1 changed-bytes=1\n");
#ifndef OZ_TEST_ENCRYPTION_KEEP
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/gui-crypt");
#endif
    bug("RESULT native Manager encrypted create PASS raw=1 malformed=1 unauthorized=1 key-reload=1 exact-DOS=1\n");
    bug("RESULT native Manager encryption alias PASS on=aes-256-gcm off-with-key-refused=1\n");
#ifdef OZ_TEST_ENCRYPTED_CREATE_GUI
    bug("RESULT native encrypted create GUI PASS actual-manager=1 CLI-properties=1 key-reload=1 exact-DOS=1\n");
#endif
    const char *crypto_formats[]={"hex","passphrase","raw"};
    for(unsigned variant=0;variant<3;variant++){
        char format_options[160],hex_key[65];
        const char *phrase="Public AROS encrypted creation fixture";
        for(unsigned i=0;i<sizeof(crypto_key);i++)snprintf(hex_key+2*i,3,"%02x",crypto_key[i]);
        const void *material=variant==0?(const void *)hex_key:variant==1?(const void *)phrase:(const void *)crypto_key;
        size_t material_size=variant==0?64:variant==1?strlen(phrase):sizeof(crypto_key);
        crypto_file=Open((CONST_STRPTR)"RAM:openzfs-api-format.key",MODE_NEWFILE);ZCHECK(crypto_file);
        ZCHECK(Write(crypto_file,material,material_size)==material_size);ZCHECK(Close(crypto_file));
        snprintf(format_options,sizeof(format_options),"%sencryption=aes-256-gcm,keyformat=%s%s",
            variant==2?"type=volume,volsize=2M,":"",crypto_formats[variant],
            variant==1?",pbkdf2iters=350001":"");
        ZCHECK(!manager_permission_or_create(37,"qemu_mirror/api-format","RAM:openzfs-api-format.key",format_options,0));
        RUN(0,crypto_formats[variant],"C:zfs","get","-H","-o","value","keyformat","qemu_mirror/api-format");
        RUN(0,variant==2?"volume":"filesystem","C:zfs","get","-H","-o","value","type","qemu_mirror/api-format");
        if(variant==1){
            RUN(0,"350001","C:zfs","get","-Hp","-o","value","pbkdf2iters","qemu_mirror/api-format");
        }
        if(variant<2){
            RUN(0,NULL,"C:zfs","set","mountpoint=OZCRYPT:","qemu_mirror/api-format");
            crypto_file=Open((CONST_STRPTR)"OZCRYPT:format-payload",MODE_NEWFILE);ZCHECK(crypto_file);
            ZCHECK(Write(crypto_file,expected,sizeof(expected))==sizeof(expected));ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
            RUN(0,NULL,"C:zfs","unmount","qemu_mirror/api-format");
        }else{
            ZCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
            ZSET(OZ_ZVOL_OPEN,-1);packet.q.flags=OZ_ZVOL_WRITABLE;strcpy(packet.q.name,"qemu_mirror/api-format");
            ZCALL(0);fd=packet.q.descriptor;ZCHECK(fd>0);
            ZSET(OZ_ZVOL_WRITE,fd);packet.q.offset=512;packet.q.count=sizeof(expected);
            memcpy(packet.bytes,expected,sizeof(expected));ZCALL(0);ZCHECK(packet.q.completed==sizeof(expected));
            ZSET(OZ_ZVOL_FLUSH,fd);ZCALL(0);
            ZSET(OZ_ZVOL_CLOSE,fd);ZCALL(0);
            ZCHECK(!oz_session_close(OZ_ADMISSION_PORT,&session));
        }
        RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/api-format");
        RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/api-format");
        RUN(0,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-api-format.key","qemu_mirror/api-format");
        if(variant<2){
            RUN(0,NULL,"C:zfs","mount","qemu_mirror/api-format");
            crypto_file=Open((CONST_STRPTR)"OZCRYPT:format-payload",MODE_OLDFILE);ZCHECK(crypto_file);
            ZCHECK(Read(crypto_file,api_crypto_read,sizeof(api_crypto_read))==sizeof(api_crypto_read));
            ZCHECK(!memcmp(api_crypto_read,expected,sizeof(expected)));ZCHECK(Close(crypto_file));
            RUN(0,NULL,"C:zfs","unmount","qemu_mirror/api-format");
        }else{
            RUN(0,"2097152","C:zfs","get","-Hp","-o","value","volsize","qemu_mirror/api-format");
            ZCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
            ZSET(OZ_ZVOL_OPEN,-1);strcpy(packet.q.name,"qemu_mirror/api-format");ZCALL(0);fd=packet.q.descriptor;
            ZSET(OZ_ZVOL_READ,fd);packet.q.offset=512;packet.q.count=sizeof(expected);
            memset(packet.bytes,0,sizeof(expected));ZCALL(0);
            ZCHECK(packet.q.completed==sizeof(expected)&&!memcmp(packet.bytes,expected,sizeof(expected)));
            ZSET(OZ_ZVOL_CLOSE,fd);ZCALL(0);
            ZCHECK(!oz_session_close(OZ_ADMISSION_PORT,&session));
            bug("RESULT native encrypted volume I/O PASS write=1 flush=1 key-reload=1 exact-block-read=1\n");
        }
#ifdef OZ_TEST_ENCRYPTION_KEEP
        char retained_dataset[96];snprintf(retained_dataset,sizeof(retained_dataset),"qemu_mirror/api-format-%s",crypto_formats[variant]);
        RUN(0,NULL,"C:zfs","rename","qemu_mirror/api-format",retained_dataset);
        RUN(0,NULL,"C:zfs","unload-key",retained_dataset);
#else
        RUN(0,NULL,"C:zfs","destroy","qemu_mirror/api-format");
#endif
        ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-api-format.key"));
    }
    bug("RESULT native Manager crypto formats PASS hex=1 passphrase=1 volume=1 CLI-key-reload=3 exact-DOS=2\n");
    bug("RESULT native Manager PBKDF2 creation PASS custom=350001 invalid-refused=3\n");
    RUN(0,NULL,"C:zfs","create","-u","-o","encryption=aes-256-gcm","-o","keyformat=raw",
        "-o","keylocation=file:///RAM/openzfs-cli-test.key","-o","mountpoint=OZCRYPT:","qemu_mirror/cli-encrypted");
    RUN(0,"aes-256-gcm","C:zfs","get","-H","-o","value","encryption","qemu_mirror/cli-encrypted");
    RUN(0,"available","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-encrypted");
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_NEWFILE);ZCHECK(crypto_file);
    ZCHECK(Write(crypto_file,expected,sizeof(expected))==sizeof(expected));ZCHECK(Flush(crypto_file));ZCHECK(Close(crypto_file));
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_OLDFILE);ZCHECK(crypto_file);
    unsigned char crypto_read[512];ZCHECK(Read(crypto_file,crypto_read,sizeof(crypto_read))==sizeof(crypto_read));
    ZCHECK(!memcmp(crypto_read,expected,sizeof(expected)));ZCHECK(Close(crypto_file));
    RUN(-1,"busy","C:zfs","unload-key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-encrypted");
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int crypto_denied=run((char *const[]){"C:zfs","unload-key","qemu_mirror/cli-encrypted",NULL},-1,"Permission denied");
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!crypto_denied);
    RUN(0,"available","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/cli-encrypted");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    crypto_file=Open((CONST_STRPTR)"RAM:openzfs-cli-new.key",MODE_NEWFILE);ZCHECK(crypto_file);
    crypto_key[0]^=0x55;ZCHECK(Write(crypto_file,crypto_key,sizeof(crypto_key))==sizeof(crypto_key));ZCHECK(Close(crypto_file));
    RUN(-1,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-new.key","qemu_mirror/cli-encrypted");
    RUN(0,"unavailable","C:zfs","get","-H","-o","value","keystatus","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","load-key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","change-key","-o","keylocation=file:///RAM/openzfs-cli-new.key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","unload-key","qemu_mirror/cli-encrypted");
    RUN(-1,NULL,"C:zfs","load-key","-L","file:///RAM/openzfs-cli-test.key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","load-key","qemu_mirror/cli-encrypted");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-encrypted");
    crypto_file=Open((CONST_STRPTR)"OZCRYPT:payload",MODE_OLDFILE);ZCHECK(crypto_file);
    ZCHECK(Read(crypto_file,crypto_read,sizeof(crypto_read))==sizeof(crypto_read));
    ZCHECK(!memcmp(crypto_read,expected,sizeof(expected)));ZCHECK(Close(crypto_file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-encrypted");
#ifdef OZ_TEST_ENCRYPTION_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native CLI encryption retained PASS synced=1 replacement-key=1\n");
    bug("RESULT native API encryption retained PASS hex=1 passphrase=1 volume=1 synced=1\n");
    bug("RESULT native raw receive retained PASS incremental=1 synced=1\n");
    bug("RESULT native raw resume retained PASS pending=1 source-locked=1 synced=1 CLI=1\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-encrypted");
#endif
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-test.key"));
    ZCHECK(DeleteFile((CONST_STRPTR)"RAM:openzfs-cli-new.key"));
    bug("RESULT native CLI encrypted create PASS upstream-create=1 key-available=1 DOS-roundtrip=1 cleanup=1\n");
    bug("RESULT native CLI key lifecycle PASS mounted-busy=1 nonowner-denied=1 wrong-key=1 reload=1 replacement=1 old-key-denied=1 exact-read=1\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/accounting");
    ZCHECK(!manager_permission_flags(69,"qemu_mirror/accounting","quota","",ENOTSUP,0));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/accounting");
    RUN(0,NULL,"C:zpool","set","feature@project_quota=enabled","qemu_mirror");
    RUN(0,NULL,"C:zpool","set","feature@userobj_accounting=enabled","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZPROJECT:","qemu_mirror/project-packets");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/project-packets");
    BPTR project_lock=Lock((CONST_STRPTR)"OZPROJECT:",SHARED_LOCK);ZCHECK(project_lock);
    struct MsgPort *project_port=((struct FileLock *)BADDR(project_lock))->fl_Task;
    struct oz_project_request project_request={.version=OZ_PROJECT_VERSION,.operation=OZ_PROJECT_GET};
#define PROJECT_PACKET() DoPkt(project_port,OZ_ACTION_PROJECT,(SIPTR)project_lock,(SIPTR)&project_request,sizeof(project_request),0,0)
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(!project_request.project&&!project_request.inherit);
    project_request.operation=OZ_PROJECT_SET;project_request.project=1000;project_request.inherit=1;
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(!project_request.engine_error);
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    project_request.project=1001;
    LONG project_denied=PROJECT_PACKET();
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(project_denied==DOSFALSE);ZCHECK(project_request.engine_error==EACCES||project_request.engine_error==EPERM);
    project_request.operation=OZ_PROJECT_GET;
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(project_request.project==1000&&project_request.inherit==1);
    project_request.version=99;
    ZCHECK(PROJECT_PACKET()==DOSFALSE);ZCHECK(IoErr()==ERROR_BAD_NUMBER);
    project_request.version=OZ_PROJECT_VERSION;
    UnLock(project_lock);
    ZCHECK(PROJECT_PACKET()==DOSFALSE);ZCHECK(project_request.engine_error==EBADF);
    BPTR project_file=Open((CONST_STRPTR)"OZPROJECT:child",MODE_NEWFILE);ZCHECK(project_file);ZCHECK(Close(project_file));
    project_lock=Lock((CONST_STRPTR)"OZPROJECT:child",SHARED_LOCK);ZCHECK(project_lock);
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(project_request.project==1000);
    UnLock(project_lock);
    RUN(0,"1000","C:zfs","project","-d","OZPROJECT:");
    RUN(0,NULL,"C:zfs","project","-p","1002","OZPROJECT:child");
    RUN(0,"1002","C:zfs","project","OZPROJECT:child");
    RUN(0,NULL,"C:zfs","project","-C","OZPROJECT:child");
    project_lock=Lock((CONST_STRPTR)"OZPROJECT:child",SHARED_LOCK);ZCHECK(project_lock);
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(!project_request.project&&!project_request.inherit);
    UnLock(project_lock);
    RUN(0,NULL,"C:zfs","project","-r","-s","-p","1003","OZPROJECT:");
    project_lock=Lock((CONST_STRPTR)"OZPROJECT:child",SHARED_LOCK);ZCHECK(project_lock);
    ZCHECK(PROJECT_PACKET()==DOSTRUE);ZCHECK(project_request.project==1003&&project_request.inherit);
    UnLock(project_lock);
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int project_cli_denied=run((char *[]){"C:zfs","project","-p","1004","OZPROJECT:child",NULL},-1,"failed to set xattr");
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!project_cli_denied);
    RUN(0,"1003","C:zfs","project","OZPROJECT:child");
#ifdef OZ_TEST_PROJECT_GUI
    bug("Project GUI READY: set OZPROJECT: recursively to id=2000;inherit=on; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    RUN(0,"2000 P","C:zfs","project","-d","OZPROJECT:");
    RUN(0,"2000 P","C:zfs","project","OZPROJECT:child");
    RUN(0,NULL,"C:zfs","project","-r","-s","-p","1003","OZPROJECT:");
    bug("RESULT native project GUI PASS actual-manager=1 recursive-set=1 independent-cli=2\n");
#endif
    const char *bad_project_options[]={"id=4294967295","id=-1","id=12;id=13","id=1;","id=1;;inherit=on","id=1;inherit=off","id=1;keep=on"};
    for(unsigned i=0;i<sizeof(bad_project_options)/sizeof(bad_project_options[0]);i++)
        ZCHECK(!project_worker_test("OZPROJECT:child","set",bad_project_options[i],OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","set","id=4",OZ_CONFIRMED|OZ_DRYRUN,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","set","id=4",0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("relative","list","",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","clear","",0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","list","id=999",0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","check","inherit=on",0,OZ_ENGINE_ERROR,"before command execution"));
    ZCHECK(!project_worker_test("OZPROJECT:child","list","",0,OZ_OK,"1003 P"));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int project_worker_denied=project_worker_test("OZPROJECT:child","set","id=4000",OZ_CONFIRMED,OZ_ENGINE_ERROR,"Operation not permitted");
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!project_worker_denied);
    ZCHECK(SetOwner((CONST_STRPTR)"OZPROJECT:child",(77UL<<16)|88));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int project_owner_error=project_worker_test("OZPROJECT:child","set","id=4000;inherit=on",OZ_CONFIRMED,OZ_OK,"exit=0");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","list","",OZ_CONFIRMED,OZ_OK,"4000 P");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","check","id=4001",0,OZ_OK,"project ID is not set properly");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","clear","keep=on",OZ_CONFIRMED,OZ_OK,"exit=0");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","list","",OZ_CONFIRMED,OZ_OK,"4000 -");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","clear","",OZ_CONFIRMED,OZ_OK,"exit=0");
    if(!project_owner_error)project_owner_error=project_worker_test("OZPROJECT:child","list","",OZ_CONFIRMED,OZ_OK,"    0 -");
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!project_owner_error);
    ZCHECK(SetOwner((CONST_STRPTR)"OZPROJECT:child",secOWNER_SYSTEM));
    RUN(0,NULL,"C:zfs","project","-s","-p","1003","OZPROJECT:child");
    bug("RESULT native project worker PASS malformed=13 nonowner=1 owner-set=1 list=1 check=1 clear-keep=1 clear=1 query-unconfirmed=2\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/project-packets");
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/project-packets");
    project_lock=Lock((CONST_STRPTR)"OZPROJECT:",SHARED_LOCK);ZCHECK(project_lock);
    project_port=((struct FileLock *)BADDR(project_lock))->fl_Task;
    project_request.operation=OZ_PROJECT_SET;project_request.project=1001;
    ZCHECK(PROJECT_PACKET()==DOSFALSE);ZCHECK(project_request.engine_error==EROFS);
    UnLock(project_lock);
    RUN(-1,"Read-only file system","C:zfs","project","-p","1004","OZPROJECT:child");
    RUN(0,"1003","C:zfs","project","OZPROJECT:child");
    bug("RESULT native upstream project CLI PASS list=1 set=1 clear=1 recursive=1 packet-readback=1 nonowner=1 readonly=1\n");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/project-packets");
#ifdef OZ_TEST_PROJECT_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native project retained PASS synced=1\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/project-packets");
#endif
    bug("RESULT native project packets PASS get=1 set=1 inherited=1 nonowner=1 malformed=1 stale=1 readonly=1\n");
#undef PROJECT_PACKET
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZQUOTA:","qemu_mirror/quota-enforce");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/quota-enforce");
    const char *quota_limits[]={"defaultuserquota","defaultgroupquota","defaultprojectquota",
        "defaultuserobjquota","defaultgroupobjquota","defaultprojectobjquota"};
    for(unsigned kind=0;kind<6;kind++){
        char setting[96];snprintf(setting,sizeof(setting),"%s=1",quota_limits[kind]);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/quota-enforce",setting,"",0));
        RUN(0,NULL,"C:zpool","sync","qemu_mirror");
        BPTR denied=Open((CONST_STRPTR)"OZQUOTA:limit-test",MODE_NEWFILE);
        LONG quota_error=IoErr();
        bug("Quota enforcement kind=%s opened=%ld dos-error=%ld\n",quota_limits[kind],(long)(denied!=0),(long)quota_error);
        if(denied)Close(denied);
        /* Upstream zfs_acl_ids_overquota exempts the default project (0). */
        int project_default=(kind==2||kind==5);
        ZCHECK(project_default ? denied!=0 : (!denied&&quota_error==ERROR_DISK_FULL));
        if(project_default)ZCHECK(DeleteFile((CONST_STRPTR)"OZQUOTA:limit-test"));
        snprintf(setting,sizeof(setting),"%s=none",quota_limits[kind]);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/quota-enforce",setting,"",0));
        BPTR allowed=Open((CONST_STRPTR)"OZQUOTA:limit-test",MODE_NEWFILE);
        ZCHECK(allowed);ZCHECK(Close(allowed));
        ZCHECK(DeleteFile((CONST_STRPTR)"OZQUOTA:limit-test"));
        RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    }
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/quota-enforce");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/quota-enforce");
    bug("RESULT native default quota enforcement PASS denied-create=4 project-zero-exempt=2 cleared-create=6\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/accounting");
    for(unsigned id=1000;id<1030;id++){
        char property[80];snprintf(property,sizeof(property),"userquota@%u=1M",id);
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting",property,"",0));
    }
    ZCHECK(!accounting_test(67,"quota",30,1048576));
    ZCHECK(!accounting_test(68,"quota",0,0));
    ZCHECK(!accounting_test(69,"quota",0,0));
    ZCHECK(!accounting_test(67,"objquota",0,0));
    ZCHECK(!manager_permission_flags(67,"qemu_mirror/accounting","bad-metric","",EINVAL,0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int accounting_denied=manager_permission_flags(67,"qemu_mirror/accounting","quota","",EPERM,0);
    int quota_set_denied=manager_permission_or_create(41,"qemu_mirror/accounting","userquota@1000=2M","",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!accounting_denied);
    ZCHECK(!quota_set_denied);
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/accounting","user:77","permissions=userquota",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int accounting_allowed=accounting_test(67,"quota",30,1048576);
    int quota_set_allowed=manager_permission_or_create(41,"qemu_mirror/accounting","userquota@1000=1M","",0);
    int group_denied=manager_permission_flags(68,"qemu_mirror/accounting","quota","",EPERM,0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!accounting_allowed&&!group_denied);
    ZCHECK(!quota_set_allowed);
    ZCHECK(!manager_permission_or_create(66,"qemu_mirror/accounting","user:77","permissions=userquota",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    accounting_denied=manager_permission_flags(67,"qemu_mirror/accounting","quota","",EPERM,0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!accounting_denied);
    const char *quota_settings[]={"groupquota@1000=2M","projectquota@1000=3M",
        "userobjquota@1000=7","groupobjquota@1000=8","projectobjquota@1000=9",
        "defaultuserquota=4M","defaultgroupquota=5M","defaultprojectquota=6M",
        "defaultuserobjquota=101","defaultgroupobjquota=102","defaultprojectobjquota=103"};
    for(unsigned i=0;i<sizeof(quota_settings)/sizeof(quota_settings[0]);i++)
        ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting",quota_settings[i],"",0));
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userused@1000=1M","",EINVAL));
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userquota@-1=1M","",EINVAL));
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userquota@4294967296=1M","",ERANGE));
    ZCHECK(!manager_permission_or_create(42,"qemu_mirror/accounting","userquota@1000","",EINVAL));
    RUN(0,"1048576","C:zfs","get","-Hp","-o","value","userquota@1000","qemu_mirror/accounting");
    ZCHECK(!accounting_test(68,"quota",1,2097152));
    ZCHECK(!accounting_test(69,"quota",1,3145728));
    ZCHECK(!accounting_test(67,"objquota",1,7));
    ZCHECK(!accounting_test(68,"objquota",1,8));
    ZCHECK(!accounting_test(69,"objquota",1,9));
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    ZCHECK(!accounting_matrix());
    ZCHECK(!accounting_default_reads());
    bug("RESULT native accounting setter PASS quota-kinds=6 defaults=6 delegated=1 denied=1 invalid=4 cli-readback=1\n");
    bug("RESULT native accounting default reads PASS properties=6 exact=1\n");
#ifdef OZ_TEST_ACCOUNTING_SET_GUI
    bug("Accounting setter GUI READY: Dataset Set qemu_mirror/accounting userquota@1000=2M and defaultuserquota=8M; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
    RUN(0,"2097152","C:zfs","get","-Hp","-o","value","userquota@1000","qemu_mirror/accounting");
    RUN(0,"8388608","C:zfs","get","-Hp","-o","value","defaultuserquota","qemu_mirror/accounting");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","defaultuserquota=4M","",0));
    bug("RESULT native accounting setter GUI PASS actual-manager=1 independent-cli=2\n");
#endif
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userquota@1000=none","",0));
    /* Upstream literal (-p) output represents an unset quota as numeric zero. */
    RUN(0,"0","C:zfs","get","-Hp","-o","value","userquota@1000","qemu_mirror/accounting");
    RUN(0,"none","C:zfs","get","-H","-o","value","userquota@1000","qemu_mirror/accounting");
    ZCHECK(!manager_permission_or_create(41,"qemu_mirror/accounting","userquota@1000=1M","",0));
    bug("RESULT native accounting metrics PASS quota-kinds=3 objectquota-kinds=3 usage-kinds=6 defaults=6 units=1\n");
#ifdef OZ_TEST_ACCOUNTING_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native accounting retained PASS synced=1\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/accounting");
#endif
    bug("RESULT native accounting PASS quota-rows=30 pages=2 empty=3 delegated=1 other-metric-denied=1 revoked=1 invalid=1\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/volume-delegation");
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/volume-delegation","user:77","permissions=create,mount;scope=bogus",EINVAL));
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/volume-delegation","user:77","permissions=create,create",EINVAL));
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/volume-delegation","user:77","permissions=not-a-zfs-permission",EINVAL));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int self_grant=manager_permission_or_create(65,"qemu_mirror/volume-delegation","user:77","permissions=create,mount",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!self_grant);
#ifdef OZ_TEST_PERMISSION_GUI
    bug("Native permissions GUI GRANT READY: allow qemu_mirror/volume-delegation user:77 permissions=create,mount; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/volume-delegation","user:77","permissions=create,mount;scope=both",0));
#endif
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int delegated=manager_volume_create_target("qemu_mirror/volume-delegation/created","type=volume,volsize=2M,volblocksize=8K",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!delegated);
    RUN(0,"volume","C:zfs","get","-H","-o","value","type","qemu_mirror/volume-delegation/created");
#ifdef OZ_TEST_PERMISSION_GUI
    bug("Native permissions GUI REVOKE READY: unallow qemu_mirror/volume-delegation user:77 permissions=create,mount; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_permission_or_create(66,"qemu_mirror/volume-delegation","user:77","permissions=create,mount;scope=both",0));
#endif
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int revoked=manager_volume_create_target("qemu_mirror/volume-delegation/refused","type=volume,volsize=2M",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!revoked);
    RUN(1,NULL,"C:zfs","list","qemu_mirror/volume-delegation/refused");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/volume-delegation/created");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/volume-delegation");
    bug("RESULT native Manager volume delegation PASS granted=1 created=1 revoked=1 refused-absent=1\n");
#ifdef OZ_TEST_PERMISSION_GUI
    bug("RESULT native Manager permission GUI PASS actual-grant=1 actual-revoke=1 independent-create=1 revoked-denial=1\n");
#endif
    bug("RESULT native Manager permission API PASS allow=1 unallow=1 invalid=3 self-grant-denied=1 real-create=1 revoked-create-denied=1\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/scope-test");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/scope-test/branch");
    const char *principals[]={"user:77","group:88","everyone"};
    const char *scopes[]={"local","descendants","both"};
    for(unsigned principal=0;principal<3;principal++)for(unsigned s=0;s<3;s++){
        char options[96];snprintf(options,sizeof(options),"permissions=create,mount;scope=%s",scopes[s]);
        ZCHECK(!manager_permission_or_create(65,"qemu_mirror/scope-test",principals[principal],options,0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        int direct=manager_volume_create_target("qemu_mirror/scope-test/direct","type=volume,volsize=2M",s==1?EPERM:0);
        int descendant=manager_volume_create_target("qemu_mirror/scope-test/branch/nested","type=volume,volsize=2M",s==0?EPERM:0);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
        ZCHECK(!direct&&!descendant);
        if(s!=1){RUN(0,NULL,"C:zfs","destroy","qemu_mirror/scope-test/direct");}
        else {RUN(1,NULL,"C:zfs","list","qemu_mirror/scope-test/direct");}
        if(s!=0){RUN(0,NULL,"C:zfs","destroy","qemu_mirror/scope-test/branch/nested");}
        else {RUN(1,NULL,"C:zfs","list","qemu_mirror/scope-test/branch/nested");}
        ZCHECK(!manager_permission_or_create(66,"qemu_mirror/scope-test",principals[principal],options,0));
        secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
        direct=manager_volume_create_target("qemu_mirror/scope-test/direct","type=volume,volsize=2M",EPERM);
        descendant=manager_volume_create_target("qemu_mirror/scope-test/branch/nested","type=volume,volsize=2M",EPERM);
        secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
        ZCHECK(!direct&&!descendant);
        bug("Manager permission scope PASS principal=%s scope=%s revoke=1\n",principals[principal],scopes[s]);
    }
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/scope-test/branch");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/scope-test");
    bug("RESULT native Manager permission scope matrix PASS principals=3 scopes=3 cases=9 revoked=9\n");
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/named-permissions");
    const char *named="qemu_mirror/named-permissions";
    ZCHECK(!manager_permission_or_create(65,named,"set:not-prefixed","permissions=create",EINVAL));
    ZCHECK(!manager_permission_or_create(65,named,"creator","permissions=snapshot;scope=both",EINVAL));
    ZCHECK(!manager_permission_or_create(65,named,"set:@base","permissions=create,mount",0));
    ZCHECK(!manager_permission_or_create(65,named,"set:@outer","permissions=@base",0));
    ZCHECK(!manager_permission_or_create(65,named,"user:77","permissions=@outer",0));
    ZCHECK(!manager_permission_or_create(65,named,"creator","permissions=snapshot",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int first=manager_volume_create_target("qemu_mirror/named-permissions/first","type=volume,volsize=2M",0);
    int snapshot=manager_permission_or_create(50,"qemu_mirror/named-permissions/first@owned","","",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!first&&!snapshot);
    ZCHECK(!manager_permission_or_create(66,named,"creator","permissions=snapshot",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int second=manager_volume_create_target("qemu_mirror/named-permissions/second","type=volume,volsize=2M",0);
    int no_creator=manager_permission_or_create(50,"qemu_mirror/named-permissions/second@denied","","",EPERM);
    int retained_creator=manager_permission_or_create(50,"qemu_mirror/named-permissions/first@retained","","",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!second&&!no_creator&&!retained_creator);
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/second@denied");
    ZCHECK(!manager_permission_or_create(66,named,"set:@base","permissions=create",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int changed_set=manager_volume_create_target("qemu_mirror/named-permissions/third","type=volume,volsize=2M",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!changed_set);
    RUN(1,NULL,"C:zfs","list","qemu_mirror/named-permissions/third");
#ifdef OZ_TEST_PERMISSION_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native permissions retained PASS synced=1\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/first@owned");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/first@retained");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/first");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions/second");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/named-permissions");
#endif
    bug("RESULT native Manager named permissions PASS nested-set=1 changed-set-denial=1 creator-snapshot=1 removed-creator=1 existing-grant-retained=1\n");
    const char *revoke="qemu_mirror/revoke-all";
    RUN(0,NULL,"C:zfs","create","-u",revoke);
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/revoke-all/child");
    ZCHECK(!manager_permission_or_create(65,revoke,"user:77","all=on",EINVAL));
    ZCHECK(!manager_permission_or_create(66,revoke,"user:77","all=on;permissions=snapshot",EINVAL));
    ZCHECK(!manager_permission_or_create(66,revoke,"user:77","all=on;all=on",EINVAL));
    ZCHECK(!manager_permission_or_create(65,revoke,"set:@test","permissions=mount",0));
    ZCHECK(!manager_permission_or_create(65,revoke,"user:77","permissions=snapshot,@test",0));
    ZCHECK(!manager_permission_or_create(65,revoke,"group:99","permissions=mount",0));
    ZCHECK(!manager_permission_or_create(65,revoke,"creator","permissions=snapshot,@test",0));
#ifdef OZ_TEST_REVOKE_ALL_GUI
    bug("Revoke all GUI READY: unallow qemu_mirror/revoke-all user:77 all=on;scope=local, then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_permission_or_create(66,revoke,"user:77","all=on;scope=local",0));
#endif
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int local_all=manager_permission_or_create(50,"qemu_mirror/revoke-all@denied","","",EPERM);
    int descendant_all=manager_permission_or_create(50,"qemu_mirror/revoke-all/child@allowed","","",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!local_all&&!descendant_all);
    RUN(0,"Descendent permissions:","C:zfs","allow",revoke);
    ZCHECK(!manager_permission_or_create(66,revoke,"user:77","all=on",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int both_all=manager_permission_or_create(50,"qemu_mirror/revoke-all/child@denied","","",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!both_all);
    RUN(0,NULL,"C:zfs","allow",revoke);
    ZCHECK(!strstr(last_command_output,"user ozreader")&&!strstr(last_command_output,"user 77"));
    ZCHECK((strstr(last_command_output,"group extra mount")||strstr(last_command_output,"group 99 mount"))&&strstr(last_command_output,"@test mount"));
    ZCHECK(!manager_permission_or_create(66,revoke,"creator","all=on",0));
    RUN(0,NULL,"C:zfs","allow",revoke);ZCHECK(!strstr(last_command_output,"Create time permissions:"));
    ZCHECK(!manager_permission_or_create(66,revoke,"set:@test","all=on",0));
    RUN(0,NULL,"C:zfs","allow",revoke);ZCHECK(!strstr(last_command_output,"@test")&&(strstr(last_command_output,"group extra mount")||strstr(last_command_output,"group 99 mount")));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/revoke-all@denied");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/revoke-all/child@denied");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/revoke-all/child@allowed");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/revoke-all/child");
    RUN(0,NULL,"C:zfs","destroy",revoke);
    bug("RESULT native permission revoke all PASS local-only=1 descendants-retained=1 both-denied=1 other-principal-retained=1 creator=1 named-set=1 invalid=3\n");
    const char *admin="qemu_mirror/delegated-admin";
    RUN(0,NULL,"C:zfs","create","-u",admin);
    ZCHECK(!manager_permission_or_create(65,admin,"user:77","permissions=snapshot",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int no_allow=manager_permission_or_create(65,admin,"group:99","permissions=snapshot",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!no_allow);
    ZCHECK(!manager_permission_or_create(65,admin,"user:77","permissions=allow",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int permitted=manager_permission_or_create(65,admin,"group:99","permissions=snapshot",0);
    int escalation=manager_permission_or_create(65,admin,"user:78","permissions=destroy",EPERM);
    int delegate_allow=manager_permission_or_create(65,admin,"user:78","permissions=allow",EPERM);
    int revoke_other=manager_permission_or_create(66,admin,"group:99","all=on",EPERM);
    int revoke_self=manager_permission_or_create(66,admin,"user:77","permissions=snapshot",0);
    int inherited_allowed=manager_permission_or_create(65,admin,"user:78","permissions=snapshot",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!manager_permission_or_create(66,admin,"group:99","all=on",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int no_right=manager_permission_or_create(65,admin,"user:79","permissions=snapshot",EPERM);
    int revoke_self_all=manager_permission_or_create(66,admin,"user:77","all=on",0);
    int no_authority=manager_permission_or_create(65,admin,"user:77","permissions=allow",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);
    ZCHECK(!permitted&&!escalation&&!delegate_allow&&!revoke_other&&!revoke_self&&!inherited_allowed&&!no_right&&!revoke_self_all&&!no_authority);
    RUN(0,NULL,"C:zfs","allow",admin);
    ZCHECK(!strstr(last_command_output,"user ozreader")&&!strstr(last_command_output,"user 77")&&!strstr(last_command_output,"user 79")&&!strstr(last_command_output,"unknown: 79")&&!strstr(last_command_output,"group extra")&&!strstr(last_command_output,"group 99"));
    ZCHECK(strstr(last_command_output,"user (unknown: 78) snapshot")||strstr(last_command_output,"user 78 snapshot"));
    RUN(0,NULL,"C:zfs","destroy",admin);
    bug("RESULT native delegated administration PASS allow-required=1 held-right-granted=1 escalation-denied=1 allow-redelegation-denied=1 other-revoke-denied=1 self-revoke=1 inherited-authority=1 self-all=1 revoked-denial=1\n");
    const char *tree[]={"qemu_mirror/revoke-tree","qemu_mirror/revoke-tree/child","qemu_mirror/revoke-tree/child/leaf"};
    for(unsigned n=0;n<3;n++){
        RUN(0,NULL,"C:zfs","create","-u",tree[n]);
        ZCHECK(!manager_permission_or_create(65,tree[n],"user:77","permissions=snapshot;scope=local",0));
        ZCHECK(!manager_permission_or_create(65,tree[n],"group:99","permissions=mount;scope=local",0));
    }
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/revoke-outside");
    ZCHECK(!manager_permission_or_create(65,"qemu_mirror/revoke-outside","user:77","permissions=snapshot;scope=local",0));
#ifdef OZ_TEST_RECURSIVE_REVOKE_GUI
    bug("Recursive revoke GUI READY: unallow qemu_mirror/revoke-tree user:77 all=on with Recursive enabled, then close Manager\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_permission_flags(66,tree[0],"user:77","all=on",0,OZ_CONFIRMED|OZ_RECURSIVE));
#endif
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int denied_tree=0;
    for(unsigned n=0;n<3;n++){
        char snap[256];snprintf(snap,sizeof(snap),"%s@denied",tree[n]);
        denied_tree|=manager_permission_or_create(50,snap,"","",EPERM);
    }
    int outside_allowed=manager_permission_or_create(50,"qemu_mirror/revoke-outside@allowed","","",0);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!denied_tree&&!outside_allowed);
    for(unsigned n=0;n<3;n++){
        RUN(0,NULL,"C:zfs","allow",tree[n]);
        ZCHECK(!strstr(last_command_output,"user ozreader")&&!strstr(last_command_output,"user 77"));
        ZCHECK(strstr(last_command_output,"group extra mount")||strstr(last_command_output,"group 99 mount"));
        char snap[256];snprintf(snap,sizeof(snap),"%s@denied",tree[n]);RUN(1,NULL,"C:zfs","list",snap);
    }
    for(unsigned n=0;n<3;n++)ZCHECK(!manager_permission_or_create(65,tree[n],"user:77","permissions=snapshot,mount;scope=local",0));
    ZCHECK(!manager_permission_flags(66,tree[0],"user:77","permissions=snapshot",0,OZ_CONFIRMED|OZ_RECURSIVE));
    for(unsigned n=0;n<3;n++){
        RUN(0,NULL,"C:zfs","allow",tree[n]);
        ZCHECK(!strstr(last_command_output,"snapshot"));
        ZCHECK(strstr(last_command_output,"user ozreader mount")||strstr(last_command_output,"user 77 mount"));
    }
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/revoke-outside@allowed");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/revoke-outside");
    for(unsigned n=3;n>0;n--){RUN(0,NULL,"C:zfs","destroy",tree[n-1]);}
    bug("RESULT native recursive revoke PASS datasets=3 denied=3 other-principal-retained=1 outside-subtree-retained=1\n");
    const char *partial[]={"qemu_mirror/partial-revoke","qemu_mirror/partial-revoke/child","qemu_mirror/partial-revoke/child/leaf"};
    for(unsigned n=0;n<3;n++){
        RUN(0,NULL,"C:zfs","create","-u",partial[n]);
        ZCHECK(!manager_permission_or_create(65,partial[n],"user:77",n==2?"permissions=snapshot,allow;scope=local":"permissions=snapshot;scope=local",0));
    }
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int partial_failure=manager_permission_expect(66,partial[0],"user:77","all=on",EPERM,OZ_CONFIRMED|OZ_RECURSIVE,
        "Authenticated recursive permission revoke completed=1 errno=1 stopped-at=qemu_mirror/partial-revoke/child");
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!partial_failure);
    for(unsigned n=0;n<2;n++){
        RUN(0,NULL,"C:zfs","allow",partial[n]);
        ZCHECK(strstr(last_command_output,"user ozreader snapshot")||strstr(last_command_output,"user 77 snapshot"));
    }
    /* A leaf listing includes ancestor grants; check actual leaf authorization. */
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int leaf_denied=manager_permission_or_create(50,"qemu_mirror/partial-revoke/child/leaf@denied","","",EPERM);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!leaf_denied);
    ZCHECK(!manager_permission_or_create(65,partial[0],"user:77","permissions=allow",0));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    int self_recursive=manager_permission_flags(66,partial[0],"user:77","all=on",0,OZ_CONFIRMED|OZ_RECURSIVE);
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);ZCHECK(!self_recursive);
    for(unsigned n=0;n<3;n++){
        RUN(0,NULL,"C:zfs","allow",partial[n]);
        ZCHECK(!strstr(last_command_output,"user ozreader")&&!strstr(last_command_output,"user 77"));
    }
    RUN(1,NULL,"C:zfs","list","qemu_mirror/partial-revoke/child/leaf@denied");
    for(unsigned n=3;n>0;n--){RUN(0,NULL,"C:zfs","destroy",partial[n-1]);}
    bug("RESULT native recursive revoke failure PASS completed=1 precise-target=1 ancestors-retained=1 self-retry=3 explicit-permission-preserved-others=1\n");
    ZCHECK(!manager_volume_create("type=volume",EINVAL));
    ZCHECK(!manager_volume_create("type=volume,type=volume,volsize=2M",EINVAL));
    ZCHECK(!manager_volume_create("volsize=2M",EINVAL));
    ZCHECK(!manager_volume_create("type=volume,volsize=2M,utf8only=on",EINVAL));
    ZCHECK(!manager_volume_create("type=volume,volsize=0",EINVAL));
#ifdef OZ_TEST_ZVOL_CREATE_GUI
    bug("Native volume creation GUI READY: create qemu_mirror/transport-volume with type=volume,volsize=2M,volblocksize=8K,refreservation=0; close Manager to verify\n");
    ZCHECK(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)==0);
#else
    ZCHECK(!manager_volume_create("type=volume,volsize=2M,volblocksize=8192,refreservation=0",0));
#endif
    ZCHECK(!manager_volume_create("type=volume,volsize=2M",EEXIST));
    bug("RESULT native Manager volume creation PASS upstream=1 invalid-refused=1 duplicate-refused=1\n");
    ZCHECK(!oz_session_open(OZ_ADMISSION_PORT,&session));
    ZSET(OZ_ZVOL_OPEN,-1);packet.q.flags=OZ_ZVOL_WRITABLE;strcpy(packet.q.name,"qemu_mirror/transport-volume");
    ZCALL(0);fd=packet.q.descriptor;ZCHECK(fd>0);
    ZSET(OZ_ZVOL_INFO,fd);ZCALL(0);ZCHECK(packet.q.size==2097152&&packet.q.block_size==8192&&!packet.q.readonly);
    bug("RESULT native Manager volume authorization PASS unprivileged-denied=1 privileged-created=1 geometry=1\n");
#ifdef OZ_TEST_ZVOL_CREATE_GUI
    bug("RESULT native Manager volume GUI PASS actual-manager=1 independently-opened=1 exact-geometry=1\n");
#endif
    ZSET(OZ_ZVOL_WRITE,fd);packet.q.count=512;packet.q.offset=512;memcpy(packet.bytes,expected,512);ZCALL(0);ZCHECK(packet.q.completed==512);
    ZSET(OZ_ZVOL_FLUSH,fd);ZCALL(0);
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=512;packet.q.offset=512;memset(packet.bytes,0,512);ZCALL(0);ZCHECK(packet.q.completed==512&&!memcmp(packet.bytes,expected,512));
    ZSET(OZ_ZVOL_WRITE,fd);packet.q.count=sizeof(packet.bytes);
    for(unsigned i=0;i<sizeof(packet.bytes);i++)packet.bytes[i]=(unsigned char)(i*7);
    ZCALL(0);ZCHECK(packet.q.completed==sizeof(packet.bytes));
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.offset=512;packet.q.count=16384;ZCALL(0);ZCHECK(packet.q.completed==16384);
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=sizeof(packet.bytes);memset(packet.bytes,0xa5,sizeof(packet.bytes));ZCALL(0);
    for(unsigned i=0;i<sizeof(packet.bytes);i++)ZCHECK(packet.bytes[i]==(i>=8192&&i<16384?0:(unsigned char)(i*7)));
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.offset=512;packet.q.count=512;ZCALL(0);ZCHECK(packet.q.completed==512);
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.offset=2097152;packet.q.count=8192;ZCALL(EINVAL);ZCHECK(!packet.q.completed);
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.offset=0xfffffffffffffe00ULL;packet.q.count=8192;ZCALL(EINVAL);ZCHECK(!packet.q.completed);
    RUN(0,NULL,"C:zfs","set","readonly=on","qemu_mirror/transport-volume");
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.count=8192;ZCALL(EROFS);ZCHECK(!packet.q.completed);
    RUN(0,NULL,"C:zfs","set","readonly=off","qemu_mirror/transport-volume");
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=512;packet.q.offset=512;memset(packet.bytes,0,512);ZCALL(0);ZCHECK(!memcmp(packet.bytes,expected,512));
    bug("RESULT native zvol discard PASS freed-block-zero=1 partial-edges-preserved=1 subblock-noop=1 bounds=1 overflow=1\n");
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=512;packet.q.offset=2097152;memset(packet.bytes,0xa7,512);ZCALL(EINVAL);ZCHECK(!packet.q.completed&&packet.bytes[0]==0xa7&&packet.bytes[511]==0xa7);
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=512;packet.q.offset=1;
    struct oz_zvol_request saved=packet.q;
    ZCHECK(oz_zvol_request(OZ_ADMISSION_PORT,&packet,sizeof(packet))==EINVAL&&!memcmp(&saved,&packet.q,sizeof(saved)));
    ZSET(OZ_ZVOL_CLOSE,fd);ZCALL(0);
    ZSET(OZ_ZVOL_INFO,fd);ZCALL(EBADF);
    ZSET(OZ_ZVOL_OPEN,-1);strcpy(packet.q.name,"qemu_mirror/transport-volume");ZCALL(0);fd=packet.q.descriptor;
    ZSET(OZ_ZVOL_INFO,fd);ZCALL(0);ZCHECK(packet.q.readonly);
    ZSET(OZ_ZVOL_DISCARD,fd);packet.q.count=8192;ZCALL(EROFS);ZCHECK(!packet.q.completed);
    ZSET(OZ_ZVOL_WRITE,fd);packet.q.count=512;memcpy(packet.bytes,expected,512);ZCALL(EROFS);ZCHECK(!packet.q.completed);
    ZCHECK(!oz_control_open(OZ_ADMISSION_PORT,&session,&control));
    ZSET(OZ_ZVOL_INFO,control);ZCALL(EBADF);
    ZCHECK(!oz_control_close(OZ_ADMISSION_PORT,&session,control));
    secLogoutA(quiet);ZCHECK(secLoginA(reader)==((77UL<<16)|88));
    ZSET(OZ_ZVOL_INFO,fd);saved=packet.q;
    int denied=oz_zvol_request(OZ_ADMISSION_PORT,&packet,sizeof(packet.q));
    ZCHECK((denied==EACCES||denied==EPERM)&&!memcmp(&saved,&packet.q,sizeof(saved)));
    secLogoutA(quiet);ZCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
    ZSET(OZ_ZVOL_READ,fd);packet.q.count=512;packet.q.offset=512;memset(packet.bytes,0,512);ZCALL(0);ZCHECK(!memcmp(packet.bytes,expected,512));
    /* Session retirement must close the still-open zvol before destroy. */
    ZCHECK(!oz_session_close(OZ_ADMISSION_PORT,&session));
    ZSET(OZ_ZVOL_INFO,fd);ZCHECK(oz_zvol_request(OZ_ADMISSION_PORT,&packet,sizeof(packet.q))==ENOENT);
#ifdef OZ_TEST_ZVOL_DISCARD_KEEP
    RUN(0,NULL,"C:zpool","sync","qemu_mirror");
    bug("RESULT native zvol discard retained PASS bytes=24576 zero-offset=8192 zero-length=8192\n");
#else
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/transport-volume");
#endif
    bug("RESULT native zvol transport PASS rw=1 flush=1 geometry=1 bounds=1 readonly=1 wrong-type=1 unauthorized=1 stale=1 session-cleanup=1\n");
#endif
#undef ZCALL
#undef ZSET
#undef ZCHECK
    return 0;
}
#endif
static int scrub_complete(void)
{
    /* Starting a scan is not completion. Require upstream status to report
     * the completed scrub with zero errors, bounded to this disposable pool. */
    char *args[]={"C:zpool","status","cli_created",NULL};
    for(unsigned n=0;n<60;n++) {
        if(!run(args,0,"with 0 errors on")&&strstr(last_command_output,"scrub repaired"))return 0;
        Delay(50);
    }
    bug("RESULT native CLI maintenance FAIL scrub completion timeout\n");
    return 20;
}
static int checkpoint_discarded(void)
{
    char *args[]={"C:zpool","status","cli_created",NULL};
    for(unsigned n=0;n<60;n++) {
        if(run(args,0,"cli_created"))return 20;
        if(!strstr(last_command_output,"checkpoint:"))return 0;
        Delay(50);
    }
    bug("RESULT native CLI checkpoint FAIL discard completion timeout\n");
    return 20;
}
static int resilver_pool(const char *pool,const char *removed)
{
    char *args[]={"C:zpool","status","-P",(char *)pool,NULL};
    for(unsigned n=0;n<60;n++) {
        if(run(args,0,pool))return 20;
        int clean=1;const char *row=strstr(last_command_output,"config:");
        if(!row)clean=0;
        while(row&&(row=strstr(row," ONLINE"))) {
            unsigned long long read_errors,write_errors,checksum_errors;
            row+=7;
            if(sscanf(row,"%llu %llu %llu",&read_errors,&write_errors,&checksum_errors)!=3||
                read_errors||write_errors||checksum_errors){clean=0;break;}
        }
        if(clean&&!strstr(last_command_output,"FAULTED")&&
           !strstr(last_command_output,"DEGRADED")&&!strstr(last_command_output,"UNAVAIL")&&
           strstr(last_command_output,"resilvered ")&&
           strstr(last_command_output,"with 0 errors on")&&
           strstr(last_command_output,"state: ONLINE")&&
           (!removed||!strstr(last_command_output,removed)))return 0;
        Delay(50);
    }
    bug("RESULT native CLI vdev FAIL resilver completion timeout\n");
    return 20;
}
static int resilver_complete(void){return resilver_pool("cli_created",NULL);}
static int growth_payload(int writing)
{
    unsigned char *expected=malloc(32768),*actual=malloc(32768);
    if(!expected||!actual){free(expected);free(actual);return 20;}
    BPTR file=Open((CONST_STRPTR)"OZCLI:evacuated.bin",writing?MODE_NEWFILE:MODE_OLDFILE);
    int error=!file;unsigned long state=0x13579bdfUL;
    for(unsigned block=0;block<32&&!error;block++) {
        for(unsigned i=0;i<32768;i++) {
            state^=(state<<13)&0xffffffffUL;state^=state>>17;
            state^=(state<<5)&0xffffffffUL;state&=0xffffffffUL;
            expected[i]=(unsigned char)state;
        }
        if(writing)error=Write(file,expected,32768)!=32768;
        else error=Read(file,actual,32768)!=32768||memcmp(expected,actual,32768);
    }
    if(file) {
        if(writing){if(!Flush(file))error=1;}
        else if(!error&&Read(file,actual,1)!=0)error=1;
        if(!Close(file))error=1;
    }
    free(expected);free(actual);
    if(error)bug("RESULT native CLI evacuation payload FAIL writing=%d\n",writing);
    return error;
}
#if defined(OZ_TEST_TYPED_NATIVE_ADD) || defined(OZ_TEST_TYPED_NATIVE_REPLACE) || defined(OZ_TEST_TYPED_NATIVE_ATTACH)
static int typed_vdev_change(unsigned operation,const char *pool,const char *guid,const char *path,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 20;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=OZ_CONFIRMED;strcpy(m->target,pool);
    if(guid)snprintf(m->argument,sizeof(m->argument),"%s",guid);
    m->topology.group_count=m->topology.device_count=1;
    m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
    snprintf(m->topology.devices[0].path,sizeof(m->topology.devices[0].path),"%s",path);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    int failed=0;
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error) {
            char match[32];snprintf(match,sizeof(match),"errno=%d",expected);
            const char *found=strstr(out->detail,"errno=");
            bug("Typed native vdev operation=%u expected=%d status=%u: %s\n",operation,expected,(unsigned)out->status,out->detail);
            failed=out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!found||strcmp(found,match);
        }else failed=1;
        if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))failed=1;
    }else failed=!expected||error!=expected;
    free(m);free(out);return failed?20:0;
}
#endif
#ifdef OZ_TEST_TYPED_NATIVE_ADD
static int typed_native_add(const char *path,int expected)
{return typed_vdev_change(9,"cli_growth",NULL,path,expected);}
#endif
static int typed_vdev_guid(const char *pool,const char *path,char guid[32])
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return 20;
    if(!oz_client_open(&client)){free(m);return 20;}
    m->operation=4;strcpy(m->target,pool);
    int error=oz_submit(&client,m)!=OZ_OK,found=0;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error&&out->status==OZ_OK)for(unsigned i=0;i<out->row_count;i++) {
        if(strcmp(out->rows[i].name,path))continue;
        const char *p=strstr(out->rows[i].value,"guid=");char *end=NULL;
        if(!p)continue;
        errno=0;unsigned long long value=strtoull(p+5,&end,10);
        if(errno||!value||end==p+5||(*end&&*end!=' '))continue;
        snprintf(guid,32,"%llu",value);found++;
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);
    return error||found!=1?20:0;
}
static int manager_removal_wait(const char *removed,const char *survivor)
{
    struct oz_client client;
    if(!oz_client_open(&client))return 20;
    for(unsigned attempt=0;attempt<60;attempt++) {
        struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
        if(!m){oz_client_close(&client);return 20;}
        m->operation=4;strcpy(m->target,"cli_growth");
        int error=oz_submit(&client,m)!=OZ_OK;
        if(!error)while(!(out=oz_poll(&client)))Delay(1);
        free(m);
        if(error||!out||out->status!=OZ_OK) {
            if(out)oz_message_free(out);
            oz_client_close(&client);return 20;
        }
        char state[16];unsigned long long vdev=0,copied=0,total=0;
        const char *line=strstr(out->detail,"\nremoval=");
        int parsed=line&&sscanf(line,"\nremoval=%15s vdev=%llu copied=%llu total=%llu",
            state,&vdev,&copied,&total)==4;
        int removed_present=0,survivor_present=0;
        for(unsigned i=0;i<out->row_count;i++) {
            if(!strcmp(out->rows[i].name,removed))removed_present=1;
            if(!strcmp(out->rows[i].name,survivor))survivor_present=1;
        }
        if(parsed&&!strcmp(state,"finished")) {
            int failed=!copied||!total||removed_present||!survivor_present||out->next_cursor;
            bug("Manager removal completion: vdev=%llu copied=%llu total=%llu removed=%d survivor=%d\n",
                vdev,copied,total,removed_present,survivor_present);
            oz_message_free(out);oz_client_close(&client);
            if(failed)return 20;
            bug("RESULT native Manager removal PASS submitted=1 status-finished=1 copied-positive=1 topology-removed=1 survivor=1\n");
            return 0;
        }
        oz_message_free(out);Delay(50);
    }
    oz_client_close(&client);
    bug("RESULT native Manager removal FAIL completion timeout\n");
    return 20;
}
#ifdef OZ_TEST_MANAGER_VDEV_PROPERTIES
static int manager_vdev_property(const char *guid,const char *property,const char *value)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 20;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);m->operation=80;
    strcpy(m->target,"cli_growth");snprintf(m->argument,sizeof(m->argument),"%s",guid);
    snprintf(m->options,sizeof(m->options),"%s",property);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error) {
            bug("Manager vdev property %s: status=%u rows=%u detail=%s\n",property,(unsigned)out->status,(unsigned)out->row_count,out->detail);
            if(!value)error=out->status!=OZ_ENGINE_ERROR||out->row_count||!strstr(out->detail,"errno=2");
            else {
            error=out->status!=OZ_OK||out->row_count!=1||strcmp(out->rows[0].name,property)||
                strcmp(out->rows[0].parent,"cli_growth")||out->next_cursor;
            const char *got=!strcmp(out->rows[0].kind,"property-long")?out->detail:out->rows[0].value;
            if(strcmp(got,value)){bug("Manager vdev value mismatch: property=%s got=%s expected=%s\n",property,got,value);error=1;}
            }
        }
        if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))error=1;
    }
    free(m);free(out);return error?20:0;
}
static int manager_vdev_property_cursor_probe(struct oz_message *m,struct oz_message *out,
    UQUAD cursor,int *relation)
{
    memset(out,0,sizeof(*out));m->cursor=cursor;struct oz_ticket ticket={0};
    int submitted=!oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket),error=!submitted;
    if(!error)do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error) {
        bug("Manager vdev property catalogue cursor probe cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            (unsigned long long)cursor,(unsigned)out->status,(unsigned)out->row_count,
            (unsigned long long)out->next_cursor,out->detail);
        if(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS)*relation=-1;
        else if(out->status==OZ_OK&&!out->row_count&&!out->next_cursor)*relation=0;
        else if(out->status==OZ_ENGINE_ERROR&&!out->row_count&&!out->next_cursor&&
            strstr(out->detail,"errno=22"))*relation=1;
        else error=1;
    }
    if(submitted&&oz_admission_release(OZ_ADMISSION_PORT,&ticket))error=1;
    return error;
}
static int manager_vdev_property_catalogue_end(struct oz_message *m,struct oz_message *out,
    UQUAD known_below,UQUAD *end_out)
{
    UQUAD low=known_below,high=low+1;int relation=0;
    for(unsigned probes=0;probes<64;probes++) {
        if(manager_vdev_property_cursor_probe(m,out,high,&relation))return 1;
        if(!relation){*end_out=high;return 0;}
        if(relation>0)break;
        low=high;
        if(high>(~(UQUAD)0-1)/2)high=~(UQUAD)0;
        else high=high*2+1;
    }
    if(relation<=0)return 1;
    while(high-low>1) {
        UQUAD middle=low+(high-low)/2;
        if(manager_vdev_property_cursor_probe(m,out,middle,&relation))return 1;
        if(!relation){*end_out=middle;return 0;}
        if(relation<0)low=middle;else high=middle;
    }
    return 1;
}
static int manager_vdev_property_all(const char *guid,unsigned *rows_out,unsigned *pages_out)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    char names[256][64];unsigned names_count=0,rows=0,pages=0;
    int found_guid=0,found_user=0,found_long=0,error=0;
    if(!m||!out){free(m);free(out);return 20;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);m->operation=80;
    strcpy(m->target,"cli_growth");snprintf(m->argument,sizeof(m->argument),"%s",guid);
    strcpy(m->options,"all");UQUAD cursor=0,last_page_cursor=0,catalogue_end=0;
    do {
        UQUAD page_cursor=cursor;
        m->cursor=cursor;struct oz_ticket ticket={0};
        int submitted=!oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
        if(!submitted)error=1;
        if(!error)do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        pages++;
        if(!error)bug("Manager vdev all page=%u cursor=%llu status=%u rows=%u next=%llu detail=%s\n",
            pages,(unsigned long long)cursor,(unsigned)out->status,(unsigned)out->row_count,
            (unsigned long long)out->next_cursor,out->detail);
        if(!error&&(out->status!=OZ_OK||!out->row_count||out->row_count>OZ_MAX_ROWS))error=1;
        for(unsigned i=0;!error&&i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];
            if((strcmp(r->kind,"property")&&strcmp(r->kind,"property-long"))||
                strcmp(r->parent,"cli_growth")||!r->name[0]||!r->state[0]||names_count>=256)error=1;
            for(unsigned n=0;!error&&n<names_count;n++)if(!strcmp(names[n],r->name))error=1;
            if(error)break;
            strcpy(names[names_count++],r->name);
            if(!strcmp(r->name,"guid"))found_guid=!strcmp(r->value,guid);
            if(!strcmp(r->name,"test:manager"))found_user=!strcmp(r->value,"custom value=with;punctuation");
            if(!strcmp(r->name,"comment"))found_long=!strcmp(r->kind,"property-long")&&r->total==600;
            rows++;
        }
        if(!error&&out->next_cursor&&out->next_cursor<=cursor)error=1;
        cursor=out->next_cursor;
        if(!cursor)last_page_cursor=page_cursor;
        if(submitted&&oz_admission_release(OZ_ADMISSION_PORT,&ticket))error=1;
        if(pages>16)error=1;
    }while(!error&&cursor);
    int relation=0;
    if(!error)error=manager_vdev_property_catalogue_end(m,out,last_page_cursor,&catalogue_end);
    if(!error)error=manager_vdev_property_cursor_probe(m,out,catalogue_end,&relation)||relation;
    if(!error)error=manager_vdev_property_cursor_probe(m,out,catalogue_end+1,&relation)||relation!=1;
    if(!error)error=manager_vdev_property_cursor_probe(m,out,~(UQUAD)0,&relation)||relation!=1;
    if(!error)error=rows<=2*OZ_MAX_ROWS||pages<3||!found_guid||!found_user||!found_long;
    if(error)bug("Manager vdev all verdict rows=%u pages=%u guid=%d user=%d long=%d names=%u\n",
        rows,pages,found_guid,found_user,found_long,names_count);
    *rows_out=rows;*pages_out=pages;free(m);free(out);return error?20:0;
}
#endif
static int growth_pool(int cli)
{
    const char *native[]={"RAM:cli-growth-a.vdev","RAM:cli-growth-b.vdev"};
    char paths[2][258];char *zero=calloc(1,1024*1024);
    if(!zero)return 20;
    for(unsigned i=0;i<2;i++) {
        if(cli==2&&i==1){strcpy(paths[i],"/AROS/ata.device/3/partition/0");continue;}
        BPTR file=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!file){free(zero);return 20;}
        int error=0;
        unsigned megabytes=i?256:128;
#ifdef OZ_TEST_HISTORY_MEGARECORD
        if(!cli&&!i)megabytes=1536;
#elif defined(OZ_TEST_LARGE_HISTORY) || defined(OZ_TEST_HISTORY_RACE)
        if(!cli&&!i)megabytes=512;
#endif
        for(unsigned n=0;n<megabytes;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=1;break;}
        if(!Close(file))error=1;
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))error=1;
        if(lock)UnLock(lock);
        if(error){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);
    RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none","cli_growth",paths[0]);
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:","-o","normalization=formC",
        "-o","utf8only=on","-o","casesensitivity=insensitive","cli_growth/files");
    RUN(0,NULL,"C:zfs","mount","cli_growth/files");
    if(dos_file())return 20;
    if(growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_growth");
    if(!cli) for(unsigned online_cli=0;online_cli<2;online_cli++) {
        if(!online_cli) {
#ifdef OZ_TEST_VDEV_PROPERTIES
            char property_guid[32];
            if(typed_vdev_guid("cli_growth",paths[0],property_guid))return 20;
            RUN(0,property_guid,"C:zpool","get","-H","-o","value","guid","cli_growth",paths[0]);
            RUN(0,NULL,"C:zpool","set","comment=AROS-vdev-property-test","cli_growth",paths[0]);
            RUN(0,"AROS-vdev-property-test","C:zpool","get","-H","-o","value","comment","cli_growth",paths[0]);
#ifdef OZ_TEST_MANAGER_VDEV_PROPERTIES
#ifdef OZ_TEST_MANAGER_VDEV_GUI
            bug("Manager vdev GUI ready: pool=cli_growth guid=%s set comment=GUI-native-vdev; close Manager for independent verification\n",property_guid);
            if(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE)!=0)return 20;
            RUN(0,"GUI-native-vdev","C:zpool","get","-H","-o","value","comment","cli_growth",paths[0]);
            if(manager_vdev_property(property_guid,"comment","GUI-native-vdev"))return 20;
            bug("RESULT native Manager vdev GUI PASS actual-manager=1 independent-readback=1\n");
#endif
            if(command_worker_test(81,"cli_growth",property_guid,"comment=Manager vdev write",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"comment","Manager vdev write"))return 20;
            RUN(0,"Manager vdev write","C:zpool","get","-H","-o","value","comment","cli_growth",paths[0]);
            if(command_worker_test(81,"cli_growth",property_guid,"comment=unconfirmed",0,OZ_ENGINE_ERROR,"before command execution")||
                command_worker_test(81,"cli_growth",property_guid,"comment=dryrun",OZ_CONFIRMED|OZ_DRYRUN,OZ_ENGINE_ERROR,"before command execution")||
                command_worker_test(81,"cli_growth","0","comment=zero",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution")||
                command_worker_test(81,"cli_growth",property_guid,"guid=1",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL)||
                command_worker_test(81,"cli_growth",property_guid,"path=/dev/not-an-aros-binding",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL)||
                manager_vdev_property(property_guid,"comment","Manager vdev write"))return 20;
            RUN(0,paths[0],"C:zpool","get","-H","-o","value","path","cli_growth",paths[0]);
            if(command_worker_test(81,"cli_growth",property_guid,"comment=AROS-vdev-property-test",OZ_CONFIRMED,OZ_OK,"exit=0"))return 20;
            bug("RESULT native Manager vdev setter PASS write-readback=1 invalid=5 unchanged=1\n");
            if(command_worker_test(81,"cli_growth",property_guid,"checksum_n=42",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"checksum_n","42")||
                command_worker_test(81,"cli_growth",property_guid,"allocating=off",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL)||
                manager_vdev_property(property_guid,"allocating","on")||
                command_worker_test(81,"cli_growth",property_guid,"failfast=off",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"failfast","off")||
                command_worker_test(81,"cli_growth",property_guid,"failfast=on",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"failfast","on")||
                command_worker_test(81,"cli_growth",property_guid,"test:manager=custom value=with;punctuation",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"test:manager","custom value=with;punctuation")||
                command_worker_test(81,"cli_growth",property_guid,"comment=",OZ_CONFIRMED,OZ_OK,"exit=0")||
                manager_vdev_property(property_guid,"comment",""))return 20;
            RUN(0,"42","C:zpool","get","-H","-o","value","checksum_n","cli_growth",paths[0]);
            RUN(0,"on","C:zpool","get","-H","-o","value","failfast","cli_growth",paths[0]);
            RUN(0,"custom value=with;punctuation","C:zpool","get","-H","-o","value","test:manager","cli_growth",paths[0]);
            if(command_worker_test(81,"cli_growth",property_guid,"comment=AROS-vdev-property-test",OZ_CONFIRMED,OZ_OK,"exit=0"))return 20;
            bug("RESULT native Manager vdev types PASS numeric=1 boolean=1 user-property=1 empty=1\n");
            if(manager_vdev_property(property_guid,"guid",property_guid)||
                manager_vdev_property(property_guid,"comment","AROS-vdev-property-test")||
                manager_vdev_property(property_guid,"test:unset","-")||
                manager_vdev_property("18446744073709551615","comment",NULL))return 20;
            char long_comment[601],long_assignment[sizeof(long_comment)+8];
            memset(long_comment,'x',sizeof(long_comment)-1);long_comment[sizeof(long_comment)-1]=0;
            snprintf(long_assignment,sizeof(long_assignment),"comment=%s",long_comment);
            RUN(0,NULL,"C:zpool","set",long_assignment,"cli_growth",paths[0]);
            unsigned all_rows=0,all_pages=0;
            if(manager_vdev_property(property_guid,"comment",long_comment)||
                manager_vdev_property_all(property_guid,&all_rows,&all_pages))return 20;
            RUN(0,NULL,"C:zpool","set","comment=AROS-vdev-property-test","cli_growth",paths[0]);
            bug("RESULT native Manager vdev property long PASS bytes=600 exact=1\n");
            bug("RESULT native Manager vdev property all PASS rows=%u pages=%u guid=1 user=1 long=1 unique=%u cursor-end=1 invalid-cursor=2 errno-detail=2\n",
                all_rows,all_pages,all_rows);
            bug("RESULT native Manager vdev property PASS guid=1 comment=1 structured=1\n");
#endif
            RUN(1,NULL,"C:zpool","set","path=/dev/not-an-aros-binding","cli_growth",paths[0]);
            RUN(0,paths[0],"C:zpool","get","-H","-o","value","path","cli_growth",paths[0]);
            bug("RESULT native CLI vdev properties PASS guid=1 comment-set-get=1\n");
#endif
            RUN(0,"cli_growth/files","C:zpool","history","-i","cli_growth");
#ifdef OZ_TEST_COMMAND_HISTORY
            /* Plain history excludes internal ioctl records: only the real
             * user-command logging path can satisfy this unique marker. */
            RUN(0,NULL,"C:zfs","set","test:command-history=native-command-history-probe","cli_growth/files");
            RUN(0,NULL,"C:zpool","sync","cli_growth");
            RUN(0,"native-command-history-probe","C:zpool","history","cli_growth");
            bug("RESULT native CLI command history PASS plain-user-command=1\n");
#ifdef OZ_TEST_COMMAND_HISTORY_ISOLATION
            RUN(0,"RESULT native command history isolation PASS","C:OpenZFSCommandHistoryTest","cli_growth","qemu_mirror");
            RUN(0,"history-thread-alpha","C:zpool","history","cli_growth");
            if(strstr(last_command_output,"history-thread-beta")||strstr(last_command_output,"history-forbidden-marker"))return 20;
            if(!strstr(last_command_output,"history-wire-valid")||strstr(last_command_output,"forbidden-schema-marker"))return 20;
            RUN(0,"history-thread-beta","C:zpool","history","qemu_mirror");
            if(strstr(last_command_output,"history-thread-alpha")||strstr(last_command_output,"history-forbidden-marker"))return 20;
            bug("RESULT native command history pools PASS separate=2 forbidden-absent=1\n");
#ifdef OZ_TEST_HISTORY_MEGARECORD
            if(manager_history_megarecord("cli_growth"))return 20;
#endif
#endif
#endif
            unsigned minimum=0,overwrite_jumps=0;
#ifdef OZ_TEST_HISTORY_MEGARECORD
            minimum=OZ_HISTORY_MEGARECORD_BYTES+1;
#endif
#ifdef OZ_TEST_LARGE_HISTORY
            for(unsigned record=0;record<180;record++) {
                char property[6400];
                int prefix=snprintf(property,sizeof(property),"test:history=history-record-%03u-",record);
                memset(property+prefix,'a'+record%26,6000);property[prefix+6000]=0;
                RUN(0,NULL,"C:zfs","set",property,"cli_growth/files");
            }
            RUN(0,NULL,"C:zpool","sync","cli_growth");
            minimum=384*1024+1;overwrite_jumps=1;
#endif
#ifdef OZ_TEST_HISTORY_RACE
            if(history_concurrent_overwrite("cli_growth","cli_growth/files"))return 20;
            minimum=384*1024+1;overwrite_jumps=1;
#endif
            if(oz_history_wire_test("cli_growth",minimum,overwrite_jumps))return 20;
#ifdef OZ_TEST_LARGE_HISTORY
            char *history_args[]={"C:zpool","history","-i","cli_growth",NULL};
            if(send_to_file(history_args,"RAM:large-history.txt"))return 20;
            BPTR history=Open((CONST_STRPTR)"RAM:large-history.txt",MODE_OLDFILE);
            char *text=calloc(1,1024*1024);LONG got=history&&text?Read(history,text,1024*1024-1):-1;
            if(history)Close(history);
            int valid=got>0&&got<1024*1024-1&&!strstr(text,"history-record-000-");
            for(unsigned record=160;valid&&record<180;record++) {
                char needle[32];snprintf(needle,sizeof(needle),"history-record-%03u-",record);
                if(!strstr(text,needle))valid=0;
            }
            free(text);if(!valid)return 20;
            bug("RESULT native large CLI history PASS records=180 newest=20 oldest-overwritten=1 raw-over-384KiB=1 overwrite-jumps=1\n");
#endif
#ifdef OZ_TEST_HISTORY_RACE
            char *race_args[]={"C:zpool","history","-i","cli_growth",NULL};
            if(send_to_file(race_args,"RAM:history-race.txt"))return 20;
            BPTR race_history=Open((CONST_STRPTR)"RAM:history-race.txt",MODE_OLDFILE);
            char *race_text=calloc(1,1024*1024);
            LONG race_got=race_history&&race_text?Read(race_history,race_text,1024*1024-1):-1;
            if(race_history)Close(race_history);
            int race_valid=race_got>0&&race_got<1024*1024-1&&
                !strstr(race_text,"history-race-record-000-");
            for(unsigned record=220;race_valid&&record<240;record++) {
                char needle[40];snprintf(needle,sizeof(needle),"history-race-record-%03u-",record);
                if(!strstr(race_text,needle))race_valid=0;
            }
            free(race_text);if(!race_valid)return 20;
            bug("RESULT native concurrent CLI history PASS records=240 newest=20 oldest-overwritten=1 raw-over-384KiB=1 overwrite-jumps=1\n");
#endif
            bug("RESULT native CLI history PASS internal-dataset-record=1\n");
            RUN(0,NULL,"C:zpool","initialize","-w","cli_growth",paths[0]);
            RUN(0,"100% initialized","C:zpool","status","-i","cli_growth");
            if(growth_payload(0))return 20;
            RUN(0,NULL,"C:zpool","initialize","-u","cli_growth",paths[0]);
            RUN(0,"uninitialized","C:zpool","status","-i","cli_growth");
            if(growth_payload(0))return 20;
            bug("RESULT native CLI initialize PASS wait-complete=1 status=1 uninitialize=1 payload-bytes=1048576\n");
        }
        RUN(0,NULL,"C:zpool","list","-Hp","-o","size","cli_growth");
        unsigned long long before=strtoull(last_command_output,NULL,10);
        char guid[32];
        if(!before||typed_vdev_guid("cli_growth",paths[0],guid))return 20;
        /* Extend only our disposable backing file, preserving all old bytes. */
        BPTR backing=Open((CONST_STRPTR)native[0],MODE_READWRITE);
        char *extension=calloc(1,1024*1024);int failed=0;
        if(!backing||!extension)failed=1;
        if(!failed&&Seek(backing,0,OFFSET_END)==-1)failed=1;
        for(unsigned i=0;!failed&&i<128;i++)
            if(Write(backing,extension,1024*1024)!=1024*1024)failed=1;
        free(extension);
        if(backing&&!Close(backing))failed=1;
        if(failed)return 20;
        if(online_cli) { RUN(0,NULL,"C:zpool","online","-e","cli_growth",paths[0]); }
        else if(manager_permission_or_create(15,"cli_growth",guid,"expand=on",0))return 20;
        RUN(0,NULL,"C:zpool","sync","cli_growth");
        RUN(0,NULL,"C:zpool","list","-Hp","-o","size","cli_growth");
        unsigned long long after=strtoull(last_command_output,NULL,10);
        if(after<=before||after-before<64ULL*1024*1024||growth_payload(0))return 20;
        RUN(0,NULL,"C:zfs","unmount","cli_growth/files");
        RUN(0,NULL,"C:zpool","export","cli_growth");
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"cli_growth");
        RUN(0,NULL,"C:zpool","list","-Hp","-o","size","cli_growth");
        if(strtoull(last_command_output,NULL,10)!=after)return 20;
#ifdef OZ_TEST_VDEV_PROPERTIES
        RUN(0,"AROS-vdev-property-test","C:zpool","get","-H","-o","value","comment","cli_growth",paths[0]);
        bug("RESULT native CLI vdev properties persistence PASS reimport=1\n");
#endif
        RUN(0,NULL,"C:zfs","mount","cli_growth/files");
        if(growth_payload(0))return 20;
        bug("RESULT native %s online expansion PASS before=%llu after=%llu reimport-capacity=1 payload-bytes=1048576\n",online_cli?"CLI":"Manager",before,after);
    }
    if(cli){
        RUN(0,NULL,"C:zfs","unmount","cli_growth/files");
        RUN(0,NULL,"C:zpool","export","cli_growth");
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[0],"cli_growth");
        if(cli==2){RUN(1,"read-only","C:zpool","add","-f","cli_growth",paths[1]);}
        else {RUN(1,"read-only","C:zpool","add","cli_growth",paths[1]);}
#ifdef OZ_TEST_TYPED_NATIVE_ADD
        if(cli==2&&typed_native_add(paths[1],EROFS))return 20;
#endif
        RUN(0,NULL,"C:zpool","export","cli_growth");
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"cli_growth");
        RUN(0,NULL,"C:zfs","mount","cli_growth/files");
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase)return 20;
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
        if(cli==2){RUN(1,"Permission denied","C:zpool","add","-f","cli_growth",paths[1]);}
        else {RUN(1,"permission denied","C:zpool","add","cli_growth",paths[1]);}
#ifdef OZ_TEST_TYPED_NATIVE_ADD
        if(cli==2&&typed_native_add(paths[1],EPERM))return 20;
#endif
        secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
        CloseLibrary(secBase);
        if(cli==1){
        BPTR marked=Open((CONST_STRPTR)native[1],MODE_READWRITE);unsigned char marker=0x5a;
        if(!marked||Seek(marked,256*1024*1024-1,OFFSET_BEGINNING)==-1||Write(marked,&marker,1)!=1||!Close(marked))return 20;
        RUN(1,NULL,"C:zpool","add","-f","cli_growth",paths[1]);
        marked=Open((CONST_STRPTR)native[1],MODE_READWRITE);marker=0;
        if(!marked||Seek(marked,256*1024*1024-1,OFFSET_BEGINNING)==-1||Read(marked,&marker,1)!=1||marker!=0x5a)return 20;
        marker=0;
        if(Seek(marked,256*1024*1024-1,OFFSET_BEGINNING)==-1||Write(marked,&marker,1)!=1||!Close(marked))return 20;
        }
        if(cli==2){
            RUN(1,"No such file or directory","C:zpool","add","-f","-n","cli_growth","/AROS/ata.device/3/partition/127");
            RUN(1,"native whole-disk provisioning requires","C:zpool","add","-f","cli_growth","/AROS/ata.device/3");
            RUN(1,"mismatched replication level","C:zpool","add","cli_growth",paths[1]);
            RUN(0,paths[1],"C:zpool","add","-f","-n","-P","cli_growth",paths[1]);
        }else {RUN(0,paths[1],"C:zpool","add","-n","cli_growth",paths[1]);}
        RUN(0,paths[0],"C:zpool","status","-P","cli_growth");
        if(strstr(last_command_output,paths[1]))return 20;
        if(cli==2){
#ifdef OZ_TEST_TYPED_NATIVE_ADD
            if(typed_native_add(paths[1],0))return 20;
#else
            RUN(0,NULL,"C:zpool","add","-f","cli_growth",paths[1]);
#endif
        }
        else {RUN(0,NULL,"C:zpool","add","cli_growth",paths[1]);}
        if(cli==1)bug("RESULT native CLI add policy PASS readonly=denied unauthorized=denied nonblank-force=denied marker-preserved=1 dryrun-unchanged=1\n");
    }else{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 20;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=9;m->flags=OZ_CONFIRMED;strcpy(m->target,"cli_growth");
    m->topology.group_count=1;m->topology.device_count=1;
    m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
    strcpy(m->topology.devices[0].path,paths[1]);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error){bug("Manager growth status=%d: %s\n",out->status,out->detail);
            error=out->status!=OZ_OK||!strstr(out->detail,"errno=0");}
        int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;
    }
    free(m);free(out);if(error)return 20;
    }
    RUN(0,paths[1],"C:zpool","status","-P","cli_growth");
    RUN(0,NULL,"C:zpool","reopen","-n","cli_growth");
    char removal_guid[32];
    if(typed_vdev_guid("cli_growth",paths[0],removal_guid))return 20;
    RUN(0,"Memory that will be used after removing","C:zpool","remove","-n","cli_growth",paths[0]);
    RUN(0,paths[0],"C:zpool","remove","-n","-p","cli_growth",paths[0]);
    const char *estimate=strstr(last_command_output,paths[0])+strlen(paths[0]);
    char *estimate_end=NULL;errno=0;
    unsigned long long estimate_bytes=strtoull(estimate,&estimate_end,10);
    if(errno||!estimate_bytes||estimate_end==estimate||
        (*estimate_end!='\r'&&*estimate_end!='\n'))return 20;
    RUN(0,paths[0],"C:zpool","status","-P","cli_growth");
    if(!strstr(last_command_output,paths[1])||!strstr(last_command_output,"state: ONLINE")||
        strstr(last_command_output,"remove:"))return 20;
    if(growth_payload(0))return 20;
    RUN(1,NULL,"C:zpool","remove","-s","cli_growth");
    if(cli) {
        RUN(0,NULL,"C:zpool","remove","cli_growth",paths[0]);
        int finished=0;
        for(unsigned attempt=0;attempt<60;attempt++) {
            RUN(0,"cli_growth","C:zpool","status","-P","cli_growth");
            if(strstr(last_command_output,"Removal of vdev 0 copied ")&&
                strstr(last_command_output,"completed on ")&&
                !strstr(last_command_output,"copied 0 ")&&
                strstr(last_command_output,"state: ONLINE")&&!strstr(last_command_output,paths[0])){finished=1;break;}
            Delay(50);
        }
        if(!finished){bug("RESULT native CLI removal FAIL completion timeout\n");return 20;}
    } else {
        if(manager_permission_or_create(10,"cli_growth",removal_guid,"",0)||
            manager_removal_wait(paths[0],paths[1]))return 20;
    }
    RUN(1,NULL,"C:zpool","remove","-s","cli_growth");
    RUN(0,NULL,"C:zfs","unmount","cli_growth/files");
    RUN(0,NULL,"C:zpool","export","cli_growth");
    if(!DeleteFile((CONST_STRPTR)native[0]))return 20;
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",paths[1],"cli_growth");
    RUN(0,paths[1],"C:zpool","status","-P","cli_growth");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_growth/files");
    if(readonly_file())return 20;
    if(growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_growth/files");
    RUN(0,NULL,"C:zpool","export","cli_growth");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[1],"cli_growth");
    RUN(0,NULL,"C:zpool","destroy","cli_growth");
    if(cli!=2&&!DeleteFile((CONST_STRPTR)native[1]))return 20;
    bug("RESULT native CLI %s growth PASS named-policy=1 added-member=1 reopen=1 DOS-write=1 readonly-reimport-read=1\n",cli==2?"native-partition":cli?"upstream":"manager");
#ifdef OZ_TEST_TYPED_NATIVE_ADD
    if(cli==2)bug("RESULT native typed partition add PASS readonly-denied=1 unauthorized=1 native-survivor-read=1\n");
#endif
    bug("RESULT native %s evacuation PASS completed=1 copied-positive=1 removed-file-deleted=1 survivor-only-reimport-read=1 no-active-cancel-denied=1\n",cli?"CLI":"Manager");
    bug("RESULT native CLI removal dryrun PASS formatted=1 parsable=1 topology-unchanged=1 payload-bytes=1048576 survivor-byte-compare=1\n");
    return 0;
}
static int attach_pool(int partition)
{
    const char *native[]={"RAM:cli-attach-a.vdev","RAM:cli-attach-b.vdev","RAM:cli-attach-c.vdev"};
    char paths[3][258];char *zero=calloc(1,1024*1024);
    if(!zero)return 20;
    for(unsigned i=0;i<3;i++) {
        if(partition&&i==2){strcpy(paths[i],"/AROS/ata.device/3/partition/0");continue;}
        BPTR file=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!file){free(zero);return 20;}
        int error=0;
        for(unsigned n=0;n<128;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=1;break;}
        if(!Close(file))error=1;
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))error=1;
        if(lock)UnLock(lock);
        if(error){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);
    RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none","cli_attached",paths[0]);
#ifdef OZ_TEST_TYPED_NATIVE_REPLACE
    char original_guid[32];
    if(partition&&typed_vdev_guid("cli_attached",paths[0],original_guid))return 20;
#endif
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:","-o","normalization=formC",
        "-o","utf8only=on","-o","casesensitivity=insensitive","cli_attached/files");
    RUN(0,NULL,"C:zfs","mount","cli_attached/files");
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_attached");
    RUN(0,NULL,"C:zfs","unmount","cli_attached/files");
    RUN(0,NULL,"C:zpool","export","cli_attached");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[0],"cli_attached");
    RUN(-1,"read-only","C:zpool","attach","cli_attached",paths[0],paths[1]);
    RUN(-1,"read-only","C:zpool","replace","cli_attached",paths[0],paths[2]);
#ifdef OZ_TEST_TYPED_NATIVE_REPLACE
    if(partition&&typed_vdev_change(13,"cli_attached",original_guid,paths[2],EROFS))return 20;
#endif
    RUN(0,NULL,"C:zpool","export","cli_attached");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"cli_attached");
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    RUN(-1,"permission denied","C:zpool","attach","cli_attached",paths[0],paths[1]);
    RUN(partition?1:-1,partition?"Permission denied":"permission denied","C:zpool","replace","cli_attached",paths[0],paths[2]);
#ifdef OZ_TEST_TYPED_NATIVE_REPLACE
    if(partition&&typed_vdev_change(13,"cli_attached",original_guid,paths[2],EPERM))return 20;
#endif
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(0,NULL,"C:zfs","mount","cli_attached/files");
    for(unsigned i=1;i<3;i++) {
        if(partition&&i==2)continue;
        BPTR marked=Open((CONST_STRPTR)native[i],MODE_READWRITE);unsigned char marker=0x5a;
        if(!marked||Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||
            Write(marked,&marker,1)!=1||!Close(marked))return 20;
        RUN(-1,NULL,"C:zpool",i==1?"attach":"replace","-f","cli_attached",paths[0],paths[i]);
        marked=Open((CONST_STRPTR)native[i],MODE_READWRITE);marker=0;
        if(!marked||Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||
            Read(marked,&marker,1)!=1||marker!=0x5a)return 20;
        marker=0;
        if(Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||
            Write(marked,&marker,1)!=1||!Close(marked))return 20;
    }
    RUN(0,NULL,"C:zpool","attach","cli_attached",paths[0],paths[1]);
    if(resilver_pool("cli_attached",NULL)||growth_payload(0))return 20;
    RUN(0,"mirror","C:zpool","status","-P","cli_attached");
    if(!strstr(last_command_output,paths[0])||!strstr(last_command_output,paths[1]))return 20;
#ifdef OZ_TEST_TYPED_NATIVE_REPLACE
    if(partition){if(typed_vdev_change(13,"cli_attached",original_guid,paths[2],0))return 20;}
    else
#endif
    {RUN(0,NULL,"C:zpool","replace","cli_attached",paths[0],paths[2]);}
    if(resilver_pool("cli_attached",paths[0])||growth_payload(0))return 20;
    RUN(0,paths[2],"C:zpool","status","-P","cli_attached");
    RUN(0,NULL,"C:zpool","detach","cli_attached",paths[1]);
    RUN(0,NULL,"C:zfs","unmount","cli_attached/files");
    RUN(0,NULL,"C:zpool","export","cli_attached");
    if(!DeleteFile((CONST_STRPTR)native[0])||!DeleteFile((CONST_STRPTR)native[1]))return 20;
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[2],"cli_attached");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_attached/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_attached/files");
    RUN(0,NULL,"C:zpool","export","cli_attached");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[2],"cli_attached");
    RUN(0,NULL,"C:zpool","destroy","cli_attached");
    if(!partition&&!DeleteFile((CONST_STRPTR)native[2]))return 20;
    if(!partition)bug("RESULT native CLI attach replace blankness PASS force-denied=2 marker-preserved=2\n");
    bug("RESULT native CLI %sattach replace PASS readonly-denied=1 unauthorized=1 resilver-complete=2 old-members-deleted=1 replacement-only-read=1 payload-bytes=1048576\n",partition?"partition-":"");
#ifdef OZ_TEST_TYPED_NATIVE_REPLACE
    if(partition)bug("RESULT native typed replacement PASS readonly-denied=1 unauthorized=1 resilver-complete=1 replacement-only-read=1\n");
#endif
    return 0;
}
static int split_pool(int partition)
{
    const char *native[]={"RAM:cli-split-a.vdev","RAM:cli-split-b.vdev"};
    char paths[2][258];char *zero=calloc(1,1024*1024);
    if(!zero)return 20;
    for(unsigned i=0;i<2;i++) {
        if(partition&&i==1){strcpy(paths[i],"/AROS/ata.device/3/partition/0");continue;}
        BPTR file=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!file){free(zero);return 20;}
        int error=0;
        for(unsigned n=0;n<128;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=1;break;}
        if(!Close(file))error=1;
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))error=1;
        if(lock)UnLock(lock);
        if(error){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);
    if(partition) {
        RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none","cli_split_source",paths[0]);
    } else {
        RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none","cli_split_source","mirror",paths[0],paths[1]);
    }
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:","-o","normalization=formC",
        "-o","utf8only=on","-o","casesensitivity=insensitive","cli_split_source/files");
    RUN(0,NULL,"C:zfs","mount","cli_split_source/files");
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_split_source");
    if(partition) {
#ifdef OZ_TEST_TYPED_NATIVE_ATTACH
        char original_guid[32];
        if(typed_vdev_guid("cli_split_source",paths[0],original_guid))return 20;
        RUN(0,NULL,"C:zfs","unmount","cli_split_source/files");
        RUN(0,NULL,"C:zpool","export","cli_split_source");
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[0],"cli_split_source");
        if(typed_vdev_change(11,"cli_split_source",original_guid,paths[1],EROFS))return 20;
        RUN(0,NULL,"C:zpool","export","cli_split_source");
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"cli_split_source");
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
        if(!secBase)return 20;
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
        if(typed_vdev_change(11,"cli_split_source",original_guid,paths[1],EPERM))return 20;
        secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
        CloseLibrary(secBase);
        RUN(0,NULL,"C:zfs","mount","cli_split_source/files");
        if(typed_vdev_change(11,"cli_split_source",original_guid,paths[1],0))return 20;
#else
        RUN(0,NULL,"C:zpool","attach","cli_split_source",paths[0],paths[1]);
#endif
        if(resilver_pool("cli_split_source",NULL)||growth_payload(0))return 20;
        RUN(0,"mirror","C:zpool","status","-P","cli_split_source");
        if(!strstr(last_command_output,paths[0])||!strstr(last_command_output,paths[1]))return 20;
#ifdef OZ_TEST_TYPED_NATIVE_ATTACH
        bug("RESULT native typed attach PASS readonly-denied=1 unauthorized=1 resilver-complete=1 payload-bytes=1048576\n");
#endif
    }
    RUN(1,"busy","C:zpool","split","cli_split_source","cli_split_new",paths[1]);
    RUN(0,NULL,"C:zfs","unmount","cli_split_source/files");
    RUN(0,NULL,"C:zpool","export","cli_split_source");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[0],"-d",paths[1],"cli_split_source");
    RUN(1,"read-only","C:zpool","split","cli_split_source","cli_split_new",paths[1]);
    RUN(0,NULL,"C:zpool","export","cli_split_source");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_split_source");
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    /* Native geometry admission rejects the ordinary caller before the
     * split ioctl; file-backed selection reaches upstream's policy error. */
    RUN(1,partition?"Permission denied":"permission denied","C:zpool","split","cli_split_source","cli_split_new",paths[1]);
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(0,paths[1],"C:zpool","split","-n","-P","cli_split_source","cli_split_new",paths[1]);
    RUN(0,"mirror","C:zpool","status","-P","cli_split_source");
    if(!strstr(last_command_output,paths[0])||!strstr(last_command_output,paths[1]))return 20;
#ifdef OZ_TEST_SPLIT_RETAINED
    RUN(1,NULL,"C:zpool","split","cli_split_source","cli_split_new",paths[1]);
    RUN(0,"cli_split_new","C:zpool","list","cli_split_new");
    if(manager_pool_busy(6)||manager_pool_busy(8))return 20;
    RUN(1,"busy","C:zpool","export","cli_split_source");
    RUN(1,"busy","C:zpool","destroy","cli_split_source");
    RUN(0,NULL,"C:zfs","mount","cli_split_new/files");
    if(growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_split_new/files");
    RUN(0,NULL,"C:zpool","export","cli_split_new");
    bug("RESULT native CLI split retention PASS injected-error=1 destination-readable=1 source-export-denied=1 source-destroy-denied=1 destination-exported=1\n");
#else
    RUN(0,NULL,"C:zpool","split","cli_split_source","cli_split_new",paths[1]);
#endif
    RUN(0,paths[0],"C:zpool","status","-P","cli_split_source");
    if(strstr(last_command_output,paths[1])||strstr(last_command_output,"mirror"))return 20;
    RUN(1,NULL,"C:zpool","list","cli_split_new");
    RUN(0,NULL,"C:zpool","export","cli_split_source");
    const char *pools[]={"cli_split_source","cli_split_new"};
    for(unsigned i=0;i<2;i++) {
        char dataset[80];snprintf(dataset,sizeof(dataset),"%s/files",pools[i]);
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[i],pools[i]);
        RUN(0,paths[i],"C:zpool","status","-P",pools[i]);
        if(strstr(last_command_output,paths[1-i]))return 20;
        RUN(0,NULL,"C:zfs","mount","-o","ro",dataset);
        if(readonly_file()||growth_payload(0))return 20;
        RUN(0,NULL,"C:zfs","unmount",dataset);
        RUN(0,NULL,"C:zpool","export",pools[i]);
#ifdef OZ_TEST_NATIVE_SPLIT_KEEP
        if(partition&&i==1) {
            bug("RESULT native CLI split retained PASS pool=cli_split_new member=/AROS/ata.device/3/partition/0 payload-bytes=1048576\n");
            continue;
        }
#endif
        RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[i],pools[i]);
        RUN(0,NULL,"C:zpool","destroy",pools[i]);
        if((!partition||i==0)&&!DeleteFile((CONST_STRPTR)native[i]))return 20;
    }
    bug("RESULT native CLI %ssplit PASS mounted-denied=1 readonly-denied=1 unauthorized=1 dryrun-unchanged=1 destination-exported=1 separate-imports=2 payload-bytes=1048576\n",partition?"partition-attach-":"");
    return 0;
}
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
#define CREATED_IMPORT_PATHS "-d","/AROS/ata.device/3/partition/0","-d","/RAM/typed-native-mirror.vdev"
#else
#define CREATED_IMPORT_PATHS "-d","/AROS/ata.device/3/partition/0"
#endif
#ifdef OZ_TEST_NATIVE_CREATE
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
static int mixed_backing_create(void)
{
    char *zero=calloc(1,1024*1024);if(!zero)return 20;
    BPTR backing=Open((CONST_STRPTR)"RAM:typed-native-mirror.vdev",MODE_NEWFILE);
    if(!backing){free(zero);return 20;}
    int error=0;
    for(unsigned i=0;i<128;i++)if(Write(backing,zero,1024*1024)!=1024*1024){error=20;break;}
    if(!Close(backing))error=20;
    free(zero);return error;
}
#endif
#ifdef OZ_TEST_TYPED_NATIVE_CREATE
static int typed_native_create(unsigned flags,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 1;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=5;m->flags=flags;strcpy(m->target,"cli_native_created");
    m->topology.group_count=m->topology.device_count=1;m->topology.ashift=12;
    m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
    strcpy(m->topology.devices[0].path,"/AROS/ata.device/3/partition/0");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    m->topology.device_count=2;m->topology.groups[0].layout=OZ_MIRROR;
    strcpy(m->topology.devices[1].path,"RAM:typed-native-mirror.vdev");
#endif
    struct oz_ticket ticket={0};
    int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket),failed=0;
    if(!error) {
        do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        if(!error) {
            bug("Typed native create status=%u expected=%d: %s\n",(unsigned)out->status,expected,out->detail);
            if(expected) {
                char match[32];snprintf(match,sizeof(match),"returned errno=%d",expected);
                const char *found=strstr(out->detail,match);
                failed=out->status==OZ_OK||!found||found[strlen(match)]!=0;
            } else failed=out->status!=OZ_OK;
        } else failed=1;
        if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))failed=1;
    } else failed=!expected||error!=expected;
    free(m);free(out);return failed;
}
#endif
static int native_create_pool(void)
{
    /* The runner provisions only this fresh, blank disposable GPT extent. */
    char *path="/AROS/ata.device/3/partition/0";
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    if(mixed_backing_create())return 20;
#endif
#ifdef OZ_TEST_TYPED_NATIVE_CREATE
    if(typed_native_create(0,EINVAL))return 20;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    if(typed_native_create(OZ_CONFIRMED,EPERM))return 20;
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    if(typed_native_create(OZ_CONFIRMED,0))return 20;
#else
    RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none",
        "cli_native_created",path);
#endif
    RUN(0,path,"C:zpool","status","-P","cli_native_created");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    if(!strstr(last_command_output,"mirror")||!strstr(last_command_output,"typed-native-mirror.vdev"))return 20;
#endif
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:","-o","normalization=formC",
        "-o","utf8only=on","-o","casesensitivity=insensitive","cli_native_created/files");
    RUN(0,NULL,"C:zfs","mount","cli_native_created/files");
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_native_created");
    RUN(0,NULL,"C:zfs","unmount","cli_native_created/files");
    RUN(0,NULL,"C:zpool","export","cli_native_created");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    if(!DeleteFile((CONST_STRPTR)"RAM:typed-native-mirror.vdev"))return 20;
    /* Omitting the missing member must still fail: a surviving label is
     * not authority to access a path the caller never admitted. */
    RUN(1,"no such pool available","C:zpool","import","-N","-o","cachefile=none",
        "-o","readonly=on","-d",path,"cli_native_created");
#endif
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        CREATED_IMPORT_PATHS,"cli_native_created");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    RUN(0,"DEGRADED","C:zpool","status","-P","cli_native_created");
#endif
    RUN(0,"on","C:zpool","get","-H","-o","value","readonly","cli_native_created");
    RUN(0,NULL,"C:zfs","mount","cli_native_created/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_native_created/files");
    RUN(0,NULL,"C:zpool","export","cli_native_created");
    /* Keep exported media for independent host integrity and cold-boot tests. */
    bug("RESULT native CLI partition create PASS native-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576\n");
#ifdef OZ_TEST_TYPED_NATIVE_CREATE
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    /* Supply a fresh blank file so this negative case reaches the populated
     * native extent check instead of failing earlier on a missing file. */
    if(mixed_backing_create())return 20;
#endif
    if(typed_native_create(OZ_CONFIRMED,EEXIST))return 20;
    bug("RESULT native typed partition create PASS API-create=1 CLI-import=1 DOS-read=1\n");
    bug("RESULT native typed create policy PASS unconfirmed=denied unauthorized=denied nonblank=denied\n");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    if(!DeleteFile((CONST_STRPTR)"RAM:typed-native-mirror.vdev"))return 20;
    bug("RESULT native typed mixed mirror PASS file-member-deleted=1 native-survivor-read=1\n");
#endif
#endif
    return 0;
}
#endif
#ifdef OZ_TEST_CLI_DRAID
#ifdef OZ_TEST_TYPED_DRAID
static int typed_draid_change(const char **paths,unsigned parity,unsigned operation)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return 20;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=OZ_CONFIRMED;strcpy(m->target,"cli_draid");
    unsigned children=parity+2;
    m->topology.group_count=1;m->topology.device_count=children;m->topology.ashift=12;
    struct oz_vdev_group *g=&m->topology.groups[0];
    g->role=OZ_DATA;g->layout=OZ_DRAID;g->draid_parity=parity;g->draid_data=1;g->draid_spares=1;
    for(unsigned i=0;i<children;i++)snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),"%s",paths[i]);
    char why[256];unsigned data=999,groups=999;int failed=0;
    if(oz_validate_topology(&m->topology,why,sizeof(why))!=OZ_OK||
        !oz_draid_geometry(g,children,&data,&groups)||data!=1||groups!=1)failed=1;
    for(unsigned parity=1;parity<=3;parity++)for(unsigned children=2;children<=OZ_MAX_DEVICES;children++)
        for(unsigned spare=0;spare<=children;spare++) {
            struct oz_vdev_group trial={.role=OZ_DATA,.layout=OZ_DRAID,.draid_parity=parity,.draid_spares=spare};
            unsigned d=999,n=999;int valid=children>parity&&spare<children-parity;
            if(oz_draid_geometry(&trial,children,&d,&n)!=valid)failed=1;
            if(valid){unsigned expected=children-parity-spare;if(expected>8)expected=8;
                if(d!=expected||!n||n*(d+parity)%(children-spare))failed=1;
                for(unsigned smaller=1;smaller<n;smaller++)if(!(smaller*(d+parity)%(children-spare)))failed=1;
            }else if(d!=999||n!=999)failed=1;
        }
    g->draid_spares=children;
    if(oz_validate_topology(&m->topology,why,sizeof(why))!=OZ_BAD_REQUEST)failed=1;
    g->draid_spares=1;g->layout=OZ_MIRROR;
    if(oz_validate_topology(&m->topology,why,sizeof(why))!=OZ_BAD_REQUEST)failed=1;
    g->layout=OZ_DRAID;
    struct oz_ticket ticket={0};int error=failed?EINVAL:oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error){
        do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
        bug("Typed dRAID operation=%u status=%u: %s\n",operation,(unsigned)out->status,out->detail);
        if(error||out->status!=OZ_OK)failed=1;
        if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))failed=1;
    }else failed=1;
    if(!failed&&operation==5)bug("RESULT native typed dRAID create PASS geometry=1 API-create=1\n");
    free(m);free(out);return failed?20:0;
}
#endif
#ifdef OZ_TEST_DRAID_COLD
static int draid_manager_spare(void)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return 1;
    if(!oz_client_open(&client)){free(m);return 1;}
    m->operation=4;strcpy(m->target,"cli_draid");
    int error=oz_submit(&client,m)!=OZ_OK;unsigned active=0;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error){
        error=out->status!=OZ_OK||out->next_cursor!=0;
        for(unsigned i=0;i<out->row_count;i++){
            const struct oz_row *r=&out->rows[i];
            if(!strcmp(r->kind,"dspare")&&strstr(r->value,"role=spare ")){
                bug("Manager dRAID spare state=%s value=%s\n",r->state,r->value);
                if(!strcmp(r->state,"INUSE"))active++;
            }
        }
        if(active!=1)error=1;
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);
    if(!error)bug("RESULT native Manager dRAID cold spare PASS inuse=1\n");
    return error;
}
#endif
static int draid_pool(void)
{
#ifdef OZ_TEST_DRAID_PERSIST
    const char *native[]={"OZFILES:cli-draid-a.vdev","OZFILES:cli-draid-b.vdev","OZFILES:cli-draid-c.vdev"};
#else
    const char *native[]={"RAM:cli-draid-a.vdev","RAM:cli-draid-b.vdev","RAM:cli-draid-c.vdev"};
#endif
    char paths[3][258];
    void *zero=calloc(1,1024*1024);if(!zero)return 20;
    for(unsigned i=0;i<3;i++) {
        int failed=0;
#ifndef OZ_TEST_DRAID_PERSIST
        BPTR f=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!f){free(zero);return 20;}
        for(unsigned n=0;n<128;n++)if(Write(f,zero,1024*1024)!=1024*1024){failed=1;break;}
        if(!Close(f))failed=1;
        if(failed){free(zero);return 20;}
#endif
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))failed=1;
        if(lock)UnLock(lock);
        if(failed){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);
#ifdef OZ_TEST_DRAID_COLD
    expected_data="spareIO!";
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",paths[0],"-d",paths[1],"-d",paths[2],"cli_draid");
    RUN(0,"INUSE","C:zpool","status","cli_draid");
    if(!strstr(last_command_output,"OFFLINE"))return 20;
    if(draid_manager_spare())return 20;
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
    if(!Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE))return 20;
    expected_data="CLI data";
    bug("RESULT native CLI dRAID cold PASS active-spare=1 original-offline=1 payload-bytes=1048576 readonly=1 exported=1\n");
    return 0;
#endif
#ifdef OZ_TEST_TYPED_DRAID
    if(typed_draid_change(native,1,5))return 20;
#else
    RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none",
        "cli_draid","draid1:1d:3c:1s",paths[0],paths[1],paths[2]);
#endif
    RUN(0,"draid1","C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:",
        "-o","normalization=formC","-o","utf8only=on","-o","casesensitivity=insensitive","cli_draid/files");
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_draid");
    RUN(0,NULL,"C:zpool","offline","cli_draid",paths[0]);
    RUN(0,"state: DEGRADED","C:zpool","status","cli_draid");
    /* Force new writes while one child is unavailable, then require upstream
     * scan completion before accepting the repaired pool or exporting it. */
    expected_data="dRAIDnew";
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_draid");
    RUN(0,NULL,"C:zpool","online","cli_draid",paths[0]);
    if(resilver_pool("cli_draid",NULL)||growth_payload(0))return 20;
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    RUN(-1,"permission denied","C:zpool","replace","cli_draid",paths[0],"draid1-0-0");
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(0,"AVAIL","C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zpool","replace","cli_draid",paths[0],"draid1-0-0");
    RUN(0,"INUSE","C:zpool","status","cli_draid");
    if(resilver_pool("cli_draid",NULL)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","offline","cli_draid",paths[0]);
    RUN(0,"OFFLINE","C:zpool","status","cli_draid");
    expected_data="spareIO!";
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_draid");
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
#ifdef OZ_TEST_DRAID_PERSIST
    if(!Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE))return 20;
    expected_data="CLI data";
    bug("RESULT native CLI dRAID retained PASS active-spare=1 original-offline=1 payload-bytes=1048576 exported=1\n");
    return 0;
#endif
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"-d",paths[2],"cli_draid");
    RUN(0,"INUSE","C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","online","cli_draid",paths[0]);
    if(resilver_pool("cli_draid",NULL)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","detach","cli_draid","draid1-0-0");
    RUN(0,"AVAIL","C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[0],"-d",paths[1],"-d",paths[2],"cli_draid");
    RUN(-1,"read-only","C:zpool","replace","cli_draid",paths[0],"draid1-0-0");
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"-d",paths[2],"cli_draid");
    RUN(0,NULL,"C:zpool","destroy","cli_draid");
    for(unsigned i=0;i<3;i++)if(!DeleteFile((CONST_STRPTR)native[i]))return 20;
    expected_data="CLI data";
    bug("RESULT native CLI dRAID PASS parity=1 data=1 children=3 spares=1 payload-bytes=1048576 readonly-reimport=1\n");
    bug("RESULT native CLI dRAID resilver PASS degraded-write=1 completed=1 readonly-reimport-read=1\n");
    bug("RESULT native CLI dRAID spare PASS inuse=1 resilver-complete=1 detached=1 available=1 unauthorized-denied=1 readonly-denied=1\n");
    bug("RESULT native CLI dRAID active spare PASS original-offline=1 DOS-write-read=1 active-reimport=1 restored=1\n");
    return 0;
}
#endif
#ifdef OZ_TEST_TYPED_DRAID
/* Identify the new top-level group by its actual member, not row ordering or
 * a formatted CLI name. Read upstream allocation counters through Manager API. */
static int draid_group_allocation(const char *member,UQUAD *allocated)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return 20;
    if(!oz_client_open(&client)){free(m);return 20;}
    m->operation=4;strcpy(m->target,"cli_draid");
    int error=oz_submit(&client,m)!=OZ_OK;const char *parent=NULL;unsigned found=0;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    if(!error){
        error=out->status!=OZ_OK||out->next_cursor!=0;
        for(unsigned i=0;i<out->row_count;i++)
            if(!strcmp(out->rows[i].name,member)){
                if(parent)error=1;
                parent=out->rows[i].parent;
            }
        if(parent)for(unsigned i=0;i<out->row_count;i++){
            const struct oz_row *r=&out->rows[i];
            if(!strcmp(r->name,parent)&&!strcmp(r->kind,"draid")&&
                !strcmp(r->parent,"cli_draid")&&!strcmp(r->state,"ONLINE")){
                *allocated=r->used;found++;
            }
        }
        if(found!=1)error=1;
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);
    if(error)bug("RESULT native dRAID allocation FAIL member=%s\n",member);
    return error?20:0;
}
static int draid_parity_pool(unsigned parity,int expand)
{
    unsigned children=parity+2,total=expand?children*2:children;
    UQUAD allocation_before=0,allocation_after=0;
    char native[10][64],paths[10][258],layout[16];const char *members[10];
    void *zero=calloc(1,1024*1024);if(!zero)return 20;
    for(unsigned i=0;i<total;i++){
#ifdef OZ_TEST_DRAID_EXPAND_PARITY
        snprintf(native[i],sizeof(native[i]),"OZFILES:cli-draid-%u.vdev",i);
        members[i]=native[i];int failed=0;
#else
        snprintf(native[i],sizeof(native[i]),"RAM:typed-draid-%u-%u.vdev",parity,i);
        members[i]=native[i];BPTR f=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!f){free(zero);return 20;}
        int failed=0;
        for(unsigned n=0;n<128;n++)if(Write(f,zero,1024*1024)!=1024*1024){failed=1;break;}
        if(!Close(f))failed=1;
#endif
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))failed=1;
        if(lock)UnLock(lock);
        if(failed){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);
#if defined(OZ_TEST_DRAID_EXPAND_PARITY) && defined(OZ_TEST_DRAID_COLD)
    expected_data="parityIO";
    goto readonly_import;
#endif
    if(typed_draid_change(members,parity,5))return 20;
    snprintf(layout,sizeof(layout),"draid%u",parity);
    RUN(0,layout,"C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:",
        "-o","normalization=formC","-o","utf8only=on","-o","casesensitivity=insensitive","cli_draid/files");
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_draid");
    if(expand){
        RUN(0,NULL,"C:zpool","list","-Hp","-o","size","cli_draid");
        unsigned long long before=strtoull(last_command_output,NULL,10);
        if(!before||typed_draid_change(members+children,parity,9))return 20;
        RUN(0,NULL,"C:zpool","list","-Hp","-o","size","cli_draid");
        unsigned long long after=strtoull(last_command_output,NULL,10);
        if(after<=before)return 20;
        RUN(0,layout,"C:zpool","status","-P","cli_draid");
        char group_name[24];snprintf(group_name,sizeof(group_name),"draid%u:",parity);
        const char *first=strstr(last_command_output,group_name);
        const char *second=first?strstr(first+strlen(group_name),group_name):NULL;
        if(!second||strstr(second+strlen(group_name),group_name))return 20;
        for(unsigned i=0;i<total;i++)if(!strstr(last_command_output,paths[i]))return 20;
        RUN(0,NULL,"C:zpool","sync","cli_draid");
        if(draid_group_allocation(paths[children],&allocation_before))return 20;
        if(growth_payload(0))return 20;
        bug("Typed dRAID capacity before=%llu after=%llu\n",before,after);
    }
    if(!expand){
        for(unsigned i=0;i<parity;i++){RUN(0,NULL,"C:zpool","offline","cli_draid",paths[i]);}
        RUN(0,"state: DEGRADED","C:zpool","status","cli_draid");
    }
    expected_data="parityIO";
    if(dos_file()||growth_payload(1)||growth_payload(0))return 20;
    RUN(0,NULL,"C:zpool","sync","cli_draid");
    if(!expand){
        for(unsigned i=0;i<parity;i++){
            RUN(0,NULL,"C:zpool","online","cli_draid",paths[i]);
        }
        if(resilver_pool("cli_draid",NULL)||growth_payload(0))return 20;
    }else{
        RUN(0,NULL,"C:zpool","scrub","cli_draid");
        int complete=0;char *status[]={"C:zpool","status","cli_draid",NULL};
        for(unsigned retry=0;retry<60;retry++){
            if(run(status,0,NULL))return 20;
            if(strstr(last_command_output,"scrub repaired 0B")&&
                strstr(last_command_output,"with 0 errors on")){complete=1;break;}
            Delay(50);
        }
        if(!complete)return 20;
        if(draid_group_allocation(paths[children],&allocation_after)||
            allocation_after<=allocation_before)return 20;
        bug("Typed dRAID new-group allocation parity=%u before=%llu after=%llu\n",
            parity,(unsigned long long)allocation_before,(unsigned long long)allocation_after);
    }
    RUN(0,"state: ONLINE","C:zpool","status","cli_draid");
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
#if defined(OZ_TEST_DRAID_EXPAND_PARITY) && defined(OZ_TEST_DRAID_COLD)
readonly_import: ;
#endif
    char *args[32]={"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on"};unsigned n=7;
    for(unsigned i=0;i<total;i++){args[n++]="-d";args[n++]=paths[i];}
    args[n++]="cli_draid";args[n]=NULL;if(run(args,0,NULL))return 20;
#ifdef OZ_TEST_DRAID_EXPAND_PARITY
    RUN(0,"state: ONLINE","C:zpool","status","-P","cli_draid");
    char group_name[24];snprintf(group_name,sizeof(group_name),"draid%u:",parity);
    const char *first=strstr(last_command_output,group_name);
    const char *second=first?strstr(first+strlen(group_name),group_name):NULL;
    if(!second||strstr(second+strlen(group_name),group_name))return 20;
    for(unsigned i=0;i<total;i++)if(!strstr(last_command_output,paths[i]))return 20;
#endif
    if(expand){
        UQUAD retained=0;
        if(draid_group_allocation(paths[children],&retained)||retained<=allocation_before)return 20;
        bug("Typed dRAID new-group readonly allocation parity=%u retained=%llu\n",
            parity,(unsigned long long)retained);
    }
    RUN(0,NULL,"C:zfs","mount","cli_draid/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_draid/files");
    RUN(0,NULL,"C:zpool","export","cli_draid");
#ifdef OZ_TEST_DRAID_EXPAND_PARITY
    if(!Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE))return 20;
    expected_data="CLI data";
#ifdef OZ_TEST_DRAID_COLD
    bug("RESULT native typed dRAID expansion cold PASS parity=%u members=%u groups=2 allocated=1 readonly=1 exact-payload=1 exported=1\n",parity,total);
#else
    bug("RESULT native typed dRAID expansion retained PASS parity=%u members=%u groups=2 new-allocation=1 scrub=1 exact-payload=1 exported=1\n",parity,total);
#endif
    return 0;
#endif
    args[6]="readonly=off";if(run(args,0,NULL))return 20;
    RUN(0,NULL,"C:zpool","destroy","cli_draid");
    for(unsigned i=0;i<total;i++)if(!DeleteFile((CONST_STRPTR)native[i]))return 20;
    expected_data="CLI data";
    if(!expand)bug("RESULT native typed dRAID parity PASS parity=%u children=%u degraded-write=1 resilver=1 readonly-reimport=1 payload-bytes=1048576\n",parity,children);
    if(expand)bug("RESULT native typed dRAID add PASS parity=%u groups=2 members=%u capacity-increased=1 scrub=1 readonly-reimport=1 payload-bytes=1048576\n",parity,total);
    if(expand)bug("RESULT native typed dRAID allocation PASS parity=%u new-group-growth=1 readonly-retained=1\n",parity);
    return 0;
}
#endif
static int stream_has(const char *path,const char *needle)
{
    BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);if(!file)return -1;
    if(Seek(file,0,OFFSET_END)==-1){Close(file);return -1;}
    LONG size=Seek(file,0,OFFSET_BEGINNING);
    if(size<=0||size>16*1024*1024){Close(file);return -1;}
    char *bytes=malloc(size);if(!bytes){Close(file);return -1;}
    LONG got=Read(file,bytes,size);int closed=Close(file),found=0;
    if(got!=size||!closed){free(bytes);return -1;}
    size_t n=strlen(needle);
    for(size_t i=0;i+n<=(size_t)size;i++)if(!memcmp(bytes+i,needle,n)){found=1;break;}
    free(bytes);return found;
}
static void events_follower_done(IPTR code,IPTR data)
{
    struct pipeline_completion *done=(void *)data;
    done->status=(LONG)code;
    Close(done->writer);Close(done->errors);
    /* Keep the child task alive until the observer acknowledges completion,
     * so sending its break signal cannot race a freed task structure. */
    struct MsgPort *reply=CreateMsgPort();
    done->message.mn_ReplyPort=reply;
    PutMsg(done->port,&done->message);
    if(reply){WaitPort(reply);GetMsg(reply);DeleteMsgPort(reply);}
}
static int events_follow(void)
{
    BPTR output=Open((CONST_STRPTR)"RAM:events-follow-output",MODE_NEWFILE);
    BPTR errors=Open((CONST_STRPTR)"RAM:events-follow-errors",MODE_NEWFILE);
    BPTR input=Open((CONST_STRPTR)"NIL:",MODE_OLDFILE);
    BPTR segment=LoadSeg((CONST_STRPTR)"C:zpool");
    struct MsgPort *port=CreateMsgPort();
    if(!output||!errors||!input||!segment||!port){
        if(output)Close(output);if(errors)Close(errors);if(input)Close(input);
        if(segment)UnLoadSeg(segment);if(port)DeleteMsgPort(port);return 20;
    }
    struct pipeline_completion done={0};done.port=port;done.writer=output;done.errors=errors;
    done.message.mn_Length=sizeof(done.message);
    struct Process *child=CreateNewProcTags(NP_Seglist,segment,NP_FreeSeglist,TRUE,
        NP_Cli,TRUE,NP_CommandName,(IPTR)"C:zpool",NP_Name,(IPTR)"OpenZFS event follower",
        NP_Arguments,(IPTR)"events -Hf\n",NP_StackSize,262144,
        NP_Input,input,NP_CloseInput,TRUE,NP_Output,output,NP_CloseOutput,FALSE,
        NP_Error,errors,NP_CloseError,FALSE,NP_CopyVars,TRUE,
        NP_ExitCode,(IPTR)events_follower_done,NP_ExitData,(IPTR)&done,TAG_DONE);
    if(!child){Close(output);Close(errors);Close(input);UnLoadSeg(segment);DeleteMsgPort(port);return 20;}
    Delay(10);
    char *status[]={"C:zpool","status","qemu_mirror",NULL};
    char *scrub[]={"C:zpool","scrub","qemu_mirror",NULL};
    char *wait[]={"C:zpool","wait","-t","scrub","qemu_mirror",NULL};
    int failed=run(status,0,"ONLINE")||run(scrub,0,NULL)||run(wait,0,NULL);
    Delay(10);
    Signal(&child->pr_Task,SIGBREAKF_CTRL_C);
    struct Message *message=NULL;
    for(unsigned i=0;i<250&&!message;i++){message=GetMsg(port);if(!message)Delay(1);}
    if(!message){failed=1;bug("RESULT native events follow FAIL no-break-completion\n");
        /* Completion data stays live even if the harness reports failure. */
        WaitPort(port);message=GetMsg(port);}
    failed|=message!=&done.message||done.status==0||!done.message.mn_ReplyPort;
    if(done.message.mn_ReplyPort)ReplyMsg(message);
    DeleteMsgPort(port);
    BPTR read=Open((CONST_STRPTR)"RAM:events-follow-output",MODE_OLDFILE);
    char text[8192];LONG got=read?Read(read,text,sizeof(text)-1):-1;
    if(read)Close(read);
    if(got<0)failed=1;else{text[got]=0;failed|=!strstr(text,"sysevent.fs.zfs.scrub_start")||!strstr(text,"sysevent.fs.zfs.scrub_finish");}
    if(!failed)bug("RESULT native events follow PASS concurrent-status=1 scrub-start=1 scrub-finish=1 break-completed=1\n");
    return failed?20:0;
}

static int events_pool(void)
{
#define ECHECK(v) do{if(!(v)){bug("RESULT native events FAIL line=%u\n",__LINE__);return 20;}}while(0)
    RUN(0,NULL,"C:zpool","events","-c");
    RUN(0,NULL,"C:zpool","scrub","qemu_mirror");
    RUN(0,NULL,"C:zpool","wait","-t","scrub","qemu_mirror");
    RUN(0,"sysevent.fs.zfs","C:zpool","events","-H");
    RUN(0,"sysevent.fs.zfs","C:zpool","events","-H");
    RUN(0,NULL,"C:zpool","events","-H","not_this_pool");
    ECHECK(!last_command_output[0]);
    RUN(0,"sysevent.fs.zfs.scrub_start","C:zpool","events","-H","qemu_mirror");
    ECHECK(strstr(last_command_output,"sysevent.fs.zfs.scrub_finish"));
    bug("RESULT native events filter PASS excluded-empty=1 included-start-finish=1\n");
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);ECHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);ECHECK(secLoginA(reader)==((77UL<<16)|88));
        char *list[]={"C:zpool","events","-H",NULL};
        char *clear[]={"C:zpool","events","-c",NULL};
        int denied=run(list,-1,"cannot get event: permission denied"),
            clear_denied=run(clear,-1,"cannot clear events: permission denied");
        secLogoutA(quiet);ECHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);
        ECHECK(!denied&&!clear_denied);
    }
    RUN(0,"sysevent.fs.zfs","C:zpool","events","-H");
    bug("RESULT native events permissions PASS nonroot-list-denied=1 nonroot-clear-denied=1 queue-retained=1\n");
    ECHECK(!command_worker_test(34,"qemu_mirror","","verbose=on",0,OZ_OK,"sysevent.fs.zfs"));
    ECHECK(!command_worker_test(34,"","","clear=on",0,OZ_ENGINE_ERROR,"before command execution"));
    ECHECK(!command_worker_test(34,"","","clear=on;verbose=on",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    ECHECK(!command_worker_test(34,"","","clear=on",OZ_CONFIRMED,OZ_OK,"cleared"));
    RUN(0,NULL,"C:zpool","events","-H");
    ECHECK(!last_command_output[0]);
    ECHECK(!events_follow());
    bug("RESULT native events PASS CLI=1 independent-readers=2 Manager=1 clear-confirmed=1 invalid=2 empty=1\n");
#undef ECHECK
    return 0;
}

#ifdef OZ_TEST_ZVOL_TRANSPORT
static int redacted_incremental_resume(void)
{
#define ICHECK(x) do{if(!(x)){bug("RESULT native redacted incremental resume FAIL line=%d\n",__LINE__);return 20;}}while(0)
    unsigned char block[8192];
    BPTR source=Open((CONST_STRPTR)"RAM:cli-incremental.stream",MODE_OLDFILE);ICHECK(source);
    BPTR cut=Open((CONST_STRPTR)"RAM:cli-redacted-inc-cut.stream",MODE_NEWFILE);ICHECK(cut);
    for(unsigned n=0;n<64;n++){
        ICHECK(Read(source,block,sizeof(block))==sizeof(block));
        ICHECK(Write(cut,block,sizeof(block))==sizeof(block));
    }
    ICHECK(Close(source));ICHECK(Flush(cut));ICHECK(Close(cut));
    for(unsigned manager=0;manager<2;manager++){
        char *initial[]={"C:zfs","receive","-s","-u","-o","origin=qemu_mirror/cli-redacted-recv@base",
            "-o","mountpoint=OZINCRES:","qemu_mirror/cli-redacted-inc-resume",NULL};
        char *receive[]={"C:zfs","receive","-s","-u","qemu_mirror/cli-redacted-inc-resume",NULL};
        ICHECK(!receive_from_file_expected(initial,"RAM:cli-redacted-inc-cut.stream",1));
        RUN(0,NULL,"C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-redacted-inc-resume");
        char *token=strdup(last_command_output);ICHECK(token);
        token[strcspn(token,"\r\n")]=0;ICHECK(strlen(token)>16&&strcmp(token,"-"));
        if(manager){
            char options[1024];
            ICHECK(snprintf(options,sizeof(options),"resume=%s",token)<(int)sizeof(options));
            ICHECK(!manager_permission_or_create(60,"qemu_mirror/cli-safe@safe","RAM:cli-redacted-inc-resume.stream",options,EINVAL));
            ICHECK(snprintf(options,sizeof(options),"from=qemu_mirror/cli-redact@base;resume=%s",token)<(int)sizeof(options));
            ICHECK(!manager_permission_or_create(60,"qemu_mirror/cli-safe@safe","RAM:cli-redacted-inc-resume.stream",options,EINVAL));
            ICHECK(snprintf(options,sizeof(options),"from=qemu_mirror/cli-redact#mixed;resume=%s",token)<(int)sizeof(options));
            ICHECK(!manager_permission_or_create(60,"qemu_mirror/cli-safe@safe","RAM:cli-redacted-inc-resume.stream",options,ESTALE));
            BPTR absent=Lock((CONST_STRPTR)"RAM:cli-redacted-inc-resume.stream",ACCESS_READ);
            if(absent)UnLock(absent);ICHECK(!absent);
            ICHECK(snprintf(options,sizeof(options),"from=qemu_mirror/cli-redact#native;resume=%s",token)<(int)sizeof(options));
            ICHECK(!manager_permission_or_create(60,"qemu_mirror/cli-safe@safe","RAM:cli-redacted-inc-resume.stream",options,0));
        }else{
            char *send[]={"C:zfs","send","-t",token,NULL};
            ICHECK(!send_to_file(send,"RAM:cli-redacted-inc-resume.stream"));
        }
        free(token);
        ICHECK(!receive_from_file(receive,"RAM:cli-redacted-inc-resume.stream"));
        RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-redacted-inc-resume");
        RUN(0,"-","C:zfs","get","-H","-o","value","redact_snaps","qemu_mirror/cli-redacted-inc-resume@safe");
        RUN(0,"qemu_mirror/cli-redacted-recv@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/cli-redacted-inc-resume");
        RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redacted-inc-resume");
        BPTR file=Open((CONST_STRPTR)"OZINCRES:incremental-public",MODE_OLDFILE);ICHECK(file);
        for(unsigned n=0;n<128;n++){
            ICHECK(Read(file,block,sizeof(block))==sizeof(block));
            for(unsigned j=0;j<sizeof(block);j++)ICHECK(block[j]==(unsigned char)(j*19u+j/113u));
        }
        ICHECK(Read(file,block,1)==0);ICHECK(Close(file));
        RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redacted-inc-resume");
        RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-inc-resume");
        ICHECK(DeleteFile((CONST_STRPTR)"RAM:cli-redacted-inc-resume.stream"));
    }
    ICHECK(DeleteFile((CONST_STRPTR)"RAM:cli-redacted-inc-cut.stream"));
    bug("RESULT native redacted incremental resume PASS CLI=1 Manager=1 missing-base=1 intact-base-refused=1 wrong-redaction-set=1 output-absent=1 DOS-MiB=2 token-cleared=2 cleanup=1\n");
    return 0;
#undef ICHECK
}
static int redact_pool(void)
{
#define RCHECK(x) do{if(!(x)){bug("RESULT native redaction FAIL line=%d\n",__LINE__);return 20;}}while(0)
    RUN(0,NULL,"C:zpool","set","feature@redaction_bookmarks=enabled","qemu_mirror");
    RUN(0,NULL,"C:zpool","set","feature@redacted_datasets=enabled","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZREDACT:","-o","compression=off","-o","dedup=off","-o","recordsize=128K","qemu_mirror/cli-redact");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redact");
    const char *public="OPENZFS-AROS-PUBLIC-CANARY-0123456789";
    const char *secret="OPENZFS-AROS-SECRET-CANARY-9876543210";
    char *bytes=malloc(128*1024);RCHECK(bytes);
    const char *paths[]={"OZREDACT:public","OZREDACT:secret"};
    for(unsigned n=0;n<2;n++){
        memset(bytes,n?'S':'P',128*1024);memcpy(bytes,n?secret:public,strlen(n?secret:public));
        BPTR file=Open((CONST_STRPTR)paths[n],MODE_NEWFILE);RCHECK(file);
        RCHECK(Write(file,bytes,128*1024)==128*1024);RCHECK(Close(file));
    }
    /* Retained multi-record data makes a partial redacted receive resumable
     * beyond its header; the later completed dataset is read byte-for-byte. */
    for(unsigned n=0;n<128*1024;n++)bytes[n]=(char)(n*37u+n/251u);
    BPTR payload=Open((CONST_STRPTR)"OZREDACT:resume-public",MODE_NEWFILE);RCHECK(payload);
    for(unsigned n=0;n<8;n++)RCHECK(Write(payload,bytes,128*1024)==128*1024);
    RCHECK(Flush(payload));RCHECK(Close(payload));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redact");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-redact@base");
    RUN(0,NULL,"C:zfs","clone","-u","-o","mountpoint=OZSAFE:","qemu_mirror/cli-redact@base","qemu_mirror/cli-safe");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-safe");
    BPTR file=Open((CONST_STRPTR)"OZSAFE:secret",MODE_NEWFILE);RCHECK(file);
    memset(bytes,'X',128*1024);RCHECK(Write(file,bytes,128*1024)==128*1024);RCHECK(Close(file));free(bytes);
    unsigned char incremental_block[8192];
    for(unsigned n=0;n<sizeof(incremental_block);n++)incremental_block[n]=(unsigned char)(n*19u+n/113u);
    file=Open((CONST_STRPTR)"OZSAFE:incremental-public",MODE_NEWFILE);RCHECK(file);
    for(unsigned n=0;n<128;n++)RCHECK(Write(file,incremental_block,sizeof(incremental_block))==sizeof(incremental_block));
    RCHECK(Flush(file));RCHECK(Close(file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-safe");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-safe@safe");
    const char *opts="snapshots=qemu_mirror/cli-safe@safe";
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","native",opts,0,OZ_ENGINE_ERROR,"before command execution"));
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","native","snapshots=",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","native","snapshots=qemu_mirror/cli-safe@safe,",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);RCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);RCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=command_worker_test(59,"qemu_mirror/cli-redact@base","native",opts,OZ_CONFIRMED,OZ_ENGINE_ERROR,"internal error");
        int send_denied=manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
            "RAM:manager-redact-invalid.stream","redact=native",EPERM);
        secLogoutA(quiet);RCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);RCHECK(!denied&&!send_denied);
    }
    RUN(1,NULL,"C:zfs","list","-t","bookmark","qemu_mirror/cli-redact#native");
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","native",opts,OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","guid","qemu_mirror/cli-safe@safe");
    char guid[64];snprintf(guid,sizeof(guid),"%s",last_command_output);guid[strcspn(guid,"\r\n")]=0;RCHECK(guid[0]);
    RUN(0,guid,"C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redact#native");
    const char *bad_send[]={"redact=","redact=native;redact=native",
        "redact=qemu_mirror/cli-redact#native","redact=native@bad",
        "redact=native;resume=invalid","redact=native;"};
    for(unsigned i=0;i<sizeof(bad_send)/sizeof(bad_send[0]);i++)
        RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
            "RAM:manager-redact-invalid.stream",bad_send[i],EINVAL));
    BPTR invalid_output=Lock((CONST_STRPTR)"RAM:manager-redact-invalid.stream",ACCESS_READ);
    if(invalid_output)UnLock(invalid_output);
    RCHECK(!invalid_output);
    bug("RESULT Manager redacted send refusal PASS malformed=6 nonroot-denied=1 output-absent=1\n");
    char *plain[]={"C:zfs","send","qemu_mirror/cli-redact@base",NULL};
    char *redacted[]={"C:zfs","send","--redact","native","qemu_mirror/cli-redact@base",NULL};
    RCHECK(!send_to_file(plain,"RAM:cli-plain.stream"));
    RCHECK(stream_has("RAM:cli-plain.stream",secret)==1);
    RCHECK(!send_to_file(redacted,"RAM:cli-redacted.stream"));
    RCHECK(stream_has("RAM:cli-redacted.stream",secret)==0);
    RCHECK(stream_has("RAM:cli-redacted.stream",public)==1);
    RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
        "RAM:manager-redacted.stream","redact=native",0));
    RCHECK(stream_has("RAM:manager-redacted.stream",secret)==0);
    RCHECK(stream_has("RAM:manager-redacted.stream",public)==1);
    char *manager_receive[]={"C:zfs","receive","-u","qemu_mirror/manager-redacted-recv",NULL};
    RCHECK(!receive_from_file(manager_receive,"RAM:manager-redacted.stream"));
    RUN(0,guid,"C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/manager-redacted-recv@base");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/manager-redacted-recv");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:manager-redacted.stream"));
    bug("RESULT Manager redacted send PASS secret-absent=1 public-retained=1 receive-guid=1\n");
    char *receive[]={"C:zfs","receive","-u","qemu_mirror/cli-redacted-recv",NULL};
    RCHECK(!receive_from_file(receive,"RAM:cli-redacted.stream"));
    RUN(0,guid,"C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redacted-recv@base");
    unsigned char cut_block[8192];
    BPTR source=Open((CONST_STRPTR)"RAM:cli-redacted.stream",MODE_OLDFILE);RCHECK(source);
    BPTR cut=Open((CONST_STRPTR)"RAM:cli-redacted-cut.stream",MODE_NEWFILE);RCHECK(cut);
    for(unsigned n=0;n<64;n++){
        RCHECK(Read(source,cut_block,sizeof(cut_block))==sizeof(cut_block));
        RCHECK(Write(cut,cut_block,sizeof(cut_block))==sizeof(cut_block));
    }
    RCHECK(Close(source));RCHECK(Flush(cut));RCHECK(Close(cut));
    char *resume_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/cli-redacted-resumed",NULL};
    RCHECK(!receive_from_file_expected(resume_receive,"RAM:cli-redacted-cut.stream",1));
    RUN(0,NULL,"C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-redacted-resumed");
    char *token=strdup(last_command_output);RCHECK(token);
    token[strcspn(token,"\r\n")]=0;RCHECK(strlen(token)>16&&strcmp(token,"-"));
    char *resume_send[]={"C:zfs","send","-t",token,NULL};
    RCHECK(!send_to_file(resume_send,"RAM:cli-redacted-resume.stream"));
    char resume_options[1024];
    /* Mutate a real receiver token, keeping the legitimate one for the
     * successful continuation below. Refusals must not create output. */
    for(unsigned invalid=0;invalid<3;invalid++){
        char *bad=strdup(token);RCHECK(bad);
        size_t length=strlen(bad);RCHECK(length>16);
        if(invalid==0)bad[0]='2'; /* unsupported wire version */
        else if(invalid==1)bad[length-1]=0; /* odd/truncated hex payload */
        else bad[length-1]=bad[length-1]=='0'?'1':'0'; /* checksum mismatch */
        RCHECK(snprintf(resume_options,sizeof(resume_options),"resume=%s",bad)<(int)sizeof(resume_options));
        free(bad);
        RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
            "RAM:manager-redacted-invalid.stream",resume_options,EINVAL));
        BPTR unexpected=Lock((CONST_STRPTR)"RAM:manager-redacted-invalid.stream",ACCESS_READ);
        if(unexpected)UnLock(unexpected);RCHECK(!unexpected);
    }
    bug("RESULT native redacted resume malformed wire PASS version=1 truncated=1 checksum=1 output-absent=3\n");
    RCHECK(snprintf(resume_options,sizeof(resume_options),"resume=%s",token)<(int)sizeof(resume_options));
    free(token);
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);RCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);RCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
            "RAM:manager-redacted-resume.stream",resume_options,EPERM);
        secLogoutA(quiet);RCHECK(secLoginA(root)==0xffffffffUL);CloseLibrary(secBase);RCHECK(!denied);
    }
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#native");
    RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
        "RAM:manager-redacted-resume.stream",resume_options,ENOENT));
    BPTR absent=Lock((CONST_STRPTR)"RAM:manager-redacted-resume.stream",ACCESS_READ);
    if(absent)UnLock(absent);RCHECK(!absent);
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","replacement",opts,OZ_CONFIRMED,OZ_OK,"exit=0"));
    RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@base",
        "RAM:manager-redacted-resume.stream",resume_options,0));
    RCHECK(stream_has("RAM:manager-redacted-resume.stream",secret)==0);
    char *manager_resume_receive[]={"C:zfs","receive","-s","-u","qemu_mirror/manager-redacted-resumed",NULL};
    RCHECK(!receive_from_file_expected(manager_resume_receive,"RAM:cli-redacted-cut.stream",1));
    RCHECK(!receive_from_file(manager_resume_receive,"RAM:manager-redacted-resume.stream"));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/manager-redacted-resumed");
    RUN(0,guid,"C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/manager-redacted-resumed@base");
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","native",opts,OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#replacement");
    RCHECK(stream_has("RAM:cli-redacted-resume.stream",secret)==0);
    RCHECK(!receive_from_file(resume_receive,"RAM:cli-redacted-resume.stream"));
    RUN(0,"-","C:zfs","get","-H","-o","value","receive_resume_token","qemu_mirror/cli-redacted-resumed");
    RUN(0,guid,"C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redacted-resumed@base");
    /* A second selected snapshot still referencing the old block must keep
     * it in the stream. Only absence from both permits its redaction. */
    RUN(0,NULL,"C:zfs","clone","-u","-o","mountpoint=OZSAFE2:","qemu_mirror/cli-redact@base","qemu_mirror/cli-safe2");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-safe2@original");
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","mixed","snapshots=qemu_mirror/cli-safe@safe,qemu_mirror/cli-safe2@original",OZ_CONFIRMED,OZ_OK,"exit=0"));
    char *mixed[]={"C:zfs","send","--redact","mixed","qemu_mirror/cli-redact@base",NULL};
    RCHECK(!send_to_file(mixed,"RAM:cli-multi.stream"));
    RCHECK(stream_has("RAM:cli-multi.stream",secret)==1);
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-safe2");
    bytes=malloc(128*1024);RCHECK(bytes);memset(bytes,'Y',128*1024);
    file=Open((CONST_STRPTR)"OZSAFE2:secret",MODE_NEWFILE);RCHECK(file);
    RCHECK(Write(file,bytes,128*1024)==128*1024);RCHECK(Close(file));free(bytes);
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-safe2");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-safe2@safe");
    RCHECK(!command_worker_test(59,"qemu_mirror/cli-redact@base","both","snapshots=qemu_mirror/cli-safe@safe,qemu_mirror/cli-safe2@safe",OZ_CONFIRMED,OZ_OK,"exit=0"));
    char *both[]={"C:zfs","send","--redact","both","qemu_mirror/cli-redact@base",NULL};
    RCHECK(!send_to_file(both,"RAM:cli-multi.stream"));
    RCHECK(stream_has("RAM:cli-multi.stream",secret)==0);
    RCHECK(stream_has("RAM:cli-multi.stream",public)==1);
    bug("RESULT native multi-redaction PASS selected=2 referenced-retained=1 absent-both-omitted=1 public-retained=1\n");
    /* Clone incrementals create a new destination. Pin its origin to the
     * received redacted snapshot, not the intact same-GUID source nearby. */
    char *incremental[]={"C:zfs","send","-i","qemu_mirror/cli-redact#native","qemu_mirror/cli-safe@safe",NULL};
    RCHECK(!send_to_file(incremental,"RAM:cli-incremental.stream"));
    RCHECK(!redacted_incremental_resume());
    char *complete[]={"C:zfs","receive","-u","-o","origin=qemu_mirror/cli-redacted-recv@base","-o","mountpoint=OZRESTORE:","qemu_mirror/cli-redacted-complete",NULL};
    RCHECK(!receive_from_file(complete,"RAM:cli-incremental.stream"));
    RUN(0,"qemu_mirror/cli-redacted-recv@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/cli-redacted-complete");
    RUN(0,"-","C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redacted-complete@safe");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redacted-complete");
    bytes=malloc(128*1024);RCHECK(bytes);
    file=Open((CONST_STRPTR)"OZRESTORE:public",MODE_OLDFILE);RCHECK(file);
    RCHECK(Read(file,bytes,128*1024)==128*1024);RCHECK(Close(file));
    RCHECK(!memcmp(bytes,public,strlen(public)));
    for(unsigned n=strlen(public);n<128*1024;n++)RCHECK(bytes[n]=='P');
    file=Open((CONST_STRPTR)"OZRESTORE:secret",MODE_OLDFILE);RCHECK(file);
    RCHECK(Read(file,bytes,128*1024)==128*1024);RCHECK(Close(file));
    for(unsigned n=0;n<128*1024;n++)RCHECK(bytes[n]=='X');
    free(bytes);RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redacted-complete");
    bug("RESULT native redacted incremental PASS received=1 redacted-origin=1 unredacted=1 DOS-public-KiB=128 DOS-replacement-KiB=128\n");
    char *resume_complete[]={"C:zfs","receive","-u","-o","origin=qemu_mirror/cli-redacted-resumed@base","-o","mountpoint=OZREDRES:","qemu_mirror/cli-redacted-resume-complete",NULL};
    RCHECK(!receive_from_file(resume_complete,"RAM:cli-incremental.stream"));
    RUN(0,"qemu_mirror/cli-redacted-resumed@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/cli-redacted-resume-complete");
    RUN(0,"-","C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redacted-resume-complete@safe");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redacted-resume-complete");
    bytes=malloc(128*1024);RCHECK(bytes);
    file=Open((CONST_STRPTR)"OZREDRES:resume-public",MODE_OLDFILE);RCHECK(file);
    for(unsigned block=0;block<8;block++){
        RCHECK(Read(file,bytes,128*1024)==128*1024);
        for(unsigned n=0;n<128*1024;n++)RCHECK((unsigned char)bytes[n]==(unsigned char)(n*37u+n/251u));
    }
    RCHECK(Read(file,bytes,1)==0);RCHECK(Close(file));free(bytes);
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redacted-resume-complete");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-resume-complete");
    resume_complete[4]="origin=qemu_mirror/manager-redacted-resumed@base";
    RCHECK(!receive_from_file(resume_complete,"RAM:cli-incremental.stream"));
    RUN(0,"qemu_mirror/manager-redacted-resumed@base","C:zfs","get","-H","-o","value","origin","qemu_mirror/cli-redacted-resume-complete");
    RUN(0,"-","C:zfs","get","-H","-p","-o","value","redact_snaps","qemu_mirror/cli-redacted-resume-complete@safe");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redacted-resume-complete");
    bytes=malloc(128*1024);RCHECK(bytes);
    file=Open((CONST_STRPTR)"OZREDRES:resume-public",MODE_OLDFILE);RCHECK(file);
    for(unsigned block=0;block<8;block++){
        RCHECK(Read(file,bytes,128*1024)==128*1024);
        for(unsigned n=0;n<128*1024;n++)RCHECK((unsigned char)bytes[n]==(unsigned char)(n*37u+n/251u));
    }
    RCHECK(Read(file,bytes,1)==0);RCHECK(Close(file));free(bytes);
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redacted-resume-complete");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-resume-complete");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/manager-redacted-resumed");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:manager-redacted-resume.stream"));
    bug("RESULT native Manager redacted resume PASS token=1 missing-bookmark-denied=1 output-absent=1 replacement-bookmark=1 unauthorized-denied=1 secret-absent=1 received=1 DOS-MiB=1 cleanup=1\n");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-resumed");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-redacted-cut.stream"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-redacted-resume.stream"));
    bug("RESULT native redacted resume PASS truncated=1 CLI-token=1 secret-absent=1 receive-guid=1 token-cleared=1 incremental-origin=1 DOS-MiB=1 cleanup=1\n");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-complete");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redacted-recv");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-safe2");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-safe");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#mixed");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#both");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#native");
    /* Receive redacted data into a live ordinary namespace. Upstream must
     * end that mount instead of resuming cached ordinary file handles. */
    char *live_base[]={"C:zfs","receive","-u","-o","mountpoint=OZLIVERED:",
        "-o","readonly=on","qemu_mirror/live-redacted",NULL};
    RCHECK(!receive_from_file(live_base,"RAM:cli-plain.stream"));
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/live-redacted");
    BPTR retained=Open((CONST_STRPTR)"OZLIVERED:secret",MODE_OLDFILE);RCHECK(retained);
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-redact");
    file=Open((CONST_STRPTR)"OZREDACT:secret",MODE_NEWFILE);RCHECK(file);
    RCHECK(Write(file,"new secret",10)==10);RCHECK(Close(file));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-redact");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-redact@live");
    RUN(0,NULL,"C:zfs","clone","-u","-o","mountpoint=OZLIVESAFE:","qemu_mirror/cli-redact@live","qemu_mirror/live-safe");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/live-safe");
    RCHECK(DeleteFile((CONST_STRPTR)"OZLIVESAFE:secret"));
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/live-safe");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/live-safe@safe");
    RUN(0,NULL,"C:zfs","redact","qemu_mirror/cli-redact@live","live","qemu_mirror/live-safe@safe");
    RCHECK(!manager_permission_or_create(60,"qemu_mirror/cli-redact@live","RAM:live-redacted.stream",
        "from=qemu_mirror/cli-redact@base;redact=live",0));
    RCHECK(!manager_permission_or_create(61,"qemu_mirror/live-redacted@live","RAM:live-redacted.stream","",0));
    char stale[16];RCHECK(Read(retained,stale,sizeof(stale))==-1);RCHECK(Close(retained));
    struct Process *self=(struct Process *)FindTask(NULL);
    APTR prior_window=self->pr_WindowPtr;self->pr_WindowPtr=(APTR)-1;
    BPTR ended=Lock((CONST_STRPTR)"OZLIVERED:public",ACCESS_READ);
    self->pr_WindowPtr=prior_window;
    if(ended)UnLock(ended);RCHECK(!ended);
    /* The service drains ended mounts automatically after the retained
     * handle closes. Confirm withdrawal, then destroy to prove release of
     * dataset ownership rather than demanding a second successful unmount. */
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","qemu_mirror/live-redacted");
    RUN(1,"not currently mounted","C:zfs","unmount","qemu_mirror/live-redacted");
    RUN(0,"1","C:zfs","get","-H","-o","value","redacted","qemu_mirror/live-redacted");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/live-redacted");
    RCHECK(!receive_from_file(live_base,"RAM:cli-plain.stream"));
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/live-redacted");
    retained=Open((CONST_STRPTR)"OZLIVERED:secret",MODE_OLDFILE);RCHECK(retained);
    char *live_cli[]={"C:zfs","receive","-u","qemu_mirror/live-redacted",NULL};
    RCHECK(!receive_from_file(live_cli,"RAM:live-redacted.stream"));
    RCHECK(Read(retained,stale,sizeof(stale))==-1);RCHECK(Close(retained));
    prior_window=self->pr_WindowPtr;self->pr_WindowPtr=(APTR)-1;
    ended=Lock((CONST_STRPTR)"OZLIVERED:public",ACCESS_READ);
    self->pr_WindowPtr=prior_window;
    if(ended)UnLock(ended);RCHECK(!ended);
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","qemu_mirror/live-redacted");
    RUN(1,"not currently mounted","C:zfs","unmount","qemu_mirror/live-redacted");
    RUN(0,"1","C:zfs","get","-H","-o","value","redacted","qemu_mirror/live-redacted");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/live-redacted");
    bug("RESULT native CLI mounted redacted receive PASS received=1 retained-read-denied=1 namespace-ended=1 unmounted=1 cleanup=1\n");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/live-safe");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-redact#live");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:live-redacted.stream"));
    bug("RESULT native Manager mounted redacted receive PASS received=1 retained-read-denied=1 namespace-ended=1 unmounted=1 cleanup=1\n");
    RUN(0,NULL,"C:zfs","destroy","-r","qemu_mirror/cli-redact");
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-plain.stream"));RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-redacted.stream"));
    RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-multi.stream"));RCHECK(DeleteFile((CONST_STRPTR)"RAM:cli-incremental.stream"));
    bug("RESULT native redaction PASS Manager=1 nonroot-denied=1 invalid=3 bookmark-guid=1 plain-secret=1 redacted-secret-absent=1 public-retained=1 receive-guid=1 cleanup=1\n");
    return 0;
#undef RCHECK
}
#endif
static int channel_program(void)
{
#define PCHECK(x) do{if(!(x)){bug("RESULT native channel program FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *native="RAM:cli-program.lua";
    const char *hello="local a=...; assert(a.argv[1]=='hello world'); return {marker='AROS-ZCP-OK'}";
    BPTR file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,hello,strlen(hello))==(LONG)strlen(hello));PCHECK(Close(file));
    char canonical[256],path[258];BPTR lock=Lock((CONST_STRPTR)native,ACCESS_READ);PCHECK(lock);
    int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);PCHECK(named);
    char *colon=strchr(canonical,':');PCHECK(colon);*colon='/';snprintf(path,sizeof(path),"/%s",canonical);
    RUN(0,"AROS-ZCP-OK","C:zfs","program","qemu_mirror",path,"hello world");
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"sync=off;json=on;arg=hello world",OZ_CONFIRMED,OZ_OK,"AROS-ZCP-OK"));
    const char *invalid[]={"instructions=0","memory=-1","instructions=1;instructions=2","sync=maybe","unknown=1","memory=18446744073709551616"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        PCHECK(!command_worker_test(75,"qemu_mirror",path,invalid[i],OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"",0,OZ_ENGINE_ERROR,"before command execution"));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);PCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);PCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=command_worker_test(75,"qemu_mirror",path,"arg=hello world",OZ_CONFIRMED,OZ_ENGINE_ERROR,"Permission denied");
        secLogoutA(quiet);PCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);PCHECK(!denied);
    }
    RUN(0,NULL,"C:zfs","create","-u","qemu_mirror/cli-program");
    const char *snapshot="assert(zfs.sync.snapshot('qemu_mirror/cli-program@native')==0); return {marker='SNAPSHOT-OK'}";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,snapshot,strlen(snapshot))==(LONG)strlen(snapshot));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"sync=off",OZ_CONFIRMED,OZ_ENGINE_ERROR,"zfs.sync"));
    RUN(1,NULL,"C:zfs","list","qemu_mirror/cli-program@native");
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"",OZ_CONFIRMED,OZ_OK,"SNAPSHOT-OK"));
    RUN(0,"qemu_mirror/cli-program@native","C:zfs","list","-H","qemu_mirror/cli-program@native");
    PCHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/cli-program@native","OZPROGRAM",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
    const char *rename_busy="local e=zfs.sync.rename_snapshot('qemu_mirror/cli-program','native','moved'); assert(e==EBUSY,'rename errno='..tostring(e)..' expected='..tostring(EBUSY)); return {marker='MOUNTED-REFUSED'}";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,rename_busy,strlen(rename_busy))==(LONG)strlen(rename_busy));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"",OZ_CONFIRMED,OZ_OK,"MOUNTED-REFUSED"));
    RUN(0,"qemu_mirror/cli-program@native","C:zfs","list","-H","qemu_mirror/cli-program@native");
    PCHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/cli-program@native","",OZ_MOUNT_DETACH,0));
    const char *rename_ok="assert(zfs.sync.rename_snapshot('qemu_mirror/cli-program','native','moved')==0); return {marker='RENAME-OK'}";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,rename_ok,strlen(rename_ok))==(LONG)strlen(rename_ok));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"",OZ_CONFIRMED,OZ_OK,"RENAME-OK"));
    RUN(0,"qemu_mirror/cli-program@moved","C:zfs","list","-H","qemu_mirror/cli-program@moved");
    RUN(0,NULL,"C:zfs","rename","qemu_mirror/cli-program@moved","qemu_mirror/cli-program@native");
    bug("RESULT native channel mounted rename PASS refused=1 original-retained=1 unmounted-rename=1\n");
    const char *loop="while true do end";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,loop,strlen(loop))==(LONG)strlen(loop));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"instructions=1000",OZ_CONFIRMED,OZ_ENGINE_ERROR,"instructions"));
    const char *memory="local t={}; for i=1,100000 do t[i]=string.rep('x',1024)..i end return {marker='unexpected'}";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,memory,strlen(memory))==(LONG)strlen(memory));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"memory=262144",OZ_CONFIRMED,OZ_ENGINE_ERROR,"Memory limit exhausted"));
    const char *syntax="this is not lua";
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,syntax,strlen(syntax))==(LONG)strlen(syntax));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"",OZ_CONFIRMED,OZ_ENGINE_ERROR,"Channel program execution failed"));
    file=Open((CONST_STRPTR)native,MODE_NEWFILE);PCHECK(file);
    PCHECK(Write(file,hello,strlen(hello))==(LONG)strlen(hello));PCHECK(Close(file));
    PCHECK(!command_worker_test(75,"qemu_mirror",path,"arg=hello world",OZ_CONFIRMED,OZ_OK,"AROS-ZCP-OK"));
    bug("RESULT native channel limits PASS instructions=1 memory=1 syntax=1 valid-after-errors=1\n");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-program@native");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-program");PCHECK(DeleteFile((CONST_STRPTR)native));
    bug("RESULT native channel program PASS CLI=1 Manager=1 arguments=1 json=1 nonroot-denied=1 nosync-denied=1 snapshot=1 instruction-limit=1 recovery=1 invalid=7 cleanup=1\n");
    return 0;
#undef PCHECK
}
static int upgrade_pool(void)
{
#define UCHECK(x) do{if(!(x)){bug("RESULT native pool upgrade FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const char *native="RAM:cli-upgrade.vdev";
    char canonical[256],path[258];char *zero=calloc(1,1024*1024);UCHECK(zero);
    BPTR file=Open((CONST_STRPTR)native,MODE_NEWFILE);UCHECK(file);
    for(unsigned n=0;n<128;n++)UCHECK(Write(file,zero,1024*1024)==1024*1024);
    UCHECK(Close(file));free(zero);
    BPTR lock=Lock((CONST_STRPTR)native,ACCESS_READ);UCHECK(lock);
    int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);UCHECK(named);
    char *colon=strchr(canonical,':');UCHECK(colon);*colon='/';snprintf(path,sizeof(path),"/%s",canonical);
    RUN(0,NULL,"C:zpool","create","-d","-o","version=26","-o","cachefile=none","-O","mountpoint=none","cli_upgrade",path);
    RUN(0,"26","C:zpool","get","-H","-o","value","version","cli_upgrade");
    RUN(1,"is a member","C:zpool","labelclear","-f",path);
    UCHECK(!command_worker_test(29,path,"","",OZ_CONFIRMED|OZ_FORCE,OZ_ENGINE_ERROR,"is a member"));
    UCHECK(!command_worker_test(29,path,"","",0,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(29,path,"","",OZ_CONFIRMED|OZ_DRYRUN,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(29,path,"","force=on",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    bug("RESULT native labelclear active refusal PASS forced=1\n");
    RUN(0,NULL,"C:zfs","create","-u","-o","version=4","-o","mountpoint=OZUPGRADE:","cli_upgrade/files");
    RUN(0,"4","C:zfs","get","-H","-o","value","version","cli_upgrade/files");
    RUN(0,NULL,"C:zfs","mount","cli_upgrade/files");
    file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_NEWFILE);UCHECK(file);
    const char payload[]="upgrade preserved";
    UCHECK(Write(file,payload,sizeof(payload))==sizeof(payload));UCHECK(Close(file));
    char rewrite_readback[sizeof(payload)];
    file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_READWRITE);UCHECK(file);
    struct FileHandle *rewrite_fh=BADDR(file);UCHECK(rewrite_fh&&rewrite_fh->fh_Type);
    struct oz_rewrite_request rewrite={.version=OZ_REWRITE_VERSION,.offset=1,.length=5};
    UCHECK(DoPkt(rewrite_fh->fh_Type,OZ_ACTION_REWRITE,rewrite_fh->fh_Arg1,
        (SIPTR)&rewrite,sizeof(rewrite)-1,0,0)==DOSFALSE&&IoErr()==ERROR_BAD_NUMBER);
    rewrite.flags=2;
    UCHECK(DoPkt(rewrite_fh->fh_Type,OZ_ACTION_REWRITE,rewrite_fh->fh_Arg1,
        (SIPTR)&rewrite,sizeof(rewrite),0,0)==DOSFALSE&&IoErr()==ERROR_BAD_NUMBER);
    rewrite.flags=0;
    UCHECK(DoPkt(rewrite_fh->fh_Type,OZ_ACTION_REWRITE,rewrite_fh->fh_Arg1,
        (SIPTR)&rewrite,sizeof(rewrite),0,0)==DOSTRUE&&!rewrite.engine_error);
    UCHECK(Close(file));
    RUN(0,NULL,"C:zfs","rewrite","-o","2","-l","7","OZUPGRADE:payload");
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","offset=3;length=6",OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(ENOTSUP,"Not supported","C:zfs","rewrite","-P","OZUPGRADE:payload");
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","unexpected","",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","offset=-1",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","offset=1;offset=2",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","xdev=on",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","",0,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","",OZ_CONFIRMED|OZ_FORCE,OZ_ENGINE_ERROR,"before command execution"));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);UCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);UCHECK(secLoginA(reader)==((77UL<<16)|88));
        int rewrite_denied=command_worker_test(48,"OZUPGRADE:payload","","",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL);
        secLogoutA(quiet);UCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);UCHECK(!rewrite_denied);
    }
    file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_OLDFILE);UCHECK(file);
    UCHECK(Read(file,rewrite_readback,sizeof(rewrite_readback))==sizeof(rewrite_readback));UCHECK(Close(file));
    UCHECK(!memcmp(rewrite_readback,payload,sizeof(payload)));
    RUN(0,NULL,"C:zfs","unmount","cli_upgrade/files");
    UCHECK(!command_worker_test(25,"cli_upgrade","","version=28",0,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(25,"cli_upgrade","","version=29",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(25,"cli_upgrade","","all=on",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    {
        struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);UCHECK(secBase&&secIsConfigured());
        struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
        struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
        secLogoutA(quiet);UCHECK(secLoginA(reader)==((77UL<<16)|88));
        int denied=command_worker_test(25,"cli_upgrade","","version=28",OZ_CONFIRMED,OZ_ENGINE_ERROR,"permission denied");
        int dataset_denied=command_worker_test(47,"cli_upgrade/files","","",OZ_CONFIRMED,OZ_ENGINE_ERROR,"permission denied");
        secLogoutA(quiet);UCHECK(secLoginA(root)==secOWNER_SYSTEM);CloseLibrary(secBase);UCHECK(!denied);
        UCHECK(!dataset_denied);
    }
    RUN(0,"26","C:zpool","get","-H","-o","value","version","cli_upgrade");
    RUN(0,"4","C:zfs","get","-H","-o","value","version","cli_upgrade/files");
    UCHECK(!command_worker_test(47,"cli_upgrade/files","unexpected","",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(47,"cli_upgrade/files","","version=6",OZ_CONFIRMED,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(47,"cli_upgrade/files","","",0,OZ_ENGINE_ERROR,"before command execution"));
    UCHECK(!command_worker_test(47,"cli_upgrade/files","","",OZ_CONFIRMED|OZ_FORCE,OZ_ENGINE_ERROR,"before command execution"));
    RUN(0,"4","C:zfs","get","-H","-o","value","version","cli_upgrade/files");
    UCHECK(!command_worker_test(47,"cli_upgrade/files","","",OZ_CONFIRMED,OZ_OK,"1 filesystems upgraded"));
    RUN(0,"5","C:zfs","get","-H","-o","value","version","cli_upgrade/files");
    UCHECK(!command_worker_test(25,"cli_upgrade","","version=28",OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,"28","C:zpool","get","-H","-o","value","version","cli_upgrade");
    UCHECK(!command_worker_test(25,"cli_upgrade","","",OZ_CONFIRMED,OZ_OK,"exit=0"));
    RUN(0,NULL,"C:zpool","get","-H","-o","value","feature@lz4_compress","cli_upgrade");
    UCHECK(!strcmp(last_command_output,"enabled\n")||!strcmp(last_command_output,"active\n"));
    RUN(0,NULL,"C:zpool","export","cli_upgrade");
    RUN(1,"import request not supported","C:zpool","import","-N","-d",path,"cli_upgrade");
    UCHECK(!strstr(last_command_output,"out of memory"));
    RUN(1,NULL,"C:zpool","list","cli_upgrade");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",path,"cli_upgrade");
    RUN(0,NULL,"C:zpool","get","-H","-o","value","feature@lz4_compress","cli_upgrade");
    UCHECK(!strcmp(last_command_output,"enabled\n")||!strcmp(last_command_output,"active\n"));
    RUN(0,NULL,"C:zpool","get","-H","-o","value","feature@physical_rewrite","cli_upgrade");
    UCHECK(!strcmp(last_command_output,"enabled\n")||!strcmp(last_command_output,"active\n"));
    RUN(0,NULL,"C:zfs","mount","cli_upgrade/files");
    char readback[sizeof(payload)];file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_OLDFILE);UCHECK(file);
    UCHECK(Read(file,readback,sizeof(readback))==sizeof(readback));UCHECK(Close(file));UCHECK(!memcmp(readback,payload,sizeof(payload)));
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","physical=on;offset=1;length=5",OZ_CONFIRMED,OZ_OK,"exit=0"));
    file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_OLDFILE);UCHECK(file);
    UCHECK(Read(file,rewrite_readback,sizeof(rewrite_readback))==sizeof(rewrite_readback));UCHECK(Close(file));
    UCHECK(!memcmp(rewrite_readback,payload,sizeof(payload)));
    RUN(0,NULL,"C:zfs","unmount","cli_upgrade/files");
    RUN(0,NULL,"C:zpool","export","cli_upgrade");
    RUN(0,NULL,"C:zpool","import","-N","-o","readonly=on","-o","cachefile=none","-d",path,"cli_upgrade");
    RUN(0,NULL,"C:zfs","mount","cli_upgrade/files");
    UCHECK(!command_worker_test(48,"OZUPGRADE:payload","","",OZ_CONFIRMED,OZ_ENGINE_ERROR,NULL));
    file=Open((CONST_STRPTR)"OZUPGRADE:payload",MODE_OLDFILE);UCHECK(file);
    UCHECK(Read(file,rewrite_readback,sizeof(rewrite_readback))==sizeof(rewrite_readback));UCHECK(Close(file));
    UCHECK(!memcmp(rewrite_readback,payload,sizeof(payload)));
    RUN(0,NULL,"C:zfs","unmount","cli_upgrade/files");
    UCHECK(!command_worker_test(29,path,"","",OZ_CONFIRMED|OZ_FORCE,OZ_ENGINE_ERROR,"is a member"));
    RUN(0,NULL,"C:zpool","export","cli_upgrade");
    UCHECK(!command_worker_test(29,path,"","",OZ_CONFIRMED,OZ_ENGINE_ERROR,"exported pool"));
    /* On-disk vdev_label_t: four 256-KiB labels, each with 16-KiB pads.
     * Inspect every label independently of the upstream discovery helper. */
    enum { label_bytes=256*1024, label_pads=16*1024 };
    unsigned char *labels=malloc(4*label_bytes),*after=malloc(label_bytes);
    UCHECK(labels&&after);
    file=Open((CONST_STRPTR)native,MODE_OLDFILE);UCHECK(file);
    for(unsigned l=0;l<4;l++){
        LONG offset=l<2?l*label_bytes:128*1024*1024-(4-l)*label_bytes;
        UCHECK(Seek(file,offset,OFFSET_BEGINNING)!=-1);
        UCHECK(Read(file,labels+l*label_bytes,label_bytes)==label_bytes);
        unsigned nonzero=0;for(unsigned n=label_pads;n<label_bytes;n++)nonzero|=labels[l*label_bytes+n];
        UCHECK(nonzero);
    }
    UCHECK(Close(file));
    UCHECK(!command_worker_test(29,path,"","",OZ_CONFIRMED|OZ_FORCE,OZ_OK,"exit=0"));
    file=Open((CONST_STRPTR)native,MODE_OLDFILE);UCHECK(file);
    for(unsigned l=0;l<4;l++){
        LONG offset=l<2?l*label_bytes:128*1024*1024-(4-l)*label_bytes;
        UCHECK(Seek(file,offset,OFFSET_BEGINNING)!=-1);
        UCHECK(Read(file,after,label_bytes)==label_bytes);
        UCHECK(!memcmp(after,labels+l*label_bytes,label_pads));
        for(unsigned n=label_pads;n<label_bytes;n++)UCHECK(after[n]==0);
    }
    UCHECK(Close(file));free(after);free(labels);
    bug("RESULT native labelclear bytes PASS labels=4 pads-preserved=1 label-regions-zero=1\n");
    RUN(1,"no such pool available","C:zpool","import","-N","-o","cachefile=none","-d",path,"cli_upgrade");
    UCHECK(DeleteFile((CONST_STRPTR)native));
    bug("RESULT native Manager labelclear PASS active-denied=1 readonly-active-denied=1 exported-denied=1 force-cleared=1 import-absent=1 invalid=3 cleanup=1\n");
    bug("RESULT native dataset rewrite PASS CLI=1 Manager=2 packet=1 exact-data=3 physical-refused=1 physical=1 nonroot-denied=1 readonly-denied=1 malformed-packet=2 invalid-manager=6 cleanup=1\n");
    bug("RESULT native Manager dataset upgrade PASS legacy4=1 denied-unchanged=1 version5=1 exact-data=1 invalid=3 unconfirmed=1 cleanup=1\n");
    bug("RESULT native Manager pool upgrade PASS legacy26=1 denied-unchanged=1 version28=1 features=1 reimport=1 exact-data=1 invalid=2 unconfirmed=1 cleanup=1\n");
    bug("RESULT native import rejection recovery PASS unsupported=1 no-false-enomem=1 absent-after-reject=1 valid-reimport=1 exact-data=1\n");
    return 0;
#undef UCHECK
}
static int create_pool(void)
{
#ifdef OZ_TEST_CLI_DRAID
#ifdef OZ_TEST_DRAID_EXPAND_PARITY
    if(draid_parity_pool(OZ_TEST_DRAID_EXPAND_PARITY,1))return 20;
#else
    if(draid_pool())return 20;
#endif
#if defined(OZ_TEST_TYPED_DRAID) && !defined(OZ_TEST_DRAID_PERSIST)
    if(draid_parity_pool(2,0)||draid_parity_pool(3,0))return 20;
    for(unsigned parity=1;parity<=3;parity++)if(draid_parity_pool(parity,1))return 20;
#endif
#endif
#ifdef OZ_TEST_NATIVE_CREATE
    if(native_create_pool())return 20;
#endif
    if(split_pool(0))return 20;
#ifdef OZ_TEST_NATIVE_SPLIT
    if(split_pool(1))return 20;
#endif
    if(attach_pool(0))return 20;
#ifdef OZ_TEST_NATIVE_REPLACE
    if(attach_pool(1))return 20;
#endif
    if(growth_pool(0)||growth_pool(1))return 20;
    if(legacy_history_pool())return 20;
#ifdef OZ_TEST_NATIVE_ADD
    if(growth_pool(2))return 20;
#endif
    /* Temporary create names are live namespace aliases, never disk labels. */
    for(unsigned typed=0;typed<2;typed++) {
        const char *file_name="RAM:cli-temporary.vdev";char canonical[256],path[258];
        char *blank=calloc(1,1024*1024);if(!blank)return 20;
        BPTR file=Open((CONST_STRPTR)file_name,MODE_NEWFILE);
        if(!file){free(blank);return 20;}
        int error=0;
        for(unsigned n=0;n<128;n++)if(Write(file,blank,1024*1024)!=1024*1024){error=1;break;}
        free(blank);if(!Close(file))error=1;if(error)return 20;
        BPTR lock=Lock((CONST_STRPTR)file_name,ACCESS_READ);
        if(!lock)return 20;
        int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);
        if(!named)return 20;
        char *colon=strchr(canonical,':');if(!colon)return 20;
        *colon='/';snprintf(path,sizeof(path),"/%s",canonical);
        RUN(1,NULL,"C:zpool","create","-t","qemu_mirror","-O","mountpoint=none","cli_permanent",path);
        if(!typed) {
            RUN(0,NULL,"C:zpool","create","-t","cli_temporary","-O","mountpoint=none","cli_permanent",path);
        } else {
            struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
            if(!m||!out){free(m);free(out);return 20;}
            m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
            m->operation=5;m->flags=OZ_CONFIRMED;strcpy(m->target,"cli_permanent");
            strcpy(m->options,"tname=cli_temporary");
            m->topology.group_count=m->topology.device_count=1;m->topology.ashift=12;
            m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
            strcpy(m->topology.devices[0].path,path);
            const char *create_options[]={"tname=","tname=bad/name",
                "tname=cli_temporary;force=on","unknown=on","tname=cli_temporary"};
            for(unsigned attempt=0;attempt<5&&!error;attempt++) {
                strcpy(m->options,create_options[attempt]);
#ifdef OZ_TEST_TEMPORARY_CREATE_GUI
                if(attempt==4) {
                    bug("Temporary pool GUI READY: Pools/create cli_permanent options tname=cli_temporary, stripe backing %s; disable dry-run, confirm, close Manager\n",path);
                    error=SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE);
                    continue;
                }
#endif
                struct oz_ticket ticket={0};
                error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
                if(!error) {
                    do {error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
                    if(!error) {
                        if(attempt==4){if(out->status!=OZ_OK)error=EIO;}
                        else {
                            char expected[48];snprintf(expected,sizeof(expected),"returned errno=%d",EINVAL);
                            const char *found=strstr(out->detail,expected);
                            if(out->status==OZ_OK||!found||found[strlen(expected)])error=EIO;
                        }
                    }
                    if(oz_admission_release(OZ_ADMISSION_PORT,&ticket))error=EIO;
                }
            }
            free(m);free(out);if(error)return 20;
            bug("RESULT native Manager temporary create validation PASS malformed=4 valid-after-refusals=1\n");
        }
        RUN(0,"ONLINE","C:zpool","status","cli_temporary");
        RUN(1,NULL,"C:zpool","list","-H","cli_permanent");
        RUN(0,NULL,"C:zfs","create","-u","cli_temporary/retained");
        RUN(0,NULL,"C:zpool","export","cli_temporary");
        RUN(0,NULL,"C:zpool","import","-N","-d",path,"-o","cachefile=none","cli_permanent");
        RUN(0,"cli_permanent/retained","C:zfs","list","-H","cli_permanent/retained");
        RUN(1,NULL,"C:zpool","list","-H","cli_temporary");
        RUN(0,NULL,"C:zpool","destroy","cli_permanent");
        if(!DeleteFile((CONST_STRPTR)file_name))return 20;
        if(typed)bug("RESULT native Manager temporary create PASS live-name=1 persistent-name=1 reimport=1 release=1\n");
        else bug("RESULT native CLI temporary create PASS collision=1 live-name=1 persistent-name=1 reimport=1 release=1\n");
#ifdef OZ_TEST_TEMPORARY_CREATE_GUI
        if(typed)bug("RESULT native temporary create GUI PASS actual-manager=1 permanent-reimport=1 child-retained=1\n");
#endif
    }
#ifdef OZ_TEST_POOL_WIZARD_GUI
    {
        const char *files[]={"RAM:wizard-a.vdev","RAM:wizard-b.vdev"};
        char members[2][258];char *blank=calloc(1,1024*1024);if(!blank)return 20;
        for(unsigned i=0;i<2;i++) {
            BPTR f=Open((CONST_STRPTR)files[i],MODE_NEWFILE);if(!f){free(blank);return 20;}
            int error=0;for(unsigned n=0;n<128;n++)if(Write(f,blank,1024*1024)!=1024*1024){error=1;break;}
            if(!Close(f))error=1;if(error){free(blank);return 20;}
            BPTR l=Lock((CONST_STRPTR)files[i],ACCESS_READ);char canonical[256];
            if(!l){free(blank);return 20;}
            int named=NameFromLock(l,(STRPTR)canonical,sizeof(canonical));UnLock(l);
            char *colon=named?strchr(canonical,':'):NULL;if(!colon){free(blank);return 20;}
            *colon='/';snprintf(members[i],sizeof(members[i]),"/%s",canonical);
        }
        free(blank);
        bug("Pool wizard GUI READY: create wizard_created using Mirror and members %s and %s; submit, wait for success, close Manager\n",members[0],members[1]);
        if(SystemTags((CONST_STRPTR)"C:OpenZFSManagerNative",TAG_DONE))return 20;
        RUN(0,"mirror","C:zpool","status","wizard_created");
        RUN(0,"state: ONLINE","C:zpool","status","wizard_created");
        RUN(0,NULL,"C:zfs","create","-u","wizard_created/files");
        if(oz_mount_control(OZ_ADMISSION_PORT,"wizard_created/files","OZWIZ",OZ_MOUNT_ATTACH,0))return 20;
        static const char payload[]="Created by the native Simple pool wizard";
        BPTR f=Open((CONST_STRPTR)"OZWIZ:proof.txt",MODE_NEWFILE);if(!f)return 20;
        int error=Write(f,payload,sizeof(payload))!=sizeof(payload);
        if(!Flush(f))error=1;if(!Close(f))error=1;if(error)return 20;
        if(oz_mount_control(OZ_ADMISSION_PORT,"wizard_created/files","",OZ_MOUNT_DETACH,0))return 20;
        RUN(0,NULL,"C:zpool","export","wizard_created");
        RUN(0,NULL,"C:zpool","import","-N","-d",members[0],"-d",members[1],"-o","cachefile=none","wizard_created");
        if(oz_mount_control(OZ_ADMISSION_PORT,"wizard_created/files","OZWIZ",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
        char actual[sizeof(payload)+1];f=Open((CONST_STRPTR)"OZWIZ:proof.txt",MODE_OLDFILE);if(!f)return 20;
        error=Read(f,actual,sizeof(actual))!=sizeof(payload)||memcmp(actual,payload,sizeof(payload));
        if(!Close(f))error=1;if(error)return 20;
        if(oz_mount_control(OZ_ADMISSION_PORT,"wizard_created/files","",OZ_MOUNT_DETACH,0))return 20;
        RUN(0,NULL,"C:zpool","destroy","wizard_created");
        for(unsigned i=0;i<2;i++)if(!DeleteFile((CONST_STRPTR)files[i]))return 20;
        bug("RESULT native pool wizard GUI PASS actual-manager=1 mirror=1 DOS-persistence=1 cleanup=1\n");
    }
#endif
    const char *native[]={"RAM:cli-create-a.vdev","RAM:cli-create-b.vdev"};
    char paths[2][258];char *zero=calloc(1,1024*1024);
    if(!zero)return 20;
    for(unsigned i=0;i<2;i++) {
        BPTR file=Open((CONST_STRPTR)native[i],MODE_NEWFILE);
        if(!file){free(zero);return 20;}
        int error=0;
        for(unsigned n=0;n<128;n++)if(Write(file,zero,1024*1024)!=1024*1024){error=1;break;}
        if(!Close(file))error=1;
        BPTR lock=Lock((CONST_STRPTR)native[i],ACCESS_READ);
        char canonical[256];
        if(!lock||!NameFromLock(lock,(STRPTR)canonical,sizeof(canonical)))error=1;
        if(lock)UnLock(lock);
        if(error){free(zero);return 20;}
        char *colon=strchr(canonical,':');if(!colon){free(zero);return 20;}
        *colon='/';snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    BPTR marked=Open((CONST_STRPTR)native[1],MODE_READWRITE);
    char marker=0x5a;
    if(!marked||Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||Write(marked,&marker,1)!=1||
        !Flush(marked)||!Close(marked)){free(zero);return 20;}
    RUN(1,NULL,"C:zpool","create","-f","-o","cachefile=none","-O","mountpoint=none",
        "cli_created","mirror",paths[0],paths[1]);
    RUN(1,NULL,"C:zpool","list","-H","cli_created");
    BPTR untouched=Open((CONST_STRPTR)native[0],MODE_OLDFILE);
    if(!untouched){free(zero);return 20;}
    for(unsigned n=0;n<128;n++) {
        if(Read(untouched,zero,1024*1024)!=1024*1024){Close(untouched);free(zero);return 20;}
        for(unsigned b=0;b<1024*1024;b++)if(zero[b]){Close(untouched);free(zero);return 20;}
    }
    if(!Close(untouched)){free(zero);return 20;}
    marked=Open((CONST_STRPTR)native[1],MODE_READWRITE);
    if(!marked||Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||Read(marked,&marker,1)!=1||marker!=0x5a)
        {free(zero);return 20;}
    marker=0;
    if(Seek(marked,128*1024*1024-1,OFFSET_BEGINNING)==-1||Write(marked,&marker,1)!=1||
        !Flush(marked)||!Close(marked)){free(zero);return 20;}
    free(zero);
    /* Exercise user-facing aliases through upstream argument handling too.
     * Keep canonical paths for subsequent vdev identity operations. */
    BPTR alias=Lock((CONST_STRPTR)"RAM:",ACCESS_READ);
    if(!alias)return 20;
    if(!AssignLock((CONST_STRPTR)"OZCLICREATE",alias)){UnLock(alias);return 20;}
    char create_cache[300],create_cache_setting[320];
    const char *cache_leaf=strrchr(paths[0],'/');if(!cache_leaf)return 20;
    snprintf(create_cache,sizeof(create_cache),"%.*s/cli-created.cache",(int)(cache_leaf-paths[0]),paths[0]);
    snprintf(create_cache_setting,sizeof(create_cache_setting),"cachefile=%s",create_cache);
    RUN(0,NULL,"C:zpool","create","-o",create_cache_setting,"-O","mountpoint=none",
        "cli_created","mirror","/OZCLICREATE/cli-create-a.vdev","/OZCLICREATE/cli-create-b.vdev");
    RUN(0,create_cache,"C:zpool","get","-H","-o","value","cachefile","cli_created");
    BPTR created_cache=Lock((CONST_STRPTR)"RAM:cli-created.cache",ACCESS_READ);
    if(!created_cache)return 20;UnLock(created_cache);
    RUN(0,NULL,"C:zpool","set","cachefile=none","cli_created");
    created_cache=Lock((CONST_STRPTR)"RAM:cli-created.cache",ACCESS_READ);
    if(created_cache){UnLock(created_cache);return 20;}
    bug("RESULT native CLI create cachefile PASS explicit=1 written=1 disabled-removed=1\n");
    if(!AssignLock((CONST_STRPTR)"OZCLICREATE",0))return 20;
    bug("RESULT native CLI create alias PASS bound-assign=1 canonical-lifetime=1\n");
    RUN(0,"mirror","C:zpool","status","cli_created");
    /* Exercise upstream cache serialization and parsing, not just property
     * readback. Preserve a copy because export removes the live cache. */
    {
        char cache[300],saved[300],setting[320];
        const char *leaf=strrchr(paths[0],'/');if(!leaf)return 20;
        int parent=(int)(leaf-paths[0]);
        snprintf(cache,sizeof(cache),"%.*s/cli-pool.cache",parent,paths[0]);
        snprintf(saved,sizeof(saved),"%.*s/cli-pool.saved",parent,paths[0]);
        snprintf(setting,sizeof(setting),"cachefile=%s",cache);
        RUN(0,NULL,"C:zpool","set",setting,"cli_created");
        RUN(0,cache,"C:zpool","get","-H","-o","value","cachefile","cli_created");
        if(manager_permission_or_create(32,"cli_created","cachefile=relative-cache","",EINVAL)||
           manager_permission_or_create(32,"cli_created","cachefile=","",ENOTSUP))return 20;
        RUN(0,cache,"C:zpool","get","-H","-o","value","cachefile","cli_created");
        if(manager_permission_or_create(32,"cli_created","cachefile=none","",0))return 20;
        BPTR disabled=Lock((CONST_STRPTR)"RAM:cli-pool.cache",ACCESS_READ);
        if(disabled){UnLock(disabled);return 20;}
        if(manager_permission_or_create(32,"cli_created",setting,"",0))return 20;
        BPTR input=Open((CONST_STRPTR)"RAM:cli-pool.cache",MODE_OLDFILE);
        if(!input)return 20;
        BPTR output=Open((CONST_STRPTR)"RAM:cli-pool.saved",MODE_NEWFILE);
        if(!output){Close(input);return 20;}
        char bytes[8192];LONG count;unsigned total=0;int failed=0;
        while((count=Read(input,bytes,sizeof(bytes)))>0){
            if(total>16*1024*1024||Write(output,bytes,count)!=count){failed=1;break;}
            total+=count;
        }
        if(count<0||!total)failed=1;
        if(!Close(input))failed=1;
        if(!Flush(output))failed=1;
        if(!Close(output))failed=1;
        if(failed)return 20;
        RUN(0,NULL,"C:zpool","export","cli_created");
        BPTR absent=Lock((CONST_STRPTR)"RAM:cli-pool.cache",ACCESS_READ);
        if(absent){UnLock(absent);return 20;}
        /* Preserve the valid serialized cache and alter only a key of equal
         * length. It remains a packed nvlist but lacks required pool metadata.
         * This must be a normal error, never a fatal fnvlist assertion. */
        const char *badkeys[]={"pool_guid","vdev_tree"};
        for(unsigned bad=0;bad<2;bad++) {
        char *badcache=malloc(total);if(!badcache)return 20;
        input=Open((CONST_STRPTR)"RAM:cli-pool.saved",MODE_OLDFILE);
        if(!input){free(badcache);return 20;}
        int badread=Read(input,badcache,total)!=(LONG)total;
        if(!Close(input))badread=1;
        if(badread){free(badcache);return 20;}
        unsigned altered=0;
        size_t keysize=strlen(badkeys[bad])+1;
        for(unsigned b=0;b+keysize<=total;b++)
            if(!memcmp(badcache+b,badkeys[bad],keysize)){
                badcache[b]='X';altered++;
            }
        if(!altered){free(badcache);return 20;}
        output=Open((CONST_STRPTR)"RAM:cli-pool.invalid",MODE_NEWFILE);
        if(!output){free(badcache);return 20;}
        int badwrite=Write(output,badcache,total)!=(LONG)total;
        free(badcache);if(!Flush(output))badwrite=1;if(!Close(output))badwrite=1;
        if(badwrite)return 20;
        char invalid[300];snprintf(invalid,sizeof(invalid),"%.*s/cli-pool.invalid",parent,paths[0]);
        RUN(1,bad?"invalid native cache path selection":"invalid native cache pool metadata",
            "C:zpool","import","-N","-c",invalid,"-o","cachefile=none","cli_created");
        RUN(1,NULL,"C:zpool","list","-H","cli_created");
        if(!DeleteFile((CONST_STRPTR)"RAM:cli-pool.invalid"))return 20;
        }
        bug("RESULT native CLI invalid cache PASS metadata-refused=1 tree-refused=1 pool-absent=2 cleanup=1\n");
        RUN(0,NULL,"C:zpool","import","-N","-c",saved,"-o",setting,"cli_created");
        RUN(0,"ONLINE","C:zpool","status","cli_created");
        RUN(0,cache,"C:zpool","get","-H","-o","value","cachefile","cli_created");
        BPTR regenerated=Lock((CONST_STRPTR)"RAM:cli-pool.cache",ACCESS_READ);
        if(!regenerated)return 20;UnLock(regenerated);
        RUN(0,NULL,"C:zpool","set","cachefile=none","cli_created");
        regenerated=Lock((CONST_STRPTR)"RAM:cli-pool.cache",ACCESS_READ);
        if(regenerated){UnLock(regenerated);return 20;}
        bug("RESULT native CLI import cachefile PASS explicit=1 regenerated=1 disabled-removed=1\n");
        if(!DeleteFile((CONST_STRPTR)"RAM:cli-pool.saved"))return 20;
        bug("RESULT native CLI cachefile PASS written=1 parsed-reimport=1 export-removed=1 cleanup=1\n");
        bug("RESULT native Manager cachefile PASS invalid=2 unchanged=1 disabled=1 enabled=1 serialized=1\n");
    }
    RUN(0,NULL,"C:zpool","clear","cli_created");
    RUN(0,NULL,"C:zpool","clear","cli_created",paths[0]);
    RUN(1,NULL,"C:zpool","clear","-F","cli_created");
    RUN(0,"state: ONLINE","C:zpool","status","cli_created");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:",
        "-o","normalization=formC","-o","utf8only=on","-o","casesensitivity=insensitive",
        "cli_created/files");
#ifdef OZ_TEST_STARTUP_MOUNT
    RUN(0,NULL,"C:zfs","create","-u","-o","canmount=noauto","-o","mountpoint=OZNOAUTO:","cli_created/noauto");
    RUN(0,NULL,"C:zfs","create","-u","-o","canmount=off","-o","mountpoint=OZOFF:","cli_created/off");
    RUN(1,"mismatched","C:OpenZFSStartupMount","cli_created","0000000000000001");
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","cli_created/files");
    RUN(0,NULL,"C:zpool","get","-H","-p","-o","value","guid","cli_created");
    char startup_guid[32];snprintf(startup_guid,sizeof(startup_guid),"%016llx",strtoull(last_command_output,NULL,10));
    RUN(0,"mounted=1 already=0 skipped=3 failed=0","C:OpenZFSStartupMount","cli_created",startup_guid);
    RUN(0,"mounted=0 already=1 skipped=3 failed=0","C:OpenZFSStartupMount","cli_created",startup_guid);
#ifdef OZ_TEST_STARTUP_COMMAND
    static struct oz_startup_settings boot;
    boot.guid=strtoull(startup_guid,NULL,16);boot.count=2;boot.flags=OZ_STARTUP_MOUNT;
    strcpy(boot.pool,"cli_created");strcpy(boot.paths[0],paths[0]);strcpy(boot.paths[1],paths[1]);
    unsigned boot_published=0;
    if(oz_startup_settings_apply_trusted(&boot,0,&boot_published)||boot_published!=1)return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,"disabled; no import","C:OpenZFSStartup",startup_guid);
    RUN(1,NULL,"C:zpool","list","-H","cli_created");
    boot.flags|=OZ_STARTUP_ENABLED;
    if(oz_startup_settings_apply_trusted(&boot,0,&boot_published)||boot_published!=1)return 20;
    RUN(0,"mounted=1 already=0 skipped=3 failed=0","C:OpenZFSStartup",startup_guid);
    RUN(1,"imported=0","C:OpenZFSStartup",startup_guid);
    if(dos_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    boot.flags=OZ_STARTUP_ENABLED;
    if(oz_startup_settings_apply_trusted(&boot,0,&boot_published)||boot_published!=1)return 20;
    RUN(0,"mounting disabled","C:OpenZFSStartup",startup_guid);
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    boot.flags=OZ_STARTUP_ENABLED|OZ_STARTUP_MOUNT|OZ_STARTUP_READONLY;
    BPTR archive_dir=CreateDir((CONST_STRPTR)"RAM:CLI-Startup-Archive");
    if(!archive_dir||!AssignLock((CONST_STRPTR)"ENVARC",archive_dir))return 20;
    if(oz_startup_settings_apply_trusted(&boot,1,&boot_published)||boot_published!=3)return 20;
    char startup_path[80];snprintf(startup_path,sizeof(startup_path),"ENV:OpenZFS-%s.prefs",startup_guid);
    if(!DeleteFile((CONST_STRPTR)startup_path))return 20;
    RUN(1,"Startup import:","C:OpenZFSStartup",startup_guid);
    RUN(0,"mounted=1 already=0 skipped=3 failed=0","C:OpenZFSStartup",startup_guid,"RESTORE");
    RUN(0,"on","C:zpool","get","-H","-o","value","readonly","cli_created");
    RUN(1,NULL,"C:zfs","create","cli_created/refused_startup_ro");
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    boot.flags=OZ_STARTUP_ENABLED|OZ_STARTUP_MOUNT;
    if(oz_startup_settings_apply_trusted(&boot,0,&boot_published)||boot_published!=1)return 20;
    RUN(0,"mounted=1 already=0 skipped=3 failed=0","C:OpenZFSStartup",startup_guid);
    bug("RESULT native startup policy PASS mount-disabled=1 readonly-import=1 readonly-mutation-denied=1\n");
    bug("RESULT native startup command PASS disabled=1 trusted-import=1 mounted=1 duplicate-failed=1 DOS-write-read=1\n");
#endif
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","cli_created/noauto");
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","cli_created/off");
    RUN(0,NULL,"C:zfs","destroy","cli_created/noauto");
    RUN(0,NULL,"C:zfs","destroy","cli_created/off");
    bug("RESULT native startup mount PASS GUID-guard=1 scoped=1 noauto=1 off=1 repeat=1\n");
#else
    RUN(0,NULL,"C:zfs","mount","cli_created/files");
#endif
    if(dos_file())return 20;
    RUN(0,NULL,"C:zpool","sync","cli_created");
    char manager_leaf_guid[32];
    if(typed_vdev_guid("cli_created",paths[1],manager_leaf_guid)||
       manager_permission_or_create(16,"cli_created",manager_leaf_guid,"temporary=wrong",EINVAL)||
       manager_permission_or_create(15,"cli_created",manager_leaf_guid,"expand=wrong",EINVAL)||
       manager_permission_or_create(16,"cli_created",manager_leaf_guid,"temporary=on",0))return 20;
    RUN(0,"state: DEGRADED","C:zpool","status","cli_created");
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,"state: ONLINE","C:zpool","status","cli_created");
    if(manager_permission_or_create(15,"cli_created",manager_leaf_guid,"expand=on",0))return 20;
    RUN(0,NULL,"C:zfs","mount","cli_created/files");
    if(dos_file())return 20;
    bug("RESULT native Manager vdev flags PASS malformed=2 temporary-offline=1 reimport-online=1 explicit-expand-accepted=1 DOS-read=1\n");
    RUN(0,NULL,"C:zpool","offline","cli_created",paths[1]);
    RUN(0,"state: DEGRADED","C:zpool","status","cli_created");
    RUN(1,NULL,"C:zpool","offline","cli_created",paths[0]);
    expected_data="CLI new!";
    if(dos_file())return 20;
    RUN(0,NULL,"C:zpool","sync","cli_created");
    RUN(0,NULL,"C:zpool","online","cli_created",paths[1]);
    if(resilver_complete())return 20;
    RUN(0,NULL,"C:zpool","reopen","-n","cli_created");
    RUN(0,NULL,"C:zpool","scrub","cli_created");
    if(scrub_complete())return 20;
    /* Exercise the public CLI flag, not the Manager's separate import path.
     * A readonly checkpoint preview must leave the newer namespace intact. */
    RUN(0,NULL,"C:zfs","create","-o","canmount=off","cli_created/rewind_before");
    RUN(0,NULL,"C:zpool","checkpoint","cli_created");
    RUN(0,NULL,"C:zfs","create","-o","canmount=off","cli_created/rewind_after");
    RUN(0,NULL,"C:zpool","sync","cli_created");
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","--rewind-to-checkpoint","-N","-o","readonly=on","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","list","cli_created/rewind_before");
    RUN(1,NULL,"C:zfs","list","cli_created/rewind_after");
    RUN(0,NULL,"C:zfs","mount","cli_created/files");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","readonly=on","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","list","cli_created/rewind_after");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","--rewind-to-checkpoint","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","list","cli_created/rewind_before");
    RUN(1,NULL,"C:zfs","list","cli_created/rewind_after");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(1,NULL,"C:zpool","import","--rewind-to-checkpoint","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","list","cli_created/rewind_before");
    RUN(1,NULL,"C:zfs","list","cli_created/rewind_after");
    RUN(0,NULL,"C:zfs","destroy","cli_created/rewind_before");
    RUN(0,NULL,"C:zfs","mount","cli_created/files");
    if(dos_file())return 20;
    bug("RESULT native CLI checkpoint rewind PASS preview=1 current-retained=1 writable=1 no-checkpoint-denied=1 reimport=1 DOS-readonly=1\n");
    RUN(0,NULL,"C:zpool","checkpoint","cli_created");
    RUN(0,"checkpoint: created","C:zpool","status","cli_created");
    RUN(-1,NULL,"C:zpool","reguid","cli_created");
    RUN(1,NULL,"C:zpool","checkpoint","cli_created");
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    /* Force the next import to read from the repaired member, not the member
     * that accepted the degraded write. Export drops the prior SPA cache. */
    if(manager_permission_or_create(32,"cli_created","bootfs=cli_created/files","",0))return 20;
    RUN(0,NULL,"C:zpool","offline","cli_created",paths[0]);
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_created/files");
    RUN(0,"cli_created/files","C:zpool","get","-Hp","-o","value","bootfs","cli_created");
    if(manager_permission_or_create(32,"cli_created","bootfs=","",EROFS))return 20;
    RUN(0,"cli_created/files","C:zpool","get","-Hp","-o","value","bootfs","cli_created");
    bug("RESULT native Manager bootfs reimport PASS persisted=1 readonly-refused=1 unchanged=1\n");
    if(manager_permission_or_create(32,"cli_created","cachefile=none","",EROFS))return 20;
    bug("RESULT native Manager cachefile readonly PASS refused=1\n");
    RUN(0,"state: DEGRADED","C:zpool","status","cli_created");
    if(readonly_file())return 20;
    RUN(1,NULL,"C:zpool","scrub","cli_created");
    RUN(1,NULL,"C:zpool","offline","cli_created",paths[1]);
    RUN(1,NULL,"C:zpool","online","cli_created",paths[1]);
    RUN(1,"read-only","C:zpool","online","-e","cli_created",paths[1]);
    RUN(-1,"read-only","C:zpool","initialize","cli_created",paths[1]);
#ifdef OZ_TEST_VDEV_PROPERTIES
    RUN(0,paths[1],"C:zpool","get","-H","-o","value","path","cli_created",paths[1]);
    RUN(1,"read-only","C:zpool","set","comment=readonly-must-not-write","cli_created",paths[1]);
#ifdef OZ_TEST_MANAGER_VDEV_PROPERTIES
    char manager_policy_guid[32];
    if(typed_vdev_guid("cli_created",paths[1],manager_policy_guid)||
        command_worker_test(81,"cli_created",manager_policy_guid,"comment=readonly-must-not-write",OZ_CONFIRMED,OZ_ENGINE_ERROR,"read-only"))return 20;
#endif
#endif
    RUN(0,"cli_created/files","C:zpool","history","-i","cli_created");
    if(manager_permission_or_create(15,"cli_created",manager_leaf_guid,"expand=on",EROFS)||
       manager_permission_or_create(16,"cli_created",manager_leaf_guid,"temporary=on",EROFS))return 20;
    RUN(-1,"read-only","C:zpool","detach","cli_created",paths[0]);
    RUN(1,NULL,"C:zpool","checkpoint","-d","cli_created");
    RUN(0,"checkpoint: created","C:zpool","status","cli_created");
    RUN(1,"read-only","C:zpool","remove","cli_created",paths[1]);
    RUN(1,"read-only","C:zpool","clear","cli_created");
    RUN(1,"read-only","C:zpool","clear","cli_created",paths[1]);
    RUN(-1,NULL,"C:zpool","reguid","cli_created");
    RUN(0,NULL,"C:zpool","reopen","-n","cli_created");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(1,NULL,"C:zpool","destroy","cli_created");
    RUN(1,NULL,"C:zfs","rename","-u","cli_created/files","cli_created/renamed");
    RUN(0,"cli_created","C:zpool","list","-H","cli_created");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",paths[0],"-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zpool","online","cli_created",paths[0]);
    if(resilver_complete())return 20;
    RUN(0,"checkpoint: created","C:zpool","status","cli_created");
    RUN(0,NULL,"C:zpool","checkpoint","-d","cli_created");
    if(checkpoint_discarded())return 20;
    RUN(0,NULL,"C:zpool","reguid","cli_created");
    RUN(0,NULL,"C:zpool","clear","cli_created");
    RUN(0,NULL,"C:zpool","reguid","-g","987654321","cli_created");
    RUN(0,NULL,"C:zpool","clear","cli_created");
    RUN(0,"987654321","C:zpool","get","-H","-o","value","guid","cli_created");
    RUN(-1,NULL,"C:zpool","reguid","-g","987654321","cli_created");
    RUN(-1,NULL,"C:zpool","reguid","-g","0","cli_created");
    RUN(0,NULL,"C:zpool","reopen","-n","cli_created");
    RUN(0,"state: ONLINE","C:zpool","status","cli_created");
    RUN(1,NULL,"C:zpool","checkpoint","-d","cli_created");
    RUN(0,NULL,"C:zfs","mount","cli_created/files");
    BPTR held=Open((CONST_STRPTR)"OZCLI:CAFE\xcc\x81",MODE_OLDFILE);
    if(!held)return 20;
    char *busy_destroy[]={"C:zpool","destroy","cli_created",NULL};
    int busy_error=run(busy_destroy,1,NULL);
    char *forced_destroy[]={"C:zpool","destroy","-f","cli_created",NULL};
    if(run(forced_destroy,1,NULL))busy_error=1;
    char *busy_rename[]={"C:zfs","rename","-u","cli_created/files","cli_created/renamed",NULL};
    if(run(busy_rename,1,NULL))busy_error=1;
    char bytes[9]={0};
    if(Read(held,bytes,sizeof(bytes))!=8||memcmp(bytes,expected_data,8))busy_error=1;
    if(!Close(held)||busy_error)return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    RUN(1,NULL,"C:zpool","destroy","cli_created");
    RUN(1,NULL,"C:zpool","scrub","cli_created");
    RUN(1,NULL,"C:zpool","reopen","cli_created");
    RUN(1,"permission denied","C:zpool","clear","cli_created");
    RUN(1,"permission denied","C:zpool","remove","cli_created",paths[1]);
    RUN(-1,NULL,"C:zpool","reguid","cli_created");
    RUN(1,"permission denied","C:zpool","offline","cli_created",paths[1]);
    RUN(1,"permission denied","C:zpool","online","cli_created",paths[1]);
    RUN(1,"permission denied","C:zpool","online","-e","cli_created",paths[1]);
    RUN(-1,"permission denied","C:zpool","initialize","cli_created",paths[1]);
#ifdef OZ_TEST_VDEV_PROPERTIES
    RUN(1,"permission denied","C:zpool","set","comment=nonroot-must-not-write","cli_created",paths[1]);
#ifdef OZ_TEST_MANAGER_VDEV_PROPERTIES
    if(command_worker_test(81,"cli_created",manager_policy_guid,"comment=nonroot-must-not-write",OZ_CONFIRMED,OZ_ENGINE_ERROR,"permission denied"))return 20;
    bug("RESULT native Manager vdev setter policy PASS readonly=1 unauthorized=1\n");
#endif
    bug("RESULT native CLI vdev properties policy PASS readonly-read=1 readonly-write-denied=1 unauthorized-write-denied=1 path-change-denied=1\n");
#endif
    RUN(1,"permission denied","C:zpool","history","-i","cli_created");
    bug("RESULT native CLI history policy PASS readonly-read=1 unauthorized-denied=1\n");
    bug("RESULT native CLI initialize policy PASS readonly-denied=1 unauthorized-denied=1\n");
    bug("RESULT native CLI online expansion policy PASS readonly-denied=1 unauthorized-denied=1\n");
    if(manager_permission_or_create(15,"cli_created",manager_leaf_guid,"expand=on",EPERM)||
       manager_permission_or_create(16,"cli_created",manager_leaf_guid,"temporary=on",EPERM))return 20;
    bug("RESULT native Manager vdev flag policy PASS readonly-denied=2 unauthorized-denied=2\n");
    RUN(-1,"permission denied","C:zpool","detach","cli_created",paths[0]);
    RUN(1,"permission denied","C:zfs","rename","-u","cli_created/files","cli_created/renamed");
    RUN(1,NULL,"C:zpool","checkpoint","cli_created");
    RUN(1,NULL,"C:zpool","checkpoint","-d","cli_created");
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(0,"cli_created","C:zpool","list","-H","cli_created");
    RUN(0,NULL,"C:zpool","detach","cli_created",paths[0]);
    RUN(0,"state: ONLINE","C:zpool","status","cli_created");
    if(strstr(last_command_output,paths[0])||!strstr(last_command_output,paths[1]))return 20;
    RUN(-1,NULL,"C:zpool","detach","cli_created",paths[1]);
    RUN(0,NULL,"C:zpool","export","cli_created");
    if(!DeleteFile((CONST_STRPTR)native[0]))return 20;
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",paths[1],"cli_created");
    RUN(0,"987654321","C:zpool","get","-H","-o","value","guid","cli_created");
    RUN(0,"state: ONLINE","C:zpool","status","cli_created");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_created/files");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/files");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zfs","rename","-u","cli_created/files","cli_created/renamed");
    RUN(1,NULL,"C:zfs","list","-H","cli_created/files");
    RUN(0,"cli_created/renamed","C:zfs","list","-H","cli_created/renamed");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=none","cli_created/renamed/child");
    RUN(0,NULL,"C:zfs","snapshot","-r","cli_created/renamed@before");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@before","cli_created/renamed");
    {char *end=NULL;errno=0;unsigned long long written=strtoull(last_command_output,&end,10);
        if(errno||written||end==last_command_output||(*end!='\r'&&*end!='\n'))return 20;}
    RUN(0,NULL,"C:zfs","mount","cli_created/renamed");
    if(growth_payload(1))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/renamed");
    RUN(0,NULL,"C:zpool","sync","cli_created");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@before","cli_created/renamed");
    {char *end=NULL;errno=0;unsigned long long written=strtoull(last_command_output,&end,10);
        if(errno||!written||end==last_command_output||(*end!='\r'&&*end!='\n'))return 20;}
    RUN(0,"would reclaim","C:zfs","destroy","-n","-v","cli_created/renamed@before");
    RUN(0,"cli_created/renamed@before","C:zfs","list","-H","-t","snapshot","cli_created/renamed@before");
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@before","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    BPTR rename_lock=Lock((CONST_STRPTR)"OZCLI:",ACCESS_READ);
    if(!rename_lock)return 20;
    if(manager_rename("cli_created/renamed@before","cli_created/renamed@ui",EBUSY))return 20;
    RUN(1,"busy","C:zfs","rename","cli_created/renamed@before","cli_created/renamed@after");
    UnLock(rename_lock);
    /* Upstream CLI limits -u to filesystems before issuing the ioctl. */
    RUN(1,"snapshot delimiter","C:zfs","rename","-u","cli_created/renamed@before","cli_created/renamed@after");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","rename","cli_created/renamed@before","cli_created/renamed@live");
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@live","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(readonly_file())return 20;
    if(manager_rename("cli_created/renamed@live","cli_created/renamed@before",OZ_OK))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@before","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(readonly_file())return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@before","",OZ_MOUNT_DETACH,0))return 20;
    bug("RESULT native mounted snapshot rename PASS busy-lock=1 nounmount-refused=1 CLI-detach=1 Manager-detach=1 remount-read=1\n");
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed/child@before","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@before","OZPARENT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    rename_lock=Lock((CONST_STRPTR)"OZCLI:",ACCESS_READ);if(!rename_lock)return 20;
    RUN(1,"busy","C:zfs","rename","-r","cli_created/renamed@before","cli_created/renamed@after");
    RUN(0,"cli_created/renamed@before","C:zfs","list","-H","-t","snapshot","cli_created/renamed@before");
    {BPTR parent=Lock((CONST_STRPTR)"OZPARENT:",ACCESS_READ);if(!parent)return 20;UnLock(parent);}
    UnLock(rename_lock);
    RUN(0,NULL,"C:zfs","rename","-r","cli_created/renamed@before","cli_created/renamed@after");
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@after","OZPARENT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed/child@after","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@after","",OZ_MOUNT_DETACH,0))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed/child@after","",OZ_MOUNT_DETACH,0))return 20;
    bug("RESULT native recursive mounted snapshot rename PASS busy-child=1 parent-retained=1 idle-detach=2 remount=2\n");
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@after","OZPARENT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed/child@after","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    rename_lock=Lock((CONST_STRPTR)"OZCLI:",ACCESS_READ);if(!rename_lock)return 20;
    if(manager_rename_flags("cli_created/renamed@after","cli_created/renamed@ui-rec",EBUSY,OZ_RECURSIVE))return 20;
    {BPTR parent=Lock((CONST_STRPTR)"OZPARENT:",ACCESS_READ);if(!parent)return 20;UnLock(parent);}
    UnLock(rename_lock);
    if(manager_rename_flags("cli_created/renamed@after","cli_created/renamed@ui-rec",OZ_OK,OZ_RECURSIVE))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@ui-rec","OZPARENT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed/child@ui-rec","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(manager_rename_flags("cli_created/renamed@ui-rec","cli_created/renamed@after",OZ_OK,OZ_RECURSIVE))return 20;
    if(manager_rename_flags("cli_created/renamed","cli_created/not-recursive",EINVAL,OZ_RECURSIVE))return 20;
    bug("RESULT native Manager recursive snapshot rename PASS busy-child=1 parent-retained=1 idle-detach=2 remount=2 filesystem-refused=1\n");
    RUN(0,"cli_created/renamed/child@after","C:zfs","list","-H","-t","snapshot","cli_created/renamed/child@after");
    RUN(1,NULL,"C:zfs","list","-H","-t","snapshot","cli_created/renamed/child@before");
    RUN(0,NULL,"C:zfs","rename","cli_created/renamed@after","cli_created/renamed@final");
    if(manager_rename("cli_created/renamed@final","cli_created/renamed@ui",OZ_OK))return 20;
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on","-d",paths[1],"cli_created");
    RUN(0,"cli_created/renamed@ui","C:zfs","list","-H","-t","snapshot","cli_created/renamed@ui");
    RUN(0,NULL,"C:zfs","get","-H","-p","-o","value","written@ui","cli_created/renamed");
    {char *end=NULL;errno=0;unsigned long long written=strtoull(last_command_output,&end,10);
        if(errno||!written||end==last_command_output||(*end!='\r'&&*end!='\n'))return 20;}
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@ui","OZCLI",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY))return 20;
    if(readonly_file())return 20;
    if(manager_rename("cli_created/renamed@ui","cli_created/renamed@denied",EROFS))return 20;
    if(readonly_file())return 20; /* Refusal must retain the published mount. */
    if(oz_mount_control(OZ_ADMISSION_PORT,"cli_created/renamed@ui","",OZ_MOUNT_DETACH,0))return 20;
    if(manager_rename("cli_created/renamed@ui","cli_created/renamed@denied",EROFS))return 20;
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_created/renamed");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_created/renamed");
    RUN(0,NULL,"C:zpool","export","cli_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-d",paths[1],"cli_created");
    RUN(0,NULL,"C:zpool","destroy","cli_created");
    RUN(1,NULL,"C:zpool","list","-H","cli_created");
    RUN(1,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",paths[1],"cli_created");
    if(!DeleteFile((CONST_STRPTR)native[1]))return 20;
    expected_data="CLI data";
    bug("RESULT native CLI pool create PASS mirror=1 DOS-write=1 export=1 import=1 readonly-read=1\n");
    bug("RESULT native CLI create blankness PASS force-rejected=1 first-member-unchanged=1 marker-retained=1\n");
    bug("RESULT native CLI pool destroy PASS busy=1 unauthorized=1 removed=1 normal-import-denied=1 bindings-released=1 readonly-denied=1 forced-busy-denied=1\n");
    bug("RESULT native CLI maintenance PASS scrub-complete=1 reopen=1 readonly-scrub-denied=1 readonly-reopen-read=1 unauthorized=1\n");
    bug("RESULT native CLI clear PASS pool=1 device=1 readonly-denied=1 unauthorized=1\n");
    bug("RESULT native CLI accounting PASS written-after-snapshot=1 destroy-dryrun=1 snapshot-retained=1 readonly-query=1\n");
    bug("RESULT native CLI checkpoint PASS create=1 duplicate-denied=1 reimport=1 readonly-discard-denied=1 discard-complete=1 absent-discard-denied=1 unauthorized=1\n");
    bug("RESULT native CLI reguid PASS random=1 explicit=1 reopen=1 reimport-guid=1 duplicate-denied=1 zero-denied=1 readonly-denied=1 checkpoint-denied=1 unauthorized=1\n");
    bug("RESULT native CLI vdev PASS offline=1 last-replica-denied=1 degraded-write=1 online=1 resilver-complete=1 readonly-denied=1 unauthorized=1 repaired-member-read=1\n");
    bug("RESULT native CLI detach PASS readonly-denied=1 unauthorized=1 topology=1 last-member-denied=1 removed-file-deleted=1 survivor-import-read=1\n");
    bug("RESULT native CLI rename PASS busy=1 readonly-denied=1 unauthorized=1 renamed=1 reimport-read=1\n");
    bug("RESULT native CLI snapshot rename PASS mounted-denied=1 descendant-mounted-denied=1 recursive=1 single=1 reimport-read=1\n");
    bug("RESULT native manager snapshot rename PASS mounted-denied=1 rename=1 CLI-enumeration=1 reimport-read=1 readonly-denied=1\n");
    return 0;
}
#ifdef OZ_TEST_POOL_PAGINATION
static int pool_pagination(void)
{
#define PCHECK(x) do{if(!(x)){bug("RESULT native pool pagination FAIL line=%d\n",__LINE__);return 20;}}while(0)
    const unsigned count=OZ_MAX_ROWS+1;
    char *zero=calloc(1,1024*1024);PCHECK(zero);
    for(unsigned i=0;i<count;i++) {
        char name[64],file_name[96],canonical[256],path[258];
        snprintf(name,sizeof(name),"page_pool_%02u",i);
        snprintf(file_name,sizeof(file_name),"RAM:page-pool-%02u.vdev",i);
        BPTR file=Open((CONST_STRPTR)file_name,MODE_NEWFILE);PCHECK(file);
        for(unsigned n=0;n<128;n++)PCHECK(Write(file,zero,1024*1024)==1024*1024);
        PCHECK(Close(file));
        BPTR lock=Lock((CONST_STRPTR)file_name,ACCESS_READ);PCHECK(lock);
        int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);PCHECK(named);
        char *colon=strchr(canonical,':');PCHECK(colon);*colon='/';
        snprintf(path,sizeof(path),"/%s",canonical);
        RUN(0,NULL,"C:zpool","create","-o","cachefile=none","-O","mountpoint=none",name,path);
        bug("Native pagination pool created %u/%u\n",i+1,count);
    }
    free(zero);
    struct oz_client client;PCHECK(oz_client_open(&client));
    struct oz_message *m=calloc(1,sizeof(*m));PCHECK(m);
    unsigned seen[OZ_MAX_ROWS+1]={0},baseline=0,total=0,pages=0;
    UQUAD cursor=0;
    do {
        memset(m,0,sizeof(*m));m->operation=3;m->cursor=cursor;
        PCHECK(oz_submit(&client,m)==OZ_OK);
        struct oz_message *out;while(!(out=oz_poll(&client)))Delay(1);
        PCHECK(out->status==OZ_OK&&out->row_count&&out->row_count<=OZ_MAX_ROWS);
        for(unsigned r=0;r<out->row_count;r++) {
            struct oz_row *row=&out->rows[r];unsigned matched=0;
            if(!strcmp(row->name,"qemu_mirror")){baseline++;matched=1;}
            for(unsigned i=0;i<count;i++) {
                char name[64];snprintf(name,sizeof(name),"page_pool_%02u",i);
                if(!strcmp(row->name,name)){seen[i]++;matched++;}
            }
            PCHECK(matched==1&&row->total&&row->used<=row->total&&
                row->available==row->total-row->used);
        }
        total+=out->row_count;pages++;
        PCHECK(!out->next_cursor||out->next_cursor>cursor);
        cursor=out->next_cursor;oz_message_free(out);PCHECK(pages<=2);
    }while(cursor);
    PCHECK(pages==2&&total==count+1&&baseline==1);
    for(unsigned i=0;i<count;i++)PCHECK(seen[i]==1);
    /* The last named pool falls beyond page one when unfiltered. A filter
     * must be applied before cursor accounting, not after collecting a page. */
    for(unsigned end=0;end<2;end++) {
        memset(m,0,sizeof(*m));m->operation=3;m->cursor=end;
        snprintf(m->target,sizeof(m->target),"page_pool_%02u",count-1);
        PCHECK(oz_submit(&client,m)==OZ_OK);
        struct oz_message *out;while(!(out=oz_poll(&client)))Delay(1);
        PCHECK(out->status==OZ_OK&&out->row_count==(end?0:1)&&!out->next_cursor);
        if(!end)PCHECK(!strcmp(out->rows[0].name,m->target));
        oz_message_free(out);
    }
    free(m);oz_client_close(&client);
    for(unsigned i=0;i<count;i++) {
        char name[64],file_name[96];snprintf(name,sizeof(name),"page_pool_%02u",i);
        RUN(0,NULL,"C:zpool","destroy",name);
        snprintf(file_name,sizeof(file_name),"RAM:page-pool-%02u.vdev",i);
        PCHECK(DeleteFile((CONST_STRPTR)file_name));
    }
    bug("RESULT native pool pagination PASS pages=2 pools=26 unique=26 exact-filter=1 end-cursor=1 cleanup=25\n");
#undef PCHECK
    return 0;
}
#endif
int main(void)
{
#ifdef OZ_TEST_RUNTIME_CHECK
    RUN(0,"PASS checked runtime prerequisites only","C:OpenZFSRuntimeCheck");
    bug("RESULT native runtime check PASS child-stdio=1 configured-security=1\n");
#endif
    if(gui_version())return 20;
#ifdef OZ_TEST_NATIVE_LABELCLEAR_ONLY
    return native_labelclear_test()?20:0;
#endif
#ifdef OZ_TEST_DATASET_ADMIN_ONLY
    return upgrade_pool()?20:0;
#endif
#ifdef OZ_TEST_LIVE_CREATE_ONLY
    return live_iostat_rates()?20:0;
#endif
#ifdef OZ_TEST_DOS_PUBLICATION_FAILURE
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZPUBFAIL:","qemu_mirror/publication-failure");
    RUN(1,NULL,"C:zfs","mount","qemu_mirror/publication-failure");
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","qemu_mirror/publication-failure");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/publication-failure");
    RUN(0,"yes","C:zfs","get","-H","-o","value","mounted","qemu_mirror/publication-failure");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/publication-failure");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/publication-failure");
    bug("RESULT native DOS publication recovery PASS failed-not-mounted=1 retry=1 unmount=1 destroy=1\n");
    BPTR close_sentinel=Open((CONST_STRPTR)"RAM:openzfs-retired-close-failure",MODE_NEWFILE);
    if(!close_sentinel||!Close(close_sentinel))return 20;
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLOSEFAIL:","qemu_mirror/retired-close-failure");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/retired-close-failure");
    struct DevProc *retired=GetDeviceProc((CONST_STRPTR)"OZCLOSEFAIL:",NULL);
    if(!retired)return 20;
    RUN(1,NULL,"C:zfs","unmount","qemu_mirror/retired-close-failure");
    /* The sentinel holds every owner-pump retry in EIO through this request.
     * The detached filesystem stays hidden, but cannot be remounted until its
     * cleanup completes. */
    RUN(1,NULL,"C:zfs","mount","qemu_mirror/retired-close-failure");
    if(!DeleteFile((CONST_STRPTR)"RAM:openzfs-retired-close-failure"))return 20;
    if(DoPkt(retired->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)!=DOSFALSE||
        IoErr()!=ERROR_DEVICE_NOT_MOUNTED){FreeDeviceProc(retired);return 20;}
    /* The next service wake retries the detached filesystem close before
     * dispatch. It must be absent from active mount state even after EIO. */
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","qemu_mirror/retired-close-failure");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/retired-close-failure");
    struct DevProc *current=GetDeviceProc((CONST_STRPTR)"OZCLOSEFAIL:",NULL);
    if(!current||current->dvp_Port==retired->dvp_Port||
        DoPkt(current->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)!=DOSTRUE||
        DoPkt(retired->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)!=DOSFALSE||
        IoErr()!=ERROR_DEVICE_NOT_MOUNTED) {
        if(current)FreeDeviceProc(current);
        FreeDeviceProc(retired);return 20;
    }
    FreeDeviceProc(current);
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/retired-close-failure");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/retired-close-failure");
    FreeDeviceProc(retired);
    bug("RESULT native retired close recovery PASS detach-error=1 sentinel-held=1 remount-blocked=1 active-hidden=1 retry=1 distinct-remount=1 late-packet=1 cleanup=1\n");
#endif
#ifdef OZ_TEST_MOUNT_REGISTRY_ALLOC
    if(mount_registry_allocation_test())return 20;
#endif
#ifdef OZ_TEST_ERROR_LOG
    if(error_log_test())return 20;
#endif
#ifdef OZ_TEST_RECEIVED_PROPERTIES
    if(received_properties_test())return 20;
#endif
#ifdef OZ_TEST_POOL_PAGINATION
    if(pool_pagination())return 20;
#endif
    if(oz_iostat_rates_test())return 20;
#ifndef OZ_TEST_CLI_COLD
    /* Cold CLI starts with no imported pool and must stay readonly. */
    if(live_iostat_rates())return 20;
    if(live_histograms(0)||live_histograms(1))return 20;
#endif
#if defined(OZ_TEST_ZVOL_TRANSPORT) && !defined(OZ_TEST_ZVOL_DISCARD_COLD)
    if(zvol_transport_test())return 20;
    if(upgrade_pool())return 20;
    if(channel_program())return 20;
    if(redact_pool())return 20;
    if(events_pool())return 20;
#endif
#if defined(OZ_TEST_ZVOL_DEVICE) && !defined(OZ_TEST_ZVOL_DEVICE_COLD)
    if(device_test())return 20;
#endif
#ifdef OZ_TEST_CLI_COLD
#ifdef OZ_TEST_NATIVE_CREATE_COLD
    RUN(1,NULL,"C:zpool","list","cli_native_created");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        CREATED_IMPORT_PATHS,"cli_native_created");
#ifdef OZ_TEST_TYPED_NATIVE_MIRROR
    RUN(0,"DEGRADED","C:zpool","status","-P","cli_native_created");
#endif
    RUN(0,"/AROS/ata.device/3/partition/0","C:zpool","status","-P","cli_native_created");
    RUN(0,"on","C:zpool","get","-H","-o","value","readonly","cli_native_created");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_native_created/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_native_created/files");
    RUN(0,NULL,"C:zpool","export","cli_native_created");
    bug("RESULT native CLI create cold PASS partition-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576\n");
    return 0;
#endif
#ifdef OZ_TEST_NATIVE_SPLIT_COLD
    RUN(1,NULL,"C:zpool","list","cli_split_new");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d","/AROS/ata.device/3/partition/0","cli_split_new");
    RUN(0,"/AROS/ata.device/3/partition/0","C:zpool","status","-P","cli_split_new");
    if(strstr(last_command_output,"cli-split-a.vdev")||strstr(last_command_output,"mirror"))return 20;
    RUN(0,"on","C:zpool","get","-H","-o","value","readonly","cli_split_new");
    RUN(0,NULL,"C:zfs","mount","-o","ro","cli_split_new/files");
    if(readonly_file()||growth_payload(0))return 20;
    RUN(0,NULL,"C:zfs","unmount","cli_split_new/files");
    RUN(0,NULL,"C:zpool","export","cli_split_new");
    bug("RESULT native CLI split cold PASS partition-only=1 normalized-read=1 readonly=1 exported=1 payload-bytes=1048576\n");
#endif
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
#ifdef OZ_TEST_CLI_TINY_REPLY
    const long import_status=1;
#else
    const long import_status=0;
#endif
    RUN(import_status,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
#ifdef OZ_TEST_ZVOL_DEVICE_COLD
    if(device_test())return 20;
#endif
#ifdef OZ_TEST_ZVOL_DISCARD_COLD
    if(zvol_transport_test())return 20;
#endif
#ifdef OZ_TEST_COMMAND_HISTORY
    RUN(0,"history-thread-beta","C:zpool","history","qemu_mirror");
    if(strstr(last_command_output,"history-thread-alpha")||
       strstr(last_command_output,"history-forbidden-marker"))return 20;
    bug("RESULT native CLI cold history PASS persisted=1 isolated=1\n");
#endif
    RUN(0,"formC","C:zfs","get","-H","-o","value","normalization","qemu_mirror/cli-import");
    RUN(0,"insensitive","C:zfs","get","-H","-o","value","casesensitivity","qemu_mirror/cli-import");
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/cli-import");
    if(readonly_file()){bug("RESULT native CLI cold FAIL file\n");return 20;}
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-import");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
    bug("RESULT native CLI cold PASS import=1 properties=2 normalized-read=1 readonly=1 export=1\n");
#ifdef OZ_TEST_RAW_RESUME_COLD
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=off",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    if(cold_raw_resume())return 20;
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
#endif
#ifdef OZ_TEST_PERMISSION_AUTHORIZE
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=off",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    if(cold_permission_authorization())return 20;
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
#endif
#ifdef OZ_TEST_CLI_TINY_REPLY
    bug("RESULT native CLI tiny reply PASS imported=1 readable=1 exported=1\n");
#endif
    return 0;
#else
#ifdef OZ_TEST_CLI_POOL_CREATE
    if(create_pool())return 20;
#endif
#ifdef OZ_TEST_NATIVE_REPATH
    if(native_repath())return 20;
#endif
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    RUN(0,"qemu_mirror/files","C:zfs","list","-H","qemu_mirror/files");
    if(deferred_snapshot_destroy_test())return 20;
    if(recursive_snapshot_create_test())return 20;
    if(recursive_snapshot_holds_test())return 20;
    if(bookmark_pagination_test())return 20;
    if(status_cursor_boundary_test())return 20;
    if(history_cursor_boundary_test())return 20;
    if(recursive_snapshot_destroy_test())return 20;
    if(recursive_dataset_destroy_test())return 20;
    if(recursive_property_query_test())return 20;
    if(all_property_query_test())return 20;
    RUN(1,"native volume root","C:zfs","create","-u","-o","mountpoint=:","qemu_mirror/cli-invalid");
    RUN(1,"native volume root","C:zfs","create","-u","-o","mountpoint=OZCLI:child","qemu_mirror/cli-invalid");
    RUN(1,NULL,"C:zfs","list","qemu_mirror/cli-invalid");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:",
        "-o","normalization=formC","-o","utf8only=on",
        "-o","casesensitivity=insensitive","qemu_mirror/cli-files");
    RUN(0,"formC","C:zfs","get","-H","-o","value","normalization","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","set","test:origin=upstream-cli","qemu_mirror/cli-files");
    RUN(0,"upstream-cli","C:zfs","get","-H","-o","value","test:origin","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-files");
    RUN(0,"yes","C:zfs","get","-H","-o","value","mounted","qemu_mirror/cli-files");
    if(gui_property()||dos_file()) {
        bug("RESULT native service CLI FAIL GUI-property or DOS-file\n");return 20;
    }
    BPTR held=Open((CONST_STRPTR)"OZCLI:caf\xc3\xa9",MODE_OLDFILE);
    if(!held)return 20;
    char *busy[]={"C:zfs","unmount","qemu_mirror/cli-files",NULL};
    int busy_error=run(busy,1,NULL);
    char *busy_export[]={"C:zpool","export","qemu_mirror",NULL};
    if(run(busy_export,1,NULL))busy_error=1;
    char held_bytes[9]={0};
    if(Read(held,held_bytes,sizeof(held_bytes))!=8||memcmp(held_bytes,"CLI data",8))busy_error=1;
    int close_error=!Close(held);
    if(busy_error||close_error){bug("RESULT native service CLI FAIL busy unmount\n");return 20;}
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-files");
    RUN(0,"no","C:zfs","get","-H","-o","value","mounted","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/cli-files");
    BPTR forbidden=Open((CONST_STRPTR)"OZCLI:readonly-denied",MODE_NEWFILE);
    LONG denied=IoErr();
    if(forbidden){Close(forbidden);bug("RESULT native service CLI FAIL readonly write allowed\n");return 20;}
    if(denied!=ERROR_DISK_WRITE_PROTECTED&&denied!=ERROR_WRITE_PROTECTED) {
        bug("RESULT native service CLI FAIL readonly error=%ld\n",(long)denied);return 20;
    }
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-files");
    RUN(0,NULL,"C:zfs","snapshot","qemu_mirror/cli-files@first");
    RUN(0,"qemu_mirror/cli-files@first","C:zfs","list","-H","-t","snapshot","qemu_mirror/cli-files@first");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-files@first");
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-files");
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    secLogoutA(quiet);
    if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    if(getuid()!=77||geteuid()!=77)return 20;
    RUN(1,"permission denied","C:zpool","import","-d",OZ_TEST_PRIMARY0);
    RUN(1,NULL,"C:zpool","export","qemu_mirror");
    secLogoutA(quiet);
    if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    if(getuid()!=0||geteuid()!=0)return 20;
    CloseLibrary(secBase);
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    RUN(1,NULL,"C:zpool","export","-f","qemu_mirror");
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
#ifdef OZ_TEST_CLI_DISCOVERY
    /* The second member appears in labels, but was not selected. Do not
     * expand read admission merely to satisfy that label-derived path. */
    RUN(0,"no pools available","C:zpool","import","-d",OZ_TEST_PRIMARY0);
    RUN(0,"qemu_mirror","C:zpool","import","-d",OZ_TEST_PRIMARY0,
        "-d",OZ_TEST_PRIMARY1);
    RUN(0,"qemu_mirror","C:zpool","import","-d",OZ_TEST_PRIMARY0,
        "-d",OZ_TEST_PRIMARY1);
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
    bug("RESULT native CLI discovery PASS mirror=1 repeat=1 excluded-path=1 not-imported=1\n");
#endif
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase)return 20;
    secLogoutA(quiet);if(secLoginA(reader)!=((77UL<<16)|88))return 20;
    RUN(1,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)return 20;
    CloseLibrary(secBase);
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
    RUN(1,NULL,"C:zpool","import","-N",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(1,NULL,"C:zpool","list","-H","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    RUN(0,NULL,"C:zfs","create","-u","-o","mountpoint=OZCLI:",
        "-o","normalization=formC","-o","utf8only=on","-o","casesensitivity=insensitive",
        "qemu_mirror/cli-import");
    RUN(0,NULL,"C:zfs","mount","qemu_mirror/cli-import");
    if(dos_file()){bug("RESULT native CLI import FAIL DOS write\n");return 20;}
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-import");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none","-o","readonly=on",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
    RUN(0,NULL,"C:zfs","mount","-o","ro","qemu_mirror/cli-import");
    if(readonly_file())return 20;
    RUN(0,NULL,"C:zfs","unmount","qemu_mirror/cli-import");
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    RUN(0,NULL,"C:zpool","import","-N","-o","cachefile=none",
        "-d",OZ_TEST_PRIMARY0,"-d",OZ_TEST_PRIMARY1,"qemu_mirror");
#ifndef OZ_TEST_CLI_KEEP
    RUN(0,NULL,"C:zfs","destroy","qemu_mirror/cli-import");
#else
    bug("RESULT native CLI retained PASS dataset=qemu_mirror/cli-import\n");
#endif
    RUN(0,NULL,"C:zpool","export","qemu_mirror");
    bug("RESULT native CLI import PASS writable=1 readonly=1 persisted=1 repeat=1\n");
    bug("RESULT native CLI import admission PASS nonroot=1 implicit-cache-rejected=1 recovery=1\n");
    if(reimport()){bug("RESULT native service CLI FAIL reimport\n");return 20;}
    RUN(0,"qemu_mirror","C:zpool","list","-H","qemu_mirror");
    bug("RESULT native CLI export PASS busy=1 force-rejected=1 export=1 API-reimport=1\n");
    bug("RESULT native CLI export permission PASS nonroot-denied=1 pool-retained=1\n");
    bug("RESULT native CLI identity PASS root=0 reader=77 discovery-denied=1\n");
    bug("RESULT native service CLI PASS list=1 create=1 property=1 snapshot=1 destroy=1\n");
    bug("RESULT native CLI integration PASS GUI-property=1 DOS-file=1 normalized-lookup=1\n");
    bug("RESULT native CLI mounts PASS mount=1 unmount=1 busy=1 repeat=1 readonly=1\n");
#ifdef OZ_TEST_NATIVE_LABELCLEAR
    if(native_labelclear_test())return 20;
#endif
    return 0;
#endif
}
