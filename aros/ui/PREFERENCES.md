# Native preferences and secret storage

## Native host identity

Identity publication creates `ENV:OpenZFSHost.update` before checking the
service's Starting/ready reservations. Service startup publishes Starting
before checking this marker. This publish-before-observe handshake excludes
concurrent startup and publication without treating CPU-local Forbid as an
SMP lock. The writer retains the marker through ENVARC and ENV publication;
other writers also refuse it. A leftover marker fails closed. After a failed
writer, stop OpenZFS and verify both identity records and any `.txn` directory
before manually removing the empty update marker and retrying provisioning.
Do not clear a marker belonging to a live writer. Changing ENV assignments
while a service or identity writer runs is not supported.

Native qualification (2026-10-02): external `startup-cold.GXotMcVf`, bundle
`native-bundle.tVWAy5JT`, two-CPU QEMU Create/Read run exited 0. The fixture
checks startup-reservation writer refusal, competing-writer refusal, both
held and stale update-marker startup refusal, unchanged identity records,
and normal startup after cleanup. The same run verified cold pool restore,
handler activation and exact DOS reads; original fixture hashes were unchanged.
These are deterministic overlap-boundary tests, not a randomized scheduling
stress run or power-loss recovery qualification.

Manager's **Startup...** dialog includes **Host identity (8 hex digits)** and
**Save host ID...**. Choose a nonzero ID unique to the machine. Confirming
saves a version-one IFF `OpenZFSHost.prefs` to ENVARC, then ENV. The normal
native bundle also provides `OpenZFSHostIdentity 1234abcd SAVE` (example
syntax, not an ID to reuse on every machine). Both use the same provisioning
API, require configured security and SYSTEM ownership, and refuse while
the native service is active. This is not a live identity change.

The service reads ENV at startup; it never substitutes the `ZFS_HOSTID`
environment variable or generates an ID. An absent record leaves identity
zero. A malformed/untrusted record or pending identity transaction refuses
startup. ENVARC is persistent and follows the normal startup copy to ENV.
The ID is not a password or encryption key; IFF does not make it secret.

Save reports ENVARC and ENV publication separately on failure: the two
volumes are not an atomic transaction. Do not assume both changed after a
partial failure. Pending `.txn` records require recovery, not blind removal.
Persisting a host ID does not enable or qualify multi-host protection (MMP).

User direction, 2026-09-23: use the Amiga/AROS IFF preference convention,
not a text configuration file. IFF is a container and can hold encrypted
payloads; binary representation alone is not confidentiality protection.

## Current startup integration

`OpenZFSStartup GUID-16-HEX` now joins the trusted saved-record import stage
and the linked, pool-scoped upstream libzfs mount stage in one native command.
It consumes the validated record once, submits expected-GUID authenticated
import, waits for completion, and mounts only when requested. Disabled records
do nothing. Failure reports admission status, release status and any retained
ticket; it does not retry an uncertain import or export a partially mounted
pool. The mount implementation is linked into this command, not resolved from
an editable command search path. Existing standalone `OpenZFSStartupMount`
remains available for explicit mounting of an already imported pool.

Private full-service QEMU `native-import-run.3K6rFNDC` passed real command
invocation for disabled settings, successful import/mount, duplicate-import
refusal and native DOS file write/read. The complete CLI/native lifecycle suite
exited zero and both original fixture hashes remained unchanged. These are
isolated fixtures, not user pools. Link evidence: `cli-link.qkyRwbMM`.

Expanded run `native-import-run.9rnbqGo6` also passed mount-disabled import
(filesystem remains unmounted), read-only import with eligible mounting,
rejected dataset creation on that read-only pool, and subsequent writable
re-import. The complete regression suite again exited zero and the original
fixture hashes matched. These tests execute the actual linked command binary.

The combined SFS-backed cold-boot import/mount path is now verified in isolated
QEMU as described below. This is not yet an installed production boot service:
deployment into a user's startup sequence, executable trust, and filesystem
security provisioning still require explicit integration. No boot script is modified by building these commands
or using Save for boot. The sections below retain the earlier staged evidence;
their historical outstanding-work statements are superseded where described
here and in subsequent entries.

The stock AROS `workbench/s/Startup-Sequence` uses `Copy ENVARC: ENV: ALL
NOPRO ...`. `NOPRO` deliberately leaves creation protection in place. Native
run `preferences-run.eYiUCEOW` executes the real Copy command with permissive
MuFS defaults and proves that the resulting ENV record is rejected, not treated
as trusted merely because its IFF data is valid.

