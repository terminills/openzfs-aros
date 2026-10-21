/* SPDX-License-Identifier: CDDL-1.0 */
#include <string.h>
#include <stdio.h>
#include <aros/debug.h>
#include <proto/dos.h>
#include <dos/var.h>
int main(int argc, char **argv)
{
    const char *expected[] = {"", "two words", "quote\"star*", "line\nbreak",
        "tab\there", "$HOME;`literal`", "SYS:pool/path"};
    if (argc != 8 && argc != 9) {
        bug("CLI arguments child argc=%d expected=8-or-9\n", argc);
        return 20;
    }
    for (int i = 1; i < 8; ++i) {
        if (strcmp(argv[i], expected[i-1]) != 0) {
            bug("CLI arguments child mismatch index=%d\n", i);
            return 20;
        }
    }
    char value[32];
    if (argc == 9) {
        if (strcmp(argv[8], "CHECK_ENV") != 0) return 20;
        if (GetVar((CONST_STRPTR)"OZ_CLI_CHILD_ENV", (STRPTR)value,
            sizeof(value), GVF_LOCAL_ONLY) != 12) return 20;
        if (strcmp(value, "parent-value") != 0) return 20;
        if (!SetVar((CONST_STRPTR)"OZ_CLI_CHILD_ENV", (CONST_STRPTR)"child-value",
            -1, GVF_LOCAL_ONLY)) return 20;
    }
    puts("argv-child-output");
    return 17; /* Verify the real program result, not just launch success. */
}
