/* SPDX-License-Identifier: CDDL-1.0 */
/* Disposable native mirror smoke. Creates only exclusive files in a new
 * random directory under this isolated runtime's SYS:. No device arguments. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/vdev.h>
#include <sys/fs/zfs.h>
#include <sys/zio.h>
#include <sys/stat.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <sys/dmu_tx.h>
#include <sys/txg.h>
#include <stdio.h>
#include <fcntl.h>
#include "zpl-fixture.h"
#ifdef __AROS__
/* Native CRT exports these; this SDK omits the public prototypes. */
extern ssize_t pread(int,void *,size_t,off_t);
extern ssize_t pwrite(int,const void *,size_t,off_t);
#ifndef OZ_TEST_DISCOVERY
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/dos64.h>
struct Library *DOS64Base;
#endif
#endif
#ifdef OZ_TEST_DISCOVERY
#include <libzutil.h>
#include "import-platform.h"
#include <sys/aros_vdev_policy.h>
extern int oz_dos_read_mount_test(struct oz_zpl *,int);
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <stdarg.h>
static int discovery_report(const char *format,...)
{
    char buf[512];va_list ap;va_start(ap,format);
    int n=vsnprintf(buf,sizeof(buf),format,ap);va_end(ap);
    for(const char *p=buf;*p;p++)RawPutChar(*p);
    return n;
}
#define printf discovery_report
static int import_path_tests(struct oz_import_paths *allowed)
{
    nvlist_t *leaf=fnvlist_alloc(),*root=fnvlist_alloc(),*config=fnvlist_alloc();
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_FILE);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,allowed->paths[0]);
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf,1);
    fnvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,root);
    int error=oz_import_config_check(allowed,config);
    if(!error&&oz_import_config_check(NULL,config)!=EINVAL)error=EIO;
    /* This path is not a device and must never be opened or scanned. */
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,"/NOT_ADMITTED/member");
    const char *lists[]={ZPOOL_CONFIG_CHILDREN,ZPOOL_CONFIG_SPARES,ZPOOL_CONFIG_L2CACHE};
    for(unsigned i=0;i<3&&!error;i++) {
        nvlist_t *bad=fnvlist_dup(root);
        fnvlist_add_nvlist_array(bad,lists[i],(const nvlist_t *const *)&leaf,1);
        fnvlist_add_nvlist(config,ZPOOL_CONFIG_VDEV_TREE,bad);
        allowed->rejected=0;
        nvlist_t *result=oz_aros_config_ops.pco_refresh_config(allowed,config);
        if(result||allowed->rejected!=EACCES)error=EIO;
        nvlist_free(result);nvlist_free(bad);
    }
    allowed->rejected=0;
    nvlist_free(config);nvlist_free(root);nvlist_free(leaf);
    printf("label path admission child/spare/cache rejection=%d\n",error);
    return error;
}
#endif

static int payload(const char *dataset, uint64_t *object, boolean_t write)
{
    objset_t *os;
    unsigned char expected[16384],actual[16384];
    for (unsigned i=0;i<sizeof(expected);i++)
        expected[i]=(unsigned char)((i*37u)^(i>>5)^0xa7);
    int rc;
    if (write && (rc=dmu_objset_create(dataset,DMU_OST_OTHER,0,NULL,NULL,NULL)))
        return rc;
    rc=dmu_objset_own(dataset,DMU_OST_OTHER,!write,B_FALSE,FTAG,&os);
    if (rc) return rc;
    if (write) {
        dmu_tx_t *tx=dmu_tx_create(os);
        dmu_tx_hold_bonus(tx,DMU_NEW_OBJECT);
        dmu_tx_hold_write(tx,DMU_NEW_OBJECT,0,sizeof(expected));
        rc=dmu_tx_assign(tx,DMU_TX_WAIT);
        if (rc) dmu_tx_abort(tx);
        else {
            *object=dmu_object_alloc(os,DMU_OT_PLAIN_FILE_CONTENTS,4096,DMU_OT_NONE,0,tx);
            dmu_write(os,*object,0,sizeof(expected),expected,tx,0);
            dmu_tx_commit(tx);
            txg_wait_synced(dmu_objset_pool(os),0);
        }
    } else {
        rc=dmu_read(os,*object,0,sizeof(actual),actual,0);
        if (!rc && memcmp(expected,actual,sizeof(actual))) rc=EIO;
    }
    dmu_objset_disown(os,B_FALSE,FTAG);
    return rc;
}

