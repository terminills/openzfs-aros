# AROS OpenZFS implementation and historical test notes

Pinned upstream: OpenZFS zfs-2.4.4, commit
`71a9f9578616a90c3c14bb59629fb4d31bfd68d1`.

This is a port of the upstream engine, not an extension of AROS's private
`rom/filesys/zfs` on-disk format. Keep that handler available for existing
volumes. Migration requires a verified file copy to a new pool; do not point
OpenZFS at the existing Work volume or assume in-place conversion.

## Current integration evidence (2026-10-04)

Real mirror checksum repair now has cold-boot evidence:
`checksum-run.9qVqvwzI` completed with host exit 0 after one byte of an
allocated 4-KiB payload block was changed in a disposable member copy.
The AROS scrub required a checksum-error counter on that member and a
finished scan with no unrepaired errors; all 16 KiB of payload matched.
A separate boot with only the repaired member attached passed native reads,
with no change to that member during the read-only boot. Independent Linux
`zdb -bcc` of the repaired partition passed with no leaks. Original fixture
hashes remained unchanged. This is actual data damage and repair, distinct
from seeded error-report records; it is not physical durability qualification
or verification of the normal Manager's error presentation.

`checksum-run.VJuV26lQ` separately passes the unrecoverable case: the same
payload byte is damaged in both mirror members, scrub reports checksum errors
on both leaves and unrepaired errors, and a payload read returns ECKSUM.
The real upstream error-log retrieval, using a registered output buffer,
identifies the exact objset/object/level/block without any seeded record.
This run proves in-session attribution. Follow-up `checksum-run.k79jslE2`
completed with host exit 0 and verifies the same record after a separate cold
boot, before scrubbing or reading the damaged payload. This establishes error
log persistence after orderly export, not abrupt-power-loss persistence or
presentation through the DOS handler and Manager.

The separate real-ZPL-file test closes that presentation gap for one file:
`file-corruption-run.4tkyW8Ym` reproduced `Read=-1, IoErr=224` (incorrectly
read-protected) while upstream CLI correctly named `OZCORRUPT:/hello.txt`.
Fix `268e8c3d` translates EIO/ECKSUM to unreadable-disk error 225 without
changing permission errors. Rebuilt shared-engine `IHjB3u8i` passed
`file-corruption-run.dXzJ5bhb` with exact IoErr 225, correct CLI filename,
unmount/export and unchanged original hashes. Actual Manager run
`file-corruption-run.K91ZCXxW` also completed with host exit 0; screenshot
`real-error-04-report.png` shows the permanent filename and both mirror-leaf
checksum counters. Closing Manager normally allowed unmount/export to finish.
The broader normal-package regression `native-import-run.kV4rJ49e` also
completed with host exit 0 for `native-bundle.gPvzL6mg`: configured CLI/DOS,
library lifetime, cache pressure, received-property expansion, vdev/zvol,
relocation, and writable/read-only reimport gates passed, with original hashes
unchanged. `file-corruption-run.3shJ35Bh` additionally verifies three repeated
failed reads return error 225 without advancing the DOS file position.
These remain private-runtime qualification results, not stock-nightly support.

Current reduced-runtime qualification found a concrete prerequisite:
`native-import-run.KU39PI8X` failed with the posix-pipe-stdio profile because
both LoadSeg and NewLoadSeg executed a RAM file with FIBF_EXECUTE disabled.
The redirected-stdio checks passed. Do not extrapolate older minimal-profile
passes to the current runtime-check requirements. The explicit
dos-posix-pipe-stdio comparison adds the verified DOS overlay but still rejects
ATA, partition and filesystem replacements. Run `native-import-run.toIPqtFU`
completed with host exit 0 at 16 vCPUs/8192 MiB: runtime prerequisites and
the configured normal shared-engine CLI/DOS, cache-pressure, library-lifetime,
received-property, vdev/zvol and relocation tests passed, including writable
and read-only reimport and original backing hashes. The selected local nightly
ISO still receives the entire `loader-overlay.L7ctzwkQ/aros-base.pkg.xz`, not
just a separately loaded DOS library. A subsequent binary package audit against
that ISO found the same 26 module names in the same boot order; only
`dos.library` differs (base SHA256
`57afb89798e8dcff9e427310d23e0256092ae29f9bcce41e906ce7efaf4bdc53`, overlay
`f60747a99a68ca824765fa239b06df2f2d56e36dc591216850df90de85014c5b`).
The extracted base is retained in external `runtime-package-audit.g2h6rwpR`.
This isolates the changed boot module, not an individual DOS source patch:
the overlay's link inputs also include the earlier private CLI-environment DOS
build. DOS, POSIX, PIPE and stdio remain explicit runtime prerequisites for
this qualified configuration; arbitrary stock nightly support remains unproven.

