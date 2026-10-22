# Fresh source engine build

`build-source.sh` creates a new private `build-aros/source-engine.*` directory.
It compiles the complete libzpool and x86-compatible crypto/AVL/Unicode/zstd
source inventories before invoking the full native dispatcher/service build.
No engine objects are copied from a previous successful test directory.

Required environment variables:

- `AROS_SDK`: ABI-matched native AROS Developer directory.
- `AROS_PRIVATE_PTHREAD`: complete private pthread archive.
- `AROS_PTHREAD_INCLUDE`: that archive's matching source/header snapshot.

`AROS_NVPAIR_ARCHIVE` is optional. If omitted, the build compiles native
nvpair and the pinned libtirpc memory-XDR codec into the new build's own
`nvpair/` directory. Supplying it explicitly selects a prebuilt ABI-matched
archive. The native nvpair test links the same private pthread archive, not
an implicitly selected SDK pthread library. No test output is installed.

Optional tool/dependency selections are inherited by the existing build tools:
`AROS_CC`, `AROS_ZLIB_IMPORT`, `AROS_CRYPTO_INCLUDE`, `AROS_CRYPTO_ARCHIVE`,
`AROS_TIRPC_SOURCE`, and `AROS_TIRPC_ARCHIVE`.
Extracted libtirpc headers are selected consistently by nvpair and engine
compilation through `AROS_TIRPC_SOURCE` (default `build-aros/libtirpc-1.3.7`).
The default nvpair build verifies the archive's
pinned SHA256 and extracts it if absent. It can download the pinned archive
if necessary. SDK, pthread, zlib and crypto are not bootstrapped here.

### Building the private pthread dependency

`build-private-pthread.sh` can build the complete pthread source set from an
explicit AROS source checkout; an old private archive is not required:

```sh
export AROS_CC=/path/to/x86_64-aros-gcc
export AROS_SDK=/path/to/matching/Developer
export AROS_PTHREAD_SOURCE=/path/to/AROS/compiler/pthread
bash aros/engine/build-private-pthread.sh
```

Use the printed `AROS_PRIVATE_PTHREAD` and `AROS_PTHREAD_INCLUDE` paths together
for subsequent builds. The helper snapshots the sources, records their Git
revision and local changes when available, compiles every listed source unit,
and verifies snapshot hashes after compilation. It never installs into the SDK.
Do not combine a new core object with an older archive's remaining objects.
If that source requires a newer `dos/dostags.h`, `AROS_PTHREAD_DOSTAGS` explicitly
selects a matching header for the private snapshot. This is not a substitute
for a runtime implementing those tags; do not invent tag values to force a build.
Public header changes require rebuilding consumers, not merely relinking them.

On 2026-10-22, `pthread-consistent.tNXnCvVm` compiled all 35 units from clean
ADT pthread sources at `671efe3b321926332e3330a0e138ed4a3d4e820e` using the
native SDK and explicit compatible dostags overlay. The three public pthread
headers match the prior engine build. Shared candidate `shared-engine.p1N5gZAY`
relinks the qualified engine against this complete new archive and freshly
built nvpair. `public-service-run.6XxQhdGd` passed the native persistent mirror
suite: CLI/handler, sparse64, full/incremental raw encrypted replication,
healthy/degraded/restored encrypted cold reads, excessive-loss refusal,
unchanged members and independent zdb (no leaks). This qualifies that selected
dependency replacement, not an unpatched stock runtime or a new full SDK.
The prior frozen binary handoff was not overwritten.

Run the engine build from the checkout:

```sh
bash aros/engine/build-source.sh
```

The script reports its evidence directory. It records the checkout revision,
worktree patch, source inventory/hashes, individual compile logs and summary,
link log, generated revision-header hash and output/dependency binary hashes.
The generated revision header lives inside this new build. The compiler's
`AROS_GENERATED_INCLUDE` override avoids the old shared generated-header path;
existing callers retain their previous default. ZPL and full-dispatcher variants
are freshly compiled by the linker, not taken from the initial common objects.

