/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_SETTINGS_FILE_H
#define OZ_SETTINGS_FILE_H
#include <dos/dos.h>
#include <stddef.h>
struct FileInfoBlock;
struct oz_settings_codec {
    size_t size;
    int (*read)(BPTR,void *);
    int (*write)(BPTR,const void *);
    int (*equal)(const void *,const void *);
    int (*verify)(const struct FileInfoBlock *,int directory);
};
int oz_settings_load(const char *,void *,int *,const struct oz_settings_codec *);
int oz_settings_store(const char *,const void *,int *,const struct oz_settings_codec *);
#endif
