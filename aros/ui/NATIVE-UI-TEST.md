# Native authenticated GUI fixture

## Loader prerequisite GUI verification (2026-09-30)

Actual Runtime check button verification passes in private hosted
`hosted-security-test.wMdOTnBV`: patched DOS gives exit=0, both loader APIs
pass on/off/on, and response/activity panes show all diagnostics. Screenshot
`03-runtime-result.png` captures the named AROS window; saved response and
activity retain the full output. Manager closes normally and fixture exits 0.

Control `hosted-security-test.nU7PPmJ9` omits the DOS patch. The same button
shows exit=20, both execute-off bypasses, and the explicit runtime-failure
status without calling it a pool-health failure (`02-runtime-failure.png`).
Connect works afterward (`03-reconnected.png`), and normal close lets the
fixture finish. Its expected checker failure is not a passing OS prerequisite.
Both evidence sets are registered to the development test archive. No user hosted session or DUT
was used. Updated checker native compilation succeeds separately in
`runtime-execute-native.qJbz9tJP`; that compile is not runtime verification.

## Missing runtime checker and recovery (2026-09-30)

`hosted-security-test.65xW6FPn` intentionally omitted the checker and
exposed misleading generic collection/cleanup wording after DOS error 205.
`hosted-security-test.YK09NnOC` verifies the corrected missing-executable
message through the real Runtime check button. It explicitly says the
checker did not run and identifies the matching bundle as its source.
The special case requires the runtime action, status -1 and DOS
ERROR_OBJECT_NOT_FOUND; other command failures retain conservative wording.
Both runs exited normally. The latter saved the missing-checker response
and then successfully reconnected to the service, recorded in
`02-connect-after-failure.png` and the activity log. Hosted and native
Manager builds pass. This checks absent-component handling, not storage.

## Multiline activity display (2026-09-30)

`hosted-security-test.NKC4Xohl` verifies the Runtime check action after
splitting multiline activity messages into bounded single MUI list rows.
`01-activity-result.png` visibly shows diagnostics in both response and
activity panes, unlike the blank activity pane in the earlier runtime run.
The saved transcript retains the complete original multiline text. The
Manager was closed normally and the private fixture completed with exit 0.
Hosted and native builds pass (`hosted-activity-build.log` and
`native-activity-build.log`). This is a Manager fix, not a Zune modification
or a storage-health qualification. The private hosted stdcio overlay remains
required. Evidence is registered to the development test archive.

## Hosted runtime stdio repair verification (2026-09-30)

Private hosted run `build-aros/hosted-security-test.GvVeimKK` passes the
actual Manager Runtime check action with `exit=0`, transport/child status 0,
and exact stdout=1/stderr=1. Saved response and activity plus
`01-runtime-result.png` retain the result. Closing Manager normally allows
the hosted authentication/storage fixture to finish with exit 0.

The comparison baseline `hosted-security-test.ZSB2zVsl` failed stdout
ownership with the same Manager/checker. The passing run supplies only the
ABI-matched hosted overlay `cli-stdio.TKCE3ewE/stdcio.library` via
`AROS_TEST_HOSTED_STDCIO_LIBRARY`. The overlay builder accepts `AROS_SDK`,
`AROS_STDIO_GENERATED`, `AROS_STDIO_BASELINE`, and `AROS_CC`; all four must
refer to the same build family. This run used aros-adt-hosted-clean's SDK
and stdcio link map, aros-adt-src headers and aros-adt-crosstools compiler.
It substituted the current __stdio.c object only, retaining other map inputs.
Native defaults remain unchanged. Inputs and output hashes are retained.

This is a private runtime prerequisite repair, not proof that an unmodified
nightly is qualified or that pool durability is complete. No DUT, user
hosted session or remote storage was accessed.

## Runtime prerequisite action (2026-09-30)

Manager's Runtime check button starts the real `C:OpenZFSRuntimeCheck`
through the existing joined background worker and exact argument-vector
launcher, without a shell or a new service wire operation. It refuses to
overlap outstanding requests, stops automatic I/O polling, displays captured
stdout/stderr and exit status, and participates in normal close/drain logic.
The report can be saved with Save response and Save activity log. Status
wording distinguishes prerequisite failure from pool health; neither a PASS
nor failure claims storage durability. The button needs the checker installed
in C:, as provided by the native bundle.

