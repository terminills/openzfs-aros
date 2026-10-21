# Native manager pool creation and Wanderer (2026-09-22)

## Guided dRAID creation (2026-09-27)

`native-gui-run.CVD26KlO` passes actual Manager creation of
`aros_gui_created`, draid1:1d:3c:1s, ashift 12, on three newly prepared
128 MiB `RAM:OZGuiCreate/disk0..2` files. The fixture was compiled with
`OZ_GUI_DRAID`; only the Manager issues pool.create. The separate fixture
then requires six healthy status rows: pool, one draid, three file leaves,
and one distributed spare, all with zero read/write/checksum errors.
It creates/mounts the filesystem, writes an actual project and icon, and
requires exact DOS readback after browsing, unmount and both pool exports.

Inputs: `typed-draid-gui-harness/OpenZFSAdmissionServiceHarness`,
`OpenZFSPoolCreateGui-draid`, `OpenZFSManagerNative-typed-draid-v2`.
The runner freezes these inputs and source. QEMU exited zero after the
fixture PASS; `verify-create-ui.sh` passed on the stopped guest.
Screenshots 03/04 show exact geometry/paths and the corrected main draid
selector, 05 explicit confirmation, 06 authenticated errno=0, 08 desktop
volume insertion, 09 the real file icon in Wanderer, and 10 volume removal.
This is RAM-backed GUI proof, not a cold-persistence or hardware claim.
The frozen service's greeting contains a stale literal api.version=2 despite
API3 messages; a separate diagnostic fix removes the hard-coded version.

## Native-partition GUI lane (2026-09-27)

`AROS_UI_SCENARIO=create-partition` prepares a fresh 256 MiB GPT disk at ATA
unit 3, with a blank BF01 partition starting at sector 2048. Only its private
qcow2 overlay is writable. Build `pool-create-gui.c` with
`-DOZ_GUI_NATIVE_PARTITION` and supply it as `AROS_TEST_CREATE_BINARY`, together
with the current full service, Manager and explicit partition.library overlay.
The separate startup starts the actual service in TEST-WRITE mode; no pool is
pre-created. The Manager must create `aros_gui_created` as one data/stripe leaf,
`/AROS/ata.device/3/partition/0`, ashift 12. The fixture checks the disk topology,
mounts the created dataset, writes its project/icon and checks exact bytes after
Wanderer browsing. Its result is distinct from the older RAM-mirror marker.

`native-gui-run.cJZdDvEJ` reached the authenticated service and Manager READY
state. Its private storage-test QEMU had no `screendump` command. It was stopped
through its own QMP socket without submitting pool creation. This is retained
as an incomplete run, not GUI acceptance. A separate `build-gui` configuration
of the same worker-fix QEMU source enables pixman; the storage-test binary is
unchanged. The screenshot helper preserves PPM framebuffer captures and converts
them to PNG, avoiding a dependency on QEMU's optional PNG encoder.

Native GUI acceptance still requires reviewed screenshots, fixture completion,
partition-exterior comparisons and offline integrity checks. Cold persistence
of this particular GUI-created pool is not yet established. The older
`verify-create-ui.sh` intentionally accepts only the RAM-backed `create` scenario.

Follow-up `native-gui-run.bXl0nzzT` passes the interactive native-partition
workflow with the current full service (`full-typed-native-replace`) and Manager
(`OpenZFSManagerNative-typed-vdev`). Reviewed captures show exact pool name,
data/stripe partition path and ashift 12 (15-layout.png), explicit confirmation
(16-confirm.png), authenticated creation errno=0 (17-result.png), the inserted
OZCREATED desktop volume (19-desktop.png), its project icon in the open drawer
(20-files.png), and removal after unmount (21-final.png). The icon label is
abbreviated by Wanderer; complete filename display is not claimed.

Serial evidence requires both pool/disk rows ONLINE with zero error counters
before and after browsing, exact project-file bytes, successful dataset mount,
unmount and pool export, and the dedicated native creation PASS marker. QEMU
quit through its private QMP socket; runner exit status was zero.
`verify-create-partition-ui.sh` then passed original/unused-disk hashes, exact
GPT exterior comparisons, GPT validation and independent upstream `zdb -bcc`
(exit zero and No leaks), retained in `offline.sAzuRAyF`. This establishes the
GUI-to-native-partition-to-Wanderer path, not a cold guest import of this pool.

