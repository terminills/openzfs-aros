# Native upstream pool and filesystem wait

## Native persistent host identity

Production service startup now reads `ENV:OpenZFSHost.prefs` using
iffparse.library before engine initialization (`1efa563`). The 42-byte
version-one FORM PREF contains a zero PRHD and OZHI with a big-endian
version and nonzero 32-bit host identity. It requires configured security,
SYSTEM ownership and protected parent/file metadata, rejects a final soft
link, and opens the already-verified lock. It does not read ENVARC directly,
generate an identity, or accept `ZFS_HOSTID` as a native-service override.
Absent preferences leave identity zero. Standalone bootstrap fixtures retain
their separate environment-based identity path.

External bundle `native-bundle.TKMO7z3U` passes two-process native QEMU
qualification in `startup-cold.WKoK0tbm` and `startup-cold.acPxH3xk`.
The engine reports saved ID `51a20519` on each cold boot despite conflicting
environment ID `11223344`. The latter run (`051af9e`) also rejects malformed
IFF, unsupported version, zero identity and foreign ownership, preserving
the input bytes and publishing neither service port. Restoring the valid
record then starts the service normally. Both runs exit zero.

`de0c524` adds the independent unprovisioned variant
`AROS_TEST_STARTUP_HOSTID_EMPTY=1` (requires `AROS_TEST_STARTUP_HOSTID=1`).
`startup-cold.iEBTg3zp` exits zero: both native boots report identity zero,
ignore the conflicting environment, and leave both ENV and ENVARC identity
files absent. The combined cache, startup-copy, import, DOS handler and
readonly tests also pass; original backing hashes match.

Evidence is under `/mnt/openzfs-artifacts/`:
`hostid-cold-native-20261001.log`, `hostid-rejection-cold-native-20261001.log`,
`hostid-empty-cold-native-20261001.log`, and the named build-aros run folders.
Subsequent provisioning work: `49c2cf0` shares the production reader with a
real iffparse writer. `startup-cold.gSwbCmf6` exits zero, comparing the writer's
output byte-for-byte with the version-one record before service startup.
`3bce6d6` adds `oz_hostid_settings_apply`: SYSTEM-only, protected parents and
files, explicit refusal while service ports exist, ENVARC publication first,
then ENV, using the existing transaction/readback helper. The returned mask
reports partial completion; it never claims cross-volume atomicity.
`startup-cold.X0ggeaAE` exits zero for provisioning, zero-ID refusal, and
running-service refusal. The service does not change its live identity.

Production bundle `native-bundle.S8nO1vVZ` includes the extracted reader and
startup refusal of a pending `OpenZFSHost.prefs.txn`. Run
`startup-cold.uEZIBokX` exits zero, verifies that refusal and subsequent
recovery, and repeats both cold phases with cache/import/handler checks.
Logs: external `hostid-codec-cold-native-20261002.log`,
`hostid-provision-cold-native-20261002.log`, and
`hostid-provision-rebuilt-cold-native-20261002.log`.

`d035239` adds the native `OpenZFSHostIdentity <8 hexadecimal digits> SAVE`
command, built by `bash aros/ui/build-hostid.sh`. It requires a nonzero ID,
does not silently generate one, and prints partial publication on failure.
Command-path run `startup-cold.PWzbrGNM` exits zero with warning-free command
and fixture builds. Invalid zero and missing SAVE return 20; valid SAVE
returns zero. Both boots report the saved identity and refuse a command
attempt to change it while the service runs. Full cache/handler regression
and backing hashes pass. Log: external `hostid-cli-cold-native-20261002.log`.
Distribution/UI follow-up: `ad414a4` includes the command in normal CLI
builds and staging; `9733d2d` wires Manager's Startup dialog directly to the
same provisioning API with explicit confirmation. First GUI run
`startup-cold.LbmpXIBE` independently verifies the saved ENV/ENVARC files,
but fails later because the test ISO omitted the packaged command (DOS 205).
This is retained as a failed run, not an end-to-end success. `4a183ed` fixes
the test mapping; `d944900` widens the ID entry after visual inspection.

Bundle `native-bundle.hjIZZ7kS`, CLI `cli-link.UrUCawpK` and the rebuilt
Manager pass `startup-cold.oybqyIPX` with host exit zero. Real GUI input saves
ID `51a20519`; after closing Manager the guest independently reads both
files, exercises the packaged CLI, then verifies persistent engine identity
and cache/import/handler/readonly behavior across two cold QEMU processes.
Backing hashes match; Manager and fixture builds are warning-free.
Evidence: external `hostid-manager-layout-cold-native-20261002.log`, and
`startup-cold.oybqyIPX/{confirm.png,saved-result.png,ui-actions.jsonl}`.
The screenshots preserve native framebuffer originals alongside PNGs.

MMP, crash-injection publication recovery, and broader provisioning
permission/failure qualification remain unfinished.

Authorization/partial-save qualification `715d9c9` passes in
`startup-cold.qx2DGVhE` (host exit zero). A real ordinary security.library
account receives EPERM from the API and return 20 from the packaged command;
independent reads show both records unchanged. A conflicting ENV transaction
then causes EBUSY after ENVARC publication: the API reports exactly
`OZ_HOSTID_SAVED`, readback proves new ENVARC/old ENV identities, and the
existing transaction remains intact. Removing only that fixture-created
empty transaction and restoring the test identity succeeds, followed by the
complete two-boot regression. This is a real DOS conflict, not a mocked write
failure, but is not a power-loss recovery proof. External log:
`hostid-permissions-cold-native-20261002.log`.

## Protected native default cache (in qualification)

`b87e591` selects a service-owned, canonical `ENVARC:OpenZFS.cache` default
only when ENVARC is a secured filesystem with a system-owned directory that
does not grant group/other write or delete access. Existing cache objects
must be protected regular files; direct child lookup refuses soft links.
The service retains the directory lock. Missing/untrusted defaults fail
without substituting RAM or Unix paths; explicit paths and `none` remain.

The engine resolves this location after libzpool initialization, so a cache
cannot populate the pool namespace before native device admission. CLI
create/import, CLI reset-to-default and Manager reset share the availability
check. `cache-default-guarded-20261001` builds, shared engine `a3pVMJqe` and
bundle `TUAJc2d8` stage successfully. Native regression and protected SFS
two-boot default-cache tests are running; no runtime success is claimed yet.
The latter deliberately leaves a cache in ENVARC for the next service start
and requires the pool to remain absent until an authenticated import.
Logs: external `cache-default-*-20261001.log`.

Explicit-policy regression `EE4TBb62` passes with host exit zero. First default
fixture `TCyAirfM` fails at initial import. Source audit of
`rom/security/secIsVolumeSecured.c` found its lookup uses volume/device names,
not assigns: checking `ENVARC:` cannot identify the secured SFS volume.
`358e7b5` resolves the canonical volume from the retained directory lock before
querying security.library, and adds failure-only guest log capture. The failed
run is preserved; the corrected default policy still requires runtime proof.

Further retained runs `VDeLORRW` and `NxEsKcXK` still refused the default;
the latter captured the actual unsupported-request diagnostic. Inspection of
security's registry population found native key-file entries are keyed by
DOS device name, not the displayed volume name. `059fca9` resolves the pinned
handler to that device identity, without relaxing ownership/protection checks.
Shared bundle `8A3OKk3O` run `vunb8ONf` passes default import/disable/reset,
no automatic import on the second service start, cached readonly import, and
restored DOS data. Its host archive-hash gate fails while member hashes match.
`561d437` adds a specific stale-default-cache removal assertion: disabling the
host cache is distinct from modifying readonly pool members. The retry retains
strict member hashes and exact DOS reads; its result is pending. Logs through
`cache-default-cold-native-20261001-5.log` preserve all attempts.

Retry `EmEAtjke` exits zero. Explicit stale-default removal is observed, all
default import/disable/reset/persistence/no-auto-import markers pass, restored
DOS data matches and readonly mutation is refused. Both pool overlays remain
unchanged across READ and original backing hashes match. The host SFS cache
archive intentionally changes. This qualifies default import/reset on protected
ENVARC; default create and graphical Manager reset still need dedicated tests.

Default creation follow-up: `dhBBaTOz` completes guest create/destroy and
both cold phases, but host verification rejects a result split by concurrent
security diagnostics (`RESULT na` / `tive startup cold pool PASS`). This is
retained as a failed harness run, not an end-to-end pass. Result lines now use
one kernel debug call rather than individually unlocked RawPutChar calls.
Retry `uwKDvn9d` exits zero, with a warning-free build: ordinary create without
a cachefile option serializes the protected default, destroy removes it, and
the existing import/reset/persistence/readonly restoration checks all pass.
Pool overlays remain unchanged across READ and original backing hashes match.
Evidence: external `cache-default-create-cold-native-20261001{,-2}.log` and
their `startup-cold.*` directories. Graphical reset remains unverified; the
optional `AROS_TEST_STARTUP_CACHE_GUI=1` fixture requires a real Manager reset
after disabling the cache and checks the serialized file after Manager exits.

