# Native zvol device integration

### Volume-create delegation and read-only acceptance

`native-import-run.f05nukuu` exits zero with actual upstream `zfs allow -u 77
create,mount` on an isolated parent filesystem, a successful Manager-API zvol
creation under that user, and independent CLI type verification. After upstream
`zfs unallow`, a fresh create under the same user returns EPERM and CLI listing
confirms no destination exists. Root cleanup and the remaining native/CLI and
zvol I/O suite pass. No test-only permission bypass is used.

Fresh cold reader `native-import-run.xLQzEvmr` also exits zero: creating a new
volume against the read-only service returns EROFS, independent CLI listing
confirms absence, and the retained volume's exact bytes and discarded range
still pass. Independent upstream zdb checks report No leaks in
`control-integrity.zNFIkXc6` (writer) and `control-integrity.l1o9SF2c` (reader),
with unchanged verification inputs and source/retained-writer hashes.
Mandatory runner markers were added after these runners were snapshotted;
both exact new markers were explicitly checked in their retained serial logs.
This covers grant/create/revoke/fresh-request refusal, not an in-flight
revocation race or every delegation/property combination.

### Interactive Manager volume creation acceptance

`native-import-run.8VJOQozQ` now passes actual window-driven volume creation.
The separate client first submits a valid create as UID 77 without delegation,
receives EPERM, restores system identity, and tests malformed inputs. It then
waits for the real Manager to create `qemu_mirror/transport-volume`, with no
test-client successful creation fallback. Screenshots `06-confirm.png` and
`07-result.png` retain the full options and authenticated errno=0 result.
After the Manager closes normally, the independent client verifies EEXIST,
opens the volume, checks its exact 2 MiB/8 KiB geometry, and exercises real
write/flush/read/discard and session cleanup. The remaining selected native/CLI
suite passes, the final Manager closes normally, and the runner exits zero.
Independent `control-integrity.QwKLVEUZ` reports No leaks and matching input
hashes. No DUT, user hosted session or physical disks were used.

Reproduce with `AROS_TEST_GUI=1 AROS_TEST_ZVOL_TRANSPORT=1
AROS_TEST_ZVOL_CREATE_GUI=1` and the existing private native-import runner,
using service/Manager from `manager-volume-create.Qyvgr2vo`. The runner requires
both authorization and GUI geometry markers. Earlier captures 03/04 retained
missed options-field clicks; capture 06 confirms the actual submitted options.
The short UI hint still says Create filesystem; the target label and operation
help include volumes. Positive delegated creation, revocation, and creation
against a read-only service remain separate tests; this proves ordinary-user
denial, not the entire delegation matrix. The immutable bundle is unchanged.

## Manager API volume creation (2026-09-27)

Dataset create operation 37 now accepts comma-separated
`type=volume,volsize=2G,volblocksize=8K,refreservation=0` options. Size and
block size use the upstream numeric parser; block size is optional. Reservation
is explicit: omission does not add the CLI's automatic thick-volume reservation.
The adapter calls upstream ZFS_IOC_CREATE with captured caller credentials,
retaining upstream permission, geometry, inheritance and transaction checks.
Mixed filesystem/volume fields and duplicate types/properties are rejected.
Creation does not publish an openzfs.device mapping or mount a filesystem.
Without type=volume, existing filesystem creation behavior is retained.

Full service/Manager build `manager-volume-create.Qyvgr2vo` succeeded.
Native QEMU `native-import-run.LAVVjvGt` exited zero: actual authenticated
Manager-API volume creation, missing size/duplicate type/mixed properties/zero
size refusals, duplicate creation EEXIST, exact 2 MiB/8 KiB geometry and real
write/flush/read/discard through that newly created volume. The selected
UNMAP, resize, readonly and native/CLI regression also passed. Independent
`control-integrity.0D3MNw6S` reports No leaks and unchanged extracted-member
and original backing hashes. This is API acceptance, not interactive Manager
creation, delegated-create negative testing or cold retention of this new
creation path. Those remain separate verification work. No physical storage
or user runtime was accessed.

Follow-up retained writer `native-import-run.N4xHJxry` and fresh read-only
reader `native-import-run.8R97DSMy` both exit zero. The API-created volume
survives export and a new guest boot with exact 24 KiB contents, including the
freed middle block and unchanged neighbors; cold discard returns EROFS.
Independent `control-integrity.I5dJPMg0` reports No leaks and all extracted,
source, retained-writer and reader hashes match. This closes cold data
persistence for the new creation path, not interactive GUI or delegated-create
coverage. The published development archive has not yet been refreshed.

The upstream zvol backend exists in `zvol-native.c`. It owns real upstream
volume registration, DMU block I/O, ZIL commit, geometry and open lifetime.
An initial explicit resident Exec `.device` frontend now has native runtime
evidence below. General device installation and DOS-device publication remain
unfinished. Do not present the transport alone as an installed block device.

## Live inherited readonly policy (2026-09-27)

### Native upstream range deallocation

OZ_ZVOL_DISCARD extends the session-owned native transport with offset/count
and no data payload. The real backend follows upstream zvol_discard:
validate sector alignment and subtraction-safe volume bounds; enforce current
effective readonly and per-handle access; retain partial volume-block edges;
hold the native suspend/range locks; journal with zvol_log_truncate, free with
dmu_free_long_range, and commit the intent log before reporting success.
No memset replacement, private on-disk format, physical TRIM or secure erase
is used. A successful completed count is the accepted request length; a
sub-block request can succeed without freeing any blocks. Errors do not promise
rollback of an upstream partially executed range free.

Service build `zvol-discard.tUMhMshS/service` passes
`native-import-run.48emZ5b8` (runner exit zero): write three patterned 8 KiB
blocks, discard a sector-aligned range with partial edges, verify the complete
middle block is zero and both edge blocks are byte-exact, verify a sub-block
no-op, reject end-of-volume and overflow ranges, reject effective readonly
on an existing writable descriptor and a separately readonly descriptor.
Selected native/CLI/resize/readonly regressions also pass. Independent
`control-integrity.lWMdrzwm` reports No leaks and matching verification inputs.

Retained writer `native-import-run.LZ6aqj4B` exits zero and preserves that
volume alongside the existing FAT/device fixtures. Fresh readonly guest
`native-import-run.L8t2lqsy` checks every byte across the three blocks,
including the persisted hole and exact neighbors, and rejects discard EROFS.
AROS_TEST_ZVOL_DISCARD_PERSIST=write requires transport testing and retained
CLI fixtures; read requires the writer evidence and a cold read-only import.
This initially added backend/transport support only; standard device routing
is covered below. Physical-device TRIM and crash recovery tests remain
unfinished; the previously frozen distribution archive does not contain it.

### Standard SCSI UNMAP in openzfs.device 1.10

HD_SCSICMD now handles bounded UNMAP(10), not arbitrary SCSI passthrough.
It snapshots and validates the complete parameter list before any mutation:
header/reserved fields, at most 255 descriptors, sector ranges and aggregate
maximum 0x7fffff logical blocks. Bad later descriptors cannot free earlier
ranges. ANCHOR and malformed direction/length fields are rejected. Readonly
returns DATA PROTECT/write-protected sense. Cancellation and execution errors
do not promise rollback of earlier completed descriptors.

The service adds a descriptor-scoped capabilities query. Supported VPD pages,
block limits (0xb0), logical provisioning (0xb2) and READ CAPACITY(16) LBPME
are advertised only when the connected backend reports discard support.
LBPRZ, WRITE SAME, secure erase and physical TRIM are not advertised. Partial
volume-block edges retain data, following upstream zvol behavior.

