/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <aros/debug.h>
#include "../ui/ioctl-api.h"

static void prepare(struct oz_ioctl_wire *w,const struct oz_ticket *ticket,
    LONG fd,unsigned operation,const char *name,size_t capacity)
{
    memset(w,0,sizeof(*w)+sizeof(zfs_cmd_t)+capacity);
    w->magic=OZ_IOCTL_WIRE_MAGIC;w->version=OZ_IOCTL_WIRE_VERSION;
    w->session=*ticket;w->descriptor=fd;w->command=operation;
    w->command_size=sizeof(zfs_cmd_t);w->dst_capacity=capacity;
    zfs_cmd_t *c=(void *)(w+1);
    strlcpy(c->zc_name,name,sizeof(c->zc_name));c->zc_nvlist_dst_size=capacity;
}
int oz_error_log_denied_test(void)
{
    const size_t capacity=sizeof(zbookmark_phys_t);
    const size_t base=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *w=calloc(1,base+capacity);
    struct oz_ticket ticket={0};LONG fd=-1;int opened=0,error=ENOMEM;
    if(!w)return error;
    error=oz_session_open(OZ_ADMISSION_PORT,&ticket);
    if(error)goto done;
    opened=1;error=oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd);
    if(error)goto done;
    prepare(w,&ticket,fd,ZFS_IOC_ERROR_LOG,"qemu_mirror",capacity);
    error=oz_ioctl_request(OZ_ADMISSION_PORT,w,base+capacity);
    if(!error)error=w->engine_error;
    error=(error==EPERM||error==EACCES)?0:EIO;
done:
    if(fd>0&&oz_control_close(OZ_ADMISSION_PORT,&ticket,fd))error=EIO;
    if(opened&&oz_session_close(OZ_ADMISSION_PORT,&ticket))error=EIO;
    free(w);
    if(!error)bug("RESULT native error log privilege PASS nonroot-denied=1\n");
    return error;
}

int oz_error_log_wire_test(uint64_t dsobj,int seeded)
{
    const size_t capacity=4*sizeof(zbookmark_phys_t);
    const size_t base=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *w=calloc(1,base+capacity+16);
    struct oz_ticket ticket={0};LONG fd=-1;int opened=0,error=0;
    zfs_cmd_t *c=w?(void *)(w+1):NULL;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(w);CHECK(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    CHECK(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    /* fib_DiskKey is a DOS enumeration cursor, not a ZFS object number.
     * Resolve the real identity through upstream object iteration/path APIs. */
    uint64_t cursor=0,object=0;
    for(unsigned i=0;i<128&&!object;i++) {
        prepare(w,&ticket,fd,ZFS_IOC_NEXT_OBJ,"qemu_mirror/error-log",0);c->zc_obj=cursor;
        CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base)&&!w->engine_error);
        CHECK(c->zc_obj>cursor);cursor=c->zc_obj;
        prepare(w,&ticket,fd,ZFS_IOC_OBJ_TO_PATH,"qemu_mirror/error-log",0);c->zc_obj=cursor;
        CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base));
        if(!w->engine_error&&!strcmp(c->zc_value,"/evidence"))object=cursor;
    }
    CHECK(object);
    prepare(w,&ticket,fd,ZFS_IOC_ERROR_LOG,"qemu_mirror",sizeof(zbookmark_phys_t));
    memset((char *)w+base+sizeof(zbookmark_phys_t),0xa5,16);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base+sizeof(zbookmark_phys_t)));
    CHECK(w->engine_error==(seeded?ENOMEM:0));
    CHECK(c->zc_nvlist_dst_size<=(seeded?0:1));
    for(unsigned i=0;i<16;i++)CHECK(((unsigned char *)w)[base+sizeof(zbookmark_phys_t)+i]==0xa5);
    for(unsigned bad=0;bad<10;bad++) {
        prepare(w,&ticket,fd,ZFS_IOC_ERROR_LOG,"qemu_mirror",capacity);
        switch(bad) {
        case 0:c->zc_cookie=1;break;case 1:c->zc_obj=1;break;
        case 2:c->zc_guid=1;break;case 3:c->zc_flags=1;break;
        case 4:c->zc_history_offset=1;break;case 5:c->zc_cleanup_fd=1;break;
        case 6:c->zc_value[0]='x';break;case 7:c->zc_string[0]='x';break;
        case 8:c->zc_nvlist_dst=1;break;
        case 9:--w->dst_capacity;--c->zc_nvlist_dst_size;break;
        }
        int rc=oz_ioctl_request(OZ_ADMISSION_PORT,w,base+w->dst_capacity);
        CHECK(rc==EINVAL||(!rc&&w->engine_error==EINVAL));
    }
    prepare(w,&ticket,fd,ZFS_IOC_ERROR_LOG,"qemu_mirror",capacity);
    memset((char *)w+base+capacity,0xa5,16);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base+capacity));
    CHECK(!w->engine_error&&c->zc_nvlist_dst_filled&&!c->zc_nvlist_dst);
    CHECK(c->zc_nvlist_dst_size==(seeded?2:4));
    zbookmark_phys_t *bookmarks=(void *)((char *)w+base);
    if(seeded) {
        CHECK(bookmarks[2].zb_objset==dsobj&&bookmarks[3].zb_objset==dsobj);
        CHECK(bookmarks[2].zb_object==object&&bookmarks[3].zb_object==object);
        CHECK(bookmarks[2].zb_level==0&&bookmarks[3].zb_level==0);
        CHECK(bookmarks[2].zb_blkid+bookmarks[3].zb_blkid==1);
    }
    for(unsigned i=0;i<16;i++)CHECK(((unsigned char *)w)[base+capacity+i]==0xa5);
    prepare(w,&ticket,fd,ZFS_IOC_DSOBJ_TO_DSNAME,"qemu_mirror",0);c->zc_obj=dsobj;
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base)&&!w->engine_error);
    CHECK(!strcmp(c->zc_value,"qemu_mirror/error-log"));
    /* Reuse output value exactly like upstream zpool_obj_to_path_impl. */
    w->command=ZFS_IOC_OBJ_TO_PATH;strcpy(c->zc_name,"qemu_mirror/error-log");c->zc_obj=object;
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,base)&&!w->engine_error);
    CHECK(!strcmp(c->zc_value,"/evidence"));
    bug("RESULT native error log wire PASS seeded=%d malformed=10 canaries=2 reused-path=1\n",seeded);
done:
    if(error)bug("RESULT native error log wire FAIL line=%d engine=%ld\n",error,w?(long)w->engine_error:-1L);
    if(fd>0&&oz_control_close(OZ_ADMISSION_PORT,&ticket,fd))error=EIO;
    if(opened&&oz_session_close(OZ_ADMISSION_PORT,&ticket))error=EIO;
    free(w);return error;
#undef CHECK
}
