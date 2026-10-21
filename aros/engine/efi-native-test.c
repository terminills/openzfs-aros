/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/types.h>
#include <sys/efi_partition.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#include <aros/debug.h>
#include <proto/dos.h>
#include <dos/dosextens.h>
#include <pthread.h>
#include <stdint.h>
#include "efi-native.h"
static void *guard_probe(void *arg)
{
    int fd=(int)(intptr_t)arg;
    struct DosList *list=AttemptLockDosList(LDF_ALL|LDF_READ);
    if(list){UnLockDosList(LDF_ALL|LDF_READ);return (void *)1;}
    uint64_t sectors=0;unsigned sector=0;
    if(oz_efi_disk_info(fd,&sectors,&sector)!=-1||errno!=EBUSY)return (void *)2;
    if(oz_efi_close(fd)!=EBUSY||oz_efi_guard_end(fd)!=EBUSY)return (void *)3;
    return NULL;
}
static void *guard_contended(void *arg)
{
    int fd=(int)(intptr_t)arg;
    int error=oz_efi_guard_begin(fd);
    if(!error)oz_efi_guard_end(fd);
    return error==EBUSY?NULL:(void *)1;
}
static void *guard_available(void *arg)
{
    int fd=(int)(intptr_t)arg;
    if(oz_efi_guard_begin(fd))return (void *)1;
    return oz_efi_guard_end(fd)?(void *)2:NULL;
}
static void identity(const char *name,const void *value)
{
    const unsigned char *bytes=value;char hex[33];
    for(unsigned i=0;i<16;i++)snprintf(hex+2*i,3,"%02x",bytes[i]);
    bug("EFI identity %s=%s\n",name,hex);
}
static void identities(const struct dk_gpt *table)
{
    identity("disk",&table->efi_disk_uguid);
    identity("data",&table->efi_parts[0].p_uguid);
    identity("reserved",&table->efi_parts[8].p_uguid);
}
int oz_efi_native_cold_test(void)
{
    int fd=oz_efi_open("ata.device",0,0),error=0;
    struct dk_gpt *table=NULL;
    if(fd<0)error=errno;
    if(!error&&(efi_alloc_and_read(fd,&table)!=0||!table))error=EIO;
    if(!error) {
        uint64_t limit=(UINT64_C(524288)-1-33-EFI_MIN_RESV_SIZE)&~UINT64_C(2047);
        if(table->efi_nparts!=128||table->efi_lbasize!=512||
           table->efi_last_lba!=524287||table->efi_first_u_lba!=34||
           table->efi_parts[0].p_start!=2048||table->efi_parts[0].p_size!=limit-2048||
           table->efi_parts[0].p_tag!=V_USR||table->efi_parts[8].p_start!=limit||
           table->efi_parts[8].p_size!=EFI_MIN_RESV_SIZE||table->efi_parts[8].p_tag!=V_RESERVED)
            error=EINVAL;
    }
    if(!error&&efi_rescan(fd))error=errno;
    if(!error)identities(table);
    efi_free(table);if(fd>0)oz_efi_close(fd);
    bug("RESULT upstream native EFI cold %s layout=1 readonly=1 rescan=1 error=%d\n",
        error?"FAIL":"PASS",error);
    return error;
}

