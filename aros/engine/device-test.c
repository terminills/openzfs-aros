/* Destructive probe ONLY for the explicit disposable 256 MiB QEMU test disks. */
#include "device-io.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <stdarg.h>
static void report(const char *format,...)
{
    char text[512];va_list ap;va_start(ap,format);
    vsnprintf(text,sizeof(text),format,ap);va_end(ap);
    Write(Output(),text,strlen(text));
    for(char *p=text;*p;p++){if(*p=='\n')RawPutChar('\r');RawPutChar(*p);}
}
int main(int argc,char **argv)
{
    if(argc!=4 || strcmp(argv[3],"DISPOSABLE"))return 20;
    char *end;unsigned long unit=strtoul(argv[2],&end,10);
    if(!*argv[2]||*end||unit>1||strcmp(argv[1],"ata.device"))return 20;
    struct oz_device *d=NULL;int rc=oz_device_open(argv[1],unit,1,&d);
    report("device=%s unit=%lu open=%d\n",argv[1],unit,rc);
    if(rc)return 20;
    uint64_t size=oz_device_size(d);
    report("size=%llu sector=%u\n",(unsigned long long)size,oz_device_sector(d));
    if(size!=UINT64_C(268435456)){oz_device_close(d);return 20;}
    unsigned char data[4096],readback[4096];
    uint64_t offsets[]={UINT64_C(1048576),size-sizeof(data)};
    for(unsigned j=0;j<2&&!rc;j++) {
        rc=oz_device_transfer(d,readback,sizeof(readback),offsets[j],0);
        if(!rc)for(unsigned i=0;i<sizeof(readback);i++)if(readback[i]){rc=EEXIST;break;}
        if(rc)break; /* Never overwrite an existing nonzero test extent. */
        for(unsigned i=0;i<sizeof(data);i++)data[i]=(unsigned char)(i*13u+unit+j+1);
        rc=oz_device_transfer(d,data,sizeof(data),offsets[j],1);
        if(!rc)rc=oz_device_flush(d);
        if(!rc)rc=oz_device_transfer(d,readback,sizeof(readback),offsets[j],0);
        if(!rc&&memcmp(data,readback,sizeof(data)))rc=EIO;
        report("offset=%llu write/flush/read/compare=%d\n",(unsigned long long)offsets[j],rc);
    }
    if(!rc && oz_device_transfer(d,readback,sizeof(readback),size,0)!=EINVAL)rc=EIO;
    oz_device_close(d);
    report("RESULT QEMU native block I/O unit=%lu %s\n",unit,rc?"FAIL":"PASS");
    return rc?20:0;
}
