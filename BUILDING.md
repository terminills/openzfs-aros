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

Tools inherit the configured target compiler, SDK, flags and output directories.
`OPENZFS_PTHREAD_INCLUDE` and `OPENZFS_PTHREAD_LIBDIR` can select matching private
pthread inputs when the configured SDK does not yet include the required port.
`OPENZFS_TOOLSDIR` can override the Manager's installation directory.

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

This integration is not yet a complete `make contrib-openzfs` port: shared-engine,
upstream CLI, startup targets and full package verification remain to be
wired and tested. The standalone build sequence below remains available.

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
