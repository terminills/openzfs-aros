# Native exchange intent-log replay

`6295cbe` enables the upstream exchange replay callback and its shared rename
flag validation for AROS. Previously the live exchange path was implemented,
but the replay callback returned ENOTSUP outside Linux. Clean exports could
not expose that gap. Whiteout remains unsupported.

Eight-CPU run `qemu-run.Mn8ut5H3` passed the five-boot mirror matrix, including
actual replay-vector dispatch of native and byte-swapped exchange records.
That diagnostic checks both names after each swap and restores their original
identities; it is not itself crash recovery.

`091a535` adds an internal mount exchange bridge and extends the existing
private power-loss harness. Two files with distinct contents in separate
directories are created and exchanged after TXG persistence is frozen, then
the ZIL is explicitly committed. The runner kills only its own VM without
guest close/export. Recovery uses copies of these disks; the captured crash
images and original baseline remain unchanged.

Two-CPU run `crash-run.1kwfpXkV` passed staging, recovery, and fresh verification.
Recovery observed `ZIL_REPLAY_NEEDED=1`, deliberately rejected replay, then
injected a later SETATTR failure after real namespace callbacks had run.
Retry recovered both exchanged payloads. Fresh verification observed no replay
needed and the same payloads. Independent host `zdb -bcc` reported no leaks.
This run retries partial replay in one recovery VM; a restart between partial
replay and retry is a separate test, not established by this result.

Logs and input hashes are retained in `dispatch/` and `crash/`. Full artifacts
are under `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
The builds reuse upstream object inputs and selected patched AROS libraries.
These results do not establish physical power-loss behavior, all record types,
all exchange topologies, or opposite-endian pool compatibility.
