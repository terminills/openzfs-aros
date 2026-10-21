/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_CONDVAR_H
#define OZ_AROS_CONDVAR_H
/* Use the real native pthread/SPL wait/signal implementation, not kernel
 * condition-variable structures from a different operating system. */
#include <sys/zfs_context.h>
#endif
