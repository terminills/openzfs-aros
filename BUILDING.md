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

Tools inherit the configured target compiler, SDK, flags and output directories.
`OPENZFS_PTHREAD_INCLUDE` and `OPENZFS_PTHREAD_LIBDIR` can select matching private
pthread inputs when the configured SDK does not yet include the required port.
`OPENZFS_TOOLSDIR` can override the Manager's installation directory.

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
command execution and cleanup. Rollback here uses an unchanged snapshot and
does not independently prove data restoration. Initial runs correctly refused
LZ4 because the minimal fixture had not enabled its feature. Explicit child
stderr capture exposed that upstream diagnostic; the trailing DOS error alone
was misleading. The test now enables and verifies the feature through `zpool`
before setting the dataset property, without weakening feature validation.

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
