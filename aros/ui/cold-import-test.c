/* SPDX-License-Identifier: CDDL-1.0 */
/* Separate native client; only the marked, isolated two-boot fixture. */
#include "admission-api.h"
#include <proto/exec.h>
#include <proto/dos.h>
#include <proto/security.h>
#include <libraries/security.h>
#include <dos/dosextens.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef OZ_COLD_RELEASE
/* The marked disposable client supplies identity, never the release daemon. */
#undef OZ_ADMISSION_TEST_PORT
#undef OZ_NATIVE_TEST_PORT
#define OZ_ADMISSION_TEST_PORT OZ_ADMISSION_PORT
#define OZ_NATIVE_TEST_PORT OZ_PORT
#endif

static void note(const char *s) {while(*s)RawPutChar(*s++);RawPutChar('\n');}
static unsigned member_count=2;
static unsigned missing_count;
static unsigned pool_layout=OZ_MIRROR;
static unsigned canonical_paths;
static unsigned relocated_files;
static unsigned promotion_test;
static unsigned detach_test,detached;
static unsigned attach_test,attached;
static unsigned add_test,expanded;
static unsigned aux_test,aux_added,aux_readonly,add_role,add_disk;
static unsigned remove_test,removed,remove_extra;
static unsigned removal_resume,resume_phase;
static unsigned split_mode,split_done; /* 1=create/split, 2=cold source, 3=cold destination */
static int present(void)
{Forbid();int found=FindPort((CONST_STRPTR)OZ_ADMISSION_TEST_PORT)!=NULL;Permit();return found;}
static int request(unsigned op,const char *target,const char *arg,const char *opts,int expected)
{
    struct oz_message *m=calloc(1,sizeof(*m)),*out=calloc(1,sizeof(*out));
    if(!m||!out){free(m);free(out);return ENOMEM;}
    m->magic=OZ_MAGIC;m->version=OZ_VERSION;m->msg.mn_Length=sizeof(*m);
    m->operation=op;m->flags=OZ_CONFIRMED;
    snprintf(m->target,sizeof(m->target),"%s",target);
    snprintf(m->argument,sizeof(m->argument),"%s",arg?arg:"");
    snprintf(m->options,sizeof(m->options),"%s",opts?opts:"");
    if(op==5||op==7||op==9||op==11||op==13) {
        char directory[224]="OZFILES:OpenZFS-test-51a20519";
        if(canonical_paths) {
            BPTR parent=Lock((CONST_STRPTR)directory,ACCESS_READ);
            if(!parent){free(m);free(out);return ENOENT;}
            int named=NameFromLock(parent,(STRPTR)directory,sizeof(directory));UnLock(parent);
            if(!named){free(m);free(out);return ENAMETOOLONG;}
        }
        unsigned count=(op==7&&(attach_test||add_test))?4:member_count;
        if(op==7&&aux_test)count=7;
        if(op==7&&remove_test)count=3; /* Only surviving disk2/disk3/disk4. */
        if(op==7&&resume_phase)count=5;
        if(op==7&&split_mode>=2)count=2;
        m->topology.device_count=count;
        if(op==5||op==9) {
            m->topology.group_count=1;m->topology.ashift=12;
            m->topology.groups[0].role=OZ_DATA;
            m->topology.groups[0].layout=pool_layout;
        }
        for(unsigned i=0;i<count;i++)snprintf(m->topology.devices[i].path,
            sizeof(m->topology.devices[i].path),"%s/%s%u",directory,relocated_files?"moved":"disk",i+((op==9||(op==7&&remove_test&&!resume_phase))?2:0));
        if(op==7&&split_mode>=2)for(unsigned i=0;i<count;i++)
            snprintf(m->topology.devices[i].path,sizeof(m->topology.devices[i].path),
                "%s/disk%u",directory,2*i+(split_mode==3));
        if(op==9&&(add_role||add_disk)) {
            m->topology.device_count=1;m->topology.groups[0].layout=OZ_STRIPE;
            m->topology.groups[0].role=add_role;
            snprintf(m->topology.devices[0].path,sizeof(m->topology.devices[0].path),"%s/disk%u",directory,add_disk);
        }
        if(op==11||op==13) {
            m->topology.group_count=1;m->topology.device_count=1;
            m->topology.groups[0].role=OZ_DATA;m->topology.groups[0].layout=OZ_STRIPE;
            snprintf(m->topology.devices[0].path,sizeof(m->topology.devices[0].path),"%s/disk%u",directory,op==11?2:3);
        }
    }
    struct oz_ticket ticket={0};int error=oz_admission_submit(OZ_ADMISSION_TEST_PORT,m,&ticket);
    if(!error)do {error=oz_admission_query(OZ_ADMISSION_TEST_PORT,&ticket,out);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(!error) {
        char wanted[32];snprintf(wanted,sizeof(wanted),"errno=%d",expected);
        const char *reported=strstr(out->detail,"errno=");
        if(out->status!=(expected?OZ_ENGINE_ERROR:OZ_OK)||!reported||strcmp(reported,wanted))error=EIO;
        note(out->detail);
    }
    if(ticket.number){int rc=oz_admission_release(OZ_ADMISSION_TEST_PORT,&ticket);if(!error)error=rc;}
    free(m);free(out);return error;
}
static int check_health(const char *pool,int absent)
{
    struct oz_client client;struct oz_message m={0};
    if(!oz_client_open(&client))return ENOMEM;
    client.server_name=OZ_NATIVE_TEST_PORT;m.operation=4;
    snprintf(m.target,sizeof(m.target),"%s",pool);
    int error=oz_submit(&client,&m)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;
        while(!(reply=oz_poll(&client)))Delay(1);
        if(absent) {
            const char *reported=strstr(reply->detail,"errno=");
            error=reply->status==OZ_ENGINE_ERROR&&!reply->row_count&&reported&&
                strtol(reported+6,NULL,10)==ENOENT?0:EIO;
            note(reply->detail);oz_message_free(reply);oz_client_close(&client);
            return error;
        }
        unsigned missing=0,files=0,mirrors=0,raids=0,indirect=0;int pool_seen=0;
        if(reply->status!=OZ_OK||reply->next_cursor)error=EIO;
        for(unsigned i=0;i<reply->row_count;i++) {
            struct oz_row *r=&reply->rows[i];
            char line[512];snprintf(line,sizeof(line),"Cold pool status %s %s %s",r->kind,r->state,r->value);note(line);
            if(!strcmp(r->kind,"pool")) {
                pool_seen=1;if(strcmp(r->state,missing_count?"DEGRADED":"ONLINE"))error=EIO;
            }
            if(!strcmp(r->kind,"file")) {
                if(relocated_files&&!strstr(r->name,"/moved"))error=EIO;
                files++;if(!strcmp(r->state,"UNAVAIL"))missing++;
                else if(aux_readonly&&(strstr(r->value,"role=spare ")||strstr(r->value,"role=cache "))) {
                    if(strcmp(r->state,"INACTIVE")||!strstr(r->value,"statistics=unavailable"))error=EIO;
                } else if(strcmp(r->state,"ONLINE"))error=EIO;
            }
            if(!strcmp(r->kind,"mirror"))mirrors++;
            if(!strcmp(r->kind,"indirect"))indirect++;
            if(!strcmp(r->kind,"raidz")) {
                raids++;char parity[32];
                snprintf(parity,sizeof(parity),"parity=%u",pool_layout-OZ_RAIDZ1+1);
                if(!strstr(r->value,parity))error=EIO;
            }
        }
        if(!pool_seen||files!=(split_done?2:removed?3:remove_extra?5:aux_added?7:expanded?4:attached?3:detached?1:member_count)||missing!=missing_count||
            mirrors!=(split_done?0:removed?1:expanded?2:(!detached&&pool_layout==OZ_MIRROR))||raids!=(pool_layout>=OZ_RAIDZ1)||indirect!=removed)error=EIO;
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int cold_detach(const char *pool)
{
    struct oz_client client;struct oz_message m={0};char guid[32]={0};
    if(!oz_client_open(&client))return ENOMEM;
    client.server_name=OZ_NATIVE_TEST_PORT;m.operation=4;
    snprintf(m.target,sizeof(m.target),"%s",pool);
    int error=oz_submit(&client,&m)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;while(!(reply=oz_poll(&client)))Delay(1);
        if(reply->status!=OZ_OK||reply->next_cursor)error=EIO;
        for(unsigned i=0;!error&&i<reply->row_count&&!guid[0];i++) {
            if(strcmp(reply->rows[i].kind,"file"))continue;
            const char *p=strstr(reply->rows[i].value,"guid=");
            if(!p){error=EIO;break;}
            snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);
    if(error||!guid[0])return error?error:EIO;
    error=request(12,pool,guid,NULL,0);if(error)return error;
    detached=1;error=check_health(pool,0);
    if(!error)note("Cold detach: one ONLINE leaf after mirror collapse PASS");
    return error;
}
static struct oz_message *cold_status(const char *pool)
{
    struct oz_client client;struct oz_message m={0},*reply=NULL;
    if(!oz_client_open(&client))return NULL;
    client.server_name=OZ_NATIVE_TEST_PORT;m.operation=4;
    snprintf(m.target,sizeof(m.target),"%s",pool);
    if(oz_submit(&client,&m)==OZ_OK)while(!(reply=oz_poll(&client)))Delay(1);
    oz_client_close(&client);return reply;
}
static int cold_expansion(const char *pool,int write_phase)
{
    int error=0;BPTR file=0;unsigned char block[4096];
    struct oz_message *out=NULL;unsigned long long old_space=0,new_space=0;
#define ADD_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    if(write_phase) {
        out=cold_status(pool);ADD_CHECK(out&&out->status==OZ_OK&&!out->next_cursor);
        for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"pool"))old_space=out->rows[i].total;
        oz_message_free(out);out=NULL;ADD_CHECK(old_space);
        ADD_CHECK(!request(9,pool,NULL,NULL,0));expanded=1;
        ADD_CHECK(!check_health(pool,0));
        out=cold_status(pool);ADD_CHECK(out&&out->status==OZ_OK&&!out->next_cursor);
        for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"pool"))new_space=out->rows[i].total;
        oz_message_free(out);out=NULL;ADD_CHECK(new_space>old_space);
        if(aux_test) {
            const unsigned roles[]={OZ_LOG,OZ_CACHE,OZ_SPARE};
            for(unsigned i=0;i<3;i++) {
                add_role=roles[i];add_disk=4+i;
                ADD_CHECK(!request(9,pool,NULL,NULL,0));
            }
            add_role=0;aux_added=1;ADD_CHECK(!check_health(pool,0));
        }
        file=Open((CONST_STRPTR)"OZCOLD:expanded.bin",MODE_NEWFILE);ADD_CHECK(file);
        for(unsigned n=0;n<2048;n++) {
            for(unsigned i=0;i<sizeof(block);i++)block[i]=(unsigned char)((n*37+i*13)^(i>>3)^(n>>4));
            ADD_CHECK(Write(file,block,sizeof(block))==sizeof(block));
        }
        ADD_CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
        ADD_CHECK(Close(file));file=0;
        ADD_CHECK(!request(26,pool,NULL,NULL,0));
    }
    file=Open((CONST_STRPTR)"OZCOLD:expanded.bin",MODE_OLDFILE);ADD_CHECK(file);
    for(unsigned n=0;n<2048;n++) {
        ADD_CHECK(Read(file,block,sizeof(block))==sizeof(block));
        for(unsigned i=0;i<sizeof(block);i++)ADD_CHECK(block[i]==(unsigned char)((n*37+i*13)^(i>>3)^(n>>4)));
    }
    ADD_CHECK(Read(file,block,1)==0);ADD_CHECK(Close(file));file=0;
    out=cold_status(pool);ADD_CHECK(out&&out->status==OZ_OK&&!out->next_cursor);
    char parent[160]={0};int allocated=0;
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];size_t n=strlen(r->name);
        if(!strcmp(r->kind,"file")&&n>=6&&!strcmp(r->name+n-6,"/disk2"))snprintf(parent,sizeof(parent),"%s",r->parent);
    }
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];
        if(!strcmp(r->kind,"mirror")&&!strcmp(r->name,parent)&&r->used)allocated=1;
    }
    if(split_done) {
        unsigned populated=0;
        for(unsigned i=0;i<out->row_count;i++)if(!strcmp(out->rows[i].kind,"file")&&out->rows[i].used)populated++;
        ADD_CHECK(populated==2);
        note("Cold split: independent pool exact 8MiB DOS payload and allocated leaves PASS");
    } else ADD_CHECK(allocated);
    if(aux_test) {
        const char *roles[]={"role=log ","role=cache ","role=spare "};
        for(unsigned j=0;j<3;j++) {
            unsigned found=0;char suffix[16];snprintf(suffix,sizeof(suffix),"/disk%u",4+j);
            for(unsigned i=0;i<out->row_count;i++) {
                struct oz_row *r=&out->rows[i];size_t n=strlen(r->name);
                if(strcmp(r->kind,"file")||!strstr(r->value,roles[j]))continue;
                ADD_CHECK(n>=strlen(suffix)&&!strcmp(r->name+n-strlen(suffix),suffix));
                ADD_CHECK(!strcmp(r->state,(!write_phase&&j)?"INACTIVE":"ONLINE"));found++;
            }
            ADD_CHECK(found==1);
        }
        note(write_phase?"Cold auxiliary: log/cache/spare added and native payload PASS":
            "Cold auxiliary: fresh read-only log ONLINE cache/spare INACTIVE and native payload PASS");
    }
    if(!split_done)note(write_phase?"Cold expansion: increased capacity, new mirror allocated, 8MiB DOS payload PASS":
        "Cold expansion: fresh import, new mirror allocated, exact 8MiB DOS payload PASS");
