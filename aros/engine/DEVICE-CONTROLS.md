# Native device online/offline controls

Operations 15 (online) and 16 (offline) are routed through native
MuFS-authenticated admission to upstream `vdev_online` and `vdev_offline`.
The target is an imported pool name; the argument is a nonzero decimal leaf
vdev GUID obtained from pool status. Root authorization and a writable pool
are required. The legacy message port does not provide a mutation bypass.

Requests must have exactly CONFIRMED flags. Empty options use upstream
defaults; online also accepts exactly `expand=on` (ZFS_ONLINE_EXPAND), and
offline accepts exactly `temporary=on` (ZFS_OFFLINE_TEMPORARY). Force and
other options are refused. Upstream pool properties, including autoexpand,
still apply. Parent vdev GUIDs are refused;
upstream decides whether a leaf can be offlined without losing the pool.
Online success acknowledges the operation, not completion of any resilver.

## Evidence and limits

Private native two-boot regression `admission-service-run.ODIE1DtF` passed:
offline one mirror member, observe DEGRADED/one OFFLINE, refuse offlining the
last usable member with EBUSY, online the first member, observe ONLINE.
Malformed/overflow/zero GUIDs, parent GUIDs, absent GUIDs, legacy requests,
read-only requests and non-root callers were checked. The rest of the native
DOS regression, cold verification, unchanged read-only image hashes and
independent upstream zdb leak check also passed.

The original pre-modifier tests first failed in `admission-service-run.vopF3nKz`:
the test expected ENOTSUP for an unconfirmed dry-run, but common validation
requires confirmation first and returns EINVAL. The test now distinguishes
unconfirmed dry-run (EINVAL) from confirmed unsupported dry-run (ENOTSUP).
No production refusal behavior was changed to satisfy that expectation.
The corrected expanded regression passed both boots in
`admission-service-run.ypS7pfKI`, including both dry-run cases, option/force
refusal, cold DOS checks, read-only hashes and independent zdb validation.

Native GUI `native-gui-run.x0vC25w9` images `01-online.png` and
`02-offline.png` show the pool/GUID labels and unclipped concise guidance.
Full operation guidance is available as a tooltip. This editor-only guest
intentionally had no service, so it is visual evidence, not execution proof.
It was shut down after capture. The earlier clipped help is retained in
`native-gui-run.MG6JQIwH/03-online.png`.

## Degraded-write and recovered-member verification

The dedicated `AROS_TEST_SUITE=device` runner uses fresh private baseline
copies. Its native client identifies `/AROS/ata.device/0` by status and GUID,
offlines that member, observes DEGRADED, creates a dataset through authenticated
admission, then writes and flushes a deterministic 262144-byte native DOS file.
After onlining the member it polls real upstream scan status until resilver is
finished with zero scan errors, checks all members ONLINE and reads every byte.

The first two-boot run `admission-service-run.aoasbHvL` passed with both members
present on the second boot. The stronger `admission-service-run.Glz8GRRK`
withholds disk1 entirely on the second boot: AROS imports read-only, observes
DEGRADED with one UNAVAIL member, and reads the exact payload using only the
repaired disk0. Both original fixtures and read-only-phase hashes stayed intact;
independent upstream zdb checksum/leak traversal passed.

`verify-recovered-member.sh` additionally discovers disk0 alone, runs upstream
checksum/leak traversal, extracts `payload.bin`, compares all 262144 bytes with
an independently generated expected pattern, and rechecks image hashes. This
passed for Glz8GRRK (`recovered-payload-check.log`). The earlier manual host
extraction using `/payload.bin` failed because zdb uses dataset-relative lookup;
the relative `payload.bin` extraction succeeded. Both logs remain under
aoasbHvL. No native filesystem change was made for that diagnostic error.

Cold client startup skips the unrelated timestamp fixture via the existing
OPENZFS_EXEC_ONLY test switch, just as the focused exec suite does. This is
test isolation, not disabled storage or permission validation.

This is not evidence of cold persistence of offline state, device replacement,
physical failure handling, crash durability, or complete production filesystem support.
All runs used disposable private QEMU copies; no DUT or shared hosted session.

## Subsequent modifier and capacity-growth qualification

The older option-refusal evidence above predates `9e4ba25`. That commit
exposes the two exact modifiers described at the top of this document.
Native run `native-import-run.2KqKzXYN` verified temporary offline produces
DEGRADED and export/reimport restores ONLINE; expansion acceptance alone in
that run did not establish growth. The stronger `fdbd671` test extends the
disposable file vdev, checks increased capacity, exports/reimports, and checks
both persisted capacity and an exact 1-MiB native DOS payload. Run
`native-import-run.l95fYdkE` passed with usable size increasing from 117440512
to 251658240 bytes, plus read-only and unprivileged modifier refusals.
The earlier `ssaepoI6` reached its observation deadline while processing
commands, not a proven crash. See `../cli/WAIT.md` for retained evidence.

## Device-event integration is a separate unfinished boundary

Manual online/reopen and file-vdev growth are not automatic hotplug support.
The native backing-device adapter currently reads geometry when opening a
handle; it does not register TD_ADDCHANGEINT notifications or subscribe to
an OS device-arrival feed. It must not be described as automatically finding
replacement media or qualifying physical autoexpansion.

The separate zvol device already implements change-number/change-interrupt
notifications for its exported virtual media. Those notifications do not
establish equivalent monitoring of the physical devices backing a pool.
Remaining work needs device identity/media-generation handling, safe worker
handoff outside interrupt context, and removal/replacement/reopen tests;
automatic replacement must retain pool policy and device-admission checks.

## Reported media-change invalidation (2026-10-02)

The backing adapter now latches an actual native TDERR_DiskChanged response
and returns ENXIO for later device requests through that handle. New bindings
and bound opens also reject the invalidated handle. Closing it and obtaining
fresh admission is required; cached geometry must not silently authorize a
replacement. Existing in-flight requests are not cancelled, and no detection
of unreported changes, generation-wrap protection or atomic media-check/write
guarantee is claimed. This is a prerequisite, not completed hotplug support.

AROS_TEST_BACKING_MEDIA_CHANGE=1 compiles the production device adapter into
the native test client. It opens the actual openzfs.device virtual medium,
reads it, ejects it through Exec, observes TDERR_DiskChanged, then requires
ENXIO for read/write/flush/inquiry, both bound-open modes and rebinding. The
host runner requires that specific result marker. No fabricated DoIO response
is used. Runs MPsM3XoV (initial I/O checks) and 1bZWlOEt (binding checks) passed.

The full engine was rebuilt, including device-io.c, and shared-engine.hgyDzBtl
was staged as native-bundle.bcx2MQ2N. Run native-import-run.vmW1YGmA passed
with that bundle, the expanded adapter test, native pool/DOS regression and
unchanged original backing hashes; runner exit zero. This tests the adapter
directly against virtual-media removal plus normal service integration, not
fault propagation through a physically removed pool member. Existing build
warnings remain. External logs: backing-media-change-20261002.log,
backing-media-change-20261002-2.log and backing-media-change-package-20261002.log.

Admission now also samples TD_CHANGENUM before and after reading geometry,
when the device supports that command. Binding and bound reopening compare
the current value with that admitted generation; mismatch latches ENXIO even
before ordinary I/O fails. Bound open retains the handle under the registry
lock, releases that lock before device I/O, and drops the retained reference
on refusal. IOERR_NOCMD at initial sampling means no generation contract;
other errors are not silently ignored. Normal transfers do not acquire an
extra generation query, and this still is not continuous change monitoring.

The extended native test keeps two independently opened adapter handles. After
real virtual-device eject, bound reopen must reject the first by generation
before any failed I/O through that handle; the second exercises the native
DiskChanged error path. Native run kDAWPcaZ passed. Rebuilt
shared-engine.lTVDY2Qy / native-bundle.CJf9gLBS then passed the exact-package
run native-import-run.L3dtnGtO, including both required markers, normal
native pool/DOS regression and unchanged original hashes (runner exit zero).
Logs: backing-media-generation-20261002.log and
backing-media-generation-package-20261002.log on the external archive.
Generation wrap, simultaneous media replacement during I/O, and drivers with
no generation contract remain outside this proof. No change interrupt was
registered: Cause can queue callbacks, and safe completion before freeing a
registration has not yet been established for this consumer.

## Background generation monitor

The engine now starts a dedicated polling worker and joins it before engine
teardown (also on dispatcher startup failure). Roughly once per second, it
snapshots generation-capable admitted handles under the registry mutex,
retains them, releases the mutex, and queries device generations. Unbind may
proceed while a query waits; retained references keep handles valid until
each query completes. No DOS packets or pool mutations run in this worker.
Only the starting task can stop it; duplicate start is refused and successful
stop joins all outstanding work before permitting restart. Query failures are
rate-limited in diagnostics and do not silently establish a media change.

The native adapter test M7B64Uci passed idle-handle eject detection without
calling adapter I/O or reopen, followed by duplicate-start refusal, joined
stop, repeated stop and restart. Rebuilt shared-engine.3bJKQHDZ was packaged
as native-bundle.hzL5sEPD. Exact-package run native-import-run.3S3YEItt passed
the polling marker, library start/stop/reload lifecycle probe, broader native
pool/DOS regression and unchanged original hashes; runner exit zero. Logs:
external media-monitor-adapter-20261002.log and media-monitor-package-20261002.log.

This supersedes the admission-only monitoring limitation above for admitted
generation-capable handles. It does not discover new devices, implement
autoreplace, immediately update upstream vdev health before subsequent I/O,
or detect changes that a driver does not report. Hung device-query recovery,
generation wrap, concurrent replacement during a request, and physical
hotplug remain unqualified. No interrupt registration/lifetime assumption is
introduced, and no DUT or shared hosted session was used.

## Upstream removal notification integration

The monitor now accepts a callback after releasing snapshot references and the
device registry lock. Invalid handles remain eligible for later notifications,
so a busy namespace/configuration lock can be retried. The engine callback uses
spa_namespace_tryenter and spa_config_tryenter, scans native-backed root/cache/
spare leaves, sets vdev_remove_wanted, and requests upstream SPA_ASYNC_REMOVE.
It does not perform device close or pool mutation under the adapter mutex.

