/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_ZPL_FIXTURE_H
#define OZ_ZPL_FIXTURE_H
#include <stdint.h>
#include <stddef.h>
struct oz_zpl;
int oz_zpl_ended(struct oz_zpl *);
struct cred;
struct dsl_crypto_params;
int oz_zpl_fixture_create_crypto(const char *,struct dsl_crypto_params *);
/* Internal traversal result, distinct from host/AROS errno values. */
#define OZ_ZPL_SOFTLINK 0x10000
int oz_zpl_access(struct oz_zpl *,uint64_t,uint32_t,struct cred *);
int oz_zpl_counter(struct oz_zpl *, const char *, uint64_t *);
enum oz_zpl_special_kind {OZ_SPECIAL_NONE,OZ_SPECIAL_FIFO,OZ_SPECIAL_SOCKET,
    OZ_SPECIAL_CHARACTER,OZ_SPECIAL_BLOCK};
struct oz_zpl_info {
    uint64_t object,size,parent,mtime,blocks,generation;
    uint64_t uid,gid; /* Original on-disk FUIDs; never silently narrowed. */
    uint32_t mapped_uid,mapped_gid; /* OZ_ID_UNMAPPED if domain unmapped. */
    uint32_t native_protection;
    uint32_t mode; /* Upstream Unix mode; fallback display, not an ACL grant. */
    uint32_t mtime_nsec;
    int has_native_protection;
    int directory;
    int symlink;
    enum oz_zpl_special_kind special;
};
int oz_zpl_same_name(struct oz_zpl *,const char *,const char *,int *);
uint64_t oz_zpl_root(struct oz_zpl *fs);
int oz_zpl_stat(struct oz_zpl *fs,uint64_t object,struct oz_zpl_info *info);
void oz_zpl_space(struct oz_zpl *fs,uint64_t *referenced,uint64_t *available);
int oz_zpl_lookup_at(struct oz_zpl *fs,uint64_t parent,const char *name,struct oz_zpl_info *info);
int oz_zpl_replace_as(struct oz_zpl *,uint64_t,const char *,const char *,
    const struct oz_zpl_info *,const struct oz_zpl_info *,struct cred *,int (*)(void *),void *);
int oz_zpl_resolve(struct oz_zpl *fs,uint64_t parent,const char *path,struct oz_zpl_info *info);
int oz_zpl_resolve_as(struct oz_zpl *,uint64_t,const char *,struct oz_zpl_info *,struct cred *);
typedef int (*oz_path_policy_t)(void *,uint64_t,uint32_t,struct cred *);
int oz_zpl_resolve_policy(struct oz_zpl *,uint64_t,const char *,struct oz_zpl_info *,
    struct cred *,oz_path_policy_t,void *);
int oz_zpl_resolve_for(struct oz_zpl *,uint64_t,const char *,struct oz_zpl_info *,
    struct cred *,oz_path_policy_t,void *,uint32_t);
int oz_zpl_name(struct oz_zpl *fs,uint64_t object,char *name,size_t capacity);
int oz_zpl_nth(struct oz_zpl *fs,uint64_t directory,uint32_t index,char *name,size_t capacity,struct oz_zpl_info *info);
int oz_zpl_fixture_create(const char *dataset);
int oz_zpl_open(const char *dataset,struct oz_zpl **out);
/* Explicit writer admission; packet mutation must also enforce native policy.
 * No implicit mode upgrade. */
int oz_zpl_open_writable(const char *,struct oz_zpl **);
int oz_zpl_sync(struct oz_zpl *);
int oz_zpl_readonly(struct oz_zpl *);
int oz_zpl_exec(struct oz_zpl *);
int oz_zpl_mount_policy(struct oz_zpl *,uint32_t *);
int oz_zpl_shares_object(struct oz_zpl *,uint64_t *);
int oz_zpl_resolve_property(struct oz_zpl *,uint64_t,const char *,struct oz_zpl_info *,
    struct cred *,oz_path_policy_t,void *,uint32_t);
int oz_zpl_protection_store(struct oz_zpl *,uint64_t,uint32_t,struct cred *);
int oz_zpl_comment_get(struct oz_zpl *,uint64_t,char *,size_t);
int oz_zpl_comment_store(struct oz_zpl *,uint64_t,const char *,struct cred *);
int oz_zpl_set_mtime(struct oz_zpl *,uint64_t,int64_t,uint32_t,struct cred *);
/* Internal POSIX-ID operation. Native DOS ownership policy/mapping must be
 * checked separately before exposing this through an ownership packet. */
int oz_zpl_set_owner(struct oz_zpl *,uint64_t,uint32_t,uint32_t,struct cred *);
/* Object already reached through authorized traversal. Metadata read enforces
 * READ_ATTRIBUTES; mutation additionally requires owner/root upstream policy. */
int oz_zpl_project_get(struct oz_zpl *,uint64_t,uint32_t *,int *,struct cred *);
int oz_zpl_project_set(struct oz_zpl *,uint64_t,uint32_t,int,struct cred *);
int oz_zpl_close(struct oz_zpl *fs);
int oz_zpl_lookup(struct oz_zpl *fs,const char *name,uint64_t *object,uint64_t *size);
int oz_zpl_read(struct oz_zpl *fs,uint64_t object,uint64_t offset,void *buffer,size_t size);
int oz_zpl_read_as(struct oz_zpl *,uint64_t,uint64_t,void *,size_t,struct cred *);
int oz_zpl_write_as(struct oz_zpl *,uint64_t,uint64_t,const void *,size_t,size_t *,struct cred *);
int oz_zpl_rewrite_as(struct oz_zpl *,uint64_t,uint64_t,uint64_t,uint64_t,uint64_t,struct cred *);
int oz_zpl_resize_as(struct oz_zpl *,uint64_t,uint64_t,struct cred *);
int oz_zpl_create_as(struct oz_zpl *,uint64_t,const char *,uint32_t,struct cred *,struct oz_zpl_info *);
int oz_zpl_mkdir_as(struct oz_zpl *,uint64_t,const char *,uint32_t,struct cred *,struct oz_zpl_info *);
int oz_zpl_remove_as(struct oz_zpl *,uint64_t,const char *,int,struct cred *);
int oz_zpl_rename_as(struct oz_zpl *,uint64_t,const char *,uint64_t,const char *,struct cred *);
/* Internal exchange; no DOS packet or public management opcode implied. */
int oz_zpl_exchange_as(struct oz_zpl *,uint64_t,const char *,uint64_t,const char *,struct cred *);
int oz_zpl_whiteout_as(struct oz_zpl *,uint64_t,const char *,uint64_t,const char *,struct cred *);
int oz_zpl_whiteout_replace_as(struct oz_zpl *,uint64_t,const char *,uint64_t,const char *,struct cred *);
int oz_zpl_link_as(struct oz_zpl *,uint64_t,uint64_t,const char *,struct cred *);
int oz_zpl_symlink_as(struct oz_zpl *,uint64_t,const char *,const char *,uint32_t,struct cred *);
int oz_zpl_readlink_as(struct oz_zpl *,uint64_t,char *,size_t,struct cred *);
#endif