`oz_startup_settings_restore_trusted` provides explicit provisioning: validate
the system-owned protected ENVARC record through the same open handle, then
publish only ENV through the secure transaction writer. This is not automatic
reader fallback, permission repair, or a pool import. Unsafe existing ENV files
remain refused. Run `preferences-run.wBENVGWR` passes that refusal, protected
fresh ENV publication under permissive defaults, default restoration, safe
replacement, unsafe/foreign archive rejection, pending archive transaction
refusal and ordinary-user rejection. No OS startup file was changed.

`OpenZFSStartup GUID-16-HEX RESTORE` explicitly performs that provisioning before
the ordinary trusted ENV import path. Without RESTORE, a missing ENV record
still fails; ENVARC cannot silently override an absent/disabled runtime record.
RESTORE is an explicit request to replace runtime configuration with persistent
configuration, including its enabled flag. Automatic installation remains
unverified and disabled.

`OpenZFSStartup GUID-16-HEX PREPARE` performs only the same secure publication
from ENVARC into ENV, returning before any service request or pool import.
It preserves all identity, ownership, protection and transaction checks;
it does not repair an unsafe existing ENV file. This provides an explicit
pre-copy boot preparation step: configure security and assigns, PREPARE each
explicitly selected record in fresh ENV, run the normal `Copy ... NOPRO`,
then start the service and invoke ordinary `OpenZFSStartup GUID` to import.
No startup sequence is automatically edited and no record discovery or blanket
authorization is implied. As with RESTORE, PREPARE explicitly replaces runtime
settings with their saved values, including the enabled flag.

Initial helper-level cold test `startup-cold.H53T6bq6` verifies protected
publication followed by real `Copy ENVARC: ENV: ALL NOPRO NOREQ` and ordinary
ENV import across separate QEMU boots, with exact DOS read and readonly denial.
The archive, mirror overlays and original backing hashes remain unchanged
during the readonly second boot. This uses isolated security accounts and
private OS overlays; production deployment is not thereby qualified.

Command-level `startup-cold.Fc5FbXy1` repeats this with the actual packaged
`OpenZFSStartup GUID PREPARE` from `native-bundle.ZzLVrHMp`, and additionally
checks that the pool is still absent after PREPARE. Ordinary startup then
imports and mounts it after Copy. Both phases and hash checks pass; host exit
is zero (`startup-prepare-cold-1.log`). CLI build `cli-link.AUPAiRcg` links all
four commands successfully (`startup-prepare-link-1.log`).

Further cold run `startup-cold.QGaHJStU` moves PREPARE and Copy before service
startup and verifies both public/admission ports absent before and after
preparation. Expanded `startup-cold.PW0qHhmj` also rejects three malformed
command invocations and an actual ordinary-user PREPARE; ENV is still absent
before the subsequent system-user preparation succeeds. Both full two-boot
runs exit zero, including import/mount/read-only checks and unchanged hashes
(`startup-prepare-cold-2.log`, `startup-prepare-cold-3.log`). These qualify the
explicit ordering in the isolated fixture, not automatic installation.

Full-service run `native-import-run.zWNCk2FC` verifies the linked RESTORE option:
secure Save publishes both assigns, the harness removes only its private ENV
record, ordinary invocation fails without archive fallback, and explicit
RESTORE imports/mounts the readonly pool. Mutation is rejected; later writable
re-import and the complete CLI/native suite pass. Runner exit zero and original
fixture hashes match. This models ENV regeneration within one guest, not a
durable archive surviving power loss or a completed unattended cold boot.

`test-qemu-startup-cold.sh` now tests persistent settings on a newly created
32 MiB SFS image across two separate QEMU processes. Native run
`startup-cold.ItRKsY1x` passes CREATE (secure ENV/ENVARC save) and READ (initial
ENV absence, trusted restoration, exact GUID/flags/name/member comparison).
SFS is explicitly configured for native MuFS enforcement with a disposable
test-only volume marker. The test follows native SFSformat's inhibit sequence,
inhibits the volume after completing file operations, and gracefully exits QEMU
between boots. The archive hash is identical after READ. Build log is empty.
No OpenZFS engine/pool is attached in this focused settings test; this is not
combined boot-time import/mount or abrupt-power-loss durability proof.

