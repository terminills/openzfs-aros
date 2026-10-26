/* SPDX-License-Identifier: CDDL-1.0 */
/* Real RPC client: no linked engine, pool or policy implementation. */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <libzfs_core.h>
#include "../../lib/libzfs_core/libzfs_core_impl.h"
#include "../ui/ioctl-api.h"
#define TEST(x) do {if(!(x)){char text[96];snprintf(text,sizeof(text),"Public ioctl FAIL line=%d\n",__LINE__);for(char *p=text;*p;p++)RawPutChar(*p);error=EIO;goto done;}}while(0)
static void prepare(struct oz_ioctl_wire *w,const struct oz_ticket *ticket,LONG fd,const char *pool,size_t capacity)
{
    memset(w,0,sizeof(*w)+sizeof(zfs_cmd_t)+capacity);
    w->session=*ticket;w->magic=OZ_IOCTL_WIRE_MAGIC;w->version=OZ_IOCTL_WIRE_VERSION;
    w->command_size=sizeof(zfs_cmd_t);w->command=ZFS_IOC_POOL_STATS;
    w->descriptor=fd;w->dst_capacity=capacity;
    zfs_cmd_t *command=(void *)(w+1);
    strlcpy(command->zc_name,pool,sizeof(command->zc_name));
    command->zc_nvlist_dst_size=capacity;
}
static int invoke_nvlist(const struct oz_ticket *ticket,LONG fd,unsigned operation,
    const char *name,nvlist_t *input,nvlist_t **output)
{
    *output=NULL;
    size_t packed_size=0,capacity=65536;
    char *packed=fnvlist_pack(input,&packed_size);
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+packed_size+capacity;
    struct oz_ioctl_wire *w=calloc(1,size);
    if(!w){fnvlist_pack_free(packed,packed_size);return ENOMEM;}
    prepare(w,ticket,fd,name,0);
    w->command=operation;w->src_size=packed_size;w->dst_capacity=capacity;
    zfs_cmd_t *command=(void *)(w+1);
    command->zc_nvlist_src_size=packed_size;command->zc_nvlist_dst_size=capacity;
    memcpy(command+1,packed,packed_size);fnvlist_pack_free(packed,packed_size);
    int error=oz_ioctl_request(OZ_ADMISSION_PORT,w,size);
    if(!error)error=w->engine_error;
    if(!error && (command->zc_nvlist_src||command->zc_nvlist_dst||command->zc_iflags))error=EIO;
    if(!error && command->zc_nvlist_dst_size>capacity)error=EOVERFLOW;
    if(!error)error=nvlist_unpack((char *)(command+1)+packed_size,command->zc_nvlist_dst_size,output,0);
    free(w);return error;
}
static int upstream_enumeration(const char *pool,const char *filesystem,const char *snapshot)
{
    int error=0,fd=-1;char output[65536];nvlist_t *properties=NULL;
    zfs_cmd_t command={0};
    TEST((fd=lzc_open_os())>=0);
    strlcpy(command.zc_name,pool,sizeof(command.zc_name));
    command.zc_nvlist_dst=(uintptr_t)output;command.zc_nvlist_dst_size=sizeof(output);
    TEST(!lzc_ioctl_fd_os(fd,ZFS_IOC_DATASET_LIST_NEXT,&command));
    TEST(!strcmp(command.zc_name,filesystem));TEST(command.zc_cookie!=0);
    TEST(command.zc_nvlist_dst_filled);
    TEST(!nvlist_unpack(output,command.zc_nvlist_dst_size,&properties,0));
    nvlist_free(properties);properties=NULL;
    strlcpy(command.zc_name,pool,sizeof(command.zc_name));
    command.zc_nvlist_dst_size=sizeof(output);
    TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_DATASET_LIST_NEXT,&command)==-1&&errno==ESRCH);
    memset(&command,0,sizeof(command));
    strlcpy(command.zc_name,filesystem,sizeof(command.zc_name));
    /* Snapshot properties can exceed the initial reply allocation. Preserve
     * ENOMEM and the required size, without copying a partial reply; reset
     * the iterator cookie just as upstream zfs_do_list_ioctl does on retry. */
    output[0]=0x5a;
    command.zc_nvlist_dst=(uintptr_t)output;command.zc_nvlist_dst_size=1;
    TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_SNAPSHOT_LIST_NEXT,&command)==-1&&errno==ENOMEM);
    TEST(command.zc_nvlist_dst_size>1&&command.zc_nvlist_dst_size<=sizeof(output));
    TEST(command.zc_nvlist_dst==(uintptr_t)output&&output[0]==0x5a);
    command.zc_cookie=0;
    strlcpy(command.zc_name,filesystem,sizeof(command.zc_name));
    command.zc_nvlist_dst=(uintptr_t)output;command.zc_nvlist_dst_size=sizeof(output);
    TEST(!lzc_ioctl_fd_os(fd,ZFS_IOC_SNAPSHOT_LIST_NEXT,&command));
    TEST(!strcmp(command.zc_name,snapshot));TEST(command.zc_cookie!=0);
    TEST(command.zc_objset_stats.dds_is_snapshot);
    TEST(command.zc_nvlist_dst_filled);
    TEST(!nvlist_unpack(output,command.zc_nvlist_dst_size,&properties,0));
    strlcpy(command.zc_name,filesystem,sizeof(command.zc_name));
    command.zc_nvlist_dst_size=sizeof(output);
    TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_SNAPSHOT_LIST_NEXT,&command)==-1&&errno==ESRCH);
    const char *s="Public upstream enumeration PASS dataset=1 snapshot=1 properties=1 end=1 snapshot-growth=1\n";
    while(*s)RawPutChar(*s++);