`native-import-run.tPCziJxW` passes real device UNMAP, exact middle-block zero,
preserved edge blocks, invalid-second-descriptor/no-write, malformed fields
and readonly sense, plus the existing selected native/CLI suite (exit zero).
Independent `control-integrity.kXDs8tZx` reports No leaks and unchanged inputs.
Old-service compatibility `YB8xARyo` exits zero and remains unadvertised with
UNMAP refused; independent `control-integrity.wi6Do1k4` reports No leaks.

Expanded retained writer `native-import-run.JR6KhGUE` exits zero, checking the
READ CAPACITY provisioning bit and the bytes preserved after readonly refusal.
Fresh cold `native-import-run.Vc3geVLo` exits zero: the device reads the exact
persisted hole and neighbors, refuses UNMAP readonly and reports capabilities.
Its mandatory `zvol-integrity.xxjvlick` checks GPT extents, upstream No leaks,
and unchanged writer/source/reader hashes. These are disposable QEMU tests,
not SSD TRIM, crash recovery or physical-array qualification.

Use AROS_TEST_ZVOL_UNMAP=1 for writable tests, 2 with an old service for
unsupported-capability checks, and 3 with a retained resize cold fixture for
fresh readonly verification. The supplied frozen archive still needs updating
and its exact packaged binaries require their own runtime acceptance.

The native backend now resolves upstream effective `readonly` at open, status
and write boundaries, including already-open descriptors. Snapshot and pool
read-only restrictions and per-handle read-only access remain independent.
Failed policy lookup refuses the operation rather than trusting cached flags.

Preserved reproduction `native-import-run.JxeMAH6x` showed the actual upstream
CLI reporting inherited readonly=on while TD_PROTSTATUS reported writable and
CMD_WRITE accepted 512 bytes. This was a real cached-policy defect, confined to
disposable QEMU images. The candidate service in `zvol-readonly.a2ZqLI2r/service`
passes the same reproduction in `native-import-run.diJDuqrY` (runner exit zero):
protected=1, write error TDERR_WriteProt, zero bytes transferred. Independent
`control-integrity.VXm6hxdr` reports No leaks and unchanged verification inputs.

Expanded `native-import-run.C5agzSI4` also exits zero. An existing direct service
descriptor rejects WRITE without a preceding INFO/status call; a new writable
open is rejected. Changing only the parent back to readonly=off permits a direct
write, flush and exact device readback. The runner requires both dedicated
readonly markers alongside the existing selected CLI/device regression.
Use AROS_TEST_ZVOL_READONLY_POLICY=1 with the writable native runner.
Independent expanded-run verification `control-integrity.tbNWXbcC` reports
No leaks with unchanged extracted members and original backing files.
This does not qualify concurrent property changes versus in-flight writes,
service death or physical media.

### Manager readonly property API

The native Manager's existing dataset.set/get/inherit API now accepts zvol
readonly. It retains upstream property delegation and writable-pool checks;
atime/relatime remain filesystem-only. Effective policy is enforced by the
native device boundaries above, not merely stored as an inert property.

Service `manager-zvol-readonly.kbwY2d3K/service` passes native API test
`native-import-run.8n4TO2OB` (exit zero): set on/off, exact get, inheritance
from the parent, direct write refusal/restoration, malformed-value refusal,
filesystem-only property refusal, and unauthorized-user refusal with unchanged
property. Independent `control-integrity.8PFjXUQw` reports No leaks and matching
input hashes. Updated Manager help builds as OpenZFSManagerNative-zvol-readonly-v1;
these API tests do not substitute for interactive GUI acceptance.

Subsequent interactive `native-import-run.b7uJtt66` uses the exact packaged
Manager/service/device from `native-bundle.tMQQLH2l`. Screenshots
04-readonly-request, 05-readonly-confirm, 06-readonly-result and 07-readonly-get
record confirmed set success and exact readonly=on. After normal GUI close,
the independent client verifies the property and direct write refusal, emitting
the required `RESULT native Manager zvol readonly GUI PASS` marker. Both
Manager invocations and the test process exit normally. The final summary is
split by security logging (`RESULT nativ` / `e pool import PASS`), so the runner
misses completion. Only this owned guest was then stopped through QMP; runner
exit is 1, not a full harness pass. This retained reporting limitation does not
replace the separate terminal packaged headless passes documented in distribution.
Enable this optional GUI test with AROS_TEST_ZVOL_READONLY_GUI=1,
AROS_TEST_ZVOL_READONLY_POLICY=1 and AROS_TEST_GUI=1. Actual visual interaction
is required; the checker does not set readonly=on in place of the Manager.

The runner now receives a separate versioned completion record through a
private QEMU ISA debug console at port 0xe9 (`completion.log`). Only this test
executable, compiled with OZ_QEMU_COMPLETION_PORT, emits it after its final
cleanup and assertions. Production binaries are unchanged. The normal serial
log and all mandatory feature checks remain; a completion record alone is not
acceptance. A missing record times out and a FAIL record fails immediately.

`native-import-run.1ABW7a4b` passes the normal packaged writable/resize/readonly
suite with runner exit zero and dedicated PASS. Independent
`control-integrity.9AS7v75J` reports No leaks and unchanged verification inputs.
Explicit test-only AROS_TEST_COMPLETION_FAIL=1 in `native-import-run.MIOfkH8a`
emits FAIL line=181 and the runner exits 1, as required; it does not run the
storage suite. `native-import-run.HDX3o7VT` repeats actual GUI confirmation,
successful readonly set and independent property/write-refusal checks, then
closes both Managers normally. This time the dedicated completion record and
all required feature/hash checks pass with runner exit zero. Captures
02-confirm.png and 03-result.png preserve the UI evidence. The earlier split
serial result remains recorded rather than retroactively called a harness pass.

Fresh cold read-only run `native-import-run.TKVWf2QY` also exits zero. Both
readonly set and inherit are refused with EROFS; exact property value remains
unchanged, raw device writes remain denied and retained FAT bytes pass. Required
independent `zvol-integrity.oPZtsxRQ` validates GPT extents, upstream No leaks,
and unchanged source, writer and reader image hashes. This is isolated QEMU
evidence, not stock-nightly or production-array qualification.

## Cold FAT-on-zvol protection (2026-09-27)

Writer `native-import-run.ST2gDP2o` retained a FAT filesystem in the zvol
after successful format, 128 KiB write/flush/reopen and native/CLI regression.
Fresh reader `fRxJx7oA` recovered the exact payload but FAILED the next
assertion: FAT accepted MODE_NEWFILE on the read-only device. This is not a
passing cold-filesystem run. Both inspected nightly and ADT FAT packet paths
lack TD_PROTSTATUS admission checks; device-level rejection alone is too late
to prevent accepted write-back-cache changes.

`aros/platform/fat-device-readonly.patch` is a private dependency correction.
It queries TD_PROTSTATUS before mutating packets, returns
ERROR_DISK_WRITE_PROTECTED before changing the cache, and reports protected
media in InfoData. Unsupported queries retain legacy behavior; other query
failures refuse mutation. No shared AROS source or build is changed.
Build using `AROS_FAT_READONLY_FIX=1 AROS_FAT_DIAGNOSTICS=0` with
`build-fat-zvol-overlay.sh`; supply both its filesystem package and explicit
`AROS_TEST_FAT_HANDLER` to the native runner.

Overlay `fat-zvol-diagnostic.jIGdvDqz` passes cold reader `S3EYHo0B`,
including the mandatory independent GPT and zdb No-leaks gate. Expanded
reader `lLnpmKV5` also passes: protected InfoData, refused create/update-open,
mkdir, rename, delete and SetProtection, exact 128 KiB reread, handler shutdown
and device eject. Both runners exit zero; original, retained-writer and
reader-overlay hashes remain unchanged. The stronger marker was independently
checked in lLnpmKV5/serial.log. These are clean-export cold tests, not sudden
power loss, dynamic protection transitions, every FAT packet/error branch,
or stock-nightly compatibility. FAT dependency packaging remains explicit.

