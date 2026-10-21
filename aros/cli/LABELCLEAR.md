# Native label clearing

Manager operation 29 has two explicit routes. File vdevs invoke the real
upstream `C:zpool labelclear` through the inherited-identity asynchronous
command worker. Exact `/AROS/<device>/<unit>` and
`/AROS/<device>/<unit>/partition/<position>` targets go through the
authenticated resident service. Both routes require confirmation, accept only
the Force flag, and reject argument/options/dry-run fields. The help states
that clearing ZFS labels is not secure data erasure.

The native route acquires the same writable whole-device or partition
reservation used by native pools before checking or changing labels. A member
of any currently imported pool is therefore refused even with Force. Exported
and potentially-active membership requires Force; destroyed labels do not.
The service reads all four complete 256-KiB slots, accepts only labels with a
nonzero GUID and known pool state, rejects inconsistent valid identities, and
zeros each valid label from `VDEV_SKIP_SIZE` onward. The two leading 8-KiB pads
remain untouched. L2ARC labels additionally clear the aligned L2ARC header.
Success requires a device flush and a full reread proving every cleared byte is
zero. A failure after any write is reported as partial and requires inspection
before retrying.

## Safety investigation

Upstream `vdev_is_active` uses `open(path, O_EXCL)` to decide whether Force can
override active membership. AROS documentation only promises O_EXCL with
O_CREAT. The suspected successful-open bypass was **not reproduced**:
baseline `native-import-run.pZy8lhyx`, using `cli-link.zhOWGskB`, refused the
forced clear of an active writable file pool. Full regression and backing
hashes passed, host exit 0. Source inspection then confirmed AROS POSIXC
`__oflags2amode()` explicitly rejects standalone O_EXCL. No upstream command
change was retained. Experimental compile/link outputs `cli-probe.mHNXGzoO`
and `cli-link.aPfOXG8i` contain a discarded defensive change and must not be
used as release artifacts.

## Integration regression

The disposable 128-MiB RAM-file pool fixture checks forced active-member
refusal through CLI and Manager, unconfirmed/malformed request refusal,
read-only imported member refusal, exported-pool refusal without Force,
and actual forced clearing after export. Independent import must then report
that no such pool is available before the exact backing file is deleted.
The preexisting upgrade/export/reimport and exact-payload checks run before
destructive label clearing. No user pool or physical disk is used.
The extended fixture independently reads all four 256-KiB label slots before
and after clearing. Each must initially contain nonzero label bytes; afterward
the 16-KiB leading pads must be identical and every remaining byte zero.

Service/Manager build: `source-engine.P2gN7Bsv/labelclear-service`, log
`labelclear-service-build-1.log`. CLI: `cli-link.zhOWGskB` (unchanged upstream
labelclear policy).

Initial GUI-worker run `native-import-run.yBB7HymL` executed the active/exported
refusals and forced clear successfully, then failed a fixture text expectation:
named import reports `no such pool available`, not the generic discovery
message `no pools available`. Host exit 1, no full-suite or final backing-hash
claim. Failure evidence is retained in the development test archive; the assertion is corrected.

Run `native-import-run.l2psGA05` (`labelclear-ui-native-2.log`) passes the
corrected worker regression, including read-only imported pool refusal,
full native suite and original backing-image hashes, host exit 0.

Run `native-import-run.UijNeEAf` (`labelclear-ui-native-3.log`) additionally
passes the independent four-label byte inspection: all label regions zero,
all leading pads preserved. Full suite and backing hashes pass, host exit 0.
Both successful runs are registered in the development test archive. These exercise Manager's
actual command worker, not interactive GUI clicks; physical durability and
stock-nightly integration remain unqualified.

## Native device regression

The first combined-suite attempt, `native-import-run.O6VofuET`, aborted in the
preexisting dataset-create path before operation 29 ran. It is retained as a
negative and establishes no label-clear result. The first narrow attempt,
`native-import-run.I4kIdpAb`, proved that the narrow harness launched its
client before importing `qemu_mirror`; operation 29 again did not run. The
harness now performs only authenticated import, health validation and the
label-clear sequence.

`native-import-run.XI0fdHy1` passed the focused native-device gate. It proves:

- Force cannot clear a member of the imported writable mirror (`EBUSY`).
- An exported member without Force is refused.
- Force clears and rereads four label slots on each of two exact native
  partitions.
- All eight 16-KiB pads are preserved and all eight 240-KiB label bodies are
  zero.
- A subsequent exact-path discovery reports no pool available to import.
- Independent host conversion verifies both GPTs, the bytes outside each ZFS
  partition, the eight preserved pads and the eight zero bodies; the original
  backing-image hashes remain unchanged.

The fixture uses disposable QEMU overlays only. It does not claim DUT, physical
power-loss, unrelated raw-device-user exclusion or interactive GUI proof.
