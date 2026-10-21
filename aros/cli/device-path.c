/* SPDX-License-Identifier: CDDL-1.0 */
#include "device-path.h"
#include <string.h>
#include <stdlib.h>
#include <errno.h>
#include <limits.h>

static int number(const char **text,uint32_t max,uint32_t *out)
{
    const char *p=*text;
    uint32_t n=0;
    if(*p<'0'||*p>'9'||(*p=='0'&&p[1]>='0'&&p[1]<='9'))return EINVAL;
    do {
        unsigned digit=(unsigned)(*p-'0');
        if(n>(max-digit)/10)return ERANGE;
        n=n*10+digit;p++;
    } while(*p>='0'&&*p<='9');
    *text=p;*out=n;return 0;
}

int oz_device_path_parse(const char *path,struct oz_device_path *out)
{
    if(!path||!out)return EINVAL;
    if(strncmp(path,"/AROS/",6))return EINVAL;
    struct oz_device_path parsed={0};
    const char *p=path+6,*name=p;
    while(*p&&*p!='/') {
        unsigned char c=(unsigned char)*p;
        if(!((c>='a'&&c<='z')||(c>='A'&&c<='Z')||
             (c>='0'&&c<='9')||c=='_'||c=='-'||c=='.'))return EINVAL;
        if((size_t)(p-name)>=sizeof(parsed.device)-1)return ENAMETOOLONG;
        p++;
    }
    size_t length=(size_t)(p-name);
    if(*p!='/'||length<=7||memcmp(p-7,".device",7))return EINVAL;
    memcpy(parsed.device,name,length);p++;
    int error=number(&p,INT32_MAX,&parsed.unit);
    if(error)return error;
    parsed.whole_length=(size_t)(p-path);
    if(*p) {
        if(strncmp(p,"/partition/",11))return EINVAL;
        p+=11;
        error=number(&p,UINT32_MAX-1,&parsed.position);
        if(error)return error;
        if(*p)return EINVAL;
        parsed.partition=1;
    }
    *out=parsed;return 0;
}

char *zfs_strip_partition(const char *path)
{
    if(!path){errno=EINVAL;return NULL;}
    struct oz_device_path parsed;
    size_t length=strlen(path);
    if(!oz_device_path_parse(path,&parsed)&&parsed.partition)length=parsed.whole_length;
    char *copy=malloc(length+1);
    if(copy){memcpy(copy,path,length);copy[length]=0;}
    return copy;
}

const char *zfs_strip_path(const char *path)
{
    struct oz_device_path parsed;
    if(!path)return NULL;
    return !oz_device_path_parse(path,&parsed)?path+6:path;
}

int zfs_append_partition(char *path,size_t capacity)
{
    static const char suffix[]="/partition/0";
    if(!path){errno=EINVAL;return -1;}
    size_t length=strnlen(path,capacity);
    if(length==capacity){errno=ENAMETOOLONG;return -1;}
    struct oz_device_path parsed;
    int error=oz_device_path_parse(path,&parsed);
    if(error||parsed.partition){errno=error?error:EINVAL;return -1;}
    if(sizeof(suffix)>capacity-length){errno=ENAMETOOLONG;return -1;}
    memcpy(path+length,suffix,sizeof(suffix));
    return (int)(length+sizeof(suffix)-1);
}
