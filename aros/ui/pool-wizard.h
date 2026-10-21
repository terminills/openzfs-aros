/* Pool creation drafts use the same typed topology and request validator as
 * Advanced. No device is opened by any wizard page. Submission is exclusively
 * through send_request(), including its confirmation and admission worker. */
static Object *wizardwin,*wizardroot,*wizardpages[3],*wizardtitle,*wizardnote;
static Object *wizardname,*wizardkind,*wizardpath,*wizarddevices,*wizardreview;
static Object *wizardnext,*wizardback,*wizardcancel,*wizardadd,*wizardremove;
static unsigned wizardstep;
static struct oz_topology wizardlayout;
static const char *wizardkinds[]={"Stripe - no device-failure protection",
    "Mirror - copies on every member", "RAIDZ1 - one-device fault tolerance",
    "RAIDZ2 - two-device fault tolerance", "RAIDZ3 - three-device fault tolerance",NULL};
static void wizard_page(unsigned page) {
    static const char *titles[]={"1 of 3 - Name and protection", "2 of 3 - Choose members", "3 of 3 - Review creation"};
    wizardstep=page;
    DoMethod(wizardroot,MUIM_Group_InitChange);
    for(unsigned i=0;i<3;i++)set(wizardpages[i],MUIA_ShowMe,i==page);
    DoMethod(wizardroot,MUIM_Group_ExitChange);
    set(wizardtitle,MUIA_Text_Contents,titles[page]);
    set(wizardback,MUIA_Disabled,page==0);
    set(wizardnext,MUIA_Text_Contents,page==2?"Confirm creation...":"Next >");
    set(wizardnote,MUIA_Text_Contents,"Draft only. No storage has been changed.");
}
static void wizard_refresh(void) {
    DoMethod(wizarddevices,MUIM_List_Clear);
    for(unsigned i=0;i<wizardlayout.device_count;i++)
        DoMethod(wizarddevices,MUIM_List_InsertSingle,(IPTR)wizardlayout.devices[i].path,MUIV_List_Insert_Bottom);
}
static Object *make_wizard_window(void) {
    Object *members=listview(&wizarddevices),*review=listview(&wizardreview);
    return wizardwin=WindowObject,MUIA_Window_Title,"Create an OpenZFS pool",MUIA_Window_ID,MAKE_ID('O','Z','W','Z'),
        MUIA_Window_Width,640,MUIA_Window_Height,350,
        WindowContents,wizardroot=VGroup,
            Child,wizardtitle=TextObject,MUIA_Text_Contents,"Name and protection",End,
            Child,wizardpages[0]=VGroup,
                Child,Label2("Pool name (for example: WorkPool)"),
                Child,wizardname=StringObject,StringFrame,MUIA_String_MaxLen,256,MUIA_CycleChain,TRUE,End,
                Child,Label2("How should the members protect your data?"),
                Child,wizardkind=CycleObject,MUIA_Cycle_Entries,wizardkinds,MUIA_Cycle_Active,OZ_MIRROR,End,
                Child,TextObject,MUIA_Text_Contents,"A mirror needs at least two members. RAIDZ needs parity + 1.\nRedundancy is not a backup. One group is created by this wizard.\nAdvanced offers multiple groups, dRAID and extra pool properties.",End,
            End,
            Child,wizardpages[1]=VGroup,
                Child,TextObject,MUIA_Text_Contents,"Add each exact member path separately.\nUse blank, pre-provisioned files or explicit native partitions.\nThis wizard does not partition disks or discover free space.",End,
                Child,members,
                Child,wizardpath=StringObject,StringFrame,MUIA_String_MaxLen,256,MUIA_CycleChain,TRUE,End,
                Child,HGroup,Child,wizardadd=SimpleButton("Add member"),Child,wizardremove=SimpleButton("Remove selected"),End,
            End,
            Child,wizardpages[2]=VGroup,
                Child,TextObject,MUIA_Text_Contents,"Verify every path. Creating a pool writes labels and metadata.\nKeep other programs from writing these members.\nThe next button opens a separate confirmation; it is not a dry-run.",End,
                Child,review,
            End,
            Child,wizardnote=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Draft only",End,
            Child,HGroup,Child,wizardback=SimpleButton("< Back"),Child,wizardnext=SimpleButton("Next >"),Child,wizardcancel=SimpleButton("Cancel"),End,
        End,End;
}
static void init_wizard_notifications(void) {
    notify_button(wizard_button,EV_WIZARD_OPEN);notify_button(wizardnext,EV_WIZARD_NEXT);
    notify_button(wizardback,EV_WIZARD_BACK);notify_button(wizardcancel,EV_WIZARD_CLOSE);
    notify_button(wizardadd,EV_WIZARD_ADD);notify_button(wizardremove,EV_WIZARD_REMOVE);
    DoMethod(wizardwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,EV_WIZARD_CLOSE);
}
static void wizard_close(void) {
    set(wizardwin,MUIA_Window_Open,FALSE);set(win,MUIA_Window_Sleep,FALSE);
}
static int wizard_validate(void) {
    static struct oz_message draft;char why[256];
    memset(&draft,0,sizeof(draft));draft.magic=OZ_MAGIC;draft.version=OZ_VERSION;
    draft.msg.mn_Length=sizeof(draft);draft.operation=5;draft.flags=OZ_CONFIRMED;
    snprintf(draft.target,sizeof(draft.target),"%s",string(wizardname));draft.topology=wizardlayout;
    if(oz_validate(&draft,why,sizeof(why))!=OZ_OK){set(wizardnote,MUIA_Text_Contents,why);return 0;}
    return 1;
}
static void wizard_event(IPTR event) {
    switch(event){
    case EV_WIZARD_OPEN:
        if(closing||auth_worker||projectworker||gptworker){set(status,MUIA_Text_Contents,"Wait for the current request before creating a pool");break;}
        stop_auto_io();memset(&wizardlayout,0,sizeof(wizardlayout));wizardlayout.group_count=1;
        wizardlayout.groups[0].role=OZ_DATA;
        set(wizardname,MUIA_String_Contents,"");set(wizardpath,MUIA_String_Contents,"");
        set(wizardkind,MUIA_Cycle_Active,OZ_MIRROR);wizard_refresh();wizard_page(0);
        set(win,MUIA_Window_Sleep,TRUE);set(wizardwin,MUIA_Window_Open,TRUE);break;
    case EV_WIZARD_CLOSE:wizard_close();break;
    case EV_WIZARD_BACK:if(wizardstep)wizard_page(wizardstep-1);break;
    case EV_WIZARD_ADD:{
        const char *path=string(wizardpath);
        if(!*path){set(wizardnote,MUIA_Text_Contents,"Enter an exact member path first.");break;}
        if(wizardlayout.device_count>=OZ_MAX_DEVICES){set(wizardnote,MUIA_Text_Contents,"Member limit reached.");break;}
        unsigned i;for(i=0;i<wizardlayout.device_count;i++)if(!strcmp(path,wizardlayout.devices[i].path))break;
        if(i<wizardlayout.device_count){set(wizardnote,MUIA_Text_Contents,"That member is already in the list.");break;}
        snprintf(wizardlayout.devices[i].path,sizeof(wizardlayout.devices[i].path),"%s",path);
        wizardlayout.devices[i].group=0;wizardlayout.device_count++;
        set(wizardpath,MUIA_String_Contents,"");wizard_refresh();set(wizardnote,MUIA_Text_Contents,"Member added to draft; no device opened.");break;}
    case EV_WIZARD_REMOVE:{
        IPTR i=value(wizarddevices,MUIA_List_Active);if(i>=wizardlayout.device_count)break;
        memmove(&wizardlayout.devices[i],&wizardlayout.devices[i+1],(wizardlayout.device_count-i-1)*sizeof(wizardlayout.devices[0]));
        memset(&wizardlayout.devices[--wizardlayout.device_count],0,sizeof(wizardlayout.devices[0]));wizard_refresh();break;}
    case EV_WIZARD_NEXT:
        if(wizardstep==0){
            if(!string(wizardname)[0]){set(wizardnote,MUIA_Text_Contents,"Enter a pool name before continuing.");break;}
            wizardlayout.groups[0].layout=value(wizardkind,MUIA_Cycle_Active);wizard_page(1);break;
        }
        if(!wizard_validate())break;
        if(wizardstep==1){
            char line[320];DoMethod(wizardreview,MUIM_List_Clear);
            snprintf(line,sizeof(line),"Pool: %s",string(wizardname));DoMethod(wizardreview,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);
            DoMethod(wizardreview,MUIM_List_InsertSingle,(IPTR)wizardkinds[wizardlayout.groups[0].layout],MUIV_List_Insert_Bottom);
            DoMethod(wizardreview,MUIM_List_InsertSingle,(IPTR)"Sector alignment: automatic. Force: off.",MUIV_List_Insert_Bottom);
            for(unsigned i=0;i<wizardlayout.device_count;i++)DoMethod(wizardreview,MUIM_List_InsertSingle,(IPTR)wizardlayout.devices[i].path,MUIV_List_Insert_Bottom);
            wizard_page(2);break;
        }
        set(category,MUIA_Cycle_Active,1);select_category();
        for(unsigned i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==5){set(actions,MUIA_List_Active,i);select_operation();break;}
        pool_layout=wizardlayout;set(target,MUIA_String_Contents,string(wizardname));
        set(argument,MUIA_String_Contents,"");set(options,MUIA_String_Contents,"");
        set(recursive,MUIA_Selected,FALSE);set(force,MUIA_Selected,FALSE);set(dryrun,MUIA_Selected,FALSE);
        set(raw,MUIA_Selected,FALSE);set(resumable,MUIA_Selected,FALSE);
        wizard_close();send_request(0,0);break;
    }
}
