# Upstream import integration: incomplete (2026-09-22)

## Hosted handler descriptor refresh (2026-09-30)

`source-engine.v6hMwWvC/diskinfo-production` supersedes the previous hosted
production binary with the valid DOS startup device-name BSTR fix. It is a
matching-SDK full dispatcher build with test modes disabled. Authenticated
persistent mirror regression `hosted-security-test.ZSQXDlfk` exits zero:
writer/export, independent zdb, separate cold readonly DOS validation,
degraded assign import, and unchanged member hashes after the reader.
Logs: `diskinfo-hosted-production-build.log` and
`diskinfo-hosted-production-test.log`; evidence registered to the development test archive.
Private security.library and matching stdcio remain explicit prerequisites.
This does not establish a stock nightly, physical durability or remote sharing.

## Current production hosted refresh (2026-09-30)

`source-engine.v6hMwWvC/current-production` rebuilds the native adapters
and full management dispatcher against the matching hosted SDK and existing
upstream engine objects. Provenance records `service_test_modes=0` and
`full_dispatcher=1`; this is not the legacy diagnostic service harness.
Build log: `hosted-current-production-build.log` (successful, with warnings).

`hosted-security-test.CBHeJtXq` passes authenticated persistent mirror
creation, DOS writes/readback, busy/duplicate mount rejection and export.
After stopping that isolated writer, independent host zdb passes; a new
hosted process imports read-only, verifies exact DOS reads and write refusal,
then verifies missing-member degraded import through a native assign and
restores the member. Both original member hashes match after that reader.
Runner exit is zero (`hosted-current-production-test.log`), and evidence is
registered to the development test archive. This supersedes the older hosted service binary as
a tested baseline, not the preserved historical test results.

The private hosted security and ABI-matched stdcio overlays remain explicit
prerequisites. This is neither a clean stock-nightly qualification nor
physical durability, server sharing or a 50 TB capacity test. No DUT or
user hosted session was accessed.

## Writable degraded imports (current)

Authenticated explicit file imports now permit writable degraded pools when
upstream finds sufficient redundancy. The default remains read-only; writable
mode requires explicit readonly=off. No force flag, automatic path search,
creation of absent files, or fallback to a different mode was introduced.

Three-boot native tests create a real upstream pool, withhold members, import
writable and write/flush/read a new 262144-byte DOS file, then reboot and import
read-only to verify every byte and directory enumeration. Absent members stay
byte-identical, surviving members change after writes and remain byte-identical
during the final read-only boot. Independent zdb uses only surviving extracted
members and passes checksum/leak traversal.

| Layout | Absent members | Three-boot evidence suffix |
| --- | ---: | --- |
| Mirror | 1 of 2 | DzSAD1Hr |
| RAIDZ1 | 1 of 3 | suJOpJJy |
| RAIDZ2 | 2 of 4 | Htg0yorh |
| RAIDZ3 | 3 of 5 | ipzdEB27 |

Regression evidence: read-only degraded mirror rUeCL4lV; healthy read-only
mirror MGUzvFOl; RAIDZ3 beyond tolerance oOd2T6cq refuses BOTH writable and
read-only imports, leaves no pool, releases backing locks and changes no member.
The five-layout RAM creation/import/refusal suite OZf77rvC also passes.
All are private QEMU tests, not independent physical-drive failure tests.

Preserved failures IjyiHD4x and qmDJNcMC returned EBUSY. Failure-only diagnostics
localized the latter to pin-files. Native FAT writable opens require exclusive
locks and conflicted with our retained shared file locks. Import validation
now reads under shared identity locks; after label discovery, writable import
pins every parent before releasing file locks for upstream writable opens.
Read-only imports retain file locks. This handoff is not atomic against outside
rename/replacement: dedicated backing files and no external writers/renames
are required. Parent locks pin directories, not absent/present file identities.
Exact per-pool path admission remains in force through import, reopen and export.

UI run native-gui-run.yIKgRtM0/02-import-editor.png verifies the updated
read-only-default/write-opt-in wording fits the native editor. This visual-only
guest intentionally has no service; backend evidence is the separate matrix.
Native raw-mirror online/offline and repaired-member recovery evidence is in
DEVICE-CONTROLS.md. Replacement, arbitrary device discovery, crash durability
and complete production lifecycle support are still incomplete.

## Earlier read-only recovery milestone (historical)

