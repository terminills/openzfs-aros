/* SPDX-License-Identifier: CDDL-1.0 */
/* Real service transport, deliberately bypassing libzfs property validation. */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include "../ui/ioctl-api.h"

int oz_mmp_raw_property(const char *pool, int *engine_error)
{
    struct oz_ticket ticket={0};
    LONG fd=-1;
    nvlist_t *props=NULL;
    char *packed=NULL;
    size_t packed_size=0;
    struct oz_ioctl_wire *wire=NULL;
    int opened=0,error=oz_session_open(OZ_ADMISSION_PORT,&ticket);
    *engine_error=0;
    if(error)goto done;
    opened=1;
    error=oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd);
    if(error)goto done;
    error=nvlist_alloc(&props,NV_UNIQUE_NAME,0);
    if(error)goto done;
    error=nvlist_add_uint64(props,"multihost",1);
    if(error)goto done;
    error=nvlist_pack(props,&packed,&packed_size,NV_ENCODE_XDR,0);
    if(error)goto done;
    size_t size=sizeof(*wire)+sizeof(zfs_cmd_t)+packed_size;
    wire=calloc(1,size);
    if(!wire){error=ENOMEM;goto done;}
    wire->session=ticket;
    wire->magic=OZ_IOCTL_WIRE_MAGIC;wire->version=OZ_IOCTL_WIRE_VERSION;
    wire->command_size=sizeof(zfs_cmd_t);wire->command=ZFS_IOC_POOL_SET_PROPS;
    wire->descriptor=fd;wire->src_size=packed_size;
    zfs_cmd_t *command=(void *)(wire+1);
    strlcpy(command->zc_name,pool,sizeof(command->zc_name));
    command->zc_nvlist_src_size=packed_size;
    memcpy(command+1,packed,packed_size);
    error=oz_ioctl_request(OZ_ADMISSION_PORT,wire,size);
    if(!error)*engine_error=wire->engine_error;
done:
    free(wire);free(packed);nvlist_free(props);
    if(fd>=0){int e=oz_control_close(OZ_ADMISSION_PORT,&ticket,fd);if(!error)error=e;}
    if(opened){int e=oz_session_close(OZ_ADMISSION_PORT,&ticket);if(!error)error=e;}
    return error;
}
