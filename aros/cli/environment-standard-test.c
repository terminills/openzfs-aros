/* SPDX-License-Identifier: CDDL-1.0 */
/* Diskless-guest prerequisite probe: the fixture must be created in RAM ENV.
 * Uses only public execve/GetVar/SetVar, never private process tags. */
#include <aros/debug.h>
#include <proto/dos.h>
#include <dos/var.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    char global[64];
    if (GetVar((CONST_STRPTR)"OZ_SDK_GLOBAL", (STRPTR)global,
        sizeof(global), GVF_GLOBAL_ONLY) != 7 || strcmp(global, "fixture")) {
        bug("RESULT OpenZFS standard environment FAIL fixture\n");
        return 20;
    }
    if (argc == 3 && !strcmp(argv[1], "child")) {
        int empty = !strcmp(argv[2], "empty");
        const char *value = getenv("OZ_SDK_VALUE");
        int explicit_ok = empty ? value == NULL : value && !strcmp(value, "child");
        int parent_hidden = getenv("OZ_SDK_PARENT") == NULL;
        int global_hidden = getenv("OZ_SDK_GLOBAL") == NULL;
        int ok = explicit_ok && parent_hidden && global_hidden;
        bug("RESULT OpenZFS standard environment %s mode=%s explicit=%d parent-hidden=%d global-hidden=%d\n",
            ok ? "PASS" : "FAIL", argv[2], explicit_ok, parent_hidden, global_hidden);
        return ok ? 0 : 20;
    }
    if (argc != 2 || (strcmp(argv[1], "explicit") && strcmp(argv[1], "empty")))
        return 20;
    if (!SetVar((CONST_STRPTR)"OZ_SDK_PARENT", (CONST_STRPTR)"parent", -1, GVF_LOCAL_ONLY))
        return 20;
    char *child_args[] = {"OpenZFSEnvironmentStandardTest", "child", argv[1], NULL};
    char *explicit_env[] = {"OZ_SDK_VALUE=child", NULL};
    char *empty_env[] = {NULL};
    bug("RESULT OpenZFS standard environment BEGIN mode=%s\n", argv[1]);
    execve("PROGDIR:OpenZFSEnvironmentStandardTest", child_args,
        !strcmp(argv[1], "empty") ? empty_env : explicit_env);
    bug("RESULT OpenZFS standard environment FAIL execve errno=%d\n", errno);
    return 20;
}
