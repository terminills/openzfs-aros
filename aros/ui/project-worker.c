/* SPDX-License-Identifier: CDDL-1.0 */
#include "project-worker.h"
#include "admission-api.h"
#include "../cli/process.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <pthread.h>
#include <proto/dos.h>

static int command_capture(struct oz_message *,char **);

int oz_clone_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=52||
       m->flags!=OZ_CONFIRMED||m->cursor||m->job_id||!m->options[0])return EINVAL;
    char *args[39]={"C:zfs","clone","-u"};unsigned count=3,properties=0;
    char *keys[16];size_t lengths[16];
    strcpy(options,m->options);
    for(char *item=options;*item;){
        char *end=strchr(item,';');if(end)*end=0;
        char *equals=strchr(item,'=');
        if(!equals||equals==item||!equals[1]||item[0]=='-'||properties==16)return EINVAL;
        size_t length=(size_t)(equals-item);
        for(unsigned i=0;i<properties;i++)
            if(lengths[i]==length&&!memcmp(keys[i],item,length))return EINVAL;
        keys[properties]=item;lengths[properties++]=length;
        args[count++]="-o";args[count++]=item;
        if(!end)break;
        item=end+1;if(!*item)return EINVAL;
    }
    /* Exact argv, not a shell command. libzfs validates property types and
     * upstream clone enforces the inherited caller's permissions. -u keeps
     * the existing explicit-mount workflow; no implicit parent creation. */
    args[count++]="--";args[count++]=m->target;args[count++]=m->argument;
    args[count]=NULL;
    return command_capture(m,args);
}

static int pool_errors_execute(struct oz_message *m)
{
    char why[256];
    if(oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=82||
       m->flags||m->argument[0]||m->options[0]||m->cursor||m->job_id)return EINVAL;
    char *args[]={"C:zpool","status","-v","--",m->target,NULL};
    return command_capture(m,args);
}

static int vdev_set_execute(struct oz_message *m)
{
    char why[256];
    if(oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=81||
       m->flags!=OZ_CONFIRMED||m->cursor||m->job_id)return EINVAL;
    if(!m->argument[0]||strspn(m->argument,"0123456789")!=strlen(m->argument))return EINVAL;
    errno=0;unsigned long long guid=strtoull(m->argument,NULL,10);
    if(errno||!guid)return EINVAL;
    const char *equals=strchr(m->options,'=');
    if(!equals||equals==m->options||m->options[0]=='-')return EINVAL;
    /* One exact argv element, never shell syntax. Upstream parses types,
     * names, read-only properties and enforces caller/pool authorization.
     * The native ioctl boundary validates retained device-path admission;
     * ordinary file path changes remain unsupported. */
    char *args[]={"C:zpool","set",m->options,m->target,m->argument,NULL};
    return command_capture(m,args);
}

static int cancel_execute(struct oz_message *m)
{
    char why[256];
    if(oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=77||
       m->flags!=OZ_CONFIRMED||m->argument[0]||m->options[0]||m->cursor||m->job_id)
        return EINVAL;
    const char *p=m->target;
    uint64_t values[2]={0,0};
    for(unsigned i=0;i<2;i++) {
        if(*p<'0'||*p>'9')return EINVAL;
        while(*p>='0'&&*p<='9') {
            unsigned digit=(unsigned)(*p++-'0');
            if(values[i]>(UINT64_MAX-digit)/10)return EINVAL;
            values[i]=values[i]*10+digit;
        }
        if(!values[i])return EINVAL;
        if(i==0) {if(*p++!=':')return EINVAL;}
        else if(*p)return EINVAL;
    }
    struct oz_ticket ticket={values[0],values[1]};
    int error=oz_admission_cancel(OZ_ADMISSION_PORT,&ticket);
    if(!error) {
        m->status=OZ_OK;
        snprintf(m->detail,sizeof(m->detail),"Queued request %s cancelled before execution. Its result remains available until released.",m->target);
    }
    return error;
}

