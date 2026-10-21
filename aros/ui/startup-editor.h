/* SPDX-License-Identifier: CDDL-1.0
 * Included by the single-owner native Manager event loop. */
#include "startup-settings.h"
#include "hostid-settings.h"
static Object *startupwin,*hostid_value,*hostid_button,*hostid_message;
static char hostid_feedback[256];
static void hostid_save(void)
{
    const char *text=string(hostid_value);uint32_t identity=0;
    int error=strlen(text)==8?0:EINVAL;
    for(unsigned i=0;!error&&i<8;i++){
        unsigned digit;
        if(text[i]>='0'&&text[i]<='9')digit=text[i]-'0';
        else if(text[i]>='a'&&text[i]<='f')digit=text[i]-'a'+10;
        else if(text[i]>='A'&&text[i]<='F')digit=text[i]-'A'+10;
        else {error=EINVAL;break;}
        identity=(identity<<4)|digit;
    }
    if(error||!identity){set(hostid_message,MUIA_Text_Contents,"Enter 8 hexadecimal digits; the ID must not be zero.");return;}
    if(MUI_Request(app,startupwin,0,(CONST_STRPTR)"Save OpenZFS host identity",
        (CONST_STRPTR)"_Save|_Cancel",(CONST_STRPTR)"Save host ID %08lx to ENVARC and ENV?\nChoose an ID unique to this machine.\nThe OpenZFS service must not be running.",(ULONG)identity)!=1)return;
    unsigned published=0;error=oz_hostid_settings_apply(identity,&published);
    if(!error)snprintf(hostid_feedback,sizeof(hostid_feedback),"Host ID %08lx saved to ENVARC and ENV.",(unsigned long)identity);
    else if(error==EBUSY&&!published)snprintf(hostid_feedback,sizeof(hostid_feedback),"Service active or settings transaction pending; identity not saved.");
    else if(error==EPERM&&!published)snprintf(hostid_feedback,sizeof(hostid_feedback),"Saving host identity requires SYSTEM ownership.");
    else snprintf(hostid_feedback,sizeof(hostid_feedback),"Save error %d; ENVARC saved=%s, ENV saved=%s",error,
        published&OZ_HOSTID_SAVED?"yes":"no",published&OZ_HOSTID_CURRENT?"yes":"no");
    set(hostid_message,MUIA_Text_Contents,hostid_feedback);logline(hostid_feedback);
}
static Object *startup_guid,*startup_pool,*startup_path,*startup_paths;
static Object *startup_enabled,*startup_ro,*startup_mount,*startup_message;
static Object *startup_load,*startup_use,*startup_save,*startup_add,*startup_remove;
static Object *startup_boot_save;
static struct oz_startup_settings startup_record;
static char startup_feedback[256];
static int startup_parse_guid(uint64_t *out)
{
    const char *s=string(startup_guid);uint64_t guid=0;
    if(strlen(s)!=16)return EINVAL;
    for(unsigned i=0;i<16;i++){
        unsigned digit;
        if(s[i]>='0'&&s[i]<='9')digit=s[i]-'0';
        else if(s[i]>='a'&&s[i]<='f')digit=s[i]-'a'+10;
        else if(s[i]>='A'&&s[i]<='F')digit=s[i]-'A'+10;
        else return EINVAL;
        guid=(guid<<4)|digit;
    }
    if(!guid)return EINVAL;
    *out=guid;return 0;
}
static void startup_refresh(void)
{
    DoMethod(startup_paths,MUIM_List_Clear);
    for(unsigned i=0;i<startup_record.count;i++)
        DoMethod(startup_paths,MUIM_List_InsertSingle,(IPTR)startup_record.paths[i],MUIV_List_Insert_Bottom);
}
static Object *make_startup_window(void)
{
    Object *paths=listview(&startup_paths);
    return startupwin=WindowObject,MUIA_Window_Title,"OpenZFS pool startup",MUIA_Window_ID,MAKE_ID('O','Z','B','T'),
        MUIA_Window_Width,620,MUIA_Window_Height,410,
        WindowContents,VGroup,
            Child,TextObject,MUIA_Text_Contents,"Pool startup settings - saving does not import a pool",End,
            Child,ColGroup(2),
                Child,Label2("Pool GUID (16 hex digits)"),Child,startup_guid=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,17,End,
                Child,Label2("Pool name"),Child,startup_pool=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,256,End,
            End,
            Child,HGroup,Child,startup_enabled=make_check(FALSE),Child,Label1("Enabled"),
                Child,startup_ro=make_check(TRUE),Child,Label1("Read only"),
                Child,startup_mount=make_check(FALSE),Child,Label1("Mount datasets"),End,
            Child,TextObject,MUIA_Text_Contents,"Explicit member paths (up to 32)",End,
            Child,paths,
            Child,HGroup,
                Child,startup_path=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,256,End,
                Child,startup_add=SimpleButton("Add path"),Child,startup_remove=SimpleButton("Remove"),End,
            Child,startup_message=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Load uses ENV; Save writes ENVARC and ENV",End,
            Child,HGroup,Child,startup_load=SimpleButton("Load"),Child,startup_use=SimpleButton("Use"),Child,startup_save=SimpleButton("Save"),
                Child,startup_boot_save=SimpleButton("Save for boot"),End,
            Child,HGroup,
                Child,Label2("Host identity (8 hex digits)"),
                Child,hostid_value=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,9,MUIA_FixWidth,90,End,
                Child,hostid_button=SimpleButton("Save host ID..."),End,
            Child,hostid_message=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,
                MUIA_Text_Contents,"SYSTEM only. Save before starting OpenZFS.",End,
        End,End;
}
static void startup_notify(void)
{
    notify_button(startup_load,EV_START_LOAD);notify_button(startup_use,EV_START_USE);
    notify_button(startup_save,EV_START_SAVE);notify_button(startup_add,EV_START_ADD);notify_button(startup_remove,EV_START_REMOVE);
    notify_button(startup_boot_save,EV_START_BOOT_SAVE);
    notify_button(hostid_button,EV_HOSTID_SAVE);
    set(startup_boot_save,MUIA_ShortHelp,"System owner only. Secure ENV/ENVARC records; does not install a boot command.");
    DoMethod(startupwin,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,EV_START_CLOSE);
}
static void startup_event(ULONG event)
{
    int error=0,recovered=0;unsigned published=0;uint64_t guid=0;
    if(event==EV_START_CLOSE){set(startupwin,MUIA_Window_Open,FALSE);return;}
    if(event==EV_START_ADD){
        const char *path=string(startup_path);
        if(!path[0]||strlen(path)>255||startup_record.count==OZ_STARTUP_PATHS)error=EINVAL;
        for(unsigned i=0;!error&&i<startup_record.count;i++)if(!strcmp(path,startup_record.paths[i]))error=EEXIST;
        if(!error){strcpy(startup_record.paths[startup_record.count++],path);startup_refresh();set(startup_path,MUIA_String_Contents,"");}
        snprintf(startup_feedback,sizeof(startup_feedback),error?"Path not added (errno %d)":"Path added",error);
    }else if(event==EV_START_REMOVE){
        IPTR index=value(startup_paths,MUIA_List_Active);
        if(index<startup_record.count){
            for(unsigned i=index;i+1<startup_record.count;i++)memcpy(startup_record.paths[i],startup_record.paths[i+1],256);
            memset(startup_record.paths[--startup_record.count],0,256);startup_refresh();
        }
        snprintf(startup_feedback,sizeof(startup_feedback),"%u member paths",startup_record.count);
    }else{
        error=startup_parse_guid(&guid);
        if(!error&&event==EV_START_LOAD){
            error=oz_startup_settings_current(guid,&startup_record,&recovered);
            if(!error){
                set(startup_pool,MUIA_String_Contents,startup_record.pool);
                set(startup_enabled,MUIA_Selected,!!(startup_record.flags&OZ_STARTUP_ENABLED));
                set(startup_ro,MUIA_Selected,!!(startup_record.flags&OZ_STARTUP_READONLY));
                set(startup_mount,MUIA_Selected,!!(startup_record.flags&OZ_STARTUP_MOUNT));startup_refresh();
            }
        }else if(!error){
            startup_record.guid=guid;snprintf(startup_record.pool,sizeof(startup_record.pool),"%s",string(startup_pool));
            startup_record.flags=(value(startup_enabled,MUIA_Selected)?OZ_STARTUP_ENABLED:0)|
                (value(startup_ro,MUIA_Selected)?OZ_STARTUP_READONLY:0)|(value(startup_mount,MUIA_Selected)?OZ_STARTUP_MOUNT:0);
            error=event==EV_START_BOOT_SAVE?oz_startup_settings_apply_trusted(&startup_record,1,&published):
                oz_startup_settings_apply(&startup_record,event==EV_START_SAVE,&published);
        }
        if(error)snprintf(startup_feedback,sizeof(startup_feedback),"Settings error %d; published ENV=%s ENVARC=%s",error,
            published&1?"yes":"no",published&2?"yes":"no");
        else if(event==EV_START_LOAD)snprintf(startup_feedback,sizeof(startup_feedback),"Loaded ENV settings%s",recovered?" from backup; recovery required":"");
        else if(event==EV_START_BOOT_SAVE)snprintf(startup_feedback,sizeof(startup_feedback),"Secure settings saved; boot command not installed");
        else snprintf(startup_feedback,sizeof(startup_feedback),"Settings %s; no pool imported",event==EV_START_SAVE?"saved to ENVARC and ENV":"applied to ENV");
    }
    set(startup_message,MUIA_Text_Contents,startup_feedback);logline(startup_feedback);
}
