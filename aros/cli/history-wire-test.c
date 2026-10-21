/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_ioctl.h>
#include <aros/debug.h>
#include <proto/dos.h>
#include "../ui/ioctl-api.h"

static void prepare(struct oz_ioctl_wire *w,const struct oz_ticket *ticket,
    LONG fd,const char *pool,size_t capacity,uint64_t offset)
{
    memset(w,0,sizeof(*w)+sizeof(zfs_cmd_t)+capacity);
    w->session=*ticket;w->magic=OZ_IOCTL_WIRE_MAGIC;
    w->version=OZ_IOCTL_WIRE_VERSION;w->command_size=sizeof(zfs_cmd_t);
    w->command=ZFS_IOC_POOL_GET_HISTORY;w->descriptor=fd;w->dst_capacity=capacity;
    zfs_cmd_t *c=(void *)(w+1);
    strlcpy(c->zc_name,pool,sizeof(c->zc_name));
    c->zc_nvlist_dst_size=capacity;c->zc_history_len=capacity;c->zc_history_offset=offset;
}

int oz_history_wire_test(const char *pool,unsigned minimum,unsigned expected_overwrite_jumps)
{
    const size_t capacity=65536;
#ifdef OZ_TEST_HISTORY_MEGARECORD
    /* The 1.5 GiB growth member plus the later added member can retain more
     * than 2 MiB of ring history even though only one record exceeds 1 MiB. */
    const size_t limit=4*1024*1024;
#else
    const size_t limit=1024*1024;
#endif
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+capacity;
    struct oz_ioctl_wire *w=calloc(1,size+16);
    unsigned char *baseline=malloc(limit);
    struct oz_ticket ticket={0};LONG fd=-1;int opened=0,error=0;
    size_t used=0;unsigned pages=0,reference_jumps=0,replay_jumps=0;uint64_t offset=0,eof=0;
    zfs_cmd_t *c=w?(void *)(w+1):NULL;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(w&&baseline);
    CHECK(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    CHECK(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    for(unsigned i=0;i<64;i++) {
        uint64_t requested=offset;
        prepare(w,&ticket,fd,pool,capacity,offset);
        CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,size)&&!w->engine_error);
        CHECK(!c->zc_history&&!c->zc_nvlist_dst&&!c->zc_nvlist_dst_filled);
        CHECK(c->zc_history_len<=capacity&&c->zc_history_offset>=c->zc_history_len);
        uint64_t start=c->zc_history_offset-c->zc_history_len;
        CHECK(start>=requested);
        if(start>requested)reference_jumps++;
        if(!c->zc_history_len)break;
        if(c->zc_history_len>limit-used)
            bug("History wire baseline capacity used=%lu chunk=%llu limit=%lu start=%llu requested=%llu\n",
                (unsigned long)used,(unsigned long long)c->zc_history_len,
                (unsigned long)limit,(unsigned long long)start,(unsigned long long)requested);
        CHECK(c->zc_history_len<=limit-used);
        memcpy(baseline+used,c+1,c->zc_history_len);used+=c->zc_history_len;
        offset=c->zc_history_offset;
        CHECK(i!=63);
    }
    eof=offset;
    CHECK(used>128&&used>=minimum&&reference_jumps==expected_overwrite_jumps);
    offset=0;size_t replayed=0;
    while(replayed<used) {
        size_t n=MIN((size_t)(minimum?4093:64),used-replayed);
        uint64_t requested=offset;
        prepare(w,&ticket,fd,pool,n,offset);
        size_t short_size=sizeof(*w)+sizeof(*c)+n;
        memset((char *)w+short_size,0xa5,16);
        CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,short_size)&&!w->engine_error);
        CHECK(c->zc_history_len&&c->zc_history_len<=n);
        CHECK(c->zc_history_offset>=c->zc_history_len);
        uint64_t start=c->zc_history_offset-c->zc_history_len;
        CHECK(start>=requested);
        if(start>requested)replay_jumps++;
        CHECK(!c->zc_history&&!c->zc_nvlist_dst&&!c->zc_nvlist_dst_filled);
        CHECK(!memcmp(baseline+replayed,c+1,c->zc_history_len));
        for(unsigned b=0;b<16;b++)CHECK(((unsigned char *)w)[short_size+b]==0xa5);
        replayed+=c->zc_history_len;offset=c->zc_history_offset;pages++;
    }
    CHECK(replayed==used&&pages>2&&replay_jumps==expected_overwrite_jumps);
    for(unsigned bad=0;bad<6;bad++) {
        prepare(w,&ticket,fd,pool,capacity,0);
        switch(bad) {
        case 0:c->zc_history=1;break;
        case 1:c->zc_history_len=capacity+1;break;
        case 2:c->zc_history_len=0;break;
        case 3:c->zc_cookie=1;break;
        case 4:w->history_size=1;break;
        case 5:c->zc_history_offset=UINT64_MAX;break;
        }
        int rc=oz_ioctl_request(OZ_ADMISSION_PORT,w,size);
        CHECK(rc==EINVAL||(!rc&&w->engine_error==EINVAL));
    }
    prepare(w,&ticket,fd,pool,capacity,eof);
    CHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,size)&&!w->engine_error);
    CHECK(c->zc_history_len==0&&c->zc_history_offset==eof);
    bug("RESULT native history wire PASS bytes=%lu pages=%u overwrite-jumps=%u malformed=6 guards=1 eof=1 recovered=1\n",
        (unsigned long)used,pages,reference_jumps);
