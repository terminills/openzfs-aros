/* Disposable native file-pool test; no device or implicit discovery requests. */
static int split_import(const char *name,unsigned parity)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=7;m->flags=OZ_CONFIRMED;strcpy(m->target,name);
    strcpy(m->options,"readonly=off");m->topology.device_count=2;
    for(unsigned i=0;i<2;i++)snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),
        "RAM:OZAddTest/disk%u",i*2+parity);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)error=poll_result(&ticket,out);
    if(!error&&out->status!=OZ_OK)error=EIO;
    for(const char *p=out->detail;*p;p++)RawPutChar(*p);
    RawPutChar('\n');
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    free(m);free(out);return error;
}
static int pool_split_test(void)
{
    struct oz_message *out=calloc(1,sizeof(*out));unsigned char *buf=calloc(1,1024*1024);
    BPTR file=0,dir=0;int error=0;
    char guids[4][32]={{0}},options[128];
#define SPLIT_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    SPLIT_CHECK(out&&buf);dir=CreateDir((CONST_STRPTR)"RAM:OZAddTest");SPLIT_CHECK(dir);UnLock(dir);dir=0;
    for(unsigned i=0;i<4;i++) {
        char path[64];snprintf(path,sizeof(path),"RAM:OZAddTest/disk%u",i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);SPLIT_CHECK(file);
        for(unsigned n=0;n<64;n++)SPLIT_CHECK(Write(file,buf,1024*1024)==1024*1024);
        SPLIT_CHECK(Close(file));file=0;
    }
    SPLIT_CHECK(!add_request(5,0,2,NULL,0));
    SPLIT_CHECK(!add_request(9,2,2,NULL,0));
    SPLIT_CHECK(!worker_test(37,"aros_added/files",OZ_OK,0));
    SPLIT_CHECK(!worker_options_test(43,"aros_added/files","OZSPLIT","readonly=off",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZSPLIT:shared",MODE_NEWFILE);SPLIT_CHECK(file);
    SPLIT_CHECK(Write(file,"Before mirror split\n",20)==20&&Flush(file));SPLIT_CHECK(Close(file));file=0;
    SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split","guids=1",OZ_ENGINE_ERROR,EBUSY));
    SPLIT_CHECK(!worker_test(44,"aros_added/files",OZ_OK,0));
    SPLIT_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    SPLIT_CHECK(!add_request(7,0,4,"readonly=on",0));
    SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split","guids=1",OZ_ENGINE_ERROR,EROFS));
    SPLIT_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    SPLIT_CHECK(!add_request(7,0,4,"readonly=off",0));
    SPLIT_CHECK(!legacy(4,"aros_added",out)&&out->status==OZ_OK&&!out->next_cursor);
    for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"file")) {
        for(unsigned d=0;d<4;d++) {
            char suffix[16];snprintf(suffix,sizeof(suffix),"/disk%u",d);
            if(strstr(out->rows[i].name,suffix)) {
                const char *p=strstr(out->rows[i].value,"guid=");SPLIT_CHECK(p);
                unsigned n=0;p+=5;while(p[n]>='0'&&p[n]<='9'&&n<31){guids[d][n]=p[n];n++;}
                guids[d][n]=0;
            }
        }
    }
    for(unsigned i=0;i<4;i++)SPLIT_CHECK(guids[i][0]);
    {
        struct oz_message *request=calloc(1,sizeof(*request));SPLIT_CHECK(request);
        request->magic=OZ_MAGIC;request->version=OZ_VERSION;request->msg.mn_Length=sizeof(*request);
        request->operation=14;strcpy(request->target,"aros_added");strcpy(request->argument,"aros_split");
        strcpy(request->options,"guids=1");
        const ULONG flags[]={OZ_FORCE,OZ_DRYRUN,OZ_RECURSIVE,OZ_RAW,OZ_RESUMABLE};
        int refused=1;
        for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
            struct oz_ticket ticket={0};request->flags=OZ_CONFIRMED|flags[i];
            if(oz_admission_submit(OZ_ADMISSION_TEST_PORT,request,&ticket)!=ENOTSUP)refused=0;
            if(ticket.number)oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);
        }
        free(request);SPLIT_CHECK(refused);
    }
    const char *invalid[]={"guids=0","guids=1","guids=18446744073709551616","guids=1,1","force=on"};
    for(unsigned i=0;i<5;i++)SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split",invalid[i],
        OZ_ENGINE_ERROR,i==2?ERANGE:EINVAL));
    snprintf(options,sizeof(options),"guids=%s,%s",guids[0],guids[1]);
    SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split",options,OZ_ENGINE_ERROR,EINVAL));
    snprintf(options,sizeof(options),"guids=%s,%s",guids[3],guids[1]); /* reverse order */
    SPLIT_CHECK(!worker_options_test(14,"aros_added","qemu_mirror",options,OZ_ENGINE_ERROR,EEXIST));
    SPLIT_CHECK(!worker_argument_test(32,"aros_added","feature@zpool_checkpoint=enabled",OZ_OK,0));
    SPLIT_CHECK(!worker_argument_test(22,"aros_added","create",OZ_OK,0));
    SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split",options,OZ_ENGINE_ERROR,1024));
    SPLIT_CHECK(!worker_argument_test(22,"aros_added","discard",OZ_OK,0));
    {
        unsigned n;
        for(n=0;n<120&&!checkpoint_state("aros_added","none");n++)Delay(50);
        SPLIT_CHECK(n<120);
    }
    SPLIT_CHECK(!worker_options_test(14,"aros_added","aros_split",options,OZ_OK,0));
    SPLIT_CHECK(!legacy(3,"",out)&&out->status==OZ_OK);
    for(unsigned i=0;i<out->row_count;i++)SPLIT_CHECK(strcmp(out->rows[i].name,"aros_split"));
    SPLIT_CHECK(!split_import("aros_split",1));
    for(unsigned pool=0;pool<2;pool++) {
        const char *name=pool?"aros_split":"aros_added";
        const char *dataset=pool?"aros_split/files":"aros_added/files";
        SPLIT_CHECK(!legacy(4,name,out)&&out->status==OZ_OK&&!out->next_cursor);
        unsigned leaves=0;
        for(unsigned i=0;i<out->row_count;i++) {
            SPLIT_CHECK(!strcmp(out->rows[i].state,"ONLINE"));
            if(!strcmp(out->rows[i].kind,"file"))leaves++;
        }
        SPLIT_CHECK(leaves==2);
        SPLIT_CHECK(!worker_options_test(43,dataset,"OZSPLIT","readonly=off",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZSPLIT:shared",MODE_OLDFILE);SPLIT_CHECK(file);
        SPLIT_CHECK(Read(file,buf,64)==20&&!memcmp(buf,"Before mirror split\n",20));SPLIT_CHECK(Close(file));file=0;
        if(!pool) {
            file=Open((CONST_STRPTR)"OZSPLIT:source-only",MODE_NEWFILE);SPLIT_CHECK(file);
            SPLIT_CHECK(Write(file,"independent",11)==11&&Flush(file));SPLIT_CHECK(Close(file));file=0;
        } else {
            file=Open((CONST_STRPTR)"OZSPLIT:source-only",MODE_OLDFILE);SPLIT_CHECK(!file&&IoErr()==ERROR_OBJECT_NOT_FOUND);
        }
        SPLIT_CHECK(!worker_test(44,dataset,OZ_OK,0));
    }
    SPLIT_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    SPLIT_CHECK(!worker_test(8,"aros_split",OZ_OK,0));
    SPLIT_CHECK(!split_import("aros_added",0));
    SPLIT_CHECK(!split_import("aros_split",1));
    SPLIT_CHECK(!worker_test(6,"aros_added",OZ_OK,0));
    SPLIT_CHECK(!worker_test(6,"aros_split",OZ_OK,0));
done:
    if(file)Close(file);
    if(dir)UnLock(dir);
    free(buf);free(out);
    char line[128];snprintf(line,sizeof(line),"Native mirror split explicit leaves/export/import/independent DOS result=%d\n",error);
    for(const char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef SPLIT_CHECK
}
