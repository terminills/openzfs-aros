/* Disposable two-native-member split and independent cold readback. */
static int native_split_request(unsigned op,const char *pool,const char *guid,unsigned unit)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;strcpy(m->target,pool);
    if(op==14){strcpy(m->argument,"qemu_split");snprintf(m->options,sizeof(m->options),"guids=%s",guid);}
    if(op==7){strcpy(m->options,"readonly=on");m->topology.device_count=1;
        snprintf(m->topology.devices[0].path,256,"/AROS/ata.device/%u/partition/0",unit);}
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error)do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error){note(out->detail);if(out->status!=OZ_OK)error=EIO;}
    if(ticket.number){int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;}
    free(m);free(out);return error;
}
static int native_split_test(void)
{
    int error=0;BPTR file=0;
#define SPLIT_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
#ifndef OZ_NATIVE_MANAGER_SPLIT_COLD
    SPLIT_CHECK(!request(7,"readonly=off",0));
    SPLIT_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","OZSPLIT",OZ_MOUNT_ATTACH,0));
    file=Open((CONST_STRPTR)"OZSPLIT:split-marker",MODE_NEWFILE);SPLIT_CHECK(file);
    const char marker[]="Native manager split marker\n";
    SPLIT_CHECK(Write(file,(APTR)marker,sizeof(marker)-1)==sizeof(marker)-1&&Flush(file));
    SPLIT_CHECK(Close(file));file=0;
    SPLIT_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"qemu_mirror/files","",OZ_MOUNT_DETACH,0));
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    SPLIT_CHECK(m);if(!oz_client_open(&client)){free(m);SPLIT_CHECK(0);}
    m->operation=4;strcpy(m->target,"qemu_mirror");
    int submitted=oz_submit(&client,m)==OZ_OK;
    if(submitted)while(!(out=oz_poll(&client)))Delay(1);
    char guid[32]={0};unsigned found=0;
    if(out&&out->status==OZ_OK)for(unsigned i=0;i<out->row_count;i++){
        if(strcmp(out->rows[i].name,"/AROS/ata.device/1/partition/0"))continue;
        const char *p=strstr(out->rows[i].value,"guid=");
        if(p){char *end;errno=0;unsigned long long n=strtoull(p+5,&end,10);
            if(!errno&&n&&end!=p+5&&(!*end||*end==' ')){snprintf(guid,sizeof(guid),"%llu",n);found++;}}
    }
    free(m);if(out)oz_message_free(out);oz_client_close(&client);
    SPLIT_CHECK(found==1);
    SPLIT_CHECK(!native_split_request(14,"qemu_mirror",guid,0));
    SPLIT_CHECK(!native_split_request(8,"qemu_mirror",NULL,0));
#endif
    for(unsigned cycle=0;cycle<2;cycle++)for(unsigned unit=0;unit<2;unit++){
        const char *pool=unit?"qemu_split":"qemu_mirror";char dataset[64];
        snprintf(dataset,sizeof(dataset),"%s/files",pool);
        SPLIT_CHECK(!native_split_request(7,pool,NULL,unit));
        SPLIT_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,dataset,"OZSPLIT",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        const char *names[]={"OZSPLIT:hello.txt","OZSPLIT:split-marker"};
        const char *expected[]={"OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n","Native manager split marker\n"};
        for(unsigned i=0;i<2;i++){
            char data[128];file=Open((CONST_STRPTR)names[i],MODE_OLDFILE);SPLIT_CHECK(file);
            SPLIT_CHECK(Read(file,data,sizeof(data))==(LONG)strlen(expected[i])&&!memcmp(data,expected[i],strlen(expected[i])));
            SPLIT_CHECK(Close(file));file=0;
        }
        file=Open((CONST_STRPTR)"OZSPLIT:no-write",MODE_NEWFILE);SPLIT_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        SPLIT_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,dataset,"",OZ_MOUNT_DETACH,0));
        SPLIT_CHECK(!native_split_request(8,pool,NULL,unit));
    }
done:
    if(file)Close(file);
    char line[160];snprintf(line,sizeof(line),"RESULT native Manager all-disk split %s line=%d pools=2 cycles=2 exact-files=2 readonly=1",error?"FAIL":"PASS",error);note(line);
    return error;
#undef SPLIT_CHECK
}