Native adapter run kFrdjLVW passed the required notification=1 marker after
actual virtual-media eject, joined stop/restart, broader native pool/DOS checks,
and original backing-file hashes (runner exit zero). This verifies callback
delivery, not removal of an actual pool member. The rebuilt engine/library is
shared-engine.evXux7Yu, staged as native-bundle.OyfDEylR. Separate package run
HsNZuULn passed with runner exit zero, including the callback marker, library
lifecycle probe, native pool/DOS regression and unchanged original backing
hashes. Log: external media-notification-package-20261002.log. This normal
pool regression must not be confused with a positive pool-member removal test.

Automatic callback-driven member removal remains unverified. The separately
verified explicit Manager removal below proves upstream evacuation and status
reporting, but does not prove that a media-change notification reaches the same
result. spa_async_request queues callback work; this upstream version dispatches
it from spa_sync. Idle read-only and suspended-pool handling therefore need a
separate solution/test before claiming prompt health updates for every pool.
Automatic discovery/replacement remains unfinished. Initial build failures from
using the private namespace mutex and an obsolete state constant were corrected
to the public namespace API and POOL_STATE_ACTIVE; both failed logs and
successful build-3 are retained on the external archive alongside
media-notification-adapter-20261002.log.

### Explicit Manager member evacuation

The production `pool.remove` route is now positively exercised instead of
inferred from the CLI path. On a disposable two-file pool, the test resolves the
original member's GUID through structured Manager status, submits operation 10,
and polls operation 4 until it reports `removal=finished`. The completed sample
reported 1,220,608 copied bytes, omitted the removed path, and retained the
surviving member. The test then deletes the removed backing file, imports from
the survivor alone in read-only and writable modes, and rereads the exact DOS
payload. No production change was required; the advertised route already
worked, while the prior test had silently used `zpool remove` for this phase.

Fresh source build `source-engine.l0VwjTcI` compiled all 248 upstream units.
Native run `native-import-run.JQiIVEVX` emitted
`RESULT native Manager removal PASS submitted=1 status-finished=1
copied-positive=1 topology-removed=1 survivor=1`, completed the wider pool/CLI
suite, and preserved both original QEMU backing hashes. The retained preceding
run `native-import-run.2r6wI4vQ` is a harness-selection failure: it did not set
`AROS_TEST_CLI_POOL_CREATE=1`, so the new marker gate ran without executing the
scenario. It is not a removal failure. This milestone does not qualify automatic
hotplug, read-only/suspended callback dispatch, physical media, or the DUT.

## Emulated removable-member baseline

AROS_TEST_NATIVE_BUS=usb selects two disposable USB mass-storage members on a
private QEMU OHCI controller. The readonly mirror fixture uses usbscsi.device
for explicit admission and saved-record reimport, and starts the normal USB
class scan. It currently rejects writable, RAIDZ, GUI, GPT and zvol test modes;
the established IDE paths are unchanged. Run Xvw7yVQU passed two imports,
authenticated DOS readback, mutation refusal, library lifecycle and original
backing-file hash checks with runner exit zero. This is a real emulated USB
transport baseline, not yet the unplug/health-state test. Log:
external usb-member-import-3-20261002.log. Earlier Dr91n2cS was refused by the
writable-only bundled CLI fixture, and T1UywMHJ failed before guest boot because
the USB controller appeared after its devices in QEMU arguments; both retained.

### Readonly member unplug: failing control and corrected dispatch

AROS_TEST_USB_REMOVE=1 removes ozmember0 through the isolated QEMU monitor
after the second readonly import. The guest queries health without file I/O,
requires DEGRADED within 30 seconds, then performs the existing exact DOS reads
and exports. L7XFIGoj failed the health gate with the previous library while
the QEMU transcript showed only the surviving USB device. This exposed the
sync-driven async-dispatch limitation above.

The AROS-only spa_async_remove_notify entry point now queues removal and kicks
the existing upstream async worker while namespace protection holds. On pools
without a sync thread, the worker takes only removal tasks, retaining unrelated
queued work. Readonly removal updates in-memory state but does not add dirty
metadata. Existing suspend/join gates remain in force. The monitor retries
pending removal on later polls if a worker was already finishing.

Rebuilt shared-engine.FRLAVWqs passed the same positive test in ku11pHBO with
runner exit zero: idle health became DEGRADED, subsequent DOS reads were exact,
export succeeded, and original plus overlay hashes stayed unchanged. Logs:
external usb-member-remove-20261002.log (failure) and
usb-member-remove-2-20261002.log (pass). This supersedes the readonly dispatch
gap for this tested mirror. It does not qualify writable/suspended removal,
all-members-lost recovery, reinsertion/autoreplace or physical hotplug.

### Writable mirror member unplug

AROS_TEST_USB_REMOVE=2 performs the second saved-record import writable. After
QEMU removes member zero, health must become DEGRADED before file I/O. The
guest creates usb-survivor.txt, flushes/closes it, detaches and exports, then
reimports readonly using the original member paths and verifies exact contents
plus EOF through the survivor. Original raw backing hashes remain mandatory;
only disposable child overlays may change.

TavdV4Pn passed with runner exit zero against shared-engine.FRLAVWqs. Both
overlays changed during writable import/use, while both original hashes stayed
unchanged. This proves same-guest export/reimport, not a fresh boot or abrupt
power loss. The earlier oKq08B67 reached write/flush/export but its test supplied
an unrelated absent unit 100 on reimport; admission returned EACCES. Correcting
the fixture to retain the pool's original device identity closed that failure
without changing production admission policy. Logs: external
usb-member-writable-remove-20261002.log and its -2 follow-up.

The updated full package native-bundle.zcfFesJG separately passed normal native
pool/DOS, media callback and library lifecycle regression kOAWr0wn with exit
zero. Log: external media-remove-dispatch-package-20261002.log. This remains an
incremental test package, not the clean ADT release qualification.

### Fresh-boot survivor recovery

AROS_TEST_USB_COLD_SOURCE selects a completed writable-removal run, requires its
success markers, and verifies its original backing hashes. A new QEMU guest
receives only a readonly child of the retained survivor plus a newly generated
empty GPT peer with no partitions or previous ZFS labels. Two imports must
report DEGRADED and read usb-survivor.txt with exact bytes and EOF through DOS.
The retained survivor, original raws, empty peer and new child overlays must
remain unchanged during this run.

8H1IrfA6 passed both cold-read markers and all hashes with runner exit zero,
using TavdV4Pn as its retained writer. Log: external
usb-survivor-cold-2-20261002.log. Initial KYpt8buW used an entirely unformatted
peer; partition admission failed EIO before pool GUID validation. Giving the
empty peer a valid GPT (no partitions) matches the established degraded-device
fixture and required no production admission changes. This verifies orderly
export then a fresh guest, not crash/power-loss recovery or autoreplacement.

### Same-device readonly reinsertion

AROS_TEST_USB_REINSERT=1 with USB_REMOVE=1 reattaches the same overlay after
confirmed removal. The fixture now assigns explicit USB serials and ports,
recreates QEMU's auto-deleted legacy drive backend, checks monitor errors and
requires the reinserted device in info usb. After enumeration time, existing
pool health must still be DEGRADED. Only export and fresh admitted import may
return ONLINE; ordinary exact DOS reads and all unchanged-image checks follow.

9VmlVTPD passed with runner exit zero. Earlier WDxPP3xf did not reinsert at all
because QEMU had deleted the drive backend. c26qT0sF did reinsert, but at a
different port with default identity, and fresh import returned EACCES. The
passing explicit-identity case does not establish relocation/reidentification
or automatic replacement. All failures are retained in external
usb-member-reinsert-20261002.log and -2; the pass is -3. No production admission
policy was weakened and old invalid handles were not reactivated.

### Explicit relocation admission

USB_REINSERT=2 deliberately changes the emulated port and serial, producing
usbscsi.device unit 2. Existing pool state remains DEGRADED. After export, a
request admitting only original units 0/1 must return EACCES; a fresh request
explicitly admitting 2/1 must import ONLINE and pass exact DOS reads. Upstream
label identity/path reconstruction operates inside the caller-admitted set;
this is not permission for arbitrary path discovery.

1nNgXHNl passed explicit unit-2 import. QQQ64HmE adds the same-run old-path
refusal and also passes with exit zero, unchanged original/overlay hashes and
the required old-path-refused=1 marker. Logs: external
usb-member-relocate-20261002.log and -2. This explains the earlier default-path
EACCES without weakening production policy. Explicitly admitted relocation is
now qualified for this readonly mirror fixture; automatic arrival discovery,
replacement/resilver policy and physical transport variations remain separate.

### Total readonly member loss

USB_ALL_LOST=1 requires same-identity reinsertion mode and removes both emulated
members after import. The guest requires an UNAVAIL response from management
while neither disk is present. Reinserted media must not silently reactivate
old admissions: health stays UNAVAIL until export/fresh import, after which it
must be ONLINE and ordinary exact DOS reads must succeed.

8rGDpbOh passed with runner exit zero, explicit total-loss and reinsertion
markers, successful export, and unchanged original/overlay hashes. Log:
external usb-total-loss-20261002.log. This establishes responsiveness and
recovery for orderly readonly loss. It is not an in-flight writable I/O,
failmode/suspended-pool or abrupt power-loss test; those remain separate gates.

### Production storage inventory QEMU regression (2026-10-02)

`device-catalog-test.c` links the existing `aros/platform/storage-inventory.c`,
not an alternative enumeration implementation. Build it with the AROS SDK,
then supply the resulting executable as `AROS_TEST_DEVICE_INVENTORY` to the
native-import runner. The fixture requires both IDE members and checks unit
0 and 1, inventory version 1, and a successful native enumeration return.
The executable hash is retained in `inventory.sha256`.

External run `native-import-run.I5NDBMWc` passed (runner exit 0), reporting
ata.device units 0/1 (hard disks) and 2 (CD-ROM), with model/serial metadata.
The combined writable pool/DOS export-reimport regression also passed.
Log: `/mnt/openzfs-artifacts/device-inventory-qemu-3-20261002.log`.
This verifies the production enumeration function, not a new UI feature or
all USB/legacy drivers, concurrent hotplug, or the authorization transport.

An initial redundant probe stopped after the first unit because it returned
TRUE to continue; AROS requires FALSE. Failed run R8uzcVla and corrected
probe run n2lc326L are retained. Source inspection then identified the existing
production implementation and UI integration; the duplicate implementation
was removed rather than shipped. No production callback bug was demonstrated.

### Inventory client response validation (2026-10-02)