Retained failed fixtures: `startup-cold.Q11TjhZL` omitted the SFS formatter's
inhibit/uninhibit sequence and CreateDir returned ERROR_NOT_A_DOS_DISK;
`startup-cold.QJbyehpu` reached secure publication without enabling SFS's
multi-user-volume mode and was refused. Fixes are confined to the disposable
test harness; production trusted-metadata checks were not weakened.

The optional `AROS_TEST_STARTUP_BUNDLE` mode adds two private qcow2 mirror
members at ATA units 1/3 beside the SFS archive at unit 0. CREATE starts the
packaged real service, imports those explicit members, records the real GUID,
sets pool/filesystem mount properties, exports and securely saves the record.
A completely new QEMU process starts with empty ENV and an unimported pool;
the harness starts the service, waits for its admission endpoint, invokes the
packaged `OpenZFSStartup GUID RESTORE`, then checks exact DOS file contents,
read-only mutation refusal, unmount and export. The GUID selector fixture is
not authority; the command still verifies protected IFF and on-disk identity.

`startup-cold.G8qE7sN2` passes both phases, runner exit zero, empty build log.
SFS archive and both mirror-overlay hashes are identical across the readonly
second boot; both original raw backing hashes also match. It uses the private
three-disk ATA-wakeup-fixed QEMU and hashes it with the required OS overlays.
Earlier combined pass `startup-cold.H3DZT8Iy` retains a harmless indentation
warning fixed before the final run. Neither run proves abrupt-power-loss
durability, automatic production installation or a full feature-complete port.

The Manager persists category and window dimensions as native IFF preferences.
Its activity log and response text are separate diagnostic exports.

## Pool startup record codec

`startup-settings.c` provides a separate native iffparse codec for one pool
startup record. It does not import pools, execute commands, store credentials,
write persistent files itself. The startup editor and file layer described
below provide that integration. The
future startup executor must still authenticate its caller, match the expected
GUID, perform backend path/identity validation and mount only the configured
pool. Format validity alone is neither authority nor device identity proof.

The version-1 representation is `FORM PREF`, a zero six-byte `PRHD`, then one
`OZBT` chunk. Its payload is explicitly big-endian, never a dumped C structure:
16-bit version, 16-bit flags (readonly=1, mount-datasets=2, enabled=4), 64-bit
nonzero expected pool GUID, 16-bit path count (1-32), 16-bit reserved zero,
256-byte pool name, then one 256-byte field per explicit path. Strings use
native DOS path bytes, reject controls, require termination and zero padding.
Paths must be absolute/native-volume-qualified and unique. Full pool-name and
device semantics remain backend validation, not a substitute parser here.

Readers cap the container at 16 KiB, reject malformed/nested/duplicate known
chunks, unknown flags/version, nonzero reserved fields, invalid counts and
hidden bytes after string terminators. Unknown bounded leaf chunks are skipped
for extension. Failed reads preserve the caller's prior settings. Invalid
writes return before output; valid writes require a fresh staging handle and
the caller must check close. Calls belong to one owning task, not concurrent
workers. Startup records now share the caller-context transaction engine with
UI preferences: metadata-preserving replacement, pending-transaction refusal,
read-only previous-record recovery and failure-preserves-output semantics.
Authentication and execution for automatic imports remain unwired.

`startup-settings-file.c` implements Use/Save routing through
`oz_startup_settings_apply`: one `OpenZFS-<16 lowercase hex GUID>.prefs` file
per pool, Use in ENV only, Save in ENVARC first and then ENV. Publication bits
separately identify completed writes even when cleanup or the second write
fails; the two volumes are not an atomic transaction. The current-record
loader reads ENV only and verifies the embedded GUID against the requested
GUID. It never resurrects an absent ENV entry from ENVARC. Normal OS startup
copies ENVARC to ENV; this API does not change that convention. Disabled
records remain representable without deleting either file. These functions
store references only and never import, mount, or load encryption keys.

Native diskless QEMU `preferences-run.XLS0aY42` verifies transaction reuse;
`preferences-run.IgABobwZ` additionally verifies Use vs Save, persistent-only
partial publication on an ENV transaction conflict, GUID mismatch rejection
without output modification, and missing ENV without ENVARC fallback. The
runner supplies private RAM-backed ENV/ENVARC assigns. Both runs exit zero
with startup and original preferences/security PASS markers; build logs
`startup-storage-build.log` and `startup-env-build.log` are warning-free.