done:
    if(file)Close(file);
    if(out)oz_message_free(out);
    if(error){char line[96];snprintf(line,sizeof(line),"Cold expansion failed line=%d",error);note(line);}
    return error;
#undef ADD_CHECK
}
static int cold_split(const char *pool)
{
    struct oz_message *out=cold_status(pool);
    if(!out||out->status!=OZ_OK||out->next_cursor){if(out)oz_message_free(out);return EIO;}
    char guids[2][32]={{0}},options[96];
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];size_t len=strlen(r->name);
        if(strcmp(r->kind,"file")||len<6)continue;
        for(unsigned j=0;j<2;j++) {
            const char *suffix=j?"/disk3":"/disk1";
            if(strcmp(r->name+len-6,suffix))continue;
            const char *p=strstr(r->value,"guid=");
            if(p){unsigned n=0;p+=5;while(p[n]>='0'&&p[n]<='9'&&n<31){guids[j][n]=p[n];n++;}guids[j][n]=0;}
        }
    }
    oz_message_free(out);
    if(!guids[0][0]||!guids[1][0])return EIO;
    snprintf(options,sizeof(options),"guids=%s,%s",guids[1],guids[0]);
    int error=request(14,pool,"aros_split_51a20519",options,0);
    if(!error)error=check_health("aros_split_51a20519",1);
    if(!error){split_done=1;error=check_health(pool,0);}
    if(!error)note("Cold split: populated mirrors split, destination exported, source healthy PASS");
    return error;
}
static int cold_removal_wait(const char *pool)
{
    for(unsigned n=0;n<120;n++) {
        struct oz_message *out=cold_status(pool);if(!out)return EIO;
        const char *progress=strstr(out->detail,"removal=finished");
        const char *copied=progress?strstr(progress,"copied="):NULL;
        int finished=out->status==OZ_OK&&copied&&strtoull(copied+7,NULL,10)>0;
        oz_message_free(out);if(finished)return 0;Delay(50);
    }
    return ETIMEDOUT;
}
static int cold_remove(const char *pool)
{
    struct oz_message *out=cold_status(pool);char parent[160]={0},guid[32]={0};
    if(!out)return EIO;
    if(out->status!=OZ_OK||out->next_cursor){oz_message_free(out);return EIO;}
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];size_t n=strlen(r->name);
        if(!strcmp(r->kind,"file")&&n>=6&&!strcmp(r->name+n-6,"/disk0"))snprintf(parent,sizeof(parent),"%s",r->parent);
    }
    for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];
        if(strcmp(r->kind,"mirror")||strcmp(r->name,parent)||!r->used)continue;
        const char *p=strstr(r->value,"guid=");
        if(p)snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));
    }
    oz_message_free(out);if(!guid[0])return EIO;
    add_disk=4;int error=request(9,pool,NULL,NULL,0);add_disk=0;
    if(error)return error;
    remove_extra=1;
    if((error=check_health(pool,0)))return error;
    if((error=request(10,pool,guid,NULL,ENOTSUP)))return error;
    if((error=request(32,pool,"feature@device_removal=enabled",NULL,0)))return error;
    if((error=request(10,pool,guid,NULL,0)))return error;
    if(removal_resume) {
        if((error=request(26,pool,NULL,NULL,0)))return error;
        out=cold_status(pool);if(!out)return EIO;
        int running=out->status==OZ_OK&&strstr(out->detail,"removal=running");
        oz_message_free(out);if(!running)return EIO;
        note("RESULT removal synced and running ready for guest interruption");
        for(;;)Delay(50); /* Host kills only this private QEMU, without export. */
    }
    if((error=cold_removal_wait(pool)))return error;
    removed=1;error=check_health(pool,0);
    if(!error)note("Cold removal: original populated mirror evacuated, copied bytes positive, indirect topology PASS");
    return error;
}
static int cold_attach_replace(const char *pool)
{
    char guid[32]={0};struct oz_message *out=cold_status(pool);
    if(!out)return EIO;
    if(out->status==OZ_OK&&!out->next_cursor)for(unsigned i=0;i<out->row_count;i++) {
        struct oz_row *r=&out->rows[i];size_t n=strlen(r->name);
        if(strcmp(r->kind,"file")||n<6||strcmp(r->name+n-6,"/disk0"))continue;
        const char *p=strstr(r->value,"guid=");
        if(p)snprintf(guid,sizeof(guid),"%llu",strtoull(p+5,NULL,10));
    }
    oz_message_free(out);if(!guid[0])return EIO;
    for(unsigned step=0;step<2;step++) {
        int error=request(step?13:11,pool,guid,NULL,0);if(error)return error;
        int finished=0;
        for(unsigned n=0;n<120;n++) {
            out=cold_status(pool);if(!out)return EIO;
            unsigned files=0;int expected=0,replacing=0,bad=0;
            if(out->status!=OZ_OK||out->next_cursor)bad=1;
            for(unsigned i=0;i<out->row_count;i++) {
                struct oz_row *r=&out->rows[i];size_t len=strlen(r->name);
                if(!strcmp(r->kind,"replacing"))replacing=1;
                if(strcmp(r->kind,"file"))continue;
                files++;if(strcmp(r->state,"ONLINE"))bad=1;
                if(len>=6&&!strcmp(r->name+len-6,step?"/disk3":"/disk2"))expected=1;
            }
            finished=!bad&&!replacing&&files==3&&expected&&
                strstr(out->detail,"scan=resilver state=finished")&&strstr(out->detail," errors=0 ");
            oz_message_free(out);if(finished)break;Delay(50);
        }
        if(!finished)return ETIMEDOUT;
        note(step?"Cold replace: resilver complete zero errors PASS":"Cold attach: resilver complete zero errors PASS");
    }
    attached=1;return check_health(pool,0);
}
static int creation_refusal_test(const char *pool)
{
    unsigned char *buf=malloc(1024*1024);BPTR file=0;int error=0;
#define PREFLIGHT_CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    PREFLIGHT_CHECK(buf);
    /* A nonzero byte well outside the leading labels must prevent ALL writes. */
    file=Open((CONST_STRPTR)"OZFILES:OpenZFS-test-51a20519/disk1",MODE_READWRITE);PREFLIGHT_CHECK(file);
    PREFLIGHT_CHECK(Seek(file,1024*1024,OFFSET_BEGINNING)>=0);
    buf[0]=0x5a;PREFLIGHT_CHECK(Write(file,buf,1)==1);
    PREFLIGHT_CHECK(Close(file));file=0;
    PREFLIGHT_CHECK(!request(5,pool,NULL,NULL,EEXIST));
    file=Open((CONST_STRPTR)"OZFILES:OpenZFS-test-51a20519/disk0",MODE_OLDFILE);PREFLIGHT_CHECK(file);
    for(unsigned block=0;block<64;block++) {
        PREFLIGHT_CHECK(Read(file,buf,1024*1024)==1024*1024);
        for(unsigned i=0;i<1024*1024;i++)PREFLIGHT_CHECK(!buf[i]);
    }
    PREFLIGHT_CHECK(Close(file));file=0;
    file=Open((CONST_STRPTR)"OZFILES:OpenZFS-test-51a20519/disk1",MODE_READWRITE);PREFLIGHT_CHECK(file);
    PREFLIGHT_CHECK(Seek(file,1024*1024,OFFSET_BEGINNING)>=0);
    PREFLIGHT_CHECK(Read(file,buf,1)==1&&buf[0]==0x5a);
    PREFLIGHT_CHECK(Seek(file,1024*1024,OFFSET_BEGINNING)>=0);
    buf[0]=0;PREFLIGHT_CHECK(Write(file,buf,1)==1);
    PREFLIGHT_CHECK(Close(file));file=0;
    note("Cold authenticated creation: nonblank refusal without partial labels PASS");
done:
    if(file)Close(file);
    free(buf);return error;
#undef PREFLIGHT_CHECK
}
static int cold_value(unsigned operation,const char *pool,const char *property,char *value,size_t size)
{
    struct oz_client client;struct oz_message m={0};
    if(!oz_client_open(&client))return ENOMEM;
    client.server_name=OZ_NATIVE_TEST_PORT;m.operation=operation;
    snprintf(m.target,sizeof(m.target),"%s",pool);
    snprintf(m.argument,sizeof(m.argument),"%s",property);
    int error=oz_submit(&client,&m)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;while(!(reply=oz_poll(&client)))Delay(1);
        if(reply->status!=OZ_OK||reply->row_count!=1||strlen(reply->rows[0].value)>=size)error=EIO;
        else strcpy(value,reply->rows[0].value);
        if(reply->status==OZ_ENGINE_ERROR&&!reply->row_count) {
            const char *reported=strstr(reply->detail,"errno=");
            if(reported) {
                long code=strtol(reported+6,NULL,10);
                if(code>0&&code<4096)error=(int)code;
            }
        }
        oz_message_free(reply);
    }
    oz_client_close(&client);return error;
}
static int cold_pool_value(const char *pool,const char *property,char *value,size_t size)
{return cold_value(31,pool,property,value,size);}