Graphical reset is now qualified by `startup-cold.NpgQrfqY`, host exit zero,
using the same `8A3OKk3O` production bundle and private GUI-capable QEMU.
Manager Pools/set `qemu_mirror`, `cachefile=` passes its real confirmation,
authenticated worker and upstream property dispatch (`errno=0`). After closing
Manager the guest independently observes the serialized default cache. A fresh
QEMU boot verifies no automatic import, cached readonly import, exact restored
DOS data and mutation refusal. Pool overlay hashes across READ and original
backing hashes match. Build diagnostics are empty. The external run retains
`confirm.png`, `reset-completed.png`, raw PPM captures and `ui-actions.jsonl`.
Earlier GUI attempts `NS2j46Db`, `5mpHWBOC`, `Yzrnw4MA` are retained: first
binary lacked screendump, then fixture input setup/relative monitor events
prevented the UI action. They are not product failures or passing tests.
The optional fixture now enables the stock USB class scan, emulated tablet,
and private phase-specific QMP socket; no user session or DUT was used.

Combined noninteractive regression `startup-cold.GFuuYXcz` also exits zero:
default cache plus saved handler mountfile and protected ENVARC-to-ENV
PREPARE/copy startup. It includes malformed IFF and ordinary-user rejection,
service absence during preparation, imported-but-unmounted pool followed by
Mount activation, exact DOS file data and readonly mutation refusal. Both
READ member hashes and original backing hashes match; compile log is empty.
Evidence: external `cache-default-handler-copy-cold-native-20261001.log`.

Live trust revalidation `startup-cold.NgcByrbA` exits zero. After service
startup and cache disable, reset rejects a directory at the cache pathname,
a foreign-owned regular cache file, and a group-writable parent directory.
The directory and foreign file contents survive unchanged, no cache appears
under the unsafe parent, and the pool's cachefile property stays `none`.
After restoring safe metadata the regular reset succeeds, followed by the
combined handler/ENV cold sequence and unchanged pool-member hashes.
Evidence: external `cache-default-rejection-cold-native-20261001.log`.

Soft-link extension `startup-cold.McHlyhOU` exposed a real CLI failure: the
service's ELOOP refusal reached libzfs's unknown-error branch, which aborts
with SIGABRT instead of returning a normal error. The run timed out and its
serial backtrace is retained. `cd67127` maps native ELOOP to a normal path
error without relaxing the no-follow policy. CLI linking now rebuilds the
current error reporter; `c212ea2` restores required native directory defines
and probe version metadata, with input hashes. The first rebuild's missing
metadata error is retained in `cache-softlink-cli-build-20261001.log`.
CLI `ajRAf4CB` and bundle `NRvigvzL` build/stage successfully; native rerun
must prove target bytes unchanged, normal error exit, recovery and cold boot.

Post-fix `sAC8LKS4` returns the expected normal CLI error and preserves the
link target, but fails deleting the link. `LZgHkxvR` preserves the actual
DOS error 233 (`ERROR_IS_SOFT_LINK`): SFS's security path check rejects the
link before its link-aware delete routine. This separate OS defect is not
fixed here. `ca94d55` moves the link test to the last readonly phase and
retains link and target in the disposable archive; no cleanup is claimed.
It additionally captures stderr and requires the exact symbolic-link refusal,
so a generic readonly error cannot produce a false pass.

Run `startup-cold.9SngU3X3` exits zero with bundle `NRvigvzL`. Normal CLI
exit 1, explicit link diagnostic and byte-identical target are verified.
All directory/owner/parent rejection and subsequent default reset checks,
saved-handler Mount activation, protected ENV restoration, exact DOS reads
and readonly mutation refusal also pass across two QEMU processes. Pool
overlay hashes remain unchanged across READ; original backings match. Empty
fixture build log. External `cache-default-softlink-cold-native-20261001{,-2,-3,-4}.log`
and their run directories retain the full failed-to-passing sequence.

## Explicit pool-cache integration (2026-10-01, in qualification)

`7c57fb1` adds an actual cache serialization/export/removal/cache-based-import
test. Native run `IUFa5FbN` failed at cache reimport with "no such pool
available" after successfully writing and preserving the cache. Source audit
found that normal discovery attaches the explicit candidate-path admission
record, but upstream's cached-import branch did not. `7e88150` constructs a
bounded selection from the user-selected cache before TRYIMPORT and preserves
it for import; existing service-side identity/permission checks remain active.
Embedded admission metadata is discarded, never trusted. CLI build
`l0b3NoMm` links zfs/zpool/startup tools and recompiles the current importer
rather than selecting the stale probe object.

`6351cdb` wires Manager's explicit cachefile policy through the separate
upstream configfile/write path (spa_prop_set alone does not perform this
operation). An empty default path remains unsupported. Native service
`cachefile-manager-20261001` built;
run `xBbZMHUN` passed CLI cache import plus Manager disable/enable,
malformed-policy refusal and read-only refusal. The full writable/readonly
native mirror regression passed and both original backing hashes were
unchanged. Evidence: external `cachefile-native-20261001-2.log` and the run's
`serial.log`. This qualifies an explicit RAM cache within one boot, not
cold-boot persistent-cache recovery. New build artifacts and logs are
external; failure `IUFa5FbN` is retained unchanged.

Follow-up: CLI create/import now admit explicit absolute cache paths as well
as `none`; missing/empty policy still refuses the implicit Unix default.
Upstream SPA remains responsible for cache serialization. The native fixture
now requires cache creation at pool creation and regeneration during cached
import, property readback, and removal on disabling the cache. Service
`cache-create-import-20261001` built. Native run `W2oOOM30` passes all added
cache markers and the selected regression, with unchanged original images.
Shared engine `Lo3uaENA`, bundle `GByqNeIe`, exact-package run `q0uiJEmY`
also pass; staging negative-policy tests pass. Logs are external as
`cache-create-import-{build,shared,stage,stage-policy,native,bundle-native}-20261001.log`.

Two-boot cache qualification is in progress (`c4106f5`). First run
`startup-cold.dSgxzSBI` failed with ATA timeouts using the default QEMU before
cache qualification. The previous combined cold fixture documents a private
three-disk ATA-wakeup-fixed QEMU; retry uses that binary and the previously
qualified partition overlay. Retry `startup-cold.ez8gvExe` exits zero with an
empty build log: saved cache serialization in CREATE, cache-based readonly
import/export in a fresh QEMU process, and startup restore/mount/exact DOS
read all pass. SFS archive and mirror-overlay hashes match across READ, and
both original backing hashes match. This is private-runtime cold cache
qualification, not stock-runtime or physical power-loss durability. The
failed first run is retained, not evidence of cache corruption. Logs:
`cache-cold-native-20261001{,-2}.log`.

### Malformed cache refusal

`270b47a` replaces fatal pool-metadata accessors with checked reads for the
native CLI. `a301e9d` validates bounded vdev trees before trusted scan/platform
walkers. The native fixture changes a key in an otherwise serialized cache,
requires an ordinary error and absent pool, then imports the untouched cache.
`KRm35iHS` passes the missing-GUID case; expanded `EpTyXt0J` passes both the
missing-GUID and missing-tree cases, the full selected regression and original
backing hashes (host exit zero). CLI build `HR6lkgI8` is packaged in bundle
`lGvU04jY`; separate cold-cache run `LSccCcOB` exits zero, including saved
cache import, startup restore/mount/exact DOS read, unchanged archive/member
hashes across READ and unchanged original backing images. Logs are external as
`cache-invalid-{cli-build,native}-20261001{,-2}.log`,
`cache-invalid-stage-20261001.log`, and `cache-invalid-cold-native-20261001.log`.

## Boot dataset property (2026-10-01)

`cd3ab51` routes Manager bootfs metadata through upstream SPA with same-pool
name validation. Normal native service `bootfs-20261001` built successfully.
Initial run `WkpHhhnT` exited zero with original fixture hashes unchanged,
but only exercised baseline import/DOS: AROS_TEST_WHOLE_CLI_LINK was omitted.
It is NOT bootfs qualification. Retry `QRWVhV47` enables the CLI artifacts
and the set/readback/clear, invalid-target preservation and privilege tests.
The exact bootfs marker and the broader regression passed; host exit zero
and both original fixture hashes unchanged. `e2ec3cb` adds property persistence across export/reimport and
read-only mutation refusal, verified by run `WpDStr0E` (exit zero, original
fixture hashes unchanged).
Both use isolated COW members. Shared library `gc1snxKr` and bundle `h9hRuXPk`
built successfully; staging refusal-policy tests passed without changing inputs.
Exact shared-package run `f5NPqWZe` also passed with exit zero, both bootfs
markers and unchanged original fixtures (`bootfs-bundle-native-20261001.log`).
This verifies metadata persistence, not a ZFS boot loader.
The runner now refuses transport-test requests without
CLI artifacts and requires the specific bootfs marker. Logs are retained on
the external drive as `bootfs-native-20261001{,-2}.log`.

## Native cache-file prerequisite: unlink errors (2026-10-01)

Upstream spa_config_remove expects positive errno, including ENOENT, whereas
the libzpool adapter returned libc's -1. `7d53c0b` added errno conversion.
Actual isolated native tests `RvkOAint` and `3M5OIyoE` still failed: a missing
file returned EIO, including with the normal private stdcio overlay. Diagnostic
run `uQdl3wxo` proved caller errno=0 while DOS IoErr=205 translates to ENOENT=2.
`298f600` captures DOS errors directly after clearing both stale error channels;
the regression links the actual kernel.c adapter and removes only a newly
created RAM file, with no hard disk or network attached. Fourth run `SAPKwnv0`
passed the exact native marker and exited zero.
This prerequisite does not enable persistent pool caches. All attempts remain
on the external drive as `unlink-native-20261001*.log` and `unlink-run.*`.

