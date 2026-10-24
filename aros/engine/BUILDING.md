# Fresh source engine build

## Clean rebuild qualification, 2026-10-23

`source-engine.vhKxMXuk` rebuilt all 248 inventory source units and nvpair/XDR
from source, then linked the full service and current standalone acceptance
client. Source and output hashes were rechecked successfully. Shared candidate
`shared-engine.eUg6BS3T` was built from that new engine object and nvpair archive,
with explicit private pthread and crypto dependencies (not freshly rebuilt in
this run). No prior engine objects seeded this build.

Native run `public-service-run.KKc6pmMp` passed the persistent public storage
suite, including trusted ENVARC startup restoration and healthy, degraded and
restored cold imports. Independent `zdb` verification and unchanged pool-member
hashes pass. Selected logs and manifests are committed under
`aros/tests/evidence/fresh-engine`. This does not replace the prior package's
binary identity or qualify physical storage; the new candidate still needs its
own GUI/package qualification.

The first attempt, `source-engine.E3DnS3hs`, compiled all engine units but failed
to link the acceptance client: the standalone management builder omitted the
command-worker, process and startup-record dependencies already present in
MetaMake. Commit `266a8fc` fixes that source/dependency list. The failed log is
retained locally; the second build above started with a fresh output directory.

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

### Building the crypto dependency independently

`build-crypto.sh` builds BoringSSL's full `crypto` target in a private directory,
without invoking Chromium's build or consuming its compiled archive. Host
CMake, Ninja, Perl and Go are required. To prepare its source independently:

```sh
bash aros/engine/prepare-crypto-source.sh
```

This fetches upstream BoringSSL commit
`4fa4804c8ab4521079af62dba5260a99c34b8a29` into a new private checkout and applies
`aros/patches/boringssl-aros.patch`. It prints the `AROS_CRYPTO_SOURCE` path to
export below. `AROS_BORINGSSL_REPOSITORY` may select a local mirror, but the
expected commit remains pinned. The helper never edits a supplied checkout.
The pin reproduces the tested dependency, not a claim that it is the latest
security-reviewed BoringSSL. Review and requalify upgrades separately.

```sh
export AROS_CXX=/path/to/x86_64-aros-g++
export AROS_CRYPTO_SOURCE=/path/to/boringssl/src
export AROS_HOST_GO=/path/to/host/go
export AROS_BUILD_JOBS=8
bash aros/engine/build-crypto.sh
```

Keep the SDK, C compiler and matching pthread headers selected as above. Use
the printed `AROS_CRYPTO_ARCHIVE` and `AROS_CRYPTO_INCLUDE` together. The helper
hashes the source tree and consumed pthread/compatibility headers before and
after building, retains compiler/configuration/build logs, and limits jobs to
1-32. Go caches and all CMake outputs stay private; nothing is installed.
The local forced-include shim selects actual pthread limits over SDK POSIX
minima. Missing trailing static semaphore initializer fields remain warnings
(C zero-initializes them); other upstream warning-error policy is unchanged.

`crypto-source.GIQEzBFj` completed the 234-step native crypto target on
2026-10-22. Shared candidate `shared-engine.98HB7sbf` uses that archive and
current ADT pthread. Native run `public-service-run.GXf0nnuv` passed a real
passphrase-encrypted dataset lifecycle at 350000 PBKDF2 iterations, including
write/flush, key unload, locked mount denial, reload and exact DOS readback.
The existing raw-key full/incremental replication and cold mirror recovery
suite also passed with unchanged members and independent zdb validation.
This is not a FIPS certification or a fresh execution of standalone PBKDF2
known-answer tests. Failed configure/build attempts `uB3vNled`, `4XEQYWtC`
and `gjcbbSZz` remain retained alongside the successful build.

The subsequent `public-service-run.bZtMJ64h` executes the standalone native
PBKDF2 test `pbkdf2-test.KoB9JNRh` linked against that independently built crypto
archive. RFC 6070 vectors at 1, 2 and 4096 iterations, the independently checked
32-byte OpenZFS salt/350000-iteration vector, and invalid-input output clearing
all pass. The packaged dataset and cold-recovery suite also passes in that run.
The builder `build-pbkdf2-test.sh` now requires explicit SDK/compiler/crypto/
pthread inputs and creates a new private evidence directory instead of
overwriting a shared test binary. Pass its output executable through
`AROS_TEST_PBKDF2_BINARY` to the native public-service runner; when supplied,
that runner requires the known-answer PASS marker as well as its storage gates.

The independent upstream fetch was subsequently tested end to end:
`crypto-input.97AxCjOW` fetched the pinned commit from the default upstream
URL and applied the bundled patch. Its non-Git files match the prior selected
AROS-compatible source tree. Archive `crypto-source.bC1xHzNG`, known-answer
test `pbkdf2-test.mwrxbwBR`, and shared candidate `shared-engine.gzDMgVs4`
passed native run `public-service-run.tFEjoFyz`, including PBKDF2, the actual
passphrase dataset, raw replication and cold mirror recovery. Host zdb reports
no leaks and cold reads leave member files unchanged. This removes the need
for a Chromium source checkout or compiled crypto archive from this path.

`public-service-run.2hBHnHsY` also qualifies upstream `zfs change-key` through
the native commands/service. It rotates a written passphrase dataset from
350000 to 400000 PBKDF2 iterations, verifies the new property, unloads the key,
requires the old passphrase to fail with the dataset still locked, then loads
the new passphrase and reads the exact original payload through the handler.
The test destroys that disposable dataset afterward. The surrounding cold
raw-key mirror suite passes; this run does not yet prove cold persistence of
the rotated passphrase itself.

The following persistent test retains the rotated dataset with its key unloaded
and removes both RAM passphrase files before export. Fresh-boot run
`public-service-run.4C5TK2Ta` verifies the stored 400000-iteration property,
locked mount refusal, old-passphrase rejection without unlocking, new-passphrase
load and exact payload read. Healthy, degraded and restored imports all pass,
then unload the key again. Member-file hashes remain unchanged and independent
zdb reports no leaks. The fixture passphrases are public test strings recreated
only in guest RAM; this does not test secure memory erasure or physical media
power-loss behavior. Nonpersistent runs still destroy the disposable dataset.

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
