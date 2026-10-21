# Native pool policy properties

## MMP qualification boundary (2026-10-02)

The typed Manager pool.set path now accepts `multihost` (`7ae82ad`), using
upstream SPA validation and the same native identity as the real CLI ioctl
path. Neither path creates a host ID or bypasses authorization, read-only
policy, or upstream MMP protection.
The upstream engine's MMP thread runs on the native platform, not a replacement
heartbeat implementation.

Opt-in `AROS_TEST_STARTUP_MMP=1` in the cold-start fixture creates only a
disposable 128MiB RAM file-vdev, enables multihost using packaged `zpool`,
and reads its four labels directly. Exploratory `startup-cold.WACKxfk5`
exits zero but its observer could also accept normal TXG records. This is
not the discriminating heartbeat proof. `ca3bb40` excludes sequence zero,
which ordinary uberblocks use (`module/zfs/uberblock.c`); MMP heartbeat
writes start at one (`module/zfs/mmp.c`).

Strict run `startup-cold.6Vz8cDIx`, fixture `c41e2b5`, production bundle
`native-bundle.hjIZZ7kS`, exits zero with a warning-free fixture build.
It observes nonzero-sequence records at timestamp 1790905468/sequence 1 and
1790905469/sequence 1, then disables multihost and destroys the disposable
pool. The full identity/cache/import/handler/readonly cold regression and
source backing hashes also pass. Evidence is external
`mmp-heartbeat-strict-cold-native-20261002.log` and the named run directory.

Missing-hostid command qualification `60ecba3` passes in
`startup-cold.MU3E92E2` with host exit zero and warning-free fixture build.
Both native boots report identity zero despite a conflicting environment
variable. Packaged `zpool set multihost=on` returns 1 with the exact upstream
`requires a non-zero system hostid` diagnostic; independent property readback
remains `off`. The complete cache/import/handler cold regression also passes.
Log: external `mmp-no-hostid-cold-native-20261002.log`. This tests libzfs's
front-door check, not a raw-ioctl bypass of that check.

Concurrent-owner exclusion passes in `mmp-contend.FHISrNmh`, using production
bundle `native-bundle.srNagCbq` and engine fix `690ef9a`. Two private native
QEMU guests use distinct protected host IDs and shared disposable raw copies
with direct I/O. Owner 51a20519 imports and enables multihost; contender
51a20520 requests forced import and receives the upstream diagnostic
`pool is imported on host` identifying 51a20519. Its subsequent pool list
confirms absence locally while owner liveness continues. Original fixture
hashes remain unchanged. Log: external `mmp-contend-native-20261002-3.log`.
The force-import adapter now accepts upstream ZFS_IMPORT_ANY_HOST; this
bypasses historical host ownership checks, not upstream active MMP exclusion.

Retained failures: `mmp-contend.upOlhOEl` exposed a slave-only ATA fixture
topology problem, corrected with private scratch masters in `29c4220`;
`mmp-contend.z7snjHUq` exposed rejected ANY_HOST flags, fixed in `690ef9a`.
Neither is counted as successful MMP qualification.

Stopped-owner recovery also passes in `mmp-contend.Gkz4zs1s`, fixture
`32a4cb9`, same production bundle. After repeating active-owner refusal,
the runner terminates and reaps its two exact private guest PIDs without
exporting. A fresh guest with host ID 51a20520 imports the stale-owned pool,
lists it, syncs, exports, and verifies it is no longer locally imported.
Host exit zero, warning-free fixture build, unchanged original backing
hashes. Log: external `mmp-recovery-native-20261002.log`. This is guest
termination recovery, not physical power-loss or storage-cache durability.

Zero-ID import refusal passes in `mmp-contend.DODLUWuk`, fixture `ac99868`,
same production bundle. Between owner termination and identified recovery,
a fresh guest with queried engine identity zero attempts forced import.
It receives `system's hostid is not set`, returns 1, and independently
confirms the pool is absent locally. The identified recovery guest still
imports/syncs/exports successfully afterward. Host exit zero, warning-free
fixture build, original backing hashes unchanged. External log:
`mmp-nohost-recovery-native-20261002.log`. The upstream import load path
checks spa_hostid independently of libzfs property validation. Its user
diagnostic in that historical bundle still names zgenhostid; see the native
guidance qualification below for its replacement.