static int events_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=34||
       m->argument[0]||(m->flags&~OZ_CONFIRMED))return EINVAL;
    char *args[8]={"C:zpool","events"};unsigned count=2,seen=0;
    strcpy(options,m->options);
    for(char *p=options;*p;){
        char *end=strchr(p,';');if(end)*end=0;
        unsigned bit;char *flag;
        if(!strcmp(p,"verbose=on")){bit=1;flag="-v";}
        else if(!strcmp(p,"scripted=on")){bit=2;flag="-H";}
        else if(!strcmp(p,"clear=on")){bit=4;flag="-c";}
        else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;args[count++]=flag;
        if(!end)break;
        p=end+1;if(!*p)return EINVAL;
    }
    if((seen&4)&&(!(m->flags&OZ_CONFIRMED)||seen!=4||m->target[0]))return EINVAL;
    args[count++]="--";
    if(m->target[0])args[count++]=m->target;
    args[count]=NULL;
    return command_capture(m,args);
}

static int redact_execute(struct oz_message *m)
{
    char why[256],snapshots[sizeof(m->options)];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=59||
       m->flags!=OZ_CONFIRMED||strncmp(m->options,"snapshots=",10)||
       !m->options[10])return EINVAL;
    strcpy(snapshots,m->options+10);
    char *args[37]={"C:zfs","redact",m->target,m->argument};unsigned count=4;
    for(char *p=snapshots;*p;){
        char *end=strchr(p,',');if(end)*end=0;
        if(count==36||!*p||!strchr(p,'@')||strchr(p,';'))return EINVAL;
        for(unsigned i=4;i<count;i++)if(!strcmp(args[i],p))return EINVAL;
        args[count++]=p;
        if(!end)break;
        p=end+1;if(!*p)return EINVAL;
    }
    args[count]=NULL;return command_capture(m,args);
}

static int program_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)],numbers[2][24];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=75||
       m->flags!=OZ_CONFIRMED||!strcmp(m->argument,"-"))return EINVAL;
    char *args[32]={"C:zfs","program"},*scriptargs[16];
    unsigned count=2,nscript=0,seen=0;
    strcpy(options,m->options);
    for(char *p=options;*p;){
        char *end=strchr(p,';');if(end)*end=0;
        if(!strncmp(p,"arg=",4)){
            if(nscript==16)return EINVAL;
            scriptargs[nscript++]=p+4;
        }else if(!strcmp(p,"sync=on")||!strcmp(p,"sync=off")){
            if(seen&1)return EINVAL;
            seen|=1;
            if(!strcmp(p,"sync=off"))args[count++]="-n";
        }else if(!strcmp(p,"json=on")||!strcmp(p,"json=off")){
            if(seen&2)return EINVAL;
            seen|=2;
            if(!strcmp(p,"json=on"))args[count++]="-j";
        }else{
            unsigned index;const char *value;
            if(!strncmp(p,"instructions=",13)){index=0;value=p+13;}
            else if(!strncmp(p,"memory=",7)){index=1;value=p+7;}
            else return EINVAL;
            if((seen&(4U<<index))||!*value)return EINVAL;
            seen|=4U<<index;uint64_t number=0;
            for(;*value;value++){
                if(*value<'0'||*value>'9'||number>(UINT64_MAX-(unsigned)(*value-'0'))/10)return EINVAL;
                number=number*10+(unsigned)(*value-'0');
            }
            if(!number)return EINVAL;
            snprintf(numbers[index],sizeof(numbers[index]),"%llu",(unsigned long long)number);
            args[count++]=index?"-m":"-t";args[count++]=numbers[index];
        }
        if(!end)break;
        p=end+1;if(!*p)return EINVAL;
    }
    args[count++]="--";args[count++]=m->target;args[count++]=m->argument;
    for(unsigned i=0;i<nscript;i++)args[count++]=scriptargs[i];
    args[count]=NULL;return command_capture(m,args);
}

