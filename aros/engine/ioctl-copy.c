/* SPDX-License-Identifier: CDDL-1.0 */
#include "ioctl-copy.h"
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
struct oz_ioctl_copy_scope {
    struct oz_ioctl_copy_scope *parent;
    size_t count;
    struct oz_ioctl_range ranges[];
};
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_key_t key;
static int key_error;
static void discard(void *value)
{
    struct oz_ioctl_copy_scope *scope=value;
    while(scope) {struct oz_ioctl_copy_scope *parent=scope->parent;free(scope);scope=parent;}
}
static void initialize(void) {key_error=pthread_key_create(&key,discard);}
static int ready(void)
{int error=pthread_once(&once,initialize);return error?error:key_error;}

int oz_ioctl_copy_enter(const struct oz_ioctl_range *ranges,size_t count,
    struct oz_ioctl_copy_scope **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(count&&!ranges)return EINVAL;
    if(count>(SIZE_MAX-sizeof(struct oz_ioctl_copy_scope))/sizeof(*ranges))return EOVERFLOW;
    for(size_t i=0;i<count;i++) {
        if(!ranges[i].access||(ranges[i].access&~(OZ_IOCTL_INPUT|OZ_IOCTL_OUTPUT)))return EINVAL;
        if(ranges[i].size&&!ranges[i].base)return EINVAL;
        if(ranges[i].size>UINTPTR_MAX-(uintptr_t)ranges[i].base)return EOVERFLOW;
    }
    int error=ready();if(error)return error;
    struct oz_ioctl_copy_scope *scope=malloc(sizeof(*scope)+count*sizeof(*ranges));
    if(!scope)return ENOMEM;
    scope->parent=pthread_getspecific(key);scope->count=count;
    if(count)memcpy(scope->ranges,ranges,count*sizeof(*ranges));
    error=pthread_setspecific(key,scope);
    if(error)free(scope);else *out=scope;
    return error;
}
int oz_ioctl_copy_leave(struct oz_ioctl_copy_scope *scope)
{
    int error=ready();if(error)return error;
    if(!scope||pthread_getspecific(key)!=scope)return EINVAL;
    error=pthread_setspecific(key,scope->parent);
    if(!error)free(scope);
    return error;
}
static size_t available(const void *address,unsigned access)
{
    if(ready())return 0;
    const struct oz_ioctl_copy_scope *scope=pthread_getspecific(key);
    if(!scope)return 0;
    uintptr_t pointer=(uintptr_t)address;
    size_t maximum=0;
    for(size_t i=0;i<scope->count;i++) {
        const struct oz_ioctl_range *range=&scope->ranges[i];
        uintptr_t base=(uintptr_t)range->base;
        if((range->access&access)==access&&pointer>=base&&pointer-base<range->size) {
            size_t remaining=range->size-(pointer-base);
            if(remaining>maximum)maximum=remaining;
        }
    }
    return maximum;
}
int ddi_copyin(const void *from,void *to,size_t size,int flags)
{
    if((unsigned)flags!=FKIOCTL)return EFAULT;
    if(!size)return 0;
    if(!to||available(from,OZ_IOCTL_INPUT)<size)return EFAULT;
    memmove(to,from,size);return 0;
}
int xcopyout(const void *from,void *to,size_t size)
{
    if(!size)return 0;
    if(!from||available(to,OZ_IOCTL_OUTPUT)<size)return EFAULT;
    memmove(to,from,size);return 0;
}
int ddi_copyout(const void *from,void *to,size_t size,int flags)
{
    if((unsigned)flags!=FKIOCTL)return EFAULT;
    return xcopyout(from,to,size);
}
int copyinstr(const void *from,void *to,size_t limit,size_t *done)
{
    if(done)*done=0;
    if(!limit)return ENAMETOOLONG;
    if(!to)return EFAULT;
    size_t remaining=available(from,OZ_IOCTL_INPUT);
    if(!remaining)return EFAULT;
    size_t scan=remaining<limit?remaining:limit;
    const char *end=memchr(from,0,scan);
    if(!end)return scan==limit?ENAMETOOLONG:EFAULT;
    size_t size=(size_t)(end-(const char *)from)+1;
    memmove(to,from,size);
    if(done)*done=size;
    return 0;
}
