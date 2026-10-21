/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_PROCESS_H
#define OZ_CLI_PROCESS_H
#include <dos/dos.h>
#include "output.h"
/* Native exact-path executable only; no shell or PATH search. Streams remain
 * caller-owned. The child inherits local environment; normal AROS process and
 * executable credential rules apply (no daemon elevation is performed).
 * Returns errno-style failure, preserving the DOS diagnostic separately. */
struct oz_cli_result { long status; long dos_error; };
int oz_cli_spawn_exact(const char *, char *const [], BPTR, BPTR,
    struct oz_cli_result *);
/* Explicit native local environment, NULL inherits; {NULL} starts empty.
 * Names conflicting under AROS case-insensitive lookup are rejected. */
int oz_cli_spawn_env_exact(const char *, char *const [], char *const [],
    BPTR, BPTR, struct oz_cli_result *);
/* Read from the current position; does not close the caller's handle. */
int oz_cli_read_lines(BPTR, size_t, char ***, int *);
/* Synchronous exact native executable, capturing text stdout in an exclusive
 * temporary file. limit bounds accepted text, not bytes produced by the child.
 * Child exit status is independent of transport/collection errors. */
int oz_cli_capture_env_exact(const char *,char *const [],char *const [],BPTR,
    size_t,char ***,int *,struct oz_cli_result *);
/* Upstream STDOUT_VERBOSE=1, STDERR_VERBOSE=2, NO_DEFAULT_PATH=4.
 * Requires installed SYS:Tools/OpenZFS/ExecHelper. No shell interpolation. */
int oz_cli_run_process(const char *,char *const [],char *const [],int,char ***,int *);
#endif