`native-bundle.h0Gjs0Wd` is the subsequent real-UI candidate: it replaces the
regression bundle's Manager validation harness with the freshly built native
Manager, retaining shared engine `IHjB3u8i`, CLI `f8YUrNaj`, handler
`7ZpHp1AH` and normal device from `fresh-zvol-20261003`. Staging and negative
packaging-policy tests passed. Exact-package graphical runtime acceptance
`native-import-run.kVRdhJhQ` completed with host exit 0 using the reduced-runtime
profile. The actual packaged Manager reported upstream OpenZFS 2.4.4 available
and its live List pools request returned zero pools after fixture exports;
normal window close completed the runner and all original backing hashes
matched. Screenshots `release-manager-01.png` and
`release-manager-03-pool-response.png` retain that narrow UI verification.
The first invocation `EjiXrruV` was rejected before boot because cache-pressure
mode requires the headless standalone-handler fixture. That gate was not
weakened: the GUI invocation omits that mode, whose coverage remains the
separate completed `toIPqtFU` run. No GUI pass is inferred from package hashes.

DOS publication failure recovery is verified in the isolated 16-vCPU/8192-MiB
run `native-import-run.uH9XJyny` (host exit 0). The private fixture injects
registration EEXIST followed by cleanup EIO: the failed filesystem is not
advertised as mounted, and retry, unmount and destroy all succeed. The full
configured writable/read-only reimport suite and original backing hashes also
pass. This fixes the ownership leak reproduced in `native-import-run.SRKjvUAs`,
where retry stayed busy after the failed cleanup. Cleanup-only entries retain
ownership but are excluded from mounted reports and DOS stream routing.
This is deterministic fault-injection evidence, not physical failure testing.
The separate normal shared package `native-bundle.6kFxkAtM` with
`shared-engine.vuxt0f2t` passed `native-import-run.qBkVic8B` (host exit 0),
without the fault wrapper. Its configured CLI/DOS, library lifetime, received
property expansion, cache pressure, vdev/zvol and relocation checks, followed
by writable/read-only reimports and unchanged backing hashes, passed. Both
runs use private AROS runtime dependencies, not a stock-nightly qualification.

This remains an experimental port, not a production filesystem release.
The current cache-integrated shared engine `shared-engine.zFjS8hrZ`, staged
as `native-bundle.bIv7D6Vr`, passed the private 16-vCPU/8192-MiB QEMU run
`native-import-run.yZmIwFUs`: upstream CLI operations, native DOS handler,
zvol, library lifetime, native path relocation, export/reimport and unchanged
original backing hashes. This package contains a Manager validation harness;
it is an acceptance-test package, not a public end-user release artifact.

Native metadata caching now reclaims idle clean znodes at owner sync and
metadata traversal points while retaining referenced, dirty and suspended
state. `qemu-run.oIhv31gC` verifies 320 real files over three read-only passes;
`qemu-run.CPJjt0Al` passes all five boots with rebuilt creation, import and
reopen binaries. Production run `native-import-run.uCBZSX5t` also passes
320-file creation, enumeration and exact reads through public DOS calls,
including three passes after export/read-only reimport. DOS correctness and
the standalone cache-count bound remain distinct coverage claims.

Remaining qualification boundaries include stock-nightly/runtime dependency
compatibility, broader production-service pressure and lifecycle coverage,
and physical flush/disconnect/power-loss durability. High-memory QEMU OHCI
tests expose an OS memory-lock wait in completion-buffer release; passing IDE
or lower-memory USB tests do not fix that fault. Network sharing providers
and live file-backed path relocation are not supplied by these results.
The intentional no-mmap boundary below is not a missing pager to recreate.

Normal shared-service file-backed qualification (2026-10-04):
`file-backing-run.oybu5cry` completed with host exit 0 using the service/library
from real-Manager bundle `h0Gjs0Wd`. A marked disposable client starts the
normal WRITABLE service under the fixture identity and uses public ports;
the diagnostic admission service is absent. Four-file RAIDZ2 creation, DOS
payload, export, offline relocation, cold read-only import, ONLINE members,
directory enumeration and unchanged member bytes passed. Separate upstream
`zdb -e -bcc` on the extracted relocated members exited 0 with No leaks.
Commit `8a6a1b23` makes that independent check mandatory in subsequent
relocation runs. The earlier `KYFCfXf0` passed only the first guest boot and
then failed the host's CRLF-incompatible readiness check; it is retained as
partial evidence, not counted as a cold-import pass. These tests use private
QEMU runtime overlays and FAT files, not physical devices or live file rename.
Two-file mirror `file-backing-run.gfraZVFg` also passed both boots and the
runner's mandatory independent zdb check. Three-boot production RAIDZ2 run
`file-backing-run.fxlQd6KH` passed with two of four members withheld: native
DOS wrote a 256 KiB patterned payload through the remaining members, the
third boot read it back exactly, absent members remained byte-identical, and
survivor-only upstream zdb reported No leaks. Both host runners exited 0.
This covers explicitly absent regular-file members, not hot removal during I/O.