Native and hosted Manager builds pass. In the separate hosted fixture
`hosted-security-test.ZSB2zVsl`, a real click invoked the checker, captured
exit 20 and the hosted runtime's redirected-stdout ownership failure, and
displayed `Runtime check failed; see diagnostics (not a pool-health result)`.
`01-runtime-result.png` captures the named AROS window; saved response/activity
files retain the result. Normal Manager close and the fixture's subsequent
storage/authentication checks completed with exit zero. This verifies the
GUI failure path, not a successful hosted runtime prerequisite qualification.
The earlier `hosted-security-test.zPzHGcL0` preserves the misleading generic
engine-error wording that prompted the status correction. The first checker
build also failed because the hosted SDK lacks libsecurity.a; the checker
uses an explicitly opened local base, so the hosted link needs no such import
archive. Build logs are retained as `hosted-runtime-check-build*.log`.

This is a private, offline QEMU test, not a distributable image or a DUT
procedure. The ISO contains marker-guarded empty-password MuFS test accounts.
Never deploy it to another runtime. It uses copied disposable mirror files,
not host devices or an imported host pool.

Build `admission-gui-launcher.c` with `api.c` using the native AROS SDK. Build
the normal manager with `api.c`, `admission-client.c`, `admission-worker.c`, the
native pthread core, and pthread/muimaster/intuition/utility imports. Install
only into build-aros as OpenZFSAdmissionGuiLauncher and OpenZFSManagerNative.
The existing native OpenZFSAdmissionServiceHarness imports the copied mirror
and grants UID77 dataset permissions under `qemu_mirror/delegated-root`.

Run `native-ui-qemu.sh` with the verified mirror baseline, setting
AROS_TEST_DOS_OVERLAY and AROS_TEST_ATA_OVERLAY to the private tested overlays.
It prints a unique evidence directory and waits for its one guest. It does
not operate any existing QEMU/hosted session. Its cleanup targets only the PID
it launched. Startup launches Wanderer, the real service, and a launcher that
logs in as UID77/GID88 before running the real manager. The launcher does not
create the GUI test dataset or snapshot.

Use `qmp-ui.py RUN capture NAME.png` to inspect the actual framebuffer before
operating the UI. `click X Y WIDTH HEIGHT` sends normalized tablet coordinates;
this native image's tablet calibration has approximately 5% inset, so observed
screen positions and requested coordinates differ. Do not change host/DUT
input settings to compensate. `type TEXT` deliberately holds keys for 80 ms
with 180 ms spacing; the earlier 30/50 ms timing lost input. Inspect the full
confirmation dialog and cancel/retype any mismatch before submitting.

In the manager, select Datasets/create, enter
`qemu_mirror/delegated-root/gui-created`, turn Dry-run off, review, verify the
exact target, and submit. Capture the result. Then select Snapshots/snapshot,
enter `qemu_mirror/delegated-root/gui-created@ui`, review and submit. Capture the
result. Return to Datasets and set `org.aros:gui=parent` on
`qemu_mirror/delegated-root`, then set `org.aros:gui=child` on
`qemu_mirror/delegated-root/gui-created`. Finally inherit `org.aros:gui` on
that child. Review the full target and argument before each submission.
Capture each result, click Save response and Save activity log, then close
the manager. Do not capture immediately after input and assume redraw finished:
separate the action and inspection, and inspect the actual confirmation text.
The launcher dumps those logs to serial, queries the created dataset, checks
the child's inherited property value `parent` and source
`qemu_mirror/delegated-root` through the diagnostic read endpoint, and exports
the pool, restores its previous identity, and reports its result. This fixture
export uses the diagnostic endpoint; it is not production export admission.

Only after observing `RESULT authenticated native GUI PASS`, use
`qmp-ui.py RUN quit` and wait for the owning shell/QEMU session to exit. Run
`verify-native-ui.sh RUN`. It requires all five authenticated GUI activity entries,
five successful ticket/release results, a saved inherit result, exact inherited
value/source and orderly
export. Independent userspace zdb must find the dataset and snapshot with
UID77/GID88 and no leaked blocks, without changing either disk hash. It refuses
to inspect disks while the recorded guest PID is alive. Screenshots still need
human/agent visual inspection; log checks do not prove correct rendering.

## Evidence (2026-09-21)

- `build-aros/native-gui-run.xe0NzGOW`: real GUI dataset creation, normal close,
  exported dataset owned by UID77/GID88, zdb No leaks. Screenshot
  `11-create-result.png` revealed a false "engine unavailable" status despite
  a successful authenticated result. Missing hello metadata is not failure.
  This run also retains cancelled incomplete keyboard input and calibration
  screenshots. No snapshot was requested in this first run.
- `build-aros/native-gui-run.LDEskH0I`: fresh copied disks and reboot with the
  status fix. `02-confirm-create.png` and `03-create-success.png` show real
  dataset creation; `04-confirm-snapshot.png` and `05-snapshot-success.png`
  show snapshot creation, each with the correct OK status. `06-closed.png`
  records normal window closure. Serial, saved activity/response dumps,
  `ui-actions.jsonl`, source archive and input hashes retain provenance.

