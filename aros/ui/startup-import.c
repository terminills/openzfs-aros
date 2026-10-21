/* SPDX-License-Identifier: CDDL-1.0 */
#include "startup-import.h"
#include "startup-settings.h"
#include <proto/dos.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>
static int import_record(const char *port,uint64_t guid,struct oz_startup_import_result *result,int trusted)
{
    if(!result)return EINVAL;
    memset(result,0,sizeof(*result));result->status=OZ_NO_SERVICE;
    if(!port||!*port||!guid)return EINVAL;
    struct oz_startup_settings *settings=calloc(1,sizeof(*settings));
    struct oz_message *request=calloc(1,sizeof(*request)),*reply=calloc(1,sizeof(*reply));
    int error=(!settings||!request||!reply)?ENOMEM:0;
    if(!error)error=trusted?oz_startup_settings_trusted(guid,settings):
        oz_startup_settings_current(guid,settings,&result->recovered);
    /* Interrupted file replacement is not authorization to replay a prior
     * boot configuration. Leave recovery explicit and side-effect free. */
    if(!error&&result->recovered)error=EBUSY;
    if(error)goto done;
    strcpy(result->pool,settings->pool);
    if(!(settings->flags&OZ_STARTUP_ENABLED)){result->skipped=1;goto done;}
    result->mount_requested=!!(settings->flags&OZ_STARTUP_MOUNT);
    request->magic=OZ_MAGIC;request->version=OZ_VERSION;request->msg.mn_Length=sizeof(*request);
    request->operation=7;request->flags=OZ_CONFIRMED;
    strcpy(request->target,settings->pool);
    snprintf(request->options,sizeof(request->options),"readonly=%s;guid=%016llx",
        settings->flags&OZ_STARTUP_READONLY?"on":"off",(unsigned long long)guid);
    request->topology.device_count=settings->count;
    for(unsigned i=0;i<settings->count;i++)strcpy(request->topology.devices[i].path,settings->paths[i]);
    error=oz_admission_submit(port,request,&result->ticket);
    if(error)goto done;
    do{error=oz_admission_query(port,&result->ticket,reply);if(error==EAGAIN)Delay(1);}while(error==EAGAIN);
    if(error)goto done; /* Retain accepted ticket on uncertain outcome. */
    result->status=reply->status;
    snprintf(result->detail,sizeof(result->detail),"%s",reply->detail);
    result->imported=reply->status==OZ_OK;
    result->release_error=oz_admission_release(port,&result->ticket);
    if(!result->release_error)memset(&result->ticket,0,sizeof(result->ticket));
    error=result->status==OZ_OK?result->release_error:EIO;
done:
    free(settings);free(request);free(reply);return error;
}
int oz_startup_import(const char *port,uint64_t guid,struct oz_startup_import_result *result)
{return import_record(port,guid,result,0);}
int oz_startup_import_trusted(const char *port,uint64_t guid,struct oz_startup_import_result *result)
{return import_record(port,guid,result,1);}
