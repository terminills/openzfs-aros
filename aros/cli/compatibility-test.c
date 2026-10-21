/* SPDX-License-Identifier: CDDL-1.0 */
#include <libzfs.h>
#include <zfeature_common.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <aros/debug.h>
#include <proto/dos.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI compatibility FAIL line=%d\n", __LINE__); return 20; } } while (0)
static int put(const char *name, const char *data, size_t size)
{
    int fd = open(name, O_CREAT | O_EXCL | O_WRONLY, 0600);
    if (fd < 0) return -1;
    ssize_t n = write(fd, data, size);
    int closed = close(fd);
    return n == (ssize_t)size && closed == 0 ? 0 : -1;
}
int main(void)
{
    boolean_t features[SPA_FEATURES];
    char report[1024];
    /* Real upstream feature table, initialized with the engine's feature set.
     * This fixture does not claim to query a running service's capabilities. */
    zpool_feature_init();
    CHECK(zpool_load_compat("off", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_OK);
    for (unsigned i = 0; i < SPA_FEATURES; ++i) CHECK(features[i]);
    CHECK(zpool_load_compat("legacy", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_OK);
    for (unsigned i = 0; i < SPA_FEATURES; ++i) CHECK(!features[i]);
    CHECK(mkdir("RAM:cli-compatibility", 0700) == 0);
    const char first[] = "# first profile\nasync_destroy, lz4_compress # comment\n";
    const char second[] = "lz4_compress\n";
    CHECK(put("RAM:cli-compatibility/first", first, sizeof(first)-1) == 0);
    CHECK(put("RAM:cli-compatibility/second", second, sizeof(second)-1) == 0);
    for (int run = 0; run < 16; ++run) {
        CHECK(zpool_load_compat("RAM:cli-compatibility/first", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_OK);
        for (unsigned i = 0; i < SPA_FEATURES; ++i)
            CHECK(!!features[i] == (i == SPA_FEATURE_ASYNC_DESTROY || i == SPA_FEATURE_LZ4_COMPRESS));
        CHECK(zpool_load_compat("RAM:cli-compatibility/first,RAM:cli-compatibility/second", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_OK);
        for (unsigned i = 0; i < SPA_FEATURES; ++i)
            CHECK(!!features[i] == (i == SPA_FEATURE_LZ4_COMPRESS));
    }
    char original[sizeof(first)] = {0};
    int fd = open("RAM:cli-compatibility/first", O_RDONLY);
    CHECK(fd >= 0 && read(fd, original, sizeof(first)-1) == sizeof(first)-1);
    CHECK(close(fd) == 0 && strcmp(original, first) == 0);
    CHECK(put("RAM:cli-compatibility/no-newline", "lz4_compress", 12) == 0);
    CHECK(put("RAM:cli-compatibility/empty", "", 0) == 0);
    CHECK(put("RAM:cli-compatibility/unknown", "unknown_fixture_feature\n", 24) == 0);
    char large[ZPOOL_COMPAT_MAXSIZE + 1];
    memset(large, ' ', sizeof(large)); large[sizeof(large)-1] = '\n';
    CHECK(put("RAM:cli-compatibility/oversize", large, sizeof(large)) == 0);
    CHECK(zpool_load_compat("RAM:cli-compatibility/no-newline", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_BADFILE);
    CHECK(zpool_load_compat("RAM:cli-compatibility/empty", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_BADFILE);
    CHECK(zpool_load_compat("RAM:cli-compatibility/absent", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_BADFILE);
    CHECK(zpool_load_compat("RAM:cli-compatibility/oversize", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_BADFILE);
    CHECK(zpool_load_compat("RAM:cli-compatibility/unknown", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_BADTOKEN);
    CHECK(zpool_load_compat(",,,", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_NOFILES);
    const char *names[] = {"first", "second", "no-newline", "empty", "unknown", "oversize"};
    for (unsigned i = 0; i < sizeof(names)/sizeof(*names); ++i) {
        char path[128]; snprintf(path, sizeof(path), "RAM:cli-compatibility/%s", names[i]);
        CHECK(unlink(path) == 0);
    }
    CHECK(rmdir("RAM:cli-compatibility") == 0);
    /* Private guest ENV assignment: no ENVARC writes. Distribution fixtures
     * are packaged read-only in this test ISO under PKGDATADIR. */
    BPTR previous_env = Lock((CONST_STRPTR)"ENV:", SHARED_LOCK);
    BPTR private_env = CreateDir((CONST_STRPTR)"RAM:cli-compat-env");
    CHECK(private_env && AssignLock((CONST_STRPTR)"ENV", private_env));
    CHECK(mkdir(SYSCONFDIR, 0700) == 0);
    CHECK(mkdir(SYSCONFDIR "/zfs", 0700) == 0);
    CHECK(mkdir(ZPOOL_SYSCONF_COMPAT_D, 0700) == 0);
    CHECK(put(ZPOOL_SYSCONF_COMPAT_D "/precedence", second, sizeof(second)-1) == 0);
    zpool_compat_status_t local_status = zpool_load_compat("precedence", features, report, sizeof(report));
    bug("CLI compatibility local directory=%s status=%d report=%s\n", ZPOOL_SYSCONF_COMPAT_D, local_status, report);
    CHECK(local_status == ZPOOL_COMPATIBILITY_OK);
    CHECK(features[SPA_FEATURE_LZ4_COMPRESS] && !features[SPA_FEATURE_ASYNC_DESTROY]);
    CHECK(unlink(ZPOOL_SYSCONF_COMPAT_D "/precedence") == 0);
    CHECK(zpool_load_compat("precedence", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_OK);
    CHECK(!features[SPA_FEATURE_LZ4_COMPRESS] && features[SPA_FEATURE_ASYNC_DESTROY]);
    CHECK(zpool_load_compat("distribution", features, report, sizeof(report)) == ZPOOL_COMPATIBILITY_WARNTOKEN);
    CHECK(rmdir(ZPOOL_SYSCONF_COMPAT_D) == 0);
    CHECK(rmdir(SYSCONFDIR "/zfs") == 0);
    CHECK(rmdir(SYSCONFDIR) == 0);
    CHECK(AssignLock((CONST_STRPTR)"ENV", previous_env));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-compat-env"));
    bug("RESULT native CLI compatibility PASS runs=16\n");
    return 0;
}
