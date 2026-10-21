/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CACHE_POLICY_H
#define OZ_CACHE_POLICY_H
void oz_cache_policy_start(void);
void oz_cache_policy_stop(void);
int oz_cache_default_check(void);
const char *oz_cache_default_path(void);
#endif
