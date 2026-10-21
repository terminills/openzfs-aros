/* Private fresh-import verification of the fault-recovered transaction. */
#include <sys/zfs_context.h>
#include <sys/spa.h>
#include <sys/dmu.h>
#include <sys/dmu_objset.h>
#include <proto/exec.h>
#include <stdio.h>
#include "zpl-fixture.h"

int __real_spa_import(char *, nvlist_t *, nvlist_t *, uint64_t);
int __wrap_spa_import(char *pool, nvlist_t *config, nvlist_t *props, uint64_t flags)
{
    int error = __real_spa_import(pool, config, props, flags);
    if (error || strcmp(pool, "qemu_mirror")) return error;
    objset_t *os = NULL;
    uint64_t object = 0, value = 0;
    error = dmu_objset_own("qemu_mirror/flushprobe", DMU_OST_OTHER,
        B_TRUE, B_FALSE, FTAG, &os);
    if (!error) error = dmu_object_next(os, &object, B_FALSE, 0);
    if (!error) error = dmu_read(os, object, 0, sizeof(value), &value, 0);
    if (!error && value != UINT64_C(0x51a20519fedcba98)) error = EIO;
    if (os) dmu_objset_disown(os, B_FALSE, FTAG);
    char line[160];
    snprintf(line, sizeof(line), "DIAGNOSTIC cold TXG flush payload=%d object=%llu error=%d\n",
        value == UINT64_C(0x51a20519fedcba98), (unsigned long long)object, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
#ifdef OZ_TEST_ZIL_FLUSH
    struct oz_zpl *fs = NULL;
    uint64_t size = 0;
    value = object = 0;
    if (!error) error = oz_zpl_open("qemu_mirror/zilflush", &fs);
    if (!error) error = oz_zpl_lookup(fs, "probe", &object, &size);
    if (!error && size != sizeof(value)) error = EIO;
    if (!error) error = oz_zpl_read(fs, object, 0, &value, sizeof(value));
    if (!error && value != UINT64_C(0x51a20519aabbccdd)) error = EIO;
    if (fs) {
        int closed = oz_zpl_close(fs);
        if (!error) error = closed;
    }
    snprintf(line, sizeof(line), "DIAGNOSTIC cold ZPL recovery payload=%d size=%llu error=%d\n",
        value == UINT64_C(0x51a20519aabbccdd), (unsigned long long)size, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
#endif
#ifdef OZ_TEST_LOG_FLUSH
    struct oz_zpl *logfs = NULL;
    uint64_t logsize = 0;
    value = object = 0;
    if (!error) error = oz_zpl_open("qemu_mirror/slogflush", &logfs);
    if (!error) error = oz_zpl_lookup(logfs, "probe", &object, &logsize);
    if (!error && logsize != sizeof(value)) error = EIO;
    if (!error) error = oz_zpl_read(logfs, object, 0, &value, sizeof(value));
    if (!error && value != UINT64_C(0x51a20519ccddeeff)) error = EIO;
    if (logfs) {
        int closed = oz_zpl_close(logfs);
        if (!error) error = closed;
    }
    snprintf(line, sizeof(line), "DIAGNOSTIC cold SLOG fallback payload=%d size=%llu error=%d\n",
        value == UINT64_C(0x51a20519ccddeeff), (unsigned long long)logsize, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
#endif
#ifdef OZ_TEST_ACTIVE_SPARE
    os = NULL;
    value = object = 0;
    if (!error) error = dmu_objset_own("qemu_mirror/activeflush", DMU_OST_OTHER,
        B_TRUE, B_FALSE, FTAG, &os);
    if (!error) error = dmu_object_next(os, &object, B_FALSE, 0);
    if (!error) error = dmu_read(os, object, 0, sizeof(value), &value, 0);
    if (!error && value != UINT64_C(0x51a20519fedcba98)) error = EIO;
    if (os) dmu_objset_disown(os, B_FALSE, FTAG);
    snprintf(line, sizeof(line), "DIAGNOSTIC cold active spare payload=%d object=%llu error=%d\n",
        value == UINT64_C(0x51a20519fedcba98), (unsigned long long)object, error);
    for (char *p = line; *p; p++) RawPutChar(*p);
#endif
    return error;
}