The inventory client now rejects duplicate device/unit identities as EPROTO,
matching the native provider's unique-identity contract. Ten malformed worker
responses cover excessive count, unterminated device/model/serial, reserved
fields, out-of-range unit, invalid removable flag, empty device, wrong version,
and duplicate identity. Rejected replies leave the caller snapshot unchanged.
The existing delayed-reply, busy-free, permission-denied and absent-endpoint
checks remain. This is native Exec transport testing with a private fixture
port, not a replacement for public service authorization tests.

QEMU run `native-import-run.qI2CUFkY` completed with exit 0, including all ten
malformed cases and the full writable pool/DOS export-reimport regression.
Log: `/mnt/openzfs-artifacts/inventory-worker-validation-20261002.log`.
The standalone test executable contains the updated client. Installed Manager
and release packages still require rebuilding before this change is deployed.

Manager follow-through: rebuilt `inventory-manager-20261002/OpenZFSManagerNative`
and tested in private QEMU `native-import-run.gQyzR5fC`. The live-service Devices
window scanned three devices, selected ata.device/0, inspected its GPT OpenZFS
partition (sectors 4097..528384, 512-byte sectors), and refreshed inventory.
Both windows closed normally and the runner completed with exit 0. Exact
artifact identity is retained in `manager.sha256`, input actions in
`ui-actions.jsonl`, and screenshots `manager-scan.png`, `manager-inspect.png`,
and `manager-refresh.png` in that run directory. This is a rebuilt application
verification; distribution staging is still pending. The screenshot hash for
manager-inspect.png is recorded in the relay evidence along with the run log.

### Writable total-loss recovery: failing qualification (2026-10-02)

`AROS_TEST_USB_ALL_LOST_WRITABLE=1` requires USB_REMOVE=1, USB_ALL_LOST=1
and USB_REINSERT=1. It imports the second cycle writable, completes pool sync,
unplugs both USB mirror members, observes unavailable health, reinserts both,
then attempts explicit clear before export/readonly reimport. Only private
qcow2 overlays may change; original backing hashes remain protected.

This test is currently RED, not a supported recovery claim:
- TZ0u1dXk failed the readonly assumption that health remains UNAVAIL.
- INzu7oWn added actual-state diagnostics and proved health was SUSPENDED.
- GHPV2bls explicitly cleared the suspended pool after reinsertion; the service
  returned EIO (5), so the runner failed at the clear-success assertion.

Logs are external `usb-writable-total-loss[-2|-3]-20261002.log`; all runners
terminated with exit 1 through guest failure markers, not observation timeouts.
The binding adapter rejects media_changed handles with ENXIO even on reopen;
that intentional safety invariant must not be removed to force this test green.
Next work is explicit, identity-validated readmission for active suspended pools,
including wrong-media rejection and safe handling of retained old references.
In-flight user-write loss and power-loss persistence remain separate tests.

### Suspended export preflight (2026-10-02)

Management export/destroy and the CLI export preflight now return EAGAIN if
the named pool is already suspended, before entering upstream export's
txg_wait_synced. This keeps that known condition from monopolizing the
serialized service. It does not guarantee safety against suspension racing
after preflight, and it does not readmit reinserted members.

`AROS_TEST_USB_SUSPEND_GUARD=1` is a separate negative safety test layered on
the writable total-loss fixture. Jzf3zrXn passed: after reinsertion, management
export and destroy returned EAGAIN and another health request still returned
SUSPENDED. Its final marker explicitly says safety only, not pool recovery.
The CLI guard shares the check but was not separately exercised in this run.

The same fixture now also submits authenticated pool-history operation 33 in
summary and raw modes before export/destroy. Both reads must return EAGAIN with
zero rows and zero next cursor, and the subsequent health query must still
report SUSPENDED. This closes the history adapter's suspended-pool response
gap without changing or recovering the pool. Clean run `aJUTmelE` passed on
the default two-vCPU `pci-ohci` topology with both original backing hashes
unchanged. Clean 16-vCPU attempts `eanLyphF` and `9H1qDeFu` timed out during
initial USB import before a removal marker; neither reached the history check,
so 16-vCPU USB startup remains unqualified rather than being reported as a
history failure.
Companion regression 0VGwM6h8 passed single-member removal, survivor write,
flush, export/reimport and exact readback using the same rebuilt engine.
Both runners exited 0; original backing checksums passed.

Engine: external `suspended-export-20261002/full-engine.o`, shared candidate
`shared-engine.ps7nXV5A`. Logs: external `suspended-export-build-20261002.log`,
`suspended-export-library-20261002.log`, `suspended-export-qemu-20261002.log`,
and `suspended-export-survivor-regression-20261002.log`.
The separate writable total-loss recovery regression remains RED.

### Explicit suspended member readmission (2026-10-02)

The previously RED idle writable total-loss case now passes with candidate
`shared-engine.2w6IhDbi` (engine object `readmission-20261002/full-engine.o`).
Confirmed pool clear may readmit invalid native bindings of an already imported
pool. It opens the same explicit device/partition readonly, uses the upstream
label reader, rejects conflicting decodable labels, and requires matching pool
and leaf GUIDs plus ACTIVE state. Binding replacement additionally requires the
same device name/unit, extent base/length and sector size, a fresh media check,
no DOS claim for writable access, and no conflicting registered extent.

Replacement transfers the registry reservation to a fresh handle; old retained
references remain media-invalid and cannot resume I/O. AROS vdev reopen closes
the invalid file handle and opens through the admitted binding; upstream then
validates the reopened vdev. Managed imports now retain their discovered pool
GUID, as the CLI path already did. No arbitrary scan or automatic readmission
was added. GUID matching is ZFS identity validation, not cryptographic proof
against a maliciously cloned disk. A later member failing validation may leave
earlier independently validated bindings refreshed, but no resume is requested.

Evidence, all runner exit 0:
- Wpg7rt2S: sync, unplug both writable members, SUSPENDED, reinsert, explicit
  clear, ONLINE, export/readonly reimport, exact DOS baseline readback.
- eTM6b6E4: swap both member images on reinsertion; clear returns ESTALE,
  health remains SUSPENDED. Both overlays hash-identical to immediately before
  reinsertion, and original backing hashes unchanged.
- Companion survivor regression in `readmission-survivor-qemu-20261002.log`:
  single-member loss, new data write/flush, export/reimport and exact readback.

Logs are external `readmission-build-3-20261002.log`,
`readmission-library-20261002.log`, `readmission-qemu-20261002.log`,
`readmission-swapped-qemu-20261002.log`, and the survivor log above. Earlier
failures remain retained. This is same-path idle mirror recovery, not relocated
member recovery, cache/spare readmission, arbitrary concurrent replacement,
in-flight user-write loss, physical power-loss durability, or a release claim.

### Repeated readmission lifecycle (2026-10-02)

`AROS_TEST_USB_RECOVERY_ROUNDS=3` repeats the writable total-loss fixture in
one private guest, importing writable between rounds and readonly after the
last round. The runner checks the exact number of hotplug and recovery events
and retains separate per-round monitor logs. Values 1..3 are accepted; repeated
rounds require writable total loss without the negative guard/swap variants.

Run `native-import-run.62YMk32q` passed all three sync/remove/reinsert/explicit
clear/export/reimport cycles with candidate `shared-engine.2w6IhDbi`. Final DOS
baseline readback and original backing hashes passed; runner exit 0. This
exercises repeated handle/reservation replacement but is not a heap-leak proof
or an in-flight write-loss test. Readback is after the final round, not each round.

The first attempt `native-import-run.MOnJXkiM` exited 1 because the test required
observing transient UNAVAIL before SUSPENDED. A writable pool can suspend before
the first poll. The test now accepts either loss state, while still requiring
SUSPENDED after reinsertion and ONLINE after explicit clear. Both attempt logs
are retained externally as `readmission-repeat-20261002.log` and
`readmission-repeat-2-20261002.log`; no production behavior was changed.

### Suspended clear target validation (2026-10-02)

Clear now rejects an unknown nonzero vdev GUID under the config reader lock
before attempting native member readmission. The existing lookup under the
state lock remains. Previously even an invalid target could refresh bindings
before being rejected, or return a member-admission error instead of ENODEV.

Candidate `shared-engine.LlMKy8ot`, engine `clear-target-20261002/full-engine.o`:
- `9ArFbmlD`: three unknown-target refusals left the pool SUSPENDED; each was
  followed by successful pool-wide clear and recovery. Final DOS readback and
  original backing hashes passed, runner 0.
- `lGOBOQAi`: unknown target ENODEV, then swapped-member pool-wide clear ESTALE,
  still SUSPENDED; overlays and original backings unchanged, runner 0.
- Earlier `NKMoRCGO` failed because the test put the GUID in options instead
  of argument. Corrected test `zHG3h55b` passed the unknown-target check but
  returned ENODEV on the following pool-wide clear rather than expected ESTALE.
  Its cause remains unresolved; the passing repeat does not diagnose it.

External logs: `clear-target-build-20261002.log`,
`clear-target-library-20261002.log`, `clear-target-recovery-20261002.log`, and
`clear-target-swap-20261002.log`, `clear-target-swap-2-20261002.log`,
`clear-target-swap-3-20261002.log`. Pre-change swapped-member regression
`91ziKAiv` also passed (`readmission-repeat-swap-20261002.log`).

### Bundle regression and failure attribution (2026-10-02)

Full staged bundle `native-bundle.Q8RQvzcS` (engine LlMKy8ot) passed private
QEMU `czYT4f0N`, including writable DOS/export/reimport persistence, standalone
handler mounting, trusted startup imports, and unchanged original backing
hashes. Logs: external `clear-target-bundle-stage-20261002.log` and
`clear-target-bundle-qemu-20261002.log`. This is the combined bundle regression,
not a new GUI visual validation or stock-nightly qualification.

Readmission now emits one failure-only diagnostic naming the failed stage,
errno and explicit member path. Stages distinguish binding snapshot, vdev path,
path parsing, device opening, labels and binding replacement. Successful I/O
does not gain per-request logging. This does not by itself fix the earlier
intermittent ENODEV observation.

Candidate `shared-engine.irj2p9ec` passed swapped-member run `XpjuxILO`:
unknown-target ENODEV followed by pool-wide ESTALE, attributed to
`stage=member-labels errno=70`; suspended health and both overlay/backing hash
checks passed, runner 0. Logs: external `readmit-diagnostic-build-20261002.log`,
`readmit-diagnostic-library-20261002.log`, and
`readmit-diagnostic-swap-20261002.log`. Input manifest hashes were checked.

### USB readiness observation (2026-10-02)

