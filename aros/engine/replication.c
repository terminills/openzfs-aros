/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <sys/dmu_recv.h>
#include <sys/dmu_send.h>
#include <sys/spa_impl.h>
#include <sys/dsl_dataset.h>
#include <sys/dsl_pool.h>
#include <sys/dsl_bookmark.h>
#include <sys/dsl_destroy.h>
#include <sys/dmu_objset.h>
#include <zfs_fletcher.h>
#include <zlib.h>
#include <zfs_namecheck.h>
#include "replication.h"
#ifdef OZ_NATIVE_DISPATCHER
#include "../zpl/online-receive.h"
#endif

struct send_output {
    int (*write)(void *,const void *,size_t);
    void *arg;
};
static int send_bytes(objset_t *os,void *bytes,int length,void *arg)
{
    (void)os;
    struct send_output *out=arg;
    if(length<0)return EINVAL;
    return out->write(out->arg,bytes,(size_t)length);
}
int oz_send_options(char *options,const char **from,unsigned *flags,const char **token,const char **redact)
{
    if(!options||!from||!flags||!token||!redact)return EINVAL;
    *from=NULL;*flags=0;*token=NULL;*redact=NULL;
    unsigned seen=0;
    for(char *p=options;*p;){
        char *next=strchr(p,';');
        if(next){*next++=0;if(!*next)return EINVAL;}
        char *value=strchr(p,'=');
        if(!value||value==p||!value[1])return EINVAL;
        *value++=0;
        unsigned bit;
        if(!strcmp(p,"from"))bit=8u;
        else if(!strcmp(p,"resume"))bit=16u;
        else if(!strcmp(p,"redact"))bit=64u;
        else if(!strcmp(p,"compressed"))bit=OZ_SEND_COMPRESSED;
        else if(!strcmp(p,"large-blocks"))bit=OZ_SEND_LARGE_BLOCKS;
        else if(!strcmp(p,"embedded"))bit=OZ_SEND_EMBEDDED;
        else if(!strcmp(p,"raw"))bit=OZ_SEND_RAW;
        else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;
        if(bit==8u){
            if((strchr(value,'#')?bookmark_namecheck(value,NULL,NULL):
                dataset_namecheck(value,NULL,NULL))||
                (!strchr(value,'@')&&!strchr(value,'#')))return EINVAL;
            *from=value;
        }else if(bit==16u)*token=value;
        else if(bit==64u)*redact=value;
        else if(!strcmp(value,"on"))*flags|=bit;
        else if(strcmp(value,"off"))return EINVAL;
        if(!next)break;
        p=next;
    }
    /* A token already specifies its stream representation. */
    return *token&&(seen&(OZ_SEND_SUPPORTED|64u))?EINVAL:0;
}
/* dmu_send takes a short bookmark component. Check the complete derived name
 * before opening output, including its size, to prevent upstream truncation. */
int oz_send_redact_validate(const char *source,const char *redact)
{
    if(!redact)return 0;
    if(!source||!*redact||strpbrk(redact,"/@#"))return EINVAL;
    const char *at=strchr(source,'@');
    if(!at)return EINVAL;
    size_t prefix=(size_t)(at-source),length=strlen(redact);
    char name[ZFS_MAX_DATASET_NAME_LEN];
    if(prefix+1+length>=sizeof(name))return ENAMETOOLONG;
    memcpy(name,source,prefix);name[prefix]='#';
    memcpy(name+prefix+1,redact,length+1);
    return bookmark_namecheck(name,NULL,NULL)?EINVAL:0;
}
static int send_at(const char *source,const char *from,unsigned flags,uint64_t object,
    uint64_t resume_offset,const char *redact,cred_t *caller,
    int (*write)(void *,const void *,size_t),void *arg)
{
    if(!caller)return EACCES;
    if(flags&~OZ_SEND_SUPPORTED)return EINVAL;
    if(!source||!write||dataset_namecheck(source,NULL,NULL))return EINVAL;
    int redact_error=oz_send_redact_validate(source,redact);
    if(redact_error)return redact_error;
    if(from&&(strchr(from,'#')?bookmark_namecheck(from,NULL,NULL):
        dataset_namecheck(from,NULL,NULL)))return EINVAL;
    int error=oz_send_perms(source,!!(flags&OZ_SEND_RAW),caller);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    struct send_output output={write,arg};
    dmu_send_outparams_t params={.dso_outfunc=send_bytes,.dso_arg=&output,
        .dso_dryrun=B_FALSE};
    offset_t offset=0;
    error=dmu_send(source,from,!!(flags&OZ_SEND_EMBEDDED),
        !!(flags&OZ_SEND_LARGE_BLOCKS),!!(flags&OZ_SEND_COMPRESSED),!!(flags&OZ_SEND_RAW),B_FALSE,
        object,resume_offset,redact,-1,&offset,&params);
    int restore=oz_cred_pop(scope);
    return error?error:restore;
}
int oz_send_stream_flags(const char *source,const char *from,unsigned flags,cred_t *caller,
    int (*write)(void *,const void *,size_t),void *arg)
{
    return send_at(source,from,flags,0,0,NULL,caller,write,arg);
}
int oz_send_stream_redacted(const char *source,const char *from,unsigned flags,const char *redact,
    cred_t *caller,int (*write)(void *,const void *,size_t),void *arg)
{
    return send_at(source,from,flags,0,0,redact,caller,write,arg);
}

