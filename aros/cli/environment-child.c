/* SPDX-License-Identifier: CDDL-1.0 */
#include <stdlib.h>
#include <string.h>
#include <proto/dos.h>
#include <dos/var.h>
#include <aros/debug.h>
#include "process.h"
int main(int argc, char **argv)
{
    char local[32];
    if (argc != 2) return 20;
    if (!strcmp(argv[1], "nested-global")) {
        char *args[] = {"environment-child", "global-probe", NULL};
        struct oz_cli_result result;
        if (oz_cli_spawn_exact("C:OpenZFSEnvironmentChild", args,
            Output(), Output(), &result) != 0) return 20;
        return result.status;
    }
    if (!strcmp(argv[1], "global-probe")) {
        const char *value = getenv("OZ_CLI_GLOBAL_ONLY");
        if (value != NULL) {
            bug("CLI global ENV value visible\n");
            return 31;
        }
        if (GetVar((CONST_STRPTR)"OZ_CLI_GLOBAL_ONLY", (STRPTR)local,
            sizeof(local), GVF_GLOBAL_ONLY) != 14 ||
            strcmp(local, "global-fixture") != 0) return 20;
        return 29;
    }
    if (GetVar((CONST_STRPTR)"OZ_CLI_PARENT_ONLY", (STRPTR)local,
        sizeof(local), GVF_LOCAL_ONLY) >= 0) return 20;
    if (getenv("OZ_CLI_PARENT_ONLY") != NULL) return 20;
    if (!strcmp(argv[1], "explicit")) {
        const char *value = getenv("OZ_CLI_ENV_VALUE");
        const char *empty = getenv("OZ_CLI_EMPTY");
        const char *literal = getenv("OZ_CLI_LITERAL");
        if (!value || strcmp(value, "child-value") || !empty || *empty ||
            !literal || strcmp(literal, "$HOME;`literal`")) return 20;
    } else if (!strcmp(argv[1], "empty")) {
        if (getenv("OZ_CLI_ENV_VALUE") != NULL) return 20;
    } else return 20;
    if (!SetVar((CONST_STRPTR)"OZ_CLI_ENV_VALUE", (CONST_STRPTR)"child-changed",
        -1, GVF_LOCAL_ONLY)) return 20;
    return 29;
}
