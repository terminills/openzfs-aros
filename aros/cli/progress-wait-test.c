/* SPDX-License-Identifier: CDDL-1.0 */
#include "progress-wait.h"
#include <pthread.h>
#include <proto/exec.h>
#include <proto/dos.h>
#include <aros/debug.h>
#include <sys/time.h>
#include <errno.h>
#include <stdint.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI progress wait FAIL line=%d\n", __LINE__); return 20; } } while (0)
struct context {
    struct oz_progress_wait wait;
    struct Task *parent;
    ULONG ready;
    int periodic, cleaned, pending_cleanup, init_error;
};
static void cleanup(void *arg)
{
    struct context *ctx = arg;
    ctx->pending_cleanup = ctx->wait.pending;
    oz_progress_wait_destroy(&ctx->wait);
    ++ctx->cleaned;
}
static void *worker(void *arg)
{
    struct context *ctx = arg;
    int error = oz_progress_wait_init(&ctx->wait);
    ctx->init_error = error;
    if (error) { Signal(ctx->parent, ctx->ready); return (void *)(uintptr_t)error; }
    pthread_cleanup_push(cleanup, ctx);
    Signal(ctx->parent, ctx->ready);
    int timer;
    error = oz_progress_wait_next(&ctx->wait, ctx->periodic, &timer);
    pthread_cleanup_pop(1);
    return (void *)(uintptr_t)error;
}
int main(void)
{
    struct oz_progress_wait wait;
    struct timeval before, after;
    int timer = 0;
    CHECK(oz_progress_wait_init(&wait) == 0);
    CHECK(gettimeofday(&before, NULL) == 0);
    CHECK(oz_progress_wait_next(&wait, 1, &timer) == 0 && timer);
    CHECK(gettimeofday(&after, NULL) == 0);
    long long usec = (after.tv_sec-before.tv_sec)*1000000LL + after.tv_usec-before.tv_usec;
    CHECK(usec >= 800000);
    CHECK(!wait.pending);
    SetSignal(SIGBREAKF_CTRL_E, SIGBREAKF_CTRL_E);
    CHECK(oz_progress_wait_next(&wait, 1, &timer) == 0 && !timer);
    CHECK(!wait.pending);
    SetSignal(SIGBREAKF_CTRL_E, SIGBREAKF_CTRL_E);
    CHECK(oz_progress_wait_next(&wait, 0, &timer) == 0 && !timer);
    oz_progress_wait_destroy(&wait);
    CHECK(!wait.port && !wait.request && !wait.opened);
    BYTE bit = AllocSignal(-1);
    CHECK(bit >= 0);
    int active_cleanup = 0;
    for (int i = 0; i < 16; ++i) {
        struct context ctx = {.parent = FindTask(NULL), .ready = 1UL << bit, .periodic = i % 2};
        pthread_t thread;
        void *status = NULL;
        CHECK(pthread_create(&thread, NULL, worker, &ctx) == 0);
        Wait(ctx.ready);
        Delay(2);
        CHECK(pthread_cancel(thread) == 0);
        CHECK(pthread_join(thread, &status) == 0);
        CHECK(!ctx.init_error && status == PTHREAD_CANCELED);
        CHECK(ctx.cleaned == 1 && !ctx.wait.pending && !ctx.wait.opened && !ctx.wait.port && !ctx.wait.request);
        active_cleanup += ctx.pending_cleanup;
    }
    FreeSignal(bit);
    CHECK(active_cleanup > 0);
    bug("CLI progress timer usec=%lld active cancellations=%d\n", usec, active_cleanup);
    bug("RESULT native CLI progress wait PASS runs=16\n");
    return 0;
}
