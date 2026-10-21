/* SPDX-License-Identifier: CDDL-1.0 */
/* Native AROS shared-address-space buffer transfers for upstream ZPL. */
#include <sys/zfs_context.h>
#include <sys/uio.h>

/* Validate the entire requested span before modifying buffers or the cursor.
 * The caller owns and keeps the memory valid until its synchronous packet is
 * replied to. No Linux copy_to_user, page cache, or direct-I/O claim is made. */
static int
check(const zfs_uio_t *u, size_t n)
{
    if (!u || u->uio_iovcnt < 0 || u->uio_resid < 0 || u->uio_loffset < 0)
        return (EINVAL);
    if (u->uio_segflg != UIO_SYSSPACE || (u->uio_extflg & ~UIO_DIRECT))
        return (ENOTSUP);
    if ((u->uio_extflg & UIO_DIRECT) &&
        (!u->uio_dio.pages || !u->uio_dio.npages)) return (ENOTSUP);
    if (n > (size_t)u->uio_resid || n > INT64_MAX - (uint64_t)u->uio_loffset)
        return (EOVERFLOW);
    if (!n)
        return (0);
    if (!u->uio_iov)
        return (EFAULT);
    size_t skip = u->uio_skip;
    for (int i = 0; i < u->uio_iovcnt; i++) {
        const struct iovec *v = &u->uio_iov[i];
        if (skip > v->iov_len)
            return (EINVAL);
        size_t take = MIN(n, v->iov_len - skip);
        if (take && (!v->iov_base ||
            (uintptr_t)v->iov_base > UINTPTR_MAX - skip ||
            (uintptr_t)v->iov_base + skip > UINTPTR_MAX - (take - 1)))
            return (EFAULT);
        n -= take;
        if (!n)
            return (0);
        skip = 0;
    }
    return (EINVAL);
}

int
zfs_uio_prefaultpages(ssize_t n, zfs_uio_t *u)
{
    if (!u || n < 0 || u->uio_resid < 0)
        return (EINVAL);
    /* AROS synchronous DOS buffers are shared-address-space, not pageable
     * Linux user iov_iter memory. Validate their span; other segments are
     * rejected by check(), never treated as successfully pinned pages. */
    return (check(u, MIN((size_t)n, (size_t)u->uio_resid)));
}

boolean_t
zfs_uio_page_aligned(zfs_uio_t *u)
{
    if (!u || u->uio_resid < 0 || check(u, u->uio_resid) ||
        (u->uio_iovcnt && !u->uio_iov))
        return (B_FALSE);
    size_t skip = u->uio_skip, remaining = u->uio_resid;
    for (int i = 0; i < u->uio_iovcnt && remaining; i++) {
        const struct iovec *v = &u->uio_iov[i];
        if (skip > v->iov_len || (uintptr_t)v->iov_base > UINTPTR_MAX - skip)
            return (B_FALSE);
        size_t take = MIN(remaining, v->iov_len - skip);
        uintptr_t addr = (uintptr_t)v->iov_base + skip;
        if (take && ((addr & (PAGESIZE - 1)) || (take & (PAGESIZE - 1))))
            return (B_FALSE);
        remaining -= take;
        skip = 0;
    }
    return (B_TRUE);
}

/* Only called with a validated span. Iovec contents are never modified, so
 * zfs_uiocopy can safely use a shallow cursor copy, as upstream expects. */
static void
transfer(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *u, boolean_t copy)
{
    while (n) {
        struct iovec *v = u->uio_iov;
        size_t take = MIN(n, v->iov_len - u->uio_skip);
        if (copy && take) {
            char *q = (char *)v->iov_base + u->uio_skip;
            if (rw == UIO_READ)
                memcpy(q, p, take);
            else
                memcpy(p, q, take);
            p = (char *)p + take;
        }
        u->uio_skip += take;
        if (u->uio_skip == v->iov_len) {
            u->uio_skip = 0;
            u->uio_iov++;
            u->uio_iovcnt--;
        }
        u->uio_resid -= take;
        u->uio_loffset += take;
        n -= take;
    }
}

