/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CONTROL_FILE_H
#define OZ_CONTROL_FILE_H
#include <sys/zfs_file.h>
/* Internal service only, not a public /dev namespace or client pointer API.
 * Start/stop/open are serialized by the owner task. Upstream fm must be live
 * before start and remain live through successful stop. Stop rejects live
 * files, including operation references that outlive a closed session. */
int oz_control_start(void);
int oz_control_stop(void);
int oz_control_file_open(zfs_file_t **);
#endif