## Simple view and pool wizard (2026-10-01)

`56b4aec` introduces IFF v2 view mode (fresh users Simple; legacy v1 Advanced).
`4c217bb` adds the switch without a separate backend. Native IFF run
`gHh5pvog` passed. Native GUI `6iVloq2p` passed actual switching, session-only
Use, Save to ENV/ENVARC and archive fallback; evidence is on the external
drive under `evidence-simple-mode-20261001`.

`dbac26b` adds plain-language Simple actions and a three-page pool wizard:
name/protection, exact member paths, review. It supports one stripe, mirror,
or RAIDZ group; Advanced retains multi-group and dRAID. The wizard uses the
existing typed validation, final confirmation and authenticated worker; it
does not create files, partition devices or guess safe targets. Native GUI
`MtI6tGNQ` verified empty-name and incomplete-mirror refusal, exact review,
final cancellation, and the preference regression. Evidence is under
`evidence-pool-wizard-20261001`; this first run has no service attached.
The final commit improves confirmation wording after that run. Rebuilt
`simple-wizard-final-20261001` built. Initial live test `khWSpD4U` failed
before boot: without ZVOL_TRANSPORT, the existing growth test references
`manager_permission_or_create` outside its conditional definition. The failed
log is retained. A retry enables that test configuration and uses the
`13bdb31` CLI/DOS persistence test. Retry `yjjzdDCw` completed with exit zero
and both original fixture hashes unchanged. Actual Manager wizard created
`wizard_created` from two blank 128-MiB files; independent upstream CLI verified
ONLINE mirror state, and an exact DOS payload survived export/reimport before
cleanup. This is same-boot file-vdev persistence, not power-loss durability.
`9f67589` subsequently fixes the harness helper guard; both configurations
cross-compile. `44ca74c` restores Simple mount's required DOS-name input;
`lwuuQNCs` visually verifies both inputs and readonly request preview, and
finishes the native preference regression. The live wizard binary predates
this mount-only correction.

Following the user's margin report, `de74557` removes the fixed action width,
sets 35/65 expanding columns and zero nested horizontal insets, and hides an
empty Simple field grid as a whole. The empty grid's maximum width otherwise
caps its parent result column. Checkbox rows also gain an expanding spacer.
The initial spacer-only run `OS8xY5Dw` retained the side gap (hypothesis not
sufficient); `KtJcl0gA` removed it with fields present but exposed the empty-grid
constraint. Final visual run `t2FsYfkh` shows aligned content in Simple and
Advanced. Logs/screenshots of all three attempts remain external.

## Manager creation-time limits (2026-10-01)

`b59f8e2` adds quota/refquota, reservation/refreservation and recordsize to
typed filesystem creation; volumes also accept reservation/refreservation.
`none` maps to zero only for space-limit properties. `dfec601` retains the
libzfs pre-ioctl power-of-two and absolute block-size bounds for typed callers;
upstream feature, quota, property and authorization checks remain in effect.
Normal `create-limits-validated-20261001` built successfully. External private
QEMU `ZBKw8ep6` has passed the five exact property readbacks, none handling,
and invalid recordsize refusal with no dataset left behind. Its broader
regression completed successfully with original fixture hashes unchanged.
Shared `UdM1YeTJ`, rebuilt Manager `create-limits-manager-20261001` and bundle
`XuqyItnd` built/staged externally with manifest and refusal-policy checks
passing. Exact-package runtime `jfINTPvV` completed successfully with original
fixture hashes unchanged, including upstream reservation failure cleanup,
same-name retry, real vdev capacity growth and permission refusals. The later hello-text
correction for temporary pool creation does not change operation semantics
and is not included in these binaries.

## Manager vdev flags and storage retention (2026-10-01)

`9e4ba25` wires authenticated online `expand=on` and offline `temporary=on`
to upstream vdev flags, preserving leaf-GUID, root, readonly and suspended
checks. Normal service `vdev-flags-20261001` built successfully. Private QEMU
`native-import-run.2KqKzXYN` completed with both original fixture hashes unchanged:
two malformed options refused, temporary offline produced DEGRADED, export/
reimport restored ONLINE, and explicit expansion was accepted with DOS readback.
This does not prove capacity growth. `fdbd671` adds actual backing-file growth,
capacity comparison and payload checks across export/reimport. External run
`native-import-run.ssaepoI6` hit the 600-second harness deadline while still
processing commands; this was not a crash verdict. Retry `l95fYdkE`, with a
2400-second bound, completed with exit zero and unchanged original fixture
hashes. Usable size increased from 117440512 to 251658240 bytes, persisted
across reimport, and the 1-MiB payload matched. The same run passed both new
flags' read-only and unprivileged refusals (two each). This is file-vdev
growth, not physical hotplug or automatic device expansion qualification.

Following the user's disk-space escalation, 33 inactive qemu-run directories
were checksum-verified and relocated to
`/mnt/openzfs-artifacts/build-aros`, with links at their
original paths. Logs and screenshots are preserved. The 30-directory batch log
is `OpenZFS-archive/retention-20261001.log`; the other three are VCRcjgEx,
ZRSbtCZW and ZznKptrK. This is not a complete cleanup of the build tree.
`4ac9520` adds a tested 100-GiB host reserve guard. `b427bbf` permits explicit
external artifact storage and rejects same-filesystem fallback. The initial
growth launch was refused because the guard incorrectly applied the host
reserve to RAM-backed /tmp; `eee7955` instead places compiler temporary files
with the run. The refusal log is preserved as `vdev-growth-20261001.log`, and
the external retry as `vdev-growth-20261001-2.log`. No DUT access occurred.

## Temporary-name pool creation (2026-10-01)

`6e387fe` removes the AROS blanket rejection of upstream `zpool create -t`.
The adapter validates the temporary live namespace and keys reservations and
export cleanup to that name; upstream retains the permanent on-disk name.
Normal build `temporary-create-20261001` and native run `fokxxsF3` completed,
testing collision refusal, live-name visibility, child dataset creation, export
and permanent-name reimport, destruction and backing-file release. Exact
package `native-bundle.0KPCdW3b` / `shared-engine.MFA1Ubnr` run `Iyd4NqDd`
also completed with exit 0, original hashes unchanged and staging policy PASS.
the historical test archive registers `evidence-temporary-create-20261001`. The first normal run froze
the older host gate; the exact-package run required the new explicit marker.

`844b2cd` adds authenticated Manager creation options `tname=temporary`,
validated by the backend before storage access. Normal Manager run `YDbjnmab`
completed with exit 0 and unchanged source hashes. `bd11c99` adds four
malformed-option refusals before successful creation; those checks postdate
that fixture. Shared `GPMcfMOx` / package `8IQDnzk4` passed staging policy;
expanded exact-package run `CPgSK193` passed the four refusal cases and full
regression with exit 0 and unchanged hashes; evidence is registered as
`evidence-temporary-manager-package-20261001`. `0651dc0` adds an actual
Manager GUI creation pause followed by independent namespace/reimport checks;
GUI run `BOBGOlsC` passed actual creation, permanent-name reimport and retained
child checks, then completed the broader regression with unchanged hashes.
It exposed layout application clearing tname before submission: cancelled,
re-entered the option, then submitted. The failed interaction and successful
result are retained in `evidence-temporary-manager-gui-20261001`.
`2c564db` preserves create options when applying a create layout; rebuilt
Manager `temporary-builder-20261001` fresh GUI test `INzgdUY5` retained tname
through layout application without re-entry, returned errno 0 and passed
independent permanent-name reimport/retained-child assertions. Screenshots are
registered as `evidence-temporary-builder-20261001`; its broader regression
subsequently completed successfully, with both original fixture hashes unchanged
in `temporary-builder-20261001-gui.log`.
Earlier CLI-only packages do not contain the Manager integration, and package
`8IQDnzk4` predates the builder option-retention fix.

## Selected-page I/O refresh (2026-10-01)

`3cbac39` retains the successful I/O query's options and page cursor during
automatic read-only refresh. Query edits stop refresh; an edited query must
succeed before rearming. Native `GWhWyqTh` completed with exit 0, the explicit
I/O page refresh marker and unchanged original fixture hashes. GUI `5CiVTpLv`
visually verified repeated latency page-two responses (bucket 24 onwards),
edit-stop/rearm refusal, and repeated request-size histogram responses after
explicit submission. `evidence-io-page-refresh-20261001` registers screenshots
and native logs. The broader GUI regression finished with exit 0 and unchanged
original backing-image hashes after normal Manager close. This refreshes
the selected page only, not a synchronized aggregate of all pages. Physical
hardware and stock-nightly runtime remain unqualified.

## Promotion of published snapshots (2026-10-01)

`40cf039` replaces blanket mounted-snapshot refusal with an owner-task busy
preflight in the upstream AROS promotion path. Snapshot callback errors and
traversal errors now propagate before ancestry changes, covering CLI and
Manager. Busy DOS references remain protected; preflight is not a reservation
or an atomic multi-volume detach. Regression retains a snapshot handle across
both refusal paths, then tests delegated Manager promotion and two CLI
promotions with remount/read checks while live filesystem handles remain open.
`e4fac74` requires its explicit marker. Build `published-promotion-20261001`
completes successfully. Native run `uGV8k2dM` completed with both original
fixture hashes unchanged and the explicit busy=2, Manager-detach=1,
CLI-detach=2, remount-read=2 marker (`published-promotion-20261001-native.log`).

