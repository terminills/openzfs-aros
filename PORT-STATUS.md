# Port status

Experimental development snapshot; not production-qualified storage software.

## Matched native package inputs validated (2026-10-28)

The current normal shared engine, CLI, handler, Manager and selected source-built
DOS64/POSIX/stdc/stdcio now pass the matched native GUI, persistent writer/cold
reader, encrypted-root discovery and full native GPT debugger acceptance gates.
The GUI replay saved and verified a raw health response and read exact file data
after Manager closed with Wanderer still running. Its original interactive run
was incomplete; the separate fresh-boot replay is the passing evidence.
See `aros/tests/evidence/current-native-suite-20261028/gui-pass` and sibling
cold/discovery evidence, plus `native-zdb-gpt-current-crt-20261028`.

The staging validator accepts all 20 selected artifacts, their matching native
evidence, and retained platform source snapshots. Validation-only output and
the exact input-selection recipe are in
`aros/tests/evidence/package-validation-20261028`. No new distributable bundle
has been created by this validation; output-copy verification remains pending.
The shared artifact drive was below the 100-GiB allocation reserve. Complete
SDK/toolchain source packaging, broader completion auditing and physical-device
qualification remain separate. Earlier dated entries below retain the state
at their time and must not be read as the latest native-matrix result.

Subsequent staging completed into the private host directory
`openzfs-package-staging/mmake-bundle.xt1czzqv` after cleanup and destination
headroom checks. The bundle occupies 69 MiB; all 4,995 manifest entries match,
no output symlinks remain, and all seven copied platform source inventories
bind to their packaged binaries. This closes copy verification for this
development bundle. It does not close SDK/toolchain source distribution,
independent bootstrap or broader upstream feature qualification. Proof is in
`aros/tests/evidence/package-validation-20261028/staged`.

## Current CRT actual-Manager persistence (2026-10-28)

The current Manager with source-built DOS64/POSIX/stdc/stdcio passed an actual
hosted GUI snapshot submission followed by independent zdb and a separate cold
handler reader. The snapshot persisted, exact readback/read-only refusal and
degraded/restored recovery passed, and original members stayed unchanged.
Named-window screenshots, activity logs and complete input hashes are retained
in `aros/tests/evidence/current-crt-manager-20261028`. This closes the hosted
GUI check for this selected CRT set, not native public-package acceptance.

## Current-engine large-pool qualification (2026-10-28)

Fresh diagnostic frontends linked against the source/header/hash-verified normal
engine now pass the 50-TB-class sparse RAIDZ1 create/write/export/import test,
independent host zdb, and a separate cold hosted DOS-handler reader with unchanged
sparse member digests. This extends the earlier historical-fixture runtime test
to the current engine build. Fixture-only credential scoping and precise handling
of the upstream libzpool kstat notice were required; production authorization is
unchanged. Native QEMU and physical large-array qualification remain separate.
See `aros/tests/evidence/current-large-pool-20261028` for passes and failed controls.

## Native debugger and selected CRT acceptance (2026-10-28)

The current debugger with selected source-built DOS64/POSIX/stdc/stdcio passes
the isolated native QEMU labels, lifecycle, damaged-label, privileged/unprivileged
device and L2ARC endian/corruption/bounds suite. Prepared ISO libraries are now
byte-compared against selected inputs; this caught and fixed a runner map-array
replacement bug. The missing-security native attempt failed its fixture because
security.library remained boot-resident. A corrected image removing that module
while preserving all other BSP records subsequently passed native QEMU: actual
library/resident absence, EACCES device denial and byte-identical device backing.
The complete native GUI/cold/encrypted package matrix remains open. See
`aros/tests/evidence/native-zdb-current-crt-20261028`.

## Large-pool runtime requalification (2026-10-28)

The source-built DOS64/POSIX/stdc/stdcio combination passes a retained 50-TB-class
RAIDZ1 fixture in isolated hosted AROS, independent host zdb verification and a
cold DOS-handler reader with unchanged sparse member digests. This requalifies
the runtime combination using historical fixture executables, not the current
engine source or native QEMU. The large-pool runner now uses explicit runtime
and host-tool selection, external storage safeguards and complete CRT hashes.
See `aros/tests/evidence/large-pool-runtime-20261028` for both the passing run
and the rejected older fixture attempt.

## Source-rebuilt DOS64 dependency (2026-10-28)

DOS64 now builds from a complete retained source inventory without cached module
objects, with a byte-identical relocated replay. Staging requires and packages
the hash-bound source snapshot. The new candidate passes >4 GiB offset/growth
and reopen checks, full hosted writer/cold-reader service/CLI/handler tests,
and the independent hosted debugger regression. SDK headers, link archives and
toolchain remain external recorded dependencies; this is not whole-OS closure
or native QEMU acceptance. See `aros/tests/evidence/dos64-source-20261028`.

## DOS64 packaging and matched hosted runtime (2026-10-28)

Staging now binds the selected DOS64, POSIX, stdc and stdcio binaries across
all four required native acceptance runs, refusing missing/mismatched evidence.
The full hosted storage/CLI/handler writer and cold-reader suite passes with
corrected DOS64. An independent hosted debugger run passes exact labels,
lifecycle, absent-security denial and L2ARC bounds/corruption/recovery using
the same runtime set. The QEMU runners now support explicit selected CRT copies.
These hosted tests do not satisfy the native release gate; matching QEMU runs
and retained DOS64 source/rebuild packaging remain open. See
`aros/tests/evidence/dos64-package-binding-20261028`.

## Explicit DOS64 dependency requalification (2026-10-28)

The current source-built CRT set passes >4 GiB positioned I/O, close/reopen,
unchanged descriptor positions and sampled zero-filled holes when paired with
the retained corrected DOS64 library. The old hosted runtime template's DOS64
binary fails file growth near 4 GiB by replacing a past-EOF SEEK64 error with
object-too-large. This is the previously fixed AROS `3df3790e58` behavior,
not a new OpenZFS fix. Existing-file high-offset reads alone missed it.
The offset runner now requires explicit DOS64/probe/runtime selection, checks
that error distinction, supports external storage and binds all selected CRT
hashes. Packaging still needs to bind DOS64 and qualify the selected combination
in native QEMU; no general stock-runtime compatibility is claimed. See
`aros/tests/evidence/current-offset-runtime-20261028`.

## Complete report export and backward paging (2026-10-28)