Beyond-redundancy cold refusal is now verified, separately from successful
degraded recovery. Mirror missing 2/2 (WMEnCuZN), RAIDZ1 missing 2/3 (idvMzYcR),
RAIDZ2 missing 3/4 (OxZbInfi), and RAIDZ3 missing 4/5 (ssWQgnyN) all refuse
read-only import with ENOENT from discovery. A subsequent pool.status query
also reports ENOENT with no rows, and Inhibit of the private backing volume
succeeds, proving retained file/parent locks were released. Every original
member remains byte-identical, with all-member host zdb/no-leaks verification.
All runners exit zero. These PASS results mean correct refusal, not recovery.

Preserved l9LqNjf4: RAIDZ1 correctly refused but the first test expected ENXIO
from final spa_import; discovery instead returned ENOENT before that stage.
The assertion now matches the observed selector contract and still separately
checks namespace absence, lock release and unchanged contents. No production
failure path was relaxed to pass this test. Read-only degradation beyond
redundancy remains unavailable, as expected; writable recovery and resilvering
were still unimplemented at that milestone; see the current sections above.

Read-only degraded file import now passes cold native tests. An explicitly
absent member is admitted only after ERROR_OBJECT_NOT_FOUND, with an existing
native parent directory locked for the import lifetime. Its exact caller
spelling is retained because discovery cannot canonicalize an absent label
path. No sibling scan, file creation, force import or write-mode fallback.
Existing files still receive identity/alias/type/size checks; all vdev opens
remain constrained by the per-pool explicit path set. Dedicated backing files
and no external writers remain required; parent locks are not exclusive writer
locks or identities for absent files. Writable degraded admission was then refused.

| Cold recovery layout | Absent members | Evidence suffix |
| --- | ---: | --- |
| Mirror | 1 of 2 | NLJ7gTRE |
| RAIDZ1 | 1 of 3 | tFAsXQsv |
| RAIDZ2 | 2 of 4 | 7cWo96v9 |
| RAIDZ3 | 3 of 5 | mlrooPGy |

Each guest confirms DEGRADED pool status and exact UNAVAIL member count,
mounts read-only, reads the exact ZPL file, enumerates it, rejects writes and
mounted export, then unmounts/exports and releases backing locks. The RAIDZ
tests additionally confirm writable degraded import is refused first. Healthy
control w3ZuK90a passes ONLINE status. All runs exit zero. Every member stays
byte-identical. Withheld members are renamed only in the private FAT image
between boots; the guest never scans their replacement names. Host zdb uses
all extracted original members, including withheld ones, to verify unchanged
pool integrity; it is NOT evidence of degraded zdb reconstruction. Native
degraded readback is the reconstruction evidence. This does not establish
resilvering, sustained workloads, failure beyond redundancy, writable degraded
recovery, or physical-device behavior.

Regression admission-service-run.LmWkcJcT passes all five existing RAM creation
and import/refusal layouts. Failed bUJ6614M is retained: parent canonicalization
changed volume spelling for an absent label path, so strict path checks
correctly rejected it with EACCES. Exact caller spelling fixes that mismatch
without case-folding arbitrary label paths or relaxing the path boundary.

Cold multi-file API matrix (healthy pools): native creation in one guest,
fresh-guest authenticated read-only import, DOS exact file read/enumeration,
write denial, mounted export refusal, clean unmount/export, all member bytes
unchanged, independent Linux zdb -bcc/no leaks, runner exit zero:

| Layout | Members | Evidence directory suffix |
| --- | ---: | --- |
| Mirror | 2 | XQNrmRRr |
| RAIDZ1 | 3 | 6o5K2Nft |
| RAIDZ2 | 4 | LrHfl4Cy |
| RAIDZ3 | 5 | on2Mg64T |

All are under build-aros/file-backing-run.*. These are dedicated 64 MiB
member files inside one disposable FAT backing image, not independent physical
disks. Missing-device recovery, reconstruction, resilvering and crash durability
are not established by healthy-pool readback. The fixed payload text still says
two-device mirror; it is an exact fixture byte string, not topology evidence.
Creation logs and cold-layout.txt identify the actual layout/member count.

Runner accepts AROS_TEST_COLD_LAYOUT=mirror/raidz1/raidz2/raidz3, snapshots
executables, generates matching mount geometry, extracts and compares every
member, then supplies every member to the independent verifier. Failed run
14LIK04L retained: enlarged FAT image still used the old 256 MiB mount bounds,
so the guest could not open the first file before spa_create. Corrected bounds
pass; this was not a RAID failure. Explicit fixture FAIL messages now terminate
the runner without waiting for its deadline.

