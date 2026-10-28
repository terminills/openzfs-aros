/* SPDX-License-Identifier: CDDL-1.0 */
#include "report-export.h"
#include <stdio.h>
#define CHECK(c) do { if(!(c)){fprintf(stderr,"FAIL export line=%d\n",__LINE__);return 20;} }while(0)
int main(void)
{
    char dir[]="/tmp/openzfs-report-export-XXXXXX",source[256],dest[256],alias[256];
    CHECK(mkdtemp(dir));
    snprintf(source,sizeof(source),"%s/source",dir);
    snprintf(dest,sizeof(dest),"%s/destination",dir);
    snprintf(alias,sizeof(alias),"%s/alias",dir);
    int fd=open(source,O_RDWR|O_CREAT|O_EXCL,0600);CHECK(fd>=0);
    char bytes[8192];for(unsigned i=0;i<sizeof(bytes);++i)bytes[i]=(char)(i%251);
    for(unsigned i=0;i<17;++i)CHECK(write(fd,bytes,sizeof(bytes))==sizeof(bytes));
    CHECK(lseek(fd,17,SEEK_SET)==17);
    uint64_t count=0;
    CHECK(!oz_report_copy_new(fd,dest,&count)&&count==17*sizeof(bytes));
    CHECK(lseek(fd,0,SEEK_CUR)==17);
    CHECK(oz_report_copy_new(fd,dest,&count)==EEXIST);
    CHECK(!symlink(source,alias));
    CHECK(oz_report_copy_new(fd,alias,&count)==EEXIST);
    CHECK(!unlink(dest));
    struct oz_report_export *worker=NULL;
    CHECK(!oz_report_export_start(fd,dest,&worker));
    CHECK(oz_report_export_free(worker)==EBUSY);
    /* The exporter owns its duplicate even after the pager closes/unlinks. */
    CHECK(!close(fd));CHECK(!unlink(source));
    int error;while((error=oz_report_export_poll(worker))==EAGAIN)usleep(1000);
    CHECK(!error&&!worker->error&&worker->bytes==17*sizeof(bytes));
    CHECK(!oz_report_export_free(worker));
    fd=open(dest,O_RDONLY);CHECK(fd>=0);
    for(unsigned i=0;i<17;++i){char actual[8192];CHECK(read(fd,actual,sizeof(actual))==sizeof(actual));CHECK(!memcmp(actual,bytes,sizeof(bytes)));}
    CHECK(read(fd,bytes,1)==0);CHECK(!close(fd));
    CHECK(!unlink(alias));CHECK(!unlink(dest));CHECK(!rmdir(dir));
    puts("PASS report export: exact binary bytes, unchanged cursor, no overwrite, symlink refusal, joined worker, duplicate lifetime");
    return 0;
}
