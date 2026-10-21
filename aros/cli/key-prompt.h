/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_KEY_PROMPT_H
#define OZ_CLI_KEY_PROMPT_H
#include <stdio.h>
#include <stddef.h>
int oz_cli_read_secret(FILE *, char **, size_t *);
#endif
