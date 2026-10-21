/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_HOSTID_SETTINGS_H
#define OZ_HOSTID_SETTINGS_H
#include <stdint.h>
#define OZ_HOSTID_SAVED 1U
#define OZ_HOSTID_CURRENT 2U
/* SYSTEM-only, while the native service is absent. Saves ENVARC first,
 * then ENV. On any failure published describes exact partial completion;
 * callers must not report an all-or-nothing transaction across volumes. */
int oz_hostid_settings_apply(uint32_t identity,unsigned *published);
#endif
