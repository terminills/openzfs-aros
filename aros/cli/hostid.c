/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/systeminfo.h>
#include <errno.h>
#include "../ui/support-api.h"
#include "../ui/admission-api.h"
unsigned long get_system_hostid(void)
{
    ULONG hostid=0;
    int error=oz_hostid_query(OZ_ADMISSION_PORT,&hostid);
    if(error){errno=error;return 0;}
    /* Zero means unprovisioned. Never invent an identity or trust a client's
     * environment in place of the identity actually used by the pool engine. */
    return hostid;
}