int main(int argc, char **argv)
{
    unsigned disks=2,parity=0;
    int rc=0;
    const char *topology="mirror";
#ifdef OZ_TEST_DISCOVERY
    const char *cold=getenv("OPENZFS_COLD_PHASE");
    int cold_read=cold&&!strcmp(cold,"VERIFY");
    if(cold&&strcmp(cold,"CREATE")&&!cold_read)return 20;
    /* This entry is built only for an isolated marked test ISO. */
    FILE *marker=fopen("SYS:Security/openzfs-test-only","r");
    if(!marker)return 20;
    fclose(marker);
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    if(!secBase||!secIsConfigured())return 20;
    struct TagItem login[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    if(secLoginA(login)!=secOWNER_SYSTEM)return 20;
#endif
    if (argc==2) {
        topology=argv[1];
        if (!strcmp(topology,"raidz1")) parity=1;
        else if (!strcmp(topology,"raidz2")) parity=2;
        else if (!strcmp(topology,"raidz3")) parity=3;
        else if (strcmp(topology,"mirror")) return 20;
        if (parity) disks=parity+2;
    } else if (argc!=1) return 20;
    uint32_t nonce;
    char dir[128],path[160],pool[64],id[32],cache[160];
#ifdef OZ_TEST_DISCOVERY
    const off_t size=64LL*1024*1024;
#else
    const char *large=getenv("OPENZFS_TEST_LARGE_FILES");
    if(large&&strcmp(large,"1"))return 20;
    const char *array50=getenv("OPENZFS_TEST_ARRAY50");
    if(array50&&(!large||strcmp(array50,"1")||strcmp(topology,"raidz1")))return 20;
    if(array50)disks=5;
    const off_t size=array50?13LL*1024*1024*1024*1024:
        large?8LL*1024*1024*1024:256LL*1024*1024;
#endif
    nvlist_t *leaves[5],*mirror,*root,*props,*exported=NULL;
    char dataset[96];
    uint64_t object=0;
    const char *base="/SYS";
#ifdef OZ_TEST_DISCOVERY
    base=cold?"/OZFILES":"/RAM";
#endif
    snprintf(path,sizeof(path),"%s/pool-%s-errors.log",
#ifdef OZ_TEST_DISCOVERY
        "/RAM",
#else
        base,
#endif
        topology);
    if (!freopen(path,"w",stderr)) return 20;
    setvbuf(stderr,NULL,_IONBF,0);
    setvbuf(stdout,NULL,_IONBF,0);
    if (getentropy(&nonce,sizeof(nonce)) || nonce==0) return 20;
#ifdef OZ_TEST_DISCOVERY
    if(cold&&!cold_read)nonce=0x51a20519; /* runner's exclusive blank-file fixture */
    if(cold_read) {
        unsigned saved_nonce;unsigned long long saved_object;
        FILE *manifest=fopen("/OZFILES/manifest.txt","r");
        if(!manifest)return 20;
        int fields=fscanf(manifest,"%x %llu",&saved_nonce,&saved_object);
        fclose(manifest);
        if(fields!=2||!saved_nonce||!saved_object)return 20;
        nonce=saved_nonce;object=saved_object;
    }
#endif
    snprintf(dir,sizeof(dir),"%s/OpenZFS-test-%08x",base,nonce);
    snprintf(pool,sizeof(pool),"aros_%s_%08x",topology,nonce);
    snprintf(dataset,sizeof(dataset),"%s/payload",pool);
    snprintf(id,sizeof(id),"0x%08x",nonce);
    printf("Disposable native pool directory: %s\n",dir);
    if (setenv("ZFS_HOSTID",id,1)) return 20;
    snprintf(cache,sizeof(cache),"%s/zpool.cache",dir);
    spa_config_path=cache;
#ifdef OZ_TEST_DISCOVERY
    if(cold_read)goto import_phase;
#endif
    if (
#ifdef OZ_TEST_DISCOVERY
        !cold &&
#endif
        mkdir(dir,0700)) { printf("FAIL mkdir errno=%d\n",errno); return 20; }
    for (unsigned i=0;i<disks;i++) {
        snprintf(path,sizeof(path),"%s/disk%u",dir,i);
        int flags=O_CREAT|O_EXCL|O_RDWR;
#ifdef OZ_TEST_DISCOVERY
        if(cold)flags=O_RDWR;
#endif
        int fd=open(path,flags,0600);
        struct stat st;
        if (fd<0) { printf("FAIL create %s errno=%d\n",path,errno); return 20; }
        int rc=0;
#ifdef OZ_TEST_DISCOVERY
        if(!cold)
#endif
#ifndef OZ_TEST_DISCOVERY
        if(large) {
            /* Large hosted fixture geometry is not a POSIX gap-fill test.
             * Use the qualified native sizing primitive to avoid allocating
             * fourteen zero-filled 8GiB members before testing the engine. */
            char native_path[160];
            snprintf(native_path,sizeof(native_path),"SYS:%s/disk%u",dir+5,i);
            DOS64Base=OpenLibrary((CONST_STRPTR)"dos64.library",0);
            BPTR native=DOS64Base?Open((CONST_STRPTR)native_path,MODE_OLDFILE):0;
            rc=!native||SetFileSize64(native,OFFSET_BEGINNING,size)!=size;
            if(native&&!Close(native))rc=1;
            if(DOS64Base)CloseLibrary(DOS64Base);
            DOS64Base=NULL;
        } else
#endif
        rc=ftruncate(fd,size);
        int sr=fstat(fd,&st);
        printf("file %s truncate=%d stat=%d size=%llu\n",path,rc,sr,
            (unsigned long long)(sr?0:st.st_size));
        if (rc || sr || st.st_size!=size) { printf("FAIL backing file size errno=%d\n",errno);close(fd); return 20; }
#ifdef OZ_TEST_DISCOVERY
        if(cold) {
            unsigned char blank[65536];
            for(off_t offset=0;offset<size;offset+=sizeof(blank)) {
                if(read(fd,blank,sizeof(blank))!=sizeof(blank)){close(fd);return 20;}
                for(unsigned j=0;j<sizeof(blank);j++)if(blank[j]){close(fd);return 20;}
            }
        }
#endif
        unsigned char out[4096]={0},in[4096];
        out[0]=0x41+i;
        if (pwrite(fd,out,sizeof(out),size-sizeof(out))!=sizeof(out) ||
            pread(fd,in,sizeof(in),size-sizeof(in))!=sizeof(in) ||
            memcmp(out,in,sizeof(out)) || fsync(fd)) {
            printf("FAIL end-of-file preflight errno=%d\n",errno);
            close(fd); return 20;
        }
        close(fd);
        leaves[i]=fnvlist_alloc();
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_FILE);
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_PATH,path);
        fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,12);
    }
    mirror=fnvlist_alloc();root=fnvlist_alloc();props=fnvlist_alloc();
    fnvlist_add_string(mirror,ZPOOL_CONFIG_TYPE,parity?VDEV_TYPE_RAIDZ:VDEV_TYPE_MIRROR);
    if (parity) fnvlist_add_uint64(mirror,ZPOOL_CONFIG_NPARITY,parity);
    const nvlist_t *leaf_view[5];
    for (unsigned i=0;i<disks;i++) leaf_view[i]=leaves[i];
    fnvlist_add_nvlist_array(mirror,ZPOOL_CONFIG_CHILDREN,leaf_view,disks);
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    const nvlist_t *mirror_view[1]={mirror};
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,mirror_view,1);
    fnvlist_add_string(props,"cachefile","none");
    fnvlist_add_uint64(props,"failmode",ZIO_FAILURE_MODE_CONTINUE);
    printf("Starting upstream read/write engine, explicit test hostid=%s\n",id);
    kernel_init(SPA_MODE_READ|SPA_MODE_WRITE);
    printf("Creating upstream %s %s\n",topology,pool);
    rc=spa_create(pool,root,props,NULL,NULL);
    printf("spa_create=%d\n",rc);
    if (!rc) {
        spa_t *spa;
        rc=spa_open(pool,&spa,FTAG);
        printf("spa_open=%d\n",rc);
        if (!rc) {
            printf("native pool guid=%llu\n",(unsigned long long)spa_guid(spa));
            spa_close(spa,FTAG);
            /* Direct engine diagnostic, not an authenticated service request.
             * Scope fixture creation explicitly, as the QEMU pool test does;
             * never restore the old implicit-root behavior in production. */
            struct oz_cred_scope *fixture_scope=NULL;
            rc=oz_cred_push(kcred,&fixture_scope);
            if(!rc)rc=payload(dataset,&object,B_TRUE);
            printf("dataset payload write=%d object=%llu bytes=16384\n",rc,
                (unsigned long long)object);
            if(!rc) {
                char files[256];snprintf(files,sizeof(files),"%s/files",pool);
                rc=oz_zpl_fixture_create(files);printf("real ZPL fixture creation=%d\n",rc);
            }
            if(fixture_scope)VERIFY0(oz_cred_pop(fixture_scope));
            if (!rc) rc=spa_export(pool,&exported,B_FALSE,B_FALSE);
            printf("spa_export=%d\n",rc);
        }
    }
    fnvlist_free(props);fnvlist_free(root);fnvlist_free(mirror);
    for (unsigned i=0;i<disks;i++) fnvlist_free(leaves[i]);
    kernel_fini();
    if (!rc && !exported) rc=EIO;
