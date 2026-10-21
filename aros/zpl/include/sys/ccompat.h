/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_AROS_CCOMPAT_H
#define OZ_AROS_CCOMPAT_H
#include <stdint.h>
#include <stddef.h>
/* Reuse upstream's BSD-licensed portable hlist/counter representation.
 * Only its FreeBSD machine-atomic spelling needs a native compiler binding.
 * hlist mutations still require the caller's zvol_state_lock. */
#define READ_ONCE(value) __atomic_load_n(&(value), __ATOMIC_SEQ_CST)
#define WRITE_ONCE(value, next) __atomic_store_n(&(value), (next), __ATOMIC_SEQ_CST)
#define atomic_fetchadd_int(address, delta) \
    __atomic_fetch_add((address), (delta), __ATOMIC_SEQ_CST)
#include "../../../../include/os/freebsd/spl/sys/ccompat.h"
#undef atomic_fetchadd_int
#endif