static int hex_digit(unsigned char c)
{
    if(c>='0'&&c<='9')return c-'0';
    if(c>='a'&&c<='f')return c-'a'+10;
    if(c>='A'&&c<='F')return c-'A'+10;
    return -1;
}
static int token_number(const char **cursor,uint64_t *value)
{
    const char *p=*cursor;unsigned count=0;uint64_t number=0;int digit;
    while((digit=hex_digit(*p))>=0){
        if(++count>16)return EOVERFLOW;
        number=(number<<4)|(unsigned)digit;p++;
    }
    if(!count||*p!='-')return EINVAL;
    *value=number;*cursor=p+1;return 0;
}
/* Upstream version-1 token wire format: Fletcher4 over gzip-compressed
 * packed nvlist, represented as hex. Bounded sizes and strict hex parsing
 * precede allocation; do not use unchecked sscanf lengths from wire input. */
static int resume_decode(const char *token,nvlist_t **out)
{
    *out=NULL;
    if(!token||strncmp(token,"1-",2))return EINVAL;
    const char *p=token+2;uint64_t checksum=0,packed_len=0;
    int error=token_number(&p,&checksum);
    if(!error)error=token_number(&p,&packed_len);
    if(error)return error;
    size_t hexlen=strnlen(p,131073);
    if(!packed_len||packed_len>65536||!hexlen||hexlen>131072||(hexlen&1))return EINVAL;
    size_t len=hexlen/2;unsigned char *compressed=malloc(len),*packed=malloc(packed_len);
    if(!compressed||!packed){free(compressed);free(packed);return ENOMEM;}
    for(size_t i=0;!error&&i<len;i++){
        int a=hex_digit(p[i*2]),b=hex_digit(p[i*2+1]);
        if(a<0||b<0)error=EINVAL;else compressed[i]=(a<<4)|b;
    }
    zio_cksum_t ck;
    if(!error){fletcher_4_native_varsize(compressed,len,&ck);
        if(ck.zc_word[0]!=checksum)error=EINVAL;}
    uLongf actual=packed_len;
    if(!error&&(uncompress(packed,&actual,compressed,len)!=Z_OK||actual!=packed_len))error=EINVAL;
    if(!error)error=nvlist_unpack((char *)packed,packed_len,out,KM_SLEEP);
    free(packed);free(compressed);return error;
}
/* Resolve the token's redaction GUID set using actual upstream bookmark
 * properties, following libzfs find_redact_book. Source authorization and GUID
 * validation must already have succeeded. A matching incomplete bookmark must
 * never become an ordinary (unredacted) send. */
