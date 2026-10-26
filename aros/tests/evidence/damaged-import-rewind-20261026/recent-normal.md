# Recent-transaction recovery fixture

Seed guest `LJRQCeuT` starts from disposable overlays of the original good
mirror. It creates/truncates a separate `recovery-seed` file eight times,
Flushes/closes it and invokes actual `zpool sync` after each generation.
It leaves the pool imported when the private QEMU harness stops the guest;
no export-time transactions are added. This is fixture preparation, not
physical durability or a recovery pass. Original backing hashes remain
unchanged and both overlays change.

Converted full disks are retained in `qemu-run.recent-recovery.gBEd5SD9`.
Extracted 256-MiB members begin at full-disk sector 4097. Host `zdb -l -u`
finds latest TXG 632 and preceding TXG 629. The preparation script uses
the actual latest MOS-root DVA offsets 0x3245000, 0x4227000 and 0x71c3000,
plus the 4-MiB label/boot area. The existing verified-block corruption
helper exclusively creates copies, flipping one byte in each of the three
4096-byte blocks on both members. Original source disks are preserved.

Corrupted copies are inserted into new full disks under
`qemu-run.recent-corrupt.zj_ccalr`. Host zdb rejects the latest state (exit 1)
and accepts TXG 629 (exit 0, no leaks). This validates the fixture, not the
exact TXG selected later by the native recovery algorithm.

Manager API run `Kerz4glc` with `AROS_TEST_IMPORT_REWIND_NORMAL=1` and
`AROS_TEST_IMPORT_REWIND_MANAGER=1` passes two ordinary-import refusals,
two non-extreme read-only recoveries with exact DOS content/write refusal,
then non-extreme writable recovery, flushed new file, export, and ordinary
read-only import/readback of both files. Root/non-root admission and parser
checks pass. Source backings remain unchanged; both writer overlays change.
Production engine is `Rw1LBfue`, actual MetaMake tools are `xSOZUKNQ`.

Actual upstream CLI run `fSCC81UQ` repeats the same preview/commit/readback
sequence with `zpool import -F` (no `-X`). Both writer overlays change while
their source backings remain unchanged.

Separate cold guest `zg18a27G` boots with new overlays over raw conversions
of the Manager writer disks (`qemu-run.normal-recovery-cold.lVLyZJNO`).
Ordinary read-only import, without recovery flags, reads the original and
post-recovery file exactly, refuses writes and exports. Cold backing and
overlay hashes remain unchanged. This is persistent recovery for the
specific recent-root mirror fixture, not all recovery/topology/corruption
cases, interrupted recovery, or hardware durability.
