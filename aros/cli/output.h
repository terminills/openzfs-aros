/* SPDX-License-Identifier: CDDL-1.0 */
#ifndef OZ_CLI_OUTPUT_H
#define OZ_CLI_OUTPUT_H
#include <stddef.h>
typedef long (*oz_cli_reader)(void *, char *, size_t);
/* Text stdout: remove LF separators, retain empty lines and final partial line.
 * Reader returns bytes, zero EOF, negative failure. Failure returns no partial
 * result. max_bytes limits accepted content, not the child's production or
 * reader read-ahead (at most one 4096-byte chunk beyond the accepted limit). */
int oz_cli_collect(oz_cli_reader, void *, size_t, char ***, int *);
void oz_cli_free_lines(char **, int);
#endif
