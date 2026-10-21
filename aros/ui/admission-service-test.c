/* SPDX-License-Identifier: CDDL-1.0 */
/* Separate native client process: no upstream engine linked here. */
#include "admission-api.h"
#include "admission-worker.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/record.h>
#include <dos/dosextens.h>
#include <dos/notify.h>
#include <dos/exall.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int present(const char *name)
{ Forbid();int found=FindPort((CONST_STRPTR)name)!=NULL;Permit();return found; }
static ULONG login(struct Library *secBase,const char *user)
{
    struct TagItem tags[]={{secT_UserID,(IPTR)user},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    return secLoginA(tags);
}
static ULONG logout(struct Library *secBase)
{struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};return secLogoutA(tags);}
static int legacy_page(unsigned op,const char *target,const char *argument,
    const char *options,uint64_t cursor,struct oz_message *result)
{
    struct oz_client c;if(!oz_client_open(&c))return ENOMEM;
    c.server_name=OZ_NATIVE_TEST_PORT;
    memset(result,0,sizeof(*result));result->operation=op;result->flags=OZ_CONFIRMED;
    result->cursor=cursor;
    snprintf(result->target,sizeof(result->target),"%s",target);
    snprintf(result->argument,sizeof(result->argument),"%s",argument?argument:"");
    snprintf(result->options,sizeof(result->options),"%s",options?options:"");
    int error=oz_submit(&c,result)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;
        while(!(reply=oz_poll(&c)))Delay(1);
        memcpy(result,reply,sizeof(*result));oz_message_free(reply);
        if(result->status!=OZ_OK)error=EIO;
    }
    oz_client_close(&c);return error;
}
static int legacy_args(unsigned op,const char *target,const char *argument,
    const char *options,struct oz_message *result)
{return legacy_page(op,target,argument,options,0,result);}
static int legacy(unsigned op,const char *target,struct oz_message *result)
{return legacy_args(op,target,NULL,NULL,result);}
static int poll_result(struct oz_ticket *ticket,struct oz_message *result)
{
    int error;
    do {error=oz_admission_query(OZ_ADMISSION_TEST_PORT,ticket,result);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    return error;
}
static int worker_options_test(unsigned operation,const char *target,const char *argument,const char *options,
    ULONG expected_status,int expected_errno)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=operation;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"%s",target);
    snprintf(m->argument,sizeof(m->argument),"%s",argument?argument:"");
    snprintf(m->options,sizeof(m->options),"%s",options?options:"");
    struct oz_admission_worker *worker=NULL;
    int error=oz_worker_start(OZ_ADMISSION_TEST_PORT,m,&worker);
    memset(m,0xa5,sizeof(*m));free(m); /* borrowed GUI request is gone */
    if(error){free(out);return error;}
    int refused=oz_worker_free(worker)==EBUSY;
    struct oz_ticket ticket;int transport=0,release=0;
    do {
        error=oz_worker_poll(worker,out,&ticket,&transport,&release);
        if(error==EAGAIN)Delay(1);
    }while(error==EAGAIN);
    if(error)return error; /* retain live worker on join failure */
    char expected[32];snprintf(expected,sizeof(expected),"errno=%d",expected_errno);
    const char *reported=strstr(out->detail,"errno=");
    int pass=refused&&!transport&&!release&&ticket.number&&ticket.epoch&&
        out->operation==operation&&out->status==expected_status&&!strcmp(out->target,target)&&
        reported&&!strcmp(reported,expected);
    if(!pass) {
        char diagnostic[512];snprintf(diagnostic,sizeof(diagnostic),
            "Worker mismatch operation=%u status=%lu expected=%lu %s detail=%.400s\n",
            operation,(unsigned long)out->status,(unsigned long)expected_status,expected,out->detail);
        for(char *p=diagnostic;*p;p++)RawPutChar(*p);
    }
    error=oz_worker_free(worker);free(out);
    return error?error:pass?0:EIO;
}
static int worker_argument_test(unsigned operation,const char *target,const char *argument,ULONG status,int error)
{return worker_options_test(operation,target,argument,NULL,status,error);}
static int worker_test(unsigned operation,const char *target,ULONG status,int error)
{return worker_argument_test(operation,target,NULL,status,error);}
static int mounted_state_test(const char *dataset,const char *expected)
{
    struct oz_message *listed=calloc(1,sizeof(*listed));if(!listed)return ENOMEM;
    int error=legacy(36,dataset,listed);
    if(!error&&(listed->row_count!=1||strcmp(listed->rows[0].name,dataset)||
        strcmp(listed->rows[0].state,expected)))error=EIO;
    char line[256];snprintf(line,sizeof(line),"Native mount state dataset=%s state=%s result=%d\n",
        dataset,listed->row_count?listed->rows[0].state:"missing",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    free(listed);return error;
}
static int receive_api_test(int write)
{
    const char *path="RAM:OpenZFS-api-receive.zfs";
    const char *dataset="qemu_mirror/api-received";
    BPTR source=0,dest=0;int error=0,source_mounted=0,target_mounted=0,created=0,local_mounted=0;
    char bytes[4096];LONG count;
#define RECV_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    RECV_CHECK(!worker_options_test(43,"qemu_mirror/stream-artifacts","OZREPLSRC","readonly=on",OZ_OK,0));
    source_mounted=1;
    RECV_CHECK(!mounted_state_test("qemu_mirror/stream-artifacts","read-only mount OZREPLSRC:"));
    dest=Lock((CONST_STRPTR)path,ACCESS_READ);
    if(dest){UnLock(dest);dest=0;error=EEXIST;goto done;}
    RECV_CHECK(!worker_argument_test(60,"qemu_mirror/stream-source@base",path,OZ_OK,0));
    created=1;
    RECV_CHECK(!worker_argument_test(60,"qemu_mirror/stream-source@base",path,OZ_ENGINE_ERROR,EEXIST));
    source=Open((CONST_STRPTR)"OZREPLSRC:full.zfs",MODE_OLDFILE);RECV_CHECK(source);
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-self@busy","OZREPLSRC:full.zfs",
        OZ_ENGINE_ERROR,write?EBUSY:EROFS));
    {LONG rc=Close(source);source=0;RECV_CHECK(rc);}
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-self@directory","OZREPLSRC:",
        OZ_ENGINE_ERROR,write?EISDIR:EROFS));
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-self@missing","OZREPLSRC:missing.zfs",
        OZ_ENGINE_ERROR,write?ENOENT:EROFS));
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-self@base","OZREPLSRC:full.zfs",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-self@second","OZREPLSRC:incremental.zfs",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    RECV_CHECK(!worker_options_test(43,"qemu_mirror/api-self","OZLOCALREAD","readonly=on",OZ_OK,0));local_mounted=1;
    source=Open((CONST_STRPTR)"OZLOCALREAD:hello.txt",MODE_OLDFILE);RECV_CHECK(source);
    count=Read(source,bytes,sizeof(bytes));
    const char *local_expected="Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";
    RECV_CHECK(count==(LONG)strlen(local_expected)&&!memcmp(bytes,local_expected,(size_t)count));
    {LONG rc=Close(source);source=0;RECV_CHECK(rc);}
    RECV_CHECK(!worker_test(44,"qemu_mirror/api-self",OZ_OK,0));local_mounted=0;
    {const char *line="Native own-volume full/incremental receive content PASS\n";
        for(const char *p=line;*p;p++)RawPutChar(*p);}
    RECV_CHECK(!worker_test(44,"qemu_mirror/stream-artifacts",OZ_OK,0));source_mounted=0;
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-missing@base","OZREPLMISSING:stream",
        OZ_ENGINE_ERROR,write?ENOENT:EROFS));
    RECV_CHECK(!worker_argument_test(61,write?"qemu_mirror/api-received@base":"qemu_mirror/api-readonly@base",path,
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    RECV_CHECK(!worker_options_test(43,dataset,"OZREPLDST","readonly=on",OZ_OK,0));target_mounted=1;
    source=Open((CONST_STRPTR)"OZREPLDST:hello.txt",MODE_OLDFILE);RECV_CHECK(source);
    count=Read(source,bytes,sizeof(bytes));
    const char *expected=write?"OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n":
        "Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";
    RECV_CHECK(count==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)count));
    {LONG closed=Close(source);source=0;RECV_CHECK(closed);}
    RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-received@blocked",path,
        OZ_ENGINE_ERROR,write?EBUSY:EROFS));
    RECV_CHECK(!worker_test(44,dataset,OZ_OK,0));target_mounted=0;
    if(write){
        RECV_CHECK(!worker_options_test(43,"qemu_mirror/stream-artifacts","OZREPLINC","readonly=on",OZ_OK,0));
        source_mounted=1;
        /* Remove only the private output created by this test above. */
        RECV_CHECK(DeleteFile((CONST_STRPTR)path));created=0;
        RECV_CHECK(!worker_test(44,"qemu_mirror/stream-artifacts",OZ_OK,0));source_mounted=0;
        RECV_CHECK(!worker_options_test(60,"qemu_mirror/stream-source@second",path,
            "from=qemu_mirror/stream-source@base",OZ_OK,0));
        created=1;
        RECV_CHECK(!worker_argument_test(61,"qemu_mirror/api-received@second",path,OZ_OK,0));
        RECV_CHECK(!worker_options_test(43,dataset,"OZREPLDST2","readonly=on",OZ_OK,0));target_mounted=1;
        source=Open((CONST_STRPTR)"OZREPLDST2:hello.txt",MODE_OLDFILE);RECV_CHECK(source);
        count=Read(source,bytes,sizeof(bytes));
        expected="Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";
        RECV_CHECK(count==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)count));
        {LONG closed=Close(source);source=0;RECV_CHECK(closed);}
    }
    {const char *line="Native authenticated incremental receive content PASS\n";
        for(const char *p=line;*p;p++)RawPutChar(*p);}
done:
    if(source)Close(source);if(dest)Close(dest);
    if(source_mounted){int rc=worker_test(44,"qemu_mirror/stream-artifacts",OZ_OK,0);if(!error)error=rc;}
    if(target_mounted){int rc=worker_test(44,dataset,OZ_OK,0);if(!error)error=rc;}
    if(local_mounted){int rc=worker_test(44,"qemu_mirror/api-self",OZ_OK,0);if(!error)error=rc;}
    if(created&&!DeleteFile((CONST_STRPTR)path)&&!error)error=EIO;
    char line[128];snprintf(line,sizeof(line),"Native authenticated receive API write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef RECV_CHECK
}

