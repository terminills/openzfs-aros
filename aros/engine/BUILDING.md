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
`AROS_CC`, `AROS_ZLIB_IMPORT`, `AROS_CRYPTO_INCLUDE`, `AROS_CRYPTO_ARCHIVE`.
The repository's pinned extracted libtirpc headers are required under
`build-aros/libtirpc-1.3.7`; the default nvpair build verifies the archive's
pinned SHA256 and extracts it if absent. It can download the pinned archive
if necessary. SDK, pthread, zlib and crypto are not bootstrapped here.

Run from the checkout:

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
service packaging, not the unfinished shared-engine production layout. Debug
configuration remains the coherent configuration in `aros/engine/config.h`.
A completed build is not evidence of runtime correctness or warning-free code.

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
