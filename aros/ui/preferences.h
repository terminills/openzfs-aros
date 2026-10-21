/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_PREFERENCES_H
#define OZ_PREFERENCES_H
#include <dos/dos.h>
#include <stdint.h>
struct oz_preferences {
    uint16_t category, width, height;
    uint16_t view_mode;
};
enum { OZ_UI_SIMPLE=0, OZ_UI_ADVANCED=1 };
void oz_preferences_default(struct oz_preferences *);
/* Seekable native DOS handles; caller retains handle ownership. Read does
 * not change the result on error. Write must target a new staging file. */
int oz_preferences_read(BPTR,struct oz_preferences *);
int oz_preferences_write(BPTR,const struct oz_preferences *);
/* Native caller context, no service/root identity. A pending transaction is
 * never overwritten. Load can recover the previous file read-only and reports
 * that fact to the UI; it does not silently repair or delete pending files. */
int oz_preferences_load(const char *,struct oz_preferences *,int *recovered);
int oz_preferences_store(const char *,const struct oz_preferences *,int *published);
#endif