Execution-property gap update: see EXEC-POLICY.md for the newly implemented
DSL callback, authenticated setter, native access check and effective DOS
protection mapping. UeB15a8j passes live changes and cold persistence. Older
exec=off pending notes below are historical; loader end-to-end verification
and other mount-property policies remain incomplete.

Current evidence supersedes the historical pending notes below:

- Authenticated typed file import is committed in 4b9e1d3; native backing
  locks and per-pool open-path admission are retained through export/shutdown.
  Shared native locks do not exclude external writers: dedicated files remain
  a requirement, and generic physical-device import is not implemented.
- Full existing service write/cold-reopen regression passed in
  admission-service-run.1NhP09ds (typed-import-service-regression.log).
- Native import editor is committed in 9819492. Private GUI run lb79ifSP
  passed create/export/read-only import/export/writable import, then mounted
  the dataset through the handler and displayed CreatedByManager.txt in
  Wanderer. Exact native readback, zero pool errors and clean exports passed.
  These members were RAM files, not a cold-import API test.
- Rebuilt editor visual cleanup passed in native-gui-run.FQl9B7sX, captures
  02-import-request.png and 03-import-editor.png. This deliberately no-service
  run verifies presentation and the unavailable-service response only.
  Both private GUI guests are stopped; neither used the DUT or shared hosted
  session. Protocol details and evidence boundaries are in ../ui/PROTOCOL.md.

Cold authenticated API integration now passes in file-backing-run.B01IdtF9
(cold-api-test-3.log): fresh engine without saved configuration or preimport,
root-authenticated typed import of two persistent files, read-only native mount,
exact hello.txt contents, ExNext enumeration, write refusal, EBUSY export while
mounted, unmount/export and successful Inhibit of the backing volume. Both
member files remain byte-identical to the first guest's exported pool and
independent Linux zdb -bcc reports no leaks. This is native cold DOS/API proof,
not a new cold GUI screenshot or crash-durability proof.

Preserved failed expectations: TRaUzbB5 passed import/read/enumeration but
export returned EROFS because upstream spa_export requires SPA_MODE_WRITE on
the global engine, even for a read-only pool. COLD harness now runs a
write-capable engine with the pool explicitly readonly=on. GThBD6dw then
passed export but expected service termination incorrectly: current DOS
retirement intentionally keeps the service resident after a mount. The final
test checks that contract and proves backing locks are released separately.
General service unload/retired-endpoint reclamation remains incomplete.

Duplicate-name ambiguity and broader import options remain unverified.
This is not a completed OpenZFS port.

Authenticated file import candidate: admission-service-run.nPVfUQU1 passes
native separate-client/service requests for stripe, mirror and RAIDZ1/2/3:
create/export, alias refusal, read-only import, duplicate-live-name refusal,
export, writable import and export. The mirror additionally mounts the
imported dataset, reads its original DOS payload, rejects writes read-only,
rejects export while mounted, then writes/reads a new file after writable
import. Non-root import returns EPERM. Baseline disk hashes remain unchanged.
This run uses disposable RAM member files, not cold-persistence evidence.
Earlier kiCAEzUh passes the import/export API sequence without mount extensions.

API candidate: operation 7 through authenticated admission, target pool name,
topology.device_count/paths only (no groups/ashift), empty argument,
readonly=on/off, flags exactly CONFIRMED. Native backing locks and copied path
admission remain until successful export or engine shutdown. Alias checks use
SameLock. The legacy message port rejects this typed request; it cannot bypass
native sender identity. The old saved-XDR fixture route is retained separately.
UI editor wiring and full existing service regression remain pending.

Native open-path enforcement: file-backing-run.gwDhIyXs (build 14, test 12)
passes an actual spa_tryimport with only one of the two fixture paths admitted:
the vdev worker accepts the allowed path and rejects the other. A duplicate
registration for that GUID returns EBUSY. The test then admits both paths and
passes cold import, active-pool exclusion, authenticated DOS access, unchanged
member comparison and Linux zdb. Earlier 5x6NGTtv is the full-admission control
(four accepted native opens, zero rejected).