Writable regression `VwQLninN` with the same overlay also exits zero:
format/write/flush/reopen/exact bytes, handler teardown, device notifications
and eject, plus the selected full native/CLI suite. The cold runner now
requires the expanded FAT protection marker (already checked independently
in lLnpmKV5), rather than only the single create refusal.
Committed-harness rerun `tBGTkZNK` passes that mandatory expanded marker and
all cold gates (exit zero); independent `zvol-integrity.t6yYs6SM` reports
No leaks. Writable VwQLninN's independent `control-integrity.iEaoeugj`
also reports No leaks and unchanged extracted/source hashes.

## Explicit resident Exec frontend

### Live raw-zvol resizing

The full-engine Manager property API now supports `dataset.set` with
`volsize=<size>` through upstream `zvol_set_volsize`. It checks syntax,
nonzero size, volume type, writable pool and caller volsize delegation;
upstream still owns alignment, dataset-readonly checks, truncation and native
capacity update. Inherit and snapshot targets are refused. The adapter does
not resize a filesystem or implicitly adjust reservation/refreservation.
Manager help explicitly warns that shrinking discards data and requires
unmounting the filesystem first. Partial non-dispatcher engines return
ENOTSUP rather than pretending to resize.

`dataset.get` for volsize/volblocksize now calls actual upstream
`zvol_get_stats`, with type and inconsistent-dataset checks, instead of
falling through to DSL defaults. Native API run `1prDpdQe` exits zero: four
invalid sizes, inherit, filesystem, readonly and unauthorized refusals,
unchanged capacity after refusals, successful 32 MiB resize and exact getter,
plus native device grow/shrink/regrow checks and the complete selected
CLI/native regression. Independent `control-integrity.wEjozJTZ` reports
No leaks and unchanged extracted/source hashes.

Actual GUI `native-import-run.z6wOHAov` with
`OpenZFSManagerNative-volsize-v1` performs Datasets/set `volsize=32M`, confirms
the exact target and reads back 33554432 through Datasets/get. Captures 05,
06 and 09 retain confirmation, success and exact readback. After normal UI
close, the independent parent checks the API value, native device capacity
and high-offset I/O, shrink refusal and regrown zeroes. Both Manager windows
close normally and the complete runner exits zero. Independent
`control-integrity.x3yvqota` reports No leaks with unchanged hashes. Captures
03/07 retain automation mistakes (wrong row and Ctrl-A moves to beginning
rather than selecting all); the latter sends a malformed property name and
is correctly refused, then Ctrl-X clears it for the successful read.

Fresh cold readonly service test `native-import-run.CUI5kqe8` reads the saved
32 MiB size, refuses a Manager API shrink with EROFS and verifies unchanged
size. The full retained raw-zvol and FAT read/denial suite plus mandatory
independent integrity gate pass with runner exit zero and unchanged hashes.
The archive V4kSsH9S predates these Manager API changes; its earlier CLI/device
resize evidence must not be presented as support for the new API setter.

Retained lObxjuCK passes mutation checks but fails because its test incorrectly
sent operation 40 through the mutation-admission port. The corrected getter
test uses the ordinary Manager client. This failure did not independently
prove the missing-stats read-path defect; source inspection identified that.
The first service build also retains its missing kernel-gated zvol prototype
error; build-3 includes the corrected declarations and getter integration.

`native-import-run.2S9QQiLP` executes the frozen `native-bundle.V4kSsH9S`
commands/service/device, keeps a resident device handle open, and changes a
disposable raw volume through actual upstream `zfs set volsize` calls. Growth
16 -> 32 MiB updates mapping metadata, SCSI capacity and trackdisk geometry;
write/flush/read at offset 24 MiB succeeds. Shrinking back to 16 MiB refuses
reads and writes at that offset without reporting transferred bytes, while
the original 512-byte prefix remains exact. Regrowth to 32 MiB returns zeroes
at the discarded range, not the earlier data. The selected full native/CLI
runner exits zero; independent `control-integrity.3CricYHy` reports No leaks
and unchanged extracted/source hashes. No production backend change was
needed for this test: it verifies existing upstream size handling through
the native device bridge.

This is not resizing a mounted filesystem, automatic filesystem expansion,
media-change notification coverage, or concurrent resize versus queued I/O.

Persistence extension: `AROS_TEST_ZVOL_RESIZE_PERSIST=write` requires the
existing KEEP/filesystem/CLI-retention mode and preserves the resized raw
volume separately from the FAT volume. Writer `native-import-run.wADUl6Ye`
exits zero; independent `control-integrity.pQuzuRSF` reports No leaks.
Fresh `read` guest `native-import-run.qigOPw5o` requires the writer's retained
marker, maps the resized volume readonly, verifies 32 MiB capacity, the exact
512-byte prefix, zeroes at the truncated range, write refusal and eject.
The full cold FAT regression and mandatory integrity gate
`zvol-integrity.ImuYrdVL` also pass, with all source/writer/reader hashes
unchanged. Initial uUSdtuxZ was refused before boot because the invocation
used AROS_TEST_CLI_KEEP instead of AROS_TEST_KEEP_CLI; no guard was changed.

### Safe administrative eject (device 1.9)

`OpenZFSDevice EJECT unit` and the Manager's confirmed `Eject unit...`
action use the versioned `OZCMD_EJECTMAPPING` request. The device requires
the caller's fresh change generation, a present mapping, exactly one open
handle (the requesting administrative client), no change subscriptions, and
no other queued requests. It excludes new opens/I/O while flushing through
the upstream zvol/ZIL path and releasing the mapping. It never unmounts a
filesystem, forces a detach, or deletes a dataset. An already absent unit is
an error, not a silently successful repeat. `io_Actual=1` reports release
even if subsequent session cleanup fails; CLI and GUI distinguish that case
from a refused/uncompleted eject and do not automatically retry.

`native-import-run.waAKU5Qo` passed the full native/FAT/CLI runner: malformed,
stale-generation and busy requests refused, mapping preserved on refusal,
successful release/change generation, repeated absent refusal, and actual
CLI requests. A mounted FAT handler alone prevents administrative eject;
the test closes its extra raw handle before trying, then verifies exact
file bytes after refusal. Independent `control-integrity.5C5hrpna` reports
No leaks and unchanged extracted/source hashes.

Private GUI `native-import-run.Chteip5Z` records writable remap, eject Cancel
(`06-eject-cancel.png`), then confirmed eject (`07-ejected.png`). After normal
Manager close, the independent native client proves absent state, generation
3, refused reads and successful remapping. It also writes without a preceding
CMD_UPDATE, calls actual CLI EJECT, remaps readonly and reads identical bytes.
Both Manager instances and the guest test exited normally. Security logging
split the final pool-import marker, so the harness did not detect completion;
QMP then stopped this specific private guest. This is GUI/action and guest
marker evidence, not a terminal harness pass. Independent
`control-integrity.RZjxrvB8` passes zdb No leaks and unchanged hashes.

Final headless `native-import-run.Aly43jG1` passes the complete FAT/native/CLI
runner with exit zero, including the new no-CMD_UPDATE writable-eject/readback
gate and mounted-handler refusal. Independent `control-integrity.RCZqSWcC`
reports No leaks and unchanged extracted/source hashes.

Fresh cold FAT run `native-import-run.aSWZcfB9` exits zero with exact retained
file bytes, write refusal, handler shutdown, independent GPT/zdb verification
(`zvol-integrity.Yssz32WU`) and unchanged source/overlay hashes. These tests
do not establish service-death recovery, flush-failure injection, physical
power-loss persistence or forced unmount support.