This does not verify every management operation, general device import,
successful authenticated hosted GUI operation, or production packaging.

## Property workflow extension (2026-09-21)

`build-aros/native-gui-run.whnFUvqM` passes the expanded five-operation workflow
above, including real GUI property set/inherit as UID77/GID88 and the exact
inherited result. `14-inherit-confirm.png` and `15-inherit-result.png` show the
confirmed request and successful response. Saved activity records all five
successful ticket/release operations. The launcher verified inherited value and
source before orderly export; independent zdb then found both created objects,
UID77/GID88 and No leaks, with unchanged disk hashes. See
`build-aros/native-property-ui-verify.log` and `native-property-launcher-build.log`.
The owning QEMU session exited normally after explicit quit.

Screenshots 11/12 retain a cancelled incorrect edit and a stale immediate
capture. Clicking inside the argument put the cursor within the value, so six
backspaces removed the wrong characters. Nothing from that review was submitted.
Clear fields and full retyping corrected the input; the subsequent separate
capture confirmed the exact argument before submit. This was test interaction,
not a backend mutation failure. Earlier two-operation evidence above used the
older verifier; the current verifier deliberately requires five operations.

## Clone promotion workflow (2026-09-21)

Set `AROS_UI_SCENARIO=promotion` when starting native-ui-qemu.sh. This selects
the private Startup-Native-Promote and invokes the same launcher with PROMOTE.
The default five-operation property workflow remains unchanged. The scenario
is retained as scenario.txt so the verifier cannot mistake one for the other.

Use the real GUI to create `qemu_mirror/delegated-root/gui-created`, snapshot
it as `@ui`, clone that snapshot to `qemu_mirror/delegated-root/gui-promoted`,
then promote the clone (target is a filesystem, not an @snapshot). Disable
dry-run and inspect each complete confirmation before submitting. Clear the
fields before promotion so the clone destination argument is not left over.
Save response and activity after promotion; close the manager normally.

The launcher checks the moved snapshot and the source filesystem's new origin
before export. The verifier requires four successful tickets and the saved
promotion result, checks the snapshot at its new name with upstream zdb,
UID77/GID88, no leaks and unchanged disk hashes after guest exit.

Verified run: `build-aros/native-gui-run.lc372mYE`; visible confirmation
`10-promote-confirm.png`, success `11-promote-result.png`, orderly export
`12-exported.png`. Full verification log: `build-aros/auth-promote-gui-verify.log`.
These screenshots are native offline QEMU evidence, not a remote VPN pool or
the user's DUT/hosted desktop.

## Bookmark workflow (2026-09-21)

Set `AROS_UI_SCENARIO=bookmark`
to select the private bookmark startup and launcher mode. Create gui-created
and gui-created@ui as above, then choose Snapshots/bookmark with that snapshot
as Target and qemu_mirror/delegated-root/gui-created#ui as Argument. Review
and submit with dry-run disabled. Capture the success; then use Datasets/list
with Target qemu_mirror/delegated-root/gui-created and Options bookmarks=on,
Argument empty. Capture and save that response and the activity log, then
close/export/quit and run verify-native-ui.sh.

The launcher checks the actual upstream bookmark identity and numeric GUID,
TXG and creation fields. The verifier requires three successful authenticated
tickets, the saved enumeration, identity result, export, independent filesystem
and snapshot ownership, no leaks and unchanged hashes. Run
`build-aros/native-gui-run.4fwkRdlv` passes; see
`build-aros/auth-bookmark-gui-verify.log`. Screenshot 09-bookmark-list.png
retains the discovered generic zero-capacity/progress display defect for
metadata-only rows; those zeros are not bookmark capacity measurements.

### Metadata display correction and verifier negative control

`build-aros/native-gui-run.8SHifTrs` repeats the entire bookmark GUI workflow
with the corrected manager. Screenshot `09-bookmark-list.png` shows the
bookmark identity and parent without invented zero capacity/progress values.
Properties, holds and conflict rows use the same metadata-only presentation.
The bookmark scenario was visually tested; the other metadata kinds were not
separately clicked in this run. Input hashes match the rebuilt binaries.

The initial negative-control verification of 4fwkRdlv incorrectly passed:
a standalone `! rg` does not trigger bash `set -e` when its negation fails.
Retain `build-aros/metadata-row-negative-verifier.log` as the failed test of
the verifier. Explicit if/exit gates now reject GUI failure/assertion markers
and invented bookmark counters. The admission QEMU harness's analogous
unexpected-shutdown guard was corrected too; both existing qE722MZ8 logs
were checked and contain no such marker (nor failure/assertion markers).