static int cold_promotion(const char *pool,int write)
{
    char source[128],clone[128],snapshot[160],moved[160];
    char guid[64],saved[64],origin[192],arg[128];
    snprintf(source,sizeof(source),"%s/promote-source",pool);
    snprintf(clone,sizeof(clone),"%s/promote-clone",pool);
    snprintf(snapshot,sizeof(snapshot),"%s@seed",source);
    snprintf(moved,sizeof(moved),"%s@seed",clone);
    int error=0;
    if(write) {
        if((error=request(37,source,NULL,NULL,0)))return error;
        if((error=request(50,snapshot,NULL,NULL,0)))return error;
        if((error=cold_value(40,snapshot,"guid",guid,sizeof(guid))))return error;
        snprintf(arg,sizeof(arg),"org.aros:promotion-guid=%s",guid);
        if((error=request(32,pool,arg,NULL,0)))return error;
        if((error=request(52,snapshot,clone,NULL,0)))return error;
        if((error=request(53,clone,NULL,NULL,0)))return error;
    } else if((error=request(53,source,NULL,NULL,EROFS)))return error;
    if((error=cold_value(40,source,"origin",origin,sizeof(origin))))return error;
    if(strcmp(origin,moved))return EIO;
    if((error=cold_value(40,clone,"origin",origin,sizeof(origin))))return error;
    if(strcmp(origin,"-"))return EIO;
    if((error=cold_value(40,moved,"guid",guid,sizeof(guid))))return error;
    if((error=cold_pool_value(pool,"org.aros:promotion-guid",saved,sizeof(saved))))return error;
    if(strcmp(saved,guid))return EIO;
    if(cold_value(40,snapshot,"guid",origin,sizeof(origin))!=ENOENT)return EIO;
    note(write?"Cold promotion: created origins and snapshot GUID PASS":
        "Cold promotion: fresh boot origins snapshot GUID and readonly refusal PASS");
    return 0;
}