Raw property-ioctl zero-ID refusal passes in `mmp-contend.Sx0SzaHn`,
fixture `340b5e6`, same production bundle. A first native zero-ID guest
imports the ordinary pool and sends ZFS_IOC_POOL_SET_PROPS with multihost=1
directly through the authenticated service transport, bypassing libzfs
validation. Transport succeeds; engine returns ENOTSUP; independent packaged
zpool get returns off. The guest exports, and the complete active-owner,
zero-ID import refusal and identified recovery sequence passes afterward.
Host exit zero and original backing hashes unchanged. Existing pthread
macro redefinition warnings are retained in raw-build.log; fixture link
build.log is empty. External log: `mmp-raw-property-native-20261002-2.log`.

Retained `mmp-contend.SjOW9dIM` passed its first raw guest but the host runner
failed with unexpected EOF (exit 2) after its script was edited during
execution for dependency provenance. This is a harness error, not a complete
pass or a storage regression. The unchanged committed rerun above is the
full result. Do not edit a running shell script in place.

I/O-stall suspension passes in `mmp-contend.sDZzAif4`, fixture `dbbca7d`,
same production bundle. QMP applies and queries a 64 byte/second write
limit on both disposable native ATA members after import/heartbeat startup.
Independent packaged zpool status reports SUSPENDED and the specific reason
`multihost writes failed or were delayed`. The host runner exits zero,
releases throttles, and terminates its private guest without forced cleanup.
Original backing hashes match. External `mmp-stall-native-20261002-4.log`,
stall-control.log, stall-serial.log and stall-release.log retain the sequence.
This proves suspension under delayed writes, not recovery via zpool clear,
absence of all pending writes, or physical device/cache failure behavior.

Retained stall fixture failures: `2oQx2Wsj` put blkdebug above the raw event
emitter, so no write was held and the guest deadline failed. `0GCy1JlC`
observed real MMP suspension but the host incorrectly required CE_WARN on
serial (it goes to service stderr), then QMP cleanup hung. `JEFiAJwK` held
a write but guest/monitor progress stopped; bounded exact-child cleanup
terminated it. These are not full passes. The successful throttle-based
fixture avoids breakpoint suspension and uses no patched engine behavior.

Recovery after delay removal passes in `mmp-contend.LCIQ03zn`, fixture
`259211a`, same production bundle. After the MMP-specific suspension,
QMP removes both write limits and independently reads back zero limits.
Packaged zpool clear succeeds, sync succeeds, status is ONLINE, multihost
remains on, export succeeds, and subsequent list confirms no local pool.
Host exit zero and original backing hashes match. Evidence: external
`mmp-stall-recovery-native-20261002.log`, stall-recovery-control.log and
stall-serial.log. No other host imports during this recovery; refusal to
clear after a different host takes ownership remains a separate test.

Typed API qualification `mmp-contend.vPruipFH`, fixture `4938759`, production
bundle `native-bundle.6X2a9Uh6`, exits zero. The authenticated property API
rejects invalid spelling, sets on/off with independent CLI readback, and
refuses on at identity zero. The raw-ioctl refusal, active-owner exclusion,
zero-ID import refusal and stopped-owner recovery all pass afterward.
External log: `mmp-typed-mode0-native-20261002.log`.
Concurrent separate stall fixture `mmp-contend.vUk1FrE2` failed its off
readback at line 92 before fault injection. The API returned zero, but that
test did not yet distinguish command failure from unexpected output. This
failure remains unresolved; `d3db660` adds discriminating diagnostics.
Diagnostic rerun `mmp-contend.Rws9xMqx` exits zero with the same production
bundle: typed on/off command return zero and exact 3-byte on / 4-byte off
readbacks, then MMP-specific suspension and clear/sync/ONLINE/export recovery
pass. Original backing hashes match. External log:
`mmp-typed-stall-retest-native-20261002.log`. This successful rerun does not
explain or erase the earlier intermittent failure.

Repeated transition fixture `8cb889a` reproduced a discriminating failure
in `mmp-contend.DSHniOsd`, cycle 5/16: the readback command returned zero,
IoErr was 333, and the output began `C:zpool: Error ` rather than a property
value. This is not evidence that SPA retained the old property. The capture
was truncated to 15 bytes; `b48fc3d` expands it to retain the complete error.
The source of the CLI/launch failure remains unresolved; no OS fix is claimed.
Subsequent unchanged-library runs `mmp-contend.XA5eUDqR` (16 cycles) and
`mmp-contend.SajpjIyC` (32 cycles) both exit zero, with each typed on/off
independently read back by packaged zpool, then full suspension/clear/sync/
ONLINE/export recovery and original backing hashes passing. External logs:
`mmp-typed-stress-native-20261002.log` (failed),
`mmp-typed-stress-native-20261002-2.log`, and
`mmp-typed-stress32-native-20261002.log`. These do not resolve the failure.

