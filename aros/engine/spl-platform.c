/* SPDX-License-Identifier: CDDL-1.0 */
/* Native identities for the userspace engine bootstrap. */
#include <sys/types.h>
#include <sys/systeminfo.h>
#include <limits.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <proto/dos.h>
#include <libzutil.h>
#include "hostid-policy.h"
static uint32_t native_hostid;
static int native_identity_selected;
void oz_hostid_set_native(uint32_t identity)
{native_hostid=identity;native_identity_selected=1;}

const char * const *
zpool_default_search_paths(size_t *count)
{
    /* No raw-device enumeration until a native device backend is implemented.
     * File vdevs must be supplied explicitly, never discovered on the host. */
    static const char * const paths[] = { NULL };
    *count = 0;
    return paths;
}


ssize_t
getexecname_impl(char *name)
{
    if (!GetProgramName((STRPTR)name, PATH_MAX)) {
        errno = EIO;
        return -1;
    }
    return strlen(name);
}

unsigned long
get_system_hostid(void)
{
    if(native_identity_selected)return native_hostid;
    /* Standalone bootstrap fixtures only. Production selects protected native
     * preferences before kernel_init; client/service environment is ignored. */
    const char *value = getenv("ZFS_HOSTID");
    char *end;
    unsigned long long id;
    if (!value || !*value || *value == '-')
        return 0;
    errno = 0;
    id = strtoull(value, &end, 0);
    if (errno || *end || id == 0 || id > HOSTID_MASK)
        return 0;
    return (unsigned long)id;
}
