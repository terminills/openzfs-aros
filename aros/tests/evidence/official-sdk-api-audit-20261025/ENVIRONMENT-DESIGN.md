# Proposed prerequisite repair boundary (not implemented)

The native regression in ENVIRONMENT.md reproduces global fallback leakage.
The following design is for review by the POSIX runtime owner. No DOS or CRT
code is changed here, and this is not an accepted upstream interface proposal.

## Current upstream source comparison

Inspected revision: 827810f5f1, separately from the published nightly image.

* `compiler/crt/posixc/__exec.c`, `__exec_prepare_regular`: replacement of DOS
  local variables happens only when `envp && envp != environ`. It saves the old
  local list, clears the process list, imports through putenv, then frees old
  values on success or restores them on failure. NULL currently takes the
  non-replacement branch; pointer identity is not a sound expression of the
  caller's intended environment policy.
* `compiler/crt/posixc/getenv.c`: looks up through GetVar without LOCAL_ONLY;
  it obtains cache storage through the stdcio environment adapter.
* `compiler/crt/posixc/setenv.c` and `unsetenv.c`: modify DOS local variables
  and refresh the emulated environ pointer. Removing a local override can
  therefore reveal an unwanted global value again.
* `compiler/crt/posixc/putenv.c`: explicitly documents its copying BSD-style
  behavior; splits a copied input string and delegates to setenv/unsetenv.
  Changing that ownership contract is outside this focused repair.
* `compiler/crt/posixc/posixc_environ.c`: environ emulation is activated by a
  link-library initializer only when that symbol is pulled into the program.
  Absence of an environ pointer therefore cannot mean that a child should
  ignore an explicitly supplied execve environment.
* `compiler/crt/stdc/__stdcio_environ.c`: owns the registered application
  environ-pointer address and materializes locals into an allocated array.
  Refresh/replacement ownership must be audited, not bypassed with another
  unmanaged table.
* `__exec_do_regular` invokes RunCommand in the process and preserves/restores
  launcher CRT bases across the child lifecycle. A policy stored only in a
  temporary caller stack or freed launcher cache cannot safely govern the new
  program. The vfork/PRETEND_CHILD handoff is another lifetime boundary.

## Narrow semantic repair

Keep normal AROS environment lookup unchanged outside an explicit POSIX
environment context. For an execve call with a non-NULL envp, the supplied
environment must become authoritative for POSIX getenv and environ, including
an empty vector and an explicitly supplied pointer equal to environ. Missing
names must stay absent rather than falling back to ENV:/ENVARC:.

Represent this as internal per-execution CRT state with owned environment
storage and a defined handoff to the newly initialized program runtime. Do not
invent public LVOs, add private process tags to application headers, change
global assignments, borrow caller strings beyond their lifetime, or modify
ordinary DOS GetVar behavior as a shortcut. Existing explicit
GetVar(..., GVF_GLOBAL_ONLY) remains a separate DOS operation, not getenv.

An inherited execv/execvp chain must preserve an already authoritative context.
The runtime entry points must carry this distinction explicitly; comparing
envp to environ is insufficient. NULL envp is a separate compatibility case:
the inspected implementation inherits local state. Preserve that behavior in
the initial proposal unless the owner deliberately chooses and documents a
different contract. The native regression has not yet exercised NULL envp.

setenv, unsetenv, putenv and the exposed environ array must describe one
coherent context. In authoritative mode, unsetting the last local value must
not reveal a same-named global value. Preserve the documented putenv copy
semantics in this focused change. Direct caller replacement of environ needs
an explicit ownership policy; never free caller-owned arrays or retain a
pointer to a child's torn-down data segment.

Prepare new context transactionally. An allocation, validation or LoadSeg
failure must leave the caller's old environment, policy, cache and environ
pointer usable. Commit the handoff only when execution can proceed; release
owned storage at the matching child/runtime teardown. Review vfork shared
state, nested RunCommand and SMP isolation before claiming this complete.

## Required isolated acceptance matrix

1. Normal AROS getenv still sees permitted globals; sibling processes are
   unaffected by another process's authoritative environment.
2. Explicit nonempty, empty, NULL, and envp == environ cases have documented
   outcomes. Test binaries both with and without an environ symbol reference.
3. Local-only and global-only sentinels, same-name collisions, empty values,
   nested execv/execve, and failed exec leave the expected values visible.
4. setenv overwrite/no-overwrite, unsetenv and putenv operations remain
   coherent with getenv and environ, without global writes or fallback leaks.
5. Repeated exec/cleanup and injected allocation failure expose no dangling
   pointers, double frees or environment growth. Exercise the vfork handoff
   separately from the ordinary RunCommand path.
6. Check official old callers against repaired runtime as well as rebuilt
   callers, and rerun the OpenZFS exact-environment tests before replacing the
   current adapter. No pipe or errno ABI changes belong in this prerequisite.