Actual shared physical storage remains unqualified. MMP is experimental;
these software tests are not a production data-safety certification.

The host-identity handshake bundle `native-bundle.tVWAy5JT` reproduced the
CLI failure in `mmp-contend.V6tsrQBj` at cycle 4/4. This time the full output
is captured: `C:zpool: Error 333`, SystemTags return 0, IoErr 333. The preceding
typed off request returned errno 0. The runner exits 1 before owner contention;
it does not qualify active-owner exclusion or prove a property failure.
Log: `hostid-handshake-mmp-20261002.log` in the external artifact root.

Private diagnostic overlay `loader-overlay.jwjssdkE` traces loader policy and
RunCommand entry, non-ELF rejection, allocation failure and entry return.
`mmp-contend.QPOYrISr` passes 32 cycles/64 readbacks and suspension/clear
recovery. Failure-only overlay `loader-overlay.vgpr1YY5` removes successful
launch tracing and disables loader-policy tracing; `mmp-contend.ZCuqc7aL`
also passes the same sequence. Both exit 0 with unchanged original hashes.
These tests neither reproduce nor fix Error 333; changes to timing and binary
layout remain possible confounders. No installed OS modules were replaced.
Logs: `mmp-launch-probe-runtime-20261002.log` and
`mmp-launch-quiet-runtime-20261002.log`. The first private diagnostic build
failed on a missing SysBase declaration; that failure is retained separately.

The same failure-only overlay and production bundle pass the full multi-host
sequence in `mmp-contend.2vu2SmyD`: raw/typed zero-ID refusal, 32 typed cycles,
active-owner exclusion, missing-ID import refusal and stopped-owner recovery
with sync/export. Runner exit 0, original backing hashes unchanged; log
`mmp-launch-quiet-contention-20261002.log`. This qualifies that combination,
not the original uninstrumented runtime or resolution of the intermittent error.

`e1271ab` adds a narrower private SetIoErr probe: it emits the task and caller
only for value 333. Overlay `loader-overlay.FSvyv6lA` keeps the original
RunCommand object; its LoadSeg object is byte-identical to the original
`loader-overlay.L7ctzwkQ` (SHA256 prefix `00576ed39a97`). The original IoErr,
SetIoErr and rebuilt probe all address Process.pr_Result2 at offset 0x1b0.
This rules out that particular structure-offset mismatch, not all ABI faults.
Runs `mmp-contend.sW3Pt9w1` (32-cycle suspension/recovery) and
`mmp-contend.oxrl4oOo` (32-cycle full multi-host sequence) exit 0, preserve
original hashes, and never hit the 333 probe. Logs are
`mmp-ioerr-runtime-20261002.log` and `mmp-ioerr-contention-20261002.log`.
The error origin remains unknown: a non-reproduction cannot identify its
caller or rule out direct Result2 writes, timing or binary-layout effects.

Native provisioning guidance (`b253c51`, fixture `53446bd`) passes in
`mmp-contend.Wx1sFcKJ`, bundle `native-bundle.yTyqbC5W`, CLI build
`cli-link.JUneYzbt`. The zero-ID import refusal explicitly names
`C:OpenZFSHostIdentity <8-digit-hex-ID> SAVE`, SYSTEM and service restart,
and contains no zgenhostid reference. The full raw/typed zero-ID refusal,
typed on/off readback, active-owner exclusion and stale-owner recovery
sequence exits zero with original backing hashes unchanged. External log:
`hostid-guidance-native-20261002.log`. probe-link now recompiles and hashes
the affected zpool main and libzfs pool objects rather than using cached
probe objects. Non-AROS diagnostic text is unchanged.

Operation 32 (pool.set) is now an authenticated native worker operation,
not a shell command or a placeholder. It requires root/config privilege,
a pool name, exactly one `property=value` argument and confirmation. Force,
dry-run, recursive and extra options are rejected before execution. The
legacy unauthenticated route does not perform writes. Read-only imports
refuse mutation with EROFS, even when the service supports writable pools.

The current integrated set is comment, delegation, listsnapshots, failmode,
ashift, dedup_table_quota, bootfs, cachefile, multihost, and upstream-valid user properties. Types and
enum spellings come from the upstream property table; numeric conversion
uses upstream libzutil. Ashift gets the libzfs range check. DDT quota
supports `none` (0) and `auto` (UINT64_MAX), rejects numeric zero and accepts
size suffixes. `spa_prop_set` performs upstream validation and transactions.
Suspended pools refuse ordinary writes; failmode retains upstream's special
in-core-change/EIO behavior, not yet exercised by a suspension fixture.