Retained earlier failures: AzuaV3MO had a misplaced protocol-test invocation;
0iaxJmFz passed guest checks but editing its running Bash script caused an EOF
parse failure. The runner now executes a private source snapshot so later
source edits cannot splice a running test. Neither earlier run is a full pass.

### Standard AROS mount-file generation

The native Manager Devices window now provides FFS/FAT selection, optional
512-byte block count, output path, Preview mount file and Save mount file.
Each action starts a fresh asynchronous device query with copied inputs;
the GUI and command share `aros/device/mountfile.h`, not separate geometry
implementations. Preview changes only the displayed description. Save confirms
the exact destination, unit and dataset and warns about replacement. The
existing native staged-file writer flushes, closes, reads back and compares
the content before publication, preserving existing metadata on replacement.
Failures distinguish unpublished output from completed publication with failed
cleanup. Nothing invokes Mount, Format, eject, or startup installation.

Private GUI `native-import-run.wMduVfaH` uses
`OpenZFSManagerNative-mountfile-v1` (build log has no warnings). Screenshot 03
shows absent-unit refusal, 07 the live bounded FFS preview, 08 confirmation,
09 Cancel, and 10/11 actual save and confirmed replacement. After normal GUI
close, a separate native test reads T:OZBLOCK and compares its exact bytes
against actual CLI MOUNTFILE output for unit 0 / FFS / 131072 blocks, checking
that the transaction directory is gone. The writable remap and remaining
two-unit/CLI/native tests pass; both Manager instances close normally and the
runner exits zero. This is not interactive FAT selection, concurrent mapping
changes, save-failure injection, pending-close stress or arbitrary target-file
security qualification. The saved description remains a snapshot as below.
Independent `wMduVfaH/control-integrity.z9HXkfkk` reports No leaks and
unchanged extracted/source hashes. Shared-formatter cold FAT regression
`oO6rL4wb` also exits zero, including generated-file actual Mount, exact bytes,
six mutation refusals and mandatory `zvol-integrity.mp5qblzc` GPT/zdb No-leaks
gate. Source, writer and readonly-reader hashes are unchanged.

Device 1.8 adds `OpenZFSDevice MOUNTFILE unit FFS|FAT [512-byte-block-count]`.
It queries the live mapping and writes a standard Mount description to stdout.
For example, explicitly redirect to a new file `T:OZBLOCK`, inspect it, then
run `Mount T:OZBLOCK`. The filename supplies the DOS device name. FFS selects
`L:afs-handler`; FAT selects `L:fat-handler`. Those handlers must be installed
separately. This command does not create a filesystem, format, mount, eject,
remap, install a startup entry, or overwrite a file itself. Shell redirection
can overwrite files, so choose a new output path and check the exit status.

The optional count is an explicit zero-based view bounded by the mapped zvol;
it is not a partition-table parser. Without it the whole mapped capacity is
described. A 512-byte sector is required. Geometry preserves every sector and
keeps fields within signed 32-bit tool limits; capacities that cannot be
represented exactly are refused, never rounded down. Producing a description
does not establish a filesystem handler's large-volume compatibility. The
output is a snapshot, not a lease: do not eject or remap between generating
the file and mounting, or reuse it after a mapping change without reinspection.
Existing raw-device system-owner authorization and readonly enforcement remain.

Native `xyQPptGO` uses the real command output to Mount a bounded 64 MiB FAT
view, then formats, writes, flushes, reopens and compares 128 KiB exactly.
Invalid unit, filesystem, zero count, numeric overflow and out-of-capacity
requests are refused. The selected native/CLI suite exits zero; independent
`control-integrity.D2VBHpxN` reports No leaks with unchanged member/source
hashes. `HUSLhwTm` passes the same actual-Mount filesystem lifecycle with FFS,
plus whole-6-GiB description checks. Both use the documented private FAT/AFS
fixes, not an arbitrary stock nightly.

Cold FAT `2AC5TQzO` passes the final harness's mandatory generated-mountfile
marker, exact retained bytes, six mutation refusals, teardown and independent
`zvol-integrity.3lua4aZh` GPT/zdb gate with No leaks and unchanged source,
writer and reader hashes. This is clean-export persistence, not power-loss
qualification. Frozen bundle 68mzVmSL still contains device 1.7; do not claim
this new command is present in that immutable archive.

Final two-unit `AZlSokKl` verifies exact 6 GiB and 3 TiB descriptions (the
latter uses 1610612736 cylinders of four sectors), refuses generation after
eject, and passes the full two-unit/remap/CLI suite. Independent
`control-integrity.vzhsXfrz` reports No leaks and unchanged extracted/source
hashes. FFS writer HUSLhwTm's independent `control-integrity.8oZMpMuM` also
passes the same gate. No filesystem was formatted at the 3 TiB capacity.

### Empty startup

Device 1.7 supports `OpenZFSDevice EMPTY`: publish 32 absent units without
opening a service session, importing a pool or opening any zvol. Configured
security.library and a system-owned caller are still required. Each empty
slot reports no name/capacity/media and generation zero; ordinary I/O returns
disk-changed. The existing REMAP command or Manager action can populate any
slot after a pool is imported. Readonly remains the default; writable access
requires the explicit option. An absent service makes mapping fail, not
launch/import storage implicitly. The device stays resident until reboot.

Initial native fixture `2X6DlwBJ` passes all 32 absent-unit queries, slot 31
readonly mapping/read/write refusal/eject, unit 0 writable mapping and the
complete selected native/CLI regression, with original hashes unchanged.
That initial run already had the engine running and is not evidence of
service-independent startup; the stronger pre-service fixture checks that
separately before the parent starts OpenZFSServiceNative.
Final `nJy7Y4DM` passes that stronger test: service port absent before and
after EMPTY publication; all 32 slots unchanged after a refused pre-service
remap; then the same device accepts real mappings after normal service launch.
The whole runner exits zero. Independent
`control-integrity.XnpBWfnP` reports No leaks and unchanged extracted/original
hashes. This proves first late service availability, not recovery from service
death while a volume is mapped. EMPTY still publishes a fixed inventory of
32 slots; it does not grow a previously registered smaller inventory.
Ordinary device-1.7 cold FAT regression `cVWb5ZNT` also exits zero: exact
filesystem bytes, six mutation refusals, remap/CLI lifecycle and mandatory
GPT/zdb gate `zvol-integrity.1g9vGNom` pass with No leaks and unchanged
source/writer/reader hashes.

### Native Manager remapping

The Devices window now provides a zvol dataset field, an unchecked writable
mapping option and a confirmed Remap ejected unit action using its Unit field.
It uses an owned worker with copied inputs, not a shell command. The worker
opens the unit, reads its generation, sends OZCMD_SETMAPPING once, closes the
handle and refreshes mappings. Failure after a successful mutation is reported
as completed remap / failed refresh, never as a safe automatic retry. Window
shutdown retains the existing worker-drain contract. No implicit eject or
filesystem detach was added.

`native-import-run.7QRcXc5Z` visually verifies Cancel leaving both units
absent (05), confirmation and successful readonly remap (04/06), visible
active-unit refusal (07), and fresh inspection preserving that mapping (08).
Only the Manager issues the initial remap; after normal close, the separate
native verifier checks readonly flags, exact sector contents and eject.
Both Manager windows close normally and the remaining two-unit/CLI/native
suite passes, runner exit zero and original backing hashes unchanged.
Binary: `OpenZFSManagerNative-remap-v1`; input hashes and QMP actions retained.
Independent `control-integrity.bqOK3C0r` reports No leaks and unchanged
extracted members/original backing hashes after the guest exits.
The writable GUI checkbox is covered below. Pending-request close stress, allocation failures
and a successful-remap/failed-refresh injection remain unverified; writable
protocol and CLI paths have separate acceptance below.

