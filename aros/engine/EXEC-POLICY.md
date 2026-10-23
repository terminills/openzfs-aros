# Native execution property

## Persistent settings archive (2026-10-22)

`AROS_TEST_STARTUP_ARCHIVE=1` adds an independent 32 MiB muFS IDE disk
to the persistent companion suite. Writer and fresh-boot reader exercise
HostIdentity SAVE, IFF readback, trusted Startup PREPARE, and disabled
Startup RESTORE using ENVARC on that disk and fresh RAM ENV directories.
Run `public-service-run.Lffxaiig` passed both phases, the complete mirror
suite, and the full archive image unchanged-hash check. Enabled pool imports
are separately exercised by the existing startup-import fixture; this does
not yet qualify an enabled record restored from persistent ENVARC.

This exposed a real filename limit: a 30-character GUID preferences name
plus `.txn` is rejected by muFS with DOS error 210. Staging now replaces
`.prefs` with `.txn` for long preferences names. Legacy pending transaction
directories are preserved and rejected, not overwritten or silently adopted.
`BmV4g2lW` and `CkSkvbmB` retain the pre-fix failures; `L4FQzBxT` passed
the writer but its cold QEMU invocation rejected a read-only IDE block node.
The passing run uses a writable IDE node and proves no cold archive change
by comparing the entire disk hash; it does not claim hardware write protection.

## Alpha18 companion qualification (2026-10-22)

The opt-in public-service runner `AROS_TEST_COMPANION_TOOLS=1` installs and
hashes the four packaged companion commands and runs OpenZFSRuntimeCheck
before the storage tests. A prerequisite failure is a suite failure, not a
skipped security check. Subsequent invalid-argument checks must not be counted
as verified if RuntimeCheck fails first.

On the alpha18c5 base image (SHA256
`bbc9776d6a5cf0a8ce7c26010e297e9e7a02980714750955053a38d89eca0e27`),
`public-service-run.S3WXz3N7` fails: both LoadSeg and NewLoadSeg accept the
execute-protected RAM probe. Ordinary loading and redirected stdio pass.
This reproduces the missing loader prerequisite on the current platform;
earlier private-overlay passes do not qualify this unmodified image.

Do not rebuild its DOS overlay from the shared generated-object map without
checking provenance. The ISO identifies AROS revision `f81cd211c4`, whereas
the inspected source checkout is `671efe3b321926332e3330a0e138ed4a3d4e820e`.
Nineteen DOS source files differ, and the ISO DOS library's text differs from
the current shared build. A matched private build and fresh native tests are
required before claiming this runtime prerequisite is satisfied. The failed
run's logs and input manifests are retained; no physical device was used.

Private revision rebuild `dos-revision.0s3jHPGK` compiles the DOS translation
units and source headers from `f81cd211c4`, generates the module headers anew,
and applies the existing loader patch. It still uses the explicitly recorded
SDK link archives and generated catalog header; it is not claimed byte-identical
to the original ISO DOS library. `build-dos-revision.sh` makes these inputs
explicit and hashes them instead of recycling shared DOS objects.

With this private replacement, `public-service-run.vHG7IgBM` passes both APIs
for on/off/on on RAM and on the mounted ZFS dataset. Execute-off returns
DOS error 224 while ordinary reads remain permitted. RuntimeCheck, companion
invalid-argument cases, and the complete RAM-backed storage suite pass. The
temporary ISO is pruned after success; logs/manifests remain. This does not
qualify successful persistent startup configuration or physical USB storage.

The public runner accepts this focused replacement as
`AROS_TEST_BASE_DOS_PACKAGE` in base-platform mode, with `AROS_PACKAGE_TOOL`.
Its verifier extracts the selected ISO's original package and requires exactly
one changed member, dos.library, with all other bytes and module order intact.
This packaging check is not ABI proof; revision provenance and runtime testing
are separately required. Legacy mixed DOS/ATA/zlib overlays remain prohibited
in base-platform mode. Five host tests cover the package comparison guard.

The freshly rebuilt unpatched control `dos-revision.KKtbRZHG`, tested in
`public-service-run.dxAbRbx0`, reproduces the same execute-off acceptance in
both APIs and fails RuntimeCheck. Thus the passing patched result is not
explained merely by rebuilding DOS from the pinned revision. Both control
and patched runs reach the runtime checks; neither result is inferred from
compilation alone.

Cold mirror regression `public-service-run.APtjYbHn` also passes with the
patched DOS package and the separately recorded FAT clean-detach overlay:
writer companion/loader checks, fresh healthy and degraded imports, exact
DOS reads, wide directory enumeration, encryption/key rotation, zvol device
checks, and beyond-parity refusal followed by restored reads. Reader member
hashes remain unchanged; independent zdb reports no leaks. Successful test
images are automatically pruned. The two diagnosed negative-run ISOs are
also pruned after terminal-process checks and SHA256 capture; their logs and
manifests remain. No other agent's VM or physical storage was modified.

