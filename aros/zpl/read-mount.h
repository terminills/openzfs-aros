/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_READ_MOUNT_H
#define OZ_READ_MOUNT_H
/* Private bridge between the libzpool frontend and native ZPL context.
 * Borrowed-objset entry points retain caller-owned objset/SA lifetime.
 * Dataset entry point owns its objset with the native zfsvfs as ownership tag.
 * The creating task owns close and lifecycle transitions, and excludes them
 * from DOS operations. Foreign-task close returns EDEADLK without teardown.
 * Published upstream mount borrowers independently block close with EBUSY. */
struct oz_read_mount;
int oz_read_mount_is_owner(struct oz_read_mount *);
struct oz_zpl_info;
int oz_read_mount_replace(struct oz_read_mount *,uint64_t,const char *,const char *,
    const struct oz_zpl_info *,const struct oz_zpl_info *,cred_t *,int (*)(void *),void *);
struct zfsvfs;
struct oz_mount_policy;
int oz_mount_policy_open(struct zfsvfs *, boolean_t, struct oz_mount_policy **);
void oz_mount_policy_close(struct oz_mount_policy *);
int oz_zpl_mount_metadata(struct zfsvfs *, objset_t *);
int oz_read_mount_counter(struct oz_read_mount *, const char *, uint64_t *);
int oz_read_mount_access(struct oz_read_mount *, uint64_t, uint32_t, cred_t *);
int oz_read_mount_stat(struct oz_read_mount *, uint64_t, struct oz_zpl_info *);
int oz_read_mount_lookup(struct oz_read_mount *, uint64_t, const char *, struct oz_zpl_info *);
int oz_read_mount_same_name(struct oz_read_mount *,const char *,const char *,int *);
int oz_read_mount_open(objset_t *, sa_attr_type_t *, uint64_t,
    struct oz_read_mount **);
int oz_read_mount_open_mode(objset_t *, sa_attr_type_t *, uint64_t,
    boolean_t, struct oz_read_mount **);
int oz_read_mount_open_dataset(const char *, boolean_t, struct oz_read_mount **);
/* Borrowed current state: caller excludes close/suspend/resume. Never cache
 * these across a lifecycle transition that can replace the underlying objset. */
objset_t *oz_read_mount_objset(struct oz_read_mount *);
uint64_t oz_read_mount_root(struct oz_read_mount *);
int oz_read_mount_ended(struct oz_read_mount *);
/* On failed recovery, a non-NULL output is cleanup-only and retains objset
 * ownership until close succeeds. Never expose that mount to DOS callers. */
int oz_read_mount_sync(struct oz_read_mount *);
int oz_read_mount_readonly(struct oz_read_mount *);
int oz_read_mount_exec(struct oz_read_mount *);
int oz_read_mount_policy(struct oz_read_mount *,uint32_t *);
int oz_read_mount_shares_object(struct oz_read_mount *,uint64_t *);
int oz_read_mount_protection_store(struct oz_read_mount *,uint64_t,uint32_t,cred_t *);
/* Internal metadata read; DOS callers authorize the object before using it. */
int oz_read_mount_comment_get(struct oz_read_mount *,uint64_t,char *,size_t);
int oz_read_mount_comment_store(struct oz_read_mount *,uint64_t,const char *,cred_t *);
int oz_read_mount_set_mtime(struct oz_read_mount *,uint64_t,int64_t,uint32_t,cred_t *);
int oz_read_mount_set_owner(struct oz_read_mount *,uint64_t,uint32_t,uint32_t,cred_t *);
int oz_read_mount_project_get(struct oz_read_mount *,uint64_t,uint32_t *,int *,cred_t *);
int oz_read_mount_project_set(struct oz_read_mount *,uint64_t,uint32_t,int,cred_t *);
int oz_read_mount_close(struct oz_read_mount *);
int oz_read_mount_read(struct oz_read_mount *, uint64_t, uint64_t, void *, size_t);
int oz_read_mount_write(struct oz_read_mount *,uint64_t,uint64_t,const void *,size_t,size_t *,cred_t *);
int oz_read_mount_rewrite(struct oz_read_mount *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,cred_t *);
int oz_read_mount_resize(struct oz_read_mount *,uint64_t,uint64_t,cred_t *);
int oz_read_mount_create(struct oz_read_mount *,uint64_t,const char *,uint32_t,cred_t *,struct oz_zpl_info *);
int oz_read_mount_mkdir(struct oz_read_mount *,uint64_t,const char *,uint32_t,cred_t *,struct oz_zpl_info *);
/* Caller serializes the mount and excludes DOS handles/locks on the target. */
int oz_read_mount_remove(struct oz_read_mount *,uint64_t,const char *,int,cred_t *);
int oz_read_mount_rename(struct oz_read_mount *,uint64_t,const char *,uint64_t,const char *,cred_t *);
int oz_read_mount_exchange(struct oz_read_mount *,uint64_t,const char *,uint64_t,const char *,cred_t *);
int oz_read_mount_whiteout(struct oz_read_mount *,uint64_t,const char *,uint64_t,const char *,cred_t *);
int oz_read_mount_whiteout_replace(struct oz_read_mount *,uint64_t,const char *,uint64_t,const char *,cred_t *);
int oz_read_mount_link(struct oz_read_mount *,uint64_t,uint64_t,const char *,cred_t *);
int oz_read_mount_symlink(struct oz_read_mount *,uint64_t,const char *,const char *,uint32_t,cred_t *);
int oz_read_mount_readlink(struct oz_read_mount *,uint64_t,char *,size_t,cred_t *);
#endif