Pool user-property reads now use upstream spa_prop_get. Unlike filesystem
user properties, assigning an empty pool user value removes it upstream.
Read-only and create-only properties are invalid; other unfinished pool
properties explicitly return ENOTSUP. Compatibility files,
version upgrades and automatic device
actions still need integration. This is not a claim that every pool property
or its associated host facility is complete. Long values still face the
management API's bounded fields; lookup returns an error rather than silently
truncating them.

Explicit cachefile paths and `none` use upstream configfile selection and
serialization, separately from MOS properties. Empty values select a protected
native ENVARC default when available, otherwise fail; no Unix fallback occurs.
Default create/destroy/import/reset and graphical Manager reset have separate
two-boot qualification, including serialization, no implicit service-start
import, readonly restored data and unchanged pool-member hashes. Native CLI explicit
cache create/import and Manager set operations
are qualified, including cache-based import across two QEMU processes using
a protected SFS archive. See `../cli/WAIT.md` for exact run IDs and remaining
private-runtime and physical-durability limitations.

`cd3ab51` adds bootfs metadata management. Typed requests enforce libzfs's
same-pool name boundary before calling SPA; upstream checks the pool version,
bootable topology and ZPL dataset type. An empty value clears the property.
This neither installs a boot loader nor qualifies AROS booting from ZFS.
Native run `QRWVhV47` passed set/readback/clear, same-pool boundary refusal,
missing-target preservation and unprivileged refusal; the broader regression
exited zero and original fixture hashes were unchanged. Export/reimport and
read-only refusal added in `e2ec3cb` passed in `WpDStr0E` and exact shared-bundle
run `f5NPqWZe`; both exited zero with original fixture hashes unchanged.

The native manager routes pool.set through its authenticated worker and
confirmation flow. Capability text is generated from the same bitset as the
structured reply; stale hand-written operation lists and the obsolete eight
lifetime mounts claim were removed.

## Evidence and retained incorrect hypotheses

- `admission-service-run.pSU2D4ga`: failed test expected an empty pool user
  property to remain present. Upstream spa_sync_props removes that ZAP entry;
  corrected test now verifies removal before setting a new value.
- `admission-service-run.Espss5Y1`: write passed, verify failed because test
  wrongly equated a read-only imported pool with service-wide read-only
  capability. Both harness boots deliberately run a writable-capable service.
- `admission-service-run.O4V4ImeQ`: write passed, cold comment lookup failed.
  Independent zdb showed the new comment in disk labels. This old harness
  imports its original pre-write XDR config; spa_ld_parse_config obtains the
  comment from that stale config, not the MOS. Do not inject expected strings
  into the fixture and call it persistence proof.
- `admission-service-run.3epD2Trj`: corrected complete write/cold admission
  regression passes. Pool user property, enum/numeric values persist; root,
  read-only and request-flag refusals pass. The old-XDR comment remains absent
  as explicitly expected for this legacy fixture.
- `file-backing-run.RQOLWfzr`: actual native label-discovery import after a
  fresh QEMU boot reads the persisted comment, user property and DDT quota.
  Mutation refuses read-only, every member remains byte-identical, native
  filesystem payload/enumeration passes and independent zdb reports no leaks.
  fsync error-path and FAT dirty-retry tests pass in both boots.
- `native-gui-run.tybOof5K`: root GUI selected pool.set, confirmed
  `comment=Native UI pool policy`, received errno=0, then pool.get read it
  back. Screenshots 03-confirm.png, 05-set-completed.png and 06-readback.png
  retain the real interaction. The export preset was used only for root login;
  no export action was clicked. Private QEMU was shut down through QMP.

Final native regression `admission-service-run.hKFmAwW6` additionally passes
comment length and ashift=none checks before restoring ashift=12. Both native
write/verify clients report success. The final engine also passes real-label
RAIDZ1 cold import in `file-backing-run.WHGBNOY8`, with all property checks,
unchanged read-only members and independent zdb verification.

No DUT or user-hosted AROS session was accessed.

Follow-up: explicit feature@name=enabled and feature-state reads are now
integrated through upstream SPA. See POOL-CHECKPOINT.md for policy, warnings,
tests and the distinction between enabling a flag and completing its AROS
facility integration. The ordinary property allowlist above is unchanged.