int oz_efi_native_test(void)
{
    int fd=-1,error=0;
    struct dk_gpt *table=NULL,*readback=NULL;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    uint64_t sectors=99;unsigned sector=99;
    CHECK(oz_efi_disk_info(-1,&sectors,&sector)==-1&&errno==EBADF&&sectors==99&&sector==99);
    fd=oz_efi_open("ata.device",0,1);CHECK(fd>0);
    CHECK(oz_efi_open("ata.device",0,1)==-1&&errno==EBUSY);
    CHECK(oz_efi_open("ata.device",0,0)==-1&&errno==EBUSY);
    CHECK(!oz_efi_disk_info(fd,&sectors,&sector)&&sector==512&&sectors==524288);
    CHECK(!oz_efi_guard_begin(fd));
    CHECK(oz_efi_guard_begin(fd)==EBUSY&&oz_efi_close(fd)==EBUSY);
    pthread_t probe;void *probe_result=(void *)99;
    CHECK(!pthread_create(&probe,NULL,guard_probe,(void *)(intptr_t)fd));
    CHECK(!pthread_join(probe,&probe_result)&&!probe_result);
    CHECK(!oz_efi_guard_end(fd));
    CHECK(oz_efi_guard_end(fd)==EINVAL);
    CHECK(LockDosList(LDF_ALL|LDF_READ)!=NULL);
    int created=pthread_create(&probe,NULL,guard_contended,(void *)(intptr_t)fd);
    int joined=created?0:pthread_join(probe,&probe_result);
    UnLockDosList(LDF_ALL|LDF_READ);
    CHECK(!created&&!joined&&!probe_result);
    unsigned char scratch[512]={0};
    unsigned char before[512],after[512];
    CHECK(!oz_efi_transfer(fd,0,before,sizeof(before),0));
    CHECK(oz_efi_label(-1,0,"AROS OpenZFS")==EBADF);
    CHECK(oz_efi_label(fd,0,NULL)==EINVAL);
    CHECK(oz_efi_label(fd,1,"AROS OpenZFS")==EINVAL);
    CHECK(oz_efi_label(fd,UINT64_MAX,"AROS OpenZFS")==EINVAL);
    CHECK(oz_efi_label(fd,sectors-34-EFI_MIN_RESV_SIZE,"AROS OpenZFS")==ENOSPC);
    CHECK(!oz_efi_transfer(fd,0,after,sizeof(after),0)&&!memcmp(before,after,sizeof(before)));
    /* Invalid geometry must unwind the guard, not poison the next operation. */
    CHECK(!oz_efi_guard_begin(fd)&&!oz_efi_guard_end(fd));
    CHECK(!pthread_create(&probe,NULL,guard_available,(void *)(intptr_t)fd));
    CHECK(!pthread_join(probe,&probe_result)&&!probe_result);
    CHECK(oz_efi_transfer(fd,UINT64_MAX,scratch,sizeof(scratch),0)==-1&&errno==EOVERFLOW);
    CHECK(!efi_alloc_and_init(fd,128,&table));
    CHECK(table&&table->efi_lbasize==512&&table->efi_last_lba==sectors-1);
    CHECK(table->efi_first_u_lba==34&&table->efi_last_u_lba==sectors-34);
    table->efi_parts[0].p_start=2048;
    table->efi_parts[0].p_size=(table->efi_last_u_lba+1-EFI_MIN_RESV_SIZE-2048)&~UINT64_C(2047);
    /* Leave unallocated tail space so upstream expansion has real work. */
    table->efi_parts[0].p_size=(table->efi_parts[0].p_size/2)&~UINT64_C(2047);
    table->efi_parts[0].p_tag=V_USR;
    strcpy(table->efi_parts[0].p_name,"AROS OpenZFS");
    table->efi_parts[8].p_start=2048+table->efi_parts[0].p_size;
    table->efi_parts[8].p_size=EFI_MIN_RESV_SIZE;
    table->efi_parts[8].p_tag=V_RESERVED;
    CHECK(!efi_write(fd,table));
    CHECK(!efi_rescan(fd)); /* independent native partition.library parse */
    CHECK(efi_alloc_and_read(fd,&readback)==0&&readback);
    CHECK(!memcmp(&table->efi_disk_uguid,&readback->efi_disk_uguid,sizeof(table->efi_disk_uguid)));
    for(unsigned i=0;i<9;i+=8) {
        CHECK(table->efi_parts[i].p_start==readback->efi_parts[i].p_start);
        CHECK(table->efi_parts[i].p_size==readback->efi_parts[i].p_size);
        CHECK(table->efi_parts[i].p_tag==readback->efi_parts[i].p_tag);
        CHECK(!memcmp(&table->efi_parts[i].p_uguid,&readback->efi_parts[i].p_uguid,sizeof(table->efi_parts[i].p_uguid)));
    }
    efi_free(readback);readback=NULL;
    CHECK(!oz_efi_close(fd));fd=-1;
    fd=oz_efi_open("ata.device",0,0);CHECK(fd>0);
    int grew=-1;
    CHECK(oz_efi_expand(fd,&grew)==EROFS&&grew==-1);
    CHECK(!oz_efi_close(fd));fd=-1;
    fd=oz_efi_open("ata.device",0,1);CHECK(fd>0);
    CHECK(oz_efi_expand(fd,NULL)==EINVAL);
    CHECK(!oz_efi_expand(fd,&grew)&&grew==1);
    CHECK(!oz_efi_expand(fd,&grew)&&grew==0);
    CHECK(!efi_rescan(fd));
    CHECK(efi_alloc_and_read(fd,&readback)==0&&readback);
    CHECK(readback->efi_parts[0].p_start==2048);
    CHECK(readback->efi_parts[0].p_size>table->efi_parts[0].p_size);
    CHECK(readback->efi_parts[8].p_start==2048+readback->efi_parts[0].p_size);
    CHECK(readback->efi_parts[8].p_size==EFI_MIN_RESV_SIZE);
    CHECK(!memcmp(&table->efi_disk_uguid,&readback->efi_disk_uguid,sizeof(table->efi_disk_uguid)));
    CHECK(!memcmp(&table->efi_parts[0].p_uguid,&readback->efi_parts[0].p_uguid,sizeof(table->efi_parts[0].p_uguid)));
    efi_free(readback);readback=NULL;
    CHECK(!oz_efi_label(fd,0,"AROS OpenZFS"));
    CHECK(!efi_alloc_and_read(fd,&readback)&&readback);
    CHECK(readback->efi_nparts==128&&readback->efi_parts[0].p_start==2048);
    CHECK(!strcmp(readback->efi_parts[0].p_name,"AROS OpenZFS"));
    identities(readback);
    efi_free(readback);readback=NULL;
    CHECK(!oz_efi_close(fd));
    CHECK(oz_efi_close(fd)==EBADF);
    CHECK(oz_efi_disk_info(fd,&sectors,&sector)==-1&&errno==EBADF);fd=-1;
    fd=oz_efi_open("ata.device",0,0);CHECK(fd>0);
    CHECK(efi_alloc_and_read(fd,&readback)==0&&readback);
    CHECK(efi_write(fd,readback)!=0&&errno==EROFS);
    CHECK(oz_efi_label(fd,0,"AROS OpenZFS")==EROFS);
    bug("RESULT native GPT label PASS bounds=1 minimum=1 stale=1 write=1 reread=1 readonly=1\n");
    bug("RESULT native GPT expand PASS grow=1 identity=1 already-full=1 readonly=1\n");
    bug("RESULT native GPT guard PASS publication=1 owner=1 close=1 unwind=1\n");
done:
    if(fd>0)oz_efi_guard_end(fd);
    efi_free(table);efi_free(readback);
    if(fd>0)oz_efi_close(fd);
    bug("RESULT upstream native EFI %s create=1 expand=1 reread=1 rescan=1 readonly=1 stale=1 collision=1 overflow=1 line=%d\n",
        error?"FAIL":"PASS",error);
    return error;
#undef CHECK
}
