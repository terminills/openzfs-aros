/* SPDX-License-Identifier: CDDL-1.0 */
/* Real libzfs client test: no engine implementation linked here. */
#include <libzfs.h>
#include <pthread.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <limits.h>
#include <sys/zfs_ioctl.h>
#include "../ui/ioctl-api.h"
libzfs_handle_t *g_zfs;

#ifdef OZ_TEST_HISTORY_MEGARECORD
#define OZ_HISTORY_MEGARECORD_BYTES (1024U * 1024U + 4096U)
#define OZ_HISTORY_MEGARECORD_PREFIX "history-megarecord-v1-"
static void megarecord_fill(char *record)
{
    const size_t prefix=strlen(OZ_HISTORY_MEGARECORD_PREFIX);
    memcpy(record,OZ_HISTORY_MEGARECORD_PREFIX,prefix);
    for(size_t i=prefix;i<OZ_HISTORY_MEGARECORD_BYTES;i++)record[i]=(char)('a'+i%23);
    record[OZ_HISTORY_MEGARECORD_BYTES]=0;
}
static int megarecord_test(libzfs_handle_t *h,zfs_handle_t *fs,const char *pool_name)
{
    char *expected=malloc(OZ_HISTORY_MEGARECORD_BYTES+1);int error=!expected;
    if(!error)megarecord_fill(expected);
    if(!error&&(zfs_prop_set(fs,"test:history-megarecord","armed")||
        zpool_log_history(h,expected)))error=1;
    zpool_handle_t *pool=!error?zpool_open(h,pool_name):NULL;
    if(!error&&!pool)error=1;
    uint64_t offset=0;boolean_t eof=B_FALSE;unsigned found=0,batches=0;
    while(pool&&!eof&&!error) {
        nvlist_t *history=NULL;
        if(zpool_get_history(pool,&history,&offset,&eof)){error=1;break;}
        nvlist_t **records=NULL;uint_t count=0;
        if(!nvlist_lookup_nvlist_array(history,ZPOOL_HIST_RECORD,&records,&count))
            for(unsigned i=0;i<count;i++) {
                const char *message=NULL;
                if(!nvlist_lookup_string(records[i],ZPOOL_HIST_CMD,&message)&&
                    strlen(message)==OZ_HISTORY_MEGARECORD_BYTES&&
                    !memcmp(message,expected,OZ_HISTORY_MEGARECORD_BYTES))found++;
            }
        nvlist_free(history);batches++;
        if(batches>128)error=1;
    }
    if(pool)zpool_close(pool);
    free(expected);
    if(found!=1)error=1;
    printf("RESULT native command history megarecord %s bytes=%u exact-record=%u libzfs-resize=%u batches=%u\n",
        error?"FAIL":"PASS",OZ_HISTORY_MEGARECORD_BYTES,found==1,!error,batches);
    return error;
}
#endif

