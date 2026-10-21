/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_EFI_NATIVE_H
#define OZ_EFI_NATIVE_H
#include <stddef.h>
#include <stdint.h>
/* Private libefi handles, NOT POSIX file descriptors. Callers must own the
 * whole device; DOS collision checks do not establish exclusive ownership.
 * All operations return -1/errno on error except close (errno value). */
int oz_efi_open(const char *,unsigned,int);
int oz_efi_close(int);
int oz_efi_disk_info(int,uint64_t *,unsigned *);
int oz_efi_transfer(int,uint64_t,void *,size_t,int);
int oz_efi_rescan(int);
/* Short synchronous GPT transaction guard; errno-value returns. Excludes
 * DOS-list publication and other EFI callers until the owning task ends it.
 * Nonblocking acquisition, no nesting, no close while held. Not a raw-device
 * system-wide reservation; external raw users remain the caller's concern. */
int oz_efi_guard_begin(int);
int oz_efi_guard_end(int);
/* Destructive upstream GPT labeling of an already owned writable whole-disk
 * handle. Start 0 selects NEW_START_BLOCK. Returns an errno value; a failure
 * after writing does not imply unchanged media. Caller owns authorization,
 * foreign-filesystem checks and external raw-device coordination. These
 * helpers hold the DOS publication guard. No public CLI admission. */
int oz_efi_label(int,uint64_t,const char *);
/* Upstream whole-disk expansion, preserving existing identities. No-space
 * (already full or unsuitable layout) is a successful no-op as in libzfs.
 * changed is written only on success; errors may follow partial writes. */
int oz_efi_expand(int,int *);
#endif