Manager can export the entire retained command report through a background
worker with exclusive destination creation, and navigate previous report pages
without rerunning commands. Export uses a duplicated descriptor and positioned
reads, so closing or repositioning the pager cannot truncate the exported report.
Host fault tests and native hosted checks cover exact bytes, overwrite refusal,
pager-close lifetime and backward traversal. Fresh hosted writer/cold-reader
regressions pass. Paging remains byte-based, not Unicode/line pagination.
See `aros/tests/evidence/report-export-20261028` and
`aros/tests/evidence/report-reverse-20261028` for scope and retained limitations.

## Retained command-report pages (2026-10-28)

Oversized locally captured command output is now readable through Manager's
Next page control without rerunning the command. Actual upstream verbose pool
events were captured and paged in isolated hosted AROS; snapshot creation and
the subsequent cold-import suite still passed. A native fixture independently
checks all 131085 bytes of a failed command's report over 33 pages, three times.
Only joined local-worker metadata can open a retained report; service text and
rows are not treated as file-access instructions. The later export/backward
paging change above extends this original forward-only implementation.
See `aros/tests/evidence/command-report-pages-20261028`.

## Retained stdc source and runtime packaging (2026-10-28)

The runtime snapshot builder now rebuilds all 518 selected stdc objects,
including x86-64 math/setjmp assembly, with a retained generated locale table
and ABI headers. Relocated replay produces a byte-identical library. Two fresh
isolated hosted writer/cold-reader runs pass with this candidate; one includes
native Manager response saving and authenticated snapshot creation followed
by independent cold discovery. The stdcio default build still reproduces its
previously qualified binary. Packaging now requires retained stdc and stdcio
source snapshots and binds both binaries, alongside POSIX, to native evidence.
This is not whole-OS/toolchain source closure or native-QEMU qualification.
See `aros/engine/CRT-SOURCE-BUILDS.md` and
`aros/tests/evidence/stdc-source-20261028`.

## Native response save destination (2026-10-28)

Manager response saving uses the native file requester and refuses existing
files rather than overwriting a fixed temporary destination. It saves the
current backend response/page, including details hidden in Simple mode; full
large command reports remain separate. Actual hosted save, repeat refusal,
cancellation, continued snapshot interaction and cold import passed. Host tests
also cover interrupted/short writes and write/close failures. These do not
prove power-loss durability. See `aros/tests/evidence/hosted-response-save-20261028`.

## UI clone cold persistence (2026-10-28)

The persistent hosted GUI fixture now retains its UI-created clone instead
of deleting it before export. Fresh healthy, degraded and restored imports
verify quota, compression, mountpoint and exact origin, mount the clone through
the native handler, read hello.txt exactly, reject creation with write-protected
DOS error, and detach it. Independent host zdb also finds the clone and file.
Full pool integrity, both runtime preflights and unchanged cold-reader member
hashes pass. This extends hosted clone evidence through restart; it does not
claim physical-device durability or replace pending QEMU qualification.
See `aros/tests/evidence/hosted-clone-cold-20261028`.

## Rebuilt Manager clone UI qualification (2026-10-28)

The fresh MetaMake Manager creates a clone through its real confirmation UI
and upstream CLI worker in isolated hosted AROS. Independent post-UI queries
verify quota=32M, compression=off, mounted=no and the exact snapshot origin;
the fixture then removes the clone and its source snapshot. A separate UI
snapshot survives export and fresh read-only import, with unchanged member
hashes. Both full runtime preflights pass with source-built stdcio/POSIX.
This is current-binary hosted UI evidence, not QEMU or every UI operation.
The earlier automation/deadline failure is retained separately. See
`aros/tests/evidence/hosted-manager-clone-20261028`.

## Source-retained stdcio runtime (2026-10-28)

`build-stdcio-snapshot.py` replaces the cached-object stdcio overlay workflow
with a complete 69-translation-unit rebuild. Source, generated ABI headers,
recipe and provenance are retained; SDK headers/archives and the toolchain
remain explicit hash-bound external prerequisites. Relocated source replay
produces a byte-identical library without the original object cache.
The new binary passes both full hosted runtime preflights and the persistent
writer, CLI/handler, independent zdb, cold/degraded-reader suite. This closes
the stdcio cached-object dependency, not whole-platform source closure or
stock-runtime/QEMU qualification. See
`aros/tests/evidence/stdcio-source-20261028`.

## Packaged CLI full hosted preflight (2026-10-28)

A fresh generated MetaMake CLI build now passes the full RuntimeCheck with
the matched private stdc/stdcio/POSIX and loader-policy DOS libraries. This
includes errno, configured security, three stream teardown paths, the actual
ExecHelper subprocess, and both loader APIs refusing execute-off files.
The runner explicitly installs and hashes the selected ExecHelper; writer and
cold-reader preflight logs are separate and both are checked. The persistent
storage/CLI/handler suite, independent host zdb, cold degraded reads and
unchanged member hashes pass with these exact CLI binaries.
The initial missing-helper/default-DOS failure is retained, not suppressed.
This remains private hosted evidence, not stock SDK or QEMU qualification.
See `aros/tests/evidence/hosted-packaged-runtime-20261028`.

## Runtime teardown preflight (2026-10-28)

OpenZFSRuntimeCheck now tests explicit flush, normal return and exit with an
independent reader before parent-side flushing. The old checker could recover
and conceal missing child output. Private hosted controls reject the no-hook
POSIX library and accept the stream-flush variant with matched stdc and the
retained stdcio flag-restoration fix. SDK-installed stdcio still fails the
inherited-handle check in this hosted setup; stock-runtime acceptance remains
open. `--streams-only` deliberately reports only this narrow check.
See `aros/tests/evidence/runtime-stream-teardown-20261028`.

## Hosted full-service runtime regression (2026-10-28)

The legacy atime SA-upgrade transaction fix passes CLI and Manager dataset
upgrade/remount/exact-read tests. A later empty CLI-output failure required
restoring the POSIX stream-flush teardown hook in the source-retained runtime.
The old hosted stdc library also had a private-layout mismatch with that POSIX
build; a matching runtime eliminates the delegation-test trap. The harness
records both libraries and supports explicit private overrides.

The persistent hosted writer now seeds the bootenv marker required by the
independent cold reader. The full writer/service suite, host zdb integrity
check, cold DOS read/metadata/directory tests, degraded mirror checks and
unchanged read-only member hashes pass with the matched private runtime.
This does not establish stock nightly or physical-device qualification.
See `aros/tests/evidence/hosted-cli-stream-flush-20261028` for failures and pass.

The native Manager also creates a snapshot through its real confirmation and
authenticated worker path in isolated hosted AROS. Independent zdb and a fresh
read-only hosted import verify the exact snapshot and unchanged member hashes.
Screenshots and the preceding timed-out attempt are retained in
`aros/tests/evidence/hosted-manager-persistence-20261028`; this is one verified
UI mutation path, not a complete UI or stock-runtime qualification.

