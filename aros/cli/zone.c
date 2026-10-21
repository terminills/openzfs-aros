/* SPDX-License-Identifier: CDDL-1.0 */
#include <zone.h>
/* AROS has a single global filesystem namespace, not Solaris zones.
 * This says nothing about UID or privilege: native packet credentials and
 * the service's OpenZFS permission checks remain mandatory. */
zoneid_t getzoneid(void)
{
    return GLOBAL_ZONEID;
}