See [device and recovery evidence](engine/DEVICE-CONTROLS.md) and
[distribution history](distribution/README.md) for exact provenance and
limitations. New artifacts and compiler scratch are on the external
`OpenZFS-archive` drive; older `build-aros` references remain historical.
The dated reports below are retained rather than rewritten as current claims.

## Reading the verification history (2026-09-28)

### Intentional platform boundary: file mapping (2026-10-02)

Per Nick Andrews' clarification supplied by terminills, AROS intentionally
does not implement mmap. File-backed MAP_SHARED/pager coherence is therefore
not an OpenZFS completion requirement for this target. It must remain explicitly
unsupported, not be advertised as working through copied buffers.

The supported no-mmap alternative is native DOS message/packet I/O: callers
provide buffers, the handler completes upstream ZFS I/O before replying, and
callers may reuse/free those buffers only after completion. This shares access
to an allocated buffer between cooperating tasks, not a writable memory alias
of a file or of ARC. See zpl/README.md for the ownership and durability boundary.

This file contains dated stage reports. Statements such as "native import is
readonly only" below describe the binaries and tests in those older reports;
they are not the current implementation's overall capability limit. Do not
combine a historical limitation with a newer result without checking its
exact source/build inputs and test scope.

Current private-QEMU evidence includes writable native mirror import, DOS
write, export/reimport, and the upstream CLI/Manager regression. For example,
`native-import-run.W4JPzK1V` completed with host exit 0 and unchanged original
backing hashes; writes occurred in disposable overlays. It also retained
actual native Manager screenshots for DDT pruning. This is not evidence of
safe production deployment or physical-device durability.

Recent operation-specific implementation and evidence records:

- [Pool wait](cli/WAIT.md): real upstream wait, Manager worker, and a
  deterministic pending wait with concurrent status and scrub-stop.
- [Snapshot diff](cli/DIFF.md): upstream snapshot/live differences, delegated
  access, retained large reports, and native Manager visual verification.
- [Metadata prefetch](cli/PREFETCH.md): upstream DDT/BRT requests and
  authenticated Manager execution; no cache-performance guarantee.
- [DDT pruning](cli/DDTPRUNE.md): populated pruning, shared-reference
  retention, exact file reads, permission checks, invalid-age recovery and
  actual Manager UI. Includes the discovered upstream active-state repair.

The port is still a development build. Tests use private AROS overlays;
both monolithic and shared-engine bundles now have scoped runtime evidence.
For example, `native-bundle.zpVlZgxR` passes the selected native CLI, alias
pool-create, ZVOL and DOS-handler regression in `native-import-run.FJ3GnMjA`,
and two-process startup testing in `startup-cold.WgqCzq0M`. Hosted authenticated
GUI snapshot persistence is recorded in `hosted-security-test.jYIpzmlR`.
These do not qualify a stock nightly, every UI/CLI option combination, or
hardware flush/disconnect/power-loss behavior. Consult [distribution notes](distribution/README.md)
for exact staged bundles and their narrower acceptance evidence. Catalogue
visibility alone is never an implemented-operation claim.

## Native partition import and GUI acceptance (2026-09-25)

`build-aros/native-import-run.iM32VkMS` completed with exit 0 using the real
management service and two disposable GPT mirror disks in private QEMU
10.1.94. Two authenticated import/DOS-read/export cycles passed before the
interactive manager opened. In the manager, Devices inspection of ata.device
units 0 and 1 populated the partition catalog; Add to import constructed the
exact `/AROS/ata.device/{0,1}/partition/0` paths. Confirmed readonly import and
subsequent export both returned errno=0. Screenshots `catalog-draft.png`,
`live-preview.png`, `gui-import-result.png` and `gui-export-result.png` retain
the UI evidence. After closing the manager, both backing-disk and both qcow2
overlay SHA256 checks passed unchanged.

The subsequent noninteractive `native-import-run.yY3E13lK` also passed with
stronger DOS checks: three separate opens per import cycle, exact fixture
length and byte comparison, EOF, seek to beginning and reread. Both import
cycles and all four unchanged-disk hashes passed. This replaces the earlier
prefix-only content check; it does not broaden the readonly ownership claim.

