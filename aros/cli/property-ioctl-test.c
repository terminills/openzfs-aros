/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <sys/zfs_ioctl.h>
#include "../../lib/libzfs_core/libzfs_core_impl.h"
#include <proto/exec.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>

static int change_source(int fd,const char *dataset,int inherit,uint64_t value,int received)
{
    zfs_cmd_t command={0};
    strlcpy(command.zc_name,dataset,sizeof(command.zc_name));
    strlcpy(command.zc_value,"atime",sizeof(command.zc_value));
    command.zc_cookie=received;
    nvlist_t *props=fnvlist_alloc();char *packed=NULL;size_t size=0;
    if(!inherit) {
        fnvlist_add_uint64(props,"atime",value);
        packed=fnvlist_pack(props,&size);
        command.zc_nvlist_src=(uintptr_t)packed;command.zc_nvlist_src_size=size;
    }
    int result=lzc_ioctl_fd_os(fd,inherit?ZFS_IOC_INHERIT_PROP:ZFS_IOC_SET_PROP,&command);
    int error=result?errno:0;
    if(packed)fnvlist_pack_free(packed,size);
    nvlist_free(props);return error;
}
static int change(int fd,const char *dataset,int inherit,uint64_t value)
{return change_source(fd,dataset,inherit,value,0);}
static int change_pool(int fd,const char *pool)
{
    zfs_cmd_t command={0};
    strlcpy(command.zc_name,pool,sizeof(command.zc_name));
    nvlist_t *props=fnvlist_alloc();size_t size=0;
    fnvlist_add_string(props,"bootfs","public_mirror/files");
    char *packed=fnvlist_pack(props,&size);
    command.zc_nvlist_src=(uintptr_t)packed;command.zc_nvlist_src_size=size;
    int result=lzc_ioctl_fd_os(fd,ZFS_IOC_POOL_SET_PROPS,&command);
    int error=result?errno:0;
    fnvlist_pack_free(packed,size);nvlist_free(props);return error;
}
static int read_bootfs(const char *pool,char *out,size_t size)
{
    libzfs_handle_t *hdl=libzfs_init();if(!hdl)return errno?errno:EIO;
    zpool_handle_t *handle=zpool_open(hdl,pool);
    int error=handle?zpool_get_prop(handle,ZPOOL_PROP_BOOTFS,out,size,NULL,B_FALSE):EIO;
    if(handle)zpool_close(handle);
    libzfs_fini(hdl);return error;
}
static int read_atime(const char *dataset,uint64_t *value)
{
    libzfs_handle_t *hdl=libzfs_init();if(!hdl)return errno?errno:EIO;
    zfs_handle_t *fs=zfs_open(hdl,dataset,ZFS_TYPE_FILESYSTEM);
    if(fs){*value=zfs_prop_get_int(fs,ZFS_PROP_ATIME);zfs_close(fs);}
    libzfs_fini(hdl);return fs?0:EIO;
}
int oz_upstream_property_ioctl_test(void)
{
    const char *parent="public_mirror",*child="public_mirror/files";
    int error=0,fd=-1,switched=0;
    struct Library *secBase=NULL;
    struct TagItem quiet[]={{secT_Quiet,TRUE},{TAG_DONE,0}};
    struct TagItem reader[]={{secT_UserID,(IPTR)"ozreader"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    struct TagItem root[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    uint64_t original=0,value=0;
    char bootfs_before[ZPOOL_MAXPROPLEN],bootfs_after[ZPOOL_MAXPROPLEN];
#define CHECK(x) do {if(!(x)){bug("Public property ioctl FAIL line=%d errno=%d\n",__LINE__,errno);error=EIO;goto done;}}while(0)
    CHECK(!read_atime(parent,&original));
    CHECK(!read_bootfs(parent,bootfs_before,sizeof(bootfs_before)));
    CHECK((fd=lzc_open_os())>=0);
    for(unsigned i=0;i<4;i++) {
        CHECK(!change(fd,parent,0,i&1));
        CHECK(!change(fd,child,0,!(i&1)));
        CHECK(!read_atime(child,&value)&&value==!(i&1));
        CHECK(!change(fd,child,1,0));
        CHECK(!read_atime(child,&value)&&value==(i&1));
    }
    CHECK(!change(fd,parent,0,original));
    CHECK(!change(fd,child,0,1));
    CHECK(change(fd,child,0,2)==ZFS_ERR_BADPROP);
    CHECK(!read_atime(child,&value)&&value==1);
    /* Received values must not override a local value until inherit -S. */
    CHECK(!change_source(fd,child,0,0,1));
    CHECK(!read_atime(child,&value)&&value==1);
    CHECK(!change_source(fd,child,1,0,1));
    CHECK(!read_atime(child,&value)&&value==0);
    CHECK(!change(fd,child,0,1));
    CHECK(change_source(fd,child,1,0,2)==EINVAL);
    CHECK(!read_atime(child,&value)&&value==1);
    CHECK(!lzc_close_os(fd));fd=-1;
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase);
    secLogoutA(quiet);switched=1;
    CHECK(secLoginA(reader)==((77UL<<16)|88));
    CHECK((fd=lzc_open_os())>=0);
    int denied=change(fd,child,0,0);
    CHECK(denied==EPERM||denied==EACCES);
    denied=change(fd,child,1,0);
    CHECK(denied==EPERM||denied==EACCES);
    denied=change_pool(fd,parent);
    CHECK(denied==EPERM||denied==EACCES);
    CHECK(!lzc_close_os(fd));fd=-1;
    secLogoutA(quiet);CHECK(secLoginA(root)==secOWNER_SYSTEM);switched=0;
    CHECK(!read_atime(child,&value)&&value==1);
    CHECK(!read_bootfs(parent,bootfs_after,sizeof(bootfs_after))&&
        !strcmp(bootfs_before,bootfs_after));
    bug("Public upstream pool property authorization PASS own-session-denied=1 fresh-read=1 no-mutation=1\n");
    bug("Public upstream property ioctls PASS set=4 inherit=4 fresh-read=8 denied-set=1 denied-inherit=1 no-mutation=1\n");
    bug("Public upstream property sources PASS received=1 local-precedence=1 revert-received=1 invalid-value=1 invalid-source=1\n");
done:
    if(fd>=0&&lzc_close_os(fd)&&!error)error=errno;
    if(switched){secLogoutA(quiet);if(secLoginA(root)!=secOWNER_SYSTEM)error=EACCES;}
    if(secBase)CloseLibrary(secBase);
    return error;
#undef CHECK
}
