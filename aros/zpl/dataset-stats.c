/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/dataset_kstats.h>

/* Native named/virtual provider for upstream dataset statistics. This is
 * mount-owned, not a replacement for all raw/global kstats. The caller keeps
 * mount lifetime across reads and excludes destroy from active readers. */
struct native_kstat { kstat_t header; kmutex_t lock; boolean_t installed; };

static kstat_t *
native_kstat_create(const char *module, int instance, const char *name,
    const char *class, uchar_t type, ulong_t count, uchar_t flags)
{
    if (!module || !name || !class || type != KSTAT_TYPE_NAMED ||
        flags != KSTAT_FLAG_VIRTUAL || !count || count > UINT_MAX ||
        count > SIZE_MAX / sizeof (kstat_named_t) ||
        strlen(module) >= KSTAT_STRLEN || strlen(name) >= KSTAT_STRLEN ||
        strlen(class) >= KSTAT_STRLEN) return (NULL);
    struct native_kstat *n = kmem_zalloc(sizeof (*n), KM_SLEEP);
    kstat_t *k = &n->header;
    strlcpy(k->ks_module, module, sizeof (k->ks_module));
    strlcpy(k->ks_name, name, sizeof (k->ks_name));
    strlcpy(k->ks_class, class, sizeof (k->ks_class));
    k->ks_instance = instance; k->ks_type = type; k->ks_flags = flags;
    k->ks_ndata = count; k->ks_data_size = count * sizeof (kstat_named_t);
    k->ks_crtime = gethrtime();
    mutex_init(&n->lock, NULL, MUTEX_DEFAULT, NULL);
    return (k);
}

static void native_kstat_install(kstat_t *k)
{
    struct native_kstat *n = (void *)k;
    ASSERT(k->ks_data && k->ks_update);
    mutex_enter(&n->lock); n->installed = B_TRUE; mutex_exit(&n->lock);
}

static void native_kstat_delete(kstat_t *k)
{
    struct native_kstat *n = (void *)k;
    if (!n) return;
    mutex_enter(&n->lock); n->installed = B_FALSE; mutex_exit(&n->lock);
    mutex_destroy(&n->lock);
    /* Virtual data belongs to the upstream dataset provider. */
    kmem_free(n, sizeof (*n));
}

#define kstat_create native_kstat_create
#define kstat_install native_kstat_install
#define kstat_delete native_kstat_delete
#include "../../module/zfs/dataset_kstats.c"

int
oz_dataset_read_counter(dataset_kstats_t *dk, const char *name, uint64_t *out)
{
    if (!dk || !name || !out) return (EINVAL);
    if (!dk->dk_kstats) return (ENOTSUP);
    struct native_kstat *n = (void *)dk->dk_kstats;
    kstat_t *k = &n->header;
    mutex_enter(&n->lock);
    int error = n->installed ? k->ks_update(k, KSTAT_READ) : ENOENT;
    if (!error) {
        k->ks_snaptime = gethrtime();
        kstat_named_t *values = k->ks_data;
        error = ENOENT;
        for (uint_t i = 0; i < k->ks_ndata; i++) {
            if (strcmp(values[i].name, name)) continue;
            error = values[i].data_type == KSTAT_DATA_UINT64 ? 0 : ENOTSUP;
            if (!error) *out = values[i].value.ui64;
            break;
        }
    }
    mutex_exit(&n->lock);
    return (error);
}
