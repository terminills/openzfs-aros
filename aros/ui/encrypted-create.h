/* Guided draft only. The normal op37 validator, confirmation and authenticated
 * worker remain the sole submission path. No key material enters this UI. */
static Object *cryptwin,*cryptname,*cryptformat,*cryptpath,*cryptnote,*cryptreview,*cryptcancel;
static const char *cryptformats[]={"Passphrase file (8-512 bytes on first line)",
    "Raw key file (exactly 32 bytes)","Hex key file (64 hex digits)",NULL};
static const char *cryptvalues[]={"passphrase","raw","hex"};
static Object *make_crypt_window(void)
{
    return cryptwin=WindowObject,MUIA_Window_Title,"Create encrypted filesystem",
        MUIA_Window_ID,MAKE_ID('O','Z','E','C'),MUIA_Window_Width,600,
        WindowContents,VGroup,
            Child,TextObject,MUIA_Text_Contents,"Create a new encryption root in an imported writable pool.\nThe pool's encryption feature must already be enabled.\nExisting datasets are not converted or overwritten.",End,
            Child,Label2("New filesystem (pool/name)"),
            Child,cryptname=StringObject,StringFrame,MUIA_String_MaxLen,256,MUIA_CycleChain,TRUE,End,
            Child,Label2("Key file format"),
            Child,cryptformat=CycleObject,MUIA_Cycle_Entries,cryptformats,End,
            Child,Label2("Authorized AROS key-file path (not the secret itself)"),
            Child,cryptpath=StringObject,StringFrame,MUIA_String_MaxLen,512,MUIA_CycleChain,TRUE,End,
            Child,TextObject,MUIA_Text_Contents,"Keep a secure backup of the key file. Losing it loses access.\nUses upstream encryption=on and default derivation iterations.\nThe key-file path is not saved as the dataset's keylocation.\nThe filesystem is created unmounted; load keys explicitly after restart.",End,
            Child,cryptnote=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Draft only. No storage has been changed.",End,
            Child,HGroup,Child,cryptreview=SimpleButton("Review creation..."),Child,cryptcancel=SimpleButton("Cancel"),End,
        End,End;
}
static void crypt_notify(void)
{
    notify_button(crypt_button,EV_CRYPT_OPEN);notify_button(cryptreview,EV_CRYPT_REVIEW);
    notify_button(cryptcancel,EV_CRYPT_CLOSE);
    DoMethod(cryptwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,EV_CRYPT_CLOSE);
}
static void crypt_close(void)
{
    set(cryptwin,MUIA_Window_Open,FALSE);set(win,MUIA_Window_Sleep,FALSE);
}
static void crypt_event(IPTR event)
{
    if(event==EV_CRYPT_CLOSE){crypt_close();return;}
    if(closing||auth_worker||projectworker||gptworker){set(status,MUIA_Text_Contents,"Wait for the current request before creating a filesystem");return;}
    if(event==EV_CRYPT_OPEN){
        stop_auto_io();set(cryptname,MUIA_String_Contents,"");set(cryptpath,MUIA_String_Contents,"");
        set(cryptformat,MUIA_Cycle_Active,0);
        set(cryptnote,MUIA_Text_Contents,"Draft only. No storage has been changed.");
        set(win,MUIA_Window_Sleep,TRUE);set(cryptwin,MUIA_Window_Open,TRUE);return;
    }
    if(event==EV_CRYPT_REVIEW){
        static struct oz_message draft;char why[256];
        IPTR format=value(cryptformat,MUIA_Cycle_Active);
        if(format>=3||!strchr(string(cryptname),'/')||!strchr(string(cryptpath),':')){
            set(cryptnote,MUIA_Text_Contents,"Enter pool/name and an AROS key-file path such as Work:Keys/private.key.");return;
        }
        memset(&draft,0,sizeof(draft));draft.magic=OZ_MAGIC;draft.version=OZ_VERSION;
        draft.msg.mn_Length=sizeof(draft);draft.operation=37;draft.flags=OZ_CONFIRMED;
        snprintf(draft.target,sizeof(draft.target),"%s",string(cryptname));
        snprintf(draft.argument,sizeof(draft.argument),"%s",string(cryptpath));
        snprintf(draft.options,sizeof(draft.options),"encryption=on,keyformat=%s",cryptvalues[format]);
        if(oz_validate(&draft,why,sizeof(why))!=OZ_OK){set(cryptnote,MUIA_Text_Contents,why);return;}
        crypt_close();set(view_mode,MUIA_Cycle_Active,1);apply_view_mode();
        set(category,MUIA_Cycle_Active,2);select_category();
        for(unsigned i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==37){set(actions,MUIA_List_Active,i);select_operation();break;}
        set(target,MUIA_String_Contents,draft.target);set(argument,MUIA_String_Contents,draft.argument);
        set(options,MUIA_String_Contents,draft.options);
        Object *checks[]={recursive,force,dryrun,raw,resumable};
        for(unsigned i=0;i<sizeof(checks)/sizeof(checks[0]);i++)set(checks[i],MUIA_Selected,FALSE);
        send_request(0,0);
    }
}
