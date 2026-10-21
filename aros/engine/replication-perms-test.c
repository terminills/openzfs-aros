/* SPDX-License-Identifier: CDDL-1.0 */
/* Native DSL grants, not a substitute permission table. */
#include <sys/zfs_context.h>
#include <sys/dsl_deleg.h>
#include <sys/dsl_prop.h>
#include <zfs_deleg.h>
#include <proto/exec.h>
static int grant(const char *ds,const char *who,const char *perm,int remove)
{
    nvlist_t *acl=fnvlist_alloc(),*perms=fnvlist_alloc();
    fnvlist_add_boolean(perms,perm);fnvlist_add_nvlist(acl,who,perms);
    int error=zfs_deleg_verify_nvlist(acl);
    if(!error)error=dsl_deleg_set(ds,acl,remove);
    fnvlist_free(perms);fnvlist_free(acl);return error;
}
int oz_replication_perms_test(int write)
{
    const char *ds="qemu_mirror/stream-source";
    const char *snap="qemu_mirror/stream-source@base";
    const char *child="qemu_mirror/stream-source/new-receive";
    gid_t group=99;
    cred_t *reader=oz_cred_create(77,77,88,1,&group);
    cred_t *other=oz_cred_create(78,78,88,0,NULL);
    int error=0;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    CHECK(reader&&other);
    CHECK(oz_send_perms(ds,B_FALSE,NULL)==EPERM);
    CHECK(oz_receive_perms(ds,B_FALSE,NULL)==EPERM);
    CHECK(!oz_send_perms(snap,B_FALSE,kcred));
    CHECK(!oz_receive_perms(ds,B_TRUE,kcred));
    CHECK(oz_receive_perms(snap,B_FALSE,kcred)==EINVAL);
    CHECK(oz_send_perms(ds,B_FALSE,reader)==EPERM);
    CHECK(oz_receive_perms(ds,B_FALSE,reader)==EPERM);
    if(!write)goto done; /* persisted temporary grants must have been removed */
    CHECK(!grant(ds,"ul$77","send:raw",0));
    CHECK(!oz_send_perms(ds,B_TRUE,reader));
    CHECK(oz_send_perms(ds,B_FALSE,reader)==EPERM);
    CHECK(oz_send_perms(ds,B_TRUE,other)==EPERM);
    CHECK(!grant(ds,"ul$77","send:raw",1));
    CHECK(!grant(ds,"ul$77","send:encrypted",0));
    CHECK(oz_send_perms(ds,B_TRUE,reader)==EPERM); /* fixture is unencrypted */
    CHECK(!grant(ds,"ul$77","send:encrypted",1));
    CHECK(!grant(ds,"gl$99","send",0));
    CHECK(!oz_send_perms(ds,B_FALSE,reader));
    CHECK(oz_send_perms(ds,B_FALSE,other)==EPERM);
    CHECK(oz_send_perms(snap,B_FALSE,reader)==EPERM);
    CHECK(!grant(ds,"gd$99","send",0));
    CHECK(!oz_send_perms(snap,B_FALSE,reader));
    CHECK(!grant(ds,"gl$99","send",1));
    CHECK(!grant(ds,"gd$99","send",1));
    CHECK(!grant(ds,"ul$77","receive:append",0));
    CHECK(oz_receive_perms(ds,B_FALSE,reader)==EPERM); /* still needs mount/create */
    CHECK(!grant(ds,"ul$77","mount",0));
    CHECK(oz_receive_perms(ds,B_FALSE,reader)==EPERM);
    CHECK(!grant(ds,"ul$77","create",0));
    CHECK(!oz_receive_perms(ds,B_FALSE,reader));
    CHECK(!oz_receive_perms(child,B_FALSE,reader));
    CHECK(oz_receive_perms(ds,B_TRUE,reader)==EPERM);
    CHECK(oz_receive_perms(child,B_TRUE,reader)==EPERM);
    CHECK(oz_receive_perms(ds,B_FALSE,other)==EPERM);
    CHECK(oz_receive_perms("qemu_mirror/stream-source/missing/child",B_FALSE,reader)==ENOENT);
    CHECK(!grant(ds,"ul$77","receive",0));
    CHECK(!oz_receive_perms(ds,B_TRUE,reader));
    CHECK(!oz_receive_perms(child,B_TRUE,reader));
    CHECK(!grant(ds,"ul$77","send:raw",0));
    CHECK(!dsl_prop_set_int(ds,"zoned",ZPROP_SRC_LOCAL,1));
    CHECK(oz_send_perms(ds,B_TRUE,reader)==EPERM);
    CHECK(oz_receive_perms(ds,B_FALSE,reader)==EPERM);
    CHECK(!oz_receive_perms(ds,B_TRUE,kcred));
    CHECK(!dsl_prop_inherit(ds,"zoned",ZPROP_SRC_INHERITED));
    CHECK(!grant(ds,"ul$77","send:raw",1));
    CHECK(!grant(ds,"ul$77","receive",1));
    CHECK(!grant(ds,"ul$77","receive:append",1));
    CHECK(!grant(ds,"ul$77","mount",1));
    CHECK(!grant(ds,"ul$77","create",1));
    CHECK(oz_receive_perms(ds,B_FALSE,reader)==EPERM);
done:
    if(reader)crfree(reader);if(other)crfree(other);
    char line[128];snprintf(line,sizeof(line),"Native replication delegation write=%d result=%d\n",write,error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef CHECK
}