Early clicks in this run missed narrow widgets because tablet coordinates did
not match screenshot coordinates; screenshots retain those failed input steps.
The exact topology was reviewed before confirmation. No backend result was
inferred from an unconsumed click. The separate private graphics QEMU build
uses pixman and retains the storage worker fix; no user runtime was changed.

### Cold import of the exact GUI-created pool

`native-gui-run.pnPEKHQY` boots a new guest with a fresh qcow2 child of the
verified writer result `bXl0nzzT/offline.sAzuRAyF/result.raw`. The fixture is
built with `OZ_GUI_NATIVE_PARTITION` and `OZ_GUI_NATIVE_COLD`; it does not create
a pool, dataset, file or icon. Authenticated import admits only the explicit
ATA3 partition and requests readonly=on. Native DOS reads compare the original
project bytes, icon.library loads the saved icon, attempted file creation must
fail with ERROR_DISK_WRITE_PROTECTED, then unmount/export succeed.

The dedicated cold PASS marker is present, QEMU exits zero, and the offline
verifier passes unchanged writer-result, reader-raw, reader-qcow2 and unused
fixture disk hashes. GPT exterior/structure and independent upstream `zdb -bcc`
also pass (exit zero, No leaks), in `offline.2jl2WG8H`. This closes the cold
guest import gap above for this exact GUI-created pool. It is not another
interactive GUI import test, physical power-loss proof or general topology
coverage. The cold service is write-capable, so successful write refusal
specifically exercises the imported pool/mount read-only policy.

Reproduce with `AROS_UI_SCENARIO=create-partition-cold`, the cold fixture as
`AROS_TEST_CREATE_BINARY`, and `AROS_UI_CREATE_RESULT` pointing at an offline
verified writer result. After the dedicated serial marker, quit that guest
through its own QMP socket, wait for runner exit, and invoke
`verify-create-partition-ui.sh` on the cold evidence directory. The runner
never imports label-discovered paths that were not explicitly admitted.

The real manager creates an upstream OpenZFS mirror through authenticated
admission. The fixture prepares two exclusively owned, blank 64 MiB RAM files;
it does not create the pool. After the manager closes, it verifies the actual
pool/mirror/two-file topology and zero error counters, creates and mounts a
dataset, and writes a project file and icon through the native DOS handler.
Wanderer is launched under the explicit test-root session, not an anonymous
startup identity. The test-only security marker is mandatory.

## Reproduce

Build `OpenZFSPoolCreateGui` with the native AROS compiler and SDK:
`aros/ui/api.c aros/ui/admission-client.c aros/ui/pool-create-gui.c`, linking
`-lintuition -licon`. Build the existing native manager and service normally.
Run `aros/ui/native-ui-qemu.sh` with `AROS_UI_SCENARIO=create` and the same
private DOS/security/ATA overlays and verified mirror baseline as other GUI
tests. The runner archives source, input hashes, serial output and UI actions.

In Pool builder enter `aros_gui_created`, ashift 12, one data/mirror group,
and add `RAM:OZGuiCreate/disk0` and `RAM:OZGuiCreate/disk1` separately. Use draft,
inspect the paths, disable Dry-run, send and confirm. Require errno=0. Save
activity and response, then close the manager. Open the automatically appearing
OZCREATED volume in Wanderer; inspect its project icon. Close the drawer before
closing the fixture's browse-control window. Require exact file readback,
post-browse healthy topology, unmount and both exports. Quit only that guest,
then run `bash aros/ui/verify-create-ui.sh <private-run-directory>`.

QMP keystroke delivery is asynchronous: wait for the guest to consume typed
paths before clicking Add device. In Zune strings Ctrl-A moves to the start;
Ctrl-A then Ctrl-K clears the field. Do not assume Ctrl-A selects everything.

## Evidence

- `build-aros/native-gui-run.nSVfz3QO`: first creation/mount/browse/export PASS.
  Initial scripted typing outran guest processing, producing incomplete draft
  paths. These were corrected and visually checked before submission; screenshot
  07-exact-layout.png records the submitted paths. Opening the drawer requested
  missing THEME assignment; cancelled it, and the real icon appeared. No engine
  failure. The startup script now matches the existing Wanderer theme assigns.