Repeat `OmxBkocI` reproduced the unexpected ENODEV with failure attribution:
`stage=device-open errno=19 path=/AROS/usbscsi.device/0/partition/0`. It did not
reach label validation. The batch stopped on that first failure; later planned
runs were not executed. Log: `readmit-diagnostic-repeat-1-20261002.log`.

The writable total-loss fixture now queries both explicit units through the
existing read-only partition API after reinsertion, requiring partition 0 on
each before testing clear. It records errors for up to 30 observations, one
second apart, then fails if still unavailable. This is readiness evidence only,
not disk identity validation or readmission. The engine's GUID/extent checks
remain required; no automatic production retry or alternate-unit scan is added.

First readiness run `Pf1g8Kfz` passed both catalogs, rejected the invalid clear
target, then rejected swapped labels with ESTALE; overlay/backing hashes passed,
runner 0 (`readmit-readiness-swap-20261002.log`). This passing run does not prove
that the earlier missing-device condition was merely a timing delay. Independent
repeats are needed to distinguish delayed enumeration from persistent absence.

Readiness repeats: `Wkl8nhhw` passed, but `oMIQWdlD` returned ENODEV from both
explicit unit partition queries for all 30 observations. QEMU monitor logs list
both attached devices. The guest did not proceed to clear or label validation;
the batch stopped and the third run was not executed. This is a persistent
path-unavailability observation, not proof of a short enumeration delay or of
an OpenZFS label-validation defect. Logs `readmit-readiness-repeat-1-20261002.log`
and `readmit-readiness-repeat-2-20261002.log` preserve both outcomes. Root cause
remains unresolved in the USB/partition open path.

### Created-pool admission identity (2026-10-02)

`oz_cli_import_end` now records the actual SPA GUID while holding the namespace
lock when retaining a successful import/create reservation. Create reservations
start before a GUID exists; leaving their GUID zero caused the readmission
lookup to skip them until export/reimport. Existing nonzero import GUIDs still
must match; successful creation is not inferred solely from the returned errno.

Candidate `shared-engine.uvlE0q44` passed private QEMU `gdVSwOsV`: typed creation
on a fresh disposable GPT partition, unauthorized/unconfirmed/nonblank refusal,
DOS writes, sync, export, readonly import and 1 MiB payload readback. The runner
also passed independent host zdb verification, GPT verification and original
backing hashes. Logs: external `created-identity-build-20261002.log`,
`created-identity-library-20261002.log`, `created-identity-qemu-20261002.log`.
Input hashes match the engine object. This regression proves creation/persistence
still works; it does not yet exercise loss/recovery before the first export.

Independent cold guest `JXBHEZUM` also exited 0: partition-only readonly import,
normalized lookup, exact 1 MiB payload readback and export; source and all guest
overlay hashes unchanged. Log: `created-identity-cold-20261002.log`. This closes
the cold persistence regression for this candidate, not the remaining live
created-pool recovery case.

### Live created-pool recovery fixture: failing (2026-10-02)

`AROS_TEST_USB_CREATED_RECOVERY=1` provisions two fresh blank GPT images behind
disposable overlays, creates a native mirror through the typed API, writes and
flushes a DOS payload, then removes both members before any export. It requires
explicit clear, first export only after recovery, readonly reimport and exact
payload readback. Its final marker is distinct from the baseline import test.

First run `nkXntTKO` failed an inherited expected-GUID assertion that assumed
preexisting labels; that assertion is excluded only for this blank-disk mode.
Run `0RZx12w2` reached successful creation and flushed DOS write, then removal.
Subsequent health requests returned OZ_TIMEOUT (status 6), not a pool health
state. The runner exited 1 before reinsertion/clear. This is an unresolved
newly-created-pool loss failure; it does not verify the GUID fix's recovery path
or establish a cause. Logs: external `created-live-recovery-20261002.log` and
`created-live-recovery-2-20261002.log`; serial/completion/monitor logs retained
under each run directory. No DUT or user hosted session was accessed.

Diagnostic run `4zO4Dv3h` with `AROS_STATUS_TRACE=1` reproduced the timeout.
The last health trace is `namespace-enter` without `namespace-held`; the request
does not reach upstream stats. This localizes the blocked query to namespace
acquisition, not the holder's cause. Candidate `shared-engine.ooAa7rfG` and
`created-status-trace-qemu-20261003.log` are diagnostic evidence, not release
artifacts. The default-off compile flag traces only status stages and, in the
next diagnostic build, async config-update namespace ownership/update entry and
return. Async config update is a hypothesis until that trace establishes it.

### Native removal batch ordering (2026-10-03)

Follow-up commit `306d36f` addresses configuration work that already owns the
namespace when suspension occurs: async configuration uses the upstream
TXG_WAIT_SUSPEND waiter, releases the namespace on suspension, and retains the
request for explicit recovery. The synchronous path remains blocking. Original
core negative control `qemu-run.uf4dpAv6` returns EBUSY; candidate
`qemu-run.y2fUJ5b0` passes the diagnostic and complete five-boot mirror suite.
See `../distribution/README.md` for provenance and test boundaries.

Production `shared-engine.uZ0vOvfz`, packaged in `native-bundle.nNoj0oRw`,
passes created-pool USB total-loss recovery in `native-import-run.QuUqvPKM`:
responsive health, both partition catalogs available after reinsertion,
invalid clear-target refusal, explicit clear ONLINE, first export and exact
DOS read after readonly reimport. Runner exit 0 and original backing hashes
pass. The earlier `GsR51TE3` attempt stopped before compilation because bundle
CLI mode and USB recovery mode have incompatible writable settings; the
successful test selects the exact packaged service/library without CLI mode.
External log: `async-config-usb-created-2-qemu-20261003.log`. Evidence registered.
One passing created-pool recovery does not erase the earlier intermittent
readiness failures or establish repeated imported-pool/hardware recovery.

The subsequent imported-pool run `native-import-run.zMJ9ynlF` passes three
consecutive total-loss/reinsertion/explicit-clear/export/reimport cycles with
the same production engine. Each cycle observes both returned partition
catalogs, retains suspension until explicit clear, and returns ONLINE. Final
DOS reads and original backing hashes pass; runner exit 0. Log:
`async-config-usb-imported-three-qemu-20261003.log`. This completes that specific
three-cycle regression, not a proof that intermittent OS USB readiness faults
are eliminated. No physical hardware was used.

Wrong-member safety run `native-import-run.UqtasdQa` reinserts the two
members at swapped device identities. Explicit clear returns ESTALE and the
pool remains suspended; both swapped overlay hashes and original backing
hashes remain unchanged. Runner exit 0. This is a successful refusal test,
not successful recovery. Log: `async-config-usb-wrong-member-qemu-20261003.log`.

The 16-vCPU repeat `native-import-run.w1v96Lry` stopped producing serial output
during the optional library-lifecycle preflight, after seven cycles and the
eighth denied child's exit, before USB recovery began. Two live monitor samples
show CPU 0 at `KrnSpinLock+0x97`, called from `nommu_FreeMem`/`FreeMem`; captured
code matches the ISO kernel at text relocation `0x1aaf000`. Lock `0x100013100`
contains `0x08000000` and owner `0x1086a40`, whose name reads `Lib & Dev Loader
Daemon`. Sampled CPUs 1 and 2 are halted. The monitor, code bytes, kernel
reference and owner memory are retained in that run. This localizes the wait,
not its root cause. Relay `the platform issue tracker` requests OS-lane correlation; no shared
OS source was changed. An independent 16-vCPU recovery-only run omits the
optional lifecycle preflight to discriminate the two paths, not to claim that
the combined run passed. Both runner results are still pending at this entry.

Independent run `native-import-run.ctOSFwu8` omits that optional preflight
but also stops emitting serial during service pthread creation (last thread
58), before USB removal. Its CPU 0 is at the same instruction and memory lock;
the recorded lock owner is now `aros_taskq_spawn` (`0x122f890`), not the loader
daemon. This rules out the optional lifecycle test as the sole trigger, not
every OpenZFS contribution. `monitor-sample-1.log`, `lock-stack.log` and
`owner-name.log` preserve the observation. Caller return addresses above
FreeMem are `0x1ebe8da` and `0x1ec0648`, not yet symbolized. Both owned runners
remain under their original 1800-second deadlines; neither result is a pass.

Further byte matching identifies those caller addresses in the actual ISO
`pciusb.device` (text relocation `0x1ebb300`). Its matching `ohci_hcd.o` names
the local functions: `ohciFreeEDContext` (object offset `0x61a`) and
`ohciCompleteInt` (`0x2388`). The call path is OHCI completion interrupt,
ED-context cleanup, `usbReleaseBuffer`, `FreeVec`, `FreeMem`, memory spinlock.
The buffer release frees a bounce allocation; the SMP allocator's MEM_LOCK
uses Forbid plus a spinlock, not interrupt masking. An interrupt reentering
the task-owned memory lock is the leading explanation. All other 15 CPUs
sampled halted while CPU 0 spins. Exact module/object copies, caller code bytes
and symbolized offsets are retained in `ctOSFwu8`. Relay `the platform issue tracker` supplies
this refinement to the OS lane; no OS fix is claimed or applied here.
The first run `w1v96Lry` has now ended at its 1800-second harness deadline
(exit 1, no guest completion). The independent run is still pending.

CPU-count/RAM discrimination, same production engine and OHCI fixture:

| vCPUs | RAM MiB | Evidence | Result |
| --- | ---: | --- | --- |
| 2 | 2048 | `zMJ9ynlF` | Three recovery cycles pass, lifecycle preflight included |
| 16 | 8192 | `w1v96Lry` | Memory-spinlock stall in lifecycle preflight; runner timeout |
| 16 | 8192 | `ctOSFwu8` | Same lock/FreeMem/OHCI path without lifecycle preflight; runner timeout exit 1 |
| 16 | 2048 | `XoMPg68b` | Three recovery cycles pass without lifecycle preflight, runner 0 |
| 2 | 8192 | `rmkPJJdM` | Same RIP `0x1ac5c3c`, lock and FreeMem/OHCI caller chain during startup; runner timeout exit 1 |

