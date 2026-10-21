/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/abd.h>
#include <sys/dmu_impl.h>
#include <sys/dnode.h>
#include <sys/dmu_tx.h>
#include <sys/dmu_objset.h>

/* ZPL context; run after kernel_init for upstream ABD and direct-Uio paths. */
int
oz_dio_test(void)
{
    void *alloc[2] = {malloc(2 * PAGESIZE), malloc(2 * PAGESIZE)};
    if (!alloc[0] || !alloc[1]) { free(alloc[0]); free(alloc[1]); return (1); }
    char *page[2];
    struct iovec vec[2];
    for (unsigned i = 0; i < 2; i++) {
        page[i] = (char *)P2ROUNDUP((uintptr_t)alloc[i], PAGESIZE);
        memset(page[i], i ? 'B' : 'A', PAGESIZE);
        vec[i] = (struct iovec){page[i], PAGESIZE};
    }
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, vec, 2, 2 * PAGESIZE, UIO_SYSSPACE, 2 * PAGESIZE, 0);
    int error = zfs_uio_get_dio_pages_alloc(&u, UIO_READ);
    if (!error && (u.uio_dio.npages != 2 || u.uio_dio.pages[0] != page[0] ||
        u.uio_dio.pages[1] != page[1] || !(u.uio_extflg & UIO_DIRECT) ||
        zfs_uio_get_dio_pages_alloc(&u, UIO_READ) != EBUSY)) error = 2;
    if (!error) {
        /* Cross-page borrowed view with nonzero head and tail offsets. */
        abd_t *abd = abd_alloc_from_pages(u.uio_dio.pages, PAGESIZE - 4, 8);
        char bytes[8];
        abd_copy_to_buf(bytes, abd, sizeof (bytes));
        if (memcmp(bytes, "AAAABBBB", 8)) error = 3;
        abd_copy_from_buf(abd, "12345678", 8);
        if (memcmp(page[0] + PAGESIZE - 4, "1234", 4) ||
            memcmp(page[1], "5678", 4) || page[0][PAGESIZE - 5] != 'A' || page[1][4] != 'B')
            error = 4;
        abd_free(abd);
        zfs_uioskip(&u, PAGESIZE);
        if (u.uio_resid != PAGESIZE || u.uio_loffset != 3 * PAGESIZE) error = 5;
    }
    zfs_uio_free_dio_pages(&u, UIO_READ);
    if (u.uio_dio.pages || u.uio_dio.npages || u.uio_extflg) error = 6;
    zfs_uio_free_dio_pages(&u, UIO_READ); /* descriptor cleanup is idempotent */
    page[0][0] = 'Z'; /* ABD/page descriptor destruction did not own this memory. */
    zfs_uio_iovec_init(&u, vec, 2, 1, UIO_SYSSPACE, 2 * PAGESIZE, 0);
    if (zfs_uio_get_dio_pages_alloc(&u, UIO_WRITE) != EINVAL || u.uio_dio.pages || u.uio_extflg)
        error = 7;
    free(alloc[0]); free(alloc[1]);
    return (error);
}

int
oz_dio_read_test(objset_t *os, uint64_t object)
{
    enum { BYTES = 16384 };
    void *allocation = malloc(BYTES + PAGESIZE);
    if (!allocation) return (ENOMEM);
    unsigned char *buffer = (void *)P2ROUNDUP((uintptr_t)allocation, PAGESIZE);
    memset(buffer, 0, BYTES);
    struct iovec vec = {buffer, BYTES};
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, BYTES, 0);
    int error = zfs_uio_get_dio_pages_alloc(&u, UIO_READ);
    dnode_t *dn = NULL;
    if (!error) error = dnode_hold(os, object, FTAG, &dn);
    for (unsigned off = 0; !error && off < BYTES; off += PAGESIZE)
        error = dmu_read_uio_direct(dn, &u, PAGESIZE, DMU_DIRECTIO);
    if (!error && (u.uio_resid || u.uio_loffset != BYTES)) error = EIO;
    for (unsigned i = 0; !error && i < BYTES; i++)
        if (buffer[i] != (unsigned char)((i * 37u) ^ (i >> 5) ^ 0xa7 ^
            ((i >= 3077 && i < 9078) ? 0x5a : 0))) error = EIO;
    if (dn) dnode_rele(dn, FTAG);
    zfs_uio_free_dio_pages(&u, UIO_READ);
    free(allocation);
    return (error);
}

int
oz_dio_write_test(objset_t *os, uint64_t object)
{
    enum { BYTES = 16384 };
    void *allocation = malloc(BYTES + PAGESIZE);
    if (!allocation) return (ENOMEM);
    unsigned char *buffer = (void *)P2ROUNDUP((uintptr_t)allocation, PAGESIZE);
    for (unsigned i = 0; i < BYTES; i++) buffer[i] = (i * 37u) ^ (i >> 5) ^ 0xa7;
    struct iovec vec = {buffer, BYTES};
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, &vec, 1, 0, UIO_SYSSPACE, BYTES, 0);
    int error = zfs_uio_get_dio_pages_alloc(&u, UIO_WRITE);
    dnode_t *dn = NULL;
    if (!error) error = dnode_hold(os, object, FTAG, &dn);
    if (!error) {
        dmu_tx_t *tx = dmu_tx_create(os);
        dmu_tx_hold_write(tx, object, 0, BYTES);
        error = dmu_tx_assign(tx, DMU_TX_WAIT);
        if (error) dmu_tx_abort(tx);
        else {
            for (unsigned off = 0; !error && off < BYTES; off += PAGESIZE)
                error = dmu_write_uio_direct(dn, &u, PAGESIZE, DMU_DIRECTIO, tx);
            dmu_tx_commit(tx);
        }
    }
    if (!error && (u.uio_resid || u.uio_loffset != BYTES)) error = EIO;
    if (dn) dnode_rele(dn, FTAG);
    zfs_uio_free_dio_pages(&u, UIO_WRITE);
    free(allocation);
    return (error);
}

int
oz_dio_cold_read_test(void)
{
    objset_t *os = NULL;
    int error = dmu_objset_own("qemu_mirror/payload", DMU_OST_OTHER,
        B_TRUE, B_FALSE, FTAG, &os);
    if (error) return (error);
    uint64_t object = 0;
    error = dmu_object_next(os, &object, B_FALSE, 0);
    if (!error) error = oz_dio_read_test(os, object);
    dmu_objset_disown(os, B_FALSE, FTAG);
    return (error);
}
