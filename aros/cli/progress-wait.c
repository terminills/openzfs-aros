/* SPDX-License-Identifier: CDDL-1.0 */
#include "progress-wait.h"
#include <proto/exec.h>
#include <dos/dos.h>
#include <pthread.h>
#include <errno.h>
#include <string.h>

void oz_progress_wait_destroy(void *arg)
{
    struct oz_progress_wait *wait = arg;
    if (wait->pending) {
        if (!CheckIO((struct IORequest *)wait->request))
            AbortIO((struct IORequest *)wait->request);
        WaitIO((struct IORequest *)wait->request);
        wait->pending = 0;
    }
    if (wait->opened) CloseDevice((struct IORequest *)wait->request);
    if (wait->request) DeleteIORequest((struct IORequest *)wait->request);
    if (wait->port) DeleteMsgPort(wait->port);
    memset(wait, 0, sizeof(*wait));
}

int oz_progress_wait_init(struct oz_progress_wait *wait)
{
    memset(wait, 0, sizeof(*wait));
    wait->port = CreateMsgPort();
    if (!wait->port) return ENOMEM;
    wait->request = (struct timerequest *)CreateIORequest(wait->port, sizeof(*wait->request));
    if (!wait->request) { oz_progress_wait_destroy(wait); return ENOMEM; }
    if (OpenDevice((CONST_STRPTR)TIMERNAME, UNIT_MICROHZ,
        (struct IORequest *)wait->request, 0)) {
        oz_progress_wait_destroy(wait);
        return ENODEV;
    }
    wait->opened = 1;
    return 0;
}

int oz_progress_wait_next(struct oz_progress_wait *wait, int periodic, int *timer)
{
    if (!wait || !wait->opened || !timer || wait->pending) return EINVAL;
    *timer = 0;
    pthread_testcancel();
    ULONG signals = SIGBREAKF_CTRL_C | SIGBREAKF_CTRL_E;
    if (periodic) {
        wait->request->tr_node.io_Command = TR_ADDREQUEST;
        wait->request->tr_node.io_Flags = 0;
        wait->request->tr_time.tv_secs = 1;
        wait->request->tr_time.tv_micro = 0;
        wait->pending = 1;
        signals |= 1UL << wait->port->mp_SigBit;
        SendIO((struct IORequest *)wait->request);
    }
    ULONG received = Wait(signals);
    pthread_testcancel();
    int error = 0;
    if (wait->pending) {
        int complete = CheckIO((struct IORequest *)wait->request) != NULL;
        if (!complete) AbortIO((struct IORequest *)wait->request);
        WaitIO((struct IORequest *)wait->request);
        wait->pending = 0;
        if (complete && wait->request->tr_node.io_Error) error = EIO;
        *timer = complete && !(received & SIGBREAKF_CTRL_E);
    }
    if (received & SIGBREAKF_CTRL_C) return EINTR;
    return error;
}