- `build-aros/native-gui-run.djItqMT7`: corrected repeat, PASS. Screenshot
  03-layout.png shows both exact files, data/mirror, ashift 12; 04-confirm.png
  shows confirmation; 05-created.png shows authenticated errno=0; 07-volume.png
  shows automatic volume insertion; 08-files.png shows the populated drawer
  without a theme requester. The displayed icon label is abbreviated by
  Wanderer, not evidence of a complete filename display.
- The repeat serial log proves exact CreatedByManager.txt bytes, all four
  status rows healthy with zero read/write/checksum errors both before and after
  browsing, successful unmount and two exports. Guest session exited zero;
  verify-create-ui.sh passed after the recorded QEMU PID had exited.

These are RAM-backed pools, not newly-created-pool cold-persistence evidence.
The copied qemu_mirror disks are a separate bootstrap fixture, not the new
pool's vdevs. No claim of offline inspection of the new pool is made. Existing
two-boot native block-backed tests remain separate evidence. This does not
complete generic discovery/import, native-device provisioning, all vdev roles,
or the full OpenZFS port. No DUT, shared hosted instance or VPN storage touched.

## Production endpoint, native partition (2026-09-30)

Build the fixture with `OZ_GUI_NATIVE_PARTITION` and
`OZ_GUI_PUBLIC_SERVICE`, and select `AROS_UI_PUBLIC_CREATE=1` with scenario
`create-partition`. The runner requires a service build with diagnostic modes
disabled and the full dispatcher enabled. The private fixture authenticates
its test root before launching that production service; Manager uses the
normal public endpoint, not TEST-NATIVE. This does not change normal startup.

`native-gui-run.5fuADiMI` exercised Scan devices, Inspect, Create draft,
explicit stripe group/device selection, ashift 12 and confirmed creation of
`aros_gui_created` on `/AROS/ata.device/3/partition/0`. Screenshot
`08-create-completion.png` records authenticated errno=0. After Manager closed,
the fixture verified healthy topology, created and mounted its dataset,
wrote a file and icon, and Wanderer displayed them (`14-wanderer-loaded.png`).
Serial records exact DOS readback, successful unmount/export and fixture PASS.

**Evidence failure retained:** Manager's Save activity log and Save response
buttons were not pressed before closing. Both saved logs are missing, so
`verify-create-partition-ui.sh` exits 1 at its required creation-response
marker. This run is not a strict-verifier pass. The first immediate verifier
attempt also correctly refused while the quitting QEMU PID was still alive.
Do not replace these failures with the later independent result.

Manual offline inspection in `offline.manual-RyrKYoU9` verified unchanged
original backing hashes, unchanged GPT exterior, valid GPT, and upstream
`zdb -bcc` reported No leaks. Its extracted result/member hashes are retained.
Only the disposable 256 MiB fixture was written. Cold verification is separate;
no physical-device durability or complete-port claim follows from this run.

The production-endpoint cold fixture adds `OZ_GUI_NATIVE_COLD`, uses scenario
`create-partition-cold` and the hash-verified extracted result as
`AROS_UI_CREATE_RESULT`. Run `native-gui-run.6LCP5nBL` passed fresh read-only
import, exact file bytes, icon loading, denied write, unmount and export.
This closes cold readback for this pool but does not retroactively satisfy the
writer run's missing saved-response marker.

Strict writer repeat `native-gui-run.AdDb4TuI` saves both Manager response and
activity before closing. Actual creation errno=0, mounted Wanderer file icon,
exact DOS readback and export are captured. The unchanged
`verify-create-partition-ui.sh` exits zero, verifying backing hashes, GPT
exterior and independent zdb No leaks (`offline.6k6ou2ac`). This is a new
passing run, not a correction of the earlier failed log. Cold import above
uses the earlier writer's pool and remains separate evidence.

## Operation-specific recovery controls

The pool editor now hides its recovery group outside Import. Previously the
controls were disabled but still visible in Create/Add. Reset and disable
semantics are unchanged. Native build `ui-operation-controls-20260930` has an
empty build log. Service-free isolated GUI run `native-gui-run.dAwQVqyy`
visually verifies Create and Add without recovery controls, Import with both
controls, a preview containing `destroyed=on;rewind=on`, and unchecked controls
when Import is reopened. Only drafts/previews were used; this test does not
claim recovery execution or pool changes. Screenshots 02 through 06 retain
the mode transitions; normal Manager closure is also captured.
