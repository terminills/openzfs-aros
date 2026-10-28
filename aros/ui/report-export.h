/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_REPORT_EXPORT_H
#define OZ_REPORT_EXPORT_H
#include "report-copy.h"
#include <pthread.h>
#include <stdlib.h>
#include <string.h>

struct oz_report_export {
    pthread_t thread;pthread_mutex_t lock;
    int source,done,joined,error;
    uint64_t bytes;
    char path[1024];
};
static void *oz_report_export_run(void *argument)
{
    struct oz_report_export *worker=argument;
    worker->error=oz_report_copy_new(worker->source,worker->path,&worker->bytes);
    if(close(worker->source)&&!worker->error)worker->error=errno;
    pthread_mutex_lock(&worker->lock);worker->done=1;pthread_mutex_unlock(&worker->lock);
    return NULL;
}
static int oz_report_export_start(int source,const char *path,struct oz_report_export **result)
{
    if(!result||source<0||!path||!*path)return EINVAL;
    *result=NULL;
    if(strlen(path)>=sizeof(((struct oz_report_export *)0)->path))return ENAMETOOLONG;
    struct oz_report_export *worker=calloc(1,sizeof(*worker));if(!worker)return ENOMEM;
    strcpy(worker->path,path);worker->source=dup(source);
    if(worker->source<0){int error=errno;free(worker);return error;}
    int error=pthread_mutex_init(&worker->lock,NULL);
    if(error){close(worker->source);free(worker);return error;}
    error=pthread_create(&worker->thread,NULL,oz_report_export_run,worker);
    if(error){pthread_mutex_destroy(&worker->lock);close(worker->source);free(worker);return error;}
    *result=worker;return 0;
}
static int oz_report_export_poll(struct oz_report_export *worker)
{
    if(!worker)return EINVAL;
    pthread_mutex_lock(&worker->lock);int done=worker->done;pthread_mutex_unlock(&worker->lock);
    if(!done)return EAGAIN;
    if(!worker->joined){int error=pthread_join(worker->thread,NULL);if(error)return error;worker->joined=1;}
    return 0;
}
static int oz_report_export_free(struct oz_report_export *worker)
{
    if(!worker)return 0;
    if(!worker->joined)return EBUSY;
    int error=pthread_mutex_destroy(&worker->lock);if(error)return error;
    free(worker);return 0;
}
#endif
