# Building the AROS port

## AROS build-system integration (in progress)

### Current deferred-destruction development package

Local archive `build-aros/mmake-bundle.7uo8g04u-verified.tar.xz` has SHA-256
`f185bc6e401e77fcb565b8948d66d5884a3c60c6d249b4d09ee142e6904cdb0f`.
It binds shared engine `shared-engine.scTORDPP` and the rebuilt MetaMake Manager
to GUI run `native-gui-run.jDpBYxhs` and three-phase storage run
`public-service-run.fkCVCuGC`. The Manager queries the live ONLINE pool;
mounted files are visible and exact post-UI DOS readback passes. Separate native
API tests qualify recursive deferred destruction, retained holds over cold
imports, and writable cold release. GUI entry of that mutation is not claimed.

The stricter packager also requires the optional writable cold-release phase's
completion, zdb and unchanged verification hashes. All package SHA256SUMS pass
both before archiving and after extraction to a fresh directory. Platform
overrides remain explicit. This local development artifact is not an uploaded
release, stock-nightly compatibility claim or hardware qualification.

### Refreshed development package, 2026-10-23

The MetaMake tools, CLI and acceptance client were rebuilt and qualified with
native GUI/desktop run `native-gui-run.HtAMyLoU` and persistent run
`public-service-run.iFt6XMEu`. The latter covers healthy/degraded/restored cold
imports, trusted ENVARC startup restoration, unchanged member hashes and
independent `zdb` verification. The GUI run checks exact data after UI closure.
`stage-mmake.py` accepts matching input hashes across these runs, and every
staged SHA256SUMS entry passes. The selected shared engine remains the explicit
previously built candidate; this was not a clean rebuild of upstream OpenZFS.

Retained archive: `build-aros/mmake-bundle.ch8cjgk8.tar.xz`.
SHA-256: `ba715a81de8f282245c38f8543435f51dc5686531ad807b0f6e2b6079945ac0b`.
It includes the clone-properties Manager and ExecHelper. Patched platform
prerequisites remain explicit; it is not stock-nightly or physical-media proof.
The archive is a local development artifact, not an uploaded release asset.
Screenshots and matching GUI evidence are committed under
`aros/tests/evidence/package-refresh`. Three disposable test runs pruned
18.82 GiB of images while retaining logs and provenance.

### Manager clone properties

Snapshot clone requests with nonempty Options now use the existing asynchronous
command worker and installed `C:zfs clone -u`. Options are up to 16
semicolon-separated `property=value` pairs, passed as exact argv entries with
no shell interpretation. Duplicate names, empty entries and missing
confirmation are refused before execution; upstream libzfs and the native
ioctl boundary own property validation and caller permissions. Empty Options
retain the existing authenticated service route. No parents or mounts are
created implicitly. Inspect the destination after a command failure rather
than assuming atomicity or automatically retrying.

Native QEMU run `public-service-run.CSAlCo0c` invokes the same clone-command
adapter from the acceptance client, verifies quota/compression and `mounted=no`,
and rejects duplicate/malformed/unconfirmed requests. The complete storage
suite passes. Manager and client were rebuilt with the explicit alpha18 SDK.
This is command-path runtime evidence; the new Manager interaction has not yet
received visual qualification. No production storage was accessed.

Follow-up `public-service-run.wFyWOuwW` exercises the actual Manager worker
start/poll/join/free path with a bounded wait. The clone's properties and
unmounted state remain correct. Invalid upstream quota input returns a command
error and an independent dataset query confirms no destination was created.
The complete storage suite passes. This closes the asynchronous-worker test
gap, not the remaining visual interaction check.

Visual run `native-gui-run.A56TMiQN` closes that interaction check for this
fixture. With `AROS_PUBLIC_GUI=1 AROS_TEST_GUI_CLONE=1`, the harness prepares
`public_mirror/files@gui_clone`. Advanced / Snapshots / clone was selected in
Manager; destination `public_mirror/gui_clone` and options
`compression=off;quota=32M;mountpoint=none` were entered and confirmed.
Screenshots `05-request.png`, `06-confirm.png`, and `07-result.png` retain
the request, confirmation and upstream exit-zero response. After Manager
closed, independent CLI queries confirmed origin, quota, compression and
`mounted=no`; both fixture objects were destroyed and the complete storage
suite passed. The optional harness mode requires its own PASS marker before
pruning the disposable ISO. This does not qualify all properties, delegation
combinations, or physical media.

Run `public-service-run.peSQN9rk` additionally switches to the unprivileged
`ozreader` fixture account before starting the actual clone worker. The
upstream clone command returns exit 1, the fixture's root identity is restored,
and a subsequent query confirms no destination exists. The full storage suite
then passes. This covers nondelegated denial through worker/child identity
inheritance; it does not yet qualify a user with selectively delegated clone
and property permissions.

`public-service-run.PnXbHX1E` adds selective delegation: UID 77 receives clone
permission on the source and create/mount/compression/mountpoint permissions
on a dedicated destination parent. The actual worker succeeds, and independent
queries verify origin, compression and no automatic mount. After both grants
are revoked, another worker request is denied and its destination is absent.
Fixture cleanup and the full storage suite pass. This checks permission
inheritance and revocation for this explicit grant set, not every delegation
combination. Successful-run logs are retained and the ISO is pruned.

The root `mmakefile.src` uses native `%build_prog` templates, not the standalone
shell build scripts. Place the source in a directory scanned by the configured
AROS Contrib build and regenerate its metamake files. Available targets:

- `contrib-openzfs-tools`: Manager, host-identity, runtime-check and device tools.
- `contrib-openzfs-manager`: `Tools/OpenZFSManagerNative`.
- `contrib-openzfs-hostidentity`: `C/OpenZFSHostIdentity`.
- `contrib-openzfs-runtimecheck`: `C/OpenZFSRuntimeCheck`.
- `contrib-openzfs-exechelper`: `Tools/OpenZFS/ExecHelper`, required by native
  upstream subprocess calls and built with the CLI/runtime-check targets.
  RuntimeCheck tests actual helper execution, captured stdout and child exit
  status, without accessing a pool. It uses temporary RAM: and T: probe files.
- `contrib-openzfs-device`: `C/OpenZFSDevice` (resident device installer).
- `contrib-openzfs-handler`: generated module from `aros/handler/mmakefile.src`.
- `contrib-openzfs-cli`: upstream `zfs`, `zpool`, and both startup commands.
- `contrib-openzfs-zfs` and `contrib-openzfs-zpool`: individual commands in `C`.
- `contrib-openzfs-startup`: `C/OpenZFSStartup`.
- `contrib-openzfs-startupmount`: `C/OpenZFSStartupMount`.
- `contrib-openzfs-library`: `Libs/openzfs.library` and its generated import ABI.
- `contrib-openzfs-service`: small `C/OpenZFSServiceNative` library launcher.
- `contrib-openzfs-nvpair`: source-built `libnvpair.a` with memory XDR support.
- `contrib-openzfs-nvpair-test`: native `C/OpenZFSNvpairTest` regression tool.
- `contrib-openzfs-engine`: full normal engine object, compiled from source.
- `contrib-openzfs`: aggregate of tools, CLI/startup, handler, library and service.
- `contrib-openzfs-public-test`: optional native acceptance client, built from
  current source in its own object directory; not installed by the aggregate.
  It includes the same native processor.resource CPU-count helper used by the
  engine and commands, querying it 128 times for consistency. Set
  `AROS_TEST_REQUIRE_CPU=1` with `AROS_TEST_CPUS=1..8` in the public QEMU runner
  to require the reported count to match the requested virtual topology.
- `contrib-openzfs-upstream-test`: optional `C/OpenZFSUpstreamServiceTest`,
  combining the public acceptance suite with current upstream libzfs client
  sources and deeper initialization/mount/property/import tests. It builds in
  a separate object directory without cached probe objects and is not installed
  by the production aggregate. Select it with `AROS_TEST_CLIENT_BINARY` and
  set `AROS_TEST_REQUIRE_INIT=1` in the public QEMU runner.

The same MetaMake acceptance binary passes the complete native RAM-backed
suite with one CPU (`public-service-run.4myrPlwM`) and eight CPUs
(`public-service-run.sBdi1Rhe`), reporting exactly 1 and 8 respectively on all
128 queries. Both use the extracted `mmake-bundle.w5_grp_3` production binaries
and explicit patched platform inputs. This verifies topology detection and
these bounded regression runs, not long-duration stress or physical SMP.
Logs and hashes are retained in `aros/tests/evidence/native-cpu`; both test
ISOs were pruned after success.

