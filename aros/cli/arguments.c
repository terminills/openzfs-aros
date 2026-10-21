/* SPDX-License-Identifier: CDDL-1.0 */
#include "arguments.h"
#include <errno.h>
#include <stdlib.h>

int oz_cli_arguments(char *const argv[], size_t limit, char **out)
{
    size_t size = 2; /* newline and NUL */
    char *buffer, *p;
    if (out == NULL) return EINVAL;
    *out = NULL;
    if (argv == NULL || argv[0] == NULL) return EINVAL;
    if (limit < size) return E2BIG;
    for (size_t i = 1; argv[i] != NULL; ++i) {
        if (limit - size < 3) return E2BIG; /* quotes and separator */
        size += 3;
        for (const unsigned char *s = (const unsigned char *)argv[i]; *s; ++s) {
            size_t n = (*s == '*' || *s == '"' || *s == '\n') ? 2 : 1;
            if (limit - size < n) return E2BIG;
            size += n;
        }
    }
    buffer = malloc(size);
    if (buffer == NULL) return ENOMEM;
    p = buffer;
    for (size_t i = 1; argv[i] != NULL; ++i) {
        *p++ = '"';
        for (const unsigned char *s = (const unsigned char *)argv[i]; *s; ++s) {
            if (*s == '*' || *s == '"' || *s == '\n') {
                *p++ = '*';
                *p++ = *s == '\n' ? 'N' : *s;
            } else {
                *p++ = *s;
            }
        }
        *p++ = '"';
        *p++ = ' ';
    }
    *p++ = '\n';
    *p = '\0';
    *out = buffer;
    return 0;
}