The Manager's separate **Startup...** window now loads, edits and saves these
records through the real codec/storage API. It accepts a nonzero 16-hex-digit
GUID, pool name, explicit list of up to 32 member paths, and enabled/read-only/
mount flags. Add/remove modify the editor; Load reads ENV; Use/Save perform
the file operations above and report errors/partial publication. No request
editor secrets are copied. Saving a record does not import or mount anything;
the boot executor is still outstanding.

Private native QEMU `native-gui-run.Juuy8rOI` verifies the actual Manager:
opened Startup, entered GUID, loaded a seeded two-member ENV record, enabled
it, clicked Save and closed both windows. The native harness then decoded
ENV and ENVARC, asserting GUID, enabled/read-only flags, pool and paths, and
exited zero (`Native startup GUI Load and Save actual records PASS`). Captures
`03-loaded.png` and `05-save-result.png` show the loaded record and completed
save respectively. No OpenZFS service or disk devices were attached for this
UI/settings test; it is not proof of automatic import or a pool operation.

The authenticated native typed-import API now accepts optional
`guid=<16 hex digits>` alongside the mandatory `readonly=on|off`. Zero,
malformed and duplicate GUID fields are rejected by the parser; omission
retains the interactive name-based behavior. The engine compares the supplied
GUID with the discovered pool labels before granting the path policy,
handing off writable locks, or calling `spa_import`. Startup execution must
always supply this field; checking identity only after import is insufficient.
Private full-service QEMU `native-import-run.CvKBx81C` verifies refusal of GUID
1, then matching-GUID re-import using the upstream pool property. The complete
CLI lifecycle and native DOS read/write/export/re-import suite also passes,
runner exit zero and original fixture hashes unchanged. This validates the
identity prerequisite, not an implemented startup executor.

`startup-import.c` now implements the saved-record **import stage**: read the
GUID-keyed ENV record, refuse implicit transaction-backup recovery, skip a
disabled record, and submit typed explicit-path import with expected GUID to
the authenticated admission service. It preserves caller identity, never
forces or rewinds, and waits for a completed job rather than reporting ticket
acceptance as import success. Transport errors retain the accepted ticket for
diagnosis; release errors and backend status/detail are separate result fields.
It does not parse diagnostic prose for errno. A successful import sets
`imported`; `mount_requested` remains a separate obligation for its caller.
This stage does not yet enumerate/mount datasets, install a boot command, or
establish trusted configuration provenance for unattended privileged startup.
In particular, filesystem parser validity alone cannot make an editable ENV
record trusted boot policy; do not install this as an automatic root task yet.

Private full-service QEMU `native-import-run.JumS0aLX` tests the real saved IFF
record end to end: disabled skips even with an absent endpoint, enabled reports
missing endpoint, then authenticated import succeeds and releases its ticket.
The same pool is then read through native DOS and exported by the existing
suite. Full CLI/native suite exits zero and both original fixture hashes are
unchanged. This is import-stage proof, not completion of the boot/mount flow.

`native-import-run.ZQ7coFO4` extends that real-service verification: with the
same saved record, MuFS user 77/88 receives backend EPERM, the system owner
imports successfully, an interrupted transaction's previous record is refused
before admission, and a repeated import reports failure rather than claiming
the existing pool as a fresh success. Completed tickets are released in both
backend success and rejection paths. The full CLI/native suite and original
fixture hash checks pass. Caller authorization is therefore exercised, but it
does not establish ownership/protection/provenance of a configuration that a
future privileged unattended boot process reads; that remains separate work.

The separate `oz_startup_settings_trusted` reader now requires configured
security.library, the MuFS system caller, a regular system-owned ENV file,
and no group/other write or delete permissions. It checks `ExamineFH` on the
same file handle used by iffparse, both before and after decode; owner/group,
protection, size and timestamp changes cause refusal. It rejects a pending
transaction, never reads an archive/backup fallback, never changes permissions,
and preserves output on error. Filesystems that cannot supply the metadata
fail closed. This relies on the running DOS/MuFS filesystem enforcing those
permissions; it is not a signature or protection against privileged writers.

