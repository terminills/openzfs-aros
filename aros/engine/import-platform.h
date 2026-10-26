/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_IMPORT_PLATFORM_H
#define OZ_IMPORT_PLATFORM_H
#include <libzutil.h>
#include "../cli/create-intent.h"
#define OZ_IMPORT_ADMISSION "org.aros:import-admission"
#define OZ_IMPORT_PATHS "paths"
/* Caller-owned, stable canonical (realpath) explicit scan paths. This bounds label-derived config
 * refresh, not concurrent writers or paths later recovered from pool metadata.
 * Native lock/identity and lifetime admission remain the service's obligation. */
struct oz_import_paths {
    char *const *paths;
    unsigned count;
    int rejected;
};
extern pool_config_ops_t oz_aros_config_ops;
struct oz_packet_security;
int oz_import_config_check(struct oz_import_paths *,nvlist_t *);
/* selected has room for allowed->count borrowed pointers. No paths opened. */
int oz_import_config_select(struct oz_import_paths *,nvlist_t *,char **selected,unsigned *count);
int oz_cli_tryimport_begin(const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_tryimport_end(void *);
int oz_cli_import_begin(const char *,uint64_t,void *,size_t *,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_import_end(void *,int);
int oz_cli_create_begin(const char *,const void *,size_t,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_create_request(const char *,void *,size_t *,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
/* Caller retains checked file identity/access locks; reads only. */
int oz_destroyed_file_backing(int,uint64_t);
int oz_exported_file_backing(int,uint64_t);
int oz_cli_create_force_begin(const char *,const void *,size_t,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_add_begin(const char *,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_add_force_begin(const char *,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_add_request(const char *,void *,size_t *,cred_t *,const struct oz_packet_security *,void **);
int oz_cli_attach_begin(const char *,uint64_t,int,const void *,size_t,cred_t *,const struct oz_packet_security *,void **);
void oz_cli_add_end(void *,int);
int oz_cli_split_begin(const char *,const char *,const void *,size_t,cred_t *,void **);
int oz_cli_split_end(void *,int);
#endif