## Companion configuration commands (2026-10-22)

`public-service-run.kOHm9dDV` adds two successful executions of the actual
OpenZFSHostIdentity and OpenZFSStartup commands. Each uses fresh private RAM
directories, system ownership and private permissions, temporarily assigned
as ENV and ENVARC. HostIdentity SAVE succeeds; the native IFF reader verifies
the exact identity in both records. A disabled startup record passes CHECK,
does not import or start a service, fails CHECK when its ENV copy is removed,
then passes PREPARE from ENVARC, trusted readback, and disabled RESTORE.
The original assigns are restored and temporary records removed before the
service starts. The complete subsequent RAM storage suite passes.

The negative fixture runs Ic4B1y0r, IJ9rz0cD, BNhaZOUR and RicJdNjI are
retained as logs/manifests, with test ISOs pruned after open-use checks and
hash capture. The final fixture correction releases CreateDir's exclusive
lock and reacquires a shared lock before AssignLock. Passing an exclusive
directory lock into the assign prevented the publisher from accessing it.
No publication security checks were relaxed. This qualifies command execution
and explicit ENV provisioning, not an enabled boot import or durable ENVARC
storage across a reboot.

`public-service-run.TlvWh0Z2` additionally verifies the packaged
OpenZFSStartupMount against the actual pool GUID. A mismatched GUID leaves
the mount table empty. With a native `OZSTARTUP:` mountpoint and the parent
dataset's canmount disabled, the first invocation reports one child mounted
and one parent skipped; a second reports the same child already mounted.
The test checks the live mount table and opens the DOS volume, then detaches
and restores the temporary properties before the existing storage tests.
The full RAM suite passes. This is command-level mount verification, not
yet proof of a complete enabled OpenZFSStartup import across a reboot.

Retained negative fixture evidence qD1pDPy4 used an inherited Unix-style
mountpoint, unsupported by the native DOS-volume mount adapter. chHvNroI
then observed the mount caused by `zfs set mountpoint` itself. The corrected
test sets a native mountpoint and explicitly unmounts before exercising the
startup command, so it cannot credit the property setter's side effect.
Both failed-run ISOs are pruned after hash capture and open-use checks;
their serial output and manifests remain.

The expanded fixture also passes cold mirror run `public-service-run.omcqRDJB`:
writer configuration and startup-mount command checks, loader policy, fresh
healthy/degraded/restored readers, exact data, unchanged reader member hashes,
and independent zdb with no leaks. Temporary runtime images are pruned by
the runner; source/input manifests and serial logs remain. Configuration
records in this test remain RAM-backed and must not be described as durable
boot-time settings qualification.

## Enabled startup imports in a fresh native guest (2026-10-22)

`AROS_TEST_STARTUP_IMPORT=1` requires the persistent mirror and companion
fixtures. The writer records the actual pool GUID and exact member paths in
IFF. The cold client reads this persisted fixture, checks the fixed pool name,
read-only/enabled flags and every member path, then explicitly publishes a
trusted RAM ENV record. The shipped OpenZFSStartup command performs each
import; the existing handler tests verify exact data and write refusal.

`public-service-run.guD7mq9N` passes healthy, degraded and restored imports.
Before each import, a trusted record with a deliberately wrong GUID is rejected
and the public pool query confirms no pool was imported. The correct GUID
then succeeds. All three command markers, exact DOS data, unchanged reader
member hashes and independent zdb/no-leaks checks pass. Mounting is explicitly
disabled in these records; StartupMount is qualified separately above.

This is not yet durable trusted-ENVARC qualification: the FAT backing disk
preserves fixture bytes but not MuFS ownership. No production trust check is
bypassed. A separate ownership-preserving boot-settings volume is still needed
to test direct trusted ENVARC restoration across reboot.

Earlier `wy0mqAQv` completed guest and independent checks, but its shell runner
exited 2 after a source edit shifted Bash's ongoing script read. The runner now
executes a private snapshot, retained and hashed with each run. Clean repeat
`dRPDbgYM` exits zero; `guD7mq9N` adds the wrong-GUID negative checks and also
exits zero. Successful test images are pruned; evidence remains.

## User-facing runtime detection (2026-09-30)

OpenZFSRuntimeCheck now checks both loader APIs using a copied executable in
a new private RAM directory. It loads/unloads but never executes that copy,
checks on/off/on permissions, removes its files and returns failure if an
execute-protected file loads or setup/cleanup fails. It does not open pools
or physical devices. This tests the runtime loader prerequisite on RAM,
not dataset policy propagation or every execution mechanism.

