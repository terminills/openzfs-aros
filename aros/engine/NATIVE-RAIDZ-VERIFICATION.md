# Native RAIDZ verification — incomplete

The fixture runner `test-qemu-native-raidz.sh` creates three new 512 MiB GPT
hardfiles under a fresh `build-aros/native-raidz-run.*` directory. Each has a
256 MiB ZFS partition; ATA units 0, 1 and 3 hold the disks and unit 2 is the
readonly CD. No existing disks, host devices or DUT access are used.

`OpenZFSQEMUPoolTest DISPOSABLE-RAIDZ1` constructs an upstream RAIDZ vdev with
three children and nparity=1. It retains writable native extent bindings,
rather than extending the legacy raw-open fallback. The historical pool name
`qemu_mirror` and text payload remain for shared DOS test compatibility;
neither describes or selects this fixture's topology.

The runner requires all existing pool/DOS tests to pass and verifies bytes
outside the GPT data partitions unchanged. Only then does it publish the
created-image hash manifest and success marker accepted by the importer.

The import runner accepts `AROS_TEST_NATIVE_LAYOUT=raidz1` and members `both`
(all three) or `01` (missing unit 3). It submits all three explicit native
paths, withholds the missing disk from QEMU and hash-protects that overlay.
Writable/cold-readonly modes retain the existing original-image checks.
Healthy and missing-member writable imports have passed on the private QEMU
build below. A separate cold boot verifies the degraded write persisted.
These results do not establish acceptance on other emulator builds or hardware.

## Evidence

- `native-raidz-run.aH0p2SC0`: initial fixture failed spa_create with errno 6.
  Inspection found the old test-only raw-open fallback admits only units 0/1.
  Fixture was changed to use the retained-binding path instead; production
  fallback permissions were not broadened.
- `native-raidz-run.j6bqhDKu`: revised build
  `native-raidz-bound-fixture/OpenZFSQEMUPoolTest` created the RAIDZ pool and ZPL
  dataset, then encountered repeated primary-channel ATA DMA timeouts during
  the wider test sequence. Private QEMU stopped after repeated errors; failed.
- `native-raidz-run.rX8v3fw1`: independent fresh-disk repeat of the same binary
  reproduced the timeout after successful creation. Logs show write command
  0xca, device status 0x58, bus-master active with IRQ count no longer advancing;
  timeouts occur on units 1 and 0. Private QEMU stopped after recording block
  statistics. Not a successful export or usable acceptance fixture.

Build logs: `native-raidz-fixture-build.log` and
`native-raidz-bound-fixture-build.log` under `build-aros`, both exit 0.
Source and binaries are recorded by each runner. The next task is to diagnose
the command/interrupt completion failure using this isolated reproducer.
These initial runs did not establish native RAIDZ acceptance.

## DMA investigation and fixture completion

- Private ATA sequencing candidate `ata-dma-order.0IPNC0RW` starts disk DMA
  after command submission. Build passed, but `native-raidz-run.715ni0JF`
  reproduced the timeout. Rejected as a fix; baseline driver unchanged.
- `8nr17bwQ` retained targeted host IDE/DMA trace. The request enters QEMU's
  DMA path; this alone does not discriminate host I/O latency from callback
  delivery. No completed interrupt/driver root cause was established.
- `QykSPCxB`, using the existing private worker-instrumented QEMU with explicit
  threaded disk AIO, completed guest tests. Task notifications split the PASS
  marker. Raw serial was retained, and removal of only complete identified
  notification records reconstructs it. The old runner exited 1 after manual
  shutdown and did not publish a fixture-success manifest. It is not silently
  reclassified as a passing runner.
- `iDQYxo9z`, untraced distro QEMU 10.2.1 with threaded AIO, reproduced the
  timeout. Stopped after repeated errors. A QEMU-version-only fix is unproven.
- `IMI11cSb`, private worker QEMU with all tracing disabled and threaded AIO,
  passed the corrected runner (exit 0), complete guest RAIDZ/DOS sequence and
  outside-partition hashes. This is a usable fixture, not proof that the
  recurring timeout in other emulator builds is repaired.

Runner now records QEMU version/hash and disk-AIO selection, supports targeted
IDE and optional private worker tracing, and parses interrupted serial markers
using the same bounded notification removal as the mirror test harness.

## Authenticated service import

- `native-import-run.wFDtbmk3`: installed QEMU failed with repeated ATA
  timeouts during cold import. Stopped after recording block statistics;
  original fixture hashes unchanged.
- `native-import-run.rHm95I81`: same service/ATA binaries and fixture, private
  worker QEMU with tracing disabled, exit 0. Healthy writable RAIDZ1 import,
  DOS file creation/write/flush/close, export, readonly reimport and exact
  readback, followed by two readonly import/DOS cycles. Original backings
  unchanged. This verifies the service path on that emulator build only;
  it does not repair or explain the installed-emulator timeout.
- `native-import-run.O6gp1NDu`: missing ATA unit 3, private worker QEMU,
  writable degraded RAIDZ1 import, DOS write/export/reimport and readonly
  cycles passed. All original raw images and the unattached unit-3 overlay
  retained their recorded hashes.
- `native-import-run.L21gZTOI`: independent cold boot using new overlays over
  O6gp1NDu, still missing unit 3. Guest reported `persisted=1` and completed
  two readonly import/DOS cycles. Runner passed all original raw, parent
  overlay and new readonly-overlay hashes. This is persistence evidence,
  not merely a read from the writing service's cache.
- `native-import-run.GPPyshYM`: all three members attached, independent cold
  boot over healthy writable run rHm95I81. Persisted-file verification and
  two readonly import/DOS cycles passed (runner exit 0); original raw images,
  parent writable overlays and new readonly overlays all matched hashes.
