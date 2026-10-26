# Interrupted writable recovery

Production engine `Rw1LBfue` and tools `xSOZUKNQ` remain uninstrumented.
Only the disposable test client emits a marker before its writable recovery
request. The host runner starts its own eight-CPU QEMU paused, arms a block
debug breakpoint, then boots it. The helper verifies the PID command line
contains this run's exact ISO before any signal. It refuses a cut before
the writable-request marker or after completed recovery.

## Failed qualification attempts retained

- `QLEVkZ6b`: pwritev_done breakpoint reached; QMP stop waited on the paused
  I/O and timed out. The private QEMU was subsequently killed explicitly.
  The helper no longer requests stop/drain after reaching its breakpoint.
- `OwIotIN4`: direct cut at pwritev_done succeeded, but qemu-img compare found
  guest-visible bytes unchanged. This is not accepted as partial persistence.
- `c97Dk0ws`: no flush breakpoint before guest completion with baseline ATA.
- `Cp5pyv6n`: flush-capable ATA alone still did not expose the breakpoint;
  the filter above qcow2 was at the wrong event layer. Neither no-hit run
  counts as an interrupted-recovery pass.

QEMU block/io.c sends the flush_to_disk event to the primary child after
the driver's flush-to-OS step. The final graph is qcow2 -> blkdebug -> file,
not blkdebug -> qcow2 -> file. See the upstream implementation:
https://raw.githubusercontent.com/qemu/qemu/master/block/io.c
and the retained QMP greeting for the actual local emulator version.

## Cut, recover, cold verification

`xKyI7e5j` uses the explicit dos-posix-ata profile: alpha18c5, selected DOS
and posixc prerequisites, and source-built ATA overlay `6a3qpi9h` with the
cache-flush patch. This is not stock-nightly or hardware qualification.
The two read-only recovery cycles finish first. At the first member-0
flush_to_disk breakpoint during writable recovery, the helper sends SIGKILL
only to its verified private QEMU, without guest export, breakpoint resume,
or host-side QEMU drain. The import request has not returned. Both original
corrupted backings remain unchanged. qemu-img compare reports an actual
guest-visible mismatch at full-disk offset 140509696, not merely different
qcow2 bookkeeping. The trace and failed attempt logs are retained.

The cut disks are converted to fresh full raw images in
`qemu-run.recovery-cut.qPrgUNWM`. New guest `uEZYT1Zk` without blkdebug passes
ordinary-import refusal, two normal read-only recovery cycles, writable
normal recovery, new-file Flush/export and exact ordinary-import readback.
The captured cut-disk backings remain unchanged; both new writer overlays
change.

A third guest `dXxOCFNC`, over new conversions of the recovered writer,
performs ordinary read-only import with no recovery flags. Original and
post-recovery files match exactly, writes are refused and export passes.
Both cold backings and reader overlays remain unchanged. This rules out
dependence on the interrupted or recovery guest's memory.

This covers one member-0 flush boundary on the recent-root mirror fixture.
It does not prove every interrupted import stage, both-member atomicity,
all topologies, loss of host page cache, or physical power-loss durability.
The harness intentionally requires a cut marker and changed guest-visible
data; ordinary successful completion cannot stand in for interruption.

Replay cut: set `AROS_TEST_IMPORT_REWIND_CUT=1`, normal=1, manager=1 and
mode=commit with the recent corrupted fixture and explicit flush-capable
runtime. Subsequent recovery uses the captured images without CUT; final
verification uses mode=cold over repaired images. Keep all source backings
immutable. Runner admission tests also pass: unrelated paths and system
filesystem artifact roots are refused without creating guest images.