Private hosted `hosted-security-test.DgVNarsA` passes all six outcomes with
the qualified DOS overlay. Control `hosted-security-test.26qRMaBm` uses the
same checker without the overlay: ordinary loads and redirected stdio pass,
both execute-off loads incorrectly succeed, and the checker reports FAIL.
The negative harness expects that failure explicitly, not a repaired runtime.
Both are registered to the development test archive. Logs: `runtime-execute-patched-test.log` and
`runtime-execute-unpatched-test.log`. The first compile attempt's unavailable
hosted `-lsecurity` flag is retained in `runtime-execute.vDAAwjlI/build.log`;
the direct inline-API build succeeds in `runtime-execute.V3ZITWvZ`.
Actual Manager-button verification is complete in private hosted runs
`hosted-security-test.wMdOTnBV` (patched PASS) and
`hosted-security-test.nU7PPmJ9` (unpatched detected FAIL, then successful
reconnect). Screenshots and saved responses are recorded in
`aros/ui/NATIVE-UI-TEST.md`. Native checker packaging is tracked separately
in `aros/distribution/README.md`.

## Both public loader APIs and re-enabling (2026-09-30)

`public-service-run.Et3diuT8` (native SMP) and
`hosted-security-test.VVlHY8a9` (private hosted) both pass LoadSeg and
NewLoadSeg with on/off/on live transitions. Each API succeeds in phases
0 and 2 and returns NULL/IoErr224 in phase 1. Effective protection is 0/2/0;
ordinary ELF reads remain valid throughout. No copied executable is run.
Both runners exit zero; hosted persistence/cold read/degraded assign and
unchanged backing hashes also pass. Evidence is registered to the development test archive.
Logs use `loader-api-native-` and `loader-api-hosted-` prefixes. Native uses
untraced `loader-overlay.L7ctzwkQ`, hosted uses `loader-overlay.4CUMjP0r`.
These remain explicit private DOS overlays, not changes to a shared runtime.

## Private DOS loader candidate (2026-09-30)

Native boot failure localized: diagnostic `public-service-run.N1kOyUyA`
faults in input.device ProcessEvents before any loader-policy trace. The
builder had repacked modules using directory traversal order, changing boot
order despite identical module contents. It now parses the original archive,
preserves its exact order, and verifies every non-DOS module against its
original archived bytes. With that correction, diagnostic
`public-service-run.mdgy6LJK` passes on native SMP: exec=on loaded=1,
exec=off loaded=0/IoErr224, readable=1 for both, and service/DOS roundtrip PASS.
This supersedes the unresolved packaging diagnosis below; it is not a new
filesystem behavior fix. Both earlier boot failures remain retained.

Untraced native repetition `public-service-run.jofTq7Am` also passes with
`loader-overlay.L7ctzwkQ`, including the required loader marker and public
service lifecycle/DOS roundtrip. No loader trace is compiled into this build.
Both corrected native runs are registered to the development test archive. This qualifies this
specific LoadSeg path, not all execution APIs or an unmodified nightly.

Earlier native SMP qualification failed. Overlay
`loader-overlay.YdKtHTds` preserves the qualified cli-environment DOS map
and every other base-package module. `public-service-run.16VlWq5s` faults
during startup at CR2=0xb0 before the OpenZFS checks. The same new service
and client with original `cli-environment.fgzmQKMR` DOS package pass in
`public-service-run.qaBGhhhB` (loader probe disabled). Both runs are retained.
The suspected source-version mismatch was checked: the native and hosted
LoadSeg and dos_intern.h sources were identical, so that hypothesis does
not explain the fault. That misordered package must not be shipped.
Logs: `loader-policy-native-test.log`, `loader-policy-native-control.log`,
`loader-overlay-native-build-2.log`. The test runner now explicitly requires
the loader-policy PASS marker when the opt-in probe is selected.

`aros/patches/dos-loader-execute.patch` checks effective protection on the
already-open file before InternalLoadSeg. It refuses an all-identities
execute veto, including for root, without denying ordinary reads. It fails
closed if ExamineFH fails. It does not introduce new per-user MuFS ACL
semantics, enforce interpreted scripts, or prevent custom/manual ELF loaders.
Those wider security paths are not qualified by this candidate.

`build-loader-overlay.sh` replaces only the ABI-matched generated LoadSeg
object in a new private DOS library and records input/output hashes. The
hosted runner accepts an explicit `AROS_TEST_DOS_LIBRARY`, copied only into
its fresh runtime and included in its checked manifest. No shared OS files
are modified. `loader-overlay.4CUMjP0r` with hosted run
`hosted-security-test.bfh85LGs` passes execution-on load, execution-off refusal,
ordinary ELF reads, authentication, persistent mirror write/export, independent
zdb, cold DOS reads/degraded assign and unchanged reader backing hashes.
This is a private hosted candidate, not stock-nightly/native-SMP qualification.

