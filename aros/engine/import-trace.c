/* Diagnostic-only forwarding of upstream import messages to retained serial.
 * Selected explicitly at link time; never enabled in production by default. */
#include <proto/exec.h>
#include <string.h>
void __real___zfs_dbgmsg(char *);
void __wrap___zfs_dbgmsg(char *message)
{
    if(strstr(message,"spa.c:") || strstr(message,"spa_misc.c:") ||
       strstr(message,"vdev.c:")) {
        const char *prefix="Import trace: ";
        while(*prefix)RawPutChar(*prefix++);
        for(const char *p=message;*p;p++)RawPutChar(*p);
        RawPutChar('\n');
    }
    __real___zfs_dbgmsg(message);
}