#ifdef OZ_TEST_DISCOVERY
    /* Discard the in-memory export configuration. Only on-disk labels may
     * supply the configuration for this test's subsequent import. */
    nvlist_free(exported);exported=NULL;
    if(cold) {
        if(!rc) {
            int fd=open("/OZFILES/manifest.txt",O_WRONLY|O_CREAT|O_EXCL,0600);
            char text[80];int length=snprintf(text,sizeof(text),"%08x %llu\n",nonce,(unsigned long long)object);
            if(fd<0)rc=errno?errno:EIO;
            else {
                if(write(fd,text,length)!=length||fsync(fd))rc=EIO;
                if(close(fd)&&!rc)rc=EIO;
            }
        }
        /* Explicit clean backing-volume detach. Do not mistake DOS Flush
         * for a FAT stable-storage barrier or claim crash durability. */
        if(!rc&&!Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE))rc=EIO;
        printf("RESULT cold discovery create %s\n",rc?"FAIL":"PASS");
        return rc?20:0;
    }
#else
    if (!rc) {
        char *packed=NULL; size_t length=0;
        rc=nvlist_pack(exported,&packed,&length,NV_ENCODE_XDR,0);
        snprintf(path,sizeof(path),"%s/export.nvlist",dir);
        int fd=rc?-1:open(path,O_CREAT|O_EXCL|O_WRONLY,0600);
        if (!rc && fd<0) rc=errno?errno:EIO;
        if (!rc && (write(fd,packed,length)!=(ssize_t)length || fsync(fd))) rc=EIO;
        if (fd>=0 && close(fd) && !rc) rc=EIO;
        free(packed);
        if (!rc) printf("Exported upstream XDR configuration: %s\n",path);
    }
