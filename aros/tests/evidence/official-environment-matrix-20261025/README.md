# Official runtime environment matrix

Characterization, not qualification: **6 passing cases and 10 reproduced
defects** on the pristine 20261023 SMP nightly. No runtime overlays or private
API additions. The source probes use only public exec/getenv/environment APIs.

Baseline ISO SHA-256:
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
SDK is the unmodified Developer extracted from that ISO. Native test used a
private diskless QEMU, KVM, host CPU, 2 CPUs, 2048 MiB, no network and BIOS CD
boot. Original OS modules unchanged. Generated BIOS-only images explicitly
zeroed inherited system-area metadata and both xorriso runs exited zero.

External artifacts: `/mnt/short-term/openzfs-env-matrix.cJJ5YNnS`.
Logs and input hashes retained here; binaries and link maps retained there.
Map confirms no environ initializer in Plain and stdcio_environ.o in Environ.

| Case | Plain | Environ | Observation |
| --- | --- | --- | --- |
| ordinary lookup | PASS | PASS | Parent-local and global value visible |
| NULL envp | PASS | PASS | Preserves legacy local/global inheritance |
| explicit envp | FAIL | FAIL | Requested value correct; global leaks |
| empty non-NULL envp | FAIL | FAIL | Locals absent; global leaks |
| nested execv | FAIL | FAIL | Global leaks in both scopes |
| mutation | FAIL | FAIL | Local changes coherent; unset reveals global |
| failed exec | PASS | PASS | ENOENT; original environment intact |
| envp == environ | not applicable | FAIL | Explicit environment still leaks global |
| direct environ replacement | not applicable | FAIL | getenv ignores replacement array |

Mutation covers setenv overwrite/no-overwrite, putenv, unsetenv, empty values,
and environ coherence where the symbol is linked. No claim of fault-injection,
concurrent mutation safety, lifetime/leak freedom or vfork coverage is made.

The first run used Echo's default newline, making exact-value ordinary, NULL
and rollback checks fail spuriously. Its serial.log and Startup-Sequence are
retained as a failed harness attempt. Corrected startup uses NOLINE;
serial-exact.log and Startup-Exact are authoritative. Missing-name/global-leak
and direct-array failures were not caused by the newline.

`verify-environment-matrix.py --expect-known-defects serial-exact.log` validates
the exact baseline pattern and prints NOT qualified. Default verification
rejects this run. All final results, shell exits, nested scopes, mutation
invariants, rollback and completion markers are checked. Eight verifier tests
exercise valid results, known-defect classification and missing/duplicate/
contradictory evidence. Logs record both verifier modes.

Build: x86_64-aros-gcc, explicit official sysroot and -B SDK/lib/, -nix,
-D_GNU_SOURCE -O2 -Wall -Wextra -Werror, with OZ_ENVIRON_ACCESS=0 or 1.
The initial compile omitted feature-test macros and correctly rejected missing
setenv/unsetenv declarations; _GNU_SOURCE enables the official header's own
declarations, with no injected declarations or private stubs.

QEMU processes were stopped through their private QMP sockets after DONE.
Disposable ISO copies were removed only after terminal/open-file checks and
recording hashes; logs, source and binaries remain. See pruned-images files.