## Private hosted debugger regression (2026-10-28)

An isolated hosted AROS copy now exercises shared debugger lifecycle, exact
label output, absent-security native-path denial, synthetic populated L2ARC
logs in both byte orders, checksum rejection, oversized entry-count rejection
and healthy recovery. It exposed final buffered output being lost when the
library returned from nixmain. The library now flushes stdout/stderr before
restoring its caller's runtime; output errors cannot become success.
No physical device or QEMU ATA read is established by this hosted run.
See `aros/tests/evidence/hosted-zdb-flush-20261028` for controls and repeats.

## L2ARC debugger bounds (2026-10-28)

The debugger now rejects on-disk log-entry counts exceeding its fixed array
before traversal. A private host candidate passes 16 valid/boundary/malformed
invocations across both byte orders and verbosity levels. The changed AROS
shared library and debugger link through MetaMake and pass the isolated hosted
regression above; QEMU runtime qualification remains pending. Existing bundle
qualification still names the prior binary.
See `aros/tests/evidence/l2arc-entry-bound-20261028`.

## Native zdb build audit (2026-10-27)

All three upstream `zdb`/`libzdb` translation units now cross-compile against
the recorded private SDK. AROS maps `open64` to native `open`, guarded by a
compile-time 64-bit `off_t` assertion. This is not native debugger qualification:
the initial dependency-only link audit failed on five symbols. Linking real
client support now resolves them. An isolated eight-CPU native diagnostic
executable reads all four ISO-backed labels twice, with both zero-exit reports
matching host zdb exactly. The following shared-library implementation replaces
that diagnostic executable: version 2 adds a one-shot debugger entry and a
small `C:zdb` launcher. Both standalone and generated MetaMake builds pass
native label/lifetime tests with a 64-KiB caller stack; the library supplies
the required 4-MiB stack. Native block/MOS traversal now passes for an
ISO-backed two-member pool with a special allocation class, matching host
block accounting, dataset roots, feature checks and exact object rows.
Damaged-label tests also match host output exactly: one invalid configuration
returns 1 with three labels decoded; all four invalid configurations return 2.
Fresh healthy invocations following both errors return 0. The same eight-CPU
guest passes block/MOS inspection; original and damaged-copy hashes remain
unchanged. See `aros/tests/evidence/native-zdb-damage-20261027`.
Native `zdb -l /AROS/ata.device/0` now uses read-only Exec device access with
configured-security/root authorization, checked again for each read. An
eight-CPU QEMU run matches host labels before/after an actual UID 77 caller's
EACCES denial; its disposable ATA overlay is byte-identical to the backing
fixture. A separate guest verifies native MBR slots 0 and 2, correct extent
label reads and UID 77 denial; another verifies EACCES when security is
unconfigured. All overlays remain identical to their input disks. See
`aros/tests/evidence/native-zdb-device-20261027`. Whole-pool inspection now
binds explicitly supplied native `-p` paths read-only through teardown. An
eight-CPU guest passes full block/MOS comparison on two native MBR extents,
actual UID 77 admission denial, and a failed partial admission followed by
successful traversal with duplicate path arguments. The whole disk remains
unchanged. See `aros/tests/evidence/native-zdb-bindings-20261027`.
GPT paths now pass with the private partition-gap fix (and fail with the
stock partition library); see `aros/tests/evidence/native-zdb-gpt-20261027`.
Synthetic empty L2ARC headers also pass through ISO and authenticated native
device reads; this does not cover populated cache logs or rebuild behavior.
See `aros/tests/evidence/native-zdb-l2arc-20261027`. Missing-library denial now
passes in isolated hosted AROS with actual OpenLibrary(name, 0) failure and
no resident; the prepared QEMU/device case is still pending. Unconfigured
library denial does not substitute for that result.
Broader diagnostic/corruption coverage and
public release-bundle qualification remain unfinished. See
`aros/tests/evidence/native-zdb-pool-20261027`.
See `aros/tests/evidence/native-zdb-build-20261027`. Existing pool-integrity
results continue to use the independent **host** zdb, not this native candidate.

## Compiler input drift checks (2026-10-27)

The MetaMake engine target now includes SDK/system headers in its compiler
dependency manifest and verifies the exact 360-object source inventory.
Fresh build and eight-CPU three-boot persistent regression evidence is in
`aros/tests/evidence/compiler-inputs-20261027`. This is a post-build drift check,
not complete source/toolchain closure or immutable-build attestation; see
`aros/engine/BUILD-INPUTS.md`.

## Coordinated dependency builds (2026-10-26)

An explicit `contrib-openzfs-dependencies` target builds the selected pthread,
pinned BoringSSL, and nvpair/XDR sources into one private external directory,
with shared shell/MetaMake inputs. Fresh consumers pass an eight-CPU public
service run and a three-boot persistent mirror/handler/CLI suite, including
native PBKDF2 and nvpair tests. See
`aros/tests/evidence/dependency-coordination-20261026` for inputs, failed
attempts, and passing guest plus host-integrity evidence. This closes a manual
dependency-assembly gap, not the official SDK/runtime API compatibility or
complete public binary source-packaging requirements below.

## Current local package (2026-10-25)

**Runtime ABI boundary:** this archive is qualified against the recorded
alpha18c5/private platform, not the custom integration stdc interface that places
memory-pool inheritance at vector 527 and errno at 528. Inheritance at 527 is
present in current upstream source; errno at 528 is a local extension, not an
official nightly SDK contract. The pristine published 20261023 nightly SDK
still uses a link-library errno accessor and exposes neither of these vectors.
Its source commit has not been established. Do not install these old consumers
with that runtime. A fresh matched SDK/dependency/application rebuild and
new-baseline QEMU tests are required. `fb61d32` adds an errno accessor/POSIX
writer probe. Subsequent standalone official-SDK/runtime tests pass; that
does not qualify this older engine archive on a different runtime.

The standalone `aros/cli/errno-native-test.c` emits serial BEGIN, three case
results, and a final PASS/FAIL marker without opening storage. It is intended
for the official/custom caller-runtime compatibility matrix. A clean compile
or a matched custom-SDK run does not prove compatibility with official callers;
both cross-runtime directions require isolated native tests.

### Published nightly SDK qualification (2026-10-25)

The `qualification/official-sdk-20261025` branch additionally records bounded
tests against the pristine published 20261023 SDK and runtime:

