/* SPDX-License-Identifier: CDDL-1.0 */
#include "admission-api.h"
#include "ioctl-api.h"
#include "mount-api.h"
#include "support-api.h"
#include "partition-api.h"
#include "gpt-api.h"
#include "zvol-api.h"
#include "handler-api.h"
#include <dos/dosextens.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <errno.h>
#include <string.h>
#include <stdlib.h>
static int request(const char *name, LONG action, IPTR a, IPTR b, IPTR c)
{
    if(!name)return EINVAL;
    struct MsgPort *reply=CreateMsgPort();
    if(!reply)return ENOMEM;
    struct StandardPacket p={0};
    p.sp_Msg.mn_Node.ln_Name=(void *)&p.sp_Pkt;
    p.sp_Msg.mn_Length=sizeof(p);
    p.sp_Pkt.dp_Link=&p.sp_Msg;p.sp_Pkt.dp_Type=action;
    p.sp_Pkt.dp_Arg1=a;p.sp_Pkt.dp_Arg2=b;p.sp_Pkt.dp_Arg3=c;
    /* Match server removal's exclusion. SendPkt only queues, never waits. */
    Forbid();
    struct MsgPort *server=FindPort((CONST_STRPTR)name);
    if(server)SendPkt(&p.sp_Pkt,server,reply);
    Permit();
    int error=ENOENT;
    if(server) {
        WaitPort(reply);
        struct Message *m=GetMsg(reply);
        error=m==&p.sp_Msg?(int)p.sp_Pkt.dp_Res2:EIO;
        if(!error&&p.sp_Pkt.dp_Res1!=DOSTRUE)error=EIO;
    }
    DeleteMsgPort(reply);
    return error;
}
int oz_handler_attach_request(const char *port,struct oz_handler_attach *query)
{
    if(!query||query->endpoint)return EINVAL;
    struct oz_handler_attach local=*query;
    int error=request(port,OZ_HANDLER_ATTACH,(IPTR)&local,sizeof(local),0);
    if(!error&&!local.endpoint)return EPROTO;
    if(!error)query->endpoint=local.endpoint;
    return error;
}
int oz_admission_list(const char *port,struct oz_message *out)
{
    if(!out)return EINVAL;
    struct oz_message *local=calloc(1,sizeof(*local));if(!local)return ENOMEM;
    int error=request(port,OZ_ADMISSION_LIST,(IPTR)local,sizeof(*local),0);
    if(!error&&(local->magic!=OZ_MAGIC||local->version!=OZ_VERSION||local->operation!=76||
        local->row_count>OZ_ADMISSION_LIST_MAX||local->row_count>OZ_MAX_ROWS))error=EPROTO;
    if(!error)*out=*local;
    free(local);return error;
}
int oz_zvol_request(const char *port,void *query,ULONG size)
{
    if(!query||size<sizeof(struct oz_zvol_request)||
        size>sizeof(struct oz_zvol_request)+OZ_ZVOL_IO_MAX)return EINVAL;
    void *local=malloc(size);if(!local)return ENOMEM;
    memcpy(local,query,size);
    int error=request(port,OZ_ZVOL_ACTION,(IPTR)local,size,0);
    if(!error)memcpy(query,local,size);
    memset(local,0,size);free(local);return error;
}
int oz_gpt_request(const char *port,struct oz_gpt_query *query)
{
    if(!query)return EINVAL;
    struct oz_gpt_query local=*query;
    int error=request(port,OZ_GPT_ACTION,(IPTR)&local,sizeof(local),0);
    if(!error&&local.changed>1)error=EPROTO;
    if(!error)query->changed=local.changed;
    return error;
}
int oz_device_handle_open(const char *port,const struct oz_ticket *ticket,
    const char *device,int32_t unit,LONG *fd,struct oz_device_geometry *geometry)
{
    if(!ticket||!device||!*device||strlen(device)>255||unit<0||!fd||!geometry)return EINVAL;
    *fd=-1;
    struct oz_device_handle_open q={0};q.session=*ticket;q.descriptor=-1;
    q.request.version=OZ_PARTITION_QUERY_VERSION;q.request.unit=unit;
    strcpy(q.request.device,device);
    int error=request(port,OZ_DEVICE_HANDLE_OPEN,(IPTR)&q,sizeof(q),0);
    if(!error){*fd=q.descriptor;*geometry=q.geometry;}
    return error;
}
int oz_device_handle_read(const char *port,const struct oz_ticket *ticket,LONG fd,
    uint64_t offset,void *out,ULONG count)
{
    if(!ticket||fd<=0||!out||!count||count>OZ_DEVICE_READ_MAX)return EINVAL;
    ULONG size=sizeof(struct oz_device_handle_read)+count;
    struct oz_device_handle_read *q=calloc(1,size);if(!q)return ENOMEM;
    q->session=*ticket;q->descriptor=fd;q->offset=offset;q->count=count;
    int error=request(port,OZ_DEVICE_HANDLE_READ,(IPTR)q,size,0);
    if(!error)memcpy(out,q+1,count);
    memset(q,0,size);free(q);return error;
}
int oz_device_read_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_DEVICE_READ_ACTION,(IPTR)query,size,0);
}
int oz_device_read_bytes(const char *port,const char *device,int32_t unit,
    uint64_t offset,void *out,ULONG count)
{
    if(!device||!*device||strlen(device)>255||unit<0||!out||
       !count||count>OZ_DEVICE_READ_MAX)return EINVAL;
    ULONG size=sizeof(struct oz_device_read_query)+count;
    struct oz_device_read_query *query=calloc(1,size);
    if(!query)return ENOMEM;
    query->request.version=OZ_PARTITION_QUERY_VERSION;
    query->request.unit=unit;strcpy(query->request.device,device);
    query->offset=offset;query->count=count;
    int error=oz_device_read_request(port,query,size);
    if(!error)memcpy(out,query+1,count);
    memset(query,0,size);free(query);
    return error;
}
int oz_device_geometry_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_DEVICE_GEOMETRY_ACTION,(IPTR)query,size,0);
}
int oz_storage_inventory_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_STORAGE_INVENTORY_ACTION,(IPTR)query,size,0);
}
int oz_storage_inventory_read(const char *port,struct oz_storage_inventory *out)
{
    if(!out)return EINVAL;
    struct oz_storage_inventory_query *query=calloc(1,sizeof(*query));
    if(!query)return ENOMEM;
    query->version=1;
    int error=oz_storage_inventory_request(port,query,sizeof(*query));
    if(!error&&(query->inventory.version!=1||query->inventory.count>OZ_STORAGE_INVENTORY_MAX))error=EPROTO;
    for(uint32_t i=0;!error&&i<query->inventory.count;i++) {
        const struct oz_storage_unit *u=&query->inventory.units[i];
        if(u->reserved||u->unit>INT32_MAX||u->removable>1||!u->device[0]||
           !memchr(u->device,0,sizeof(u->device))||!memchr(u->model,0,sizeof(u->model))||
           !memchr(u->serial,0,sizeof(u->serial)))error=EPROTO;
        for(uint32_t j=0;!error&&j<i;j++)
            if(u->unit==query->inventory.units[j].unit&&
               !strcmp(u->device,query->inventory.units[j].device))error=EPROTO;
    }
    if(!error)*out=query->inventory;
    free(query);return error;
}
int oz_device_dos_claim_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_DEVICE_DOS_CLAIM_ACTION,(IPTR)query,size,0);
}
int oz_device_dos_claim_read(const char *port,const char *device,int32_t unit)
{
    if(!device||!*device||strlen(device)>255||unit<0)return EINVAL;
    struct oz_partition_request query={.version=OZ_PARTITION_QUERY_VERSION,.unit=unit};
    strcpy(query.device,device);
    return oz_device_dos_claim_request(port,&query,sizeof(query));
}
int oz_device_inquiry_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_DEVICE_INQUIRY_ACTION,(IPTR)query,size,0);
}
int oz_device_inquiry_read(const char *port,const char *device,int32_t unit,
    struct oz_device_inquiry *out)
{
    if(!device||!*device||strlen(device)>255||unit<0||!out)return EINVAL;
    struct oz_device_inquiry_query query={0};
    query.request.version=OZ_PARTITION_QUERY_VERSION;
    query.request.unit=unit;strcpy(query.request.device,device);
    int error=oz_device_inquiry_request(port,&query,sizeof(query));
    if(!error&&(query.inquiry.version!=OZ_PARTITION_QUERY_VERSION||
       query.inquiry.length<36||query.inquiry.length>sizeof(query.inquiry.data)))error=EPROTO;
    if(!error)*out=query.inquiry;
    return error;
}
int oz_device_geometry_read(const char *port,const char *device,int32_t unit,
    struct oz_device_geometry *out)
{
    if(!device||!*device||strlen(device)>255||unit<0||!out)return EINVAL;
    struct oz_device_geometry_query query={0};
    query.request.version=OZ_PARTITION_QUERY_VERSION;
    query.request.unit=unit;memcpy(query.request.device,device,strlen(device)+1);
    int error=oz_device_geometry_request(port,&query,sizeof(query));
    const struct oz_device_geometry *g=&query.geometry;
    if(!error&&(g->version!=OZ_PARTITION_QUERY_VERSION||g->sector_size<512||
       g->sector_size>4096||(g->sector_size&(g->sector_size-1))||
       !g->bytes||g->bytes%g->sector_size))error=EPROTO;
    if(!error)*out=*g;
    return error;
}
int oz_partitions_request(const char *port,void *query,ULONG size)
{
    if(!query)return EINVAL;
    return request(port,OZ_PARTITION_ACTION,(IPTR)query,size,0);
}
int oz_partitions_read(const char *port,const char *device,int32_t unit,
    struct oz_partition_catalog *out)
{
    if(!device||!*device||strlen(device)>255||unit<0||!out)return EINVAL;
    struct oz_partition_query *query=calloc(1,sizeof(*query));
    if(!query)return ENOMEM;
    query->request.version=OZ_PARTITION_QUERY_VERSION;
    query->request.unit=unit;
    strcpy(query->request.device,device);
    int error=oz_partitions_request(port,query,sizeof(*query));
    if(!error&&(query->catalog.version!=OZ_PARTITION_CATALOG_VERSION||
       query->catalog.count>OZ_PARTITION_CATALOG_MAX))error=EPROTO;
    if(!error)memcpy(out,&query->catalog,sizeof(*out));
    free(query);return error;
}
int oz_admission_submit(const char *name,const struct oz_message *m,struct oz_ticket *ticket)
{
    if(!m||!ticket)return EINVAL;
    memset(ticket,0,sizeof(*ticket));
    return request(name,OZ_ADMISSION_ACTION,(IPTR)m,sizeof(*m),(IPTR)ticket);
}
int oz_admission_query(const char *name,const struct oz_ticket *ticket,struct oz_message *out)
{
    if(!ticket||!out)return EINVAL;
    return request(name,OZ_ADMISSION_QUERY,(IPTR)ticket,(IPTR)out,sizeof(*out));
}
int oz_admission_release(const char *name,const struct oz_ticket *ticket)
{
    if(!ticket)return EINVAL;
    return request(name,OZ_ADMISSION_RELEASE,(IPTR)ticket,0,0);
}
int oz_admission_cancel(const char *name,const struct oz_ticket *ticket)
{
    if(!ticket||!ticket->epoch||!ticket->number)return EINVAL;
    return request(name,OZ_ADMISSION_CANCEL,(IPTR)ticket,0,0);
}
int oz_session_open(const char *name,struct oz_ticket *ticket)
{
    if(!ticket)return EINVAL;
    memset(ticket,0,sizeof(*ticket));
    return request(name,OZ_SESSION_OPEN,(IPTR)ticket,sizeof(*ticket),0);
}
int oz_session_close(const char *name,const struct oz_ticket *ticket)
{
    if(!ticket)return EINVAL;
    return request(name,OZ_SESSION_CLOSE,(IPTR)ticket,sizeof(*ticket),0);
}
int oz_control_open(const char *name,const struct oz_ticket *ticket,LONG *descriptor)
{
    if(!ticket||!descriptor)return EINVAL;
    *descriptor=-1;
    struct oz_control_request payload={.session=*ticket,.descriptor=-1};
    int error=request(name,OZ_CONTROL_OPEN,(IPTR)&payload,sizeof(payload),0);
    if(!error) {
        if(payload.descriptor<=0)return EIO;
        *descriptor=payload.descriptor;
    }
    return error;
}
int oz_control_close(const char *name,const struct oz_ticket *ticket,LONG descriptor)
{
    if(!ticket||descriptor<=0)return EINVAL;
    struct oz_control_request payload={.session=*ticket,.descriptor=descriptor};
    return request(name,OZ_CONTROL_CLOSE,(IPTR)&payload,sizeof(payload),0);
}
int oz_ioctl_request(const char *name,void *buffer,size_t length)
{
    if(!buffer||length<sizeof(struct oz_ioctl_wire))return EINVAL;
    return request(name,OZ_IOCTL_ACTION,(IPTR)buffer,(IPTR)length,0);
}
int oz_hostid_query(const char *port,ULONG *hostid)
{
    if(!hostid)return EINVAL;
    *hostid=0;
    struct oz_hostid_query query={.version=OZ_SUPPORT_VERSION};
    int error=request(port,OZ_HOSTID_ACTION,(IPTR)&query,sizeof(query),0);
    if(error)return error;
    if(query.version!=OZ_SUPPORT_VERSION)return EPROTO;
    *hostid=query.hostid;return 0;
}
int oz_engine_version_query(const char *port,char *engine,size_t capacity)
{
    if(!engine||!capacity)return EINVAL;
    engine[0]=0;
    struct oz_engine_version_query query={.version=OZ_SUPPORT_VERSION};
    int error=request(port,OZ_ENGINE_VERSION_ACTION,(IPTR)&query,sizeof(query),0);
    if(error)return error;
    if(query.version!=OZ_SUPPORT_VERSION||query.reserved||!query.engine[0]||
       !memchr(query.engine,0,sizeof(query.engine)))return EPROTO;
    size_t length=strlen(query.engine);
    if(length>=capacity)return ENOSPC;
    memcpy(engine,query.engine,length+1);return 0;
}
int oz_support_query(const char *port,const char *scope,const char *name,int *supported)
{
    if(!scope||!name||!supported)return EINVAL;
    *supported=0;
    struct oz_support_query query={.version=OZ_SUPPORT_VERSION};
    if(!*scope||!*name)return EINVAL;
    if(strlen(scope)>=sizeof(query.scope)||strlen(name)>=sizeof(query.name))return ENAMETOOLONG;
    strcpy(query.scope,scope);strcpy(query.name,name);
    int error=request(port,OZ_SUPPORT_ACTION,(IPTR)&query,sizeof(query),0);
    if(error)return error;
    if(query.version!=OZ_SUPPORT_VERSION||query.supported>1||
       memcmp(query.scope,scope,strlen(scope)+1)||memcmp(query.name,name,strlen(name)+1))return EPROTO;
    *supported=query.supported;return 0;
}
int oz_mount_control(const char *port,const char *dataset,const char *volume,ULONG operation,ULONG flags)
{
    if(!dataset||!*dataset||!volume)return EINVAL;
    struct oz_mount_control query={.version=OZ_MOUNTS_VERSION,.operation=operation,.flags=flags};
    if(strlen(dataset)>=sizeof(query.dataset)||strlen(volume)>=sizeof(query.volume))return ENAMETOOLONG;
    if((operation!=OZ_MOUNT_ATTACH&&operation!=OZ_MOUNT_DETACH)||
       flags&~OZ_MOUNT_READONLY||(operation==OZ_MOUNT_DETACH&&(flags||*volume)))return EINVAL;
    strcpy(query.dataset,dataset);strcpy(query.volume,volume);
    return request(port,OZ_MOUNT_CONTROL_ACTION,(IPTR)&query,sizeof(query),0);
}
int oz_mount_policy_query(const char *port,const char *dataset,ULONG *supported,ULONG *enabled)
{
    if(!supported||!enabled)return EINVAL;
    *supported=*enabled=0;
    if(!dataset||!*dataset)return EINVAL;
    struct oz_mount_policy_query query={.version=OZ_MOUNTS_VERSION};
    if(strlen(dataset)>=sizeof(query.dataset))return ENAMETOOLONG;
    strcpy(query.dataset,dataset);
    int error=request(port,OZ_MOUNT_POLICY_ACTION,(IPTR)&query,sizeof(query),0);
    if(error)return error;
    if(query.version!=OZ_MOUNTS_VERSION||query.reserved||
       query.supported&~OZ_POLICY_KNOWN||query.enabled&~query.supported||
       memcmp(query.dataset,dataset,strlen(dataset)+1))return EPROTO;
    *supported=query.supported;*enabled=query.enabled;return 0;
}
int oz_mount_shares_query(const char *port,const char *dataset,UQUAD *object)
{
    if(!object)return EINVAL;
    *object=0;
    if(!dataset||!*dataset)return EINVAL;
    struct oz_mount_shares_query query={.version=OZ_MOUNTS_VERSION};
    if(strlen(dataset)>=sizeof(query.dataset))return ENAMETOOLONG;
    strcpy(query.dataset,dataset);
    int error=request(port,OZ_MOUNT_SHARES_ACTION,(IPTR)&query,sizeof(query),0);
    if(error)return error;
    if(query.version!=OZ_MOUNTS_VERSION||query.reserved||
       memcmp(query.dataset,dataset,strlen(dataset)+1))return EPROTO;
    *object=query.object;return 0;
}
int oz_mounts_query(const char *name,void *buffer,size_t length)
{
    if(!buffer||length<sizeof(struct oz_mount_snapshot))return EINVAL;
    return request(name,OZ_MOUNTS_ACTION,(IPTR)buffer,(IPTR)length,0);
}
int oz_mounts_read(const char *name,struct oz_mount_snapshot **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    ULONG capacity=0;
    for(unsigned attempt=0;attempt<8;attempt++) {
        size_t length=sizeof(struct oz_mount_snapshot)+(size_t)capacity*sizeof(struct oz_mount_record);
        struct oz_mount_snapshot *table=calloc(1,length);
        if(!table)return ENOMEM;
        table->version=OZ_MOUNTS_VERSION;table->capacity=capacity;
        int error=oz_mounts_query(name,table,length);
        if(!error||error==ENOSPC) {
            if(table->version!=OZ_MOUNTS_VERSION||table->capacity!=capacity||
               table->reserved||table->count>OZ_MOUNTS_LIMIT)error=EPROTO;
        }
        if(error==ENOSPC) {
            if(table->count<=capacity){free(table);return EPROTO;}
            capacity=table->count;free(table);continue;
        }
        if(!error) {
            if(table->count>capacity)error=EPROTO;
            struct oz_mount_record *records=(void *)(table+1);
            for(ULONG i=0;!error&&i<table->count;i++) {
                struct oz_mount_record *r=&records[i];
                if(!memchr(r->dataset,0,sizeof(r->dataset))||
                   !memchr(r->mountpoint,0,sizeof(r->mountpoint))||
                   !r->dataset[0]||!r->mountpoint[0]||
                   (r->flags&~OZ_MOUNT_READONLY))error=EPROTO;
            }
        }
        if(error){free(table);return error;}
        *out=table;return 0;
    }
    return EAGAIN;
}
int oz_mounts_record_for_root(const char *name,const char *path,struct oz_mount_record *record)
{
    if(!record)return EINVAL;
    memset(record,0,sizeof(*record));
    if(!path||!*path)return EINVAL;
    BPTR object=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(!object)return IoErr()==ERROR_READ_PROTECTED?EACCES:ENOENT;
    struct oz_mount_snapshot *table=NULL;
    int error=oz_mounts_read(name,&table);
    if(!error) {
        error=ENODEV;
        struct oz_mount_record *records=(void *)(table+1);
        for(ULONG i=0;i<table->count;i++) {
            BPTR root=Lock((CONST_STRPTR)records[i].mountpoint,SHARED_LOCK);
            if(!root)continue;
            LONG same=SameLock(object,root);
            UnLock(root);
            if(same==LOCK_SAME){*record=records[i];error=0;break;}
        }
        free(table);
    }
    UnLock(object);return error;
}
int oz_mounts_dataset_for_path(const char *name,const char *path,char *dataset,size_t size)
{
    if(!dataset||!size)return EINVAL;
    dataset[0]=0;
    if(!path||!*path)return EINVAL;
    BPTR object=Lock((CONST_STRPTR)path,SHARED_LOCK);
    if(!object) {
        switch(IoErr()) {
        case ERROR_OBJECT_NOT_FOUND:case ERROR_DIR_NOT_FOUND:return ENOENT;
        case ERROR_READ_PROTECTED:return EACCES;
        case ERROR_DEVICE_NOT_MOUNTED:return ENODEV;
        default:return EIO;
        }
    }
    struct oz_mount_snapshot *table=NULL;
    int error=oz_mounts_read(name,&table);
    if(!error) {
        error=ENODEV;
        struct oz_mount_record *records=(void *)(table+1);
        for(ULONG i=0;i<table->count;i++) {
            BPTR root=Lock((CONST_STRPTR)records[i].mountpoint,SHARED_LOCK);
            if(!root)continue; /* Mount disappeared since the snapshot. */
            int same=SameDevice(object,root);
            UnLock(root);
            if(!same)continue;
            size_t needed=strlen(records[i].dataset)+1;
            if(needed>size)error=ENAMETOOLONG;
            else {memcpy(dataset,records[i].dataset,needed);error=0;}
            break;
        }
    }
    free(table);UnLock(object);return error;
}
