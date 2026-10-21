/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ZVOL_NATIVE_H
#define OZ_ZVOL_NATIVE_H
#include <sys/zfs_context.h>
struct oz_zvol_handle;
int zvol_init(void);
void zvol_fini(void);
/* Resident-service backend, not yet an Exec .device frontend. Callers must
 * authenticate credentials; raw volume access currently requires ZFS admin.
 * Close is serialized after all operations using that handle have finished. */
int oz_zvol_open(const char *,boolean_t,cred_t *,struct oz_zvol_handle **);
void oz_zvol_close(struct oz_zvol_handle *);
int oz_zvol_io(struct oz_zvol_handle *,uint64_t,void *,size_t,boolean_t,size_t *);
int oz_zvol_flush(struct oz_zvol_handle *);
int oz_zvol_discard(struct oz_zvol_handle *,uint64_t,uint64_t);
int oz_zvol_info(struct oz_zvol_handle *,uint64_t *,uint64_t *,boolean_t *);
#endif
