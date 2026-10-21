/* Disposable RAM files, separate native admission client. No existing disks. */
static unsigned file_pool_layout=OZ_MIRROR,file_pool_disks=2;
static int import_options_test(void)
{
    static const struct {const char *text;int valid,ro,recover,rewind;} cases[]={
        {"readonly=on",1,1,0},{"readonly=off",1,0,0},
        {"readonly=on;destroyed=off",1,1,0},
        {"readonly=on;destroyed=on",1,1,1},
        {"destroyed=on;readonly=off",1,0,1},
        {"destroyed=off;readonly=off",1,0,0},
        {"readonly=on;rewind=on",1,1,0,1},
        {"rewind=on;destroyed=on;readonly=off",1,0,1,1},
        {"readonly=on;rewind=off",1,1,0,0},
        {"readonly=on;rewind=on;rewind=off",0,0,0,0},
        {"readonly=on;rewind=yes",0,0,0,0},
        {"",0,0,0},{NULL,0,0,0},{"destroyed=on",0,0,0},
        {"readonly=on;",0,0,0},{";readonly=on",0,0,0},
        {"readonly=on;;destroyed=on",0,0,0},
        {"readonly=on;readonly=off",0,0,0},
        {"readonly=on;destroyed=on;destroyed=off",0,0,0},
        {"readonly=on;force=on",0,0,0},{"readonly=on;destroyed=yes",0,0,0},
        {"readonly=on; destroyed=on",0,0,0},
        {"readonly=on;destroyed=onx",0,0,0}
    };
    for(unsigned i=0;i<sizeof(cases)/sizeof(cases[0]);i++) {
        int ro=-1,recover=-1,rewind=-1;
        int valid=oz_parse_import_options(cases[i].text,&ro,&recover,&rewind);
        if(valid!=cases[i].valid||ro!=(valid?cases[i].ro:-1)||
            recover!=(valid?cases[i].recover:-1)||rewind!=(valid?cases[i].rewind:-1))return EINVAL;
    }
    return 0;
}
static int file_pool_paths(const char *name,const char *first,const char *second,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=5;m->flags=OZ_CONFIRMED;snprintf(m->target,sizeof(m->target),"%s",name);
    m->topology.group_count=1;m->topology.device_count=file_pool_disks;m->topology.ashift=12;
    m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=file_pool_layout;
    snprintf(m->topology.devices[0].path,sizeof(m->topology.devices[0].path),"%s",first);
    for(unsigned i=1;i<file_pool_disks;i++)
        snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),"RAM:OZCreateTest/disk%u",i);
    if(second)snprintf(m->topology.devices[1].path,sizeof(m->topology.devices[1].path),"%s",second);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)error=poll_result(&ticket,out);
    if(!error) {
        const char *p=strstr(out->detail,"errno=");
        char wanted[32];snprintf(wanted,sizeof(wanted),"errno=%d",expected);
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!p||strcmp(p,wanted))error=EIO;
        for(p=out->detail;*p;p++)RawPutChar(*p);
        RawPutChar('\n');
    }
    if(ticket.number){int release=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=release;}
    free(m);free(out);return error;
}
static int file_pool_request(const char *name,int expected)
{return file_pool_paths(name,"/RAM/OZCreateTest/disk0",NULL,expected);}
static int file_pool_import(const char *name,const char *mode,int expected,int alias)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=7;m->flags=OZ_CONFIRMED;snprintf(m->target,sizeof(m->target),"%s",name);
    snprintf(m->options,sizeof(m->options),"%s",mode);
    m->topology.device_count=file_pool_disks;
    for(unsigned i=0;i<file_pool_disks;i++)
        snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),"RAM:OZCreateTest/disk%u",i);
    if(alias)snprintf(m->topology.devices[1].path,sizeof(m->topology.devices[1].path),"/RAM/OZCreateTest/disk0");
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)error=poll_result(&ticket,out);
    if(!error) {
        char wanted[32];snprintf(wanted,sizeof(wanted),"errno=%d",expected);
        const char *result=strstr(out->detail,"errno=");
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!result||strcmp(result,wanted))error=EIO;
        for(const char *s=out->detail;*s;s++)RawPutChar(*s);
        RawPutChar('\n');
    }
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    free(m);free(out);return error;
}
static int sync_request(const char *pool,ULONG flags,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=26;m->flags=flags;snprintf(m->target,sizeof(m->target),"%s",pool);
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)error=poll_result(&ticket,out);
    if(!error) {
        char wanted[128];snprintf(wanted,sizeof(wanted),"Authenticated pool sync returned errno=%d",expected);
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||strcmp(out->detail,wanted))error=EIO;
        for(const char *p=out->detail;*p;p++)RawPutChar(*p);
        RawPutChar('\n');
    }
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    free(m);free(out);return error;
}
static int lifecycle_admission_refusals(unsigned operation)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    int error=0;
    if(legacy_args(operation,"aros_created",operation==22?"create":NULL,NULL,out)!=EIO||out->status!=OZ_UNSUPPORTED)error=EIO;
    for(unsigned test=0;test<(operation==22||operation==23?5:6)&&!error;test++) {
        memset(m,0,sizeof(*m));m->magic=OZ_MAGIC;m->version=OZ_VERSION;
        m->msg.mn_Length=sizeof(*m);m->operation=operation;m->flags=OZ_CONFIRMED;
        snprintf(m->target,sizeof(m->target),"aros_created");
        if(operation==22)strcpy(m->argument,"create");
        if(test==0)m->flags=0;
        if(test==1)m->flags|=operation==26?OZ_RAW:OZ_FORCE;
        if(test==2)m->flags|=OZ_DRYRUN;
        if(test==3)m->flags|=OZ_RECURSIVE;
        if(test==4)strcpy(m->options,"force=on");
        if(test==5)strcpy(m->argument,"unexpected");
        struct oz_ticket ticket={0};
        int rc=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
        if(rc!=(test?ENOTSUP:EINVAL)||ticket.number)error=EIO;
    }
    free(m);free(out);return error;
}
static int checkpoint_state(const char *pool,const char *state)
{
    struct oz_message *out=calloc(1,sizeof(*out));if(!out)return 0;
    int rc=legacy(4,pool,out);char text[64];snprintf(text,sizeof(text),"checkpoint=%s",state);
    int ok=!rc&&strstr(out->detail,text)!=NULL;free(out);return ok;
}
static int file_pool_detach_test(void)
{
    struct oz_message *out=calloc(1,sizeof(*out));int error=0;
    char leaves[OZ_MAX_DEVICES][32]={{0}};unsigned count=0;
#define DETACH_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    DETACH_CHECK(out&&!legacy(4,"aros_created",out));
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];
        if(strcmp(r->kind,"file"))continue;
        const char *p=strstr(r->value,"guid=");DETACH_CHECK(p&&count<OZ_MAX_DEVICES);
        char *end;unsigned long long guid=strtoull(p+5,&end,10);
        DETACH_CHECK(guid&&*end==' ');snprintf(leaves[count++],32,"%llu",guid);
    }
    DETACH_CHECK(count==file_pool_disks);
    {
        struct oz_ticket rejected={0};
        memset(out,0,sizeof(*out));out->magic=OZ_MAGIC;out->version=OZ_VERSION;
        out->msg.mn_Length=sizeof(*out);out->operation=12;
        strcpy(out->target,"aros_created");strcpy(out->argument,leaves[0]);
        const ULONG flags[]={OZ_CONFIRMED|OZ_FORCE,OZ_CONFIRMED|OZ_DRYRUN,OZ_CONFIRMED|OZ_RECURSIVE};
        for(unsigned i=0;i<sizeof(flags)/sizeof(flags[0]);i++) {
            out->flags=flags[i];
            DETACH_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==ENOTSUP);
        }
        out->flags=OZ_CONFIRMED;strcpy(out->options,"force=on");
        DETACH_CHECK(oz_admission_submit(OZ_ADMISSION_TEST_PORT,out,&rejected)==ENOTSUP);
    }
    DETACH_CHECK(!worker_argument_test(12,"aros_created","0",OZ_ENGINE_ERROR,EINVAL));
    DETACH_CHECK(!worker_argument_test(12,"aros_created","18446744073709551616",OZ_ENGINE_ERROR,EINVAL));
    DETACH_CHECK(!worker_argument_test(12,"aros_created","1",OZ_ENGINE_ERROR,ENODEV));
    DETACH_CHECK(legacy_args(12,"aros_created",leaves[0],NULL,out)==EIO&&out->status==OZ_UNSUPPORTED);
    if(file_pool_layout!=OZ_MIRROR) {
        DETACH_CHECK(!worker_argument_test(12,"aros_created",leaves[0],OZ_ENGINE_ERROR,ENOTSUP));
    } else {
        DETACH_CHECK(!worker_argument_test(16,"aros_created",leaves[1],OZ_OK,0));
        DETACH_CHECK(!worker_argument_test(12,"aros_created",leaves[0],OZ_ENGINE_ERROR,EBUSY));
        DETACH_CHECK(!worker_argument_test(15,"aros_created",leaves[1],OZ_OK,0));
        /* No writes occurred while offline, so both leaves retain all data. */
        DETACH_CHECK(!worker_argument_test(12,"aros_created",leaves[0],OZ_OK,0));
        DETACH_CHECK(!worker_argument_test(12,"aros_created",leaves[0],OZ_ENGINE_ERROR,ENODEV));
        DETACH_CHECK(!legacy(4,"aros_created",out));
        unsigned remaining=0;
        for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"file")) {
            const char *p=strstr(out->rows[i].value,"guid=");DETACH_CHECK(p);
            snprintf(leaves[1],32,"%llu",strtoull(p+5,NULL,10));remaining++;
        }
        DETACH_CHECK(remaining==1);
        /* Collapsing the mirror can transfer the parent's GUID to the leaf. */
        DETACH_CHECK(!worker_argument_test(12,"aros_created",leaves[1],OZ_ENGINE_ERROR,ENOTSUP));
    }
    for(const char *p="Native detach topology/refusal PASS\n";*p;p++)RawPutChar(*p);
