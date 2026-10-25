/* SPDX-License-Identifier: CDDL-1.0 */
/* Exercise a real runtime-check command and report capture in a diskless guest.
 * A correctly delivered prerequisite failure is NOT runtime qualification. */
#include "project-worker.h"
#include <aros/debug.h>
#include <proto/dos.h>
#include <errno.h>
#include <string.h>

int main(void)
{
    bug("RESULT runtime capture BEGIN runs=4\n");
    for (unsigned i = 0; i < 4; ++i) {
        struct oz_project_worker *worker = NULL;
        struct oz_message response;
        int error = oz_runtime_worker_start(&worker);
        if (error) {
            bug("RESULT runtime capture FAIL start=%d run=%u\n", error, i);
            return 20;
        }
        for (unsigned ticks = 0; ticks < 2500; ++ticks) {
            error = oz_project_worker_poll(worker, &response);
            if (error != EAGAIN) break;
            Delay(1);
        }
        if (error) {
            bug("RESULT runtime capture FAIL poll=%d run=%u (host must stop guest)\n", error, i);
            for (;;) Delay(50);
        }
        int success = strstr(response.detail, "exit=0, DOS=0, capture errno=0") != NULL &&
            strstr(response.detail, "PASS checked runtime prerequisites only") != NULL;
        int prerequisite_failure = strstr(response.detail, "exit=20, DOS=0, capture errno=0") != NULL &&
            strstr(response.detail, "FAIL runtime prerequisites; review diagnostics above") != NULL;
        int complete = strstr(response.detail, "OpenZFS runtime check (no pools or devices opened)") != NULL &&
            strstr(response.detail, "Not tested: pipe disconnects, storage I/O, durability, GUI or pool operations.") != NULL;
        int status = (success && response.status == OZ_OK) ||
            (prerequisite_failure && response.status == OZ_ENGINE_ERROR);
        int cleanup = oz_project_worker_free(worker);
        bug("RESULT runtime capture run=%u complete=%d status=%d cleanup=%d prerequisite-pass=%d\n",
            i, complete, status, cleanup, success);
        bug("RESULT runtime capture report run=%u\n%s\n", i, response.detail);
        if (!complete || !status || cleanup) return 20;
    }
    bug("RESULT runtime capture PASS runs=4 delivery-only=1\n");
    return 0;
}
