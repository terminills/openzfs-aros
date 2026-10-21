/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <aros/debug.h>
#include "../ui/ioctl-api.h"

static void prepare(struct oz_ioctl_wire *w, const struct oz_ticket *ticket,
    LONG fd, const char *dataset, size_t capacity)
{
    memset(w, 0, sizeof(*w) + sizeof(zfs_cmd_t) + capacity);
    w->session = *ticket;
    w->magic = OZ_IOCTL_WIRE_MAGIC;
    w->version = OZ_IOCTL_WIRE_VERSION;
    w->command_size = sizeof(zfs_cmd_t);
    w->command = ZFS_IOC_OBJSET_RECVD_PROPS;
    w->descriptor = fd;
    w->dst_capacity = capacity;
    zfs_cmd_t *c = (void *)(w + 1);
    strlcpy(c->zc_name, dataset, sizeof(c->zc_name));
    c->zc_nvlist_dst_size = capacity;
}

int oz_received_large_wire_test(const char *dataset)
{
    const size_t capacity=256*1024;
    const size_t base=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *w=calloc(1,base+capacity);
    struct oz_ticket ticket={0};LONG fd=-1;
    int opened=0,error=ENOMEM;
    if(!w)return error;
    error=oz_session_open(OZ_ADMISSION_PORT,&ticket);
    if(error)goto done;
    opened=1;error=oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd);
    if(error)goto done;
    prepare(w,&ticket,fd,dataset,capacity);
    error=oz_ioctl_request(OZ_ADMISSION_PORT,w,base+capacity);
    zfs_cmd_t *c=(void *)(w+1);
    if(!error&&(w->engine_error!=ENOMEM||c->zc_nvlist_dst_size<=capacity))error=EINVAL;
    bug("Native received expansion sizing capacity=%lu required=%llu engine=%ld error=%d\n",
        (unsigned long)capacity,(unsigned long long)c->zc_nvlist_dst_size,
        (long)w->engine_error,error);
done:
    if(fd>0&&oz_control_close(OZ_ADMISSION_PORT,&ticket,fd))error=EIO;
    if(opened&&oz_session_close(OZ_ADMISSION_PORT,&ticket))error=EIO;
    free(w);return error;
}

int oz_received_wire_test(const char *dataset, const char *local_only)
{
    const size_t capacity = 65536;
    const size_t base = sizeof(struct oz_ioctl_wire) + sizeof(zfs_cmd_t);
    struct oz_ioctl_wire *w = calloc(1, base + capacity + 16);
    struct oz_ticket ticket = {0};
    LONG fd = -1;
    int opened = 0, error = 0;
    zfs_cmd_t *c = w ? (void *)(w + 1) : NULL;
#define CHECK(x) do { if (!(x)) { error = __LINE__; goto done; } } while (0)
    CHECK(w);
    CHECK(!oz_session_open(OZ_ADMISSION_PORT, &ticket));
    opened = 1;
    CHECK(!oz_control_open(OZ_ADMISSION_PORT, &ticket, &fd));
    prepare(w, &ticket, fd, local_only, capacity);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT, w, base + capacity));
    CHECK(w->engine_error == ENOTSUP);
    prepare(w, &ticket, fd, dataset, 1);
    memset((char *)w + base + 1, 0xa5, 16);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT, w, base + 1));
    CHECK(w->engine_error == ENOMEM && c->zc_nvlist_dst_size > 1);
    /* Upstream put_nvlist sets filled even for its ENOMEM sizing reply.
     * The returned required size is authoritative; no bytes may overflow. */
    CHECK(!c->zc_nvlist_dst && c->zc_nvlist_dst_filled);
    for (unsigned b = 0; b < 16; b++)
        CHECK(((unsigned char *)w)[base + 1 + b] == 0xa5);
    for (unsigned bad = 0; bad < 12; bad++) {
        prepare(w, &ticket, fd, dataset, capacity);
        switch (bad) {
        case 0: c->zc_cookie = 1; break;
        case 1: c->zc_guid = 1; break;
        case 2: c->zc_obj = 1; break;
        case 3: c->zc_flags = 1; break;
        case 4: c->zc_history_offset = 1; break;
        case 5: c->zc_cleanup_fd = 1; break;
        case 6: c->zc_value[0] = 'x'; break;
        case 7: c->zc_string[0] = 'x'; break;
        case 8: c->zc_nvlist_dst = 1; break;
        case 9: c->zc_nvlist_src = 1; break;
        case 10: c->zc_nvlist_conf = 1; break;
        case 11: w->history_size = 1; break;
        }
        int rc = oz_ioctl_request(OZ_ADMISSION_PORT, w, base + capacity);
        CHECK(rc == EINVAL || (!rc && w->engine_error == EINVAL));
    }
    prepare(w, &ticket, fd, dataset, capacity);
    memset((char *)w + base + capacity, 0xa5, 16);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT, w, base + capacity));
    CHECK(!w->engine_error && c->zc_nvlist_dst_filled);
    CHECK(c->zc_nvlist_dst_size > 1 && c->zc_nvlist_dst_size <= capacity);
    CHECK(!c->zc_nvlist_dst && !c->zc_nvlist_src && !c->zc_nvlist_conf);
    for (unsigned b = 0; b < 16; b++)
        CHECK(((unsigned char *)w)[base + capacity + b] == 0xa5);
    bug("RESULT native received wire PASS absent=1 tiny=1 malformed=12 guards=1 recovered=1\n");
done:
    if (error) bug("RESULT native received wire FAIL line=%d\n", error);
    if (fd > 0 && oz_control_close(OZ_ADMISSION_PORT, &ticket, fd))
        error = error ? error : 1;
    if (opened && oz_session_close(OZ_ADMISSION_PORT, &ticket))
        error = error ? error : 1;
    free(w);
    return error;
#undef CHECK
}