The per-pool registry copies paths and is checked at each AROS vdev-file open
(including the userspace disk-ops alias), before stat/open and on reopen. It
uses spa_config_guid so paths reloaded from MOS do not escape the registered
path set. Refresh holds registration until spa_tryimport returns; the cold
test holds it through actual import and kernel shutdown. Nonregistered legacy
fixture pools retain existing behavior. Registration is NOT file identity,
exclusive ownership, an access-control identity, or a public import API.
Public service wiring must retain native identity locks and this registration
until pool export; it remains pending. No hardware/device admission added.

Explicit label-path admission: file-backing-run.hL35L8Mn (build 12, test 10)
passes native tests rejecting an unlisted child/spare/cache path before
spa_tryimport, then the complete cold discovery/authenticated DOS/active-pool
suite and unchanged-member/zdb checks. oz_aros_config_ops now requires a
caller-owned oz_import_paths context; missing context is rejected. Canonical
paths must be supplied. Prior Pjx3Kt9V passes negative checks but rejects the
real pool because caller OZFILES differs from canonical Ozfiles; realpath on
explicit inputs fixes this without case-insensitive comparison.

This does NOT yet constrain paths recovered inside spa_tryimport from the
MOS, concurrent writers or native backing identity/lifetime. Public import
still must not claim an arbitrary untrusted pool is confined by this check.

Active-pool exclusion: file-backing-run.GWdF5snj (build 10, test 8) reruns the
complete cold authenticated DOS suite and adds a second upstream discovery
after spa_import. It returns ENOENT with no configuration for the already
active pool, as required. Exact payload, unchanged members and zdb all pass.
Remaining import work: duplicate-name ambiguity coverage, validation of every
label-derived backing path before spa_tryimport, and authenticated management
API integration replacing the saved-XDR-only import route. No generic device
enumeration or production import safety claim is made by these fixture tests.

Latest verified cold DOS result: file-backing-run.SjEuQI1D (build 9, test 7)
passes both fixture and authenticated DOS suites after fresh guest label
discovery. Exact/nested reads, directory ExAll levels/filter/continuations,
lock conflicts and conversion, relative opens, MuFS queries, logout denial,
retained endpoints and remount all pass. Both member images remain identical;
independent Linux zdb verifies checksums and reports No leaks. This is native
handler evidence, not a visual Wanderer capture or a complete public import API.

## Cold regular-file mirror discovery passes

file-backing-run.F8FaFIvX completed two separate isolated QEMU boots. Native
AROS created an upstream mirror on two preprovisioned blank 64 MiB regular
files in a new private FAT disk image, wrote the DMU payload and ZPL fixture,
exported and shut down the engine, then cleanly detached OZBACK:. The second
guest rediscovered the pool from upstream labels, without a saved config,
imported read-only and compared all 16384 payload bytes. Both extracted member
files remained byte-identical between boots. Independent Linux upstream zdb
-bcc verified checksums and reported No leaks (linux-zdb.log in that directory).
This is clean-detached persistence, NOT power-loss or fsync durability.

Retained backing failures: A42mL2I1 lost the newly written proof at immediate
guest exit despite Flush/Close; clean Inhibit is needed to flush the FAT volume
cache. BYwPSn0u and xiDqyfw3 failed Inhibit; using distinct device OZBACK and
volume OZFILES names resolved ERROR_DEVICE_NOT_MOUNTED. Q2rKtlWy passed the
small proof's two-boot test and independent host extraction. Earlier iQR2cocP
and pB2sfUXV failed startup/mount syntax and are not engine results.

NnMKa0Zi failed native FAT extension before kernel_init/spa_create. The second
64 MiB file exhausted a 256 MiB image; read-only fsck found 32768 unreferenced
clusters (128 MiB), retained in offline-fat-check.log. No repair was performed.
The passing test provisions blank files offline, then AROS verifies their full
size and blank contents before use. Native FAT extension and volume durability
remain unresolved dependencies; this test does not claim either is fixed.

Runner: test-qemu-file-backing.sh with AROS_TEST_COLD_DISCOVERY_BINARY pointing
to the discovery-enabled pool test. No physical disks, network, DUT or shared
hosted runtime. Cold native DOS directory/file access is the next extension.

Native DOS extension candidate: file-backing-run.ggSS55jK passes cold label
discovery/import and raw payload compare, then fails the existing handler
suite with EIO. The same-drain packet/lock check passes; failure precedes the
client's lock/enumeration diagnostic. dos-handler-test now exposes its
dataset-independent suite separately from the hardcoded restricted fixture.
Build 6 adds read-boundary diagnostics; runtime remains unverified until its
new run completes. Do not report cold native DOS browsing as passed.

