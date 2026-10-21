/* SPDX-License-Identifier: CDDL-1.0 */
#include "admission-worker.h"
#include <pthread.h>
#include <proto/dos.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <proto/exec.h>
#include <exec/io.h>
#include <exec/errors.h>
#include <devices/trackdisk.h>
struct oz_mapping_worker {
    pthread_t thread;pthread_mutex_t lock;
    int done,joined,error;struct oz_mapping_catalog catalog;
    int remap,remapped;unsigned unit;struct oz_device_remap request;
};
static void *mapping_run(void *arg)
{
    struct oz_mapping_worker *w=arg;
    struct MsgPort *port=CreateMsgPort();
    struct IOStdReq *io=port?(void *)CreateIORequest(port,sizeof(*io)):NULL;
    if(!io)w->error=ENOMEM;
    if(w->remap&&!w->error){
        if(OpenDevice((CONST_STRPTR)"openzfs.device",w->unit,(struct IORequest *)io,0))
            w->error=ENXIO;
        else{
            io->io_Command=TD_CHANGENUM;int error=DoIO((struct IORequest *)io);
            if(!error){
                w->request.expected_change=io->io_Actual;
                struct oz_device_eject eject={OZ_EJECT_VERSION,sizeof(eject),io->io_Actual};
                io->io_Command=w->remap==2?OZCMD_EJECTMAPPING:OZCMD_SETMAPPING;
                io->io_Data=w->remap==2?(void *)&eject:(void *)&w->request;
                io->io_Length=w->remap==2?sizeof(eject):sizeof(w->request);
                error=DoIO((struct IORequest *)io);
                if(!error||(w->remap==2&&io->io_Actual==1))w->remapped=1;
            }
            CloseDevice((struct IORequest *)io);
            if(error)w->error=error==IOERR_UNITBUSY?EBUSY:error==TDERR_DiskChanged?ESTALE:
                error==IOERR_NOCMD?ENOTSUP:error==TDERR_WriteProt?EROFS:EIO;
        }
    }
    unsigned count=1;
    for(unsigned unit=0;unit<count&&!w->error;unit++){
        if(OpenDevice((CONST_STRPTR)"openzfs.device",unit,(struct IORequest *)io,0)){
            w->error=ENXIO;break;
        }
        struct oz_device_mapping m={0};
        io->io_Command=OZCMD_GETMAPPING;io->io_Data=&m;io->io_Length=sizeof(m);
        int error=DoIO((struct IORequest *)io);CloseDevice((struct IORequest *)io);
        if(error){w->error=error==IOERR_NOCMD?ENOTSUP:EIO;break;}
        if(io->io_Actual!=sizeof(m)||m.version!=OZ_MAPPING_VERSION||m.structure_size!=sizeof(m)||
           m.unit!=unit||!m.unit_count||m.unit_count>32||(unit&&count!=m.unit_count)||
           !memchr(m.dataset,0,sizeof(m.dataset))){w->error=EPROTO;break;}
        count=m.unit_count;w->catalog.units[unit]=m;w->catalog.count=unit+1;
    }
    if(io)DeleteIORequest((struct IORequest *)io);
    if(port)DeleteMsgPort(port);
    pthread_mutex_lock(&w->lock);w->done=1;pthread_mutex_unlock(&w->lock);return NULL;
}
static int mapping_start(unsigned unit,const char *dataset,int writable,int eject,struct oz_mapping_worker **out)
{
    if(!out)return EINVAL;
    *out=NULL;struct oz_mapping_worker *w=calloc(1,sizeof(*w));if(!w)return ENOMEM;
    if(eject){
        if(unit>=32){free(w);return EINVAL;}
        w->remap=2;w->unit=unit;
    }
    if(dataset){
        if(unit>=32||!*dataset||strlen(dataset)>=sizeof(w->request.dataset)||
           (writable!=0&&writable!=1)){free(w);return EINVAL;}
        w->remap=1;w->unit=unit;
        w->request.version=OZ_REMAP_VERSION;w->request.structure_size=sizeof(w->request);
        w->request.flags=writable?OZ_REMAP_WRITABLE:0;
        strcpy(w->request.dataset,dataset);
    }
    int e=pthread_mutex_init(&w->lock,NULL);if(e){free(w);return e;}
    e=pthread_create(&w->thread,NULL,mapping_run,w);
    if(e){pthread_mutex_destroy(&w->lock);free(w);return e;}
    *out=w;return 0;
}
int oz_mapping_worker_start(struct oz_mapping_worker **out)
{return mapping_start(0,NULL,0,0,out);}
int oz_mapping_worker_remap_start(unsigned unit,const char *dataset,int writable,struct oz_mapping_worker **out)
{if(!dataset)return EINVAL;return mapping_start(unit,dataset,writable,0,out);}
int oz_mapping_worker_eject_start(unsigned unit,struct oz_mapping_worker **out)
{return mapping_start(unit,NULL,0,1,out);}
int oz_mapping_worker_remapped(struct oz_mapping_worker *w)
{return w&&w->joined&&w->remapped;}
int oz_mapping_worker_poll(struct oz_mapping_worker *w,struct oz_mapping_catalog *out,int *error)
{
    if(!w||!out||!error)return EINVAL;
    pthread_mutex_lock(&w->lock);int done=w->done;pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined){int e=pthread_join(w->thread,NULL);if(e)return e;w->joined=1;}
    *error=w->error;if(!w->error)*out=w->catalog;return 0;
}
int oz_mapping_worker_free(struct oz_mapping_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int e=pthread_mutex_destroy(&w->lock);if(e)return e;
    free(w);return 0;
}
struct oz_inventory_worker {
    pthread_t thread;
    pthread_mutex_t lock;
    char *endpoint;
    struct oz_storage_inventory inventory;
    int done,joined,error;
};
static void *inventory_run(void *arg)
{
    struct oz_inventory_worker *w=arg;
    w->error=oz_storage_inventory_read(w->endpoint,&w->inventory);
    pthread_mutex_lock(&w->lock);w->done=1;pthread_mutex_unlock(&w->lock);
    return NULL;
}
int oz_inventory_worker_start(const char *endpoint,struct oz_inventory_worker **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!endpoint||!*endpoint)return EINVAL;
    struct oz_inventory_worker *w=calloc(1,sizeof(*w));
    if(!w)return ENOMEM;
    w->endpoint=strdup(endpoint);
    if(!w->endpoint){free(w);return ENOMEM;}
    int error=pthread_mutex_init(&w->lock,NULL);
    if(error){free(w->endpoint);free(w);return error;}
    error=pthread_create(&w->thread,NULL,inventory_run,w);
    if(error){pthread_mutex_destroy(&w->lock);free(w->endpoint);free(w);return error;}
    *out=w;return 0;
}
int oz_inventory_worker_poll(struct oz_inventory_worker *w,
    struct oz_storage_inventory *out,int *operation_error)
{
    if(!w||!out||!operation_error)return EINVAL;
    pthread_mutex_lock(&w->lock);int done=w->done;pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined){int error=pthread_join(w->thread,NULL);if(error)return error;w->joined=1;}
    *operation_error=w->error;
    if(!w->error)*out=w->inventory;
    return 0;
}
int oz_inventory_worker_free(struct oz_inventory_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int error=pthread_mutex_destroy(&w->lock);if(error)return error;
    free(w->endpoint);free(w);return 0;
}
struct oz_partition_worker {
    pthread_t thread;
    pthread_mutex_t lock;
    char *endpoint;
    char device[256];
    int32_t unit;
    struct oz_partition_catalog catalog;
    struct oz_device_geometry geometry;
    int geometry_error;
    int done,joined,error;
};
static void *partition_run(void *arg)
{
    struct oz_partition_worker *w=arg;
    w->geometry_error=oz_device_geometry_read(w->endpoint,w->device,w->unit,&w->geometry);
    w->error=oz_partitions_read(w->endpoint,w->device,w->unit,&w->catalog);
    pthread_mutex_lock(&w->lock);w->done=1;pthread_mutex_unlock(&w->lock);
    return NULL;
}
int oz_partition_worker_start(const char *endpoint,const char *device,int32_t unit,
    struct oz_partition_worker **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!endpoint||!device||!*device||strlen(device)>255||unit<0)return EINVAL;
    struct oz_partition_worker *w=calloc(1,sizeof(*w));
    if(!w)return ENOMEM;
    w->endpoint=strdup(endpoint);
    if(!w->endpoint){free(w);return ENOMEM;}
    strcpy(w->device,device);w->unit=unit;
    int error=pthread_mutex_init(&w->lock,NULL);
    if(error){free(w->endpoint);free(w);return error;}
    error=pthread_create(&w->thread,NULL,partition_run,w);
    if(error){pthread_mutex_destroy(&w->lock);free(w->endpoint);free(w);return error;}
    *out=w;return 0;
}
int oz_partition_worker_poll(struct oz_partition_worker *w,
    struct oz_partition_catalog *out,int *operation_error)
{
    if(!w||!out||!operation_error)return EINVAL;
    pthread_mutex_lock(&w->lock);int done=w->done;pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined){int error=pthread_join(w->thread,NULL);if(error)return error;w->joined=1;}
    *operation_error=w->error;
    if(!w->error)*out=w->catalog;
    return 0;
}
int oz_partition_worker_free(struct oz_partition_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int error=pthread_mutex_destroy(&w->lock);if(error)return error;
    free(w->endpoint);free(w);return 0;
}
int oz_partition_worker_geometry(struct oz_partition_worker *w,struct oz_device_geometry *out)
{
    if(!w||!out)return EINVAL;
    if(!w->joined)return EAGAIN;
    if(w->geometry_error)return w->geometry_error;
    *out=w->geometry;return 0;
}
struct oz_gpt_worker {
    pthread_t thread;
    pthread_mutex_t lock;
    char *endpoint;
    struct oz_gpt_query query;
    int done,joined,error;
};
static void *gpt_run(void *arg)
{
    struct oz_gpt_worker *w=arg;
    w->error=oz_gpt_request(w->endpoint,&w->query);
    pthread_mutex_lock(&w->lock);w->done=1;pthread_mutex_unlock(&w->lock);
    return NULL;
}
int oz_gpt_worker_start(const char *endpoint,const struct oz_gpt_query *query,
    struct oz_gpt_worker **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!endpoint||!*endpoint||!query)return EINVAL;
    struct oz_gpt_worker *w=calloc(1,sizeof(*w));
    if(!w)return ENOMEM;
    w->query=*query;w->endpoint=strdup(endpoint);
    if(!w->endpoint){free(w);return ENOMEM;}
    int error=pthread_mutex_init(&w->lock,NULL);
    if(error){free(w->endpoint);free(w);return error;}
    error=pthread_create(&w->thread,NULL,gpt_run,w);
    if(error){pthread_mutex_destroy(&w->lock);free(w->endpoint);free(w);return error;}
    *out=w;return 0;
}
int oz_gpt_worker_poll(struct oz_gpt_worker *w,struct oz_gpt_query *out,int *operation_error)
{
    if(!w||!out||!operation_error)return EINVAL;
    pthread_mutex_lock(&w->lock);int done=w->done;pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined){int error=pthread_join(w->thread,NULL);if(error)return error;w->joined=1;}
    *operation_error=w->error;
    if(!w->error)*out=w->query;
    return 0;
}
int oz_gpt_worker_free(struct oz_gpt_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int error=pthread_mutex_destroy(&w->lock);if(error)return error;
    free(w->endpoint);free(w);return 0;
}
struct oz_admission_worker {
    pthread_t thread;
    pthread_mutex_t lock;
    char *endpoint;
    struct oz_message request, result;
    struct oz_ticket ticket;
    int done, joined, error, release_error;
};
static void *run(void *arg)
{
    struct oz_admission_worker *w=arg;
    w->error=oz_admission_submit(w->endpoint,&w->request,&w->ticket);
    if(!w->error) {
        do {
            w->error=oz_admission_query(w->endpoint,&w->ticket,&w->result);
            if(w->error==EAGAIN)Delay(1);
        } while(w->error==EAGAIN);
        /* Do not discard a result we could not retrieve. Retain the ticket
         * for diagnosis/recovery. Never resubmit a possibly accepted change. */
        if(!w->error)
            w->release_error=oz_admission_release(w->endpoint,&w->ticket);
    }
    pthread_mutex_lock(&w->lock);
    w->done=1;
    pthread_mutex_unlock(&w->lock);
    return NULL;
}
int oz_worker_start(const char *endpoint,const struct oz_message *request,
    struct oz_admission_worker **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(!endpoint||!request)return EINVAL;
    struct oz_admission_worker *w=calloc(1,sizeof(*w));
    if(!w)return ENOMEM;
    w->endpoint=strdup(endpoint);
    if(!w->endpoint){free(w);return ENOMEM;}
    w->request=*request;
    /* No borrowed Exec pointers survive the copy. */
    memset(&w->request.msg,0,sizeof(w->request.msg));
    w->request.msg.mn_Length=sizeof(w->request);
    memset(w->request.reply_name,0,sizeof(w->request.reply_name));
    int error=pthread_mutex_init(&w->lock,NULL);
    if(error){free(w->endpoint);free(w);return error;}
    error=pthread_create(&w->thread,NULL,run,w);
    if(error){pthread_mutex_destroy(&w->lock);free(w->endpoint);free(w);return error;}
    *out=w;
    return 0;
}
int oz_worker_poll(struct oz_admission_worker *w,struct oz_message *result,
    struct oz_ticket *ticket,int *operation_error,int *release_error)
{
    if(!w||!result||!ticket||!operation_error||!release_error)return EINVAL;
    pthread_mutex_lock(&w->lock);
    int done=w->done;
    pthread_mutex_unlock(&w->lock);
    if(!done)return EAGAIN;
    if(!w->joined) {
        int error=pthread_join(w->thread,NULL);
        if(error)return error;
        w->joined=1;
    }
    *result=w->result;*ticket=w->ticket;
    *operation_error=w->error;*release_error=w->release_error;
    return 0;
}
int oz_worker_free(struct oz_admission_worker *w)
{
    if(!w)return 0;
    if(!w->joined)return EBUSY;
    int error=pthread_mutex_destroy(&w->lock);
    if(error)return error;
    free(w->endpoint);free(w);
    return 0;
}
