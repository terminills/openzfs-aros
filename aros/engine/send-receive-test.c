/* SPDX-License-Identifier: CDDL-1.0 */
/* Private native engine proof. No unauthenticated management operation added. */
#include <sys/zfs_context.h>
#include <sys/dmu_send.h>
#include <sys/zfs_ioctl.h>
#include <sys/dmu_recv.h>
#include <sys/dsl_dataset.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_destroy.h>
#include <sys/dsl_pool.h>
#include <sys/zap.h>
#include <zfs_deleg.h>
#include <sys/zfs_file.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <libraries/security.h>
#include "zpl-fixture.h"
#include "stream-file.h"
#include "packet-cred.h"
#include "replication.h"
#include "../ui/api.h"
int oz_user_property_change(struct oz_message *,cred_t *);
int oz_packet_reader_work_test(int (*)(const struct oz_packet_security *,void *),void *);

static const char *source="qemu_mirror/stream-source";
static const char *received="qemu_mirror/stream-received";
static const char revised[]="Native incremental OpenZFS stream: preserved across a cold AROS boot.\n";

static void report(const char *stage,int error)
{
    char line[192];snprintf(line,sizeof(line),"Native send/receive %s errno=%d\n",stage,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
}
static int snapshot(const char *name)
{
    nvlist_t *snaps=fnvlist_alloc(),*errors=fnvlist_alloc();
    fnvlist_add_boolean(snaps,name);
    int error=dsl_dataset_snapshot(snaps,NULL,errors);
    fnvlist_free(snaps);fnvlist_free(errors);return error;
}
static int native_output(void *arg,const void *bytes,size_t length)
{
    const char *at=bytes;
    while(length){
        size_t actual=0;
        int error=oz_stream_file_write(arg,at,length,&actual);
        if(error)return error;
        if(!actual||actual>length)return EIO;
        at+=actual;length-=actual;
    }
    return 0;
}
static int failed_output(void *arg,const void *bytes,size_t length)
{(void)arg;(void)bytes;(void)length;return ENOSPC;}
struct native_send_request {const char *source,*from;};
static int send_grant(int remove)
{
    nvlist_t *acl=fnvlist_alloc(),*perms=fnvlist_alloc();
    fnvlist_add_boolean(perms,"send");fnvlist_add_nvlist(acl,"gd$99",perms);
    int error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(source,acl,remove);
    fnvlist_free(perms);fnvlist_free(acl);return error;
}
static int native_send(const struct oz_packet_security *caller,void *arg)
{
    struct native_send_request *request=arg;
    cred_t *cred=oz_packet_security_cred(caller),*previous=CRED();
    const char *path="RAM:OpenZFS-native-stream.bin";
    struct oz_stream_file *stream=NULL;BPTR lock=0;
    int error=0,granted=0;
    if(oz_send_stream(request->source,request->from,cred,failed_output,NULL)!=EPERM)error=EIO;
    if(!error){error=send_grant(0);if(!error)granted=1;}
    if(!error&&oz_send_stream(request->source,request->from,cred,failed_output,NULL)!=ENOSPC)error=EIO;
    if(!error&&(!SetOwner((CONST_STRPTR)path,(55UL<<16)|99)||
        !SetProtection((CONST_STRPTR)path,FIBF_GRP_WRITE)))error=EIO;
    if(!error){lock=Lock((CONST_STRPTR)path,ACCESS_READ);if(!lock)error=EIO;}
    if(!error)error=oz_stream_file_open(caller,&lock,secAt_Write,&stream);
    if(!error&&oz_send_stream(request->source,request->from,NULL,native_output,stream)!=EACCES)error=EIO;
    if(!error)error=oz_send_stream(request->source,request->from,cred,native_output,stream);
    if(CRED()!=previous&&!error)error=EIO;
    if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
    if(lock)UnLock(lock);
    if(granted){int rc=send_grant(1);if(!error)error=rc;}
    if(!error&&oz_send_perms(request->source,B_FALSE,cred)!=EPERM)error=EIO;
    report("authorized native handle send",error);return error;
}
static int retain_stream(const char *path,const char *name)
{
    struct oz_zpl *fs=NULL;zfs_file_t *file=NULL;
    struct oz_zpl_info info;uint64_t offset=0;char bytes[4096];
    int error=oz_zpl_open_writable("qemu_mirror/stream-artifacts",&fs);
    if(!error)error=oz_zpl_create_as(fs,oz_zpl_root(fs),name,S_IFREG|0600,kcred,&info);
    if(!error)error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
    while(!error) {
        ssize_t residual=sizeof(bytes);size_t written=0;
        error=zfs_file_read(file,bytes,sizeof(bytes),&residual);if(error)break;
        size_t count=sizeof(bytes)-residual;if(!count)break;
        error=oz_zpl_write_as(fs,info.object,offset,bytes,count,&written,kcred);
        if(!error&&written!=count)error=EIO;
        offset+=written;
    }
    if(!error)error=oz_zpl_sync(fs);
    if(file)zfs_file_close(file);
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    report("retain exact stream",error);return error;
}
static int native_reader(void *arg,void *buffer,size_t length,size_t *actual)
{
    return oz_stream_file_read(arg,buffer,length,actual);
}
struct native_receive_request {
    const char *dataset,*snapshot,*path;
    int expected,observed;
};
static int receive_grant(int remove)
{
    nvlist_t *acl=fnvlist_alloc(),*perms=fnvlist_alloc();
    fnvlist_add_boolean(perms,"receive");
    fnvlist_add_boolean(perms,"mount");
    fnvlist_add_boolean(perms,"create");
    fnvlist_add_nvlist(acl,"ul$77",perms);
    fnvlist_add_nvlist(acl,"ud$77",perms);
    int error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set("qemu_mirror",acl,remove);
    fnvlist_free(perms);fnvlist_free(acl);return error;
}
static int native_receive(const struct oz_packet_security *caller,void *arg)
{
    struct native_receive_request *request=arg;
    const char *path=request->path;
    BPTR lock=0;struct oz_stream_file *stream=NULL;zfs_file_t *input=NULL;
    /* Test setup by the privileged fixture owner; the stream is group-readable
     * to the captured UID77/GID99 reader, not owned by that reader. */
    cred_t *cred=oz_packet_security_cred(caller);
    int granted=0,error=0;
    if(oz_receive_perms(request->dataset,B_FALSE,cred)!=EPERM)error=EIO;
    if(!error){error=receive_grant(0);if(!error)granted=1;}
    if(!error)error=oz_receive_perms(request->dataset,B_FALSE,cred);
    if(!error&&(!SetOwner((CONST_STRPTR)path,(55UL<<16)|99)||
        !SetProtection((CONST_STRPTR)path,FIBF_GRP_READ)))error=EIO;
    if(!error){lock=Lock((CONST_STRPTR)path,ACCESS_READ);if(!lock)error=EIO;}
    if(!error)error=oz_stream_file_open(caller,&lock,secAt_Read,&stream);
    if(!error)error=zfs_file_from_reader(native_reader,stream,&input);
    if(!error){
        cred_t *other=oz_cred_create(78,78,88,0,NULL);
        if(!other)error=ENOMEM;
        else {
            if(oz_receive_stream(request->dataset,request->snapshot,input,NULL)!=EACCES||
                oz_receive_stream(request->dataset,"bad/name",input,cred)!=EINVAL||
                oz_receive_stream(request->dataset,request->snapshot,input,other)!=EPERM)
                error=EIO;
            crfree(other);
        }
        /* The subsequent real receive also proves refused calls consumed no
         * header bytes from this same borrowed stream. */
    }
    cred_t *previous=CRED();
    if(!error){
        request->observed=oz_receive_stream(request->dataset,request->snapshot,input,cred);
        if(request->observed!=request->expected)
            error=request->observed?request->observed:EIO;
    }
    if(CRED()!=previous&&!error)error=EIO;
    if(input)zfs_file_close(input); /* Wrapper borrows the native handle. */
    if(stream){int rc=oz_stream_file_close(stream);if(!error)error=rc;}
    if(lock)UnLock(lock);
    if(granted){int rc=receive_grant(1);if(!error)error=rc;}
    if(!error&&oz_receive_perms(request->dataset,B_FALSE,cred)!=EPERM)error=EIO;
    report("authorized native handle receive",error);return error;
}
static int interrupted_receive(const char *path)
{
    const char *partial="/RAM/OpenZFS-truncated-stream.bin";
    const char *retry="qemu_mirror/stream-retry";
    zfs_file_t *in=NULL,*out=NULL;zfs_file_attr_t attr;char bytes[4096];
    int error=zfs_file_open(path,O_RDONLY,0,kcred,&in);
    if(!error)error=zfs_file_getattr(in,&attr);
    if(!error&&attr.zfa_size<8192)error=EINVAL;
    if(!error)error=zfs_file_open(partial,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&out);
    uint64_t remaining=error?0:(attr.zfa_size/2)&~7ULL;
    while(!error&&remaining) {
        size_t n=MIN(remaining,sizeof(bytes));
        error=zfs_file_read(in,bytes,n,NULL);
        if(!error)error=zfs_file_write(out,bytes,n,NULL);
        remaining-=n;
    }
    if(in)zfs_file_close(in);in=NULL;
    if(out)zfs_file_close(out);
    if(!error) {
        struct native_receive_request request={retry,"base",
            "RAM:OpenZFS-truncated-stream.bin",ZFS_ERR_STREAM_TRUNCATED,0};
        error=oz_packet_reader_work_test(native_receive,&request);
        report("truncated receive observed",request.observed);
    }
    if(!error) {
        objset_t *os=NULL;int found=dmu_objset_hold(retry,FTAG,&os);
        if(!found)dmu_objset_rele(os,FTAG);
        if(found!=ENOENT)error=EIO;
    }
    if(!error){
        struct native_receive_request request={retry,"base",
            "RAM:OpenZFS-native-stream.bin",0,0};
        error=oz_packet_reader_work_test(native_receive,&request);
    }
    if(!error&&unlink(partial))error=errno;
    report("truncated cleanup and full retry",error);return error;
}
static int roundtrip(const char *snap,const char *from,const char *to)
{
    const char *path="/RAM/OpenZFS-native-stream.bin";
    zfs_file_t *file=NULL;
    /* A fresh private guest owns this test-only RAM path. O_EXCL prevents
     * replacement of an existing object even within the disposable guest. */
    int error=zfs_file_open(path,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
    if(error)return error;
    zfs_file_close(file);file=NULL;
    struct native_send_request send={snap,from};
    error=oz_packet_reader_work_test(native_send,&send);
    report(from?"incremental send":"full send",error);
    if(error)return error; /* Keep failed stream for diagnosis. */
    struct native_receive_request request={received,to,"RAM:OpenZFS-native-stream.bin",0,0};
    error=oz_packet_reader_work_test(native_receive,&request);
    if(!error&&!from)error=interrupted_receive(path);
    if(!error)error=retain_stream(path,from?"incremental.zfs":"full.zfs");
    if(!error&&unlink(path))error=errno;
    return error;
}
static int compare_file(const char *left,const char *right,const char *name)
{
    struct oz_zpl *a=NULL,*b=NULL;
    struct oz_zpl_info ai,bi;char ab[256],bb[256];
    int error=oz_zpl_open(left,&a);
    if(!error)error=oz_zpl_open(right,&b);
    if(!error)error=oz_zpl_resolve(a,oz_zpl_root(a),name,&ai);
    if(!error)error=oz_zpl_resolve(b,oz_zpl_root(b),name,&bi);
    if(!error&&(ai.size!=bi.size||ai.size>sizeof(ab)||!ai.size))error=EIO;
    if(!error)error=oz_zpl_read(a,ai.object,0,ab,ai.size);
    if(!error)error=oz_zpl_read(b,bi.object,0,bb,bi.size);
    if(!error&&memcmp(ab,bb,ai.size))error=EIO;
    if(b){int rc=oz_zpl_close(b);if(!error)error=rc;}
    if(a){int rc=oz_zpl_close(a);if(!error)error=rc;}
    return error;
}
int oz_send_receive_test(int write)
{
    int error=0;
    if(write) {
        error=oz_zpl_fixture_create("qemu_mirror/stream-artifacts");if(error)goto done;
        error=oz_zpl_fixture_create(source);if(error)goto done;
        error=snapshot("qemu_mirror/stream-source@base");if(error)goto done;
        error=roundtrip("qemu_mirror/stream-source@base",NULL,"base");if(error)goto done;
        error=compare_file(source,received,"hello.txt");if(error)goto done;
        error=compare_file(source,received,"subdir/nested.txt");if(error)goto done;
        struct oz_zpl *fs=NULL;uint64_t object,size;size_t written=0;
        error=oz_zpl_open_writable(source,&fs);
        if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
        if(!error)error=oz_zpl_write_as(fs,object,0,revised,sizeof(revised)-1,&written,kcred);
        if(!error&&written!=sizeof(revised)-1)error=EIO;
        if(!error)error=oz_zpl_resize_as(fs,object,sizeof(revised)-1,kcred);
        if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
        if(error)goto done;
        error=snapshot("qemu_mirror/stream-source@second");if(error)goto done;
        error=roundtrip("qemu_mirror/stream-source@second","qemu_mirror/stream-source@base","second");
        if(error)goto done;
    }
    error=compare_file(source,received,"hello.txt");
    if(!error)error=compare_file(source,received,"subdir/nested.txt");
    /* Snapshot data survives and is independent of the changed live head. */
    if(!error)error=compare_file("qemu_mirror/stream-source@base","qemu_mirror/stream-received@base","hello.txt");
    if(!error)error=compare_file("qemu_mirror/stream-source@base","qemu_mirror/stream-retry","hello.txt");
    if(!error)error=compare_file("qemu_mirror/stream-source@second","qemu_mirror/stream-received@second","hello.txt");
    if(!error) {
        struct oz_zpl *fs=NULL;uint64_t object,size;char bytes[sizeof(revised)];
        error=oz_zpl_open(received,&fs);
        if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
        if(!error&&size!=sizeof(revised)-1)error=EIO;
        if(!error)error=oz_zpl_read(fs,object,0,bytes,size);
        if(!error&&memcmp(bytes,revised,sizeof(revised)-1))error=EIO;
        if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    }
done:
    report(write?"WRITE full+incremental":"VERIFY cold full+incremental",error);
    return error;
}

/* Representation proof, separate from the ordinary replication fixture. */
static unsigned char advanced_byte(size_t at)
{ return (unsigned char)(((at%4096)*73+((at%4096)>>3)*19+1)&255); }
static int advanced_content(const char *dataset)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;unsigned char bytes[4096];
    int error=oz_zpl_open(dataset,&fs);
    if(!error)error=oz_zpl_resolve(fs,oz_zpl_root(fs),"large.bin",&info);
    if(!error&&info.size!=1048576)error=EIO;
    for(size_t at=0;!error&&at<1048576;at+=sizeof(bytes)){
        error=oz_zpl_read(fs,info.object,at,bytes,sizeof(bytes));
        for(size_t i=0;!error&&i<sizeof(bytes);i++)
            if(bytes[i]!=advanced_byte(at+i))error=EIO;
    }
    if(!error)error=oz_zpl_resolve(fs,oz_zpl_root(fs),"embedded.bin",&info);
    if(!error&&info.size!=sizeof(bytes))error=EIO;
    if(!error)error=oz_zpl_read(fs,info.object,0,bytes,sizeof(bytes));
    for(size_t i=0;!error&&i<sizeof(bytes);i++)if(bytes[i]!='A')error=EIO;
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    return error;
}
static int advanced_writer(void *arg,const void *bytes,size_t size)
{ return zfs_file_write(arg,bytes,size,NULL); }
static int advanced_records(const char *path,int advanced)
{
    zfs_file_t *file=NULL;int error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
    unsigned compressed=0,large=0,embedded=0,ended=0,records=0;
    while(!error&&!ended){
        dmu_replay_record_t r;uint64_t payload=0;
        error=zfs_file_read(file,&r,sizeof(r),NULL);if(error)break;
        if(++records>100000){error=EOVERFLOW;break;}
        switch(r.drr_type){
        case DRR_BEGIN: payload=r.drr_payloadlen;break;
        case DRR_OBJECT: payload=DRR_OBJECT_PAYLOAD_SIZE(&r.drr_u.drr_object);break;
        case DRR_WRITE:
            compressed+=!!DRR_WRITE_COMPRESSED(&r.drr_u.drr_write);
            large+=r.drr_u.drr_write.drr_logical_size>131072;
            payload=DRR_WRITE_PAYLOAD_SIZE(&r.drr_u.drr_write);break;
        case DRR_WRITE_EMBEDDED:
            embedded++;payload=P2ROUNDUP(r.drr_u.drr_write_embedded.drr_psize,8);break;
        case DRR_SPILL:payload=DRR_SPILL_PAYLOAD_SIZE(&r.drr_u.drr_spill);break;
        case DRR_FREE:case DRR_FREEOBJECTS:break;
        case DRR_END:ended=1;break;
        default:error=EINVAL;break;
        }
        if(payload>16777216){error=EOVERFLOW;break;}
        char scratch[4096];
        while(!error&&payload){size_t n=MIN(payload,sizeof(scratch));
            error=zfs_file_read(file,scratch,n,NULL);payload-=n;}
    }
    if(!error){char extra;ssize_t resid=1;error=zfs_file_read(file,&extra,1,&resid);
        if(!error&&resid!=1)error=EIO;}
    if(!error&&(advanced?(!compressed||!large||!embedded):(compressed||large||embedded)))error=EIO;
    if(file)zfs_file_close(file);
    char line[192];snprintf(line,sizeof(line),
        "Native stream representations advanced=%d compressed=%u large=%u embedded=%u errno=%d\n",
        advanced,compressed,large,embedded,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
}
int oz_advanced_stream_test(int write)
{
    const char *dataset="qemu_mirror/stream-advanced";
    int error=0;
    if(write){
        /* Explicit feature upgrade only on disposable private fixture disks. */
        spa_t *spa=NULL;error=spa_open("qemu_mirror",&spa,FTAG);
        if(!error){nvlist_t *props=fnvlist_alloc();
            fnvlist_add_uint64(props,"feature@embedded_data",0);
            error=spa_prop_set(spa,props);fnvlist_free(props);spa_close(spa,FTAG);}
        if(!error)error=oz_zpl_fixture_create(dataset);
        struct oz_message *m=calloc(1,sizeof(*m));
        if(!m&&!error)error=ENOMEM;
        if(!error){m->operation=41;strcpy(m->target,dataset);
            strcpy(m->argument,"compression=lz4");error=oz_user_property_change(m,kcred);
            if(!error){strcpy(m->argument,"recordsize=1M");error=oz_user_property_change(m,kcred);}}
        free(m);
        struct oz_zpl *fs=NULL;struct oz_zpl_info info;
        unsigned char *bytes=malloc(1048576);if(!bytes&&!error)error=ENOMEM;
        if(!error)error=oz_zpl_open_writable(dataset,&fs);
        if(!error)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"large.bin",S_IFREG|0600,kcred,&info);
        if(!error){for(size_t i=0;i<1048576;i++)bytes[i]=advanced_byte(i);
            size_t actual=0;error=oz_zpl_write_as(fs,info.object,0,bytes,1048576,&actual,kcred);
            if(!error&&actual!=1048576)error=EIO;}
        if(!error)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"embedded.bin",S_IFREG|0600,kcred,&info);
        if(!error){memset(bytes,'A',4096);size_t actual=0;
            error=oz_zpl_write_as(fs,info.object,0,bytes,4096,&actual,kcred);
            if(!error&&actual!=4096)error=EIO;}
        if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}free(bytes);
        if(!error)error=snapshot("qemu_mirror/stream-advanced@base");
        for(unsigned mode=0;!error&&mode<2;mode++){
            const char *path=mode?"/RAM/advanced.zfs":"/RAM/plain.zfs";
            zfs_file_t *file=NULL;
            error=zfs_file_open(path,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
            if(!error)error=oz_send_stream_flags("qemu_mirror/stream-advanced@base",NULL,
                mode?OZ_SEND_REPRESENTATION:0,kcred,advanced_writer,file);
            if(file)zfs_file_close(file);file=NULL;
            if(!error)error=advanced_records(path,mode);
            if(!error)error=retain_stream(path,mode?"advanced.zfs":"plain.zfs");
            if(!error)error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
            if(!error)error=oz_receive_stream(mode?"qemu_mirror/advanced-received":
                "qemu_mirror/plain-received","base",file,kcred);
            if(file)zfs_file_close(file);
            if(!error&&unlink(path))error=errno;
        }
    }
    if(!error)error=advanced_content(dataset);
    if(!error)error=advanced_content("qemu_mirror/advanced-received");
    if(!error)error=advanced_content("qemu_mirror/plain-received");
    report(write?"WRITE advanced representations":"VERIFY cold advanced representations",error);
    return error;
}

