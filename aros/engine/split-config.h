/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SPLIT_CONFIG_H
#define OZ_SPLIT_CONFIG_H
#include <libnvpair.h>
/* One explicit decimal leaf GUID per concrete mirror, in any order. No
 * implicit last-member selection. Input tree is never modified. */
int oz_split_guids(const char *, uint64_t *, unsigned *);
int oz_split_config(nvlist_t *, const uint64_t *, unsigned, nvlist_t **);
#endif