done:
    nvlist_free(properties);if(fd>=0&&lzc_close_os(fd)&&!error)error=errno;return error;
}
static int upstream_core_datasets(const char *pool)
{
    int error=0,initialized=0;
    nvlist_t *props=fnvlist_alloc(),*snaps=fnvlist_alloc(),*result=NULL;
    char filesystem[256],snapshot[280],clone[256],volume[256],target[280];
    snprintf(filesystem,sizeof(filesystem),"%s/files",pool);
    snprintf(snapshot,sizeof(snapshot),"%s@core-baseline",filesystem);
    snprintf(clone,sizeof(clone),"%s/core-clone",pool);
    snprintf(volume,sizeof(volume),"%s/core-volume",pool);
    TEST(!libzfs_core_init());initialized=1;
    TEST(!lzc_create(filesystem,LZC_DATSET_TYPE_ZFS,props,NULL,0));
    TEST(lzc_exists(filesystem));
    TEST(lzc_create(filesystem,LZC_DATSET_TYPE_ZFS,props,NULL,0)==EEXIST);
    fnvlist_add_boolean(snaps,snapshot);
    TEST(!lzc_snapshot(snaps,NULL,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    TEST(!upstream_enumeration(pool,filesystem,snapshot));
    TEST(!lzc_clone(clone,snapshot,props));TEST(lzc_exists(clone));
    TEST(!lzc_destroy(clone));TEST(!lzc_exists(clone));
    TEST(!lzc_rollback(filesystem,target,sizeof(target)));TEST(!strcmp(target,snapshot));
    TEST(!lzc_destroy_snaps(snaps,B_FALSE,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    fnvlist_add_uint64(props,"volsize",1025);
    fnvlist_add_uint64(props,"volblocksize",8192);
    TEST(lzc_create(volume,LZC_DATSET_TYPE_ZVOL,props,NULL,0)==EINVAL);
    TEST(!lzc_exists(volume));
    fnvlist_add_uint64(props,"volsize",1024*1024);
    TEST(!lzc_create(volume,LZC_DATSET_TYPE_ZVOL,props,NULL,0));TEST(lzc_exists(volume));
    TEST(!lzc_destroy(volume));TEST(!lzc_exists(volume));
    /* Leave the actual upstream-created filesystem for the subsequent native
     * DOS mount/write/read/export/reimport test in public-service-test.c. */
    TEST(lzc_exists(filesystem));
    const char *s="Public upstream dataset ioctls PASS create=1 clone=1 rollback=1 volume=1 handler-ready=1\n";
    while(*s)RawPutChar(*s++);
done:
    if(initialized)libzfs_core_fini();
    nvlist_free(props);nvlist_free(snaps);nvlist_free(result);return error;
}
static int same_nvlist(nvlist_t *a,nvlist_t *b)
{
    size_t as=0,bs=0;
    char *ap=fnvlist_pack(a,&as),*bp=fnvlist_pack(b,&bs);
    int same=as==bs&&!memcmp(ap,bp,as);
    fnvlist_pack_free(ap,as);fnvlist_pack_free(bp,bs);return same;
}
int oz_public_accounting_upgrade_test(const char *dataset,int readonly)
{
    int error=0;LONG fd=-1;zfs_cmd_t command={0};
    fd=lzc_open_os();TEST(fd>0);
    strlcpy(command.zc_name,dataset,sizeof(command.zc_name));
    if(readonly){TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_USERSPACE_UPGRADE,&command)==-1&&errno==EROFS);}
    else {
        TEST(!lzc_ioctl_fd_os(fd,ZFS_IOC_USERSPACE_UPGRADE,&command));
        TEST(!lzc_ioctl_fd_os(fd,ZFS_IOC_USERSPACE_UPGRADE,&command));
    }
    command.zc_cookie=1;
    TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_USERSPACE_UPGRADE,&command)==-1&&errno==EINVAL);
    command.zc_cookie=0;
    strlcat(command.zc_name,"/missing-upgrade-fixture",sizeof(command.zc_name));
    /* Upstream checks pool readonly state before dataset existence. */
    TEST(lzc_ioctl_fd_os(fd,ZFS_IOC_USERSPACE_UPGRADE,&command)==-1&&errno==(readonly?EROFS:ENOENT));
done:
    if(fd>0&&lzc_close_os(fd)&&!error)error=errno;
    if(!error){const char *p=readonly?
        "Public accounting upgrade PASS readonly=1 malformed=1 absent-gated=1\n":
        "Public accounting upgrade PASS writable=1 repeat=1 malformed=1 absent=1\n";
        while(*p)RawPutChar(*p++);}
    return error;
}
int oz_public_bootenv_persistence_test(const char *pool,int write)
{
    int error=0,initialized=0;
    nvlist_t *record=NULL,*input=fnvlist_alloc();
    TEST(!libzfs_core_init());initialized=1;
    if(write){
        /* Dedicated disposable pool only: retain this marker for cold boot. */
        fnvlist_add_uint64(input,"version",1);
        fnvlist_add_string(input,"aros-cold-test","bootenv-persistent-v1");
        TEST(!lzc_set_bootenv(pool,input));
    }
    TEST(!lzc_get_bootenv(pool,&record));
    const char *value=NULL;uint64_t version=0;
    TEST(!nvlist_lookup_uint64(record,"version",&version)&&version==1);
    TEST(!nvlist_lookup_string(record,"aros-cold-test",&value));
    TEST(!strcmp(value,"bootenv-persistent-v1"));
done:
    if(initialized)libzfs_core_fini();
    nvlist_free(record);nvlist_free(input);
    if(!error){
        const char *p=write?"Public upstream bootenv persistence PASS write=1 readback=1\n":
            "Public upstream bootenv persistence PASS cold=1 marker=1 version=1\n";
        while(*p)RawPutChar(*p++);
    }
    return error;
}
int oz_public_bootenv_readonly_test(const char *pool)
{
    int error=0,initialized=0;
    nvlist_t *before=NULL,*after=NULL,*input=fnvlist_alloc();
    TEST(!libzfs_core_init());initialized=1;
    TEST(!lzc_get_bootenv(pool,&before));
    fnvlist_add_uint64(input,"version",1);
    fnvlist_add_string(input,"aros-test","readonly-must-not-write");
    TEST(lzc_set_bootenv(pool,input)==EROFS);
    TEST(!lzc_get_bootenv(pool,&after));TEST(same_nvlist(before,after));
done:
    if(initialized)libzfs_core_fini();
    nvlist_free(before);nvlist_free(after);nvlist_free(input);
    if(!error){const char *p="Public upstream bootenv readonly PASS get=1 set-erofs=1 unchanged=1\n";while(*p)RawPutChar(*p++);}
    return error;
}
static int upstream_bootenv(const char *pool)
{
    int error=0,initialized=0,changed=0;
    nvlist_t *saved=NULL,*written=fnvlist_alloc(),*readback=NULL,*invalid=fnvlist_alloc();
    TEST(!libzfs_core_init());initialized=1;
    TEST(!lzc_get_bootenv(pool,&saved));
    /* VB_NVLIST is upstream's version 1, not an AROS private format. */
    fnvlist_add_uint64(written,"version",1);
    fnvlist_add_string(written,"aros-test","upstream-label-roundtrip");
    TEST(!lzc_set_bootenv(pool,written));changed=1;
    TEST(!lzc_get_bootenv(pool,&readback));
    const char *value=NULL;
    TEST(!nvlist_lookup_string(readback,"aros-test",&value));
    TEST(!strcmp(value,"upstream-label-roundtrip"));
    nvlist_free(readback);readback=NULL;
    /* A missing mandatory version must not replace the accepted label. */
    TEST(lzc_set_bootenv(pool,invalid)!=0);
    TEST(!lzc_get_bootenv(pool,&readback));
    TEST(!nvlist_lookup_string(readback,"aros-test",&value));
    TEST(!strcmp(value,"upstream-label-roundtrip"));
done:
    if(changed&&saved){
        int rc=lzc_set_bootenv(pool,saved);
        if(!rc){
            nvlist_free(readback);readback=NULL;
            rc=lzc_get_bootenv(pool,&readback);
            if(!rc){
                size_t expected_size=0,actual_size=0;
                char *expected=fnvlist_pack(saved,&expected_size);
                char *actual=fnvlist_pack(readback,&actual_size);
                if(expected_size!=actual_size||memcmp(expected,actual,expected_size))rc=EIO;
                fnvlist_pack_free(expected,expected_size);
                fnvlist_pack_free(actual,actual_size);
            }
        }
        if(rc){
            const char *p="Public upstream bootenv RESTORE FAIL\n";
            while(*p)RawPutChar(*p++);
            if(!error)error=rc;
        }
    }
    if(initialized)libzfs_core_fini();
    nvlist_free(saved);nvlist_free(written);nvlist_free(readback);nvlist_free(invalid);
    if(!error){const char *p="Public upstream bootenv PASS get=1 set=1 invalid-preserved=1 restored=1\n";while(*p)RawPutChar(*p++);}
    return error;
}
static int upstream_core_commands(const char *pool)
{
    int error=0,initialized=0;LONG cleanup=-1;
    nvlist_t *entries=fnvlist_alloc(),*result=NULL;
    char snapshot[256];snprintf(snapshot,sizeof(snapshot),"%s@libzfs-core",pool);
    TEST(!libzfs_core_init());initialized++;
    TEST(!libzfs_core_init());initialized++;
    libzfs_core_fini();initialized--;
    fnvlist_add_boolean(entries,snapshot);
    TEST(!lzc_snapshot(entries,NULL,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    fnvlist_free(entries);entries=fnvlist_alloc();
    cleanup=lzc_open_os();TEST(cleanup>0);
    fnvlist_add_string(entries,snapshot,"real-libzfs-core");
    TEST(!lzc_hold(entries,cleanup,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    TEST(!lzc_get_holds(snapshot,&result));TEST(nvlist_exists(result,"real-libzfs-core"));
    nvlist_free(result);result=NULL;
    TEST(!lzc_close_os(cleanup));cleanup=-1;
    TEST(!lzc_get_holds(snapshot,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    fnvlist_free(entries);entries=fnvlist_alloc();fnvlist_add_boolean(entries,snapshot);
    TEST(!lzc_destroy_snaps(entries,B_FALSE,&result));TEST(nvlist_empty(result));
    nvlist_free(result);result=NULL;
    libzfs_core_fini();initialized--;
    /* Fresh connection after final fini, not reuse of a stale ticket. */
    TEST(!libzfs_core_init());initialized++;
    TEST(lzc_get_holds(snapshot,&result)==ENOENT);
    const char *s="Public upstream libzfs_core PASS snapshot=1 temporary-hold=1 destroy=1 reopen=1\n";
    while(*s)RawPutChar(*s++);
done:
    if(cleanup>0)(void)lzc_close_os(cleanup);
    while(initialized-->0)libzfs_core_fini();
    nvlist_free(entries);nvlist_free(result);return error;
}
static int denied_snapshot(const char *pool,const struct oz_ticket *root_ticket,LONG root_fd)
{
    int error=0,switched=0,opened=0;LONG fd=-1;struct oz_ticket ticket={0};
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    nvlist_t *input=fnvlist_alloc(),*snaps=fnvlist_alloc(),*result=NULL;
    char snapshot[256];snprintf(snapshot,sizeof(snapshot),"%s@denied-public-ioctl",pool);
    fnvlist_add_boolean(snaps,snapshot);fnvlist_add_nvlist(input,"snaps",snaps);
    TEST(secBase!=NULL);
    secLogoutA(quiet);switched=1;
    TEST(secLoginA(reader)==((77UL<<16)|88));
    /* Existing privileged ticket must not be usable under the new identity. */
    TEST(invoke_nvlist(root_ticket,root_fd,ZFS_IOC_SNAPSHOT,pool,input,&result)==EACCES);
    TEST(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    TEST(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    int denied=invoke_nvlist(&ticket,fd,ZFS_IOC_SNAPSHOT,pool,input,&result);
    TEST(denied==EPERM||denied==EACCES);
done:
    if(fd>0)(void)oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);
    if(opened)(void)oz_session_close(OZ_ADMISSION_PORT,&ticket);
    if(switched){secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;}
    if(secBase)CloseLibrary(secBase);
    nvlist_free(input);nvlist_free(snaps);nvlist_free(result);
    if(!error) {
        input=fnvlist_alloc();result=NULL;
        int status=invoke_nvlist(root_ticket,root_fd,ZFS_IOC_GET_HOLDS,snapshot,input,&result);
        nvlist_free(input);nvlist_free(result);
        if(status!=ENOENT)error=EIO;
    }
    if(!error) {
        const char *s="Public upstream snapshot authorization PASS changed-user=1 own-session-denied=1 no-mutation=1\n";
        while(*s)RawPutChar(*s++);
    }
    return error;
}
static int invoke_accounting_upgrade(const struct oz_ticket *ticket,LONG fd,const char *dataset)
{
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *w=calloc(1,size);
    if(!w)return ENOMEM;
    prepare(w,ticket,fd,dataset,0);w->command=ZFS_IOC_USERSPACE_UPGRADE;
    int error=oz_ioctl_request(OZ_ADMISSION_PORT,w,size);
    if(!error)error=w->engine_error;
    free(w);return error;
}
static int accounting_upgrade_access(const char *dataset,const struct oz_ticket *root_ticket,LONG root_fd,int allowed)
{
    int error=0,switched=0,opened=0;LONG fd=-1;struct oz_ticket ticket={0};
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    TEST(secBase!=NULL);
    secLogoutA(quiet);switched=1;
    TEST(secLoginA(reader)==((77UL<<16)|88));
    TEST(invoke_accounting_upgrade(root_ticket,root_fd,dataset)==EACCES);
    TEST(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    TEST(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    int denied=invoke_accounting_upgrade(&ticket,fd,dataset);
    TEST(allowed ? denied==0 : (denied==EPERM||denied==EACCES));
done:
    if(fd>0)(void)oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);
    if(opened)(void)oz_session_close(OZ_ADMISSION_PORT,&ticket);
    if(switched){secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;}
    if(secBase)CloseLibrary(secBase);
    if(!error&&!allowed){const char *p="Public accounting upgrade authorization PASS changed-user=1 own-session-denied=1\n";while(*p)RawPutChar(*p++);}
    return error;
}
static int accounting_upgrade_grant(const char *dataset,const struct oz_ticket *ticket,LONG fd,int revoke)
{
    nvlist_t *acl=fnvlist_alloc(),*permissions=fnvlist_alloc();
    fnvlist_add_boolean(permissions,"version");
    fnvlist_add_nvlist(acl,"ul$77",permissions);
    size_t packed_size=0;
    char *packed=fnvlist_pack(acl,&packed_size);
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+packed_size;
    struct oz_ioctl_wire *w=calloc(1,size);
    int error=ENOMEM;
    if(w){
        prepare(w,ticket,fd,dataset,0);
        w->command=ZFS_IOC_SET_FSACL;w->src_size=packed_size;
        zfs_cmd_t *command=(void *)(w+1);
        command->zc_nvlist_src_size=packed_size;command->zc_perm_action=revoke;
        memcpy(command+1,packed,packed_size);
        error=oz_ioctl_request(OZ_ADMISSION_PORT,w,size);
        if(!error)error=w->engine_error;
    }
    free(w);fnvlist_pack_free(packed,packed_size);
    fnvlist_free(permissions);fnvlist_free(acl);return error;
}
static int delegated_accounting_upgrade(const char *dataset,const struct oz_ticket *ticket,LONG fd)
{
    int error=0,granted=0;
    TEST(!accounting_upgrade_access(dataset,ticket,fd,0));
    TEST(!accounting_upgrade_grant(dataset,ticket,fd,0));granted=1;
    TEST(!accounting_upgrade_access(dataset,ticket,fd,1));
    TEST(!accounting_upgrade_grant(dataset,ticket,fd,1));granted=0;
    TEST(!accounting_upgrade_access(dataset,ticket,fd,0));
    {const char *p="Public accounting upgrade delegation PASS denied=1 granted=1 revoked=1\n";while(*p)RawPutChar(*p++);}
done:
    if(granted){int cleanup=accounting_upgrade_grant(dataset,ticket,fd,1);if(!error)error=cleanup;}
    return error;
}
static int denied_bootenv(const char *pool,const struct oz_ticket *root_ticket,LONG root_fd)
{
    int error=0,switched=0,opened=0;LONG fd=-1;struct oz_ticket ticket={0};
    struct Library *secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    nvlist_t *empty=fnvlist_alloc(),*input=fnvlist_alloc(),*before=NULL,*result=NULL;
    fnvlist_add_uint64(input,"version",1);
    fnvlist_add_string(input,"aros-test","unprivileged-must-not-write");
    TEST(secBase!=NULL);
    TEST(!invoke_nvlist(root_ticket,root_fd,ZFS_IOC_GET_BOOTENV,pool,empty,&before));
    secLogoutA(quiet);switched=1;
    TEST(secLoginA(reader)==((77UL<<16)|88));
    TEST(invoke_nvlist(root_ticket,root_fd,ZFS_IOC_SET_BOOTENV,pool,input,&result)==EACCES);
    TEST(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    TEST(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    int denied=invoke_nvlist(&ticket,fd,ZFS_IOC_SET_BOOTENV,pool,input,&result);
    TEST(denied==EPERM||denied==EACCES);
    nvlist_free(result);result=NULL;
    TEST(!invoke_nvlist(&ticket,fd,ZFS_IOC_GET_BOOTENV,pool,empty,&result));
    TEST(same_nvlist(before,result));
done:
    if(fd>0)(void)oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);
    if(opened)(void)oz_session_close(OZ_ADMISSION_PORT,&ticket);
    if(switched){secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;}
    if(!error){
        nvlist_free(result);result=NULL;
        error=invoke_nvlist(root_ticket,root_fd,ZFS_IOC_GET_BOOTENV,pool,empty,&result);
        if(!error&&!same_nvlist(before,result))error=EIO;
    }
    if(secBase)CloseLibrary(secBase);
    nvlist_free(empty);nvlist_free(input);nvlist_free(before);nvlist_free(result);
    if(!error){const char *p="Public upstream bootenv authorization PASS changed-user=1 own-session-denied=1 reader-get=1 unchanged=1\n";while(*p)RawPutChar(*p++);}
    return error;
}
static int snapshot_commands(const char *pool,const struct oz_ticket *ticket,LONG fd)
{
    int error=0;LONG cleanup=-1;
    char snapshot[256];snprintf(snapshot,sizeof(snapshot),"%s@public-ioctl",pool);
    nvlist_t *input=fnvlist_alloc(),*entries=fnvlist_alloc(),*result=NULL;
    TEST(invoke_nvlist(ticket,fd,ZFS_IOC_SNAPSHOT,pool,input,&result)==ZFS_ERR_IOC_ARG_REQUIRED);
    fnvlist_add_boolean(entries,snapshot);fnvlist_add_nvlist(input,"snaps",entries);
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_SNAPSHOT,pool,input,&result));
    TEST(nvlist_empty(result));nvlist_free(result);result=NULL;
    fnvlist_free(input);input=fnvlist_alloc();fnvlist_free(entries);entries=fnvlist_alloc();
    TEST(!oz_control_open(OZ_ADMISSION_PORT,ticket,&cleanup));
    fnvlist_add_string(entries,snapshot,"temporary-rpc-hold");
    fnvlist_add_nvlist(input,"holds",entries);fnvlist_add_int32(input,"cleanup_fd",cleanup);
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_HOLD,pool,input,&result));
    TEST(nvlist_empty(result));nvlist_free(result);result=NULL;
    fnvlist_free(input);input=fnvlist_alloc();
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_GET_HOLDS,snapshot,input,&result));
    TEST(nvlist_exists(result,"temporary-rpc-hold"));nvlist_free(result);result=NULL;
    TEST(!oz_control_close(OZ_ADMISSION_PORT,ticket,cleanup));cleanup=-1;
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_GET_HOLDS,snapshot,input,&result));
    TEST(nvlist_empty(result));nvlist_free(result);result=NULL;
    /* A non-temporary hold is released through the real RELEASE ioctl. */
    fnvlist_add_nvlist(input,"holds",entries);
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_HOLD,pool,input,&result));
    nvlist_free(result);result=NULL;
    fnvlist_free(input);input=fnvlist_alloc();fnvlist_free(entries);entries=fnvlist_alloc();
    fnvlist_add_boolean(entries,"temporary-rpc-hold");fnvlist_add_nvlist(input,snapshot,entries);
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_RELEASE,pool,input,&result));
    TEST(nvlist_empty(result));nvlist_free(result);result=NULL;
    fnvlist_free(input);input=fnvlist_alloc();fnvlist_free(entries);entries=fnvlist_alloc();
    fnvlist_add_boolean(entries,snapshot);fnvlist_add_nvlist(input,"snaps",entries);
    TEST(!invoke_nvlist(ticket,fd,ZFS_IOC_DESTROY_SNAPS,pool,input,&result));
    TEST(nvlist_empty(result));nvlist_free(result);result=NULL;
    fnvlist_free(input);input=fnvlist_alloc();
    TEST(invoke_nvlist(ticket,fd,ZFS_IOC_GET_HOLDS,snapshot,input,&result)==ENOENT);
    const char *s="Public upstream snapshot ioctls PASS create=1 hold=1 onexit=1 release=1 destroy=1 schema=1\n";
    while(*s)RawPutChar(*s++);
