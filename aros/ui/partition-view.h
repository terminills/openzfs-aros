/* Inspection supplies a draft candidate, never admission or proof of a pool.
 * The service reopens and validates the extent when import is confirmed. */
#include "../device/mountfile.h"
#include "settings-file.h"
static Object *partwin,*partdevice,*partunit,*partquery,*partclose,*partlist,*partnote;
static Object *partimport,*partcreate;
static Object *partscan,*partchoose,*partinventory;
static struct oz_inventory_worker *inventoryworker;
static struct oz_storage_inventory inventory;
static const char *inventory_empty[]={"Scan to list devices",NULL};
static char inventory_labels[OZ_STORAGE_INVENTORY_MAX][80];
static const char *inventory_entries[OZ_STORAGE_INVENTORY_MAX+1];
static Object *partmappings;
static Object *partremap,*partvolume,*partwrite,*parteject;
static int mapping_mutation;
static Object *partfs,*partblocks,*partmountpath,*partmountpreview,*partmountsave;
static const char *mountfilesystems[]={"FFS","FAT",NULL};
static unsigned mountunit;
static int mountaction,mountffs;
static UQUAD mountblocks;
static char mountpath[256];
static struct oz_mapping_worker *mappingworker;
static Object *partlabel,*partexpand;
static struct oz_device_geometry partgeometry;
static int partgeometryvalid;
static struct oz_partition_catalog partcatalog;
static int partvalid;
static struct oz_partition_worker *partworker;
static char partmessage[256],partqueried[256];
static int32_t partqueriedunit;
static Object *make_partition_window(void)
{
    Object *rows=listview(&partlist);
    return partwin=WindowObject,MUIA_Window_Title,"OpenZFS - devices and partitions",
        MUIA_Window_ID,MAKE_ID('O','Z','D','V'),MUIA_Window_Width,650,MUIA_Window_Height,520,
        WindowContents,VGroup,
            Child,TextObject,MUIA_Text_Contents,"AROS device inspection and GPT preparation. Root access required.",End,
            Child,HGroup,
                Child,partscan=SimpleButton("Scan devices"),
                Child,partinventory=CycleObject,MUIA_Cycle_Entries,inventory_empty,End,
                Child,partchoose=SimpleButton("Use device"),End,
            Child,ColGroup(2),Child,Label2("Device"),
                Child,partdevice=StringObject,StringFrame,MUIA_String_MaxLen,256,End,
                Child,Label2("Unit"),Child,partunit=StringObject,StringFrame,
                    MUIA_String_MaxLen,12,MUIA_String_Contents,"0",End,End,
            Child,partnote=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,
                MUIA_Text_Contents,"Enter a device name and unit, then Inspect. Nothing has been queried.",End,
            Child,rows,
            Child,ColGroup(2),Child,Label2("Zvol dataset"),
                Child,partvolume=StringObject,StringFrame,MUIA_String_MaxLen,256,End,End,
            Child,HGroup,Child,Label2("Writable mapping"),
                Child,partwrite=make_check(FALSE),Child,partremap=SimpleButton("Remap ejected unit..."),
                Child,parteject=SimpleButton("Eject unit..."),End,
            Child,HGroup,Child,Label2("Mount file"),
                Child,partfs=CycleObject,MUIA_Cycle_Entries,mountfilesystems,End,
                Child,Label2("512-byte blocks"),Child,partblocks=StringObject,StringFrame,
                    MUIA_String_MaxLen,21,MUIA_ShortHelp,"Blank describes the whole zvol. A count limits the view from sector zero.",End,End,
            Child,ColGroup(2),Child,Label2("Output file"),Child,partmountpath=StringObject,StringFrame,
                MUIA_String_MaxLen,256,MUIA_String_Contents,"T:OZBLOCK",End,End,
            Child,HGroup,Child,partmountpreview=SimpleButton("Preview mount file"),
                Child,partmountsave=SimpleButton("Save mount file..."),End,
            Child,TextObject,MUIA_Text_Contents,"Inspect is read-only. GPT changes require a separate confirmation.",End,
            Child,HGroup,Child,partlabel=SimpleButton("Replace GPT..."),
                Child,partexpand=SimpleButton("Expand GPT..."),End,
            Child,HGroup,Child,partquery=SimpleButton("Inspect"),Child,partmappings=SimpleButton("Zvol mappings"),
                Child,partimport=SimpleButton("Add to import"),Child,partcreate=SimpleButton("Create draft"),
                Child,partclose=SimpleButton("Close"),End,
        End,End;
}
static void init_partition_notifications(void)
{
    notify_button(partscan,EV_PART_SCAN);notify_button(partchoose,EV_PART_CHOOSE);
    set(partchoose,MUIA_Disabled,TRUE);
    notify_button(partquery,EV_PART_QUERY);notify_button(partclose,EV_PART_CLOSE);
    notify_button(partmappings,EV_PART_MAPPINGS);
    notify_button(partremap,EV_PART_REMAP);
    notify_button(parteject,EV_PART_EJECT);
    notify_button(partmountpreview,EV_PART_MOUNT_PREVIEW);notify_button(partmountsave,EV_PART_MOUNT_SAVE);
    notify_button(partimport,EV_PART_IMPORT);set(partimport,MUIA_Disabled,TRUE);
    notify_button(partcreate,EV_PART_CREATE);set(partcreate,MUIA_Disabled,TRUE);
    DoMethod(partimport,MUIM_Notify,MUIA_Disabled,MUIV_EveryTime,(IPTR)partcreate,
        3,MUIM_Set,MUIA_Disabled,MUIV_TriggerValue);
    notify_button(partlabel,EV_PART_LABEL);notify_button(partexpand,EV_PART_EXPAND);
    set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    DoMethod(partwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,EV_PART_CLOSE);
}
static void inventory_start(void)
{
    if(inventoryworker||closing)return;
    inventory.count=0;set(partchoose,MUIA_Disabled,TRUE);
    set(partinventory,MUIA_Cycle_Entries,inventory_empty);
    int error=oz_inventory_worker_start(auth_endpoint,&inventoryworker);
    if(error){snprintf(partmessage,sizeof(partmessage),"Cannot start device scan: %s (%d)",strerror(error),error);
        set(partnote,MUIA_Text_Contents,partmessage);return;}
    set(partscan,MUIA_Disabled,TRUE);
    set(partnote,MUIA_Text_Contents,"Reading storage device inventory...");
}
static void inventory_poll(void)
{
    if(!inventoryworker)return;
    int operation=0,error=oz_inventory_worker_poll(inventoryworker,&inventory,&operation);
    if(error==EAGAIN)return;
    if(error){set(partnote,MUIA_Text_Contents,"Waiting for device scan cleanup...");return;}
    if(operation==ENOENT)snprintf(partmessage,sizeof(partmessage),
        "Device scan unavailable (2). Check that OpenZFSServiceNative is running.");
    else if(operation==EACCES||operation==EPERM)snprintf(partmessage,sizeof(partmessage),
        "Device scan denied (%d). Root access is required.",operation);
    else if(operation)snprintf(partmessage,sizeof(partmessage),"Device scan failed: %s (%d)",strerror(operation),operation);
    else {
        for(uint32_t i=0;i<inventory.count;i++){
            const struct oz_storage_unit *u=&inventory.units[i];
            snprintf(inventory_labels[i],sizeof(inventory_labels[i]),"%.24s/%lu %.24s",
                u->device,(unsigned long)u->unit,u->model);
            /* Device text must not inject MUI formatting sequences. */
            for(char *p=inventory_labels[i];*p;p++)if((unsigned char)*p<32||*p==127)*p='?';
            inventory_entries[i]=inventory_labels[i];
        }
        inventory_entries[inventory.count]=NULL;
        if(inventory.count){set(partinventory,MUIA_Cycle_Entries,inventory_entries);
            set(partinventory,MUIA_Cycle_Active,0);set(partchoose,MUIA_Disabled,FALSE);}
        snprintf(partmessage,sizeof(partmessage),"%lu storage devices found. Select one, then Use device and Inspect.",
            (unsigned long)inventory.count);
    }
    set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);
    if(!oz_inventory_worker_free(inventoryworker))inventoryworker=NULL;
    set(partscan,MUIA_Disabled,FALSE);
}
static void inventory_choose(void)
{
    if(inventoryworker||partworker||mappingworker||gptworker||closing)return;
    IPTR selected=value(partinventory,MUIA_Cycle_Active);
    if(selected>=inventory.count)return;
    const struct oz_storage_unit *u=&inventory.units[selected];
    if(u->unit>INT32_MAX){set(partnote,MUIA_Text_Contents,"Device unit exceeds the inspection API range.");return;}
    char unit[12];snprintf(unit,sizeof(unit),"%lu",(unsigned long)u->unit);
    set(partdevice,MUIA_String_Contents,u->device);set(partunit,MUIA_String_Contents,unit);
    partvalid=partgeometryvalid=0;
    set(partimport,MUIA_Disabled,TRUE);set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    DoMethod(partlist,MUIM_List_Clear);
    set(partnote,MUIA_Text_Contents,"Device selected. Inspect to read its current partition table.");
}
static void mapping_inspect(void)
{
    if(mappingworker||partworker||gptworker||closing)return;
    partvalid=partgeometryvalid=0;
    set(partimport,MUIA_Disabled,TRUE);set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    DoMethod(partlist,MUIM_List_Clear);
    mapping_mutation=0;mountaction=0;
    int e=oz_mapping_worker_start(&mappingworker);
    if(e){snprintf(partmessage,sizeof(partmessage),"Cannot start mapping query: %s (%d)",strerror(e),e);
        set(partnote,MUIA_Text_Contents,partmessage);return;}
    set(partquery,MUIA_Disabled,TRUE);set(partmappings,MUIA_Disabled,TRUE);
    set(partnote,MUIA_Text_Contents,"Reading resident zvol mappings...");
}
static void mapping_remap(void)
{
    if(mappingworker||partworker||gptworker||auth_worker||client.pending||closing)return;
    unsigned unit=0;const char *value=string(partunit),*dataset=string(partvolume);
    if(!*value||!*dataset)goto invalid;
    for(const char *p=value;*p;p++){
        if(*p<'0'||*p>'9')goto invalid;
        unit=unit*10+(*p-'0');if(unit>=32)goto invalid;
    }
    IPTR writable=0;get(partwrite,MUIA_Selected,&writable);
    char prompt[512];
    snprintf(prompt,sizeof(prompt),"Map openzfs.device unit %u to %s (%s)?\nThe unit must already be ejected and unused.\nNo filesystem will be unmounted or formatted.",
        unit,dataset,writable?"writable requested":"read-only");
    if(MUI_Request(app,partwin,0,(CONST_STRPTR)"Confirm zvol mapping",
        (CONST_STRPTR)"_Remap|_Cancel",(CONST_STRPTR)"%s",prompt)!=1)return;
    int error=oz_mapping_worker_remap_start(unit,dataset,writable!=0,&mappingworker);
    if(error){snprintf(partmessage,sizeof(partmessage),"Cannot start remap: %s (%d)",strerror(error),error);
        set(partnote,MUIA_Text_Contents,partmessage);return;}
    mapping_mutation=1;mountaction=0;partvalid=partgeometryvalid=0;
    set(partimport,MUIA_Disabled,TRUE);set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    set(partquery,MUIA_Disabled,TRUE);set(partmappings,MUIA_Disabled,TRUE);
    set(partremap,MUIA_Disabled,TRUE);set(partvolume,MUIA_Disabled,TRUE);
    set(partunit,MUIA_Disabled,TRUE);set(partwrite,MUIA_Disabled,TRUE);
    DoMethod(partlist,MUIM_List_Clear);
    set(partnote,MUIA_Text_Contents,"Remapping ejected unit; waiting for device response...");return;
invalid:
    set(partnote,MUIA_Text_Contents,"Enter unit 0..31 and a zvol dataset name.");
}
static void mapping_eject(void)
{
    if(mappingworker||partworker||gptworker||auth_worker||client.pending||closing)return;
    unsigned unit=0;const char *v=string(partunit);
    if(!*v)goto invalid;
    for(;*v;v++){if(*v<'0'||*v>'9')goto invalid;unit=unit*10+(*v-'0');if(unit>=32)goto invalid;}
    char prompt[384];snprintf(prompt,sizeof(prompt),
        "Flush and eject openzfs.device unit %u?\nUnmount its filesystem and close other users first.\nBusy units are refused; no forced detach.\nThe zvol dataset is not deleted.",unit);
    if(MUI_Request(app,partwin,0,(CONST_STRPTR)"Confirm zvol eject",
        (CONST_STRPTR)"_Eject|_Cancel",(CONST_STRPTR)"%s",prompt)!=1)return;
    int error=oz_mapping_worker_eject_start(unit,&mappingworker);
    if(error){snprintf(partmessage,sizeof(partmessage),"Cannot start eject: %s (%d)",strerror(error),error);
        set(partnote,MUIA_Text_Contents,partmessage);return;}
    mapping_mutation=2;mountaction=0;partvalid=partgeometryvalid=0;
    set(partimport,MUIA_Disabled,TRUE);set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    DoMethod(partlist,MUIM_List_Clear);
    set(partnote,MUIA_Text_Contents,"Flushing and ejecting unused zvol unit...");return;
invalid:
    set(partnote,MUIA_Text_Contents,"Enter unit 0..31 to eject.");
}
static int mount_read(BPTR f,void *out)
{
    char *p=out;LONG n=0;
    while(n<OZ_MOUNTFILE_SIZE-1){LONG got=Read(f,p+n,OZ_MOUNTFILE_SIZE-1-n);
        if(got<0)return EIO;
        if(!got)break;
        n+=got;}
    char extra;LONG more=Read(f,&extra,1);
    if(more!=0||!n||memchr(p,0,n))return EIO;
    p[n]=0;return 0;
}
static int mount_write(BPTR f,const void *p)
{size_t n=strlen(p);return Write(f,(APTR)p,n)==(LONG)n?0:EIO;}
static int mount_equal(const void *a,const void *b){return !strcmp(a,b);}
static const struct oz_settings_codec mountcodec={OZ_MOUNTFILE_SIZE,mount_read,mount_write,mount_equal,NULL};
static void mapping_mountfile(int savefile)
{
    if(mappingworker||partworker||gptworker||auth_worker||client.pending||closing)return;
    unsigned unit=0;const char *v=string(partunit);UQUAD blocks=0;
    if(!*v)goto invalid;
    for(;*v;v++){if(*v<'0'||*v>'9')goto invalid;unit=unit*10+(*v-'0');if(unit>=32)goto invalid;}
    if(*string(partblocks)&&oz_mountfile_count(string(partblocks),&blocks))goto invalid;
    if(savefile&&(!strchr(string(partmountpath),':')||!*string(partmountpath)))goto invalid;
    mapping_mutation=0;mountaction=savefile?2:1;mountunit=unit;mountblocks=blocks;
    mountffs=value(partfs,MUIA_Cycle_Active)==0;
    snprintf(mountpath,sizeof(mountpath),"%s",string(partmountpath));
    partvalid=partgeometryvalid=0;
    set(partimport,MUIA_Disabled,TRUE);set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    int e=oz_mapping_worker_start(&mappingworker);
    if(e){mountaction=0;snprintf(partmessage,sizeof(partmessage),"Cannot query mapping: %s (%d)",strerror(e),e);
        set(partnote,MUIA_Text_Contents,partmessage);return;}
    set(partnote,MUIA_Text_Contents,"Reading current zvol mapping for mount file...");return;
invalid:
    set(partnote,MUIA_Text_Contents,"Enter unit 0..31, optional positive block count, and a full output path.");
}
static void mountfile_result(const struct oz_mapping_catalog *catalog,int error)
{
    char text[OZ_MOUNTFILE_SIZE];
    if(!error){
        if(mountunit>=catalog->count)error=ENXIO;
        else error=oz_mountfile_format(&catalog->units[mountunit],mountffs,mountblocks,text,sizeof(text));
    }
    if(error){snprintf(partmessage,sizeof(partmessage),"Mount file not generated: %s (%d)",strerror(error),error);return;}
    DoMethod(partlist,MUIM_List_Clear);
    char lines[OZ_MOUNTFILE_SIZE];strcpy(lines,text);char *p=lines;
    while(*p){char *end=strchr(p,'\n');if(end)*end=0;
        DoMethod(partlist,MUIM_List_InsertSingle,(IPTR)p,MUIV_List_Insert_Bottom);
        if(!end)break;
        p=end+1;}
    snprintf(partmessage,sizeof(partmessage),"Unit %u %s mount file preview. Nothing mounted or formatted.",mountunit,mountffs?"FFS":"FAT");
    if(mountaction!=2||closing)return;
    char prompt[768];
    snprintf(prompt,sizeof(prompt),"Save mount file to %s?\nExisting file will be replaced.\nUnit %u: %.255s\n%s, %s\nNo filesystem will be mounted or formatted.\nDo not remap the unit before using this file.",
        mountpath,mountunit,catalog->units[mountunit].dataset,mountffs?"FFS":"FAT",mountblocks?"bounded view":"whole mapped capacity");
    if(MUI_Request(app,partwin,0,(CONST_STRPTR)"Save zvol mount file",
        (CONST_STRPTR)"_Save|_Cancel",(CONST_STRPTR)"%s",prompt)!=1){
        snprintf(partmessage,sizeof(partmessage),"Mount file save cancelled; preview only.");return;}
    int published=0;
    error=oz_settings_store(mountpath,text,&published,&mountcodec);
    if(error)snprintf(partmessage,sizeof(partmessage),"%s (%d). Inspect before retrying.",
        published?"Mount file saved; cleanup failed":"Mount file not saved",error);
    else snprintf(partmessage,sizeof(partmessage),"Saved %.190s; use Mount explicitly. Nothing formatted.",mountpath);
}
static void mapping_poll(void)
{
    if(!mappingworker)return;
    static struct oz_mapping_catalog catalog;int operation=0;
    int e=oz_mapping_worker_poll(mappingworker,&catalog,&operation);
    if(e==EAGAIN)return;
    if(e){set(partnote,MUIA_Text_Contents,"Waiting for mapping worker cleanup; request retained.");return;}
    int remapped=oz_mapping_worker_remapped(mappingworker);
    const char *action=mapping_mutation==2?"Eject":"Remap";
    if(mountaction)mountfile_result(&catalog,operation);
    else if(remapped&&operation)snprintf(partmessage,sizeof(partmessage),"%s completed; cleanup/refresh failed (%d). Inspect before retrying.",action,operation);
    else if(mapping_mutation&&operation==EBUSY)snprintf(partmessage,sizeof(partmessage),"%s refused: unit active, other handles open, or queued I/O.",action);
    else if(mapping_mutation&&operation)snprintf(partmessage,sizeof(partmessage),"%s failed: %s (%d). No automatic retry.",action,strerror(operation),operation);
    else if(operation==ENXIO)snprintf(partmessage,sizeof(partmessage),"Zvol device unavailable or access denied; system-owner access required.");
    else if(operation)snprintf(partmessage,sizeof(partmessage),"Mapping query failed: %s (%d)",strerror(operation),operation);
    else{
        snprintf(partmessage,sizeof(partmessage),"%s%u explicit zvol mappings. Names reflect the current mapping.",
            remapped?(mapping_mutation==2?"Eject completed. ":"Remap completed. "):"",catalog.count);
        for(unsigned i=0;i<catalog.count;i++){
            const struct oz_device_mapping *m=&catalog.units[i];char line[320];
            snprintf(line,sizeof(line),"Unit %lu: %.255s",(unsigned long)m->unit,m->dataset);
            DoMethod(partlist,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
            snprintf(line,sizeof(line),"  %s | %llu bytes | %lu-byte sectors",
                !(m->flags&OZ_MAPPING_PRESENT)?"Absent":(m->flags&OZ_MAPPING_READONLY)?"Read-only":"Writable",
                (unsigned long long)m->bytes,(unsigned long)m->sector_size);
            DoMethod(partlist,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
        }
    }
    set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);
    if(!oz_mapping_worker_free(mappingworker))mappingworker=NULL;
    if(!mappingworker)mountaction=0;
    set(partquery,MUIA_Disabled,mappingworker!=NULL);set(partmappings,MUIA_Disabled,mappingworker!=NULL);
    set(partremap,MUIA_Disabled,mappingworker!=NULL);set(partvolume,MUIA_Disabled,mappingworker!=NULL);
    set(partunit,MUIA_Disabled,mappingworker!=NULL);set(partwrite,MUIA_Disabled,mappingworker!=NULL);
}
static void partition_inspect(void)
{
    if(partworker||gptworker||mappingworker||closing)return;
    partgeometryvalid=0;set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);
    partvalid=0;set(partimport,MUIA_Disabled,TRUE);
    const char *unit=string(partunit);uint64_t number=0;
    if(!*unit)goto invalid;
    for(const char *p=unit;*p;p++) {
        if(*p<'0'||*p>'9')goto invalid;
        number=number*10+(unsigned)(*p-'0');if(number>INT32_MAX)goto invalid;
    }
    if(!*string(partdevice))goto invalid;
    snprintf(partqueried,sizeof(partqueried),"%s",string(partdevice));partqueriedunit=number;
    DoMethod(partlist,MUIM_List_Clear);
    int error=oz_partition_worker_start(auth_endpoint,partqueried,partqueriedunit,&partworker);
    if(error){snprintf(partmessage,sizeof(partmessage),"Cannot start inspection: %s (%d)",strerror(error),error);
        set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);return;}
    set(partquery,MUIA_Disabled,TRUE);set(partdevice,MUIA_Disabled,TRUE);set(partunit,MUIA_Disabled,TRUE);
    set(partnote,MUIA_Text_Contents,"Reading partition metadata...");return;
