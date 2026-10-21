/* SPDX-License-Identifier: CDDL-1.0 */
#include "session-cleanup.h"
#include <sys/zfs_file.h>
#include <sys/zfs_onexit.h>
#include "../engine/file-scope.h"
#include <limits.h>

struct oz_session_cleanup {
    kmutex_t lock;
    size_t references;
    boolean_t closed;
    zfs_onexit_t *state;
    proc_t *caller;
    int next_descriptor;
    struct oz_file_binding files[OZ_SESSION_FILES];
    char *history[OZ_SESSION_FILES];
    boolean_t history_busy[OZ_SESSION_FILES];
};

int oz_session_history_take(struct oz_session_cleanup *s,int fd,char **out)
{
    if(!s||!out||fd<=0)return EINVAL;
    *out=NULL;int error=EBADF;
    mutex_enter(&s->lock);
    if(!s->closed)for(unsigned i=0;i<OZ_SESSION_FILES;i++)
        if(s->files[i].file&&s->files[i].descriptor==fd) {
            error=s->history_busy[i]?EBUSY:0;
            if(!error){s->history_busy[i]=B_TRUE;*out=s->history[i];s->history[i]=NULL;}
            break;
        }
    mutex_exit(&s->lock);return error;
}
void oz_session_history_put(struct oz_session_cleanup *s,int fd,char *pool)
{
    mutex_enter(&s->lock);
    if(!s->closed)for(unsigned i=0;i<OZ_SESSION_FILES;i++)
        if(s->files[i].file&&s->files[i].descriptor==fd) {
            ASSERT(s->history_busy[i]);ASSERT(s->history[i]==NULL);
            s->history[i]=pool;pool=NULL;s->history_busy[i]=B_FALSE;break;
        }
    mutex_exit(&s->lock);
    if(pool)kmem_strfree(pool);
}

int oz_session_file_insert(struct oz_session_cleanup *s,zfs_file_t *file,int *out)
{
    if(!s||!file||!out)return EINVAL;
    *out=-1;
    mutex_enter(&s->lock);
    int error=s->closed?EBADF:s->next_descriptor==INT_MAX?EOVERFLOW:0;
    unsigned i=0;
    if(!error) {
        for(;i<OZ_SESSION_FILES&&s->files[i].file;i++);
        error=i==OZ_SESSION_FILES?EMFILE:oz_file_hold(file);
        if(!error) {
            s->files[i]=(struct oz_file_binding){++s->next_descriptor,file};
            *out=s->next_descriptor;
        }
    }
    mutex_exit(&s->lock);
    return error;
}

int oz_session_file_get(struct oz_session_cleanup *s,int fd,zfs_file_t **out)
{
    if(!s||!out)return EINVAL;
    *out=NULL;
    mutex_enter(&s->lock);
    int error=EBADF;
    if(!s->closed)for(unsigned i=0;i<OZ_SESSION_FILES;i++) {
        if(s->files[i].file&&s->files[i].descriptor==fd) {
            error=oz_file_hold(s->files[i].file);
            if(!error)*out=s->files[i].file;
            break;
        }
    }
    mutex_exit(&s->lock);
    return error;
}

int oz_session_file_close(struct oz_session_cleanup *s,int fd)
{
    if(!s)return EINVAL;
    zfs_file_t *file=NULL;
    char *history=NULL;
    mutex_enter(&s->lock);
    if(!s->closed)for(unsigned i=0;i<OZ_SESSION_FILES;i++) {
        if(s->files[i].file&&s->files[i].descriptor==fd) {
            if(s->history_busy[i]){mutex_exit(&s->lock);return EBUSY;}
            history=s->history[i];s->history[i]=NULL;
            file=s->files[i].file;
            s->files[i]=(struct oz_file_binding){0};
            break;
        }
    }
    mutex_exit(&s->lock);
    /* Final file release can run arbitrary cleanup: never under table lock. */
    if(file)zfs_file_close(file);
    if(history)kmem_strfree(history);
    return file?0:EBADF;
}

int oz_session_file_scope(struct oz_session_cleanup *s,struct oz_file_scope **out)
{
    if(!s||!out)return EINVAL;
    *out=NULL;
    struct oz_file_binding bindings[OZ_SESSION_FILES];
    size_t count=0;
    mutex_enter(&s->lock);
    if(!s->closed)for(unsigned i=0;i<OZ_SESSION_FILES;i++)
        if(s->files[i].file)bindings[count++]=s->files[i];
    int error=s->closed?EBADF:oz_file_scope_enter(bindings,count,out);
    mutex_exit(&s->lock);
    return error;
}

int oz_session_cleanup_create(struct oz_session_cleanup **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    struct oz_session_cleanup *session = calloc(1, sizeof (*session));
    if (!session) return ENOMEM;
    mutex_init(&session->lock, NULL, MUTEX_DEFAULT, NULL);
    int error = oz_caller_create(&session->caller);
    if (error) { mutex_destroy(&session->lock); free(session); return error; }
    zfs_onexit_init(&session->state);
    session->references = 1;
    *out = session;
    return 0;
}

int oz_session_cleanup_hold(struct oz_session_cleanup *session)
{
    if (!session) return EINVAL;
    mutex_enter(&session->lock);
    int error = session->closed ? EBADF :
        session->references == SIZE_MAX ? EOVERFLOW : 0;
    if (!error) ++session->references;
    mutex_exit(&session->lock);
    return error;
}

int oz_session_cleanup_add(struct oz_session_cleanup *session,
    void (*callback)(void *), void *data)
{
    if (!session || !callback) return EINVAL;
    mutex_enter(&session->lock);
    int error = session->closed ? EBADF :
        zfs_onexit_add_cb_state(session->state, callback, data, NULL);
    mutex_exit(&session->lock);
    return error;
}

void oz_session_cleanup_release(struct oz_session_cleanup *session)
{
    ASSERT(session != NULL);
    mutex_enter(&session->lock);
    ASSERT3U(session->references, >, 0);
    boolean_t last = --session->references == 0;
    ASSERT(!last || session->closed);
    mutex_exit(&session->lock);
    if (last) {
        /* No session/registry lock may be held while callbacks run. */
        zfs_onexit_destroy(session->state);
        oz_caller_release(session->caller);
        mutex_destroy(&session->lock);
        free(session);
    }
}

proc_t *oz_session_cleanup_caller(struct oz_session_cleanup *session)
{
    return session ? session->caller : NULL;
}

void oz_session_cleanup_close(struct oz_session_cleanup *session)
{
    ASSERT(session != NULL);
    zfs_file_t *files[OZ_SESSION_FILES];
    size_t count=0;
    mutex_enter(&session->lock);
    ASSERT(!session->closed);
    session->closed = B_TRUE;
    for(unsigned i=0;i<OZ_SESSION_FILES;i++)if(session->files[i].file) {
        if(session->history[i])kmem_strfree(session->history[i]);
        session->history[i]=NULL;
        files[count++]=session->files[i].file;
        session->files[i]=(struct oz_file_binding){0};
    }
    mutex_exit(&session->lock);
    for(size_t i=0;i<count;i++)zfs_file_close(files[i]);
    oz_session_cleanup_release(session);
}