Outputs include `service/OpenZFSServiceNative`, `service/OpenZFSManagerNative`
and native verification helpers. This is the existing monolithic development
service packaging. The shared-engine packaging is built separately using
`aros/library/build.sh`; see its README for provenance requirements. Debug
configuration remains the coherent configuration in `aros/engine/config.h`.
A completed build is not evidence of runtime correctness or warning-free code.

## Isolated hosted security and storage regression

`test-hosted-security.sh` requires explicit `AROS_CC`, `AROS_SDK`, and
`AROS_TEST_SECURITY_LIBRARY`. Set `AROS_TEST_HOSTED_RUNTIME` to a compatible
hosted runtime tree; the script copies it into a new private artifact directory
and starts its own X server. It never runs the supplied runtime in place.
The usual 100 GiB storage reserve applies before copying.

Set `AROS_TEST_HOSTED_SERVICE_BUILD` to the fresh engine's `service/` directory
to test storage as well as login/logout. For the shared-engine configuration,
also set `AROS_TEST_SHARED_BUILD` to the output of `aros/library/build.sh`.
Its binary hashes and engine-object provenance are checked before staging its
launcher and library; the matching service build supplies the regression tool.

`AROS_TEST_HOSTED_PERSISTENT=1` creates two disposable 128 MiB files in the
private runtime, checks the exported mirror with host `zdb`, and boots a cold
reader. `AROS_TEST_HOST_ZDB` and `AROS_TEST_HOST_LIBRARY_PATH` select the host
verifier and its library search path when not under `build-control`.
The cold reader checks exact DOS file data, write refusal on read-only mounts,
and degraded operation with one member hidden, then restores that member.
Both original file hashes must remain unchanged after the read-only phase.

On 2026-10-21 the public-source shared build passed both the storage regression
and this persistent mirror/cold-reader sequence. The independent host verifier
reported no leaks. Evidence directories: `hosted-security-test.bRTIKnB7` and
`hosted-security-test.tirxqAQ5`. This validates the selected hosted cases, not
stock-nightly compatibility, physical media durability, or all OpenZFS features.

## Build evidence, 2026-09-28

`source-engine.72CYAuks` compiled all 248 inventory units, then linked the full
service and Manager, exit 0. It uses the matched pthread s3r1CHla archive and
native nvpair archive with the configured native SDK. Its private revision
header records c3c3395 plus dirty source changes, rather than the old shared
header's 4ae1a38 revision. Logs: `engine-fresh-source-2.log` and the build's
individual logs/link.log. Source warnings are retained.

First attempt `source-engine.IB7g2FOw` exited before compilation because the
upstream revision generator rejects an absolute output filename. The builder
now passes the equivalent repository-relative path. That failed run and
`engine-fresh-source-1.log` remain as evidence; they are not runtime failures.

The fresh service subsequently passed the complete native-import QEMU runner
in `native-import-run.HGyjHiCX`, exit 0, with both original backing-disk hashes
unchanged. It used freshly built commands `cli-link.L12HV1ow`, including
plain/raw CLI resumption, eight reused pipelines, strict private reader-close
policy, native DOS read/write/export/reimport, and permission checks. Evidence
is registered under the development test archive; log: `engine-fresh-native-1.log`. Manager compiled
but was not visually exercised in this headless run. Private DOS/POSIX/ATA and
other OS overlays are still required; hardware, stock-nightly compatibility,
shared-engine packaging and dependency bootstrap remain unproven.

`source-engine.5M5ATbJJ` then rebuilt with no supplied nvpair archive: the
builder compiled its own nvpair/XDR dependency, all 248 engine inventory
units, full service and Manager. Build exit 0 is retained in
`engine-fresh-nvpair-1.log`; the dependency compile log and binary hash are
inside that build. Full native run `native-import-run.8Iqd3UEE` exits 0 with
unchanged original backing hashes, using this new service and CLI L12HV1ow.
Evidence is registered under the development test archive; log: `engine-fresh-nvpair-native-1.log`.
This exercises the new nvpair through the service; the separately built
OpenZFSNvpairTest was not executed and is not claimed as a standalone pass.
The CLI in this run retains its previously verified dependency archive.
