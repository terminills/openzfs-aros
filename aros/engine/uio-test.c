/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/uio.h>

/* Runs in the actual native QEMU guest before pool creation, not on Linux. */
int
oz_uio_test(void)
{
    char a[4] = "abc", b[4] = "def", out[16] = {0};
    struct iovec vec[] = {{NULL, 0}, {a, 3}, {NULL, 0}, {b, 3}};
    struct iovec original[4];
    memcpy(original, vec, sizeof (vec));
    const offset_t start = (1ULL << 32) + 37;
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 6, 0);
    zfs_uio_t before = u;
    size_t copied = 99;
    if (zfs_uiocopy(out, sizeof (out), UIO_WRITE, &u, &copied) ||
        copied != 6 || memcmp(out, "abcdef", 6) ||
        memcmp(&u, &before, sizeof (u)) || memcmp(vec, original, sizeof (vec)))
        return (1);
    memset(out, 0, sizeof (out));
    if (zfs_uiomove(out, 2, UIO_WRITE, &u) || memcmp(out, "ab", 2) ||
        u.uio_skip != 2 || u.uio_resid != 4 || u.uio_loffset != start + 2)
        return (2);
    if (zfs_uiomove(out + 2, 10, UIO_WRITE, &u) ||
        memcmp(out, "abcdef", 6) || u.uio_resid || u.uio_iovcnt ||
        u.uio_loffset != start + 6 || u.uio_soffset != start ||
        memcmp(vec, original, sizeof (vec)))
        return (3);
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 6, 0);
    if (zfs_uiomove("ABCDEF", 6, UIO_READ, &u) ||
        memcmp(a, "ABC", 3) || memcmp(b, "DEF", 3))
        return (4);
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 6, 0);
    zfs_uioskip(&u, 4);
    if (u.uio_resid != 2 || u.uio_skip != 1 || u.uio_iovcnt != 1 ||
        u.uio_loffset != start + 4 || zfs_uiomove(out, 2, UIO_WRITE, &u) ||
        memcmp(out, "EF", 2))
        return (5);
    before = u;
    zfs_uioskip(&u, 1);
    if (memcmp(&u, &before, sizeof (u)))
        return (6);
    /* Nonzero initial vector skip, and zero-length operations at EOF. */
    zfs_uio_iovec_init(&u, vec + 1, 3, start, UIO_SYSSPACE, 5, 1);
    if (zfs_uiomove(out, 5, UIO_WRITE, &u) || memcmp(out, "BCDEF", 5) ||
        zfs_uiomove(NULL, 0, UIO_READ, &u))
        return (7);
    /* Invalid spans must fail before changing even the valid first vector. */
    struct iovec bad[] = {{a, 3}, {NULL, 3}};
    zfs_uio_iovec_init(&u, bad, 2, start, UIO_SYSSPACE, 6, 0);
    before = u;
    if (zfs_uiomove("xxxxxx", 6, UIO_READ, &u) != EFAULT ||
        memcmp(a, "ABC", 3) || memcmp(&u, &before, sizeof (u)))
        return (8);
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 7, 0);
    before = u;
    if (zfs_uiocopy(out, 7, UIO_WRITE, &u, &copied) != EINVAL ||
        copied || memcmp(&u, &before, sizeof (u)))
        return (9);
    zfs_uio_iovec_init(&u, vec, 4, INT64_MAX - 1, UIO_SYSSPACE, 6, 0);
    if (zfs_uiomove(out, 2, UIO_WRITE, &u) != EOVERFLOW)
        return (10);
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 6, 0);
    u.uio_extflg = 1;
    if (zfs_uiomove(out, 1, UIO_WRITE, &u) != ENOTSUP)
        return (11);
    u.uio_extflg = 0;
    u.uio_segflg = (zfs_uio_seg_t)99;
    if (zfs_uiomove(out, 1, UIO_WRITE, &u) != ENOTSUP)
        return (12);
    u.uio_segflg = UIO_SYSSPACE;
    if (zfs_uiomove(out, 1, (zfs_uio_rw_t)99, &u) != EINVAL ||
        zfs_uiomove(NULL, 1, UIO_READ, &u) != EFAULT)
        return (13);
    u.uio_resid = -1;
    if (zfs_uiomove(out, 1, UIO_WRITE, &u) != EINVAL)
        return (14);

    unsigned char *allocation = malloc(3 * PAGESIZE);
    if (!allocation) return (15);
    unsigned char *page = (void *)P2ROUNDUP((uintptr_t)allocation, (uintptr_t)PAGESIZE);
    struct iovec pages[] = {{page, PAGESIZE}, {page + PAGESIZE, PAGESIZE}};
    zfs_uio_iovec_init(&u, pages, 2, 2 * PAGESIZE, UIO_SYSSPACE, 2 * PAGESIZE, 0);
    int error = 0;
    before = u;
    if (!zfs_uio_page_aligned(&u) || !zfs_uio_aligned(&u, PAGESIZE) ||
        zfs_uio_prefaultpages(2 * PAGESIZE, &u) || memcmp(&u, &before, sizeof (u)))
        error = 16;
    zfs_uio_setoffset(&u, 2 * PAGESIZE + 1);
    zfs_uio_setsoffset(&u, 2 * PAGESIZE + 1);
    if (zfs_uio_aligned(&u, PAGESIZE) || !zfs_uio_page_aligned(&u) ||
        zfs_uio_soffset(&u) != 2 * PAGESIZE + 1 || u.uio_resid != 2 * PAGESIZE)
        error = 17;
    pages[0].iov_base = page + 1;
    if (zfs_uio_page_aligned(&u)) error = 18;
    /* Alignment must use the current cursor, not the original base address. */
    pages[0].iov_len = 2 * PAGESIZE - 1;
    zfs_uio_iovec_init(&u, pages, 1, 0, UIO_SYSSPACE, PAGESIZE, PAGESIZE - 1);
    if (!zfs_uio_page_aligned(&u)) error = 19;
    u.uio_skip++;
    u.uio_resid--;
    if (zfs_uio_page_aligned(&u)) error = 20;
    u.uio_extflg = 1;
    if (zfs_uio_prefaultpages(1, &u) != ENOTSUP) error = 21;
    u.uio_extflg = 0;
    if (zfs_uio_prefaultpages(-1, &u) != EINVAL) error = 22;
    pages[0].iov_base = NULL;
    if (zfs_uio_prefaultpages(1, &u) != EFAULT || zfs_uio_page_aligned(&u)) error = 23;
    free(allocation);
    if (error) return (error);
    /* Rewind across consumed vectors, including zero-length entries. */
    zfs_uio_iovec_init(&u, vec, 4, start, UIO_SYSSPACE, 6, 0);
    zfs_uio_t initial = u;
    zfs_uio_advance(&u, 4);
    if (u.uio_skip != 1 || u.uio_resid != 2) return (24);
    zfs_uio_advance(&u, -4);
    if (memcmp(&u, &initial, sizeof (u))) return (25);
    if (zfs_uiomove(out, 6, UIO_WRITE, &u)) return (26);
    zfs_uio_advance(&u, -3);
    memset(out, 0, sizeof (out));
    if (zfs_uiomove(out, 3, UIO_WRITE, &u) || memcmp(out, "DEF", 3)) return (27);
    before = u;
    zfs_uio_advance(&u, -7);
    zfs_uio_advance(&u, -SSIZE_MAX - 1);
    if (memcmp(&u, &before, sizeof (u))) return (28);
    zfs_uio_iovec_init(&u, vec + 1, 3, start, UIO_SYSSPACE, 5, 1);
    initial = u;
    zfs_uio_advance(&u, 5);
    zfs_uio_advance(&u, -5);
    if (memcmp(&u, &initial, sizeof (u))) return (29);
    /* Only the active byte range participates in alignment. Unused vector
     * tails may be unaligned or inaccessible and must not be inspected. */
    allocation = malloc(3 * PAGESIZE);
    if (!allocation) return (30);
    page = (void *)P2ROUNDUP((uintptr_t)allocation, (uintptr_t)PAGESIZE);
    struct iovec bounded[] = {{page, PAGESIZE + 1}, {NULL, 17}};
    zfs_uio_iovec_init(&u, bounded, 2, 0, UIO_SYSSPACE, PAGESIZE, 0);
    error = 0;
    if (!zfs_uio_page_aligned(&u) ||
        zfs_uio_get_dio_pages_alloc(&u, UIO_WRITE)) error = 31;
    else {
        if (u.uio_dio.npages != 1 || u.uio_dio.pages[0] != page) error = 32;
        zfs_uio_free_dio_pages(&u, UIO_WRITE);
    }
    zfs_uio_iovec_init(&u, bounded, 2, 0, UIO_SYSSPACE, PAGESIZE + 1, 0);
    if (zfs_uio_page_aligned(&u)) error = 33;
    free(allocation);
    if (error) return (error);
    return (0);
}
