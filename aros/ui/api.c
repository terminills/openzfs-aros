#include "api.h"
#include "clear-policy.h"
#include <proto/exec.h>
#include <exec/memory.h>
#include <stdio.h>
#include <string.h>
#ifndef OZ_CLIENT_TIMEOUT_SECONDS
#define OZ_CLIENT_TIMEOUT_SECONDS 3
#endif
#if OZ_CLIENT_TIMEOUT_SECONDS < 1
#error OZ_CLIENT_TIMEOUT_SECONDS must be positive
#endif
static ULONG client_serial;
const struct oz_operation oz_operations[] = {
#define OZ_OP(id,cat,key,label,mut,target,arg,help) {id,cat,key,label,mut,target,arg,help},
#include "operations.def"
#undef OZ_OP
};
const unsigned oz_operation_count=sizeof(oz_operations)/sizeof(oz_operations[0]);
int oz_parse_import_policy(const char *options,struct oz_import_policy *out)
{
    unsigned seen=0;int ro=0,recover=0,checkpoint=0,assessment=0;UQUAD guid=0;
    enum oz_import_recovery recovery=OZ_IMPORT_CURRENT;
    if(!options||!out)return 0;
    for(const char *p=options;*p;) {
        const char *end=strchr(p,';');size_t n=end?(size_t)(end-p):strlen(p);
        unsigned field=0;int on=0;
        if(n==11&&!memcmp(p,"readonly=on",11)){field=1;on=1;}
        else if(n==12&&!memcmp(p,"readonly=off",12))field=1;
        else if(n==12&&!memcmp(p,"destroyed=on",12)){field=2;on=1;}
        else if(n==13&&!memcmp(p,"destroyed=off",13))field=2;
        else if(n==9&&!memcmp(p,"rewind=on",9)){field=4;on=1;}
        else if(n==10&&!memcmp(p,"rewind=off",10))field=4;
        else if(n==13&&!memcmp(p,"recovery=none",13)){field=16;recovery=OZ_IMPORT_CURRENT;}
        else if(n==13&&!memcmp(p,"recovery=safe",13)){field=16;recovery=OZ_IMPORT_RECOVER;}
        else if(n==16&&!memcmp(p,"recovery=extreme",16)){field=16;recovery=OZ_IMPORT_EXTREME;}
        else if(n==13&&!memcmp(p,"assessment=on",13)){field=32;on=1;}
        else if(n==14&&!memcmp(p,"assessment=off",14))field=32;
        else if(n==21&&!memcmp(p,"guid=",5)){
            field=8;guid=0;
            for(unsigned i=5;i<21;i++){
                unsigned digit;
                if(p[i]>='0'&&p[i]<='9')digit=p[i]-'0';
                else if(p[i]>='a'&&p[i]<='f')digit=p[i]-'a'+10;
                else if(p[i]>='A'&&p[i]<='F')digit=p[i]-'A'+10;
                else return 0;
                guid=(guid<<4)|digit;
            }
            if(!guid)return 0;
        }
        if(!field||(seen&field))return 0;
        seen|=field;if(field==1)ro=on;else if(field==2)recover=on;else if(field==4)checkpoint=on;else if(field==32)assessment=on;
        if(!end)break;
        p=end+1;if(!*p)return 0;
    }
    if(!(seen&1)||(checkpoint&&recovery!=OZ_IMPORT_CURRENT))return 0;
    if(assessment&&(!ro||recovery==OZ_IMPORT_CURRENT))return 0;
    struct oz_import_policy parsed={.readonly=ro,.destroyed=recover,
        .checkpoint=checkpoint,.assessment=assessment,.recovery=recovery,.expected_guid=guid};
    *out=parsed;return 1;
}
int oz_parse_import_identity(const char *options,int *readonly,int *destroyed,int *rewind,UQUAD *expected_guid)
{
    struct oz_import_policy parsed;
    /* Legacy callers cannot represent transaction recovery. Refuse it,
     * rather than accepting the string then dropping its load policy. */
    if(!readonly||!destroyed||!rewind||!expected_guid||
        !oz_parse_import_policy(options,&parsed)||parsed.recovery!=OZ_IMPORT_CURRENT)return 0;
    *readonly=parsed.readonly;*destroyed=parsed.destroyed;
    *rewind=parsed.checkpoint;*expected_guid=parsed.expected_guid;return 1;
}
int oz_parse_import_options(const char *options,int *readonly,int *destroyed,int *rewind)
{UQUAD guid;return oz_parse_import_identity(options,readonly,destroyed,rewind,&guid);}
const struct oz_operation *oz_find(ULONG id) {
    unsigned i; for(i=0;i<oz_operation_count;i++) if(oz_operations[i].id==id) return &oz_operations[i];
    return NULL;
}
const char *oz_operation_unavailable(ULONG id) {
    switch(id) {
    case 45: case 46:
        return "Unavailable on AROS: no native NFS or SMB export provider is installed";
    case 71: case 72:
        return "Not applicable on AROS: FreeBSD jail namespaces are not present";
    case 73: case 74:
        return "Not applicable on AROS: Linux user namespaces are not present";
    default:
        return NULL;
    }
}
enum oz_route oz_request_route(const struct oz_message *m) {
    if(!m)return OZ_ROUTE_INVALID;
    switch(m->operation) {
    case 1: case 2: case 3: case 4: case 31: case 35: case 36: case 40: case 57:
        return OZ_ROUTE_PUBLIC;
    case 7:
        return m->topology.device_count?OZ_ROUTE_AUTHENTICATED:OZ_ROUTE_PUBLIC;
    case 29:
        /* Native device paths require the resident service's retained
         * credential and bounded block-I/O adapter.  Ordinary file vdevs
         * retain the asynchronous upstream command-worker path. */
        return !strncmp(m->target,"/AROS/",6)?OZ_ROUTE_AUTHENTICATED:OZ_ROUTE_PROJECT;
    case 52:
        return m->options[0]?OZ_ROUTE_PROJECT:OZ_ROUTE_AUTHENTICATED;
    case 17:
        return m->options[0]?OZ_ROUTE_PROJECT:OZ_ROUTE_AUTHENTICATED;
    case 38:
        return (m->flags&OZ_DRYRUN)?OZ_ROUTE_PROJECT:OZ_ROUTE_AUTHENTICATED;
    case 5: case 6: case 8: case 9: case 10: case 11: case 12: case 13:
    case 14: case 15: case 16: case 18: case 19: case 20: case 21:
    case 22: case 23: case 24: case 26: case 32: case 33: case 37:
    case 39: case 41: case 42: case 43: case 44: case 50: case 51:
    case 53: case 54: case 55: case 56: case 60: case 61: case 62: case 63:
    case 64: case 65: case 66: case 67: case 68: case 69: case 78: case 79:
    case 80:
        return OZ_ROUTE_AUTHENTICATED;
    case 25: case 27: case 28: case 30: case 34: case 47: case 48:
    case 49: case 58: case 59: case 70: case 75: case 76: case 77: case 81:
    case 82:
        return OZ_ROUTE_PROJECT;
    case 45: case 46: case 71: case 72: case 73: case 74:
        return OZ_ROUTE_UNAVAILABLE;
    default:
        return OZ_ROUTE_INVALID;
    }
}
int oz_request_mutates(ULONG id,const char *argument) {
    const struct oz_operation *op=oz_find(id);
    if(id==70 && argument && (!strcmp(argument,"list")||!strcmp(argument,"check")))return 0;
    return !op || op->mutation;
}
const char *oz_status(ULONG s) {
    static const char *names[]={"OK","Service unavailable","Engine unavailable","Invalid request",
        "API version mismatch","Unsupported operation","No response (timeout)","Request pending","Out of memory","Engine operation failed","Request cancelled"};
    return s<sizeof(names)/sizeof(names[0])?names[s]:"Unknown backend status";
}
ULONG oz_validate(const struct oz_message *m,char *why,unsigned size) {
    const struct oz_operation *op=oz_find(m->operation);
    const char *error=NULL; ULONG status=OZ_BAD_REQUEST;
    if(m->magic!=OZ_MAGIC || m->msg.mn_Length!=sizeof(*m)) error="Invalid message envelope";
    else if(m->version!=OZ_VERSION) {error="Expected management API version 3";status=OZ_BAD_VERSION;}
    else if(!op) {error="Unknown operation ID";status=OZ_UNSUPPORTED;}
    else if((error=oz_operation_unavailable(m->operation))!=NULL) status=OZ_UNSUPPORTED;
    else if(m->flags & ~(OZ_CONFIRMED|OZ_RECURSIVE|OZ_FORCE|OZ_DRYRUN|OZ_RAW|OZ_RESUMABLE)) error="Unknown request flags";
    else if(!memchr(m->target,0,sizeof(m->target)) || !memchr(m->argument,0,sizeof(m->argument)) ||
            !memchr(m->options,0,sizeof(m->options))) error="Unterminated parameter";
    else if(op->target_label[0] && !m->target[0] && m->operation!=34 && m->operation!=3) error="Target is required";
    else if(m->operation==64 && !strcmp(m->options,"inherit=on") && m->argument[0]) error="Key inheritance does not accept a key file";
    else if(op->argument_label[0] && !m->argument[0] && !(m->operation==64 && !strcmp(m->options,"inherit=on")) && m->operation!=58 && m->operation!=23 && m->operation!=17 && !(m->operation==5 && m->topology.group_count) && !(m->operation==7 && m->topology.device_count)) error="Argument is required";
    else if(oz_request_mutates(m->operation,m->argument) && !(m->flags&OZ_CONFIRMED)) error="Change requires explicit confirmation";
    if(error) {snprintf(why,size,"%s",error);return status;}
    if(m->operation==17&&m->options[0]) {
        struct oz_clear_policy policy;
        if(!oz_parse_clear_policy(m->options,&policy)||m->argument[0]||m->flags!=OZ_CONFIRMED||m->cursor||m->job_id) {
            snprintf(why,size,"Clear recovery requires recovery=safe|extreme, optional assessment=on|off, confirmation and no device GUID or extra flags");
            return OZ_BAD_REQUEST;
        }
    }
    if(m->operation==9)return oz_validate_add_topology(&m->topology,why,size);
    if(m->operation==11||m->operation==13) {
        if(m->options[0]&&strcmp(m->options,"rebuild=on")){
            snprintf(why,size,"Attach/replace accepts only rebuild=on or empty options");return OZ_BAD_REQUEST;
        }
        if(m->topology.group_count!=1||m->topology.device_count!=1||
            m->topology.groups[0].role!=OZ_DATA||m->topology.groups[0].layout!=OZ_STRIPE) {
            snprintf(why,size,"Attach/replace requires exactly one new file in a data/stripe group");
            return OZ_BAD_REQUEST;
        }
        return oz_validate_topology(&m->topology,why,size);
    }
    if(m->topology.group_count || m->topology.device_count || m->topology.ashift) {
        if(m->operation==7&&!m->argument[0])return oz_validate_import_files(&m->topology,why,size);
        if(m->operation!=5 || m->argument[0]) {
            snprintf(why,size,"Typed topology is pool.create only; do not mix with device argument");return OZ_BAD_REQUEST;
        }
        status=oz_validate_topology(&m->topology,why,size);
        if(status!=OZ_OK)return status;
    }
    snprintf(why,size,"Validated %s",op->key);return OZ_OK;
}
ULONG oz_validate_import_files(const struct oz_topology *t,char *why,unsigned size)
{
    const char *error=NULL;
    if(t->group_count||t->ashift||!t->device_count||t->device_count>OZ_MAX_DEVICES)
        error="Import requires 1-32 explicit paths, without layout or ashift";
    for(unsigned i=0;!error&&i<t->device_count;i++) {
        if(t->devices[i].group||!t->devices[i].path[0]||!memchr(t->devices[i].path,0,sizeof(t->devices[i].path)))
            error="Invalid import path";
        for(unsigned j=0;!error&&j<i;j++)if(!strcmp(t->devices[i].path,t->devices[j].path))error="Duplicate import path";
    }
    snprintf(why,size,"%s",error?error:"Explicit import path list validated");
    return error?OZ_BAD_REQUEST:OZ_OK;
}
/* Same geometry and slice-group calculation as upstream
 * cmd/zpool/zpool_vdev.c:draid_config_by_type, within this API's device bound.
 * Validate before subtraction/multiplication; leave outputs unchanged on error. */
