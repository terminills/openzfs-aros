/* SPDX-License-Identifier: CDDL-1.0 */
#include <sys/zfs_context.h>
#include <sys/dmu.h>
#include <sys/dmu_tx.h>

/* Overlay a non-page-aligned range of the direct-I/O fixture. All surrounding
 * bytes must remain intact; the cold direct-read test checks the same pattern. */
int
oz_buffered_test(objset_t *os, uint64_t object)
{
    enum { BYTES = 16384, START = 3077, LENGTH = 6001 };
    unsigned char *expected = malloc(BYTES), *actual = malloc(BYTES);
    if (!expected || !actual) { free(expected); free(actual); return (ENOMEM); }
    for (unsigned i = 0; i < BYTES; i++)
        expected[i] = ((i * 37u) ^ (i >> 5) ^ 0xa7) ^
            ((i >= START && i < START + LENGTH) ? 0x5a : 0);
    struct iovec writevec[] = {
        {expected + START - 2, 27}, {NULL, 0},
        {expected + START + 25, 3033}, {expected + START + 3058, LENGTH - 3058}
    };
    zfs_uio_t u;
    zfs_uio_iovec_init(&u, writevec, 4, START, UIO_SYSSPACE, LENGTH, 2);
    dmu_tx_t *tx = dmu_tx_create(os);
    dmu_tx_hold_write(tx, object, START, LENGTH);
    int error = dmu_tx_assign(tx, DMU_TX_WAIT);
    if (error) dmu_tx_abort(tx);
    else {
        error = dmu_write_uio(os, object, &u, LENGTH, tx, 0);
        dmu_tx_commit(tx);
    }
    if (!error && (u.uio_resid || u.uio_loffset != START + LENGTH)) error = EIO;
    memset(actual, 0, BYTES);
    struct iovec readvec[] = {{actual, 11}, {NULL, 0},
        {actual + 11, 4090}, {actual + 4101, BYTES - 4101}};
    zfs_uio_iovec_init(&u, readvec, 4, 0, UIO_SYSSPACE, BYTES, 0);
    if (!error) error = dmu_read_uio(os, object, &u, BYTES, 0);
    if (!error && (u.uio_resid || u.uio_loffset != BYTES ||
        memcmp(expected, actual, BYTES))) error = EIO;
    free(expected); free(actual);
    return (error);
}