#endif
#ifdef OZ_TEST_DISCOVERY
import_phase:
#endif
    if (!rc) {
        printf("Reinitializing read-only engine for on-disk import\n");
        kernel_init(SPA_MODE_READ);
#ifdef OZ_TEST_DISCOVERY
        char scan_paths[5][160];char *scan[5];
        for(unsigned i=0;i<disks;i++) {
            snprintf(scan_paths[i],sizeof(scan_paths[i]),"%s/disk%u",dir,i);
            scan[i]=scan_paths[i];
            char resolved[1024];struct stat check;
            char *rp=realpath(scan[i],resolved);
            printf("discovery input path=%s realpath=%s stat=%d\n",scan[i],rp?rp:"(failed)",stat(scan[i],&check));
            if(!rp||strlen(resolved)>=sizeof(scan_paths[i])){rc=EINVAL;break;}
            strcpy(scan_paths[i],resolved);
            int fd=open(scan[i],O_RDONLY),labels=0;nvlist_t *label=NULL;
            int lr=fd<0?errno:zpool_read_label(fd,&label,&labels);
            printf("discovery direct label fd=%d result=%d labels=%d config=%d\n",fd,lr,labels,label!=NULL);
            nvlist_free(label);if(fd>=0)close(fd);
        }
        importargs_t args={.path=scan,.paths=disks,.poolname=pool,.scan=B_TRUE};
        struct oz_import_paths allowed={.paths=scan,.count=disks};
        struct oz_vdev_policy *lifetime_policy=NULL;
        libpc_handle_t handle={.lpc_ops=&oz_aros_config_ops,.lpc_lib_handle=&allowed};
        /* Discovery returns a non-unique nvlist (duplicate pool names are
         * valid). Upstream's selector iterates it and rejects ambiguity. */
        if(!rc)rc=import_path_tests(&allowed);
        if(!rc)rc=zpool_find_config(&handle,pool,&exported,&args);
        printf("upstream label discovery=%d rejected=%d diagnostic=%s\n",rc,allowed.rejected,handle.lpc_desc);
        if(!rc) {
            uint64_t guid;
            rc=nvlist_lookup_uint64(exported,ZPOOL_CONFIG_POOL_GUID,&guid);
            if(!rc&&disks>1) {
                /* Exercise actual upstream open workers with only one
                 * fixture member admitted. Other paths must be refused at
                 * vdev open, not merely filtered from the returned config. */
                struct oz_vdev_policy *restricted=NULL,*duplicate=NULL;
                rc=oz_vdev_policy_begin(guid,scan,1,&restricted);
                if(!rc) {
                    int busy=oz_vdev_policy_begin(guid,scan,disks,&duplicate);
                    if(busy!=EBUSY||duplicate)rc=EIO;
                    if(duplicate)oz_vdev_policy_end(duplicate,NULL,NULL);
                    nvlist_t *trial=rc?NULL:spa_tryimport(exported);
                    nvlist_free(trial);
                    unsigned accepted=0,rejected=0;
                    oz_vdev_policy_end(restricted,&accepted,&rejected);
                    printf("restricted upstream opens accepted=%u rejected=%u duplicate=%d\n",accepted,rejected,busy);
                    if(!rc&&(!accepted||!rejected))rc=EIO;
                }
            }
            if(!rc)rc=oz_vdev_policy_begin(guid,scan,disks,&lifetime_policy);
        }
#endif
        nvlist_t *import_props=fnvlist_alloc();
        fnvlist_add_string(import_props,"cachefile","none");
        fnvlist_add_uint64(import_props,"readonly",1);
        if(!rc)rc=spa_import(pool,exported,import_props,0);
        printf("spa_import=%d\n",rc);
#ifdef OZ_TEST_DISCOVERY
        if(!rc) {
            nvlist_t *duplicate=NULL;
            libpc_handle_t again={.lpc_ops=&oz_aros_config_ops,.lpc_lib_handle=&allowed};
            int active=zpool_find_config(&again,pool,&duplicate,&args);
            printf("active pool excluded from discovery=%d expected=%d config=%d\n",
                active,ENOENT,duplicate!=NULL);
            if(active!=ENOENT||duplicate)rc=EIO;
            nvlist_free(duplicate);
        }
#endif
        if (!rc) {
            rc=payload(dataset,&object,B_FALSE);
            printf("dataset payload read/compare=%d bytes=16384\n",rc);
#ifdef OZ_TEST_DISCOVERY
            if(!rc) {
                char files[256];struct oz_zpl *fs=NULL;
                snprintf(files,sizeof(files),"%s/files",pool);
                rc=oz_zpl_open(files,&fs);
                /* Creation/discovery used the private root login. Drop it
                 * before spawning DOS clients: their denial/login/logout
                 * checks require an unauthenticated inherited identity. */
                if(!rc) {
                    struct TagItem logout[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
                    secLogoutA(logout);
                    if(secGetTaskOwner(FindTask(NULL))==secOWNER_SYSTEM)rc=EACCES;
                }
                /* This fixture has mode 0777 and an EVERYONE all-rights
                 * ACE, unlike the external 0644 fixture in the older suite. */
                if(!rc)rc=oz_dos_read_mount_test(fs,0);
                if(fs){int close=oz_zpl_close(fs);if(!rc)rc=close;}
                printf("discovered pool native DOS mount/read suite=%d\n",rc);
            }
#endif
            int er=spa_export(pool,NULL,B_FALSE,B_FALSE);
            /* Upstream rejects administrative export in global read-only
             * mode. Assert that protection; kernel_fini unloads the pool. */
            printf("read-only export refusal=%d expected=%d\n",er,EROFS);
            if (!rc && er!=EROFS) rc=EIO;
        }
        fnvlist_free(import_props);
        kernel_fini();
#ifdef OZ_TEST_DISCOVERY
        if(lifetime_policy) {
            unsigned accepted=0,rejected=0;
            oz_vdev_policy_end(lifetime_policy,&accepted,&rejected);
            printf("native file-open admission accepted=%u rejected=%u\n",accepted,rejected);
            if(!rc&&(!accepted||rejected))rc=EIO;
        }
#endif
    }
    fnvlist_free(exported);
    printf("RESULT native %s data roundtrip %s\n",topology,rc?"FAIL":"PASS");
    printf("RESULT native %s create/open/export %s\n",topology,rc?"FAIL":"PASS");
#ifdef OZ_TEST_DISCOVERY
    if(cold_read)printf("RESULT cold discovery verify %s\n",rc?"FAIL":"PASS");
#endif
    return rc?20:0;
}