The last two cases separate CPU count from guest RAM size: high CPU count
alone does not explain the observed failure. `usbGetBuffer` allocates bounce
storage for buffers above 4 GiB and `usbReleaseBuffer` frees it in completion.
The above-4-GiB bounce path is therefore a stronger lead, not a demonstrated
fix. `XoMPg68b` has final exact DOS reads, unchanged backing hashes and registered
evidence. `rmkPJJdM/highram-monitor.log` preserves the independent low-CPU
high-RAM stall. No memory-limit workaround was added to production.
The two-CPU run also reached its 1800-second deadline without guest
completion. Its final evidence is registered as late-backfill; timeout alone
does not establish a crash, while the retained monitor sample identifies the
same memory-lock wait. It never reached the USB-removal recovery workload.
The independent `ctOSFwu8` run has now reached its original 1800-second
deadline; final evidence, including all-CPU snapshots and the exact matching
USB module/object references, is registered to the development test archive.

`sjAww8o2` confirmed async config update had acquired the namespace and entered
`spa_config_update` without returning while health waited on namespace entry.
The native notification dispatched all pending async bits; for a newly created
pool this included config update, which waits for TXG sync before removal ran.
Log: `created-async-trace-qemu-20261003.log` (runner 1).

On AROS, an async batch with REMOVE pending now processes REMOVE alone and leaves
other bits queued for subsequent dispatch. The readonly restriction remains.
This prevents native removal dispatch from first entering unrelated config work;
it does not prove absence of every race with an already-running config update.

Diagnostic candidate `shared-engine.pHjJs6RD`, run `GOIkrBIG`, passed created-pool
loss before first export: removal-only batch observed, responsive health,
partition readiness, invalid target rejection, explicit clear ONLINE, first
export, readonly reimport and exact DOS payload readback. Original backing hashes
passed and runner exited 0. Logs: `remove-first-build-20261003.log`,
`remove-first-library-20261003.log`, `remove-first-created-qemu-20261003.log`.
Trace-disabled regression is still required; this result uses opt-in tracing.

Trace-disabled candidate `shared-engine.Kwc6QfY9` passed the same created-pool
recovery in `E7A9hgbZ`, runner 0, exact DOS readback and original backing hashes.
Input hashes matched and diagnostic trace strings were absent from the library.
Log: `remove-first-release-created-2-20261003.log`. The first trace-disabled
attempt `HWDlHCwm` kept health responsive but failed both reinsertion partition
queries with ENODEV through 30 observations; it did not reach clear. That failure
is retained in `remove-first-release-created-20261003.log` and is not erased by
the passing repeat. Failure-only fixture diagnostics now query geometry of
explicit units 0..7 without adopting them, to distinguish reassignment from
unavailable devices on a future recurrence. That diagnostic branch was not
reached in the passing repeat.

Imported-pool regression `chMrVOLt` with the same trace-disabled candidate
failed readiness before clear. After both partition paths remained ENODEV,
read-only geometry probes of explicit units 0..7 all returned ENODEV, with
zero output capacity. This does not support reassignment within that tested
unit range; it does not establish why the USB paths are unavailable. No
alternate path was admitted. Log: `remove-first-release-imported-20261003.log`.
The intended three-cycle imported regression therefore remains incomplete.

### Removal-ordering bundle and diagnostic staging policy (2026-10-03)

Trace-disabled bundle `native-bundle.B53Azg4F` with engine `Kwc6QfY9` passed
private QEMU `PsSIbbUg` (runner 0): writable DOS persistence across export and
reimport, readonly reads, standalone handler/startup regressions and original
backing hashes. Logs: `remove-first-bundle-stage-20261003.log` and
`remove-first-bundle-qemu-20261003.log`. This does not resolve USB reinsertion.

New management builds record `status_trace`; shared-library builds preserve
that engine metadata rather than inferring it from wrapper environment. Staging
rejects a nonzero trace setting and checks selected service/library diagnostic
strings to catch older trace candidates without the metadata. The staging-policy
suite passed, including the new status-trace refusal and unchanged input hashes
(`status-trace-stage-policy-20261003.log`). Actual legacy diagnostic candidate
`eaYGORrS` was refused with exit 2 before bundle creation
(`status-trace-legacy-refusal-20261003.log`). Shell syntax checks passed.

### USB stack failure snapshot and controller comparison (2026-10-03)

The writable USB loss fixture now snapshots up to 64 recent Poseidon error
records only after reinsertion readiness fails. Strings are copied under the
stack read lock and printed after unlocking; no USB configuration is changed.
Private QEMU `XAocSp7y` compiled and exercised this branch: two complete recovery
rounds passed, but the third had no subsequent reconnect records after removal,
and units 0..7 returned ENODEV. QEMU accepted both device additions. The guest
reported failure at line 543 and the runner exited 1; the third clear was not
attempted. Log: `usb-stack-errors-2-qemu-20261003.log`, with full serial and host
hotplug records in `build-aros/native-import-run.XAocSp7y`. This narrows the
failure to device availability, not label rejection, without proving its cause.
The initial diagnostic compile failure is retained separately in
`usb-stack-errors-qemu-20261003.log`.

`AROS_TEST_USB_CONTROLLER` permits `pci-ohci` (unchanged default) or `qemu-xhci`
for isolated controller comparisons with otherwise identical pool tests. A
passing alternate controller must not be reported as fixing the OHCI failure.

The xHCI comparison is also failing, with a different observed symptom:
`Gc3iMmC3` completed one recovery then faulted during removal two in task
`massstorage.class<015133A0,0>` (CR2 `0x24`, IP `0x1ede974`). Repeat `LlKgGmaO`
completed two recoveries then faulted during removal three in task
`massstorage.class<015133B0,0>` (CR2 `0x801`, IP `0x1ed992d`). Both runners
exited 1 on the guest fault diagnostic. Logs: `xhci-recovery-qemu-20261003.log`
and `xhci-recovery-repeat-qemu-20261003.log`. Task identity establishes the
faulting context, not the offending source function or corruption origin.
No shared OS source was changed. Repeated removal remains unqualified on both
tested controllers; xHCI is not a workaround. The native OpenZFS adapter uses
synchronous, worker-owned IORequests and does not register TD_ADDCHANGEINT;
that source inspection does not rule out all adapter/driver lifetime races.

A later exact-source boundary test separates one-member loss from that repeated
failure. Run `native-import-run.4WJOwayA` at source `00b6b266` uses the packaged
service/library from `native-bundle.58W5Goph`, qemu-xhci, two virtual CPUs and
2 GiB RAM. It removes member 0 once, observes DEGRADED health, writes and
flushes a new file through member 1, exports/reimports degraded and rereads the
payload exactly. The final two import cycles, completion line zero, zero-byte
source patch and both pristine original backing hashes passed.

Fresh-guest run `native-import-run.IaXGd7UY` replaces the missing member with a
new blank disk and places a child qcow2 overlay over the retained survivor.
Both readonly import cycles reread the exact survivor payload; final imports,
completion line zero, the full backing chain and unchanged cold overlays all
passed with a zero-byte source patch. This qualifies one xHCI member removal,
degraded committed write and independent cold-survivor persistence. It neither
qualifies reinsertion/resilver nor contradicts the retained repeated-removal
faults above. No physical hardware was used.

Exact-source run `native-import-run.SvpNSJHI` at `c35d273c` adds one bounded
writable total-loss cycle on the same qemu-xhci, two-vCPU, 2 GiB topology. It
syncs before loss, removes both members, observes responsive UNAVAIL or
SUSPENDED health, reinserts both same identities and verifies both partition
catalogues. An invalid clear target returns ENODEV without resuming the pool;
the subsequent explicit pool-wide clear returns ONLINE. Export, fresh import,
ordinary DOS read, two final import cycles, completion line zero and both
protected original backing hashes pass with a zero-byte source patch. Writable
qcow2 overlays change as expected. This qualifies one idle total-loss and
same-identity readmission cycle only; it does not qualify repeated removal,
in-flight write loss, relocated media or physical hardware.

Paired exact-source safety run `native-import-run.1FeR3xRE` at `22308532`
repeats one total loss but swaps the two media identities on reinsertion. Both
partition catalogues become readable, the invalid-target clear again returns
ENODEV while suspended, and the pool-wide clear rejects the mismatched labels
with ESTALE without resuming the pool. Completion line zero, the zero-byte
source patch, both protected raw backing hashes and both swapped qcow2 hashes
pass. This is a wrong-member refusal proof, not a recovery claim.

Current-ADT follow-up on 2026-10-06 supersedes the historical repeated-xHCI
qualification boundary above for the exact tested stack. The earlier focused
ISO overlays replaced root-tree copies of `pcixhci.device` and
`massstorage.class`, but the booted USB modules actually came from
`boot/poseidon.pkg.xz`; those attempts therefore did not test current ADT USB
code. The corrected package embeds clean modules rebuilt from ADT source
`cdafb29defb00a3447619ea19fa5081d5f4927dd`: `pcixhci.device` SHA256
`00409ab096cb12ee58ea27a990cecb2ed19bee7e3fd66cf0c29f1bf29edcef67`
and `massstorage.class` SHA256
`4f2577b9ca0d4cccd3e3774346e4b7e842db54c129c48b70a0941bd1c0a6ce92`.
Verified package `adt-poseidon-pkg.GOESypoQ/poseidon-current.pkg.xz` has SHA256
`e4c0227581e7c79eac704f76dcf6787ecd7a7bae144b2581351a8f7a35106b3a`;
the bootable hybrid image `adt-poseidon-iso.YFZydTXS/adt-poseidon-current.iso`
has SHA256
`61e5065be9613538483dfeef32db62aac009249a763f68e6b3f91aa882f3c2aa`.
Extraction from the finished image reproduces the package hash, and unpacking
the package reproduces both module hashes.

Exact OpenZFS source `c8c335b2` first passed one full current-module cycle in
`native-import-run.wAn8xZZA`. Run `native-import-run.PHYZjrnP` then passed
three consecutive total-loss/reinsert cycles at SMP2, and
`native-import-run.tPCDHGRG` passed the same three-cycle sequence at SMP8.
Every round synchronized before removal, kept the service responsive at
UNAVAIL-or-SUSPENDED, read both partition catalogues after reinsertion,
required explicit clear, fresh-imported ONLINE without automatic adoption and
passed writable idle recovery. Both runs then passed the two final read-only
import/DOS-read cycles, completion line zero and independent protected-raw
member hash checks. Their OpenZFS `source.patch` files are empty and neither
serial log contains a page fault.

