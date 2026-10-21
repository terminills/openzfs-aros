/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_NATIVE_LABEL_H
#define OZ_NATIVE_LABEL_H
#include <libzutil.h>
#include <libzfs.h>
/* Read-only native client: errno value on error; no write/import admission. */
int oz_native_read_label(const char *,nvlist_t **,int *);
struct oz_ticket;
/* Optional parent-owned session stays live until every reader has joined. */
int oz_native_read_label_session(const char *,const struct oz_ticket *,nvlist_t **,int *);
/* Read-only policy query. Returns an errno value, never write admission. */
int oz_native_pool_in_use(libzfs_handle_t *,const char *,pool_state_t *,char **,boolean_t *);
#endif