With explicit gates, the original 4fwkRdlv run exits 1 at the intended counter
check (`metadata-row-negative-verifier-2.log`); the corrected 8SHifTrs run
exits 0 (`metadata-row-gui-verify-2.log`), including orderly export, upstream
zdb No leaks, UID77/GID88 and unchanged disk hashes. This supersedes the
display result only; earlier bookmark engine tests remain retained evidence.

## Space-property workflow (2026-09-21)

Use `AROS_UI_SCENARIO=space`. Create gui-created and gui-created@ui as in
the earlier workflows. Select Datasets/set, target
qemu_mirror/delegated-root/gui-created, argument quota=16777216, disable
dry-run, review and submit. Then select Datasets/get with the same target
and argument quota. Save the response and activity, close the manager,
wait for launcher verification/export, quit QEMU and run verify-native-ui.sh.

Run `build-aros/native-gui-run.EhU7c5es` passes this workflow: three successful
authenticated tickets, actual quota value/source, UID77/GID88 filesystem and
snapshot ownership, orderly export, zdb No leaks and unchanged verification
hashes. See `build-aros/space-property-gui-verify.log` and screenshots
06-quota-confirm-final.png, 07-quota-result.png, 08-quota-readback.png.
The earlier 04-snapshot-confirm.png contains dropped simulated keystrokes;
it was cancelled, corrected and reviewed before submission. The guest's
manager predates the confirmation wording cleanup in ba6959e; that rebuilt
binary still needs a new-guest visual check. This is isolated native QEMU,
not the DUT, shared hosted session or a remote VPN pool.

The follow-up run `build-aros/native-gui-run.Ia1wbz0I` uses quota=16M rather
than raw bytes and reads back the same 16777216 value. Its complete verifier
passes (`build-aros/size-parser-gui-verify.log`). Screenshot 06-size-confirm.png
also verifies the ba6959e wording change in a freshly booted guest; the
confirmation is narrower, so review its actual button coordinates before
reusing prior click positions. Screenshot 08-size-readback.png records the
actual property result. All guest processes exited after orderly export.

## Compression set/inherit workflow (2026-09-21)

Use `AROS_UI_SCENARIO=compression`. Create gui-created and gui-created@ui.
In Datasets/set, set compression=lz4 on qemu_mirror/delegated-root, then
compression=zstd-3 on qemu_mirror/delegated-root/gui-created. Read the child's
compression with Datasets/get: it must show zstd-3 and the child as source.
Choose Datasets/inherit, same child target, argument compression. Confirm,
then read the property again: lz4, source qemu_mirror/delegated-root. Save the
response and activity, close/export, quit QEMU and run verify-native-ui.sh.

Run `build-aros/native-gui-run.Rs9wCeQM` passes. Screenshot
10-child-zstd-readback.png records the actual local value/source;
11-inherit-confirm.png records the reviewed inherit operation;
13-inherited-lz4-readback.png records the actual inherited value/source.
The launcher independently queries both parent and child after manager exit.
The verifier requires exactly five authenticated successes, two sets, one
inherit, the saved read response, successful export, UID77/GID88 filesystem
and snapshot ownership, upstream zdb No leaks, and unchanged verification
hashes. Log: `build-aros/compression-gui-verify.log`.
This closes the compression-specific GUI verification gap; compressed data
blocks and cold native DOS reads remain covered by the separate engine tests.
The private QEMU process exited normally. No DUT/shared hosted/VPN access.

## Recordsize set/inherit workflow (2026-09-21)

Use `AROS_UI_SCENARIO=recordsize`. Create gui-created and gui-created@ui.
Set recordsize=64K on qemu_mirror/delegated-root, then recordsize=1M on
qemu_mirror/delegated-root/gui-created. Get the child's recordsize: 1048576,
source the child. Inherit recordsize on the child and get it again: 65536,
source qemu_mirror/delegated-root. Save response and activity, close the
manager, let the fixture verify/export, then quit the private QEMU and run
verify-native-ui.sh.

Run `build-aros/native-gui-run.CtNJ7BYr` passes this actual GUI workflow.
Screenshots: 06-parent-confirm.png is the parent 64K request;
10-local-1M.png is the child's local 1 MiB readback;
11-inherit-confirm.png is the reviewed inherit request;
13-inherited-64K-readback.png shows 65536 and the parent source.
The independent post-manager query verifies both parent and child. The final
verifier requires five successful authenticated mutations (create, snapshot,
two sets, inherit), saved property response, export, actual UID77/GID88
ownership, upstream zdb No leaks and unchanged offline verification hashes.
Log: `build-aros/recordsize-gui-verify.log`. Launcher build is warning-free;
the guest exited normally. This closes the recordsize GUI gap, not the full
port. Earlier native DOS cold-read/block-size tests remain separate evidence.
No DUT, shared hosted session or remote storage was used.