- Native errno accessor/writer and ordinary pipe transport probes pass.
  The SDK does not provide `pipe2`; `pipe` plus `fcntl` is not claimed to
  reproduce atomic close-on-exec behavior.
- The complete 16-case POSIX environment matrix reproduces ten failures and
  six passes. Explicit/empty environment global fallback, nested execution,
  unset behavior and direct `environ` replacement remain unresolved. The
  known-defect verifier is a reproduction check, **not qualification**.
- The native Manager opens from the shell, closes through its supported
  break path, and reopens independently from Workbench without a backend.
- Runtime-check workers complete 16 missing-command lifecycles and four
  real-checker report captures. The checker still reports unmet security and
  execution-policy prerequisites; successful delivery does not turn those
  failures into passes.
- Ordinary inherited-environment command capture passes 112 sequential and
  896 concurrent cases, including exact per-caller tokens, error status,
  bounded output rejection, thread joins and temporary-file cleanup.
- Source-built nvpair/XDR and BoringSSL pass their selected native nvlist and
  PBKDF2 tests. A subsequent fresh full-engine build compiles all 248 upstream
  sources but fails service linking on `pwrite`, which the published SDK does
  not provide. This is a failed build, not a newly qualified engine.
- All 25 common upstream CLI source files compile after mapping AROS
  `fdatasync` to the stronger native `fsync` operation. A RAM-file QEMU test
  passes invalid/closed descriptor errors, data and unchanged-offset checks;
  it does not test durability. SDK-only links of `zfs`, `zpool`, and both
  startup commands fail on `pipe2` and `pwrite`. RuntimeCheck and HostIdentity
  link, but this does not qualify the complete command suite.

Evidence is under `aros/tests/evidence/official-sdk-api-audit-20261025`,
`official-environment-matrix-20261025`, `official-manager-startup-20261025`,
`official-runtime-worker-20261025`, and `official-inherited-capture-20261025`.
Dependency and failed engine-build evidence is in `official-dependencies-20261025`
and `official-engine-build-20261025`.
CLI and synchronization evidence is in `official-cli-build-20261025`.
These are isolated software tests, not new engine/pool, general SMP, or
physical-device qualification. Shared-runtime compatibility repairs remain a
separate prerequisite before rebuilding and qualifying the complete engine
against a newly accepted SDK/runtime pair.

`mmake-bundle.7ospuo7d.tar.xz` includes the corrected snapshot-local POSIX ABI
header, retained FAT/DOS/POSIX sources and current qualified engine/Manager.
Its SHA256 is `cad7e06486e17c8760b22af1fd9791fcf61df78940d633b693584ef987535846`.
Later local source changes, including dataset-used presentation and TRIM
dispatch/error-path qualification, are not implicitly included in this archive.
Matching eight-CPU GUI/Wanderer, persistent recovery and encrypted-root runs
pass the staging gates. Extracted checksums and extracted POSIX source replay
pass. See `aros/tests/evidence/posix-local-abi`. Other platform modules and
complete SDK/toolchain sources remain public-binary-release gates; this is a
local development archive, not a production or public binary release.

## Previous local packages

The local `mmake-bundle.jvc1mxmr.tar.xz` adds a pinned DOS source/catalog snapshot,
upstream licenses, recorded compiler commands and exact-byte replay. Direct
and archive-extracted replay pass; fresh Manager/Wanderer, persistent cold and
native encrypted-root QEMU suites qualify the new DOS package. See
`aros/tests/evidence/dos-source-package`. Posixc, the other boot modules and
complete SDK/toolchain source packaging remain public-binary-release gates.
This is a local development archive, not a public binary release.

Its predecessor `mmake-bundle.o6mq86dd.tar.xz` adds a hash-bound FAT source snapshot,
upstream license files and checkout-independent rebuild recipe. Both direct
and archive-extracted source replay produce the exact previously qualified FAT
binary; a fresh persistent QEMU suite passes using the replayed output. See
`aros/tests/evidence/fat-source-package`. DOS/posixc and complete SDK/toolchain
source packaging remain public-binary-release gates. This is still a local
development archive, not a public binary release.

Its predecessor `mmake-bundle.hntel_xu.tar.xz` includes checked destroyed-member
admission for add/attach/replace and separate tested FAT/DOS prerequisites.
CLI and Manager API file-backed and native-partition candidate tests pass;
fresh archive extraction passes full two-boot cold qualification. See
`aros/tests/evidence/native-reuse-package` for hashes, screenshots, exact limits
and remaining public binary source/license packaging work. This is not a
production or public binary release. Archive references below are historical.

The source now additionally validates embedded label checksums before allowing
destroyed-member reuse. Old-engine reproduction and three fresh eight-CPU
QEMU runs qualify the repair; see `aros/tests/evidence/destroyed-label-checksum`.
The refreshed `mmake-bundle.0uirh9w0.tar.xz` includes that repair and passes
exact-binary Manager, persistent cold-start, native encrypted-root and fresh
archive-extraction tests. See `aros/tests/evidence/checksum-package` for its
hash, screenshots and logs. The archive references below are historical.

`mmake-bundle.exm_ebrp.tar.xz` includes the ARC repair, native/file destroyed
member reuse, and updated Manager guidance. Exact-binary GUI, persistent cold
start, native encrypted-root and freshly extracted runtime tests pass in
eight-CPU QEMU. See `aros/tests/evidence/reuse-package` for the archive hash,
screenshots and logs. Earlier archive references below are historical.

## Encrypted root cache repair (2026-10-24)

Native encrypted pool-root import exposed an ARC assertion when cached
plaintext metadata was revisited for authentication. The source now preserves
that plaintext buffer while performing the existing MAC checks, avoiding a
second decompression/byteswap. Assertions remain enabled. Eight-CPU QEMU
native-partition and cold file-mirror regressions pass; see
`aros/tests/evidence/encrypted-root`. The local development archive
`mmake-bundle.knv27h5n.tar.xz` predates this repair and can reproduce the abort;
do not treat its earlier qualification as coverage of this encrypted-root case.

The refreshed local archive `mmake-bundle.ncbjd29s.tar.xz` includes the repair.
Its exact binaries passed the encrypted-root, Manager/Wanderer, persistent
cold-start and freshly extracted eight-CPU QEMU acceptance runs. Packaging now
requires the encrypted-root results as well as the GUI and cold evidence.
See `aros/tests/evidence/arc-package` for hashes, screenshots and retained logs.

An eight-CPU two-VM test also passes clean-export recovery of an encrypted pool
root on a native ATA partition: fresh import starts locked, mounting is denied
until key loading, exact DOS data is recovered read-only, and the entire disk
hash remains unchanged. See `aros/tests/evidence/native-encrypted-cold`. This
does not establish abrupt-power-loss or physical-media durability.