Reproduce the interactive extension with `AROS_TEST_GUI=1` and
`AROS_TEST_MANAGER_BINARY` pointing to the built manager, alongside the normal
inputs for `aros/engine/test-qemu-native-import.sh`. It retains binary hashes,
QEMU identity, serial output and source diff. The automatic PASS covers the
backend cycles and unchanged disks; GUI actions require separate visual
evidence, not merely a successful manager exit. The earlier headless attempt
`native-import-run.KITlqJOx` lacked screendump support and was stopped; it is
not a completed GUI pass.

Native import is readonly only: writable import returns ENOTSUP, and requesting
a writable DOS mount of the readonly pool returns EROFS. This does not prove
exclusive device ownership, writable native administration or physical durability.

### Readonly native degraded recovery

Explicit native paths now retain unavailable bindings when the unit cannot be
opened (IOERR_OPENFAIL) or a valid partition catalog lacks the requested slot.
Busy devices and other native open errors remain errors; unreadable/corrupt
partition catalogs are not silently treated as missing members. An unavailable
binding returns ENODEV on reads and EROFS on writes, never unbound ENOENT, so it
cannot fall through to fixture raw opens or adopt a replacement until export
and fresh import. Upstream OpenZFS decides whether survivors suffice. All label
paths must still belong to the caller's explicit path list.

Final full-service QEMU runs, all exit 0 with unchanged backing/overlay hashes:

- `native-import-run.bwmcfIu6`: member 0 only, member 1 physically absent;
  upstream status DEGRADED, two import/DOS-read/export cycles.
- `native-import-run.BsBi5ltS`: member 1 plus a fresh empty-GPT nonmember at
  unit 0; requested partition 0 is absent. DEGRADED, same two cycles. The
  empty disk and its overlay also remain unchanged.
- `native-import-run.wDhPnbAs`: both original members, ONLINE, same cycles.

Each run rejects writable native import, repeats an all-missing import refusal
to check binding cleanup, and rejects duplicate paths at admission. Reads check
exact bytes, EOF, seeking and three reopen cycles per import. Use
`AROS_TEST_NATIVE_MEMBERS=0`, `1-empty-peer`, or `both` with the native import
runner. The manager's import builder explains that absent member paths must
also be supplied; its updated native binary builds successfully.

Retained failures: `2PnME2KK` expected duplicate-path EEXIST from the engine,
but admission correctly returned EINVAL before creating a job; the test was
corrected. `tTXOAoja` attached only the IDE slave: ATA failed to identify the
absent master and then the slave, leaving no readable members. The empty-GPT
peer case avoids that enumeration failure, but does not claim to fix ATA or
prove a slave-only physical layout. RAIDZ degraded native recovery and native
replacement/resilver still need their own coverage.

Broader regression failures retained after this change: public-service runs
`htGR0SzW` (default AIO) and `Vo28fjPj` (thread AIO) both reached an ATA unit-1
DMA 0xc8 timeout, LBA32/224 sectors, under `/usr/local/bin/qemu-system-x86_64`
(SHA256 d93c221a...). The prior control used the distinct private QEMU binary
5919320a..., despite both reporting version 10.1.94. With that exact private
binary, `FXjPaPVC` reached all guest lifecycle/storage PASS results but was
manually stopped: the invocation mistakenly required the optional mount-cache
probe absent from the supplied client. That run is not a terminal harness/hash
pass. No ATA or QEMU fix is claimed from changing the test executable.

`public-service-run.XcGQUZA7` then completed the matching full public-service
regression with exit 0, using private QEMU 5919320a..., thread AIO and the same
new service binary. Required control, native initialization, partition and
label tests passed, including repeated native label reads and the RAM-backed
mirror DOS round trip. Final test-disk and original-label-source hashes passed.
The optional upstream mount-cache probe was not compiled into this client and
is not claimed by this run.

## Native GPT labeling operation (2026-09-25)

`oz_efi_label` now implements the upstream `zpool_label_disk` data/reserved
partition layout against an already owned native libefi handle. AROS uses
128 GPT entries; start 0 selects upstream NEW_START_BLOCK, and the data size
uses upstream end alignment, reserved size and SPA_MINDEVSIZE. Signed capacity
and subtraction bounds are checked before writing. The operation writes with
upstream libefi, verifies disk GUID, geometry and every partition's extent,
tag, flags, GUID and name through libefi readback, rejects fallback from a
corrupt primary header, then validates through AROS partition.library. Native
transfer flushes each write. Failure after a write does not imply rollback.

