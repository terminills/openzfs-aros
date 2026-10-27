/* SPDX-License-Identifier: CDDL-1.0 */
/* Exercise the actual runtime API, not an OpenZFS seek/write replacement. */
#include <unistd.h>
#include <fcntl.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#ifdef __AROS__
#include <aros/debug.h>
#define REPORT(...) bug(__VA_ARGS__)
#else
#define REPORT(...) printf(__VA_ARGS__)
#endif

enum { WORKERS = 4, ITERATIONS = 1000, BLOCK = 64, PREFIX = 1024 };
_Static_assert(sizeof(off_t) == 8, "64-bit offsets required");
static pthread_mutex_t gate;
static pthread_cond_t ready;
static int started, arrived;
struct worker { int fd, id, failed, error; };

static void *run_worker(void *arg)
{
    struct worker *w = arg;
    unsigned char expected[BLOCK], actual[BLOCK];
    off_t offset = 4096 + w->id * BLOCK;
    pthread_mutex_lock(&gate);
    ++arrived;
    pthread_cond_broadcast(&ready);
    while (!started) pthread_cond_wait(&ready, &gate);
    pthread_mutex_unlock(&gate);
    for (int n = 0; n < ITERATIONS; ++n) {
        memset(expected, 33 + w->id, sizeof(expected));
        memcpy(expected, &n, sizeof(n));
        if (pwrite(w->fd, expected, sizeof(expected), offset) != BLOCK ||
            pread(w->fd, actual, sizeof(actual), offset) != BLOCK ||
            memcmp(expected, actual, sizeof(actual))) {
            w->failed = 1; w->error = errno; break;
        }
    }
    return NULL;
}

int main(void)
{
    if (pthread_mutex_init(&gate, NULL)) return 20;
    if (pthread_cond_init(&ready, NULL)) { pthread_mutex_destroy(&gate); return 20; }
#ifdef __AROS__
    const char *path = "RAM:openzfs-positioned-concurrent";
#else
    const char *path = "openzfs-positioned-concurrent.tmp";
#endif
    int fd = open(path, O_CREAT | O_EXCL | O_RDWR, 0600), failed = 0, count = 0;
    if (fd < 0) return 20;
    unsigned char initial[8192];
    memset(initial, 0, sizeof(initial));
    for (int i = 0; i < PREFIX; ++i) initial[i] = (unsigned char)(i % 251);
    if (write(fd, initial, sizeof(initial)) != sizeof(initial) || lseek(fd, 0, SEEK_SET) != 0) {
        close(fd); unlink(path); return 20;
    }
    struct worker workers[WORKERS];
    pthread_t threads[WORKERS];
    memset(workers, 0, sizeof(workers));
    for (int i = 0; i < WORKERS; ++i) {
        workers[i].id = i;
        workers[i].fd = dup(fd);
        if (workers[i].fd < 0) { failed = 1; break; }
        int error = pthread_create(&threads[i], NULL, run_worker, &workers[i]);
        if (error) { close(workers[i].fd); failed = 1; break; }
        ++count;
    }
    pthread_mutex_lock(&gate);
    while (arrived < count) pthread_cond_wait(&ready, &gate);
    started = 1;
    pthread_cond_broadcast(&ready);
    pthread_mutex_unlock(&gate);
    /* Ordinary reads must advance the shared open-file offset, without
     * accidentally visiting the positional writers' disjoint slots. */
    for (int n = 0; n < ITERATIONS && !failed; ++n) {
        if (lseek(fd, 0, SEEK_SET) != 0) { failed = 1; break; }
        unsigned char actual[PREFIX];
        if (read(fd, actual, sizeof(actual)) != PREFIX ||
            memcmp(initial, actual, sizeof(actual)) ||
            lseek(fd, 0, SEEK_CUR) != PREFIX) failed = 1;
    }
    for (int i = 0; i < count; ++i) {
        if (pthread_join(threads[i], NULL)) failed = 1;
        if (workers[i].failed) {
            REPORT("Positioned worker=%d failed errno=%d\n", i, workers[i].error);
            failed = 1;
        }
        if (close(workers[i].fd)) failed = 1;
    }
    if (lseek(fd, 0, SEEK_CUR) != PREFIX) failed = 1;
    if (close(fd)) failed = 1;
    if (unlink(path)) failed = 1;
    if (pthread_cond_destroy(&ready)) failed = 1;
    if (pthread_mutex_destroy(&gate)) failed = 1;
    REPORT("RESULT positioned concurrency %s workers=%d iterations=%d dup=1 ordinary-read=1 offset=1\n",
        failed ? "FAIL" : "PASS", count, ITERATIONS);
    return failed ? 20 : 0;
}
