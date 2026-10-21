/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/dmu.h>

/* Borrow native shared-memory pages without copying their data. Each child
 * owns only its ABD descriptor. Freeing the gang must not free caller pages. */
abd_t *
abd_alloc_from_pages(void **pages, unsigned long offset, uint64_t size)
{
    VERIFY(pages != NULL);
    VERIFY3U(offset, <, PAGESIZE);
    VERIFY3U(size, >, 0);
    VERIFY3U(size, <=, DMU_MAX_ACCESS);
    abd_t *gang = abd_alloc_gang();
    gang->abd_flags |= ABD_FLAG_FROM_PAGES;
    for (size_t i = 0; size; i++) {
        VERIFY(pages[i] != NULL);
        size_t take = MIN(size, PAGESIZE - offset);
        abd_t *part = abd_get_from_buf((char *)pages[i] + offset, take);
        abd_gang_add(gang, part, B_TRUE);
        size -= take;
        offset = 0;
    }
    return (gang);
}
