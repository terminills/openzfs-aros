/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_ZPL_LIFECYCLE_H
#define OZ_AROS_ZPL_LIFECYCLE_H
/* Engine-owner calls only: init after kernel_init; fini after kernel_fini. */
void zfs_init(void);
void zfs_fini(void);
#endif
