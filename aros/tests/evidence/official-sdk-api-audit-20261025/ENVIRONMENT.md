# Explicit execve environment: reproduced global fallback leak

Pristine published 20261023 SMP runtime and its unmodified SDK; same baseline
and diskless QEMU configuration as README.md. No private process flags or
runtime overlays. Source: `aros/cli/environment-standard-test.c`.

Each test execs its own executable using standard execve and a supplied envp.
The parent sets a process-local sentinel. Startup creates a distinct global
sentinel only in the disposable guest's RAM:ENV. Child confirms the global
fixture exists with explicit GVF_GLOBAL_ONLY, then checks normal getenv.

Both explicit and empty environment tests reached their child main and returned
20, with identical diagnostic fields:

```
explicit=1 parent-hidden=1 global-hidden=0
```

The requested explicit value (or absence for empty envp) was correct; inherited
local sentinel was hidden; unrequested global sentinel remained visible.
Shell exit-failure markers and final audit DONE confirm complete execution of
both cases. Startup deletes the guest fixture afterward. No host data, physical
media, shared hosted process or DUT was involved.

This reproduces a standard POSIX API semantic prerequisite, not proof that
OpenZFS needs a private DOS process flag. Current upstream 827810f5f1 separately
shows posixc/getenv calling GetVar without GVF_LOCAL_ONLY, and DOS GetVar falling
back to ENV:/ENVARC:. The published nightly's exact source revision is unknown.

Still required before a fix: establish NULL versus empty envp semantics,
setenv/putenv/unsetenv/environ coherence, nested execution, ownership/lifetime
and preservation of ordinary AROS getenv defaults. No shared runtime fix or
new private API is included in this commit.

Build used the same explicit official sysroot, -B SDK/lib/, -nix, -O2,
-Wall -Wextra -Werror as the errno probe, producing standalone executable
`OpenZFSEnvironmentStandardTest`; map and binary remain under
`/mnt/short-term/openzfs-official-sdk-audit.xi9RkF3r`.