Shared build `shared-engine.ddjVrfXY` and package `native-bundle.ZhkI4UVf`
passed staging and negative-policy checks. Exact-package run `S9rOy4Yp`
completed with exit 0 and unchanged original backing hashes. GUI fixture
`7dc156d` run `B8lsgPgK` returned errno=0 from the
actual Manager and passed mounted-before/detach/remount-read plus ancestry
and two preserved-file checks. Its broader regression completed with exit 0
and unchanged original backing hashes after the final Manager closed normally;
screenshots `03-review.png` and `04-result.png` retain visual provenance.
The first GUI launch failed before boot due to an incorrect manager-variable
name; retained `published-promotion-20261001-gui.log`, corrected run in
`published-promotion-20261001-gui-2.log`. These are private-runtime QEMU
results, not stock-nightly or hardware qualification. Nine logs/screenshots
are registered in the historical test archive as `evidence-published-promotion-20261001`.

## Recursive mounted snapshot preflight (2026-10-01)

`22627e1` adds owner-task `oz_dos_detach_check`, checking retained records,
handles, locks, pending packets and DOS publication contention without removing
the device. Recursive CLI snapshot rename now checks all affected source
mounts before upstream begins detach. Each actual detach still rechecks;
this is not a reservation or an atomic multi-volume unmount transaction.
A new regression holds a child lock, requires the parent to remain mounted
after refusal, then releases the lock and requires both renamed snapshots
to remount. Host qualification requires the new explicit marker.
Build `snapshot-recursive-20261001` is in progress. No runtime pass yet.
Manager recursive admission remains disabled and needs separate integration.

Run `fPQ9pWJi` completes exit 0 with the required recursive CLI marker,
selected regression and original backing hashes unchanged. Evidence:
`evidence-snapshot-recursive-cli-20261001`. Subsequent commit `a320382`
enables authenticated Manager recursive snapshot rename through the upstream
dispatcher, rejects recursive filesystem rename, updates operation help and
adds Manager busy-child/parent-retention/double-remount tests. Its separate
`snapshot-recursive-manager-20261001` build is in progress; the earlier CLI
run does not qualify these new Manager changes.

Manager build completes successfully. Targeted run `JBGZfhhq` exits 0,
including Manager recursive busy-child refusal, parent retention, idle pair
detach/remount, recursive-filesystem refusal, selected regression and original
hashes. Evidence: `evidence-snapshot-recursive-manager-20261001`.
Shared `gKEPdEis` builds and stages as `native-bundle.nyxnGrzV`; negative
staging policy passes. Exact-package and GUI interaction tests are running
separately (`snapshot-recursive-bundle-20261001-native.log` and
`snapshot-recursive-gui-20261001-native.log`), not yet qualified.

Exact-package `rQ72kuC0` and GUI `SfZ3AVwk` both finish exit 0 with original
backing hashes unchanged. Package checks include multi-mount CLI and Manager
recursion, busy-child/parent-retention, remounts and the selected regression.
GUI captures prove the Recursive checkbox, confirmation Recursive=yes and
errno 0; its independent fixture checks device withdrawal and exact file
contents after read-only reimport. The GUI fixture has one snapshot; subtree
coverage comes from the native API tests, not that screenshot.
Evidence: `evidence-snapshot-recursive-gui-20261001` and
`evidence-snapshot-recursive-final-20261001`. Promotion with published snapshots
remains unfinished; no whole-port or hardware completion is implied.

## Mounted single-snapshot rename (2026-10-01)

`250393c` admits authenticated single-snapshot rename after native detach,
propagating busy DOS reference errors instead of ignoring unmount failures.
Recursive mounted rename and nounmount requests remain guarded; diagnostic
dispatch cannot implicitly detach authenticated mounts. `871ed0b` additionally
checks that read-only-pool refusal preserves an existing mount.
Normal final build `snapshot-rename-auth-20261001` succeeds. Run `47Awvbgm`
is in progress; busy-lock, nounmount, CLI/Manager detach and remount-read
assertions are not yet qualified. The earlier `snapshot-rename-live-20261001`
build was not deployed: review added the explicit authenticated boundary.
Shared build `shared-engine.0kFuSnEX`, stage `native-bundle.G3rxV18m`, and
negative staging-policy tests pass; exact-package runtime remains pending.
Logs: `snapshot-rename-auth-20261001-{build,native,shared}.log` and
`snapshot-rename-bundle-20261001-{stage,policy}.log`.

`47Awvbgm` completed exit 0 with original hashes unchanged, but did NOT run
the new rename fixture: it lives under optional `AROS_TEST_CLI_POOL_CREATE=1`.
The missing specific markers caught this coverage gap; baseline success is
not rename qualification. Exact-package rerun explicitly enables that mode;
log `snapshot-rename-bundle-20261001-native.log`. Runtime proof still pending.

Targeted `VAjia7yw` exits 1 at line 4654: upstream CLI `rename -u` rejects
snapshot names with "snapshot delimiter", not the expected "busy" string.
Retained-lock refusal passed before that assertion. Failure evidence is kept
as `evidence-snapshot-rename-failure-20261001`. `ad891fe` corrects the test
expectation and adds an explicit host gate for the new rename marker; no
production change was justified by this failure. Run `Bumz1agO` repeats the
exact package with corrected tests (`snapshot-rename-bundle-20261001-native-2.log`).
`b9b0d79` extends GUI mode 3 to mount the snapshot before interaction and
verify native device withdrawal; that GUI fixture has not run yet.

Corrected exact-package run `Bumz1agO` exits 0 with the specific mounted
rename marker, busy-lock refusal, both transport detach/remount reads,
read-only mount retention, remaining selected regression and original hashes.
Evidence: `evidence-snapshot-rename-pass-20261001`. The `-u` check proves
upstream CLI refusal, not direct nounmount ioctl execution. GUI launch
`KXLhQ6Px` exited before boot because GUI mode 3 excludes whole-CLI options;
the corrected dedicated GUI run uses `snapshot-rename-gui-20261001-native-2.log`.

GUI run `OuXvvbdy` exits 0. Manager confirmation and authenticated errno 0
are captured; independent fixture checks prove the snapshot was mounted before,
its DOS device was withdrawn afterward, the old name is absent and exact file
contents survive read-only reimport. Original disk hashes remain unchanged.
Evidence: `evidence-snapshot-rename-gui-20261001` and its `-final-` counterpart.
This completes single mounted snapshot qualification, not recursive mounted
rename, promotion of published snapshots, or hardware durability.

## Manager I/O histograms (2026-10-01)

`4b969b2` adds read-only `histogram=latency` to pool.iostat, exposing all 11
upstream extended-statistics distributions as paginated cumulative buckets.
Unavailable samples are explicit; zero and saturated final bucket bounds match
upstream HISTO. `26d27b9` tests four vdevs, bounds, positive leaf samples,
pagination and invalid-option refusal. Normal run `OE7HwIBT` exits 0 with
1,628 rows across 68 pages, the remaining selected regression and unchanged
original backing hashes. Evidence: `evidence-latency-histogram-20261001`.

`e00ed90` adds `histogram=size`, all 14 independent/aggregated request-size
distributions in bytes, and the corresponding native test. Normal build
`io-histograms-20261001` completes successfully; combined runtime and native
Manager visual checks are running in `io-histograms-gui-20261001-run.log`.
These are live pages, not atomic pool snapshots or per-second rates; automatic
refresh remains restricted to complete ordinary-counter queries. Hardware and
complete-port qualification are not implied.

Combined run `39FOLNk0` passes latency 1,628 rows/68 pages and size 1,400
rows/59 pages, including positive disk buckets and invalid-option refusal.
Actual Manager captures `04-latency-view.png`, `05-latency-next.png` and
`08-size-view.png` verify units/bounds/count rendering and next-page advance;
registered as `evidence-io-histograms-gui-20261001`. A Ctrl+A field edit moved
to the start instead of selecting all; Ctrl+A/Ctrl+K corrected the contents
before submission. The fixture's receive subsequently returned errno 0 and
the first Manager was closed to resume the remaining regression. Final run
completion, final Manager close and original hashes were pending at capture.

Final Manager was subsequently closed normally; `39FOLNk0` exits 0 with
both original backing hashes unchanged and the selected native regression
complete. Final evidence: `evidence-io-histograms-final-20261001`.
Matching shared library `shared-engine.k8HMfh2j` builds successfully and
stages as `native-bundle.SfOBb8MV`; negative staging-policy tests pass.
Exact-package run `sCeFndx3` exits 0, including the selected native regression,
shared-library lifetime and unchanged backing hashes. Evidence:
`evidence-io-histograms-package-20261001`. Private OS overlays remain required.

## I/O refresh editor consistency (2026-10-01)

`eeb6597` prevents ordinary-counter auto refresh from silently discarding
edited histogram options or arguments. Both arming and scheduled submission
require the selected operation, sampled pool and empty argument/options to
match. Native helper regression `Qz0HZV60` passes these rejection cases,
the selected full regression and unchanged original backing hashes, exit 0.
Evidence: `evidence-refresh-editor-20261001`. Revised Manager builds as
`refresh-editor-20261001/OpenZFSManagerNative`; interactive GUI verification
is running in `kLKWSPiT`, log `refresh-editor-gui-20261001-run.log`.
The helper pass is not evidence of the actual checkbox/editor interaction.