int
zfs_uiomove(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *u)
{
    if (!u || u->uio_resid < 0 || (rw != UIO_READ && rw != UIO_WRITE))
        return (EINVAL);
    n = MIN(n, (size_t)u->uio_resid);
    int error = check(u, n);
    if (error)
        return (error);
    if (n && (!p || (uintptr_t)p > UINTPTR_MAX - (n - 1)))
        return (EFAULT);
    transfer(p, n, rw, u, B_TRUE);
    return (0);
}

int
zfs_uiocopy(void *p, size_t n, zfs_uio_rw_t rw, zfs_uio_t *u, size_t *copied)
{
    if (!copied)
        return (EINVAL);
    *copied = 0;
    if (!u)
        return (EINVAL);
    zfs_uio_t cursor = *u;
    int error = zfs_uiomove(p, n, rw, &cursor);
    if (!error)
        *copied = u->uio_resid - cursor.uio_resid;
    return (error);
}

void
zfs_uioskip(zfs_uio_t *u, size_t n)
{
    /* Upstream's void API leaves the cursor unchanged on an oversize skip. */
    if (!u || u->uio_resid < 0 || n > (size_t)u->uio_resid)
        return;
    int error = check(u, n);
    ASSERT0(error);
    if (!error)
        transfer(NULL, n, UIO_READ, u, B_FALSE);
}

/* Descriptor ownership only: synchronous AROS shared-memory buffers remain
 * owned by the caller. No virtual-memory pinning or physical-address claim. */
int
zfs_uio_get_dio_pages_alloc(zfs_uio_t *u, zfs_uio_rw_t rw)
{
    if (!u || (rw != UIO_READ && rw != UIO_WRITE)) return (EINVAL);
    if (u->uio_extflg || u->uio_dio.pages) return (EBUSY);
    if (u->uio_resid <= 0) return (EINVAL);
    int error = check(u, u->uio_resid);
    if (error) return (error);
    if (!zfs_uio_page_aligned(u) || !zfs_uio_aligned(u, PAGESIZE)) return (EINVAL);
    size_t count = (size_t)u->uio_resid / PAGESIZE;
    if (count > SIZE_MAX / sizeof (void *)) return (EOVERFLOW);
    void **pages = malloc(count * sizeof (*pages));
    if (!pages) return (ENOMEM);
    size_t skip = u->uio_skip, page = 0;
    for (int i = 0; i < u->uio_iovcnt && page < count; i++) {
        const struct iovec *v = &u->uio_iov[i];
        for (size_t off = skip; off < v->iov_len && page < count; off += PAGESIZE)
            pages[page++] = (char *)v->iov_base + off;
        skip = 0;
    }
    ASSERT3U(page, ==, count);
    u->uio_dio.pages = pages;
    u->uio_dio.npages = count;
    u->uio_soffset = u->uio_loffset;
    u->uio_extflg |= UIO_DIRECT;
    return (0);
}

void
zfs_uio_free_dio_pages(zfs_uio_t *u, zfs_uio_rw_t rw)
{
    ASSERT(rw == UIO_READ || rw == UIO_WRITE);
    if (!u) return;
    free(u->uio_dio.pages);
    u->uio_dio.pages = NULL;
    u->uio_dio.npages = 0;
    u->uio_extflg &= ~UIO_DIRECT;
}

void
zfs_uio_advance(zfs_uio_t *u, ssize_t size)
{
    if (size >= 0) {
        zfs_uioskip(u, size);
        return;
    }
    /* Reconstruct a previous cursor without ever modifying caller iovecs.
     * Avoid negating SSIZE_MIN. Invalid rewinds leave all state unchanged. */
    if (!u || size == -SSIZE_MAX - 1 || u->uio_resid < 0 ||
        u->uio_initial_resid < u->uio_resid ||
        -size > u->uio_initial_resid - u->uio_resid ||
        u->uio_loffset < -size) return;
    ssize_t consumed = u->uio_initial_resid - u->uio_resid + size;
    zfs_uio_t cursor = *u;
    cursor.uio_iov = u->uio_initial_iov;
    cursor.uio_iovcnt = u->uio_initial_iovcnt;
    cursor.uio_skip = u->uio_initial_skip;
    cursor.uio_resid = u->uio_initial_resid;
    cursor.uio_loffset = u->uio_loffset + size - consumed;
    if (check(&cursor, consumed)) return;
    transfer(NULL, consumed, UIO_READ, &cursor, B_FALSE);
    *u = cursor;
}
