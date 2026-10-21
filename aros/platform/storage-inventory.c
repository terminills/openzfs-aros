/* SPDX-License-Identifier: CDDL-1.0 */
#define __OOP_NOATTRBASES__
#include <proto/exec.h>
#include <proto/oop.h>
#include <hidd/storage.h>
#include <interface/HW.h>
#include <utility/hooks.h>
#include <aros/asmcall.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <limits.h>
#include "../ui/partition-api.h"

struct inventory_context {
    struct Library *oop;
    OOP_AttrBase attrs;
    struct oz_storage_inventory *result;
    int error;
};
static int copy_string(char *out,size_t size,const char *value)
{
    if(!value){out[0]=0;return 0;}
    size_t n=strnlen(value,size);
    if(n==size)return ENAMETOOLONG;
    memcpy(out,value,n+1);return 0;
}
AROS_UFH3S(BOOL,inventory_unit,
    AROS_UFHA(struct Hook *,hook,A0),AROS_UFHA(OOP_Object *,obj,A2),
    AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    (void)message;
    struct inventory_context *ctx=hook->h_Data;
    struct Library *OOPBase=ctx->oop;
    if(ctx->error)return TRUE;
    struct oz_storage_unit entry={0};
    IPTR value=0;
    OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Device,&value);
    ctx->error=copy_string(entry.device,sizeof(entry.device),(const char *)value);
    value=(IPTR)-1;OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Number,&value);
    if(!ctx->error&&(!entry.device[0]||value>INT32_MAX))ctx->error=EINVAL;
    if(ctx->error)return TRUE;
    entry.unit=value;
    value=0;OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Type,&value);entry.type=value;
    value=0;OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Removable,&value);entry.removable=!!value;
    value=0;OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Model,&value);
    ctx->error=copy_string(entry.model,sizeof(entry.model),(const char *)value);
    if(ctx->error)return TRUE;
    value=0;OOP_GetAttr(obj,ctx->attrs+aoHidd_StorageUnit_Serial,&value);
    ctx->error=copy_string(entry.serial,sizeof(entry.serial),(const char *)value);
    if(ctx->error)return TRUE;
    for(uint32_t i=0;i<ctx->result->count;i++)
        if(ctx->result->units[i].unit==entry.unit&&
           !strcmp(ctx->result->units[i].device,entry.device)) {
            ctx->error=EEXIST;return TRUE;
        }
    if(ctx->result->count==OZ_STORAGE_INVENTORY_MAX){ctx->error=EOVERFLOW;return TRUE;}
    ctx->result->units[ctx->result->count++]=entry;
    return FALSE;
    AROS_USERFUNC_EXIT
}
AROS_UFH3S(BOOL,inventory_bus,
    AROS_UFHA(struct Hook *,hook,A0),AROS_UFHA(OOP_Object *,obj,A2),
    AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    (void)message;
    struct inventory_context *ctx=hook->h_Data;
    if(ctx->error)return TRUE;
    struct Hook child={.h_Entry=(HOOKFUNC)inventory_unit,.h_Data=ctx};
    HIDD_StorageBus_EnumUnits(obj,&child,NULL);
    return ctx->error!=0;
    AROS_USERFUNC_EXIT
}
AROS_UFH3S(BOOL,inventory_controller,
    AROS_UFHA(struct Hook *,hook,A0),AROS_UFHA(OOP_Object *,obj,A2),
    AROS_UFHA(APTR,message,A1))
{
    AROS_USERFUNC_INIT
    (void)message;
    struct inventory_context *ctx=hook->h_Data;
    if(ctx->error)return TRUE;
    struct Hook child={.h_Entry=(HOOKFUNC)inventory_bus,.h_Data=ctx};
    HIDD_StorageController_EnumBuses(obj,&child,NULL);
    return ctx->error!=0;
    AROS_USERFUNC_EXIT
}
int oz_storage_inventory_native(struct oz_storage_inventory *out)
{
    if(!out)return EINVAL;
    struct Library *OOPBase=OpenLibrary("oop.library",0);
    if(!OOPBase)return ENOSYS;
    struct inventory_context ctx={.oop=OOPBase};
    ctx.attrs=OOP_ObtainAttrBase(IID_Hidd_StorageUnit);
    if(!ctx.attrs){CloseLibrary(OOPBase);return ENOMEM;}
    ctx.result=calloc(1,sizeof(*ctx.result));
    if(!ctx.result)ctx.error=ENOMEM;
    OOP_Object *root=NULL;
    if(!ctx.error&&!(root=OOP_NewObject(NULL,CLID_Hidd_Storage,NULL)))ctx.error=ENODEV;
    if(!ctx.error) {
        struct Hook hook={.h_Entry=(HOOKFUNC)inventory_controller,.h_Data=&ctx};
        HW_EnumDrivers(root,&hook,NULL);
        if(!ctx.error){ctx.result->version=1;*out=*ctx.result;}
    }
    if(root)OOP_DisposeObject(root);
    free(ctx.result);OOP_ReleaseAttrBase(IID_Hidd_StorageUnit);CloseLibrary(OOPBase);
    return ctx.error;
}