Interactive run `kLKWSPiT` subsequently verifies repeated normal refresh,
automatic stop on entering `histogram=latency`, and rearm refusal with that
option still present. Request count stays at 20 across the stopped/refused
captures. The receive fixture then returns errno 0; both Managers close
normally and the run exits 0 with the selected regression and original hashes
passing. Evidence: `evidence-refresh-editor-gui-20261001` and
`evidence-refresh-editor-gui-final-20261001`.

## Live redacted receive teardown (2026-10-01)

New fixture `D36GYk2Q` receives a redacted incremental into an ordinary mounted
readonly target while retaining a DOS handle. Receive succeeds and the retained
read fails, but a new public-file lookup succeeds: namespace-ended invariant
fails at line 4142. Log `mounted-redacted-20261001-run.log` is retained.

The old condition inspected cookie.drc_ds after begin, but upstream begin has
already activated redaction on that temporary dataset. The owner helper now
checks the original mounted dataset immediately before suspend/swap. Manager
and AROS CLI callers pass the incoming stream's redaction bit instead of the
temporary dataset's state. Normal build `redacted-live-end-20261001` succeeds;
unchanged fixture runtime retest is pending. No successful teardown claim yet.

Fixed run `aV9PzBbh` now reaches AROS's missing-volume requester on the new
lookup. Capture `01-ended-volume-requester.png` confirms this is a requester,
not evidence of a crash. The harness deadline ended this run incomplete.
Final-source run `dOPcIvKo` reached the same requester. Headless QEMU has no
absolute pointer input handler, so a pointer dismissal attempt was refused.
`9e972e7` suppresses requesters only around the deliberately failing Lock calls,
restoring pr_WindowPtr immediately afterward; the failure assertion stays.
Shared package `W09VZgpQ` / library `LrNwg7ba` now runs the corrected harness
with both Manager and CLI receive paths. Qualification remains pending.

`uakfV6BF` terminated with exit 1 at the explicit unmount assertion, not a
requester timeout. The namespace lookup correctly failed and the CLI reported
"not currently mounted". Inspection of `oz_engine_pump` shows automatic
ended-mount detach/close after retained references drain. The regression now
checks mounted=no and the expected redundant-unmount refusal, then requires
successful dataset destruction to prove ownership release. No production
lifecycle change was justified by this harness failure. Retest pending;
original log: `redacted-live-bundle-20261001-native.log`.

Follow-up `DLFaCrfs` exited 1 at the redacted-property assertion after passing
automatic withdrawal checks. Upstream reports numeric `redacted=1`, not `on`;
both new assertions now expect that representation. This was another test
expectation error, not a production property fix. Log retained as
`redacted-auto-drain-20261001-native.log`; destruction and CLI path still pending.

`Pezinhb1` (log `redacted-auto-drain-20261001-native2.log`) passes withdrawal
and redacted=1, then aborts inside CLI zfs_verror during recursive destruction.
The exact backend error is not established by that trace. Opt-in CLI ioctl
diagnostics now retain failed request/target/transport/backend errors before
libzfs's unknown-error abort. Normal builds do not enable this trace.
Diagnostic build attempt `redacted-destroy-trace-20261001-build.log` failed
before linking because it selected an old pipe directory lacking posixc.library;
`build2.log` corrects it to the original CLI's XSGxwRgI input. No storage or
production cleanup change has been made for this abort; diagnostic replay pending.

Diagnostic replay `2lBrZXZE` terminated with exit 1 and identified snapshot
enumeration, not destruction/ownership release, as the failing operation:
request 23061 returned engine ENOMEM (12), translated by the client to
EOVERFLOW (84). Upstream zfs_do_list_ioctl retries ENOMEM with a larger buffer
and restores the original iterator cookie. The native transport admitted this
for dataset enumeration but omitted snapshot enumeration. Fix `655ae18` adds
the read-only snapshot iterator to that same bounded growth path; import's
non-retry behavior is unchanged. Normal, trace-disabled CLI `4hjewxHq` builds
successfully. Regression `snapshot-growth-20261001-run.log` is pending.

Normal replay `dbmePsBV` completed with host exit 0: both Manager and CLI
mounted redacted receives reject retained reads, withdraw the namespace,
automatically drain the mount, and destroy the datasets. Recreation and the
second receive also pass. The remaining selected transport regression and
original mirror backing hashes pass. Evidence is registered in
`evidence-redacted-live-pass-20261001`. This uses normal service
`redacted-live-final-20261001` and trace-disabled CLI `4hjewxHq`; exact shared
package `3l3aQJlc` is staged and its separate qualification is pending.
Private OS overlays remain required; this is not full-port/hardware proof.

Exact shared package `3l3aQJlc` subsequently passed in `OGdH9hVi` with host
exit 0, both live-redacted markers, full selected regression and unchanged
original hashes. Negative staging-policy tests also pass. A separate direct
public-API regression (`2104ed4`) now forces a one-byte snapshot reply, checks
ENOMEM/required size/no partial copy, resets the iterator cookie and verifies
the complete retry. Its diagnostic fixture build/runtime qualification is pending.

The focused test-mode public service build `snapshot-growth-public-20261001`
completed successfully. QEMU run `dkzFXFhS` exits 0 with
`Public upstream enumeration PASS ... snapshot-growth=1`, followed by public
service lifecycle and RAM-backed mirror DOS-roundtrip success. This tests the
real client/service transport separately from the normal shared package above;
the test-mode service is not a release artifact. Logs:
`snapshot-growth-public-20261001-{build,run}.log`.

Native Manager run `g7zjyUui` visually verifies the long receive-conflict
explanation: ordinary receive is refused with errno 26 and the entire message
wraps visibly in the response pane (`06-conflict.png`). Explicit Force review
shows the destructive warning (`07-force-review.png`); confirmation reports
errno 0 (`09-force-complete.png`). Screenshots and actions are registered under
`evidence-receive-conflict-gui-20261001`. The Manager was closed to resume
independent DOS-content checks and the remaining regression; final completion
and hashes are pending. No DUT or real-array access occurred.

`g7zjyUui` subsequently passed independent local-change removal, exact two-file
readback, resumed wait and rollback. After the remaining selected regression
and successful export, the final Manager was closed normally. Host exit 0 and
unchanged original disk hashes complete this run. GUI evidence does not imply
physical storage qualification or full-port completion.

## Receive conflict explanation (2026-10-01)

`f0f737b` retains ETXTBSY but explains the incremental base/raw snapshot-chain
conflict, possible mount-time accounting, and the destructive nature of Force.
Normal build `receive-conflict-20261001`, run `yN7jj4eY`, passed exact response
text, plain and writable raw refusal/explicit-force cases, the remaining selected
regression and original disk hashes with exit 0. Matching shared library
`mwp66L8l` and package `ePvxg9OP` built and passed staging policy. Exact-package
run `nEnZwJZz` completed the selected full regression with exit 0 and unchanged
original hashes, including the writable raw explicit-force variation.
No GUI render claim from API text tests.

## Mounted encrypted raw receive investigation (2026-10-01)

Fixture `4e305d4` extends raw incremental receive to an already mounted,
unlocked destination while the sender's key is unloaded. `6MqQ5Bsd` failed
at receive begin with ETXTBSY (26); `kyvR1wop` still failed after disabling
target atime before verification reads. Thus atime alone is not an established
cause. No force or production guard change was made. Both logs are retained:
`manager-mounted-raw-20261001-run.log` and `run2.log`.

Control `X84SX5CD` additionally mounts the destination read-only and passes
the scoped receive, exact new DOS bytes, retained key and wait gates. Its full
selected regression completed with host exit 0 and unchanged backing hashes.
This does not resolve writable-mount modification
or qualify writable mounted raw receive; that investigation remains open.
`AROS_TEST_RAW_MOUNT_WRITABLE=1` retains a separate diagnostic variation using
the same production package, querying written@next before mount, after mount,
and after verification reads. It still requires successful ordinary receive;
it does not reinterpret ETXTBSY as a passing mounted receive.

Diagnostic `6KMBlYhX` completed with the expected unresolved ETXTBSY failure,
not a passing regression. `written@next` was 0 before mount, 172032 after
writable mount and pool sync (before DOS reads), and still 172032 after the
reads and another sync. This localizes divergence to mount-time activity,
not those verification reads. Trace the writable mount/recovery path next;
do not disable recovery or force receive to hide it. Log:
`manager-mounted-raw-20261001-diagnostic.log`.

Private observational build `raw-mount-trace-20261001` / run `4Ox6lKo5`
traces the actual objset ownership and ZIL replay calls without skipping either.
Before writable own, flags=0/users=0; before replay, flags=1/users=1.
ZIL flags remain 0, the log is a hole, and replayed=0. This matches upstream
`dsl_crypt.c` clearing USERACCOUNTING_COMPLETE on raw receive and
`dmu_objset_own()` invoking the userspace-accounting rebuild on writable mount.
The run retains ETXTBSY and exits 1; it is diagnostic evidence, not a pass.
No recovery bypass or accounting removal is justified by this observation.

`AROS_TEST_RAW_MOUNT_FORCE=1` (requires the writable variation) separately
requires ordinary receive to refuse, then explicitly confirmed Force to
succeed, with old and new DOS contents checked. Qualification is pending.
The first force-run launch `Ux93PgjU` failed before boot because of a mistyped
private posixc overlay path; `raw-mounted-force-20261001-run.log` is retained.
The corrected run uses the unchanged production shared package, not wrappers.