## Current public-source qualification

The current engine also passes native ATA+AHCI changed-pool recovery in
eight-CPU QEMU: read-only healthy/missing-device/restored imports, writes while
one disk is absent, restored-member resilver, then a fresh read-only exact-data
check with its surviving peer deliberately offline. Whole-disk hash checks and
independent zdb verification pass. This requires the documented private AHCI
capacity overlay, does not fix the separate two-channel ATA timeout, and is
not power-loss or physical-media qualification. See
`aros/tests/evidence/all-native-changed-cold` for the retained evidence.

The native MetaMake package now passes isolated two-CPU QEMU tests of the
shared engine, upstream `zfs`/`zpool` commands and standalone DOS handler.
This includes clone/rename, LZ4 configuration, rollback checked through mounted
data, full and incremental replication, and handler read-only enforcement.
The persistent run `public-service-run.qA8HX86o` additionally passed a fresh
guest boot, healthy and degraded mirror imports, exact file reads, unchanged
pool-member bytes, and independent host `zdb -bcc` with no leaks.
The same current native package also passed three-, four-, and five-member
RAIDZ1/2/3 suites, including fresh-boot reads with one/two/three members missing,
respectively. Host configuration inspection confirmed the actual parity.
The fresh native package additionally passes small dRAID1/2/3 configurations
with one distributed spare each, including cold degraded reads, excessive-loss
refusal and restored reads. Independent configuration checks confirm actual
dRAID parity/data/spare geometry. Subsequent public-package tests also qualify
typed distributed-spare activation and healing recovery for dRAID1/2/3.
Sequential reconstruction is exercised separately with the upstream
device_rebuild feature enabled: persisted rebuild completion, zero reported
errors, and the automatically started post-rebuild scrub are checked before
and after cold imports. The scrub must start no earlier than rebuild completion.
These are small file-backed layouts, not general hardware/topology qualification.

Mirror tests additionally cover offline/write/online resilver and replacement
with a new file-backed member. Cold imports read exact payload bytes with only
the repaired or replacement member available. See BUILDING.md for retained
run identifiers and the distinction between healing and sequential recovery.

Additional native tests qualify sparse DOS files beyond 4 GiB (including
replication and cold reads), excessive member-loss refusal followed by
restoration, and AES-256-GCM raw-key lifecycle with wrong-key denial.
Encrypted datasets survive healthy/degraded/restored cold imports. Full and
incremental raw encrypted streams transfer with unloaded keys; received data
remains inaccessible until key load, then matches the source through the DOS
handler. See BUILDING.md for individual runs and qualification limits.

These tests use newly created virtual storage and explicitly selected patched
runtime libraries. They supersede the initial startup-only qualification below,
but do not establish stock-nightly or physical-device safety. See BUILDING.md
for exact scope, inputs and evidence identifiers.

Public-package zvol qualification now includes the session transport and
resident `openzfs.device`: logical discard boundaries, Exec block I/O larger
than one transport request, repeated close/reopen, mapping ejection, and cold
healthy/degraded/restored reads on mirror and RAIDZ2. Read-only writes and
busy-client ejection are refused. FAT inside a sparse zvol additionally passes
format/write/read, DOS directory/volume enumeration and cold mirror
healthy/degraded/restored reads, using the explicitly selected combined FAT
flush/read-only overlay. The flush-only FAT runtime fails the cold disk-state
protection check; stock-nightly compatibility must not be inferred. FFS passes
the same current-package cold mirror lifecycle with its explicit private AFS
shutdown fix and controlled cancellation of expected write-protection
requesters. Native Manager remapping and mount-file save now pass actual UI
submission followed by independent device/data/file verification. Saving the
description is not formatting or mounting; desktop icon presentation remains
a separate qualification gap.

## Implemented integration

Authenticated key changes support raw/hex/passphrase conversion, PBKDF2
iterations and key-location metadata. Manager run `native-gui-run.l9Yuy70p`
confirms actual GUI submission followed by independent property and old/new
key checks. Run `public-service-run.CJJ999MO` verifies distinct load-key versus
change-key permissions, private key-file denial and revocation without changing
the stored properties. The full suite also passes with QEMU configured for
eight CPUs in `public-service-run.Pvylzwwr`; this is not a stress-duration,
guest CPU-detection or physical-SMP qualification. Evidence and input hashes
are retained under `aros/tests/evidence/key-change-auth` and
`aros/tests/evidence/key-change-smp8`.

Authenticated recursive snapshot destruction supports `defer=on` through the
upstream snapshot-destruction ioctl. Native QEMU run `public-service-run.rlEPj1HF`
checks unheld removal, held-snapshot retention and removal on release, plus
confirmation and snapshot-only restrictions. This is not a physical-device
qualification or an exhaustive delegation-policy test.
Native Manager run `native-gui-run.RFfSUBE5` additionally qualifies actual
request entry, preview, confirmation and authenticated submission. Independent
post-UI queries verify parent removal, held-child deferral and removal after
release; unrelated DOS readback and the full storage suite pass.

Manager clone requests with properties now execute through the asynchronous
worker and upstream `zfs clone -u`, without implicitly mounting the result.
Native GUI run `native-gui-run.A56TMiQN` exercises request entry, confirmation,
and submission; independent queries verify origin, quota, compression and
unmounted state before cleanup. Native run `public-service-run.PnXbHX1E`
also verifies denial for an unprivileged caller, success with explicit clone,
destination-create and property grants, and denial after those grants are
revoked. Rejected requests leave no destination dataset. This qualifies the
tested property/grant combinations, not every delegation policy or a physical
installation. See BUILDING.md for the exact cases and retained evidence.

Current-package DOS links are qualified by `public-service-run.Hp9I60Hi`:
hard links survive deletion of the original filename; relative soft links
resolve to the retained data; dangling links return object-not-found. Writer
and cold healthy/degraded/restored mirror imports read identical payloads,
and read-only mounts reject both hard- and soft-link creation. Independent
host `zdb` reports no leaks and the cold pool-member hashes remain unchanged.
This is file-backed native QEMU evidence, not physical-media qualification.

Persistent startup settings now have native muFS ENVARC evidence: the actual
startup command restores trusted ENV records, imports and mounts read-only
datasets in healthy/degraded/restored mirror cases. The refreshed Manager
also passes live pool queries, UI settings save and post-UI DOS readback.
See `aros/engine/EXEC-POLICY.md` for exact runs and platform prerequisites.

