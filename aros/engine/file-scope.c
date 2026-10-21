/* SPDX-License-Identifier: CDDL-1.0 */
#include "file-scope.h"
struct oz_file_scope {
    struct oz_file_scope *parent;
    size_t count;
    struct oz_file_binding bindings[];
};
static pthread_once_t once=PTHREAD_ONCE_INIT;
static pthread_key_t key;
static int key_error;
static void release_scope(struct oz_file_scope *scope)
{
    for(size_t i=0;i<scope->count;i++)zfs_file_close(scope->bindings[i].file);
    free(scope);
}
static void discard(void *value)
{
    struct oz_file_scope *scope=value;
    while(scope){struct oz_file_scope *parent=scope->parent;release_scope(scope);scope=parent;}
}
static void initialize(void){key_error=pthread_key_create(&key,discard);}
static int ready(void){int e=pthread_once(&once,initialize);return e?e:key_error;}
int oz_file_scope_enter(const struct oz_file_binding *bindings,size_t count,struct oz_file_scope **out)
{
    if(!out)return EINVAL;
    *out=NULL;
    if(count && !bindings)return EINVAL;
    if(count>(SIZE_MAX-sizeof(struct oz_file_scope))/sizeof(*bindings))return EOVERFLOW;
    for(size_t i=0;i<count;i++) {
        if(bindings[i].descriptor<0 || !bindings[i].file)return EINVAL;
        for(size_t j=0;j<i;j++)if(bindings[j].descriptor==bindings[i].descriptor)return EINVAL;
    }
    int error=ready();if(error)return error;
    struct oz_file_scope *scope=calloc(1,sizeof(*scope)+count*sizeof(*bindings));
    if(!scope)return ENOMEM;
    scope->parent=pthread_getspecific(key);
    for(size_t i=0;i<count;i++) {
        error=oz_file_hold(bindings[i].file);
        if(error){release_scope(scope);return error;}
        scope->bindings[scope->count++]=bindings[i];
    }
    error=pthread_setspecific(key,scope);
    if(error)release_scope(scope);else *out=scope;
    return error;
}
int oz_file_scope_leave(struct oz_file_scope *scope)
{
    int error=ready();if(error)return error;
    if(!scope || pthread_getspecific(key)!=scope)return EINVAL;
    error=pthread_setspecific(key,scope->parent);
    if(!error)release_scope(scope);
    return error;
}
zfs_file_t *oz_file_scope_get(int descriptor)
{
    if(descriptor<0 || ready())return NULL;
    struct oz_file_scope *scope=pthread_getspecific(key);
    if(!scope)return NULL;
    for(size_t i=0;i<scope->count;i++)if(scope->bindings[i].descriptor==descriptor) {
        zfs_file_t *file=scope->bindings[i].file;
        return oz_file_hold(file)?NULL:file;
    }
    return NULL;
}
