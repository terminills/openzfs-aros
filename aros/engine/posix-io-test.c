/* Discriminator: no OpenZFS objects or special link flags. */
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <errno.h>
_Static_assert(sizeof(off_t)==8,"Positioned I/O needs 64-bit offsets");
int main(void)
{
    unsigned char random[4],out[512],in[512];char path[100];int failures=0;
    if(getentropy(random,sizeof(random)))return 20;
    snprintf(path,sizeof(path),"SYS:posix-io-%02x%02x%02x%02x.bin",random[0],random[1],random[2],random[3]);
    int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return 20;
    printf("file=%s off_t=%zu fd=%d\n",path,sizeof(off_t),fd);
    const off_t offsets[]={0,16384,UINT64_C(0x7ffffe00),UINT64_C(0x80000000),
        UINT64_C(0xfffffe00),UINT64_C(0x100000000)};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memset(out,0x41+i,sizeof(out));memset(in,0,sizeof(in));errno=0;
        ssize_t wr=pwrite(fd,out,sizeof(out),offsets[i]);int we=errno;errno=0;
        ssize_t rd=pread(fd,in,sizeof(in),offsets[i]);int re=errno;
        struct stat st;int sr=fstat(fd,&st);int diff=memcmp(in,out,sizeof(in));
        printf("offset=%lld write=%ld errno=%d read=%ld errno=%d diff=%d stat=%d size=%lld\n",
            (long long)offsets[i],(long)wr,we,(long)rd,re,diff,sr,(long long)(sr?-1:st.st_size));fflush(stdout);
        if(wr!=512 || rd!=512 || diff || sr || st.st_size!=offsets[i]+512)failures++;
        if(lseek(fd,0,SEEK_CUR)!=0)failures++;
    }
    /* Revisit earlier positions after growth: matching low 32-bit addresses
     * must not alias. Check sampled unwritten holes, not just stored blocks. */
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memset(out,0x41+i,sizeof(out));memset(in,0,sizeof(in));
        if(pread(fd,in,sizeof(in),offsets[i])!=sizeof(in)||memcmp(in,out,sizeof(in)))failures++;
    }
    const off_t holes[]={8192,UINT64_C(0x40000000),UINT64_C(0xc0000000)};
    memset(out,0,sizeof(out));
    for(unsigned i=0;i<sizeof(holes)/sizeof(holes[0]);i++){
        memset(in,0xff,sizeof(in));
        if(pread(fd,in,sizeof(in),holes[i])!=sizeof(in)||memcmp(in,out,sizeof(in)))failures++;
    }
    if(lseek(fd,0,SEEK_CUR)!=0)failures++;
    if(fsync(fd))failures++;
    if(close(fd))failures++;
    fd=open(path,O_RDONLY);
    if(fd<0)return 20;
    for(unsigned i=0;i<sizeof(offsets)/sizeof(offsets[0]);i++){
        memset(out,0x41+i,sizeof(out));memset(in,0,sizeof(in));
        if(pread(fd,in,sizeof(in),offsets[i])!=sizeof(in)||memcmp(in,out,sizeof(in)))failures++;
    }
    memset(out,0,sizeof(out));
    for(unsigned i=0;i<sizeof(holes)/sizeof(holes[0]);i++){
        memset(in,0xff,sizeof(in));
        if(pread(fd,in,sizeof(in),holes[i])!=sizeof(in)||memcmp(in,out,sizeof(in)))failures++;
    }
    if(lseek(fd,0,SEEK_CUR)!=0)failures++;
    if(close(fd))failures++;
    printf("RESULT raw POSIX reopen exact=1 holes=1 failures=%d\n",failures);
    printf("RESULT raw POSIX boundaries=6 holes=3 preserved-offset=1 failures=%d\n",failures);
    printf("RESULT raw POSIX failures=%d\n",failures);return failures?20:0;
}