The deeper MetaMake target passes the native eight-CPU suite in
`public-service-run.6FSUKowt`, including 80 double-handle initialization cycles,
80 failure-cleanup cycles, property source/authorization checks, bookmark
delegation, mounted snapshot guards, read-only promotion, file discovery and
the normal storage suite. These are current-source native tests, not merely a
link check. Initial run `SWVKg0qz` failed because two retained promotion test
datasets had default mountpoints and were inadvertently included in companion
startup mounting. Both fixtures now explicitly use `mountpoint=none`; no
production mount behavior was changed. Failed and passing logs are retained
under `aros/tests/evidence/mmake-upstream`. The successful ISO was pruned;
the failed-run artifacts remain available for diagnosis.

Tools inherit the configured target compiler, SDK, flags and output directories.
`OPENZFS_PTHREAD_INCLUDE` and `OPENZFS_PTHREAD_LIBDIR` can select matching private
pthread inputs when the configured SDK does not yet include the required port.
`OPENZFS_TOOLSDIR` can override the Manager's installation directory.

To rebuild the private pthread dependency, explicitly supply `AROS_CC`,
`AROS_SDK`, and `AROS_PTHREAD_SOURCE` (the matching AROS pthread source
directory containing `pthread.c`, public headers, and `mmakefile.src`) to
`bash aros/engine/build-private-pthread.sh`. It snapshots the complete source
set, compiles every listed function with the selected SDK, and uses the
compiler-selected archiver. It records compiler identity, input paths, source,
object and archive hashes without installing into the SDK. Keep its reported
archive and header directory together. `AROS_ARTIFACT_ROOT` can select a
separate mounted filesystem; the common 100-GiB reserve guard applies.

Fresh 35-source pthread build `pthread-consistent.6ujRL4nB` was linked into
shared engine `shared-engine.RDWvxF8a`. Hosted run
`hosted-security-test.KzxOl3nq` passed the selected CLI replication/rollback,
handler and persistent/cold/degraded mirror suite. Its source was the previously
qualified AROS pthread snapshot, not an automatically downloaded new version;
the engine objects and test client otherwise remained the qualified MetaMake
build. This does not establish compatibility with arbitrary pthread revisions.

The CLI targets compile the upstream command and userland library sources;
they use authenticated service IPC rather than embedding the storage engine.
Supply compatible dependency inputs using `OPENZFS_TIRPC_INCLUDE`,
`OPENZFS_CRYPTO_INCLUDE`, `OPENZFS_NVPAIR_ARCHIVE`, `OPENZFS_CRYPTO_ARCHIVE`,
`OPENZFS_ZLIB_ARCHIVE`, and `OPENZFS_PTHREAD_ARCHIVE` when they are not installed
in the configured SDK. `OPENZFS_PIPE2_OBJECT` optionally supplies the generated
pipe2 import stub for older SDKs; it must match the target POSIX runtime.
Most dependencies are not yet fetched or built by the metamake targets.
For nvpair, run `contrib-openzfs-nvpair` with `OPENZFS_TIRPC_SOURCE` set to an
extracted libtirpc 1.3.7 tree. The standalone nvpair builder documents and checks
the pinned source archive hash; the metamake target uses the supplied source
tree without downloading or modifying it. `OPENZFS_NVPAIR_LIBDIR` controls the
archive installation directory (default: the target SDK library directory).
The target builds upstream nvpair plus only four memory-XDR codec sources, not
the RPC networking or authentication stack. Select this archive in subsequent
CLI/library builds with `OPENZFS_NVPAIR_ARCHIVE` if using a private directory.

The standalone `aros/engine/build-nvpair.sh` now requires explicit `AROS_CC`
and `AROS_SDK`. Use a fresh `AROS_NVPAIR_OUTPUT_DIR` to retain separate build
evidence. It verifies the pinned libtirpc archive and compares all consumed
XDR sources and the complete tirpc header directory against an extraction of
that archive, including when `AROS_TIRPC_SOURCE` already exists. A valid archive
hash is not treated as proof of an arbitrary source tree. The retained
`nvpair-inputs.*` directory records compiler/SDK selection, XDR source hashes,
post-compilation source checks and output hashes. The archiver is selected by
the compiler, and the storage reserve guard applies to both output directories.
Native build `nvpair-explicit.WDG3zSio` passed; private negative fixture
`nvpair-mismatch.9753vsZU` changed one XDR source and was rejected before an
archive was produced. This establishes build/provenance behavior, not a new
runtime qualification of that standalone archive or a complete dependency
bootstrap. The earlier MetaMake package runtime evidence remains separate.

The standalone nvpair provenance also records its OpenZFS C sources and
configuration, project and selected SDK headers, optional private pthread
headers/archive, builder script, Git revision and local patch. Input hashes
are checked again after the test executable has linked. Native cross-build
`nvpair-provenance.l9f0JZ5l` passes this expanded check (18,468 input files) and
its output manifest verifies. This is a fresh build/provenance result, not a
new runtime qualification or a complete dependency bootstrap.

Set `AROS_TEST_NVPAIR_BINARY` to a freshly built `OpenZFSNvpairTest` when
running `aros/engine/test-qemu-public-service.sh` to require its native result
alongside the storage suite. The runner records the executable hash and uses
a RAM: output file, never the boot medium. Build `nvpair-runtime.YoPeUibc`
passes this check in `public-service-run.rjCuKSSW`: allocation, nested lists,
64-bit GUID preservation, XDR pack/unpack, file output and truncated-input
rejection. The full service/storage suite also passes. This qualifies the
standalone dependency test; it does not relink the resident engine or claim
that every engine consumer was rebuilt against this archive.

Expanded run `public-service-run.0rL96uTk`, using build
`nvpair-arrays.WcB8JItW`, also passes uint64 arrays (including UINT64_MAX),
string arrays with an empty member, nested vdev-list arrays, boolean values,
and rejection of every truncated prefix of its packed fixture. Each assertion
is retained in the native serial log. The complete storage suite passes in
the same guest. Earlier fixture build `nvpair-arrays.EBFcrH4L` failed because
its array inputs lacked the upstream API's const qualification; the associated
runner stopped before ISO creation or guest boot. Those logs remain retained.

The fresh standalone archive was subsequently linked into native shared-engine
candidate `shared-engine.j0FGUlhp`, using the same full MetaMake engine object,
native pthread archive, crypto archive and SDK `libz1.a` import library.
`public-service-run.EkYSVMiY` passed service lifecycles, packaged CLI replication
and rollback, standalone handler checks, normal export, fresh-boot healthy and
degraded mirror reads, unchanged member bytes and host `zdb -bcc` with no leaks.
CLI and acceptance-client objects were unchanged; this qualifies the new
archive in the shared engine, not a fresh rebuild of every consumer. The
earlier `shared-engine.SzmiKStH` used `libz.a` and was not runtime-tested; the
corrected candidate deliberately matches MetaMake's `libz1.a` dependency.
The frozen AmiWest handoff package was not replaced by this experiment.

The handoff's actual native Manager (SHA256
`d7f2d0027627a69b72fbf7835933303e97e44d8018739463c600d85cbc59b9a7`)
also passed an interactive QEMU check in `native-gui-run.d8Gr4rb3`.
The Simple connection response displayed the real engine connection, and
selecting List pools then Show results returned `public_mirror` ONLINE from
the backend. `manager-start.png` and `query-result.png` retain the screenshots;
`screenshots.sha256` records their hashes. The Manager closed normally and
the remaining packaged handler/export/read-only reimport suite passed.
This is a native connection/list/close check on a RAM pool, not native GUI
pool-creation coverage or qualification on the show laptop's runtime.

The metamake-built native nvpair test passed allocation, nested lists, XDR
encode/decode, exact 64-bit values, file output, and truncated-input rejection
in isolated hosted AROS. The shared engine relinked with that archive also
passed the persistent mirror/cold/degraded regression. This does not remove
the need for compatible pthread, crypto, zlib, and AROS runtime dependencies.

The library target depends on `contrib-openzfs-engine` and defaults to its
`full/full-engine.o` and provenance directory in the generated build tree.
`OPENZFS_ENGINE_OBJECT` and `OPENZFS_SERVICE_BUILD` can still select a matching
standalone source build. It verifies the recorded engine hash and rejects diagnostic objects
before renaming the service entry in a private copy. It uses the same archive
overrides as the CLI, except `OPENZFS_ZLIB_IMPORT` selects the engine's zlib
import archive. `OPENZFS_OBJCOPY` and `OPENZFS_NM` default to the enclosing
compiler's tools. The normal AROS includes/core-linklibs stages must run first;
these provide header directories and the resident-entry object.

