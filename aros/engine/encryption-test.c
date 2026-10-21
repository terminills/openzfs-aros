/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable QEMU fixture only. Public deterministic test key, never a user key. */
#include <sys/zfs_context.h>
#include <sys/dsl_crypt.h>
#include <sys/dsl_deleg.h>
#include <zfs_deleg.h>
#include <sys/spa.h>
#include <sys/dmu_objset.h>
#include <sys/dsl_prop.h>
#include <sys/zio_crypt.h>
#include <sys/stat.h>
#include <proto/exec.h>
#include "zpl-fixture.h"
#include "passphrase.h"
static const char *dataset="qemu_mirror/encrypted-fixture";
static void report(const char *stage,int error)
{
    char line[160];snprintf(line,sizeof(line),"Native encryption %s errno=%d\n",stage,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
}
static int params_format(int create,int wrong,zfs_keyformat_t format,dsl_crypto_params_t **out)
{
    nvlist_t *props=fnvlist_alloc(),*hidden=fnvlist_alloc();
    uint8_t key[WRAPPING_KEY_LEN];
    for(unsigned i=0;i<sizeof(key);i++)key[i]=(uint8_t)(i+1+wrong);
    fnvlist_add_uint8_array(hidden,"wkeydata",key,sizeof(key));
    memset(key,0,sizeof(key));
    if(create){
        if(create==1)fnvlist_add_uint64(props,"encryption",ZIO_CRYPT_AES_256_GCM);
        fnvlist_add_uint64(props,"keyformat",format);
        fnvlist_add_string(props,"keylocation","prompt");
    }
    int error=dsl_crypto_params_create_nvlist(create==2?DCP_CMD_NEW_KEY:DCP_CMD_NONE,create?props:NULL,hidden,out);
    fnvlist_free(props);fnvlist_free(hidden);return error;
}
static int params(int create,int wrong,dsl_crypto_params_t **out)
{return params_format(create,wrong,ZFS_KEYFORMAT_RAW,out);}
static int load(int wrong)
{
    dsl_crypto_params_t *p=NULL;int error=params(0,wrong,&p);
    if(!error)error=spa_keystore_load_wkey(dataset,p,B_FALSE);
    if(p)dsl_crypto_params_free(p,!!error);
    return error;
}
static int key_sources(void)
{
    const char *name="qemu_mirror/encrypted-delegated";
    dsl_crypto_params_t *p=NULL;int error=params(1,2,&p);
    if(!error)error=oz_zpl_fixture_create_crypto(name,p);
    if(p)dsl_crypto_params_free(p,!!error);
    if(!error)error=spa_keystore_unload_wkey(name);
    nvlist_t *acl=fnvlist_alloc(),*perms=fnvlist_alloc();
    fnvlist_add_boolean(perms,"load-key");fnvlist_add_nvlist(acl,"ul$77",perms);
    if(!error)error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(name,acl,B_FALSE);
    fnvlist_free(perms);fnvlist_free(acl);
    if(!error)error=oz_zpl_fixture_create("qemu_mirror/key-source");
    p=NULL;
    if(!error)error=params(1,2,&p);
    if(!error)error=oz_zpl_fixture_create_crypto("qemu_mirror/encrypted-api-change",p);
    if(p)dsl_crypto_params_free(p,!!error);
    if(!error)error=spa_keystore_unload_wkey("qemu_mirror/encrypted-api-change");
    p=NULL;
    if(!error)error=params_format(1,2,ZFS_KEYFORMAT_HEX,&p);
    if(!error)error=oz_zpl_fixture_create_crypto("qemu_mirror/encrypted-hex",p);
    if(p)dsl_crypto_params_free(p,!!error);
    if(!error)error=spa_keystore_unload_wkey("qemu_mirror/encrypted-hex");
    if(!error){
        nvlist_t *props=fnvlist_alloc(),*hidden=fnvlist_alloc();
        uint8_t key[32];const char *phrase="AROS OpenZFS fixture passphrase";
        uint64_t salt=UINT64_C(0x0102030405060708);
        error=oz_passphrase_derive(phrase,strlen(phrase),salt,100000,key);
        fnvlist_add_uint64(props,"encryption",ZIO_CRYPT_AES_256_GCM);
        fnvlist_add_uint64(props,"keyformat",ZFS_KEYFORMAT_PASSPHRASE);
        fnvlist_add_string(props,"keylocation","prompt");
        fnvlist_add_uint64(props,"pbkdf2salt",salt);
        fnvlist_add_uint64(props,"pbkdf2iters",100000);
        fnvlist_add_uint8_array(hidden,"wkeydata",key,sizeof(key));
        memset(key,0,sizeof(key));p=NULL;
        if(!error)error=dsl_crypto_params_create_nvlist(DCP_CMD_NONE,props,hidden,&p);
        if(!error)error=oz_zpl_fixture_create_crypto("qemu_mirror/encrypted-passphrase",p);
        if(p)dsl_crypto_params_free(p,!!error);
        fnvlist_free(props);fnvlist_free(hidden);
        /* dmu_objset_create consumes crypto parameters, but unlike the
         * public create ioctl it does not apply ordinary DSL properties. */
        if(!error){
            nvlist_t *location=fnvlist_alloc();
            fnvlist_add_string(location,"keylocation","prompt");
            error=dsl_props_set("qemu_mirror/encrypted-passphrase",ZPROP_SRC_LOCAL,location);
            fnvlist_free(location);
        }
        if(!error)error=spa_keystore_unload_wkey("qemu_mirror/encrypted-passphrase");
    }
    return error;
}
static int content(int write)
{
    struct oz_zpl *fs=NULL;uint64_t object,size;
    const char *expected="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    unsigned char bytes[4096];int error=write?oz_zpl_open_writable(dataset,&fs):oz_zpl_open(dataset,&fs);
    if(!error&&spa_keystore_unload_wkey(dataset)!=EBUSY)error=EIO;
    if(!error)error=oz_zpl_lookup(fs,"hello.txt",&object,&size);
    if(!error&&size!=strlen(expected))error=EIO;
    if(!error)error=oz_zpl_read(fs,object,0,bytes,(size_t)size);
    if(!error&&memcmp(bytes,expected,(size_t)size))error=EIO;
    struct oz_zpl_info info;
    if(!error)error=oz_zpl_resolve(fs,oz_zpl_root(fs),"encrypted-data.bin",&info);
    if(error==ENOENT&&write)error=oz_zpl_create_as(fs,oz_zpl_root(fs),"encrypted-data.bin",S_IFREG|0600,kcred,&info);
    for(size_t at=0;!error&&at<65536;at+=sizeof(bytes)){
        if(write){
            for(size_t i=0;i<sizeof(bytes);i++)bytes[i]=(unsigned char)((at+i)*37+((at+i)>>8));
            size_t actual=0;error=oz_zpl_write_as(fs,info.object,at,bytes,sizeof(bytes),&actual,kcred);
            if(!error&&actual!=sizeof(bytes))error=EIO;
        }
        if(!error)error=oz_zpl_read(fs,info.object,at,bytes,sizeof(bytes));
        for(size_t i=0;!error&&i<sizeof(bytes);i++)
            if(bytes[i]!=(unsigned char)((at+i)*37+((at+i)>>8)))error=EIO;
    }
    if(!error)error=oz_zpl_resolve(fs,oz_zpl_root(fs),"encrypted-data.bin",&info);
    if(!error&&info.size!=65536)error=EIO;
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    return error;
}
static int passphrase_metadata(void)
{
    objset_t *os=NULL;nvlist_t *props=NULL,*entry=NULL;
    uint64_t salt=0,iters=0,format=0;char location[MAXNAMELEN]={0};
    int error=dmu_objset_hold("qemu_mirror/encrypted-passphrase",FTAG,&os);
    if(!error){
        props=fnvlist_alloc();dmu_objset_stats(os,props);
        error=nvlist_lookup_nvlist(props,"pbkdf2salt",&entry);
        if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&salt);
        if(!error)error=nvlist_lookup_nvlist(props,"pbkdf2iters",&entry);
        if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&iters);
        if(!error)error=nvlist_lookup_nvlist(props,"keyformat",&entry);
        if(!error)error=nvlist_lookup_uint64(entry,ZPROP_VALUE,&format);
        /* keylocation is a DSL property, unlike the computed crypto stats. */
        dmu_objset_rele(os,FTAG);fnvlist_free(props);
        if(!error&&(!salt||salt==UINT64_C(0x0102030405060708)||
            iters!=DEFAULT_PBKDF2_ITERATIONS||format!=ZFS_KEYFORMAT_PASSPHRASE))error=EIO;
    }
    if(!error)error=dsl_prop_get("qemu_mirror/encrypted-passphrase","keylocation",
        1,sizeof(location),location,NULL);
    char line[320];snprintf(line,sizeof(line),"Native passphrase metadata salt=%llu iters=%llu format=%llu expected-iters=%llu location=%s errno=%d\n",
        (unsigned long long)salt,(unsigned long long)iters,(unsigned long long)format,
        (unsigned long long)DEFAULT_PBKDF2_ITERATIONS,location,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    if(!error&&strcmp(location,"prompt"))error=EIO;
    report("cold passphrase metadata",error);
    return error;
}
int oz_encryption_test(int write)
{
    int error=0;
    if(write){
        spa_t *spa=NULL;error=spa_open("qemu_mirror",&spa,FTAG);
        if(!error){nvlist_t *props=fnvlist_alloc();fnvlist_add_uint64(props,"feature@encryption",0);
            error=spa_prop_set(spa,props);fnvlist_free(props);spa_close(spa,FTAG);}
        report("enable",error);
        dsl_crypto_params_t *p=NULL;
        if(!error)error=params(1,0,&p);
        if(!error)error=oz_zpl_fixture_create_crypto(dataset,p);
        if(p)dsl_crypto_params_free(p,!!error);
        report("create",error);
    }else{
        struct oz_zpl *fs=NULL;int rc=oz_zpl_open(dataset,&fs);
        if(fs)oz_zpl_close(fs);
        if(rc!=EACCES)error=EIO;
        if(!error){int rc=load(0);report("cold old key observed",rc);if(rc!=EACCES)error=EIO;}
        if(!error)error=load(2);
        report("cold load",error);
    }
    if(!error)error=content(write);
    report("read and busy unload",error);
    if(!error)error=spa_keystore_unload_wkey(dataset);
    report("unload",error);
    if(!error){struct oz_zpl *fs=NULL;int rc=oz_zpl_open(dataset,&fs);
        if(fs)oz_zpl_close(fs);
        if(rc!=EACCES)error=EIO;}
    if(!error){int rc=load(1);report("wrong key observed",rc);if(rc!=EACCES)error=EIO;}
    if(!error)error=load(write?0:2);
    if(!error)error=content(0);
    if(!error&&write){
        dsl_crypto_params_t *p=NULL;error=params(2,2,&p);
        if(!error)error=spa_keystore_change_key(dataset,p);
        if(p)dsl_crypto_params_free(p,!!error);
        report("replace key",error);
        if(!error)error=spa_keystore_unload_wkey(dataset);
        if(!error){int rc=load(0);report("old key observed",rc);if(rc!=EACCES)error=EIO;}
        if(!error)error=load(2);
        if(!error)error=content(0);
    }
    if(!error)error=spa_keystore_unload_wkey(dataset);
    report(write?"WRITE key lifecycle":"VERIFY cold key lifecycle",error);
    /* Leave a loaded fixture key for the separate authenticated client to
     * exercise permission checks, mounted rejection and successful unload. */
    if(!error)error=load(2);
    report("prepare authenticated unload",error);
    if(!error&&write)error=key_sources();
    if(!error&&!write&&getenv("OPENZFS_TEST_PASSPHRASE_CHANGE"))error=passphrase_metadata();
    report("prepare key sources",error);
    return error;
}