Large-file directory metadata now has current alpha18 native evidence:
legacy enumeration remains usable through size saturation, while the separate
dos64.library examination APIs return exact 64-bit sizes. QEMU checks cover
writer boot and cold healthy/degraded/restored imports, plus invalid packet
arguments. Wanderer visibly lists both a small file and a >4 GiB sparse file;
exact post-UI readback and teardown pass. See `aros/handler/README.md` for runs
and API limits. This does not qualify every desktop workflow or hardware.

Native DOS notifications now pass both message and signal delivery, initial
notifications, deletion, wait-reply coalescing, unsubscribe, and request reuse
with outstanding replies. In `native-gui-run.wdcMRy2K`, Wanderer automatically
adds and removes a directory icon in an already-open pool drawer after shell
create/delete commands. Retained screenshots document both transitions;
post-UI exact readback and the complete storage suite pass. See the handler
README for the evidence sequence and remaining desktop limitations.

The source contains the upstream OpenZFS engine, a shared AROS library/service,
native DOS handler, management UI, upstream command-line tools, startup support,
and device adapters. The UI sends backend requests rather than simulating pool
operations. Security and MuFS integration are platform dependencies.

Historical development testing includes native file access, pool lifecycle,
file-backed multi-device pools, mirror checksum repair, unrecoverable checksum
errors, degraded RAIDZ2, and recovery/packaging checks. See the component notes
for the exact scope and restrictions of individual tests. These are prior test
results, not a fresh qualification of this public snapshot.

## Remaining boundaries

Manager disk-leaf split update (2026-10-27): the topology selector no longer
rejects admitted native disk leaves as file-only. Actual old-engine comparison
refuses ENOTSUP; new authenticated Manager API succeeds. A fresh two-native-disk
writer and separate cold reader verify both resulting pools' exact DOS files,
readonly refusal and exports; reader images remain unchanged and both pools
pass independent host zdb. See `aros/tests/evidence/native-manager-split-20261027`.
Mixed-backing secondary-channel ATA timeout runs are preserved as failures,
not full-suite passes. Subsequent CLI hole/indirect admission qualification is
recorded in `aros/tests/evidence/split-topology-20261027`: the config is rebuilt
from trusted live source topology, with bounded repacking and retained leaf
admission. Actual native CLI hole, indirect-after-removal and plain split runs
pass separate readonly imports and exact DOS readback. These use guest-RAM
files and are not cold-persistence or physical-power-loss proof.
Subsequent persistent fixtures exposed an omitted-log metadata leak in the
split destination despite successful guest readback. Split now frees inherited
log space maps, metaslab arrays and flush-state objects only in the destination
MOS. Fresh hole and indirect writers plus separate cold readers pass exact DOS
readback, readonly refusal, unchanged member hashes, clean FAT, and independent
block and metadata-object checks. See `aros/tests/evidence/split-persistent-20261027`
for the failed old engine, first-candidate lock assertion, corrected build and
general public regression. This qualifies those private file-backed layouts,
not concurrent workload stress, every topology, or physical power loss.
Trailing single-log and interior mirrored-log layouts now have separate writer
and cold-reader qualification too. The trailing case reproduced a null-vdev
fault while replaying inherited log-space maps. Internal split construction
now retains trailing hole slots; invalid unrelated vdev references fail with
EINVAL. Both layouts pass unchanged cold-member hashes and independent block
and MOS-object checks. See `aros/tests/evidence/split-log-layouts-20261027`.
Mirrored special and dedup allocation classes also pass separate writer/cold
qualification, including nonzero class allocation, preserved top-vdev allocation
metadata, exact 4 KiB DOS payloads, actual shared dedup blocks, unchanged cold
members and independent integrity checks. See
`aros/tests/evidence/split-allocation-classes-20261027`. These qualify existing
production behavior; they are not new driver functionality or concurrent/power-loss
qualification.
The same layouts also pass authenticated Manager operation-14 split with GUIDs
obtained from live pool-status rows and separate cold readers. This is API/backend
coverage, not GUI click-through evidence; see that directory's `manager-api.md`.

Damaged-pool CLEAR update (2026-10-27): actual upstream `clear -nF/-F` and
`clear -nFX/-FX` pass on separately prepared damaged mirror fixtures through
explicit native admission. The extreme result also passes a separate cold
ordinary import, exact DOS readback, unchanged reader images and independent
host zdb. This is not only healthy-clear acceptance. See
`aros/tests/evidence/damaged-clear-rewind-20261027` for failed controls,
authorization checks and the remaining Manager/assessment-only/crash gaps.
Subsequent `clear-assessment-20261027` evidence closes normal/extreme
assessment-only disk equality and visible Manager assessment/report checks.
`interrupted-clear-20261027` adds one precise member-0 emulator flush cut,
fresh recovery and separate cold exact readback; it is not general physical
power-loss durability or all-boundary coverage.

Current-source refresh (2026-10-26): all 248 upstream inventory units, service
adapters and nvpair/XDR were rebuilt without old engine objects. Both the
standalone service and actual shared library/launcher passed the file/native
force suite in isolated eight-CPU QEMU. A fresh handler/device/client package
also passed persistent dRAID repair and separate cold recovery, host FAT
validation, unchanged member hashes and independent zdb checks. Shared runs
now require eight library ownership/unload/reload cycles before storage tests.
See `aros/tests/evidence/current-source-shared-20261026` for exact inputs and
scope. The compiler/SDK, private pthread/crypto/CRT and OS overlays remain
selected dependencies, not a newly qualified official or bootstrap release.