invalid:
    DoMethod(partlist,MUIM_List_Clear);
    set(partnote,MUIA_Text_Contents,"Enter a device name and a unit from 0 to 2147483647.");
}
static void partition_poll(void)
{
    if(!partworker)return;
    static struct oz_partition_catalog catalog;
    int operation_error=0,error=oz_partition_worker_poll(partworker,&catalog,&operation_error);
    if(error==EAGAIN)return;
    if(error){set(partnote,MUIA_Text_Contents,"Waiting for inspection worker cleanup; request storage retained.");return;}
    partgeometryvalid=!oz_partition_worker_geometry(partworker,&partgeometry);
    if(operation_error) {
        snprintf(partmessage,sizeof(partmessage),"Inspection failed: %s (%d). No partition list available.",
            strerror(operation_error),operation_error);
    } else {
        partcatalog=catalog;partvalid=1;
        const char *tables[]={"Unknown","RDB","MBR","EBR","GPT"};
        snprintf(partmessage,sizeof(partmessage),"%.80s unit %ld: %s, %lu partitions, %lu-byte sectors",
            partqueried,(long)partqueriedunit,catalog.table_type<5?tables[catalog.table_type]:"Unknown",
            (unsigned long)catalog.count,(unsigned long)catalog.sector_size);
        for(uint32_t i=0;i<catalog.count;i++) {
            const struct oz_partition_entry *e=&catalog.entries[i];char line[320],type[65];
            unsigned n=e->type_length<=32?e->type_length:32;
            for(unsigned j=0;j<n;j++)snprintf(type+2*j,3,"%02x",e->type[j]);
            type[2*n]=0;
            snprintf(line,sizeof(line),"Slot %lu: sectors %llu..%llu (%llu bytes)",
                (unsigned long)e->position,(unsigned long long)e->first_sector,
                (unsigned long long)e->last_sector,
                (unsigned long long)((e->last_sector-e->first_sector+1)*catalog.sector_size));
            DoMethod(partlist,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
            snprintf(line,sizeof(line),"  Name: %.127s | native type: %s",e->name[0]?e->name:"(unnamed)",type);
            DoMethod(partlist,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
        }
    }
    set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);
    if(!oz_partition_worker_free(partworker))partworker=NULL;
    set(partquery,MUIA_Disabled,partworker!=NULL);set(partdevice,MUIA_Disabled,partworker!=NULL);set(partunit,MUIA_Disabled,partworker!=NULL);
    set(partimport,MUIA_Disabled,partworker!=NULL||!partvalid||!partcatalog.count);
    set(partlabel,MUIA_Disabled,partworker!=NULL||!partgeometryvalid);
    set(partexpand,MUIA_Disabled,partworker!=NULL||!partgeometryvalid||!partvalid||partcatalog.table_type!=4);
}
static void partition_gpt(unsigned operation)
{
    if(closing||auth_worker||partworker||gptworker||mappingworker||!partgeometryvalid)return;
    /* Editing either field invalidates the selection for destructive actions. */
    char unit[16];snprintf(unit,sizeof(unit),"%ld",(long)partqueriedunit);
    if(strcmp(string(partdevice),partqueried)||strcmp(string(partunit),unit)) {
        set(partnote,MUIA_Text_Contents,"Device selection changed. Inspect again before changing GPT.");return;
    }
    struct oz_gpt_query query={0};
    query.version=OZ_GPT_VERSION;query.operation=operation;query.flags=OZ_GPT_CONFIRMED;
    query.unit=partqueriedunit;strcpy(query.device,partqueried);
    query.bytes=partgeometry.bytes;query.sector_size=partgeometry.sector_size;
    if(operation==OZ_GPT_LABEL){query.flags|=OZ_GPT_REPLACE_TABLE;strcpy(query.name,"AROS OpenZFS");}
    char prompt[1024];
    snprintf(prompt,sizeof(prompt),
        "%s\nDevice: %s unit %ld\nSize: %llu bytes; sector: %lu bytes\n%s\nStop all external raw-device users first.\nA failed operation may already have written data.",
        operation==OZ_GPT_LABEL?"REPLACE the whole disk partition table?":"Expand the existing ZFS GPT partition?",
        query.device,(long)query.unit,(unsigned long long)query.bytes,(unsigned long)query.sector_size,
        operation==OZ_GPT_LABEL?"Existing partitions will be lost. This does not create a pool.":"This changes GPT only; it does not expand an imported pool.");
    if(MUI_Request(app,partwin,0,(CONST_STRPTR)"Confirm GPT change",
        (CONST_STRPTR)"_Apply|_Cancel",(CONST_STRPTR)"%s",prompt)!=1)return;
    int error=oz_gpt_worker_start(auth_endpoint,&query,&gptworker);
    if(error){snprintf(partmessage,sizeof(partmessage),"Cannot start GPT operation: %s (%d)",strerror(error),error);
        set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);return;}
    partvalid=partgeometryvalid=0;
    set(partquery,MUIA_Disabled,TRUE);set(partdevice,MUIA_Disabled,TRUE);set(partunit,MUIA_Disabled,TRUE);
    set(partlabel,MUIA_Disabled,TRUE);set(partexpand,MUIA_Disabled,TRUE);set(partimport,MUIA_Disabled,TRUE);
    DoMethod(partlist,MUIM_List_Clear);
    snprintf(partmessage,sizeof(partmessage),"GPT %s pending on %.120s unit %ld; do not terminate manager",
        operation==OZ_GPT_LABEL?"replacement":"expansion",query.device,(long)query.unit);
    set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);
}
static void partition_gpt_poll(void)
{
    if(!gptworker)return;
    struct oz_gpt_query result;int operation_error=0;
    int error=oz_gpt_worker_poll(gptworker,&result,&operation_error);
    if(error==EAGAIN)return;
    if(error){set(partnote,MUIA_Text_Contents,"Waiting for GPT worker cleanup; request retained.");return;}
    if(operation_error)snprintf(partmessage,sizeof(partmessage),
        "GPT failed: %s (%d). Writes may have occurred. Inspect before retrying.",strerror(operation_error),operation_error);
    else snprintf(partmessage,sizeof(partmessage),"%s Inspect to refresh.",
        result.changed?"GPT change verified.":"GPT already uses available space; no change.");
    set(partnote,MUIA_Text_Contents,partmessage);logline(partmessage);
    if(!oz_gpt_worker_free(gptworker))gptworker=NULL;
    set(partquery,MUIA_Disabled,gptworker!=NULL);set(partdevice,MUIA_Disabled,gptworker!=NULL);set(partunit,MUIA_Disabled,gptworker!=NULL);
}
static int partition_selected_path(char *path,size_t size)
{
    if(closing||auth_worker||partworker||gptworker||mappingworker||inventoryworker||!partvalid)return 0;
    IPTR row=value(partlist,MUIA_List_Active);
    if((unsigned long)row/2>=partcatalog.count) {
        set(partnote,MUIA_Text_Contents,"Select a partition row first.");return 0;
    }
    int n=snprintf(path,size,"/AROS/%s/%ld/partition/%lu",partqueried,
        (long)partqueriedunit,(unsigned long)partcatalog.entries[(unsigned long)row/2].position);
    if(n<0||(size_t)n>=size) {
        set(partnote,MUIA_Text_Contents,"This native path exceeds the API limit.");return 0;
    }
    return 1;
}
static void partition_create_draft(void)
{
    char path[256];
    if(!partition_selected_path(path,sizeof(path)))return;
    set(category,MUIA_Cycle_Active,1);select_category();
    for(unsigned i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==5){
        set(actions,MUIA_List_Active,i);select_operation();break;
    }
    set(partwin,MUIA_Window_Open,FALSE);open_pool_editor();
    /* Preserve the existing creation layout. A partition is only a candidate;
     * never invent a stripe/group, claim blankness, or submit from this action. */
    set(poolpath,MUIA_String_Contents,path);
    set(poolnote,MUIA_Text_Contents,"Partition selected. Choose a group, then Add device. Draft only.");
    logline("Inspected partition copied to creation path field; no device added or request submitted");
}
static void partition_add_import(void)
{
    char path[256];
    if(!partition_selected_path(path,sizeof(path)))return;
    for(unsigned i=0;i<import_files.device_count;i++)if(!strcmp(import_files.devices[i].path,path)) {
        set(partnote,MUIA_Text_Contents,"That partition is already in the import draft.");return;
    }
    if(import_files.device_count==OZ_MAX_DEVICES) {
        set(partnote,MUIA_Text_Contents,"Import draft already contains 32 paths.");return;
    }
    struct oz_vdev_leaf *leaf=&import_files.devices[import_files.device_count++];
    leaf->group=0;strcpy(leaf->path,path);
    set(category,MUIA_Cycle_Active,1);select_category();
    for(unsigned i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==7) {
        set(actions,MUIA_List_Active,i);select_operation();break;
    }
    set(argument,MUIA_String_Contents,"");set(options,MUIA_String_Contents,"readonly=on");
    set(partwin,MUIA_Window_Open,FALSE);open_pool_editor();
    set(poolnote,MUIA_Text_Contents,"Partition added. Enter pool name and all member paths; nothing imported yet.");
    logline("Inspected partition added to readonly import draft; no request submitted");
}
