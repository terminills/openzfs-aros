/* SPDX-License-Identifier: CDDL-1.0 */
#include "process.h"
#include <proto/dos.h>
#include <dos/var.h>
#include <aros/debug.h>
#include <errno.h>
#include <string.h>
#define CHECK(x) do { if (!(x)) { bug("RESULT native CLI environment FAIL line=%d\n", __LINE__); return 20; } } while (0)
int main(void)
{
    char *env[] = {"OZ_CLI_ENV_VALUE=child-value", "OZ_CLI_EMPTY=",
        "OZ_CLI_LITERAL=$HOME;`literal`", NULL};
    char *empty[] = {NULL};
    struct oz_cli_result result;
    CHECK(SetVar((CONST_STRPTR)"OZ_CLI_ENV_VALUE", (CONST_STRPTR)"parent-value", -1, GVF_LOCAL_ONLY));
    CHECK(SetVar((CONST_STRPTR)"OZ_CLI_PARENT_ONLY", (CONST_STRPTR)"excluded", -1, GVF_LOCAL_ONLY));
    BPTR out = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    BPTR err = Open((CONST_STRPTR)"NIL:", MODE_NEWFILE);
    CHECK(out && err);
    for (int i = 0; i < 16; ++i) {
        char *args[] = {"environment-child", i % 2 ? "empty" : "explicit", NULL};
        CHECK(oz_cli_spawn_env_exact("C:OpenZFSEnvironmentChild", args,
            i % 2 ? empty : env, out, err, &result) == 0);
        bug("CLI environment run=%d status=%ld error=%ld\n", i, result.status, result.dos_error);
        CHECK(result.status == 29 && result.dos_error == 0);
        char value[32];
        CHECK(GetVar((CONST_STRPTR)"OZ_CLI_ENV_VALUE", (STRPTR)value, sizeof(value), GVF_LOCAL_ONLY) == 12);
        CHECK(strcmp(value, "parent-value") == 0);
    }
    char *args[] = {"environment-child", "explicit", NULL};
    char *invalid[] = {"missing-equals", NULL};
    char *duplicate[] = {"Name=a", "NAME=b", NULL};
    CHECK(oz_cli_spawn_env_exact("C:OpenZFSEnvironmentChild", args, invalid, out, err, &result) == EINVAL);
    CHECK(result.status == -1);
    CHECK(oz_cli_spawn_env_exact("C:OpenZFSEnvironmentChild", args, duplicate, out, err, &result) == EINVAL);
    CHECK(result.status == -1);
    /* Only in this diskless disposable QEMU: temporarily replace ENV: with
     * a guest RAM directory, then restore it before checking the result. */
    BPTR previous_env = Lock((CONST_STRPTR)"ENV:", SHARED_LOCK);
    BPTR private_env = CreateDir((CONST_STRPTR)"RAM:cli-private-env");
    CHECK(private_env);
    CHECK(AssignLock((CONST_STRPTR)"ENV", private_env));
    CHECK(SetVar((CONST_STRPTR)"OZ_CLI_GLOBAL_ONLY", (CONST_STRPTR)"global-fixture",
        -1, GVF_GLOBAL_ONLY));
    char *global_args[] = {"environment-child", "global-probe", NULL};
    int global_error = oz_cli_spawn_env_exact("C:OpenZFSEnvironmentChild", global_args,
        empty, out, err, &result);
    long global_status = result.status;
    int inherit_error = oz_cli_spawn_exact("C:OpenZFSEnvironmentChild", global_args,
        out, err, &result);
    long inherit_status = result.status;
    char *nested_args[] = {"environment-child", "nested-global", NULL};
    int nested_error = oz_cli_spawn_env_exact("C:OpenZFSEnvironmentChild", nested_args,
        empty, out, err, &result);
    long nested_status = result.status;
    CHECK(DeleteVar((CONST_STRPTR)"OZ_CLI_GLOBAL_ONLY", GVF_GLOBAL_ONLY));
    CHECK(AssignLock((CONST_STRPTR)"ENV", previous_env));
    CHECK(DeleteFile((CONST_STRPTR)"RAM:cli-private-env"));
    bug("CLI explicit global isolation status=%ld launch-error=%d\n", global_status, global_error);
    CHECK(global_error == 0 && global_status == 29);
    CHECK(inherit_error == 0 && inherit_status == 31);
    CHECK(nested_error == 0 && nested_status == 29);
    CHECK(Close(out)); CHECK(Close(err));
    CHECK(DeleteVar((CONST_STRPTR)"OZ_CLI_ENV_VALUE", GVF_LOCAL_ONLY));
    CHECK(DeleteVar((CONST_STRPTR)"OZ_CLI_PARENT_ONLY", GVF_LOCAL_ONLY));
    bug("RESULT native CLI environment PASS runs=16\n");
    return 0;
}
