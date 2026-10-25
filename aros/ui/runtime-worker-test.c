/* SPDX-License-Identifier: CDDL-1.0 */
/* No-service, missing-checker fixture. Must run in an isolated RAM-backed guest. */
#include "project-worker.h"
#include <aros/debug.h>
#include <proto/dos.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    BPTR checker = Lock((CONST_STRPTR)"C:OpenZFSRuntimeCheck", SHARED_LOCK);
    if (checker) {
        UnLock(checker);
        bug("RESULT runtime worker FAIL checker unexpectedly installed\n");
        return 20;
    }
    if (IoErr() != ERROR_OBJECT_NOT_FOUND) {
        bug("RESULT runtime worker FAIL checker lookup error=%ld\n", (long)IoErr());
        return 20;
    }
    bug("RESULT runtime worker BEGIN missing-checker runs=16\n");
    for (unsigned i = 0; i < 16; ++i) {
        struct oz_project_worker *worker = NULL;
        struct oz_message response;
        int error = oz_runtime_worker_start(&worker);
        if (error) {
            bug("RESULT runtime worker FAIL start=%d run=%u\n", error, i);
            return 20;
        }
        /* This tests the actual worker contract: destruction before join is
         * refused even if the worker has already completed. */
        if (oz_project_worker_free(worker) != EBUSY) {
            bug("RESULT runtime worker FAIL unjoined destruction run=%u\n", i);
            for (;;) Delay(50);
        }
        for (unsigned ticks = 0; ticks < 250; ++ticks) {
            error = oz_project_worker_poll(worker, &response);
            if (error != EAGAIN) break;
            Delay(1);
        }
        if (error) {
            /* Do not return through CRT teardown while a worker may be live.
             * The host must capture this failure and stop this isolated VM. */
            bug("RESULT runtime worker FAIL poll=%d run=%u (host must stop guest)\n", error, i);
            for (;;) Delay(50);
        }
        int status = response.status == OZ_ENGINE_ERROR;
        int detail = strstr(response.detail, "C:OpenZFSRuntimeCheck was not found.") != NULL &&
            strstr(response.detail, "The checker did not run.") != NULL;
        int cleanup = oz_project_worker_free(worker);
        bug("RESULT runtime worker run=%u status=%d detail=%d cleanup=%d\n",
            i, status, detail, cleanup);
        if (!status || !detail || cleanup) return 20;
    }
    bug("RESULT runtime worker PASS missing-checker runs=16 joined=16\n");
    return 0;
}
