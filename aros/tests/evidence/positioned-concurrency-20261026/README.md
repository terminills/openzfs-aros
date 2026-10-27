# Concurrent positional I/O on the selected AROS runtime

The new native test exercises actual `pread`/`pwrite` through four duplicated
descriptors sharing one open-file description. A start gate waits for all four
workers. Each performs 1,000 disjoint 64-byte write/read/compare iterations;
the main task concurrently performs 1,000 ordinary read/offset checks. It
joins every worker, closes every duplicate, and removes its private RAM file.
No unlocked seek/write replacement or private import-vector stub is used.

Final eight-CPU QEMU run `positioned-concurrent.uDHqQlPs` passes three fresh
process executions in one boot, with a separately checked shell-success marker.
The guest has no writable disk or network device. Inputs are alpha18c5, the
recorded private DOS/POSIX overlays, and the fresh source-built pthread archive
from `dependencies.YF6P1Tca`. This does not establish pristine official-nightly
compatibility, physical durability, or correctness under every interleaving.
The 8 KiB RAM fixture also does not replace the existing large-offset tests.

Host controls `positioned-host.rKhINLT9` pass three ordinary POSIX executions
and detect three deliberately unsafe replacements. The negative control wraps
only the host test's calls with unlocked seek/read-or-write/restore and yields
between operations. It is not linked into an AROS binary. All six host runs
remove their private files. This verifies sensitivity to an offset race rather
than merely counting successful thread creation.

Earlier attempts retained under `build-audit/positioned-concurrent-20261026`:
the first cross-compile rejected the pthread header's static initializer under
`-Werror`; the test now uses explicit mutex/condition initialization. A first
pack invocation had an accidental joined command and failed before boot. The
initial guest emitted a native PASS but timed out waiting for a shell Echo
which was sent to the display, not serial. The corrected runner mounts SER,
checks three native results plus shell status, and executes a private script
snapshot. Its generated ISO is checksum-recorded and removed after the owned
QEMU process exits and an open-file check passes. Logs and inputs are retained.

Reproduce with explicit SDK/compiler/private pthread and ISO/DOS/POSIX inputs
using `aros/cli/test-qemu-positioned-concurrent.sh`; use
`aros/cli/test-positioned-concurrent-host.sh` for the host controls. Both use
`AROS_TEST_ARTIFACT_ROOT` on a mounted external filesystem.