done:
    free(out);return error;
#undef DETACH_CHECK
}
static int attach_request(unsigned op,const char *guid,const char *path,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;strcpy(m->target,"aros_created");
    snprintf(m->argument,sizeof(m->argument),"%s",guid);
    m->topology.group_count=1;m->topology.device_count=1;
    m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
    snprintf(m->topology.devices[0].path,sizeof(m->topology.devices[0].path),"%s",path);
    if(!expected) {
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
static int file_pool_attach_test(void)
{
    if(file_pool_layout!=OZ_MIRROR)return 0;
    struct oz_message *out=calloc(1,sizeof(*out));unsigned char *zeros=calloc(1,1024*1024);
    BPTR file=0;int error=0;char guid[32]={0},path[96];
#define ATTACH_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    ATTACH_CHECK(out&&zeros&&!legacy(4,"aros_created",out));
    for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"file")) {
        const char *p=strstr(out->rows[i].value,"guid=");ATTACH_CHECK(p);
        snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));
    }
    ATTACH_CHECK(guid[0]);
    for(unsigned step=0;step<2;step++) {
        unsigned op=step?13:11;
        snprintf(path,sizeof(path),"RAM:OZCreateTest/attach%u",step);
        file=Open((CONST_STRPTR)path,MODE_NEWFILE);ATTACH_CHECK(file);
        for(unsigned n=0;n<64;n++)ATTACH_CHECK(Write(file,zeros,1024*1024)==1024*1024);
        ATTACH_CHECK(Close(file));file=0;
        ATTACH_CHECK(!attach_request(op,"0",path,EINVAL));
        ATTACH_CHECK(!attach_request(op,guid,"RAM:OZCreateTest/disk1",EEXIST));
        ATTACH_CHECK(!attach_request(op,guid,path,0));
        int finished=0;
        for(unsigned n=0;n<120;n++) {
            ATTACH_CHECK(!legacy(4,"aros_created",out));
            if(strstr(out->detail,"scan=resilver state=finished")&&strstr(out->detail," errors=0 ")) {
                unsigned leaves=0;int found=0;
                char suffix[32];snprintf(suffix,sizeof(suffix),"/attach%u",step);
                for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"file")) {
                    leaves++;ATTACH_CHECK(!strcmp(out->rows[i].state,"ONLINE"));
                    if(strstr(out->rows[i].name,suffix)) {
                        const char *p=strstr(out->rows[i].value,"guid=");ATTACH_CHECK(p);
                        snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));found=1;
                    }
                }
                if(found&&leaves==2){finished=1;break;}
            }
            Delay(50);
        }
        ATTACH_CHECK(finished);
        ATTACH_CHECK(!worker_options_test(43,"aros_created/files","OZATTACH","readonly=off",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZATTACH:proof.txt",MODE_OLDFILE);ATTACH_CHECK(file);
        char bytes[24];ATTACH_CHECK(Read(file,bytes,sizeof(bytes))==24&&!memcmp(bytes,"Typed upstream creation\n",24));
        ATTACH_CHECK(Close(file));file=0;
        ATTACH_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
        for(const char *p=step?"Native replace resilver and DOS read PASS\n":"Native attach resilver and DOS read PASS\n";*p;p++)RawPutChar(*p);
    }
    ATTACH_CHECK(!worker_argument_test(12,"aros_created",guid,OZ_OK,0));
    ATTACH_CHECK(DeleteFile((CONST_STRPTR)"RAM:OZCreateTest/attach0"));
    ATTACH_CHECK(DeleteFile((CONST_STRPTR)"RAM:OZCreateTest/attach1"));
done:
    if(file)Close(file);free(zeros);free(out);return error;
#undef ATTACH_CHECK
}
static int file_pool_create_test(void)
{
    int error=0;BPTR file=0,dir=0;
    char changed_guid[64]={0};
    unsigned char *buf=calloc(1,1024*1024);if(!buf)return ENOMEM;
#define CREATE_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    dir=CreateDir((CONST_STRPTR)"RAM:OZCreateTest");CREATE_CHECK(dir);UnLock(dir);dir=0;
    for(unsigned i=0;i<file_pool_disks;i++) {
        char name[64];snprintf(name,sizeof(name),"RAM:OZCreateTest/disk%u",i);
        file=Open((CONST_STRPTR)name,MODE_NEWFILE);CREATE_CHECK(file);
        for(unsigned j=0;j<64;j++)CREATE_CHECK(Write(file,buf,1024*1024)==1024*1024);
        CREATE_CHECK(Close(file));file=0;
    }
    CREATE_CHECK(!file_pool_paths("aros_alias","/RAM/OZCreateTest/disk0","RAM:OZCreateTest/disk0",EEXIST));
    file=Open((CONST_STRPTR)"RAM:OZCreateTest/disk1",MODE_READWRITE);CREATE_CHECK(file);
    buf[0]=0x41;CREATE_CHECK(Write(file,buf,1)==1);CREATE_CHECK(Close(file));file=0;
    CREATE_CHECK(!file_pool_request("aros_created",EEXIST));
    /* Refusing the second nonblank vdev must not label the first one. */
    file=Open((CONST_STRPTR)"RAM:OZCreateTest/disk0",MODE_OLDFILE);CREATE_CHECK(file);
    for(unsigned i=0;i<64;i++) {
        CREATE_CHECK(Read(file,buf,1024*1024)==1024*1024);
        for(unsigned j=0;j<1024*1024;j++)CREATE_CHECK(buf[j]==0);
    }
    CREATE_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"RAM:OZCreateTest/disk1",MODE_READWRITE);CREATE_CHECK(file);
    CREATE_CHECK(Read(file,buf,1)==1&&buf[0]==0x41);
    CREATE_CHECK(Seek(file,0,OFFSET_BEGINNING)>=0);buf[0]=0;
    CREATE_CHECK(Write(file,buf,1)==1);CREATE_CHECK(Close(file));file=0;
    CREATE_CHECK(!file_pool_request("aros_created",0));
    CREATE_CHECK(!lifecycle_admission_refusals(23));
    {
        struct oz_message *out=calloc(1,sizeof(*out));CREATE_CHECK(out);
        CREATE_CHECK(!legacy_args(31,"aros_created","guid",NULL,out)&&out->row_count==1);
        char old[64];snprintf(old,sizeof(old),"%.63s",out->rows[0].value);
        CREATE_CHECK(!worker_argument_test(23,"aros_created",old,OZ_ENGINE_ERROR,EEXIST));
        CREATE_CHECK(!worker_argument_test(23,"aros_created","0",OZ_ENGINE_ERROR,EINVAL));
        CREATE_CHECK(!worker_argument_test(23,"aros_created","18446744073709551616",OZ_ENGINE_ERROR,EINVAL));
        CREATE_CHECK(!worker_argument_test(23,"aros_created","-1",OZ_ENGINE_ERROR,EINVAL));
        CREATE_CHECK(!worker_argument_test(23,"aros_created","1x",OZ_ENGINE_ERROR,EINVAL));
        CREATE_CHECK(!worker_test(23,"aros_created/files",OZ_ENGINE_ERROR,EINVAL));
        CREATE_CHECK(!worker_test(23,"aros_absent",OZ_ENGINE_ERROR,ENOENT));
        CREATE_CHECK(!worker_test(23,"aros_created",OZ_OK,0));
        CREATE_CHECK(!legacy_args(31,"aros_created","guid",NULL,out)&&strcmp(old,out->rows[0].value));
        /* Restore a formerly used GUID, then generate another. */
        CREATE_CHECK(!worker_argument_test(23,"aros_created",old,OZ_OK,0));
        CREATE_CHECK(!legacy_args(31,"aros_created","guid",NULL,out)&&!strcmp(old,out->rows[0].value));
        CREATE_CHECK(!worker_test(23,"aros_created",OZ_OK,0));
        CREATE_CHECK(!legacy_args(31,"aros_created","guid",NULL,out)&&strcmp(old,out->rows[0].value));
        snprintf(changed_guid,sizeof(changed_guid),"%.63s",out->rows[0].value);
        free(out);
    }
    CREATE_CHECK(!lifecycle_admission_refusals(6));
    CREATE_CHECK(!lifecycle_admission_refusals(22));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","invalid",OZ_ENGINE_ERROR,EINVAL));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","create",OZ_ENGINE_ERROR,ENOTSUP));
    CREATE_CHECK(!worker_argument_test(32,"aros_created","feature@zpool_checkpoint=disabled",OZ_ENGINE_ERROR,EINVAL));
    CREATE_CHECK(!worker_argument_test(32,"aros_created","feature@not_a_feature=enabled",OZ_ENGINE_ERROR,ENOTSUP));
    CREATE_CHECK(!worker_argument_test(32,"aros_created","feature@zpool_checkpoint=enabled",OZ_OK,0));
    CREATE_CHECK(checkpoint_state("aros_created","none"));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","discard",OZ_ENGINE_ERROR,1026)); /* ZFS_ERR_NO_CHECKPOINT */
    CREATE_CHECK(!worker_argument_test(22,"aros_created","create",OZ_OK,0));
    CREATE_CHECK(checkpoint_state("aros_created","exists"));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","create",OZ_ENGINE_ERROR,1024)); /* ZFS_ERR_CHECKPOINT_EXISTS */
    CREATE_CHECK(!worker_test(23,"aros_created",OZ_ENGINE_ERROR,1024));
    CREATE_CHECK(!lifecycle_admission_refusals(26));
    CREATE_CHECK(!lifecycle_admission_refusals(24));
    CREATE_CHECK(!worker_test(24,"aros_created/files",OZ_ENGINE_ERROR,EINVAL));
    CREATE_CHECK(!worker_test(24,"aros_not_imported",OZ_ENGINE_ERROR,ENOENT));
    CREATE_CHECK(!worker_test(24,"aros_created",OZ_OK,0));
    CREATE_CHECK(!worker_options_test(24,"aros_created","","scrub_restart=off",OZ_OK,0));
    CREATE_CHECK(!worker_options_test(24,"aros_created","","scrub_restart=on",OZ_OK,0));
    CREATE_CHECK(!sync_request("aros_created/files",OZ_CONFIRMED,EINVAL));
    CREATE_CHECK(!sync_request("aros_not_imported",OZ_CONFIRMED,ENOENT));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED,0));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED|OZ_FORCE,0));
    CREATE_CHECK(!worker_test(6,"aros_created/files",OZ_ENGINE_ERROR,EINVAL));
    {
        struct oz_message *status=calloc(1,sizeof(*status));CREATE_CHECK(status);
        int rc=legacy(4,"aros_created",status);
        unsigned files=0,mirrors=0,raids=0;int parity_ok=0;
        char parity[32];snprintf(parity,sizeof(parity),"parity=%u",file_pool_layout>=OZ_RAIDZ1?file_pool_layout-OZ_RAIDZ1+1:0);
        for(unsigned i=0;!rc&&i<status->row_count;i++) {
            struct oz_row *r=&status->rows[i];
            if(strcmp(r->state,"ONLINE"))rc=EIO;
            if(!strcmp(r->kind,"file"))files++;
            if(!strcmp(r->kind,"mirror"))mirrors++;
            if(!strcmp(r->kind,"raidz")){raids++;parity_ok=strstr(r->value,parity)!=NULL;}
        }
        int ok=!rc&&files==file_pool_disks&&
            mirrors==(file_pool_layout==OZ_MIRROR)&&raids==(file_pool_layout>=OZ_RAIDZ1)&&
            (file_pool_layout<OZ_RAIDZ1||parity_ok);
        free(status);CREATE_CHECK(ok);
    }
    CREATE_CHECK(!worker_test(37,"aros_created/files",OZ_OK,0));
    CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZCREATED","readonly=off",OZ_OK,0));
    CREATE_CHECK(!file_pool_paths("aros_recursive","/OZCREATED/proof.txt",NULL,EDEADLK));
    /* Native /AROS device paths are intentionally admitted since 937de4a1
     * and belong to the dedicated destructive-device fixtures.  This
     * portable file-vdev suite must not probe whichever disk booted it. */
    CREATE_CHECK(!file_pool_paths("aros_missing","/SYS/not-a-vdev",NULL,ENOENT));
    file=Open((CONST_STRPTR)"OZCREATED:proof.txt",MODE_NEWFILE);CREATE_CHECK(file);
    CREATE_CHECK(Write(file,"Typed upstream creation\n",24)==24);
    CREATE_CHECK(!worker_test(24,"aros_created",OZ_OK,0));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED,0));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED|OZ_FORCE,0));
    CREATE_CHECK(Flush(file));CREATE_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZCREATED:proof.txt",MODE_OLDFILE);CREATE_CHECK(file);
    CREATE_CHECK(Read(file,buf,24)==24&&!memcmp(buf,"Typed upstream creation\n",24));
    CREATE_CHECK(Close(file));file=0;
    {
        struct oz_message *status=calloc(1,sizeof(*status));CREATE_CHECK(status);
        int rc=legacy(4,"aros_created",status);
        for(const char *p=status->detail;*p;p++)RawPutChar(*p);
        RawPutChar('\n');
        for(unsigned i=0;i<status->row_count;i++) {
            char line[512];snprintf(line,sizeof(line),"Created pool status kind=%s state=%s %s\n",
                status->rows[i].kind,status->rows[i].state,status->rows[i].value);
            for(char *p=line;*p;p++)RawPutChar(*p);
            if(strcmp(status->rows[i].state,"ONLINE")||!strstr(status->rows[i].value,
                "read-errors=0 write-errors=0 checksum-errors=0"))rc=EIO;
        }
        free(status);CREATE_CHECK(!rc);
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_ENGINE_ERROR,EBUSY));
    CREATE_CHECK(!worker_test(6,"aros_created",OZ_ENGINE_ERROR,EBUSY));
    CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",EEXIST,1));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on;destroyed=on",ENOENT,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",0,0));
    CREATE_CHECK(!attach_request(11,"1","RAM:OZCreateTest/disk0",EROFS));
    CREATE_CHECK(!attach_request(13,"1","RAM:OZCreateTest/disk0",EROFS));
    CREATE_CHECK(!worker_argument_test(12,"aros_created","1",OZ_ENGINE_ERROR,EROFS));
    CREATE_CHECK(checkpoint_state("aros_created","exists"));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","discard",OZ_ENGINE_ERROR,EROFS));
    CREATE_CHECK(!worker_argument_test(32,"aros_created","feature@zpool_checkpoint=enabled",OZ_ENGINE_ERROR,EROFS));
    {
        struct oz_message *out=calloc(1,sizeof(*out));CREATE_CHECK(out);
        CREATE_CHECK(!legacy_args(31,"aros_created","guid",NULL,out)&&!strcmp(changed_guid,out->rows[0].value));
        free(out);
        CREATE_CHECK(!worker_test(23,"aros_created",OZ_ENGINE_ERROR,EROFS));
        for(const char *p="Native reguid generated/explicit/reimport/read-only PASS\n";*p;p++)RawPutChar(*p);
    }
    CREATE_CHECK(!worker_test(24,"aros_created",OZ_OK,0));
    CREATE_CHECK(!worker_options_test(24,"aros_created","","scrub_restart=off",OZ_OK,0));
    CREATE_CHECK(!worker_test(6,"aros_created",OZ_ENGINE_ERROR,EROFS));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED,EROFS));
    CREATE_CHECK(!sync_request("aros_created",OZ_CONFIRMED|OZ_FORCE,EROFS));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",EEXIST,0));
    if(file_pool_layout==OZ_MIRROR) {
        CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZIMPORTED","readonly=on",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZIMPORTED:proof.txt",MODE_OLDFILE);CREATE_CHECK(file);
        CREATE_CHECK(Read(file,buf,24)==24&&!memcmp(buf,"Typed upstream creation\n",24));
        CREATE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZIMPORTED:forbidden",MODE_NEWFILE);
        CREATE_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        CREATE_CHECK(!worker_test(8,"aros_created",OZ_ENGINE_ERROR,EBUSY));
        CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    }
    /* More than an Exec task's signal count, not just the old eight slots.
     * Retain each real DeviceProc across detach; new registrations must not
     * turn old ports into handles to the newly mounted filesystem. */
    if(file_pool_layout==OZ_MIRROR) {
        struct DevProc *oldest=NULL;
        for(unsigned cycle=0;cycle<40;cycle++) {
            CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZMOUNTLOOP","readonly=on",OZ_OK,0));
            struct DevProc *dev=GetDeviceProc((CONST_STRPTR)"OZMOUNTLOOP:",NULL);
            CREATE_CHECK(dev);
            int active=DoPkt(dev->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)==DOSTRUE;
            if(oldest)CREATE_CHECK(oldest->dvp_Port!=dev->dvp_Port&&
                !DoPkt(oldest->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED);
            int rc=worker_test(44,"aros_created/files",OZ_OK,0);
            int retired=!DoPkt(dev->dvp_Port,ACTION_IS_FILESYSTEM,0,0,0,0,0)&&IoErr()==ERROR_DEVICE_NOT_MOUNTED;
            if(oldest)FreeDeviceProc(dev);else oldest=dev;
            CREATE_CHECK(active&&!rc&&retired);
        }
        FreeDeviceProc(oldest);
        for(const char *p="Native DOS forty mount retirements and late packets PASS\n";*p;p++)RawPutChar(*p);
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=off",0,0));
    CREATE_CHECK(!worker_argument_test(22,"aros_created","discard",OZ_OK,0));
    {
        unsigned tries=0;
        while(!checkpoint_state("aros_created","none")&&tries++<600)Delay(5);
        CREATE_CHECK(tries<600);
        for(const char *p="Native checkpoint create/duplicate/reguid-conflict/reimport/discard-complete PASS\n";*p;p++)RawPutChar(*p);
    }
    if(file_pool_layout==OZ_MIRROR) {
        CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZIMPORTED","readonly=off",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZIMPORTED:imported.txt",MODE_NEWFILE);CREATE_CHECK(file);
        CREATE_CHECK(Write(file,"Imported write\n",15)==15&&Flush(file));
        CREATE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZIMPORTED:imported.txt",MODE_OLDFILE);CREATE_CHECK(file);
        CREATE_CHECK(Read(file,buf,15)==15&&!memcmp(buf,"Imported write\n",15));
        CREATE_CHECK(Close(file));file=0;
        CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    /* A second create on exported labels must refuse rather than overwrite. */
    CREATE_CHECK(!file_pool_request("aros_replacement",EEXIST));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=off",0,0));
    CREATE_CHECK(!file_pool_detach_test());
    CREATE_CHECK(!file_pool_attach_test());
    CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZDETACH","readonly=off",OZ_OK,0));
    file=Open((CONST_STRPTR)"OZDETACH:proof.txt",MODE_OLDFILE);CREATE_CHECK(file);
    CREATE_CHECK(Read(file,buf,24)==24&&!memcmp(buf,"Typed upstream creation\n",24));
    CREATE_CHECK(Close(file));file=0;
    CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    CREATE_CHECK(!worker_test(6,"aros_created",OZ_OK,0));
    CREATE_CHECK(!worker_test(6,"aros_created",OZ_ENGINE_ERROR,ENOENT));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",ENOENT,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=off",ENOENT,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on;destroyed=on",0,0));
    if(file_pool_layout==OZ_MIRROR) {
        CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZRECOVERRO","readonly=on",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZRECOVERRO:proof.txt",MODE_OLDFILE);CREATE_CHECK(file);
        CREATE_CHECK(Read(file,buf,24)==24&&!memcmp(buf,"Typed upstream creation\n",24));
        CREATE_CHECK(Close(file));file=0;
        file=Open((CONST_STRPTR)"OZRECOVERRO:forbidden",MODE_NEWFILE);
        CREATE_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",ENOENT,0));
    CREATE_CHECK(!file_pool_import("aros_created","destroyed=on;readonly=off",0,0));
    if(file_pool_layout==OZ_MIRROR) {
        CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZRECOVERRW","readonly=off",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZRECOVERRW:recovered.txt",MODE_NEWFILE);CREATE_CHECK(file);
        CREATE_CHECK(Write(file,"Recovered write\n",16)==16&&Flush(file));
        CREATE_CHECK(Close(file));file=0;
        CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    CREATE_CHECK(!file_pool_import("aros_created","readonly=on",0,0));
    if(file_pool_layout==OZ_MIRROR) {
        CREATE_CHECK(!worker_options_test(43,"aros_created/files","OZRECOVERED","readonly=on",OZ_OK,0));
        file=Open((CONST_STRPTR)"OZRECOVERED:recovered.txt",MODE_OLDFILE);CREATE_CHECK(file);
        CREATE_CHECK(Read(file,buf,16)==16&&!memcmp(buf,"Recovered write\n",16));
        CREATE_CHECK(Close(file));file=0;
        CREATE_CHECK(!worker_test(44,"aros_created/files",OZ_OK,0));
    }
    CREATE_CHECK(!worker_test(8,"aros_created",OZ_OK,0));
    /* Destroy is not a blank/secure erase: existing contents still prevent
     * our blank-only create operation from silently reusing these files. */
    CREATE_CHECK(!file_pool_request("aros_replacement",EEXIST));
    for(unsigned i=0;i<file_pool_disks;i++) {
        char name[64];snprintf(name,sizeof(name),"RAM:OZCreateTest/disk%u",i);
        CREATE_CHECK(DeleteFile((CONST_STRPTR)name));
    }
    CREATE_CHECK(DeleteFile((CONST_STRPTR)"RAM:OZCreateTest"));
done:
    if(file)Close(file);
    if(dir)UnLock(dir);
    free(buf);
    char line[128];snprintf(line,sizeof(line),"Native authenticated file pool layout=%u disks=%u result=%d\n",file_pool_layout,file_pool_disks,error);
    for(char *p=line;*p;p++)RawPutChar(*p);
    return error;
#undef CREATE_CHECK
}
