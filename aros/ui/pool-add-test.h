/* Dedicated RAM-file pool; does not alter the baseline device fixture. */
static int removal_cancel_test;
/* Native engine's Solaris-compat errno; absent from AROS POSIX errno.h. */
enum { test_enotactive=4098 };
static int add_role_request(unsigned op,unsigned first,unsigned count,const char *opts,int expected,unsigned role)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;strcpy(m->target,"aros_added");
    if(opts)snprintf(m->options,sizeof(m->options),"%s",opts);
    m->topology.device_count=count;
    if(op!=7){m->topology.group_count=1;m->topology.groups[0].role=role;m->topology.groups[0].layout=count>1?OZ_MIRROR:OZ_STRIPE;}
    if(op==5)m->topology.ashift=12;
    for(unsigned i=0;i<count;i++)snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),"RAM:OZAddTest/disk%u",first+i);
    if(op==9&&!expected) {
        struct oz_ticket rejected={0};
        const ULONG flags[]={OZ_CONFIRMED|OZ_FORCE,OZ_CONFIRMED|OZ_DRYRUN,OZ_CONFIRMED|OZ_RECURSIVE};
        for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
            m->flags=flags[i];
            if(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)!=ENOTSUP){free(m);free(out);return EIO;}
        }
        m->flags=OZ_CONFIRMED;strcpy(m->options,"force=on");
        if(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&rejected)!=ENOTSUP){free(m);free(out);return EIO;}
        m->options[0]=0;
    }
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)error=poll_result(&ticket,out);
    if(!error) {
        char wanted[32];snprintf(wanted,sizeof(wanted),"errno=%d",expected);
        const char *p=strstr(out->detail,"errno=");
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!p||strcmp(p,wanted))error=EIO;
        for(p=out->detail;*p;p++)RawPutChar(*p);RawPutChar('\n');
    }
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    free(m);free(out);return error;
}
static int add_request(unsigned op,unsigned first,unsigned count,const char *opts,int expected)
{return add_role_request(op,first,count,opts,expected,OZ_DATA);}
static int added_aux_status(struct oz_message *out,int readonly)
{
    int error=legacy(4,"aros_added",out);if(error)return error;
    if(out->status!=OZ_OK||out->next_cursor)return EIO;
    unsigned log=0,cache=0,spare=0;
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];
        char line[640];snprintf(line,sizeof(line),"Added auxiliary status %s %s %s %s\n",r->kind,r->name,r->state,r->value);
        for(const char *p=line;*p;p++)RawPutChar(*p);
        if(strcmp(r->kind,"file"))continue;
        if(strstr(r->value,"role=log ")&&strcmp(r->state,"ONLINE"))return EIO;
        if(strstr(r->value,"role=cache ")||strstr(r->value,"role=spare ")) {
            if(strcmp(r->state,readonly?"INACTIVE":"ONLINE"))return EIO;
            if(readonly&&!strstr(r->value,"statistics=unavailable"))return EIO;
        }
        if(strstr(r->value,"role=log "))log++;
        if(strstr(r->value,"role=cache "))cache++;
        if(strstr(r->value,"role=spare "))spare++;
    }
    return log==1&&cache==1&&spare==1?0:EIO;
}
static int pool_add_test(void)
{
    struct oz_message *out=calloc(1,sizeof(*out));unsigned char *buf=calloc(1,1024*1024);
    BPTR file=0,dir=0;int error=0;UQUAD before=0,after=0;
#define ADD_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    ADD_CHECK(out&&buf);dir=CreateDir((CONST_STRPTR)"RAM:OZAddTest");ADD_CHECK(dir);UnLock(dir);dir=0;
    {
        struct oz_message *m=calloc(1,sizeof(*m));ADD_CHECK(m);
        m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
        m->operation=10;strcpy(m->target,"aros_added");strcpy(m->argument,"1");
        const ULONG flags[]={OZ_CONFIRMED|OZ_FORCE,OZ_CONFIRMED|OZ_DRYRUN,OZ_CONFIRMED|OZ_RECURSIVE,OZ_CONFIRMED};
        int rejected=1;
        for(unsigned i=0;i<4;i++) {
            struct oz_ticket ticket={0};m->flags=flags[i];
            if(i==3)strcpy(m->options,"force=on");
            if(oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket)!=ENOTSUP)rejected=0;
            if(ticket.number)oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);
        }
        free(m);ADD_CHECK(rejected);
    }
    for(unsigned i=0;i<8;i++) {
        char path[64];snprintf(path,sizeof(path),"RAM:OZAddTest/disk%u",i);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);ADD_CHECK(file);
        for(unsigned n=0;n<64;n++)ADD_CHECK(Write(file,buf,1024*1024)==1024*1024);
        ADD_CHECK(Close(file));file=0;
    }
    ADD_CHECK(!add_request(5,0,2,NULL,0));
    ADD_CHECK(!worker_test(37,"aros_added/files",OZ_OK,0));
    ADD_CHECK(!worker_options_test(43,"aros_added/files","OZADDED","readonly=off",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZADDED:before",MODE_NEWFILE);ADD_CHECK(file);
    ADD_CHECK(Write(file,"Before expansion\n",17)==17&&Flush(file));ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!legacy(4,"aros_added",out));
    for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"pool"))before=out->rows[i].total;
    ADD_CHECK(before);
    file=Open((CONST_STRPTR)"RAM:OZAddTest/disk3",MODE_READWRITE);ADD_CHECK(file);
    ADD_CHECK(Write(file,"X",1)==1);ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!add_request(9,2,2,NULL,EEXIST));
    file=Open((CONST_STRPTR)"RAM:OZAddTest/disk2",MODE_OLDFILE);ADD_CHECK(file);
    for(unsigned n=0;n<64;n++) {ADD_CHECK(Read(file,buf,1024*1024)==1024*1024);for(unsigned i=0;i<1024*1024;i++)ADD_CHECK(!buf[i]);}
    ADD_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"RAM:OZAddTest/disk3",MODE_READWRITE);ADD_CHECK(file);
    ADD_CHECK(Write(file,buf,1)==1);ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!add_request(9,2,2,NULL,0));
    ADD_CHECK(!legacy(4,"aros_added",out));
    unsigned mirrors=0,leaves=0;
    for(unsigned i=0;i<out->row_count;i++) {
        ADD_CHECK(!strcmp(out->rows[i].state,"ONLINE"));
        if(!strcmp(out->rows[i].kind,"pool"))after=out->rows[i].total;
        if(!strcmp(out->rows[i].kind,"mirror"))mirrors++;
        if(!strcmp(out->rows[i].kind,"file"))leaves++;
    }
    ADD_CHECK(after>before&&mirrors==2&&leaves==4);
    /* Disabled allocation-class feature must not be silently enabled. Both
     * refusals must leave the proposed file entirely blank and reusable. */
    ADD_CHECK(!add_role_request(9,4,1,NULL,ENOTSUP,OZ_SPECIAL));
    ADD_CHECK(!add_role_request(9,4,1,NULL,ENOTSUP,OZ_DEDUP));
    file=Open((CONST_STRPTR)"RAM:OZAddTest/disk4",MODE_OLDFILE);ADD_CHECK(file);
    for(unsigned n=0;n<64;n++){ADD_CHECK(Read(file,buf,1024*1024)==1024*1024);for(unsigned i=0;i<1024*1024;i++)ADD_CHECK(!buf[i]);}
    ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!add_role_request(9,4,1,NULL,0,OZ_LOG));
    ADD_CHECK(!add_role_request(9,5,1,NULL,0,OZ_CACHE));
    ADD_CHECK(!add_role_request(9,6,1,NULL,0,OZ_SPARE));
    ADD_CHECK(!added_aux_status(out,0));
    file=Open((CONST_STRPTR)"OZADDED:after",MODE_NEWFILE);ADD_CHECK(file);
    ADD_CHECK(Write(file,"After expansion\n",16)==16&&Flush(file));ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!worker_test(44,"aros_added/files",OZ_OK,0));
    ADD_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    ADD_CHECK(!add_request(7,0,7,"readonly=on",0));
    ADD_CHECK(!added_aux_status(out,1));
    ADD_CHECK(!worker_options_test(10,"aros_added","1",NULL,OZ_ENGINE_ERROR,EROFS));
    ADD_CHECK(!worker_options_test(10,"aros_added","cancel",NULL,OZ_ENGINE_ERROR,EROFS));
    ADD_CHECK(!add_request(9,2,2,NULL,EROFS));
    ADD_CHECK(!worker_options_test(43,"aros_added/files","OZADDED","readonly=on",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZADDED:before",MODE_OLDFILE);ADD_CHECK(file);
    ADD_CHECK(Read(file,buf,32)==17&&!memcmp(buf,"Before expansion\n",17));ADD_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZADDED:after",MODE_OLDFILE);ADD_CHECK(file);
    ADD_CHECK(Read(file,buf,32)==16&&!memcmp(buf,"After expansion\n",16));ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!worker_test(44,"aros_added/files",OZ_OK,0));
    ADD_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    ADD_CHECK(!add_request(7,0,7,"readonly=off",0));
    ADD_CHECK(!added_aux_status(out,0));
    ADD_CHECK(!worker_options_test(10,"aros_added","cancel",NULL,OZ_ENGINE_ERROR,test_enotactive));
    ADD_CHECK(!worker_options_test(10,"aros_added","0",NULL,OZ_ENGINE_ERROR,EINVAL));
    ADD_CHECK(!worker_options_test(10,"aros_added","bad",NULL,OZ_ENGINE_ERROR,EINVAL));
    ADD_CHECK(!worker_options_test(10,"aros_added","18446744073709551616",NULL,OZ_ENGINE_ERROR,EINVAL));
    ADD_CHECK(!worker_options_test(10,"aros_added","1",NULL,OZ_ENGINE_ERROR,ENODEV));
    const char *remove_roles[]={"role=cache ","role=spare ","role=log "};
    for(unsigned j=0;j<3;j++) {
        char guid[32]={0};ADD_CHECK(!legacy(4,"aros_added",out));
        for(unsigned i=0;i<out->row_count;i++) {
            struct oz_row *r=&out->rows[i];
            if(strcmp(r->kind,"file")||!strstr(r->value,remove_roles[j]))continue;
            const char *p=strstr(r->value,"guid=");ADD_CHECK(p);
            snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));
        }
        ADD_CHECK(guid[0]);
        ADD_CHECK(!worker_options_test(10,"aros_added",guid,NULL,OZ_OK,0));
        ADD_CHECK(!legacy(4,"aros_added",out));
        for(unsigned i=0;i<out->row_count;i++)ADD_CHECK(!strstr(out->rows[i].value,remove_roles[j]));
        ADD_CHECK(!worker_options_test(10,"aros_added",guid,NULL,OZ_ENGINE_ERROR,ENODEV));
    }
    ADD_CHECK(!worker_options_test(43,"aros_added/files","OZADDED","readonly=off",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZADDED:after",MODE_OLDFILE);ADD_CHECK(file);
    ADD_CHECK(Read(file,buf,32)==16&&!memcmp(buf,"After expansion\n",16));ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!worker_test(44,"aros_added/files",OZ_OK,0));
    /* Remove one populated top-level data mirror using upstream evacuation. */
    char data_guid[32]={0};UQUAD allocated=0;
    ADD_CHECK(!legacy(4,"aros_added",out));
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];
        if(strcmp(r->kind,"mirror")||r->used<=allocated)continue;
        const char *p=strstr(r->value,"guid=");ADD_CHECK(p);
        snprintf(data_guid,sizeof(data_guid),"%llu",strtoull(p+5,NULL,10));allocated=r->used;
    }
    ADD_CHECK(data_guid[0]&&allocated);
    ADD_CHECK(!worker_options_test(10,"aros_added",data_guid,NULL,OZ_ENGINE_ERROR,ENOTSUP));
    ADD_CHECK(!worker_argument_test(32,"aros_added","feature@device_removal=enabled",OZ_OK,0));
    /* Tiny equal mirrors reserve half the pool as slop. Preserve the real
     * ENOSPC refusal, then add actual capacity rather than changing policy. */
    ADD_CHECK(!worker_options_test(10,"aros_added",data_guid,NULL,OZ_ENGINE_ERROR,ENOSPC));
    ADD_CHECK(!add_role_request(9,7,1,NULL,0,OZ_DATA));
    ADD_CHECK(!worker_options_test(10,"aros_added",data_guid,NULL,OZ_OK,0));
    int removed=0;
    if(removal_cancel_test) {
        ADD_CHECK(!legacy(4,"aros_added",out));ADD_CHECK(strstr(out->detail,"removal=running"));
        ADD_CHECK(!worker_options_test(10,"aros_added","cancel",NULL,OZ_OK,0));
        ADD_CHECK(!legacy(4,"aros_added",out));ADD_CHECK(strstr(out->detail,"removal=canceled"));
        ADD_CHECK(!worker_options_test(10,"aros_added","cancel",NULL,OZ_ENGINE_ERROR,test_enotactive));
        unsigned mirrors=0,indirect=0;
        for(unsigned i=0;i<out->row_count;i++) {
            if(!strcmp(out->rows[i].kind,"mirror"))mirrors++;
            if(!strcmp(out->rows[i].kind,"indirect"))indirect++;
        }
        ADD_CHECK(mirrors==2&&!indirect);removed=1;
    } else for(unsigned n=0;n<120;n++) {
        ADD_CHECK(!legacy(4,"aros_added",out));
        if(strstr(out->detail,"removal=finished")){removed=1;break;}Delay(50);
    }
    ADD_CHECK(removed);
    ADD_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    ADD_CHECK(!add_request(7,0,8,"readonly=on",0));
    ADD_CHECK(!worker_options_test(43,"aros_added/files","OZADDED","readonly=on",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZADDED:after",MODE_OLDFILE);ADD_CHECK(file);
    ADD_CHECK(Read(file,buf,32)==16&&!memcmp(buf,"After expansion\n",16));ADD_CHECK(Close(file));file=0;
    ADD_CHECK(!worker_test(44,"aros_added/files",OZ_OK,0));
    ADD_CHECK(!worker_test(8,"aros_added",OZ_OK,0));
    ADD_CHECK(!add_request(7,0,8,"readonly=off",0));
    ADD_CHECK(!worker_test(6,"aros_added",OZ_OK,0));
    for(unsigned i=0;i<8;i++){char path[64];snprintf(path,sizeof(path),"RAM:OZAddTest/disk%u",i);ADD_CHECK(DeleteFile((CONST_STRPTR)path));}
    ADD_CHECK(DeleteFile((CONST_STRPTR)"RAM:OZAddTest"));
    for(const char *p="Native pool add capacity/topology/preflight/reimport/DOS PASS\n";*p;p++)RawPutChar(*p);
    for(const char *p="Native pool add log/cache/spare and disabled allocation-class refusal PASS\n";*p;p++)RawPutChar(*p);
    for(const char *p="Native pool remove cache/spare/log, refusals and DOS payload PASS\n";*p;p++)RawPutChar(*p);
    const char *notice=removal_cancel_test?"Native pool remove cancellation, source retained and reimport DOS payload PASS\n":
        "Native pool remove populated mirror, finished evacuation and reimport DOS payload PASS\n";
    for(const char *p=notice;*p;p++)RawPutChar(*p);
done:
    if(error){char line[80];snprintf(line,sizeof(line),"Pool add test failed line=%d\n",error);for(const char *p=line;*p;p++)RawPutChar(*p);}
    if(file)Close(file);if(dir)UnLock(dir);free(buf);free(out);return error;
#undef ADD_CHECK
}
