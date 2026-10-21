/* SPDX-License-Identifier: CDDL-1.0 */
#include "output.h"
#include <errno.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>

void oz_cli_free_lines(char **lines, int count)
{
    for (int i = 0; i < count; ++i) free(lines[i]);
    free(lines);
}

int oz_cli_collect(oz_cli_reader reader, void *context, size_t max_bytes,
    char ***result, int *result_count)
{
    char buffer[4096], *line = NULL, **lines = NULL;
    size_t used = 0, capacity = 0, total = 0, slots = 0;
    int count = 0, error = 0;
    if (!result || !result_count) return EINVAL;
    *result = NULL; *result_count = 0;
    if (!reader || max_bytes > INT_MAX) return EINVAL;
    for (;;) {
        long got = reader(context, buffer, sizeof(buffer));
        if (got < 0 || (size_t)got > sizeof(buffer)) { error = EIO; break; }
        if ((size_t)got > max_bytes - total) { error = EOVERFLOW; break; }
        total += got;
        for (long i = 0; i < got || (!got && used); ++i) {
            int end = !got || buffer[i] == '\n';
            if (!end && buffer[i] == '\0') { error = EILSEQ; break; }
            if (used + (end ? 1 : 2) > capacity) {
                size_t next = capacity ? capacity * 2 : 128;
                if (next > max_bytes + 1) next = max_bytes + 1;
                char *grown = realloc(line, next);
                if (!grown) { error = ENOMEM; break; }
                line = grown; capacity = next;
            }
            if (!end) { line[used++] = buffer[i]; continue; }
            line[used] = '\0';
            if ((size_t)count == slots) {
                size_t next = slots ? slots * 2 : 16;
                if (next > INT_MAX || next > (size_t)-1 / sizeof(*lines)) {
                    error = EOVERFLOW; break;
                }
                char **grown = realloc(lines, next * sizeof(*lines));
                if (!grown) { error = ENOMEM; break; }
                lines = grown; slots = next;
            }
            lines[count] = malloc(used + 1);
            if (!lines[count]) { error = ENOMEM; break; }
            memcpy(lines[count++], line, used + 1);
            used = 0;
        }
        if (error || !got) break;
    }
    free(line);
    if (error) { oz_cli_free_lines(lines, count); return error; }
    *result = lines; *result_count = count;
    return 0;
}
