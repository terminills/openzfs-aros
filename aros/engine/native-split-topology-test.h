/* Actual upstream CLI split of disposable file pools with hole/indirect slots. */
static int split_topology_command(const char *command)
{
    note(command);
    BPTR file=Open((CONST_STRPTR)"T:split-command.log",MODE_NEWFILE);
    if(!file)return EIO;
    LONG result=SystemTags((CONST_STRPTR)command,SYS_Output,file,SYS_Error,file,TAG_DONE);
    if(!Close(file))return EIO;
    file=Open((CONST_STRPTR)"T:split-command.log",MODE_OLDFILE);
    if(!file)return EIO;
    char bytes[512];LONG n;
    while((n=Read(file,bytes,sizeof(bytes)-1))>0){bytes[n]=0;note(bytes);}
    int closed=Close(file);
    char line[64];snprintf(line,sizeof(line),"Split CLI result=%ld",(long)result);note(line);
    return result||n<0||!closed?EIO:0;
}
#ifdef OZ_NATIVE_SPLIT_TOPOLOGY_MANAGER
static int split_topology_manager(const char *first,const char *second)
{
    struct oz_client client;struct oz_message *m=calloc(1,sizeof(*m)),*out=NULL;
    if(!m)return ENOMEM;
    if(!oz_client_open(&client)){free(m);return EIO;}
    m->operation=4;strcpy(m->target,"topo_source");
    int error=oz_submit(&client,m)==OZ_OK?0:EIO;
    if(!error)while(!(out=oz_poll(&client)))Delay(1);
    unsigned found=0;unsigned long long guids[2]={0};
    if(out&&out->status==OZ_OK)for(unsigned i=0;i<out->row_count;i++){
        unsigned slot=!strcmp(out->rows[i].name,first)?0:
            second&&!strcmp(out->rows[i].name,second)?1:2;
        if(slot==2)continue;
        const char *p=strstr(out->rows[i].value,"guid=");
        if(!p||guids[slot]){error=EINVAL;break;}
        char *end;errno=0;guids[slot]=strtoull(p+5,&end,10);
        if(errno||!guids[slot]||end==p+5||(*end&&*end!=' ')){error=EINVAL;break;}
        found++;
    }
    else error=EIO;
    if(out)oz_message_free(out);
    oz_client_close(&client);
    if(found!=(second?2:1))error=EINVAL;
    if(error){free(m);return error;}
    memset(m,0,sizeof(*m));out=calloc(1,sizeof(*out));
    if(!out){free(m);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=14;m->flags=OZ_CONFIRMED;
    strcpy(m->target,"topo_source");strcpy(m->argument,"topo_split");
    if(second)snprintf(m->options,sizeof(m->options),"guids=%llu,%llu",guids[0],guids[1]);
    else snprintf(m->options,sizeof(m->options),"guids=%llu",guids[0]);
    struct oz_ticket ticket={0};error=oz_admission_submit(OZ_ADMISSION_PORT,m,&ticket);
    if(!error)do{error=oz_admission_query(OZ_ADMISSION_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error){note(out->detail);if(out->status!=OZ_OK)error=EIO;}
    if(ticket.number){int released=oz_admission_release(OZ_ADMISSION_PORT,&ticket);if(!error)error=released;}
    free(m);free(out);
    if(!error)note("RESULT split topology authenticated Manager API PASS");
    return error;
}
#endif
static int native_split_topology_test(unsigned mode)
{
    unsigned indirect=mode==2,hole=mode==1,trailing=mode==4,mirrorlog=mode==5;
    const char *allocation=mode==6?"special":mode==7?"dedup":NULL;
    unsigned haslog=hole||trailing||mirrorlog;
    int cold=0,persistent=0;
#ifdef OZ_NATIVE_SPLIT_PERSISTENT
    persistent=1;
#endif
#ifdef OZ_NATIVE_SPLIT_COLD
    cold=1;
#endif
    int error=0;BPTR file=0;char paths[6][256],command[2048],native[80];
    char *zero=calloc(1,1024*1024);
#define TOPO_CHECK(x) do{if(!(x)){error=__LINE__;goto done;}}while(0)
    TOPO_CHECK(zero);
    for(unsigned i=0;i<(mirrorlog?6:haslog?5:4);i++){
        snprintf(native,sizeof(native),"%s:split-topology-%u",persistent?"OZBACK":"RAM",i);
        if(!cold){
        file=Open((CONST_STRPTR)native,MODE_NEWFILE);TOPO_CHECK(file);
        /* Removal must leave room for its reservation on the surviving top. */
        for(unsigned n=0;n<(indirect?256:64);n++)TOPO_CHECK(Write(file,zero,1024*1024)==1024*1024);
        TOPO_CHECK(Flush(file)&&Close(file));file=0;
        }
        BPTR lock=Lock((CONST_STRPTR)native,ACCESS_READ);TOPO_CHECK(lock);
        char canonical[254];int named=NameFromLock(lock,(STRPTR)canonical,sizeof(canonical));UnLock(lock);
        TOPO_CHECK(named);char *colon=strchr(canonical,':');TOPO_CHECK(colon);*colon='/';
        snprintf(paths[i],sizeof(paths[i]),"/%s",canonical);
    }
    free(zero);zero=NULL;
    char payload[4097];
    for(unsigned i=0;i<sizeof(payload)-1;i++)payload[i]=(char)(33+(i*37+i/31)%90);
    payload[sizeof(payload)-1]=0;
    if(!cold){
    if(allocation)snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -O mountpoint=none topo_source mirror \"%s\" \"%s\" %s mirror \"%s\" \"%s\"",paths[0],paths[1],allocation,paths[2],paths[3]);
    else if(mirrorlog)snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -O mountpoint=none topo_source mirror \"%s\" \"%s\" log mirror \"%s\" \"%s\"",paths[0],paths[1],paths[4],paths[5]);
    else if(!hole)snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -O mountpoint=none topo_source mirror \"%s\" \"%s\" mirror \"%s\" \"%s\"",paths[0],paths[1],paths[2],paths[3]);
    else snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -O mountpoint=none topo_source mirror \"%s\" \"%s\" log \"%s\"",paths[0],paths[1],paths[4]);
    TOPO_CHECK(!split_topology_command(command));
    if(hole||mirrorlog){
        snprintf(command,sizeof(command),"C:zpool add topo_source mirror \"%s\" \"%s\"",paths[2],paths[3]);
        TOPO_CHECK(!split_topology_command(command));
    }
    if(trailing){
        snprintf(command,sizeof(command),"C:zpool add topo_source log \"%s\"",paths[4]);
        TOPO_CHECK(!split_topology_command(command));
    }
    TOPO_CHECK(!split_topology_command("C:zfs create -u topo_source/files"));
    if(mode==6)TOPO_CHECK(!split_topology_command("C:zfs set special_small_blocks=128K topo_source/files"));
    if(mode==7)TOPO_CHECK(!split_topology_command("C:zfs set compression=off dedup=on topo_source/files"));
    TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"topo_source/files","OZTOPO",OZ_MOUNT_ATTACH,0));
    file=Open((CONST_STRPTR)"OZTOPO:payload",MODE_NEWFILE);TOPO_CHECK(file);
    TOPO_CHECK(Write(file,(APTR)payload,sizeof(payload)-1)==sizeof(payload)-1&&Flush(file));
    TOPO_CHECK(Close(file));file=0;
    if(mode==7){
        file=Open((CONST_STRPTR)"OZTOPO:duplicate",MODE_NEWFILE);TOPO_CHECK(file);
        TOPO_CHECK(Write(file,(APTR)payload,sizeof(payload)-1)==sizeof(payload)-1&&Flush(file));
        TOPO_CHECK(Close(file));file=0;
    }
    TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"topo_source/files","",OZ_MOUNT_DETACH,0));
    TOPO_CHECK(!split_topology_command("C:zpool sync topo_source"));
    if(indirect){
        TOPO_CHECK(!split_topology_command("C:zpool remove topo_source mirror-0"));
        TOPO_CHECK(!split_topology_command("C:zpool wait -t remove topo_source"));
        snprintf(command,sizeof(command),"C:zpool split topo_source topo_split \"%s\"",paths[3]);
    }else snprintf(command,sizeof(command),"C:zpool split topo_source topo_split \"%s\" \"%s\"",paths[1],paths[3]);
