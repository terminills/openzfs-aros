/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/zfs_znode.h>
#include <proto/exec.h>
#include "../ui/api.h"
#include "zpl-fixture.h"
int oz_user_property_change(struct oz_message *,cred_t *);
int oz_exec_policy_test(int write)
{
    const char *dataset="qemu_mirror/exec-policy";
    struct oz_zpl *fs=NULL;struct oz_zpl_info file,root;
    struct oz_message *m=calloc(1,sizeof(*m));int error=0;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(m);m->operation=41;strcpy(m->target,dataset);
    if(write)CHECK(!oz_zpl_fixture_create(dataset));
    CHECK(!oz_zpl_open(dataset,&fs));
    CHECK(!oz_zpl_lookup_at(fs,oz_zpl_root(fs),"hello.txt",&file));
    CHECK(!oz_zpl_stat(fs,oz_zpl_root(fs),&root));
    if(write) {
        CHECK(oz_zpl_exec(fs));
        CHECK(!oz_zpl_access(fs,file.object,ACE_EXECUTE,kcred));
        strcpy(m->argument,"exec=off");CHECK(!oz_user_property_change(m,kcred));
        CHECK(!oz_zpl_exec(fs));
        CHECK(oz_zpl_access(fs,file.object,ACE_EXECUTE,kcred)==EACCES);
        CHECK(!oz_zpl_access(fs,root.object,ACE_EXECUTE,kcred));
        strcpy(m->argument,"exec=invalid");CHECK(oz_user_property_change(m,kcred)==EINVAL);
        CHECK(!oz_zpl_exec(fs));
        m->operation=42;strcpy(m->argument,"exec");CHECK(!oz_user_property_change(m,kcred));
        CHECK(oz_zpl_exec(fs)&&!oz_zpl_access(fs,file.object,ACE_EXECUTE,kcred));
        m->operation=41;strcpy(m->argument,"exec=off");CHECK(!oz_user_property_change(m,kcred));
    }
    CHECK(!oz_zpl_exec(fs));
    CHECK(oz_zpl_access(fs,file.object,ACE_EXECUTE,kcred)==EACCES);
    CHECK(!oz_zpl_access(fs,root.object,ACE_EXECUTE,kcred));
    char bytes[4];CHECK(!oz_zpl_read_as(fs,file.object,0,bytes,sizeof(bytes),kcred));
    CHECK(!memcmp(bytes,"Open",4));
done:
    if(fs){int rc=oz_zpl_close(fs);if(!error)error=rc;}
    free(m);char line[100];snprintf(line,sizeof(line),"Native exec policy write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef CHECK
}
