/* SPDX-License-Identifier: CDDL-1.0 */
#include "output.h"
#include <assert.h>
#include <errno.h>
#include <stdio.h>
#include <string.h>
struct source { const char *text; size_t length, position, chunk; int fail; };
static long read_source(void *context, char *out, size_t capacity)
{
    struct source *s = context;
    if (s->fail && s->position >= s->chunk) return -1;
    size_t n = s->length - s->position;
    if (n > capacity) n = capacity;
    if (n > s->chunk) n = s->chunk;
    memcpy(out, s->text + s->position, n); s->position += n;
    return n;
}
int main(void)
{
    const size_t chunks[] = {1, 3, 4096};
    for (size_t i = 0; i < 3; ++i) {
        struct source src = {"one\n\nlast", 9, 0, chunks[i], 0};
        char **lines = NULL; int count = 0;
        assert(oz_cli_collect(read_source, &src, 9, &lines, &count) == 0);
        assert(count == 3 && !strcmp(lines[0], "one") &&
            !strcmp(lines[1], "") && !strcmp(lines[2], "last"));
        oz_cli_free_lines(lines, count);
        src.position = 0;
        assert(oz_cli_collect(read_source, &src, 8, &lines, &count) == EOVERFLOW);
        assert(lines == NULL && count == 0);
    }
    struct source src = {"a\nb", 3, 0, 2, 1};
    char **lines = NULL; int count = 0;
    assert(oz_cli_collect(read_source, &src, 3, &lines, &count) == EIO);
    assert(lines == NULL && count == 0);
    src = (struct source){"a\nb\0c", 5, 0, 2, 0};
    assert(oz_cli_collect(read_source, &src, 5, &lines, &count) == EILSEQ);
    assert(lines == NULL && count == 0);
    src = (struct source){"", 0, 0, 1, 0};
    assert(oz_cli_collect(read_source, &src, 0, &lines, &count) == 0);
    assert(lines == NULL && count == 0);
    src = (struct source){"\n", 1, 0, 1, 0};
    assert(oz_cli_collect(read_source, &src, 1, &lines, &count) == 0);
    assert(count == 1 && !strcmp(lines[0], ""));
    oz_cli_free_lines(lines, count);
    puts("CLI stdout collector PASS");
    return 0;
}