- A compatible patched AROS runtime is required; stock-nightly compatibility is
  not established for every component.
  Current large-ATA evidence: alpha18c5 with the selected DOS/posixc prerequisites
  but baseline ATA rejects READ CAPACITY(16). Fresh eight-CPU 50 TiB sparse
  runs `capacity-run.OwtdsHQK` and diagnostic `capacity-run.vdSy0a9R` fail at
  device open, before high-LBA reads. The latter reports native error 20,
  SCSI status 2, no command/data completion and sense 5/0x24/0. This is not a
  successful capacity or pool test. `aros/engine/ata-capacity.patch` supplies
  the missing command path. Private comparison overlay `ata-capacity.xe_howx1`
  changes only ATA in the baseline BSP package (other module bytes/order
  verified). Fresh eight-CPU runs `capacity-run.TKRL5E1R` (diagnostic) and
  `capacity-run.N3W6Tayv` (capacity diagnostics disabled) both pass: 50 TiB
  geometry, marked final-sector read, bounded subrange read, out-of-range and
  read-only-write denial, and separate 3 TiB high-LBA GPT enumeration. Both
  qcow2 overlays remain byte-identical. This is device-access qualification,
  not a 50 TiB pool test or hardware qualification. Captures are in
  `aros/tests/evidence/current-ata-capacity`. The private builder uses the
  selected build's remaining ATA objects, not a clean full-driver rebuild.
  Subsequent command-boundary qualification exposed three companion defects:
  wrong-direction requests were accepted, and legal zero/short allocation
  lengths were rejected (`capacity-run.9DrpPME5`). Commit `119d0e1` checks
  read direction and copies only the requested response length, capped at 32
  bytes. Fresh eight-CPU runs `capacity-run.9WBHwAUV` and
  `capacity-run.O3l2jVcH` (capacity diagnostics disabled) pass all ten command
  cases plus the same 50 TiB/high-LBA/GPT checks with unchanged disk hashes.
  Cases cover malformed CDBs, service action, PMI/LBA, direction, null/short
  buffers, zero/short/oversized allocations and output guards. Failed and
  passing captures are retained in `aros/tests/evidence/ata-capacity-boundaries`.
  The optional `build-ata-capacity-overlay.py --full-source` mode subsequently
  rebuilt all 16 driver objects, including module entry objects, without
  consuming old `.o` files. Overlay `ata-capacity.tuxzwxmw` passed the same
  ten boundary cases, 50 TiB reads and 3 TiB GPT enumeration in fresh eight-CPU
  QEMU run `capacity-run.hk6pisWP`, with capacity diagnostics disabled and
  unchanged disk hashes. Evidence is in the `full-source` subdirectory of
  `ata-capacity-boundaries`. Selected generated AROS sources/headers and SDK
  libraries remain prerequisites; this is not a standalone AROS source build.
  The same source-rebuilt ATA overlay also passes the current eight-CPU
  public-service acceptance workload (`public-service-run.U0sLqzCA`): native
  partition discovery, handler writes/reads and read-only reimport, encrypted
  native retention, conflicting-label rejection/restoration, and file-backed
  mirror lifecycle. This supplements raw-capacity qualification with actual
  pool/handler operations; it does not add physical-device evidence. Captures
  are in `ata-capacity-boundaries/full-source-service`.
  Those capacity-only overlays do not qualify flush durability: the selected
  ATA source still acknowledged CMD_UPDATE without sending a cache flush.
  The explicit `--full-source --flush` build includes the existing flush
  companion. Eight-CPU five-boot mirror run `qemu-run.x15GhcXn` passes with
  this overlay, including 24 flush commands per virtual disk during writable
  reopen, unchanged read-only images, and independent zdb leak checks.
- Internal ZPL same-parent directory exchange now passes native tests for two
  sibling directories and mixed file/directory swaps, including identities,
  parents, link counts and restoration before cold imports. Cross-parent
  directory exchange also passes the eight-CPU diagnostic matrix, with a
  mount-private cross-parent rename mutex and both ancestry checks. Evidence and limitations
  are recorded in `aros/zpl/README.md` and `aros/tests/evidence/sibling-exchange`.
  Exchange intent-log replay is now enabled on AROS, correcting a Linux-only
  upstream callback guard that clean-export tests did not exercise. Native
  record dispatch (including byte-swapped headers), private VM power loss,
  and a second shutdown after partial replay pass with exact exchanged payloads
  and independent leak checks. See `aros/tests/evidence/exchange-replay`.
  These are controlled two-file exchange cases, not all topology/record or
  physical power-loss qualification. Ordinary rename and exchange also reject
  tested malformed parent cycles; see `ordinary-rename-cycle` and
  `exchange-ancestry-errors` in the same evidence directory.
  Internal whiteout rename now validates native attributes and passes cold
  marker/payload checks plus interrupted ZIL replay in isolated QEMU. See
  `aros/tests/evidence/whiteout-rename`. This does not add overlay filesystem
  semantics, a DOS whiteout command, or arbitrary special-device I/O.
- Physical storage flush, disconnect, and abrupt power-loss behavior require
  hardware qualification. Virtual-disk tests do not establish these guarantees.
  Current CLI clear now shares Manager's native identity/readmission checks.
  Eight-CPU QEMU tests verify idle virtual USB total-loss recovery, swapped
  member refusal with unchanged disks, and in-flight write interruption.
  All 57 acknowledged 64-KiB writes survive CLI clear and two readonly imports
  in a separate guest with unchanged backing/overlay hashes. Evidence is in
  `aros/tests/evidence/cli-clear-hotplug-20261026` and
  `aros/tests/evidence/cli-clear-inflight-20261026`. Unacknowledged tail data,
  physical durability and damaged-pool clear-with-rewind recovery are not
  qualified. The native CLEAR transport now forwards validated upstream
  rewind policies, with mounted-pool exclusion and retained-identity checks.
  Fresh eight-CPU file and partition/discovery QEMU runs pass real CLI
  `clear -nF`, `-F`, `-nFX`, and `-FX` on healthy unmounted pools; malformed,
  unauthorized, mounted and readonly requests are refused. AROS libzfs no
  longer decodes an absent recovery response after admission refusal.
  See `aros/tests/evidence/clear-rewind-policy-20261026`. Healthy SPA loads
  do not exercise selection of an older transaction group; that damaged-clear
  recovery qualification remains open. Manager now routes explicit recent or
  extreme clear recovery/assessment options through the upstream command worker,
  with confirmation and distinct loss warnings. Healthy unmounted execution,
  mounted refusal, and native UI preview/confirmation/error presentation pass;
  see `aros/tests/evidence/manager-clear-recovery-20261026`.
  Separately, a real damaged-pool readonly `import -FX` test now passes:
  both mirror members have all three newest MOS-root copies corrupted,
  ordinary import fails, and extreme readonly recovery exposes exact file
  contents through the native DOS handler. Two cycles leave backing and
  overlay hashes unchanged. Host zdb independently rejects the latest state
  and verifies an older TXG with no leaks. See
  `aros/tests/evidence/damaged-import-rewind-20261026`. The subsequent
  writable `import -FX` test also passes: new DOS file flushed, exported,
  then read alongside the original file by ordinary readonly import in a
  separate eight-CPU guest. Host zdb verifies the repaired current state
  with no leaks; cold backing/overlay hashes are unchanged. This is one
  mirror/MOS-root corruption fixture, not all rewind modes, topologies,
  interrupted recovery or physical durability. The Manager operation-7 API
  now forwards recent/extreme transaction-recovery policy to SPA, preserving
  authorization, read-only and expected-identity guards. Extreme recovery
  passes exact DOS read/write and a separate ordinary-import cold reader.
  Actual native Manager draft/preview tests verify mode selection, round-trip
  retention and distinct read-only/writable loss warnings; no GUI import was
  submitted. See `damaged-import-rewind-20261026/manager-api.md` for scope.
  A separately prepared recent-root fixture now also passes non-extreme
  recovery through both Manager API and actual `zpool import -F`, including
  read-only previews, writable recovery and exact data in an ordinary-import
  separate cold guest. The original fixture still refuses normal recovery;
  it is not silently promoted to extreme mode. See `recent-normal.md` and
  `normal-window.md` in the same evidence directory for the discriminator.
  Actual native CLI `zpool import -nF` and `-nFX` recovery assessments now
  report recoverability without leaving a pool imported; both complete guest
  disks remain byte-identical. Writable upstream device probes can rewrite
  identical padding, so this is not zero-write-I/O proof. See
  `aros/tests/evidence/damaged-import-rewind-20261026/assessment.md`.
  Native Manager/API now offers explicit read-only recovery assessment:
  structured CURRENT/RECOVERABLE results, no pool left imported on success,
  normal/extreme/healthy mirror tests and unchanged image hashes. See
  `aros/tests/evidence/damaged-import-rewind-20261026/manager-assessment.md`
  for tested authorization, identity, UI and error-path boundaries.
  One interrupted writable-recovery boundary is now qualified: a host-side
  block-debug cut at member 0's flush boundary leaves changed guest-visible
  bytes before import returns. A new guest recovers those disks and a third
  ordinary-import guest reads the original and post-recovery files exactly.
  This uses the explicit flush-capable ATA overlay, not baseline ATA. See
  `interrupted.md`; other cut points and physical durability remain open.
