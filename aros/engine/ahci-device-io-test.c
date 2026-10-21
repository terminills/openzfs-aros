/* Destructive ONLY on the fresh, snapshot-protected 3 TiB QEMU fixture. */
#include "device-io.h"
#include <proto/exec.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <pthread.h>
static void note(const char *s){while(*s)RawPutChar(*s++);RawPutChar('\n');}
struct worker { struct oz_device *device; unsigned id; int error; unsigned completed; };
static void *concurrent_io(void *opaque)
{
    struct worker *w=opaque;
    unsigned char expected[4096],actual[4096];
    uint64_t offset=(UINT64_C(2)<<40)+UINT64_C(1048576)*(w->id+1);
    for(unsigned round=0;round<64&&!w->error;round++) {
        for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(i*19+round*31+w->id*73)&255;
        w->error=oz_device_transfer(w->device,expected,sizeof(expected),offset,1);
        if(!w->error)w->error=oz_device_flush(w->device);
        if(!w->error)w->error=oz_device_transfer(w->device,actual,sizeof(actual),offset,0);
        if(!w->error&&memcmp(expected,actual,sizeof(actual)))w->error=EIO;
        if(!w->error)w->completed++;
    }
    return NULL;
}
int main(int argc,char **argv)
{
    const uint64_t size=UINT64_C(3)<<40;
    const uint64_t offsets[]={UINT64_C(0x100000000)+4096,
        (UINT64_C(2)<<40)-512,(UINT64_C(2)<<40)+8192,size-65536};
    struct oz_device *d=NULL; unsigned failures=0; char line[180];
    unsigned char expected[1024],actual[1024],zero[1024]={0};
    int fault=argc==3&&!strcmp(argv[2],"EXPECT-FLUSH-ERROR");
    if((argc!=2&&!fault)||strcmp(argv[1],"DISPOSABLE-QEMU-3T"))return 20;
    int error=oz_device_open("ahci.device",1,1,&d);
    if(error||oz_device_size(d)!=size||oz_device_sector(d)!=512) {
        snprintf(line,sizeof(line),"RESULT AHCI device IO FAIL open=%d",error);note(line);
        if(d)oz_device_close(d);
        return 20;
    }
    unsigned char inquiry[96]={0};unsigned inquiry_length=0;
    error=oz_device_inquiry(d,inquiry,&inquiry_length);
    int identified=!error&&inquiry_length>=36&&!memcmp(inquiry+8,"SATA    ",8)&&
        !memcmp(inquiry+16,"QEMU HARDDISK",13);
    snprintf(line,sizeof(line),"INQUIRY adapter error=%d length=%u vendor=%.8s product=%.16s %s",
        error,inquiry_length,inquiry+8,inquiry+16,identified?"PASS":"FAIL");note(line);
    failures+=!identified;
    for(unsigned test=0;test<4;test++) {
        uint64_t alias=(uint32_t)offsets[test];
        for(unsigned i=0;i<sizeof(expected);i++)expected[i]=(i*37u+test*19u+1u)&255;
        error=oz_device_transfer(d,actual,sizeof(actual),offsets[test],0);
        if(!error&&memcmp(actual,zero,sizeof(actual)))error=EIO;
        if(!error)error=oz_device_transfer(d,expected,sizeof(expected),offsets[test],1);
        if(!error) {
            error=oz_device_flush(d);
            if(fault&&test==0) {
                int rejected=error==EIO;
                snprintf(line,sizeof(line),"FLUSH injected error=%d %s",error,rejected?"PASS":"FAIL");note(line);
                failures+=!rejected;
                if(rejected) {
                    error=oz_device_flush(d);
                    note(error?"FLUSH retry FAIL":"FLUSH retry PASS");
                } else if(!error)error=EIO;
            }
        }
        memset(actual,0,sizeof(actual));
        if(!error)error=oz_device_transfer(d,actual,sizeof(actual),offsets[test],0);
        if(!error&&memcmp(actual,expected,sizeof(actual)))error=EIO;
        if(!error)error=oz_device_transfer(d,actual,sizeof(actual),alias,0);
        if(!error&&memcmp(actual,zero,sizeof(actual)))error=EIO;
        snprintf(line,sizeof(line),"HIGHIO case=%u offset=%llu alias=%llu error=%d %s",test,
            (unsigned long long)offsets[test],(unsigned long long)alias,error,error?"FAIL":"PASS");note(line);
        failures+=error!=0;
    }
    int a=oz_device_transfer(d,actual,512,size,0);
    int b=oz_device_transfer(d,actual,1024,size-512,1);
    int c=oz_device_transfer(d,actual,512,UINT64_MAX-511,0);
    int e=oz_device_transfer(d,actual,512,(UINT64_C(2)<<40)+1,0);
    int ok=a==EINVAL&&b==EINVAL&&c==EINVAL&&e==EINVAL;
    snprintf(line,sizeof(line),"HIGHIO bounds=%d,%d,%d alignment=%d %s",a,b,c,e,ok?"PASS":"FAIL");note(line);
    failures+=!ok;
    pthread_t threads[3];struct worker workers[3]={0};unsigned started=0;
    for(unsigned i=0;i<3;i++) {
        workers[i].device=d;workers[i].id=i;
        error=pthread_create(&threads[i],NULL,concurrent_io,&workers[i]);
        if(error){failures++;break;}
        started++;
    }
    for(unsigned i=0;i<started;i++) {
        error=pthread_join(threads[i],NULL);
        ok=!error&&!workers[i].error&&workers[i].completed==64;
        snprintf(line,sizeof(line),"CONCURRENT worker=%u completed=%u error=%d join=%d %s",
            i,workers[i].completed,workers[i].error,error,ok?"PASS":"FAIL");note(line);
        failures+=!ok;
    }
    oz_device_close(d);d=NULL;
    error=oz_device_open("ahci.device",1,0,&d);
    if(!error) {
        error=oz_device_transfer(d,actual,512,offsets[3],1);
        ok=error==EROFS;failures+=!ok;
        note(ok?"HIGHIO readonly PASS":"HIGHIO readonly FAIL");
        oz_device_close(d);
    }else failures++;
    note(failures?"RESULT AHCI device IO FAIL":"RESULT AHCI device IO PASS");
    return failures?20:0;
}
