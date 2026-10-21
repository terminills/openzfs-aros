/* SPDX-License-Identifier: CDDL-1.0 */
#include "arguments.h"
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef __AROS__
#include <proto/dos.h>
#include <aros/debug.h>
#endif
int main(void)
{
    char *args[] = {"helper", "", "two words", "quote\"star*", "line\nbreak",
        "tab\there", "$HOME;`literal`", "SYS:pool/path", NULL};
    const char *expected = "\"\" \"two words\" \"quote*\"star**\" \"line*Nbreak\" "
        "\"tab\there\" \"$HOME;`literal`\" \"SYS:pool/path\" \n";
    char *encoded = NULL;
    if (oz_cli_arguments(args, 4096, &encoded) != 0 ||
        strcmp(encoded, expected) != 0) return 20;
    size_t exact = strlen(encoded) + 1;
    free(encoded);
    if (oz_cli_arguments(args, exact - 1, &encoded) != E2BIG || encoded != NULL)
        return 20;
    if (oz_cli_arguments(NULL, exact, &encoded) != EINVAL || encoded != NULL)
        return 20;
    if (oz_cli_arguments(args, exact, &encoded) != 0) return 20;
#ifdef __AROS__
    BPTR segment = LoadSeg((CONST_STRPTR)"C:OpenZFSArgvChild");
    if (!segment) return 20;
    LONG result = RunCommand(segment, 262144, (STRPTR)encoded, strlen(encoded));
    UnLoadSeg(segment);
    free(encoded);
    if (result != 17) {
        bug("RESULT native CLI arguments FAIL child=%ld\n", (long)result);
        return 20;
    }
    bug("RESULT native CLI arguments PASS\n");
#else
    free(encoded);
    puts("CLI arguments encoding PASS");
#endif
    return 0;
}