- Not every upstream feature or topology has end-to-end AROS validation.
- Native and file-backed pool creation accept blank extents or consistently labeled destroyed
  members (all four checksummed labels must agree on destroyed state and pool/member GUIDs).
  Explicit confirmed force also accepts consistently labeled exported members
  for create/add/attach/replace. CLI and Manager typed-API paths now pass
  file-backed and virtual ATA partition candidate tests, including active and
  readonly-imported refusal, resilvering where applicable, and exact data after
  readonly reimport. See `aros/tests/force-create-qualification.md` and its
  retained failure/passing evidence. This uses private CRT/pthread dependencies;
  it is not stock SDK or physical-device qualification. Arbitrary nonzero media
  and potentially-active labels remain outside the implemented force policy.
  Add/attach/replace now use the same checked destroyed-member admission as
  create. CLI and Manager API file-backed lifecycles pass. Four isolated QEMU
  runs also qualify native partition candidates for all three operations in
  mixed native/file pools; see `aros/tests/evidence/native-member-changes`.
  All-native ATA+AHCI changed-pool healthy cold recovery now passes with the
  separately selected AHCI capacity overlay, unchanged whole-disk hashes and
  independent zdb checks. Degraded/restored all-native cold recovery and
  degraded writes followed by healed-member cold reads also pass with that
  overlay; the retained healing fixture intentionally offlines the peer for
  the final read verification.
  The two-channel ATA variant produces I/O timeouts with both one and eight
  CPUs and is not qualified; see `aros/tests/evidence/all-native-changed-cold`.
  The actual changed file-backed two-mirror
  fixture now passes separate-VM healthy/degraded/restored read-only recovery,
  unchanged full-member hashes and independent zdb checks, using the additional
  private FAT BPB-boundary fix. See `aros/tests/evidence/changed-pool-cold`,
  `aros/tests/evidence/fat-bpb-bound`, `aros/tests/evidence/destroyed-member-changes`,
  `aros/tests/evidence/native-destroyed-reuse` and
  `aros/tests/evidence/file-destroyed-reuse` for runtime coverage.
  File-backed QEMU tests also verify refusal by both entry points for each
  corrupt or conflicting member label, unchanged complete members after
  refusal, and successful recreation after restoring the original labels.
  Coverage correction (2026-10-24): earlier conflicting-label fixtures copied
  the donor's first label to every position, so positions 1–3 could be rejected
  by the offset checksum before the identity check. The corrected file and
  native fixtures copy each donor label to its corresponding position. Fresh
  eight-CPU QEMU run `public-service-run.YjUPhqzi` passed all four positions in
  both lanes, including refusal, unchanged backing data and restoration.
  Package qualification now requires explicit same-position markers; old logs
  do not prove this stronger coverage. Production admission policy is unchanged.
- TRIM/discard is not implemented for AROS file or native-device backings;
  requests report unsupported rather than claiming successful discard.
- This does not provide an NFS/SMB client or server for browsing a remote ZFS
  server automatically.
- File-backed shared `mmap` is not provided by AROS; the port must use supported
  native I/O and explicit ownership rather than assume Linux memory semantics.
- Reproducible dependency bootstrap and wider independent testing remain work
  for contributors. No prebuilt binaries or test-disk images are included.
  Private source-build helpers now cover pthread and BoringSSL crypto as well
  as nvpair/XDR; matching AROS platform sources, SDK and overlays must still be
  selected explicitly. Crypto no longer requires Chromium's prebuilt archive.

Use `ENV:` for active settings and `ENVARC:` for persistent settings. Preserve
backups outside experimental pools, and never assume compatibility with the
older private AROS ZFS on-disk format.

## Public snapshot validation, 2026-10-21

The native Manager compiled and linked from initial public source `b3ecbadd`
using an explicitly configured x86-64 SDK and matching private pthread archive.
The resulting executable SHA-256 is
`0798042fe8f867d1721d613c7a61de3711089c909d1e26849395f1fbd47e0506`.

A separate isolated hosted test failed before the PC/SMP-linked Manager opened:
AROS reported a bus/address-error alert in `Exec_43_RemHead`, with a null-address
trap. The same binary also failed in a second historical hosted runtime.

Rebuilding the same public source with the hosted SDK, keeping the compiler,
pthread archive, and second runtime unchanged, produced a Manager that opened
and displayed the expected service-unavailable response. The older qualified
Manager also opened in that runtime. This isolates the observed failure to the
SDK/link configuration; it is not evidence of an application-source regression.
The precise low-level ABI mismatch has not been traced. The hosted executable
SHA-256 is `adb58f9246b22645e2197dfff0207c8b8ec56c41ab9383a2ac79745ed0ce67f7`.

The build helper now requires an explicit SDK. This hosted startup check does
not qualify the PC/SMP binary, service operations, or filesystem behavior.

The test used a private runtime copy, no storage service, and no physical disks.
Source syntax checks covered 99 shell scripts and 16 Python files; four metadata
unit tests passed. These checks are separate from native runtime acceptance.
