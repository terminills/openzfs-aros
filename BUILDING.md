# Building the AROS port

## AROS build-system integration (in progress)

The root `mmakefile.src` uses native `%build_prog` templates, not the standalone
shell build scripts. Place the source in a directory scanned by the configured
AROS Contrib build and regenerate its metamake files. Available targets:

- `contrib-openzfs-tools`: Manager, host-identity, runtime-check and device tools.
- `contrib-openzfs-manager`: `Tools/OpenZFSManagerNative`.
- `contrib-openzfs-hostidentity`: `C/OpenZFSHostIdentity`.
- `contrib-openzfs-runtimecheck`: `C/OpenZFSRuntimeCheck`.
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

This is a developer source release, not a standalone SDK or prebuilt installer.
The ordinary upstream Linux build does not build the AROS integration.

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

Start with isolated hosted AROS or QEMU and newly created disposable disk files.
Never point recovery, fault-injection, formatting, or pool-creation tests at a
real data disk. Retain commands, source revisions, binary hashes, and logs.
Test create/write/read/export/import, cold boots, repeated application runs,
and degraded/recovery behavior separately. Hardware durability requires its own
qualification and is not inferred from QEMU success.