Two preceding current-package runs are retained but are diagnostic-probe
failures, not driver verdicts. `native-import-run.OUE8KIr8` faulted during
mass-storage task startup and `native-import-run.uk4ss4cE` faulted at the
first removal because temporary `bug()` calls dereferenced the module-local
uninitialized `SysBase`; removing only those probes restored the exact clean
module hashes above. This closes repeated idle total-loss/readmission for the
specified current ADT xHCI package at SMP2 and SMP8. It does not qualify OHCI
(tracked separately as the development test archive), in-flight writes, the full current-ADT ISO's
unrelated early-boot path, physical disconnects or power-loss durability.

### Release provenance end-to-end refresh (2026-10-03)

Fresh management engine `release-provenance-20261003` and shared engine
`TABBAleE` both record `status_trace=0`; shared input hashes passed. Staging
produced `native-bundle.114ncLUw` with that same metadata, full dispatcher and
test modes disabled. Private QEMU `pyRMrHVz` passed with runner exit 0,
including writable DOS/export/reimport, standalone writable handler persistence,
startup authority checks and original backing hashes. This verifies the new
metadata through the real build/library/staging/runtime path, not only the
staging policy fixture. Logs on the external archive:
`release-provenance-{build,library,input-check,stage,bundle-qemu}-20261003.log`.
This IDE-backed bundle regression does not qualify USB repeated removal.
USB evidence was handed over for OS correlation as relay `the platform issue tracker` without
editing the shared OS tree or requesting use of the DUT.

### Explicit SMP topology regression (2026-10-03)

`AROS_TEST_SMP` accepts 1, 2 (default), 4, 8 or 16 virtual CPUs; the runner logs
the value and passes it to QEMU. Invalid zero was refused with exit 2 before
guest creation using the complete fixture environment
(`smp-invalid-full-20261003.log`). The earlier incomplete-environment check
did not reach topology validation and is not counted as evidence.

Release bundle `114ncLUw` passed private QEMU `qwu200t2` with eight virtual CPUs,
runner exit 0, writable DOS/export/reimport and standalone writable handler
persistence, and unchanged backing hashes. The serial trace contains APIC
calibration through CPU 007. Log: `release-smp8-bundle-qemu-20261003.log`.
This broadens tested topology; it is not exhaustive race or performance proof.

USB xHCI control `IMDilhQI` used one virtual CPU and still faulted in a
mass-storage task at IP `0x1ede974`, CR2 `0x24`, matching one prior two-CPU
failure signature. Runner exited 1 (`xhci-up-recovery-qemu-20261003.log`).
Simultaneous execution on multiple CPUs is therefore not necessary for that
observed failure; task/interrupt interleaving and lifetime bugs remain possible.

### Active-range UIO alignment (2026-10-03)

Alignment validation now limits inspection to `uio_resid`, rather than rejecting
an aligned requested range because of unused, unaligned iovec tails. Native
regression `G783rtdn` reproduced the old failure with UIO result 31; corrected
binary `U598i5oN` returned UIO result 0 and direct-buffer descriptor result 0.
The latter full legacy mirror runner nevertheless failed with `spa_create=16`
(EBUSY); it is not a complete mirror pass. Both logs remain in the external
archive as `uio-active-range-{before,fixed}-qemu-20261003.log`.

The standalone pool target also required the actual cache-policy dependency
when linking property validation without the management service. Early failed
build attempts are retained in `uio-active-range-before-build*-20261003.log`.
Mirror-runner artifacts can now use `AROS_TEST_ARTIFACT_ROOT`, requiring a
separate mounted filesystem and applying the existing free-space preflight.

Production integration: refreshed engine `uio-release-20261003`, shared library
`yy8IpGHV` and staged bundle `KK3aeu64` passed input hashes and packaging.
Private QEMU `oCfCGtmR` exited 0 with writable DOS/export/reimport, standalone
writable handler persistence and unchanged original member hashes. Logs:
`uio-release-{build,library,input-check,stage,bundle-qemu}-20261003.log`.
This confirms the alignment change in the current release-service path without
claiming that the separate legacy fixture EBUSY or USB removal faults are fixed.

### Legacy mirror fixture admission audit (2026-10-03)

Using retained writer bindings for all blank-checked members did not fix the
whole-disk fixture: `gjvOa7mY` failed its first writable open with EBUSY before
binding. Diagnostic `DKH7bOn0` showed DOS claim candidates HD0/unit0 and HD1/unit1
(CD0/unit2 is the boot media). No safety check was bypassed. Failure-only
diagnostics and immediate runner recognition retain that observation.

The existing GPT-partition variant `8E2gLV7T` passed creation and its first
filesystem phase, including UIO regression, direct writes/reads, buffered
partial-block I/O and legacy on-disk versions 1..4. Its later cold import
returned 86 using the inherited older import executable; overall runner 1.
That mixed-binary run is not a cold-persistence pass. The import/reopen
companions have now been rebuilt with the same source/SDK for a fresh run.
Logs: external `bound-mirror-{qemu,diagnostic-qemu,gpt-qemu}-20261003.log` and
`bound-mirror-{build,diagnostic-build,import-build,reopen-build}-20261003.log`.

Matched-binary GPT run `K4184jM8` completed the full runner with exit 0:
creation and DOS access, cold readonly import, both survivor/degraded cases,
zero-write checks and cold writable reopen on COW disks. Original member
hashes passed. Current import/reopen companions supersede the inherited older
executables used in `8E2gLV7T`; its error 86 did not recur. Full log:
`bound-mirror-gpt-current-qemu-20261003.log`. This qualifies the explicit GPT
fixture, not unrestricted whole-disk opens or physical hardware durability.

### Optional exact pool selection (2026-10-03)

Management operation 3 and the Manager catalogue now accept an optional exact
pool name. Blank lists all imported pools; a missing name returns an empty
successful page. No prefix or glob matching is implied. Filtering happens
before cursor accounting under the SPA namespace lock; capacity sampling and
explicit statistics errors retain their existing semantics.

Trace-disabled full-dispatcher build `pool-filter-20261003`, shared library
`shared-engine.Xir7S6YE`, and rebuilt Manager `pool-filter-manager-20261003`
were staged as `native-bundle.bJ2pZU84`. Input hashes and bundle gates passed.
Private two-CPU native QEMU run `AyTYtC45` exited 0, exercising blank selection,
exact match, missing prefix, literal wildcard, and cursor past the sole match
through the public management IPC. It also passed writable DOS access,
export/reimport and standalone-handler persistence; source disk hashes passed.
This is a one-pool filter/cursor regression, not a multi-page namespace stress
test. The field-schema executable and Manager compiled; the separate schema
executable was not run, and the new field has not yet been visually verified.

All artifacts/logs are under the external OpenZFS archive, including
`pool-filter-{build,manager,library,input-check,stage,bundle-qemu,fields-build}-20261003.log`.
No DUT, user hosted session, OS source or physical block device was touched.

Follow-up private GUI run `vOJS7bo8` also exited 0 after closing the actual
rebuilt Manager. Simple mode displayed the optional pool-name field; submitting
`absent_pool` through Show results returned OK with zero rows and zero statistics
failures. Screenshot `pool-filter-response.png` and original framebuffer PPM,
plus `ui-actions.jsonl`, are retained in that run directory. Matching-pool
semantics were verified by IPC above, not by this post-export GUI query.
Runner log: external `pool-filter-gui-qemu-20261003.log`; original backing hashes
passed after the GUI closed. This supersedes the visual-verification pending
note above, not the separate unexecuted field-schema-test limitation.

### Multi-pool namespace stress: exposed thread ceiling (2026-10-03)

Opt-in `AROS_TEST_POOL_PAGINATION=1` creates 25 disposable 128-MiB RAM-backed
pools alongside the imported mirror, checks two pages for 26 unique names and
valid capacity, tests the last named pool with filter-relative cursors, then
destroys the added pools. It requires `AROS_TEST_MEMORY_MB=8192`; the runner
otherwise retains its 2048-MiB default (4096 is also accepted). This test is
not yet passing and must not be counted as pagination qualification.

First run `8VipNBkq` refused the first create with ENOTSUP: the fixture omitted
cachefile selection and this private boot has no trusted persistent ENVARC
cache policy. The rerun explicitly selects `cachefile=none` for disposable
pools, without changing production policy.

Run `pXt4560i` created 23 additional pools, then aborted in the service:
`zk_thread_create -> taskq_create -> spa_activate -> spa_create`. Last emitted
pthread slot was 2018. The exact private pthread snapshot defines
`PTHREAD_THREADS_MAX=2019`; its creation path returns EAGAIN when no slot is
available, while upstream libzpool's wrapper uses VERIFY0 on pthread_create.
The eager per-SPA task queues reach this finite table. This is strong evidence
of thread-slot exhaustion, not a measured heap-OOM result. No limit increase,
pool-count reduction or production workaround has been applied. Fix/verification
remains open at this point in the test history.

Both runners exited 1; their private guests are terminal. External logs:
`pool-pagination-qemu-20261003.log` and `pool-pagination-2-qemu-20261003.log`,
with source snapshots, build logs and serial backtraces in the named run dirs.

### Demand-provisioned taskq candidate, not release-qualified (2026-10-03)

The AROS libzpool candidate starts TASKQ_DYNAMIC queues with one worker and
grows to the unchanged requested maximum. Optional growth allocation failure
retains queued work; all queue workers are joinable and destruction joins them
before reclaiming queue storage. Idle retirement and arbitrary resource-pressure
liveness are not claimed. Non-AROS behavior and taskq_t layout are unchanged.

Candidate `shared-engine.x3dm5TNN`, bundle `native-bundle.MDSmadpX` passed the
full 26-pool run `NeLtzlQD`: two pages/26 unique names, filter-relative cursors,
25 successful destroys, subsequent DOS persistence and original backing hashes.
Runner exit 0; external `dynamic-taskq-pagination-qemu-20261003.log`.

However, standalone GPT mirror run `xdehGdyO` exited 1 after page faults at
IP 0x1ae298c / CR2 0x312 in two tasks during later teardown. Its earlier native
growth test passed 16 rounds with eight simultaneous blocked workers and 16
jobs per round, including preallocated jobs, membership and joined destruction.
This is NOT a full mirror pass or release qualification. Direct worker-created
growth threads have a parent-lifetime risk; inspect a dedicated long-lived
spawner before accepting the candidate. Failed logs and build inputs are kept
under `dynamic-taskq-*-20261003.log` and the named run directories.

Follow-up growth requests now run on a dedicated fixed single-thread spawner
created before the system queues and destroyed after them. Outstanding spawn
requests count as active work, preventing queue destruction from freeing their
target. Optional spawn failure removes the reservation, not queued jobs. This
avoids creating growth workers with transient queue workers as their parents.