static int labelclear_execute(struct oz_message *m)
{
    char why[256];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=29||
       !(m->flags&OZ_CONFIRMED)||(m->flags&~(OZ_CONFIRMED|OZ_FORCE))||
       m->argument[0]||m->options[0])return EINVAL;
    char *args[6]={"C:zpool","labelclear"};unsigned count=2;
    if(m->flags&OZ_FORCE)args[count++]="-f";
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int upgrade_execute(struct oz_message *m)
{
    char why[256],number[16];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=25||
       m->flags!=OZ_CONFIRMED||m->argument[0])return EINVAL;
    char *args[8]={"C:zpool","upgrade"};unsigned count=2;
    if(m->options[0]){
        if(strncmp(m->options,"version=",8)||!m->options[8])return EINVAL;
        unsigned version=0;
        for(const char *p=m->options+8;*p;p++){
            if(*p<'0'||*p>'9'||version>500)return EINVAL;
            version=version*10+(unsigned)(*p-'0');
        }
        if(!version||(version>28&&version!=5000))return EINVAL;
        snprintf(number,sizeof(number),"%u",version);
        args[count++]="-V";args[count++]=number;
    }
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int dataset_upgrade_execute(struct oz_message *m)
{
    char why[256],number[16];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=47||
       !(m->flags&OZ_CONFIRMED)||(m->flags&~(OZ_CONFIRMED|OZ_RECURSIVE))||
       m->argument[0])return EINVAL;
    char *args[9]={"C:zfs","upgrade"};unsigned count=2;
    if(m->flags&OZ_RECURSIVE)args[count++]="-r";
    if(m->options[0]){
        if(strncmp(m->options,"version=",8)||!m->options[8])return EINVAL;
        unsigned version=0;
        for(const char *p=m->options+8;*p;p++){
            if(*p<'0'||*p>'9'||version>5)return EINVAL;
            version=version*10+(unsigned)(*p-'0');
        }
        /* OpenZFS 2.4.4 supports ZPL versions 1..5.  Reject future or
         * malformed values here rather than turning options into argv. */
        if(!version||version>5)return EINVAL;
        snprintf(number,sizeof(number),"%u",version);
        args[count++]="-V";args[count++]=number;
    }
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int dataset_rewrite_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)],offset[24],length[24];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=48||
       !(m->flags&OZ_CONFIRMED)||(m->flags&~(OZ_CONFIRMED|OZ_RECURSIVE))||
       m->argument[0]||!strchr(m->target,':'))return EINVAL;
    char *args[14]={"C:zfs","rewrite"};unsigned count=2,seen=0;
    int physical=0,verbose=0,xdev=0;
    strcpy(options,m->options);
    for(char *p=options;*p;){
        char *end=strchr(p,';');if(end)*end=0;
        unsigned bit=0;const char *value=NULL;char *number=NULL;
        if(!strcmp(p,"physical=on")||!strcmp(p,"physical=off"))
            {bit=1;physical=!strcmp(p,"physical=on");}
        else if(!strcmp(p,"verbose=on")||!strcmp(p,"verbose=off"))
            {bit=2;verbose=!strcmp(p,"verbose=on");}
        else if(!strcmp(p,"xdev=on")||!strcmp(p,"xdev=off"))
            {bit=4;xdev=!strcmp(p,"xdev=on");}
        else if(!strncmp(p,"offset=",7)){bit=8;value=p+7;number=offset;}
        else if(!strncmp(p,"length=",7)){bit=16;value=p+7;number=length;}
        else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;
        if(value){
            uint64_t amount=0;if(!*value)return EINVAL;
            for(;*value;value++){
                unsigned digit=(unsigned)(*value-'0');
                if(*value<'0'||*value>'9'||amount>(UINT64_MAX-digit)/10)return EINVAL;
                amount=amount*10+digit;
            }
            snprintf(number,24,"%llu",(unsigned long long)amount);
        }
        if(!end)break;
        p=end+1;if(!*p)return EINVAL;
    }
    if(xdev&&!(m->flags&OZ_RECURSIVE))return EINVAL;
    if(physical)args[count++]="-P";
    if(seen&16){args[count++]="-l";args[count++]=length;}
    if(seen&8){args[count++]="-o";args[count++]=offset;}
    if(m->flags&OZ_RECURSIVE)args[count++]="-r";
    if(verbose)args[count++]="-v";
    if(xdev)args[count++]="-x";
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int prune_execute(struct oz_message *m)
{
    char why[256];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=28||
       m->flags!=OZ_CONFIRMED||m->argument[0])return EINVAL;
    char *value,*flag;uint64_t limit,amount=0;
    if(!strncmp(m->options,"percent=",8)){
        value=m->options+8;flag="-p";limit=100;
    }else if(!strncmp(m->options,"days=",5)){
        value=m->options+5;flag="-d";limit=UINT64_MAX/86400;
    }else return EINVAL;
    if(!*value)return EINVAL;
    for(const char *p=value;*p;p++){
        if(*p<'0'||*p>'9'||amount>(limit-(unsigned)(*p-'0'))/10)return EINVAL;
        amount=amount*10+(unsigned)(*p-'0');
    }
    if(!amount||amount>limit)return EINVAL;
    /* Normalize to decimal: upstream strtoull uses base 0. */
    char number[32];snprintf(number,sizeof(number),"%llu",(unsigned long long)amount);
    char *args[]={"C:zpool","ddtprune",flag,number,"--",m->target,NULL};
    return command_capture(m,args);
}

