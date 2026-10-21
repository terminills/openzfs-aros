/* Compile/link probe only; it does not exercise synchronization or durability. */
#include <pthread.h>
#include <unistd.h>
#include <sys/types.h>
#include <time.h>

_Static_assert(sizeof(off_t) >= 8, "OpenZFS needs 64-bit file offsets");

int main(void)
{
    pthread_mutex_t mutex;
    pthread_rwlock_t rwlock;
    pthread_cond_t cond;
    pthread_attr_t attr;
    pthread_key_t key;
    struct timespec now;
    char byte;
    int result = pthread_mutex_init(&mutex, 0);
    result |= pthread_rwlock_init(&rwlock, 0);
    result |= pthread_cond_init(&cond, 0);
    result |= pthread_attr_init(&attr);
#ifndef AROS_PROBE_WITHOUT_GUARD
    result |= pthread_attr_setguardsize(&attr, 4096);
#endif
    result |= pthread_key_create(&key, 0);
    result |= clock_gettime(CLOCK_MONOTONIC, &now);
    result |= (int)pread(-1, &byte, 1, (off_t)1 << 33);
    result |= (int)pwrite(-1, &byte, 1, (off_t)1 << 33);
    result |= fsync(-1);
    return result;
}