`device-range-run.KP1iETTj` passed with exit 0: invalid handle/name/start,
overflow-sized start, below-minimum layout, actual label/write/readback and
readonly refusal. The final labeled disk passed independent host `sgdisk -v`
and a new AROS guest's cold layout/rescan checks; disk/data/reserved GUIDs
match between boots, cold overlays remain unchanged, and the original backing
files remain unchanged. Existing bounded I/O, reader/writer reservation and
GPT expansion checks also passed. `tDULdhse` is the earlier passing version
before tightening signed arithmetic and readback fields; both runs are kept.

This operation is callable engine code, not a public writable-device grant.
It does not scan for foreign filesystems or authorize formatting. The CLI
label/relabel entry points and their authenticated
ownership integration remain unfinished; no success stubs replace them.

The matching `oz_efi_expand` operation calls upstream `efi_use_whole_disk`,
then independently rereads and validates the result. Existing disk/partition
GUIDs, names, tags, flags and the first usable LBA must survive; data cannot
shrink or move, and the reserved partition cannot shrink. Upstream VT_ENOSPC
is a successful no-op, matching libzfs relabel semantics, only after readback
confirms no extent/header change. The success output includes header-only
changes; on error it remains untouched, without promising media rollback.

`device-range-run.9cc5M7le` passed growth from a shortened partition, preserved
identity, repeat expansion as an already-full no-op, null-output rejection and
readonly expansion refusal. The full bounded-I/O/GPT suite, independent host
verification, cold guest and unchanged backing/cold-overlay checks also
passed. `womhYECw` retains the earlier passing version before header-change
reporting was tightened. These are engine-handle tests, not proof that zpool's
public relabel entry point is wired or authorized.

Native label/expand transactions now acquire a nonblocking exclusive DOS-list
guard before their first read and release it after readback/rescan, including
error paths. AROS AddDosEntry uses a shared device-list lock, so a shared guard
would not prevent publication during writes. The exclusive guard nests the
existing DOS claim inspection on the owning task; other EFI callers refuse
with EBUSY, and closing the guarded handle is refused. Acquisition never waits
on DOS while holding the EFI mutex, avoiding the opposite-order deadlock.
This serializes DOS-list publication, not arbitrary raw-device consumers or
opaque startup records; it is not a general writable-pool ownership grant.

`device-range-run.JJoNjD93` exercises cross-task publication exclusion,
wrong-owner access/end refusal, close refusal, nonblocking contention with an
existing DOS reader, and reacquisition by another task after rejected geometry.
Its earlier counterpart `zRmz1rUl` retains the first guard test. Both retain
native GPT and cold-boot checks alongside backing-file and cold-overlay hashes.

The common full-engine link now includes upstream `rdwr_efi.c`, native EFI
transport, UUID adapter and label/expand operations without section stripping.
`full-native-efi-integrated/OpenZFSServiceNative` built successfully; symbol
inspection confirms `efi_write`, `efi_use_whole_disk`, `oz_efi_label`,
`oz_efi_expand` and the guard are defined in the final executable. The rebuilt
service passed `native-import-run.Z2S8kAFc`: survivor 1 plus an empty GPT peer,
DEGRADED health, two exact DOS read/export cycles, and unchanged original and
overlay hashes. This is linked-engine regression evidence, not invocation of
the GPT helpers through public management or CLI APIs; that routing remains
unfinished.

An explicit management packet now exposes native GPT label/expand through
`oz_gpt_request` (`aros/ui/gpt-api.h`). Fresh packet credentials must satisfy
root pool policy and the service must be writable. Each request names one
whole device/unit and expected sector size/byte capacity; mismatches return
ESTALE. Labeling requires both CONFIRMED and REPLACE_TABLE flags: it replaces
the partition table, including foreign partitions, not an implicit safe-disk
probe. DOS claims and retained pool handles remain hard refusals. No mounted
device is detached, no automatic retry is attempted, and output changes only
on success. An error may follow writes. This API still requires explicitly
provisioned media with no external raw users: capacity checks are not serial
identity or global device ownership. UI/CLI callers must uphold that contract.

`native-import-run.8lTK5XzN` passed actual authenticated label/expand calls:
unconfirmed EINVAL, non-root EPERM, retained imported-member EBUSY, stale-size
ESTALE, then label after export and already-full expansion. Host `sgdisk`
validated the result and partition name; both original mirror images and the
untargeted overlay stayed unchanged. Only the explicit test mode permits
overlay 0 mutation. `FIWWUXGv` retains an earlier fixture failure at reader
login (incorrect test account name), before any GPT mutation. Separate
`native-import-run.krcRZeZ6` passes readonly-service EROFS refusal for both
label and expansion, including unchanged hashes of both backing files and
both overlays. This mode does not run mirror-import cycles. `aP0YHhHE`
retains its earlier passing probe whose generic terminal status line wrongly
listed import-cycle coverage; the final mode-specific marker corrects that.
Upstream `zpool_label_disk`/`zpool_relabel_disk` command routing remains to wire.
Final runner/client snapshot `native-import-run.xUPlMqkl` repeated the complete
writable GPT API test successfully, including independent host GPT validation
and unchanged original/untargeted image hashes; API source hashes are retained.

