/* SPDX-License-Identifier: CDDL-1.0 */
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv)
{
    if (argc == 3 && !strcmp(argv[1], "token")) {
        fputs(argv[2], stdout);
        return fflush(stdout) == 0 ? 23 : 20;
    }
    if (argc != 2) return 20;
    if (!strcmp(argv[1], "empty")) return 23;
    if (!strcmp(argv[1], "lines")) {
        fputs("alpha\n\nomega", stdout);
    } else if (!strcmp(argv[1], "large")) {
        char block[4096]; memset(block, 'x', sizeof(block));
        for (int i = 0; i < 32; ++i)
            if (fwrite(block, 1, sizeof(block), stdout) != sizeof(block)) return 20;
        fputs("\ntail", stdout);
    } else if (!strcmp(argv[1], "nul")) {
        const char bytes[] = {'a', 0, 'b', '\n'};
        if (fwrite(bytes, 1, sizeof(bytes), stdout) != sizeof(bytes)) return 20;
    } else return 20;
    return fflush(stdout) == 0 ? 23 : 20;
}