The module-template library and launcher passed the isolated hosted persistent
mirror regression, including cold and degraded reads, unchanged backing-file
hashes, and an independent host `zdb` check. The launcher contains no engine
entry symbols. The subsequent engine target compiled all 360 normal engine
source units, including whole upstream ZPL and ioctl units, using the configured
target compiler. Its relocatable link does not use section garbage collection.
The library built from this engine and the metamake nvpair archive passed the
hosted persistent mirror, cold/degraded DOS read, and host `zdb` checks in
`hosted-security-test.HZ3F4tC8`. The test helper came from the earlier public
source build; its hash is retained separately. This does not prove the complete
aggregate target from a pristine AROS checkout or stock SDK compatibility.

The complete `contrib-openzfs` MetaMake aggregate subsequently passed with
initially empty OpenZFS object and staging directories, using a preconfigured
hosted AROS SDK and explicitly supplied dependencies. This exposed and fixed
missing aggregate make recipes and generated-header/directory prerequisites
for CLI dependency files. Build evidence is retained in
`mmake-clean-final-20261021/aggregate-build.log`. It is not a fresh OS/SDK build.
The resulting library, launcher, `zfs`, and `zpool` passed isolated hosted
pool/dataset listing, property retrieval, and snapshot creation/listing/destruction
against disposable mirror files (`hosted-security-test.NJcLlGxE`). The same run
passed cold and degraded DOS reads, independent host `zdb`, and unchanged-file
hash checks. The freshly compiled test helper reused the earlier public-build
client objects, with input hashes retained separately. Enable this packaged CLI
coverage with `AROS_TEST_CLI_DIR` pointing to the staged `C` directory when
running `aros/engine/test-hosted-security.sh` with a normal full service build.

The packaged CLI coverage also includes clone/origin lookup, rename, explicit
LZ4 pool-feature enablement and state query, compression set/get, rollback
command execution and cleanup. A subsequent test writes and flushes a new file
after the snapshot, detaches, rolls back through `zfs`, and remounts read-only:
the later file must be absent and the original payload byte-exact. Initial runs correctly refused
LZ4 because the minimal fixture had not enabled its feature. Explicit child
stderr capture exposed that upstream diagnostic; the trailing DOS error alone
was misleading. The test now enables and verifies the feature through `zpool`
before setting the dataset property, without weakening feature validation.

`hosted-security-test.1toSrTja` repeats the packaged lifecycle, rollback data,
standalone handler and cold/degraded mirror checks with the acceptance client
itself built through `contrib-openzfs-public-test`. All ten client source units
were compiled afresh; this run no longer reuses the earlier client objects.
The SDK and external dependency archives remain explicitly supplied inputs.

`hosted-security-test.029wFKQH` additionally passes full and incremental
replication through the actual packaged `zfs send` and `zfs receive` commands,
using ordinary redirected stream files inside the isolated runtime. The full
receive preserves the snapshot's original payload and excludes the later file;
the incremental receive adds a new file while preserving the original. Both
are verified through read-only native DOS mounts, followed by exact snapshot
and dataset cleanup. Existing rollback, standalone handler, cold/degraded
mirror and independent `zdb` checks also pass. This is same-pool local stream
coverage, not network transfer, encrypted/raw streams, interrupted receive,
or a fresh-boot check of the received dataset (which the test destroys).

The MetaMake-built Manager also passed an interactive real-service check in
isolated hosted run `hosted-security-test.5fpE94eD`: connect, list the ONLINE
disposable pool, select snapshot creation, review the exact target, submit,
and close normally. `public_mirror/files@hosted_gui` was then verified by
independent host `zdb` and the cold reader boot, with the existing backing-file
hash gates intact. Screenshots of pool results, confirmation and successful
snapshot response are retained in that run. Only the owned AROS display was
captured and controlled; no user session or physical machine was accessed.
This covers those UI operations, not every management action or layout size.

Simple mode now summarizes successful connection replies instead of displaying
the full protocol capability dump. It distinguishes engine availability from
storage health and points to List pools and Advanced mode. Error replies remain
unabridged, and Save response retains the complete backend response. Hosted
run `hosted-security-test.J5hxGuXa` visually verifies the concise connected
view, Advanced reconnect with full capabilities, and a saved response containing
the original protocol fields. Normal window close and the selected storage
suite pass. Its unavailable-engine summary branch is not runtime-qualified by
that connected-engine test.

All four CLI/startup executables linked through the generated makefile.
The resulting `zfs --version`, `zpool --version`, and repeated `zfs --version`
ran in isolated hosted AROS and printed `zfs-2.4.4-1`. They returned 1 with no
service running, because the upstream version command also requests the engine
version. This is executable startup evidence, not pool-operation qualification.

A subsequent isolated hosted run loaded a freshly rebuilt `openzfs.library`
and its small service launcher. An unauthenticated engine-version query returned
EACCES (13). After login through the disposable fixture's `security.library`,
the same metamake-built `zfs --version` reported both client and engine versions
with status 0; `zpool list` returned `no pools available` with status 0. Manager
also connected. No disk files were attached in this check: authenticated IPC
is verified, while current-package pool and persistence tests remain separate.

The four tools were compiled and linked through the generated AROS makefile,
and the resulting Manager opened in isolated hosted AROS with the expected
absent-service response. The handler also builds with a matching generator.
An initial generator/SDK mismatch produced startup code referencing
`PR_RETURNADDR_NOPROGRAM`, absent from the older SDK. A private generator built
from the parent of AROS commit `bd7713c6b9` resolves that particular test mismatch.

A separate PC/SMP-SDK aggregate and acceptance-client build also completed in
`mmake-native-20261021`. That older SDK requires a different generator: the
parent of AROS commit `7405611e23` predates out-of-line module init/expunge.
Both the hosted generator and the installed native-build generator emitted
`InitSets`/`set_libinit` startup code unsupported by this SDK. A privately
compiled matching generator resolved the build without changing the SDK or
shared tools. Its old host sources produced warnings under the current host
compiler, retained in `generator-build-3.log`. The pthread build additionally
required an explicit `AROS_PTHREAD_DOSTAGS` header containing `NP_Affinity`.
That tag must be supported by the selected runtime; compiling against the
header does not prove runtime support. Native aggregate and client logs are
`aggregate-native-matched.log` and `public-test-native.log`. This paragraph
records cross-build success only, not QEMU or hardware acceptance.

The resulting native shared engine and acceptance client subsequently passed
`public-service-run.5eKxPfIv` in isolated two-CPU QEMU: repeated service
lifecycles, authenticated native admission, and a RAM-backed mirror DOS
write/read/read-only-reimport sequence. The test used explicitly selected
historical DOS/ATA/POSIX overlays and zlib; their hashes and base ISO hash are
retained. This is not stock-nightly qualification, physical-device proof, or
the extended packaged CLI/handler suite. The public-service QEMU runner now
requires explicit ISO, acceptance client and zlib inputs, supports verified
`AROS_TEST_SHARED_BUILD` staging, and applies the storage reserve guard with
optional separate-filesystem `AROS_TEST_ARTIFACT_ROOT`.

The extended native run `public-service-run.vredzyyV` also passed with the
MetaMake-staged `zfs`, `zpool`, and standalone handler. Set
`AROS_TEST_CLI_DIR` to the staged `C` directory and
`AROS_TEST_HANDLER_BINARY` to the staged handler to require these checks.
The runner retains package hashes and requires separate CLI, replication,
and handler PASS markers. Coverage includes clone/rename, LZ4 property
activation, rollback verified through mounted files, full and incremental
send/receive with exact mounted data checks, and standalone DOS Mount with
read-only write denial and detach. CLI scratch lives in `T:`, not the
read-only boot ISO. This run used the same explicit runtime overlays and a
disposable RAM mirror; it does not prove cold persistence or hardware safety.

Persistent native qualification is optional via `AROS_TEST_PERSISTENT=1`,
requiring an explicit flush-capable `AROS_TEST_FAT_HANDLER` and executable
`AROS_TEST_HOST_ZDB` (and its host library search path, if needed). The runner
creates a new 512 MiB regular-file FAT disk and two 128 MiB pool members; it
never accepts an existing backing disk for this mode. It runs the packaged
CLI/handler tests, exports, then boots a fresh QEMU process for healthy and
missing-member read-only imports, exact DOS payload checks, and write denial.
Member files must remain byte-identical. Host `zdb -bcc` must then pass without
leaks and without changing the extracted members. The historical client log
label `Public cold hosted DOS` is also used by this native cold-import path.