## Earlier native evidence (2026-09-21)

### Retained readonly native extents exclude service writers (2026-09-25)

Binding an authorized readonly extent now refuses overlapping registered
writers, using actual opened device/unit identity and byte ranges rather than
path spelling. Once admitted, the device handle remains in the reservation
registry until its final reference closes: unbinding its path does not let a
new writer race outstanding pool I/O. Whole-device and overlapping writable
opens return EBUSY; adjacent writable extents and ordinary unbound inspection
reads remain possible. This is service-local coordination, not exclusion of
external processes or an atomic DOS mount lease.

`device-range-run.wqyMsdq9` passed the direct writer-before-reader and
reader-before-writer cases, whole-device exclusion, adjacent extent access,
cross-thread writer refusal after unbind, and writable reopen after the final
read reference closes. Existing translated/cross-4GiB I/O, DOS-claim checks,
upstream GPT create/expand and cold-boot GPT identity/read-only hash checks
also passed. The rebuilt full service then passed healthy mirror import in
`native-import-run.gmHpJluJ` and single-survivor recovery with an empty-GPT
peer in `native-import-run.C0arTx19`; both ran two exact DOS-read/export cycles
with unchanged source and overlay hashes. General native CLI labeling remains
incomplete pending the wider ownership and mount-coordination contract.

Authenticated filesystem `atime` and `relatime` set/inherit now use the
upstream property parser, their own delegated permissions and live DSL mount
callbacks. `build-aros/admission-service-run.GpirNuSm` passes the writable and
cold read-only native suite: local/inherited values and sources, invalid input,
snapshot and unauthorized refusal, and read-only mutation rejection. The
native callback test uses a real owned objset plus a synthetic cached znode
to check deterministic relatime aging and explicit read-only suppression.
Separately, real DOS reads of two files are persisted: independent zdb shows
enabled atime later than mtime (1790034459 > 1790034457), while disabled atime
equals creation time (1790034454). `timestamp-offline-verify-2.log` records
second-precision checks and unchanged image hashes. This is not nanosecond or
dedicated timestamp-property GUI proof. The manager builds with updated help.

Retained failures: PLkogXma used an objset hold in the callback test and hit
the upstream nested configuration-lock assertion; the test now uses ownership,
matching a mount. osKSTaCA and z8mB7Dww found the quota test assumed exhaustion
could occur only at Write, although dirty access-time metadata can make Flush
return ERROR_DISK_FULL. The test now accepts only that specific exhaustion
error, still requiring writes and flush on the same open handle to recover
after quota is raised. No flush error is hidden by the handler. The first
offline inspection used insufficient zdb verbosity and failed without timestamp
fields; the corrected verifier is required by the native suite.

The port is incomplete, but it now has a real upstream-backed DOS handler and
authenticated management service. Native QEMU tests verify MuFS identity and
upstream delegation for dataset create, snapshot, rename, destroy, mount and
unmount. An explicitly writable authenticated DOS volume accepts ordinary AROS
file writes; a cold readonly mount reads identical bytes. Independent Linux
zdb checks and readonly backing-file hashes pass. Dynamic stable file/lock
entries are verified with 96 simultaneous handles and 96 simultaneous locks.
Native DOS notifications now cover message/signal delivery, WAIT_REPLY
coalescing, current permission rechecks, and safe replies after EndNotify.
Record-lock tests include independent timed waiters and pending-handle close.
See `zpl/CREDENTIALS.md` for exact test directories, logs and limitations.

The native Zune manager uses an owned request worker for these authenticated
operations. Private hosted tests verify unavailable authentication feedback and
actual window-close draining of a delayed DOS reply. A separate native QEMU GUI
run now proves delegated dataset and snapshot creation through the real Zune
window, with independent exported-disk ownership/integrity checks. See
`ui/NATIVE-UI-TEST.md` for screenshots, logs and reproduction. Other GUI
operations remain unverified. Hosted run `hosted-security-test.u8ceazjO` now
also verifies an actual Manager snapshot creation and subsequent snapshot
enumeration through the authenticated upstream service. Normal window closure
was followed by export/readonly reimport and DOS readback. Its RAM-file pool
does not establish cold persistence; see `engine/README.md` for the isolated
runtime, matching security build, screenshots and retained failed attempt.
The subsequent persistent hosted GUI run `hosted-security-test.jYIpzmlR`
also passes independent zdb snapshot/integrity inspection and a separate
readonly hosted boot retaining the snapshot and DOS payload, with unchanged
member-file hashes. This remains software file-vdev persistence, not physical
power-loss qualification.