Follow-up failures retained: 4RN2px1M reaches ACL preflight, not the read-boundary
checks. efKgGcoI shows execute allowed (0), write denied (EROFS), invalid rights
rejected (EINVAL). The native fixture deliberately has 0777/all-rights ACLs,
unlike the older externally generated fixture. The shared test now takes an
explicit execute expectation; the older fixture retains EACCES. The attempted
exec=off workaround was removed: dataset execution policy is not yet wired
into the mount bridge and must be implemented separately, not claimed fixed.
mEvuA6RM then passes the fixture-mode DOS read/enumeration/lock/remount suite,
but the authenticated client's expected denial fails because it inherits the
discovery harness's root login. Build 9 drops that test login before client
creation. Full authenticated cold-suite completion is pending that rerun.

## Earlier native discovery runtime: RAM-file mirror passes

discovery-run.8vqy6dQs is a completed isolated two-CPU QEMU run (exit 0): two
64 MiB RAM file vdevs, upstream mirror creation, 16 KiB DMU payload plus ZPL
fixture creation, export, engine shutdown, free exported configuration, engine
restart read-only, upstream zpool_find_config() label discovery, spa_import(),
and exact payload comparison. The read-only export refusal is checked too.
No export.nvlist was saved or consumed. Build used the coherent ADT pthread
snapshot s3r1CHla, matching headers and libnvpair-adt-consistent.a. Final run
disabled import tracing. RAM survives engine restart, not guest power loss;
this is explicitly NOT cold persistence or native-device discovery evidence.

Actual AROS callbacks (oz_aros_config_ops) use spa_tryimport and the real
engine namespace/GUIDs. They do not query Linux /dev/zfs or claim all pools
inactive. Upstream's separate libzpool_config_ops remains in the engine;
callers must select the AROS table.

Port correction: native CRT __oflags2amode rejects O_EXCL without O_CREAT.
Upstream's post-label multipath probe used that combination and discarded both
valid regular-file members. The AROS branch now checks regular-file type and
uses a read-only open. It does NOT promise exclusive device ownership, admit
block devices, or relax ownership policy for a public import API.

Failure history (all under build-aros):
- discovery-test-build.log: missing native pread/pwrite prototypes; no run.
- discovery-test-build-2.log: duplicate libzpool_config_ops; no run. Corrected
  to explicitly selected oz_aros_config_ops, preserving upstream's table.
- discovery-run.I7X8Rqy0: create/write/export pass; discovery finds no pool.
- MjbiH7ul: direct upstream reads find four labels on each member.
- 1QPHE4lr: worker path/stat/open/read also succeed; post-label O_EXCL probe
  identified by source inspection as incompatible with the native CRT.
- cWPKFToD: after the open fix, upstream spa_tryimport loads the pool; test
  still fails selecting returned config.
- t5aZxWuN: returned list contains correct name/type. Test incorrectly used
  nvlist_lookup_nvlist on a non-unique list. Replaced with upstream selector,
  which iterates and checks duplicate-name ambiguity.
- 8vqy6dQs: final discovery, import and payload readback PASS.

Runner: test-qemu-discovery.sh, opt-in test build AROS_TEST_DISCOVERY=1 and
AROS_IMPORT_DISCOVERY_LINK=1. It requires a marked private security fixture,
attaches only its read-only ISO, has no networking or host disks, stores inputs,
source, serial and QEMU logs, and terminates only its owned guest.

Next: cold file-vdev discovery/payload verification with disk images, runtime
active-pool exclusion/ambiguity coverage, then authenticated public import/UI
wiring and native backing-device identity policy. No generic import claim yet.

## Current result: consistent ADT build passes both runtime suites

The preserved passing service hash was rechecked against sJPEkFrs and rerun in
admission-service-run.1WTiBbQh: full write/cold-reopen regression PASS. DWARF in
pthread-native-current.o identifies its source as aros-adt-src/compiler/pthread,
not the other AROS tree used by the initial private archive. ADT's ThreadInfo
includes adopted-task state and a 40-byte pthread_attr_t (CPU affinity), versus
the other tree's 32-byte attribute. ADT also implements foreign-task adoption.
This source-provenance difference was missed by the first consistent rebuild.