Writable GUI follow-up `native-import-run.eguyZrjO` uses the same Manager
binary, with `AROS_TEST_ZVOL_REMAP_GUI=2`. The actual checkbox changes the
confirmation to writable requested (02-writable-confirm.png); the completed
query shows a writable 6 GiB mapping (03-writable-mapped.png). Only the GUI
maps the unit. After normal close, the independent fixture verifies writable
flags, writes a new 512-byte pattern, flushes, compares every byte and ejects.
The final Manager window also closes normally; the whole runner exits zero
with unchanged original backing hashes.

Device 1.6 regression `Ptx1VImI` separately passes all 32 units, distinct
geometry/data, boundary and partial-startup cleanup, eight queued rounds
(32 MiB written and 32 MiB read with 256 flushes), four native client
processes with 1024 flushes, and independent per-unit eject/destruction.
Runner exits zero; original hashes match. This is concurrent I/O coverage,
not simultaneous remapping or task-death recovery.
Independent `Ptx1VImI/control-integrity.km6kKSXb` and
`eguyZrjO/control-integrity.Kt8oMscC` both report No leaks, with extracted
member and original backing hashes unchanged.

Retained preparation failures: initial Manager compilation used unavailable
CheckMark instead of the existing make_check helper; no binary from that
attempt ran. `hEZznicC` was rejected before guest boot by the explicit
FAT_HANDLER/non-filesystem harness guard. The corrected invocation removes
the irrelevant FAT handler override; it does not weaken the guard.

Device 1.6 adds explicit remapping of existing ejected units:
`OpenZFSDevice REMAP 0 pool/volume [WRITABLE]`. The default is readonly.
It does not eject, unmount, format, create a zvol, grow the unit inventory,
or publish a DOS device. The caller must close all other unit handles and
remove subscriptions first. The serialized worker requires an absent backing,
one open handle and no other queued requests; it excludes new opens/requests
during the remap transaction. Duplicate active dataset mappings are refused.
The versioned OZCMD_SETMAPPING structure supplies expected TD_CHANGENUM;
stale, malformed and busy requests fail before opening a volume.

Successful eject and remap increment the media-change counter. Failed opens
preserve the absent mapping and counter. After last-unit eject closes the
service session, remapping opens a new authenticated session. Mapping queries
return the latest explicit name; they still do not track dataset renames.
Code remains resident until reboot. Live inventory growth,
automatic DOS publication, service-loss recovery and broad SMP race testing
remain unfinished.

Native `CBCMfW6Q` passes the full writable FAT/native/CLI runner with device
1.6: malformed/stale remap rejection, another-open-handle exclusion, missing
dataset failure, readonly reopen with exact sector contents, session reopen,
generation changes, real REMAP command readonly and explicit writable paths,
then remap to a different 16 MiB zvol with new geometry and exact
write/flush/readback. All temporary mappings are ejected before destruction;
original backing hashes match. Earlier `BUWOO0Wf` passed protocol-only tests
and `6i187GBc` added command tests; neither covered the final changed-dataset
case. `j5PxARGy` is a retained test compilation failure (duplicate variable),
not a guest failure or passing run.
Independent `CBCMfW6Q/control-integrity.wlT5s703` reports No leaks and
unchanged extracted/source hashes. Fresh readonly FAT guest `CSMkd8lX`
also passes protocol/CLI remapping, exact retained filesystem contents,
protection and teardown; its mandatory GPT/zdb gate
`zvol-integrity.ip6sob3Q` reports No leaks and all source/writer/reader
hashes remain unchanged. Both final runners exit zero.

Device 1.5 adds persistent TD_ADDCHANGEINT registrations. Registration does
not reply the request; TD_REMCHANGEINT on that same request removes it and
completes it once. AbortIO removes and replies an outstanding registration
with IOERR_ABORTED. Successful eject causes callbacks for the affected unit.
Registration/removal are serialized with the device lock, separate from the
ordinary I/O queue. This remains a task-context API; callbacks are native
software interrupts and must not block. Clients must remove or abort/drain
registrations before freeing their requests/interrupts or closing their handles.
Process-death reclamation and broad concurrent-removal stress are not proven.

This closes the observed FAT shutdown failure. Failed normal-handler
`0Rsfvfo0` and traced `Ix0FbU7a` end at the bounded runner timeout (exit 1).
The trace shows unsupported TD_REMCHANGEINT completion while FAT reuses its
registration request; that lifecycle was incompatible with the previous device.
Candidate `anVvJ90t` completes the entire FAT/native regression; expanded
`j4JYKKKm` also passes explicit pending-registration, removal, abort, one eject
callback and empty reply-port checks. Revision-bump run `NkjtSDsB` fails only
the stale inquiry assertion expecting 1.4, retained as test failure evidence.

Final device-1.5 run `native-import-run.uU2QyXDs` passes with normal FAT handler
and no device tracing: format, 128 KiB write/flush/reopen/exact-read, handler
shutdown, eject, notification tests and the full selected CLI/native suite.
Runner exit zero and original hashes match. Independent
`control-integrity.0gjEbz7c` reports No leaks and unchanged extracted/source
hashes. Cold FFS reader `KE1e7sR3` passes exact bytes, readonly denial, clean
shutdown, the same notification checks and mandatory independent GPT/zdb gate;
writer/source/reader hashes remain unchanged. Both driver builds are clean.
This supersedes the earlier FAT first-open and cleanup failures below. FAT
cold persistence is covered by the later private-overlay tests above, not
these original runs; physical-media behavior remains unqualified. Frozen bundle
`mTc6zXx2` remains version 1.4 and is not silently updated.

Device 1.4 exposes a read-only mapping snapshot through the device-specific
`OZCMD_GETMAPPING` command in `aros/device/mapping.h`, advertised in the newstyle
command list. The versioned output returns unit/count, original mapped dataset
name, live capacity/readonly state and media presence. Ejected units retain
their original mapping name but report absent media and zero capacity/sector
size. Names describe startup mappings, not live rename tracking. Query errors
remain failures, not fabricated absent media. Short output buffers are refused
without modification. Existing system-owner checks apply, including requests
through a handle opened before the caller changed identity.

`OpenZFSDevice LIST` enumerates the resident device through that ABI; it does
not start a service, map new volumes or create DOS nodes. Absent/inaccessible
devices and unsupported query versions return failure with an explanation.
The command does not change the mapping. The Manager Devices window now has
a Zvol mappings action using an owned asynchronous native device-query worker.
It validates the versioned replies, displays startup names and media state,
and drains the worker before application exit. Mapping inspection invalidates
the previous partition selection and disables GPT/import actions; automatic
I/O refresh waits while its worker is active. Errors are not empty inventories.

`native-import-run.WzopsHgX` passes the combined CLI/device regression and
actual Manager inspection after both mapped volumes have been ejected. Capture
`07-mappings.png` shows both original dataset names, absent media and zero
capacity; a repeated query succeeds and the Manager closes normally. The runner
exits zero and original backing hashes match. Independent
`control-integrity.24ZlsULb` reports No leaks with unchanged extracted members
and original fixtures. Manager/device builds have no diagnostics; the CLI test
build retains existing const-qualification and unused-function warnings.
This proves the absent-media GUI path, not live-capacity rendering, unavailable
device feedback, delayed-worker close or service-loss behavior. Those remain
separate acceptance work. Earlier GUI launches never reached QEMU: two harness
guards disallowed this combined mode. Only post-regression GUI mode 1 is now
allowed alongside CLI tests; mutation GUI modes keep their restrictions.

