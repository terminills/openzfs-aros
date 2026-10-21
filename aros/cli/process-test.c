/* SPDX-License-Identifier: CDDL-1.0 */
#include "process.h"
#include <proto/dos.h>
#include <aros/debug.h>
#include <string.h>
#include <dos/var.h>
#include <dos/dosextens.h>
#include <dos/stdio.h>

#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI process FAIL line=%d\n", __LINE__); return 20; } } while (0)
int main(void)
{
    char *args[] = {"helper", "", "two words", "quote\"star*", "line\nbreak",
        "tab\there", "$HOME;`literal`", "SYS:pool/path", "CHECK_ENV", NULL};
    struct oz_cli_result result;
    CHECK(SetVar((CONST_STRPTR)"OZ_CLI_CHILD_ENV", (CONST_STRPTR)"parent-value",
        -1, GVF_LOCAL_ONLY));
    /* Control: raw DOS writes versus the selected overlay's buffered Flush. */
    BPTR control = Open((CONST_STRPTR)"RAM:cli-process-control", MODE_NEWFILE);
    CHECK(control);
    CHECK(Write(control, "argv-child-output\n", 18) == 18);
    LONG control_flush = Flush(control);
    bug("CLI process raw control flush=%ld error=%ld\n", (long)control_flush, (long)IoErr());
    CHECK(Close(control));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-process-control"));
    for (int i = 0; i < 16; ++i) {
        BPTR output = Open((CONST_STRPTR)"RAM:cli-process-output", MODE_NEWFILE);
        BPTR errors = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
        CHECK(output && errors);
        LONG mode = i % 3 == 0 ? BUF_FULL : i % 3 == 1 ? BUF_LINE : BUF_NONE;
        CHECK(SetVBuf(output, NULL, mode, -1) == 0);
        struct FileHandle *fh = BADDR(output);
        ULONG policy = fh->fh_Flags & 0x0c; /* native DOS LINEBUF/NOBUF */
        bug("CLI handle before flags=%lx pos=%ld end=%ld\n",
            (unsigned long)fh->fh_Flags, (long)fh->fh_Pos, (long)fh->fh_End);
        CHECK(oz_cli_spawn_exact("C:OpenZFSArgvChild", args, output, errors, &result) == 0);
        bug("CLI handle after flags=%lx pos=%ld end=%ld\n",
            (unsigned long)fh->fh_Flags, (long)fh->fh_Pos, (long)fh->fh_End);
        {
            LONG flushed = Flush(output);
            bug("CLI handle flush=%ld error=%ld flags=%lx pos=%ld end=%ld\n",
                (long)flushed, (long)IoErr(), (unsigned long)fh->fh_Flags,
                (long)fh->fh_Pos, (long)fh->fh_End);
            CHECK(flushed);
        }
        CHECK(result.status == 17 && result.dos_error == 0);
        CHECK((fh->fh_Flags & 0x0c) == policy);
        char environment[32];
        CHECK(GetVar((CONST_STRPTR)"OZ_CLI_CHILD_ENV", (STRPTR)environment,
            sizeof(environment), GVF_LOCAL_ONLY) == 12);
        CHECK(strcmp(environment, "parent-value") == 0);
        /* C runtime exit flushes the child's stdio. Read actual bytes, rather
         * than add a parent-side buffered-DOS Flush to a raw DOS handle. */
        CHECK(Seek(output, 0, OFFSET_BEGINNING) != -1);
        char text[64] = {0};
        LONG size = Read(output, text, sizeof(text)-1);
        CHECK(size == 18 && strcmp(text, "argv-child-output\n") == 0);
        /* Child must not have closed handles the parent still owns. */
        CHECK(Write(output, "parent", 6) == 6);
        CHECK(Close(output));
        CHECK(Close(errors));
    }
    BPTR output = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    BPTR errors = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    CHECK(output && errors);
    CHECK(oz_cli_spawn_exact("C:MissingOpenZFSHelper", args, output, errors, &result) != 0);
    CHECK(result.status == -1 && result.dos_error != 0);
    CHECK(Close(output)); CHECK(Close(errors));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-process-output"));
    CHECK(DeleteVar((CONST_STRPTR)"OZ_CLI_CHILD_ENV", GVF_LOCAL_ONLY));
    bug("RESULT native CLI process PASS runs=16\n");
    return 0;
}
