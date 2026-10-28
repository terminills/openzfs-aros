/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REPORT_PAGE_H
#define OZ_REPORT_PAGE_H
#include <errno.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>

struct oz_report_page { int fd, pending, eof; };

static void oz_report_page_close(struct oz_report_page *page)
{
    if(page->fd>=0)close(page->fd);
    page->fd=-1;page->pending=-1;page->eof=1;
}

static int oz_report_page_open(struct oz_report_page *page,const char *path)
{
    oz_report_page_close(page);
    if(!path||!*path)return EINVAL;
    page->fd=open(path,O_RDONLY);
    if(page->fd<0)return errno;
    page->pending=-1;page->eof=0;return 0;
}

/* Bounded byte pages, not line pages. No command is executed or retried.
 * One-byte lookahead distinguishes exact boundaries from additional output. */
static int oz_report_page_next(struct oz_report_page *page,char *text,size_t capacity)
{
    if(!page||!text||capacity<2||page->fd<0)return EINVAL;
    size_t used=0;text[0]=0;
    if(page->pending>=0){text[used++]=(char)page->pending;page->pending=-1;}
    while(used<capacity-1&&!page->eof){
        ssize_t got=read(page->fd,text+used,capacity-1-used);
        if(got<0&&errno==EINTR)continue;
        if(got<0){int error=errno;oz_report_page_close(page);text[0]=0;return error;}
        if(!got){page->eof=1;break;}
        if(memchr(text+used,0,(size_t)got)){oz_report_page_close(page);text[0]=0;return EILSEQ;}
        used+=(size_t)got;
    }
    text[used]=0;
    if(!page->eof){
        unsigned char next;ssize_t got;
        do {got=read(page->fd,&next,1);}while(got<0&&errno==EINTR);
        if(got<0|| (got>0&&!next)){
            int error=got<0?errno:EILSEQ;oz_report_page_close(page);text[0]=0;return error;
        }
        if(!got)page->eof=1;else page->pending=next;
    }
    return 0;
}
#endif
