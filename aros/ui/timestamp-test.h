/* SPDX-License-Identifier: CDDL-1.0 */
static int timestamp_property_tests(int write)
{
    const char *parent="qemu_mirror/delegated-root",*ds="qemu_mirror/delegated-root/moved";
    const char *props[]={"atime","relatime"};int error=0;
    BPTR file=0;
#define TIME_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    /* Real DOS reads for independent exported-SA timestamp inspection. Wait
     * two seconds so the existing zdb textual timestamps can distinguish
     * creation/write time from read time without claiming nanosecond proof. */
    for(unsigned enabled=0;enabled<2;enabled++) {
        const char *path=enabled?"OZAUTHFILES:atime-enabled":"OZAUTHFILES:atime-disabled";
        if(write) {
            TIME_CHECK(!worker_argument_test(41,ds,"relatime=off",OZ_OK,0));
            TIME_CHECK(!worker_argument_test(41,ds,enabled?"atime=on":"atime=off",OZ_OK,0));
            file=Open((CONST_STRPTR)path,MODE_NEWFILE);TIME_CHECK(file);
            TIME_CHECK(Write(file,(APTR)"timestamp policy\n",17)==17);
            LONG closed=Close(file);file=0;TIME_CHECK(closed);
            Delay(100);
        }
        file=Open((CONST_STRPTR)path,MODE_OLDFILE);TIME_CHECK(file);
        char bytes[18]={0};TIME_CHECK(Read(file,bytes,sizeof(bytes))==17&&!memcmp(bytes,"timestamp policy\n",17));
        LONG closed=Close(file);file=0;TIME_CHECK(closed);
    }
    for(unsigned i=0;i<2;i++) {
        char on[40],off[40],invalid[40];
        snprintf(on,sizeof(on),"%s=on",props[i]);snprintf(off,sizeof(off),"%s=off",props[i]);
        TIME_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only",on,OZ_ENGINE_ERROR,EPERM));
        TIME_CHECK(!worker_argument_test(42,"qemu_mirror/hold-only",props[i],OZ_ENGINE_ERROR,EPERM));
        TIME_CHECK(!worker_argument_test(41,"qemu_mirror/delegated-root/moved@keep",on,OZ_ENGINE_ERROR,EINVAL));
        const char *bad[]={"0","1","true","inherit",""};
        for(unsigned j=0;j<sizeof(bad)/sizeof(*bad);j++) {
            snprintf(invalid,sizeof(invalid),"%s=%s",props[i],bad[j]);
            TIME_CHECK(!worker_argument_test(41,ds,invalid,OZ_ENGINE_ERROR,EINVAL));
        }
        if(write) {
            TIME_CHECK(!worker_argument_test(41,parent,off,OZ_OK,0));
            TIME_CHECK(!worker_argument_test(41,ds,on,OZ_OK,0));
            TIME_CHECK(property_matches(ds,props[i],"on",ds));
            TIME_CHECK(!worker_argument_test(42,ds,props[i],OZ_OK,0));
        } else {
            TIME_CHECK(!worker_argument_test(41,ds,on,OZ_ENGINE_ERROR,EROFS));
            TIME_CHECK(!worker_argument_test(42,ds,props[i],OZ_ENGINE_ERROR,EROFS));
        }
        TIME_CHECK(property_matches(ds,props[i],"off",parent));
    }
done:
    if(file)Close(file);
    char line[100];snprintf(line,sizeof(line),"Native authenticated timestamps write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef TIME_CHECK
}
