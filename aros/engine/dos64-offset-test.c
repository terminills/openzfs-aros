/* Standalone discriminator; no OpenZFS or raw devices. Exclusive scratch file. */
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/dos64.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
struct Library *DOS64Base;
int main(void)
{
    unsigned char random[4],out[512],in[512];char path[100];int failures=0;
    if(getentropy(random,sizeof(random)))return 20;
    snprintf(path,sizeof(path),"SYS:dos64-offset-%02x%02x%02x%02x.bin",random[0],random[1],random[2],random[3]);
    int fd=open(path,O_CREAT|O_EXCL|O_RDWR,0600);if(fd<0)return 20;
    close(fd);
    DOS64Base=OpenLibrary((CONST_STRPTR)"dos64.library",0);
    if(!DOS64Base)return 20;
    BPTR file=Open((CONST_STRPTR)path,MODE_OLDFILE);
    if(!file){CloseLibrary(DOS64Base);return 20;}
    printf("file=%s dos64=%u.%u capable=%ld\n",path,DOS64Base->lib_Version,
        DOS64Base->lib_Revision,(long)IsFileSystem64(file));
    struct FileHandle *fh=BADDR(file);
    SetIoErr(0);
    SIPTR raw=DoPkt(fh->fh_Type,ACTION_SEEK64,fh->fh_Arg1,
        (IPTR)UINT64_C(0x100000000),OFFSET_BEGINNING,0,0);
    LONG raw_error=IoErr();SetIoErr(0);
    QUAD past=Seek64(file,OFFSET_BEGINNING,UINT64_C(0x100000000));
    printf("past-eof raw-seek=%lld/%ld dos64-seek=%lld/%ld\n",
        (long long)raw,(long)raw_error,(long long)past,(long)IoErr());fflush(stdout);
    SetIoErr(0);
    QUAD size=SetFileSize64(file,OFFSET_BEGINNING,UINT64_C(0x100001000));
    printf("expand=%lld ioerr=%ld\n",(long long)size,(long)IoErr());fflush(stdout);
    if(size!=UINT64_C(0x100001000))failures++;
    const uint64_t offsets[]={0,0x7ffffe00,0x80000000,0xfffffe00,UINT64_C(0x100000000)};
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
        memset(out,0x41+i,sizeof(out));memset(in,0,sizeof(in));SetIoErr(0);
        QUAD seek=Seek64(file,OFFSET_BEGINNING,offsets[i]);LONG se=IoErr();
        QUAD wr=seek<0?-1:Write64(file,out,sizeof(out));LONG we=IoErr();
        QUAD back=Seek64(file,OFFSET_BEGINNING,offsets[i]);LONG be=IoErr();
        QUAD rd=back<0?-1:Read64(file,in,sizeof(in));LONG re=IoErr();
        int diff=memcmp(out,in,sizeof(out));
        printf("offset=%llu seek=%lld/%ld write=%lld/%ld back=%lld/%ld read=%lld/%ld diff=%d\n",
            (unsigned long long)offsets[i],(long long)seek,(long)se,(long long)wr,(long)we,
            (long long)back,(long)be,(long long)rd,(long)re,diff);fflush(stdout);
        if(seek<0||wr!=512||back<0||rd!=512||diff)failures++;
    }
    Close(file);CloseLibrary(DOS64Base);
    fd=open(path,O_RDWR);if(fd<0)return 20;
    for(unsigned i=0;i<sizeof(offsets)/sizeof(*offsets);i++) {
        memset(in,0,sizeof(in));errno=0;
        ssize_t n=pread(fd,in,sizeof(in),(off_t)offsets[i]);int e=errno;
        memset(out,0x41+i,sizeof(out));int diff=memcmp(out,in,sizeof(out));
        printf("posix-existing offset=%llu read=%ld errno=%d diff=%d\n",
            (unsigned long long)offsets[i],(long)n,e,diff);fflush(stdout);
        if(n!=512||diff)failures++;
    }
    close(fd);printf("RESULT DOS64 offset failures=%d\n",failures);fflush(stdout);
    return failures?20:0;
}
