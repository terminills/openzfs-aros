/* SPDX-License-Identifier: CDDL-1.0 */
/* Private fixture: fail only the two management mount-registry growth sites. */
#include <stddef.h>
#include <aros/debug.h>
#include <proto/dos.h>

static int calloc_injected,realloc_injected;

static int sentinel_present(const char *name)
{
    BPTR lock=Lock((CONST_STRPTR)name,ACCESS_READ);
    if(!lock)return 0;
    UnLock(lock);
    return 1;
}

void *__real_oz_mount_registry_calloc(size_t,size_t);
void *__wrap_oz_mount_registry_calloc(size_t count,size_t size)
{
    if(!calloc_injected&&sentinel_present("RAM:openzfs-mount-registry-calloc-failure")) {
        calloc_injected=1;
        bug("MOUNT REGISTRY ALLOCATION FIXTURE calloc errno=12\n");
        return NULL;
    }
    return __real_oz_mount_registry_calloc(count,size);
}

void *__real_oz_mount_registry_realloc(void *,size_t);
void *__wrap_oz_mount_registry_realloc(void *memory,size_t size)
{
    if(!realloc_injected&&sentinel_present("RAM:openzfs-mount-registry-realloc-failure")) {
        realloc_injected=1;
        bug("MOUNT REGISTRY ALLOCATION FIXTURE realloc errno=12\n");
        return NULL;
    }
    return __real_oz_mount_registry_realloc(memory,size);
}
