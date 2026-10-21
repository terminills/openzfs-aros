/* SPDX-License-Identifier: CDDL-1.0 */
static int recordsize_tests(int write)
{
    const char *parent="qemu_mirror/delegated-root",*dataset="qemu_mirror/delegated-root/moved";
    int error=0;BPTR file=0;
    unsigned char *block=malloc(65536),*readback=malloc(65536);
    const char *sizes[]={"32K","128K","1M"};
    const char *bytes[]={"32768","131072","1048576"};
#define RECORD_CHECK(expr) do {if(!(expr)){error=__LINE__;goto done;}}while(0)
    RECORD_CHECK(block&&readback);
    RECORD_CHECK(!worker_argument_test(41,"qemu_mirror/hold-only","recordsize=32K",OZ_ENGINE_ERROR,EPERM));
    RECORD_CHECK(!worker_argument_test(42,"qemu_mirror/hold-only","recordsize",OZ_ENGINE_ERROR,EPERM));
    RECORD_CHECK(!worker_argument_test(41,"qemu_mirror/delegated-root/moved@keep","recordsize=32K",OZ_ENGINE_ERROR,EINVAL));
    const char *invalid[]={"recordsize=0","recordsize=511","recordsize=513","recordsize=3K",
        "recordsize=32M","recordsize=none","recordsize=-1","recordsize=","recordsize=1KBgarbage"};
    for(unsigned i=0;i<sizeof(invalid)/sizeof(*invalid);i++)
        RECORD_CHECK(!worker_argument_test(41,dataset,invalid[i],OZ_ENGINE_ERROR,EINVAL));
    RECORD_CHECK(!worker_argument_test(41,dataset,"recordsize=18446744073709551616",OZ_ENGINE_ERROR,ERANGE));
    if(write) {
        /* Keep this fixture below later quota tests' physical-space limits. */
        RECORD_CHECK(!worker_argument_test(41,dataset,"compression=lz4",OZ_OK,0));
        RECORD_CHECK(!worker_argument_test(41,parent,"recordsize=64K",OZ_OK,0));
        RECORD_CHECK(!worker_argument_test(41,dataset,"recordsize=512",OZ_OK,0));
        RECORD_CHECK(property_matches(dataset,"recordsize","512",dataset));
        RECORD_CHECK(!worker_argument_test(42,dataset,"recordsize",OZ_OK,0));
        RECORD_CHECK(property_matches(dataset,"recordsize","65536",parent));
    } else {
        RECORD_CHECK(!worker_argument_test(42,dataset,"recordsize",OZ_ENGINE_ERROR,EROFS));
        RECORD_CHECK(property_matches(dataset,"recordsize","131072",dataset));
    }
    for(unsigned i=0;i<65536;i++)block[i]=(unsigned char)(1+(i%251));
    for(unsigned n=0;n<3;n++) {
        char argument[80],path[100];
        snprintf(argument,sizeof(argument),"recordsize=%s",sizes[n]);
        snprintf(path,sizeof(path),"OZAUTHFILES:record-%s",sizes[n]);
        RECORD_CHECK(!worker_argument_test(41,dataset,argument,write?OZ_OK:OZ_ENGINE_ERROR,write?0:EROFS));
        if(write) {
            RECORD_CHECK(property_matches(dataset,"recordsize",bytes[n],dataset));
            file=Open((CONST_STRPTR)path,MODE_NEWFILE);RECORD_CHECK(file);
            for(unsigned i=0;i<32;i++)RECORD_CHECK(Write(file,block,65536)==65536);
            RECORD_CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
            LONG closed=Close(file);file=0;RECORD_CHECK(closed);
        }
        file=Open((CONST_STRPTR)path,MODE_OLDFILE);RECORD_CHECK(file);
        for(unsigned i=0;i<32;i++) {
            RECORD_CHECK(Read(file,readback,65536)==65536);
            RECORD_CHECK(!memcmp(block,readback,65536));
        }
        RECORD_CHECK(Read(file,readback,1)==0);
        LONG closed=Close(file);file=0;RECORD_CHECK(closed);
    }
    if(write)RECORD_CHECK(!worker_argument_test(41,dataset,"recordsize=128K",OZ_OK,0));
    RECORD_CHECK(property_matches(dataset,"recordsize","131072",dataset));
done:
    if(file)Close(file);
    free(block);free(readback);
    char line[120];snprintf(line,sizeof(line),"Native authenticated recordsize write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef RECORD_CHECK
}
