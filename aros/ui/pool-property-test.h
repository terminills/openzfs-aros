/* Real root mutation followed by a fresh-guest read-only verification. */
static int pool_value(const char *name,const char *expected)
{
    struct oz_message *out=calloc(1,sizeof(*out));if(!out)return 0;
    int rc=legacy_args(31,"qemu_mirror",name,NULL,out);
    if(!expected) {
        int absent=rc==EIO&&out->status==OZ_ENGINE_ERROR&&!out->row_count&&
            strstr(out->detail,"errno=2.")!=NULL;
        free(out);return absent;
    }
    int ok=!rc&&out->row_count==1&&!strcmp(out->rows[0].name,name)&&
        !strcmp(out->rows[0].value,expected);
    if(!ok) {
        char line[512];snprintf(line,sizeof(line),"Pool get mismatch %s expected=%s status=%lu value=%s detail=%.200s\n",
            name,expected,(unsigned long)out->status,out->rows[0].value,out->detail);
        for(char *p=line;*p;p++)RawPutChar(*p);
    }
    free(out);return ok;
}
static int pool_property_tests(int write)
{
    int error=0;
#define POOL_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    {
        struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
        POOL_CHECK(m&&out);
        int rc=legacy(2,"",out);
        int advertised=(out->capability_words[1]&1)!=0;
        int listed=strstr(out->detail,"pool.set")!=NULL;
        /* Both fixture boots run a write-capable service; VERIFY imports
         * this specific pool read-only. Capabilities are service-wide. */
        if(rc||!advertised||!listed){free(m);free(out);POOL_CHECK(0);}
        rc=legacy_args(32,"qemu_mirror","comment=legacy",NULL,out);
        if(rc!=EIO||out->status!=OZ_UNSUPPORTED){free(m);free(out);POOL_CHECK(0);}
        for(unsigned test=0;test<5;test++) {
            memset(m,0,sizeof(*m));m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
            m->operation=32;m->flags=OZ_CONFIRMED;
            strcpy(m->target,"qemu_mirror");strcpy(m->argument,"comment=unsubmitted");
            if(test==0)m->flags=0;
            if(test==1)m->flags|=OZ_FORCE;
            if(test==2)m->flags|=OZ_DRYRUN;
            if(test==3)m->flags|=OZ_RECURSIVE;
            if(test==4)strcpy(m->options,"unexpected=on");
            struct oz_ticket ticket={0};rc=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
            if(rc!=(test?ENOTSUP:EINVAL)||ticket.number){free(m);free(out);POOL_CHECK(0);}
        }
        free(m);free(out);
    }
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror/files","comment=x",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","=bad",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","size=1G",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","ashift=8",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","ashift=17",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","ashift=-1",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","dedup_table_quota=0",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","delegation=maybe",OZ_ENGINE_ERROR,EINVAL));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","cachefile=/RAM/forbidden",OZ_ENGINE_ERROR,ENOTSUP));
    POOL_CHECK(!worker_argument_test(32,"qemu_mirror","readonly=on",OZ_ENGINE_ERROR,ENOTSUP));
    POOL_CHECK(!worker_argument_test(32,"qemu_absent","comment=x",OZ_ENGINE_ERROR,ENOENT));
    if(write) {
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment=123456789012345678901234567890123",OZ_ENGINE_ERROR,E2BIG));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment=bad\ncomment",OZ_ENGINE_ERROR,EINVAL));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment=",OZ_OK,0));
        POOL_CHECK(pool_value("comment",""));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment=AROS native pool=a;b",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","org.aros:pool=to-remove",OZ_OK,0));
        POOL_CHECK(pool_value("org.aros:pool","to-remove"));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","org.aros:pool=",OZ_OK,0));
        POOL_CHECK(pool_value("org.aros:pool",NULL));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","org.aros:pool=literal=a;b",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","listsnapshots=on",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","delegation=off",OZ_OK,0));
        POOL_CHECK(pool_value("delegation","off"));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","delegation=on",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","failmode=continue",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","ashift=none",OZ_OK,0));
        POOL_CHECK(pool_value("ashift","0"));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","ashift=12",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","dedup_table_quota=none",OZ_OK,0));
        POOL_CHECK(pool_value("dedup_table_quota","0"));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","dedup_table_quota=auto",OZ_OK,0));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","dedup_table_quota=16M",OZ_OK,0));
    } else {
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","comment=forbidden",OZ_ENGINE_ERROR,EROFS));
        POOL_CHECK(!worker_argument_test(32,"qemu_mirror","org.aros:pool=forbidden",OZ_ENGINE_ERROR,EROFS));
    }
    /* This legacy harness cold-imports a pre-write XDR config, so upstream
     * config-only comment is absent there. The label-discovery cold client
     * verifies its real persistence; do not fake it into the old snapshot. */
    /* Known unset pool properties return their upstream default, not ENOENT.
     * User properties above still distinguish absence from an empty value. */
    POOL_CHECK(pool_value("comment",write?"AROS native pool=a;b":"-"));
    POOL_CHECK(pool_value("org.aros:pool","literal=a;b"));
    POOL_CHECK(pool_value("listsnapshots","on"));
    POOL_CHECK(pool_value("delegation","on"));
    POOL_CHECK(pool_value("failmode","continue"));
    POOL_CHECK(pool_value("ashift","12"));
    POOL_CHECK(pool_value("dedup_table_quota","16777216"));
done:
    {char line[96];snprintf(line,sizeof(line),"Native pool properties write=%d result=%d\n",write,error);
    for(char *p=line;*p;p++)RawPutChar(*p);}
    return error;
#undef POOL_CHECK
}
