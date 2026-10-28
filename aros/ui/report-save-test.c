/* SPDX-License-Identifier: CDDL-1.0 */
#include "report-save.h"
#include <stdio.h>
#include <stdlib.h>
#include <sys/stat.h>

int main(void)
{
    char dir[]="/tmp/openzfs-report-save-XXXXXX",path[256],alias[256],empty[256];
    if(!mkdtemp(dir))return 1;
    snprintf(path,sizeof(path),"%s/report",dir);
    snprintf(alias,sizeof(alias),"%s/alias",dir);
    snprintf(empty,sizeof(empty),"%s/empty",dir);
    const char text[]="OpenZFS report\nexact second line\n";
    int failed=oz_report_save_new(path,text)!=0;
    failed|=oz_report_save_new(path,"replacement")!=EEXIST;
    failed|=symlink(path,alias)!=0;
    failed|=oz_report_save_new(alias,"replacement")!=EEXIST;
    char bytes[128]={0};int fd=open(path,O_RDONLY);
    ssize_t n=fd<0?-1:read(fd,bytes,sizeof(bytes));if(fd>=0)close(fd);
    failed|=n!=(ssize_t)strlen(text)||memcmp(bytes,text,strlen(text));
    struct stat st;failed|=stat(path,&st)!=0;
    if(!failed)failed|=(st.st_mode&0777)!=0600;
    failed|=oz_report_save_new(empty,"")!=0;
    failed|=oz_report_save_new(NULL,text)!=EINVAL;
    failed|=oz_report_save_new("",text)!=EINVAL;
    failed|=oz_report_save_new(path,NULL)!=EINVAL;
    unlink(alias);unlink(path);unlink(empty);rmdir(dir);
    puts(failed?"FAIL report save":"PASS report save: exact bytes, no overwrite, symlink refusal, private mode, empty and invalid inputs");
    return failed?1:0;
}