done:
    if(error)bug("RESULT native history wire FAIL line=%d\n",error);
    if(fd>0&&oz_control_close(OZ_ADMISSION_PORT,&ticket,fd))error=error?error:1;
    if(opened&&oz_session_close(OZ_ADMISSION_PORT,&ticket))error=error?error:1;
    free(baseline);free(w);return error;
#undef CHECK
}

#ifdef OZ_TEST_HISTORY_RACE
int oz_history_wire_race_test(const char *pool,
    int (*writer_state)(void *,unsigned *,unsigned *),void *context)
{
    const size_t capacity=1024;
    size_t size=sizeof(struct oz_ioctl_wire)+sizeof(zfs_cmd_t)+capacity;
    struct oz_ioctl_wire *w=calloc(1,size+16);
    struct oz_ticket ticket={0};LONG fd=-1;int opened=0,error=0;
    unsigned pages=0,jumps=0,overlap_jumps=0,discarded=0,records=0;
    unsigned completed=0,done=0,header_used=0;uint64_t remaining=0,bytes=0,offset=0;
    unsigned char header[8];zfs_cmd_t *c=w?(void *)(w+1):NULL;
#define RCHECK(x) do {if(!(x)){error=__LINE__;goto race_done;}}while(0)
    RCHECK(w&&writer_state);
    do {
        RCHECK(!writer_state(context,&completed,&done));
        if(completed<4&&!done)Delay(1);
    } while(completed<4&&!done);
    RCHECK(completed>=4);
    RCHECK(!oz_session_open(OZ_ADMISSION_PORT,&ticket));opened=1;
    RCHECK(!oz_control_open(OZ_ADMISSION_PORT,&ticket,&fd));
    for(unsigned attempts=0;attempts<8192;attempts++) {
        unsigned before_completed=0,before_done=0;
        RCHECK(!writer_state(context,&before_completed,&before_done));
        uint64_t requested=offset;
        prepare(w,&ticket,fd,pool,capacity,requested);
        memset((char *)w+size,0xa5,16);
        RCHECK(!oz_ioctl_request(OZ_ADMISSION_PORT,w,size)&&!w->engine_error);
        RCHECK(!c->zc_history&&!c->zc_nvlist_dst&&!c->zc_nvlist_dst_filled);
        RCHECK(c->zc_history_len<=capacity&&c->zc_history_offset>=c->zc_history_len);
        uint64_t start=c->zc_history_offset-c->zc_history_len;
        RCHECK(start>=requested);
        if(start>requested) {
            jumps++;
            if(!before_done)overlap_jumps++;
            if(header_used||remaining)discarded++;
            header_used=0;remaining=0;
        }
        for(uint64_t n=0;n<c->zc_history_len;n++) {
            unsigned char byte=((unsigned char *)(c+1))[n];
            if(remaining) {
                remaining--;
                if(!remaining)records++;
            } else {
                header[header_used++]=byte;
                if(header_used==sizeof(header)) {
                    uint64_t length=0;
                    for(unsigned b=0;b<sizeof(header);b++)length|=(uint64_t)header[b]<<(8*b);
                    RCHECK(length&&length<=1024*1024);
                    remaining=length;header_used=0;
                }
            }
        }
        for(unsigned b=0;b<16;b++)RCHECK(((unsigned char *)w)[size+b]==0xa5);
        if(c->zc_history_len) {
            bytes+=c->zc_history_len;offset=c->zc_history_offset;pages++;
            Delay(2);
            continue;
        }
        RCHECK(c->zc_history_offset==requested);
        RCHECK(!writer_state(context,&completed,&done));
        if(done)break;
        Delay(1);
    }
    RCHECK(done&&pages>2&&bytes>384*1024&&jumps&&overlap_jumps&&records&&
        !header_used&&!remaining);
race_done:
    if(fd>0&&oz_control_close(OZ_ADMISSION_PORT,&ticket,fd))error=error?error:1;
    if(opened&&oz_session_close(OZ_ADMISSION_PORT,&ticket))error=error?error:1;
    free(w);
    bug("RESULT native history concurrent overwrite %s writes=%u pages=%u bytes=%llu jumps=%u overlap-jumps=%u discarded-partials=%u records=%u eof=%u line=%d\n",
        error?"FAIL":"PASS",completed,pages,(unsigned long long)bytes,jumps,
        overlap_jumps,discarded,records,!header_used&&!remaining,error);
    return error;
#undef RCHECK
}
#endif