Final run `public-service-run.qA8HX86o` passed all these gates with the native
MetaMake artifacts and explicit historical DOS/ATA/POSIX/FAT overlays.
`public-service-run.t14Au7JK` passed the cold checks with a separate successful
host zdb check. Earlier attempts `F9HXwqKf` and `K1L3FP0M` passed the writer but
failed ISO repackaging before the cold guest launched; their evidence remains.
Both boot images are now constructed directly from the original base image.
This proves normal export/fresh-boot persistence on virtual storage, not
stock-nightly compatibility, abrupt-power-loss recovery or physical flush.

`AROS_TEST_LAYOUT=raidz1|raidz2|raidz3` extends this persistent suite to
three/four/five 128 MiB file members respectively; `mirror` remains the default.
The private FAT backing grows to fit the members plus 256 MiB headroom.
Cold verification hides one/two/three members respectively, checks exact DOS
data and read-only enforcement, and restores all filenames even on failure.
Each run still requires byte-identical members and host `zdb -bcc` success.
The pool fixture retains the name `public_mirror` regardless of its topology.

Native MetaMake topology runs passed: `xm4kcdQ2` (RAIDZ1), `3BcAZiL5`
(RAIDZ2), `tolAunKR` (RAIDZ3), and `y8zAfp56` (mirror regression), all under
`build-aros/public-service-run.*`. Separate retained `zdb-config.log` files
confirmed the three RAIDZ pools' actual parity and member counts. These checks
cover normal exports and degraded read-only reimports; they do not establish
every topology, parity reconstruction during writes, or abrupt-loss recovery.

The current acceptance client also creates a sparse file of 4 GiB + 4096
bytes through native 64-bit DOS packets. It writes a marker across the 4 GiB
boundary, verifies zero-filled holes, exact size and data, and checks that a
legacy 32-bit seek refuses overflow without changing the position. The same
checks run against full/incremental received datasets and fresh-boot healthy
and degraded read-only mounts, including resize denial on read-only datasets.
`public-service-run.dl2KyDaV` passed this final suite and independent host zdb;
`WwsWdvA9` passed the earlier cold checks before replication assertions were
added. This allocates only sparse payload blocks: it does not qualify sustained
4 GiB writes, an 18 GB directory copy, or physical USB throughput.

Cold tests additionally withhold more members than redundancy permits:
both mirror leaves, two RAIDZ1 leaves, three RAIDZ2 leaves, or four RAIDZ3
leaves. Rejection requires a backend reply, not just a transport error; pool
enumeration must remain responsive and empty, with no mounted filesystems.
Every hidden filename is restored even on failure, and another healthy import
must read the exact normal and sparse-file payloads. Final member hashes and
host zdb checks remain required. Native runs `iCGv6C2l` (mirror), `F1KwQXMi`
(RAIDZ1), `LZpfBybc` (RAIDZ2), and `L8Lhe4EJ` (RAIDZ3) all passed. This tests
missing members at import, not live device removal during outstanding writes.

The packaged CLI suite now exercises AES-256-GCM dataset creation with a
disposable 32-byte raw key in guest RAM. It writes and flushes a DOS payload,
unmounts, unloads the key, verifies `keystatus=unavailable`, attempts a different
key and requires failure with the status still unavailable, then reloads the
correct key and reads the exact payload through a read-only mount. It finally
unloads, destroys the test dataset, and deletes both RAM key fixtures.
`public-service-run.vqYeyU6c` passed this lifecycle plus the surrounding native
CLI/handler, sparse64, cold/degraded/refusal and independent zdb checks. This
uses public test bytes, not user credentials; it does not establish encrypted
dataset persistence across reboot, passphrase derivation, raw encrypted send,
or secure key-erasure behavior. Earlier historical crypto tests are separate.

The subsequent native persistent test retains that encrypted dataset, with its
key unloaded and both RAM key files deleted before export. The fresh guest
requires `keystatus=unavailable` and a rejected read-only mount, recreates only
the public fixture key in RAM, loads it, reads the exact original payload,
detaches and unloads again. Healthy, degraded and restored imports all execute
these checks. `public-service-run.SnOsxOLI` passed all three iterations and
the surrounding package suite, unchanged-member comparisons and host zdb.
This adds encrypted cold persistence on the tested virtual mirror; it still
does not establish physical USB durability or secure erasure of key memory.
Nonpersistent tests continue to destroy their encrypted dataset during cleanup.

The packaged CLI now also sends a snapshot with `zfs send -w` while its raw
encryption key is unloaded, then receives the stream into a separate dataset.
It checks AES-256-GCM and unavailable key status, requires a locked mount to
fail, loads the fixture key, and reads the exact original payload through the
handler before removing the received dataset and temporary stream.
`public-service-run.0nQnnyCF` passed this raw encrypted replication test,
the three cold encrypted imports, unchanged-member comparisons and independent
host zdb check (no leaks). This is local full-stream replication through guest
temporary files, not interrupted/resumable receive or network qualification.

`public-service-run.G2QAicmh` extends that test with a second source snapshot
containing a new file. With both keys unloaded, `zfs send -w -i` and `zfs receive`
apply the raw incremental stream. The receiver remains locked and rejects
mounting until key load, after which both old and new files match exactly.
The surrounding native package, cold encryption/recovery and independent zdb
checks also passed. This does not establish interrupted receive/resumption.

Prefer a coherent SDK and genmodule pair; do not define missing ABI constants
in this project. Handler/device compilation is not runtime qualification.

For isolated build-system testing, redirect `AROSDIR`, `GENDIR`, `DBGDIR`,
`KOBJSDIR`, and host-tool outputs, while
selecting the intended read-only SDK via `AROS_DEVELOPER`. Overriding `AROS_FS`
alone does not redirect generated module installation. AROS make can rebuild
`GENMODULE` automatically from source dependencies, so its output must also be
private; an executable-path override alone does not pin its version.

The aggregate target also passed a fresh private-output build on 2026-10-22
(`mmake-native-fresh-20261022`), using current ADT pthread and crypto built from
the independently fetched upstream source plus the bundled AROS patch. All
360 engine units, commands, handler, library, service and Manager were rebuilt.
This uses a configured SDK, its generated configuration and module-startup
object, compatible private genmodule and the existing SDK-check source shim;
it is not a clean build of the entire operating system or SDK. The first
attempts stopped on missing private target configuration/module-startup input;
those logs remain alongside the successful aggregate log.

The resulting package passed persistent native QEMU run
`public-service-run.RqXKXWLR`, including PBKDF2 known answers, real passphrase
datasets, raw full/incremental replication, sparse64, cold mirror recovery,
unchanged members and independent zdb. A separate nonpersistent native run,
`native-gui-run.yKFrLIn3`, visually verified the new Manager connecting and
listing the real `public_mirror` pool ONLINE (`pools.png`), then closing normally;
the remaining handler/storage tests passed afterward. Package hashes, build
logs, screenshots and input logs are retained. Runtime overlays are still
explicit dependencies; stock-nightly compatibility and turnkey SDK/overlay
bootstrap remain outstanding. The standalone build sequence below is also
available.

Simple-mode pool listing now formats the structured pool rows as name/state
and human-readable size, allocated and free capacity. It does not parse the
backend's prose or present unavailable/inconsistent counters as zero capacity.
Advanced replies and saved responses retain the complete backend details;
pagination remains visible. `simple-pools-test.c` passes host ASan/UBSan tests
for byte boundaries, maximum 64-bit values, unavailable/degraded states,
inconsistent samples and bounded output. The rebuilt native Manager was
visually verified against the real service in `native-gui-run.6VdaucdZ`
(`pools.png`); normal close and the remaining storage suite pass. Those native
results are separate from the host formatter checks, not inferred from them.

This is a developer source release, not a standalone SDK or prebuilt installer.
The ordinary upstream Linux build does not build the AROS integration.

### Current native dRAID qualification

The public-service runner accepts `AROS_TEST_LAYOUT=draid1`, `draid2` or
`draid3` with persistent disposable backing. These use three/four/five member
files, respectively, one data column and one distributed spare. Independent
host `zdb -C` confirms dRAID type, parity, data and spare counts; this is not
RAIDZ merely labeled as dRAID.

Runs `public-service-run.VqCeGDTO` (dRAID1), `PidNgOWc` (dRAID2), and `8rSCjl3W`
(dRAID3) pass the current package suite, including sparse64, encrypted raw
replication and passphrase rotation. Fresh-boot healthy/degraded/restored
imports read exact data with one/two/three members absent. Losing one further
member refuses import with no pool or mount left registered, and restoring the
files restores readable data. Member hashes remain unchanged throughout cold
read-only verification; independent zdb reports no leaks for all three.
This qualifies small virtual dRAID configurations, not distributed-spare
activation/replacement, resilver, large-layout throughput or physical durability.

