/* SPDX-License-Identifier: CDDL-1.0 */
#include "session-cleanup.h"
#include <aros/debug.h>
static unsigned callbacks;
static void increment(void *arg) { ++*(unsigned *)arg; }
struct worker {
    pthread_mutex_t gate;
    struct oz_session_cleanup *session;
    int error;
};
static void *finish_operation(void *arg)
{
    struct worker *worker = arg;
    pthread_mutex_lock(&worker->gate);
    worker->error = oz_session_cleanup_add(worker->session, increment, &callbacks);
    oz_session_cleanup_release(worker->session);
    pthread_mutex_unlock(&worker->gate);
    return NULL;
}
#define CHECK(expr) do { if (!(expr)) { \
    bug("RESULT native CLI session cleanup FAIL line=%d\n", __LINE__); \
    return 20; } } while (0)
int main(void)
{
    for (unsigned i = 0; i < 64; ++i) {
        struct oz_session_cleanup *session;
        CHECK(oz_session_cleanup_create(&session) == 0);
        CHECK(oz_session_cleanup_add(session, NULL, NULL) == EINVAL);
        CHECK(oz_session_cleanup_hold(session) == 0);
        CHECK(oz_session_cleanup_hold(session) == 0);
        CHECK(oz_session_cleanup_add(session, increment, &callbacks) == 0);
        CHECK(oz_session_cleanup_add(session, increment, &callbacks) == 0);
        oz_session_cleanup_close(session);
        CHECK(callbacks == i * 2);
        CHECK(oz_session_cleanup_hold(session) == EBADF);
        CHECK(oz_session_cleanup_add(session, increment, &callbacks) == EBADF);
        oz_session_cleanup_release(session);
        CHECK(callbacks == i * 2);
        oz_session_cleanup_release(session);
        CHECK(callbacks == (i + 1) * 2);
    }
    struct oz_session_cleanup *empty;
    CHECK(oz_session_cleanup_create(&empty) == 0);
    oz_session_cleanup_close(empty);
    for (unsigned i = 0; i < 16; ++i) {
        struct worker worker = {0};
        pthread_t thread;
        CHECK(pthread_mutex_init(&worker.gate, NULL) == 0);
        CHECK(pthread_mutex_lock(&worker.gate) == 0);
        CHECK(oz_session_cleanup_create(&worker.session) == 0);
        CHECK(oz_session_cleanup_hold(worker.session) == 0);
        CHECK(oz_session_cleanup_add(worker.session, increment, &callbacks) == 0);
        CHECK(pthread_create(&thread, NULL, finish_operation, &worker) == 0);
        oz_session_cleanup_close(worker.session);
        CHECK(callbacks == 128 + i);
        CHECK(pthread_mutex_unlock(&worker.gate) == 0);
        CHECK(pthread_join(thread, NULL) == 0);
        CHECK(worker.error == EBADF);
        CHECK(callbacks == 129 + i);
        CHECK(pthread_mutex_destroy(&worker.gate) == 0);
    }
    bug("RESULT native CLI session cleanup PASS runs=64 threads=16 callbacks=%u\n", callbacks);
    return 0;
}