Corrected production-package run `rSP6M9uG` passed both the expected ordinary
ETXTBSY refusal and explicitly confirmed forced raw receive while mounted
writable. New payload, prior payload, key retention, wait, the remaining
selected regression, and original backing hashes all passed; host exit 0.
Log: `raw-mounted-force-20261001-run2.log`. This qualifies the explicit-force
path, not ordinary receive across a modified base. The accounting rebuild is
upstream behavior, not grounds for suppressing recovery or silently forcing.

## Explicit Manager forced receive (2026-10-01)

Operation 61 now accepts exactly CONFIRMED or CONFIRMED|FORCE. Force is
passed to upstream receive and its permission checks; existing callers remain
no-force. The UI confirmation warns that local changes and conflicting
snapshots may be discarded. That warning still needs visual qualification.

Normal full-dispatcher build `manager-force-receive-admitted-20261001`
passed run `Oz9uImyz`: ordinary receive refused a modified mounted head,
preserved local and baseline bytes, rejected two invalid flag combinations,
and explicit forced receive discarded the local change and exposed exact
incoming bytes. Mounted rollback and the remaining selected regression passed;
host exit was 0 and both original backing-image hashes were unchanged.
Log: `build-aros/manager-force-receive-20261001-run2.log`.

Earlier run `SvrlYtcW` failed before forced receive: the test incorrectly
expected EINVAL for confirmed FORCE plus unsupported RAW. Admission correctly
returns ENOTSUP. Only that expectation was corrected; the guard was not weakened.
Its log `manager-force-receive-20261001-run.log` is retained. This result does
not qualify the shared-library package, forced-receive delegation, GUI warning,
raw/redacted permutations, physical hardware, or the complete port.

Follow-up `iFsesoYH` qualifies the forced-receive authority boundary:
receive:append plus mount/create cannot force receive, and exact local bytes
remain intact with no incoming payload. Adding receive allows the operation;
revoking all four rights denies another forced request. Exact incoming data,
rollback, the remaining selected regression, and unchanged original hashes
passed with host exit 0. Log: `manager-force-delegation-20261001-run.log`.

Optional `AROS_TEST_FORCE_RECEIVE_GUI=1` with `AROS_TEST_RECEIVE_GUI=1`
creates a local change before opening Manager. The operator must select Force
and inspect its warning; the independent client then checks the local file was
discarded, followed by the existing exact incoming/baseline and rollback gates.
This GUI variation is not yet qualified by the headless results above.

Shared package `bELY4G0G` (library `QMraXhrY`) then passed exact-package run
`RUMbEgtg` with host exit 0, staging-policy negative cases and unchanged
backing hashes. Actual GUI run `2MunV2sg` shows the correct destructive warning
in `05-force-confirm.png` and errno=0 in `07-force-complete.png`. After closing
Manager, independent checks passed: local change absent, two exact files,
wait and rollback. Its remaining selected regression subsequently completed
with host exit 0 and both original backing hashes unchanged. Final evidence:
`build-aros/evidence-manager-force-gui-final-20261001`.

## Manager mounted receive (2026-10-01)

Full-dispatcher direct receive now finalizes through the same native owner
suspend/receive-end/resume bridge as upstream CLI receive. The libzpool
adapter passes a dataset name to the ZPL bridge, which retains/releases the
actual published filesystem. Redacted transition handling follows upstream's
end-versus-resume choice. Reduced-dispatcher Manager receive remains
unmounted-only. No force or permission bypass was added.

`manager-receive-20261001-build.log` failed at the libzpool/native type
boundary; `build2.log` exposed a missing reference-management declaration.
Both failures are retained. `build3.log` succeeded. Run `vH6yMiR0` passed
actual Manager operation 61 incremental receive into a mounted filesystem,
exact received and baseline bytes, subsequent wait and mounted rollback.
The full selected regression finished with host exit 0 and unchanged original
backing-image hashes (`manager-receive-20261001-run.log`). The later preprocessor guard
restricting the new bridge to full-dispatcher builds does not change this
full build's behavior. Fresh final-source rebuild
`manager-receive-final-20261001` and matching shared library `kQm5jmdV`
subsequently succeeded; package qualification is still pending. No GUI, redacted mounted Manager receive, physical
hardware, or complete-port claim follows from this scoped test.

The final-source shared package `EvfOvnBm` is under test in `odTV5UxP`.
Its extended Manager receive test rejects the truncated stream with upstream
`ZFS_ERR_STREAM_TRUNCATED` (1040), verifies exact baseline bytes and absence
of the new file, completes a wait, then retries the complete stream and
checks both files and rollback. These scoped markers passed; the remaining
selected regression completed with host exit 0 and unchanged original backing
hashes. Negative staging-policy tests passed. Log:
`manager-receive-bundle-20261001-native.log`.

Optional `AROS_TEST_RECEIVE_GUI=1` pauses after the truncated-stream refusal
for an actual Manager receive from `RAM:online-receive.stream` into the live
filesystem. Run `tzVcf3BC` passed the GUI-specific independent checks after
closing Manager: two exact file contents, subsequent wait and rollback.
`06-receive-result.png` records the real errno=0 response. An initial
wrong-row abort-receive draft was cancelled without submission; the corrected
receive confirmation was inspected before submission. The remaining selected
GUI regression passed with host exit 0 and unchanged original backing hashes.
Initial evidence: `build-aros/evidence-manager-receive-gui-20261001`;
final logs: `build-aros/evidence-manager-receive-gui-final-20261001`.

Follow-up `Xx2Vye0P` adds a no-force data-preservation gate on the unchanged
shared package. A locally modified mounted head must return ETXTBSY (26),
retain exact local-change and baseline bytes, and not expose the incoming
payload. An explicit test rollback then restores the base before the existing
truncated-stream refusal and complete retry. All these scoped checks passed;
the remaining selected regression completed with host exit 0 and unchanged
original backing-image hashes. Log:
`manager-receive-noforce-20261001-run.log`.

Follow-up `0miN0Qk4` exercises ordinary-user mounted receive on the unchanged
shared package: no grants and receive-only grants return EPERM; adding mount
and create permits the stream; revoking all three makes the next request
return EPERM. The existing exact-file, wait and rollback checks then pass.
These gates and the remaining selected regression passed with host exit 0
and unchanged original backing-image hashes.
GUI mode retains its explicit human-equivalent root confirmation fixture and
does not claim the delegation branch. Log:
`manager-receive-delegation-20261001-run.log`.

## Manager mounted promotion (2026-10-01)

Full-dispatcher operation 53 now uses upstream `ZFS_IOC_PROMOTE` under the
authenticated caller credentials. Live source/clone filesystem mounts are
supported. Published origin snapshots still require explicit unmount:
upstream's snapshot-unmount callback discards errors, so allowing a busy DOS
snapshot through could strand its published name. The reduced dispatcher
retains the previous all-affected-volumes-unmounted requirement.

Normal build `manager-promotion-20261001` succeeded. Native test `vnCbYzyf`
passed the mounted-promotion fixture: a conflicting snapshot is refused;
after removing that collision, both already-open DOS files retain exact
original contents, the source origin points to the promoted clone snapshot,
and a new clone file is absent from the source. Both filesystems unmount and
are destroyed successfully. The remaining selected full regression and
original backing-image hash gates subsequently passed with host exit 0.
Logs: `manager-promotion-20261001-{build,run}.log`; compact evidence:
`build-aros/evidence-manager-promotion-20261001`.
This fixture invokes the real Manager API, not GUI clicks. It does not yet
qualify mounted-snapshot promotion.

Follow-up `8kuE48aV` uses the same normal service with an extended client.
The scoped authority fixture passes: no grants are denied, clone-only
promote/mount is denied, adding origin promote allows the operation, and
both grants are revoked. A published origin snapshot causes EBUSY and remains
readable with exact original bytes before explicit unmount. Retained source
and clone file handles remain readable across delegated promotion. The full
selected follow-up regression and original backing-image hash gates passed
with host exit 0. Log: `manager-promotion-authority-20261001-run.log`;
evidence: `build-aros/evidence-manager-promotion-authority-20261001`.
This is not a GUI click test.

Actual GUI follow-up `G0pPF0PD` uses shared bundle `qubo4sd1` and optional
`AROS_TEST_PROMOTION_GUI=1`. After the delegated promotion fixture it pauses
for Manager promotion of the former source, reversing the ancestry. The
actual confirmation returned errno=0; after closing Manager the harness
verified the changed clone origin and both exact file contents. Screenshot
`05-promotion-result.png` records the response. An initial wrong-row click
selected clone and was rejected locally for a missing argument; it did not
submit a backend operation. The remaining selected regression completed with
host exit 0 and unchanged original backing-image hashes. Log:
`manager-promotion-gui-20261001-run.log`; final evidence is retained under
`build-aros/evidence-manager-promotion-gui-final-20261001`.

## Manager mounted rollback (2026-10-01)

The full-dispatcher Manager operation 54 now calls the same upstream
`ZFS_IOC_ROLLBACK` entry point as the CLI. The authenticated service owner
supplies a packed exact target and bounded output buffer under the existing
credential/copy scopes. Upstream performs mounted suspend/resume, permission
checks and snapshot validation. There is no implicit recursive destruction,
force, dry-run or new authority. The reduced-dispatcher fallback still requires
unmount; the help text identifies full native service support explicitly.

