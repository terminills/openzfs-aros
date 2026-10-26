# Port status

Experimental development snapshot; not production-qualified storage software.

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
