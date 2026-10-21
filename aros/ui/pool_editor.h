/* Guided drafts never touch storage. Apply only populates the normal request
 * editor; its shared validation, preview, confirmation and IPC remain mandatory. */
static Object *poolwin,*poolname,*poolrole,*poolkind,*poolshift,*poolpath;
static Object *poolparity,*pooldata,*poolspares;
static Object *poolgroups,*pooldevices,*poolnote,*groupadd,*deviceadd,*deviceremove;
static Object *layoutclear,*layoutapply,*layoutclose,*poolhint,*poolrecover,*poolrewind;
static Object *poolrecovery,*poolreadonly;
static struct oz_topology draft_layout;
static int draft_import,draft_add;
static UQUAD draft_import_guid;
static unsigned device_indices[OZ_MAX_DEVICES],device_visible;
static const char *pool_roles[]={"data","log","cache","spare","special","dedup",NULL};
static const char *pool_kinds[]={"stripe","mirror","raidz1","raidz2","raidz3","draid",NULL};
static int pool_number(Object *field,ULONG *out) {
    const char *s=string(field);unsigned n=0;
    if(!*s)return 0;
    for(;*s;s++){if(*s<'0'||*s>'9'||n>100)return 0;n=n*10+*s-'0';}
    if(n>100)return 0;
    *out=n;return 1;
}
static const char *pool_shifts[]={"auto","9","10","11","12","13","14","15","16",NULL};
static void refresh_devices(void) {
    unsigned i;IPTR group=value(poolgroups,MUIA_List_Active);char line[280];
    DoMethod(pooldevices,MUIM_List_Clear);device_visible=0;
    for(i=0;i<draft_layout.device_count;i++)if(draft_import||draft_layout.devices[i].group==group){
        device_indices[device_visible++]=i;
        snprintf(line,sizeof(line),"%s",draft_layout.devices[i].path);
        DoMethod(pooldevices,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
    }
}
static void refresh_groups(void) {
    unsigned i;char line[80];DoMethod(poolgroups,MUIM_List_Clear);
    for(i=0;i<draft_layout.group_count;i++){
        snprintf(line,sizeof(line),"%u: %s / %s",i+1,pool_roles[draft_layout.groups[i].role],pool_kinds[draft_layout.groups[i].layout]);
        if(draft_layout.groups[i].layout==OZ_DRAID)
            snprintf(line,sizeof(line),"%u: data / draid%lu:%lud:%lus",i+1,
                (unsigned long)draft_layout.groups[i].draid_parity,
                (unsigned long)draft_layout.groups[i].draid_data,
                (unsigned long)draft_layout.groups[i].draid_spares);
        DoMethod(poolgroups,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
    }
    set(poolgroups,MUIA_List_Active,draft_layout.group_count?(IPTR)(draft_layout.group_count-1):(IPTR)MUIV_List_Active_Off);
    refresh_devices();
}
static Object *make_pool_window(void) {
    Object *gv=listview(&poolgroups),*dv=listview(&pooldevices);
    return poolwin=WindowObject,MUIA_Window_Title,"Create pool - device layout",MUIA_Window_ID,MAKE_ID('O','Z','P','L'),
        MUIA_Window_Width,650,MUIA_Window_Height,430,
        WindowContents,VGroup,
            Child,ColGroup(2),Child,Label2("Pool name"),Child,poolname=StringObject,StringFrame,MUIA_String_Columns,40,MUIA_String_MaxLen,256,End,
                Child,Label2("Sector alignment (ashift)"),Child,poolshift=CycleObject,MUIA_Cycle_Entries,pool_shifts,End,End,
            Child,HGroup,Child,poolrole=CycleObject,MUIA_Cycle_Entries,pool_roles,End,
                Child,poolkind=CycleObject,MUIA_Cycle_Entries,pool_kinds,End,Child,groupadd=SimpleButton("Add group"),End,
            Child,HGroup,Child,Label2("dRAID parity"),Child,poolparity=StringObject,StringFrame,MUIA_String_Contents,"1",MUIA_String_MaxLen,4,End,
                Child,Label2("Data (0=auto)"),Child,pooldata=StringObject,StringFrame,MUIA_String_Contents,"0",MUIA_String_MaxLen,4,End,
                Child,Label2("Distributed spares"),Child,poolspares=StringObject,StringFrame,MUIA_String_Contents,"0",MUIA_String_MaxLen,4,End,End,
            Child,HGroup,Child,gv,Child,dv,End,
            Child,poolhint=TextObject,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Select a group; add each device/file path separately (spaces preserved).",End,
            Child,poolrecovery=VGroup,
                Child,HGroup,Child,poolreadonly=make_check(1),Child,Label2("Read-only import"),End,
                Child,HGroup,Child,poolrecover=make_check(0),Child,Label2("Recover destroyed pool"),End,
                Child,HGroup,Child,poolrewind=make_check(0),Child,Label2("Rewind to checkpoint"),End,
            End,
            Child,HGroup,Child,poolpath=StringObject,StringFrame,MUIA_String_Columns,48,MUIA_String_MaxLen,256,End,
                Child,deviceadd=SimpleButton("Add device"),Child,deviceremove=SimpleButton("Remove device"),End,
            Child,poolnote=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Draft only. dRAID fields apply to the next dRAID group added.",End,
            Child,HGroup,Child,layoutclear=SimpleButton("Clear layout"),Child,layoutapply=SimpleButton("Use draft"),Child,layoutclose=SimpleButton("Cancel"),End,
        End,End;
}
static void init_pool_notifications(void) {
    notify_button(groupadd,EV_GROUP_ADD);notify_button(deviceadd,EV_DEVICE_ADD);notify_button(deviceremove,EV_DEVICE_REMOVE);
    notify_button(layoutclear,EV_LAYOUT_CLEAR);notify_button(layoutapply,EV_LAYOUT_APPLY);notify_button(layoutclose,EV_LAYOUT_CLOSE);
    DoMethod(poolgroups,MUIM_Notify,MUIA_List_Active,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_GROUP_SELECT);
    DoMethod(poolwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,EV_LAYOUT_CLOSE);
}
static void open_pool_editor(void) {
    int ro=1,recover=0,rewind=0;
    UQUAD guid=0;
    draft_import=oz_operations[selected].id==7;
    if(draft_import&&string(options)[0]&&
       !oz_parse_import_identity(string(options),&ro,&recover,&rewind,&guid)) {
        set(status,MUIA_Text_Contents,"Invalid import options; correct Properties/options before editing paths");
        return;
    }
    draft_add=oz_operations[selected].id==9;
    draft_layout=draft_import?import_files:draft_add?pool_add_layout:pool_layout;set(poolname,MUIA_String_Contents,string(target));
    set(poolwin,MUIA_Window_Title,draft_import?"Import pool - backing paths":draft_add?"Add devices - new groups only":"Create pool - device layout");
    set(poolrole,MUIA_Disabled,draft_import);set(poolkind,MUIA_Disabled,draft_import);
    set(poolparity,MUIA_Disabled,draft_import);set(pooldata,MUIA_Disabled,draft_import);set(poolspares,MUIA_Disabled,draft_import);
    set(poolshift,MUIA_Disabled,draft_import);set(groupadd,MUIA_Disabled,draft_import);
    set(poolgroups,MUIA_Disabled,draft_import);
    set(poolrecover,MUIA_Disabled,!draft_import);set(poolrecover,MUIA_Selected,FALSE);
    set(poolrewind,MUIA_Disabled,!draft_import);set(poolrewind,MUIA_Selected,FALSE);
    set(poolrecovery,MUIA_ShowMe,draft_import);
    set(poolreadonly,MUIA_Selected,TRUE);
    draft_import_guid=guid;
    if(draft_import) {
        set(poolreadonly,MUIA_Selected,ro);
        set(poolrecover,MUIA_Selected,recover);
        set(poolrewind,MUIA_Selected,rewind);
    }
    set(poolhint,MUIA_Text_Contents,draft_import?"List all member paths, including absent members.":"Select a group; add each device/file path separately (spaces preserved).");
    set(poolnote,MUIA_Text_Contents,draft_import?
        "Read/write import may update storage. Writable rewind discards later changes.":
        "Draft only. dRAID fields apply to the next dRAID group added.");
    set(poolshift,MUIA_Cycle_Active,draft_layout.ashift?draft_layout.ashift-8:0);
    set(poolkind,MUIA_Cycle_Active,value(topology,MUIA_Cycle_Active)<=OZ_DRAID?value(topology,MUIA_Cycle_Active):0);
    refresh_groups();set(poolwin,MUIA_Window_Open,TRUE);
}
static void pool_event(IPTR event) {
    IPTR group=value(poolgroups,MUIA_List_Active);unsigned i;char why[256];
    if(event==EV_GROUP_ADD || event==EV_DEVICE_ADD || event==EV_DEVICE_REMOVE || event==EV_LAYOUT_CLEAR)
        set(poolnote,MUIA_Text_Contents,"Draft changed. Use draft to validate; no devices opened.");
    switch(event){
    case EV_GROUP_SELECT:refresh_devices();break;
    case EV_GROUP_ADD:
        if(draft_import)break;
        if(draft_layout.group_count==OZ_MAX_GROUPS){set(poolnote,MUIA_Text_Contents,"Maximum 8 groups in this API page");break;}
        i=draft_layout.group_count;memset(&draft_layout.groups[i],0,sizeof(draft_layout.groups[i]));
        draft_layout.groups[i].role=value(poolrole,MUIA_Cycle_Active);draft_layout.groups[i].layout=value(poolkind,MUIA_Cycle_Active);
        if(draft_layout.groups[i].layout==OZ_DRAID &&
           (draft_layout.groups[i].role!=OZ_DATA||!pool_number(poolparity,&draft_layout.groups[i].draid_parity)||
            draft_layout.groups[i].draid_parity<1||draft_layout.groups[i].draid_parity>3||
            !pool_number(pooldata,&draft_layout.groups[i].draid_data)||!pool_number(poolspares,&draft_layout.groups[i].draid_spares))) {
            memset(&draft_layout.groups[i],0,sizeof(draft_layout.groups[i]));
            set(poolnote,MUIA_Text_Contents,"dRAID requires a data group, parity 1-3, and numeric data/spare counts.");break;
        }
        draft_layout.group_count++;
        refresh_groups();break;
    case EV_DEVICE_ADD:
        if((!draft_import&&group>=draft_layout.group_count) || !string(poolpath)[0]){set(poolnote,MUIA_Text_Contents,draft_import?"Enter a backing path first":"Select a group and enter a device path first");break;}
        if(draft_layout.device_count==OZ_MAX_DEVICES){set(poolnote,MUIA_Text_Contents,"Maximum 32 device paths in this API page");break;}
        i=draft_layout.device_count++;draft_layout.devices[i].group=draft_import?0:group;
        snprintf(draft_layout.devices[i].path,256,"%s",string(poolpath));set(poolpath,MUIA_String_Contents,"");refresh_devices();break;
    case EV_DEVICE_REMOVE:
        i=value(pooldevices,MUIA_List_Active);if(i>=device_visible)break;i=device_indices[i];
        memmove(&draft_layout.devices[i],&draft_layout.devices[i+1],(draft_layout.device_count-i-1)*sizeof(draft_layout.devices[0]));
        memset(&draft_layout.devices[--draft_layout.device_count],0,sizeof(draft_layout.devices[0]));refresh_devices();break;
    case EV_LAYOUT_CLEAR:memset(&draft_layout,0,sizeof(draft_layout));set(poolreadonly,MUIA_Selected,TRUE);set(poolrecover,MUIA_Selected,FALSE);set(poolrewind,MUIA_Selected,FALSE);refresh_groups();break;
    case EV_LAYOUT_APPLY:
        i=value(poolshift,MUIA_Cycle_Active);draft_layout.ashift=draft_import?0:i?i+8:0;
        if(!string(poolname)[0]){set(poolnote,MUIA_Text_Contents,"Pool name is required");break;}
        if((draft_import?oz_validate_import_files(&draft_layout,why,sizeof(why)):draft_add?oz_validate_add_topology(&draft_layout,why,sizeof(why)):oz_validate_topology(&draft_layout,why,sizeof(why)))!=OZ_OK){set(poolnote,MUIA_Text_Contents,why);break;}
        if(draft_import)import_files=draft_layout;else if(draft_add)pool_add_layout=draft_layout;else pool_layout=draft_layout;
        if(!draft_import&&draft_layout.group_count)
            set(topology,MUIA_Cycle_Active,draft_layout.groups[0].layout);
        set(target,MUIA_String_Contents,string(poolname));set(argument,MUIA_String_Contents,"");
        {char opts[96]="";
        if(draft_import)snprintf(opts,sizeof(opts),"readonly=%s%s%s",
            value(poolreadonly,MUIA_Selected)?"on":"off",
            value(poolrecover,MUIA_Selected)?";destroyed=on":"",
            value(poolrewind,MUIA_Selected)?";rewind=on":"");
        /* Changing paths/access mode must not drop the pool identity guard.
         * Clear layout clears members, not the expected pool identity. */
        if(draft_import&&draft_import_guid) {
            size_t used=strlen(opts);
            snprintf(opts+used,sizeof(opts)-used,";guid=%016llx",
                (unsigned long long)draft_import_guid);
        }
        /* Editing a create topology must not erase its creation options
         * (for example tname). Other workflows still reset incompatible
         * options, and import uses its explicit readonly/recovery controls. */
        if(draft_import||draft_add||oz_operations[selected].id!=5)
            set(options,MUIA_String_Contents,opts);}
        set(category,MUIA_Cycle_Active,1);select_category();
        for(i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==(draft_import?7:draft_add?9:5)){set(actions,MUIA_List_Active,i);select_operation();break;}
        set(dryrun,MUIA_Selected,TRUE);set(poolwin,MUIA_Window_Open,FALSE);send_request(0,1);break;
    case EV_LAYOUT_CLOSE:set(poolwin,MUIA_Window_Open,FALSE);break;
    }
}