Complete ADT snapshot archive: pthread-consistent.s3r1CHla. Its pthread headers
and the ADT dos/dostags.h (NP_Affinity) are private, hashed inputs. The initial
ADT attempt 1zGQDzME failed compilation on the absent SDK NP_Affinity definition;
it was not run. No shared SDK or pthread source was edited.

Consumers were rebuilt against the snapshot headers: 178 engine units and 70
dependency units compiled, followed by ZPL/adapters and a separate nvpair
archive. Service/client candidates are in build-aros/adt-consistent-service.
Native runtime evidence:

- import-thread-run.KGE4VnrJ: 8 rounds, 1024 exact jobs, actual upstream tpool
  suspend/resume/wait/destroy and cleanup checks PASS.
- admission-service-run.ozESzR7j: engine startup, import, authenticated service,
  full write/cold-reopen regression and offline verifier PASS, runner exit 0.

The old mixed library is not restored. Private-library consumers now require
AROS_PTHREAD_INCLUDE matching the archive snapshot. Select the source explicitly
with AROS_PTHREAD_SOURCE; there is no guessed default tree. Use a clean object
directory when changing the header set. Runtime affinity-mask cloning is not
covered by these tests; their attributes use default affinity.

Diagnostic trace Z1qI9fQu (other-tree archive) timed out after the first native
disk probe; retained, not counted as success. import-trace.c forwards selected
upstream messages without changing control flow, enabled only by
AROS_IMPORT_TRACE=1. The passing ADT service did not enable it.

Generic upstream discovery/import remains incomplete. Next integrate the real
upstream label discovery hooks and verify cold-created file-backed pools without
depending on the saved fixture configuration. The history below preserves the
intermediate failures and hypotheses; it does not supersede these results.

Link integration: AROS_IMPORT_DISCOVERY_LINK=1 now includes complete upstream
zutil_import.c and thread_pool.c plus native regular-file discovery hooks.
build-aros/discovery-service/OpenZFSAdmissionServiceHarness links successfully;
nm confirms zpool_search_import, zpool_read_label, zpool_open_func and tpool_wait
are present. No section garbage collection hides unresolved paths. This is
link evidence only: actual discovery/re-import has NOT run. The public API
still uses the saved fixture import path. Native device enumeration returns
ENOTSUP, file scanning uses upstream label parsing, and label-named additional
paths are not automatically opened. Next provide actual engine callbacks and
exercise export/discovery/import on isolated file vdevs, then cold persistence.

## Earlier thread-pool and service regression investigation

The thread-pool failure recorded below was traced to our mixed pthread link,
not to an established upstream tpool defect. The injected pthread core used
ThreadInfo.cleanup at +0x2e8, while archive cancellation setters wrote +0x2f8
and +0x2fc, overwriting that core's cleanup-list tail predecessor. Initial
cleanup probes passed until the cancellation setters ran.

`build-private-pthread.sh` now snapshots and compiles the complete pthread
source set against one native SDK. It records source and archive hashes and
does not install anything into shared SDKs. The engine linker rejects the old
AROS_PTHREAD_CORE override; use AROS_PRIVATE_PTHREAD for a complete archive.

Private archive: build-aros/pthread-consistent.IsswZj88/libpthread.a.
Native QEMU evidence: import-thread-run.ZjjVDxUb (diagnostic pass), followed by
import-thread-run.d49VWRiT (pass without upstream traces or linker wrappers).
Both completed eight rounds of 128 jobs, suspend/resume/wait/destroy, membership
and atomic exact-count checks, alternating default and explicit attributes.
Cleanup probes include the cancellation-state/type resets that exposed the
mixed-layout bug. This proves the tested tpool workload, not the full service.

The service and client rebuilt into build-aros/consistent-service with the
same complete archive did NOT pass the full regression: admission-service-run.
7Vu69k0U timed out before admission became ready. Its write serial log ends
after creating pthread #164; that alone does not identify the blocked stage.
The default service binaries were not replaced. A diagnostic rerun adds sparse
test-harness stage markers to discriminate engine initialization, pool import,
fixture operations and the service loop. The rerun g35jpo6I reaches "engine
ready; importing baseline" but not "baseline import returned": the observed
stall is inside spa_import, not kernel initialization. Generic discovery is not yet
integrated or verified. The earlier failure history below is retained as such.

