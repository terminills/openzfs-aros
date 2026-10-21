/* SPDX-License-Identifier: CDDL-1.0 */
#include "key-prompt.h"
#include <proto/dos.h>
#include <termios.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

static void wipe(char *p,size_t n)
{ volatile unsigned char *v=(void *)p;while(n--)*v++=0; }

/* Current AROS termios implements ICANON only: a successful tcsetattr does
 * not prove that ECHO was disabled. SetMode RAW disables console editing and
 * echo, so implement the tiny editing subset ourselves, emitting no bytes.
 * Resolve the actual FILE descriptor, never assume it matches DOS Input(). */
int oz_cli_read_secret(FILE *input,char **result,size_t *length)
{
    if(!input||!result||!length)return EINVAL;
    *result=NULL;*length=0;
    union {BPTR handle;long legacy;} native={0};
    int fd=fileno(input);
    if(fd<0||__get_default_file(fd,&native.legacy)||!native.handle||
        !IsInteractive(native.handle))return ENOTTY;
    struct termios previous={0};
    if(tcgetattr(fd,&previous))return errno?errno:EIO;
    LONG restore=(previous.c_lflag&ICANON)?0:1;
    size_t capacity=256,used=0;char *text=malloc(capacity);
    if(!text)return ENOMEM;
    if(SetMode(native.handle,1)!=DOSTRUE){wipe(text,capacity);free(text);return ENOTSUP;}
    int error=0;
    for(;;){
        errno=0;int c=fgetc(input);
        if(c==EOF){error=errno?errno:EIO;break;}
        if(c=='\n'||c=='\r')break;
        if(c==3||c==4||c==27){error=EINTR;break;}
        if(c==8||c==127){if(used)text[--used]=0;continue;}
        if(c==21){wipe(text,used);used=0;continue;}
        /* No embedded NUL or other terminal control sequences in a secret. */
        if(c<32){error=EINVAL;break;}
        if(used+1>=capacity){
            if(capacity>SIZE_MAX/2){error=EOVERFLOW;break;}
            size_t next=capacity*2;char *grown=malloc(next);
            if(!grown){error=ENOMEM;break;}
            memcpy(grown,text,used);wipe(text,capacity);free(text);
            text=grown;capacity=next;
        }
        text[used++]=(char)c;
    }
    if(SetMode(native.handle,restore)!=DOSTRUE)error=EIO;
    if(error){wipe(text,capacity);free(text);return error;}
    text[used]=0;*result=text;*length=used;return 0;
}