static int resume_redact_book(const char *source,uint64_t guid,nvlist_t *token,
    struct oz_resume_send *resume)
{
    if(!nvlist_exists(token,"redact_snaps"))return 0;
    uint64_t *snaps=NULL;uint_t count=0;
    if(nvlist_lookup_uint64_array(token,"redact_snaps",&snaps,&count))return EINVAL;
    for(uint_t i=0;i<count;i++)for(uint_t j=0;j<i;j++)
        if(snaps[i]==snaps[j])return EINVAL;
    char dataset[ZFS_MAX_DATASET_NAME_LEN];
    size_t length=(size_t)(strchr(source,'@')-source);
    if(length>=sizeof(dataset))return ENAMETOOLONG;
    memcpy(dataset,source,length);dataset[length]=0;
    nvlist_t *props=fnvlist_alloc(),*books=fnvlist_alloc();
    fnvlist_add_boolean(props,"guid");
    fnvlist_add_boolean(props,"redact_snaps");
    fnvlist_add_boolean(props,"redact_complete");
    int error=dsl_get_bookmarks(dataset,props,books);
    fnvlist_free(props);
    if(error){fnvlist_free(books);return error;}
    error=ENOENT;int incomplete=0;
    for(nvpair_t *pair=nvlist_next_nvpair(books,NULL);pair;pair=nvlist_next_nvpair(books,pair)){
        nvlist_t *book=NULL,*value=NULL;uint64_t bookguid=0,*book_snaps=NULL;uint_t book_count=0;
        boolean_t complete=B_FALSE;
        if(nvpair_value_nvlist(pair,&book)||nvlist_lookup_nvlist(book,"guid",&value)||
            nvlist_lookup_uint64(value,ZPROP_VALUE,&bookguid)||bookguid!=guid||
            nvlist_lookup_nvlist(book,"redact_snaps",&value)||
            nvlist_lookup_uint64_array(value,ZPROP_VALUE,&book_snaps,&book_count)||book_count!=count)
            continue;
        uint_t i;
        for(i=0;i<count;i++){
            uint_t j;for(j=0;j<book_count;j++)if(snaps[i]==book_snaps[j])break;
            if(j==book_count)break;
        }
        if(i!=count)continue;
        if(nvlist_lookup_nvlist(book,"redact_complete",&value)||
            nvlist_lookup_boolean_value(value,ZPROP_VALUE,&complete)||!complete){incomplete=1;continue;}
        const char *name=nvpair_name(pair);
        error=oz_send_redact_validate(source,name);
        if(!error&&strlen(name)>=sizeof(resume->redact))error=ENAMETOOLONG;
        if(!error)strcpy(resume->redact,name);
        break;
    }
    fnvlist_free(books);
    return error==ENOENT&&incomplete?EBUSY:error;
}
/* A redacted incremental token identifies its base by GUID AND redaction
 * set. An intact snapshot with the same GUID is not interchangeable. Match
 * libzfs's bookmark-only redacted-base selection, using the held upstream
 * redaction list rather than trusting the caller's bookmark name. */
