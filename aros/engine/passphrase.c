/* SPDX-License-Identifier: CDDL-1.0 */
/* Keep the crypto library's actual ABI isolated from upstream kernel headers. */
#include "passphrase.h"
#include <openssl/evp.h>
#include <errno.h>

int oz_passphrase_derive(const char *password, size_t length, uint64_t salt,
    uint64_t iterations, uint8_t output[32])
{
    if (!output) return EINVAL;
    volatile uint8_t *wipe = output;
    for (unsigned i = 0; i < 32; i++) wipe[i] = 0;
    if (!password || !iterations || iterations > UINT32_MAX) return EINVAL;
    /* OpenZFS derives from the little-endian representation of its salt. */
    uint8_t bytes[8];
    for (unsigned i = 0; i < 8; i++) bytes[i] = (uint8_t)(salt >> (8 * i));
    if (PKCS5_PBKDF2_HMAC_SHA1(password, length, bytes, sizeof(bytes),
        (uint32_t)iterations, 32, output) == 1) return 0;
    for (unsigned i = 0; i < 32; i++) wipe[i] = 0;
    return EIO;
}