static int wait_execute(struct oz_message *m)
{
    char why[256],activities[sizeof(m->options)];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||(m->operation!=30&&m->operation!=49)||
       (m->flags&~OZ_CONFIRMED)||m->argument[0])return EINVAL;
    int filesystem=m->operation==49;
    char *args[8]={filesystem?"C:zfs":"C:zpool","wait"};unsigned count=2;
    if(m->options[0]){
        if(strncmp(m->options,"activities=",11)||!m->options[11])return EINVAL;
        strcpy(activities,m->options+11);
        const char *pool_activities[]={"discard","free","initialize","replace","remove","resilver","scrub","trim","raidz_expand"};
        const char *fs_activities[]={"deleteq"};
        const char **known=filesystem?fs_activities:pool_activities;
        unsigned known_count=filesystem?1:sizeof(pool_activities)/sizeof(pool_activities[0]);
        unsigned seen=0;char *next=activities;
        while(*next){
            char *item=next,*end=strchr(next,',');
            if(end){*end=0;next=end+1;if(!*next)return EINVAL;}else next+=strlen(next);
            unsigned i=0;for(;i<known_count;i++)if(!strcmp(item,known[i]))break;
            if(i==known_count||(seen&(1U<<i)))return EINVAL;
            seen|=1U<<i;
        }
        args[count++]="-t";args[count++]=m->options+11;
    }
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int prefetch_execute(struct oz_message *m)
{
    char why[256];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=27||
       m->flags!=OZ_CONFIRMED||m->argument[0])return EINVAL;
    char *args[8]={"C:zpool","prefetch"};unsigned count=2;
    if(m->options[0]){
        if(strcmp(m->options,"type=ddt")&&strcmp(m->options,"type=brt"))return EINVAL;
        args[count++]="-t";args[count++]=m->options+5;
    }
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

int oz_diff_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)];
    if(!m||oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=58||
       (m->flags&~OZ_CONFIRMED)||!strchr(m->target,'@'))return EINVAL;
    char *args[12]={"C:zfs","diff"};unsigned count=2,seen=0;
    strcpy(options,m->options);
    char *next=options;
    while(*next){
        char *item=next,*end=strchr(next,';');unsigned bit;
        if(end){*end=0;next=end+1;if(!*next)return EINVAL;}else next+=strlen(next);
        if(!strcmp(item,"classify=on")){bit=1;args[count++]="-F";}
        else if(!strcmp(item,"scripted=on")){bit=2;args[count++]="-H";}
        else if(!strcmp(item,"timestamps=on")){bit=4;args[count++]="-t";}
        else if(!strcmp(item,"unescape=on")){bit=8;args[count++]="-h";}
        else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;
    }
    args[count++]="--";args[count++]=m->target;
    if(m->argument[0])args[count++]=m->argument;
    args[count]=NULL;
    return command_capture(m,args);
}