static int raw_request(const struct oz_ticket *ticket,LONG fd,LONG history,
    unsigned command,const char *pool,nvlist_t *input,unsigned bad)
{
    char *packed=NULL;size_t packed_size=0;
    int e=nvlist_pack(input,&packed,&packed_size,NV_ENCODE_NATIVE,0);
    if(e)return e;
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+packed_size;
    struct oz_ioctl_wire *w=calloc(1,size);
    if(!w){free(packed);return ENOMEM;}
    w->session=*ticket;w->magic=OZ_IOCTL_WIRE_MAGIC;w->version=OZ_IOCTL_WIRE_VERSION;
    w->command_size=sizeof(zfs_cmd_t);w->command=command;w->descriptor=fd;
    w->history_descriptor=history;w->src_size=packed_size;
    zfs_cmd_t *c=(void *)(w+1);c->zc_nvlist_src_size=packed_size;
    if(pool)strlcpy(c->zc_name,pool,sizeof(c->zc_name));
    memcpy(c+1,packed,packed_size);free(packed);
    if(bad==1)strcpy(c->zc_name,"forbidden-pool-override");
    if(bad==2)c->zc_cookie=1;
    if(bad==3)c->zc_nvlist_src=1;
    if(bad==4)w->history_descriptor=INT_MAX;
    e=oz_ioctl_request(OZ_ADMISSION_PORT,w,size);
    if(!e)e=w->engine_error;
    free(w);return e;
}
static int raw_boundaries(const char *pool)
{
    struct oz_ticket a={0},b={0};LONG fa=-1,fb=-1;int opened_a=0,opened_b=0,error=0;
    nvlist_t *props=fnvlist_alloc(),*log=fnvlist_alloc();
    fnvlist_add_string(props,"test:history-wire","wire-admission");
    fnvlist_add_string(log,"message","history-wire-valid");
#define CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(!oz_session_open(OZ_ADMISSION_PORT,&a));opened_a=1;
    CHECK(!oz_session_open(OZ_ADMISSION_PORT,&b));opened_b=1;
    CHECK(!oz_control_open(OZ_ADMISSION_PORT,&a,&fa));
    CHECK(!oz_control_open(OZ_ADMISSION_PORT,&b,&fb));
    CHECK(!raw_request(&a,fa,fa,ZFS_IOC_SET_PROP,pool,props,0));
    CHECK(raw_request(&a,fa,0,ZFS_IOC_LOG_HISTORY,NULL,log,0)==EPERM);
    CHECK(raw_request(&b,fb,fb,ZFS_IOC_LOG_HISTORY,NULL,log,0)==EPERM);
    for(unsigned bad=1;bad<=4;bad++)
        CHECK(raw_request(&a,fa,fa,ZFS_IOC_LOG_HISTORY,NULL,log,bad)==(bad==4?EBADF:EINVAL));
    for(unsigned bad=0;bad<3;bad++) {
        nvlist_t *invalid=fnvlist_alloc();
        if(bad==1)fnvlist_add_uint64(invalid,"message",42);
        if(bad==2){fnvlist_add_string(invalid,"message","forbidden-schema-marker");fnvlist_add_string(invalid,"unknown-key","bad");}
        int rc=raw_request(&a,fa,fa,ZFS_IOC_LOG_HISTORY,NULL,invalid,0);
        fnvlist_free(invalid);CHECK(rc!=0);
    }
    CHECK(!raw_request(&a,fa,fa,ZFS_IOC_LOG_HISTORY,NULL,log,0));
    CHECK(raw_request(&a,fa,fa,ZFS_IOC_LOG_HISTORY,NULL,log,0)==EPERM);
    CHECK(!oz_control_close(OZ_ADMISSION_PORT,&a,fa));LONG stale=fa;fa=-1;
    CHECK(!oz_control_open(OZ_ADMISSION_PORT,&a,&fa));CHECK(fa!=stale);
    CHECK(raw_request(&a,fa,stale,ZFS_IOC_LOG_HISTORY,NULL,log,0)==EBADF);
done:
    if(fa>0&&oz_control_close(OZ_ADMISSION_PORT,&a,fa))error=error?error:1;
    if(fb>0&&oz_control_close(OZ_ADMISSION_PORT,&b,fb))error=error?error:1;
    if(opened_a&&oz_session_close(OZ_ADMISSION_PORT,&a))error=error?error:1;
    if(opened_b&&oz_session_close(OZ_ADMISSION_PORT,&b))error=error?error:1;
    fnvlist_free(props);fnvlist_free(log);
    printf("RESULT native command history wire %s line=%d malformed=7 contexts=2 stale=1 recovered=1\n",error?"FAIL":"PASS",error);
    return error;
#undef CHECK
}
static pthread_mutex_t lock=PTHREAD_MUTEX_INITIALIZER;
static pthread_cond_t changed=PTHREAD_COND_INITIALIZER;
static unsigned ready;
static int release_workers;
struct trial {const char *pool,*marker;int error;};
static int denied(libzfs_handle_t *h)
{
    errno=0;int rc=zpool_log_history(h,"history-forbidden-marker");
    return rc==-1&&errno==EPERM?0:1;
}
static void *worker(void *arg)
{
    struct trial *t=arg;
    libzfs_handle_t *h=libzfs_init();
    t->error=!h;
    if(h&&denied(h))t->error=1;
    if(h&&t->pool) {
        zfs_handle_t *fs=zfs_open(h,t->pool,ZFS_TYPE_FILESYSTEM);
        if(!fs)t->error=1;
        else {if(zfs_prop_set(fs,"test:history-thread",t->marker))t->error=1;zfs_close(fs);}
    }
    if(t->pool) {
        pthread_mutex_lock(&lock);++ready;pthread_cond_broadcast(&changed);
        while(!release_workers)pthread_cond_wait(&changed,&lock);
        pthread_mutex_unlock(&lock);
        if(h&&!t->error&&zpool_log_history(h,t->marker))t->error=1;
        if(h&&denied(h))t->error=1;
    }
    if(h)libzfs_fini(h);
    return NULL;
}
int main(int argc,char **argv)
{
    if(argc!=3)return 20;
    /* Establish the process-owned session before starting pthread callers. */
    libzfs_handle_t *h=libzfs_init();if(!h)return 20;g_zfs=h;
    int error=denied(h);
    struct trial t[2]={{argv[1],"history-thread-alpha",0},{argv[2],"history-thread-beta",0}};
    pthread_t threads[2];unsigned started=0;
    for(;started<2;started++)if(pthread_create(&threads[started],NULL,worker,&t[started])){error=1;break;}
    pthread_mutex_lock(&lock);
    while(ready<started)pthread_cond_wait(&changed,&lock);
    release_workers=1;pthread_cond_broadcast(&changed);pthread_mutex_unlock(&lock);
    for(unsigned i=0;i<started;i++){if(pthread_join(threads[i],NULL)||t[i].error)error=1;}
    /* More sequential clients than the 64-slot table: exit must reclaim each
     * thread's context while the main handle keeps the session alive. */
    for(unsigned i=0;i<72&&!error;i++) {
        struct trial empty={0};pthread_t thread;
        if(pthread_create(&thread,NULL,worker,&empty))error=1;
        else if(pthread_join(thread,NULL)||empty.error)error=1;
    }
    if(denied(h))error=1;
    if(raw_boundaries(argv[1]))error=1;
    /* Exceed the legacy 4096-byte export string cap without bypassing the
     * production client bridge. Verify the exact record through libzfs. */
    char *long_record=malloc(6001);
    zfs_handle_t *fs=zfs_open(h,argv[1],ZFS_TYPE_FILESYSTEM);
    if(!long_record||!fs)error=1;
    else {
        memset(long_record,'L',6000);memcpy(long_record,"history-long-record-",20);long_record[6000]=0;
        if(zfs_prop_set(fs,"test:history-long","ready")||zpool_log_history(h,long_record))error=1;
        zpool_handle_t *pool=zpool_open(h,argv[1]);
        int found=0;
        if(!pool)error=1;
        else {
            uint64_t offset=0;boolean_t eof=B_FALSE;
            while(!eof&&!error) {
                nvlist_t *history=NULL;
                if(zpool_get_history(pool,&history,&offset,&eof)){error=1;break;}
                nvlist_t **records=NULL;uint_t count=0;
                if(!nvlist_lookup_nvlist_array(history,ZPOOL_HIST_RECORD,&records,&count))
                    for(unsigned i=0;i<count;i++) {
                        const char *message=NULL;
                        if(!nvlist_lookup_string(records[i],ZPOOL_HIST_CMD,&message)&&!strcmp(message,long_record))found++;
                    }
                nvlist_free(history);
            }
            zpool_close(pool);
        }
        if(found!=1||denied(h))error=1;
    }
    free(long_record);
#ifdef OZ_TEST_HISTORY_MEGARECORD
    if(fs&&megarecord_test(h,fs,argv[1]))error=1;
#endif
    if(fs)zfs_close(fs);
    printf("RESULT native command history long %s bytes=6000 exact-record=1\n",error?"FAIL":"PASS");
    libzfs_fini(h);
    /* Main-thread TLS must not retain a handle across connection generations. */
    h=libzfs_init();if(!h)return 20;g_zfs=h;
    if(denied(h))error=1;
    libzfs_fini(h);
    printf("RESULT native command history isolation %s threads=2 recycled=72 single-use=1 reconnect=1\n",error?"FAIL":"PASS");
    return error?20:0;
}