#ifdef OZ_NATIVE_SPLIT_TOPOLOGY_MANAGER
    TOPO_CHECK(!split_topology_manager(indirect?paths[3]:paths[1],indirect?NULL:paths[3]));
#else
    TOPO_CHECK(!split_topology_command(command));
#endif
    TOPO_CHECK(!split_topology_command("C:zpool export topo_source"));
    }
    for(unsigned pool=0;pool<2;pool++){
        const char *name=pool?"topo_split":"topo_source";char dataset[64];
        snprintf(dataset,sizeof(dataset),"%s/files",name);
        if(indirect)snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -o readonly=on -d \"%s\" %s",paths[2+pool],name);
        else if(mirrorlog&&!pool)snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -o readonly=on -d \"%s\" -d \"%s\" -d \"%s\" -d \"%s\" %s",paths[0],paths[2],paths[4],paths[5],name);
        else if(haslog&&!pool)snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -o readonly=on -d \"%s\" -d \"%s\" -d \"%s\" %s",paths[0],paths[2],paths[4],name);
        else snprintf(command,sizeof(command),"C:zpool import -N -o cachefile=none -o readonly=on -d \"%s\" -d \"%s\" %s",paths[pool],paths[2+pool],name);
        TOPO_CHECK(!split_topology_command(command));
        TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,dataset,"OZTOPO",OZ_MOUNT_ATTACH,OZ_MOUNT_READONLY));
        char bytes[sizeof(payload)];file=Open((CONST_STRPTR)"OZTOPO:payload",MODE_OLDFILE);TOPO_CHECK(file);
        TOPO_CHECK(Read(file,bytes,sizeof(bytes))==sizeof(payload)-1&&!memcmp(bytes,payload,sizeof(payload)-1));
        TOPO_CHECK(Close(file));file=0;
        if(mode==7){
            file=Open((CONST_STRPTR)"OZTOPO:duplicate",MODE_OLDFILE);TOPO_CHECK(file);
            TOPO_CHECK(Read(file,bytes,sizeof(bytes))==sizeof(payload)-1&&!memcmp(bytes,payload,sizeof(payload)-1));
            TOPO_CHECK(Close(file));file=0;
        }
        file=Open((CONST_STRPTR)"OZTOPO:no-write",MODE_NEWFILE);TOPO_CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
        TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,dataset,"",OZ_MOUNT_DETACH,0));
        snprintf(command,sizeof(command),"C:zpool export %s",name);TOPO_CHECK(!split_topology_command(command));
    }
    if(persistent){
        TOPO_CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        note("RESULT split topology backing clean detach PASS");
    }
done:
    if(file)Close(file);free(zero);
    char line[160];snprintf(line,sizeof(line),"RESULT native CLI split topology %s kind=%s line=%d separate-imports=2 exact-payload=1 readonly=1",
        error?"FAIL":"PASS",allocation?allocation:indirect?"indirect":hole?"hole":trailing?"trailing":mirrorlog?"mirrorlog":"plain",error);note(line);
    return error;
#undef TOPO_CHECK
}