static int cold_bookmark(const char *pool,int write)
{
    char dataset[128],snapshot[160],bookmark[160],denied[160],guid[64],arg[128];
    snprintf(dataset,sizeof(dataset),"%s/cold-bookmark",pool);
    snprintf(snapshot,sizeof(snapshot),"%s@seed",dataset);
    snprintf(bookmark,sizeof(bookmark),"%s#seed",dataset);
    snprintf(denied,sizeof(denied),"%s#denied",dataset);
    int error=0;
    if(write) {
        if((error=request(32,pool,"feature@bookmarks=enabled",NULL,0)))return error;
        if((error=request(37,dataset,NULL,NULL,0)))return error;
        if((error=request(50,snapshot,NULL,NULL,0)))return error;
        if((error=cold_value(40,snapshot,"guid",guid,sizeof(guid))))return error;
        snprintf(arg,sizeof(arg),"org.aros:bookmark-guid=%s",guid);
        if((error=request(32,pool,arg,NULL,0)))return error;
        if((error=request(51,snapshot,bookmark,NULL,0)))return error;
        if((error=request(51,snapshot,bookmark,NULL,EEXIST)))return error;
        if((error=request(38,snapshot,NULL,NULL,0)))return error;
    } else {
        if((error=request(38,bookmark,NULL,NULL,EROFS)))return error;
        if((error=request(51,snapshot,denied,NULL,EROFS)))return error;
    }
    if(cold_value(40,snapshot,"guid",guid,sizeof(guid))!=ENOENT)return EIO;
    if((error=cold_pool_value(pool,"org.aros:bookmark-guid",guid,sizeof(guid))))return error;
    struct oz_client client;struct oz_message *message=calloc(1,sizeof(*message));
    if(!message)return ENOMEM;
    if(!oz_client_open(&client)){free(message);return ENOMEM;}
    client.server_name=OZ_NATIVE_TEST_PORT;message->operation=36;
    snprintf(message->target,sizeof(message->target),"%s",dataset);
    strcpy(message->options,"bookmarks=on");
    error=oz_submit(&client,message)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;while(!(reply=oz_poll(&client)))Delay(1);
        unsigned long long actual=0,txg=0,created=0;
        if(reply->status!=OZ_OK||reply->row_count!=1||reply->next_cursor||
           strcmp(reply->rows[0].kind,"bookmark")||strcmp(reply->rows[0].name,bookmark)||
           sscanf(reply->rows[0].value,"guid=%llu createtxg=%llu creation=%llu",&actual,&txg,&created)!=3||
           actual!=strtoull(guid,NULL,10)||!txg||!created)error=EIO;
        oz_message_free(reply);
    }
    oz_client_close(&client);free(message);
    if(!error)note(write?"Cold bookmark: snapshot deleted and GUID retained PASS":
        "Cold bookmark: fresh boot GUID survived snapshot and readonly mutations denied PASS");
    return error;
}