static int own_send_api_test(int write)
{
    int error=0,mounted=0,received=0,bookmark_mounted=0,resume_mounted=0;BPTR file=0;char bytes[128];
    char resume_options[1024];
#define SEND_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    struct oz_message *token=calloc(1,sizeof(*token));
    if(!token)return ENOMEM;
    int token_error=legacy_args(40,"qemu_mirror/resume-pending","receive_resume_token",NULL,token);
    int token_valid=!token_error&&token->row_count==1&&
        !strcmp(token->rows[0].kind,"property-long")&&
        token->rows[0].total==strlen(token->detail)&&strlen(token->detail)>=256&&
        !strncmp(token->detail,"1-",2);
    if(token_valid)snprintf(resume_options,sizeof(resume_options),"resume=%s",token->detail);
    free(token);SEND_CHECK(token_valid);
    const char *token_notice="Native complete receive resume token API PASS\n";
    for(const char *p=token_notice;*p;p++)RawPutChar(*p);
    struct DosList *publication=LockDosList(LDF_ENTRY|LDF_WRITE);
    SEND_CHECK(publication);
    int publish_busy=worker_options_test(43,"qemu_mirror/direct-stream-policy","OZSENDOUT",
        write?"readonly=off":"readonly=on",OZ_ENGINE_ERROR,EBUSY);
    UnLockDosList(LDF_ENTRY|LDF_WRITE);
    SEND_CHECK(!publish_busy);
    SEND_CHECK(!worker_options_test(43,"qemu_mirror/direct-stream-policy","OZSENDOUT",
        write?"readonly=off":"readonly=on",OZ_OK,0));mounted=1;
    SEND_CHECK(!mounted_state_test("qemu_mirror/direct-stream-policy",
        write?"writable mount OZSENDOUT:":"read-only mount OZSENDOUT:"));
    /* A native enumerator can hold the DOS list while asking the service to
     * do work. Busy unmount must reply, not wait on this caller's lock. */
    struct DosList *held=LockDosList(LDF_ALL|LDF_READ);
    SEND_CHECK(held);
    int busy=worker_test(44,"qemu_mirror/direct-stream-policy",OZ_ENGINE_ERROR,EBUSY);
    UnLockDosList(LDF_ALL|LDF_READ);
    SEND_CHECK(!busy);
    const char *paths[]={"OZSENDOUT:api-full.zfs","OZSENDOUT:api-inc.zfs"};
    const char *invalid[]={"compressed=yes","compressed=on;compressed=off",
        "large-blocks=on;",";embedded=on","raw=invalid","from=",
        "from=qemu_mirror/stream-source","embedded=on;;compressed=on",
        "from=qemu_mirror/stream-source@base;from=qemu_mirror/stream-source@base",
        "resume=bad;compressed=off","resume=bad;resume=bad","resume=2-0-10-00",
        "resume=1-0-0-00","resume=1-0-10-0","resume=1-0-10-gg"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(invalid[0]);i++)
        SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-source@base",
            "OZSENDOUT:invalid-options.zfs",invalid[i],OZ_ENGINE_ERROR,EINVAL));
    {BPTR absent=Lock((CONST_STRPTR)"OZSENDOUT:invalid-options.zfs",ACCESS_READ);
        if(absent){UnLock(absent);error=EEXIST;goto done;}
        SEND_CHECK(IoErr()==ERROR_OBJECT_NOT_FOUND);}
    if(write)for(unsigned i=1;i<2;i++){
        /* This test creates only known-absent disposable fixture files. */
        BPTR exists=Lock((CONST_STRPTR)paths[i],ACCESS_READ);
        if(exists){UnLock(exists);error=EEXIST;goto done;}
        file=Open((CONST_STRPTR)paths[i],MODE_NEWFILE);SEND_CHECK(file);
        {LONG rc=Close(file);file=0;SEND_CHECK(rc);}
    }
    file=Open((CONST_STRPTR)paths[1],MODE_OLDFILE);SEND_CHECK(file);
    SEND_CHECK(!worker_argument_test(60,"qemu_mirror/stream-source@base",paths[1],
        OZ_ENGINE_ERROR,write?EBUSY:EROFS));
    {LONG rc=Close(file);file=0;SEND_CHECK(rc);}
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-source@base",paths[0],
        "compressed=on;large-blocks=on;embedded=on",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_argument_test(60,"qemu_mirror/stream-source@base",paths[0],
        OZ_ENGINE_ERROR,write?EEXIST:EROFS));
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-source@second",paths[1],
        "embedded=off;from=qemu_mirror/stream-source@base;compressed=on;large-blocks=off",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_options_test(61,"qemu_mirror/api-local-send@base",paths[0],
        "resumable=maybe",OZ_ENGINE_ERROR,EINVAL));
    SEND_CHECK(!worker_options_test(61,"qemu_mirror/api-local-send@base",paths[0],
        "resumable=on",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_argument_test(61,"qemu_mirror/api-local-send@second",paths[1],
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_options_test(43,"qemu_mirror/api-local-send","OZSENDREAD","readonly=on",OZ_OK,0));received=1;
    file=Open((CONST_STRPTR)"OZSENDREAD:hello.txt",MODE_OLDFILE);SEND_CHECK(file);
    const char *expected="Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";
    LONG count=Read(file,bytes,sizeof(bytes));
    SEND_CHECK(count==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)count));
    {LONG rc=Close(file);file=0;SEND_CHECK(rc);}
    if(write){
        SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-advanced@second",
            "OZSENDOUT:missing-bookmark.zfs","from=qemu_mirror/stream-advanced#missing",
            OZ_ENGINE_ERROR,ESRCH));
        BPTR absent=Lock((CONST_STRPTR)"OZSENDOUT:missing-bookmark.zfs",ACCESS_READ);
        if(absent){UnLock(absent);error=EEXIST;goto done;}
        SEND_CHECK(IoErr()==ERROR_OBJECT_NOT_FOUND);
    }
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-advanced@second",
        "OZSENDOUT:api-bookmark.zfs",
        "from=qemu_mirror/stream-advanced#base;compressed=on;large-blocks=on;embedded=on",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_argument_test(61,"qemu_mirror/plain-received@second",
        "OZSENDOUT:api-bookmark.zfs",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_options_test(43,"qemu_mirror/plain-received","OZBOOKREAD",
        "readonly=on",OZ_OK,0));bookmark_mounted=1;
    file=Open((CONST_STRPTR)"OZBOOKREAD:hello.txt",MODE_OLDFILE);SEND_CHECK(file);
    count=Read(file,bytes,sizeof(bytes));
    SEND_CHECK(count==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)count));
    {LONG rc=Close(file);file=0;SEND_CHECK(rc);}
    const char *notice="Native authenticated bookmark incremental content PASS\n";
    for(const char *p=notice;*p;p++)RawPutChar(*p);
    char damaged[1024];strcpy(damaged,resume_options);
    damaged[9]=damaged[9]=='0'?'1':'0'; /* checksum nibble */
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-advanced@second",
        "OZSENDOUT:bad-token.zfs",damaged,OZ_ENGINE_ERROR,EINVAL));
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-source@second",
        "OZSENDOUT:bad-token.zfs",resume_options,OZ_ENGINE_ERROR,ESTALE));
    {BPTR absent=Lock((CONST_STRPTR)"OZSENDOUT:bad-token.zfs",ACCESS_READ);
        if(absent){UnLock(absent);error=EEXIST;goto done;}
        SEND_CHECK(IoErr()==ERROR_OBJECT_NOT_FOUND);}
    SEND_CHECK(!worker_options_test(60,"qemu_mirror/stream-advanced@second",
        "OZSENDOUT:api-resumed.zfs",resume_options,write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_options_test(61,"qemu_mirror/resume-api-pending@second",
        "OZSENDOUT:api-resumed.zfs","resumable=on",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SEND_CHECK(!worker_options_test(43,"qemu_mirror/resume-api-pending","OZRESUMEREAD",
        "readonly=on",OZ_OK,0));resume_mounted=1;
    file=Open((CONST_STRPTR)"OZRESUMEREAD:hello.txt",MODE_OLDFILE);SEND_CHECK(file);
    count=Read(file,bytes,sizeof(bytes));
    SEND_CHECK(count==(LONG)strlen(expected)&&!memcmp(bytes,expected,(size_t)count));
    {LONG rc=Close(file);file=0;SEND_CHECK(rc);}
    notice="Native authenticated token continuation content PASS\n";
    for(const char *p=notice;*p;p++)RawPutChar(*p);
done:
    if(file)Close(file);
    if(resume_mounted){int rc=worker_test(44,"qemu_mirror/resume-api-pending",OZ_OK,0);if(!error)error=rc;}
    if(bookmark_mounted){int rc=worker_test(44,"qemu_mirror/plain-received",OZ_OK,0);if(!error)error=rc;}
    if(received){int rc=worker_test(44,"qemu_mirror/api-local-send",OZ_OK,0);if(!error)error=rc;}
    if(mounted){int rc=worker_test(44,"qemu_mirror/direct-stream-policy",OZ_OK,0);if(!error)error=rc;}
    char line[128];snprintf(line,sizeof(line),"Native own-volume send roundtrip write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef SEND_CHECK
}

static int io_statistics_test(int write)
{
    struct oz_message *first=calloc(1,sizeof(*first)),*page=calloc(1,sizeof(*page));
    int error=0;unsigned leaves=0;
#define IO_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    IO_CHECK(first&&page);
    IO_CHECK(!legacy(35,"qemu_mirror",first));
    IO_CHECK(first->row_count==8&&!first->next_cursor);
    for(unsigned i=0;i<first->row_count;i+=2) {
        struct oz_row *bytes=&first->rows[i],*ops=&first->rows[i+1];
        IO_CHECK(!strcmp(bytes->kind,"io-bytes")&&!strcmp(ops->kind,"io-ops"));
        IO_CHECK(!strcmp(bytes->name,ops->name)&&!strcmp(bytes->parent,ops->parent));
        IO_CHECK(!strcmp(bytes->state,"ONLINE")&&!strcmp(ops->state,"ONLINE"));
        IO_CHECK(bytes->total>0&&bytes->total==ops->total);
        if(strstr(bytes->value,"type=file ")||strstr(bytes->value,"type=disk ")) {
            IO_CHECK(bytes->used>0&&ops->used>0);leaves++;
            if(write)IO_CHECK(bytes->available>0&&ops->available>0);
        }
    }
    IO_CHECK(leaves==2);
    /* Start inside a byte/operation pair: pagination must not skip or repeat. */
    IO_CHECK(!legacy_page(35,"qemu_mirror",NULL,NULL,3,page));
    IO_CHECK(page->row_count==5&&!page->next_cursor);
    for(unsigned i=0;i<page->row_count;i++) {
        IO_CHECK(!strcmp(page->rows[i].kind,first->rows[i+3].kind));
        IO_CHECK(!strcmp(page->rows[i].name,first->rows[i+3].name));
        IO_CHECK(page->rows[i].total>=first->rows[i+3].total);
        IO_CHECK(page->rows[i].used>=first->rows[i+3].used);
        IO_CHECK(page->rows[i].available>=first->rows[i+3].available);
    }
    IO_CHECK(!legacy_page(35,"qemu_mirror",NULL,NULL,8,page)&&!page->row_count);
    IO_CHECK(legacy_args(35,"qemu_mirror","invalid",NULL,page)==EIO&&page->status==OZ_UNSUPPORTED);
    IO_CHECK(legacy_args(35,"qemu_mirror",NULL,"invalid",page)==EIO&&page->status==OZ_UNSUPPORTED);
    IO_CHECK(legacy(35,"qemu_mirror/missing",page)==EIO&&page->status==OZ_ENGINE_ERROR&&!page->row_count);
done:
    free(first);free(page);
    char line[128];snprintf(line,sizeof(line),"Native IO statistics write=%d leaves=%u result=%d\n",write,leaves,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef IO_CHECK
}
#include "pool-create-test.h"
#include "pool-add-test.h"
#include "pool-split-test.h"
static int authenticated_scrub_test(int write,struct oz_message *out)
{
    int error=worker_argument_test(18,"qemu_mirror","invalid",OZ_ENGINE_ERROR,EINVAL);
    if(!error)error=worker_argument_test(18,"qemu_mirror/dataset","start",OZ_ENGINE_ERROR,EINVAL);
    const char *actions[]={"start","pause","resume","stop"};
    if(!write)for(unsigned i=0;i<4&&!error;i++)
        error=worker_argument_test(18,"qemu_mirror",actions[i],OZ_ENGINE_ERROR,EROFS);
    if(write&&!error)error=worker_argument_test(18,"qemu_mirror","start",OZ_OK,0);
    int finished=0;
    for(unsigned i=0;!error&&!finished&&i<60;i++) {
        error=legacy(4,"qemu_mirror",out);
        const char *scan=strstr(out->detail,"scan=scrub state=finished");
        if(!error&&scan) {
            unsigned long long examined=0,total=0,issued=0,errors=0;
            if(sscanf(scan,"scan=scrub state=finished examined=%llu to-examine=%llu issued=%llu errors=%llu",
                &examined,&total,&issued,&errors)!=4||!examined||!issued||errors)error=EIO;
            else finished=1;
            for(const char *p=scan;*p;p++)RawPutChar(*p);
            RawPutChar('\n');
        }
        if(!write&&!finished&&!error)error=EIO;
        if(!error&&!finished)Delay(50);
    }
    if(!error&&!finished)error=ETIMEDOUT;
    char line[100];snprintf(line,sizeof(line),"Native authenticated scrub write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int property_matches(const char *dataset,const char *name,const char *value,const char *source)
{
    struct oz_message *out=calloc(1,sizeof(*out));if(!out)return 0;
    int match=!legacy_args(40,dataset,name,NULL,out)&&out->row_count==1&&
        !strcmp(out->rows[0].name,name)&&!strcmp(out->rows[0].value,value)&&
        !strcmp(out->rows[0].state,source);
    free(out);return match;
}
#include "pool-property-test.h"
#include "pool-history-test.h"
static int user_property_tests(int write)
{
    const char *parent="qemu_mirror/delegated-root";
    const char *child="qemu_mirror/delegated-root/moved";
    const char *snapshot="qemu_mirror/delegated-root/moved@keep";
    int error=0;
#define PROP_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    PROP_CHECK(!worker_argument_test(41,"qemu_mirror/worker-created","org.aros:test=denied",OZ_ENGINE_ERROR,EPERM));
    PROP_CHECK(!worker_argument_test(42,"qemu_mirror/worker-created","org.aros:test",OZ_ENGINE_ERROR,EPERM));
    /* dedup is implemented; USERPROP delegation does not authorize it. */
    PROP_CHECK(!worker_argument_test(41,child,"dedup=on",OZ_ENGINE_ERROR,EPERM));
    PROP_CHECK(!worker_argument_test(41,child,"org.aros:test",OZ_ENGINE_ERROR,EINVAL));
    PROP_CHECK(!worker_argument_test(42,child,"org.aros:test=invalid",OZ_ENGINE_ERROR,EINVAL));
    if(write) {
        char oversized[300];memset(oversized,'a',sizeof(oversized));
        oversized[3]=':';oversized[sizeof(oversized)-3]='=';
        oversized[sizeof(oversized)-2]='x';oversized[sizeof(oversized)-1]=0;
        PROP_CHECK(!worker_argument_test(41,child,oversized,OZ_ENGINE_ERROR,ENAMETOOLONG));
        PROP_CHECK(!worker_argument_test(41,parent,"org.aros:inherit=parent",OZ_OK,0));
        PROP_CHECK(!worker_argument_test(41,child,"org.aros:inherit=child",OZ_OK,0));
        PROP_CHECK(property_matches(child,"org.aros:inherit","child",child));
        PROP_CHECK(!worker_argument_test(42,child,"org.aros:inherit",OZ_OK,0));
        PROP_CHECK(property_matches(child,"org.aros:inherit","parent",parent));
        PROP_CHECK(!worker_argument_test(41,child,"org.aros:test=",OZ_OK,0));
        PROP_CHECK(property_matches(child,"org.aros:test","",child));
        PROP_CHECK(!worker_argument_test(41,child,"org.aros:test=keep=a;b",OZ_OK,0));
        PROP_CHECK(!worker_argument_test(41,snapshot,"org.aros:test=snapshot",OZ_OK,0));
    } else {
        PROP_CHECK(!worker_argument_test(41,child,"org.aros:test=readonly",OZ_ENGINE_ERROR,EROFS));
        PROP_CHECK(!worker_argument_test(42,child,"org.aros:test",OZ_ENGINE_ERROR,EROFS));
    }
    PROP_CHECK(property_matches(child,"org.aros:test","keep=a;b",child));
    PROP_CHECK(property_matches(child,"org.aros:inherit","parent",parent));
    PROP_CHECK(property_matches(snapshot,"org.aros:test","snapshot",snapshot));
done:;
    char line[110];snprintf(line,sizeof(line),"Native authenticated user properties write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef PROP_CHECK
}
static int snapshot_control_tests(int write,const char *payload,size_t length)
{
    const char *origin="qemu_mirror/delegated-root/moved@clone-content";
    const char *clone="qemu_mirror/delegated-root/cloned";
    const char *held="qemu_mirror/delegated-root/moved@held";
    int error=0,mounted=0;BPTR file=0;
    struct oz_message *out=calloc(1,sizeof(*out));
#define SNAP_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    SNAP_CHECK(out);
    SNAP_CHECK(!worker_argument_test(52,"qemu_mirror/worker-created@auth",clone,OZ_ENGINE_ERROR,EPERM));
    SNAP_CHECK(!worker_argument_test(55,"qemu_mirror/worker-created@auth","denied",OZ_ENGINE_ERROR,EPERM));
    SNAP_CHECK(!worker_argument_test(56,"qemu_mirror/worker-created@auth","denied",OZ_ENGINE_ERROR,EPERM));
    SNAP_CHECK(!worker_argument_test(55,"qemu_mirror/hold-only@test","hold-only",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SNAP_CHECK(!worker_argument_test(56,"qemu_mirror/hold-only@test","hold-only",OZ_ENGINE_ERROR,EPERM));
    SNAP_CHECK(!legacy(57,"qemu_mirror/hold-only@test",out)&&out->row_count==1);
    SNAP_CHECK(!strcmp(out->rows[0].name,"hold-only"));
    if(write) {
        SNAP_CHECK(!worker_test(50,origin,OZ_OK,0));
        SNAP_CHECK(!worker_test(50,held,OZ_OK,0));
        SNAP_CHECK(!worker_argument_test(52,origin,"qemu_mirror/outside-clone",OZ_ENGINE_ERROR,EPERM));
        SNAP_CHECK(!worker_argument_test(52,origin,clone,OZ_OK,0));
        SNAP_CHECK(!worker_argument_test(52,origin,clone,OZ_ENGINE_ERROR,EEXIST));
        SNAP_CHECK(!worker_test(38,origin,OZ_ENGINE_ERROR,EEXIST));
        SNAP_CHECK(!worker_argument_test(55,held,"temporary-test",OZ_OK,0));
        SNAP_CHECK(!worker_argument_test(55,held,"temporary-test",OZ_ENGINE_ERROR,EEXIST));
        SNAP_CHECK(!worker_test(38,held,OZ_ENGINE_ERROR,EBUSY));
        SNAP_CHECK(!worker_argument_test(56,held,"temporary-test",OZ_OK,0));
        SNAP_CHECK(!worker_argument_test(56,held,"temporary-test",OZ_ENGINE_ERROR,ENOENT));
        SNAP_CHECK(!worker_test(38,held,OZ_OK,0));
        SNAP_CHECK(!worker_argument_test(55,origin,"persisted",OZ_OK,0));
    } else {
        SNAP_CHECK(!worker_argument_test(52,origin,"qemu_mirror/delegated-root/readonly-clone",OZ_ENGINE_ERROR,EROFS));
        SNAP_CHECK(!worker_argument_test(55,origin,"readonly",OZ_ENGINE_ERROR,EROFS));
        SNAP_CHECK(!worker_argument_test(56,origin,"persisted",OZ_ENGINE_ERROR,EROFS));
    }
    SNAP_CHECK(!legacy(57,origin,out)&&out->row_count==1);
    SNAP_CHECK(!strcmp(out->rows[0].name,"persisted")&&!strcmp(out->rows[0].parent,origin));
    SNAP_CHECK(!strcmp(out->rows[0].state,"persistent"));
    SNAP_CHECK(!worker_options_test(43,clone,"OZCLONE","readonly=on",OZ_OK,0));mounted=1;
    file=Open((CONST_STRPTR)"OZCLONE:written.txt",MODE_OLDFILE);SNAP_CHECK(file);
    char bytes[256];SNAP_CHECK(length<sizeof(bytes));
    LONG count=Read(file,bytes,sizeof(bytes));
    SNAP_CHECK(count==(LONG)length&&!memcmp(bytes,payload,length));
    SNAP_CHECK(Close(file));file=0;
    SNAP_CHECK(!worker_test(44,clone,OZ_OK,0));mounted=0;
done:
    if(file)Close(file);
    if(mounted)worker_test(44,clone,OZ_OK,0);
    free(out);
    char line[110];snprintf(line,sizeof(line),"Native authenticated snapshot controls write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef SNAP_CHECK
}
static int rollback_tests(int write,const char *payload,size_t length)
{
    const char *dataset="qemu_mirror/delegated-root/cloned";
    const char *snapshot="qemu_mirror/delegated-root/cloned@before";
    int error=0,mounted=0;BPTR file=0;
#define ROLL_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    ROLL_CHECK(!worker_test(54,"qemu_mirror/worker-created@auth",OZ_ENGINE_ERROR,EPERM));
    ROLL_CHECK(!worker_test(54,dataset,OZ_ENGINE_ERROR,EINVAL));
    if(write) {
        ROLL_CHECK(!worker_test(50,snapshot,OZ_OK,0));
        ROLL_CHECK(!worker_options_test(43,dataset,"OZROLLWRITE","readonly=off",OZ_OK,0));mounted=1;
        ROLL_CHECK(!worker_test(54,snapshot,OZ_ENGINE_ERROR,EBUSY));
        const char changed[]="Changed after snapshot; rollback must discard these bytes";
        file=Open((CONST_STRPTR)"OZROLLWRITE:written.txt",MODE_NEWFILE);ROLL_CHECK(file);
        ROLL_CHECK(Write(file,(APTR)changed,sizeof(changed))==sizeof(changed));
        ROLL_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZROLLWRITE:written.txt",MODE_OLDFILE);ROLL_CHECK(file);
        char current[sizeof(changed)+1];
        ROLL_CHECK(Read(file,current,sizeof(current))==sizeof(changed)&&!memcmp(current,changed,sizeof(changed)));
        ROLL_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZROLLWRITE:post-snapshot",MODE_NEWFILE);ROLL_CHECK(file);
        ROLL_CHECK(Close(file));file=0;
        ROLL_CHECK(!worker_test(44,dataset,OZ_OK,0));mounted=0;
        ROLL_CHECK(!worker_test(50,"qemu_mirror/delegated-root/cloned@later",OZ_OK,0));
        ROLL_CHECK(!worker_test(54,snapshot,OZ_ENGINE_ERROR,EEXIST));
        ROLL_CHECK(!worker_test(38,"qemu_mirror/delegated-root/cloned@later",OZ_OK,0));
        ROLL_CHECK(!worker_test(54,"qemu_mirror/delegated-root/cloned@missing",OZ_ENGINE_ERROR,ESRCH));
        ROLL_CHECK(!worker_test(54,snapshot,OZ_OK,0));
    } else ROLL_CHECK(!worker_test(54,snapshot,OZ_ENGINE_ERROR,EROFS));
    ROLL_CHECK(!worker_options_test(43,dataset,"OZROLLREAD","readonly=on",OZ_OK,0));mounted=1;
    file=Open((CONST_STRPTR)"OZROLLREAD:written.txt",MODE_OLDFILE);ROLL_CHECK(file);
    char bytes[256];ROLL_CHECK(length<sizeof(bytes));
    LONG count=Read(file,bytes,sizeof(bytes));
    ROLL_CHECK(count==(LONG)length&&!memcmp(bytes,payload,length));
    ROLL_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZROLLREAD:post-snapshot",MODE_OLDFILE);
    ROLL_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    ROLL_CHECK(!worker_test(44,dataset,OZ_OK,0));mounted=0;
done:
    if(file)Close(file);
    if(mounted)worker_test(44,dataset,OZ_OK,0);
    char line[110];snprintf(line,sizeof(line),"Native authenticated rollback write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef ROLL_CHECK
}
static int promotion_tests(int write,const char *payload,size_t length)
{
    const char *source="qemu_mirror/delegated-root/promote-source";
    const char *clone="qemu_mirror/delegated-root/promoted";
    const char *snapshot="qemu_mirror/delegated-root/promote-source@base";
    const char *promoted="qemu_mirror/delegated-root/promoted@base";
    int error=0,mounted=0;BPTR file=0;
    struct oz_message *out=calloc(1,sizeof(*out));
#define PROMOTE_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    PROMOTE_CHECK(out);
    PROMOTE_CHECK(!worker_test(53,"qemu_mirror/worker-created",OZ_ENGINE_ERROR,EPERM));
    /* Clone-side grants must not confer promote on its outside origin. */
    PROMOTE_CHECK(!worker_test(53,"qemu_mirror/delegated-root/denied-promote",OZ_ENGINE_ERROR,EPERM));
    PROMOTE_CHECK(!worker_test(53,"qemu_mirror/delegated-root/moved",OZ_ENGINE_ERROR,EINVAL));
    PROMOTE_CHECK(!worker_test(53,"qemu_mirror/delegated-root/moved@keep",OZ_ENGINE_ERROR,EINVAL));
    if(write) {
        PROMOTE_CHECK(!worker_argument_test(52,"qemu_mirror/delegated-root/moved@clone-content",source,OZ_OK,0));
        PROMOTE_CHECK(!worker_test(50,snapshot,OZ_OK,0));
        PROMOTE_CHECK(!worker_argument_test(52,snapshot,clone,OZ_OK,0));
        PROMOTE_CHECK(!worker_options_test(43,source,"OZPROMOTEORIGIN","readonly=on",OZ_OK,0));
        int busy=worker_test(53,clone,OZ_ENGINE_ERROR,EBUSY);
        int detached=worker_test(44,source,OZ_OK,0);
        PROMOTE_CHECK(!busy&&!detached);
        PROMOTE_CHECK(!worker_test(50,promoted,OZ_OK,0));
        PROMOTE_CHECK(!worker_test(53,clone,OZ_ENGINE_ERROR,EEXIST));
        PROMOTE_CHECK(!worker_test(38,promoted,OZ_OK,0));
        PROMOTE_CHECK(!worker_options_test(43,clone,"OZPROMOTEBUSY","readonly=on",OZ_OK,0));mounted=1;
        PROMOTE_CHECK(!worker_test(53,clone,OZ_ENGINE_ERROR,EBUSY));
        PROMOTE_CHECK(!worker_test(44,clone,OZ_OK,0));mounted=0;
        PROMOTE_CHECK(!worker_test(53,clone,OZ_OK,0));
    } else PROMOTE_CHECK(!worker_test(53,source,OZ_ENGINE_ERROR,EROFS));
    PROMOTE_CHECK(!legacy(36,promoted,out)&&out->row_count==1);
    PROMOTE_CHECK(legacy(36,snapshot,out)==EIO);
    PROMOTE_CHECK(property_matches(source,"origin",promoted,"computed"));
    PROMOTE_CHECK(property_matches(clone,"origin","qemu_mirror/delegated-root/moved@clone-content","computed"));
    PROMOTE_CHECK(!worker_options_test(43,clone,"OZPROMOTEREAD","readonly=on",OZ_OK,0));mounted=1;
    file=Open((CONST_STRPTR)"OZPROMOTEREAD:written.txt",MODE_OLDFILE);PROMOTE_CHECK(file);
    char bytes[256];PROMOTE_CHECK(length<sizeof(bytes));
    LONG count=Read(file,bytes,sizeof(bytes));
    PROMOTE_CHECK(count==(LONG)length&&!memcmp(bytes,payload,length));
    PROMOTE_CHECK(Close(file));file=0;
    PROMOTE_CHECK(!worker_test(44,clone,OZ_OK,0));mounted=0;
done:
    if(file)Close(file);
    if(mounted)worker_test(44,clone,OZ_OK,0);
    free(out);
    char line[110];snprintf(line,sizeof(line),"Native authenticated promotion write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef PROMOTE_CHECK
}
static int bookmark_tests(int write)
{
    const char *dataset="qemu_mirror/delegated-root/bookmarks";
    const char *snapshot="qemu_mirror/delegated-root/bookmarks@source";
    const char *first="qemu_mirror/delegated-root/bookmarks#b00";
    const char *copy="qemu_mirror/delegated-root/bookmarks#copy";
    struct oz_message *out=calloc(1,sizeof(*out));int error=0;
#define BOOK_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    BOOK_CHECK(out);
    BOOK_CHECK(!worker_argument_test(51,"qemu_mirror/worker-created@auth","qemu_mirror/worker-created#denied",OZ_ENGINE_ERROR,EPERM));
    BOOK_CHECK(!worker_test(38,"qemu_mirror/worker-created#denied",OZ_ENGINE_ERROR,EPERM));
    BOOK_CHECK(!worker_argument_test(51,"qemu_mirror/hold-only@test","qemu_mirror/hold-only#allowed",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    BOOK_CHECK(!worker_test(38,"qemu_mirror/hold-only#allowed",OZ_ENGINE_ERROR,EPERM));
    if(write) {
        BOOK_CHECK(!worker_test(37,dataset,OZ_OK,0));
        BOOK_CHECK(!worker_test(50,snapshot,OZ_OK,0));
        BOOK_CHECK(!worker_argument_test(51,dataset,first,OZ_ENGINE_ERROR,EINVAL));
        BOOK_CHECK(!worker_argument_test(51,snapshot,dataset,OZ_ENGINE_ERROR,EINVAL));
        for(unsigned i=0;i<27;i++) {
            char name[160];snprintf(name,sizeof(name),"%s#b%02u",dataset,i);
            BOOK_CHECK(!worker_argument_test(51,snapshot,name,OZ_OK,0));
        }
        BOOK_CHECK(!worker_argument_test(51,snapshot,first,OZ_ENGINE_ERROR,EEXIST));
        BOOK_CHECK(!worker_test(38,snapshot,OZ_OK,0));
        BOOK_CHECK(!worker_argument_test(51,first,copy,OZ_OK,0));
        BOOK_CHECK(!worker_test(38,"qemu_mirror/delegated-root/bookmarks#b26",OZ_OK,0));
        BOOK_CHECK(!worker_test(38,"qemu_mirror/delegated-root/bookmarks#b26",OZ_OK,0));
    } else {
        BOOK_CHECK(!worker_argument_test(51,first,"qemu_mirror/delegated-root/bookmarks#readonly",OZ_ENGINE_ERROR,EROFS));
        BOOK_CHECK(!worker_test(38,first,OZ_ENGINE_ERROR,EROFS));
    }
    BOOK_CHECK(legacy(36,snapshot,out)==EIO); /* snapshot gone; bookmarks remain */
    unsigned total=0;uint64_t cursor=0;char reference[256]={0};
    unsigned char seen[27]={0};
    do {
        BOOK_CHECK(!legacy_page(36,dataset,NULL,"bookmarks=on",cursor,out));
        BOOK_CHECK(out->row_count==(cursor?3:OZ_MAX_ROWS));
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *row=&out->rows[i];
            BOOK_CHECK(!strcmp(row->kind,"bookmark")&&!strcmp(row->state,"persistent")&&!strcmp(row->parent,dataset));
            const char *hash=strchr(row->name,'#');BOOK_CHECK(hash);
            unsigned index=26;
            if(strcmp(hash,"#copy")) {
                BOOK_CHECK(strlen(hash)==4&&hash[1]=='b'&&hash[2]>='0'&&hash[2]<='2'&&hash[3]>='0'&&hash[3]<='9');
                index=(hash[2]-'0')*10+hash[3]-'0';BOOK_CHECK(index<26);
            }
            BOOK_CHECK(!seen[index]);seen[index]=1;
            unsigned long long guid=0,txg=0,creation=0;
            BOOK_CHECK(sscanf(row->value,"guid=%llu createtxg=%llu creation=%llu",&guid,&txg,&creation)==3&&guid&&txg&&creation);
            if(!total)snprintf(reference,sizeof(reference),"%s",row->value);
            BOOK_CHECK(!strcmp(reference,row->value));total++;
        }
        BOOK_CHECK(!out->next_cursor||out->next_cursor>cursor);
        cursor=out->next_cursor;
    }while(cursor);
    BOOK_CHECK(total==27);
    BOOK_CHECK(!legacy_page(36,dataset,NULL,"bookmarks=on",1000,out)&&!out->row_count&&!out->next_cursor);
done:
    free(out);
    char line[110];snprintf(line,sizeof(line),"Native authenticated bookmarks write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef BOOK_CHECK
}
static int space_property_tests(int write)
{
    const char *dataset="qemu_mirror/delegated-root/moved";
    const char *names[]={"quota","refquota","reservation","refreservation"};
    const char *values[]={"16777216","8388608","1048576","524288"};
    const char *sizes[]={"16M","8MiB","1mb",".5M"};
    int error=0;
#define SPACE_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    SPACE_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only","org.aros:quota-policy=userprop-only",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    SPACE_CHECK(property_matches("qemu_mirror/hold-only","org.aros:quota-policy","userprop-only","qemu_mirror/hold-only"));
    for(unsigned i=0;i<4;i++) {
        char argument[96];snprintf(argument,sizeof(argument),"%s=%s",names[i],values[i]);
        SPACE_CHECK(!worker_argument_test(41,"qemu_mirror/worker-created",argument,OZ_ENGINE_ERROR,EPERM));
        SPACE_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only",argument,OZ_ENGINE_ERROR,EPERM));
        SPACE_CHECK(!worker_argument_test(41,"qemu_mirror/delegated-root/moved@keep",argument,OZ_ENGINE_ERROR,EINVAL));
        SPACE_CHECK(!worker_argument_test(42,dataset,names[i],OZ_ENGINE_ERROR,EINVAL));
        if(write) {
            snprintf(argument,sizeof(argument),"%s=none",names[i]);
            SPACE_CHECK(!worker_argument_test(41,dataset,argument,OZ_OK,0));
            SPACE_CHECK(property_matches(dataset,names[i],"0",dataset));
            snprintf(argument,sizeof(argument),"%s=%s",names[i],values[i]);
            SPACE_CHECK(!worker_argument_test(41,dataset,argument,OZ_OK,0));
        } else SPACE_CHECK(!worker_argument_test(41,dataset,argument,OZ_ENGINE_ERROR,EROFS));
        SPACE_CHECK(property_matches(dataset,names[i],values[i],dataset));
        snprintf(argument,sizeof(argument),"%s=%s",names[i],sizes[i]);
        SPACE_CHECK(!worker_argument_test(41,dataset,argument,write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        SPACE_CHECK(property_matches(dataset,names[i],values[i],dataset));
    }
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=18446744073709551616",OZ_ENGINE_ERROR,ERANGE));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=-1",OZ_ENGINE_ERROR,EINVAL));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=1garbage",OZ_ENGINE_ERROR,EINVAL));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=",OZ_ENGINE_ERROR,EINVAL));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=16EB",OZ_ENGINE_ERROR,ERANGE));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=16.0EiB",OZ_ENGINE_ERROR,ERANGE));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=1Z",OZ_ENGINE_ERROR,ERANGE));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=16MiBB",OZ_ENGINE_ERROR,EINVAL));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=16MM",OZ_ENGINE_ERROR,EINVAL));
    SPACE_CHECK(!worker_argument_test(41,dataset,"quota=1BiB",OZ_ENGINE_ERROR,EINVAL));
    const char *zero_sizes[]={"0","0B","0MiB",".0","0.5",".0001K"};
    for(unsigned p=0;p<2;p++)for(unsigned i=0;i<sizeof(zero_sizes)/sizeof(*zero_sizes);i++) {
        char argument[96];snprintf(argument,sizeof(argument),"%s=%s",names[p],zero_sizes[i]);
        SPACE_CHECK(!worker_argument_test(41,dataset,argument,OZ_ENGINE_ERROR,EINVAL));
        SPACE_CHECK(property_matches(dataset,names[p],values[p],dataset));
    }
    /* Unlike quotas, upstream permits numeric zero for reservations. */
    for(unsigned p=2;p<4;p++) {
        char argument[96];snprintf(argument,sizeof(argument),"%s=0",names[p]);
        SPACE_CHECK(!worker_argument_test(41,dataset,argument,write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        if(write) {
            SPACE_CHECK(property_matches(dataset,names[p],"0",dataset));
            snprintf(argument,sizeof(argument),"%s=%s",names[p],sizes[p]);
            SPACE_CHECK(!worker_argument_test(41,dataset,argument,OZ_OK,0));
        }
        SPACE_CHECK(property_matches(dataset,names[p],values[p],dataset));
    }
    if(write) {
        SPACE_CHECK(!worker_argument_test(41,dataset,"quota=1",OZ_ENGINE_ERROR,ENOSPC));
        SPACE_CHECK(!worker_argument_test(41,dataset,"refquota=1",OZ_ENGINE_ERROR,ENOSPC));
        SPACE_CHECK(!worker_argument_test(41,dataset,"reservation=33554432",OZ_ENGINE_ERROR,ENOSPC));
        SPACE_CHECK(!worker_argument_test(41,dataset,"refreservation=16777216",OZ_ENGINE_ERROR,ENOSPC));
    }
    for(unsigned i=0;i<4;i++)SPACE_CHECK(property_matches(dataset,names[i],values[i],dataset));
done:;
    char line[110];snprintf(line,sizeof(line),"Native authenticated space properties write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef SPACE_CHECK
}
static int count_limit_tests(int write,int administrator)
{
    const char *dataset="qemu_mirror/count-limited";
    int error=0;
#define LIMIT_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    const char *names[]={"filesystem_limit","snapshot_limit"};
    for(unsigned i=0;i<2;i++) {
        char argument[80];snprintf(argument,sizeof(argument),"%s=none",names[i]);
        LIMIT_CHECK(!worker_argument_test(41,dataset,argument,
            administrator&&write?OZ_OK:OZ_ENGINE_ERROR,administrator?(write?0:EROFS):EPERM));
        LIMIT_CHECK(!worker_argument_test(42,dataset,names[i],OZ_ENGINE_ERROR,EINVAL));
        if(administrator) {
            snprintf(argument,sizeof(argument),"%s=-1",names[i]);
            LIMIT_CHECK(!worker_argument_test(41,dataset,argument,OZ_ENGINE_ERROR,EINVAL));
            snprintf(argument,sizeof(argument),"%s=18446744073709551616",names[i]);
            LIMIT_CHECK(!worker_argument_test(41,dataset,argument,OZ_ENGINE_ERROR,ERANGE));
            snprintf(argument,sizeof(argument),"%s=0",names[i]);
            LIMIT_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only@test",argument,OZ_ENGINE_ERROR,EINVAL));
        }
        if(administrator&&write) {
            LIMIT_CHECK(property_matches(dataset,names[i],"18446744073709551615",dataset));
            snprintf(argument,sizeof(argument),"%s=0",names[i]);
            LIMIT_CHECK(!worker_argument_test(41,dataset,argument,OZ_OK,0));
        }
    }
    if(administrator&&write) {
        /* Root may exceed zero limits, but the non-root creator below may not. */
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/root-bypass",OZ_OK,0));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited@root-bypass",OZ_OK,0));
        LIMIT_CHECK(!worker_test(38,"qemu_mirror/count-limited/root-bypass",OZ_OK,0));
        LIMIT_CHECK(!worker_test(38,"qemu_mirror/count-limited@root-bypass",OZ_OK,0));
        LIMIT_CHECK(!worker_argument_test(41,dataset,"filesystem_limit=2",OZ_OK,0));
        LIMIT_CHECK(!worker_argument_test(41,dataset,"snapshot_limit=2",OZ_OK,0));
    }
    if(!administrator&&write) {
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/one",OZ_OK,0));
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/one/nested",OZ_OK,0));
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/three",OZ_ENGINE_ERROR,EDQUOT));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited@a",OZ_OK,0));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited/one@b",OZ_OK,0));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited/one/nested@refused",OZ_ENGINE_ERROR,EDQUOT));
        LIMIT_CHECK(!worker_test(38,"qemu_mirror/count-limited/one/nested",OZ_OK,0));
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/two",OZ_OK,0));
        LIMIT_CHECK(!worker_test(38,"qemu_mirror/count-limited@a",OZ_OK,0));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited/two@c",OZ_OK,0));
    }
    if(!administrator&&!write) {
        LIMIT_CHECK(!worker_test(37,"qemu_mirror/count-limited/readonly",OZ_ENGINE_ERROR,EROFS));
        LIMIT_CHECK(!worker_test(50,"qemu_mirror/count-limited@readonly",OZ_ENGINE_ERROR,EROFS));
    }
    for(unsigned i=0;i<2;i++)LIMIT_CHECK(property_matches(dataset,names[i],"2",dataset));
    if(!administrator||!write) {
        LIMIT_CHECK(property_matches(dataset,"filesystem_count","2","computed"));
        LIMIT_CHECK(property_matches(dataset,"snapshot_count","2","computed"));
    }
done:;
    char line[128];snprintf(line,sizeof(line),"Native count limits write=%d admin=%d result=%d\n",write,administrator,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef LIMIT_CHECK
}
#include "recordsize-test.h"
#include "readonly-test.h"
#include "timestamp-test.h"
static int compression_tests(int write)
{
    const char *parent="qemu_mirror/delegated-root",*dataset="qemu_mirror/delegated-root/moved";
    int error=0;BPTR file=0;unsigned char *block=malloc(65536),*readback=malloc(65536);
    const char *algos[]={"lz4","gzip-6","zstd-3"};
    const char *canonical[]={"lz4","gzip","zstd-3"};
#define COMP_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    COMP_CHECK(block&&readback);
    COMP_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only","compression=lz4",OZ_ENGINE_ERROR,EPERM));
    COMP_CHECK(!worker_argument_test(42,"qemu_mirror/hold-only","compression",OZ_ENGINE_ERROR,EPERM));
    COMP_CHECK(!worker_argument_test(41,"qemu_mirror/delegated-root/moved@keep","compression=lz4",OZ_ENGINE_ERROR,EINVAL));
    const char *invalid[]={"compression=123","compression=inherit","compression=gzip-0","compression=zstd-20","compression=bogus"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        COMP_CHECK(!worker_argument_test(41,dataset,invalid[i],OZ_ENGINE_ERROR,EINVAL));
    if(write) {
        COMP_CHECK(!worker_argument_test(41,parent,"compression=lz4",OZ_OK,0));
        COMP_CHECK(!worker_argument_test(41,dataset,"compression=off",OZ_OK,0));
        COMP_CHECK(property_matches(dataset,"compression","off",dataset));
        COMP_CHECK(!worker_argument_test(42,dataset,"compression",OZ_OK,0));
        COMP_CHECK(property_matches(dataset,"compression","lz4",parent));
    }
    for(unsigned a=0;a<3;a++) {
        char argument[80],path[100];snprintf(argument,sizeof(argument),"compression=%s",algos[a]);
        snprintf(path,sizeof(path),"OZAUTHFILES:compressed-%s",algos[a]);
        /* Repeating but nonzero input: avoid sparse/embedded all-zero blocks. */
        for(unsigned i=0;i<65536;i++)block[i]=(unsigned char)(1+(i%251));
        if(write) {
            COMP_CHECK(!worker_argument_test(41,dataset,argument,OZ_OK,0));
            COMP_CHECK(property_matches(dataset,"compression",canonical[a],dataset));
            file=Open((CONST_STRPTR)path,MODE_NEWFILE);COMP_CHECK(file);
            for(unsigned i=0;i<8;i++)COMP_CHECK(Write(file,block,65536)==65536);
            COMP_CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
            LONG closed=Close(file);file=0;COMP_CHECK(closed);
        } else COMP_CHECK(!worker_argument_test(41,dataset,argument,OZ_ENGINE_ERROR,EROFS));
        file=Open((CONST_STRPTR)path,MODE_OLDFILE);COMP_CHECK(file);
        for(unsigned i=0;i<8;i++) {
            COMP_CHECK(Read(file,readback,65536)==65536);
            COMP_CHECK(!memcmp(block,readback,65536));
        }
        COMP_CHECK(Read(file,readback,1)==0);
        LONG closed=Close(file);file=0;COMP_CHECK(closed);
    }
    COMP_CHECK(property_matches(dataset,"compression","zstd-3",dataset));
    if(!write)COMP_CHECK(!worker_argument_test(42,dataset,"compression",OZ_ENGINE_ERROR,EROFS));
done:
    if(file)Close(file);
    free(block);free(readback);
    char line[120];snprintf(line,sizeof(line),"Native authenticated compression write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef COMP_CHECK
}
static int quota_io_test(int write)
{
    const char *dataset="qemu_mirror/delegated-root/moved";
    int error=0,full=0;BPTR file=0;unsigned char *block=NULL;
#define QUOTA_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    if(!write) {
        file=Open((CONST_STRPTR)"OZAUTHFILES:quota-test",MODE_OLDFILE);
        QUOTA_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        goto done;
    }
    block=malloc(65536);QUOTA_CHECK(block);
    for(unsigned pass=0;pass<2;pass++) {
    QUOTA_CHECK(!worker_argument_test(41,dataset,pass?"quota=2097152":"refquota=1048576",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZAUTHFILES:quota-test",MODE_NEWFILE);QUOTA_CHECK(file);
    struct FileHandle *fh=BADDR(file);struct MsgPort *port=fh->fh_Type;
    uint32_t random=0x53465154;
    int stopped=0;
    for(unsigned i=0;i<64&&!stopped;i++) {
        for(unsigned j=0;j<65536;j++){random^=random<<13;random^=random>>17;random^=random<<5;block[j]=random;}
        LONG count=Write(file,block,65536);LONG ioerror=IoErr();
        if(count<65536){QUOTA_CHECK(ioerror==ERROR_DISK_FULL);stopped=1;}
        LONG flushed=DoPkt(port,ACTION_FLUSH,0,0,0,0,0),flush_error=IoErr();
        if(flushed!=DOSTRUE) {
            char detail[150];snprintf(detail,sizeof(detail),
                "Quota flush limit pass=%u iteration=%u written=%ld write-error=%ld flush-error=%ld\n",
                pass,i,(long)count,(long)ioerror,(long)flush_error);
            for(char *p=detail;*p;p++)RawPutChar(*p);
            /* Dirty access-time metadata is also quota-accounted. An
             * accepted write may hit the limit at the durability boundary;
             * its error must propagate, then recover after raising quota. */
            QUOTA_CHECK(flush_error==ERROR_DISK_FULL);stopped=1;
        }
    }
    QUOTA_CHECK(stopped);full|=1<<pass;
    /* Same open file must resume once the upstream limit is raised. */
    QUOTA_CHECK(!worker_argument_test(41,dataset,pass?"quota=16777216":"refquota=8388608",OZ_OK,0));
    QUOTA_CHECK(Write(file,block,65536)==65536);
    QUOTA_CHECK(DoPkt(port,ACTION_FLUSH,0,0,0,0,0)==DOSTRUE);
    QUOTA_CHECK(Close(file));file=0;
    QUOTA_CHECK(DeleteFile((CONST_STRPTR)"OZAUTHFILES:quota-test"));
    QUOTA_CHECK(DoPkt(port,ACTION_FLUSH,0,0,0,0,0)==DOSTRUE);
    }
done:
    if(file)Close(file);
    free(block);
    char line[110];snprintf(line,sizeof(line),"Native DOS quota enforcement write=%d full=%d result=%d\n",write,full,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef QUOTA_CHECK
}
static int many_files_test(const char *expected,size_t length)
{
    BPTR files[96]={0};int error=0;
    for(unsigned round=0;round<2&&!error;round++) {
        for(unsigned i=0;i<96;i++) {
            files[i]=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
            if(!files[i]){error=EIO;break;}
        }
        if(!error)error=worker_test(44,"qemu_mirror/delegated-root/moved",OZ_ENGINE_ERROR,EBUSY);
        for(unsigned i=0;i<96&&!error;i++) {
            char bytes[128];
            LONG count=Read(files[i],bytes,sizeof(bytes));
            if(count!=(LONG)length||memcmp(bytes,expected,length))error=EIO;
        }
        for(unsigned i=0;i<96;i++)if(files[i]) {
            if(!Close(files[i]))error=EIO;
            files[i]=0;
        }
    }
    return error;
}
static int many_locks_test(const char *expected,size_t length)
{
    BPTR locks[96]={0};int error=0;
    for(unsigned round=0;round<2&&!error;round++) {
        for(unsigned i=0;i<96;i++) {
            locks[i]=(i&1)?DupLock(locks[i-1]):Lock((CONST_STRPTR)"OZAUTHFILES:written.txt",ACCESS_READ);
            if(!locks[i]){error=EIO;break;}
        }
        if(!error)error=worker_test(44,"qemu_mirror/delegated-root/moved",OZ_ENGINE_ERROR,EBUSY);
        for(unsigned i=0;i<96&&!error;i++) {
            struct FileInfoBlock fib;
            if(!Examine(locks[i],&fib)||fib.fib_Size!=(LONG)length){error=EIO;break;}
            BPTR file=OpenFromLock(locks[i]);
            if(!file){error=EIO;break;}
            locks[i]=0; /* successful conversion consumes this lock only */
            char bytes[128];LONG count=Read(file,bytes,sizeof(bytes));
            LONG closed=Close(file);
            if(count!=(LONG)length||!closed||memcmp(bytes,expected,length))error=EIO;
        }
        for(unsigned i=0;i<96;i++)if(locks[i]){UnLock(locks[i]);locks[i]=0;}
    }
    return error;
}
struct record_client {struct MsgPort *port;struct Message ready,done;BPTR file;LONG result,error;ULONG ticks;};
static void *record_waiter(void *arg)
{
    struct record_client *c=arg;
    PutMsg(c->port,&c->ready);
    c->result=LockRecord(c->file,0,16,REC_EXCLUSIVE,c->ticks);c->error=IoErr();
    PutMsg(c->port,&c->done);return NULL;
}
static int record_multi_waiters(int timeout_first)
{
    BPTR owner=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
    struct record_client clients[2]={{0}};
    pthread_t threads[2];unsigned started=0;int error=0;
    if(!owner||!LockRecord(owner,0,16,REC_EXCLUSIVE_IMMED,0)){error=__LINE__;goto done;}
    for(unsigned i=0;i<2;i++) {
        struct record_client *c=&clients[i];
        c->file=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
        c->port=CreateMsgPort();c->ticks=timeout_first&&i==0?30:500;
        c->ready.mn_Length=sizeof(c->ready);c->done.mn_Length=sizeof(c->done);
        if(!c->file||!c->port){error=__LINE__;goto done;}
        if(pthread_create(&threads[i],NULL,record_waiter,c)){error=__LINE__;goto done;}
        started++;
        WaitPort(c->port);
        if(GetMsg(c->port)!=&c->ready){error=__LINE__;goto done;}
        /* Ready precedes submission; allow the native worker to enter DoPkt.
         * This is a scheduling allowance, not a queue-admission handshake. */
        Delay(3);
    }
    if(!timeout_first&&!UnLockRecord(owner,0,16)){error=__LINE__;goto done;}
    WaitPort(clients[0].port);
    if(GetMsg(clients[0].port)!=&clients[0].done){error=__LINE__;goto done;}
    if(timeout_first) {
        if(clients[0].result||clients[0].error!=ERROR_LOCK_TIMEOUT){error=__LINE__;goto done;}
        if(!UnLockRecord(owner,0,16)){error=__LINE__;goto done;}
    } else if(!clients[0].result||!UnLockRecord(clients[0].file,0,16)) {
        error=__LINE__;goto done;
    }
    WaitPort(clients[1].port);
    if(GetMsg(clients[1].port)!=&clients[1].done||!clients[1].result||
        !UnLockRecord(clients[1].file,0,16))error=__LINE__;
done:
    if(owner)Close(owner);
    /* Join every started caller before releasing its stack or reply port.
     * On a failed assertion outstanding callers have a bounded lock timeout. */
    for(unsigned i=0;i<started;i++)pthread_join(threads[i],NULL);
    for(unsigned i=0;i<2;i++) {
        if(clients[i].file)Close(clients[i].file);
        if(clients[i].port){while(GetMsg(clients[i].port)){}DeleteMsgPort(clients[i].port);}
    }
    char line[100];snprintf(line,sizeof(line),"Native DOS multi-waiter timeout=%d result=%d\n",timeout_first,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static int record_close_waiting(void)
{
    BPTR owner=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
    BPTR waiting=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
    struct MsgPort *reply=CreateMsgPort();
    struct StandardPacket packet={0};int error=0;
    if(!owner||!waiting||!reply||!LockRecord(owner,0,16,REC_EXCLUSIVE_IMMED,0)) {
        error=__LINE__;goto done;
    }
    struct FileHandle *fh=BADDR(waiting);
    struct MsgPort *handler=fh->fh_Type;
    packet.sp_Msg.mn_Node.ln_Name=(char *)&packet.sp_Pkt;
    packet.sp_Msg.mn_Length=sizeof(packet);
    packet.sp_Pkt.dp_Link=&packet.sp_Msg;
    packet.sp_Pkt.dp_Type=ACTION_LOCK_RECORD;
    packet.sp_Pkt.dp_Arg1=fh->fh_Arg1;
    packet.sp_Pkt.dp_Arg3=16;
    packet.sp_Pkt.dp_Arg4=REC_EXCLUSIVE;
    packet.sp_Pkt.dp_Arg5=500;
    SendPkt(&packet.sp_Pkt,handler,reply);
    /* Same handler FIFO barrier: the blocked packet has been dispatched
     * before Close. No worker can still be dereferencing the FileHandle. */
    LONG barrier=DoPkt(handler,ACTION_IS_FILESYSTEM,0,0,0,0,0);
    LONG closed=Close(waiting);waiting=0;
    WaitPort(reply);struct Message *message=GetMsg(reply);
    if(!barrier||!closed||message!=&packet.sp_Msg||packet.sp_Pkt.dp_Res1||
        packet.sp_Pkt.dp_Res2!=ERROR_INVALID_LOCK)error=__LINE__;
done:
    if(owner)Close(owner);
    if(waiting)Close(waiting);
    if(reply)DeleteMsgPort(reply);
    char line[100];snprintf(line,sizeof(line),"Native DOS close waiting record result=%d\n",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
static struct Message *notify_message(struct MsgPort *port)
{
    for(unsigned i=0;i<200;i++) {
        struct Message *m=GetMsg(port);if(m)return m;
        Delay(1);
    }
    return NULL;
}
static int notification_tests(int write,struct Library *secBase)
{
    struct MsgPort *port=CreateMsgPort();
    struct NotifyRequest *nr=calloc(1,sizeof(*nr));
    struct NotifyRequest signal_request={0},denied={0},burst={0};
    struct Message *held=NULL;int active=0,signal_active=0,burst_active=0,error=0;
    LONG bit=AllocSignal(-1);BPTR file=0;
    struct DevProc *device=GetDeviceProc((CONST_STRPTR)"OZAUTHFILES:",NULL);
#define NOTIFY_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    NOTIFY_CHECK(port&&nr&&bit>=0&&device);
    nr->nr_Name=(STRPTR)"OZAUTHFILES:written.txt";
    nr->nr_Flags=NRF_SEND_MESSAGE|NRF_NOTIFY_INITIAL|NRF_WAIT_REPLY;
    nr->nr_stuff.nr_Msg.nr_Port=port;
    NOTIFY_CHECK(StartNotify(nr));active=1;
    held=notify_message(port);NOTIFY_CHECK(held);
    struct NotifyMessage *nm=(void *)held;
    NOTIFY_CHECK(nm->nm_NReq==nr&&nm->nm_Class==NOTIFY_CLASS&&nm->nm_Code==NOTIFY_CODE);
    EndNotify(nr);active=0;
    /* The client owns this removed message, so EndNotify cannot reply it.
     * Free the request first to exercise the handler's independent lifetime. */
    free(nr);nr=NULL;
    ReplyMsg(held);held=NULL;
    NOTIFY_CHECK(DoPkt(device->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0));
    signal_request.nr_Name=(STRPTR)"OZAUTHFILES:";
    signal_request.nr_Flags=NRF_SEND_SIGNAL|NRF_NOTIFY_INITIAL;
    signal_request.nr_stuff.nr_Signal.nr_Task=FindTask(NULL);
    signal_request.nr_stuff.nr_Signal.nr_SignalNum=bit;
    SetSignal(0,1UL<<bit);
    NOTIFY_CHECK(StartNotify(&signal_request));signal_active=1;
    NOTIFY_CHECK(SetSignal(0,1UL<<bit)&(1UL<<bit));
    nr=calloc(1,sizeof(*nr));NOTIFY_CHECK(nr);
    nr->nr_Name=(STRPTR)(write?"OZAUTHFILES:notify-file":"OZAUTHFILES:written.txt");
    nr->nr_Flags=NRF_SEND_MESSAGE|NRF_WAIT_REPLY|NRF_NOTIFY_INITIAL;
    nr->nr_stuff.nr_Msg.nr_Port=port;
    NOTIFY_CHECK(StartNotify(nr));active=1;
    if(write) {
        NOTIFY_CHECK(!GetMsg(port)); /* missing name has no initial event */
        file=Open(nr->nr_Name,MODE_NEWFILE);NOTIFY_CHECK(file);
        held=notify_message(port);NOTIFY_CHECK(held);
        NOTIFY_CHECK(Write(file,(APTR)"abc",3)==3);
        NOTIFY_CHECK(Close(file));file=0;
        NOTIFY_CHECK(!GetMsg(port)); /* WAIT_REPLY coalesces write and close */
        NOTIFY_CHECK(SetSignal(0,1UL<<bit)&(1UL<<bit));
        ReplyMsg(held);held=NULL;
        held=notify_message(port);NOTIFY_CHECK(held);
        ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(DoPkt(device->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0));
        NOTIFY_CHECK(!GetMsg(port));
        file=Open(nr->nr_Name,MODE_READWRITE);NOTIFY_CHECK(file);
        NOTIFY_CHECK(SetFileSize(file,9,OFFSET_BEGINNING)==9);
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(Close(file));file=0;
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(SetComment(nr->nr_Name,(CONST_STRPTR)"notification metadata"));
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(Rename(nr->nr_Name,(CONST_STRPTR)"OZAUTHFILES:notify-moved"));
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(Rename((CONST_STRPTR)"OZAUTHFILES:notify-moved",nr->nr_Name));
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(SetComment(nr->nr_Name,(CONST_STRPTR)"hold this event"));
        held=notify_message(port);NOTIFY_CHECK(held);
        NOTIFY_CHECK(SetComment(nr->nr_Name,(CONST_STRPTR)"coalesced event"));
        /* Ordinary SetProtection intentionally preserves MuFS group bits. */
        NOTIFY_CHECK(secSetProtection(nr->nr_Name,FIBF_READ));
        denied.nr_Name=nr->nr_Name;denied.nr_Flags=NRF_SEND_MESSAGE|NRF_NOTIFY_INITIAL;
        denied.nr_stuff.nr_Msg.nr_Port=port;
        LONG admitted=StartNotify(&denied);
        LONG denied_error=IoErr();if(admitted)EndNotify(&denied);
        NOTIFY_CHECK(!admitted&&denied_error==ERROR_READ_PROTECTED);
        ReplyMsg(held);held=NULL;
        NOTIFY_CHECK(DoPkt(device->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0));
        NOTIFY_CHECK(!GetMsg(port)); /* deferred delivery rechecks revoked read */
        NOTIFY_CHECK(secSetProtection(nr->nr_Name,0));
        /* A restored readable object may produce a property notification. */
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        /* A second, non-WAIT_REPLY registration accumulates messages.
         * Use a separate port so EndNotify cannot consume the first watch. */
        struct MsgPort *burst_port=CreateMsgPort();NOTIFY_CHECK(burst_port);
        burst.nr_Name=nr->nr_Name;burst.nr_Flags=NRF_SEND_MESSAGE|NRF_NOTIFY_INITIAL;
        burst.nr_stuff.nr_Msg.nr_Port=burst_port;
        if(!StartNotify(&burst)){
            DeleteMsgPort(burst_port);burst.nr_stuff.nr_Msg.nr_Port=NULL;
            error=__LINE__;goto done;
        }
        burst_active=1;
        for(unsigned i=0;i<3;i++) {
            NOTIFY_CHECK(SetComment(nr->nr_Name,(CONST_STRPTR)"burst"));
            held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
        }
        NOTIFY_CHECK(burst.nr_MsgCount==4);
        EndNotify(&burst);burst_active=0;
        NOTIFY_CHECK(!GetMsg(burst_port));
        DeleteMsgPort(burst_port);burst.nr_stuff.nr_Msg.nr_Port=NULL;
        NOTIFY_CHECK(DeleteFile(nr->nr_Name));
        held=notify_message(port);NOTIFY_CHECK(held);ReplyMsg(held);held=NULL;
    }
    /* Leave the readonly initial message queued: EndNotify must drain it. */
    EndNotify(nr);active=0;free(nr);nr=NULL;
    NOTIFY_CHECK(!GetMsg(port));
    EndNotify(&signal_request);signal_active=0;
    NOTIFY_CHECK(DoPkt(device->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0));
done:
    if(file)Close(file);
    if(active)EndNotify(nr);
    if(signal_active)EndNotify(&signal_request);
    if(burst_active)EndNotify(&burst);
    if(burst.nr_stuff.nr_Msg.nr_Port)DeleteMsgPort(burst.nr_stuff.nr_Msg.nr_Port);
    if(held)ReplyMsg(held);
    if(port){struct Message *m;while((m=GetMsg(port)))ReplyMsg(m);}
    free(nr);
    if(device){DoPkt(device->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0);FreeDeviceProc(device);}
    if(port)DeleteMsgPort(port);
    if(bit>=0){SetSignal(0,1UL<<bit);FreeSignal(bit);}
    char line[100];snprintf(line,sizeof(line),"Native DOS notifications write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef NOTIFY_CHECK
}
/* Desktop volume watches must not prevent storage detach. Keep an initial
 * message in the client's hands across detach and EndNotify to exercise the
 * retired endpoint's independent request/message lifetime. */
static int detach_notification_test(void)
{
    struct MsgPort *port=CreateMsgPort();
    struct NotifyRequest *nr=calloc(1,sizeof(*nr));
    struct DevProc *dev=GetDeviceProc((CONST_STRPTR)"OZAUTHFILES:",NULL);
    struct Message *held=NULL;int active=0,error=0;
#define DETACH_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    DETACH_CHECK(port&&nr&&dev);
    nr->nr_Name=(STRPTR)"OZAUTHFILES:";
    nr->nr_Flags=NRF_SEND_MESSAGE|NRF_NOTIFY_INITIAL|NRF_WAIT_REPLY;
    nr->nr_stuff.nr_Msg.nr_Port=port;
    DETACH_CHECK(StartNotify(nr));active=1;
    held=notify_message(port);DETACH_CHECK(held);
    DETACH_CHECK(worker_test(44,"qemu_mirror/delegated-root/moved",OZ_OK,0)==0);
    DETACH_CHECK(!DoPkt(dev->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED);
    EndNotify(nr);active=0;free(nr);nr=NULL;
    ReplyMsg(held);held=NULL;
    DETACH_CHECK(!DoPkt(dev->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED);
done:
    if(active)EndNotify(nr);
    if(held)ReplyMsg(held);
    if(port){struct Message *msg;while((msg=GetMsg(port)))ReplyMsg(msg);}
    free(nr);
    if(dev)FreeDeviceProc(dev);
    if(port)DeleteMsgPort(port);
    char line[100];snprintf(line,sizeof(line),"Native DOS detached notification lifetime result=%d\n",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef DETACH_CHECK
}
static int record_tests(void)
{
    BPTR a=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
    BPTR b=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);
    int error=0;
#define REC_CHECK(expr) do {if(!(expr)){error=__LINE__;goto cleanup;}}while(0)
    REC_CHECK(a&&b);
    REC_CHECK(LockRecord(a,0,16,REC_SHARED_IMMED,0));
    REC_CHECK(LockRecord(b,0,16,REC_SHARED_IMMED,0));
    REC_CHECK(!LockRecord(b,8,8,REC_EXCLUSIVE_IMMED,0)&&IoErr()==ERROR_LOCK_COLLISION);
    REC_CHECK(LockRecord(b,16,16,REC_EXCLUSIVE_IMMED,0));
    REC_CHECK(UnLockRecord(b,16,16)&&UnLockRecord(b,0,16)&&UnLockRecord(a,0,16));
    REC_CHECK(!UnLockRecord(a,0,16)&&IoErr()==ERROR_RECORD_NOT_LOCKED);
    REC_CHECK(LockRecord(a,0,16,REC_EXCLUSIVE_IMMED,0));
    char probe;
    REC_CHECK(Read(b,&probe,1)==1); /* record locks are cooperative, not I/O barriers */
    REC_CHECK(!LockRecord(b,0,16,REC_EXCLUSIVE,3)&&IoErr()==ERROR_LOCK_TIMEOUT);
    REC_CHECK(!LockRecord(b,0,16,REC_EXCLUSIVE,0)&&IoErr()==ERROR_LOCK_TIMEOUT);
    for(unsigned close_owner=0;close_owner<2;close_owner++) {
        struct record_client c={0};c.file=b;c.ticks=500;c.port=CreateMsgPort();REC_CHECK(c.port);
        c.ready.mn_Length=sizeof(c.ready);c.done.mn_Length=sizeof(c.done);
        pthread_t thread;int rc=pthread_create(&thread,NULL,record_waiter,&c);
        if(rc){DeleteMsgPort(c.port);error=rc;goto cleanup;}
        WaitPort(c.port);struct Message *ready=GetMsg(c.port);Delay(3);
        LONG released;
        if(close_owner){released=Close(a);a=0;}else released=UnLockRecord(a,0,16);
        WaitPort(c.port);struct Message *done=GetMsg(c.port);
        rc=pthread_join(thread,NULL);DeleteMsgPort(c.port);
        REC_CHECK(ready==&c.ready&&done==&c.done&&!rc&&released&&c.result);
        REC_CHECK(UnLockRecord(b,0,16));
        if(!close_owner)REC_CHECK(LockRecord(a,0,16,REC_EXCLUSIVE_IMMED,0));
    }
    REC_CHECK(LockRecord(b,UINT32_MAX,UINT32_MAX,REC_SHARED_IMMED,0));
    REC_CHECK(UnLockRecord(b,UINT32_MAX,UINT32_MAX));
    struct RecordLock batch[]={{b,64,8,REC_SHARED_IMMED},{b,128,8,REC_EXCLUSIVE_IMMED},{0}};
    REC_CHECK(LockRecords(batch,0)&&UnLockRecords(batch));
    REC_CHECK(record_multi_waiters(0)==0);
    REC_CHECK(record_multi_waiters(1)==0);
    REC_CHECK(record_close_waiting()==0);
cleanup:
    if(a)Close(a);
    if(b)Close(b);
    char line[100];snprintf(line,sizeof(line),"Native DOS record locks result=%d\n",error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef REC_CHECK
}
/* Transport-only delayed peer: does not authenticate or execute storage work.
 * Hold the actual DOS packet while the UI owner polls and requests disposal. */
static int delayed_worker_test(void)
{
    struct MsgPort *port=CreateMsgPort();
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!port||!m||!out){if(port)DeleteMsgPort(port);free(m);free(out);return ENOMEM;}
    char name[96];snprintf(name,sizeof(name),"OpenZFS.Delayed.Worker.%p",(void *)port);
    port->mp_Node.ln_Name=name;AddPort(port);
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->operation=37;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"delayed/copied-request");
    struct oz_admission_worker *worker=NULL;
    int error=oz_worker_start(name,m,&worker);
    memset(m,0xa5,sizeof(*m));free(m);
    if(error){RemPort(port);DeleteMsgPort(port);free(out);return error;}
    WaitPort(port);struct Message *message=GetMsg(port);
    struct DosPacket *packet=(struct DosPacket *)message->mn_Node.ln_Name;
    struct oz_ticket ticket;int transport=0,release=0;
    int pass=packet->dp_Type==OZ_ADMISSION_ACTION&&
        !strcmp(((struct oz_message *)packet->dp_Arg1)->target,"delayed/copied-request");
    for(unsigned i=0;i<5;i++) {
        pass=pass&&oz_worker_poll(worker,out,&ticket,&transport,&release)==EAGAIN;
        pass=pass&&oz_worker_free(worker)==EBUSY;
        Delay(1);
    }
    ReplyPkt(packet,DOSFALSE,ENOTSUP);
    do {error=oz_worker_poll(worker,out,&ticket,&transport,&release);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(error)return error; /* retain potentially live transport on join error */
    pass=pass&&transport==ENOTSUP&&!release&&!ticket.number;
    error=oz_worker_free(worker);
    RemPort(port);DeleteMsgPort(port);free(out);
    return error?error:pass?0:EIO;
}
static int device_control_test(int write)
{
    struct oz_message *out=calloc(1,sizeof(*out));int error=0;
    char leaf[2][32]={{0}},parent[32]={0};unsigned count=0;
#define DEV_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    DEV_CHECK(out&&!legacy(4,"qemu_mirror",out));
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];const char *p=strstr(r->value,"guid=");
        DEV_CHECK(p);char *end;unsigned long long guid=strtoull(p+5,&end,10);
        DEV_CHECK(guid&&*end==' ');
        if(!strcmp(r->kind,"disk")||!strcmp(r->kind,"file")) {
            DEV_CHECK(count<2);snprintf(leaf[count++],32,"%llu",guid);
        } else if(!strcmp(r->kind,"mirror"))snprintf(parent,32,"%llu",guid);
    }
    DEV_CHECK(count==2&&parent[0]);
    /* Reject all unimplemented modifiers before a job can mutate the pool. */
    for(unsigned op=15;op<=16;op++) {
        struct oz_ticket rejected={0};
        memset(out,0,sizeof(*out));out->magic=OZ_MAGIC;out->version=OZ_VERSION;
        out->msg.mn_Length=sizeof(*out);out->operation=op;out->flags=OZ_CONFIRMED;
        snprintf(out->target,sizeof(out->target),"qemu_mirror");
        snprintf(out->argument,sizeof(out->argument),"%s",leaf[0]);
        snprintf(out->options,sizeof(out->options),"force=on");
        DEV_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==ENOTSUP);
        out->options[0]=0;out->flags=OZ_CONFIRMED|OZ_FORCE;
        DEV_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==ENOTSUP);
        out->flags=OZ_DRYRUN;
        DEV_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==EINVAL);
        out->flags=OZ_CONFIRMED|OZ_DRYRUN;
        DEV_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==ENOTSUP);
    }
    DEV_CHECK(!worker_argument_test(16,"qemu_mirror","0",OZ_ENGINE_ERROR,EINVAL));
    DEV_CHECK(!worker_argument_test(16,"qemu_mirror","-1",OZ_ENGINE_ERROR,EINVAL));
    DEV_CHECK(!worker_argument_test(16,"qemu_mirror","18446744073709551616",OZ_ENGINE_ERROR,EINVAL));
    DEV_CHECK(!worker_argument_test(16,"qemu_mirror",parent,OZ_ENGINE_ERROR,write?ENOTSUP:EROFS));
    DEV_CHECK(!worker_argument_test(15,"qemu_mirror","1",OZ_ENGINE_ERROR,write?ENODEV:EROFS));
    /* The legacy message port must not acquire these mutations. */
    DEV_CHECK(legacy_args(16,"qemu_mirror",leaf[0],NULL,out)==EIO&&out->status==OZ_UNSUPPORTED);
    DEV_CHECK(!worker_argument_test(16,"qemu_mirror",leaf[0],write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
    if(write) {
        DEV_CHECK(!legacy(4,"qemu_mirror",out));
        unsigned offline=0;
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];
            if(!strcmp(r->kind,"pool"))DEV_CHECK(!strcmp(r->state,"DEGRADED"));
            if(!strcmp(r->state,"OFFLINE"))offline++;
        }
        DEV_CHECK(offline==1);
        DEV_CHECK(!worker_argument_test(16,"qemu_mirror",leaf[1],OZ_ENGINE_ERROR,EBUSY));
        DEV_CHECK(!worker_argument_test(15,"qemu_mirror",leaf[0],OZ_OK,0));
    } else DEV_CHECK(!worker_argument_test(15,"qemu_mirror",leaf[0],OZ_ENGINE_ERROR,EROFS));
    DEV_CHECK(!legacy(4,"qemu_mirror",out));
    for(unsigned i=0;i<out->row_count;i++) {
        if(!strcmp(out->rows[i].kind,"initialize"))continue;
        DEV_CHECK(!strcmp(out->rows[i].state,"ONLINE"));
    }
done:
    free(out);char line[100];snprintf(line,sizeof(line),"Native device controls write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef DEV_CHECK
}
static int degraded_payload_test(int write)
{
    const char *dataset="qemu_mirror/degraded-payload";
    struct oz_message *out=calloc(1,sizeof(*out));
    unsigned char bytes[4096];char guid[32]={0};BPTR file=0;
    int error=0,mounted=0;
#define REC_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    REC_CHECK(out);
    if(write) {
        REC_CHECK(!legacy(4,"qemu_mirror",out));
        for(unsigned i=0;i<out->row_count&&!guid[0];i++) {
            struct oz_row *r=&out->rows[i];
            if(strcmp(r->kind,"disk")&&strcmp(r->kind,"file"))continue;
            REC_CHECK(!strcmp(r->name,"/AROS/ata.device/0"));
            const char *p=strstr(r->value,"guid=");REC_CHECK(p);
            unsigned long long n=strtoull(p+5,NULL,10);REC_CHECK(n);
            snprintf(guid,sizeof(guid),"%llu",n);
        }
        REC_CHECK(guid[0]);
        REC_CHECK(!worker_argument_test(16,"qemu_mirror",guid,OZ_OK,0));
        REC_CHECK(!legacy(4,"qemu_mirror",out)&&!strcmp(out->rows[0].state,"DEGRADED"));
        REC_CHECK(!worker_test(37,dataset,OZ_OK,0));
    } else {
        /* The recovery runner withholds the originally healthy member.
         * Cold reads must come from the member repaired by resilver. */
        REC_CHECK(!legacy(4,"qemu_mirror",out));
        REC_CHECK(out->row_count&&!strcmp(out->rows[0].state,"DEGRADED"));
        unsigned missing=0;
        for(unsigned i=0;i<out->row_count;i++)
            if(!strcmp(out->rows[i].state,"UNAVAIL"))missing++;
        REC_CHECK(missing==1);
    }
    REC_CHECK(!worker_options_test(43,dataset,"OZRECOVER",write?"readonly=off":"readonly=on",OZ_OK,0));mounted=1;
    if(write) {
        file=Open((CONST_STRPTR)"OZRECOVER:payload.bin",MODE_NEWFILE);REC_CHECK(file);
        for(unsigned block=0;block<64;block++) {
            for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)((block*37+i*13)^(i>>5));
            REC_CHECK(Write(file,bytes,sizeof(bytes))==sizeof(bytes));
        }
        REC_CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
        REC_CHECK(Close(file));file=0;
        REC_CHECK(!worker_argument_test(15,"qemu_mirror",guid,OZ_OK,0));
        /* Exercise explicit restart after a real offline-write-online cycle.
         * A small repair can finish before this request; completion and cold
         * reads from the repaired member, not acceptance alone, are checked. */
        REC_CHECK(!worker_test(19,"qemu_mirror",OZ_OK,0));
        int finished=0;
        for(unsigned poll=0;poll<600;poll++) {
            REC_CHECK(!legacy(4,"qemu_mirror",out));
            if(strstr(out->detail,"scan=resilver state=finished")) {
                REC_CHECK(strstr(out->detail," errors=0 "));
                finished=1;break;
            }
            Delay(25);
        }
        if(!finished)for(const char *p=out->detail;*p;p++)RawPutChar(*p);
        REC_CHECK(finished);
        for(unsigned i=0;i<out->row_count;i++) {
            if(!strcmp(out->rows[i].kind,"initialize"))continue;
            REC_CHECK(!strcmp(out->rows[i].state,"ONLINE"));
        }
    }
    file=Open((CONST_STRPTR)"OZRECOVER:payload.bin",MODE_OLDFILE);REC_CHECK(file);
    for(unsigned block=0;block<64;block++) {
        REC_CHECK(Read(file,bytes,sizeof(bytes))==sizeof(bytes));
        for(unsigned i=0;i<sizeof(bytes);i++)REC_CHECK(bytes[i]==(unsigned char)((block*37+i*13)^(i>>5)));
    }
    REC_CHECK(Read(file,bytes,1)==0);
    REC_CHECK(Close(file));file=0;
done:
    if(file)Close(file);
    if(mounted){int rc=worker_test(44,dataset,OZ_OK,0);if(!error)error=rc;}
    free(out);char line[100];snprintf(line,sizeof(line),"Native degraded payload write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef REC_CHECK
}
static int exec_enumeration_test(int denied)
{
    BPTR root=Lock((CONST_STRPTR)"OZEXEC:",ACCESS_READ);
    if(!root)return EIO;
    struct ExAllControl *ctl=AllocDosObject(DOS_EXALLCONTROL,NULL);
    if(!ctl){UnLock(root);return ENOMEM;}
    union {IPTR align;unsigned char bytes[512];} storage;
    unsigned seen=0,calls=0;int error=0;
    for(;;) {
        BOOL more=ExAll(root,(void *)storage.bytes,sizeof(storage.bytes),ED_PROTECTION,ctl);
        LONG why=IoErr();struct ExAllData *entry=(void *)storage.bytes;
        for(unsigned i=0;i<ctl->eac_Entries;i++) {
            if(!entry){error=EIO;break;}
            if(!strcmp((const char *)entry->ed_Name,"hello.txt")) {
                if(seen&1)error=EIO;
                seen|=1;
                if(!!(entry->ed_Prot&FIBF_EXECUTE)!=denied)error=EIO;
                if(denied&&(entry->ed_Prot&(FIBF_GRP_EXECUTE|FIBF_OTR_EXECUTE)))error=EIO;
            } else if(!strcmp((const char *)entry->ed_Name,"subdir")) {
                if((seen&2)||(entry->ed_Prot&FIBF_EXECUTE))error=EIO;
                seen|=2;
            } else error=EIO;
            entry=entry->ed_Next;
        }
        if(++calls>4)error=EIO;
        if(error||!more) {
            if(!error&&(why!=ERROR_NO_MORE_ENTRIES||seen!=3))error=EIO;
            break;
        }
    }
    ExAllEnd(root,(void *)storage.bytes,sizeof(storage.bytes),ED_PROTECTION,ctl);
    FreeDosObject(DOS_EXALLCONTROL,ctl);UnLock(root);
    return error;
}
static int exec_property_test(int write)
{
    const char *dataset="qemu_mirror/exec-policy";
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    BPTR lock=0,file=0;int error=0,mounted=0;
#define EXEC_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    EXEC_CHECK(fib);
    EXEC_CHECK(!worker_options_test(43,dataset,"OZEXEC","readonly=on",OZ_OK,0));mounted=1;
    lock=Lock((CONST_STRPTR)"OZEXEC:hello.txt",ACCESS_READ);EXEC_CHECK(lock);
    EXEC_CHECK(Examine(lock,fib)&&(fib->fib_Protection&FIBF_EXECUTE));
    EXEC_CHECK(!(fib->fib_Protection&(FIBF_GRP_EXECUTE|FIBF_OTR_EXECUTE)));
    EXEC_CHECK(!exec_enumeration_test(1));
    if(write) {
        EXEC_CHECK(!worker_argument_test(41,dataset,"exec=on",OZ_OK,0));
        EXEC_CHECK(Examine(lock,fib)&&!(fib->fib_Protection&FIBF_EXECUTE));
        EXEC_CHECK(!exec_enumeration_test(0));
        EXEC_CHECK(!worker_argument_test(41,dataset,"exec=invalid",OZ_ENGINE_ERROR,EINVAL));
        EXEC_CHECK(Examine(lock,fib)&&!(fib->fib_Protection&FIBF_EXECUTE));
        EXEC_CHECK(!worker_argument_test(41,dataset,"exec=off",OZ_OK,0));
        EXEC_CHECK(Examine(lock,fib)&&(fib->fib_Protection&FIBF_EXECUTE));
        EXEC_CHECK(!exec_enumeration_test(1));
    } else EXEC_CHECK(!worker_argument_test(41,dataset,"exec=on",OZ_ENGINE_ERROR,EROFS));
    UnLock(lock);lock=0;
    lock=Lock((CONST_STRPTR)"OZEXEC:subdir",ACCESS_READ);EXEC_CHECK(lock&&Examine(lock,fib));
    EXEC_CHECK(!(fib->fib_Protection&FIBF_EXECUTE));UnLock(lock);lock=0;
    file=Open((CONST_STRPTR)"OZEXEC:subdir/nested.txt",MODE_OLDFILE);EXEC_CHECK(file);
    char bytes[4];EXEC_CHECK(Read(file,bytes,4)==4&&!memcmp(bytes,"Open",4));
    EXEC_CHECK(Close(file));file=0;
done:
    if(file)Close(file);
    if(lock)UnLock(lock);
    if(fib)FreeDosObject(DOS_FIB,fib);
    if(mounted){int rc=worker_test(44,dataset,OZ_OK,0);if(!error)error=rc;}
    char line[100];snprintf(line,sizeof(line),"Native DOS exec property write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef EXEC_CHECK
}
/* Public deterministic fixture key, never user-provided key material. */
static int fixture_key_path(const char *path,unsigned delta,unsigned length)
{
    unsigned char bytes[33];
    if(length>sizeof(bytes))return EINVAL;
    for(unsigned i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)(i+1+delta);
    BPTR file=Open((CONST_STRPTR)path,MODE_NEWFILE);
    if(!file)return EIO;
    int error=Write(file,bytes,length)==length?0:EIO;
    if(!Close(file)&&!error)error=EIO;
    return error;
}
static int fixture_key_file(unsigned delta,unsigned length)
{return fixture_key_path("RAM:oz-test-key.raw",delta,length);}
static int fixture_hex_file(unsigned delta,unsigned mode)
{
    char bytes[72];size_t length=64;
    for(unsigned i=0;i<32;i++){
        const char *digits=i&1?"0123456789ABCDEF":"0123456789abcdef";
        unsigned v=(i+1+delta)&255;bytes[2*i]=digits[v>>4];bytes[2*i+1]=digits[v&15];
    }
    if(mode==1)bytes[length++]='\n';
    if(mode==2){memcpy(bytes+length,"\ntail",5);length+=5;}
    if(mode==3)length--;
    if(mode==4)bytes[length++]='0';
    if(mode==5)bytes[31]='g';
    if(mode==6){bytes[length++]='\r';bytes[length++]='\n';}
    BPTR file=Open((CONST_STRPTR)"RAM:oz-test-hex.key",MODE_NEWFILE);
    if(!file)return EIO;
    int error=Write(file,bytes,length)==length?0:EIO;
    if(!Close(file)&&!error)error=EIO;
    return error;
}
static int encrypted_dos_read(void)
{
    BPTR file=Open((CONST_STRPTR)"OZCRYPTCHECK:encrypted-data.bin",MODE_OLDFILE);
    if(!file)return EIO;
    unsigned char bytes[4096];int error=0;
    for(size_t at=0;!error&&at<65536;at+=sizeof(bytes)){
        if(Read(file,bytes,sizeof(bytes))!=sizeof(bytes)){error=EIO;break;}
        for(size_t i=0;i<sizeof(bytes);i++)
            if(bytes[i]!=(unsigned char)((at+i)*37+((at+i)>>8))){error=EIO;break;}
    }
    if(!error&&Read(file,bytes,1)!=0)error=EIO;
    if(!Close(file)&&!error)error=EIO;
    return error;
}
static int fixture_phrase(const char *bytes,size_t length)
{
    BPTR file=Open((CONST_STRPTR)"RAM:oz-test-phrase.key",MODE_NEWFILE);
    if(!file)return EIO;
    int error=Write(file,bytes,length)==length?0:EIO;
    if(!Close(file)&&!error)error=EIO;
    return error;
}
static int changed_key_dos_read(void)
{
    const char *expected="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    char bytes[128];
    BPTR file=Open((CONST_STRPTR)"OZKEYCHANGE:hello.txt",MODE_OLDFILE);
    if(!file)return EIO;
    LONG actual=Read(file,bytes,sizeof(bytes));
    int error=actual!=(LONG)strlen(expected)||memcmp(bytes,expected,strlen(expected))?EIO:0;
    if(!Close(file)&&!error)error=EIO;
    return error;
}
int main(int argc,char **argv)
{
    if(argc!=2||(strcmp(argv[1],"WRITE")&&strcmp(argv[1],"VERIFY")&&strcmp(argv[1],"CREATE")&&strcmp(argv[1],"ADD")&&strcmp(argv[1],"SPLIT")&&strcmp(argv[1],"CANCEL")&&strcmp(argv[1],"EXECWRITE")&&strcmp(argv[1],"EXECVERIFY")&&strcmp(argv[1],"DEVICEWRITE")&&strcmp(argv[1],"DEVICEVERIFY")))return 20;
    int write=!strcmp(argv[1],"WRITE")||!strcmp(argv[1],"EXECWRITE")||!strcmp(argv[1],"DEVICEWRITE"),error=0,logged=0;
    struct Library *secBase=NULL;ULONG previous=0;
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    struct oz_ticket owner_ticket={0},reader_ticket={0};
    struct DevProc *retired=NULL;
#define CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    CHECK(m&&out);
    CHECK(delayed_worker_test()==0);
    for(unsigned i=0;i<600&&(!present(OZ_ADMISSION_TEST_PORT)||!present(OZ_NATIVE_TEST_PORT));i++)Delay(5);
    CHECK(present(OZ_ADMISSION_TEST_PORT)&&present(OZ_NATIVE_TEST_PORT));
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
    previous=secGetTaskOwner(FindTask(NULL));
    CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
    if(!strcmp(argv[1],"WRITE")||!strcmp(argv[1],"VERIFY")){
        logout(secBase);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EPERM));
        logout(secBase);logged=0;
        CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
        CHECK(!worker_options_test(43,"qemu_mirror/encrypted-fixture","OZCRYPTCHECK","readonly=on",OZ_OK,0));
        CHECK(!encrypted_dos_read());
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_ENGINE_ERROR,EBUSY));
        CHECK(!worker_test(44,"qemu_mirror/encrypted-fixture",OZ_OK,0));
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_OK,0));
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_ENGINE_ERROR,EACCES));
        CHECK(!worker_options_test(43,"qemu_mirror/encrypted-fixture","OZCRYPTCHECK","readonly=on",OZ_ENGINE_ERROR,EACCES));
        const char *key_notice="Native authenticated key unload PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        CHECK(!fixture_key_file(2,31));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EINVAL));
        CHECK(!fixture_key_file(2,33));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EINVAL));
        CHECK(!fixture_key_file(1,32));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EACCES));
        CHECK(!fixture_key_file(2,32));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_OK,0));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EEXIST));
        CHECK(!worker_options_test(43,"qemu_mirror/encrypted-fixture","OZCRYPTCHECK","readonly=on",OZ_OK,0));
        CHECK(!encrypted_dos_read());
        CHECK(!worker_test(44,"qemu_mirror/encrypted-fixture",OZ_OK,0));
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_OK,0));
        key_notice="Native authenticated key load PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        CHECK(secSetProtection((CONST_STRPTR)"RAM:oz-test-key.raw",0));
        logout(secBase);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-delegated","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EACCES));
        logout(secBase);logged=0;
        CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
        CHECK(secSetProtection((CONST_STRPTR)"RAM:oz-test-key.raw",FIBF_OTR_READ));
        logout(secBase);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-delegated","RAM:oz-test-key.raw",OZ_OK,0));
        CHECK(!worker_test(63,"qemu_mirror/encrypted-delegated",OZ_OK,0));
        logout(secBase);logged=0;
        CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
        CHECK(!worker_options_test(43,"qemu_mirror/key-source","OZKEYSRC",write?"readonly=off":"readonly=on",OZ_OK,0));
        if(write)CHECK(!fixture_key_path("OZKEYSRC:test-key.raw",2,32));
        CHECK(!worker_argument_test(62,"qemu_mirror/encrypted-fixture","OZKEYSRC:test-key.raw",OZ_OK,0));
        CHECK(!worker_test(63,"qemu_mirror/encrypted-fixture",OZ_OK,0));
        CHECK(!worker_test(44,"qemu_mirror/key-source",OZ_OK,0));
        key_notice="Native delegated and own-volume key load PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        logout(secBase);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!worker_argument_test(64,"qemu_mirror/encrypted-delegated","RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EPERM));
        logout(secBase);logged=0;
        CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
        const char *change_ds="qemu_mirror/encrypted-api-change";
        CHECK(!worker_argument_test(64,change_ds,"RAM:oz-test-key.raw",OZ_ENGINE_ERROR,write?EACCES:EROFS));
        if(!write){
            CHECK(!worker_argument_test(62,change_ds,"RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EACCES));
            CHECK(!fixture_key_file(3,32));
        }
        CHECK(!worker_argument_test(62,change_ds,"RAM:oz-test-key.raw",OZ_OK,0));
        CHECK(!worker_options_test(43,change_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        if(write)CHECK(!fixture_key_file(3,32));
        CHECK(!worker_argument_test(64,change_ds,"RAM:oz-test-key.raw",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,change_ds,OZ_OK,0));
        CHECK(!worker_test(63,change_ds,OZ_OK,0));
        CHECK(!fixture_key_file(2,32));
        CHECK(!worker_argument_test(62,change_ds,"RAM:oz-test-key.raw",OZ_ENGINE_ERROR,EACCES));
        CHECK(!fixture_key_file(3,32));
        CHECK(!worker_argument_test(62,change_ds,"RAM:oz-test-key.raw",OZ_OK,0));
        CHECK(!worker_options_test(43,change_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,change_ds,OZ_OK,0));
        CHECK(!worker_test(63,change_ds,OZ_OK,0));
        key_notice="Native authenticated key replacement PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        const char *hex_ds="qemu_mirror/encrypted-hex";
        for(unsigned malformed=3;malformed<=6;malformed++){
            CHECK(!fixture_hex_file(2,malformed));
            CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_ENGINE_ERROR,EINVAL));
        }
        CHECK(!fixture_hex_file(1,0));
        CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_ENGINE_ERROR,EACCES));
        if(!write){
            CHECK(!fixture_hex_file(2,1));
            CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_ENGINE_ERROR,EACCES));
        }
        CHECK(!fixture_hex_file(write?2:3,2));
        CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_OK,0));
        CHECK(!worker_options_test(43,hex_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        CHECK(!fixture_hex_file(3,1));
        CHECK(!worker_argument_test(64,hex_ds,"RAM:oz-test-hex.key",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,hex_ds,OZ_OK,0));
        CHECK(!worker_test(63,hex_ds,OZ_OK,0));
        CHECK(!fixture_hex_file(2,0));
        CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_ENGINE_ERROR,EACCES));
        CHECK(!fixture_hex_file(3,0));
        CHECK(!worker_argument_test(62,hex_ds,"RAM:oz-test-hex.key",OZ_OK,0));
        CHECK(!worker_options_test(43,hex_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,hex_ds,OZ_OK,0));
        CHECK(!worker_test(63,hex_ds,OZ_OK,0));
        key_notice="Native hexadecimal key lifecycle PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        const char *phrase_ds="qemu_mirror/encrypted-passphrase";
        const char *oldphrase="AROS OpenZFS fixture passphrase";
        const char *newphrase="AROS replacement passphrase";
        const char *selected=write?oldphrase:newphrase;
        CHECK(!fixture_phrase("incorrect passphrase",20));
        CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EACCES));
        if(!write){
            CHECK(!fixture_phrase(oldphrase,strlen(oldphrase)));
            CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EACCES));
        }
        char phrase_line[600];snprintf(phrase_line,sizeof(phrase_line),"%s\nignored second line",selected);
        CHECK(!fixture_phrase(phrase_line,strlen(phrase_line)));
        CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_OK,0));
        CHECK(!worker_options_test(43,phrase_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        if(write){
            CHECK(!fixture_phrase("short",5));
            CHECK(!worker_argument_test(64,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EINVAL));
            memset(phrase_line,'x',513);
            CHECK(!fixture_phrase(phrase_line,513));
            CHECK(!worker_argument_test(64,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EINVAL));
        }
        CHECK(!fixture_phrase(newphrase,strlen(newphrase)));
        CHECK(!worker_argument_test(64,phrase_ds,"RAM:oz-test-phrase.key",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,phrase_ds,OZ_OK,0));
        CHECK(!worker_test(63,phrase_ds,OZ_OK,0));
        CHECK(!fixture_phrase(oldphrase,strlen(oldphrase)));
        CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EACCES));
        snprintf(phrase_line,sizeof(phrase_line),"%s\r\n",newphrase);
        CHECK(!fixture_phrase(phrase_line,strlen(phrase_line)));
        CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_ENGINE_ERROR,EACCES));
        CHECK(!fixture_phrase(newphrase,strlen(newphrase)));
        CHECK(!worker_argument_test(62,phrase_ds,"RAM:oz-test-phrase.key",OZ_OK,0));
        CHECK(!worker_options_test(43,phrase_ds,"OZKEYCHANGE","readonly=on",OZ_OK,0));
        CHECK(!changed_key_dos_read());
        CHECK(!worker_test(44,phrase_ds,OZ_OK,0));
        CHECK(!worker_test(63,phrase_ds,OZ_OK,0));
        key_notice="Native passphrase key lifecycle PASS\n";
        for(const char *p=key_notice;*p;p++)RawPutChar(*p);
        if(write){
            logout(secBase);logged=0;
            CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
            CHECK(!worker_test(78,"qemu_mirror/abort-api-new",OZ_ENGINE_ERROR,EPERM));
            CHECK(!worker_test(78,"qemu_mirror/abort-api-incremental",OZ_ENGINE_ERROR,EPERM));
            logout(secBase);logged=0;
            CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
        }
        CHECK(!worker_test(78,"qemu_mirror/abort-incremental",OZ_ENGINE_ERROR,write?EINVAL:EROFS));
        CHECK(!worker_options_test(43,"qemu_mirror/abort-incremental","OZABORTCHECK","readonly=on",OZ_OK,0));
        CHECK(!worker_test(78,"qemu_mirror/abort-incremental",OZ_ENGINE_ERROR,EBUSY));
        CHECK(!worker_test(44,"qemu_mirror/abort-incremental",OZ_OK,0));
        CHECK(!worker_test(78,"qemu_mirror/abort-api-new",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        CHECK(!worker_test(78,"qemu_mirror/abort-api-incremental",write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        CHECK(!worker_test(78,"qemu_mirror/abort-api-incremental",OZ_ENGINE_ERROR,write?EINVAL:EROFS));
        const char *notice="Native authenticated receive abort PASS\n";
        for(const char *p=notice;*p;p++)RawPutChar(*p);
    }
    int clear_write=strstr(argv[1],"VERIFY")==NULL;
    CHECK(!worker_test(19,"qemu_mirror",clear_write?OZ_OK:OZ_ENGINE_ERROR,clear_write?0:EROFS));
    CHECK(!worker_test(19,"qemu_mirror/dataset",OZ_ENGINE_ERROR,EINVAL));
    CHECK(!worker_test(17,"qemu_mirror",clear_write?OZ_OK:OZ_ENGINE_ERROR,clear_write?0:EROFS));
    CHECK(!worker_argument_test(17,"qemu_mirror","18446744073709551616",OZ_ENGINE_ERROR,EINVAL));
    CHECK(!worker_argument_test(17,"qemu_mirror","1",OZ_ENGINE_ERROR,clear_write?ENODEV:EROFS));
    CHECK(!worker_options_test(20,"qemu_mirror","invalid","guids=1",OZ_ENGINE_ERROR,EINVAL));
    CHECK(!worker_options_test(20,"qemu_mirror","start","guids=1",OZ_ENGINE_ERROR,clear_write?EINVAL:EROFS));
    CHECK(!worker_options_test(21,"qemu_mirror","start","guids=1;rate=18446744073709551616",OZ_ENGINE_ERROR,ERANGE));
    CHECK(!worker_options_test(21,"qemu_mirror","start","guids=1",OZ_ENGINE_ERROR,clear_write?EINVAL:EROFS));
    if(!strcmp(argv[1],"SPLIT")) {
        CHECK(!pool_split_test());
        CHECK(logout(secBase)==previous);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!worker_options_test(14,"aros_added","aros_split","guids=1",OZ_ENGINE_ERROR,EPERM));
        goto done;
    }
    if(!strcmp(argv[1],"ADD")||!strcmp(argv[1],"CANCEL")) {
        removal_cancel_test=!strcmp(argv[1],"CANCEL");
        CHECK(!pool_add_test());
        CHECK(logout(secBase)==previous);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        CHECK(!add_request(9,0,2,NULL,EPERM));
        CHECK(!worker_options_test(10,"aros_added","1",NULL,OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_options_test(10,"aros_added","cancel",NULL,OZ_ENGINE_ERROR,EPERM));
        goto done;
    }
    if(!strcmp(argv[1],"CREATE")) {
        CHECK(!import_options_test());
        for(file_pool_layout=OZ_STRIPE;file_pool_layout<=OZ_RAIDZ3;file_pool_layout++) {
            file_pool_disks=file_pool_layout>=OZ_RAIDZ1?file_pool_layout:2;
            CHECK(!file_pool_create_test());
        }
        file_pool_layout=OZ_MIRROR;file_pool_disks=2;
        CHECK(!pool_add_test());
        CHECK(logout(secBase)==previous);logged=0;
        CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
        /* Upstream sync uses zfs_secpolicy_none, not root-only config policy. */
        CHECK(!sync_request("qemu_mirror",OZ_CONFIRMED,0));
        CHECK(!sync_request("qemu_mirror",OZ_CONFIRMED|OZ_FORCE,0));
        CHECK(!file_pool_request("aros_denied",EPERM));
        CHECK(!worker_test(6,"aros_created",OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_argument_test(12,"qemu_mirror","1",OZ_ENGINE_ERROR,EPERM));
        CHECK(!attach_request(11,"1","RAM:nonroot-refusal",EPERM));
        CHECK(!attach_request(13,"1","RAM:nonroot-refusal",EPERM));
        CHECK(!add_request(9,0,2,NULL,EPERM));
        CHECK(!worker_test(24,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_test(23,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_argument_test(22,"qemu_mirror","create",OZ_ENGINE_ERROR,EPERM));
        CHECK(!worker_argument_test(32,"qemu_mirror","feature@zpool_checkpoint=enabled",OZ_ENGINE_ERROR,EPERM));
        CHECK(!file_pool_import("aros_denied","readonly=on",EPERM,0));
        CHECK(!file_pool_import("aros_denied","readonly=on;destroyed=on",EPERM,0));
        CHECK(!file_pool_import("aros_denied","readonly=on;rewind=on",EPERM,0));
        CHECK(!file_pool_import("aros_denied","readonly=off;rewind=on",EPERM,0));
        goto done;
    }
    CHECK(!worker_test(8,"qemu_mirror/delegated-root",OZ_ENGINE_ERROR,EINVAL));
    if(!strncmp(argv[1],"DEVICE",6)) {
        CHECK(!degraded_payload_test(write));
        CHECK(!worker_test(8,"qemu_mirror",OZ_OK,0));
        goto done;
    }
    if(!strncmp(argv[1],"EXEC",4)) {
        CHECK(!exec_property_test(write));
        CHECK(!worker_test(8,"qemu_mirror",OZ_OK,0));
        goto done;
    }
    CHECK(!worker_test(8,"qemu_mirror@invalid",OZ_ENGINE_ERROR,EINVAL));
    if(!strcmp(argv[1],"WRITE")||!strcmp(argv[1],"VERIFY"))CHECK(!receive_api_test(write));
    if(!strcmp(argv[1],"WRITE")||!strcmp(argv[1],"VERIFY"))CHECK(!own_send_api_test(write));
    CHECK(count_limit_tests(write,1)==0);
    CHECK(!device_control_test(write));
    CHECK(worker_test(37,write?"qemu_mirror/worker-created":"qemu_mirror/worker-readonly",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS)==0);
    CHECK(worker_test(50,write?"qemu_mirror/worker-created@auth":"qemu_mirror/worker-created@readonly",
        write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS)==0);
    if(write) {
        CHECK(worker_test(37,"qemu_mirror/lifecycle-original",OZ_OK,0)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/lifecycle-original","qemu_mirror/lifecycle-renamed",OZ_OK,0)==0);
        CHECK(worker_test(50,"qemu_mirror/lifecycle-renamed@remove",OZ_OK,0)==0);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed@remove",OZ_OK,0)==0);
        CHECK(worker_test(37,"qemu_mirror/lifecycle-disposable",OZ_OK,0)==0);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-disposable",OZ_OK,0)==0);
    } else {
        CHECK(worker_argument_test(39,"qemu_mirror/lifecycle-renamed","qemu_mirror/readonly-rename",OZ_ENGINE_ERROR,EROFS)==0);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed",OZ_ENGINE_ERROR,EROFS)==0);
    }
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=8;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"qemu_mirror");
    {struct oz_ticket rejected={0};
        strcpy(m->argument,"unexpected");
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)==ENOTSUP);
        m->argument[0]=0;strcpy(m->options,"force=on");
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)==ENOTSUP);
        m->options[0]=0;m->flags=OZ_CONFIRMED|OZ_FORCE;
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)==ENOTSUP);
        m->flags=OZ_CONFIRMED|OZ_DRYRUN;
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)==ENOTSUP);
        m->flags=0;
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)==EINVAL);
    }
    m->operation=37;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"qemu_mirror/service-created");
    if(write) {
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&owner_ticket)==0);
        CHECK(poll_result(&owner_ticket,out)==0&&out->status==OZ_OK);
    } else {
        snprintf(m->target,sizeof(m->target),"qemu_mirror/readonly-refused");
        CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&owner_ticket)==0);
        CHECK(poll_result(&owner_ticket,out)==0&&out->status==OZ_ENGINE_ERROR);
        CHECK(strstr(out->detail,"errno=30")!=NULL);
    }
    CHECK(!pool_property_tests(write));
    CHECK(!history_tests(write));
    CHECK(logout(secBase)==previous);logged=0;
    CHECK(oz_admission_query(OZ_ADMISSION_TEST_PORT,&owner_ticket,out)==EACCES);
    CHECK(login(secBase,"ozreader")==((77UL<<16)|88));logged=1;
    CHECK(count_limit_tests(write,0)==0);
    CHECK(!worker_argument_test(61,"qemu_mirror/reader-receive@base","RAM:must-not-open",OZ_ENGINE_ERROR,EPERM));
    if(!strcmp(argv[1],"WRITE")||!strcmp(argv[1],"VERIFY"))
        CHECK(!worker_argument_test(60,"qemu_mirror/stream-source@base","RAM:must-not-open",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_argument_test(32,"qemu_mirror","comment=non-root",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_test(33,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_test(17,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_options_test(20,"qemu_mirror","start","guids=1",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_options_test(21,"qemu_mirror","start","guids=1",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_test(19,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
    CHECK(worker_test(37,"qemu_mirror/worker-reader-refused",OZ_ENGINE_ERROR,write?EPERM:EROFS)==0);
    CHECK(!worker_test(8,"qemu_mirror",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_argument_test(12,"qemu_mirror","1",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_argument_test(15,"qemu_mirror","1",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_argument_test(16,"qemu_mirror","1",OZ_ENGINE_ERROR,EPERM));
    CHECK(!file_pool_request("aros_denied",EPERM));
    CHECK(!worker_argument_test(18,"qemu_mirror","start",OZ_ENGINE_ERROR,EPERM));
    CHECK(!worker_argument_test(18,"qemu_mirror","stop",OZ_ENGINE_ERROR,EPERM));
    CHECK(worker_test(50,"qemu_mirror/worker-created@reader-refused",OZ_ENGINE_ERROR,EPERM)==0);
    CHECK(worker_argument_test(39,"qemu_mirror/lifecycle-renamed","qemu_mirror/reader-rename",OZ_ENGINE_ERROR,EPERM)==0);
    CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed",OZ_ENGINE_ERROR,EPERM)==0);
    CHECK(worker_options_test(43,"qemu_mirror/lifecycle-renamed","OZDENIED","readonly=off",OZ_ENGINE_ERROR,EPERM)==0);
    if(write) {
        CHECK(worker_test(37,"qemu_mirror/delegated-root/owned",OZ_OK,0)==0);
        CHECK(worker_test(50,"qemu_mirror/delegated-root/owned@keep",OZ_OK,0)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/delegated-root/owned","qemu_mirror/delegated-root/moved",OZ_OK,0)==0);
        CHECK(worker_test(50,"qemu_mirror/delegated-root/moved@discard",OZ_OK,0)==0);
        CHECK(worker_test(38,"qemu_mirror/delegated-root/moved@discard",OZ_OK,0)==0);
        CHECK(worker_test(37,"qemu_mirror/delegated-root/disposable",OZ_OK,0)==0);
        CHECK(worker_test(38,"qemu_mirror/delegated-root/disposable",OZ_OK,0)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/delegated-root/moved","qemu_mirror/outside-grant",OZ_ENGINE_ERROR,EPERM)==0);
    } else {
        CHECK(worker_test(37,"qemu_mirror/delegated-root/readonly",OZ_ENGINE_ERROR,EROFS)==0);
        CHECK(worker_test(50,"qemu_mirror/delegated-root/moved@readonly",OZ_ENGINE_ERROR,EROFS)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/delegated-root/moved","qemu_mirror/delegated-root/readonly",OZ_ENGINE_ERROR,EROFS)==0);
        CHECK(worker_test(38,"qemu_mirror/delegated-root/moved@keep",OZ_ENGINE_ERROR,EROFS)==0);
    }
    CHECK(user_property_tests(write)==0);
    CHECK(space_property_tests(write)==0);
    if(!write)CHECK(worker_options_test(43,"qemu_mirror/delegated-root/moved","OZWRITABLE","readonly=off",OZ_ENGINE_ERROR,EROFS)==0);
    CHECK(worker_options_test(43,"qemu_mirror/delegated-root/moved","OZAUTHFILES",
        write?"readonly=off":"readonly=on",OZ_OK,0)==0);
    CHECK(legacy(44,"qemu_mirror/delegated-root/moved",out)==EIO&&strstr(out->detail,"errno=1.")!=NULL);
    const char payload[]="Native DOS file written by delegated MuFS UID 77 through OpenZFS";
    if(write) {
        BPTR file=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_NEWFILE);CHECK(file);
        LONG written=Write(file,(APTR)payload,sizeof(payload));
        LONG closed=Close(file);CHECK(written==sizeof(payload)&&closed);
    }
    BPTR file=Open((CONST_STRPTR)"OZAUTHFILES:written.txt",MODE_OLDFILE);CHECK(file);
    char bytes[sizeof(payload)+1];memset(bytes,0,sizeof(bytes));
    LONG count=Read(file,bytes,sizeof(bytes));LONG closed=Close(file);
    CHECK(count==sizeof(payload)&&closed&&!memcmp(bytes,payload,sizeof(payload)));
    CHECK(many_files_test(payload,sizeof(payload))==0);
    CHECK(many_locks_test(payload,sizeof(payload))==0);
    CHECK(record_tests()==0);
    CHECK(notification_tests(write,secBase)==0);
    CHECK(quota_io_test(write)==0);
    CHECK(recordsize_tests(write)==0);
    CHECK(compression_tests(write)==0);
    CHECK(readonly_property_tests(write)==0);
    CHECK(timestamp_property_tests(write)==0);
    if(!write) {
        file=Open((CONST_STRPTR)"OZAUTHFILES:readonly-refused",MODE_NEWFILE);
        CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    }
    CHECK(detach_notification_test()==0);
    CHECK(snapshot_control_tests(write,payload,sizeof(payload))==0);
    CHECK(rollback_tests(write,payload,sizeof(payload))==0);
    CHECK(promotion_tests(write,payload,sizeof(payload))==0);
    CHECK(bookmark_tests(write)==0);
    CHECK(oz_admission_query(OZ_ADMISSION_TEST_PORT,&owner_ticket,out)==EACCES);
    CHECK(oz_admission_release(OZ_ADMISSION_TEST_PORT,&owner_ticket)==EACCES);
    snprintf(m->target,sizeof(m->target),"qemu_mirror/reader-refused");
    CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&reader_ticket)==0);
    CHECK(poll_result(&reader_ticket,out)==0&&out->status==OZ_ENGINE_ERROR);
    CHECK(strstr(out->detail,write?"errno=1":"errno=30")!=NULL);
    CHECK(oz_admission_release(OZ_ADMISSION_TEST_PORT,&reader_ticket)==0);
    CHECK(logout(secBase)==previous);logged=0;
    CHECK(login(secBase,"oztestroot")==secOWNER_SYSTEM);logged=1;
    CHECK(oz_admission_release(OZ_ADMISSION_TEST_PORT,&owner_ticket)==0);
    CHECK(legacy(36,"qemu_mirror/service-created",out)==0&&out->row_count==1);
    CHECK(!strcmp(out->rows[0].name,"qemu_mirror/service-created"));
    CHECK(legacy(36,"qemu_mirror/worker-created@auth",out)==0&&out->row_count==1);
    CHECK(!strcmp(out->rows[0].name,"qemu_mirror/worker-created@auth"));
    CHECK(legacy(36,"qemu_mirror/lifecycle-renamed",out)==0&&out->row_count==1);
    CHECK(!strcmp(out->rows[0].name,"qemu_mirror/lifecycle-renamed"));
    CHECK(legacy(36,"qemu_mirror/delegated-root/moved@keep",out)==0&&out->row_count==1);
    CHECK(!strcmp(out->rows[0].name,"qemu_mirror/delegated-root/moved@keep"));
    CHECK(legacy(36,"qemu_mirror/delegated-root/disposable",out)==EIO&&strstr(out->detail,"errno=2,")!=NULL);
    CHECK(legacy(36,"qemu_mirror/delegated-root/moved@discard",out)==EIO&&strstr(out->detail,"errno=2,")!=NULL);
    CHECK(legacy(36,"qemu_mirror/lifecycle-disposable",out)==EIO&&strstr(out->detail,"errno=2,")!=NULL);
    CHECK(legacy(36,"qemu_mirror/lifecycle-original",out)==EIO&&strstr(out->detail,"errno=2,")!=NULL);
    CHECK(legacy(36,"qemu_mirror/lifecycle-renamed@remove",out)==EIO&&strstr(out->detail,"errno=2,")!=NULL);
    if(write) {
        CHECK(worker_test(37,"qemu_mirror/lifecycle-renamed/mounted-child",OZ_OK,0)==0);
        CHECK(legacy_args(43,"qemu_mirror/lifecycle-renamed/mounted-child","OZAUTHMOUNT","readonly=on",out)==0);
        CHECK(!worker_test(8,"qemu_mirror",OZ_ENGINE_ERROR,EBUSY));
        CHECK(!worker_argument_test(41,"qemu_mirror/lifecycle-renamed/mounted-child","readonly=on",OZ_OK,0));
        CHECK(!worker_argument_test(41,"qemu_mirror/lifecycle-renamed/mounted-child","readonly=off",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZAUTHMOUNT:must-stay-readonly",MODE_NEWFILE);
        CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        retired=GetDeviceProc((CONST_STRPTR)"OZAUTHMOUNT:",NULL);CHECK(retired);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed/mounted-child",OZ_ENGINE_ERROR,EBUSY)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/lifecycle-renamed/mounted-child","qemu_mirror/moved-child",OZ_ENGINE_ERROR,EBUSY)==0);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed",OZ_ENGINE_ERROR,EBUSY)==0);
        CHECK(worker_argument_test(39,"qemu_mirror/lifecycle-renamed","qemu_mirror/moved-parent",OZ_ENGINE_ERROR,EBUSY)==0);
        CHECK(legacy(44,"qemu_mirror/lifecycle-renamed/mounted-child",out)==0);
        CHECK(worker_test(38,"qemu_mirror/lifecycle-renamed/mounted-child",OZ_OK,0)==0);
    }
    CHECK(!authenticated_scrub_test(write,out));
    CHECK(!io_statistics_test(write));
    CHECK(!worker_test(8,"qemu_mirror",OZ_OK,0));
    {const char *p="Native authenticated pool export result=0\n";while(*p)RawPutChar(*p++);}
    CHECK(logout(secBase)==previous);logged=0;
    /* Test-only orderly stop of this known private service task. */
    Forbid();struct MsgPort *port=FindPort((CONST_STRPTR)OZ_ADMISSION_TEST_PORT);
    if(port)Signal(port->mp_SigTask,SIGBREAKF_CTRL_C);
    Permit();
    CHECK(port!=NULL);
    {
        /* A retired endpoint remains reachable through this actual DevProc.
         * Shutdown must refuse, and late packets must still receive replies. */
        Delay(5);
        CHECK(legacy(1,"",out)==0);
        CHECK(present(OZ_ADMISSION_TEST_PORT)&&present(OZ_NATIVE_TEST_PORT));
        if(retired) {
            CHECK(DoPkt(retired->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)==DOSFALSE);
            CHECK(IoErr()==ERROR_DEVICE_NOT_MOUNTED);
            FreeDeviceProc(retired);retired=NULL;
        }
    }
    Delay(5);
done:
    if(retired)FreeDeviceProc(retired);
    if(logged&&logout(secBase)!=previous)error=EIO;
    if(secBase)CloseLibrary(secBase);
    free(m);free(out);
    char line[120];snprintf(line,sizeof(line),"Standalone admission mode=%s error=%d\nRESULT standalone admission %s\n",argv[1],error,error?"FAIL":"PASS");
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error?20:0;
#undef CHECK
}