Build log `manager-rollback-20261001-build.log` retains the initial missing
dispatcher-prototype error; `build2.log` contains the corrected successful
build. Native run `xEtqG3Xr` passes two mounted rollback cycles, including
newer-snapshot EEXIST, missing-snapshot ESRCH, unauthorized EPERM, delegated
rollback success, and removal of the post-snapshot DOS file. Delegation is
revoked after each cycle. That headless run completed the selected full
regression and unchanged original backing-image hash gates with host exit 0.
The new optional `AROS_TEST_ROLLBACK_GUI=1` fixture requires an actual Manager
rollback and independently checks discarded changes and intact baseline bytes
after the window closes. Run `ihGln6Kq` passed that independent GUI check:
`actual-manager=1 discarded=1 baseline-preserved=1`. Screenshot
`07-rollback-result.png` records the real operation's errno=0 response.
The remaining selected regression and unchanged original backing-image hashes
subsequently passed with host exit 0 (`manager-rollback-gui-20261001-run.log`).
Compact logs and the screenshot are retained in
`build-aros/evidence-manager-rollback-20261001`.

The private QMP keyboard helper now sends modifier presses before base keys
and releases them in reverse order. The previous HMP chord produced a wrong
target in the first draft; that draft was not submitted. The corrected input
was visually reviewed, submitted, and independently verified by the fixture.
This affects private QEMU automation only, not the DUT or KVM configuration.

## Mounted receive failure recovery (2026-10-01)

The normal two-cycle mounted receive test now keeps a baseline file in the
base snapshot. Before each valid receive it copies the incremental stream
minus its final 512 bytes and requires the actual CLI to reject that stream.
It then verifies exact baseline bytes plus EOF, absence of the not-yet-received
file, and a successful wait. The complete stream must subsequently receive
successfully with exact new bytes. Both cycles use the normal shared package,
not linker wrappers or fabricated backend replies.

`native-import-run.EDZddj1M`, using immutable bundle `e1i9MKGu`, reports both
`cannot receive incremental stream: incomplete stream` errors and the
`truncated=2 preserved=2 retried=2` PASS marker. Its broader regression and
final backing-hash gates were still running when this note was added.
Log: `receive-recovery-bundle-20261001-native.log`.
Completion: `EDZddj1M` subsequently passed the full selected native transport,
shared-library, ZVOL and standalone-handler regression, exited zero, and
verified both original fixture hashes unchanged. Its service and library are
the unchanged packaged binaries from `e1i9MKGu`; only the test client/runner
were extended for this additional recovery coverage.
This covers incomplete input recovery, not power loss or physical I/O faults.

## Owner-task online receive correction

Native online receive now marshals the complete suspend/receive-end/resume
sequence to the existing service owner. Stream transfer remains in its retained
worker. A private embedded request travels over the service completion port;
the owner accepts it only by identity against an active receive job and uses
that job's retained authenticated credentials. No client packet supplies a
callback. The worker waits for the reply before returning or freeing its mount
reference, stack context, session or request.

The owner does not pump DOS packets in the middle of finalization. The existing
non-owner suspend guard is unchanged. Direct owner callers execute the same
helper inline. Dispatch failures still invoke upstream receive cleanup without
claiming the live mount's ownership. Upstream resume versus redacted end-of-FS
selection and error precedence are retained.

`native-import-run.QefBfXKJ` now completes with exit zero, including the
previously failing pending-wait receive case: actual suspend errno=0, waiter
interrupted, incremental receive success, resumed wait, and exact DOS bytes.
The full selected transport regression and unchanged original fixture hashes
pass. Logs: `receive-owner-20260930-{build,run}.log`. This uses test-only held
deletion/suspend diagnostics, not a distributable instrumented service.

A separate mandatory normal-build test now performs two mounted incremental
receive cycles, checking absence after rollback, exact content plus EOF after
receive, subsequent wait and cleanup. Its normal service is
`receive-owner-release-20260930`; the runtime result is not yet claimed here.
Run `native-import-run.q3ZHzrZp` has reached the required two-cycle receive
PASS marker with no test wrapper symbols in the service. Its remaining
regression and final hash checks were still running when this note was added;
the marker alone is not a complete-run pass.
Completion: `q3ZHzrZp` subsequently exited zero after the entire selected
native transport regression and verified both original backing hashes
unchanged. Its normal-build logs are
`receive-owner-release-20260930-{build,run}.log`. Together with `QefBfXKJ`,
this verifies both ordinary repeated online receive and actual pending-wait
cancellation, with the scope limitations below unchanged.
These tests do not establish every mounted raw/redacted receive combination,
stock nightly compatibility, physical durability or full-port completion.

## Pending wait during incremental receive (verification in progress)

The private pending fixture now creates a real incremental send stream from
the mounted `cli-waitfs`, rolls back to its base snapshot, and retains a newly
deleted node. It launches Manager operation 49 through the real CLI worker,
requires it to remain pending, and runs `C:zfs receive -F` from an independent
client. Acceptance requires an interrupted wait, successful receive, a new
successful wait after resume, and exact received DOS bytes plus EOF.
The existing private suspend hook releases only the retained node reference;
production wait/receive/suspend code is not replaced.

Build `fs-wait-receive-cli-20260930` succeeded. First launcher attempt
`native-import-run.26ca3zYv` failed before guest boot because the supplied
overlay directory had a lowercase final q instead of Q. Its log is retained
as `fs-wait-receive-cli-20260930-run.log`; it is not a runtime failure or pass.
The corrected invocation is retained in `fs-wait-receive-cli-20260930-run2.log`.
No receive-concurrency pass is claimed until its required marker and complete
runner/hash checks finish.

Runtime failure: `native-import-run.n6kzj22V` reaches the new receive, then
the external `C:zfs` aborts in `zfs_verror` while reporting an unknown error.
The waiter itself reports upstream errno=4 / waited=1. The runner detects the
abort and exits 1; no receive PASS or final hash-check claim is made.

Diagnostic replay `native-import-run.J8bTenEk` (build/run logs
`fs-wait-receive-diag-20260930-{build,run}.log`) reproduces the same abort.
The private suspend wrapper now records the real return value: prior rollback
suspends return 0, but the receive worker's suspend returns 11. Native
`zfs_suspend_fs` explicitly rejects non-owner tasks with EDEADLK; streaming
receive runs in an admission worker while DOS remains serviced by the owner.
This is a missing owner-task online-receive integration path, not evidence
that the ownership guard should be removed. The failed test remains mandatory
under the optional pending-wait fixture until that integration is fixed.

## Suspend/resume wait lifetime (2026-09-30)

Native rollback/receive suspension bypasses normal mount unpublish, so it
must independently cancel filesystem waiters after detaching the objset's
published ZPL pointer. Successful resume resets cancellation under the
activity lock before republishing, using the same lock order as wait.
Failed resume leaves cancellation set and the filesystem close-only.

`default-quota-run.ww4SRprK` passes a genuinely pending held-deletion wait
across native suspend/resume: cancellation returns EINTR, resume succeeds,
and normal reclamation empties the queue. Subsequent unpublish/republish and
ordinary pending/drain cases also pass. The host requires all three markers
plus the full native quota/lifecycle regression and exits zero. Logs:
`fs-wait-suspend-20260930-{build,run}.log`.

This direct test verifies native suspend/resume. The previously packaged
WD0V8QoJ bundle predates this additional lifecycle fix.

The external pending fixture additionally snapshots the private mounted
filesystem, retains a subsequently unlinked node, launches the normal Manager
operation 49 / C:zfs waiter, and invokes actual C:zfs rollback from another
client. Its private suspend hook releases only the fixture node reference;
production suspension must cancel the real waiter. The test requires the
interrupted CLI response, successful rollback, and a successful new wait
after resume. This does not cover pending waits during receive.

Run `native-import-run.POfzz6kV` reports native suspend, upstream errno=4 /
waited=1, and the explicit pending/interrupt/rollback/resumed-wait PASS marker.
At the time this note was committed, its broader regression and final backing
hash checks were still running; this is not yet a whole-run pass. Build/run
logs: `fs-wait-rollback-cli-20260930-{build,run}.log`.

Completion: `POfzz6kV` subsequently exited zero after the full selected native
regression, including the rollback marker, and verified both original backing
image hashes unchanged. Evidence is registered under the development test archive.

## Native deletion queue semantics (2026-09-30)

The transport qualification below originally reached upstream's userspace
`dsl_dir_activity_in_progress` branch, which assumes libzpool has no ZPL and
always reports idle. AROS does have a native ZPL. Its branch now queries the
mounted filesystem's actual unlinked-set ZAP, retaining upstream's activity
lock and condition wait. Mount flags/object number are copied under the
objset publication lock; no mount pointer escapes that lock. Unmounted,
readonly, snapshot and non-filesystem objects do not report pending deletion.

Diskless native run `default-quota-run.BMB3bT20` passes the real held-unlinked
node test: upstream `dsl_dir_wait` remains pending, native reclamation removes
the last queue entry, and its existing condition broadcast wakes the waiter
with `waited=1`. The complete quota/lifecycle regression exits zero. Build and
run logs: `fs-deleteq-test3-20260930-{build,run}.log`. The host verifier now
also requires the explicit deletion-wait result marker.

Retained failed test setups (not production wait failures): `QBr5vBEk` held
the objset/configuration lock across a synchronous filesystem mutation and
aborted in transaction assignment; `VhZSyTLX` used the DOS remove wrapper,
which synchronously reclaims and reported EBUSY for the deliberately held
node. The final fixture releases the lookup hold after acquiring the mount
reference and uses native ZPL unlink to deliberately retain a deletion.

