/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_HOSTID_RECORD_H
#define OZ_HOSTID_RECORD_H
#include <dos/dos.h>
#include <stdint.h>
/* Caller owns handle and serializes calls. Reader preserves output on error.
 * Writer requires a newly created empty file; identity zero is invalid. */
int oz_hostid_record_read(BPTR file,uint32_t *identity);
int oz_hostid_record_write(BPTR file,uint32_t identity);
#endif