static int cold_pool_reguid(const char *pool,int write)
{
    char before[64],after[64],saved[64];
    int error=cold_pool_value(pool,"guid",before,sizeof(before));if(error)return error;
    error=request(23,pool,NULL,NULL,write?0:EROFS);if(error)return error;
    error=cold_pool_value(pool,"guid",after,sizeof(after));if(error)return error;
    if(write) {
        if(!strcmp(before,after))return EIO;
        char arg[128];snprintf(arg,sizeof(arg),"org.aros:expectedguid=%s",after);
        error=request(32,pool,arg,NULL,0);if(error)return error;
    } else {
        error=cold_pool_value(pool,"org.aros:expectedguid",saved,sizeof(saved));if(error)return error;
        /* A split copies the source user property but deliberately generates
         * a new destination identity. Source must retain its saved GUID;
         * destination must differ, and neither may change on RO refusal. */
        if(strcmp(before,after)||(split_mode==3?!strcmp(saved,after):strcmp(saved,after)))return EIO;
    }
    char line[160];snprintf(line,sizeof(line),"Cold pool reguid write=%d guid=%s PASS",write,after);note(line);
    return 0;
}
static int cold_pool_properties(const char *pool,int write)
{
    static const char *names[]={"comment","org.aros:cold","dedup_table_quota"};
    static const char *values[]={"Cold native pool=a;b","Persistent pool user property","16777216"};
    for(unsigned i=0;i<3;i++) {
        char arg[128];snprintf(arg,sizeof(arg),"%s=%s",names[i],values[i]);
        int rc=request(32,pool,arg,NULL,write?0:EROFS);if(rc)return rc;
        struct oz_client client;struct oz_message m={0};
        if(!oz_client_open(&client))return ENOMEM;
        client.server_name=OZ_NATIVE_TEST_PORT;m.operation=31;
        snprintf(m.target,sizeof(m.target),"%s",pool);
        snprintf(m.argument,sizeof(m.argument),"%s",names[i]);
        rc=oz_submit(&client,&m)==OZ_OK?0:EIO;
        if(!rc) {
            struct oz_message *reply;while(!(reply=oz_poll(&client)))Delay(1);
            /* comment lives in labels and is not copied by upstream split;
             * the MOS user property and dedup quota remain copied. */
            const char *expected=split_mode==3&&i==0?"-":values[i];
            if(reply->status!=OZ_OK||reply->row_count!=1||strcmp(reply->rows[0].value,expected)||
                (split_mode==3&&i==0&&strcmp(reply->rows[0].state,"default"))) {
                char diagnostic[640];snprintf(diagnostic,sizeof(diagnostic),
                    "Cold property mismatch name=%s expected=%s actual=%s state=%s detail=%.200s",
                    names[i],expected,reply->rows[0].value,reply->rows[0].state,reply->detail);
                note(diagnostic);rc=EIO;
            }
            oz_message_free(reply);
        }
        oz_client_close(&client);if(rc)return rc;
    }
    note(write?"Cold pool properties: authenticated write/read PASS":"Cold pool properties: discovered label/MOS readback and RO refusal PASS");
    return 0;
}
static int cold_checkpoint(const char *pool,int write)
{
    int error;
    if(write) {
        char dataset[96];snprintf(dataset,sizeof(dataset),"%s/files",pool);
        error=request(43,dataset,"OZBEFORE","readonly=off",0);if(error)return error;
        BPTR file=Open((CONST_STRPTR)"OZBEFORE:before.txt",MODE_NEWFILE);if(!file)return EIO;
        int stored=Write(file,"Before checkpoint\n",18)==18&&Flush(file);
        int closed=Close(file);if(!stored||!closed)return EIO;
        error=request(44,dataset,NULL,NULL,0);if(error)return error;
        /* Mirror covers legacy checkpoint space maps; RAIDZ1 additionally
         * covers histogram-format maps and their nonzero feature refcount. */
        if(pool_layout==OZ_RAIDZ1) {
            error=request(32,pool,"feature@spacemap_histogram=enabled",NULL,0);if(error)return error;
        }
        error=request(32,pool,"feature@zpool_checkpoint=enabled",NULL,0);if(error)return error;
        error=request(22,pool,"create",NULL,0);if(error)return error;
        error=request(22,pool,"create",NULL,1024);if(error)return error;
        error=request(23,pool,NULL,NULL,1024);if(error)return error;
    } else {
        error=request(22,pool,"discard",NULL,EROFS);if(error)return error;
    }
    char state[32];error=cold_pool_value(pool,"feature@zpool_checkpoint",state,sizeof(state));
    if(error||strcmp(state,"active"))return error?error:EIO;
    struct oz_client client;struct oz_message m={0};
    if(!oz_client_open(&client))return ENOMEM;
    client.server_name=OZ_NATIVE_TEST_PORT;m.operation=4;
    snprintf(m.target,sizeof(m.target),"%s",pool);
    error=oz_submit(&client,&m)==OZ_OK?0:EIO;
    if(!error) {
        struct oz_message *reply;while(!(reply=oz_poll(&client)))Delay(1);
        if(reply->status!=OZ_OK||!strstr(reply->detail,"checkpoint=exists"))error=EIO;
        oz_message_free(reply);
    }
    oz_client_close(&client);
    if(!error)note(write?"Cold checkpoint create/conflicts/active PASS":"Cold checkpoint persisted/active/read-only refusal PASS");
    return error;
}
static int cold_rewind_view(const char *pool,const char *dataset,unsigned mode)
{
    int error=request(43,dataset,"OZREWIND",mode==12?"readonly=off":"readonly=on",0);
    if(error)return error;
    char bytes[32];BPTR file=Open((CONST_STRPTR)"OZREWIND:before.txt",MODE_OLDFILE);
    if(!file)return EIO;
    int valid=Read(file,bytes,sizeof(bytes))==18&&!memcmp(bytes,"Before checkpoint\n",18);
    int closed=Close(file);if(!valid||!closed)return EIO;
    file=Open((CONST_STRPTR)"OZREWIND:hello.txt",MODE_OLDFILE);
    if(mode==11) {if(!file)return EIO;if(!Close(file))return EIO;}
    else {if(file){Close(file);return EIO;}if(IoErr()!=ERROR_OBJECT_NOT_FOUND)return EIO;}
    BPTR lock=Lock((CONST_STRPTR)"OZREWIND:",ACCESS_READ);
    struct FileInfoBlock *fib=AllocDosObject(DOS_FIB,NULL);
    if(!lock||!fib){if(lock)UnLock(lock);if(fib)FreeDosObject(DOS_FIB,fib);return EIO;}
    int before=0,after=0;valid=Examine(lock,fib);
    while(valid&&ExNext(lock,fib)) {
        if(!strcmp((char *)fib->fib_FileName,"before.txt"))before=1;
        if(!strcmp((char *)fib->fib_FileName,"hello.txt"))after=1;
    }
    valid=valid&&IoErr()==ERROR_NO_MORE_ENTRIES&&before&&after==(mode==11);
    UnLock(lock);FreeDosObject(DOS_FIB,fib);if(!valid)return EIO;
    if(mode==12) {
        file=Open((CONST_STRPTR)"OZREWIND:rewound.txt",MODE_NEWFILE);if(!file)return EIO;
        valid=Write(file,"After rewind\n",13)==13&&Flush(file);
        closed=Close(file);if(!valid||!closed)return EIO;
    }
    if(mode==13) {
        file=Open((CONST_STRPTR)"OZREWIND:rewound.txt",MODE_OLDFILE);if(!file)return EIO;
        valid=Read(file,bytes,sizeof(bytes))==13&&!memcmp(bytes,"After rewind\n",13);
        closed=Close(file);if(!valid||!closed)return EIO;
    }
    if(mode!=12) {
        file=Open((CONST_STRPTR)"OZREWIND:forbidden",MODE_NEWFILE);
        if(file){Close(file);return EIO;}if(IoErr()!=ERROR_DISK_WRITE_PROTECTED)return EIO;
    }
    error=request(44,dataset,NULL,NULL,0);if(error)return error;
    error=request(8,pool,NULL,NULL,0);if(error)return error;
    char line[96];snprintf(line,sizeof(line),"Cold checkpoint rewind mode=%u payload/enumeration PASS",mode);note(line);
    return 0;
}
int main(void)
{
    int error=0,logged=0;BPTR file=0,lock=0;struct Library *secBase=NULL;
    char pool[64],dataset[96],layout[16];unsigned mode=0;
    static const char payload[]="OpenZFS on AROS: real ZPL file on a two-device QEMU mirror.\n";
    char bytes[sizeof(payload)];struct FileInfoBlock *fib=NULL;
#define CHECK(x) do {if(!(x)){error=__LINE__;goto done;}}while(0)
    file=Open((CONST_STRPTR)"SYS:Security/openzfs-test-only",MODE_OLDFILE);CHECK(file);
    CHECK(Close(file));file=0;
    FILE *config=fopen("SYS:cold-layout.txt","r");CHECK(config);
    int fields=fscanf(config,"%15s %u %u %u %u %u %u %u %u %u %u %u %u %u",layout,&member_count,&missing_count,&mode,&canonical_paths,&detach_test,&attach_test,&add_test,&aux_test,&remove_test,&removal_resume,&split_mode,&promotion_test,&relocated_files);fclose(config);
    CHECK(relocated_files<=1&&(!relocated_files||(mode==0&&canonical_paths&&!missing_count&&!detach_test&&!attach_test&&!add_test&&!promotion_test)));
    CHECK(promotion_test<=1&&(!promotion_test||(canonical_paths&&!missing_count&&!detach_test&&!attach_test&&!add_test&&(mode==0||mode==3))));
    CHECK(split_mode<=3&&(!split_mode||(add_test&&!aux_test&&!remove_test&&((split_mode==1&&mode==3)||(split_mode>=2&&mode==0)))));
    split_done=split_mode>=2;
    CHECK(removal_resume<=1&&(!removal_resume||remove_test));resume_phase=mode==14;
    CHECK(!resume_phase||removal_resume);
    CHECK(remove_test<=1&&(!remove_test||(add_test&&!aux_test)));
    CHECK(aux_test<=1&&(!aux_test||add_test));
    CHECK(add_test<=1&&(!add_test||(!attach_test&&!detach_test&&canonical_paths&&!strcmp(layout,"mirror")&&missing_count==0&&(mode==0||mode==3||resume_phase))));
    CHECK(attach_test<=1&&(!attach_test||(!detach_test&&!strcmp(layout,"mirror")&&missing_count==0&&(mode==0||mode==3))));
    CHECK(detach_test<=1&&(!detach_test||(!strcmp(layout,"mirror")&&missing_count==0&&(mode==0||mode==3))));
    CHECK(fields>=3&&mode<=14&&canonical_paths<=1&&missing_count<=member_count&&(((!strcmp(layout,"mirror")||!strcmp(layout,"stripe"))&&member_count==2)||
        (!strcmp(layout,"raidz1")&&member_count==3)||
        (!strcmp(layout,"raidz2")&&member_count==4)||
        (!strcmp(layout,"raidz3")&&member_count==5)));
    snprintf(pool,sizeof(pool),"aros_%s_51a20519",layout);
    if(split_mode==3)strcpy(pool,"aros_split_51a20519");
    pool_layout=!strcmp(layout,"stripe")?OZ_STRIPE:!strcmp(layout,"mirror")?OZ_MIRROR:
        !strcmp(layout,"raidz1")?OZ_RAIDZ1:!strcmp(layout,"raidz2")?OZ_RAIDZ2:OZ_RAIDZ3;
    snprintf(dataset,sizeof(dataset),"%s/files",pool);
    note(pool);
    secBase=OpenLibrary((CONST_STRPTR)SECURITYNAME,SECURITYVERSION);CHECK(secBase&&secIsConfigured());
    struct TagItem login_tags[]={{secT_UserID,(IPTR)"oztestroot"},{secT_Password,(IPTR)""},{TAG_DONE,0}};
    CHECK(secLoginA(login_tags)==secOWNER_SYSTEM);logged=1;
#ifdef OZ_COLD_RELEASE
    CHECK(!present());
    CHECK(SetVar((CONST_STRPTR)"ZFS_HOSTID",(CONST_STRPTR)"0x51a20519",-1,0));
    CHECK(SystemTags((CONST_STRPTR)"Run >T:cold-service.log C:OpenZFSServiceNative WRITABLE",TAG_DONE)==0);
#endif
    for(unsigned i=0;i<1500&&!present();i++)Delay(1);
    CHECK(present());
#ifdef OZ_COLD_RELEASE
    note("Cold release service: public admission ready; diagnostic harness absent");
#endif
    if(relocated_files)for(unsigned i=0;i<member_count;i++) {
        char old[128];snprintf(old,sizeof(old),"OZFILES:OpenZFS-test-51a20519/disk%u",i);
        BPTR stale=Lock((CONST_STRPTR)old,SHARED_LOCK);
        if(stale)UnLock(stale);
        CHECK(!stale&&IoErr()==ERROR_OBJECT_NOT_FOUND);
    }
    if(mode==5) {
        CHECK(!request(7,pool,NULL,"readonly=on",ENOENT));
        CHECK(!request(7,pool,NULL,"readonly=off",ENOENT));
        CHECK(!check_health(pool,1));
        CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        note("Cold authenticated destroy: fresh-service import refusal and lock release PASS");
        goto done;
    }
    if(mode==7) {
        CHECK(!request(7,pool,NULL,"readonly=on",ENOENT));
        CHECK(!request(7,pool,NULL,"readonly=off",ENOENT));
    }
    if(mode==13) {
        CHECK(!request(7,pool,NULL,"readonly=on;rewind=on",1026));
        CHECK(!request(7,pool,NULL,"readonly=off;rewind=on",1026));
        note("Cold checkpoint absent read-only/writable rewind refusal PASS");
    }
    if(mode==3||mode==6)missing_count=0; /* all blank files exist during creation */
    unsigned tolerance=!strcmp(layout,"mirror")?1:member_count-2;
    if(missing_count>tolerance) {
        /* Discovery cannot refresh an importable config beyond redundancy;
         * its selector reports no pool, before final spa_import is reached. */
        CHECK(!request(7,pool,NULL,"readonly=off",ENOENT));
        CHECK(!request(7,pool,NULL,"readonly=on",ENOENT));
        CHECK(!check_health(pool,1));
        CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        note("Cold authenticated import: beyond-redundancy refusal and lock release PASS");
        goto done;
    }
    if(mode==3||mode==6) {
#ifdef OZ_COLD_FLUSH_REFUSAL
        CHECK(mode==3);
        CHECK(!request(5,pool,NULL,NULL,EIO));
        CHECK(!check_health(pool,1));
        CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        note("Cold writable file backing: unsupported flush refused PASS");
        goto done;
#endif
        CHECK(!creation_refusal_test(pool));
        CHECK(!request(5,pool,NULL,NULL,0));
        CHECK(!request(37,dataset,NULL,NULL,0));
    } else CHECK(!request(7,pool,NULL,mode==10?"readonly=on;rewind=on":mode==12?"readonly=off;rewind=on":mode==7?"readonly=on;destroyed=on":
        mode==8?"readonly=off;destroyed=on":mode==1||mode==4||resume_phase?"readonly=off":"readonly=on",0));
    if(detach_test&&mode==0)detached=1;
    if(add_test&&mode==0)expanded=1;
    if(remove_test&&mode==0)removed=1;
    if(resume_phase) {
        CHECK(!cold_removal_wait(pool));removed=1;expanded=1;
        note("Cold removal resume: fresh writable import completed persisted evacuation PASS");
    }
    if(aux_test&&mode==0){aux_added=1;aux_readonly=1;}
    if(attach_test&&mode==0){attached=1;missing_count=1;}
    CHECK(!check_health(pool,0));
    if(relocated_files)note("Cold relocation: old names absent, discovered moved members ONLINE PASS");
    if(promotion_test)CHECK(!cold_promotion(pool,mode==3));
    if(promotion_test)CHECK(!cold_bookmark(pool,mode==3));
    if(mode>=10&&mode<=13) {
        CHECK(!cold_rewind_view(pool,dataset,mode));
        CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
        goto done;
    }
    if(canonical_paths&&(mode==0||mode==3||mode==6||mode==7||mode==9)) {
        CHECK(!cold_pool_reguid(pool,mode==3||mode==6));
        CHECK(!cold_pool_properties(pool,mode==3||mode==6));
        if(!detach_test&&!attach_test&&!add_test)CHECK(!cold_checkpoint(pool,mode==3||mode==6));
    }
    CHECK(!request(24,pool,NULL,"scrub_restart=off",0));
    CHECK(!check_health(pool,0));
    CHECK(!request(43,dataset,"OZCOLD",mode==1||mode==3||mode==6||mode==8?"readonly=off":"readonly=on",0));
    if(mode==3||mode==6) {
        file=Open((CONST_STRPTR)"OZCOLD:hello.txt",MODE_NEWFILE);CHECK(file);
        CHECK(Write(file,(APTR)payload,sizeof(payload)-1)==sizeof(payload)-1);
        if(mode==6) {
            struct FileInfoBlock *live=AllocDosObject(DOS_FIB,NULL);CHECK(live);
            int examined=ExamineFH(file,live);
            LONG live_size=live->fib_Size;
            FreeDosObject(DOS_FIB,live);
            char detail[128];snprintf(detail,sizeof(detail),
                "Pre-sync live size=%ld expected=%lu examined=%d",
                (long)live_size,(unsigned long)(sizeof(payload)-1),examined);
            note(detail);CHECK(examined&&live_size==sizeof(payload)-1);
            CHECK(!request(26,pool,NULL,NULL,0));
#ifdef OZ_COLD_POST_SYNC_FLUSH
            CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
            note("Diagnostic post-pool-sync filesystem flush acknowledged");
#endif
            /* Deliberately keep file/mount/pool open. Host kills ONLY this
             * private guest after the marker: no Flush, Close or export. */
            note("Cold authenticated creation: native DOS payload PASS");
            note("RESULT authenticated pool sync ready for guest interruption");
            for(;;)Delay(50);
        }
        CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
        CHECK(Close(file));file=0;
        note("Cold authenticated creation: native DOS payload PASS");
    }
    file=Open((CONST_STRPTR)"OZCOLD:hello.txt",MODE_OLDFILE);CHECK(file);
    {
        LONG actual=Read(file,bytes,sizeof(bytes)),reason=IoErr();
        char detail[128];snprintf(detail,sizeof(detail),
            "Cold payload read actual=%ld expected=%lu IoErr=%ld mode=%u",
            (long)actual,(unsigned long)(sizeof(payload)-1),(long)reason,mode);
        note(detail);
        CHECK(actual==sizeof(payload)-1);
    }
    CHECK(!memcmp(bytes,payload,sizeof(payload)-1));CHECK(Close(file));file=0;
    note("Cold authenticated import: exact native file read PASS");
    fib=AllocDosObject(DOS_FIB,NULL);CHECK(fib);
    lock=Lock((CONST_STRPTR)"OZCOLD:",ACCESS_READ);CHECK(lock&&Examine(lock,fib));
    int found=0;
    while(ExNext(lock,fib))if(!strcmp((const char *)fib->fib_FileName,"hello.txt"))found=1;
    CHECK(found&&IoErr()==ERROR_NO_MORE_ENTRIES);UnLock(lock);lock=0;
    note("Cold authenticated import: native directory enumeration PASS");
    if(mode==1||mode==2) {
        unsigned char block[4096];
        if(mode==1) {
            file=Open((CONST_STRPTR)"OZCOLD:degraded.bin",MODE_NEWFILE);CHECK(file);
            for(unsigned n=0;n<64;n++) {
                for(unsigned i=0;i<sizeof(block);i++)block[i]=(unsigned char)((n*29+i*11)^(i>>4));
                CHECK(Write(file,block,sizeof(block))==sizeof(block));
            }
            CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
            CHECK(Close(file));file=0;
        }
        file=Open((CONST_STRPTR)"OZCOLD:degraded.bin",MODE_OLDFILE);CHECK(file);
        for(unsigned n=0;n<64;n++) {
            CHECK(Read(file,block,sizeof(block))==sizeof(block));
            for(unsigned i=0;i<sizeof(block);i++)CHECK(block[i]==(unsigned char)((n*29+i*11)^(i>>4)));
        }
        CHECK(Read(file,block,1)==0);CHECK(Close(file));file=0;
        note(mode==1?"Cold authenticated import: writable degraded payload PASS":
            "Cold authenticated import: cold degraded payload PASS");
    }
    if(mode==8||mode==9) {
        const char recovered[]="Explicit destroyed-pool recovery\n";
        char readback[sizeof(recovered)];
        if(mode==8) {
            file=Open((CONST_STRPTR)"OZCOLD:recovered.txt",MODE_NEWFILE);CHECK(file);
            CHECK(Write(file,(APTR)recovered,sizeof(recovered)-1)==sizeof(recovered)-1);
            CHECK(DoPkt(((struct FileHandle *)BADDR(file))->fh_Type,ACTION_FLUSH,0,0,0,0,0));
            CHECK(Close(file));file=0;
        }
        file=Open((CONST_STRPTR)"OZCOLD:recovered.txt",MODE_OLDFILE);CHECK(file);
        CHECK(Read(file,readback,sizeof(readback))==sizeof(recovered)-1);
        CHECK(!memcmp(readback,recovered,sizeof(recovered)-1));CHECK(Close(file));file=0;
        note(mode==8?"Cold destroyed recovery: writable payload PASS":"Cold destroyed recovery: normal import payload PASS");
    }
    if(add_test)CHECK(!cold_expansion(pool,mode==3));
    if(mode==0||mode==2||mode==7||mode==9) {
        file=Open((CONST_STRPTR)"OZCOLD:forbidden",MODE_NEWFILE);
        CHECK(!file&&IoErr()==ERROR_DISK_WRITE_PROTECTED);
    }
    CHECK(!request(8,pool,NULL,NULL,EBUSY));
    if(mode==4)CHECK(!request(6,pool,NULL,NULL,EBUSY));
    CHECK(!request(44,dataset,NULL,NULL,0));
    if(split_mode==1)CHECK(!cold_split(pool));
    if(remove_test) {
        if(mode==3)CHECK(!cold_remove(pool));
        else if(resume_phase)note("Cold removal resume: completed topology and native DOS payload PASS");
        else note("Cold removal: original files absent, surviving-only import and exact native DOS payload PASS");
    }
    if(detach_test) {
        if(mode==3)CHECK(!cold_detach(pool));
        else note("Cold detach: fresh import topology and DOS payload PASS");
    }
    if(attach_test) {
        if(mode==3)CHECK(!cold_attach_replace(pool));
        else note("Cold attach/replace: original members absent, new members native DOS read/enumeration PASS");
    }
    if(mode==4) {
        CHECK(!request(6,pool,NULL,NULL,0));
        CHECK(!check_health(pool,1));
        CHECK(!request(6,pool,NULL,NULL,ENOENT));
        note("Cold authenticated destroy: namespace removed PASS");
    } else CHECK(!request(8,pool,NULL,NULL,0));
    if(mode==7) {
        CHECK(!request(7,pool,NULL,"readonly=on",ENOENT));
        CHECK(!request(7,pool,NULL,"readonly=off",ENOENT));
        note("Cold destroyed recovery: read-only recovery retains destroyed state PASS");
    }
    if(mode==3)CHECK(!request(5,pool,NULL,NULL,EEXIST)); /* never overwrite exported labels */
    /* Current DOS retirement contract intentionally keeps service ports alive
     * after unmount. Export must nevertheless release the backing locks. */
    Forbid();struct MsgPort *port=FindPort((CONST_STRPTR)OZ_ADMISSION_TEST_PORT);
    if(port)Signal(port->mp_SigTask,SIGBREAKF_CTRL_C);
    Permit();Delay(5);CHECK(present());
    CHECK(Inhibit((CONST_STRPTR)"OZBACK:",DOSTRUE));
    note("Cold authenticated import: exported backing volume released; retired DOS service resident");
done:
    if(file)Close(file);
    if(lock)UnLock(lock);
    if(fib)FreeDosObject(DOS_FIB,fib);
    if(logged){struct TagItem tags[]={{secT_Quiet,TRUE},{TAG_DONE,0}};secLogoutA(tags);}
    if(secBase)CloseLibrary(secBase);
    char result[128];snprintf(result,sizeof(result),"Cold API client error=%d\nRESULT cold authenticated import %s",error,error?"FAIL":"PASS");note(result);
    return error?20:0;
#undef CHECK
}
