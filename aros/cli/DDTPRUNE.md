# Native DDT pruning

The native ioctl transport admits the upstream `ZFS_IOC_DDT_PRUNE` packed
request. It rejects unrelated inline fields, pointers, configuration and
history; the upstream dispatcher owns schema, config privilege, pool state
and fast_dedup feature checks. This is the real upstream DDT implementation,
not removal of files or a synthetic success response.

Manager operation 28 executes exact `C:zpool ddtprune` arguments in the
inherited-identity worker. It accepts exactly one option: `percent=1..100`
or `days=positive-decimal`. Decimal is normalized before calling the upstream
CLI, whose parser accepts base-0 numbers. Day conversion is checked against
UINT64_MAX/86400. Confirmation is required; force, dry-run, extra arguments
and option combinations are rejected. Closing Manager drains the worker;
no asynchronous cancellation is claimed.

The native regression fixture populates four unique 128-KiB deduplicated
records, checks the upstream DDT count is at least four before and zero after
100-percent pruning,
and verifies all 512 KiB still read exactly. It also tests an empty-DDT age
request, seven invalid option strings, missing confirmation, and MuFS
non-root denial. Execution results must be recorded separately: the fixture
alone is not proof of a pass.

Build: `source-engine.P2gN7Bsv/ddtprune-service`, with existing upstream CLI
`cli-link.PTQ5X1YB` and private QEMU OS overlays. Not stock-nightly or physical
durability qualification. Age-selective populated pruning and cold reimport
remain separate tests; shared-reference and visual coverage are below.

Initial run `ak19HGuL` failed the fixture's exact-four-DDT-entry assertion:
upstream reported five entries after the four file records were written.
No prune command had run yet. The revised fixture records the actual count,
requires at least four entries, and still requires zero afterward plus exact
file bytes. The additional entry's origin was not established by this run.

Run `TpQYRNLe` reached real upstream pruning with exit 0, but still had five
DDT entries afterward, so it failed the effect gate. Upstream's prune walker
skips entries still in fast-dedup logs. The fixture now completes a real
scrub first: upstream scrub waits for the DDT log flush before its walk.
No success is inferred from the prune command's exit code alone.

Run `ae3rCqV2` completed the pre-prune scrub and accepted pruning but the
immediate count was ten rather than zero. DDT object statistics include log
entries, and pruning writes cleared physical records through the same log.
The fixture now completes a post-prune scrub as well before evaluating the
stable count. This run is retained as a failed effect check, not a pass.

The upstream test `tests/zfs-tests/tests/functional/dedup/dedup_prune.ksh`
also explicitly controls log flushing before comparing DDT counts (using
test tunables). Our fixture instead uses completed real scrubs and leaves
production tunables unchanged.

Run `DZozhqxQ` (`manager-ddtprune-native-4.log`) passes: initial count five,
zero after pruning and the post-prune flush, all 512 KiB exact, empty-DDT
days request, seven invalid inputs, no-confirmation refusal, and explicit
upstream permission denial for MuFS uid 77. The full native regression and
original backing hashes pass with host exit 0. Evidence is registered under
the development test archive; build log `engine-ddtprune-build-1.log`.

## Invalid-age recovery

Run `EfUDHGq6` (`ddtprune-recovery-native-1.log`) reproduced an upstream
state bug on a populated pool: `days=999999` returned invalid argument, then
`percent=100` returned "a prune operation is already in progress". The
upstream function had set `spa_active_ddt_prune` before validating the age
and returned without clearing it. The failed run is registered under the development test archive.

The repair validates unit, nonzero amount, percent range and age before
setting active state and before the empty-table shortcut. The regression
requires a valid prune after the rejected populated-table request; it also
requires rejection of that age on an empty table followed by a successful
valid empty-table request. Build explicitly recompiles `module/zfs/ddt.c`
before relinking the service; adapter-only relinking would retain the bug.

Run `QdVoputw` (`ddtprune-recovery-native-2.log`) passes both recovery cases,
the pruning/data/permission checks, the full native regression and unchanged
original backing hashes with host exit 0. Evidence is registered under
the development test archive. Build: `source-engine.P2gN7Bsv/ddtprune-recovery-service`, log
`engine-ddtprune-recovery-build-1.log`. Other kernel-side invalid numeric
branches and simultaneous prune races are not covered by this age test.

## Shared references

The extended fixture writes the same 512-KiB contents to two separate files
through DOS writes (not clone/copy shortcuts). After the first prune and log
flush, it requires at least four DDT entries remain and reads the duplicate
byte-for-byte. It then deletes that file, flushes, prunes the now-single-
reference entries, requires zero entries after flushing, and reads the
survivor byte-for-byte. This checks shared-reference retention as well as
pruning, rather than equating fewer DDT entries with correctness.

Run `VO2kvEM3` (`ddtprune-shared-native-1.log`) passes: five entries before
and after the shared prune, zero after duplicate deletion and unique prune,
and both exact reads. The full native regression and unchanged original
backing hashes pass with host exit 0. Evidence is registered under the development test archive.

## Native graphical Manager

Run `W4JPzK1V` (`ddtprune-gui-native-1.log`) uses the graphical private QEMU
and the actual Manager from `ddtprune-recovery-service`. The existing
`AROS_TEST_DIFF_GUI=1` pause point was reused after the DDT fixture, while
the writable pool was still imported. Selected Pools/ddtprune, entered
`qemu_mirror` and `percent=100`, disabled dry-run, reviewed and submitted
the native confirmation requester. The response displayed upstream
`exit=0, DOS=0, capture errno=0`.

`ddt-gui-confirm.png` and `ddt-gui-completed.png` retain the actual window;
this GUI request used the already-empty DDT. The earlier automated portion
of the same guest verified populated pruning, shared-reference retention,
invalid-age recovery and bytes. After closing this Manager, the full suite
continued and passed; the final inspection Manager was also closed, allowing
host exit 0 and unchanged original backing-hash checks. All evidence is
registered under the development test archive. No DUT or user hosted session was used.
