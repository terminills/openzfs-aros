/* SPDX-License-Identifier: CDDL-1.0 */
/* The SDK supplies POSIX minima; the selected pthread implementation supplies
 * its actual limits. Include them once in that order without redefinition. */
#ifndef OZ_CRYPTO_PTHREAD_LIMITS_H
#define OZ_CRYPTO_PTHREAD_LIMITS_H
#include <limits.h>
#undef PTHREAD_KEYS_MAX
#undef PTHREAD_STACK_MIN
#undef PTHREAD_THREADS_MAX
#undef PTHREAD_DESTRUCTOR_ITERATIONS
#include <pthread.h>
#endif
