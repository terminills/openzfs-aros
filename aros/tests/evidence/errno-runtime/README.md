# errno runtime boundary

`OpenZFSRuntimeCheck` now verifies its errno accessor with two sentinel values,
then requires POSIX `close(-1)` to return -1 and update that same slot to EBADF.
It restores the original errno. `--errno-only` provides the focused probe without
security, process-launch or loader-policy prerequisites. An incompatible ABI
can fault rather than report FAIL; run candidates in an isolated guest.

Host C11 builds with `-Wall -Wextra -Werror -O2` passed both
`aros/cli/test-errno-check.c` and `aros/cli/test-errno-check-negative.c`.
The latter simulates a POSIX writer failing to update errno and checks that
the probe rejects it while preserving the original errno.

Native run `public-service-run.creng8YL` used the rebuilt RuntimeCheck and
otherwise unchanged recorded alpha18c5 platform and production payloads.
The eight-CPU QEMU suite printed `PASS errno accessor and POSIX writer`,
passed companion checks and the mirror DOS/read-only-reimport suite, and
exited zero. This tests the normal RuntimeCheck route, not its standalone
`--errno-only` invocation. Generated ISO pruning and hashes are retained.

The first attempt `public-service-run.tBiunYcQ` failed before boot because
the private package lacked Tools/OpenZFS/ExecHelper. Its log is retained;
the corrected private layout referenced the existing exact helper.

This does not qualify the newer stdc ABI or all thread-local errno behavior.
The full matched payload must still be rebuilt and run against the new SDK
and runtime; no old object/archive or binary patch is a substitute.