Live GUI acceptance `native-import-run.dtoWYmwC` uses the opt-in
`AROS_TEST_ZVOL_MAPPING_GUI=1` fixture with GUI=1 and two mapped units. Before
eject, the real Manager displays unit 0 writable at 6442450944 bytes and unit 1
readonly at 3298534883328 bytes, both with 512-byte sectors
(`03-live-mappings.png`). After normal UI exit, independent native queries
require those exact mappings unchanged. The full regression then finishes,
including another normal Manager launch/close, with runner exit zero and
original hashes unchanged. Independent `control-integrity.XMx9P1RU` reports
No leaks and unchanged extracted members. The fixture adds no production
device behavior and requires its separate continuation marker when enabled.

Unavailable-device GUI run `native-import-run.ayHglXk8` also finishes with
exit zero and unchanged original hashes. With no resident device, the action
reports unavailable/access denied, not a successful empty catalog. Screenshot
`03-unavailable.png` exposed clipping of the final explanation; the message
is now shorter. The revised wording builds but has not yet had a dedicated
missing-device visual rerun. Delayed-worker close and service-loss coverage
remain unproven.

`native-import-run.Vp0kPmYr` passes live 6 GiB/readonly 3 TiB mapping queries,
live readonly-property changes, unauthorized-caller refusal without output
mutation, short-buffer refusal and actual LIST output before and after each
unit eject, including after the last backing session closes. The full native
suite exits zero with an empty driver build log. Independent
`control-integrity.sy5kTyot` reports No leaks and unchanged extracted/source
hashes. Initial `xBJO0haP` also passes
the earlier subset. Cold FFS reader `YmQO6WGE` passes mapping/LIST, exact file
bytes, readonly refusal, clean shutdown and the mandatory independent zdb
No leaks gate; source/writer/reader hashes remain unchanged.

Device 1.3 adds standard INQUIRY (36-byte direct-access response) identifying
the vendor as OpenZFS and product as AROS zvol. HDToolBox's 36-byte and the
native device inspector's 96-byte request shapes both return 36 valid bytes;
short allocation/buffer lengths are bounded. VPD pages are rejected with
ILLEGAL REQUEST rather than advertising unsupported identities/features.
This supersedes the version-1.2 INQUIRY limitation below. It is protocol
acceptance, not a claim that HDToolBox GUI interaction was tested.

`native-import-run.RtwMwTEg` passes inquiry, 6 GiB/3 TiB capacities, final-sector
I/O and full native/CLI regression. Driver build output is empty. Independent
`control-integrity.KnnkryUs` reports No leaks with unchanged source/member
hashes. Fresh FFS reader `mifzQnQe` passes inquiry/capacity, exact retained
bytes, readonly denial, handler shutdown and the mandatory independent zdb
gate with all source/writer/reader hashes unchanged.

Preserved initial run `AIKVdS5X` passes device tests and CLI import, then stops
progressing later in the broader suite. Two monitor captures about 50 seconds
apart show the same CPU-0 RIP and unchanged disk counters, with over 260
seconds disk idle time. QEMU remains running; it was explicitly quit through
the private QMP socket and the runner exits 1. Root cause is unresolved. The
unchanged-source retry above is a pass, not a fix or an explanation of the
earlier hang. Logs and both monitor captures remain registered on the development test archive.

Device version 1.2 adds capacity-only HD_SCSICMD support for READ CAPACITY(10)
and READ CAPACITY(16), service action 0x10. This supplies exact 64-bit last-LBA
and 512-byte logical-sector data when classic TD_GETGEOMETRY saturates.
READ CAPACITY(10) saturates its last-LBA field at 0xffffffff; it never wraps.
Replies respect both the CDB allocation length and caller buffer length.
Invalid CDBs/directions receive CHECK CONDITION with bounded ILLEGAL REQUEST
autosense; unknown opcodes report INVALID COMMAND OPERATION CODE. This is not
general SCSI passthrough: INQUIRY, PMI queries and SCSI data-write commands are
not implemented. Native trackdisk data I/O remains the storage path.

`native-import-run.9QsynFAR` verifies a sparse 3 TiB second unit: saturated
classic geometry/READ CAPACITY(10), exact READ CAPACITY(16), and write/flush/read
of the final sector through TD64. The 6 GiB first unit verifies unsaturated
capacity. Short replies, zero allocation, null buffers, wrong CDB lengths,
wrong direction and unsupported commands are checked. Full multi-unit and
native/CLI regression exits zero with an empty driver build log; independent
`control-integrity.anbfBQCa` reports No leaks and unchanged member/source
hashes. Earlier `QdvGYHCb` passes the initial capacity cases. Fresh cold FFS
reader `YCI2zQ2W` also passes capacity queries, exact saved bytes, readonly
denial, handler shutdown and mandatory independent GPT/zdb No leaks checks
(`zvol-integrity.XhPlTqP2`); all source/writer/reader hashes remain unchanged.
These tests do not format a 3 TiB filesystem or establish physical capacity.
The older frozen bundle nzV7QgZ0 still contains version 1.1, not this change.

`OpenZFSDevice pool/volume [pool/volume ...] [WRITABLE]` opens the authenticated
service and maps existing volumes to units in argument order, starting at 0.
The current admission limit is 32 mappings. The default is readonly;
WRITABLE is an explicit opt-in for the selected mappings. Code and vectors stay resident until reboot;
Expunge does not unload them and Ctrl-C does not terminate their owner. There
is no automatic mapping, remap or DOS mount creation yet; LIST discovers
the existing explicit mappings.

Mappings are admitted as one startup operation: duplicate names and oversized
names are refused; a failed later open closes the session and all earlier
descriptors before any device is published. Each unit has separate geometry,
readonly state and backing descriptor. Eject releases only that unit; the
session closes after the final unit is ejected. One resident worker serializes
the I/O queue across units; this is not parallel per-unit scheduling.

`native-import-run.UIJH92ui` passes two-unit native acceptance: sparse 6 GiB
and 32 MiB volumes, distinct geometry, independent data, unit-1 readonly
protection while unit 0 remains writable, successful unit-1 read after unit-0
eject, busy-volume destruction refusal, and final cleanup/destruction. Duplicate
mapping and partially failed startup are refused without retaining a backing
handle. The full selected native/CLI regression exits zero. This tests two
units, not the full 32-unit capacity or dynamic reconfiguration.
Independent `control-integrity.5yfaJ52V/zdb.log` reports No leaks. Combined
two-unit/four-starter barrier run `7UzO92lp` also passes the full selected suite.
Normal non-test binary `XtVgKOz3` passes cold FFS compatibility and the mandatory
GPT/zdb integrity gate with unchanged backing and reader images; both diagnostic
and startup-test build flags are zero in that run.

Full-capacity run `native-import-run.rYgpT8Pe` subsequently passes all 32
units: distinct 1–32 MiB geometries, unique data written at the same offset
on every unit, rejection of unit 32 and a 33-mapping startup, cleanup after
the final of 32 opens fails, readonly protection, and independent ejects
(evens first, then surviving odds). The complete native/CLI runner exits zero
with unchanged original images. Independent
`control-integrity.ZogOmVqS/zdb.log` reports No leaks. This covers the mapping
limit, not sustained 32-unit concurrent stress or dynamic reconfiguration.

Packaged-binary follow-up `native-import-run.JkdWu0Bj` passes eight rounds of
32 outstanding per-unit writes, flushes and reads: 32 MiB written, 32 MiB
byte-checked, 256 flushes and an empty reply port after each round. Transfers
are 128 KiB, exercising multiple 64 KiB service chunks; distinct unit/round
patterns and separate buffers check completion/data isolation. The full
capacity/lifecycle and CLI/native suite exits zero. Independent
`control-integrity.9yiFVCce` reports No leaks and unchanged extracted members
and original fixtures. The device is byte-identical to the frozen
`native-bundle.nzV7QgZ0` executable. This is bounded queued I/O from one
submitting task, not parallel per-unit workers, simultaneous callers, service
loss, active cancellation or long-duration endurance.