Matched standalone run `E2IZ1xsK` exited 0: 16 growth/concurrency/join rounds,
full creation/DOS matrix, cold readonly import, both degraded survivor cases,
zero-write checks and cold writable reopen on COW disks. Original hashes passed.
Log `taskq-spawner-mirror-qemu-20261003.log`; companion build logs are
`taskq-spawner-{tests,import,reopen}-build-20261003.log`. The earlier teardown
fault did not recur in this run, but its exact OS faulting function is not yet
resolved. Shared-library multi-pool replay remains pending at this checkpoint.

Shared-library replay subsequently passed: `shared-engine.B4M39eDX`, packaged
as `native-bundle.c1LyHDL5`, ran `pf1i41ya` with exit 0. All 26 pools were
enumerated across two pages, exact filtering and end cursor passed, the 25
temporary pools were destroyed, and subsequent DOS/export/reimport checks and
original backing hashes passed. Log: `taskq-spawner-pagination-qemu-20261003.log`.
This closes that replay gate for c718c9a, not unrestricted resource-pressure
qualification or the separate USB removal failures.

### TQ_NOQUEUE admission contract (2026-10-03)

The native probe now requires busy no-queue requests to be refused and idle
requests to execute. Before the fix, run `x2INaL0Y` failed with taskq test 5:
libzpool accepted work despite all eight workers being blocked. This flag is
used by upstream traversal prefetch, not merely by the test.

AROS dispatch now accounts for pending jobs against actual idle workers after
task allocation reacquires the lock. A refused request may prepare future
capacity, but cannot rely on an optional asynchronous spawn to accept its job.
Destruction also reads the changing worker count under the queue lock.

Matched binaries in `taskq-noqueue-20261003` passed full standalone GPT mirror
run `8wApYVHc` (exit 0): 16 concurrency/admission/join rounds, creation and DOS
access, cold readonly import, both degraded survivors, zero-write verification,
and cold writable reopen on disposable COW disks. Original disk hashes passed.
External logs: `taskq-noqueue-before-qemu-20261003.log` (negative control),
`taskq-noqueue-mirror-qemu-20261003.log`, and
`taskq-noqueue-matched-{pool,import,reopen}-build-20261003.log`.
Shared-library package replay remains pending; this is not USB-removal or
unrestricted memory/thread-pressure qualification.

Optional-worker failure injection is confined to the standalone pool diagnostic
using `--wrap=zk_thread_create_optional`; library packaging rejects wrapped
engine objects. In `qemu-run.laVpim6j`, the native probe reported
`optional-failure/recovery tests=0 refused=7 jobs=32`: sixteen queued jobs
completed despite forced optional-spawn failures, then the same queue recovered
eight-worker concurrency and completed sixteen more jobs before joined destroy.
The complete mirror run is still pending at this record. This tests the failed
allocation return path, not actual system-wide heap exhaustion or mandatory
initial-worker allocation failure. Log: `taskq-failure-qemu-20261003.log`.

Both pending runs subsequently completed with exit 0. The failure-injected
standalone `laVpim6j` completed the full cold mirror/survivor/COW reopen matrix
with original hashes unchanged. Release `shared-engine.sVgg6E5R`, bundle
`native-bundle.t5dLGvUJ`, passed `native-import-run.XttHPuaL`: 26 unique pools
across two pages, filter/end-cursor checks, 25 destroys, subsequent native DOS
write/export/reimport and readonly checks, and original backing hashes. Logs:
`taskq-noqueue-pagination-qemu-20261003.log` and the failure log above.
This release replay used two guest CPUs; larger CPU-count stress is separate.

The same immutable release bundle subsequently passed the eight-CPU replay
`native-import-run.0BsiWwa6` (exit 0): the 26-pool pagination/cleanup checks,
DOS write/export/reimport, readonly checks, and unchanged source hashes.
Log: `taskq-noqueue-8cpu-qemu-20261003.log`. This is a bounded SMP regression,
not unrestricted memory-pressure or device-removal qualification.

### Replacement-tree import admission (2026-10-03)

The native import whitelist omitted upstream `VDEV_TYPE_REPLACING`, rejecting
a pool configuration containing an in-progress replacement before the engine
could assess it. Native negative-control run `470aBILk` failed the new test at
case 1 (valid admitted replacement tree). Adding this upstream interior type
does not bypass recursive exact-path admission. Corrected run `DUWKSWG7`
reports admission PASS: valid tree accepted, unadmitted children/spares/cache
members each rejected with EACCES. The probe does not open devices.

Both existing taskq probes also passed in the corrected run. Full standalone
mirror and shared release rebuild are pending at this record. A real pool
exported/cold-imported during an unfinished replacement remains a separate
end-to-end test; config validation alone does not prove resilver recovery.
External logs: `replacing-admission-before-qemu-20261003.log`,
`replacing-admission-qemu-20261003.log` and corresponding build logs.

Corrected standalone `DUWKSWG7` subsequently completed with exit 0: full
mirror creation/DOS, cold readonly, both survivors, zero-write checks and
writable COW reopen; original hashes passed. Import/reopen companions are
the unchanged `taskq-noqueue-20261003` binaries (they do not link this config
validator). Release shared-library build `shared-engine.zawvfQHJ` completed;
its package replay is not yet qualified by this standalone result.

That exact package (`native-bundle.q3Xzc2Xn`) subsequently passed native
replay `Qkd2zTZL` with exit 0, including CLI/handler selected regression,
writable DOS/export/reimport, readonly checks and original backing hashes.
Log: `replacing-admission-bundle-qemu-20261003.log`. This closes the ordinary
package replay, not the unfinished-replacement end-to-end recovery gate.

### Actual unfinished replacement export/reimport (2026-10-03)

Standalone native `qemu-run.fjNT6tfl` now reports unfinished replacement
recovery PASS. The helper creates two exclusive 128 MiB files in the disposable
guest RAM filesystem, creates a real single-leaf pool, writes/syncs 1 MiB,
holds upstream scan progress, and attaches a real replacement. It exports,
rediscovers using `zpool_find_config` from the actual labels (not the saved
export config), and imports. It requires the imported tree still to be
`replacing`, releases scan progress, waits for the top-level leaf to become
the replacement alone, and compares all payload bytes before destroying the
pool and deleting its files. No simulated completion or synthetic labels.

Log `replacement-recovery-qemu-20261003.log`; build inputs in
`replacement-recovery-20261003`. The rest of the standalone mirror regression
is pending at this record. This is same-engine export/reimport with real
on-disk state, not power-loss or cold-boot unfinished replacement coverage.

The full `fjNT6tfl` runner subsequently exited 0 with the ordinary cold mirror,
both degraded survivors, zero-write checks, writable COW reopen and original
hash checks passing after the new recovery probe.

Strengthened run `O0YeL5Tg` additionally reports
`unfinished replacement recovery PASS engine-restart=1 errno=0`: after export,
the helper calls kernel_fini/kernel_init before label rediscovery, dropping
the previous engine caches and task queues. RAM-file storage persists across
this restart. The complete mirror runner is still pending at this record;
this remains distinct from guest reboot or power-loss coverage.
Log: `replacement-restart-qemu-20261003.log`.

### Cold-boot unfinished replacement (2026-10-03)

Run `uV3bTXXw` exits 0 with `AROS_TEST_COLD_REPLACEMENT=1` and GPT partition
mode. A first guest blank-checks and binds the two fixed disposable partitions,
creates a single-member pool, syncs 1 MiB, attaches a replacement with scan
progress held, verifies `replacing`, exports and shuts down its engine.
The runner terminates that guest, preserves both staged raw disks, then starts
a fresh guest on COW overlays. Recovery discovers the real labels, requires
the imported topology still to be `replacing`, resumes resilvering, requires
the replacement-only leaf and compares all 1 MiB before export/engine teardown.
Object identity is rediscovered from the actual dataset, not a RAM variable
carried from the first boot. Stage/recover markers both pass. GPT outside-partition
checks pass and both original staged raw hashes remain unchanged.

Logs: `replacement-cold-{build,qemu}-20261003.log`; binary inputs in
`replacement-cold-20261003`. This proves orderly-export cold-boot recovery,
not abrupt guest termination or physical power-loss durability. Recovery is
tested through the native engine/device path, not the management UI.

Abrupt variant `6kE1PZQ8` also exits 0 with
`AROS_TEST_REPLACEMENT_CRASH=1`. The first guest syncs the replacement TXG,
prints crash-ready and stays inside Delay without exporting, closing the SPA
or calling kernel_fini. The runner terminates only that owned guest via its
monitor. Fresh-guest COW recovery passes pending-topology admission, replacement
completion and exact payload comparison; staged raw hashes and outside-partition
checks pass. Log: `replacement-crash-qemu-20261003.log`, build inputs in
`replacement-crash-20261003`. QEMU closes its backing files normally: this is an
abrupt guest-software interruption after TXG sync, not torn-write, volatile
hardware-cache loss, or physical power-loss qualification.

### Native znode reclamation (2026-10-04)

Candidate `cache-reclaim-20261004` passes the five-boot mirror runner
`qemu-run.DNDkxbfD` (exit 0). Owner-side successful mount sync trims clean,
unreferenced cached znodes toward 256 entries. Active list walkers prevent
reclamation of borrowed list pointers. Dirty, referenced, suspended and
not-yet-deleted unlinked nodes are retained; release itself performs no I/O.
The native cache test checks live-reference and dirty-state retention, walker
exclusion, successful reclamation and reload of the real object's generation
and size. It is exercised by the passing native SA atime test in the creation
guest. Subsequent cold mirror, both survivor and writable-reopen controls use
unchanged baseline binaries, not the new cache implementation. Original disk
hashes pass. Build and runner logs are `cache-reclaim-{build,qemu}-20261004.log`
under the external OpenZFS archive. Large-directory pressure and automatic
read-only directory-walk reclamation are not established by this test.

Follow-up `qemu-run.oIhv31gC` exits 0 with owner-side stat trimming and the
read-only `cached_nodes` diagnostic counter. The creation guest creates 320
real files with distinct 64-bit payloads, closes the writer, reopens read-only,
then looks up and reads all 320 files three times. Every size and payload must
match and the cache must stay at or below 258 nodes (the 256 target plus two
lookup-held references). The pressure marker reports `tests=0`. This exercises
same-guest cache reload, not cold-boot pressure. The full five-boot regression
and backing hashes also pass; cold controls remain unchanged baseline binaries.
Logs: `cache-pressure-{build,qemu}-20261004.log` in the external archive.
Source audit confirms management.c opens mounts and pumps their DOS packets
on the engine owner, so the owner-only trim condition applies there too;
production-service pressure runtime coverage remains to be performed.