### Current native mirror repair qualification

`public-service-run.MGEST4XV` verifies repair through the shipped public API
and `zpool` command, with no test-linked SPA access. It obtains disk0's GUID
from status, confirms OFFLINE, writes and flushes 1 MiB of deterministic data
through the DOS handler, syncs the pool, and brings that member online.
After `zpool wait -t resilver`, status must independently report a finished
resilver with nonzero issued bytes and zero scan errors. This run reported
1,298,432 issued bytes; command success alone is not the acceptance condition.

A separate QEMU boot reads the exact new payload during healthy, degraded and
restored read-only imports. The degraded phase hides disk1, so the new data
must come from repaired disk0, not the original writer or its cache. All cold
member hashes remain unchanged; independent host `zdb -bcc` reports no leaks.
Repeat run `public-service-run.4flBWRkE` passes the same complete sequence with
explicit runner gates for warm repair and all three cold payload reads.
This qualifies offline/write/online repair of a two-file mirror, not physical
durability, replacement, distributed-spare activation or arbitrary topologies.

### Current native mirror replacement qualification

Set `AROS_TEST_REPLACE=1` for the persistent mirror package test (requires
the packaged CLI). The acceptance client provisions an additional blank
128 MiB file, submits operation 13 using the original leaf's exact GUID,
waits for upstream replacement/resilver activity, and checks an ONLINE new
GUID, two remaining leaves, absence of the retired leaf, and a finished
error-free resilver with nonzero issued bytes.

`public-service-run.qHFpp23h` passed this path with 1,978,368 bytes issued by
the replacement resilver. After export, the runner retains the retired member
outside the guest as `retired-disk0` with a checksum, then removes its disposable
guest copy. The next QEMU boot reads the exact data with the other original
member hidden, so neither an old member nor the writer's caches can satisfy
that check. Healthy, degraded and restored imports pass, cold member hashes
remain unchanged, and independent host `zdb -bcc` reports no leaks.
The ordinary non-replacement mirror regression run `public-service-run.GUbY9zBx`
also passes with the same rebuilt acceptance client and runner.
This covers replacement of a file-backed mirror member, not native-device
replacement, dRAID distributed spares or physical power-loss durability.

### Distributed-spare package test

`AROS_TEST_LAYOUT=draid1 AROS_TEST_DSPARE=1` selects a persistent
1-data/1-parity/3-child/1-spare test using the public replace API. It checks
refusal of an absent spare, then offlines the original member and activates
the existing spare by its status-reported name. It requires INUSE and a
finished, nonzero, error-free healing resilver. The Manager uses the same
typed request; its field/help accept an existing dRAID spare name as well as
a new external backing path. Sequential reconstruction is not selected.

Cold verification reads the exact payload, sparse file and encrypted datasets
with zero, one and two original files hidden, then restores the files and
reads again. This particular rebuilt layout tolerates the original offline
member plus one further missing child; that is not a general claim about all
dRAID geometries. With all three members hidden, import must fail without
leaving a pool or mount registered. Read-only member hashes and independent
host `zdb -bcc` are checked after restoration.

Final package run `public-service-run.fvSsNIwT` passes this complete sequence,
including the absent-spare refusal and four cold payload reads. The same
rebuilt engine also passes ordinary mirror replacement and cold recovery in
`public-service-run.zaPkqPua`. Both independent zdb checks report no leaks.
The Manager was rebuilt; this qualification exercises its public API, not
interactive operation of the newly labeled field. Hardware, higher-parity
spare activation and sequential reconstruction remain separate gates.

Follow-up higher-parity qualification extends `AROS_TEST_DSPARE=1` to
`AROS_TEST_LAYOUT=draid2` and `draid3`. Runs `public-service-run.8Y8AWT8E`
and `public-service-run.2aN8Shcv` pass the complete package suites: actual
spare activation, finished error-free healing resilver, exact cold reads
with respectively three/four original members hidden after rebuild, refusal
with all four/five members absent, restoration, unchanged cold member hashes
and independent zdb checks with no leaks. These remain small one-data,
one-distributed-spare file-backed layouts, not arbitrary-layout throughput or
hardware durability qualifications.

Private GUI run `native-gui-run.tn6yiFhe` visually verifies the rebuilt
Manager's Advanced/Pools replacement editor and typed preview. `07.png`
shows the new backing/spare field; `08.png` shows operation `pool.replace`,
the entered pool/GUID and the exact `draid1-0-0` leaf, with "Preview only -
nothing submitted". No mutation was sent by that GUI test. Normal close
returns to the remaining acceptance suite, which passes. The first pointer
positioning attempts are retained alongside the successful captures. This
supersedes the field-layout/preview gap above, not end-to-end GUI submission
or sequential reconstruction qualification.

Failed runs are retained: `VafOOFqR` and `jpwWJqGA` exposed a status adapter
bug (an auxiliary SPARED handle was incorrectly labeled UNAVAIL). The fix
matches upstream zpool's INUSE precedence, without changing in-tree health.
`uwuc289P` and `jfFOhhnt` exposed a wrong test assumption: after spare rebuild,
two missing original files still allowed import and exact payload reads.
The test now verifies that recovered redundancy and uses complete member
loss for its refusal check; it does not equate successful import with health.

### Sequential reconstruction qualification

Typed attach/replace accepts `rebuild=on`; the Manager's Sequential checkbox
emits that option without changing the backing/spare path. Empty options keep
healing resilver. Validation and authenticated admission reject other options.
Pool features are not silently enabled by this API. Upstream decides whether
the selected topology and pool features support sequential reconstruction.
Status now includes paginated `rebuild` rows with actual upstream state,
scanned/estimated bytes, issued/rebuilt bytes, errors and timestamps. Only
COMPLETE reaches 1000 permille; a byte estimate is not a completion signal.

Run with `AROS_TEST_LAYOUT=draid1 AROS_TEST_DSPARE=1 AROS_TEST_SEQUENTIAL=1`
to enable the disposable pool's device_rebuild feature and exercise this API.
`public-service-run.vcM2THBN` passes: malformed-option refusal, real sequential
spare rebuild (356,352 rebuilt bytes, zero errors), explicit checksum scrub,
and the full cold-data/redundancy sequence. All four cold imports retain the
COMPLETE rebuild row and finished scrub statistics. Member hashes are unchanged
and independent zdb reports no leaks. The same engine's ordinary mirror
replacement regression passes in `public-service-run.aemGct9B`.

The earlier `KjZrOMU7` failure exposed the missing admission-option allowance.
`mtRJyGCZ` passed guest checks but failed host extraction because FAT still
cached restored directory names when QEMU quit. Native file fixtures now
explicitly inhibit their disposable FAT backing volume after all pools export,
before emitting PASS; the runner requires that clean-detach marker in writer
and cold reader. This is clean shutdown, not power-loss qualification, and
does not change the filesystem driver to hide a failure. All failed evidence
is retained. The new Sequential control is rebuilt but not yet interactively
qualified; native attach and higher-parity sequential runs remain separate.

Higher-parity follow-up `public-service-run.iWws1mfl` (dRAID2) and
`public-service-run.89Me9RN1` (dRAID3) pass sequential spare reconstruction,
the post-rebuild scrub, all four cold status/data checks, complete-member-loss
refusal, clean backing detach, unchanged member hashes and independent zdb
checks. Failed `RKzCDpbG` is retained: the test tried to start a second scrub
while upstream's automatic post-rebuild scrub was running. The test now waits
for that scrub and requires its start time to be at or after rebuild completion,
so an older successful scrub cannot satisfy the gate. No engine behavior was
changed to suppress that failure.

Native GUI run `native-gui-run.WfKHTWCH` verifies the Sequential control's
layout, the `rebuild=on` typed preview with unchanged member path (`03.png`),
and clearing/disabling the checkbox for an unrelated operation (`04.png`).
It submits no GUI mutation. Normal close and the remaining package test pass.
This closes the control/preview gap, not end-to-end GUI reconstruction or
native-device attach qualification.

### Public-package zvol transport qualification

`public-service-run.VWf7yC6F` (mirror) and `public-service-run.KWdIi0ce`
(RAIDZ2) exercise the production shared service from
`mmake-native-fresh-20261022/package-sequential-admission` with the rebuilt
public acceptance client. The packaged upstream `zfs` creates a sparse 2 MiB
volume with 8 KiB blocks. Public session-owned zvol requests verify geometry,
24 KiB patterned writes, flush, exact reads, and discard of only the complete
interior block of a partially aligned range. The partial edge blocks retain
their original bytes. Boundary reads return EINVAL without changing the caller
buffer; closed descriptors return EBADF.

