/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SNAPSHOT_UNMOUNT_H
#define OZ_SNAPSHOT_UNMOUNT_H
/* Internal upstream dispatcher interface, not a caller-authentication API.
 * Native DOS handles cannot be forcibly invalidated. MNT_FORCE requests a
 * best-effort detach; busy locks/handles still return EBUSY. */
#define MNT_FORCE 1
int zfsctl_snapshot_unmount(const char *, int);
int oz_snapshot_unmount_preflight(const char *filesystem);
#endif
