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
static int native_split_topology_test(unsigned mode)
{
    unsigned indirect=mode==2,hole=mode==1,trailing=mode==4,mirrorlog=mode==5;
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
    const char payload[]="Split topology retained payload\n";
    if(!cold){
    if(mirrorlog)snprintf(command,sizeof(command),"C:zpool create -o cachefile=none -O mountpoint=none topo_source mirror \"%s\" \"%s\" log mirror \"%s\" \"%s\"",paths[0],paths[1],paths[4],paths[5]);
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
    TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"topo_source/files","OZTOPO",OZ_MOUNT_ATTACH,0));
    file=Open((CONST_STRPTR)"OZTOPO:payload",MODE_NEWFILE);TOPO_CHECK(file);
    TOPO_CHECK(Write(file,(APTR)payload,sizeof(payload)-1)==sizeof(payload)-1&&Flush(file));
    TOPO_CHECK(Close(file));file=0;
    TOPO_CHECK(!oz_mount_control(OZ_ADMISSION_PORT,"topo_source/files","",OZ_MOUNT_DETACH,0));
    TOPO_CHECK(!split_topology_command("C:zpool sync topo_source"));
    if(indirect){
        TOPO_CHECK(!split_topology_command("C:zpool remove topo_source mirror-0"));
        TOPO_CHECK(!split_topology_command("C:zpool wait -t remove topo_source"));
        snprintf(command,sizeof(command),"C:zpool split topo_source topo_split \"%s\"",paths[3]);
    }else snprintf(command,sizeof(command),"C:zpool split topo_source topo_split \"%s\" \"%s\"",paths[1],paths[3]);
    TOPO_CHECK(!split_topology_command(command));
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
        char bytes[128];file=Open((CONST_STRPTR)"OZTOPO:payload",MODE_OLDFILE);TOPO_CHECK(file);
        TOPO_CHECK(Read(file,bytes,sizeof(bytes))==sizeof(payload)-1&&!memcmp(bytes,payload,sizeof(payload)-1));
        TOPO_CHECK(Close(file));file=0;
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
        error?"FAIL":"PASS",indirect?"indirect":hole?"hole":trailing?"trailing":mirrorlog?"mirrorlog":"plain",error);note(line);
    return error;
#undef TOPO_CHECK
}