int oz_project_execute(struct oz_message *m)
{
    char why[256],options[sizeof(m->options)],id[16]={0};
    int inherit=0,keep=0,dir=0;
    unsigned seen=0;
    if(!m)return EINVAL;
    if(oz_validate(m,why,sizeof(why))!=OZ_OK||m->operation!=70||
       (m->flags&~(OZ_CONFIRMED|OZ_RECURSIVE))||!strchr(m->target,':'))return EINVAL;
    int list=!strcmp(m->argument,"list"),set=!strcmp(m->argument,"set"),
        clear=!strcmp(m->argument,"clear"),check=!strcmp(m->argument,"check");
    if(!list&&!set&&!clear&&!check)return EINVAL;
    strcpy(options,m->options);
    char *next=options;
    while(*next){
        char *item=next,*end=strchr(next,';');
        if(end){*end=0;next=end+1;if(!*next)return EINVAL;}else next+=strlen(next);
        unsigned bit=0;
        if(!strncmp(item,"id=",3)){
            bit=1;const char *p=item+3;uint64_t value=0;
            if(!*p)return EINVAL;
            for(;*p;p++){if(*p<'0'||*p>'9')return EINVAL;value=value*10+(*p-'0');if(value>=UINT32_MAX)return EINVAL;}
            snprintf(id,sizeof(id),"%u",(unsigned)value);
        }else if(!strcmp(item,"inherit=on")){bit=2;inherit=1;}
        else if(!strcmp(item,"keep=on")){bit=4;keep=1;}
        else if(!strcmp(item,"directory=on")){bit=8;dir=1;}
        else return EINVAL;
        if(seen&bit)return EINVAL;
        seen|=bit;
    }
    if((set&&!id[0])||(id[0]&&!set&&!check)||(inherit&&!set)||
       (keep&&!clear)||(dir&&(m->flags&OZ_RECURSIVE)))return EINVAL;
    char *args[12];unsigned count=0;
    args[count++]="C:zfs";args[count++]="project";
    if(clear)args[count++]="-C";
    if(check)args[count++]="-c";
    if(inherit)args[count++]="-s";
    if(keep)args[count++]="-k";
    if(dir)args[count++]="-d";
    if(m->flags&OZ_RECURSIVE)args[count++]="-r";
    if(id[0]){args[count++]="-p";args[count++]=id;}
    args[count++]="--";args[count++]=m->target;args[count]=NULL;
    return command_capture(m,args);
}

static int command_capture(struct oz_message *m,char **args)
{
    int error=0;
    char temporary[]="T:openzfs-command-XXXXXX";
    int reserved=mkstemp(temporary);
    if(reserved<0)return errno;
    BPTR output=Open((CONST_STRPTR)temporary,MODE_OLDFILE);
    struct oz_cli_result result={.status=-1};
    char **lines=NULL;int nlines=0;
    if(!output)error=EIO;
    if(!error)error=oz_cli_spawn_exact(args[0],args,output,output,&result);
    /* The child has its own DOS buffering state for the inherited stream.
     * Reopen without truncation after it has drained, as the CLI verifier does,
     * rather than seeking through the parent's stale buffered position. */
    if(output){if(!Close(output)&&!error)error=EIO;output=0;}
    if(!error){output=Open((CONST_STRPTR)temporary,MODE_OLDFILE);if(!output)error=EIO;}
    if(!error)error=oz_cli_read_lines(output,sizeof(m->detail)-128,&lines,&nlines);
    int retained=error==EOVERFLOW;
    if(retained)error=0;
    m->row_count=0;m->next_cursor=0;
    size_t used=snprintf(m->detail,sizeof(m->detail),"%s %s %s: exit=%ld, DOS=%ld, capture errno=%d\n",
        args[1]?"Upstream":"Runtime checker",args[0]+2,args[1]?args[1]:"",result.status,result.dos_error,error);
    for(int i=0;i<nlines&&used<sizeof(m->detail);i++)
        used+=snprintf(m->detail+used,sizeof(m->detail)-used,"%s\n",lines[i]);
    if(retained)snprintf(m->detail+used,sizeof(m->detail)-used,
        "Output exceeds the response window. Full output: %s\n"
        "This temporary report is not persistent; copy it before reboot.\n",temporary);
    oz_cli_free_lines(lines,nlines);
    if(output&&!Close(output)&&!error)error=EIO;
    if(close(reserved)&&!error)error=errno;
    if(!retained&&unlink(temporary)&&!error)error=errno;
    if(error){
        size_t len=strlen(m->detail);
        if(!args[1]&&result.status==-1&&result.dos_error==ERROR_OBJECT_NOT_FOUND)
            snprintf(m->detail+len,sizeof(m->detail)-len,
                "C:OpenZFSRuntimeCheck was not found. Install it from the matching OpenZFS bundle. The checker did not run.");
        else
            snprintf(m->detail+len,sizeof(m->detail)-len,"Collection/cleanup errno=%d; command may have changed files. Do not retry automatically.",error);
    }
    m->status=error||result.status?OZ_ENGINE_ERROR:OZ_OK;
    /* Command failure is a completed response, not a transport retry signal. */
    return 0;
}

