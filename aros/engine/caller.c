/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>

/* proc is an opaque identity token here, not a fabricated native process. */
struct oz_caller {
    proc_t proc;
    pthread_mutex_t lock;
    size_t references;
};
struct oz_caller_scope {
    proc_t *caller;
    struct oz_caller_scope *previous;
};
static pthread_once_t once = PTHREAD_ONCE_INIT;
static pthread_key_t key;
static int key_error;

int oz_caller_create(proc_t **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    struct oz_caller *caller = calloc(1,sizeof(*caller));
    if (!caller) return ENOMEM;
    int error = pthread_mutex_init(&caller->lock,NULL);
    if (error) { free(caller); return error; }
    caller->references = 1;
    *out = &caller->proc;
    return 0;
}
int oz_caller_hold(proc_t *proc)
{
    if (!proc) return EINVAL;
    struct oz_caller *caller = (struct oz_caller *)proc;
    pthread_mutex_lock(&caller->lock);
    int error = caller->references == SIZE_MAX ? EOVERFLOW : 0;
    ASSERT3U(caller->references,>,0);
    if (!error) ++caller->references;
    pthread_mutex_unlock(&caller->lock);
    return error;
}
void oz_caller_release(proc_t *proc)
{
    if (!proc) return;
    struct oz_caller *caller = (struct oz_caller *)proc;
    pthread_mutex_lock(&caller->lock);
    ASSERT3U(caller->references,>,0);
    boolean_t last = --caller->references == 0;
    pthread_mutex_unlock(&caller->lock);
    if (last) { pthread_mutex_destroy(&caller->lock); free(caller); }
}
static void destroy_scopes(void *value)
{
    struct oz_caller_scope *scope = value;
    while (scope) {
        struct oz_caller_scope *previous = scope->previous;
        oz_caller_release(scope->caller);
        free(scope); scope = previous;
    }
}
static void initialize(void) { key_error = pthread_key_create(&key,destroy_scopes); }
static int ready(void)
{
    int error = pthread_once(&once,initialize);
    return error ? error : key_error;
}
int oz_caller_enter(proc_t *proc,struct oz_caller_scope **out)
{
    if (!out) return EINVAL;
    *out = NULL;
    if (!proc) return EACCES;
    int error = ready(); if (error) return error;
    struct oz_caller_scope *scope = calloc(1,sizeof(*scope));
    if (!scope) return ENOMEM;
    error = oz_caller_hold(proc);
    if (error) { free(scope); return error; }
    scope->caller = proc; scope->previous = pthread_getspecific(key);
    error = pthread_setspecific(key,scope);
    if (error) { oz_caller_release(proc); free(scope); return error; }
    *out = scope;
    return 0;
}
int oz_caller_leave(struct oz_caller_scope *scope)
{
    int error = ready(); if (error) return error;
    if (!scope || pthread_getspecific(key) != scope) return EINVAL;
    error = pthread_setspecific(key,scope->previous);
    if (error) return error;
    oz_caller_release(scope->caller); free(scope);
    return 0;
}
proc_t *oz_caller_capture(void)
{
    if (ready()) return NULL;
    struct oz_caller_scope *scope = pthread_getspecific(key);
    if (!scope) return NULL;
    VERIFY0(oz_caller_hold(scope->caller));
    return scope->caller;
}
boolean_t zfs_proc_is_caller(proc_t *proc)
{
    if (!proc || ready()) return B_FALSE;
    struct oz_caller_scope *scope = pthread_getspecific(key);
    return scope && scope->caller == proc;
}