Important remaining work includes generic device discovery/import and release
packaging, broader authenticated administration, notification and record-lock
stress coverage,
retired endpoint reclamation (the registry now grows rather than imposing
eight service mount lifetimes; memory and dispatch cost grow with history), allocation-failure
and memory-pressure coverage, and broader recovery/feature tests. Hosted large
positioned-I/O growth now passes with the qualified DOS64-only runtime update
(see engine/README.md); arbitrary runtime-library mixing remains unqualified. No complete-port,
production-readiness or physical hardware durability claim is made.

Handler completion also requires real Wanderer verification: mounted dataset
volumes must appear on the desktop, open drawers, enumerate files with correct
metadata, and refresh after changes. Shell enumeration and management-UI
responses alone do not establish this user-facing requirement.

Native QEMU evidence `build-aros/native-gui-run.mYrMrnnO` now shows a real
authenticated `OZBROWSE` volume opening in Wanderer (02-auth-open.png), its
Documents drawer enumerating both icon-backed and iconless files
(03-documents.png), and automatic rename/delete/create refresh without closing
the drawer (05-updated-complete.png). The fixture starts Wanderer under UID 77,
GID 88; starting it outside that login failed access checks, correctly, rather
than granting desktop processes privileged filesystem access. The handler now
emits native input-device disk-inserted/removed events outside the DOSList lock.
This is partial desktop evidence: final unmount returned EBUSY after closing
drawers (07-after-unmount.png and serial.log), and detailed metadata UI remains
unverified in that run. Do not treat that earlier fixture as a pass.

Follow-up `build-aros/native-gui-run.E1a6Ltkw` repeats drawer browsing and live
updates (02-before-update.png, 03-updated.png), then successfully unmounts and
removes OZBROWSE from the desktop (04-unmounted.png; serial result PASS).
Wanderer retains a notification subscription for a desktop volume even after
all its drawers close. Storage detach now retains those subscriptions and
their reply messages on the retired endpoint, accepts late EndNotify, and
never resolves a watch against detached storage. Actual endpoint destruction
still returns EBUSY until every subscription/reply is gone; file handles and
locks still block storage detach. No forced unmount or ACL bypass was added.

The full writable/cold-read-only native suite passed in
`build-aros/admission-service-run.UckI2Fby`, including a new mandatory check
holding a notification message across detach, EndNotify, request deallocation
and late reply. Independent zdb integrity/compression/recordsize checks and
read-only image hashes also passed. Detailed metadata UI, subscription
reattachment across a future remount and general retired-endpoint reclamation
remain separate unfinished work; this is not full-port completion.

Detail-view follow-up `build-aros/native-gui-run.ozHsKkGk` shows file sizes
(Before/After 32 bytes, Iconless 38 bytes, New 35 bytes), native protection
columns, timestamps and the visible prefix of the stored comment, alongside
automatic updates (02-detail-view.png, 03-updated-details.png). Column widths
truncate the comment/date; this does not prove their complete UI formatting.
Unmount and private-fixture export both returned zero. After QEMU exited,
`bash aros/ui/verify-wanderer-ui.sh <run>` passed independent zdb integrity,
exact extraction/comparison of ReadMe.txt, Documents/After.txt and
Documents/New.txt, owner/group presence and unchanged image hashes.
`wanderer-offline-verify-2.log` records this result. Its earlier invocation
correctly refused to read while QEMU was still exiting.

Retain `native-gui-run.DS7o6uqV`: Detail-view refresh overlapped deletion of
Iconless.txt and the handler returned ERROR_OBJECT_IN_USE. The fixture now
retries only that deletion/error for at most 100 attempts with Delay(5), never
forces deletion, and fails if contention persists. The successful follow-up
did not require a retry, so the original overlap is not claimed deterministically
reproduced or fixed in the handler. No shared Wanderer sources were changed.

Run the isolated fixture with `AROS_UI_SCENARIO=wanderer` and the same private
DOS/ATA overlays as other native GUI tests. Build `aros/ui/wanderer-fixture.c`
with `api.c`, `admission-client.c`, native SDK flags and `-lintuition -licon`
as `build-aros/OpenZFSWandererFixture`. Close its separate control window only
after recording each phase. It modifies only the copied QEMU mirror pool.