int oz_bookmark_stream_test(int write)
{
    const char *dataset="qemu_mirror/stream-advanced";
    const char *base="qemu_mirror/stream-advanced@base";
    const char *mark="qemu_mirror/stream-advanced#base";
    const char *second="qemu_mirror/stream-advanced@second";
    const char *path="/RAM/bookmark.zfs";
    int error=0;
    if(write){
        nvlist_t *marks=fnvlist_alloc(),*errors=fnvlist_alloc();
        fnvlist_add_string(marks,mark,base);
        error=dsl_bookmark_create(marks,errors);
        report("bookmark create",error);
        fnvlist_free(marks);fnvlist_free(errors);
        struct oz_zpl *fs=NULL;uint64_t object,size;size_t actual=0;
        if(!error)error=oz_zpl_open_writable(dataset,&fs);
        if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
        if(!error)error=oz_zpl_write_as(fs,object,0,revised,sizeof(revised)-1,&actual,kcred);
        if(!error&&actual!=sizeof(revised)-1)error=EIO;
        if(!error)error=oz_zpl_resize_as(fs,object,sizeof(revised)-1,kcred);
        if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
        if(!error)error=snapshot(second);
        /* The actual source snapshot is gone before send: success cannot be
         * explained by silently using a still-existing snapshot as the base. */
        if(!error)error=dsl_destroy_snapshot(base,B_FALSE);
        report("bookmark source snapshot removed",error);
        if(!error){int missing=oz_send_stream_flags(second,"qemu_mirror/stream-advanced#missing",
            OZ_SEND_REPRESENTATION,kcred,failed_output,NULL);
            report("missing bookmark observed",missing);
            /* Upstream distinguishes missing bookmark (ESRCH) from missing
             * containing dataset (ENOENT); do not flatten that contract. */
            if(missing!=ESRCH)error=EIO;}
        zfs_file_t *file=NULL;
        if(!error)error=zfs_file_open(path,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
        if(!error)error=oz_send_stream_flags(second,mark,OZ_SEND_REPRESENTATION,kcred,advanced_writer,file);
        report("bookmark incremental send",error);
        if(file)zfs_file_close(file);file=NULL;
        if(!error)error=retain_stream(path,"bookmark.zfs");
        if(!error)error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
        if(!error)error=oz_receive_stream("qemu_mirror/advanced-received","second",file,kcred);
        report("bookmark incremental receive",error);
        if(file)zfs_file_close(file);
        if(!error&&unlink(path))error=errno;
    }
    objset_t *os=NULL;
    if(!error){int found=dmu_objset_hold(base,FTAG,&os);
        if(!found)dmu_objset_rele(os,FTAG);
        if(found!=ENOENT)error=EIO;}
    nvlist_t *props=fnvlist_alloc(),*marks=fnvlist_alloc();
    fnvlist_add_boolean(props,"guid");
    if(!error)error=dsl_get_bookmarks(dataset,props,marks);
    report("bookmark cold enumeration",error);
    if(!error&&!nvlist_exists(marks,"base"))error=EIO;
    fnvlist_free(props);fnvlist_free(marks);
    if(!error)error=compare_file(second,"qemu_mirror/advanced-received@second","hello.txt");
    if(!error){struct oz_zpl *fs=NULL;uint64_t object,size;char bytes[sizeof(revised)];
        error=oz_zpl_open("qemu_mirror/advanced-received",&fs);
        if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
        if(!error&&size!=sizeof(revised)-1)error=EIO;
        if(!error)error=oz_zpl_read(fs,object,0,bytes,size);
        if(!error&&memcmp(bytes,revised,sizeof(revised)-1))error=EIO;
        if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}}
    report(write?"WRITE bookmark after snapshot deletion":"VERIFY cold bookmark after snapshot deletion",error);
    return error;
}

