/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_EFI_UUID_H
#define OZ_EFI_UUID_H
/* RFC UUID network-order bytes, as consumed by upstream UUID_LE_CONVERT.
 * This boundary deliberately hides AROS's struct uuid_t from libefi's
 * byte-array uuid_t. Returns errno; leaves output unchanged on failure. */
int oz_efi_uuid_generate(unsigned char [16]);
#endif
