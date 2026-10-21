/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_CRED_H
#define OZ_AROS_CRED_H
#include <sys/types.h>

typedef struct cred cred_t;
/* Explicit identity snapshots. Creating a snapshot does NOT authenticate its
 * caller; the eventual DOS/management boundary must obtain a trusted identity.
 * A NULL snapshot is anonymous, never the privileged engine identity. */
cred_t *oz_cred_create(uid_t, uid_t, gid_t, size_t, const gid_t *);
cred_t *oz_cred_kernel(void);
void oz_cred_hold(cred_t *);
void oz_cred_release(cred_t *);
struct oz_cred_scope;
/* Internal request scope, NOT an authentication interface. No inheritance to
 * workers: upstream must explicitly retain/pass credentials to sync tasks. */
cred_t *oz_cred_current(void);
int oz_cred_push(cred_t *, struct oz_cred_scope **);
int oz_cred_pop(struct oz_cred_scope *);
int oz_dataset_create_perms(const char *, cred_t *);
int oz_dataset_mount_perms(const char *, cred_t *);
int oz_dataset_userprop_perms(const char *, cred_t *);
int oz_dataset_property_perms(const char *, const char *, cred_t *);
int oz_send_perms(const char *, boolean_t raw, cred_t *);
int oz_receive_perms(const char *, boolean_t force, cred_t *);
int oz_dataset_rollback_perms(const char *, cred_t *);
int oz_bookmark_perms(const char *, int, cred_t *);
/* Output is at least ZFS_MAX_DATASET_NAME_LEN bytes. */
int oz_dataset_promote_perms(const char *, cred_t *, char *);
int oz_snapshot_clone_perms(const char *, const char *, cred_t *);
int oz_snapshot_hold_perms(const char *, int, cred_t *);
int groupmember(gid_t, cred_t *);
struct znode;
int secpolicy_vnode_setid_retain(struct znode *, const cred_t *, boolean_t);
int secpolicy_sys_config(const cred_t *, boolean_t);
int secpolicy_zinject(const cred_t *);
boolean_t is_system_labeled(void);
#endif