This establishes actual queue blocking and drain wakeup in a private RAM pool,
not end-to-end pending CLI/Manager completion, cancellation, stock OS support,
or physical durability. Production service/UI integration remains to retest.

Follow-up `default-quota-run.SRdcKpkA` qualifies native mount-unpublish
cancellation: the registered pending upstream waiter returns EINTR, successful
republish clears cancellation, and the subsequent held-node wait/drain case
passes again. Both markers and the full native regression are required by the
host verifier, which exits zero. Logs: `fs-deleteq-cancel-20260930-{build,run}.log`.
Cancellation runs after publication/ref locks are released; publication resets
the flag under the activity lock with activity-before-publication lock order.
This does not yet prove cancellation through a pending external CLI client or
Manager shutdown, nor qualify the cancellation change in the shared bundle.

## Pending external filesystem wait fixture

`AROS_TEST_WAIT_PENDING=1` now also instruments the private service's native
remove/unpublish paths for the disposable `qemu_mirror/cli-waitfs` dataset.
It retains a real znode while DOS deletion removes its name; synchronous
reclamation truthfully returns EBUSY. The Manager operation 49 worker launches
the normal `C:zfs wait -t deleteq` child. The fixture requires it to remain
pending across an independent `zfs list`, then removes a second file to
release the retained node and let normal native reclamation wake the waiter.
The wrapper around `dsl_dir_wait` records its actual errno and waited result;
it neither substitutes completion nor changes the activity query.

Run `native-import-run.kXS26oQB` passes this pending/list/release/drain path,
reports upstream `errno=0 waited=1`, completes the full native transport
regression, and verifies unchanged original backing hashes. Host exit is zero.
Build/run logs: `fs-wait-pending-cli-20260930-build2.log` and
`fs-wait-pending-cli-20260930-run.log`. The first build log is retained: it
failed because the test wrapper omitted the header defining oz_zpl_info.

The expanded fixture repeats with a pending waiter and real CLI unmount.
Its private unpublish wrapper only releases the retained node reference;
production unpublish must cancel the waiter before close reclaims the queue.
The client requires an error response, then remounts and waits again.
Host gates require upstream `errno=4 waited=1` (native EINTR) and the explicit
cancel/remount marker as well as all normal regression markers.

Expanded run `native-import-run.DveLV1qF` satisfies those gates and exits zero
after the complete native transport regression and original backing-hash
checks. Build/run logs: `fs-wait-pending-cancel-cli-20260930-{build,run}.log`.

The distribution staging guard rejects the instrumented binary, as verified
in `fs-wait-pending-cancel-stage-refusal-20260930.log`. These test-only hooks
are never linked by default. This is Manager command-worker coverage through
the real external CLI, not a visual GUI-close or Ctrl-C interaction test.

## Filesystem deletion wait transport (2026-09-30)

`ZFS_IOC_WAIT_FS` now uses the same retained worker and session lifetime as
pool wait. The CLI releases its connection mutex while retaining an in-flight
reference; service admission permits concurrent administration rather than
treating a filesystem wait as an exclusive data stream. Live request rows
classify it as a wait. The synchronous owner-loop ioctl path still refuses it.
Upstream `zfs_ioc_wait_fs` and `dsl_dir_wait` supply actual activity semantics,
dataset holds and completion; there is no AROS timer-based success shortcut.

Baseline `native-import-run.JEfm1mLR` passed pool wait, then failed filesystem
deleteq wait with "unsupported version or feature". The smaller preceding
`67GDePQI` run did not enable the full transport test and is not evidence for
this operation. Both logs are retained. The fix rebuilds service
`fs-wait-engine.hSgLIUnw/service` and all commands in `cli-link.UQWqkMs8`.

The native regression now requires both `zfs wait -t deleteq` and default
`zfs wait`, plus refusal for a nonexistent filesystem. These idle checks do
not prove prolonged blocking, concurrent delete cancellation, signal handling
or a Manager filesystem-wait action. These require separate coverage.

Manager operation 49 (`dataset.wait`) now invokes `C:zfs wait` through the
identity-preserving command worker. Pool operation 30 remains `C:zpool wait`.
The filesystem action accepts only `activities=deleteq` or an empty activity
selection, rejects flags and extra arguments, and reports upstream output.
The initial UI build accidentally added a second wait operation (80); visual
inspection in `Lkj40uvt` caught this before commit. It was removed in favor of
the existing stable ID 49.

Production integration run `native-import-run.lWu61MjK` uses the rebuilt
`fs-deleteq-production-20260930` service and Manager, with diagnostic modes
off. CLI idle/default/missing-target tests and Manager selected/default plus
seven malformed-request tests pass. The GUI shows a single wait entry and
captures real upstream `zfs wait` exit 1 / dataset-does-not-exist feedback for
the typed absent target `qemu-mirror`; this is error-path proof, not successful
pending GUI completion. Screenshots and logs remain in that run directory.

Fixed run `native-import-run.X7FvLzjj` exits zero with the mandatory filesystem
wait marker, the full selected native transport regression, and unchanged
original disk hashes. Log: `fs-wait-fixed-native-20260930.log`. This run uses
the monolithic production service (diagnostic modes off/full dispatcher on)
and private OS overlays; it does not qualify the previously staged shared
engine bundle, a stock nightly, or physical hardware.

ZFS_IOC_WAIT now has a dedicated retained-worker entry point. Direct execution
on the DOS owner request loop is not admitted. The CLI uses the retained
request channel without data callbacks, as for send-size estimation. The
worker resolves authenticated credentials, session-local control descriptors,
and the packed upstream input schema before invoking the real dispatcher.

Wait releases the client connection mutex while keeping an in-flight reference;
close waits for final acknowledgment. This permits status queries on the same
connection. On the service, waits do not activate the stream-only exclusion
of normal administration. Send/receive/diff streams retain their existing
exclusion. Wait packets and session references remain alive until completion;
there is no premature timeout-based release or pthread cancellation.

Upstream spa_wait/spa_wait_tag determines completion, activity validation and
export cancellation. No AROS timer substitutes for actual completion. Pool
wait is not itself evidence of a healthy pool or successful repair.

The initial native test runs idle scrub wait, starts an actual scrub, waits
for scrub, and invokes default all-activity wait. A small fixture may complete
the scrub before the wait arrives: this does not prove a prolonged blocked
wait, concurrent stop/export, cancellation, or verbose-query concurrency.
The initial CLI implementation did not connect Manager operation 30.

Build inputs: source-engine.P2gN7Bsv/wait-service and cli-link.PTQ5X1YB,
with the existing private OS overlays. Logs: engine-wait-build-1.log and
cli-wait-link-1.log. This is not stock-nightly/hardware qualification.

Run bgEyJyyX (`cli-wait-native-1.log`) passes all three initial wait cases,
the full native regression, and unchanged original backing hashes with exit 0.
Evidence is registered under the development test archive. The concurrency limitations above remain
unproven and must not be inferred from this run.

## Manager worker

Operation 30 now runs exact `C:zpool wait` arguments asynchronously through the
existing inherited-identity worker. Empty options select all activities;
`activities=` accepts a comma-separated list of upstream activity names.
Unknown/duplicate/empty entries and unrelated flags/arguments are rejected.
Closing Manager drains the worker; it does not cancel the wait or free a live
packet. The help text explicitly describes this limitation and that completion
is not a health guarantee. Build: manager-pool-wait/OpenZFSManagerNative.

The added worker fixture exercises selected scrub/resilver and default-all
waits, plus six malformed requests. These are idle worker tests, not visual
verification or proof of a prolonged blocked wait.

Run mOu7H3VL (`manager-wait-native-1.log`) passes these worker cases and the
full native regression with exit 0 and unchanged original backing hashes.
Evidence is registered under the development test archive.

## Deterministic pending-wait qualification

The optional `AROS_TEST_WAIT_PENDING=1` service build links a private wrapper
around `spa_scan_range` and `spa_scan_stop`. For the first scrub of the
disposable `qemu_mirror`, it sets upstream's `zfs_scan_suspend_progress`
test tunable, without marking the scrub paused. A paused scrub is not an
active scrub for upstream wait semantics. No wrapper is linked by default.

The matching native fixture starts a Manager wait worker, requires it to
remain pending, queries status from a separate CLI client, requires it still
pending, then stops the scrub from another CLI command and drains the wait.
A separate wrapper reports upstream `spa_wait`'s actual `waited` result to
serial; the host requires `errno=0 waited=1`, not just a successful CLI exit.

Failure history (retained and registered under the development test archive):

- `LjMg3J4V`: first pending assertion failed. The initial instrumentation
  wrapped legacy `spa_scan`; modern `zpool scrub` uses `spa_scan_range`, so
  the scrub was not held. This did not establish a production wait defect.
- `UwnjUGfY`: pending/status/stop/drain and the full guest regression passed,
  but host exit was 1: wrapper messages went to service stderr rather than
  retained serial. The upstream-result gate and final backing-hash gate were
  not satisfied. Reporting was corrected to use the private serial channel.

This test does not qualify wait/export cancellation, same-connection verbose
queries, GUI close during a pending wait, hardware, or stock-nightly builds.

Run `CerqJXN7` (`wait-pending-native-3.log`) passes the pending/status/stop/drain
fixture, reports upstream `errno=0 waited=1`, completes the full native
regression, and verifies unchanged original backing hashes. Host exit is 0;
the evidence is registered under the development test archive. Build log:
`engine-wait-pending-build-3.log`.
