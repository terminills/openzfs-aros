/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_PROGRESS_WAIT_H
#define OZ_CLI_PROGRESS_WAIT_H
#include <exec/ports.h>
#include <devices/timer.h>
struct oz_progress_wait {
    struct MsgPort *port;
    struct timerequest *request;
    int opened, pending;
};
int oz_progress_wait_init(struct oz_progress_wait *);
/* Wait one second if periodic, otherwise until native Ctrl-E requests a report.
 * Ctrl-C wakes deferred pthread cancellation; a non-cancellation break returns
 * EINTR. Caller must install oz_progress_wait_destroy as a cleanup handler. */
int oz_progress_wait_next(struct oz_progress_wait *, int periodic, int *timer);
void oz_progress_wait_destroy(void *);
#endif
