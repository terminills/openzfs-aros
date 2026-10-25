/* SPDX-License-Identifier: CDDL-1.0 */
/* Published SDK/runtime capture coverage without the private explicit-env
 * process policy. NULL env inherits normally; this does not qualify env[]. */
#include "process.h"
#include <proto/dos.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>
#ifndef CAPTURE_THREADS
#define CAPTURE_THREADS 1
#endif
#if CAPTURE_THREADS > 1
#include <pthread.h>
#endif
#define CHECK(x) do { if (!(x)) { bug("RESULT inherited capture FAIL line=%d\n", __LINE__); return 20; } } while (0)

static int run_cases(unsigned worker)
{
    for (unsigned cycle = 0; cycle < 16; ++cycle) {
        BPTR errors = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
        CHECK(errors);
        const char *modes[] = {"empty", "lines", "large", "nul", "large", "absent", "token"};
        for (unsigned mode = 0; mode < 7; ++mode) {
            char token[80];
            snprintf(token, sizeof(token), "capture-worker-%u-cycle-%u", worker, cycle);
            char *args[] = {"output-child", (char *)modes[mode], mode == 6 ? token : NULL, NULL};
            struct oz_cli_result result;
            char **lines = NULL;
            int count = 0;
            int error = oz_cli_capture_env_exact(mode == 5 ?
                "C:OpenZFSAbsentCaptureChild" : "C:OpenZFSOutputChild",
                args, NULL, errors, mode == 4 ? 1024 : 200000,
                &lines, &count, &result);
            bug("RESULT inherited capture worker=%u run=%u mode=%u error=%d status=%ld lines=%d\n",
                worker, cycle, mode, error, result.status, count);
            if (mode == 5) {
                CHECK(error == ENOEXEC && result.status == -1 && !lines && !count);
            } else {
                CHECK(result.status == 23);
                if (mode == 3 || mode == 4) {
                    CHECK(error == (mode == 3 ? EILSEQ : EOVERFLOW) && !lines && !count);
                } else {
                    CHECK(!error);
                    if (mode == 0) CHECK(!count && !lines);
                    if (mode == 1) CHECK(count == 3 && !strcmp(lines[0], "alpha") &&
                        !strcmp(lines[1], "") && !strcmp(lines[2], "omega"));
                    if (mode == 2) {
                        CHECK(count == 2 && strlen(lines[0]) == 131072 && !strcmp(lines[1], "tail"));
                        for (unsigned i = 0; i < 131072; ++i) CHECK(lines[0][i] == 'x');
                    }
                    if (mode == 6) CHECK(count == 1 && !strcmp(lines[0], token));
                }
            }
            oz_cli_free_lines(lines, count);
        }
        CHECK(Close(errors));
    }
    return 0;
}

#if CAPTURE_THREADS > 1
struct worker_result { unsigned index; int status; };
static void *capture_worker(void *arg)
{
    struct worker_result *result = arg;
    result->status = run_cases(result->index);
    return NULL;
}
#endif

int main(void)
{
    bug("RESULT inherited capture BEGIN runs=16 workers=%u\n", CAPTURE_THREADS);
    BPTR old = Lock((CONST_STRPTR)"T:", SHARED_LOCK);
    BPTR temp = CreateDir((CONST_STRPTR)"RAM:inherited-capture-temp");
    CHECK(old && temp && AssignLock((CONST_STRPTR)"T", temp));
#if CAPTURE_THREADS > 1
    pthread_t threads[CAPTURE_THREADS];
    struct worker_result results[CAPTURE_THREADS];
    unsigned started = 0;
    int failed = 0;
    for (; started < CAPTURE_THREADS; ++started) {
        results[started].index = started;
        results[started].status = 20;
        if (pthread_create(&threads[started], NULL, capture_worker, &results[started])) {
            failed = 1;
            break;
        }
    }
    for (unsigned i = 0; i < started; ++i) {
        if (pthread_join(threads[i], NULL)) {
            bug("RESULT inherited capture FAIL join (host must stop guest)\n");
            for (;;) Delay(50);
        }
        if (results[i].status) failed = 1;
    }
    CHECK(!failed);
#else
    CHECK(!run_cases(0));
#endif
    CHECK(AssignLock((CONST_STRPTR)"T", old));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:inherited-capture-temp"));
    bug("RESULT inherited capture PASS runs=16 workers=%u cases=%u cleanup=1\n",
        CAPTURE_THREADS, CAPTURE_THREADS * 112);
    return 0;
}