struct oz_project_worker { pthread_t thread;pthread_mutex_t lock;int done,joined,runtime;struct oz_message response; };
static void *project_run(void *arg)
{
    struct oz_project_worker *w=arg;
    char *runtime_args[]={"C:OpenZFSRuntimeCheck",NULL};
    int error=w->runtime?command_capture(&w->response,runtime_args):w->response.operation==76?
        ((w->response.flags||w->response.target[0]||w->response.argument[0]||w->response.options[0]||w->response.cursor)?EINVAL:
        oz_admission_list(OZ_ADMISSION_PORT,&w->response)):
        w->response.operation==77?cancel_execute(&w->response):
        w->response.operation==82?pool_errors_execute(&w->response):
        w->response.operation==81?vdev_set_execute(&w->response):
        w->response.operation==52?oz_clone_execute(&w->response):
        w->response.operation==25?upgrade_execute(&w->response):
        w->response.operation==47?dataset_upgrade_execute(&w->response):
        w->response.operation==48?dataset_rewrite_execute(&w->response):
        w->response.operation==34?events_execute(&w->response):
        w->response.operation==59?redact_execute(&w->response):
        w->response.operation==75?program_execute(&w->response):
        w->response.operation==29?labelclear_execute(&w->response):
        w->response.operation==28?prune_execute(&w->response):
        (w->response.operation==30||w->response.operation==49)?wait_execute(&w->response):
        w->response.operation==27?prefetch_execute(&w->response):
        w->response.operation==58?oz_diff_execute(&w->response):oz_project_execute(&w->response);
    if(error){w->response.status=OZ_ENGINE_ERROR;snprintf(w->response.detail,sizeof(w->response.detail),"Request failed before command execution: errno=%d",error);}
    pthread_mutex_lock(&w->lock);w->done=1;pthread_mutex_unlock(&w->lock);return NULL;
}
static int worker_start(const struct oz_message *m,struct oz_project_worker **out,int runtime)
{
    if(!m||!out)return EINVAL;
    *out=NULL;struct oz_project_worker *w=calloc(1,sizeof(*w));if(!w)return ENOMEM;
    w->runtime=runtime;
    w->response=*m;memset(&w->response.msg,0,sizeof(w->response.msg));w->response.msg.mn_Length=sizeof(*m);
    int e=pthread_mutex_init(&w->lock,NULL);if(e){free(w);return e;}
    e=pthread_create(&w->thread,NULL,project_run,w);
    if(e){pthread_mutex_destroy(&w->lock);free(w);return e;}
    *out=w;return 0;
}
int oz_project_worker_start(const struct oz_message *m,struct oz_project_worker **out)
{return worker_start(m,out,0);}
int oz_runtime_worker_start(struct oz_project_worker **out)
{
    struct oz_message request={0};
    return worker_start(&request,out,1);
}
int oz_project_worker_poll(struct oz_project_worker *w,struct oz_message *out)
{
    if(!w||!out)return EINVAL;
    pthread_mutex_lock(&w->lock);int done=w->done;pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined){int e=pthread_join(w->thread,NULL);if(e)return e;w->joined=1;}
    *out=w->response;return 0;
}
int oz_project_worker_free(struct oz_project_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int e=pthread_mutex_destroy(&w->lock);if(e)return e;
    free(w);return 0;
}
