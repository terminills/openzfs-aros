/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/aros_idmap.h>

struct oz_idmap {
    size_t count;
    struct oz_idmap_entry entries[];
};

void
oz_idmap_destroy(struct oz_idmap *map)
{
    if (!map) return;
    for (size_t i = 0; i < map->count; i++)
        free((void *)map->entries[i].domain);
    free(map);
}

int
oz_idmap_create(const struct oz_idmap_entry *entries, size_t count,
    struct oz_idmap **out)
{
    if (!out) return (EINVAL);
    *out = NULL;
    if ((count && !entries) ||
        count > (SIZE_MAX - sizeof (struct oz_idmap)) / sizeof (*entries))
        return (EINVAL);
    struct oz_idmap *map = calloc(1, sizeof (*map) + count * sizeof (*entries));
    if (!map) return (ENOMEM);
    int error = 0;
    for (size_t i = 0; i < count; i++) {
        const struct oz_idmap_entry *e = &entries[i];
        if (!e->domain || !*e->domain || e->local_id == OZ_ID_UNMAPPED ||
            (e->group != B_TRUE && e->group != B_FALSE)) { error = EINVAL; break; }
        for (size_t j = 0; j < i; j++)
            if (e->rid == map->entries[j].rid && e->group == map->entries[j].group &&
                !strcmp(e->domain, map->entries[j].domain)) { error = EEXIST; break; }
        if (error) break;
        char *domain = strdup(e->domain);
        if (!domain) { error = ENOMEM; break; }
        map->entries[i] = *e;
        map->entries[i].domain = domain;
        map->count++;
    }
    if (error) { oz_idmap_destroy(map); return (error); }
    *out = map;
    return (0);
}

int
oz_idmap_lookup(const struct oz_idmap *map, const char *domain, uint32_t rid,
    boolean_t group, uint32_t *id)
{
    if (!id) return (EINVAL);
    *id = OZ_ID_UNMAPPED;
    if (!domain || !*domain || (group != B_TRUE && group != B_FALSE)) return (EINVAL);
    if (!map) return (ENOENT);
    for (size_t i = 0; i < map->count; i++) {
        const struct oz_idmap_entry *e = &map->entries[i];
        if (e->rid == rid && e->group == group && !strcmp(e->domain, domain)) {
            *id = e->local_id;
            return (0);
        }
    }
    return (ENOENT);
}