Three fresh-guest imports per run verify the same retained zvol bytes in healthy,
degraded (one mirror or two RAIDZ2 members absent), and restored configurations.
Read-only descriptors reject writes without reporting completed bytes. The
existing excessive-loss refusal, DOS dataset/crypto tests, clean backing detach,
unchanged member hashes and independent host `zdb -bcc` checks also pass with
no leaks. The RAIDZ2 runner additionally enforces all new zvol markers/counts;
the first mirror run predates those runner assertions but retains those same
markers. Source snapshots, binary hashes and serial logs remain in each run.

This is public service transport qualification, not a current-package test of
the resident `openzfs.device`, DOS filesystems formatted inside a zvol, GUI
volume publication, hardware discard or power-loss durability. Zvol logical
discard is distinct from the still-unsupported backing-device TRIM operation.

### Resident device with the public package

Set `AROS_TEST_DEVICE_BINARY` to the matching MetaMake
`stage/C/OpenZFSDevice` when running the public-service suite. The runner copies
and hashes that executable, includes device sources in its source archive, and
requires the device result markers on every persistent writer/cold-import run.

`public-service-run.StUps4JO` (mirror) qualifies EMPTY publication, writable
mapping, actual Exec OpenDevice/SendIO/WaitIO, 128 KiB read/write requests (split
across the 64 KiB service transport), CMD_UPDATE, exact geometry, three
close/reopen cycles, and explicit eject before pool export. All three cold
healthy/degraded/restored imports repeat reads and reject writes through the
read-only device mapping. The expanded `public-service-run.8Rw9gfGm` (RAIDZ2)
also verifies duplicate publication refusal, busy-client eject refusal without
losing the mapping, and out-of-range reads preserving caller buffers with zero
bytes completed. It passes all three cold imports, including two absent members.
Both full suites exit successfully with independent `zdb -bcc` reporting no
leaks and unchanged cold member hashes. No engine or device behavior was
modified to satisfy these tests.

The first guard-test compile failed because the acceptance client omitted
`exec/errors.h`; both failed and corrected build logs are retained. These
tests do not yet qualify formatting/mounting a DOS filesystem inside the zvol
with this public package, concurrent multi-process device traffic, GUI volume
publication, hardware durability, or resident-device unload (it intentionally
remains installed until reboot after its mappings are ejected).

## Requirements

- Linux build host with Bash, Git, Make, GCC-compatible AROS cross tools, binutils,
  Python 3, curl, tar, and ripgrep.
- Compatible x86-64 AROS SDK, `genmodule`, and platform/runtime changes described
  in [aros/patches](aros/patches/README.md).
- Matching private pthread headers and archive, crypto library, zlib import
  library, and the CLI's POSIX/pipe integration.
- Space for generated sources, builds, and disposable disk images. Build scripts
  enforce a 100 GiB reserve; do not disable that safeguard to force a build.

Paths under `/opt/aros-workspace` in scripts and historical examples are example
layouts. Configure the corresponding `AROS_*` variables for your environment;
some historical image/overlay helpers also require path adaptation. Dependencies
and platform overlays are not automatically bootstrapped from a stock nightly.

**Match the SDK to the runtime.** A PC/SMP-linked Manager is not a hosted-AROS
test binary. For hosted UI tests, set `AROS_SDK` to the hosted build's Developer
directory and use compatible dependencies. For PC/SMP QEMU tests, use that
target's SDK. `build-manager-native.sh` requires an explicit SDK rather than
silently choosing PC/SMP. Keep the two output binaries separate.

## Build sequence

1. Read [engine requirements](aros/engine/BUILDING.md) and the platform notes.
2. Create the ignored artifact directory with `mkdir -p build-aros`.
3. Configure `AROS_CC`, `AROS_SDK`, `AROS_PRIVATE_PTHREAD`, and
   `AROS_PTHREAD_INCLUDE` to matching tools, SDK, and pthread inputs.
4. Use `aros/engine/build-source.sh` for the full engine and service build.
   `build-nvpair.sh` can obtain the pinned, checksum-verified libtirpc source.
5. Follow [library instructions](aros/library/README.md) for the shared engine;
   retain the service build provenance and hashes used by its checks.
6. Build the upstream commands using [CLI instructions](aros/cli/BUILDING.md),
   and the handler using [handler instructions](aros/handler/README.md).
7. Follow [distribution instructions](aros/distribution/README.md) to stage the
   matching binaries. Packaging rejects diagnostic or mismatched engine builds.

Consult each script for its required inputs before running it. Native builds
require external AROS runtime dependencies; shell syntax checks alone are not a
native build or runtime qualification.

## Testing

### Native Manager zvol mapping and saved mount file

`native-gui-run.G6zXlnca` qualifies actual Manager remapping and mount-file save
with the public shared-service/device package, not only a preview. Enable
`AROS_PUBLIC_GUI=1`, `AROS_TEST_GUI_VOLUME=1`, `AROS_TEST_PERSISTENT=0`, and
provide explicit `AROS_TEST_MANAGER_BINARY` and `AROS_TEST_DEVICE_BINARY` along
with the normal public CLI/service inputs. The guest prints the exact requested
unit, dataset and output path. Through Devices, map unused unit 5 to
`public_mirror/public_volume` read-only, save the whole-volume FFS description
to `T:OZBLOCK`, then close the Manager normally.

The retained screenshots show confirmation (`03.png`), completed read-only
mapping with exact capacity/sectors (`04.png`), save confirmation (`05.png`),
and the saved description (`06.png`). After Manager exit, an independent Exec
client queries the actual mapping, reads known data, verifies write refusal,
and compares the saved file byte-for-byte with the expected description before
ejecting. The remaining public-service export/reimport suite passes, and the
runner exits zero. An initial main-window close input dragged the title bar;
that capture/input log is retained, and normal close subsequently succeeded.

This run uses RAM-backed pool members: it does not add cold-persistence or host
zdb qualification, format the mapped volume, or prove a desktop icon appeared.
Saving a mount file intentionally does not perform Mount or Format. Successful
RAM-backed GUI test ISOs are now pruned after terminal storage PASS; active,
failed and persistent-backed GUI directories are excluded. The six isolated
retention-policy tests cover that distinction. Screenshots and logs remain.

Successful persistent public-service runs now prune their disposable ISO,
backing and extracted member images after cold verification and independent
`zdb` checks. Logs, screenshots, source snapshots, binaries and original hash
manifests remain, together with `pruned-images.jsonl` recording each removed
image's path, size and SHA-256. Removed images cannot be reopened; reproduce the
fixture from the retained inputs when needed. `AROS_TEST_KEEP_IMAGES=1` opts out
only when continued image inspection is needed. Failed/incomplete runs remain
untouched. This prevents verified multi-gigabyte VM images accumulating by
default. The cleaner checks for open files and only selects fixed disposable
filenames within this repository's verified `public-service-run.*` directories.

For existing runs, preview with
`python3 aros/distribution/prune-public-test-images.py`; add `--apply` to prune.
The six isolated retention-policy tests are run with
`python3 aros/distribution/test-prune-public-images.py`.

The opt-in `AROS_TEST_ZVOL_FILESYSTEM=1` additionally requires the public device
binary and a persistent run. Negative run `public-service-run.KntFAIKw`
formats a sparse 64 MiB volume, writes
and rereads a 128 KiB file, enumerates its directory and DOS volume, refuses
eject while mounted, and shuts the handler down successfully. On the fresh
read-only import, file enumeration passes but FAT `Info()` does not report
`ID_WRITE_PROTECTED`; the test stops there rather than weakening the check.
The actual device-level read-only checks immediately before it pass. The failed
images and logs are retained to distinguish the selected FAT overlay from a
zvol integration defect.

Follow-up `public-service-run.0zjdMRxB` passes those unchanged assertions with
the private combined flush/read-only FAT overlay `flush-overlay.b4i_bpe7`.
All three cold mirror imports (healthy, one member absent, restored) enumerate
the retained file and DOS volume, report protected media, refuse file creation,
deletion and rename, reread the exact 128 KiB payload, refuse mounted-device
ejection, and cleanly stop the filesystem before eject/export. Independent host
`zdb -bcc` reports no leaks; cold member hashes remain unchanged. Successful
VM images were pruned automatically after verification, retaining logs and
the deletion manifests. This is clean-export FAT-on-zvol qualification, not
stock-nightly, abrupt-power-loss, FFS, or visual Wanderer testing.

The missing dependency is the existing
`aros/platform/fat-device-readonly.patch`: it checks `TD_PROTSTATUS` before
admitting cached mutations and when reporting disk state. Device-level write
rejection alone is insufficient for a write-back filesystem. The selected old
flush-only FAT handler lacked this correction. No OpenZFS engine/device code
or test assertion was changed to bypass the failure.

