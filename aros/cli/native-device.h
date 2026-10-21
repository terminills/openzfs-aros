/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_NATIVE_DEVICE_H
#define OZ_NATIVE_DEVICE_H
#include <stddef.h>
#include <stdint.h>
/* Authenticated read-only geometry; never grants write admission. */
int oz_native_vdev_size(const char *,uint64_t *,int *);
int oz_native_import_paths(char ***,size_t *);
void oz_native_import_paths_free(char **,size_t);
#endif