Rejected first candidate `loader-overlay.T2tGyn7f` also called global MuFS
authorization from LoadSeg. `hosted-security-test.doPRYV1z` denied a startup
utility before login; that behavior was rejected rather than weakening user
permissions. A malformed patch during revision is retained in
`loader-overlay-build-2.log`; corrected build is `loader-overlay-build-3.log`.

## Actual hosted loader gap reproduced (2026-09-30)

The opt-in `AROS_TEST_LOADER_POLICY=1` private hosted service test copies a
known executable onto the mounted ZFS dataset. It loads and unloads the ELF
without running it. It checks live protection metadata and ordinary readable
ELF bytes before each load, restores the initial exec property, and deletes
the private copied executable afterward. A policy bypass fails the suite.

`hosted-security-test.HiAcOKZa` reproduces the gap: exec=on reports protection
0 and LoadSeg succeeds; exec=off reports protection 2 (FIBF_EXECUTE denied)
and LoadSeg still succeeds with IoErr 0. Both ordinary reads succeed as they
should. Runner exits 1. Earlier `hosted-security-test.fFaFTlyQ` reproduces
the same bypass without the additional ExamineFH check. Both failed runs
are retained and registered to the development test archive. Build/test logs use the
`loader-policy-hosted-` prefix. No loaded program was executed.

This upgrades the historical source-inspection concern below to a confirmed
runtime integration defect. The handler metadata is not the missing step;
native loader execution-policy enforcement still needs implementation and
verification. These focused failing runs do not reach cold persistence tests.

The upstream DSL `exec` property now registers an initial/live callback for
each native mount. The authenticated property setter accepts its upstream
on/off values and inheritance, with the existing per-property delegation
check. It rejects invalid values and refuses changes on read-only pools.

`oz_read_mount_access` rejects ACE_EXECUTE on regular files when exec=off,
including privileged callers. Directory search remains possible. DOS Examine,
ExNext and ExAll expose the effective execute-denied protection bit and remove
group/other execute permission without modifying the stored file protection.
Changing exec back to on therefore restores the original protection metadata;
it does not grant execution contrary to the file ACL.

Verified native run: admission-service-run.UeB15a8j. The separate service and
client verify on/off/invalid transitions, inheritance, privileged file-execute
denial, directory traversal, unchanged readable file contents, live Examine
protection updates, read-only setter refusal and cold persistence. Both guest
phases pass; read-only disk hashes are unchanged and Linux zdb reports no leaks.
The existing UI property catalogue already includes dataset:exec; it uses the
same authenticated setter, not an independent UI-only implementation.

Frozen-binary full regression: admission-service-run.h9wolGin passed both
WRITE/VERIFY phases, cold property checks, existing authenticated jobs and DOS
tests, unchanged read-only hashes, Linux zdb and independent compression/atime
verification. Runner exit status is zero; both private guests are stopped.

ExAll extension: admission-service-run.qtrUJkvm passes both frozen-binary guest
phases. ED_PROTECTION reports the expected regular-file execute bit for off,
on, and off again, clears group/other execute bits when off, and leaves the
directory searchable. The cold phase verifies the persisted off state. Exact
expected names and terminal enumeration status are checked; read-only hashes
remain unchanged and Linux zdb reports no leaks.

Scope limits: this test does not launch an executable through AROS LoadSeg or
prove every loader checks DOS protection. Source inspection of the local
AROS/rom/dos/loadseg.c shows ordinary MODE_OLDFILE opening and InternalLoadSeg,
without a direct execute-bit check; newloadseg.c delegates to LoadSeg. This is
a source-level integration concern, not a claim about the exact guest loader
binary or all shell/security paths. The handler must not block ordinary file
reads to simulate loader permission checks. Native loader integration remains
required before claiming full exec=off enforcement.
Directory/symlink semantics, devices, setuid and xattr policies are not claimed
complete by this change. The existing eight-lifetime-mount/resident-endpoint
limitation remains unchanged.

Failures retained:

- exec-policy-build-1.log: test omitted the header declaring ACE_EXECUTE;
  corrected before the successful build.
- admission-service-run.5n8F2zVw: exec checks passed, then the combined suite's
  extra mount exhausted retained endpoint slots. A dedicated fresh-guest exec
  suite avoids sharing that bounded resource with the full regression.
- admission-service-run.yEuPiLZW: write checks passed; focused cold startup
  incorrectly ran timestamp verification that depends on full-client writes.
  Focused startup now explicitly selects its own fixture via OPENZFS_EXEC_ONLY.
- admission-service-run.YjTRRMmJ: full regression passed, but a simultaneous
  next build could change the binary used by its second ISO. The runner now
  snapshots both binaries before the first boot; use the subsequent frozen
  regression evidence rather than this run for exact build provenance.