FFS follow-up `public-service-run.gjhbtU9u` passes the same public-package
filesystem lifecycle with `AROS_TEST_ZVOL_FFS=1` and private AFS overlay
`afs-zvol.QXh7pd4A`. The actual DOS type is checked as FFS. All three cold
mirror imports enumerate the file/volume, report protected media, refuse all
nine create/delete/rename attempts, preserve the exact payload and shut down
before eject/export. Independent `zdb -bcc` reports no leaks and unchanged
member hashes; successful images were automatically pruned.

AFS opens its own write-protection requester directly, so the ordinary caller
requester-suppression setting is not sufficient. The first run `rmdV2dLQ`
timed out at that requester after passing the writer phase; its screenshot and
logs are retained, not counted as a passing cold run. The harness now records
each specifically announced negative operation and sends only the supported
Left-Amiga+B Cancel shortcut while that operation is pending, with a bounded
input count. It captures the requester display and still requires the returned
`ERROR_DISK_WRITE_PROTECTED` and subsequent exact read. Nine expected/resolved
markers and `ffs-denial-*.ppm` captures are retained in the passing run. This is
controlled requester interaction in a private VM, not suppression of AFS error
handling or a visual desktop-volume test.

Build the explicit private AFS dependency with the same `AROS_RELEASE_BUILD`,
`AROS_BASELINE_SOURCE`, and `AROS_CC` settings using
`bash aros/platform/build-afs-zvol-overlay.sh`. Supply its `afs-handler` as
`AROS_TEST_AFS_HANDLER`; the helper retains the existing single ACTION_DIE
reply correction and verifies its shared input hashes remain unchanged.
`AROS_AFS_BASE_PACKAGE` is optional when a complete filesystem package overlay
is also needed. No shared AROS artifacts are modified.

To reproduce the combined private FAT handler, set `AROS_RELEASE_BUILD` to the
configured PC/SMP build, `AROS_SOURCE` to the AROS source tree containing the
FAT cache/packet flush fixes, `AROS_BASELINE_SOURCE` to its matching baseline
source, and `AROS_CC` to the cross compiler. Run:

```
AROS_FAT_ONLY=1 AROS_FAT_READONLY_FIX=1 python3 aros/engine/build-flush-overlay.py
```

Pass the resulting `fat.handler` as `AROS_TEST_FAT_HANDLER`. The helper changes
only a new private output directory, uses the configured build's remaining FAT
objects, and records source/object/header/output hashes. It is not a fresh whole
AROS build. Omitting FAT_ONLY also builds the historical POSIX fsync fixtures
and requires `AROS_PRIVATE_PTHREAD` to name the matching private pthread build
directory. That broader path was not rerun for this qualification.

Start with isolated hosted AROS or QEMU and newly created disposable disk files.
Never point recovery, fault-injection, formatting, or pool-creation tests at a
real data disk. Retain commands, source revisions, binary hashes, and logs.
Test create/write/read/export/import, cold boots, repeated application runs,
and degraded/recovery behavior separately. Hardware durability requires its own
qualification and is not inferred from QEMU success.

### Target SDK ABI and desktop startup

For a MetaMake development bundle, `aros/distribution/stage-mmake.py` accepts
the private build directory, a completed GUI public-service run, a completed
cold public-service run, `--output-parent`, `--encrypted-root-run` pointing
to a completed native encrypted-root regression, and explicit `--fat-handler`
and `--dos-package` prerequisite paths. It binds selected application
and POSIX runtime bytes to both runs' input hashes (Manager to the GUI run),
checks full shared-engine symbols and release options, and creates a new
staging directory with SHA256SUMS and evidence metadata. Platform overrides
are separated under Platform/, never silently installed. The FAT handler is
bound to persistent-test input hashes; the DOS package must match all three
runs. The changed-pool healthy/degraded/restored cold suite and all five member
hashes are also required. Four companion utilities are bound to their native
startup/host-identity/runtime acceptance evidence.

The pool-root-fix development archive `mmake-bundle.knv27h5n.tar.xz` has SHA256
`091b410750703c3bb60ef96d2534b1859191186975caf2bfda25a1a0f985fa39`.
It contains the native root ZPL initialization fix from `e6a8971`. The package
gate binds its engine, CLI, handler and companion utilities to cold-startup run
`public-service-run.JGCaHIi6` and its Manager to desktop run
`native-gui-run.dHKuj0ax`. Wanderer displayed the mounted volume and files;
post-UI exact reads and export/import passed. The cold run also passed three
persistent enabled startup restores, healthy/degraded/restored pool-root reads,
read-only write denial, unchanged member hashes and independent zdb checks.
Stage and freshly extracted manifests pass. Run `public-service-run.lXNUEfyn`
then passed the full eight-CPU RAM-backed upstream acceptance suite using the
extracted production binaries and an external acceptance client. Evidence and
screenshots are in `aros/tests/evidence/root-zpl-package`. This is a local
development archive, not an uploaded release or physical-storage qualification.

The earlier key-format development archive `mmake-bundle.w5_grp_3.tar.xz` has SHA256
`59c47b607213fa6144188c22bef933026cdeefcd7ed9f6a2631aedc8080a88b7`.
It binds the engine/CLI/handler to persistent run `public-service-run.8Cq8R435`
and Manager to interactive run `native-gui-run.l9Yuy70p`. The latter confirms
raw-to-passphrase replacement, PBKDF2 iterations and key location through the
GUI, followed by independent property and old/new key checks. All staged and
freshly extracted manifest entries pass. Run `public-service-run.igbcD2Pq`
then passes the full native RAM-backed suite using the extracted engine,
service, CLI, handler and companion tools, with an external acceptance client.
Manager's GUI evidence and cold-import evidence are the matching earlier
runs, not additional phases of this package test. Evidence is retained in
`aros/tests/evidence/key-format-package`; the two GUI/package runs pruned
9.14 GiB of disposable ISOs. This is a local development archive, not an
uploaded release or stock-nightly/physical-media qualification.

The earlier key-verification development archive `mmake-bundle.av7y0i95.tar.xz` has
SHA256 `e31fb588285507270effd58ea2dc4b1726df426351759c7caff6f8f0d9591801`.
It binds the updated engine to cold run `public-service-run.NJTixzKq` and
Manager to interactive run `native-gui-run.Q2LH3GdX`. After fresh extraction,
its manifest passes and `public-service-run.F2mgXwRH` runs the packaged engine,
service, CLI, handler and companion binaries through the full native RAM-backed
suite, including key verification/delegation/formats. The acceptance client is
external to the package. Manager's interactive proof is the matching earlier
GUI run, not this headless package run. Initial attempt `GRk4JYkt` stopped
before boot because the command named `L/OpenZFS-handler` instead of the
archive's case-sensitive `L/OpenZFS-Handler`; correcting the command required
no archive change. Evidence is in `aros/tests/evidence/key-check-package`.
The explicit patched-runtime and physical-media limitations still apply.

Earlier development archive `mmake-bundle.mdsk7pku.tar.xz` includes the
destruction dry-run worker and corrected Manager confirmation. Its SHA256 is
`963038867f9b2f2474ccbde1c13d8dc46f5b3692e986d85572142754b694e821`.
Staging binds Manager to GUI run `native-gui-run.DCRePTo0`, including the
independent unchanged-snapshot/hold checks, and the matching engine/CLI bytes
to cold/writable-release run `public-service-run.fkCVCuGC`. The staging and
freshly extracted manifests pass. This is archive-integrity verification
against those runtime tests, not an additional runtime test of the extracted
archive or a stock-nightly qualification. The earlier archives remain unchanged.

Bundle `mmake-bundle.hktsxy4r.tar.xz` was unpacked and checksum-verified in a
fresh directory; native `public-service-run.l41Mia4v` passes the full RAM-backed
suite using those extracted binaries. Its SHA256 is
`0740ffcbdbd0257ea7ee6a177805ed43d5e1d3bd3cbd573b13f4b9b600fce234`.
The preceding `RNo1BZTY` failed before service startup because the private
test adapter mapped a symlink into the ISO, producing a self-referential
`Libs/openzfs.library`. Resolving the verified library before ISO mapping fixes
the harness; the archive bytes did not change. This is not a runtime-engine
fix. The package still requires the documented matching alpha18 platform.

The target triple alone does not identify a compatible AROS SDK. In particular,
the older release SDK used for the public storage fixtures has
`offsetof(struct Process, pr_MsgPort) == 0xb0`, whereas the alpha18 SDK has
`0x130` (SMP Task padding). Static startup code and pthread must be rebuilt for
the target layout along with the application. Do not deploy an older-ABI
service, handler or library merely because a newer-ABI Manager opens.

