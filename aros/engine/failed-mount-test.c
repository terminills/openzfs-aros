/* SPDX-License-Identifier: CDDL-1.0 */
/* Private linker-wrapped admission-failure reproduction, never production. */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <aros/debug.h>
#include <pthread.h>
#include "zpl-fixture.h"
#include "../zpl/read-mount.h"

static int armed;
static znode_t *borrowed;
static struct oz_read_mount *retained;
static void *other_owner(void *value)
{
    int *result=value;struct oz_zpl *fs=NULL;
    *result=oz_zpl_open_writable("qemu_mirror/files",&fs);
    if(fs){(void)oz_zpl_close(fs);*result=EIO;}
    return NULL;
}
int __real_zfs_zget(zfsvfs_t *,uint64_t,znode_t **);
int __wrap_zfs_zget(zfsvfs_t *fs,uint64_t object,znode_t **out)
{
    int error=__real_zfs_zget(fs,object,out);
    if(armed&&!error) {
        armed=0;borrowed=*out;*out=NULL;
        return EIO; /* simulate a failed admission with an outstanding loan */
    }
    return error;
}
int __real_oz_read_mount_open_dataset(const char *,boolean_t,struct oz_read_mount **);
int __wrap_oz_read_mount_open_dataset(const char *name,boolean_t ro,struct oz_read_mount **out)
{
    int error=__real_oz_read_mount_open_dataset(name,ro,out);
    if(error&&*out)retained=*out;
    return error;
}
int __real_oz_zpl_fixture_create(const char *);
int __wrap_oz_zpl_fixture_create(const char *name)
{
    int error=__real_oz_zpl_fixture_create(name);
    if(error||strcmp(name,"qemu_mirror/files"))return error;
    struct oz_zpl *fs=NULL;
    armed=1;
    int first=oz_zpl_open_writable(name,&fs);
    bug("FAILED MOUNT fixture admission=%d null-output=%d borrowed=%d retained=%d\n",
        first,fs==NULL,borrowed!=NULL,retained!=NULL);
    if(first!=EIO||fs||!borrowed||!retained)return EIO;
    int busy=oz_zpl_open_writable(name,&fs);
    bug("FAILED MOUNT fixture while-loaned retry=%d null-output=%d\n",busy,fs==NULL);
    if(busy!=EBUSY||fs)return EIO;
    oz_znode_rele(borrowed);borrowed=NULL;
    pthread_t other;int foreign=0;
    int spawn=pthread_create(&other,NULL,other_owner,&foreign);
    if(spawn)return spawn;
    int joined=pthread_join(other,NULL);
    bug("FAILED MOUNT fixture foreign-owner retry=%d joined=%d\n",foreign,joined);
    if(joined||foreign!=EBUSY)return EIO;
    int retry=oz_zpl_open_writable(name,&fs);
    bug("FAILED MOUNT fixture after-loan-release retry=%d\n",retry);
    if(retry) {
        /* Test-only escape hatch: production has lost this pointer. */
        int cleanup=oz_read_mount_close(retained);retained=NULL;
        bug("FAILED MOUNT fixture private cleanup=%d\n",cleanup);
        return retry;
    }
    retained=NULL;
    error=oz_zpl_close(fs);
    if(!error)bug("RESULT native failed mount retry PASS admission-error=5 loan-drained=1 foreign-owner-denied=1 reopen=1\n");
    return error;
}