## Historical native evidence (2026-09-18)

The complete upstream userspace engine links and runs in isolated hosted AROS.
Mirror and RAIDZ1/2/3 file pools pass native create, DMU write, export, engine
shutdown, read-only re-import and exact data comparison. Independent Linux
zdb verifies all data/metadata checksums with no leaks. See
`engine/README.md` for commands, failures, commits and evidence locations.
This is not yet the mounted DOS handler or the complete administration backend;
the historical 4 GiB positioned-I/O runtime failure is now qualified with an
explicit DOS64-only update (see engine/README.md), not arbitrary library mixes.

The diagnostic native management service now initializes the actual engine
read-only and binds `pool.list` to the upstream SPA namespace. Explicit read-only
import of exported upstream XDR configurations from owned test fixtures is now
verified across a hosted reboot, through IPC and the Zune UI. No generic device
discovery or writable imports yet. Dataset enumeration now returns actual DMU
names, types and space information; native IPC and GUI tests both pass for the
root and payload datasets. Other administration operations
remain unsupported except in the explicit fixture-write test service: real
export and snapshot creation now pass, including snapshot payload comparison
after a hosted reboot and independent upstream Linux checksum verification.
General writable administration and the mounted handler
remain explicitly unsupported. The small standalone UI service build still
reports `NO_ENGINE`; use `engine/build-management.sh` for the integrated build.

## Evidence gates

1. Linux userspace control: build upstream libzpool/ztest/zdb and run disposable
   file-vdev mirror and RAIDZ1/2/3 tests. This is NOT an AROS pass.
2. AROS engine: adapt the userspace platform interfaces; run equivalent tests
   as AROS code with AROS allocation and threading. Hosted AROS is not Linux.
3. DOS frontend: adapt locks, paths, metadata, notifications, and lifecycle;
   remove private dnode and transaction assumptions from reused packet code.
4. Interoperability: import Linux-created pools on AROS; write, export, and
   verify on Linux with zdb/scrub and file hashes, then reverse the direction.
5. Recovery: degraded import, replacement/resilver, injected checksum damage,
   snapshots, clones, send/receive, crash recovery and encryption tests.
6. Native device backend: independent requests, 64-bit partition-relative I/O,
   error propagation and real flush ordering, followed by SMP stress tests.

The userspace libzpool build is an engine foothold, not the whole filesystem:
administration, the mounted filesystem interface, memory-pressure handling,
credentials, shutdown and device durability still require AROS integration.
Retain upstream algorithms and feature checks rather than substituting stubs.

## Test isolation

No DUT access, real devices, kernel modules, system installation, or host pool
imports in the control lane. Each run owns a fresh directory of regular disk
files, keeps its log and records the source revision and exact invocation.
File-vdev RAID tests cover engine behavior, not controller failure or physical
power-loss durability. Test failures must be retained, not silently retried.

Current build logs are under `build-control/`. Missing host dependencies are
extracted there, never installed globally. Use an isolated hosted runtime.

## Initial observations (2026-09-18)

- Linux control configured with `--with-config=user --disable-pyzfs
  --disable-systemd --disable-pam`. libblkid/libtirpc development and runtime
  packages were downloaded and extracted under `build-control/deps`.
- Use `make gitrev` before targeted `make -j4 ztest zdb`; targeted builds alone
  omit the generated revision header. Original failed build logs are retained.
- Local dependency include flags: `-Ideps/usr/include -Ideps/usr/include/tirpc`
  (absolute paths when configuring); linker path: `deps/usr/lib/x86_64-linux-gnu`.
  Extract matching runtime packages too: the development-only linker symlinks
  otherwise fall back to non-PIC static archives and fail shared-library links.
- `bash aros/file-vdev-smoke.sh` runs the Linux control. These are short randomized
  engine smoke runs, NOT deterministic degraded-import/resilver acceptance tests.
- The older `aros-crosstools` SDK lacks pread/pwrite declarations. The newer
  `aros-adt-crosstools` SDK has them; do not patch libc based on the older probe.
- Both SDK probes lack `pthread_attr_setguardsize`. The newer SDK compiles and
  links `platform-probe.c` with `-DAROS_PROBE_WITHOUT_GUARD -lpthread`.
  This macro only narrows the diagnostic; it is NOT a port fix or runtime pass.
  Before engine execution, decide how task stacks/guard requirements map to AROS.
- Upstream uses pread64/pwrite64, additional OS headers, atomics, umem and other
  interfaces beyond this small probe. Successful probe linking is not evidence
  that libzpool itself builds for AROS yet.
