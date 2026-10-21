/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_HOSTID_POLICY_H
#define OZ_HOSTID_POLICY_H
#include <stdint.h>
/* Service startup only. Reads ENV, never generates or changes an identity. */
int oz_hostid_policy_start(void);
void oz_hostid_set_native(uint32_t);
#endif