Multiple-submitter acceptance `native-import-run.1LapCWs1` adds four separate
native executables. Each opens all 32 units before the parent releases their
ready-message barrier; each performs eight rounds of queued writes, flushes
and exact reads at disjoint caller-specific offsets. All 128 child opens are
closed, 1024 flushes complete and reply queues drain; the parent subsequently
verifies the original per-unit data, ejects units and destroys their datasets.
The exact packaged device remains unchanged. Full native/CLI regression exits
zero and independent `control-integrity.znLcsKBq` reports No leaks with unchanged
member/original hashes. This covers concurrent native submitters on two guest
CPUs, not parallel engine workers or abrupt task death. Initial `VmJglNOj`
also passes but retains a helper indentation warning; the repeat's helper
build log is empty after correcting that formatting.

Earlier `native-import-run.eVpI8aC2` failed waiting for device registration
after the long shell-mediated launch; no device child was observed. The exact
shell failure was not captured and is not diagnosed as a driver capacity bug.
The successful fixture uses LoadSeg/CreateNewProc with explicit arguments and
owned handles, avoiding the shell. No production device or Shell change was
needed. Both the failed fixture and its replacement are retained.

Device publication now performs its final absence check and insertion under
the public execlock.resource DeviceList write lock on SMP, or Forbid on classic
uniprocessor Exec. It prepares the same node type, flags and checksum as
AddDevice; it cannot call AddDevice while holding this nonrecursive list lock.
This direct insertion does not invoke Exec's optional internal OPAL event hook.
The losing starter releases its unpublished library, signal, buffers, security
library and service session. No shared AROS/Exec source is changed.

`native-import-run.PhRbCDjC` passes a two-CPU test with four separately launched
processes. A test-only message barrier holds all four immediately before
publication, then releases them; exactly three report duplicate rejection and
exactly one device-list entry remains. Device I/O, authorization, queued abort,
eject, volume destruction and the whole selected CLI/native regression pass,
including unchanged fixture hashes. The device destruction/export checks also
exercise release of the losers' backing references. Initial delay-based test
`hjwtJTCT` passes but is weaker evidence than the barrier run. The barrier is
compiled only with AROS_TEST_ZVOL_DEVICE_RACE=1 and its build flag is recorded.
Normal non-barrier binary acceptance `native-import-run.m25piKEL` also passes
duplicate-start rejection, cold FFS bytes/readonly/shutdown, independent zdb
No leaks and all unchanged disk hashes.

OpenDevice and BeginIO require configured security.library and the system
owner. The resident worker sends authorized queued operations through its own
authenticated service session; it does not pretend to forward the original
IORequest task as the service's packet sender. This is a task-context interface,
not an interrupt-safe block driver or an MMU security boundary.

The queue supports actual BeginIO/AbortIO and reply completion. Queued requests
can be removed; active cancellation is observed between transport chunks, not
by undoing completed writes. io_Actual reports completed bytes. Supported I/O
includes CMD_READ/WRITE/UPDATE, TD/NSCMD 64-bit read/write/format variants,
geometry, newstyle command query, protection and media-state queries. The
maximum transfer chunk is 64 KiB; larger valid requests are split. Offset and
full range/alignment are checked before the first chunk. TD_EJECT closes the
volume handle and session, leaving the resident unit reporting absent media.

`native-import-run.ai6fEJTU` passes the full native/CLI regression with exit 0
and unchanged original disk hashes. A real separate process calls OpenDevice
and DoIO against a sparse 6 GiB zvol, verifies geometry/newstyle query, writes
and flushes distinct 128 KiB payloads at low and above-4-GiB offsets, reads them
through TD_READ64, checks misalignment/end-of-volume failures, toggles the
upstream readonly property and observes write refusal, ejects, checks missing
media, closes the device and destroys the now-unheld volume. This is not a
filesystem formatted on the zvol or a cold persistence test.

`native-import-run.Lox0UY7w` extends that acceptance with rejection of unit 1,
changed-user denial on an already opened handle, ordinary-user OpenDevice
denial, successful access after restoring system identity, and eight real
asynchronous reads followed by AbortIO/WaitIO. All eight were observed aborted
and the reply port was empty after draining their completions. The complete
native/CLI regression passes with unchanged original disk hashes. This is
queued cancellation coverage, not proof of interrupting a transport call or
rolling back an already completed write.

`native-import-run.1eUEKMJ7` verifies default-readonly startup without WRITABLE:
geometry, sparse zero-filled reads, rejected writes, eject and volume destroy.
The full native/CLI suite passes with unchanged original disk hashes.

### Filesystem on the block device

`AROS_TEST_ZVOL_DEVICE_FILESYSTEM=1` adds a private 64 MiB DOS view of the
disposable 6 GiB zvol. `AROS_TEST_ZVOL_DEVICE_AFS=1` selects AROS FFS. It formats
through the actual handler, writes and flushes a patterned 128 KiB file,
inhibits/uninhibits, reopens and checks every byte, withdraws its own DOS device
node, shuts down the handler, then ejects and destroys the zvol.

`native-import-run.NWp94WS9` passes the complete native/CLI suite with unchanged
original disk hashes and device tracing disabled. It uses the private AFS
overlay `afs-zvol.sRQEO2AS`, whose shared source/object input hashes remain
unchanged. This verifies a real native filesystem consuming the block device,
not automatic volume publication, cold boot persistence or hardware durability.

The dependency overlay fixes an actual AFS ACTION_DIE double reply: its packet
loop replied before cleanup, then replied again after AFS_free. Deferring that
single reply until cleanup eliminates the reproduced next-RAM-open stall.
The patch and private rebuild are under `aros/platform/afs-single-die-reply.patch`
and `build-afs-zvol-overlay.sh`. No shared AROS checkout or OS image is changed.

Preserved failures: `FXtM7xjR` passed FFS data checks but faulted in the still-live
inhibited OZBLOCK task after eject. `hnKciBqF` found the fixture Mount command
does not implement SHUTDOWN. Direct ACTION_DIE runs `4HhAovRk`, `Aq4Muyou`,
`tS9anTmB`, `nDxm8Xib` and `a7BNZJt2` stalled on the following RAM-file open;
withdrawing the DOS device node alone did not fix it. Traces prove zvol eject
and device cleanup had completed. `mt9TsrZT` failed handler startup because
the first private relink wrongly prepended library entry code; corrected
handler ordering begins with afs_Handler, matching the original executable.

The FAT alternative is NOT passing: `Wel7xiMX`, `r0uZ21Qs`, `SBpgQFXY` and
`rO8x2INE` accept format but fail the first file open (DOS error 225 where
captured), including inhibit-before-format. The retained raw boot-sector bytes
look valid; that alone does not prove the FAT formatter/parser lifecycle is
correct. No FAT fix or full FAT-on-zvol acceptance is claimed.

Follow-up with frozen device-1.4 bundle `mTc6zXx2` reproduces DOS225 in
`native-import-run.6dk2ky4J`. Diagnostic build helper
`aros/platform/build-fat-zvol-overlay.sh` snapshots shared sources/objects,
rebuilds only private copies and verifies shared hashes unchanged. Initial
build `WYf4Gb4l` stops at an incorrect archive path; `s7lPZMHN` builds with
the actual SDK archive. Run `FL7WSIl8` replaced only the boot package and did
not emit diagnostics because the fixture explicitly loads L:fat-handler.
The runner now supports a separately hashed AROS_TEST_FAT_HANDLER override;
`x3AvImEC` proves that path with real FAT trace output.

