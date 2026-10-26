/* SPDX-License-Identifier: CDDL-1.0 */
/* Typed upstream creation/attach/replace. Native creation uses the same
 * explicit extent reservations as the upstream CLI. Never truncate files. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/spa_impl.h>
#include <sys/aros_vdev_policy.h>
#include <sys/fs/zfs.h>
#include <sys/stat.h>
#include <zfs_namecheck.h>
#include <fcntl.h>
#include <proto/dos.h>
#include <proto/exec.h>
#include <dos/dosextens.h>
#include <libraries/security.h>
#include "../ui/api.h"
#include "import-platform.h"
#include "packet-cred.h"
#include "stream-file.h"

static void role(nvlist_t *v,unsigned r)
{
    if(r==OZ_LOG)fnvlist_add_uint64(v,ZPOOL_CONFIG_IS_LOG,1);
    if(r==OZ_SPECIAL||r==OZ_DEDUP)
        fnvlist_add_string(v,ZPOOL_CONFIG_ALLOCATION_BIAS,
            r==OZ_SPECIAL?VDEV_ALLOC_BIAS_SPECIAL:VDEV_ALLOC_BIAS_DEDUP);
}

/* A distributed spare is already owned by the pool. Reuse the CLI's strict
 * live-inventory admission rather than passing its name to file preflight. */
static int replace_distributed_spare(struct oz_message *m,uint64_t guid,
    cred_t *caller,const struct oz_packet_security *security,boolean_t rebuild)
{
    nvlist_t *root=fnvlist_alloc(),*leaf=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_TYPE,VDEV_TYPE_DRAID_SPARE);
    fnvlist_add_string(leaf,ZPOOL_CONFIG_PATH,m->topology.devices[0].path);
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)&leaf,1);
    char *packed=NULL;size_t size=0;void *reservation=NULL;
    spa_t *spa=NULL;struct oz_cred_scope *scope=NULL;
    int error=nvlist_pack(root,&packed,&size,NV_ENCODE_NATIVE,0);
    if(!error)error=oz_cli_attach_begin(m->target,guid,1,packed,size,caller,security,&reservation);
    if(!error)error=spa_open(m->target,&spa,FTAG);
    if(!error)error=oz_cred_push(caller,&scope);
    if(!error)error=spa_vdev_attach(spa,guid,root,B_TRUE,rebuild);
    if(scope)VERIFY0(oz_cred_pop(scope));
    if(spa)spa_close(spa,FTAG);
    if(reservation)oz_cli_add_end(reservation,error);
    free(packed);nvlist_free(leaf);nvlist_free(root);return error;
}

/* operation: 0=create, 1=attach, 2=replace, 3=add. All share the same native
 * file identity/blankness and writable-open preflight. */