int oz_draid_geometry(const struct oz_vdev_group *g,unsigned children,
    unsigned *data,unsigned *groups) {
    unsigned p=g->draid_parity,s=g->draid_spares,d=g->draid_data,n=1;
    if(g->layout!=OZ_DRAID||g->role!=OZ_DATA||p<1||p>3||
       children>OZ_MAX_DEVICES||s>100||children<=p||s>=children-p)return 0;
    unsigned available=children-p-s;
    if(!d)d=available<8?available:8;
    if(d>available)return 0;
    while((n*(d+p))%(children-s))n++;
    *data=d;*groups=n;return 1;
}
static ULONG validate_topology(const struct oz_topology *t,char *why,unsigned size,int require_data) {
    unsigned i,j,counts[OZ_MAX_GROUPS]={0},data=0;const char *error=NULL;
    if(!t->group_count || t->group_count>OZ_MAX_GROUPS || !t->device_count || t->device_count>OZ_MAX_DEVICES)
        error="Layout requires 1-8 groups and 1-32 devices";
    else if(t->ashift && (t->ashift<9 || t->ashift>16))error="ashift must be automatic (0) or 9-16";
    if(!error)for(i=0;i<t->device_count;i++) {
        const struct oz_vdev_leaf *d=&t->devices[i];
        if(d->group>=t->group_count || !d->path[0] || !memchr(d->path,0,sizeof(d->path))) {error="Invalid device path or group";break;}
        for(j=0;j<i;j++)if(!strcmp(d->path,t->devices[j].path)){error="Duplicate device path";break;}
        if(error)break;
        counts[d->group]++;
    }
    if(!error)for(i=0;i<t->group_count;i++) {
        const struct oz_vdev_group *g=&t->groups[i];
        unsigned minimum=g->layout==OZ_MIRROR?2:g->layout>=OZ_RAIDZ1?g->layout:1;
        if(g->role>OZ_DEDUP || g->layout>OZ_DRAID){error="Unknown vdev role or layout";break;}
        if(g->role==OZ_DATA)data++;
        if(g->layout==OZ_DRAID){
            unsigned width,groups;
            if(!oz_draid_geometry(g,counts[i],&width,&groups)){error="Invalid dRAID geometry: parity 1-3; data and spares must fit the children";break;}
            continue;
        }
        if(g->draid_parity||g->draid_data||g->draid_spares){error="dRAID parameters on a different layout";break;}
        if(g->role!=OZ_DATA && g->layout>=OZ_RAIDZ1){error="RAIDZ is only supported for data groups";break;}
        if((g->role==OZ_CACHE || g->role==OZ_SPARE) && g->layout!=OZ_STRIPE){error="Cache and spare devices are ungrouped leaves";break;}
        if(counts[i]<minimum){error="Too few devices: stripe 1, mirror 2, RAIDZ1 2, RAIDZ2 3, RAIDZ3 4";break;}
    }
    if(!error && require_data && !data)error="Pool creation requires a data group";
    snprintf(why,size,"%s",error?error:"Layout validated; device identity and suitability still require engine checks");
    return error?OZ_BAD_REQUEST:OZ_OK;
}
ULONG oz_validate_topology(const struct oz_topology *t,char *why,unsigned size)
{return validate_topology(t,why,size,1);}
ULONG oz_validate_add_topology(const struct oz_topology *t,char *why,unsigned size)
{return validate_topology(t,why,size,0);}
void oz_message_free(struct oz_message *m) { if(m) {memset(m,0,sizeof(*m));FreeVec(m);} }
int oz_client_open(struct oz_client *c) {
    memset(c,0,sizeof(*c));c->server_name=OZ_PORT;c->port=CreateMsgPort();if(!c->port)return 0;
    snprintf(c->name,sizeof(c->name),"OpenZFS.Client.%p.%p.%lu.%lu",(void*)FindTask(NULL),(void*)c->port,(unsigned long)time(NULL),(unsigned long)++client_serial);
    c->port->mp_Node.ln_Name=c->name;AddPort(c->port);return 1;
}
void oz_client_close(struct oz_client *c) {
    struct oz_message *m;if(!c->port)return;
    Forbid();RemPort(c->port);Permit();
    while((m=(struct oz_message*)GetMsg(c->port))) oz_message_free(m);
    DeleteMsgPort(c->port);c->port=NULL;
    /* Server owns in-flight requests and drops a response if this port is gone. */
}
ULONG oz_submit(struct oz_client *c,const struct oz_message *input) {
    struct MsgPort *server;struct oz_message *m;
    if(c->pending)return OZ_BUSY;
    m=AllocVec(sizeof(*m),MEMF_PUBLIC|MEMF_CLEAR);if(!m)return OZ_NO_MEMORY;
    memcpy(m,input,sizeof(*m));memset(&m->msg,0,sizeof(m->msg));m->msg.mn_Length=sizeof(*m);
    m->magic=OZ_MAGIC;m->version= input->version?input->version:OZ_VERSION;
    m->id=++c->next_id;snprintf(m->reply_name,sizeof(m->reply_name),"%s",c->name);
    c->pending=m->id;c->deadline=time(NULL)+OZ_CLIENT_TIMEOUT_SECONDS;
    Forbid();server=FindPort((CONST_STRPTR)c->server_name);if(server)PutMsg(server,&m->msg);Permit();
    if(!server){c->pending=0;oz_message_free(m);return OZ_NO_SERVICE;}
    return OZ_OK;
}
struct oz_message *oz_poll(struct oz_client *c) {
    struct oz_message *m;
    while((m=(struct oz_message*)GetMsg(c->port))) {
        if(m->msg.mn_Length==sizeof(*m) && m->magic==OZ_MAGIC && m->version==OZ_VERSION && m->id==c->pending && m->row_count<=OZ_MAX_ROWS && memchr(m->detail,0,sizeof(m->detail))) {
            c->pending=0;return m;
        }
        oz_message_free(m);
    }
    if(c->pending && time(NULL)>=c->deadline) {
        m=AllocVec(sizeof(*m),MEMF_PUBLIC|MEMF_CLEAR);
        if(m){m->id=c->pending;m->status=OZ_TIMEOUT;snprintf(m->detail,sizeof(m->detail),
            "No response within %u seconds. Outcome unknown; do not automatically retry a change. Late replies are discarded.",
            (unsigned)OZ_CLIENT_TIMEOUT_SECONDS);c->pending=0;}
        return m;
    }
    return NULL;
}