static int resume_redact_base(dsl_pool_t *dp,const zfs_bookmark_phys_t *mark,nvlist_t *token)
{
    if(!nvlist_exists(token,"book_redact_snaps"))return 0;
    uint64_t *snaps=NULL;uint_t count=0;
    if(nvlist_lookup_uint64_array(token,"book_redact_snaps",&snaps,&count))return EINVAL;
    for(uint_t i=0;i<count;i++)for(uint_t j=0;j<i;j++)
        if(snaps[i]==snaps[j])return EINVAL;
    if(!mark->zbm_redaction_obj)return ESTALE;
    redaction_list_t *list=NULL;
    int error=dsl_redaction_list_hold_obj(dp,mark->zbm_redaction_obj,FTAG,&list);
    if(error)return error;
    if(list->rl_phys->rlp_num_snaps!=count)error=ESTALE;
    for(uint_t i=0;!error&&i<count;i++){
        uint_t j;for(j=0;j<count;j++)if(snaps[i]==list->rl_phys->rlp_snaps[j])break;
        if(j==count)error=ESTALE;
    }
    if(!error&&(list->rl_phys->rlp_last_object!=UINT64_MAX||
        list->rl_phys->rlp_last_blkid!=UINT64_MAX))error=EBUSY;
    dsl_redaction_list_rele(list,FTAG);
    return error;
}
int oz_send_resume_prepare(const char *source,const char *from,const char *token,
    cred_t *caller,struct oz_resume_send *resume)
{
    if(!caller)return EACCES;
    if(!resume||!source||!strchr(source,'@')||dataset_namecheck(source,NULL,NULL))return EINVAL;
    if(from&&(strchr(from,'#')?bookmark_namecheck(from,NULL,NULL):
        dataset_namecheck(from,NULL,NULL)))return EINVAL;
    memset(resume,0,sizeof(*resume));
    nvlist_t *nv=NULL;int error=resume_decode(token,&nv);if(error)return error;
    uint64_t guid=0,fromguid=0,bytes=0;const char *toname=NULL;
    if(nvlist_lookup_string(nv,"toname",&toname)||!strchr(toname,'@')||
        dataset_namecheck(toname,NULL,NULL)||
        nvlist_lookup_uint64(nv,"toguid",&guid)||!guid||
        nvlist_lookup_uint64(nv,"object",&resume->object)||!resume->object||
        nvlist_lookup_uint64(nv,"offset",&resume->offset)||
        nvlist_lookup_uint64(nv,"bytes",&bytes))error=EINVAL;
    if(!error&&nvlist_exists(nv,"fromguid"))error=nvlist_lookup_uint64(nv,"fromguid",&fromguid);
    for(nvpair_t *pair=nvlist_next_nvpair(nv,NULL);!error&&pair;pair=nvlist_next_nvpair(nv,pair)){
        const char *name=nvpair_name(pair);unsigned flag=0;
        if(!strcmp(name,"largeblockok"))flag=OZ_SEND_LARGE_BLOCKS;
        else if(!strcmp(name,"embedok"))flag=OZ_SEND_EMBEDDED;
        else if(!strcmp(name,"compressok"))flag=OZ_SEND_COMPRESSED;
        else if(!strcmp(name,"rawok"))flag=OZ_SEND_RAW;
        else if(!strcmp(name,"redact_snaps")||!strcmp(name,"book_redact_snaps")){
            if(nvpair_type(pair)!=DATA_TYPE_UINT64_ARRAY)error=EINVAL;
        }
        else if(strcmp(name,"toname")&&strcmp(name,"toguid")&&strcmp(name,"fromguid")&&
            strcmp(name,"object")&&strcmp(name,"offset")&&strcmp(name,"bytes"))error=ENOTSUP;
        if(flag){if(nvpair_type(pair)!=DATA_TYPE_BOOLEAN)error=EINVAL;else resume->flags|=flag;}
    }
    if(!error&&!!fromguid!=!!from)error=EINVAL;
    if(!error&&nvlist_exists(nv,"book_redact_snaps")&&(!from||!strchr(from,'#')))error=EINVAL;
    /* The token determines raw authorization, never a caller-supplied UID or
     * a plaintext preflight. Check before examining source object metadata. */
    if(!error)error=oz_send_perms(source,!!(resume->flags&OZ_SEND_RAW),caller);
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL,*ancestor=NULL;objset_t *os=NULL;
    if(!error)error=dsl_pool_hold(source,FTAG,&dp);
    if(!error)error=dsl_dataset_hold(dp,source,FTAG,&ds);
    if(!error&&dsl_dataset_phys(ds)->ds_guid!=guid)error=ESTALE;
    if(!error&&from){
        if(strchr(from,'#')){zfs_bookmark_phys_t mark;
            error=dsl_bookmark_lookup(dp,from,ds,&mark);
            if(!error&&mark.zbm_guid!=fromguid)error=ESTALE;
            if(!error)error=resume_redact_base(dp,&mark,nv);
        }else{
            error=dsl_dataset_hold(dp,from,FTAG,&ancestor);
            if(!error&&(dsl_dataset_phys(ancestor)->ds_guid!=fromguid||
                !dsl_dataset_is_before(ds,ancestor,0)))error=ESTALE;
        }
    }
    if(!error)error=dmu_objset_from_ds(ds,&os);
    dmu_object_info_t info;
    if(!error)error=dmu_object_info(os,resume->object,&info);
    if(!error){uint64_t unit=info.doi_data_block_size;
        if(!(resume->flags&(OZ_SEND_LARGE_BLOCKS|OZ_SEND_RAW)))unit=MIN(unit,SPA_OLD_MAXBLOCKSIZE);
        if(!unit||resume->offset%unit||resume->offset>info.doi_max_offset)error=EINVAL;
    }
    if(ancestor)dsl_dataset_rele(ancestor,FTAG);
    if(ds)dsl_dataset_rele(ds,FTAG);
    if(dp)dsl_pool_rele(dp,FTAG);
    if(!error)error=resume_redact_book(source,guid,nv,resume);
    fnvlist_free(nv);return error;
}
int oz_send_stream_token(const char *source,const char *from,const char *token,cred_t *caller,
    int (*write)(void *,const void *,size_t),void *arg)
{
    struct oz_resume_send resume;
    int error=oz_send_resume_prepare(source,from,token,caller,&resume);
    if(error)return error;
    return send_at(source,from,resume.flags,resume.object,resume.offset,
        resume.redact[0]?resume.redact:NULL,caller,write,arg);
}
int oz_send_stream(const char *source,const char *from,cred_t *caller,
    int (*write)(void *,const void *,size_t),void *arg)
{
    return oz_send_stream_flags(source,from,0,caller,write,arg);
}

