/* SPDX-License-Identifier: CDDL-1.0 */
/* Native transport for the upstream zpool_label_disk GPT layout. This is
 * deliberately a handle operation, not device discovery or authorization. */
#include <sys/types.h>
#include <sys/efi_partition.h>
#include <sys/vdev_disk.h>
#include <sys/fs/zfs.h>
#include <errno.h>
#include <stdint.h>
#include <string.h>
#include "efi-native.h"

static int result(int rc)
{
    if(!rc)return 0;
    if(errno)return errno;
    return rc==VT_EINVAL?EINVAL:rc==VT_ENOSPC?ENOSPC:EIO;
}

int oz_efi_label(int fd,uint64_t start,const char *name)
{
    struct dk_gpt *table=NULL,*check=NULL;
    if(!name||strlen(name)>=EFI_PART_NAME_LEN)return EINVAL;
    int error=oz_efi_guard_begin(fd);
    if(error)return error;
    if(!start)start=NEW_START_BLOCK;
    errno=0;
    error=result(efi_alloc_and_init(fd,EFI_NUMPAR,&table));
    if(error)goto done;
    /* Do not let subtraction wrap a too-small device into a huge partition.
     * Keep upstream's start, end alignment and reserved-slice convention. */
    if(table->efi_first_u_lba<0||table->efi_last_u_lba<table->efi_first_u_lba) {
        error=EINVAL;goto done;
    }
    uint64_t end=(uint64_t)table->efi_last_u_lba+1;
    if(start<(uint64_t)table->efi_first_u_lba||end<=EFI_MIN_RESV_SIZE||
       start>=end-EFI_MIN_RESV_SIZE) {
        error=EINVAL;goto done;
    }
    uint64_t size=(end-EFI_MIN_RESV_SIZE-start)&
        ~((uint64_t)PARTITION_END_ALIGNMENT-1);
    if(!table->efi_lbasize||size<SPA_MINDEVSIZE/table->efi_lbasize+
        !!(SPA_MINDEVSIZE%table->efi_lbasize)) {error=ENOSPC;goto done;}
    table->efi_parts[0].p_start=start;
    table->efi_parts[0].p_size=size;
    table->efi_parts[0].p_tag=V_USR;
    strcpy(table->efi_parts[0].p_name,name);
    table->efi_parts[8].p_start=start+size;
    table->efi_parts[8].p_size=EFI_MIN_RESV_SIZE;
    table->efi_parts[8].p_tag=V_RESERVED;
    errno=0;error=result(efi_write(fd,table));
    if(error)goto done;
    /* Native writes flush through oz_efi_transfer. Verify using both upstream
     * libefi and the independent AROS partition parser before reporting OK.
     * A failed write/readback can leave changed media: this is not rollback. */
    errno=0;error=result(efi_alloc_and_read(fd,&check));
    if(error)goto done;
    if((check->efi_flags&EFI_GPT_PRIMARY_CORRUPT)||
       check->efi_nparts!=table->efi_nparts||check->efi_lbasize!=table->efi_lbasize||
       check->efi_first_u_lba!=table->efi_first_u_lba||
       check->efi_last_u_lba!=table->efi_last_u_lba||check->efi_last_lba!=table->efi_last_lba||
       memcmp(&check->efi_disk_uguid,&table->efi_disk_uguid,sizeof(table->efi_disk_uguid))) {
        error=EIO;goto done;
    }
    for(unsigned i=0;i<table->efi_nparts;i++) {
        if(check->efi_parts[i].p_start!=table->efi_parts[i].p_start||
           check->efi_parts[i].p_size!=table->efi_parts[i].p_size||
           check->efi_parts[i].p_tag!=table->efi_parts[i].p_tag||
           check->efi_parts[i].p_flag!=table->efi_parts[i].p_flag||
           memcmp(&check->efi_parts[i].p_uguid,&table->efi_parts[i].p_uguid,
               sizeof(table->efi_parts[i].p_uguid))||
           memcmp(check->efi_parts[i].p_name,table->efi_parts[i].p_name,EFI_PART_NAME_LEN)) {
            error=EIO;goto done;
        }
    }
    errno=0;error=result(oz_efi_rescan(fd));
done:
    efi_free(check);efi_free(table);
    int end_error=oz_efi_guard_end(fd);return error?error:end_error;
}

int oz_efi_expand(int fd,int *changed)
{
    struct dk_gpt *before=NULL,*after=NULL;
    if(!changed)return EINVAL;
    int error=oz_efi_guard_begin(fd);
    if(error)return error;
    errno=0;error=result(efi_alloc_and_read(fd,&before));
    if(error)goto done;
    if(before->efi_flags&EFI_GPT_PRIMARY_CORRUPT){error=EIO;goto done;}
    errno=0;int rc=efi_use_whole_disk(fd);
    if(rc&&rc!=VT_ENOSPC){error=result(rc);goto done;}
    errno=0;error=result(efi_alloc_and_read(fd,&after));
    if(error)goto done;
    if((after->efi_flags&EFI_GPT_PRIMARY_CORRUPT)||
       before->efi_nparts!=after->efi_nparts||before->efi_lbasize!=after->efi_lbasize||
       before->efi_first_u_lba!=after->efi_first_u_lba||
       memcmp(&before->efi_disk_uguid,&after->efi_disk_uguid,sizeof(before->efi_disk_uguid))) {
        error=EIO;goto done;
    }
    int grew=before->efi_last_u_lba!=after->efi_last_u_lba||
        before->efi_altern_lba!=after->efi_altern_lba;
    if(rc==VT_ENOSPC&&grew){error=EIO;goto done;}
    for(unsigned i=0;i<before->efi_nparts;i++) {
        const struct dk_part *a=&before->efi_parts[i],*b=&after->efi_parts[i];
        if(a->p_tag!=b->p_tag||a->p_flag!=b->p_flag||
           memcmp(&a->p_uguid,&b->p_uguid,sizeof(a->p_uguid))||
           memcmp(a->p_name,b->p_name,EFI_PART_NAME_LEN)||
           (i!=8&&a->p_start!=b->p_start)||b->p_start<a->p_start||b->p_size<a->p_size||
           (i==8&&a->p_size!=b->p_size)||
           (rc==VT_ENOSPC&&(a->p_size!=b->p_size||a->p_start!=b->p_start))) {
            error=EIO;goto done;
        }
        if(a->p_size!=b->p_size||a->p_start!=b->p_start)grew=1;
    }
    errno=0;error=result(oz_efi_rescan(fd));
    if(!error)*changed=grew;
done:
    efi_free(after);efi_free(before);
    int end_error=oz_efi_guard_end(fd);return error?error:end_error;
}
