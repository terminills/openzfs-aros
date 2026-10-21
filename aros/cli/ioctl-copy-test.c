/* SPDX-License-Identifier: CDDL-1.0 */
#include "../engine/ioctl-copy.h"
#include <errno.h>
#include <pthread.h>
#include <string.h>
#include <stdint.h>
#ifdef __AROS__
#include <aros/debug.h>
#define report bug
#else
#include <stdio.h>
#define report printf
#endif
#define CHECK(x) do { if(!(x)) {report("RESULT native CLI ioctl copy FAIL line=%d\n",__LINE__);return 20;} } while(0)
struct worker {const char *parent_input;struct oz_ioctl_copy_scope *parent;int error;};
static void *thread_check(void *arg)
{
    struct worker *w=arg;char local[]="child",copy[8];
    if(ddi_copyin(w->parent_input,copy,1,FKIOCTL)!=EFAULT||
        oz_ioctl_copy_leave(w->parent)!=EINVAL)w->error=1;
    struct oz_ioctl_range range={local,sizeof(local),OZ_IOCTL_INPUT};
    struct oz_ioctl_copy_scope *scope=NULL;
    if(oz_ioctl_copy_enter(&range,1,&scope))w->error=2;
    if(!w->error&&(ddi_copyin(local,copy,sizeof(local),FKIOCTL)||strcmp(copy,local)))w->error=3;
    if(ddi_copyin(w->parent_input,copy,1,FKIOCTL)!=EFAULT)w->error=4;
    /* Deliberate thread exit with a live scope: TLS destructor owns metadata,
     * not these automatic buffers. Host ASan/LSan also checks this path. */
    return NULL;
}
int main(void)
{
    char input[]="payload",output[16],copy[16];memset(output,0x5a,sizeof(output));
    struct oz_ioctl_copy_scope *scope=NULL,*nested=NULL;
    struct oz_ioctl_range ranges[]={{input,sizeof(input),OZ_IOCTL_INPUT},
        {output+4,8,OZ_IOCTL_OUTPUT}};
    CHECK(ddi_copyin(input,copy,1,FKIOCTL)==EFAULT);
    CHECK(ddi_copyout(input,output,1,FKIOCTL)==EFAULT);
    CHECK(!oz_ioctl_copy_enter(ranges,2,&scope));
    /* The scope snapshots range metadata; later caller edits cannot widen it. */
    ranges[1].base=output;ranges[1].size=sizeof(output);
    CHECK(!ddi_copyin(input,copy,sizeof(input),FKIOCTL)&&!strcmp(input,copy));
    CHECK(ddi_copyin(input+sizeof(input),copy,1,FKIOCTL)==EFAULT);
    CHECK(ddi_copyin(input,copy,sizeof(input)+1,FKIOCTL)==EFAULT);
    CHECK(ddi_copyin(output+4,copy,1,FKIOCTL)==EFAULT);
    CHECK(ddi_copyout(copy,input,1,FKIOCTL)==EFAULT);
    CHECK(ddi_copyin(input,copy,1,0)==EFAULT);
    CHECK(ddi_copyout(input,output+4,1,0)==EFAULT);
    CHECK(!ddi_copyout(input,output+4,sizeof(input),FKIOCTL));
    CHECK(!memcmp(input,output+4,sizeof(input)));
    CHECK(ddi_copyout(input,output+3,1,FKIOCTL)==EFAULT);
    CHECK(xcopyout(input,output+11,2)==EFAULT);
    CHECK(output[3]==0x5a&&output[12]==0x5a);
    size_t done=99;
    CHECK(!copyinstr(input,copy,sizeof(copy),&done)&&done==sizeof(input)&&!strcmp(input,copy));
    strcpy(copy,"unchanged");
    CHECK(copyinstr(input,copy,3,&done)==ENAMETOOLONG&&done==0&&!strcmp(copy,"unchanged"));
    CHECK(copyinstr(input+sizeof(input),copy,1,&done)==EFAULT&&done==0);
    CHECK(copyinstr(input,copy,0,&done)==ENAMETOOLONG&&done==0);
    CHECK(!oz_ioctl_copy_enter(NULL,0,&nested));
    CHECK(ddi_copyin(input,copy,1,FKIOCTL)==EFAULT);
    CHECK(oz_ioctl_copy_leave(scope)==EINVAL);
    CHECK(!oz_ioctl_copy_leave(nested));
    CHECK(!ddi_copyin(input,copy,sizeof(input),FKIOCTL));
    for(unsigned i=0;i<16;i++) {
        struct worker w={input,scope,0};pthread_t thread;
        CHECK(!pthread_create(&thread,NULL,thread_check,&w));
        CHECK(!pthread_join(thread,NULL)&&!w.error);
        CHECK(!ddi_copyin(input,copy,sizeof(input),FKIOCTL));
    }
    CHECK(!oz_ioctl_copy_leave(scope));
    CHECK(ddi_copyin(input,copy,1,FKIOCTL)==EFAULT);
    struct oz_ioctl_range invalid={(void *)(uintptr_t)(UINTPTR_MAX-3),8,OZ_IOCTL_INPUT};
    CHECK(oz_ioctl_copy_enter(&invalid,1,&scope)==EOVERFLOW&&!scope);
    invalid.base=NULL;CHECK(oz_ioctl_copy_enter(&invalid,1,&scope)==EINVAL&&!scope);
    invalid.base=input;invalid.access=4;CHECK(oz_ioctl_copy_enter(&invalid,1,&scope)==EINVAL&&!scope);
    char unterminated[3]={'a','b','c'};
    invalid=(struct oz_ioctl_range){unterminated,sizeof(unterminated),OZ_IOCTL_INPUT};
    CHECK(!oz_ioctl_copy_enter(&invalid,1,&scope));
    CHECK(copyinstr(unterminated,copy,sizeof(copy),&done)==EFAULT&&done==0);
    CHECK(!oz_ioctl_copy_leave(scope));
    CHECK(!ddi_copyin(NULL,NULL,0,FKIOCTL)&&!xcopyout(NULL,NULL,0));
    report("RESULT native CLI ioctl copy PASS threads=16\n");return 0;
}
