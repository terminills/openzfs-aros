/* SPDX-License-Identifier: CDDL-1.0 */
#include <stddef.h>
#include <stdlib.h>

/* Keep registry growth in a separate translation unit so private diagnostic
 * links can inject failures without replacing libc allocation process-wide. */
void *oz_mount_registry_calloc(size_t count,size_t size)
{
    return calloc(count,size);
}

void *oz_mount_registry_realloc(void *memory,size_t size)
{
    return realloc(memory,size);
}
