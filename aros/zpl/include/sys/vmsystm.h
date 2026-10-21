/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_VMSYSTM_H
#define OZ_AROS_VMSYSTM_H
/* No Linux/FreeBSD VM is emulated. Native DMU transfers use checked,
 * synchronous caller-owned shared-memory UIO buffers. */
#include <sys/uio.h>
#endif
