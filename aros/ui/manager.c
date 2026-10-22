#include "api.h"
#include "admission-worker.h"
#include "project-worker.h"
#include "preferences.h"
#include <errno.h>
#include <exec/memory.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/intuition.h>
#include <proto/muimaster.h>
#include <proto/alib.h>
#include <libraries/mui.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "properties.h"
#include "iostat-rates.h"
#include "iostat-refresh.h"
#include "simple-pools.h"
static struct oz_rate_history io_history;
static struct oz_refresh io_refresh;
static Object *auto_iostat;
static struct oz_io_view sampled_view;

ULONG __stack=131072;
enum { EV_CATEGORY=100, EV_OPERATION, EV_SEND, EV_PREVIEW, EV_CONNECT, EV_CAPS,
       EV_SAVE, EV_TOPOLOGY, EV_CLEAR, EV_NEXT, EV_SAVE_RESPONSE, EV_PROPERTY,
       EV_GROUP_ADD, EV_GROUP_SELECT, EV_DEVICE_ADD, EV_DEVICE_REMOVE,
       EV_LAYOUT_CLEAR, EV_LAYOUT_APPLY, EV_LAYOUT_CLOSE, EV_USE_TOKEN,
       EV_PREFS_USE, EV_PREFS_SAVE, EV_AUTO_IO, EV_PART_OPEN, EV_PART_QUERY, EV_PART_CLOSE, EV_PART_IMPORT,
       EV_PART_LABEL, EV_PART_EXPAND, EV_START_OPEN, EV_START_CLOSE,
       EV_START_LOAD, EV_START_USE, EV_START_SAVE, EV_START_ADD, EV_START_REMOVE, EV_START_BOOT_SAVE, EV_PART_MAPPINGS, EV_PART_REMAP,
       EV_PART_MOUNT_PREVIEW, EV_PART_MOUNT_SAVE, EV_PART_EJECT, EV_ARGUMENT, EV_RUNTIME,
       EV_PART_SCAN, EV_PART_CHOOSE, EV_PART_CREATE, EV_VIEW_MODE,
       EV_WIZARD_OPEN, EV_WIZARD_NEXT, EV_WIZARD_BACK, EV_WIZARD_CLOSE,
       EV_WIZARD_ADD, EV_WIZARD_REMOVE, EV_HOSTID_SAVE,
       EV_CRYPT_OPEN, EV_CRYPT_REVIEW, EV_CRYPT_CLOSE };
