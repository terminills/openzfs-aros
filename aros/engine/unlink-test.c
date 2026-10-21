/* SPDX-License-Identifier: CDDL-1.0 */
/* Link the real libzpool adapter; only this private RAM file is removed. */
#include <sys/zfs_context.h>
#include <sys/zfs_file.h>
#include <aros/debug.h>
#include <fcntl.h>
#include <unistd.h>
#include <proto/dos.h>
int main(void)
{
    const char *path="RAM:OpenZFS-unlink-private-test";
    int fd=open(path,O_CREAT|O_EXCL|O_WRONLY,0600);
    if(fd<0){bug("RESULT native unlink FAIL create errno=%d\n",errno);return 20;}
    if(close(fd)){bug("RESULT native unlink FAIL close\n");return 20;}
    int existing=zfs_file_unlink(path);
    errno=0;
    int missing=zfs_file_unlink(path);
    int libc_error=errno;LONG native_error=IoErr();
    bug("unlink diagnostics: adapter=%d errno=%d IoErr=%ld translated=%d\n",
        missing,libc_error,native_error,__stdc_ioerr2errno(native_error));
    if(existing||missing!=ENOENT){
        bug("RESULT native unlink FAIL existing=%d missing=%d expected=%d\n",existing,missing,ENOENT);
        return 20;
    }
    bug("RESULT native unlink PASS existing=0 missing=ENOENT\n");
    return 0;
}