Control run admission-service-run.ranj4MwL links the identical full-engine.o
against the complete native SDK libpthread.a (no injected core); its separate
client also uses the complete SDK archive. It likewise times out after "engine
ready; importing baseline". Thus the current-source private archive alone is
not sufficient to explain the service regression. Both controls preserve copied
baseline disks and logs. Next diagnostic must locate the wait within spa_import
and compare the earlier passing service artifact/provenance, rather than assume
that replacing pthread again fixes it. Neither candidate is promoted.

Do not advertise generic discovery/import as supported. The management entry
still uses the restricted saved-fixture configuration. The work here compiles
the complete upstream libzutil/zutil_import.c, preserving its label selection,
txg merging, config assembly and refresh logic. No replacement label parser.

`AROS_SDK=<native SDK> bash aros/engine/compile-import.sh` builds only into
build-aros/import-audit, not the verified native service. The import-local
libintl header returns untranslated diagnostics (no message catalogs yet).
The local large-file header asserts 64-bit off_t and maps pread64 to the actual
native CRT pread. Each upstream label-reader has its own descriptor; the earlier
shared file-vdev offset mutex requirement still applies to the engine adapter.
CPU count uses the existing native processor.resource helper.

Compile results: import-audit-first.log lacked libintl.h; second exposed missing
TEXT_DOMAIN, pread64 and sysconf CPU selector; third exposed the SDK's missing
pread declaration; fourth is warning-free complete object compilation.
This is not a linked/runtime discovery result. Remaining platform hooks include
zpool_open_func, blkid-equivalent enumeration and device-string updates. Native
device/partition identity and allowed scan paths must be specified before any
unrestricted device scan. No actual remote pool was scanned.

## Upstream thread-pool dependency

The native SDK has no guard-size field/accessors in pthread_attr_t, and declares
six scheduling-attribute accessors that its static library does not export.
AROS-specific cloning now copies its public resource-free attribute struct.
Other platforms retain upstream behavior. This compiles and links, but runtime
verification is **failing**, not complete.

Build: `AROS_SDK=<native SDK> bash aros/engine/build-import-thread.sh`.
Run only if build succeeds: set AROS_TEST_DOS_OVERLAY and AROS_TEST_ATA_OVERLAY,
then `bash aros/engine/test-qemu-import-thread.sh`. This is a private, diskless,
two-CPU QEMU guest. It exercises real upstream suspend/dispatch/resume/wait/
destroy, intended for eight rounds of 128 jobs, alternating default and explicit
native attributes. Current failure is round zero, before explicit-attr cloning.
Diagnostic linker wrappers call the real mutex-unlock/cleanup-pop functions;
they log rather than replace those implementations. Counters are now atomic.

Retained failure evidence:

- nfRi474w: first run timed out after worker construction.
- nXprum4p: stage markers narrowed timeout to tpool_wait.
- ROB7N0Dw: first job entered and membership query returned.
- gYKUEHFX: mutex-unlock wrapper reported no error before timeout.
- w0MTK1Ui: first job returned, four cleanup-pop calls returned, still timeout.
- WJgrFPGn: **excluded**. Missing proto/dos.h caused diagnostic build failure;
  an independently issued launch used the prior binary. Stopped this owned
  guest explicitly. Corrected header; subsequent build and run chained with &&.
- 8AbCXos3: atomic job counters plus one-shot watchdog showed completed=4 of
  128, four cleanup-pop calls returned, wait did not complete. Runner reached
  120-second deadline and terminated only its owned guest. Log:
  build-aros/import-thread-qemu-7.log. Latest build log:
  build-aros/import-thread-test-build-8.log. No successful round or PASS claimed.

All directories above are build-aros/import-thread-run.<suffix>. Each preserves
serial, QEMU log, source archive/patch and binary/input hashes. Existing
build-aros/native-engine/pthread-native-current.o was linked just as in the
verified service; check its ABI/source provenance against the SDK archive when
continuing. Possible cleanup/queue/mutex interactions are hypotheses, not yet
attributed to shared pthread code. Do not bypass tpool_wait or fake completion.

Next: discriminate actual job_cleanup invocation and next worker-loop state;
audit pthread core/archive ABI consistency; fix and repeat full pool test before
using this dependency for label discovery. Then integrate upstream discovery
and cold file-backed pool import/readback. RAM-based GUI creation is independently
verified in aros/ui/VERIFICATION-CREATE.md, not cold-persistence proof.