static int pool_files_change(struct oz_message *m,cred_t *caller,
    const struct oz_packet_security *security,unsigned operation)
{
    if(!security||oz_packet_security_cred(security)!=caller)return EACCES;
    int error=secpolicy_zfs(caller);
    if(error)return error;
    if(!(spa_mode_global&SPA_MODE_WRITE))return EROFS;
    if(pool_namecheck(m->target,NULL,NULL))return EINVAL;
    const char *temporary=NULL;
    if(!operation&&m->options[0]) {
        if(strncmp(m->options,"tname=",6)||pool_namecheck(m->options+6,NULL,NULL))return EINVAL;
        temporary=m->options+6;
    }
    char why[128];
    boolean_t rebuild=(operation==1||operation==2)&&!strcmp(m->options,"rebuild=on");
    boolean_t force=!operation&&(m->flags&OZ_FORCE);
    if(m->flags!=(OZ_CONFIRMED|(force?OZ_FORCE:0))||((!operation||operation==3)&&m->argument[0])||(operation&&m->options[0]&&!rebuild)||
        (operation==3?oz_validate_add_topology(&m->topology,why,sizeof(why)):
        oz_validate_topology(&m->topology,why,sizeof(why)))!=OZ_OK)return EINVAL;
    struct oz_topology *t=&m->topology;
    uint64_t guid=0;
    if(operation&&operation!=3) {
        if(operation>2||!m->argument[0]||t->group_count!=1||t->device_count!=1||
            t->groups[0].role!=OZ_DATA||t->groups[0].layout!=OZ_STRIPE)return EINVAL;
        for(const char *p=m->argument;*p;p++)if(*p<'0'||*p>'9')return EINVAL;
        errno=0;guid=strtoull(m->argument,NULL,10);
        if(errno==ERANGE||!guid)return EINVAL;
    }
    if(operation==2&&!strncmp(t->devices[0].path,"draid",5)&&
        !strchr(t->devices[0].path,':')&&!strchr(t->devices[0].path,'/'))
        return replace_distributed_spare(m,guid,caller,security,rebuild);
    BPTR locks[OZ_MAX_DEVICES]={0},parents[OZ_MAX_DEVICES]={0};
    boolean_t native[OZ_MAX_DEVICES]={0},has_native=B_FALSE;
    nvlist_t *leaves[OZ_MAX_DEVICES]={0},*groups[OZ_MAX_GROUPS]={0};
    nvlist_t *tops[OZ_MAX_DEVICES],*cache[OZ_MAX_DEVICES],*spares[OZ_MAX_DEVICES];
    unsigned nt=0,nc=0,ns=0;
    nvlist_t *root=NULL,*props=NULL;
    struct oz_cred_scope *scope=NULL;
    spa_t *spa=NULL;
    struct oz_vdev_extension *policy_added[OZ_MAX_DEVICES]={0};
    unsigned char *buf=malloc(1024*1024);
    if(!buf)return ENOMEM;
    if(operation) {
        error=spa_open(m->target,&spa,FTAG);if(error)goto done;
        if(!spa_writeable(spa)){error=EROFS;goto done;}
        if(spa_suspended(spa)){error=EAGAIN;goto done;}
    }
    error=oz_cred_push(caller,&scope);
    if(error)goto done;
    /* Hold native file locks throughout blank/identity preflight. SameLock is
     * needed: this CRT's st_ino is a path hash, not hard-link identity. */
    for(unsigned i=0;i<t->device_count&&!error;i++) {
        const char *path=t->devices[i].path;
        if(!strncmp(path,"/AROS/",6)) {
            /* All new native members are admitted together below, before
             * upstream can label them or change the live topology. */
            native[i]=has_native=B_TRUE;
            leaves[i]=fnvlist_alloc();
            fnvlist_add_string(leaves[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_DISK);
            fnvlist_add_string(leaves[i],ZPOOL_CONFIG_PATH,path);
            fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_WHOLE_DISK,0);
            if(t->ashift)fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,t->ashift);
            else if(operation==3)fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,spa->spa_max_ashift);
            continue;
        }
        char native[256],canonical[256],unixpath[258];
        const char *start=path,*rest;size_t volume;
        if(path[0]=='/') {
            const char *slash=strchr(path+1,'/');
            if(!slash||slash==path+1||!slash[1]||strchr(path,':')||
                !strncmp(path,"/AROS/",6)){error=ENOTSUP;break;}
            start=path+1;volume=slash-start;rest=slash+1;
        } else {
            const char *colon=strchr(path,':');
            if(!colon||colon==path||!colon[1]||memchr(path,'/',colon-path)) {error=ENOTSUP;break;}
            volume=colon-path;rest=colon+1;
        }
        memcpy(native,start,volume);native[volume]=0;
        native[volume]=':';
        strcpy(native+volume+1,rest);
        /* Resolve bound/deferred/multi-assigns using the captured caller,
         * never daemon identity or GetDeviceProc's implicit handler startup.
         * The shared resolver refuses our own endpoints before any packet. */
        error=oz_stream_path_lock(security,native,&locks[i]);
        if(!error)error=oz_stream_lock_access(security,locks[i],secAt_Read|secAt_Write);
        if(error)break;
        for(unsigned j=0;j<i;j++)if(SameLock(locks[i],locks[j])==LOCK_SAME)error=EEXIST;
        if(error)break;
        if(!NameFromLock(locks[i],(STRPTR)canonical,sizeof(canonical))){error=ENAMETOOLONG;break;}
        char *colon=strchr(canonical,':');
        if(!colon){error=EINVAL;break;}
        *colon='/';snprintf(unixpath,sizeof(unixpath),"/%s",canonical);
        /* Shared identity locks coexist with read opens, but native FAT
         * requires an exclusive lock for writable opens. Handoff below. */
        int fd=open(unixpath,O_RDONLY);struct stat64 st;
        if(fd<0){error=errno?errno:EIO;break;}
        if(fstat64(fd,&st))error=errno?errno:EIO;
        else if(!S_ISREG(st.st_mode))error=ENOTSUP;
        else if(st.st_size<SPA_MINDEVSIZE||(st.st_size&511))error=EINVAL;
        /* Scan blank candidates; also accept consistently labeled
         * destroyed members through the shared CLI admission validator.
         * Retained locks are not an exclusive-write transaction;
         * callers must provide exclusively provisioned backing files. */
        for(uint64_t left=error?0:st.st_size;left&&!error;) {
            size_t n=left>1024*1024?1024*1024:(size_t)left;
            ssize_t got=read(fd,buf,n);
            if(got!=(ssize_t)n){error=EIO;break;}
            for(size_t j=0;j<n;j++)if(buf[j]){error=EEXIST;break;}
            left-=n;
        }
        if(error==EEXIST)error=force?oz_exported_file_backing(fd,st.st_size):oz_destroyed_file_backing(fd,st.st_size);
        close(fd);
        if(error)break;
        leaves[i]=fnvlist_alloc();
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_TYPE,VDEV_TYPE_FILE);
        fnvlist_add_string(leaves[i],ZPOOL_CONFIG_PATH,unixpath);
        if(t->ashift)fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,t->ashift);
        else if(operation==3)fnvlist_add_uint64(leaves[i],ZPOOL_CONFIG_ASHIFT,spa->spa_max_ashift);
    }
    if(error)goto done;
    /* Native FAT cannot open a shared-locked file writable. Pin all parents
     * before releasing any file locks, then preflight write access on every
     * member before SPA can label anything. This is not atomic against an
     * external rename/write: exclusively provisioned backing files are required. */
    for(unsigned i=0;i<t->device_count;i++) {
        if(native[i])continue;
        parents[i]=ParentDir(locks[i]);
        if(!parents[i]){error=EIO;goto done;}
    }
    for(unsigned i=0;i<t->device_count;i++){if(locks[i])UnLock(locks[i]);locks[i]=0;}
    for(unsigned i=0;i<t->device_count;i++) {
        if(native[i])continue;
        const char *path=fnvlist_lookup_string(leaves[i],ZPOOL_CONFIG_PATH);
        int fd=open(path,O_RDWR);
        if(fd<0){error=errno?errno:EIO;goto done;}
        close(fd);
    }
    root=fnvlist_alloc();props=fnvlist_alloc();
    fnvlist_add_string(root,ZPOOL_CONFIG_TYPE,VDEV_TYPE_ROOT);
    fnvlist_add_string(props,"cachefile","none");
    if(temporary)fnvlist_add_string(props,"tname",temporary);
    for(unsigned g=0;g<t->group_count;g++) {
        unsigned r=t->groups[g].role,layout=t->groups[g].layout,n=0;
        nvlist_t *children[OZ_MAX_DEVICES];
        for(unsigned i=0;i<t->device_count;i++)if(t->devices[i].group==g)children[n++]=leaves[i];
        if(r==OZ_CACHE||r==OZ_SPARE) {
            for(unsigned i=0;i<n;i++)if(r==OZ_CACHE)cache[nc++]=children[i];else spares[ns++]=children[i];
            continue;
        }
        if(r==OZ_SPECIAL||r==OZ_DEDUP)fnvlist_add_uint64(props,"feature@allocation_classes",0);
        if(layout==OZ_STRIPE) {
            for(unsigned i=0;i<n;i++){role(children[i],r);tops[nt++]=children[i];}
        } else {
            groups[g]=fnvlist_alloc();role(groups[g],r);
            fnvlist_add_string(groups[g],ZPOOL_CONFIG_TYPE,layout==OZ_MIRROR?VDEV_TYPE_MIRROR:layout==OZ_DRAID?VDEV_TYPE_DRAID:VDEV_TYPE_RAIDZ);
            if(layout==OZ_DRAID) {
                unsigned data,slices;
                if(!oz_draid_geometry(&t->groups[g],n,&data,&slices)){error=EINVAL;goto done;}
                fnvlist_add_uint64(groups[g],ZPOOL_CONFIG_NPARITY,t->groups[g].draid_parity);
                fnvlist_add_uint64(groups[g],ZPOOL_CONFIG_DRAID_NDATA,data);
                fnvlist_add_uint64(groups[g],ZPOOL_CONFIG_DRAID_NSPARES,t->groups[g].draid_spares);
                fnvlist_add_uint64(groups[g],ZPOOL_CONFIG_DRAID_NGROUPS,slices);
                if(!operation)fnvlist_add_uint64(props,"feature@draid",0);
            } else if(layout>=OZ_RAIDZ1)fnvlist_add_uint64(groups[g],ZPOOL_CONFIG_NPARITY,layout-OZ_RAIDZ1+1);
            fnvlist_add_nvlist_array(groups[g],ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)children,n);
            tops[nt++]=groups[g];
        }
    }
    fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_CHILDREN,(const nvlist_t *const *)tops,nt);
    if(nc)fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_L2CACHE,(const nvlist_t *const *)cache,nc);
    if(ns)fnvlist_add_nvlist_array(root,ZPOOL_CONFIG_SPARES,(const nvlist_t *const *)spares,ns);
    if(operation&&!has_native) {
        for(unsigned i=0;i<t->device_count;i++) {
            error=oz_vdev_policy_extend(spa_guid(spa),spa_name(spa),fnvlist_lookup_string(leaves[i],ZPOOL_CONFIG_PATH),&policy_added[i]);
            if(error)goto done;
        }
    }
    if(has_native||force) {
        char *packed_root=NULL,*packed_props=NULL;size_t root_size=0,props_size=0;
        void *reservation=NULL;
        error=nvlist_pack(root,&packed_root,&root_size,NV_ENCODE_NATIVE,0);
        if(!error)error=nvlist_pack(props,&packed_props,&props_size,NV_ENCODE_NATIVE,0);
        if(!error)error=operation?
            oz_cli_add_begin(m->target,packed_root,root_size,caller,security,&reservation):
            (force?oz_cli_create_force_begin:oz_cli_create_begin)(m->target,packed_root,root_size,packed_props,props_size,caller,security,&reservation);
        if(!error)error=operation==3?spa_vdev_add(spa,root,B_TRUE):operation?
            spa_vdev_attach(spa,guid,root,operation==2,rebuild):spa_create(m->target,root,props,NULL,NULL);
        if(reservation) {
            if(operation)oz_cli_add_end(reservation,error);
            else error=oz_cli_import_end(reservation,error);
        }
        free(packed_root);free(packed_props);
    } else error=operation==3?spa_vdev_add(spa,root,B_TRUE):operation?spa_vdev_attach(spa,guid,root,operation==2,rebuild):
        spa_create(m->target,root,props,NULL,NULL);
done:
    for(unsigned i=0;i<t->device_count;i++)oz_vdev_policy_extend_end(policy_added[i],error);
    nvlist_free(root);nvlist_free(props);
    for(unsigned i=0;i<OZ_MAX_GROUPS;i++)nvlist_free(groups[i]);
    for(unsigned i=0;i<OZ_MAX_DEVICES;i++) {
        nvlist_free(leaves[i]);if(locks[i])UnLock(locks[i]);
        if(parents[i])UnLock(parents[i]);
    }
    if(scope)VERIFY0(oz_cred_pop(scope));
    if(spa)spa_close(spa,FTAG);
    free(buf);return error;
}

int oz_pool_create(struct oz_message *m,cred_t *caller,const struct oz_packet_security *security)
{return pool_files_change(m,caller,security,0);}

int oz_pool_attach(struct oz_message *m,cred_t *caller,const struct oz_packet_security *security,int replacing)
{return pool_files_change(m,caller,security,replacing?2:1);}

int oz_pool_add(struct oz_message *m,cred_t *caller,const struct oz_packet_security *security)
{return pool_files_change(m,caller,security,3);}