`oz_startup_import_trusted` consumes that exact validated record without a
second pathname read, then uses the normal authenticated expected-GUID import.
The interactive import entry point remains distinct. Private native test
`preferences-run.xdTHlpE8` verifies system-owned protected acceptance, each
group/other write/delete rejection, foreign-owner rejection, pending
transaction refusal and ordinary-caller rejection. Full-service run
`native-import-run.1vb5ur5P` verifies ordinary caller denied before admission,
unsafe file denied, and the protected record actually imported, followed by
DOS reads/export. Both runners exit zero; full-service fixture hashes match.
Secure publication/provisioning through the UI, trusted executable lookup,
and automatic import/mount orchestration remain to be wired; no boot task is
installed by this reader or these tests.

`oz_startup_settings_apply_trusted` now provides secure Use/Save publication
for the system caller. It temporarily sets that task's MuFS default creation
protection to private, restores the original default on exit, and uses the
same transactional ENV/ENVARC routing. Optional transaction metadata gates
verify the newly created staging directory, any existing destination, and
the final staged file before publication. Foreign-owned or group/other
writable/deletable existing records are refused, not adopted or repaired.
No unsafe file is first published and then chmodded. Existing safe comments,
owner/group and protections remain preserved. Partial publication bits retain
their previous meaning; a later error is not represented as an atomic save.

Private native QEMU `preferences-run.fXBOtM6j` exits zero with the complete
preferences/security suite: secure fresh ENV/ENVARC creation despite explicitly
permissive caller defaults, exact restoration of those defaults, successful
trusted reload, safe replacement, foreign/insecure destination refusal and
ordinary-user refusal. `startup-secure-save-build.log` is empty. Manager's
ordinary Use/Save remains unchanged until an explicit secure-save action is
wired and visually tested. Boot orchestration is still not installed.

The Manager now exposes **Save for boot** separately from ordinary Use/Save.
It calls the secure publication API, reports errors/partial publication using
the same status path, and explicitly says that it has not installed a boot
command. The tooltip identifies the system-owner requirement. Private native
GUI run `native-gui-run.qe5dQCPP` opened this actual editor, loaded the seeded
two-path record, enabled it and clicked Save for boot. The parent harness
deliberately supplied permissive creation defaults; after GUI exit it checked
the saved values, trusted ENV reload, and ENVARC system ownership with no
group/other write/delete bits. Both native GUI PASS markers and runner exit
zero confirm the real storage path, not merely the displayed message.
`05-secure-result.png` captures the result and help text. Final GUI build log
`startup-secure-gui-build-clean.log` is empty; the earlier indentation warning
is retained in `startup-secure-gui-build.log`. No boot task or pool was started.

`OpenZFSStartupMount POOL GUID-16-HEX` is the separate pool-scoped mount stage.
It uses upstream libzfs to open the pool, verify its GUID, enumerate its
filesystem descendants, inspect properties and mount eligible filesystems.
It does not call global mount-all, share datasets, force mounts or prompt for
keys. `canmount=off/noauto` and `mountpoint=none/legacy` skip that filesystem;
children are visited for their own properties. Already-mounted filesystems
are counted separately. Failed mounts/enumeration return failure with counts;
successful partial mounts are not rolled back or hidden. It is not yet wired
to a trusted unattended startup command or shipped in the development bundle.

Bring-up retained the failed link (`cli-link.sH9emwAS`, missing command global
`g_zfs`) and native failures (`native-import-run.hawHhohX` aborted in libzfs's
print-on-unknown-error mode; `hKuOjqYg` reported EOVERFLOW during enumeration).
The underlying transport converted upstream ENOMEM plus a larger nvlist size
into EOVERFLOW, preventing the dataset iterator's normal resize/retry. The
fix preserves ENOMEM and required size **only for DATASET_LIST_NEXT**, without
copying beyond the old buffer. Other operations retain their previous
overflow behavior, notably post-import failures that must not invite mutation
replay. The helper reports libzfs errors rather than enabling abort-on-error.

The initial resize fix passed full native QEMU `native-import-run.FelWD8j2`:
wrong GUID refused without mount, eligible filesystem mounted, root `none`
and `noauto/off` children skipped, repeated call reports an existing mount,
and the subsequent complete CLI/native suite passes with fixture hashes
unchanged. The final iterator-only restriction is rebuilt separately in
`cli-link.mGcy5Och` and checked in `native-import-run.Q6TpoC5K`.