int oz_receive_stream_options(const char *dataset,const char *snapshot,zfs_file_t *input,
    cred_t *caller,int resumable,int force)
{
    if(!caller)return EACCES;
    if((resumable!=0&&resumable!=1)||(force!=0&&force!=1))return EINVAL;
    if(!dataset||!snapshot||!input||!snapshot[0]||
        strchr(dataset,'@')||strchr(dataset,'#')||
        strchr(snapshot,'/')||strchr(snapshot,'@')||strchr(snapshot,'#'))return EINVAL;
    char target[ZFS_MAX_DATASET_NAME_LEN];
    int length=snprintf(target,sizeof(target),"%s@%s",dataset,snapshot);
    if(length<0||(size_t)length>=sizeof(target))return ENAMETOOLONG;
    if(dataset_namecheck(dataset,NULL,NULL)||dataset_namecheck(target,NULL,NULL))return EINVAL;
    int error=oz_receive_perms(dataset,force,caller);
    if(error)return error;
    spa_t *spa=NULL;
    error=spa_open(dataset,&spa,FTAG);
    if(error)return error;
    if(!spa_writeable(spa))error=EROFS;
    else if(spa_suspended(spa))error=EAGAIN;
    spa_close(spa,FTAG);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    dmu_replay_record_t begin={0};
    error=zfs_file_read(input,&begin,sizeof(begin),NULL);
    if(!error&&begin.drr_type!=DRR_BEGIN)error=EINVAL;
    dmu_recv_cookie_t cookie;
    offset_t offset=sizeof(begin);
    if(!error){
        error=dmu_recv_begin(dataset,snapshot,&begin,force,B_FALSE,resumable,
            NULL,NULL,NULL,&cookie,input,&offset);
        /* A successful begin transfers cleanup to stream, including failure.
         * End is valid only after stream succeeds. Never skip this pairing. */
        if(!error){
            error=dmu_recv_stream(&cookie,&offset);
            if(!error){
#ifdef OZ_NATIVE_DISPATCHER
                boolean_t redacted=DMU_GET_FEATUREFLAGS(begin.drr_u.drr_begin.drr_versioninfo)&DMU_BACKUP_FEATURE_REDACTED;
                error=oz_receive_end_dataset(dataset,&cookie,!cookie.drc_newfs&&redacted);
#else
                error=dmu_recv_end(&cookie,NULL);
#endif
            }
        }
    }
    int restore=oz_cred_pop(scope);
    return error?error:restore;
}
int oz_receive_stream_resumable(const char *dataset,const char *snapshot,zfs_file_t *input,
    cred_t *caller,int resumable)
{ return oz_receive_stream_options(dataset,snapshot,input,caller,resumable,0); }
int oz_receive_stream(const char *dataset,const char *snapshot,zfs_file_t *input,cred_t *caller)
{
    return oz_receive_stream_resumable(dataset,snapshot,input,caller,0);
}

int oz_receive_abort(const char *dataset,cred_t *caller)
{
    if(!caller)return EACCES;
    if(!dataset||strchr(dataset,'@')||strchr(dataset,'#')||
        dataset_namecheck(dataset,NULL,NULL))return EINVAL;
    char candidate[ZFS_MAX_DATASET_NAME_LEN];
    int n=snprintf(candidate,sizeof(candidate),"%s/%%recv",dataset);
    if(n<0||(size_t)n>=sizeof(candidate))return ENAMETOOLONG;
    dsl_pool_t *dp=NULL;dsl_dataset_t *ds=NULL;
    int error=dsl_pool_hold(dataset,FTAG,&dp);
    if(error)return error;
    if(!spa_writeable(dp->dp_spa))error=EROFS;
    else if(spa_suspended(dp->dp_spa))error=EAGAIN;
    if(!error){
        error=dsl_dataset_hold(dp,candidate,FTAG,&ds);
        if(error==ENOENT){strlcpy(candidate,dataset,sizeof(candidate));error=dsl_dataset_hold(dp,candidate,FTAG,&ds);}
    }
    if(!error&&(!DS_IS_INCONSISTENT(ds)||!dsl_dataset_has_resume_receive_state(ds)))error=EINVAL;
    if(ds)dsl_dataset_rele(ds,FTAG);
    dsl_pool_rele(dp,FTAG);
    if(error)return error;
    error=zfs_secpolicy_destroy_perms(candidate,caller);
    if(error)return error;
    struct oz_cred_scope *scope=NULL;
    error=oz_cred_push(caller,&scope);
    if(error)return error;
    error=dsl_destroy_head(candidate);
    int restore=oz_cred_pop(scope);
    return error?error:restore;
}