The trace shows the initial blank-sector rejection and successful formatting,
but no post-format rescan. The fixture passed Exec TRUE (1) to Inhibit,
although the DOS API specifies DOSTRUE (-1); FAT compares explicitly against
DOSTRUE. Correcting the fixture lets normal-handler run `0Rsfvfo0` format,
write/flush and reopen/compare all 128 KiB. No production FAT parser or zvol
I/O change was needed for that first-open failure. The same run stops making
progress after its filesystem-data PASS marker, before device cleanup completes;
it is NOT a complete passing FAT suite. QMP still reports running. FAT's
persistent TD_ADDCHANGEINT/TD_REMCHANGEINT lifecycle is a current investigation
because this device does not implement that subscription; causality is not yet
proven. Preserve this separate cleanup failure rather than claiming completion.

### Cold filesystem read and independent integrity failure

Writer `native-import-run.N6H5yV5U` retains the FFS volume after the full
native/CLI regression. Fresh reader `native-import-run.3spkpSuK` imports that
pool readonly, starts the default-readonly device, mounts FFS without formatting,
compares all 128 KiB of the saved file, rejects file creation with
ERROR_DISK_WRITE_PROTECTED, shuts down AFS and ejects. Both runners exit 0;
original disks, retained writer overlays and cold reader overlays pass their
unchanged hashes. The cold fixture also fixes the test-only live-iostat probe:
it previously attempted writes before the cold CLI had imported its pool.

Independent verification is NOT passing. Run
`bash aros/device/verify-cold-filesystem.sh build-aros/native-import-run.3spkpSuK`.
The retained `zvol-integrity.esrLILeL/zdb.log` reports three leaked 4096-byte
allocations (12288 bytes total), so the verifier correctly exits 1. GPT extent
validation passed. The original fixture's independent zdb check reports
No leaks (`build-aros/zvol-cold-original-zdb.log`). The cause is not yet isolated
between the zvol lifecycle and the broader writer regression. Successful guest
reads therefore do not establish complete on-disk integrity. No repair was
attempted and the failed images remain available for diagnosis.

Isolation controls narrow the failure to destruction of written datasets:

- Reduced zvol writer `4lKpsJGV` and cold reader `nsYKK0A1` pass guest tests,
  but independent `zvol-integrity.KWsoLDPO/zdb.log` reports 8192 leaked bytes.
- No-zvol CLI control `FbBLBxDT` also reports 8192 leaked bytes.
- Native-only control `2hZsScGu`, with no CLI or zvol tests, reports 4096 bytes.
- Otherwise matching native control `dTBSAqmT` retains the written api-names
  dataset instead of destroying it and passes independent zdb with No leaks.

The synchronous destroy path relies on traversal for ZIL reclamation, while
traverse_zil skips unclaimed logs on writable pools. A candidate engine fix
explicitly frees precisely that skipped case; runtime verification is pending.
This is not evidence of a fault specific to the new zvol device. The zdb exit
status alone was zero even with leak diagnostics: acceptance requires the
explicit No leaks result, as enforced by the cold verifier.

The candidate rebuilt in private `sync-destroy-zil.KGho64Cx` passes the exact
native destroy reproducer in `native-import-run.zIFcjJvb`; independent
`control-integrity.wYmFol20/zdb.log` reports No leaks and extracted members
remain unchanged. The change frees unclaimed ZIL blocks in the synchronous
destroy path; claimed logs retain the existing traversal ownership to avoid
double free. Broader writer and cold zvol regression remain pending.

Full fixed writer `native-import-run.mTih84aE` subsequently passes the native
CLI, pool topology/maintenance, authenticated transport and FFS/zvol suite.
Fresh reader `okynFpHY` passes exact file read, readonly denial and clean
handler/device shutdown. Independent `zvol-integrity.gSz9yetZ` reports No leaks
and validates GPT extents, retained zvol identity and all unchanged hashes.
The updated verifier still rejects original bad reader `3spkpSuK` (retained
`zvol-integrity.4eI3bOpJ`) and now checks non-mutation even on diagnostic failure.

Cold-zvol runner acceptance now includes that independent verifier as a
mandatory final gate, rather than accepting guest byte checks alone. Repeat
reader `R7G0d8iu` passes the integrated gate (`zvol-integrity.eJmhVDFe`) and
exits zero. This completes clean-export/cold-read FFS-on-zvol acceptance, not
power-loss, active cancellation, concurrent mapping or physical-device testing.

Use AROS_TEST_ZVOL_DEVICE_KEEP=1 with AROS_TEST_KEEP_CLI=1 in the writable FFS
fixture, then AROS_TEST_ZVOL_DEVICE_COLD=1 with NATIVE_WRITABLE=2 and
AROS_TEST_NATIVE_VERIFY_RUN pointing at the writer. Both require DEVICE,
FILESYSTEM and AFS modes plus the private AFS overlay described above.

Still required: sustained full-capacity stress, hot remapping, Manager integration, DOS publication,
active mid-chunk failure injection, service-loss
recovery, safe unload if desired, and sustained concurrent lifecycle stress.

## Authenticated service transport

`ui/zvol-api.h` defines version 1 of an inline, bounded DOS-packet protocol
on the existing admission endpoint. Operations are OPEN, INFO, READ, WRITE,
FLUSH and CLOSE. OPEN accepts a dataset name, not a host path or native device.
It returns a session-local descriptor retaining the real upstream zvol handle.
No service pointers cross the interface. READ/WRITE are sector-aligned, with
64-bit byte offsets and a maximum of 65536 bytes per request. INFO returns
volume size, upstream volume block size and effective readonly state; the
logical transfer sector is 512 bytes, not the returned allocation block size.

Every request captures current security.library packet credentials, requires
ZFS administration permission, and verifies session ownership. A previously
opened writable handle does not bypass changed caller identity. A readonly
handle still refuses writes for an authorized administrator. Non-zvol session
descriptors are rejected before interpreting any private state. An open writable
request is refused when the service engine is readonly. The session table owns
the file reference: explicit handle close, session close and existing task-exit
retirement release it. The normal service busy check keeps sessions resident.

Validation/authentication failures are transport errors and do not update
caller bytes. Accepted operations return `engine_error` and `completed`;
partial I/O must not be automatically retried. Read replies copy only completed
bytes. Temporary request/data allocations are cleared before release. Engine
operations execute on the service task under the captured credential scope.
This shares the existing native flat-address-space IPC trust boundary; it does
not add MMU isolation or make malicious invalid pointers safe.

## Verification

`AROS_TEST_ZVOL_TRANSPORT=1` enables the native client in the full private
QEMU runner. It creates a sparse 2 MiB upstream zvol, opens a retained writable
descriptor, verifies geometry and exact sector write/flush/readback, rejects
unaligned and beyond-end access, verifies readonly handles and descriptor-type
checks, changes native identity to verify fresh authorization, and closes a
session with a retained handle before destroying the dataset. The runner
requires the dedicated transport PASS marker, not merely a successful build.

`native-import-run.mHUl5cHF` passes this test and the full native/CLI regression
with exit zero, using rebuilt `full-zvol-transport/OpenZFSServiceNative`.
Original disk-image hashes remain unchanged. The new wire header was copied
into the evidence directory after this run; subsequent runners capture/hash it
alongside the other API inputs before compilation. This run does not test the
listed concurrency, task-exit, cold-volume or actual Exec device cases below.

Remaining beyond the initial frontend: generalized unit mapping and controlled
DOS publication, filesystem-on-zvol cold tests,
task-exit-with-open-zvol tests, concurrent I/O/close/removal, partial-failure
injection, sustained workloads and integration with the Manager's volume UI.
