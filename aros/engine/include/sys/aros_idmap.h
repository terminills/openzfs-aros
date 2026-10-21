/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_IDMAP_H
#define OZ_AROS_IDMAP_H
#include <sys/types.h>
#define OZ_ID_UNMAPPED UINT32_MAX
struct oz_idmap;
struct oz_idmap_entry {
    const char *domain;
    uint32_t rid;
    uint32_t local_id;
    boolean_t group;
};
/* Immutable, mount-owned mapping snapshot. Caller must quiesce readers before
 * destroy. Creating a mapping does not authorize changing a mounted mapping. */
int oz_idmap_create(const struct oz_idmap_entry *, size_t, struct oz_idmap **);
void oz_idmap_destroy(struct oz_idmap *);
int oz_idmap_lookup(const struct oz_idmap *, const char *, uint32_t, boolean_t,
    uint32_t *);
#endif