`startup-iff-build-clean.log` is warning-free. Private diskless QEMU
`preferences-run.FAPRCG8c` passes exact independent 562-byte representation,
all truncation lengths, invalid framing/version/flags/GUID/count/padding,
unterminated strings, duplicate settings, trailing/concatenated data, bounded
unknown chunks, 32-path roundtrip and duplicate-path write refusal. The
existing native UI-preference/file/security suite also passes, runner exit
zero. No pool, physical device or user hosted session is used by these tests.

Implementation requirements:

- Use native iffparse.library with a versioned FORM PREF / PRHD structure
  and application-specific chunks. Follow existing AROS preference readers
  and writers rather than dumping host C structures to disk.
- Validate chunk sizes, versions, duplicates, numeric ranges, reserved bits,
  string encoding and termination explicitly. IFF parsing validates framing;
  application fields still require semantic validation. Check writes and
  close results; do not replace a valid saved file with a partial write.
- Follow native Use/Save semantics: ENV for the session, ENVARC for persistence.
  Preserve caller-based MuFS/security.library authorization and test corrupt,
  truncated, unsupported-version and access-denied inputs on native AROS.
- Ordinary preferences store references, not plaintext secret values. Do not
  persist request-editor contents or destructive-operation flags by accident.
- If an encrypted secret container is implemented, IFF chunks may contain its
  ciphertext and authenticated metadata. Use a vetted authenticated-encryption
  implementation, fresh nonces and explicit format/KDF versions. Keep its
  unlocking key outside the same file; define prompting or another protected
  key source before enabling unattended decryption. Wipe plaintext buffers and
  exclude secrets from logs, history, screenshots and generic request records.
- IFF preference framing does not replace upstream OpenZFS labels, nvlists,
  replication streams, or raw/hex/passphrase key-file interoperability.
  An encrypted IFF key container needs an explicit resolver; never silently
  reinterpret an upstream passphrase file as an IFF preference file.

This is the implementation contract; encrypted IFF secret storage is not yet
implemented.

## Implemented native codec

`preferences.c` now uses iffparse.library to read/write `FORM PREF` with a
six-byte standard version-0 `PRHD` and one twelve-byte `OZUI` chunk. The
application chunk is explicitly big-endian: 16-bit version (1), flags (0),
initial category (0-7), reserved (0), width (320-16384), height (200-16384).
Defaults are category 0, width 780, height 550. No request state or secrets are
represented. This is a handle-level codec; the caller retains the DOS handle
and must check close results. Writers must target a new staging file.

The reader caps this small preference format at 4096 bytes, rejects trailing
data and nested containers, requires exactly one header/settings chunk,
validates all fields, and only updates the caller on complete success. Bounded
unknown leaf chunks are skipped for extensibility, including odd-byte padding.
This is not an encrypted key-container implementation.

`preferences-run.efRzo1nW` / `preferences-run-1.log` passed on native AROS in
private diskless QEMU (runner exit 0). The test compares all 46 emitted bytes
against an independent expected representation, reloads them, checks every
truncation length, corrupt framing/version/flags/range fields, duplicates,
concatenated files and trailing data, then accepts a valid unknown padded
chunk. Rejected reads preserve the caller's previous settings; invalid writes
leave an empty test file. No DUT or shared hosted session is used.
After a signedness-warning cleanup, `preferences-build-2.log` is warning-free
and `preferences-run.x4dbJD57` / `preferences-run-2.log` passed again, exit 0.
The second run retains the reproducible build recipe, source/header hashes,
SDK IFF header hashes, frozen executable and exact input-image hashes.

## Native file lifecycle and Manager integration

`preferences-file.c` creates a fresh `<path>.txn` directory, writes and closes
`next`, and re-reads it through the codec before publication. The previous file
is renamed into that directory before the new file is published. Failed
publication attempts rollback; a failed rollback retains the backup. Loading
a missing main file may read that backup, without modifying anything. Existing
transactions are never silently overwritten or removed. An occupied transaction
blocks saving until explicit recovery; an automatic recovery UI is not yet
implemented. This is not an atomic two-file commit or a power-durability claim.

All DOS calls run with the current caller's identity. Non-files and existing
write/delete-protected files are rejected before replacement. The first native
file test (`preferences-run.fhIMyMrz`, `preferences-files-run-1.log`) exposed
that RAM permits renaming a delete-protected file: publication preceded a
failed cleanup. The explicit preflight protection check fixes that path;
it does not replace filesystem authorization or prove freedom from concurrent
metadata changes.

