/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_STARTUP_IMPORT_H
#define OZ_STARTUP_IMPORT_H
#include "admission-api.h"
#include <stdint.h>
struct oz_startup_import_result {
    int skipped,imported,mount_requested,recovered,release_error;
    ULONG status;
    struct oz_ticket ticket;
    char pool[256];
    char detail[4096];
};
/* Import stage only, caller task must remain alive. No mount, discovery
 * beyond explicit paths, retry, force, or root impersonation. Ticket remains
 * visible if transport/release fails, so acceptance is not mistaken for
 * completion. The caller must handle mount_requested in a separate stage. */
int oz_startup_import(const char *,uint64_t,struct oz_startup_import_result *);
/* Unattended callers must use the metadata-validating reader. The imported
 * record is the exact decoded record that passed that reader, not a reload. */
int oz_startup_import_trusted(const char *,uint64_t,struct oz_startup_import_result *);
#endif
