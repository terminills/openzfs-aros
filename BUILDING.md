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

Prefer a coherent SDK and genmodule pair; do not define missing ABI constants
in this project. Handler/device compilation is not runtime qualification.

For isolated build-system testing, redirect `AROSDIR`, `GENDIR`, `DBGDIR`,
`KOBJSDIR`, and host-tool outputs, while
selecting the intended read-only SDK via `AROS_DEVELOPER`. Overriding `AROS_FS`
alone does not redirect generated module installation. AROS make can rebuild
`GENMODULE` automatically from source dependencies, so its output must also be
private; an executable-path override alone does not pin its version.

The aggregate target is wired, but external dependency bootstrap and a complete
clean-tree aggregate/package verification remain outstanding. The standalone
build sequence below remains available.

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