`preferences-run.kmqEsf1P` passed after the fix. The expanded test in
`preferences-run.Qo1mvu6B` / `preferences-files-run-3.log` also passed, exit 0,
covering write protection, delete protection, replacement, invalid preferences,
occupied transactions, backup fallback and refusal to replace a directory.
Both failed and passing logs are retained.

Manager loads ENV first, then ENVARC only when ENV is absent. Invalid input
leaves defaults and a visible activity warning; no automatic repair/write is
performed. Use writes ENV only. Save writes ENVARC then ENV and reports partial
success if the second step fails. A cleanup error after publication is reported
as saved-with-cleanup-failure, not as an untouched file. Only category and
window dimensions are captured, never request fields, keys or mutation flags.

The diskless native GUI run `native-gui-run.1I1YJ0Ec` /
`preferences-gui-run-1.log` passed three interactive launches, runner exit 0:
Use preserved category 5 in ENV without creating ENVARC; reopening visibly
restored Encryption; Save wrote matching ENV/ENVARC dimensions and category;
after removing ENV, reopening visibly loaded ENVARC; a subsequent Use changed
ENV to category 2 without changing ENVARC category 5. The independent launcher
reads actual IFF files after each Manager exit. Screenshots and UI action logs
are retained in that directory. No service was running for this focused UI test;
the Manager correctly displayed unavailable rather than inventing pool state.

Reproduce with `build-preferences-gui.sh` followed by
`AROS_PREFERENCES_GUI=1 test-qemu-preferences.sh` and the documented private
POSIX/DOS/ATA overlay environment variables. ENVARC and ENV are both private
RAM directories in this test, with no host storage, DUT or shared session.

## Caller-identity verification

`preferences-run.P2HIT0Wi` / `preferences-security-run-5.log` passed the complete
native codec/file suite plus security.library/MuFS tests, runner exit 0 and a
warning-free build. A root-owned file and directory were explicitly protected;
after login as UID 77/GID 88, loading and saving returned EACCES, no output
settings were modified, and no staging directory was left behind. Returning
to the owner confirmed that the original saved file was unchanged. The same
unprivileged user also successfully created, replaced and reloaded preferences
in its own subdirectory of a deliberately shared parent. The production file
helper does not log in or switch identities.

Retained failed hypotheses: `preferences-security-run-1.log` wrongly treated
secLogoutA's returned owner ID as a boolean; zero is a valid restored identity.
Runs 2/3 assumed directory protection alone made the newly created file
unreadable; diagnostic run 3 confirmed UID/GID 77/88 could read it. Explicit
file protection corrected the fixture, and run 4 passed denial/preservation.
Run 5 added successful non-root saves. These tests do not prove directory
traversal enforcement, concurrent permission-change safety, or physical-media
durability.

## Replacement metadata and visible failure paths

Replacement now copies the existing owner/group, complete native protection
mask, and comment to the staged file, then re-examines it before publication.
When available, security.library's secSetProtection preserves the complete
MuFS group/other mask rather than the ordinary DOS limited-mask operation.
Changes still run as the caller; inability to preserve metadata aborts the
replacement. Creation timestamps are not preserved for new preference data.
This does not claim atomicity against concurrent permission changes.

`preferences-run.41VLVR9Z` and expanded `preferences-run.DXfuUjLy` passed native
replacement tests. The latter verifies that a root replacement retains UID 77,
GID 88, group-read and script flags rather than acquiring root ownership; a
non-root replacement retains those fields too. A private root file's comment
and restrictive protection survive replacement, and another user remains denied
read/save. Logs: `preferences-metadata-{build,run}-{1,2}.log`.

The rebuilt Manager also passed `native-gui-run.ROtycUyr` /
`preferences-failures-gui-run-1.log` (runner exit 0). Screenshot
`invalid-preferences.png` shows defaults and the rejected-ENV warning; the
launcher verified the seven invalid bytes remained untouched after closing.
`recovered-preferences.png` shows Encryption loaded from the interrupted-save
backup. Clicking Use produced EBUSY (`pending-transaction-refused.png`), and
the launcher confirmed the main file remained absent, backup valid and archive
unchanged. Reproduce with `AROS_PREFERENCES_GUI=failures`. Pending transaction
recovery still requires explicit external resolution; no automatic deletion or
repair has been added.