struct interrupted_input {zfs_file_t *file;uint64_t remaining;};
static int interrupted_reader(void *arg,void *buffer,size_t length,size_t *actual)
{
    struct interrupted_input *in=arg;
    length=MIN(length,in->remaining);*actual=0;
    if(!length)return 0;
    ssize_t residual=length;
    int error=zfs_file_read(in->file,buffer,length,&residual);
    if(!error){*actual=length-residual;in->remaining-=*actual;}
    return error;
}
static int resume_state(const char *name,uint64_t *object,uint64_t *offset,int expected)
{
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
    int error=dsl_pool_hold(name,FTAG,&dp);
    if(!error)error=dsl_dataset_hold(dp,name,FTAG,&ds);
    if(!error){
        int state=dsl_dataset_has_resume_receive_state(ds);
        if(!!state!=!!expected)error=EIO;
        if(!error&&expected){uint64_t bytes=0;
            error=zap_lookup(dp->dp_meta_objset,ds->ds_object,DS_FIELD_RESUME_OBJECT,8,1,object);
            if(!error)error=zap_lookup(dp->dp_meta_objset,ds->ds_object,DS_FIELD_RESUME_OFFSET,8,1,offset);
            if(!error)error=zap_lookup(dp->dp_meta_objset,ds->ds_object,DS_FIELD_RESUME_BYTES,8,1,&bytes);
            if(!error&&(!*object||!bytes))error=EIO;
            nvlist_t *stats=fnvlist_alloc(),*entry=NULL;const char *token=NULL;
            if(!error)dsl_dataset_stats(ds,stats);
            if(!error)error=nvlist_lookup_nvlist(stats,"receive_resume_token",&entry);
            if(!error)error=nvlist_lookup_string(entry,"value",&token);
            if(!error&&strncmp(token,"1-",2))error=EIO;
            char line[192];snprintf(line,sizeof(line),
                "Native resume state object=%llu offset=%llu bytes=%llu token_length=%lu errno=%d\n",
                (unsigned long long)*object,(unsigned long long)*offset,(unsigned long long)bytes,
                token?(unsigned long)strlen(token):0,error);
            for(char *p=line;*p;p++)RawPutChar(*p);
            fnvlist_free(stats);
        }
        dsl_dataset_rele(ds,FTAG);
    }
    if(dp)dsl_pool_rele(dp,FTAG);
    return error;
}
static int resume_test_output(objset_t *os,void *bytes,int length,void *arg)
{(void)os;return length<0?EINVAL:advanced_writer(arg,bytes,(size_t)length);}
int oz_resumable_stream_test(int write)
{
    const char *source="qemu_mirror/stream-advanced@second";
    const char *complete="qemu_mirror/resumed-received",*pending="qemu_mirror/resume-pending";
    int error=0;uint64_t object=0,offset=0;
    if(write){
        const char *full="/RAM/resumable-full.zfs",*rest="/RAM/resumable-rest.zfs";
        zfs_file_t *file=NULL;
        error=zfs_file_open(full,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
        if(!error)error=oz_send_stream(source,NULL,kcred,advanced_writer,file);
        if(file)zfs_file_close(file);file=NULL;
        for(unsigned i=0;!error&&i<3;i++){
            const char *target=i==2?"qemu_mirror/resume-api-pending":i?pending:complete;zfs_file_t *input=NULL;
            error=zfs_file_open(full,O_RDONLY,0,kcred,&file);
            struct interrupted_input limited={file,524288};
            if(!error)error=zfs_file_from_reader(interrupted_reader,&limited,&input);
            if(!error){int observed=oz_receive_stream_resumable(target,"second",input,kcred,1);
                report("resumable interrupted receive observed",observed);
                if(observed!=ZFS_ERR_STREAM_TRUNCATED)error=EIO;}
            if(input)zfs_file_close(input);
            if(file)zfs_file_close(file);file=NULL;
            if(!error)error=resume_state(target,&object,&offset,1);
            if(error)break;
            if(i)continue;
            error=zfs_file_open(rest,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
            if(!error){
                /* Fixture-only upstream continuation. Public send never accepts
                 * arbitrary resume offsets in lieu of validating a token. */
                dmu_send_outparams_t params={0};
                /* The upstream callback uses int length, unlike native writer. */
                params.dso_outfunc=resume_test_output;params.dso_arg=file;
                offset_t outoff=0;
                error=dmu_send(source,NULL,B_FALSE,B_FALSE,B_FALSE,B_FALSE,B_FALSE,
                    object,offset,NULL,-1,&outoff,&params);}
            if(file)zfs_file_close(file);file=NULL;
            if(!error)error=retain_stream(rest,"resumed.zfs");
            if(!error)error=zfs_file_open(rest,O_RDONLY,0,kcred,&file);
            if(!error)error=oz_receive_stream_resumable(target,"second",file,kcred,1);
            if(file)zfs_file_close(file);file=NULL;
            if(!error)error=resume_state(target,&object,&offset,0);
        }
        if(!error&&(unlink(full)||unlink(rest)))error=errno;
    }
    if(!error)error=resume_state(pending,&object,&offset,1);
    if(!error)error=resume_state(complete,&object,&offset,0);
    if(!error)error=advanced_content(complete);
    if(!error)error=compare_file(source,complete,"hello.txt");
    /* The focused EXEC client never calls own_send_api_test(), which resumes
     * this third receive. Its cold contract is preserved pending state, not
     * the completed receive required by the full admission client. */
    int api_pending=getenv("OPENZFS_EXEC_ONLY")!=NULL;
    if(!error&&!write)error=resume_state("qemu_mirror/resume-api-pending",&object,&offset,api_pending);
    if(!error&&!write&&!api_pending)error=advanced_content("qemu_mirror/resume-api-pending");
    if(!error&&!write&&!api_pending)error=compare_file(source,"qemu_mirror/resume-api-pending","hello.txt");
    report(write?"WRITE resumable receive":"VERIFY cold resumable receive",error);
    return error;
}
int oz_cold_resume_test(void)
{
    const char *target="qemu_mirror/resume-pending";
    const char *source="qemu_mirror/stream-advanced@second";
    const char *path="/RAM/cold-resumed.zfs";
    uint64_t object=0,offset=0;
    int error=resume_state(target,&object,&offset,1);
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
    nvlist_t *stats=fnvlist_alloc(),*entry=NULL;const char *token=NULL;
    if(!error)error=dsl_pool_hold(target,FTAG,&dp);
    if(!error)error=dsl_dataset_hold(dp,target,FTAG,&ds);
    if(!error)dsl_dataset_stats(ds,stats);
    if(ds)dsl_dataset_rele(ds,FTAG);
    if(dp)dsl_pool_rele(dp,FTAG);
    if(!error)error=nvlist_lookup_nvlist(stats,"receive_resume_token",&entry);
    if(!error)error=nvlist_lookup_string(entry,"value",&token);
    struct oz_resume_send prepared;
    if(!error)error=oz_send_resume_prepare(source,NULL,token,kcred,&prepared);
    if(!error&&(prepared.object!=object||prepared.offset!=offset))error=EIO;
    zfs_file_t *file=NULL;
    if(!error)error=zfs_file_open(path,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
    if(!error)error=oz_send_stream_token(source,NULL,token,kcred,advanced_writer,file);
    if(file)zfs_file_close(file);file=NULL;
    fnvlist_free(stats);
    if(!error)error=retain_stream(path,"cold-resumed.zfs");
    if(!error)error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
    if(!error)error=oz_receive_stream_resumable(target,"second",file,kcred,1);
    if(file)zfs_file_close(file);
    if(!error)error=resume_state(target,&object,&offset,0);
    if(!error)error=advanced_content(target);
    if(!error)error=compare_file(source,target,"hello.txt");
    if(!error&&unlink(path))error=errno;
    report("cold token continuation completed",error);
    return error;
}

static int inc_payload(const char *dataset,int write,int fill)
{
    struct oz_zpl *fs=NULL;struct oz_zpl_info info;
    unsigned char *bytes=malloc(524288);if(!bytes)return ENOMEM;
    int error=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    if(!error){error=oz_zpl_resolve(fs,oz_zpl_root(fs),"payload.bin",&info);
        if(error==ENOENT&&write)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"payload.bin",S_IFREG|0600,kcred,&info);}
    if(!error&&write){size_t actual=0;memset(bytes,fill,524288);
        error=oz_zpl_write_as(fs,info.object,0,bytes,524288,&actual,kcred);
        if(!error&&actual!=524288)error=EIO;}
    if(!error&&!write){
        if(info.size!=524288)error=EIO;
        if(!error)error=oz_zpl_read(fs,info.object,0,bytes,524288);
        for(size_t i=0;!error&&i<524288;i++)if(bytes[i]!=fill)error=EIO;
    }
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    free(bytes);return error;
}
int oz_incremental_resume_test(int write)
{
    const char *source="qemu_mirror/inc-resume-source";
    const char *base="qemu_mirror/inc-resume-source@base";
    const char *next="qemu_mirror/inc-resume-source@next";
    const char *mark="qemu_mirror/inc-resume-source#base";
    const char *targets[]={"qemu_mirror/inc-resume-snapshot","qemu_mirror/inc-resume-bookmark"};
    const char *full="/RAM/inc-resume-base.zfs",*inc="/RAM/inc-resume-delta.zfs";
    int error=0;
    if(write){
        error=oz_zpl_fixture_create(source);
        if(!error)error=inc_payload(source,1,'A');
        if(!error)error=snapshot(base);
        nvlist_t *marks=fnvlist_alloc(),*errors=fnvlist_alloc();
        fnvlist_add_string(marks,mark,base);
        if(!error)error=dsl_bookmark_create(marks,errors);
        fnvlist_free(marks);fnvlist_free(errors);
        zfs_file_t *file=NULL;
        if(!error)error=zfs_file_open(full,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
        if(!error)error=oz_send_stream(base,NULL,kcred,advanced_writer,file);
        if(file)zfs_file_close(file);file=NULL;
        if(!error)error=retain_stream(full,"inc-resume-base.zfs");
        for(unsigned i=0;!error&&i<2;i++){
            error=zfs_file_open(full,O_RDONLY,0,kcred,&file);
            if(!error)error=oz_receive_stream(targets[i],"base",file,kcred);
            if(file)zfs_file_close(file);file=NULL;
        }
        if(!error)error=inc_payload(source,1,'B');
        if(!error)error=snapshot(next);
        if(!error)error=zfs_file_open(inc,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
        if(!error)error=oz_send_stream(next,base,kcred,advanced_writer,file);
        if(file)zfs_file_close(file);file=NULL;
        if(!error)error=retain_stream(inc,"inc-resume-delta.zfs");
        for(unsigned i=0;!error&&i<2;i++){
            const char *from=i?mark:base;
            if(i)error=dsl_destroy_snapshot(base,B_FALSE);
            zfs_file_t *input=NULL;
            if(!error)error=zfs_file_open(inc,O_RDONLY,0,kcred,&file);
            struct interrupted_input limited={file,300000};
            if(!error)error=zfs_file_from_reader(interrupted_reader,&limited,&input);
            if(!error){int observed=oz_receive_stream_resumable(targets[i],"next",input,kcred,1);
                report("incremental interrupted receive observed",observed);
                if(observed!=ZFS_ERR_STREAM_TRUNCATED)error=EIO;}
            if(input)zfs_file_close(input);
            if(file)zfs_file_close(file);file=NULL;
            char hidden[256];snprintf(hidden,sizeof(hidden),"%s/%%recv",targets[i]);
            uint64_t object=0,offset=0;
            if(!error)error=resume_state(hidden,&object,&offset,1);
            dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
            nvlist_t *stats=fnvlist_alloc(),*entry=NULL;const char *token=NULL;
            if(!error)error=dsl_pool_hold(targets[i],FTAG,&dp);
            if(!error)error=dsl_dataset_hold(dp,targets[i],FTAG,&ds);
            if(!error)dsl_dataset_stats(ds,stats); /* parent discovers %recv */
            if(ds)dsl_dataset_rele(ds,FTAG);
            if(dp)dsl_pool_rele(dp,FTAG);
            if(!error)error=nvlist_lookup_nvlist(stats,"receive_resume_token",&entry);
            if(!error)error=nvlist_lookup_string(entry,"value",&token);
            struct oz_resume_send prepared;
            if(!error&&oz_send_resume_prepare(next,NULL,token,kcred,&prepared)!=EINVAL)error=EIO;
            if(!error&&oz_send_resume_prepare(next,next,token,kcred,&prepared)!=ESTALE)error=EIO;
            if(!error)error=oz_send_resume_prepare(next,from,token,kcred,&prepared);
            if(!error&&(prepared.object!=object||prepared.offset!=offset))error=EIO;
            const char *path=i?"/RAM/inc-bookmark-rest.zfs":"/RAM/inc-snapshot-rest.zfs";
            if(!error)error=zfs_file_open(path,O_CREAT|O_EXCL|O_WRONLY,0600,kcred,&file);
            if(!error)error=oz_send_stream_token(next,from,token,kcred,advanced_writer,file);
            if(file)zfs_file_close(file);file=NULL;
            fnvlist_free(stats);
            if(!error)error=retain_stream(path,i?"inc-bookmark-rest.zfs":"inc-snapshot-rest.zfs");
            if(!error)error=zfs_file_open(path,O_RDONLY,0,kcred,&file);
            if(!error)error=oz_receive_stream_resumable(targets[i],"next",file,kcred,1);
            if(file)zfs_file_close(file);file=NULL;
            if(!error&&unlink(path))error=errno;
        }
        /* Abort a new-filesystem receive and an incremental receive. The
         * latter must destroy only %recv, preserving the old head and base. */
        const char *aborted[]={"qemu_mirror/abort-new","qemu_mirror/abort-incremental",
            "qemu_mirror/abort-api-new","qemu_mirror/abort-api-incremental"};
        for(unsigned i=0;!error&&i<4;i++){
            if(i&1){
                error=zfs_file_open(full,O_RDONLY,0,kcred,&file);
                if(!error)error=oz_receive_stream(aborted[i],"base",file,kcred);
                if(file)zfs_file_close(file);file=NULL;
                if(!error&&oz_receive_abort(aborted[i],kcred)!=EINVAL)error=EIO;
            }
            if(!error)error=zfs_file_open((i&1)?inc:full,O_RDONLY,0,kcred,&file);
            zfs_file_t *input=NULL;struct interrupted_input limited={file,300000};
            if(!error)error=zfs_file_from_reader(interrupted_reader,&limited,&input);
            if(!error&&oz_receive_stream_resumable(aborted[i],(i&1)?"next":"base",input,kcred,1)!=ZFS_ERR_STREAM_TRUNCATED)error=EIO;
            if(input)zfs_file_close(input);
            if(file)zfs_file_close(file);file=NULL;
            if(!error&&oz_receive_abort(aborted[i],NULL)!=EACCES)error=EIO;
            if(i>=2)continue; /* Separate authenticated client must abort these. */
            if(!error)error=oz_receive_abort(aborted[i],kcred);
            if(!error&&oz_receive_abort(aborted[i],kcred)!=(i?EINVAL:ENOENT))error=EIO;
        }
        report("WRITE receive abort preserves existing head",error);
        if(!error&&(unlink(full)||unlink(inc)))error=errno;
    }
    for(unsigned i=0;!error&&i<2;i++){
        char name[256];snprintf(name,sizeof(name),"%s@base",targets[i]);
        error=inc_payload(name,0,'A');
        if(!error)error=inc_payload(targets[i],0,'B');
        snprintf(name,sizeof(name),"%s/%%recv",targets[i]);
        objset_t *os=NULL;
        if(!error){int found=dmu_objset_hold(name,FTAG,&os);
            if(!found)dmu_objset_rele(os,FTAG);
            if(found!=ENOENT)error=EIO;}
    }
    if(!error)error=inc_payload("qemu_mirror/abort-incremental",0,'A');
    if(!error)error=inc_payload("qemu_mirror/abort-incremental@base",0,'A');
    if(!error&&!write)error=inc_payload("qemu_mirror/abort-api-incremental",0,'A');
    if(!error&&!write)error=inc_payload("qemu_mirror/abort-api-incremental@base",0,'A');
    const char *absent[]={"qemu_mirror/abort-new","qemu_mirror/abort-incremental/%recv","qemu_mirror/abort-incremental@next"};
    for(unsigned i=0;!error&&i<3;i++){
        objset_t *os=NULL;int found=dmu_objset_hold(absent[i],FTAG,&os);
        if(!found)dmu_objset_rele(os,FTAG);
        if(found!=ENOENT)error=EIO;
    }
    if(!write){
        const char *api_absent[]={"qemu_mirror/abort-api-new","qemu_mirror/abort-api-incremental/%recv","qemu_mirror/abort-api-incremental@next"};
        for(unsigned i=0;!error&&i<3;i++){
            /* EXEC skips the separate client's abort calls. Prove both
             * interrupted receives survive; @next must remain absent. */
            if(getenv("OPENZFS_EXEC_ONLY")&&i<2) {
                uint64_t object=0,offset=0;
                error=resume_state(api_absent[i],&object,&offset,1);
                continue;
            }
            objset_t *os=NULL;int found=dmu_objset_hold(api_absent[i],FTAG,&os);
            if(!found)dmu_objset_rele(os,FTAG);
            if(found!=ENOENT)error=EIO;
        }
    }
    report(write?"WRITE incremental token resume":"VERIFY cold incremental token resume",error);
    return error;
}