static Object *wizard_button;
static Object *crypt_button;
static const char *view_modes[]={"Simple","Advanced",NULL};
static Object *view_mode,*main_group,*flags_group,*topology_group,*property_group,*log_group,*log_title,*fields_group;
static int simple_mode(void);
static const char *categories[]={"Service","Pools","Datasets","Snapshots","Replication","Encryption","Permissions","Advanced",NULL};
static const char *topologies[]={"stripe","mirror","raidz1","raidz2","raidz3","draid",NULL};
static Object *app,*win,*category,*actions,*target,*argument,*options,*status,*helptext,*output,*activity,*topology;
static Object *execute,*preview,*connect_button,*caps_button,*save,*build_topology,*clear;
static Object *target_label,*argument_label;
static Object *next_page,*save_response,*use_token;
static Object *prefs_use,*prefs_save;
static Object *runtime_check;
static Object *devices_button;
static Object *startup_button;
static char resume_token[sizeof(((struct oz_message *)0)->options)-sizeof("resume=")+1];
static Object *property,*property_value,*use_property,*recursive,*force,*dryrun,*raw,*resumable;
static struct oz_message last_request;
static struct oz_topology pool_layout,import_files,pool_add_layout;
static UQUAD next_cursor;
static char response_text[32768];
static struct oz_client client;
static struct oz_admission_worker *auth_worker;
static struct oz_project_worker *projectworker;
static struct oz_gpt_worker *gptworker;
static const char *auth_endpoint=OZ_ADMISSION_PORT;
static int closing;
static unsigned visible[128],visible_count,selected;
static unsigned displayed_category;
static char transcript[65536];
static size_t transcript_used;
static IPTR value(Object *o,ULONG attr){IPTR v=0;GetAttr(attr,o,&v);return v;}
static int simple_mode(void){return value(view_mode,MUIA_Cycle_Active)==OZ_UI_SIMPLE;}
static const char *string(Object *o){return (const char*)value(o,MUIA_String_Contents);}
static UQUAD io_now(void){ULONG seconds,micros;CurrentTime(&seconds,&micros);return (UQUAD)seconds*1000000+micros;}
static void stop_auto_io(void){oz_refresh_stop(&io_refresh);if(auto_iostat)set(auto_iostat,MUIA_Selected,FALSE);}
static void logline(const char *line) {
    size_t n=strlen(line);
    if(transcript_used+n+2>=sizeof(transcript)){memmove(transcript,transcript+32768,transcript_used-32768);transcript_used-=32768;}
    memcpy(transcript+transcript_used,line,n);transcript_used+=n;transcript[transcript_used++]='\n';transcript[transcript_used]=0;
    /* MUI list entries are single rows, not multiline text objects. Keep
     * the saved transcript intact but split and wrap its visible rows. */
    unsigned width=(unsigned)value(activity,MUIA_Width)/8;
    if(width<24)width=24;
    if(width>120)width=120;
    width-=3;
    while(*line){
        size_t remaining=strcspn(line,"\n");
        do {
            char row[128];size_t count=remaining>width?width:remaining;
            memcpy(row,line,count);row[count]=0;
            DoMethod(activity,MUIM_List_InsertSingle,(IPTR)row,MUIV_List_Insert_Bottom);
            if(value(activity,MUIA_List_Entries)>150)DoMethod(activity,MUIM_List_Remove,0);
            line+=count;remaining-=count;
        } while(remaining);
        if(*line=='\n')line++;
    }
    set(activity,MUIA_List_Active,MUIV_List_Active_Bottom);
}
static void save_preferences(int permanent) {
    IPTR c=value(category,MUIA_Cycle_Active),w=value(win,MUIA_Window_Width),h=value(win,MUIA_Window_Height);
    static char line[256];
    if(c>7||w<320||w>16384||h<200||h>16384){
        set(status,MUIA_Text_Contents,"Cannot save invalid window dimensions");return;
    }
    struct oz_preferences p={(uint16_t)c,(uint16_t)w,(uint16_t)h,(uint16_t)value(view_mode,MUIA_Cycle_Active)};
    int published=0,error=0;
    if(permanent){
        error=oz_preferences_store("ENVARC:OpenZFSManager.prefs",&p,&published);
        if(error){
            snprintf(line,sizeof(line),"%s (errno %d); session settings unchanged",
                published?"Preferences saved to ENVARC, cleanup failed":"Cannot save ENVARC preferences",error);
            set(status,MUIA_Text_Contents,line);logline(line);return;
        }
    }
    error=oz_preferences_store("ENV:OpenZFSManager.prefs",&p,&published);
    if(error)snprintf(line,sizeof(line),"%s%s (errno %d)",permanent?"ENVARC saved; ":"",
        published?"ENV saved, cleanup failed":"ENV preferences not saved",error);
    else snprintf(line,sizeof(line),"UI settings %s; request fields and secrets are not saved",
        permanent?"saved to ENVARC and ENV":"applied to ENV");
    set(status,MUIA_Text_Contents,line);logline(line);
}
static void show_response(const char *text) {
    static char copy[32768];char *line;snprintf(copy,sizeof(copy),"%s",text);
    snprintf(response_text,sizeof(response_text),"%s\n",text);
    DoMethod(output,MUIM_List_Clear);
    line=strtok(copy,"\n");while(line){
        unsigned width=(unsigned)value(output,MUIA_Width)/8;
        if(width<24)width=24;
        if(width>120)width=120;
        width-=3;
        while(strlen(line)>width){char part[128];unsigned cut=width;
            while(cut>width/2&&line[cut]!=' ')cut--;
            if(cut<=width/2)cut=width;
            memcpy(part,line,cut);part[cut]=0;DoMethod(output,MUIM_List_InsertSingle,(IPTR)part,MUIV_List_Insert_Bottom);
            line+=cut;while(*line==' ')line++;
        }
        DoMethod(output,MUIM_List_InsertSingle,(IPTR)line,MUIV_List_Insert_Bottom);line=strtok(NULL,"\n");}
}
static Object *options_label,*options_hint;
static void update_execute_label(void) {
    const char *unavailable=oz_operation_unavailable(oz_operations[selected].id);
    set(execute,MUIA_Disabled,unavailable!=NULL);
    set(execute,MUIA_Text_Contents,unavailable?"Unavailable on AROS":
        oz_request_mutates(oz_operations[selected].id,string(argument))?"Review change...":simple_mode()?"Show results":"Send request");
}
static void select_operation(void) {
    IPTR n=value(actions,MUIA_List_Active);
    if(n>=visible_count)return;
    selected=visible[n];
    if(simple_mode()) {
        set(options,MUIA_String_Contents,oz_operations[selected].id==43?"readonly=on":"");
        set(argument,MUIA_String_Contents,"");
        set(recursive,MUIA_Selected,oz_operations[selected].id==36);
        set(force,MUIA_Selected,FALSE);set(dryrun,MUIA_Selected,FALSE);
        set(raw,MUIA_Selected,FALSE);set(resumable,MUIA_Selected,FALSE);
    }
    if(oz_operations[selected].id!=35)stop_auto_io();
    set(auto_iostat,MUIA_Disabled,oz_operations[selected].id!=35);
    set(options_hint,MUIA_Text_Contents,oz_operations[selected].id==37?
        "Create: key=value,key=value. Omit to inherit.":
        "Options: see operation help. No commands or secrets.");
    set(options,MUIA_ShortHelp,oz_operations[selected].help);
    set(options_label,MUIA_Text_Contents,(oz_operations[selected].id==11||oz_operations[selected].id==13)?"New backing path":"Properties / options");
    if(oz_operations[selected].id==14)set(options_label,MUIA_Text_Contents,"guids=leaf,leaf,...");
    set(target_label,MUIA_Text_Contents,oz_operations[selected].target_label[0]?
        oz_operations[selected].target_label:"Target (unused)");
    set(argument_label,MUIA_Text_Contents,oz_operations[selected].argument_label[0]?
        oz_operations[selected].argument_label:"Argument (unused)");
    set(helptext,MUIA_Text_Contents,oz_operations[selected].help);
    set(helptext,MUIA_ShortHelp,oz_operations[selected].help);
    if(oz_operations[selected].id==37){
        set(argument_label,MUIA_Text_Contents,"Key file (optional)");
        set(helptext,MUIA_Text_Contents,"Create filesystem or volume; hover for options.");
    }
    if(oz_operations[selected].id==6)
        set(helptext,MUIA_Text_Contents,"Root: unmount first. Destroys the pool.");
    if(oz_operations[selected].id==26)
        set(helptext,MUIA_Text_Contents,"Sync pool. Force also updates clean labels.");
    if(oz_operations[selected].id==24)
        set(helptext,MUIA_Text_Contents,"Reopen pool devices; query status afterward.");
    if(oz_operations[selected].id==12||oz_operations[selected].id==15||oz_operations[selected].id==16)
        set(helptext,MUIA_Text_Contents,"Root: leaf GUID from status; no options.");
    update_execute_label();
    set(build_topology,MUIA_Text_Contents,oz_operations[selected].id==7?"Import paths...":"Pool builder...");
    if(oz_operations[selected].id==7) {
        set(argument_label,MUIA_Text_Contents,"Legacy config");
        set(helptext,MUIA_Text_Contents,"Use Import paths; choose read-only or read/write.");
    }
    if(simple_mode()) {
        unsigned id=oz_operations[selected].id;
        const char *hint=id==1?"Check whether the OpenZFS service is available.":
            id==3?"Show pools currently imported on this computer.":
            id==4?"Check a pool and its devices for errors.":
            id==5?"Use Create pool... to choose a name and devices.":
            id==35?"Show pool activity; enable refresh for live samples.":
            id==36?"List a pool's filesystems and their children.":
            id==37?"Create a filesystem named pool/name with inherited settings.":
            id==43?"Mount a filesystem read-only. Use Advanced for writable mounts.":
            "Unmount a filesystem; close files using it first.";
        set(helptext,MUIA_Text_Contents,hint);set(helptext,MUIA_ShortHelp,hint);
        DoMethod(main_group,MUIM_Group_InitChange);
        int has_target=oz_operations[selected].target_label[0]!=0;
        set(target_label,MUIA_ShowMe,has_target);set(target,MUIA_ShowMe,has_target);
        /* Mount still needs the user's DOS volume name. Hide only unused
         * fields, never a required backend parameter. */
        int has_argument=oz_operations[selected].argument_label[0]!=0;
        set(argument_label,MUIA_ShowMe,has_argument);set(argument,MUIA_ShowMe,has_argument);
        /* An empty ColGroup advertises a zero maximum width in Zune. Hide
         * the group itself so it cannot cap the entire result column. */
        set(fields_group,MUIA_ShowMe,has_target||has_argument);
        if(!has_target)set(target,MUIA_String_Contents,"");
        DoMethod(main_group,MUIM_Group_ExitChange);
    } else {
        DoMethod(main_group,MUIM_Group_InitChange);
        set(target_label,MUIA_ShowMe,TRUE);set(target,MUIA_ShowMe,TRUE);
        set(argument_label,MUIA_ShowMe,TRUE);set(argument,MUIA_ShowMe,TRUE);
        set(fields_group,MUIA_ShowMe,TRUE);
        DoMethod(main_group,MUIM_Group_ExitChange);
    }
}
static const char *simple_label(unsigned id) {
    switch(id){
    case 1:return "Check connection";case 3:return "List pools";
    case 4:return "Pool health";case 5:return "Create pool...";
    case 35:return "Pool activity";case 36:return "List filesystems";
    case 37:return "Create filesystem";case 43:return "Mount read-only";
    case 44:return "Unmount filesystem";default:return "";}
}
static int simple_operation(unsigned id) {
    return simple_label(id)[0]!=0;
}
static void select_category(void) {
    unsigned i,c=(unsigned)value(category,MUIA_Cycle_Active);
    displayed_category=c;
    DoMethod(actions,MUIM_List_Clear);visible_count=0;
    for(i=0;i<oz_operation_count;i++)if(simple_mode()?simple_operation(oz_operations[i].id):!strcmp(oz_operations[i].category,categories[c])) {
        char label[128];visible[visible_count++]=i;
        if(simple_mode())snprintf(label,sizeof(label),"%s",simple_label(oz_operations[i].id));
        else snprintf(label,sizeof(label),"%s%s%s",oz_operations[i].mutation?"* ":"  ",oz_operations[i].label,
            oz_operation_unavailable(oz_operations[i].id)?" [unavailable]":"");
        DoMethod(actions,MUIM_List_InsertSingle,(IPTR)label,MUIV_List_Insert_Bottom);
    }
    set(actions,MUIA_List_Active,0);select_operation();
}
static void apply_view_mode(void) {
    stop_auto_io();
    int advanced=!simple_mode();
    DoMethod(main_group,MUIM_Group_InitChange);
    Object *controls[]={category,options,options_label,options_hint,flags_group,
        topology_group,property_group,log_group,log_title,caps_button,save,use_token,runtime_check};
    for(unsigned i=0;i<sizeof(controls)/sizeof(controls[0]);i++)set(controls[i],MUIA_ShowMe,advanced);
    DoMethod(main_group,MUIM_Group_ExitChange);
    select_category();
}
static void fill_request(struct oz_message *m) {
    memset(m,0,sizeof(*m));m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=oz_operations[selected].id;
    m->flags=(value(recursive,MUIA_Selected)?OZ_RECURSIVE:0)|(value(force,MUIA_Selected)?OZ_FORCE:0)|
        (value(dryrun,MUIA_Selected)?OZ_DRYRUN:0)|(value(raw,MUIA_Selected)?OZ_RAW:0)|(value(resumable,MUIA_Selected)?OZ_RESUMABLE:0);
    snprintf(m->target,sizeof(m->target),"%s",string(target));
    snprintf(m->argument,sizeof(m->argument),"%s",string(argument));
    snprintf(m->options,sizeof(m->options),"%s",string(options));
    if(m->operation==5)m->topology=pool_layout;
    if(m->operation==9)m->topology=pool_add_layout;
    if(m->operation==7)m->topology=import_files;
    if(m->operation==11||m->operation==13) {
        m->topology.group_count=1;m->topology.device_count=1;
        m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
        if(strlen(string(options))<sizeof(m->topology.devices[0].path))
            strcpy(m->topology.devices[0].path,string(options));
        m->options[0]=0;
    }
}
static void send_request(ULONG operation,int preview_only) {
    /* Single event-loop owner. Keep large ABI/preview buffers off the small
     * shell-provided stack; __stack is not honored by every launch path. */
    static struct oz_message m;static char text[14000];char why[256],line[256];ULONG result;
    const struct oz_operation *op;
    if(closing||auth_worker||projectworker||gptworker){set(status,MUIA_Text_Contents,"Request still in flight; wait for its result");return;}
    fill_request(&m);if(operation){m.operation=operation;m.target[0]=m.argument[0]=m.options[0]=0;memset(&m.topology,0,sizeof(m.topology));}
    op=oz_find(m.operation);if(m.operation!=82&&m.operation!=76&&m.operation!=79&&m.operation!=80&&!(m.operation>=67&&m.operation<=69)&&
        (m.operation!=70||oz_request_mutates(m.operation,m.argument)))m.flags|=OZ_CONFIRMED; /* validation precedes confirmation; not submitted yet */
    result=oz_validate(&m,why,sizeof(why));
    if(strstr(m.options,"passphrase=")||strstr(m.options,"password=")) {
        show_response("Do not enter secrets in options. Use a keylocation reference; secure prompting belongs to the engine.");return;
    }
    if(result!=OZ_OK){set(status,MUIA_Text_Contents,oz_status(result));show_response(why);return;}
    snprintf(text,sizeof(text),"API 3 / %s\nTarget: %s\nArgument: %s\nProperties/options: %s\nRecursive=%s Force=%s Dry-run=%s Raw=%s Resumable=%s\n%s",
        op->key,m.target[0]?m.target:"(none)",m.argument[0]?m.argument:"(none)",m.options[0]?m.options:"(defaults)",
        m.flags&OZ_RECURSIVE?"yes":"no",m.flags&OZ_FORCE?"yes":"no",m.flags&OZ_DRYRUN?"yes":"no",m.flags&OZ_RAW?"yes":"no",m.flags&OZ_RESUMABLE?"yes":"no",
        oz_request_mutates(m.operation,m.argument)?"This operation changes storage or configuration.":"Read/query or stream operation.");
    if(m.topology.group_count){unsigned g,d;size_t n=strlen(text);
        n+=snprintf(text+n,sizeof(text)-n,"\nTyped topology: ashift=%lu\n",(unsigned long)m.topology.ashift);
        for(g=0;g<m.topology.group_count;g++){
            static const char *roles[]={"data","log","cache","spare","special","dedup"};
            n+=snprintf(text+n,sizeof(text)-n,"Group %u: %s / %s\n",g+1,roles[m.topology.groups[g].role],topologies[m.topology.groups[g].layout]);
            if(m.topology.groups[g].layout==OZ_DRAID)
                n+=snprintf(text+n,sizeof(text)-n,"  parity=%lu data=%lu (0=auto) distributed spares=%lu\n",
                    (unsigned long)m.topology.groups[g].draid_parity,(unsigned long)m.topology.groups[g].draid_data,
                    (unsigned long)m.topology.groups[g].draid_spares);
            for(d=0;d<m.topology.device_count;d++)if(m.topology.devices[d].group==g)
                n+=snprintf(text+n,sizeof(text)-n,"  %s\n",m.topology.devices[d].path);
        }
    }
    if(m.operation==7&&m.topology.device_count) {
        size_t n=strlen(text);
        n+=snprintf(text+n,sizeof(text)-n,"\nExplicit import paths (no automatic scan):\n");
        for(unsigned d=0;d<m.topology.device_count;d++)
            n+=snprintf(text+n,sizeof(text)-n,"  %s\n",m.topology.devices[d].path);
    }
    if(m.operation==42&&!strcmp(m.options,"received=on")) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nRemoves this property's local override.\nRestores the received value, if present; otherwise inherited/default.\nOther properties are unchanged.");
    }
    if(m.operation==32&&!strncmp(m.argument,"feature@",8)) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nFeature enablement is irreversible and may prevent use with older systems.\nEnabling a flag does not establish AROS support for every related facility.");
    }
    if(m.operation==47) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nFilesystem version upgrades are irreversible.\nOlder systems may lose access; this does not upgrade the pool format.");
    }
    if(m.operation==48) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nRewrites allocated blocks in the selected file range and can generate substantial pool I/O.\nphysical=on requires the pool physical_rewrite feature and reduces compatibility with older OpenZFS.");
    }
    if(m.operation==22&&!strcmp(m.argument,"discard")) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nDiscard removes this pool recovery point.\nSuccess starts reclamation; query pool.status for completion.");
    }
    if(m.operation==9) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nAdds NEW groups, not replicas of existing groups.\nRemoving added data groups later may not be supported.");
    }
    if(m.operation==10) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\n%s",!strcmp(m.argument,"cancel")?
            "Cancels this pool's active data-vdev evacuation.\nThe source device stays in the pool. Query status afterward.":
            "Removes the selected device or top-level group.\nData evacuation may be asynchronous. Acceptance is not completion.\nVerify the GUID and query pool.status afterward.");
    }
    if(m.operation==12) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nDetaching removes this device from the pool and reduces redundancy.\nVerify the leaf GUID against pool.status before submitting.");
    }
    if(m.operation==61&&(m.flags&OZ_FORCE)){
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nForced receive can discard local changes and conflicting snapshots.\nVerify the target and keep a backup before submitting.");
    }
    if(m.operation==78){
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nAbandons the saved receive; it cannot be resumed afterward.\nAn incomplete NEW filesystem is removed.\nFor an incremental receive, the original head and snapshots remain.");
    }
    if(m.operation==64){
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"%s",!strcmp(m.options,"inherit=on")?
            "\nThis filesystem will use its PARENT's encryption key.\nBoth current and parent keys must be loaded.\nKeep the parent key: the independent key will no longer unlock it.":
            "\nKeep a secure copy of the NEW key before submitting.\nThe OLD key will no longer unlock this encryption root.\nExisting snapshots of this encryption root use the replacement key too.");
    }
    if(m.operation==14) {
        size_t n=strlen(text);
        snprintf(text+n,sizeof(text)-n,"\nSplits the explicitly selected leaves into a separate pool.\nSource redundancy is reduced. Unmount source datasets first.\nThe destination stays exported; import it separately using its exact files.");
    }
    if(m.operation==7) {
        int ro,recover,rewind;
        if(oz_parse_import_options(m.options,&ro,&recover,&rewind)&&rewind) {
            size_t n=strlen(text);
            snprintf(text+n,sizeof(text)-n,"\n%s",ro?
                "Read-only checkpoint view: later changes remain on disk.":
                "WRITABLE CHECKPOINT REWIND PERMANENTLY DISCARDS ALL LATER POOL CHANGES.");
        }
    }
    show_response(text);
    if(preview_only){set(status,MUIA_Text_Contents,"Preview only - nothing submitted");return;}
    /* Long resume tokens cannot be useful unwrapped in a requester. Keep
     * the complete request in Response; explicitly identify the send here. */
    static char confirmation[1024];
    const char *confirm_text=text;
    if(m.operation==60&&strstr(m.options,"resume=")){
        snprintf(confirmation,sizeof(confirmation),"Submit token continuation send?\nSource: %.255s\nOutput: %.511s\nDry-run: %s\nFull token and options are shown in Response.\nCancel to review them before submitting.",
            m.target,m.argument,m.flags&OZ_DRYRUN?"yes":"no");
        confirm_text=confirmation;
    }
    if(m.operation==5&&m.topology.group_count)confirm_text=(m.flags&OZ_DRYRUN)?
        "Submit a dry-run request for the layout shown in Response?\nUnsupported dry-run is rejected, never changed to real execution.":
        "Create the pool using the exact member paths shown in Response?\nThis writes pool labels and metadata. This is NOT a dry-run.\nCancel if any member contains data you need to keep.";
    if((oz_request_mutates(m.operation,m.argument)||(m.operation==34&&strstr(m.options,"clear=on"))) && MUI_Request(app,win,0,(CONST_STRPTR)"Confirm OpenZFS request",(CONST_STRPTR)"_Submit|_Cancel",(CONST_STRPTR)"%s",m.operation==34?"Clear all queued OpenZFS events? This affects every event reader.":confirm_text)!=1) {
        set(status,MUIA_Text_Contents,"Cancelled - nothing submitted");return;
    }
    enum oz_route route=oz_request_route(&m);
    if(route==OZ_ROUTE_AUTHENTICATED) {
        int query=m.operation==79||m.operation==80||(m.operation>=67&&m.operation<=69);
        int error=oz_worker_start(auth_endpoint,&m,&auth_worker);
        if(error){snprintf(line,sizeof(line),"Cannot start authenticated %s: errno=%d",query?"query":"request",error);show_response(line);logline(line);}
        else {last_request=m;next_cursor=0;set(next_page,MUIA_Disabled,TRUE);
            set(status,MUIA_Text_Contents,m.operation==80?"Reading vdev property...":query?"Reading upstream permissions...":"Waiting for MuFS-authenticated admission...");
            snprintf(line,sizeof(line),"%s: authenticated worker started; no automatic retry",op->key);logline(line);}
        return;
    }
    if(route==OZ_ROUTE_PROJECT){
        int error=oz_project_worker_start(&m,&projectworker);
        if(error){snprintf(line,sizeof(line),"Cannot start project command: errno=%d",error);show_response(line);}
        else {last_request=m;next_cursor=0;set(next_page,MUIA_Disabled,TRUE);set(status,MUIA_Text_Contents,m.operation==76?"Reading your retained requests...":"Running upstream command with your identity...");}
        return;
    }
    if(route!=OZ_ROUTE_PUBLIC){
        snprintf(line,sizeof(line),"%s",route==OZ_ROUTE_UNAVAILABLE?oz_operation_unavailable(m.operation):"Operation has no audited transport route");
        set(status,MUIA_Text_Contents,oz_status(OZ_UNSUPPORTED));show_response(line);
        return;
    }
    result=oz_submit(&client,&m);
    if(result==OZ_OK){last_request=m;next_cursor=0;set(next_page,MUIA_Disabled,TRUE);}
    snprintf(line,sizeof(line),"%s: %s (request %lu)",op->key,oz_status(result),(unsigned long)client.pending);logline(line);
    set(status,MUIA_Text_Contents,result==OZ_OK?"Waiting for management service...":oz_status(result));
    if(result==OZ_NO_SERVICE)show_response("No OpenZFS management service is registered.\nStart C:OpenZFSService and choose Connect.\nRequest was not delivered. No storage was changed.");
}
static Object *listview(Object **list) {
    return ListviewObject, MUIA_Listview_List, *list=ListObject,
        InputListFrame, MUIA_List_ConstructHook,MUIV_List_ConstructHook_String,
        MUIA_List_DestructHook,MUIV_List_DestructHook_String, End, End;
}
static void notify_button(Object *o,ULONG event){DoMethod(o,MUIM_Notify,MUIA_Pressed,FALSE,(IPTR)app,2,MUIM_Application_ReturnID,event);}
static Object *make_check(int selected){Object *o=MUI_MakeObject(MUIO_Checkmark,(IPTR)"");if(o)set(o,MUIA_Selected,selected);return o;}
#include "pool_editor.h"
#include "pool-wizard.h"
#include "encrypted-create.h"
#include "partition-view.h"
#include "startup-editor.h"
int main(int argc,char **argv) {
    ULONG sigs=0;IPTR event;int quit=0;
    struct oz_preferences prefs;int recovered=0,prefs_error;
    const char *prefs_source="ENV";
    oz_preferences_default(&prefs);
    prefs_error=oz_preferences_load("ENV:OpenZFSManager.prefs",&prefs,&recovered);
    if(prefs_error==ENOENT){
        prefs_source="ENVARC";
        prefs_error=oz_preferences_load("ENVARC:OpenZFSManager.prefs",&prefs,&recovered);
    }
    Object *action_view=listview(&actions),*result_view=listview(&output),*log_view=listview(&activity);
    if(!oz_client_open(&client))return 20;
    if(argc==2&&!strcmp(argv[1],"TEST-NATIVE")){client.server_name=OZ_NATIVE_TEST_PORT;auth_endpoint=OZ_ADMISSION_TEST_PORT;}
    if(argc==2&&!strcmp(argv[1],"TEST-DELAY")) {
        BPTR marker=Lock((CONST_STRPTR)"SYS:openzfs-ui-test-only",ACCESS_READ);
        if(!marker){oz_client_close(&client);return 20;}
        UnLock(marker);client.server_name=OZ_NATIVE_TEST_PORT;
        auth_endpoint="OpenZFS.Management.Admission.Delayed.Test.v1";
    }
    Object *pool_window=make_pool_window();
    Object *partition_window=make_partition_window();
    Object *startup_window=make_startup_window();
    Object *wizard_window=make_wizard_window();
    Object *crypt_window=make_crypt_window();
    app=ApplicationObject,
        MUIA_Application_Title,"OpenZFS Manager",MUIA_Application_Version,"$VER: OpenZFS Manager 0.1 (18.09.2026)",
        MUIA_Application_Base,"OPENZFSMANAGER",
        SubWindow,pool_window,
        SubWindow,partition_window,
        SubWindow,startup_window,
        SubWindow,wizard_window,
        SubWindow,crypt_window,
        SubWindow,win=WindowObject,MUIA_Window_Title,"OpenZFS Manager - AROS",MUIA_Window_ID,MAKE_ID('O','Z','F','S'),
        MUIA_Window_Width,prefs.width,MUIA_Window_Height,prefs.height,
        WindowContents,main_group=VGroup,
            Child,HGroup,
                Child,TextObject,MUIA_Text_Contents,"\33bOpenZFS Manager",End,
                Child,view_mode=CycleObject,MUIA_Cycle_Entries,view_modes,MUIA_Cycle_Active,prefs.view_mode,End,
                Child,connect_button=SimpleButton("Connect"),Child,caps_button=SimpleButton("Capabilities"),
                Child,save=SimpleButton("Save activity log"),
                Child,devices_button=SimpleButton("Devices..."),
                Child,startup_button=SimpleButton("Startup..."),
            End,
            Child,status=TextObject,TextFrame,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Not connected - no storage state assumed",End,
            Child,HGroup,MUIA_InnerLeft,0,MUIA_InnerRight,0,
                Child,VGroup,MUIA_InnerLeft,0,MUIA_InnerRight,0,MUIA_HorizWeight,35,
                    Child,category=CycleObject,MUIA_Cycle_Entries,categories,MUIA_Cycle_Active,prefs.category,End,
                    Child,action_view,
                    Child,wizard_button=SimpleButton("Create pool..."),
                    Child,crypt_button=SimpleButton("Encrypted filesystem..."),
                    Child,TextObject,MUIA_Text_Contents,"* Requires confirmation",End,
                End,
                Child,VGroup,MUIA_InnerLeft,0,MUIA_InnerRight,0,MUIA_HorizWeight,65,
                    Child,TextObject,MUIA_Text_Contents,"\33bRequest editor",End,
                    Child,helptext=TextObject,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Select an operation",End,
                    Child,fields_group=ColGroup(2),
                        Child,target_label=Label2("Target / source"),Child,target=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,256,End,
                        Child,argument_label=Label2("Argument / destination"),Child,argument=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,512,End,
                        Child,options_label=TextObject,MUIA_Text_Contents,"Properties / options",End,Child,options=StringObject,StringFrame,MUIA_CycleChain,TRUE,MUIA_String_MaxLen,1024,End,
                    End,
                    Child,options_hint=TextObject,MUIA_Text_SetMin,FALSE,MUIA_Text_Contents,"Options: see operation help. No commands or secrets.",End,
                    Child,topology_group=HGroup,
                        Child,Label2("Pool topology"),Child,topology=CycleObject,MUIA_Cycle_Entries,topologies,End,
                        Child,build_topology=SimpleButton("Pool builder..."),Child,clear=SimpleButton("Clear fields"),
                    End,
                    Child,flags_group=HGroup,
                        Child,recursive=make_check(FALSE),Child,Label1("Recursive"),
                        Child,force=make_check(FALSE),Child,Label1("Force"),
                        Child,dryrun=make_check(TRUE),Child,Label1("Dry-run"),
                        Child,raw=make_check(FALSE),Child,Label1("Raw"),
                        Child,resumable=make_check(FALSE),Child,Label1("Resume"),
                        Child,HSpace(0),
                    End,
                    Child,HGroup,Child,preview=SimpleButton("Preview request"),Child,execute=SimpleButton("Send request"),End,
                    Child,HGroup,Child,auto_iostat=make_check(FALSE),Child,Label1("Auto I/O refresh (2s)"),Child,HSpace(0),End,
                    Child,HGroup,Child,TextObject,MUIA_Text_Contents,"\33bResponse",End,
                        Child,next_page=SimpleButton("Next page"),Child,use_token=SimpleButton("Use token"),Child,save_response=SimpleButton("Save response"),End,
                    Child,result_view,
                End,
            End,
            Child,property_group=HGroup,
                Child,PoplistObject,MUIA_Poplist_Array,property_names,
                    MUIA_Popstring_String,property=StringObject,StringFrame,MUIA_String_MaxLen,128,MUIA_String_Contents,"dataset:compression",End,
                    MUIA_Popstring_Button,PopButton(MUII_PopUp),End,
                Child,property_value=StringObject,StringFrame,MUIA_String_MaxLen,256,End,
                Child,use_property=SimpleButton("Use property"),
            End,
            Child,log_title=TextObject,MUIA_Text_Contents,"\33bActivity - unavailable is not an empty pool list",End,
            Child,log_group=VGroup,MUIA_VertWeight,25,Child,log_view,End,
            Child,HGroup,
                Child,TextObject,MUIA_Text_Contents,"View, category and window size",End,
                Child,prefs_use=SimpleButton("Use UI settings"),
                Child,runtime_check=SimpleButton("Runtime check"),
                Child,prefs_save=SimpleButton("Save UI settings"),
            End,
        End,End,End;
    if(!app){oz_client_close(&client);return 20;}
    DoMethod(view_mode,MUIM_Notify,MUIA_Cycle_Active,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_VIEW_MODE);
    DoMethod(win,MUIM_Notify,MUIA_Window_CloseRequest,TRUE,(IPTR)app,2,MUIM_Application_ReturnID,MUIV_Application_ReturnID_Quit);
    DoMethod(category,MUIM_Notify,MUIA_Cycle_Active,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_CATEGORY);
    DoMethod(actions,MUIM_Notify,MUIA_List_Active,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_OPERATION);
    DoMethod(argument,MUIM_Notify,MUIA_String_Contents,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_ARGUMENT);
    notify_button(execute,EV_SEND);notify_button(preview,EV_PREVIEW);notify_button(connect_button,EV_CONNECT);
    notify_button(caps_button,EV_CAPS);notify_button(save,EV_SAVE);notify_button(build_topology,EV_TOPOLOGY);notify_button(clear,EV_CLEAR);
    notify_button(next_page,EV_NEXT);notify_button(save_response,EV_SAVE_RESPONSE);set(next_page,MUIA_Disabled,TRUE);
    notify_button(use_token,EV_USE_TOKEN);set(use_token,MUIA_Disabled,TRUE);
    DoMethod(auto_iostat,MUIM_Notify,MUIA_Selected,MUIV_EveryTime,(IPTR)app,2,MUIM_Application_ReturnID,EV_AUTO_IO);
    set(auto_iostat,MUIA_ShortHelp,"Refresh the displayed I/O page every 2 seconds, retaining histogram options. Stops on query edits, page changes or errors. Pages are live samples, not an atomic pool snapshot. No overlapping requests.");
    notify_button(use_property,EV_PROPERTY);
    notify_button(prefs_use,EV_PREFS_USE);notify_button(prefs_save,EV_PREFS_SAVE);
    notify_button(runtime_check,EV_RUNTIME);
    set(runtime_check,MUIA_ShortHelp,"Run C:OpenZFSRuntimeCheck in the background. Checks runtime prerequisites only; does not open pools or storage devices.");
    startup_notify();notify_button(startup_button,EV_START_OPEN);
    init_pool_notifications();
    init_partition_notifications();notify_button(devices_button,EV_PART_OPEN);
    apply_view_mode();set(win,MUIA_Window_Open,TRUE);
    init_wizard_notifications();
    crypt_notify();
    if(!value(win,MUIA_Window_Open)){MUI_DisposeObject(app);oz_client_close(&client);return 20;}
    logline("OpenZFS Manager started. Existing private-format Work volume is not an OpenZFS pool.");
    if(prefs_error!=ENOENT){
        char line[256];
        if(prefs_error)snprintf(line,sizeof(line),"%s preferences rejected (errno %d); using defaults, saved files untouched",prefs_source,prefs_error);
        else snprintf(line,sizeof(line),"Loaded %s UI settings%s",prefs_source,recovered?" from transaction backup; recovery required before saving":"");
        logline(line);
    }
    send_request(1,0);
    while(!quit) {
        struct oz_message *reply;
        event=DoMethod(app,MUIM_Application_NewInput,(IPTR)&sigs);
        switch(event) {
        case MUIV_Application_ReturnID_Quit:stop_auto_io();closing=1;break;
        case EV_CATEGORY:if(value(category,MUIA_Cycle_Active)!=displayed_category)select_category();break;
        case EV_VIEW_MODE:apply_view_mode();break;
        case EV_WIZARD_OPEN:case EV_WIZARD_NEXT:case EV_WIZARD_BACK:
        case EV_WIZARD_CLOSE:case EV_WIZARD_ADD:case EV_WIZARD_REMOVE:wizard_event(event);break;
        case EV_CRYPT_OPEN:case EV_CRYPT_REVIEW:case EV_CRYPT_CLOSE:crypt_event(event);break;
        case EV_OPERATION:select_operation();break;
        case EV_ARGUMENT:update_execute_label();break;
        case EV_SEND:send_request(0,0);break;
        case EV_PREVIEW:send_request(0,1);break;
        case EV_RUNTIME:
            stop_auto_io();
            if(closing||client.pending||auth_worker||projectworker||gptworker||partworker||mappingworker)
                set(status,MUIA_Text_Contents,"Request still in flight; wait for its result");
            else {
                int error=oz_runtime_worker_start(&projectworker);
                set(status,MUIA_Text_Contents,error?"Cannot start runtime checker":"Checking runtime prerequisites; no storage devices opened...");
                if(!error)logline("Started C:OpenZFSRuntimeCheck (runtime prerequisites only)");
            }
            break;
        case EV_CONNECT:stop_auto_io();memset(&sampled_view,0,sizeof(sampled_view));oz_rate_reset(&io_history);send_request(1,0);break;
        case EV_AUTO_IO:
            if(!value(auto_iostat,MUIA_Selected))stop_auto_io();
            else if(closing||!oz_io_view_matches(&sampled_view,oz_operations[selected].id,
                string(target),string(argument),string(options))||
                !oz_refresh_arm(&io_refresh,sampled_view.pool,io_now())){
                stop_auto_io();set(status,MUIA_Text_Contents,"Run this I/O statistics query first");
            }else logline("Read-only I/O page refresh enabled (2 seconds; live page, not atomic pool snapshot)");
            break;
        case EV_CAPS:send_request(2,0);break;
        case EV_PREFS_USE:save_preferences(0);break;
        case EV_PREFS_SAVE:save_preferences(1);break;
        case EV_START_OPEN:if(!closing)set(startupwin,MUIA_Window_Open,TRUE);break;
        case EV_START_CLOSE:case EV_START_LOAD:case EV_START_USE:case EV_START_SAVE:
        case EV_START_ADD:case EV_START_REMOVE:case EV_START_BOOT_SAVE:startup_event(event);break;
        case EV_HOSTID_SAVE:if(!closing)hostid_save();break;
        case EV_PART_OPEN:if(!closing)set(partwin,MUIA_Window_Open,TRUE);break;
        case EV_PART_QUERY:partition_inspect();break;
        case EV_PART_SCAN:inventory_start();break;
        case EV_PART_CHOOSE:inventory_choose();break;
        case EV_PART_MAPPINGS:mapping_inspect();break;
        case EV_PART_REMAP:mapping_remap();break;
        case EV_PART_EJECT:mapping_eject();break;
        case EV_PART_MOUNT_PREVIEW:mapping_mountfile(0);break;
        case EV_PART_MOUNT_SAVE:mapping_mountfile(1);break;
        case EV_PART_IMPORT:partition_add_import();break;
        case EV_PART_CREATE:partition_create_draft();break;
        case EV_PART_LABEL:partition_gpt(OZ_GPT_LABEL);break;
        case EV_PART_EXPAND:partition_gpt(OZ_GPT_EXPAND);break;
        case EV_PART_CLOSE:set(partwin,MUIA_Window_Open,FALSE);break;
        case EV_TOPOLOGY:open_pool_editor();break;
        case EV_GROUP_ADD:case EV_GROUP_SELECT:case EV_DEVICE_ADD:case EV_DEVICE_REMOVE:
        case EV_LAYOUT_CLEAR:case EV_LAYOUT_APPLY:case EV_LAYOUT_CLOSE:pool_event(event);break;
        case EV_CLEAR:
            set(target,MUIA_String_Contents,"");set(argument,MUIA_String_Contents,"");set(options,MUIA_String_Contents,"");
            memset(&pool_layout,0,sizeof(pool_layout));
            memset(&pool_add_layout,0,sizeof(pool_add_layout));
            memset(&import_files,0,sizeof(import_files));
            break;
        case EV_USE_TOKEN:if(resume_token[0]&&!auth_worker&&!closing){
            char opt[1024];snprintf(opt,sizeof(opt),"resume=%s",resume_token);
            set(category,MUIA_Cycle_Active,4);select_category();
            for(unsigned i=0;i<visible_count;i++)if(oz_operations[visible[i]].id==60){set(actions,MUIA_List_Active,i);select_operation();break;}
            set(target,MUIA_String_Contents,"");set(argument,MUIA_String_Contents,"");
            set(options,MUIA_String_Contents,opt);
            set(recursive,MUIA_Selected,FALSE);set(force,MUIA_Selected,FALSE);
            set(raw,MUIA_Selected,FALSE);set(resumable,MUIA_Selected,FALSE);
            set(status,MUIA_Text_Contents,"Token copied: choose source snapshot and output; add from= for an incremental base");
            logline("Resume token copied to send options; nothing submitted");
        }break;
        case EV_PROPERTY:{
            const char *p=string(property);char name[128],pair[512];
            int is_vdev=!strncmp(p,"vdev:",5);
            int is_pool=!strncmp(p,"pool:",5),is_dataset=!strncmp(p,"dataset:",8);
            int vdev_operation=oz_operations[selected].id==80||oz_operations[selected].id==81;
            if((is_vdev&&!vdev_operation)||
                ((is_pool||is_dataset)&&vdev_operation)) {
                set(status,MUIA_Text_Contents,"Choose a property matching the selected operation");break;
            }
            /* Strip only catalogue prefixes, not a custom property's namespace. */
            const char *name_start=p+(is_vdev||is_pool?5:is_dataset?8:0);
            snprintf(name,sizeof(name),"%s",name_start);char *space=strchr(name,' ');if(space)*space=0;
            if(oz_operations[selected].id==80){set(options,MUIA_String_Contents,name);set(status,MUIA_Text_Contents,"Property selected; nothing submitted");break;}
            if(oz_operations[selected].id==81){
                if(strstr(p,"[read-only]")){set(status,MUIA_Text_Contents,"Read-only property: choose get vdev property");break;}
                if(snprintf(pair,sizeof(pair),"%s=%s",name,string(property_value))>=(int)sizeof(pair)){
                    set(status,MUIA_Text_Contents,"Property value is too long for the request");break;}
                set(options,MUIA_String_Contents,pair);set(status,MUIA_Text_Contents,"Property selected; review before submitting");break;
            }
            if(strstr(p,"[read-only]")){set(status,MUIA_Text_Contents,"Read-only property: select a get operation");set(argument,MUIA_String_Contents,name);}
            else if(!strcmp(oz_operations[selected].label,"get")){set(argument,MUIA_String_Contents,name);}
            else {snprintf(pair,sizeof(pair),"%s=%s",name,string(property_value));
                if(!strcmp(oz_operations[selected].label,"set"))set(argument,MUIA_String_Contents,pair);
                else {char all[1024];const char *separator=oz_operations[selected].id==37?",":";";snprintf(all,sizeof(all),"%s%s%s",string(options),string(options)[0]?separator:"",pair);set(options,MUIA_String_Contents,all);}}
            break;}
        case EV_NEXT:stop_auto_io();if(next_cursor&&!auth_worker){
            last_request.cursor=next_cursor;
            if(last_request.operation==33||last_request.operation==79||last_request.operation==80||
                (last_request.operation>=67&&last_request.operation<=69)) {
                int rc=oz_worker_start(auth_endpoint,&last_request,&auth_worker);
                set(status,MUIA_Text_Contents,rc?"Cannot start authenticated history page":"Loading authenticated history page...");
                if(!rc)set(next_page,MUIA_Disabled,TRUE);
            } else {ULONG rc=oz_submit(&client,&last_request);set(status,MUIA_Text_Contents,rc==OZ_OK?"Loading next page...":oz_status(rc));}
        }break;
        case EV_SAVE_RESPONSE:{FILE *f=fopen("T:OpenZFS-Response.txt","w");if(f){size_t n=fwrite(response_text,1,strlen(response_text),f);int rc=fclose(f);set(status,MUIA_Text_Contents,n==strlen(response_text)&&!rc?"Saved T:OpenZFS-Response.txt":"Response write failed");}else set(status,MUIA_Text_Contents,"Cannot open response file");break;}
        case EV_SAVE:{FILE *f=fopen("T:OpenZFS-Manager.log","w");if(f){size_t n=fwrite(transcript,1,transcript_used,f);int rc=fclose(f);set(status,MUIA_Text_Contents,n==transcript_used&&!rc?"Saved T:OpenZFS-Manager.log":"Log write failed");}else set(status,MUIA_Text_Contents,"Cannot open T:OpenZFS-Manager.log");break;}
        }
        /* Closing keeps the process, task and outstanding packets alive.
         * No pthread_cancel or timeout can make borrowed DOS storage safe. */
        struct oz_message *auth_reply=NULL;
        if(projectworker){
            static struct oz_message response;
            int error=oz_project_worker_poll(projectworker,&response);
            if(!error){show_response(response.detail);logline(response.detail);set(status,MUIA_Text_Contents,
                response.operation?oz_status(response.status):response.status==OZ_OK?
                "Runtime prerequisites passed; storage health not tested":"Runtime check failed; see diagnostics (not a pool-health result)");
                if(response.operation==76&&response.status==OZ_OK){
                    auth_reply=AllocVec(sizeof(*auth_reply),MEMF_ANY);
                    if(auth_reply)*auth_reply=response;
                }
                if(!oz_project_worker_free(projectworker))projectworker=NULL;
            }else if(error!=EAGAIN)set(status,MUIA_Text_Contents,"Project worker join failed; keeping request alive");
        }
        if(auth_worker) {
            static struct oz_message response;struct oz_ticket ticket;
            int operation_error=0,release_error=0;
            int error=oz_worker_poll(auth_worker,&response,&ticket,&operation_error,&release_error);
            if(!error) {
                char line[256];
                snprintf(line,sizeof(line),"Authenticated ticket %llu:%llu: transport errno=%d, release errno=%d",
                    (unsigned long long)ticket.epoch,(unsigned long long)ticket.number,operation_error,release_error);
                logline(line);
                if(operation_error){show_response(line);set(status,MUIA_Text_Contents,"Admission unavailable or denied; request not automatically retried");}
                else {auth_reply=AllocVec(sizeof(*auth_reply),MEMF_ANY);if(auth_reply)*auth_reply=response;
                    else {show_response(response.detail);set(status,MUIA_Text_Contents,oz_status(response.status));}}
                if(!oz_worker_free(auth_worker))auth_worker=NULL;
            } else if(error!=EAGAIN) {
                set(status,MUIA_Text_Contents,"Worker drain error; retaining outstanding request storage");
            }
        }
        while((reply=auth_reply?auth_reply:oz_poll(&client))){
            auth_reply=NULL;
            char line[1024];unsigned i;
            static char rendered[32768];
            resume_token[0]=0;
            if(reply->status==OZ_OK&&reply->operation==40&&reply->row_count==1&&
                !strcmp(reply->rows[0].name,"receive_resume_token")){
                const char *token=!strcmp(reply->rows[0].kind,"property-long")?reply->detail:reply->rows[0].value;
                /* Never silently truncate a token to fit the request ABI.
                 * Full checksum/GUID/authority validation remains backend-owned. */
                if(!strncmp(token,"1-",2)&&strlen(token)+sizeof("resume=")<=sizeof(last_request.options))
                    snprintf(resume_token,sizeof(resume_token),"%s",token);
            }
            set(use_token,MUIA_Disabled,!resume_token[0]);
            snprintf(line,sizeof(line),"Response %lu: %s",(unsigned long)reply->id,oz_status(reply->status));logline(line);
            /* Authenticated operation replies do not carry the hello
             * capability snapshot. Missing metadata is not engine failure. */
            set(status,MUIA_Text_Contents,reply->operation==1&&reply->status==OZ_OK&&!reply->engine_available?
                "Service connected - OpenZFS engine unavailable":oz_status(reply->status));
            snprintf(rendered,sizeof(rendered),"%s\n",reply->detail);
            if(reply->status!=OZ_OK){oz_rate_reset(&io_history);memset(&sampled_view,0,sizeof(sampled_view));stop_auto_io();}
            if(reply->operation==35&&reply->status==OZ_OK){
                snprintf(sampled_view.pool,sizeof(sampled_view.pool),"%s",reply->target);
                snprintf(sampled_view.options,sizeof(sampled_view.options),"%s",reply->options);
                sampled_view.cursor=reply->cursor;
            }
            for(i=0;i<reply->row_count;i++) {
                struct oz_row *r=&reply->rows[i];
                snprintf(line,sizeof(line),"%.23s | %.159s | %.47s | %.255s",r->kind,r->name,r->state,r->value);
                size_t used=strlen(rendered);
                snprintf(rendered+used,sizeof(rendered)-used,"%s\n  parent=%.159s",line,r->parent);
                /* Metadata rows do not define space/progress counters. Zero
                 * initialization is not a measurement of bookmark capacity. */
                if(!strcmp(r->kind,"io-latency")||!strcmp(r->kind,"io-size")) {
                    used=strlen(rendered);
                    if(!strcmp(r->state,"UNAVAILABLE"))snprintf(rendered+used,sizeof(rendered)-used," histogram unavailable");
                    else snprintf(rendered+used,sizeof(rendered)-used," count=%llu bounds=%llu..%llu %s",
                        (unsigned long long)r->used,(unsigned long long)r->available,(unsigned long long)r->total,
                        !strcmp(r->kind,"io-size")?"bytes":"ns");
                } else if(!strcmp(r->kind,"io-bytes")||!strcmp(r->kind,"io-ops")) {
                    used=strlen(rendered);
                    if(!strcmp(r->state,"UNAVAILABLE"))
                        snprintf(rendered+used,sizeof(rendered)-used," runtime counters unavailable");
                    else snprintf(rendered+used,sizeof(rendered)-used,
                        " cumulative read=%llu write=%llu %s; elapsed=%llu ns",
                        (unsigned long long)r->used,(unsigned long long)r->available,
                        !strcmp(r->kind,"io-bytes")?"bytes":"operations",(unsigned long long)r->total);
                    if(reply->operation==35&&reply->status==OZ_OK){
                        char rate[256];oz_rate_format(&io_history,reply->target,r,rate,sizeof(rate));
                        used=strlen(rendered);snprintf(rendered+used,sizeof(rendered)-used,"\n  %s",rate);
                    }
                } else if(!strcmp(r->kind,"accounting")) {
                    used=strlen(rendered);
                    snprintf(rendered+used,sizeof(rendered)-used," value=%llu",(unsigned long long)r->used);
                    if(!strcmp(r->state,"used")||!strcmp(r->state,"objects")){
                        used=strlen(rendered);snprintf(rendered+used,sizeof(rendered)-used," default-quota=%llu",(unsigned long long)r->total);
                    }
                } else if(!strcmp(r->kind,"history")) {
                    used=strlen(rendered);
                    snprintf(rendered+used,sizeof(rendered)-used," time=%llu uid=%llu txg=%llu",
                        (unsigned long long)r->used,(unsigned long long)r->available,(unsigned long long)r->total);
                } else if(!strcmp(r->kind,"history.raw")) {
                    used=strlen(rendered);snprintf(rendered+used,sizeof(rendered)-used," bytes=%llu",(unsigned long long)r->total);
                } else if(!strcmp(r->kind,"initialize")&&
                    strstr(r->value,"stored-only=readonly counters=unavailable")) {
                    /* Persisted state is real; zero-filled runtime counters
                     * are unavailable, not a measured 0% or zero capacity. */
                    used=strlen(rendered);snprintf(rendered+used,sizeof(rendered)-used,
                        " runtime progress unavailable (stored state above)");
                } else if(strcmp(r->kind,"bookmark")&&strcmp(r->kind,"property")&&strcmp(r->kind,"property-long")&&
                    strcmp(r->kind,"hold")&&strcmp(r->kind,"conflict")&&strcmp(r->kind,"permission")&&strcmp(r->kind,"admission-ticket")&&strcmp(r->kind,"service-worker")) {
                    used=strlen(rendered);
                    snprintf(rendered+used,sizeof(rendered)-used," used=%llu available=%llu total=%llu progress=%lu/1000",
                        (unsigned long long)r->used,(unsigned long long)r->available,
                        (unsigned long long)r->total,(unsigned long)r->progress_permille);
                }
                used=strlen(rendered);snprintf(rendered+used,sizeof(rendered)-used,"\n");
            }
            /* Rows and their numeric metadata need the same wrapping as the
             * prose detail. Direct List inserts clipped the error counters. */
            show_response(rendered);
            if(simple_mode()&&reply->operation==1&&reply->status==OZ_OK){
                show_response(reply->engine_available?
                    "Connected to the OpenZFS service.\nOpenZFS engine is available.\nChoose List pools to inspect imported pools.\nConnection alone does not verify storage health.\nUse Advanced and Connect for full capabilities.":
                    "Connected to the management service.\nThe OpenZFS engine is unavailable.\nPool operations are not ready.\nUse Advanced and Connect for diagnostic details.");
                /* Save response retains the complete backend reply even when
                 * the novice view displays only the connection summary. */
                snprintf(response_text,sizeof(response_text),"%s\n",rendered);
            }
            if(simple_mode()&&reply->operation==3&&reply->status==OZ_OK){
                static char summary[32768];
                snprintf(summary,sizeof(summary),"%lu imported pool(s) on this page.\n",
                    (unsigned long)reply->row_count);
                for(unsigned row=0;row<reply->row_count&&row<OZ_MAX_ROWS;row++){
                    struct oz_row *r=&reply->rows[row];
                    if(strcmp(r->kind,"pool"))continue;
                    size_t used=strlen(summary);
                    oz_simple_pool_row(summary+used,sizeof(summary)-used,r->name,r->state,
                        r->used,r->available,r->total);
                }
                size_t used=strlen(summary);
                snprintf(summary+used,sizeof(summary)-used,
                    "\nRaw pool capacity; usable dataset space differs.\n%s",
                    reply->next_cursor?"More pools are available: choose Next page.":"");
                show_response(summary);
                /* Saving retains the exact detailed response, not this display summary. */
                snprintf(response_text,sizeof(response_text),"%s\n",rendered);
            }
            if(reply->job_id){snprintf(line,sizeof(line),"Background job: %llu (use Advanced/jobs)",(unsigned long long)reply->job_id);logline(line);}
            next_cursor=reply->next_cursor;set(next_page,MUIA_Disabled,!next_cursor);
            oz_message_free(reply);
        }
        partition_poll();
        inventory_poll();
        mapping_poll();
        partition_gpt_poll();
        if(io_refresh.armed){
            if(closing||!oz_io_view_matches(&sampled_view,oz_operations[selected].id,
                string(target),string(argument),string(options)))stop_auto_io();
            else if(oz_refresh_due(&io_refresh,io_now(),client.pending||auth_worker||projectworker||gptworker||partworker||mappingworker)){
                static struct oz_message poll;
                oz_io_view_request(&sampled_view,&poll);
                ULONG rc=oz_submit(&client,&poll);
                if(rc!=OZ_OK){stop_auto_io();set(status,MUIA_Text_Contents,oz_status(rc));}
                else {last_request=poll;logline("Automatic read-only pool.iostat request sent");}
            }
        }
        if(SetSignal(0,SIGBREAKF_CTRL_C)&SIGBREAKF_CTRL_C)closing=1;
        if(closing){
            if(auth_worker||projectworker||partworker||gptworker||mappingworker||inventoryworker)set(status,MUIA_Text_Contents,"Closing: waiting for request to drain; do not terminate task");
            else quit=1;
        }
        Delay(1); /* yield to AROS; asynchronous replies never block Zune input */
    }
    set(win,MUIA_Window_Open,FALSE);MUI_DisposeObject(app);oz_client_close(&client);
    puts("OpenZFS Manager closed; authenticated worker drained");fflush(stdout);return 0;
}
