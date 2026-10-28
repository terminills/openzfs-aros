# Packaged CLI and full hosted runtime check

The generated MetaMake build `mmake-cli.zqQd3gIm` rebuilt the actual CLI,
Manager and companion programs from ce78b09. Build commands, compiler output
and binary hashes are retained under `build`.

`negative` is gB3OUEfy: full storage succeeded but RuntimeCheck rejected the
missing ExecHelper and unpatched hosted DOS accepting execute-off files.
The failed check stopped acceptance before cold validation.

`first-pass` is tmqAv3Nb: explicit helper installation plus the retained
loader-overlay.4CUMjP0r DOS fix pass the full runtime and persistent suite.
Its runtime-check.log was overwritten by the cold boot, so it is not two
independently retained preflight logs.

`repeat` is OiE9aafR: separate writer-runtime-check.log and
cold-runtime-check.log each pass the full preflight. Both preserve explicit
flush, normal-return and exit teardown checks, ExecHelper status 23/output,
and LoadSeg/NewLoadSeg on/off/on checks. The actual ZFS loader-policy fixture,
writer/CLI/handler suite, independent host zdb No leaks, cold exact reads,
degraded mirror and unchanged member hashes all pass. Inputs are hash-bound.

Runtime profile: SDK-matched stdc, retained TKCE3ewE stdcio, source-retained
j099bkwp POSIX flush variant and the private hosted DOS loader overlay.
This does not qualify an arbitrary stock nightly or a native QEMU package.
No DUT, user-hosted runtime or other agent's VM was accessed.
