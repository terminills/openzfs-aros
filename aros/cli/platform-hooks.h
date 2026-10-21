/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PLATFORM_HOOKS_H
#define OZ_PLATFORM_HOOKS_H
#include <libzfs.h>
#include <stdio.h>
/* Native CLI notice, with an explicit output stream for runtime verification. */
int oz_pool_upgrade_notice(zpool_handle_t *,FILE *);
#endif
