/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PASSPHRASE_H
#define OZ_PASSPHRASE_H
#include <stddef.h>
#include <stdint.h>
/* Caller owns/wipes password and output. Invalid inputs clear output. */
int oz_passphrase_derive(const char *, size_t, uint64_t, uint64_t,
    uint8_t output[32]);
#endif
