# Interrupted CLEAR recovery: one emulator flush boundary

Production engine `shared-engine.VpiTT74c` and CLI `mmake-cli.JZ90tGIt`
are unchanged. Only the disposable test client and QEMU controller are
extended. All three guests use eight CPUs, explicit dos-posix-ata runtime
prerequisites and source-built flush-capable ATA overlay
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/ata-capacity.6a3qpi9h`.
This is not a stock-nightly or physical-DUT qualification.

## Exact boundary

The CLEAR client first completes ordinary damaged-import refusal, readonly
recovery cycles, verbatim authorization checks and actual `clear -nF`
assessment on the recent-damage mirror fixture. Immediately before its
`clear -F` call, it prints a unique readiness marker and waits for Return
on its console input. This is a test-client barrier, not an instrumented
engine or a timing delay.

With `--clear-ready`, the controller boots its private paused guest without
an early storage breakpoint, waits for that marker, arms `flush_to_disk`
on member 0's qcow2 -> blkdebug -> file path, then sends Return only through
that guest's QMP connection. At the suspended I/O it checks that CLEAR has
not returned, revalidates the child PID against the exact ISO command line,
and sends SIGKILL without guest shutdown, export, drain or breakpoint
resume. Existing import-cut behavior remains unchanged without this flag.

## Cut, recover, independent cold read

- Cut run `native-import-run.lhHWfVMQ` reaches the intended member-0 flush
  before CLEAR returns. The retained QMP transcript records the input
  barrier, breakpoint setup, Return and kill. Both original backings are
  unchanged. Full-disk comparison finds changed guest-visible content at
  offset 140509696; this is not merely qcow2 bookkeeping. Both overlay
  containers pass `qemu-img check` without repair.
- Those interrupted overlays are flattened to new immutable test backings
  `qemu-run.clear-cut.xmkQJ8q8`. Fresh run `4Qz8KurK` passes ordinary import
  refusal, readonly recovery, actual `clear -nF/-F`, new-file flush/export
  and ordinary exact readback. It changes only its new overlays, not the
  captured interrupted backings.
- The completed recovery overlays are flattened to
  `qemu-run.clear-cut-repaired.KTpzJcSo`. A third guest `onH3KJXu` performs
  ordinary readonly import without recovery flags. Original and new files
  match exactly, writes are refused and export passes. Both backings and
  reader overlays remain unchanged. Extracted members additionally pass
  independent host `zdb -e -bcc`, reporting no leaks.

This proves recovery across **one selected emulator flush boundary**. It
does not prove every transaction stage, either-member interruption symmetry,
loss of host page cache, torn physical sectors, all topologies or real
device power-loss behavior. Captured overlays/backings and complete run
logs remain external; completed generated ISOs may be pruned after hashing.

## Replay

Use the recent-damage native fixture with `AROS_TEST_IMPORT_REWIND=1`,
`AROS_TEST_IMPORT_REWIND_NORMAL=1`, `AROS_TEST_IMPORT_REWIND_MODE=commit`,
`AROS_TEST_CLEAR_REWIND=1` and `AROS_TEST_IMPORT_REWIND_CUT=1`.
Supply the explicit production artifacts and flush-capable runtime above.
The runner enables the console barrier and controller `--clear-ready`.
Manager, GUI and assessment-only combinations are rejected for this cut.
Recovery uses fresh copies of the cut overlays with CUT disabled; the last
reader uses mode `cold`, without CLEAR/normal flags, over repaired copies.
