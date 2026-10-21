/* SPDX-License-Identifier: CDDL-1.0 */
/* Optional diagnostic only; never a substitute command entrypoint. */
#include <aros/debug.h>
#include <proto/dos.h>
#include <stdio.h>
#include <errno.h>
extern void __real_exit(int) __attribute__((noreturn));
void __wrap_exit(int status)
{
    int fd=fileno(stdout),before=ferror(stdout);
    int flushed=fflush(stdout),error=errno;
    bug("Whole CLI exit status=%d stdout=%p fd=%d ferror=%d flush=%d errno=%d\n",
        status,stdout,fd,before,flushed,error);
    __real_exit(status);
}
extern int __real_main(int,char **);
int __wrap_main(int argc,char **argv)
{
    bug("Whole CLI entered main argc=%d input=%p output=%p\n",argc,
        (void *)Input(),(void *)Output());
    for(int i=0;i<argc;i++)bug("Whole CLI argv[%d]=%s\n",i,argv[i]);
    int status=__real_main(argc,argv);
    bug("Whole CLI returned main status=%d\n",status);
    return status;
}