done:
    if(cleanup>0)(void)oz_control_close(OZ_ADMISSION_PORT,ticket,cleanup);
    nvlist_free(input);nvlist_free(entries);nvlist_free(result);return error;
}
int oz_public_ioctl_test(const char *pool)
{
    struct oz_ticket ticket={0};LONG fd=-1;int error=0,opened=0;
    size_t capacity=1024*1024,length=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+capacity;
    struct oz_ioctl_wire *w=malloc(length);nvlist_t *result=NULL;
    TEST(w!=NULL);TEST(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    TEST(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    zfs_cmd_t *command=(void *)(w+1);
    prepare(w,&ticket,fd,pool,1);
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)+1));
    TEST(w->engine_error==ENOMEM && command->zc_nvlist_dst_size>1 && !command->zc_nvlist_dst);
    prepare(w,&ticket,fd,pool,capacity);
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(!w->engine_error);
    TEST(!command->zc_nvlist_dst && !command->zc_iflags && !command->zc_cookie);
    TEST(command->zc_nvlist_dst_size>0 && command->zc_nvlist_dst_size<=capacity);
    TEST(!nvlist_unpack((char *)(command+1),command->zc_nvlist_dst_size,&result,0));
    const char *name=NULL;TEST(!nvlist_lookup_string(result,ZPOOL_CONFIG_POOL_NAME,&name));
    TEST(!strcmp(name,pool));nvlist_free(result);result=NULL;
    /* Exercise the real CLEAR ioctl rather than only Manager operation 17.
     * The engine must retain its native admission checks on this entry point. */
    prepare(w,&ticket,fd,pool,0);w->command=ZFS_IOC_CLEAR;
    command->zc_cookie=ZPOOL_NO_REWIND;
    command->zc_guid=UINT64_MAX;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)));
    TEST(w->engine_error==ENODEV);
    prepare(w,&ticket,fd,pool,0);w->command=ZFS_IOC_CLEAR;
    command->zc_cookie=ZPOOL_NO_REWIND;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)));
    TEST(!w->engine_error);
    const char *clear_marker="Public native clear ioctl PASS invalid-guid=1 pool-clear=1\n";
    for(const char *p=clear_marker;*p;p++)RawPutChar(*p);
    prepare(w,&ticket,fd,pool,capacity);w->command=ZFS_IOC_POOL_CONFIGS;
    command->zc_name[0]=0;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(!w->engine_error);
    TEST(command->zc_nvlist_dst_size>0 && command->zc_nvlist_dst_size<=capacity);
    TEST(!nvlist_unpack((char *)(command+1),command->zc_nvlist_dst_size,&result,0));
    nvlist_t *config=NULL;TEST(!nvlist_lookup_nvlist(result,pool,&config));
    nvlist_free(result);result=NULL;
    /* Each native control file owns an upstream event cursor. An undersized
     * response must not consume its event; seek-start must reproduce its ID. */
    prepare(w,&ticket,fd,"",1);w->command=ZFS_IOC_EVENTS_NEXT;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_NONBLOCK;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)+1));
    TEST(w->engine_error==ENOMEM&&command->zc_nvlist_dst_size>1);
    prepare(w,&ticket,fd,"",capacity);w->command=ZFS_IOC_EVENTS_NEXT;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_NONBLOCK;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(!w->engine_error);
    TEST(command->zc_nvlist_dst_size<=capacity);
    TEST(!nvlist_unpack((char *)(command+1),command->zc_nvlist_dst_size,&result,0));
    uint64_t event_id=0,replayed_id=0;
    TEST(!nvlist_lookup_uint64(result,"eid",&event_id));
    nvlist_free(result);result=NULL;
    prepare(w,&ticket,fd,"",0);w->command=ZFS_IOC_EVENTS_SEEK;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_SEEK_START;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)));TEST(!w->engine_error);
    prepare(w,&ticket,fd,"",capacity);w->command=ZFS_IOC_EVENTS_NEXT;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_NONBLOCK;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(!w->engine_error);
    TEST(command->zc_nvlist_dst_size<=capacity);
    TEST(!nvlist_unpack((char *)(command+1),command->zc_nvlist_dst_size,&result,0));
    TEST(!nvlist_lookup_uint64(result,"eid",&replayed_id)&&replayed_id==event_id);
    nvlist_free(result);result=NULL;
    prepare(w,&ticket,fd,"",0);w->command=ZFS_IOC_EVENTS_SEEK;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_SEEK_END;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,sizeof(*w)+sizeof(*command)));TEST(!w->engine_error);
    prepare(w,&ticket,fd,"",capacity);w->command=ZFS_IOC_EVENTS_NEXT;
    command->zc_cleanup_fd=fd;command->zc_guid=ZEVENT_NONBLOCK;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(w->engine_error==ENOENT);
    prepare(w,&ticket,fd,"",capacity);w->command=ZFS_IOC_EVENTS_NEXT;
    command->zc_cleanup_fd=fd+100;command->zc_guid=ZEVENT_NONBLOCK;
    TEST(!oz_ioctl_request(OZ_ADMISSION_PORT,w,length));TEST(w->engine_error==EBADF);
    const char *event_marker="RESULT native event cursor PASS tiny-retry=1 seek-start-eid=1 seek-end-empty=1 bad-handle=1\n";
    for(const char *p=event_marker;*p;p++)RawPutChar(*p);
    prepare(w,&ticket,fd,pool,capacity);w->reserved=1;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==EINVAL);
    prepare(w,&ticket,fd,pool,capacity);command->zc_nvlist_dst=1;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==EINVAL);
    prepare(w,&ticket,fd,pool,capacity);w->session.epoch^=1;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==ENOENT);
    prepare(w,&ticket,fd,pool,capacity);w->descriptor=fd+100;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==EBADF);
    prepare(w,&ticket,fd,pool,capacity);
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length-1)==EINVAL);
    prepare(w,&ticket,fd,pool,capacity);w->command=ZFS_IOC_LAST;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==ENOTSUP);
    TEST(!snapshot_commands(pool,&ticket,fd));
    TEST(!denied_snapshot(pool,&ticket,fd));
    TEST(!denied_bootenv(pool,&ticket,fd));
    TEST(!upstream_core_commands(pool));
    TEST(!upstream_bootenv(pool));
    TEST(!upstream_core_datasets(pool));
    char accounting_dataset[256];
    snprintf(accounting_dataset,sizeof(accounting_dataset),"%s/files",pool);
    TEST(!delegated_accounting_upgrade(accounting_dataset,&ticket,fd));
    TEST(!oz_public_accounting_upgrade_test(accounting_dataset,0));
    TEST(!oz_control_close(OZ_ADMISSION_PORT,&ticket,fd));
    prepare(w,&ticket,fd,pool,capacity);fd=-1;
    TEST(oz_ioctl_request(OZ_ADMISSION_PORT,w,length)==EBADF);
    const char *s="Public upstream ioctl transport PASS stats=1 configs=1 resize=1 bounds=1 stale=1\n";
    while(*s)RawPutChar(*s++);
done:
    if(result)nvlist_free(result);
    if(fd>0)(void)oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);
    if(opened)(void)oz_session_close(OZ_ADMISSION_PORT,&ticket);
    free(w);return error;
}
