/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_GPT_API_H
#define OZ_GPT_API_H
#include <stdint.h>
#include <exec/types.h>
#define OZ_GPT_ACTION 0x4f5a4750L
#define OZ_GPT_VERSION 1
#define OZ_GPT_LABEL 1
#define OZ_GPT_EXPAND 2
#define OZ_GPT_CONFIRMED 1
#define OZ_GPT_REPLACE_TABLE 2
/* Root-only, writable-service operation on an explicitly provisioned whole
 * disk. LABEL requires both flags: replacing a partition table can destroy
 * access to existing filesystems. No implicit force, DOS-node removal or
 * retry. The caller must exclude external raw-device users; geometry is a
 * stale-selection check, not a persistent hardware identity or ownership
 * proof. A failure can follow writes; inspect before deciding to retry. */
struct oz_gpt_query {
    uint32_t version,operation,flags,sector_size;
    uint64_t bytes,start;
    int32_t unit;
    uint32_t changed,reserved;
    char device[256],name[36];
};
/* Native errno result. Success copies changed back; failure leaves it alone.
 * LABEL success means a verified write; EXPAND may succeed with changed=0. */
int oz_gpt_request(const char *,struct oz_gpt_query *);
#endif
