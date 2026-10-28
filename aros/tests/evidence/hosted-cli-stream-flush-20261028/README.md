# Hosted CLI output, runtime coherence and persistent mirror

The initial full-service run returned success from `zpool list` with zero
captured bytes. A diagnostic-only CLI build proves the list callback runs and
explicit fflush restores output; the next unmodified `zfs list` then loses
output in the same way. The diagnostic patch is retained in `diagnostic/` and
was removed from production source. Two failed diagnostic build attempts are
retained (flag override dropped include paths; missing debug declaration).

The retained source POSIX snapshot did not contain the existing
`aros/cli/posix-stream-end.c` CLOSELIB priority-3 hook. The new
`build-posix-flush-snapshot.py` verifies the baseline and external inputs,
recompiles its sources plus that hook, and seals a fresh replay recipe. Its
source-only rebuild produced byte-identical output; see
`posix-flush-replay.log`. Neither upstream zpool nor zfs is patched to flush.

With output restored, delegation testing exposed a separate runtime mismatch.
`trap/gdb.log` captures a null-link write in POSIX usergroup registration.
The fault instruction sequence matches `__usergroup_available` in the retained
POSIX library: it accesses StdCIntBase.atexit_list at offset 0x378. Disassembly
of the old hosted stdc.library's __init_atexit shows offset 0x370 instead.
The SDK's matching stdc.library initializes 0x378. Selecting that matching
library in a new disposable runtime allows the entire writer suite to pass.
No permission checks were bypassed or changed.

The first cold-reader test then correctly rejected an absent bootenv marker:
only native-file writers seeded it, although hosted cold readers required it.
The persistent hosted writer now seeds and verifies it too. The cold check is
unchanged. `runtime-matched/` retains this failed test setup.

`pass/` records an unmodified CLI writer/service pass, independent host zdb
block/leak verification, a fresh-process cold reader, exact DOS payload and
metadata checks, directory enumeration, degraded mirror read, beyond-parity
refusal, and unchanged member hashes after read-only testing. All runtime
binary input hashes verify. This is private hosted qualification only; no
stock SDK, QEMU device, physical durability or complete-port claim follows.

`repeat/` independently passes the same full writer, integrity and cold-reader
cycle with unchanged member hashes. Both runs use the original untraced CLI
binaries and the same explicitly hash-recorded runtime libraries.

All changes and runs use isolated hosted copies and X servers. Logs, hashes
and recipes are retained; VM images and library binaries are not committed.
