/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_ARGUMENTS_H
#define OZ_CLI_ARGUMENTS_H
#include <stddef.h>
/* Encode argv[1..] for direct RunCommand/NP_Arguments, NOT a shell command.
 * Includes the required trailing newline. Caller frees *out on success.
 * limit bounds the allocated string including its terminating NUL. */
int oz_cli_arguments(char *const argv[], size_t limit, char **out);
#endif
