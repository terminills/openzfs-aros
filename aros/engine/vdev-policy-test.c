/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/aros_vdev_policy.h>
#include <proto/exec.h>
int oz_vdev_policy_test(void)
{
    struct oz_vdev_policy *named=NULL,*guid=NULL,*duplicate=NULL;
    struct oz_vdev_extension *extension=NULL;
    struct oz_vdev_repath *repath=NULL,*conflict=NULL;
    char *one[]={"/RAM/split-test/one"};
    char *two[]={"/RAM/split-test/one","/RAM/split-test/two"};
    int error=0;unsigned accepted=0,rejected=0;
#define POLICY_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    POLICY_CHECK(!oz_vdev_policy_begin_named("split_test",one,1,&named));
    POLICY_CHECK(oz_vdev_policy_begin_named("split_test",two,2,&duplicate)==EBUSY&&!duplicate);
    POLICY_CHECK(!oz_vdev_policy_check_named(123,"split_test",one[0]));
    POLICY_CHECK(oz_vdev_policy_check_named(456,"split_test",two[1])==EACCES);
    POLICY_CHECK(oz_vdev_policy_check_named(0,"split_test",NULL)==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check_named(123,"other_pool",two[1]));
    POLICY_CHECK(!oz_vdev_policy_begin(123,two,2,&guid));
    POLICY_CHECK(oz_vdev_policy_check_named(123,"split_test",two[1])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check_named(123,"split_test",one[0]));
    oz_vdev_policy_end(guid,NULL,NULL);guid=NULL;
    oz_vdev_policy_end(named,&accepted,&rejected);named=NULL;
    POLICY_CHECK(accepted==2&&rejected==3);
    POLICY_CHECK(!oz_vdev_policy_check_named(123,"split_test",two[1]));
    /* Reverse rule insertion order must not change intersection semantics. */
    POLICY_CHECK(!oz_vdev_policy_begin(123,one,1,&guid));
    POLICY_CHECK(!oz_vdev_policy_begin_named("split_test",two,2,&named));
    POLICY_CHECK(oz_vdev_policy_check_named(123,"split_test",two[1])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_reguid_begin(123,789));
    POLICY_CHECK(oz_vdev_policy_reguid_begin(123,999)==EBUSY);
    POLICY_CHECK(oz_vdev_policy_check(123,two[1])==EACCES);
    POLICY_CHECK(oz_vdev_policy_check(789,two[1])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check(789,one[0]));
    POLICY_CHECK(oz_vdev_policy_begin(789,two,2,&duplicate)==EBUSY&&!duplicate);
    oz_vdev_policy_reguid_end(123,789,EIO);
    POLICY_CHECK(oz_vdev_policy_check(123,two[1])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check(789,two[1]));
    POLICY_CHECK(!oz_vdev_policy_reguid_begin(123,789));
    oz_vdev_policy_reguid_end(123,789,0);
    POLICY_CHECK(oz_vdev_policy_check(789,two[1])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check(123,two[1]));
    POLICY_CHECK(!oz_vdev_policy_extend(789,"split_test",two[1],&extension));
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test",two[1]));
    oz_vdev_policy_extend_end(extension,EIO);extension=NULL;
    POLICY_CHECK(oz_vdev_policy_check(789,two[1])==EACCES);
    /* The named rule already had this path: rollback must not remove it. */
    POLICY_CHECK(!oz_vdev_policy_check_named(0,"split_test",two[1]));
    POLICY_CHECK(!oz_vdev_policy_extend(789,"split_test","/RAM/split-test/three",&extension));
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test","/RAM/split-test/three"));
    oz_vdev_policy_extend_end(extension,0);extension=NULL;
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test","/RAM/split-test/three"));
    const char *replacement="/RAM/split-test/relocated";
    POLICY_CHECK(oz_vdev_policy_repath_begin(789,"split_test",one[0],one[0],&repath)==EINVAL&&!repath);
    POLICY_CHECK(oz_vdev_policy_repath_begin(789,"split_test",one[0],two[1],&repath)==EEXIST&&!repath);
    POLICY_CHECK(oz_vdev_policy_repath_begin(789,"split_test","/missing",replacement,&repath)==ENOENT&&!repath);
    POLICY_CHECK(oz_vdev_policy_repath_begin(999,"absent",one[0],replacement,&repath)==ENOENT&&!repath);
    /* Named rule contains two, GUID rule does not: partial preparation must
     * not publish a replacement into the first rule before the second fails. */
    POLICY_CHECK(oz_vdev_policy_repath_begin(789,"split_test",two[1],replacement,&repath)==ENOENT&&!repath);
    POLICY_CHECK(oz_vdev_policy_check_named(0,"split_test",replacement)==EACCES);
    POLICY_CHECK(!oz_vdev_policy_repath_begin(789,"split_test",one[0],replacement,&repath));
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test",one[0]));
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test",replacement));
    POLICY_CHECK(oz_vdev_policy_repath_begin(789,"split_test",one[0],"/other",&conflict)==EBUSY&&!conflict);
    POLICY_CHECK(oz_vdev_policy_extend(789,"split_test","/other",&extension)==EBUSY&&!extension);
    POLICY_CHECK(oz_vdev_policy_reguid_begin(789,999)==EBUSY);
    oz_vdev_policy_repath_end(repath,EIO);repath=NULL;
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test",one[0]));
    POLICY_CHECK(oz_vdev_policy_check_named(789,"split_test",replacement)==EACCES);
    POLICY_CHECK(!oz_vdev_policy_repath_begin(789,"split_test",one[0],replacement,&repath));
    oz_vdev_policy_repath_end(repath,0);repath=NULL;
    POLICY_CHECK(oz_vdev_policy_check_named(789,"split_test",one[0])==EACCES);
    POLICY_CHECK(!oz_vdev_policy_check_named(789,"split_test",replacement));
    POLICY_CHECK(oz_vdev_policy_check_named(789,"split_test",two[1])==EACCES);
    /* Do not retire only one occurrence in a preexisting duplicate rule. */
    char *duplicates[]={"/RAM/duplicate","/RAM/duplicate"};
    POLICY_CHECK(!oz_vdev_policy_begin(555,duplicates,2,&duplicate));
    POLICY_CHECK(oz_vdev_policy_repath_begin(555,NULL,duplicates[0],replacement,&repath)==EINVAL&&!repath);
    POLICY_CHECK(!oz_vdev_policy_check(555,duplicates[0]));
    POLICY_CHECK(oz_vdev_policy_check(555,replacement)==EACCES);
done:
    oz_vdev_policy_repath_end(repath,EIO);
    oz_vdev_policy_extend_end(extension,EIO);
    oz_vdev_policy_end(named,NULL,NULL);oz_vdev_policy_end(guid,NULL,NULL);
    oz_vdev_policy_end(duplicate,NULL,NULL);
    char line[128];snprintf(line,sizeof(line),"Native named/GUID vdev policy intersection result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef POLICY_CHECK
}
