/* SPDX-License-Identifier: CDDL-1.0 */
/* Public-API prerequisite characterization, only in a disposable guest.
 * OZ_ENVIRON_ACCESS additionally exercises the optional link-library environ
 * initializer. No runtime overlay or private DOS process flags are used. */
#include <aros/debug.h>
#include <proto/dos.h>
#include <dos/var.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#ifndef OZ_ENVIRON_ACCESS
#define OZ_ENVIRON_ACCESS 0
#endif
#if OZ_ENVIRON_ACCESS
extern char **environ;
#define SELF "PROGDIR:EnvironmentMatrixEnviron"
#else
#define SELF "PROGDIR:EnvironmentMatrixPlain"
#endif

static int equal(const char *name, const char *wanted)
{
    const char *got = getenv(name);
    return wanted ? got && !strcmp(got, wanted) : got == NULL;
}

static int array_equal(const char *name, const char *wanted)
{
#if OZ_ENVIRON_ACCESS
    size_t n = strlen(name);
    for (char **p = environ; p && *p; ++p)
        if (!strncmp(*p, name, n) && (*p)[n] == '=')
            return wanted && !strcmp(*p + n + 1, wanted);
    return wanted == NULL;
#else
    (void)name; (void)wanted;
    return 1; /* Not exercised in the no-environ-symbol binary. */
#endif
}

static int report(const char *mode, int ok)
{
    bug("RESULT OpenZFS environment matrix %s mode=%s environ=%d\n",
        ok ? "PASS" : "FAIL", mode, OZ_ENVIRON_ACCESS);
    return ok ? 0 : 20;
}

static int child(const char *mode)
{
    int inherit = !strcmp(mode, "null");
    int alias = !strcmp(mode, "alias");
    int empty = !strcmp(mode, "empty");
    int value_ok = equal("OZ_SDK_VALUE", (inherit || alias || empty) ? NULL : "child");
    int parent_ok = equal("OZ_SDK_PARENT", (inherit || alias) ? "parent" : NULL);
    int global_ok = equal("OZ_SDK_GLOBAL", inherit ? "fixture" : NULL);
    int ok = value_ok && parent_ok && global_ok;
    bug("RESULT OpenZFS environment matrix scope mode=%s environ=%d value=%d parent=%d global=%d\n",
        mode, OZ_ENVIRON_ACCESS, value_ok, parent_ok, global_ok);
    if (!strcmp(mode, "nested")) {
        char *args[] = {"EnvironmentMatrix", "child", "nested-final", NULL};
        /* Record the first scope failure independently, then still inspect
         * inherited execv behavior rather than hiding it behind early exit. */
        bug("RESULT OpenZFS environment matrix nested-first=%d environ=%d\n", ok, OZ_ENVIRON_ACCESS);
        execv(SELF, args);
        bug("RESULT OpenZFS environment matrix exec-return mode=nested errno=%d\n", errno);
        return report(mode, 0);
    }
    if (!strcmp(mode, "mutate")) {
        int mutations = setenv("OZ_SDK_VALUE", "ignored", 0) == 0;
        mutations &= equal("OZ_SDK_VALUE", "child") && array_equal("OZ_SDK_VALUE", "child");
        mutations &= setenv("OZ_SDK_VALUE", "changed", 1) == 0;
        mutations &= equal("OZ_SDK_VALUE", "changed") && array_equal("OZ_SDK_VALUE", "changed");
        char put[] = "OZ_SDK_VALUE=put";
        mutations &= putenv(put) == 0;
        mutations &= equal("OZ_SDK_VALUE", "put") && array_equal("OZ_SDK_VALUE", "put");
        mutations &= unsetenv("OZ_SDK_VALUE") == 0;
        mutations &= equal("OZ_SDK_VALUE", NULL) && array_equal("OZ_SDK_VALUE", NULL);
        mutations &= setenv("OZ_SDK_EMPTY", "", 1) == 0;
        mutations &= equal("OZ_SDK_EMPTY", "") && array_equal("OZ_SDK_EMPTY", "");
        int override_ok = setenv("OZ_SDK_GLOBAL", "local", 1) == 0 && equal("OZ_SDK_GLOBAL", "local");
        int unset_ok = unsetenv("OZ_SDK_GLOBAL") == 0 && equal("OZ_SDK_GLOBAL", NULL);
        bug("RESULT OpenZFS environment matrix mutations environ=%d local-coherent=%d override=%d unset-hidden=%d\n",
            OZ_ENVIRON_ACCESS, mutations, override_ok, unset_ok);
        ok &= mutations && override_ok && unset_ok;
    }
#if OZ_ENVIRON_ACCESS
    if (!strcmp(mode, "replace")) {
        char **saved = environ;
        char value[] = "OZ_SDK_REPLACED=owned-by-caller";
        char *replacement[] = {value, NULL};
        environ = replacement;
        int observed = equal("OZ_SDK_REPLACED", "owned-by-caller") && equal("OZ_SDK_VALUE", NULL);
        environ = saved; /* Never leave a stack array registered at teardown. */
        bug("RESULT OpenZFS environment matrix replacement observed=%d\n", observed);
        ok &= observed;
    }
#endif
    return report(mode, ok);
}

int main(int argc, char **argv)
{
    char global[32];
    if (GetVar((CONST_STRPTR)"OZ_SDK_GLOBAL", (STRPTR)global,
        sizeof(global), GVF_GLOBAL_ONLY) != 7 || strcmp(global, "fixture"))
        return report("fixture", 0);
    if (argc == 3 && !strcmp(argv[1], "child"))
        return child(argv[2]);
    if (argc != 2) return report("arguments", 0);
    const char *mode = argv[1];
    if (setenv("OZ_SDK_PARENT", "parent", 1)) return report("parent-setup", 0);
    if (!strcmp(mode, "ordinary"))
        return report(mode, equal("OZ_SDK_PARENT", "parent") && equal("OZ_SDK_GLOBAL", "fixture"));
    char *args[] = {"EnvironmentMatrix", "child", argv[1], NULL};
    char *explicit_env[] = {"OZ_SDK_VALUE=child", NULL};
    char *empty_env[] = {NULL};
    char **env = explicit_env;
    if (!strcmp(mode, "null")) env = NULL;
    else if (!strcmp(mode, "empty")) env = empty_env;
#if OZ_ENVIRON_ACCESS
    else if (!strcmp(mode, "alias")) env = environ;
#endif
    bug("RESULT OpenZFS environment matrix BEGIN mode=%s environ=%d\n", mode, OZ_ENVIRON_ACCESS);
    if (!strcmp(mode, "failed")) {
        errno = 0;
        int result = execve("PROGDIR:OZ-NONEXISTENT-ENV-MATRIX", args, empty_env);
        int error = errno;
        int intact = equal("OZ_SDK_PARENT", "parent") && equal("OZ_SDK_GLOBAL", "fixture") &&
            array_equal("OZ_SDK_PARENT", "parent");
        bug("RESULT OpenZFS environment matrix failed-exec result=%d errno=%d intact=%d environ=%d\n",
            result, error, intact, OZ_ENVIRON_ACCESS);
        return report(mode, result == -1 && error != 0 && intact);
    }
    execve(SELF, args, env);
    bug("RESULT OpenZFS environment matrix exec-return mode=%s errno=%d\n", mode, errno);
    return report(mode, 0);
}