All-current-binary follow-up `qemu-run.CPJjt0Al` exits 0. Cold import and
writable-reopen executables were rebuilt with the same cache changes instead
of using baseline controls. Fresh linking exposed the missing native import
adapter dependency; commit `86f1833` supplies its complete discovery, identity,
file-lock, cache and API dependencies without stubs. Creation pressure, cold
mirror, both survivors, writable COW reopen and unchanged original hashes all
pass. This closes the baseline-binary limitation for ordinary cold regression,
not the separate production-service pressure workload. Log:
`cache-all-cold-qemu-20261004.log`. Production dispatcher build
`cache-production-20261004` and shared engine `shared-engine.zFjS8hrZ` also
build successfully with test modes and status tracing disabled; package
runtime acceptance is still pending.

Production package `native-bundle.bIv7D6Vr` subsequently passes the isolated
16-vCPU/8192-MiB run `native-import-run.yZmIwFUs` (exit 0). The normal shared
engine, upstream CLI, native handler, zvol device, library lifetime and native
path-relocation acceptance complete with original backing hashes unchanged.
Log: `cache-sixteencpu-qemu-20261004.log`. This uses the private runtime
overlays and the existing Manager validation harness; it does not qualify a
stock runtime or physical hardware. Dedicated 320-file DOS pressure remains
a separate, pending test rather than being inferred from this run.

Dedicated production DOS pressure `native-import-run.uCBZSX5t` now passes
with 16 vCPUs and 8192 MiB (runner exit 0). Enable
`AROS_TEST_DOS_CACHE_PRESSURE=1` with writable mirror, native handler, CLI and
headless acceptance inputs. The client creates 320 distinct 64-bit payload
files through DOS Open/Write/Flush/Close, enumerates exactly 320 unique entries
and checks sizes and contents over three passes. After detach/export and
read-only reimport it repeats all three enumeration/read passes. The runner
requires the dedicated pressure PASS marker and verifies original backing
hashes. Log: `cache-dos-pressure-qemu-20261004.log`.
This proves application-facing traversal and persistence across reimport;
cache-count boundedness is separately measured by the standalone test, not
by a hidden DOS counter. It is not a pressure-file cold reboot or power-loss
test. The failed second monitor connection occurred after guest completion.

Normal-UI package `native-bundle.KLQ4Zmus` (ordinary Manager, not the validation
harness) starts in `native-import-run.0bJLDSlj`, 16 vCPUs/8192 MiB. The backend
cycles pass and the actual Manager reports the available upstream engine.
List-pools selection works, but subsequent USB-tablet input stops. CPU0 is at
`0x1ac5c3c`, lock `0x100013100` write-held; the explicit stack contains
`0x1aeb64b` and `0x1adc376`, the same allocator/FreeMem frames as the earlier
high-memory USB stall. The specific higher caller is not yet symbolized.
Screenshots, input log and monitor captures were retained for the shared-OS
investigation. The runner has not yet reached its
deadline, so this is not a completed GUI pass or a terminal timeout record.
A separate 2048-MiB comparison is running without changing production code.

Caller refinement: this GUI case is **UHCI**, not the OHCI controller used
by the earlier USB-storage experiment. Guest return `0x1ec3743` minus the
verified pciusb text relocation `0x1ebb300` is `0x8443`, matching
`uhciHandleFinishedTDs+0x163` in the exact module. Captured guest code matches
that site's call to `usbReleaseBuffer` (relocated `0x1ebe260`). The nightly
source's `uhci_hcd.c:268` releases the completed data buffer there. Both
controller paths therefore reach the shared buffer-release/allocator seam.
`gui-stack-callers.log`, `gui-caller.bin` and the original module reference
retain the evidence. This is diagnosis, not an OS fix or a GUI pass.

Final comparison: `0bJLDSlj` reaches its original 1800-second deadline and
exits 1. The separate `native-import-run.VcZvltyD` with 16 vCPUs/2048 MiB
completes with exit 0 and unchanged backing hashes. Its normal packaged
Manager connects to the real service and a clicked List pools request returns
zero imported pools, as expected after the backend's export. The window is
closed normally. `lowram-manager.png`, `lowram-pools-response.png` and
`ui-actions.jsonl` retain visual/input provenance. This is a small real UI
interaction, not GUI pool creation coverage or a repair of the high-memory
USB fault. Both final evidence sets are registered to the development test archive.

### Active-write total-loss recovery on current ADT xHCI (2026-10-06)

`AROS_TEST_USB_INFLIGHT_WRITE=1` is a fail-closed extension of the existing
two-member writable USB recovery fixture.  It attaches `qemu_mirror/files`
writable, commits and flushes a sentinel, then continuously writes and flushes
64-KiB chunks while the host removes both members.  The test requires at least
one full write before a real write or flush error.  It retains the mount until
same-identity readmission and explicit clear restore ONLINE, then detaches,
exports, freshly imports read-only and rereads the committed sentinel exactly.
The option is accepted only with qemu-xhci, total-loss writable recovery, one
round, no swapped media, no created-pool mode and no separate suspend guard.

The first overlap run `native-import-run.TwhsI0r8` completed 48 flushed chunks
and observed native I/O failures on both `usbscsi.device` units, but then
reproduced a service hang.  Authenticated unmount removed the DOS publication
and synchronously called `oz_zpl_close`; closing a writable mount can wait for
a final TXG even though a suspended pool cannot advance before readmission.
Both vCPUs sampled in the kernel idle loop and the harness reached its
600-second deadline.  This is retained as an incomplete discriminator, not an
ADT or xHCI lockup and not a recovery pass.

`unmount_dataset` now opens the owning spa and returns EAGAIN before DOS
namespace withdrawal when it is already suspended.  The fixture requires that
reply and proves the namespace was retained before permitting reinsertion.
Candidate `shared-engine.ZwbBHUnF` (`openzfs.library` SHA256
`2dc647e7416538c2593b80299aac609d87a1852bb6baa875380b1022f17b0e39`)
uses the same current ADT Poseidon package and ISO already qualified above.
Run `native-import-run.2qiNX8yA` completed all guest recovery checks but exposed
a host-only CRLF exact-line verifier error; it is retained as a guest-pass,
host-false-negative artifact.  The corrected exit-zero replay
`native-import-run.FbcCXDL8` completed 46 flushed chunks before the error,
returned EAGAIN responsively, re-enumerated both partition catalogues, refused
an invalid clear target with ENODEV while still suspended, cleared ONLINE,
detached/exported/imported and reread the sentinel.  Completion is line 0 and
both protected raw backing hashes pass.  Non-injected regression
`native-import-run.kmT7kCSo` also exits zero through the ordinary writable idle
loss/readmission path with the same engine.

Commit `447a2961` records the guard, opt-in fixture and this evidence.  Fresh
source build `source-engine.Qbv6IEk1` compiled all 248 enumerated sources and
linked the engine/Manager with an empty source patch at that commit.  Its
production wrapper `shared-engine.ipalmFXH` has `openzfs.library` SHA256
`78fa08e0848a29d06929648d446bc42c451048aa0daae866f146d1f7fb9ecd6f`.
Final exit-zero replay `native-import-run.n7SiSQ9W` used that fresh library,
completed 36 flushed chunks before the induced failure, passed the responsive
EAGAIN guard and exact sentinel recovery, wrote completion line 0, preserved
both raw backing hashes and captured a zero-byte source patch.

The clean distribution was staged as `native-bundle.gxn9jkDr`; archive
`native-bundle.gxn9jkDr.tar.xz` has SHA256
`e437c18026e19429e82f54d084b35067043b21cf5659f39c84e4ce83509bd544`.
Its initial exact-package attempt `native-import-run.wpasDJF1` stopped before
QEMU because loading a bundle also enabled the normal writable whole-CLI
matrix, which rejects the dedicated `writable=0` in-flight lifecycle.  That
artifact is retained as a host harness composition failure and proves no
guest behavior.  Bundle loading now keeps the exact packaged service, shared
engine library and handler for this fixture while reserving `zfs`/`zpool`
execution for a separate normal writable package regression.  It also records
the package manifest and a second complete manifest check in every bundle run.

Corrected exact-package run `native-import-run.gy6UFLnP` passed on the current
ADT xHCI stack: the complete bundle manifest verified, an active write failed
after both members were removed, unmount returned EAGAIN without withdrawing
the namespace, readmission and clear restored ONLINE, the committed sentinel
was reread after detach/export/fresh import, completion was line 0 and both
protected raw backing hashes remained unchanged.  Its nonzero source patch is
only the host harness correction described above; no packaged guest binary was
rebuilt or substituted.

After committing that harness correction as `bd6df440`, clean exact-package
replay `native-import-run.8URiGJwX` repeated the same active-write interruption,
responsive EAGAIN guard, ONLINE readmission, fresh-import sentinel readback and
unchanged protected backing hashes with a zero-byte source patch.  Separate
normal writable package regression `native-import-run.sASwPHu9` then exercised
the bundled `zfs`, `zpool`, service, shared engine library and handler.  It
passed the 240-write concurrent history overwrite fixture, pool creation, DOS
write, flush, export/import/readback, service CLI integration and standalone
writable handler export/reimport readback.  Completion was line 0, the source
patch was zero bytes and both raw fixture hashes remained unchanged.  The
runtime component SHA256 values were service
`8a2defa1361b26c62d8df6f98c71da82d52c2872944fc6717414d08a36f8d2f1`,
library `78fa08e0848a29d06929648d446bc42c451048aa0daae866f146d1f7fb9ecd6f`,
`zfs` `58dbe28dc1e4a7d10b405cb9c2eabcb6e8c4f1b53df7ab472f7bffe4287480d2`,
`zpool` `edadce12af3401e9d884a612949d89cceb1cd9b1c7af790fee88a4341b73669a`
and handler
`a7cf00f9975c0d9f61fe312d7766ef5a5c3c527fd7a4825f0c95f29145e246ee`.

These results qualify the forced in-flight and ordinary idle recovery paths on
the exact current-ADT qemu-xhci runtime at two guest CPUs.  They do not qualify
physical USB, the DUT, OHCI/UHCI or a removal racing between the suspension
preflight and final writable close.  Repeated idle removal on this same ADT
package is separately qualified at two and eight CPUs.
