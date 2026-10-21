/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef _ZFS_IOCTL_NATIVE_H_
#define _ZFS_IOCTL_NATIVE_H_
#ifdef __AROS__
/* Resident engine alternative to zfs_kmod_init: SPA/ZPL must already be live.
 * The service owner serializes startup/shutdown, drains requests and joins
 * dispatcher workers before shutdown (their history TLS exits with them). */
int zfs_ioctl_native_init(void);
int zfs_ioctl_native_fini(void);
#endif
#endif