The native Manager build helper now records compiler/SDK identities, hashes of
Task/Process/MsgPort headers and `libautoinit.a`, and an assembly dump of their
actual compile-time layout (`*.target-abi.s`). It verifies those SDK inputs did
not change during compilation. These are provenance records, not an automatic
runtime compatibility check.

For a no-service startup test, build a complete private pthread against the
matching SDK, then build Manager with that same SDK and pthread snapshot. Run:

```
AROS_TEST_ISO=/path/to/matching.iso \
AROS_TEST_MANAGER_BINARY=/path/to/OpenZFSManagerNative \
    bash aros/ui/test-qemu-no-service.sh
```

This fixture has no writable disks, networking, service or pool. First verify
the shell-launched Manager displays Service unavailable and closes normally.
It then starts Wanderer and launches a RAM copy through WBRun with a real TOOL
icon (Calculator's icon is used only inside this private fixture). Without a
real icon WBRun selects the CLI path, so that is not Workbench-startup proof.
Verify the second window, close and repeat the Workbench launch. The runner
does not infer success from window creation or process exit: retain screenshots
and logs, stop only its recorded QEMU instance when finished, and remove its
disposable ISO after recording its hash. No physical testing is performed.

On 2026-10-22 the alpha18c5 private guest showed the rebuilt Manager's accurate
Service unavailable response in shell and real-icon Workbench launches
(`manager-startup-run.i9xix86y/01.png` and `14.png`). The Manager hash was
`9622c6404ca73fdfe883b499ea2915a412557d8b3a8f63e046cfa04635ce2265`, using the
current `aros-adt-pc-x86_64-clean` SDK and private pthread `ARVzbA2v`. This is
startup/UI evidence only. The reported older laptop binary's `.text+0x20819`
matches GetMsg in old-ABI autoinit before main; its exact installed identity and
runtime pairing must still be confirmed before closing that report.

The corresponding private alpha18 backend migration has **RAM-backed and
cold-persistence QEMU qualification**, not physical-device qualification.
`mmake-alpha18-20261022` rebuilt all 360 engine objects, nvpair, the shared
library, service launcher, handler, device, Manager and public test client.
It uses fresh pthread `ARVzbA2v`, fresh crypto `h7Dc9176`, and a privately built
current genmodule; configured platform inputs are still supplied by the SDK.
That SDK's POSIX import library/configuration does not expose `pipe2`.
`aros/platform/alpha18-pipe2.patch` is a companion candidate against the
alpha18 POSIX source: it appends a dispatch vector, preserves existing vectors,
sets CLOEXEC before publishing descriptors, and rejects unsupported nonblocking
pipes. Rebuild the entire matching POSIX library and its generated import stub;
do not mix an older ABI overlay or stub into alpha18. The private build also
required matching `libraries/fd.h` and `dos/consoleinfo.h` headers absent or stale
in that SDK. This is not yet a turnkey stock-SDK build.

With PIPE: mounted, `manager-startup-run.pkpyisEB` passed 64 sequential native
pipe iterations (flags, data, EOF and invalid arguments). The initial missing
PIPE: mount failure is retained; this is not concurrent fork/CLOEXEC proof.
`public-service-run.ChaklSOj` then passed the full RAM-backed public suite on
alpha18c5: production CLI, handler and device, sparse files across 4 GiB,
replication including locked raw incremental encrypted streams, and passphrase
rotation with old-key refusal. The ISO's kernel, DOS, ATA and zlib were untouched;
only the new POSIX library and matching OpenZFS package were mapped in.
`AROS_TEST_BASE_PLATFORM=1` selects this mode and rejects simultaneous legacy
platform-overlay inputs. Logs, source snapshots and input hashes are retained.

Cold mirror run `public-service-run.WiWLybR2` passed fresh-boot healthy,
one-member-missing and restored reads, total-loss import refusal, encrypted and
rotated-passphrase datasets, and resident-device/zvol checks. Member hashes
remained unchanged and independent host `zdb -bcc` reported no leaks. Its
disposable images were pruned (9.78 GiB); logs and hashes remain.

This required a matching private FAT rebuild, not the old two-object overlay:
the current source adds `validate.c`. `AROS_FAT_FULL_REBUILD=1` rebuilds all FAT
sources plus configured generated startup against one SDK. With that source,
the initial `k02nVDqP` run and flush-only `HcN9UKMR` attempt left the FAT clean
shutdown bit unset despite guest detach success. `fat-flush-clean.patch`,
enabled with `AROS_FAT_CLEAN_FLUSH=1`, commits the marker and its cache block on
explicit flush and before orderly inhibit clears the active volume pointer.
It propagates cache/device failures instead of acknowledging a completed detach.
This patch is separate from physical-media removal and does not prove power-loss
durability. The persistence runner now additionally requires read-only host
`fsck.fat -n` checks after each guest phase; it never repairs test images to pass.
Repeat `public-service-run.D3BzNOn1` passed both new FAT checks and the full cold
suite; another 9.78 GiB of disposable images was automatically pruned.

The larger retained add/attach/replace fixture exposed a separate FAT BPB bug:
geometry padding was counted as allocatable filesystem clusters. The private
`AROS_FAT_BPB_BOUND=1` full rebuild applies `fat-bpb-volume-bound.patch` and links
a matching private parser object, without changing the SDK archive. Host parser
tests reproduce the old off-by-one allocation and verify the correction.
Native run `public-service-run.yp3paeiP` passes read-only fsck after both boots,
changed-pool cold recovery, full-member byte comparisons and independent zdb.
See `aros/tests/evidence/fat-bpb-bound` and `changed-pool-cold`; the earlier local
bundle does not include this FAT prerequisite.

Native destroyed-member changes can be tested with `AROS_TEST_NATIVE_CHANGES`
set to `1` (CLI attach/replace), `2` (Manager API attach/replace), `3` (CLI add),
or `4` (Manager API add), alongside `AROS_TEST_REQUIRE_PARTITIONS=1` and
`AROS_TEST_NATIVE_DISCOVERY=1`. Each invocation creates fresh private MBR
fixtures and requires its exact result marker. Do not combine with native-cold
mode. See `aros/tests/evidence/native-member-changes` for four passing runs and
the distinction between mixed native/file pools and all-native qualification.

`AROS_TEST_GUI_DESKTOP=1` enables an interactive Wanderer fixture in the public
GUI runner. It leaves Wanderer running, asks the tester to inspect OZPUBLIC
and hello.txt, and rereads the file after Manager closes. This read assertion
does not prove the desktop icon or drawer presentation. The first attempt,
`native-gui-run.yVOHUdr6`, timed out during the startup-ABI investigation; it
is retained as incomplete, not desktop qualification.

The alpha18 live-service attempt `native-gui-run.M4jeHkSj` showed Manager
connecting and listing `public_mirror` ONLINE with capacity figures (`04.png`).
Manager closed normally and the post-UI file read passed. However, Wanderer was
not visually established; the later packaged
handler detach assertion failed at public-service-test.c line 1371. This run is
retained as a failure, not desktop integration proof. The logged CrashReporter
line was subsequently verified to be ordinary startup, not a crash report.
Diagnostic run `sZ5k0Gfn` identified EBUSY; `Sg7uBeHS` cleared it on the first
5-tick retry, then failed at a later teardown with the same one-shot assumption.
The fixture now gives ordinary desktop teardown at most 50 such retries, only
for EBUSY. All deliberate held-reference refusal tests remain single attempts,
and persistent busy still fails. No handler busy protection is bypassed.
The desktop fixture now explicitly asks WBRun to open OZPUBLIC: for visual
inspection; command success alone is not drawer or file-display evidence.

The GUI startup now runs the test client asynchronously and closes its original
shell, and supplies the normal default THEME assignment. In
`native-gui-run.YMOOWzut`, this exposed the mounted OZPUBLIC icon; after cancelling
the then-missing-THEME requester, screenshot `03.png` visibly shows `hello.txt`
in its Wanderer drawer. Closing the drawer and Manager normally left Wanderer
running and the remaining storage suite passed. The next run, `kVHij1YN`, has no
missing-theme requester and also passes post-UI exact reads and storage teardown,
but its drawer remained empty during visual inspection (and the initial WBRun
drawer title rendered incorrectly). Thus desktop presentation is not yet
reliably qualified. Retain both observations rather than replacing the failed
visual evidence with the successful storage result. The private client records
screen/window names without modifying Wanderer; CrashReporter's ordinary startup
message must not be called a crash.
