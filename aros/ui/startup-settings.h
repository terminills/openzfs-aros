/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_STARTUP_SETTINGS_H
#define OZ_STARTUP_SETTINGS_H
#include <dos/dos.h>
#include <stdint.h>
#define OZ_STARTUP_READONLY 1
#define OZ_STARTUP_MOUNT 2
#define OZ_STARTUP_ENABLED 4
#define OZ_STARTUP_PATHS 32
struct oz_startup_settings {
    uint64_t guid;
    uint16_t flags,count;
    char pool[256],paths[OZ_STARTUP_PATHS][256];
};
/* Codec only: no imports, credentials, key material or implicit discovery.
 * Caller owns seekable DOS handle. Write to a new staging file; check Close.
 * A failed read leaves the output unchanged. Native path bytes, no controls. */
int oz_startup_settings_read(BPTR,struct oz_startup_settings *);
int oz_startup_settings_write(BPTR,const struct oz_startup_settings *);
/* Caller-context transactional replacement; never overwrites pending
 * transactions. Load may recover the previous record without modifying it.
 * Paths are explicit; no automatic import or ENV/ENVARC copying here. */
int oz_startup_settings_load(const char *,struct oz_startup_settings *,int *recovered);
int oz_startup_settings_store(const char *,const struct oz_startup_settings *,int *published);
/* One record per GUID: ENV:OpenZFS-<16 lowercase hex digits>.prefs.
 * Save publishes ENVARC first, then ENV. Use publishes ENV only. The mask
 * reports completed publications even if cleanup or the second write fails.
 * Load reads ENV only; system startup is responsible for copying ENVARC. */
#define OZ_STARTUP_PUBLISHED_ENV 1
#define OZ_STARTUP_PUBLISHED_ENVARC 2
int oz_startup_settings_apply(const struct oz_startup_settings *,int permanent,unsigned *published);
int oz_startup_settings_current(uint64_t guid,struct oz_startup_settings *,int *recovered);
/* Privileged boot reader: configured MuFS, system caller, system-owned regular
 * ENV file, no group/other write/delete permissions. Checks metadata through
 * the same handle it decodes. No archive/transaction fallback or chmod. */
int oz_startup_settings_trusted(uint64_t guid,struct oz_startup_settings *);
/* System-owner publication: private default creation permissions, checked
 * stage and file metadata, refuses foreign/insecure existing records. */
int oz_startup_settings_apply_trusted(const struct oz_startup_settings *,int permanent,unsigned *published);
/* Explicit trusted ENVARC -> ENV provisioning, not a reader fallback.
 * Verifies archive through one handle, publishes privately, refuses unsafe
 * existing ENV files and pending transactions. Never imports or repairs. */
int oz_startup_settings_restore_trusted(uint64_t guid,unsigned *published);
int oz_startup_settings_test(void);
#endif
