/* SPDX-License-Identifier: CDDL-1.0 */
#include "process.h"
#include <proto/dos.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI environment FAIL line=%d\n", __LINE__); return 20; } } while (0)
int main(void)
{
    char *args[] = {"output-child", "lines", NULL};
    char *empty[] = {NULL};
    struct oz_cli_result result;
    BPTR out = Open((CONST_STRPTR)"RAM:cli-env-rejection", MODE_NEWFILE);
    CHECK(out);
    for (int i = 0; i < 16; ++i) {
        int error = oz_cli_spawn_env_exact("C:OpenZFSOutputChild", args,
            empty, out, out, &result);
        CHECK(error == ENOTSUP && result.status == -1);
        CHECK(Flush(out));
        CHECK(Seek(out, 0, OFFSET_CURRENT) == 0);
    }
    /* Positive control: this exact executable and handle work normally. */
    CHECK(oz_cli_spawn_exact("C:OpenZFSOutputChild", args, out, out, &result) == 0);
    CHECK(result.status == 23);
    CHECK(Flush(out));
    CHECK(Seek(out, 0, OFFSET_BEGINNING) == 12);
    char value[13] = {0};
    CHECK(Read(out, value, 12) == 12);
    CHECK(strcmp(value, "alpha\n\nomega") == 0);
    CHECK(Close(out));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-env-rejection"));
    bug("RESULT native CLI environment PASS unsupported runs=16\n");
    return 0;
}
