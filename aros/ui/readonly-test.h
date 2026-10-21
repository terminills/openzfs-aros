/* SPDX-License-Identifier: CDDL-1.0 */
static int readonly_property_tests(int write)
{
    const char *parent="qemu_mirror/delegated-root",*dataset="qemu_mirror/delegated-root/moved";
    const char *path="OZAUTHFILES:readonly-policy";
    int error=0;BPTR file=0,unexpected=0;
#define RO_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    RO_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only","readonly=on",OZ_ENGINE_ERROR,EPERM));
    RO_CHECK(!worker_argument_test(42,"qemu_mirror/hold-only","readonly",OZ_ENGINE_ERROR,EPERM));
    RO_CHECK(!worker_argument_test(41,"qemu_mirror/delegated-root/moved@keep","readonly=off",OZ_ENGINE_ERROR,EINVAL));
    const char *invalid[]={"readonly=0","readonly=1","readonly=true","readonly=inherit","readonly="};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        RO_CHECK(!worker_argument_test(41,dataset,invalid[i],OZ_ENGINE_ERROR,EINVAL));
    if(write) {
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);RO_CHECK(file);
        RO_CHECK(Write(file,(APTR)"A",1)==1);
        RO_CHECK(!worker_argument_test(41,dataset,"readonly=on",OZ_OK,0));
        RO_CHECK(property_matches(dataset,"readonly","on",dataset));
        RO_CHECK(Write(file,(APTR)"X",1)==-1&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        RO_CHECK(SetFileSize(file,0,OFFSET_BEGINNING)==-1&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        unexpected=Open((CONST_STRPTR)"OZAUTHFILES:readonly-new",MODE_NEWFILE);
        RO_CHECK(!unexpected&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        RO_CHECK(!DeleteFile((CONST_STRPTR)path)&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        RO_CHECK(!Rename((CONST_STRPTR)path,(CONST_STRPTR)"OZAUTHFILES:readonly-renamed")&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        RO_CHECK(!worker_argument_test(41,dataset,"readonly=off",OZ_OK,0));
        RO_CHECK(Write(file,(APTR)"B",1)==1);
        RO_CHECK(!worker_argument_test(41,parent,"readonly=on",OZ_OK,0));
        /* Local off overrides parent on until explicitly inherited. */
        RO_CHECK(property_matches(dataset,"readonly","off",dataset));
        RO_CHECK(Write(file,(APTR)"C",1)==1);
        RO_CHECK(!worker_argument_test(42,dataset,"readonly",OZ_OK,0));
        RO_CHECK(property_matches(dataset,"readonly","on",parent));
        RO_CHECK(Write(file,(APTR)"X",1)==-1&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        RO_CHECK(!worker_argument_test(41,parent,"readonly=off",OZ_OK,0));
        RO_CHECK(property_matches(dataset,"readonly","off",parent));
        RO_CHECK(Write(file,(APTR)"D",1)==1);
        RO_CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
        LONG closed=Close(file);file=0;RO_CHECK(closed);
    } else {
        RO_CHECK(!worker_argument_test(41,dataset,"readonly=off",OZ_ENGINE_ERROR,EROFS));
        RO_CHECK(!worker_argument_test(42,dataset,"readonly",OZ_ENGINE_ERROR,EROFS));
    }
    RO_CHECK(property_matches(dataset,"readonly","off",parent));
    const char *persistent="qemu_mirror/delegated-root/readonly-persist";
    if(write) {
        RO_CHECK(!worker_test(37,persistent,OZ_OK,0));
        RO_CHECK(!worker_argument_test(41,persistent,"readonly=on",OZ_OK,0));
    }
    RO_CHECK(property_matches(persistent,"readonly","on",persistent));
    RO_CHECK(!worker_options_test(43,persistent,"OZROPERSIST","readonly=off",OZ_ENGINE_ERROR,EROFS));
    file=Open((CONST_STRPTR)path,MODE_OLDFILE);RO_CHECK(file);
    char bytes[5]={0};RO_CHECK(Read(file,bytes,sizeof(bytes))==4&&!memcmp(bytes,"ABCD",4));
    LONG closed=Close(file);file=0;RO_CHECK(closed);
done:
    if(file)Close(file);
    if(unexpected)Close(unexpected);
    char line[120];snprintf(line,sizeof(line),"Native authenticated readonly write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef RO_CHECK
}
