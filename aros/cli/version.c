/* SPDX-License-Identifier: CDDL-1.0 */
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "../ui/admission-api.h"
#include "../ui/support-api.h"
/* Report the connected engine, never the client's compiled-in version. */
char *zfs_version_kernel(void)
{
    char version[sizeof(((struct oz_engine_version_query *)0)->engine)];
    int error=oz_engine_version_query(OZ_ADMISSION_PORT,version,sizeof(version));
    if(error){errno=error;return NULL;}
    return strdup(version);
}
