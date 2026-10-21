/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_DEVICE_PATH_H
#define OZ_DEVICE_PATH_H
#include <stddef.h>
#include <stdint.h>
struct oz_device_path {
    char device[256];
    uint32_t unit, position;
    int partition;
    size_t whole_length;
};
/* Lexical identity only. Never opens devices or grants admission. Output is
 * unchanged on error; canonical decimal components prohibit aliases. */
int oz_device_path_parse(const char *,struct oz_device_path *);
char *zfs_strip_partition(const char *);
const char *zfs_strip_path(const char *);
int zfs_append_partition(char *,size_t);
#endif
