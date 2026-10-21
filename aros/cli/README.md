# Upstream command-line tools on AROS

## Pool error logs and affected paths (2026-10-04)

The normal engine now compiles the full upstream `spa_errlog.c` using the
native ZPL context. Previously its non-kernel `spa_get_errlog` was a three-byte
no-op. Native copyout uses the existing service-owned output-range checks;
no unchecked client-pointer copy or alternate error database is introduced.

The authenticated bridge admits `ERROR_LOG`, `DSOBJ_TO_DSNAME` and
`OBJ_TO_PATH`, retaining upstream privilege/delegation checks. Error-log
allocation is bounded in bytes, while upstream input/output sizes remain
bookmark counts (returned count means unused slots at the front). The client
preserves this distinction and ENOMEM. Name lookup supports upstream's reuse
of the output-only `zc_value` field between the two path queries.

Manager's read-only Pools / error details action runs the real `C:zpool
status -v` under the caller's identity without a shell. It rejects flags,
arguments and options; it never clears or repairs errors. Reports exceeding
the response window are retained at the displayed temporary path, which must
be copied before reboot. GUI verification is separate from CLI/worker tests.

Select `AROS_TEST_ERROR_LOG=1` for normal empty-log, path and non-root-denial
checks. The private `AROS_TEST_ERROR_LOG_FIXTURE=1` engine wrapper records
two real upstream bookmarks through `spa_log_error`, includes a duplicate,
and syncs them. It does not fabricate replies. Select
`AROS_TEST_ERROR_LOG_SEEDED=1` only with that diagnostic engine: the test
requires two records, ENOMEM for one slot, malformed-request rejection,
canaries, real path lookup, CLI output and scrub clearing of the healthy-file
records. Optional `AROS_TEST_ERROR_LOG_HEAD=1` enables head_errlog on the
disposable pool and uses real block birth transaction IDs. These synthetic
records test error reporting, not detection or repair of actual media damage.

Evidence under the external archive's `build-aros/`:

- `native-import-run.sbRn02gz`: initial test failed by treating DOS
  `fib_DiskKey` as a ZFS object number. It is an enumeration cursor. The test
  now resolves the object with upstream NEXT_OBJ / OBJ_TO_PATH.
- `native-import-run.8medVZkw`: corrected legacy seeded log and complete
  configured diagnostic acceptance passed (exit 0). `zpool status -v` showed
  `OZERR:/evidence`; scrub removed the records. This run predates the added
  non-root and Manager-worker assertions.
- Normal shared package `native-bundle.2c8TT97M` / `shared-engine.HlpeHtRk`
  passed separate acceptance in `native-import-run.UTF7448r` (exit 0,
  16 CPUs, 8192 MiB): empty error log, malformed requests, non-root denial,
  received-property expansion, DOS pressure, writable/readonly reimports
  and unchanged backing hashes. This run predates the Manager error worker
  and head-format assertions.

- `native-import-run.ch1c8MUS`: head-format wire, privilege, worker, and
  scrub-clearing assertions passed. The actual Manager GUI failed with
  errno 22 (`error-04.png`): request construction added `OZ_CONFIRMED` to
  the read-only action, whose worker rejects flags. Commit `432fea15`
  excludes this action from that flag.
  The original GUI PASS marker only proved launch/exit and unchanged log,
  not displayed content. It is now explicitly named GUI lifecycle PASS;
  screenshot verification remains separate. Broader configured backend
  acceptance subsequently completed with exit 0 and unchanged backing hashes;
  this does not change the negative GUI result.
- `native-import-run.cI0hZHg1`: fresh boot of rebuilt Manager after `432fea15`.
  `error-fixed-03-report.png` visibly shows upstream status exit 0 and affected
  path `OZERR:/evidence`. After closing Manager, independent CLI verification
  confirmed records unchanged, then scrub cleared them and head-format checks
  passed. Broader configured acceptance subsequently completed with exit 0,
  including CLI/vdev/zvol/repath/handler reimports and unchanged backing hashes.
  Snapshot/clone attribution is a separate follow-on test, not proven here.
- `native-import-run.2kYEJZ8s`: follow-on head-format attribution and full
  configured regression passed (exit 0). Upstream reports both a real snapshot
  and its clone as sharing the seeded blocks. After DOS overwrites and syncs
  the live file, its path disappears from the report while snapshot and clone
  paths remain. Destruction, scrub, cleanup, reimports and backing hashes pass.
  This still tests synthetic error records, not actual checksum-error detection.

Build/run logs are in `error-log-20261004/`. No DUT or user hosted session
is used.

## Received-property queries (2026-10-04)

### Client reply expansion

The bridge must preserve upstream ENOMEM and the required destination size
for both `OBJSET_STATS` and `OBJSET_RECVD_PROPS`. Otherwise datasets with
large property trees fail to open, or `zfs get -o received` silently prints
`-` despite a successful command exit. The fix remains restricted to named
read-only queries; completed import overflow behavior is unchanged.

Select `AROS_TEST_RECEIVED_EXPANSION=1` together with
`AROS_TEST_RECEIVED_PROPERTIES=1`. The test creates 96 real user properties
with 3072-byte values, sends/receives them with the upstream CLI, verifies
that a 256 KiB service reply is insufficient, and checks the first and last
received values byte-for-byte using the normal CLI. It then removes its
disposable datasets and stream.

Negative evidence under the external archive's `build-aros/`:

- `native-import-run.rQCIqu3p`: original CLI aborts in `zfs_open` during
  fixture creation; OBJSET_STATS hits the same missing resize path first.
- `native-import-run.L9MJPhjM`: stats-only corrected CLI completes the fixture
  and send/receive. The service reports 307724 required bytes versus 262144
  capacity. The received query prints `-` with exit 0; the content assertion
  correctly fails the overall runner (exit 1).

Build/control logs are in `received-expand-20261004/`. Fixed normal CLI
`cli-link.C30NwbOI` in bundle `native-bundle.QKzE3e4p` passed the content
assertions and full configured acceptance in `native-import-run.CrgYmo9l`
(16 CPUs, 8192 MiB; runner exit 0). This includes prior received/Manager API
checks, vdev properties, zvol device, library lifetime, repath, DOS pressure,
writable/readonly reimports and unchanged backing-file hashes. It uses the
private runtime and Manager validation harness, not a stock-nightly, hardware
or new GUI qualification.

The authenticated transport admits upstream `ZFS_IOC_OBJSET_RECVD_PROPS`.
It takes a dataset name and bounded output nvlist, rejects input/config
payloads and unrelated command fields, and retains upstream dataset-read
authorization. Datasets without received properties still return upstream
ENOTSUP; local values are not substituted for received values.

Select `AROS_TEST_RECEIVED_PROPERTIES=1` in the native-import QEMU runner
with a writable disposable fixture. The real `C:zfs` test sends properties
with `send -p`, receives them, distinguishes native compression and user
properties from local overrides, and restores both using `inherit -S`.
The runner requires the complete received-properties result marker.

Evidence under the external OpenZFS archive's `build-aros/`:

- `native-import-run.cNlZcoP7`: general acceptance passed, but the new test
  was not selected (incorrectly nested in the disabled transport suite).
  This is not received-property evidence.
- `native-import-run.ogbQfFZA`: selected test failed before the query because
  the fixture lacks the LZ4 feature. The test now uses gzip without upgrading
  the pool. This is not an ioctl negative control.
- `native-import-run.INZAExTK`: old production engine successfully sent and
  received the properties, but `get -o value,received compression` returned
  `gzip` and `-`; the assertion and runner failed (exit 1).
- `native-import-run.uTZ0YhVV`: rebuilt normal engine passed all received
  property assertions, including local override and `inherit -S` for native
  and user properties. The complete runner subsequently exited 0, including
  the configured CLI, vdev properties, zvol device, library lifetime, native
  repath, DOS cache-pressure, writable/readonly reimport and backing hashes.

The fixed package is `native-bundle.fJW206Ef`, built from
`shared-engine.LP15dgOh` with full dispatcher, test modes disabled and status
trace disabled. Build and runner logs are in `received-props-20261004/`.
These are private-runtime QEMU tests, not stock-nightly or hardware proof;
the acceptance package uses the Manager validation harness.

The same selection also requires `received-wire-test.c` against the real
authenticated service: ENOTSUP without received values, a one-byte reply
returning ENOMEM and the required size, twelve malformed-field/pointer
rejections, buffer canaries and a successful query afterward. The first wire
run `native-import-run.dKBFFP7E` failed a test assumption: upstream
`put_nvlist` sets `zc_nvlist_dst_filled` even on an ENOMEM sizing reply.
Production behavior was unchanged. Corrected run `native-import-run.yKgrnmAP`
passed all wire assertions and repeated the complete CLI received-property
test; its complete acceptance runner subsequently exited 0. Logs remain
in `received-props-20261004/wire{-corrected,}-qemu.log`.

### Manager received-value restoration

The existing `dataset.inherit` action accepts exactly `received=on` to select
upstream `zfs inherit -S` behavior. It removes the local override, restoring a
received value if present or falling back according to upstream inheritance
rules. The UI help and confirmation explain this. Empty options retain the
existing parent-inheritance behavior. Recursive applies either mode to the
selected dataset and descendants, matching upstream `zfs inherit -r` / `-rS`.
The walk is non-atomic and reports completed count if a later failure leaves
earlier changes in place. Force and dry-run remain unsupported; explicit
confirmation and upstream delegation are required. The typed adapter uses the
real inherit ioctl, not a second property database.

The old production package rejected this option in
`native-import-run.ySPWPKec` (runner exit 1). Updated normal package
`native-bundle.sTUuh4NA` / `shared-engine.zrpWmEUp` passed the API test in
`native-import-run.5oo2UPBa`: unconfirmed, malformed-option and force requests
rejected without changing the property, then native/user received values
restored and independently read back by `C:zfs`. Full acceptance subsequently
finished with exit 0, including DOS pressure/reimport and backing hash checks.
Logs are under the external archive's `build-aros/manager-received-20261004/`.

For actual GUI verification, also select `AROS_TEST_RECEIVED_GUI=1` and
`AROS_TEST_GUI=1`, supplying the normal Manager binary. The fixture reinstates
both local overrides and waits for two actual Manager restores before its
independent CLI value/source checks. QEMU run `native-import-run.65ATLCK8`
passed this GUI gate using normal bundle `native-bundle.E2aZO3w8` (16 virtual
CPUs, 2048 MiB). Screenshots `received-10-confirm.png`,
`received-13-native-complete.png`, `received-14-user-confirm.png` and
`received-15-user-result.png` show the reviewed targets and successful
authenticated results; `serial.log` records both CLI readbacks and
`RESULT native Manager received GUI PASS`. The broader GUI-profile run also
finished with exit 0 after closing its final Manager window: configured CLI
and import/DOS regressions passed and all backing-file hashes matched.
The 2 GiB GUI profile does not resolve the separately reported high-memory
USB input fault. No DUT or user hosted session is used.

## Current streaming status (source/evidence audit, 2026-09-30)

The send-estimate section below is historical, not the current streaming
boundary. Current `control-client.c`, `stream-client.c` and
`engine/ioctl-request.c` implement retained native send/receive transport;
client-local POSIX descriptors are not interpreted as service descriptors.
See [STREAMING.md](STREAMING.md) for the staged implementation, failures and
verification history. Retained `native-import-run.8Iqd3UEE` serial results
include real upstream CLI streaming, repeated native pipelines, and plain/raw
resume checks, followed by successful native import/DOS verification. This is
inspection of retained evidence, not a fresh runtime run of today's binaries.

Important remaining limitations include production approval/integration of
the private PIPE disconnect policy and complete POSIX EPIPE/SIGPIPE behavior.
The opt-in native policy has strict/default compatibility tests; it is not a
claim that an unmodified stock PIPE handler has those semantics. Native NFS/SMB
export providers also remain absent. Do not mark the full port complete from
streaming or command startup tests alone.

## Upstream send estimates (2026-09-28)

The authenticated ioctl transport now admits ZFS_IOC_SEND_SPACE, using the
real upstream schema, read policy and estimator. Input is the bounded packed
nvlist; extra inline command fields/history are rejected. Its optional fd is
an upstream progress cookie, not an imported client POSIX descriptor. This
operation does not write stream bytes and does not admit SEND_NEW or receive.

Service `cli-send-estimate.Iiz8N9r8` and the actual `cli-link.qLJvc5Kp` commands
pass `native-import-run.A2EcVmDw`, complete runner exit zero and original
backing hashes unchanged. Tests invoke `zfs send -nP` for an ordinary encrypted
snapshot with its key loaded, `-w` with the key unloaded, `-t` with a real raw
resume token, and `-w -i` for the raw incremental. Estimates are respectively
1152560, 1152560, 748548 and 1141296 bytes. Full/incremental must exceed the
uncompressed 1 MiB payload and resumed size must be positive. Existing raw
full/incremental/resumable management API tests and the native regression pass.
These numbers are upstream estimates, not assertions of exact stream length.

At this initial estimate-only stage, actual CLI streaming was unfinished:
SEND_NEW/RECV were not admitted and
native stdout/stdin descriptors are not yet transferred into the service's
authenticated file scope. Simply admitting those commands would misinterpret
client-local integers as session descriptors. Implement and verify a native
stream lifetime/authorization bridge before claiming `zfs send | zfs receive`
works. Later streaming implementation and verification supersede this
historical limitation; see the current status and STREAMING.md above.

Generated test ISOs from completed yOjklz83/i7le8hCx were reclaimed before this
build; logs, executables and disk images remain, with pre-removal hashes in
`build-aros/reclaimed-test-isos-20260928-cli-send.sha256`. No DUT or user hosted
session was used. This run adds no physical-media or GUI acceptance claim.

## dRAID bring-up (2026-09-27, incomplete)

`AROS_TEST_CLI_DRAID=1` adds an actual upstream command test for a disposable
three-file `draid1:1d:3c:1s` pool. It requires creation, healthy status, native
DOS 1 MiB content plus normalized filename reads, export, readonly reimport,
write refusal and destruction. Its dedicated result is mandatory when enabled.

Baseline `native-import-run.64esbiF9` fails creation with unsupported pool:
native admission accepted root/mirror/RAIDZ only. Candidate admission now
accepts upstream dRAID grouping and validates synthetic distributed spares
separately from real paths; all real leaves retain explicit caller admission,
blankness and reservations. Upstream owns parity/map and spare-GUID semantics.
No typed Manager dRAID API or editor support is claimed.

`pudDD2q3` then fails permission checks because the new fixture used `/RAM/`
rather than canonical volume paths. The fixture now obtains paths with
NameFromLock, as existing CLI pool tests do; no authorization check is relaxed.
`9lN9wLNQ` creates the pool, reports ONLINE plus an AVAIL distributed spare,
writes/reads DOS data and exports. Readonly reimport fails with the CLI message
`internal error: out of memory`. This is a retained partial result, not a
passing dRAID suite or a diagnosis of actual memory exhaustion. Diagnostic
CLI link `cli-link.nxs9rmla` enables import tracing for the next investigation;
it must not be packaged as a normal distribution binary.

Trace run `EJyIQCtM` reproduces the failure and reports native IMPORT
request 23042, error 86, configuration 52216 bytes. The AROS SDK defines 86
as ENOTSUP, not ENOMEM. The public transport cap is 128 MiB, so a suspected
request-size rejection is not supported by this evidence. The exact remaining
unsupported validation branch still needs tracing; no reimport fix is claimed.

Follow-up `XxeX281Y` establishes that TRYIMPORT (23046) succeeds and upstream
loads the dRAID pool; actual IMPORT (23042) returns ENOTSUP. The earlier label
of 23042 as TRYIMPORT was incorrect. Inspection then found that both new
fixture import commands omitted `-o cachefile=none`. Native import deliberately
requires that explicit property, rather than implicitly writing a Unix cache
file. The fixture now supplies it, as the other CLI import tests do; production
admission has not been relaxed.

Fresh normal-binary `native-import-run.cvWWXj2B` passes the complete runner
(exit 0, original disk hashes unchanged). The dedicated dRAID check verifies
creation/status, normalized DOS access, exact 1 MiB payload, export, readonly
reimport/readback/write refusal, writable reimport and destruction. The rest
of the CLI/native regression also passes. This is a three-RAM-file lifecycle,
not cold persistence, degraded recovery, distributed-spare activation, native
partition coverage, or a typed Manager dRAID workflow.

Expanded `native-import-run.VAwEj94t` also passes the complete runner (exit 0,
original disk hashes unchanged). It syncs the initial pool, offlines one real
child, verifies DEGRADED, writes new DOS content and the 1 MiB payload, syncs,
onlines the child and waits for upstream status to report completed resilver,
ONLINE and zero errors. Readonly reimport verifies the new content and payload.
The runner requires its separate dRAID resilver marker. This adds degraded
write/recovery coverage within one guest, not distributed-spare activation,
cold dRAID persistence, or hardware durability.

### Distributed-spare admission and status sizing

`PYz67dMD` reproduces rejection of `zpool replace ... draid1-0-0`: attach
previously reused only blank external-member admission. The native adapter
now recognizes a single synthetic replacement leaf separately, requires root
configuration authority and a writable active pool, rejects nested/extraneous
identity fields, and checks the exact spare name and original child's parent
against the live pool inventory under the configuration read lock. No new file
or device is admitted/opened. Upstream still performs replacement, ownership
and topology validation. Ordinary external attach/replace retains blank-member
admission and lifetime handling.

`4iTMB7NN` rejects the first candidate because its whitelist omitted upstream's
explicit `is_log=0`; zero is now accepted, nonzero rejected. `ShwloF4l` and
`wFGoDiv1` retain test compile/link failures (missing local quiet tags and
security-library base), with no guest execution. Corrected `c9OcsTRD` activates
the spare but fails the following status query with exit 1 and empty output.

Diagnostic `M3JBZzFh` identifies the second defect: POOL_STATS requires 77848
bytes after replacement versus its initial 65536-byte buffer. Upstream returns
ENOMEM and the required size, but the native client replaced it with EOVERFLOW.
The client now allows the existing bounded size retry for read-only POOL_STATS,
as for DATASET_LIST_NEXT. Completed mutations retain non-retry overflow handling.
Optional diagnostic tracing includes status size/engine-error fields; these
traced binaries remain excluded from distribution packaging.

The runner now also stops on an intact `RESULT native service CLI FAIL` rather
than waiting when the subsequent pool-import marker is interleaved. PYz67dMD
was stopped only after its explicit client failure, by the validated private
guest PID; that failed run is not a passing lifecycle.

Normal service `full-draid-spare-v2` and commands `cli-link.S1jF9lh5` pass
`native-import-run.5pDIxzsF`: whole runner exit 0 and original hashes unchanged.
The spare reaches INUSE, upstream reports completed 1.07 MiB resilver with
zero errors, exact DOS payload remains readable, detach returns it to AVAIL,
and the existing readonly/writable reimport lifecycle passes. Nonroot activation
and readonly activation are refused. The runner requires the new spare marker.
This verifies planned replacement of an online member with a distributed spare;
it does not yet force reads exclusively through the spare, import an active
spare topology, or prove cold persistence or automatic failure response.

### Active-spare I/O and reimport

Expanded `qlwogm0x` succeeds in DOS writes/reads after offlining the original
member with its distributed spare active, but fails discovery after export
(`no such pool available`). The native import validator accepted dRAID and
synthetic leaves but omitted the upstream `spare` internal wrapper present
while a replacement remains active. That wrapper is now admitted with the
same recursive validation: no path on the wrapper, and every real descendant
must still match the caller's explicit allowed path list. Synthetic leaves
retain their separate top-GUID/spare-index validation. Upstream owns topology
and data recovery; no label-derived external path gets implicit admission.

Normal service `full-draid-active-import`, commands `cli-link.S1jF9lh5` and
`native-import-run.58Tsy1tQ` pass the complete regression (runner exit 0,
original backing hashes unchanged). The expanded test offlines the original
member, writes new `spareIO!` content plus the 1 MiB payload, syncs/exports,
reimports with the spare still INUSE and original OFFLINE, verifies the
payload, restores/resilvers the original and detaches the spare. The later
readonly reimport checks the new content and payload again. Its dedicated
active-spare marker is mandatory. This proves the tested same-kernel export/
reimport path; cold dRAID persistence and automatic failure handling remain
unverified. The non-dRAID CLI/native suite also passes with this service.

The deliverable is the upstream OpenZFS `zpool` and `zfs` commands, alongside
OpenZFS Manager. A custom subset wrapper is not a substitute. Both real commands
now link and have isolated
QEMU runtime coverage; the sections below distinguish whole-command tests
from earlier API-only milestones. Full command/feature coverage is unfinished.

### Persistent active-spare cold fixture

Optional `AROS_TEST_DRAID_PERSIST=write|read` extends the private native runner
with a new 512 MiB FAT backing image at IDE unit 3. Three 128 MiB member files
are preallocated offline, avoiding the separate native FAT extension issue.
The writer runs real upstream commands, writes with the original member
offline and distributed spare active, syncs, exports, and inhibits the backing
volume. The reader requires a successful retained writer, validates hashes,
copies its image, and boots a fresh AROS kernel. It opens existing files only,
imports readonly and requires exact changed DOS bytes, 1 MiB content, write
refusal, active-spare status and export. Both modes run the rest of the native
CLI regression. Offline extraction and `zdb -bcc` must succeed; the reader
must preserve all three ZFS member byte streams and the original writer image.
This is clean-export cold persistence, not abrupt power-loss durability.

Writer `UNTjyHWk` passes the full suite and independent zdb. First reader
`W9qvVF5t` imports successfully but aborts in `print_status_config:2894` because
the preserved read-only auxiliary nvlist lacks VDEV_STATS. The AROS libzpool
branch deliberately avoids activating shared auxiliary devices for readonly
loads; preserving their configuration alone was insufficient for real zpool.
The producer now emits actual in-tree statistics for active replacement spares
with SPARED status. Unopened auxiliaries get CLOSED state, not fictitious
AVAIL/healthy results or persisted stale counters. This applies to both saved
spare and L2ARC listings; no extra device is opened or activated. The CLI's
assertion has not been removed or bypassed.

First correction `5hAd0YMj` removes the abort but fails the INUSE assertion:
readonly libzpool also skips spare registration, so `vdev_isspare` is unset
even on the ONLINE in-tree replacement. The revised producer identifies the
matching configured GUID as the second child of the live `spare` wrapper,
rather than relying on that registration flag. Both failed readers use copies
of the retained writer; they do not alter or regenerate the writer fixture.

Revised reader `foRrH6pE`, with `full-draid-readonly-stats-v2` and the normal
`cli-link.S1jF9lh5` commands, passes the full runner. A fresh kernel reports
the distributed spare INUSE and original member OFFLINE, reads the exact
changed eight bytes and 1 MiB payload, refuses writes, and exports. Offline
comparison confirms all three member files unchanged; the original writer
image hash also matches. Independent `zdb -bcc` exits zero with no leaks.
The remaining CLI/native regression and original mirror hash checks pass.
This verifies the active-spare cold path, not dedicated L2ARC behavior,
arbitrary dRAID geometries, or power-loss recovery. The latest packaged bundle
predates this change and must be refreshed and separately runtime-tested.

## Whole-command pool creation

### Direct native-partition creation and cold persistence

`native-import-run.WL5evyzF` passes actual upstream `zpool create` on a fresh
256 MiB test GPT disk's sole data partition, using frozen corrected bundle
`native-bundle.oufX6tvZ`. The new pool has only
`/AROS/ata.device/3/partition/0`, not a RAM/file source later replaced by a
partition. The CLI creates and mounts a normalized, case-insensitive dataset;
DOS writes and reads an exact 1 MiB payload and the composed/decomposed UTF-8
filename. Export, readonly reimport, repeated byte comparison, write refusal
and final export pass. The complete regression exits zero. Both GPT copies
and the pre-partition gap compare unchanged, host GPT validation passes, and
independent upstream `zdb -bcc` on the extracted partition exits zero with
No leaks. The pool is retained exported, not destroyed.

Fresh QEMU boot `native-import-run.ZbtspyWf` passes the dedicated cold reader
against that exported native partition, with exact payload and normalized-name
readback, readonly enforcement and export. All original disks, writer backing
files and new reader overlays retain their hashes. The original mirror's cold
DOS persistence tests also pass; this mode deliberately skips the separate
CLI-created `qemu_mirror/cli-import` dataset test, which the writer did not retain.

Reproduce with `AROS_TEST_NATIVE_CREATE=1 AROS_TEST_CLI_POOL_CREATE=1` in the
native writer, then `AROS_TEST_NATIVE_CREATE_COLD=1 AROS_TEST_NATIVE_WRITABLE=2`
and `AROS_TEST_NATIVE_VERIFY_RUN=<successful writer>` in a new runner. Native
create is exclusive with the add/replace/split and split-fault modes. These are
explicit pre-provisioned disposable partition tests, not whole-disk automatic
labeling, physical durability, or general exclusion of external raw writers.
The current whole-extent blankness scan is conservative and not a scalable
large-array provisioning policy. Existing unrelated const-qualification build
warnings in the split test remain recorded in service-command-build.log.

### Explicit missing file admission in mixed-mirror discovery

Mixed native-partition/file mirror test `native-import-run.Mani982x` created
and wrote the pool, exported it and deleted the RAM-backed member. Import
explicitly supplied both the native survivor and the absent file, but reported
no pool available. The scanner discarded ENOENT candidates before assembling
the explicit admission list; the service consequently rejected the label's
unadmitted member path. The client scanner now retains an explicitly requested
missing file candidate after resolving its parent. It never adds paths from
labels. The service still pins/validates the missing member and upstream decides
whether the survivors suffice. A negative test omitting that path must still
fail discovery.

The first corrected-command run, `BVVv5hDL`, passed degraded import and exact
payload readback, then failed a later test expectation: the populated-media
creation refusal saw the deleted file first (ENOENT rather than EEXIST). The
fixture now supplies a fresh blank file solely for that negative creation test,
retains the required EEXIST native-media refusal, and deletes the file again.
It does not restore data redundancy or turn that failed run into a full pass.

Final writer `G56f2u8Q` passes the complete suite with service
`full-typed-native-create` and rebuilt upstream commands `cli-link.PU8NyHsg`
(all common client units compiled in `cli-probe.58fppUHL`). The missing-path
omission is refused, explicit two-path degraded import succeeds, and exact
1 MiB/normalized-name DOS readback passes. Authenticated create refusals remain
required. Independent `zdb -bcc` on the native survivor exits zero with No leaks;
GPT exterior comparisons and original-disk hashes pass.

Fresh reader `Gh2dDDqz` passes with no RAM-backed member available: explicit
paths, DEGRADED status, exact payload, readonly refusal and export. All original,
writer and fresh reader-overlay hashes match. Add
`AROS_TEST_TYPED_NATIVE_MIRROR=1` to both typed-native writer and native-create
cold-reader invocations. This verifies mixed-media mirror creation/recovery,
not every native RAID layout or physical durability.

### Upstream CLI mirror split

The real `zpool split` command now reaches upstream `ZFS_IOC_VDEV_SPLIT`.
Admission checks source ownership, mounted datasets, writable state, and each
selected leaf's live GUID/type/path before granting the destination-name vdev
policy. This path adds no backing devices. The destination is exported; import
options/properties and hole/indirect topologies remain explicitly unsupported.

`native-import-run.3XxDSd9Z` passed the complete native/CLI runner with service
`full-cli-split` and commands `cli-link.kN5s9imh`. The test creates a populated
two-file mirror, refuses mounted/read-only/unauthorized splits, checks dry-run
output and unchanged topology, then splits the selected member. The destination
is not left imported. Each resulting pool is separately imported read-only
using only its own member and returns the exact normalized-name file and 1 MiB
payload; each is subsequently imported writable and destroyed. Both original
native fixture hashes match. This is same-guest file-backed split coverage,
not native-partition split, independent cold-boot split, or export-failure
recovery coverage.

Retained failed run `native-import-run.6CkorPfv` correctly refused the mounted
split with status 1. The test incorrectly expected -1; only that expectation
and the equivalent refusal assertions were corrected before the passing run.

#### Retained split-destination lifetime

Both split paths retain the source name with a destination policy when the
destination survives upstream's final export. Source export/destruction now
returns EBUSY through both CLI and authenticated Manager, keeping the borrowed
native bindings and file reservations alive. Exporting/destroying the
destination releases the dependency; engine shutdown releases it only after
the SPAs have unloaded. This is a temporary failure-state dependency, not an
implementation of ordinary split-with-auto-import.

`native-import-run.Im5yElq1` passed the complete regression with private service
`full-cli-split-retained`. The build-only `AROS_TEST_SPLIT_RETAINED=1` wrapper
executes a real upstream split without its final export, then reports EIO for
the dedicated test pool. Both Manager removal operations return errno 16; both
CLI removals fail busy. The retained destination mounts and returns the exact
1 MiB payload, then exports successfully. The source subsequently exports and
both pools pass the original separate-import/readback/destruction checks.
Both baseline disk hashes match. This reproduces the retained-SPA state; it
does not inject a real hardware/export I/O failure. Normal service builds omit
the wrapper completely. The initial build log retains an incorrectly placed
check caught by compilation; build 2 corrected the branch and passed.

Normal (no injection) service `full-cli-split-lifetime` subsequently passed
`native-import-run.EVrzet6z`, including the complete CLI/native suite and native
partition replacement. The replacement-only 1 MiB readback, zero native I/O
failure diagnostics, original backing hashes, GPT exterior comparisons and GPT
validation all pass. The normal service symbol table has no split wrapper.

The same retained-destination regression passes after the upstream DTL cleanup
backport in `native-import-run.NIamufew`, using
`full-cli-split-dtl-retained`. The complete runner exits zero and both original
disk hashes match. Its dedicated retention marker confirms the injected EIO,
readable destination, blocked source export/destruction and explicit destination
export. This is failure-state lifetime coverage, not physical I/O fault coverage.

### Native partition attach and split

`native-import-run.MXRzFpoh` passes the complete CLI/native suite against the
frozen `native-bundle.eEaer0tl`. The added `AROS_TEST_NATIVE_SPLIT=1` scenario
creates a populated file-backed pool, attaches a fresh GPT partition at
`/AROS/ata.device/3/partition/0`, and waits for a completed zero-error resilver.
It then splits that partition into a separate pool, verifies dry-run topology,
mounted/read-only/non-root refusal, separately imports each result read-only,
and checks normalized filename access and all 1 MiB of payload. Both pools are
subsequently destroyed; the original RAM file is removed. Native I/O failure
diagnostics are forbidden; baseline hashes, GPT boundaries and final GPT
validation pass. This is same-guest persistence, not cold-boot evidence.

Failed run `native-import-run.kNv6ZXQw` stopped at the ordinary-user refusal:
native geometry admission correctly said `Permission denied` before reaching
the split ioctl, while the test expected upstream's lowercase `permission
denied`. The corrected assertion preserves status 1 and checks the native
diagnostic rather than weakening authorization.

The retained writer `native-import-run.W7oJMy1E` and fresh reader
`native-import-run.vyuNChlr` pass AROS cold readback: partition-only import,
normalized filename, exact 1 MiB payload, readonly rejection, export, and every
original/writer/reader-overlay hash unchanged. Reproduce the writer with
`AROS_TEST_NATIVE_SPLIT=1 AROS_TEST_NATIVE_SPLIT_KEEP=1 AROS_TEST_KEEP_CLI=1`;
the cold reader uses `AROS_TEST_NATIVE_WRITABLE=2`, that writer as
`AROS_TEST_NATIVE_VERIFY_RUN`, and `AROS_TEST_NATIVE_SPLIT_COLD=1`.

However, subsequent independent `split-zdb.log` in the reader directory exits
2: block checksums/space-map block sums pass but histogram feature refcount is
10 versus 8 live maps. This is NOT full integrity acceptance. It matches
upstream issue #5599 / merged PR #18808 (commit
`6d4ff3b4f3a9d1a7dc3948e02569765aadcee0ab`), which fixes split DTL-object
cleanup; the port lacked that change. The cold runner now requires independent
`zdb -bcc` exit zero in addition to its no-leaks text, so this failure cannot be
hidden by successful DOS reads. Original failed output and extracted partition
remain intact for before/after comparison. Backport verification follows.

Upstream engine fix backported as `6a48011`, preserving Toomas Soome's
authorship and the original PR/commit identity. Both changed core objects were
rebuilt before linking `full-cli-split-dtl`. New writer
`native-import-run.VVl1U7bQ` passes the full native/CLI suite and GPT guards;
fresh reader `native-import-run.dtHrKkdZ` passes exact partition-only DOS
readback, readonly refusal, every backing/overlay hash, and now the mandatory
independent `zdb -bcc` exit-zero check. `split-zdb.log` reports No leaks without
the former refcount mismatch. The verifier was not changed. This proves
prevention on a new split, not repair of previously affected pools. The old
failing images remain untouched. The independent zdb traversal here covers the
destination; source-pool verification in this scenario is native readback.

### Upstream CLI attach and replacement

`VDEV_ATTACH` now crosses the bounded ioctl transport with the existing member
GUID, attach/replace selector and rebuild selector. New-member admission reuses
the pool expansion transaction: authenticate, require a writable nonsuspended
pool, pin and check entire blank backings, grant exact paths, retain reservations
through asynchronous work, and roll back admission on failure. Upstream performs
the topology change and resilver; successful command return is not completion.

`full-cli-attach` with `cli-link.Xk8OeTig` passes the complete native QEMU suite
in `native-import-run.GSxgk01B`, exit zero with all original raw hashes unchanged
and the native-add GPT preservation checks passing. Actual CLI commands create
a populated single-file pool, attach a second file, wait for ONLINE/zero-error
resilver, replace the original with a third file, wait for completed resilver
and disappearance of the original, then detach the second. After export, both
old files are deleted. Replacement-only readonly import and native DOS mount
recover the exact normalized filename and all 1,048,576 patterned bytes.
Read-only and ordinary-user attach/replace fail. A marker at each candidate's
last byte causes even forced attach/replace to fail while preserving that byte;
the harness alone clears its own markers before successful operations.

The earlier `V3ZWoH1Y` guest passed its original tests, but its runner exited
one: a new blankness marker was added to the live runner before that older
guest had the matching test. Retained as failed runner evidence, not a full
pass. `GSxgk01B` repeats with matching test/verifier versions throughout.

These are file-backed, one-guest lifecycle tests. Native partition replacement
is covered separately below; native partition attach is covered above. Interrupted resilver,
sequential reconstruction, RAIDZ expansion,
spare activation, independent cold CLI recovery and physical durability remain
separate verification work. Existing native management API cold attach tests
are documented in `../engine/POOL-ATTACH.md`, not conflated with this CLI run.

### Native-partition CLI replacement

The upstream CLI spare predicate now queries native pool membership and labels
over the authenticated exact-extent reader instead of opening `/AROS/...` as a
POSIX file. This preserves the real label-based predicate; it does not return
a fabricated spare result. Positive spare activation remains untested.

`full-cli-replace-publication` / `cli-link.kN5s9imh` passes the full isolated
QEMU runner in `native-import-run.SmCLWB9q`. In addition to the file-backed
attach/replace test, a fresh fixture replaces its original populated file vdev
with a new GPT partition, requires completed resilver and zero read/write/
checksum counters on ONLINE rows, detaches the other file, exports and deletes
both old files. Partition-only readonly import recovers every byte of the 1 MiB
pattern and normalized filename. Read-only/nonroot replacement refusals pass.
The entire regression exits zero, emits no native-I/O failure diagnostic, and
preserves both GPT regions and every original raw backing hash.

Retained `aKpASodj` failed with a FAULTED replacement and 12 write errors.
Diagnostic `32WJRZvn` exposed two EBUSY writes from ordinary DOS-list readers;
its original runner passed after eventual recovery, but is not accepted as
clean-I/O proof. The publication lock repair and deterministic exclusion tests
are in `../engine/README.md`. The strengthened resilver verifier checks per-vdev
counters and unhealthy states rather than relying on the scan summary alone.
This is not an independent cold CLI replacement or physical durability test.

### Upstream CLI pool expansion

`VDEV_ADD` now reaches the real upstream dispatcher through a bounded config
nvlist, with no output nvlist or caller pointers. Admission checks caller
authority, writable/non-suspended pool state, topology paths, canonical native
file identity, entire-member blankness and writable access. New paths receive
transactional additions to the existing pool's admission policy; failure rolls
those additions back and releases reservations. Successful new backing locks
remain associated with the pool until export/destroy, not merely until the
command returns. Upstream still owns vdev topology and ashift validation.

Service `full-cli-add` with untraced CLI `cli-link.fCo2QjTj` passes private
native QEMU `native-import-run.djo5LiLI`, runner exit zero and unchanged original
backing hashes. Both the existing Manager-API expansion and actual `zpool add`
run independently. CLI coverage includes dry-run without topology change,
read-only and ordinary-user denial, and a nonzero byte at the end of a new
member refused even with `-f`, with that byte preserved. Successful expansion
is followed by reopen, real nonzero-data evacuation, deletion of the original
member, survivor-only readonly reimport and exact 1 MiB payload comparison.
The initial success run `VKGcibQK` is retained too. Build logs retain existing
pthread header macro and public-service test signedness warnings.

That run proves file-backed CLI expansion, not native partition/whole-device
expansion, CLI attach/replace/split, every allocation role or dRAID. Native
partition expansion is covered separately below. Reservations are not an exclusive lock
against unrelated external raw writers. Rollback of admission is not a claim
that upstream failed vdev creation cannot have touched an exclusively supplied
blank member; production recovery/fault-injection coverage remains outstanding.

### Native-partition CLI expansion

The native leaf builder resolves canonical `/AROS/device/unit/partition/position`
through authenticated partition.library catalog queries, not POSIX realpath/stat.
Replication sizing uses the same checked geometry. Native partition labeling
does not run the upstream POSIX pre-admission zeroing operation: the service
requires the entire reserved extent to be blank before upstream vdev creation.
Whole-device provisioning is explicitly refused by this CLI path; the existing
explicit GPT operation remains separate. Policy extension accepts only an
already bound, available native extent, and retains its ownership until export.

Service `full-cli-native-add` plus untraced `cli-link.Xk8OeTig` passes the complete
private QEMU suite in `native-import-run.rwlFQkDv`, runner exit zero with all
three original raw-image hashes unchanged. A fresh third GPT disk is attached
behind a qcow2 overlay. The test checks readonly and ordinary-user refusal,
upstream mixed file/disk topology rejection, then explicit `-f` expansion with
dry-run and real execution. The original file vdev is evacuated, completion is
checked, and its backing file is deleted. Partition-only readonly reimport
recovers the normalized filename and byte-exact 1 MiB patterned payload. The
scratch pool is finally destroyed. This is export/reimport in one guest, not
independent cold boot, hardware durability or performance evidence.

Retained failures: `ZzQwMAgs` reached a missing-volume requester because the CLI
sent the native path to POSIX realpath; `BKx1QzU7` then correctly refused mixed
file/disk topology before the intended readonly assertion; `ASlfLP62` returned
successful dry-run output but the test expected a full path without asking for
`-P`. Subsequent checks request upstream full-path formatting, rather than
changing display semantics or accepting only command exit status.

Stricter repeat `native-import-run.T0jBhMQ4` also passes the full runner, exit
zero. Missing partition 127 is refused by actual geometry lookup; attempting
whole-disk addition returns the explicit provisioning refusal. After the guest
exits, byte comparisons prove the leading gap/primary GPT and backup GPT are
unchanged, and independent `sgdisk --verify` finds no problems. Original raw
backing hashes are unchanged. This still does not establish crash durability.

### Snapshot space accounting

`SPACE_WRITTEN` and modern `SPACE_SNAPS` now reach the upstream dispatcher.
The legacy query has two bounded inline names and returns byte counts in the
command fields; no extra caller pointer or output nvlist is admitted there.
Snapshot-range input/output uses the existing bounded nvlist transport and
upstream schema, dataset permission and suspended-pool checks.

`full-cli-space` / untraced `cli-link.owgVv1U6` passes the entire suite in
`native-import-run.FpIUGGXe`, exit zero and original fixture hashes unchanged.
Real `zfs get -H -p -o value written@before` reports zero immediately after
snapshot, then 1065984 after writing the 1 MiB patterned file and syncing.
`zfs destroy -n -v` reports `would reclaim 15K`; the snapshot remains listed
and later mounts/reads successfully. Following rename/export/readonly reimport,
`written@ui` again reports 1065984. Tests validate numeric values, not merely
command exit status. This does not prove all bookmark/range/permission-edge
cases or recovery behavior, and no user pool is accessed.

### CLI device evacuation

`VDEV_REMOVE` now reaches the real upstream dispatcher with either an existing
device GUID or the explicit cancellation cookie. No input/output nvlist,
configuration payload or additional pointer is accepted. Upstream retains
pool-modify permissions, read-only/suspended checks, topology/feature/space
constraints and actual asynchronous evacuation. Backing reservations stay held
until pool export; accepted removal is not treated as completed removal.

`full-cli-remove` / untraced `cli-link.owgVv1U6` passes
`native-import-run.dxWiDEbM`. The real CLI creates a 128 MiB file-backed pool,
DOS writes its normalized-name payload, and Manager adds a 256 MiB member.
CLI removal evacuates the ORIGINAL populated member: status reports 102 KiB
copied and completion, ONLINE state and absence of its original path. After
export the test deletes that private original file, imports readonly using
only the surviving member and reads the exact payload. It then exports,
reimports writable and destroys the scratch pool. Cancellation with no active
removal, read-only removal and non-root removal all fail. The complete existing
CLI/native suite passes, runner exit zero, original fixture hashes unchanged.

This run does not prove active CLI cancellation, `remove -w`, `remove -n`,
crash durability or physical-device behavior. The previously documented native
API removal tests remain distinct evidence. No user storage was accessed.

Follow-up `native-import-run.koZeFejW` and stricter repeat
`native-import-run.jmevVxCa` verify both `remove -n` and `remove -n -p`.
Upstream already obtains the estimate from pool statistics; no new ioctl or
backend implementation was needed. The repeat validates a positive numeric
machine-readable estimate, both original paths still ONLINE, absence of a
removal operation, and unchanged data after dry-run. This is new verification,
not a newly implemented dry-run feature.

These runs write a 1 MiB deterministic xorshift-pattern file BEFORE expansion,
compare every byte before and after dry-run, then require completed evacuation
(upstream reports 1.10 MiB copied). After deleting the removed private backing
file, survivor-only readonly import must match all 1,048,576 bytes plus exact
EOF. Both full regression runners exit zero and original raw hashes remain
unchanged. Active cancellation, `-w`, interrupted evacuation, hardware and
power-loss behavior remain outside this evidence.

### Cross-interface pool growth

Manager's authenticated add/attach/replace path now extends both GUID-bound
and name-bound vdev policies. CLI-created and split pools can use name-bound
policies; previously expansion looked only for a GUID rule and returned success
without granting the new path. Each extension now records exactly which rules
changed. All matching rules are prepared before publication, and an upstream
failure removes only newly granted paths, preserving preexisting permissions.
The transaction is owned by the serialized service task until completion.

Regression `native-import-run.2RsaDbnv` uses the prior service and fails with
EACCES at Manager add after real CLI pool creation. Fixed `full-named-growth`
passes `native-import-run.XfnnRTVn`: CLI creates `cli_growth`, Manager adds a
second top-level file vdev, CLI observes it and reopens, DOS writes data, and
export/read-only reimport recovers the exact normalized payload. The scratch
pool is then reimported writable, destroyed and its two private files deleted.
The complete existing CLI/native suite also passes; runner exit zero and
original raw hashes unchanged. This is API interoperability, not a new GUI
visual verification or a hardware-expansion claim.

`named-growth-harness` / `admission-service-run.shgbAweA` passes native AROS
policy tests for named/GUID intersection, successful expansion and rollback
without removal of a preexisting grant, followed by the file creation suite.
No DUT, user hosted session or user pool was accessed.

`POOL_CREATE` now reaches the real upstream dispatcher after authenticated root
authorization, topology/path validation, backing-object pinning and an exhaustive
blankness check of every member. All checks precede SPA creation. Explicit
cache policy (`cachefile=none` or an absolute path) is supported; the new
protected ENVARC default is implemented but default creation has not yet been
separately runtime-qualified. Temporary-name
creation has since been implemented and qualified (see distribution notes).
Even `-f` does
not bypass blankness. This conservative creation policy is not yet suitable for
large production arrays: it reads each entire member, and does not exclude
uncoordinated external raw writers.

Opt-in `AROS_TEST_CLI_POOL_CREATE=1`, untraced `cli-link.XV9m5n2z` and
`full-cli-create-path` pass `native-import-run.xGzvzcwg` (runner exit zero).
Real zpool creates a mirror from two private 128 MiB guest-RAM files; real zfs
creates/mounts a normalized dataset, DOS writes it, and CLI export/read-only
reimport/mount recovers the normalized filename and rejects writes. Before
creation, a nonzero final byte in the second member makes forced creation fail;
a full scan proves the first member untouched and the marker is retained.
The existing native-device import/DOS regression also passes, with unchanged
original raw-file hashes. This is file-backed CLI creation, not a claim of
whole-device CLI provisioning or physical-array testing.

Earlier `Vpzbh7eh` and traced `MJQVwl6b` created and wrote successfully but
failed reimport. Labels were valid; joining a canonical volume root added a
second slash (`/RAM Disk//...`) and changed native DOS path interpretation.
The import scanner now avoids adding a separator when one is already present.
Both failed runs remain available alongside the passing evidence.

### Whole-command pool destruction

`POOL_DESTROY` now uses the same authenticated native mounted-subtree check
and post-success binding release as export, followed by the real upstream
dispatcher. Export/destroy history strings share the bounded input-only wire
field; arbitrary history reads remain unsupported. Destroy additionally rejects
a read-only SPA, matching the existing native management API.

Initial `full-cli-destroy` / `cli-link.owgVv1U6` passes lifecycle run
`native-import-run.yiU5W7RB`. The stricter follow-up `Ovj8SPaE` fails because
upstream destroy accepts a read-only imported pool: without the SPA sync thread,
that path unloads it without persisting destroyed labels. The native adapter now
returns EROFS instead of reporting a durable destruction that did not happen;
read-only export is still permitted. The failed evidence is retained.

Follow-up `full-cli-destroy-ro` and the same untraced commands exercise normal
and forced busy-file destruction denial, unauthorized destruction denial,
read-only destruction denial, successful writable destruction, pool absence,
normal-import rejection of destroyed labels, and deletion of the two guest
files after bindings are released. These tests only destroy the private
`cli_created` RAM-file pool, never the native fixture pool or user storage.
`native-import-run.TCyDRyPf` passes every tightened gate and the existing native
import/DOS regression, exits zero, and leaves both original raw hashes unchanged.

## Whole-command native pool export

### CLI scrub and reopen

Ordinary upstream `zpool clear <pool> [device]` is also admitted through
`ZFS_IOC_CLEAR`, with the native no-rewind constraint (`ZPOOL_NO_REWIND`) and
the real upstream configuration permission/read-only checks. An optional
device is resolved by the actual CLI; the service accepts the resulting GUID.
The wire retains bounded nvlist ranges and rejects other recovery cookies.
This does not implement rewind (`-F`), extreme recovery or device power control.

`full-cli-clear` / `cli-link.owgVv1U6` passes
`native-import-run.BOXobXs1`: pool-wide clear and device clear succeed; `-F`
is refused without losing ONLINE status; readonly pool/device clears and
non-root clear fail. All existing growth, evacuation, DOS, import/export,
mirror recovery and snapshot tests pass, runner exit zero and original raw
hashes unchanged. These CLI tests operate on healthy private pools and do not
claim injected-error recovery, suspended-I/O recovery or physical fault testing.

The transport admits upstream modern `POOL_SCRUB` and `POOL_REOPEN` requests.
The upstream dispatcher retains nvlist schema checks, configuration permissions,
suspended-pool checks and scrub's read-only prohibition; no native success stub
or replacement scan is used. Reopen continues using the retained admitted path
policy and device/file bindings.

`full-cli-maintenance` with untraced commands `cli-link.owgVv1U6` runs the
extended creation/lifecycle suite. It syncs DOS data, reopens the mirror, starts
a real scrub, and polls real CLI status until it reports a completed scrub with
zero errors (not merely successful scheduling). Read-only scrub and unauthorized
scrub/reopen must fail; read-only reopen must succeed with exact normalized DOS
file readback afterward. Pause/resume, scrub repair of injected corruption,
date-range scrubs and CLI wait semantics are not established by this test.
Evidence `native-import-run.IYJOhSU1` exits zero with every lifecycle/maintenance
marker, completed scrub status, and unchanged original native backing hashes.

### CLI pool checkpoints

Upstream `POOL_CHECKPOINT` and `POOL_DISCARD_CHECKPOINT` now pass through the
modern dispatcher, including its configuration permissions, suspended/read-only
checks and empty-input schema. The CLI does not substitute a dataset snapshot.

`full-cli-checkpoint` with untraced `cli-link.owgVv1U6` extends the private
file-mirror lifecycle: create a checkpoint, confirm real status, reject duplicate
creation, export/reimport readonly and retain the checkpoint, reject readonly
discard, then reimport writable and discard. It polls status until neither an
existing nor a discarding checkpoint remains, then requires another discard
to fail. Non-root checkpoint and discard return permission denied. DOS data and
the prior maintenance/create/destroy/native-import regression are also checked.
This establishes clean reimport persistence and discard completion, not checkpoint
rewind, power-loss recovery or a large production array.
Evidence `native-import-run.E9I2JstH` exits zero with every required marker and
unchanged original raw backing hashes. Status reports a real checkpoint consuming
space before discard; negative cases report the expected read-only/permission
errors rather than generic command transport failures.

### CLI member online/offline and mirror resilver

`VDEV_SET_STATE` admits online (no flags) and offline (normal or temporary) for
existing admitted members. Requests are inline pool/GUID/state fields with no
input/output nvlist. Upstream owns authorization, pool-readonly checks and
last-replica protection. Explicit online expansion/spare activation and injected
fault/degraded/removed states remain unsupported pending native lifetime/extent
handling. Retained backing bindings and the path policy are not dropped on
offline, and no new device is admitted by online.

`full-cli-vdev-state` and untraced `cli-link.owgVv1U6` first pass
`native-import-run.WfehEWHG`: offline one member, reject offlining the last valid
replica, DOS write while degraded, online, and real status reporting a completed
48 KiB zero-error resilver. `crYnZJKU` tightens this by changing the file from
`CLI data` to `CLI new!` while degraded and requiring those new bytes after
export/reimport. Both runners exit zero, including previous CLI/native DOS
regressions and original raw-file hash checks. Readonly and unauthorized state
changes fail with their expected pool/permission errors.

The stricter test offlines the original up-to-date member before export,
imports readonly with only the repaired member online, verifies DEGRADED status
and the new bytes through normalized DOS lookup, then restores the other member
after writable reimport. Export unloads the earlier SPA/cache. This is explicit
CLI recovery coverage on disposable RAM-backed mirror members, not physical
hotplug, replacement devices, RAIDZ recovery or abrupt-power-loss proof.
`native-import-run.lEPoQaGJ` passes this repaired-member-only readback gate,
subsequent member restoration/resilver and the complete prior suite, exits zero,
and preserves both original native raw-file hashes. All three evidence runs
remain available; the runner now requires the stronger repaired-member marker.

### CLI mirror detach

`VDEV_DETACH` now reaches the actual upstream dispatcher with bounded inline
pool/GUID fields and no config/pointer payload. Upstream enforces authorization,
read-only pools and topology/replica constraints. Like native management detach,
backing reservations remain until export/destroy; immediate detached-device reuse
while the source pool is still imported is not implemented by this change.

The private mirror test checks readonly and unauthorized detach denial, detaches
one member, verifies its absence from CLI status, and rejects detaching the sole
remaining member. It exports, deletes the detached guest-RAM backing file, then
imports from the sole surviving file, mounts readonly and checks the newer
normalized-name payload. The survivor is subsequently exported, imported writable
and destroyed through the real CLI. No fixture-native or user disk is deleted.

Initial `native-import-run.lCeESdYj` correctly rejects readonly detach but fails
the test's expected exit status: upstream `zpool_do_detach` directly returns
libzfs's -1 failure, preserved by native AROS rather than converted to 1. The
test now expects that precise native status and the read-only/permission error
text. No engine rejection was bypassed to make this test pass.
`full-cli-detach`, untraced `cli-link.owgVv1U6` and tightened test
`native-import-run.OqmklszO` pass every detach, prior lifecycle and native DOS
gate, exit zero and preserve both original raw backing hashes. Failed and passing
logs, source snapshots and deployed artifact hashes remain in their run folders.

### CLI filesystem dataset rename

Legacy `RENAME` now reaches upstream with validated inline source/destination
strings and no nvlist/pointer payload. A serialized native precheck rejects
mounted source or destination subtrees, preserving DOS endpoint names/handles;
upstream retains namespace, read-only and delegated-rename permission checks.
Normal rename and the nounmount flag are admitted. Snapshot and recursive
snapshot rename are covered by the follow-up below; bookmark rename remains
unsupported. This is not a claim of full rename-family coverage.

The private CLI lifecycle checks unmounted readonly rename denial, `rename -u`
rejection with a live mounted file (which remains readable), and unprivileged
rename denial. It then renames the unmounted filesystem, checks old-name absence
and new-name presence, exports/reimports readonly, mounts the new dataset name
and reads the newer degraded-write payload through its normalized filename.
That pool is subsequently exported/imported writable and destroyed normally.
`full-cli-rename`, untraced `cli-link.owgVv1U6` and
`native-import-run.8XAblt8k` pass these rename gates and the earlier CLI/native
DOS suite, exit zero and leave both original raw backing hashes unchanged.

### CLI single and recursive snapshot rename

The native precheck now matches every affected mounted snapshot by filesystem
boundary and snapshot name, including descendant filesystems for recursive
rename. It rejects affected source/destination mounts before upstream's recursive
unmount callbacks can detach any volume. A mounted live filesystem by itself
does not block renaming its unmounted snapshot. Upstream still performs rename
permission, same-filesystem, conflict and recursive-transaction checks.

The test creates parent/child snapshots recursively, checks that single rename
is busy while the parent snapshot is mounted and remains readable, then checks
that recursive rename is busy while the matching child snapshot is mounted.
After explicit unmount it renames both snapshots recursively, checks child old
name absence/new name presence, then renames the parent snapshot alone. A later
readonly pool reimport mounts the renamed parent snapshot through the native
authenticated mount API and verifies the distinct file data and denied writes.
This tests real CLI rename plus actual native snapshot mounts, not GUI actions.
Initial `native-import-run.BHsa76U3` stopped at a test invocation error: upstream
`rename -u` admits only filesystems, so the snapshot never reached the service.
The corrected test uses ordinary snapshot rename and still requires EBUSY with
the endpoint mounted. The failed log is retained; no native guard was relaxed.
`full-cli-snaprename`, untraced `cli-link.owgVv1U6` and
`native-import-run.2FKm77ej` pass the snapshot gates and complete prior CLI/native
DOS regression, exit zero and preserve both original raw backing hashes.

### Manager API snapshot rename

Authenticated operation 39, used by the manager's Datasets/rename action, now
accepts a single snapshot rename within its filesystem. It uses the same native
mounted-snapshot guard as CLI, retains upstream delegated rename/mount/create
permission checks, and calls real `dsl_dataset_rename_snapshot`. Recursive flags
remain unavailable in this management operation; the CLI supports them as above.
The UI labels now say dataset or snapshot and no longer incorrectly describe
authenticated rename as a writable-fixture-only operation.

The native integration test submits/polls/releases actual operation-39 tickets,
verifies EBUSY for a mounted snapshot, renames an unmounted snapshot, and uses
the independent real CLI to enumerate its new name after readonly pool reimport.
It mounts that snapshot through the authenticated native API and checks file
data and write denial, then unmounts and verifies rename fails EROFS. Exact
outer status and returned errno text are asserted. This is API integration and
a rebuilt manager binary, not visual interaction coverage of the new UI labels.

Initial `native-import-run.1rwSDcCd` correctly returns errno 16 but fails the
test's expectation of OZ_BUSY: the authenticated admission API reports operation
errors as OZ_ENGINE_ERROR with exact errno detail. The corrected assertion checks
both rather than changing or bypassing the existing error protocol.
`full-manager-snaprename`, untraced `cli-link.owgVv1U6` and
`native-import-run.RacqeO0D` pass these manager API gates and all prior CLI/native
DOS regression gates, exit zero and preserve both original raw backing hashes.
The manager executable is rebuilt as part of that full-service build.

Visual follow-up `AROS_TEST_GUI=3` in the native-import runner now creates a
private snapshot and waits for a real manager action. With the same
`full-manager-snaprename` service/manager, `native-import-run.zJEEIiV1` captures
the updated labels and filled request (`08-request.png`), confirmation
(`09-confirm.png`) and authenticated errno=0 result (`10-result.png`). After
the window closes, the harness requires old-name absence, exports and imports
readonly, mounts the renamed snapshot and compares the complete original file
payload. It exports again, exits zero and verifies original backing hashes.
The runner requires the dedicated GUI snapshot-rename PASS marker, not merely
a manager success response. `ui-actions.jsonl` records the private QMP input.

For this guest's tablet calibration, screenshot targets mapped approximately to
QMP input x=0.9*x+64, y=0.9*y+40; initial mispositioned clicks sent no mutation.
Category cycling used the arrow, not its text. No KVM or user input device was
changed. GUI mode 3 is restricted to writable disposable mirror overlays with
both members and no GPT scenario; all baseline fixtures remain read-only.

### Native CLI identity and discovery

`identity.c` resolves `getuid`/`geteuid` from the current configured
security.library task owner, rather than the private POSIX runtime's cached
UID. Native root maps to zero, ordinary users retain their UID, and native
nobody maps to 65535, matching the handler. Missing/unconfigured security
returns an invalid UID, never root. This is client preflight only: authenticated
service-side authorization remains mandatory. No shared CRT was modified.

Real commands `cli-link.OTteWp0V` pass `native-import-run.g4CQMDf3`: root UID 0,
reader UID 77, actual non-root `zpool import` denial, export authorization,
native CLI mounts and the original DOS persistence/read-only regression.

Initial discovery failures: `native-import-run.qsdcppGn` rejected root
before scanning because upstream `zpool` saw the wrong effective UID.
After the adapter, `native-import-run.zdpzONVZ` reaches discovery but reports
no pools available, despite the exported mirror. At that stage the strict gate
`AROS_TEST_CLI_DISCOVERY=1` was opt-in; earlier default regression runs are not
discovery coverage. `AROS_CLI_IMPORT_TRACE=1`
on probe-link enables the existing bounded label diagnostics for this work.
Traced `cli-link.iFQxHuvk` / `native-import-run.7znfJXD4` confirms both exact
native candidates return errno 0, four labels and a configuration each. It
failed the pool-output assertion. Fixing a second bug (native candidates were
discarded because they cannot pass DOS stat/open) then exposed the missing
`ZFS_IOC_POOL_TRYIMPORT` boundary: `native-import-run.guPQYAsm` reaches it and
records errno 86 (unsupported) with both label reads successful. Admission must retain
the caller-selected path set; merely enabling label-directed opens is unsafe.

The current implementation copies the scan cache's exact candidates into a
separate admission nvlist before processing labels. The service validates the
entire topology against it, pins read-only backing objects, installs a scoped
path policy, invokes the real upstream TRYIMPORT dispatcher and releases all
bindings afterward, including error paths. Policies reject opens recovered
from metadata outside that selection too. Config/source sizes and pointer
mapping remain bounded; no raw client pointer is forwarded.

`full-cli-tryimport` and `cli-link.FnaLFusL` passed the first full discovery
run `native-import-run.g17FF1Pe` (mirror shown ONLINE), then stricter
`astIl10U`: an excluded member returns EACCES at refresh, two full scans pass,
and a fresh list confirms discovery did not import the pool. Untraced commands
`cli-link.4RuMzDwn` repeat the strict suite successfully in `id39368G`; DOS,
export, permission, read-only and raw backing hash gates also pass. Discovery
is now enabled by default in the whole-command runner; setting
`AROS_TEST_CLI_DISCOVERY=0` explicitly skips that coverage.

Limits remain explicit: at most 32 selected paths per scan. Cached refresh now
derives a bounded candidate selection from the explicitly selected cache;
the service still enforces identity, topology and credentials. See `WAIT.md`
for subsequent cache qualification. Those earlier
TRYIMPORT proofs cover discovery, not actual import,
boot-time automount, physical hardware, or user pools. Failed compile probe
`cli-probe.3Pwziyu7` is retained (const pointer qualification), followed by
successful `cli-probe.Lw5gsd4Z`.

### Actual upstream CLI import

Refreshed configs retain the original selected-path admission for the later
POOL_IMPORT request. The service authenticates, checks name/GUID/properties,
revalidates every selected backing object, acquires readonly or exclusive
writable bindings and keeps the path policy for the imported pool's lifetime.
Admission metadata is stripped from the private config snapshot before the
real upstream dispatcher receives it; it is not saved as pool configuration.
Failed imports release temporary bindings. Successful SPA import retains them
even if output copying fails; an ENOMEM reply after actual import is reported
as EOVERFLOW to avoid the CLI blindly retrying a completed mutation. The
small-reply failure is exercised separately as described below.

The first whole-command run `native-import-run.hEmiQ7Fs` passed using
`full-cli-import/OpenZFSServiceNative` and traced real commands
`cli-link.AP7J6ZDZ` (probe `GiSOCx5y`). Actual CLI imports writable, creates and
mounts a normalized/case-insensitive dataset, writes through DOS, unmounts and
exports, reimports readonly, reads the decomposed uppercase filename and
rejects a DOS create, then reimports writable to destroy/export. The subsequent
management-API reimport and original DOS/read-only cycles also pass. Raw backing
hashes remain unchanged; only the isolated writable overlays are modified.

Untraced `cli-link.JpRMQ0NX` repeats the suite in `native-import-run.ChwsYv2Q`
(exit 0), adding actual non-root named import and implicit-cache rejection.
Fresh CLI queries confirm the pool remains absent after each rejection; all
three subsequent authorized imports and the DOS persistence checks succeed.
Both import lifecycle and admission markers are required by the runner.

Cold persistence is separately verified: `AROS_TEST_KEEP_CLI=1` writer
`native-import-run.Z8WjtOvC` retains `qemu_mirror/cli-import` after the complete
CLI lifecycle. Fresh guest `GnBxYb2Q`, with `AROS_TEST_NATIVE_WRITABLE=2` and
`AROS_TEST_NATIVE_VERIFY_RUN` pointing at that writer, starts with no imported
pool, runs the real CLI readonly import, reads normalization/case properties,
mounts through zfs, reads the original file via its decomposed uppercase alias,
rejects a write, unmounts and exports. It then passes the older native DOS cold
verification too. The cold runner checks hashes of the original raw members,
writer overlays and its own readonly-test overlays; all remain unchanged.
Both use untraced `cli-link.JpRMQ0NX` and `full-cli-import`. This is a clean
export/fresh-boot proof, not an abrupt-power-loss recovery test.

For output-failure injection, build commands with
`AROS_CLI_IMPORT_TINY_REPLY=1` and run only the cold verifier with
`AROS_TEST_CLI_TINY_REPLY=1`. This test build sends a real one-byte output
capacity for POOL_IMPORT; it does not fabricate a server result. The CLI must
return failure, while a fresh list, property queries, DOS file read and export
must still work against the actually imported pool. The runner requires exactly
one EOVERFLOW marker, rejecting a blind mutation retry, and requires the cold
read-only/hash gates too. Build-options metadata prevents accidentally using
these deliberately fault-injected binaries as the normal CLI test artifact.

Initial injection `cli-link.ebVBqR4n` / `native-import-run.263g5DTb` passed.
The tightened assertion also checks the server's engine_error field, not just
the client-side return value: `cli-link.WyDr89Sm` / `native-import-run.AHFWBBll`
passes with that assertion, one import attempt and all backing/overlay hashes
unchanged. No fault injection is enabled in normal builds.

At this historical import checkpoint, only normal/IMPORT_ONLY flags were
admitted. Import now accepts explicit `cachefile=none` or absolute cache paths,
including serialized-cache import across boots (see `WAIT.md`). A protected
ENVARC default is now qualified for import/reset; an unavailable default fails
instead of selecting a Unix fallback. Recovery/force/key-loading flag paths, automatic nested
mounts and abrupt-power-loss recovery need further work/tests.
This is not a production-packaging, crash-durability or physical-array claim.

`ZFS_IOC_POOL_EXPORT` now reaches the real upstream dispatcher after native
service-task, security and mounted-subtree checks. Successful export releases
the import/split device bindings; errors retain them. Force/hardforce remain
explicitly unsupported rather than bypassing mounted DOS handlers.

The private ioctl envelope is now version 2: client and service must be rebuilt
together. It adds a bounded input-only history string (including final NUL),
permitted only for export. No caller pointer crosses the transport; the server
maps its private copy into the existing bounded-copy scope and clears the
pointer before returning. History reads and arbitrary history buffers remain
unsupported. This does not enable CLI import or unadmitted device discovery.

`full-cli-export/OpenZFSServiceNative` and real commands `cli-link.YwY1a6vl`
passed `native-import-run.yD4arcfp`: busy export fails while the held DOS file
remains readable; force is rejected with the pool still present; normal export
removes the pool from a fresh CLI list; authenticated management-API reimport
reacquires the native devices. The original DOS persistence/read-only regression
also passes and the backing raw images have unchanged hashes. This is QEMU
with private disposable overlays, not physical hardware or the user's array.

The stricter repeat `native-import-run.QubL8i4C` also passes with an actual
`ozreader` login: its real `zpool export` fails, a fresh root CLI query confirms
the pool remains present, and root export/API-reimport subsequently succeeds.
The runner requires both the export and non-root permission markers.

Retained failures: `GUjJ6XVU` lacked stdio.h in the new test helper;
`0mPgIsmd` exported successfully but its test tried mutation through the
read-only public message port; `orPkeXwa` lacked errno.h after switching that
helper to authenticated admission; `vuDdBjFk` needed explicit security.library
opening for the added non-root test. These are not passing runs.

## Native upstream bookmark transport

### Upstream pool synchronization

The bounded native ioctl transport now admits `ZFS_IOC_POOL_SYNC` to the
actual upstream dispatcher. Its nvlist schema, suspended/readonly checks,
security policy and `txg_wait_synced` behavior are unchanged; no parallel
sync implementation or root-only policy is substituted.

`public-service-run.epukfp12` passed the strict four-CPU regression with
`full-pool-sync` and `cli-probe.Yuvx8yRa`: ordinary and forced `lzc_sync`
succeed while writable, return ENOENT after export and EROFS after readonly
reimport. Actual `zpool_sync_one` also succeeds writable and rejects readonly.
The runner requires all three phase markers alongside mirror DOS, promotion,
bookmark and other gates. This verifies the API path used by the command,
not a fully linked `zpool` executable or physical power-loss durability.

### Clone promotion transport

The libzfs promotion regression now mounts the source snapshot as a native
DOS endpoint, holds a DOS lock, requires EBUSY from upstream promotion,
verifies a fresh lock still succeeds, then releases/detaches and checks the
unchanged clone origin through both interfaces. Later unmounted promotion
must still succeed. Initial four-CPU run `public-service-run.P9z2QB3d`
passed this test and the full storage regression with no production fix.
The runner now requires a dedicated mount-guard marker, protecting this
behavior from future changes. This checks a busy mounted source snapshot;
it does not establish every mounted-subtree promotion case.
`public-service-run.JUF2kxfG` passed the stricter marker-gated four-CPU run
with `full-native-origin` and `cli-probe.rje1kaDL` (exit 0).

### Cold native-management promotion persistence

`AROS_TEST_COLD_PROMOTION=1` extends `test-qemu-file-backing.sh`'s healthy,
API-created disposable pool fixture (no concurrent lifecycle suite). The
native management client creates a separate source/snapshot/clone, stores
the pre-promotion snapshot GUID as a pool user property, promotes, and checks
the resulting origins. A fresh guest/service then imports readonly, verifies
the persisted origins and GUID, verifies the former snapshot name is absent,
and requires EROFS when trying to promote the remaining clone. Both per-boot
markers are mandatory. This exercises the native management API, not the
separate libzfs ioctl transport.

`file-backing-run.zot3iH05` passed both boots with a two-file mirror, exact
DOS payload/enumeration, byte-identical readonly member files and independent
host `zdb` reporting no leaks. Binaries were rebuilt into
`build-aros/cold-promotion`; compiler/link and runner logs are retained there.
The run used private DOS/ATA/posixc overlays and no physical devices. This
proves the promoted relationship and snapshot identity survive a guest
restart; it does not establish cold persistence of bookmarks/delegation or
complete upstream command executables.

The same cold suite now also enables bookmarks explicitly on the disposable
pool, records the source snapshot GUID in a pool user property, creates a
bookmark, rejects duplicate creation and deletes the source snapshot before
export. After a fresh readonly boot, it verifies exactly the expected bookmark
and GUID through native enumeration, verifies the snapshot remains absent,
and rejects both bookmark destruction and creation with EROFS. Both new
markers are required by the runner, not optional log observations.

Four-file RAIDZ2 `file-backing-run.NZioBAXW` passed both boot phases with
`OpenZFSColdBookmarkTest`, including the promotion regression, native DOS
payload/enumeration, unchanged readonly member files and independent zdb
reporting no leaks. The earlier promotion-only RAIDZ2 run `DhWs5JQv` also
passed. This is native management API persistence evidence, not proof that
the complete upstream command executables link or run.

The promotion origin assertions now query both a fresh libzfs handle and
the native management API used by the GUI. Baseline
`public-service-run.mxIpvbdu` failed: native origin queries worked for clones
but returned ENOENT after promotion made a filesystem independent. Ordinary
DSL property lookup cannot supply that absent computed value. The native
property adapter now obtains upstream fast objset metadata when the origin
entry is absent and reports `-` for an empty origin, without inventing any
clone relationship or changing pool state.

`public-service-run.GmpzZB4c` passed the full four-CPU regression using
`full-native-origin` and `cli-probe.QoW4zZVj`. Both interfaces agree before
promotion, after denied promotion, after successful origin reversal, and
after readonly reimport. The baseline failure and passing run are retained.
This fixes the native API query; it is not a screenshot/UI rendering test or
a cold-boot persistence result.

`ZFS_IOC_PROMOTE` now forwards the legacy inline-name request to the real
upstream dispatcher, with no source/config/output nvlist or caller pointers.
The upstream conflict name is returned in `zc_string`; upstream promotion
authorization and pool-writability policy remain authoritative.

The new lifecycle test creates a disposable dataset, snapshot and clone
through libzfs_core, rejects an independent reader's own-session promotion,
checks the unchanged origin, exercises a real duplicate-snapshot conflict,
removes that conflict and promotes. Fresh handles verify the reversed origin
relationship and the promoted dataset's empty origin metadata. A second
promotion must return EINVAL, followed by ordered destruction and absence
checks. The strict runner requires the dedicated promotion marker.

Retained `public-service-run.tmLzwCIt` failed after successful promotion
because the test expected the printable string `-` for an absent origin.
Upstream `zfs_prop_get` returns -1 when no origin string exists; the corrected
test checks the newly opened handle's empty `dds_origin` metadata instead.
Initial client link logs retain the unresolved sharing-validator dependency
pulled by high-level setup helpers, and two subsequent links reused the old
combined probe object. Rebuilding the entire probe with libzfs_core setup
resolved that test linkage, not the separate complete CLI sharing gap.

`public-service-run.jTDLbF2y` passed the strict four-CPU RAM-backed regression
(exit 0), including the dedicated promotion marker and complete mirror DOS
write/read/readonly-reimport gates. Inputs were `full-promotion` and
`cli-probe.gnkVZDXp`. This does not cover cold promotion persistence or a
promotion attempt on a readonly pool, nor the native ATA timeout.

The follow-up `public-service-run.E60rpSNh` (four CPUs, exit 0) adds a
separate retained clone across export and readonly reimport. Actual upstream
promotion returns EROFS; fresh handles confirm the original clone/source
origins, unchanged snapshot GUID and absent transferred snapshot. The strict
runner requires this marker in addition to the writable promotion lifecycle
and complete mirror DOS regression. Inputs are `full-promotion` and
`cli-probe.EY17Q3ZN`. These fixtures are RAM-backed and survive pool reimport,
not a guest reboot; cold persistence and native ATA reliability remain open.
The initial `promotion-readonly-client.log` link failed because the combined
probe's gc-sections roots omitted the new test function. The probe now retains
it explicitly; `promotion-readonly-rooted-client.log` records the passing link.

### Retained native read descriptors

The native import scanner now owns one parent-task session shared by its label
workers, closing it only after the thread pool joins. Per-label device handles
remain independent. Worker concurrency is bounded by the shared public/service
descriptor limit, not the much smaller number of session slots. Regular-file
scans do not acquire a native session. Session allocation/close failure returns
an explicit import error rather than a successful empty inventory.

`public-service-run.jKR95tIb` passed the strict host-read-traced 4-CPU regression
with `full-scanner-session/OpenZFSServiceNative` and
`cli-probe.B4xpMCN6/OpenZFSPublicServiceTest`. The scanner test fills every
available session slot and requires an explicit failure, frees exactly one,
then discovers the labeled pool while scanning a second blank device and
deduplicating the repeated candidate. It releases all reservations and repeats
after the non-root phase. Both 32-cycle read phases and the full filesystem
regression pass. This tests session budgeting, not resolution of the separately
retained untraced ATA timeout.

Native label parsing now opens one read-only Exec device handle per parse,
retained by an authenticated session-local `zfs_file_t` descriptor. Four label
reads no longer reopen the device independently. The service checks fresh root
credentials and session ownership for open/read, validates bounds/alignment via
the real device backend, copies only successful reads, and retains a file
reference during each operation. Explicit close and session cleanup release
the device. Non-device control descriptors cannot be read through this API.
This is the full-dispatcher integration path, not a general writable disk lease.
Partition catalog discovery is still separate; this does not prove stable
hardware identity across media changes or exclusive native pool ownership.

`public-service-run.VbwlY6a2` passed the strict 4-CPU, host-read-traced regression
with `full-device-handle/OpenZFSServiceNative` and
`cli-probe.qNHZMCDU/OpenZFSPublicServiceTest`. Added checks cover retained reads,
bounds with unchanged output, wrong descriptor type, closed descriptors,
session-close cleanup, and non-root rejection. Both 32-cycle alternating
label-read phases, scanner, and full mirror/DOS/delegation regression passed;
fixture hashes and zero label-disk writes remain enforced by the runner.

**Not an ATA fix:** untraced `public-service-run.2Tt8jWPz` passed the initial
retained-handle checks but failed at blank-disk repetition cycle 29, ATA command
0xc8, LBA 32, 114688 bytes, status 0x58. Retaining the device did not remove the
intermittent DMA timeout. The passing traced run changes timing and must not
replace that failure or be reported as reliable untraced native storage.

`GET_FSACL` and `SET_FSACL` now use the same native bounded transport, with
the latter restricted to a source nvlist and boolean grant/revoke action.
The upstream dispatcher retains schema validation and delegation permission
checks. `public-service-run.DcgEuXj8` passes the complete strict 4-CPU regression
using `full-delegation/OpenZFSServiceNative` and
`cli-probe.DfbpUdBq/OpenZFSPublicServiceTest`: root grants local bookmark/destroy
permissions to native UID 77 through actual `zfs_set_fsacl`, reads both back
with `zfs_get_fsacl`, then the reader's independent authenticated connection
successfully creates/destroys a bookmark. Root revokes both permissions; a
fresh ACL query requires both absent, and both operations are denied again.
The original bookmark GUID remains unchanged. Earlier `VnjoWxhn` also passed,
before tightening the ACL assertion to independently check both removals.

Earlier full-command link inventory `cli-link.sVNaMzKh` remained failing: `zfs` lacks
`libshare_nfs_type` and `libshare_smb_type`; `zpool` additionally lacks
`zpool_label_disk` and `zpool_relabel_disk`.
Do not infer complete runnable commands from the native operation tests.

Native GPT entrypoints now live in `aros/cli/gpt.c`: real
`zpool_label_disk`/`zpool_relabel_disk` calls use authenticated management GPT
requests, not POSIX opens of invented device paths. Whole-device short/full
paths are parsed canonically; partition paths are rejected. Geometry is read
then checked again by the service; existing-pool start blocks are cached or
read from native GPT partition 0 with the upstream default fallback. Error
details flow through libzfs, and service write/flush/readback replaces Linux
udev or ioctl pretending. Callers still must provision media free of external
raw consumers and perform normal upstream in-use checks.

`cli-link.Ojj7UZva` compiles and resolves both entrypoints; whole commands still
fail on `libshare_nfs_type` and `libshare_smb_type`. Native CLI runtime testing
is **not yet passing**: `native-import-run.rgCRk9Ya` and `DlcCZRy5` performed
real CLI labeling but failed the independent partition-catalog assertion.
Host sgdisk sees valid data at slot 0 and reserved space at slot 8; AROS reports
count=1, data-start=2048. The partition.library GPT reader advances its entry
pointer at the loop bottom, but empty-entry `continue` bypasses that advance,
so it never reaches an occupied entry after a gap. This is retained as a real
native integration defect, not hidden by weakening the two-partition check.
The cached-start/relabel assertions after that failure have not executed.

The isolated parser fix in `aros/platform/partition-gpt-gaps.patch` moves
pointer advancement into the GPT loop increment, covering empty-entry
`continue`. `build-partition-gpt-overlay.sh` copies the source/objects and
relinks a private library; shared source, objects and library hashes remain
unchanged. It replaces only partition.library in a private `aros-fs.pkg.xz`,
verifying member order and every other member's bytes. A disk-only replacement
in `native-import-run.CqVtlf6H` still failed because the boot package supplied
the resident old library; that failed attempt is retained.

With `partition-gpt.1LReDZq8` loaded in the boot package, fresh QEMU run
`native-import-run.p7vo0oqe` passed the unchanged count=2 assertion and the
actual libzfs CLI entrypoints: canonical short/full names, invalid/partition
path rejection, upstream error codes, default 2048 start, relabel/no-op and
cached 4096 start. Independent host GPT validation passed, with raw fixtures
and the untargeted overlay unchanged. This fixes and verifies the private
integration; the shared OS has not been patched or installed. Existing-pool
config-derived start selection and full command execution are still separate
coverage gaps, and the NFS/SMB provider link failures remain.

`native-import-run.xDqiODyp` subsequently closes config-derived start coverage:
the actual libzfs entrypoint traverses a root with a non-whole-disk child before
a native whole-disk child, reads the existing 4096-sector GPT start, caches it,
and preserves it in a new label. Removing both cache and config selects and
verifies the upstream 2048-sector default. All earlier CLI/API/mirror checks,
host GPT validation and original/untargeted image hashes passed in the same run.
Full-command execution still awaits real sharing providers. Inspection found
the SMB client in the AROS network tree, not an export server; backend selection
has been requested rather than adding success-returning provider tables.

Client default discovery uses the native `/AROS` device namespace, expands
the authenticated registered inventory into whole disks and native partition
extents, excludes optical/tape/unknown unit types, and feeds those candidates
to the actual upstream threaded label scanner/configuration assembler. Native
shortnames resolve through real geometry/partition queries rather than DOS
`access()` on fictitious filesystem entries; truncation is rejected. Explicit
file-vdev directories remain supported. The engine's separate explicit-file
search policy is unchanged, avoiding recursive RPC into its own server.

Current service `full-default-discovery`, client `cli-probe.d5z49oPK`, and
private ATA overlay `ata-inquiry.zqK1c24M` have qualified verification:
`public-service-run.4me8WOBi` found the GPT-contained qemu_mirror pool by
default discovery (no supplied device path), then failed the retained ATA
alternating-read test at cycle 16 on blank unit 1. A second installed-QEMU,
untraced run `QlakpAOX` reproduced the same DMA 0xc8 timeout during the native
policy query. Both failures remain primary instability evidence. Private
worker-traced QEMU run `pKspppiH` passed the entire strict four-CPU suite,
including two default-discovery passes, optical exclusion, pool/vdev GUID
and candidate path identity, bounded shorthand resolution and non-root denial
without a cache. Original and attached fixture hashes stayed unchanged.
Tracing changes timing: that pass is functional evidence, NOT an ATA stability
fix. Discovery previews labels with can_be_active; it does not prove native
pool activation, exclusive ownership, writable import or foreign-FS safety.
The earlier service build failed a duplicate default-search symbol; keeping
the native search definition client-only fixed that link separation. Logs
retain both builds and all test runs (`default-discovery-*`).

Registered storage inventory is available through a fixed, root-authorized
native RPC. The implementation enumerates storage controllers, buses and
units using AROS HIDD hooks and copies device/unit/type/removable/model/serial
metadata while the enumeration locks are held. No guessed unit ranges, live
object pointers, disk opens, imports or writes are used. Its 64-unit bound,
duplicate identities and oversized strings produce errors rather than a
silently incomplete successful inventory. Output is unchanged on errors.
This metadata snapshot does not establish persistent identity or ownership.
`full-inventory` service/client passed strict four-CPU partition-fixture QEMU
`public-service-run.wSHj79md`: eight repeated inventories require all three
ATA units, correct disk/optical types and nonempty model/serial metadata;
non-root, absent-service and malformed-request checks pass as well. This
test does not yet prove automatic import discovery uses the inventory.

Native `check_device` now routes actual exact-extent label queries through
upstream `check_file_generic` force/spare policy, rather than duplicating
pool-state policy. Whole-disk paths additionally inspect each reported native
partition: a GPT-contained exported pool is rejected without force just like
its explicit partition path. Failed label/control queries and non-root access
remain failures even with force. Whole/partition type mismatches are rejected.
For partition.library's EIO no-table result, an independent successful read
of an entirely zero first sector permits the unpartitioned membership path;
this is NOT proof that the entire disk is blank or free of foreign filesystems.
Nonzero/unreadable first-sector metadata does not use that fallback. This
hook is pool-membership preflight, NOT exclusive device ownership, mounted
DOS-volume detection, foreign-filesystem detection or writable device admission.
Those remaining guards must precede general native label/write support.

Strict four-CPU QEMU `public-service-run.8S1tzucm` passed with current client
`cli-probe.FcuWikAQ`, service `full-inquiry` and private ATA overlay
`ata-inquiry.zqK1c24M`. It verifies exported labels with/without force,
whole-disk child traversal, failed service-query rejection, blank membership,
missing/malformed/type mismatch rejection and non-root force denial. Existing
RAM-backed active/readonly pool tests still pass through the common policy.
Original fixture and all three attached disk hashes are unchanged. Markers
are required by the runner. Full link `cli-link.O3Za74Lt` no longer lacks
`check_device`, but the five symbols above remain unresolved. Logs:
`native-preflight-{children-probe,client,qemu,command-link}.log`.

`check_sector_size_database` now uses a real root-authorized native standard
SCSI INQUIRY and the unchanged upstream 24-byte model quirk table, shared in
`cmd/zpool/zpool_sector_quirks.h` with Linux. It does not infer physical sector
size from logical geometry or invent an override on an unknown model. Whole
device and partition paths query the same unit. No arbitrary SCSI commands,
caller pointers, writes or write leases are accepted by this fixed-size RPC.
Errors leave output unchanged. Non-root credentials are checked freshly by
the resident service. Drivers that reject standard INQUIRY remain unsupported
for model quirks; different vendor/product formatting is not heuristically
rewritten to match the table.

Service `full-inquiry/OpenZFSServiceNative`, client `cli-probe.rVQ8eeFx` and
the previous ATA overlay passed unknown-model four-CPU QEMU regression
`public-service-run.DK21IHAy`. Known-model runs `L0YR2RVu` and `rgIuOwl4`
failed honestly: ATA SCSI emulation writes firmware at byte 24, overwriting
four bytes of the 24-byte model. The diagnostic bytes show `2.5+` in the
model. Retained `aros/engine/ata-inquiry.patch` places firmware at byte 32.
It was applied only to private overlay `build-aros/ata-inquiry.zqK1c24M`,
which retains its exact source, build script, commands and build log. No
shared AROS tree or installed driver was modified. Patch handoff: the platform issue tracker.

With that overlay and client `cli-probe.KHQsNV9F`, four-CPU run
`public-service-run.3akedreH` passed the complete strict regression. QEMU's
explicit test model `ATA     ADATA SSD S396 3` exercises the actual native
INQUIRY and upstream 8192-byte quirk entry (including partition lookup).
Absent devices/service, malformed RPCs and non-root denial are also checked.
Both backing disk hashes remain unchanged. This tests model lookup, not
8 KiB physical media or power-loss durability. Opt in with
`AROS_TEST_REQUIRE_PARTITIONS=1 AROS_TEST_SECTOR_QUIRK=1`; matching markers
are mandatory. Whole-command link `cli-link.Jg4ThKze` confirms the quirk
symbol resolved; six other zpool symbols still prevent complete linking.
The same corrected overlay/client also passed unknown-model regression
`public-service-run.62ZZLFqj` (exit 0, match=0), independently checking that
the ATA correction does not force a quirk for normal QEMU model strings.

Common CLI call sites now restrict Linux device-mapper identification and
enclosure sysfs refresh/use to Linux. Upstream FreeBSD implements those
queries as no-ops; AROS does not acquire dummy implementations to link them.
An imported Linux enclosure pathname is not passed to AROS helper commands
as a usable native enclosure. Native media geometry, identity, admission,
partition labeling, sector quirks and search policy remain separate port work.
All common sources compiled in `cli-probe.iF2ox8GO`; the resulting client
passed strict four-CPU QEMU regression `public-service-run.rOMcW3xc` (exit 0).
This regression covers existing service/file-vdev behavior, not native
enclosure control or complete CLI execution. Link `cli-link.fLmuXPkr`
confirms the two Linux-only references gone and retains the seven genuine
unresolved symbols listed above. Logs: `platform-hooks-{probe,client,command-link,qemu}.log`.

The native `zfs_dev_flush` hook now validates a regular-file descriptor and
calls the CRT's real `fsync` (DOS buffered Flush plus handler ACTION_FLUSH).
It does not treat an Exec device capability as a POSIX descriptor or claim
device cache invalidation. Nonregular descriptors are rejected, and syscall
errors propagate. Four-CPU run `public-service-run.0Jvf6kgE` passed the full
strict regression with client `cli-probe.0YvPU7VH` and service `full-pool-sync`.
The required file-flush marker covers write/flush/close/reopen payload equality
and EBADF for invalid, closed and read-only descriptors. This RAM-backed test
is not a power-loss durability test. The complete command link confirms this
symbol resolved, but the remaining blockers above still prevent runnable CLI
delivery. Build/link logs are `native-flush-{probe,client,command-link}.log`.

The authenticated bounded-nvlist transport now admits `BOOKMARK`,
`GET_BOOKMARKS`, `GET_BOOKMARK_PROPS`, and `DESTROY_BOOKMARKS` to the actual
upstream dispatcher and its permission/read-only checks. No bookmark state is
reimplemented in the client or service adapter.

`public-service-run.AvMUnRxl` passed the strict 4-CPU RAM-backed QEMU regression
with `full-bookmark/OpenZFSServiceNative` and
`cli-probe.2l6ODSPR/OpenZFSPublicServiceTest`: feature enablement via upstream
pool properties, bookmark creation, duplicate rejection, snapshot GUID match,
listing, survival after source snapshot destruction, destruction, ESRCH on a
fresh lookup, and absence in a fresh listing. The existing busy-mounted snapshot
protection and complete mirror/DOS/read-only-reimport gates also passed. This
does not establish bookmark cold persistence, nor does it resolve the separate
intermittent native ATA read failure.

`public-service-run.4DZ0NXWK` additionally passes bookmark-specific permission
tests with `cli-probe.yFxx86qq/OpenZFSPublicServiceTest`: a reader opens its own
authenticated ticket and control handle; upstream create and destroy reject it;
fresh root queries prove the existing bookmark GUID is unchanged and the denied
new bookmark is absent. The complete strict regression passes. Retained
`public-service-run.3uWFiL6p` was a test-setup failure: opening a control handle
through the process-wide root connection after changing identity returned
EACCES before the intended operations. The corrected test uses an independent
reader connection, not relaxed session ownership or substituted credentials.

Retained failed runs: `cuip1Znr` returned ENOTSUP because the disposable pool
had not enabled `feature@bookmarks`; `YU8v4NsG` reached the final absence check
but expected the wrong errno. `tsHeNM7y` narrowed that failure to actual ESRCH
versus expected ENOENT. Upstream `dsl_bookmark_lookup_impl` explicitly maps
missing bookmarks to ESRCH; the test was corrected, not the engine semantics.
All run directories above are under `build-aros/public-service-run.*`.

## Current dispatcher dependency inventory

`ioctl-probe.HUhj6WVF` compiles the full upstream dispatcher and on-exit unit
after native suspend/resume/end and ZPL lifecycle adapters landed. This is not
a linked ioctl endpoint. The expanded `probe-dispatcher.sh` also compiles the
real event and zvol dependencies: `ioctl-probe.sXefxM0r` passes `zfs_ioctl.c`,
`zfs_onexit.c` and `zfs_fm.c`; `fm.c` lacks the native condition-variable header,
and `zvol.c` lacks hlist/atomic-counter/RW_NONE platform primitives. Their full
error logs and source snapshots are retained rather than selecting empty
libzpool implementations to make a link appear successful.

Link inventory also requires native device-private state, event FD ownership,
mount-cache integration and zvol OS operations. Three existing native permission
exports overlap upstream dispatcher definitions (snapshot/rename/destroy);
integration must deliberately choose the upstream implementation and preserve
native authentication, not suppress duplicate-symbol errors. The creation
callback now exports `zfs_create_fs` with real creation-property handling.

`ioctl-probe.gJc4YxVw` now compiles all five inventory units. Native
`sys/ccompat.h` reuses upstream's BSD-licensed portable hlist/counter code
with compiler atomic operations; no Linux/BSD kernel data structures are
substituted for AROS locks. `RW_NONE=-1` is deliberately distinct from the
existing native READER=0/WRITER=1 and is only a zvol lookup sentinel, not a
mode accepted by rw_enter. The condition-variable header exposes the existing
real native pthread/SPL implementation. This is not a zvol device driver.

`default-quota-run.8opDoRcB` passes middle/head/tail list deletion, backlinks,
reinsertion, atomic return values and 320,000 concurrent increment/decrement
operations across eight native threads, alongside the full filesystem fixture.
The rebuilt service/Manager (`production-platform-primitives.log`) passes
public lifecycle/session/mirror DOS regression in `public-service-run.3qyGBnFZ`.

Refreshed common CLI inventory `cli-probe.OXD6irlX` remains 16/24 units. Missing
integration is explicit: mount-table/statfs/unmount APIs, pipe2, native process
launch/environment/process-group semantics, and process-title support. Full
dispatcher linking additionally needs device-private/FD lifetime integration
and real zvol OS operations. libzpool's aborting zfs_file_get/put and diagnostic
on-exit substitutes must not be mistaken for functioning client descriptors.

## Full-dispatcher native zvol backend

The experimental `AROS_FULL_DISPATCHER=1` engine now links complete upstream
dispatcher, event, on-exit and zvol units without unresolved symbols. Native
`zvol-native.c` supplies resident-engine volume lifetime and I/O; it uses the
upstream DMU, range locks, transactions, ZIL and minor lifecycle. Volume opens
require authenticated administrative credentials. Initial I/O is serialized
under the suspend writer lock; this is not a performance claim.

`default-quota-run.gOu01SQG` passes the full engine fixture, including automatic
minor registration from upstream dataset creation, duplicate rejection,
credential rejection, aligned/bounded write and readback, flush, retained-open
export refusal, resize, read-only file reopen, byte comparison and minor removal.
The existing mounted rollback, stale DOS identity, accounting and quota
persistence gates also pass in this full-dispatcher binary. `ZVOL:<dataset>` is
an internal retained-file namespace, not an installed DOS assign or Exec device.
Public device requests, complete ioctl transport, cold volume persistence and
volume recovery/concurrency coverage remain to be implemented or verified.

Retained failures: `default-quota-run.fMG8LzLp` exposed missing-credential policy
ordering in upstream permission entrypoints; native entrypoints now reject NULL
credentials before dataset lookup. `default-quota-run.UkpYkWTG` faulted during
pool event publication: the userspace-ABI spa initializer saw inline no-op
`fm_init/fini` while the real event publisher was linked. Full-dispatcher builds
now use actual event initialization/finalization and rebuild spa_misc accordingly.
`default-quota-run.6DjQy68L` then reached volume creation but the fixture attempted
to register the already-created minor again (EEXIST). The corrected fixture
verifies automatic registration and separately requires duplicate rejection;
the backend's duplicate protection was not weakened to make the test pass.

The normal (non-full-dispatcher) service and Manager were rebuilt separately as
`production-zvol-regression`; `public-service-run.BrAbDmnE` passes public service
lifecycle, authenticated sessions and RAM-backed mirror DOS write/read plus
read-only reimport. Full-dispatcher integration is still experimental, not the
default production service path.

Expanded full-engine run `default-quota-run.Ko53AnzX` also passes real dataset
rename with asynchronous minor completion, rejection of the old name, preserved
bytes under the new name, visible snapshot registration, writable snapshot open
and write rejection, and removal/recreation of both registrations with unchanged
data. The QEMU gate requires this lifecycle marker when zvol testing is enabled.
Recreation here is within one engine instance, not cold-boot persistence.

## Resident dispatcher runtime

`zfs_ioctl_native_init/fini` initialize the actual upstream command table and
history TLS without reinitializing SPA, ZPL or rrw state owned by the resident
engine. These are internal owner-serialized lifecycle functions, an alternative
to the kernel-module startup path. Shutdown requires requests drained and worker
threads joined. Native history-key deletion explicitly releases the current
thread's value and the pthread key: the inherited libzpool `tsd_destroy` macro
is a no-op and cannot implement repeated native dispatcher shutdown.

Full-engine QEMU `default-quota-run.8E5f0N6K` passes 80 startup/shutdown cycles
(exceeding the SDK's 64 TLS keys), duplicate-start denial, real upstream
`ZFS_IOC_POOL_STATS`, decoding its packed result and checking the actual pool
name, out-of-range command denial, live-control-file shutdown refusal and
command denial after shutdown. Existing zvol and full ZPL/quota gates also pass.
The fixture registers only its own allocated output buffer with the native
bounded-copy scope. It does not accept client addresses or expose an ioctl
transport; public session marshalling/routing remains unfinished.

Retained build `full-dispatcher-runtime.log` caught a const-qualified nvlist
output mismatch in the fixture. After that correction,
`default-quota-run.oi8tEaUj` returned EFAULT because the fixture omitted its
output copy scope. The scope protection was retained; the test now explicitly
requires unscoped copying to fail before registering the trusted output range.

Full-dispatcher `oz_engine_start/stop` now own command-table/history-TLS and
control-state lifetime. Startup failure unwinds the engine; shutdown refuses
while a control-file reference survives, before tearing down any engine state.
`full-dispatcher-service-native-header` passes the real public service test in
`public-service-run.V957sFQ6`, including complete service shutdown cycles,
authenticated sessions and mirrored DOS write/read/read-only reimport.
`default-quota-run.WhR9rxmr` verifies service-owned initial activation, engine
stop refusal with a live control file, all 80 dispatcher cycles and all existing
zvol/ZPL/quota gates. The initial integration build
`full-dispatcher-service.log` exposed kernel-only types in the ioctl implementation
header; resident lifecycle declarations now have their own minimal header without
changing the management object's native lock/allocator ABI. Public ioctl request
marshalling is still not exposed or advertised.

## Authenticated public control-file lifetime

`OZ_CONTROL_OPEN/CLOSE` now use fixed-size DOS packet payloads containing a
session ticket and session-local descriptor number. The full-dispatcher server
authenticates every packet anew, snapshots its request, checks reserved fields
and descriptor shape, then acquires the UID-owned live session. Open creates a
real upstream control-file state and stores a retained file in that session;
close drops the table reference. Opening a control file grants no permission to
perform a subsequent ioctl. Non-full builds return ENOTSUP for these operations.

Actual public-service QEMU `public-service-run.ITA8zaky` passes capacity 64,
EMFILE overflow, wrong-session and stale descriptor rejection, non-reused
descriptor numbers, changed-user open/close denial, stale session denial and
session-close cleanup. Abandoned child sessions each open control files before
exiting; existing task-removal reclamation runs four rounds of eight sessions
in each service mode. Complete service shutdown and mirrored DOS storage tests
also pass. The runner requires both control-test markers when
`AROS_TEST_REQUIRE_CONTROL=1`. This adds real descriptor lifetime, not yet public
ioctl command execution or a finished libzfs ioctl bridge.

The normal non-full service and clients were rebuilt independently as
`production-public-control-regression`; `public-service-run.BC6bHtgA` passes
all existing public lifecycle/session/mirror-storage gates with the unchanged
non-full engine path.

## Public upstream ioctl marshalling

The versioned `OZ_IOCTL_ACTION` envelope carries a pointer-free copy of upstream
`zfs_cmd_t` plus bounded packed-buffer storage. Admission freshly authenticates
the packet, makes an aggregate-size-capped private snapshot, validates the
session ticket and control descriptor, and enters native credential, caller,
file and copy scopes. Only service-owned buffer addresses reach the dispatcher;
engine addresses/internal flags are cleared before reply. Transport errors are
separate from upstream operation errno, so ENOMEM can return the required
nvlist output size without silently retrying an operation. At this stage only
POOL_STATS/POOL_CONFIGS are enabled; other command numbers return ENOTSUP.

`public-service-run.J4Doi7LG` passes actual client-to-service pool statistics and
configuration queries, decoding and matching the disposable pool identity,
output-size negotiation, reserved-field/raw-pointer/malformed-size rejection,
stale ticket and descriptor denial and unimplemented-command rejection. The
client links nvpair but no SPA/DMU/engine implementation (verified symbol list),
so this is not an in-process engine test. Existing session/control/task-exit,
service shutdown and mirrored DOS storage tests also pass. Initial build
`full-dispatcher-public-ioctl.log` exposed duplicate assert/backtrace objects
already included in the nvpair archive; the corrected link is retained as
`full-dispatcher-public-ioctl-link`.

The same transport now enables upstream SNAPSHOT, DESTROY_SNAPS, HOLD,
GET_HOLDS and RELEASE. Packed source nvlists occupy their own input-only copy
range; upstream validates each command's schema and permissions. Public QEMU
`public-service-run.kgz3W6HC` passes snapshot create/destroy, schema rejection,
temporary holds tied to real control descriptors, automatic on-exit hold removal
on descriptor close, and explicit release of persistent holds. Expanded run
`public-service-run.DtkOtMND` also switches native security identities: the
reader cannot use a root ticket, can open its own session/control, but is denied
snapshot creation by upstream policy. A subsequent root query verifies that
the denied snapshot does not exist. Both runs retain all public lifecycle,
control, query and mirrored DOS storage gates. No mutation is retried implicitly.

## Genuine libzfs_core native client

The upstream `libzfs_core.c` now uses native service open/close on AROS, and
`aros/cli/control-client.c` supplies its actual `lzc_ioctl_fd_os` bridge. This is
client code only: no pool engine is linked into the command process. The bridge
packs service-owned wire buffers, preserves original caller buffer addresses,
copies only bounded filled results, propagates upstream errno/required sizes,
and serializes connection lifetime and calls. The initial connection must be
opened from a native Process, not a short-lived worker task; process-wide
initialization from arbitrary worker threads remains future integration work.
History/share buffer marshalling remains ENOTSUP, as do command families not
yet enabled by the server. The bridge has a 128 MiB aggregate allocation cap;
the service may impose a smaller limit. It does not retry requests; upstream
libzfs_core retains its existing ENOMEM output-growth behavior.

`public-service-run.CzeUDxPr` passes genuine `lzc_snapshot`, `lzc_hold`,
`lzc_get_holds` and `lzc_destroy_snaps` calls, nested init/fini reference counts,
temporary-hold release via the native cleanup descriptor, and a fresh
connection after final fini. All public transport/authorization and mirrored
DOS storage gates also pass. Client symbols contain the upstream lzc functions
and no SPA/DMU/engine startup code. This proves the tested library paths, not
complete zfs/zpool binaries or all library APIs.

Retained link `full-dispatcher-libzfs-core` exposed missing `nvlist_print`;
`full-dispatcher-libzfs-core-print` exposed its absent libintl header. The final
`full-dispatcher-libzfs-core-nls` build includes actual upstream nvpair printing
and the existing explicit no-NLS English-message header. No tracing function
was stubbed. Refreshed CLI inventory `cli-probe.yU44DIFk` remains 16/24 common
units: mount-table/statfs/unmount, pipe2/native process launching and process
title integration still block the remaining units.

Normal service/client rebuild `production-libzfs-core-regression` passes all
existing non-full public lifecycle/session/mirror-storage gates in
`public-service-run.mzRPqPap` after these client and transport changes.

## Upstream dataset/volume commands and handler interoperation

The public dispatcher now enables CREATE, CLONE, ROLLBACK, DESTROY and
OBJSET_STATS as well. `public-service-run.4LnR7eT3` passes genuine libzfs_core
filesystem creation, duplicate-create rejection, existence queries, snapshot
clone/destruction, rollback target verification, invalid-volume-size rejection,
valid volume creation and destruction. The library-created `public_mirror/files`
is deliberately retained for the existing public native DOS mount/write/read,
export and read-only reimport test, which also passes. This checks shared live
state across upstream client commands and the filesystem handler, not merely
two independent fixtures.

Private RPC snapshots and client packed buffers use volatile zeroing before
freeing, since CREATE and future key operations can carry wrapping-key data.
This strengthens transport-buffer cleanup; it is not a claim that every upstream
nvlist allocation has been audited for secret lifetime.

The final secure-wipe rebuild `full-dataset-secure-wipe` passes strict public
QEMU run `public-service-run.l0ytT91E`, including lifecycle/session ownership,
snapshot authorization and on-exit cleanup, real libzfs_core commands, all
dataset/volume gates above, and the native DOS mirror round trip. This run uses
the updated transport cleanup in both service and client; it is not evidence
of complete zfs/zpool executables or cold-boot volume recovery.

### Upstream dataset and snapshot enumeration

The public native dispatcher also accepts DATASET_LIST_NEXT and
SNAPSHOT_LIST_NEXT, retaining upstream cursor, property and authorization
behavior. The latter permits a bounded source nvlist for upstream TXG-range
filtering; configuration nvlists remain rejected. No replacement dataset cache
is used by this path.

`full-native-enumeration` and strict QEMU `public-service-run.YwKLizUO`
verify the actual created filesystem and snapshot names, advancing cursors,
snapshot type, decodable property nvlists and ESRCH end-of-iteration for both
lists through the native libzfs_core ioctl bridge. Existing dataset commands,
authorization, lifecycle and mirror DOS round-trip gates also pass. This tests
the dependency used by upstream libzfs iteration, not a complete `zfs list`
executable. TXG filtering, concurrent enumeration/mutation and cursor retries
after output-buffer growth still need explicit coverage.

## Native mount registry snapshot

`OZ_MOUNTS_ACTION` returns a pointer-free, bounded snapshot of the resident
service's active DOS mounts: full stored dataset name, colon-terminated DOS
volume name and actual read-only flag. Each request is freshly authenticated
by the native packet security adapter. The service owner serializes capture
with mount/unmount, and excludes retired endpoints. This is not a machine-wide
table of unrelated filesystems and does not invent a `/proc` mount file.

The caller supplies exact capacity/length. Insufficient capacity returns
ENOSPC with the required count and no partial records; the caller must retry a
fresh query, since mounts can change between requests. Version, reserved fields
and response-only count are checked. Above 65,536 active mounts the interface
fails explicitly with E2BIG rather than truncating its result.

Full-dispatcher build `full-native-mount-snapshot` and strict QEMU
`public-service-run.RjcnbXhf` pass empty/writable/read-only snapshots, absence
of retired mounts, size querying and malformed-capacity/count rejection,
alongside all existing public ioctl and DOS storage gates. Upstream CLI mount
iteration still needs to consume this API; no completed CLI claim is implied.
Normal service/Manager build `production-native-mount-snapshot` and QEMU
`public-service-run.R4h27aiq` pass the same mount snapshot and DOS storage
checks without full-dispatcher mode.

`oz_mounts_read` now owns allocation, bounded size-race retry (eight attempts,
then EAGAIN), response validation and failure propagation. The caller owns the
immutable successful snapshot and frees it. Missing service remains ENOENT,
not an empty mount list. Upstream `zfs_iter_mounted` now consumes this native
snapshot on AROS while retaining its existing dataset filtering and callback
logic; other operating systems keep their original mount-table path.

`cli-probe.dDFQYYuz` now compiles 17/24 checked upstream units, including
libzfs_iter.c. Seven unrelated/uncompleted integration units still fail, so
the probe correctly exits 1. Build `full-cli-mount-reader` and strict QEMU
`public-service-run.lWxAEsJZ` exercise allocation, live mount contents, empty
results, missing-service propagation and release alongside all public/DOS
gates. This is runtime proof of the snapshot reader and compile proof of the
upstream consumer, not end-to-end execution of `zfs_iter_mounted` or the CLI.

The dataset mount-cache loader and noncached lookup also consume the native
snapshot. Noncached lookups refresh owned AVL entries while retaining the
initialized tree and mutex; they do not return pointers into a freed snapshot.
The existing libzfs borrowed-entry lifetime rules still apply. Capture failures
propagate instead of treating stale entries as a successful lookup. Other OS
paths retain their original mount-table behavior.

Native `hasmntopt` matches complete names (or name=value), skips leading option
whitespace, returns a pointer into the unchanged input and rejects empty names.
`cli-probe.DpNIPbYL` compiles 18/24 common units including libzfs_dataset.c.
Build `full-cli-dataset-mount` and strict QEMU `public-service-run.PvhDHZYU`
pass snapshot/reader and option tests (ro versus rollback, rw, name=value,
prefix rejection and unchanged bytes), plus all existing public/DOS gates.
Direct upstream cache execution is now verified by
`public-service-run.IO3p48tU` with `full-upstream-mount-cache` and the selected
upstream object from `cli-probe.TeRljtX5`. One actual libzfs handle survives
five states: empty, writable mount, unmounted, read-only remount, and exported.
Each state performs 32 alternating fresh/cached lookups using the actual
upstream AVL and allocation functions. The strict runner requires all five
markers as well as the full authenticated ioctl and native DOS storage gates.
This proves these cache functions, not a fully linked or packaged CLI.

`libzfs_init` now opens an authenticated native control descriptor on AROS,
instead of a POSIX `/dev/zfs` file. Its close paths use the matching native
operation; the libzfs_core reference remains independently balanced. AROS does
not attempt a Unix module loader: the actual service open reports absence or
denial. Failure cleanup releases the URI regex and, after full initialization,
the cache, core reference and checksum resources, while preserving errno.
`cli-probe.kqebyPMK` compile-verifies this integration (19/24 common units).
The subsequent `cli-probe.2NJIkF7r` builds a real upstream init/fini fixture,
including userspace zcommon, libuutil and SPL atomic dependencies (no pool
engine in the client). `full-native-libzfs-init` and strict QEMU run
`public-service-run.oIPbJEUf` pass 80 cycles of two simultaneous libzfs handles,
followed by all authenticated ioctl and native DOS mirror round-trip gates.
The native control session has only 64 descriptors, so this also detects
unbounded per-cycle descriptor leakage. Concurrent initialization from worker
threads remains unverified.

The expanded fixture found a real error-class bug in
`public-service-run.kUJzj3aM`: an invalid ZFS_SENDRECV_MAX_NVLIST setting
returned errno=-1 because the numeric parser's return code was mistaken for
an errno. Initialization now reports EINVAL after releasing its resources.
`cli-probe.h0MiiBVK`, linked with `aros/cli/build-init-test.sh`, and strict QEMU
`public-service-run.Sjbrk66U` pass the original 80 two-handle cycles plus
80 invalid-setting cycles while a valid handle remains open. Invalid text,
invalid suffix and overflow alternate; the valid 1M limit is inspected and
the previous environment value is restored. All full public/DOS gates pass.
This proves the tested post-initialization error path and bounded descriptor
lifetime, not allocation-failure injection or arbitrary concurrent init.

Native supported-feature/property discovery now queries the authenticated
resident service's actual upstream tables. A fixed-size, versioned packet
contains no pointers; malformed strings and input response flags are rejected.
Pool features and dataset/pool/vdev properties are supported query scopes.
Unknown names return false; unknown scopes return ENOTSUP. Kernel-interface
feature advertisement is not implemented yet and must not be inferred from
pool-feature support. No Linux sysfs tree or blanket all-features success is
used for AROS clients. The QEMU run verifies a known pool feature, all three
property scopes, an unknown feature/scope, and an absent service.

Build failures are retained in `init-probe.log` (Unix VFS header),
`init-probe-native.log` (sysfs search.h), `init-probe-uutil.log` (Unix pause/fork
hooks), and `cli-probe.D3969PPN/link.log` (missing real atomic/numeric objects).
AROS project attributes use the existing portable layout without Linux VFS
headers; libuutil uses Exec Wait for secondary panic threads and omits fork
hooks on this no-fork platform. The latest inventory is 20/24 common units;
zfs_main, libzfs_diff, libzfs_mount and libzfs_sendrecv still fail compilation.
This is not yet a full CLI link or installation result.

Native mount control now has a fixed-size authenticated packet separate from
GUI status messages, returning exact errno values. It delegates to the same
owner-task lifecycle/permission checks and native DOS handler; there is no
root fallback or force-detach of open DOS references. `full-native-mount-control`
and strict QEMU `public-service-run.ROGbXUBv` pass writable attachment, actual
DOS write/read, read-only reimport/attachment, duplicate EEXIST, busy-root-lock
EBUSY, absent ENOENT and invalid-volume EINVAL. The complete public ioctl and
libzfs initialization/failure-cleanup tests also pass. Upstream do_mount and
do_unmount wrappers were the next integration step; this packet alone is not
CLI completion.

`aros/cli/mount.c` now implements do_mount/do_unmount with native volume names
(`NAME:`), the authenticated mount transport and native mount snapshots.
It accepts ro/rw/defaults and the upstream zfsutil marker; unsupported mount
options and force/overlay flags fail explicitly rather than claiming an effect.
Unmount resolves the registered volume (case-insensitive native name), checks
any supplied dataset handle matches, and preserves exact native errors.
`cli-probe.Zjx6nfTm` and strict QEMU `public-service-run.noM5CCKo` prove actual
libzfs_init + zfs_open against the service, followed by the real adapters'
mount/unmount, duplicate EEXIST and unsupported-option ENOTSUP. All existing
public service, initialization and filesystem gates passed. The earlier
`cli-probe.KPEOjZgL/link.log` retains the missing iterator dependency failure.

The common libzfs mount flow now uses native snapshots for pool-wide mount
enumeration. AROS volume registration does not create/delete Unix directories
or perform overlays; native ZPL obtains its properties from the dataset.
`cli-probe.apganHzJ` compile-verifies libzfs_mount.c (21/24 common units).
The complete high-level zfs_mount_at/zfs_unmount/zpool_disable_datasets flows
are not runtime-proven yet. Dataset default mountpoint conversion, per-mount
namespace overrides, force/remount and sharing remain integration work.

The next fixture replaces the initial do_mount call with the actual upstream
`zfs_mount_at(dataset, "rw", 0, "OZCLIMOUNT:")`. Dependencies include real
libzfs crypto/status and the existing AROS BoringSSL archive; thin-archive
members are hashed in the client build evidence. `cli-probe.s8RuO0ix`, service
`full-high-level-mount`, and strict QEMU `public-service-run.tmhN9Md4` pass the
high-level writable mount, duplicate check, OS-level unmount and all existing
gates. This closes writable zfs_mount_at runtime proof for an unencrypted
dataset and explicit DOS volume, not encrypted mount or high-level unmount.

AROS userspace zone queries now identify the single global filesystem namespace
without Linux user-namespace inode constants. This does not confer privilege;
native credentials and engine permissions are unchanged. Pool-status queries
read the host ID actually used by the resident engine through a new authenticated
fixed-size packet. The QEMU fixture changes its own ZFS_HOSTID and verifies the
reported identity still matches the service. Zero remains unprovisioned, not a
generated machine ID; persistent host-ID provisioning is still outstanding.
`cli-probe.FRtzkvcC/link.log` and `cli-probe.YVsJwz33/link.log` retain the genuine
dependency failures (crypto/zone/status, then host identity) that led here.

The AROS branches of the real `zfs` command now enumerate the resident mount
snapshot for listing and all-mount traversal, retaining upstream JSON/text
output and traversal logic. Native colon paths enter mountpoint resolution;
the root resolver holds real DOS locks and uses SameLock, not a string-prefix
or same-volume guess. An assign to the root resolves to the registered native
volume. Ordinary files, unrelated roots and missing objects do not resolve as
mountpoints. The Unix fallback umount2 path is not used on AROS.
`cli-probe.HU1FLFDx` compiles 22/24 common units, including cmd/zfs/zfs_main.c.
`cli-root-client` and strict QEMU `public-service-run.Yk4ng0zQ` verify the root
resolver and all prior public/CLI-component/DOS gates. Full command linking,
actual command output, high-level unmount and sharing remain unverified or
incomplete: the unmount path still needs a real native NFS/SMB backend, not
success-returning replacements for libshare operations.

Native process-title support is implemented in `aros/cli/title.c`. The main
command captures its own CLI before starting progress workers; workers update
that CLI's bounded BSTR using the same representation as SetProgramName, with
a pthread mutex serializing updates. No task-name pointer is replaced. The
owner restores its original title after joining workers, with an atexit restore
also registered for normal command exit. A worker cannot release the owner's
CLI reference. Workbench/no-CLI callers report ENOTSUP rather than pretending
to update a title. AROS callers must compile this adapter with native pthread.

`cli-probe.t3NxGGTK` compiles 23/24 common units including libzfs_sendrecv.c;
only libzfs_diff's pipe2 dependency remains in this compile inventory. Strict
private platform QEMU `cli-platform.uSx8Gf3a` passes 16 worker update, 255-byte
truncation and explicit restore cycles alongside all existing platform gates
and actual upstream subprocess tests. Full send/receive remains incomplete.
`cli-platform.KXCCkPD2` retains the rejected static mutex initializer build;
the adapter uses pthread_once plus real mutex initialization instead.
`cli-platform.QG1xrTQn` retains the boundary-test failure: GetProgramName reports
truncation at capacity-1, so read buffers now allow a spare byte while writes
remain bounded to the native 255-byte limit. Automatic exit restoration and
concurrent external title readers are not separately runtime-proven.

Native path-to-dataset resolution now holds a real DOS target lock, snapshots
the active mount registry and compares mounted root locks with SameDevice.
This handles assigns without guessing from string prefixes and retains the
target lock through resolution. Missing paths, unrelated volumes, unavailable
service and undersized outputs return errors with an empty output buffer.
The AROS branch of upstream `zfs_path_to_zhandle` uses this resolver for native
colon paths (and the existing explicit path forms), while ordinary dataset
names still go directly to `zfs_open`.

Build `full-native-path-dataset` and strict QEMU
`public-service-run.52QsrGF4` pass real file and assign-alias resolution,
unrelated RAM-volume rejection, missing-file/service failures and output bounds,
plus all earlier public ioctl, mount and DOS round-trip gates. The test removes
its temporary assign before unmounting. `cli-probe.DNhDxpHv` remains 18/24:
libzfs_util.c no longer fails on extmnttab/getextmntent, but still needs native
process-launch wiring (pipe2/fork/setpgid/execvpe). The complete upstream
path-to-zhandle wrapper therefore is not yet runtime-verified.

## Native synchronous child capture

`oz_cli_capture_env_exact` composes the existing native exact-path launcher,
explicit/inherited environment handling and text collector with an exclusive
0600 `mkstemp` file under T:. The reserved descriptor remains open while a
native non-truncating stream is shared with the synchronous child. Output is
flushed, rewound and collected after child cleanup; both handles and the file
are released on success and failure. This avoids pipe-capacity deadlock without
pretending AROS has fork/process groups. The limit bounds accepted output, not
how much the child can produce; this is not a resource-limit mechanism.

`cli-platform.C48X9MFj` passes 16 capture cycles: explicit empty/inherited
environments, nonzero child status 23, empty lines/final partial lines, 128-KiB
output, collector-limit and embedded-NUL rejection, absent executable and
temporary-file cleanup. Directory removal proves no capture files remain.
Existing argument/process/environment/positioned-I/O/progress fixtures pass.
Upstream process-wrapper wiring and executable search remain incomplete.

Retained failures: `cli-platform.1k0X8CHB` omitted the already-required private
stdcio overlay and failed the existing shared-stream Flush test (error 219)
before capture. `cli-platform.0J46nafo` used that overlay but the minimal
fixture had no T: assign, visibly entering a missing-volume requester; only
its private QEMU was stopped through its monitor. Private fixture startup now
assigns T: to RAM:. The capture test temporarily redirects it to a fresh owned
directory, restores it, and verifies that directory can be removed.

## Upstream native process wrapper

The real `libzfs_run_process_impl` now calls the native launcher/capture adapter
on AROS. A separate native child executable (`SYS:Tools/OpenZFS/ExecHelper`)
calls the AROS runtime's execv/execvp, preserving its executable search and path
conversion instead of inventing another PATH parser or invoking a shell. The
helper must be built with SDK `-nix` startup support; use
`bash aros/cli/build-exec-helper.sh [output]` for a non-diagnostic artifact.
Final installation packaging remains pending. A missing helper is a launch
error, not successful execution. Upstream verbose stdout/stderr flags map to
native process streams, and NO_DEFAULT_PATH selects execv. Text capture uses
the collector's INT_MAX byte limit; it does not restrict child output volume.

`AROS_TEST_BUILD_PROCESS=1 ... probe-upstream.sh --all` builds a test retaining
the actual upstream process-wrapper and string-array cleanup functions from
libzfs_util.c. `cli-probe.vAOduxVW` compiles 19/24 common units, including
libzfs_util.c. QEMU `cli-platform.PsodNdik` passes eight upstream-wrapper cycles:
explicit PATH=/C lookup, exact C: paths, NO_DEFAULT_PATH refusing lookup,
stdout content/empty lines/final partial line, child exit 23, missing program
exit 255 and quiet execution. All existing CLI platform gates pass. Complex
argument/environment round trips across the extra exec boundary, scripts,
concurrent callers and full libzfs linkage still need further coverage.

Retain `cli-platform.cMDYoV45` and `cli-platform.41RWoNQi`. Diagnostics on the
second run identified two independent errors: the helper without -nix returned
ENOENT for PATH=/C, and passing SIZE_MAX to the collector returned EINVAL
because its documented implementation limit is INT_MAX. Both were corrected;
no success-returning fallback was added. Diagnostic helper logging is enabled
only by the private test runner, not build-exec-helper.sh.

### Exec argument boundary regression

Expanded upstream-wrapper test `cli-probe.d08ntXaq` failed in QEMU
`cli-platform.lRzQ5g4r`: the real argument-checking child received argc=7
instead of 8. The native launcher had preserved the empty argument, but the
runtime exec serializer quoted only arguments containing whitespace, dropping
the empty argument and leaving quote/star-only arguments unescaped.

AROS commit `cb33538a39` fixes `compiler/crt/posixc/__exec.c` to encode every
argument with its existing DOS escape routine. `build-exec-overlay.sh` rebuilds
only that object against frozen nightly internal headers and relinks the
existing private POSIX overlay; installed runtime files remain untouched.
Its successful artifact is `cli-exec.VG4qQCWw/posixc.library`.

QEMU `cli-platform.oOk5L05O` passes eight complete upstream-wrapper cycles with
empty strings, spaces, quote/star, newline, tab, literal shell-like text and
native paths. Explicit child variables, empty values, literal values and
exclusion of a parent-only variable also pass across the extra exec boundary;
the parent variable remains unchanged. All earlier CLI platform gates pass.
This runtime overlay is now required for those argument cases. Full-system
packaging and broader concurrency/script testing remain pending.

Retained overlay build failures: `cli-exec.UEojFvzS` lacked the stdc internal
header path; `rAKgtNfj` and `R4bIuSMg` linked against a wrongly mangled internal
getenv symbol; `lcERq8ou` and `JONqZ9Wc` lacked unistd.h after an unsuccessful
-noposixc experiment. Final flags use AROS_POSIXC_BUILD to select the runtime's
own symbol names, not an alias or unresolved-symbol workaround.

## Scoped native file references

The AROS `zfs_file_get/put` aborts are now replaced by explicit service-owned
descriptor scopes (`aros/engine/file-scope.c`). Enter copies a trusted binding
list and retains every file. Lookups only see the current thread's top scope;
nested scopes do not inherit outer descriptors, workers do not inherit parent
scopes, duplicate/negative descriptors are rejected, and TLS exit releases
abandoned scope references. Scopes are not client authentication, pointer
validation, numeric POSIX-FD import, or a public ioctl endpoint. The transport
must validate session ownership before constructing these internal bindings.

Native file open/device/reader constructors now create one owned reference.
Close/put release it, and only final release closes storage and invokes optional
immutable private-state cleanup. Borrowed reader contexts must survive until
that final release, not merely until the original owner drops its reference.
Private state can only be installed before publication and cannot be replaced.
The existing scoped caller and credential mechanisms remain independent.

Full-engine QEMU `default-quota-run.RBzfwbFo` verifies non-inherited worker
scopes, 8,000 concurrent get/put pairs, exit cleanup, nested scope hiding and
LIFO checks, immutable descriptor snapshots, and rejection outside a scope.
Both a reader-backed file and an actual exclusive RAM file remain readable
after original owner/scope release while a borrowed reference exists. Private
cleanup fires exactly once; the real file reopens with unchanged bytes.
All previous filesystem tests pass. The earlier reader-only run is retained
as `1Nnje4o1`. Full service/Manager build `production-file-scope.log` and
`public-service-run.l3yBHd2k` pass the production lifecycle/session/storage
regression. These tests do not establish the still-missing client FD table,
control-device minor registry, event-state lifecycle or public CLI transport.

## Session-owned native descriptors

The existing session cleanup object now owns a bounded 64-file table. Insert
retains the file, lookup returns a new reference, and descriptor close removes
the mapping before releasing it outside the lock. Positive descriptor numbers
increase monotonically for the session; closing a slot does not allow an old
number to alias a replacement. Exhaustion returns EMFILE (table capacity) or
EOVERFLOW (identifier space), not a reused identifier. Identical integers in
different sessions are independent and require the authenticated session ticket.

Session close rejects further lookups/inserts/scopes and drops all table holds.
Existing operation references and explicit descriptor-scope snapshots survive
that close and even final session destruction. Files, including private-state
destructors, are closed only at final release. No file callback runs under the
session lock. APIs require an existing live session reference or registry
ownership; they do not validate arbitrary client pointers.

`oz_admission_session_acquire` provides the service-task-only retained lookup,
checking ticket epoch/number and freshly authenticated UID. The real session
close transport now uses it. This is the entry point for future dispatcher
operations, not permission to accept a claimed UID from a payload. File opening
under the native caller identity, control minors/events, and the actual upstream
CLI transport still require integration; no new public file packet exists yet.

Full-engine QEMU `default-quota-run.1pLsOHqz` passes 16 capacity/stale/isolation
cycles, live scope/read retention across session destruction, and 16 native
workers finishing retained reads after owner close. Each private file destructor
runs exactly once. Existing filesystem fixtures also pass. The preliminary
single-thread variant is retained as `default-quota-run.1SHiwZw2`.
Rebuilt service/Manager (`production-session-files.log`) passes the public
session ownership/stale-ticket/task-exit and mirror DOS lifecycle regression in
`public-service-run.KkQaLDEV`, exercising the new authenticated lookup on the
real close path. This does not yet test public descriptor operations.

Focused fixture follow-up: `cli-session.GYO1e5IS` exposed a missing real
`oz_device_close` dependency after session cleanup began releasing files. The
builder now includes device I/O and descriptor scopes (not a close stub), and
archives their local headers. `cli-session.9g9IHeD6` links;
`cli-platform.NGdH3XU0` passes the platform fixtures plus 64 cleanup cycles,
16 workers and 144 callbacks. `cli-platform.7ILqKEzd` is a retained runner
configuration failure: the private stdcio overlay was omitted, reproducing
the already known child-output Flush error 219 before the session test ran.
The successful rerun uses the hashed overlay from `cli-platform.Avw90ZrH`.

The additive dispatcher link inventory is retained in
`session-files/dispatcher-link-inventory.log`. It confirms overlapping native
permission functions, existing userspace fm/zfs_fm objects, and libzpool zvol
substitutes must be replaced, not linked alongside kernel versions or resolved
with allow-multiple-definition flags. This failed inventory is not an executable.

## Integration requirements

### Upstream control-file state and full-link mode

`AROS_FULL_DISPATCHER=1` on `link-engine-test.sh` selects the actual upstream
dispatcher, complete on-exit module, kernel event/fault modules and zvol common
code. It replaces the userspace event objects and disables only the overlapping
libzpool diagnostic on-exit/zvol exports and native copies of three upstream
permission functions. Native credential capture and delegation are not disabled.
This mode keeps all sections: no allow-multiple-definitions or garbage collection.
It remains a link candidate, not an executable/service to deploy or start.

The first replacement build (`full-dispatcher-first.log`) passed the relocatable
link and exposed 18 native OS symbols. With native control files and platform
hooks, `full-dispatcher-platform.log` leaves 11, all zvol OS operations:
init/fini, create/free/remove/rename/is-zvol, capacity/read-only/volsize updates,
and wait-close. The retained source archive and whole linked object are under
`full-dispatcher-platform/`. Runtime integration must additionally arrange
SPA/ZPL/zvol/event/dispatcher startup and teardown exactly once; calling
zfs_kmod_init after the current kernel_init would initialize SPA twice.

`control-file.c` retains the actual upstream minor/on-exit/zevent state in a
native zfs_file. The final reference destroys that state, including event stream
membership and on-exit callbacks. A bounded session owns the descriptors; active
on-exit/event consumers retain independent file references. Owner-task shutdown
returns EBUSY while any such file survives. Non-owner open/stop is rejected.
No /dev path, public client-passed pointer, or AROS block device is fabricated.
Global minor-state lifecycle and state-pointer destruction were factored from
the upstream implementation so both native retained files and existing kernel
paths share the same state management rather than a duplicate registry.

The native nvlist input limit uses AROS total memory (one quarter, capped at
128 MiB) with the upstream explicit override. There are no AROS-only ioctl
registrations yet, and no cached Unix mountpoint properties to invalidate.
Those empty OS hooks intentionally do not advertise Linux user namespaces,
FreeBSD jails, Unix mount tables, or auto-mount policy as implemented.

`build-session-test.sh control` is a focused, section-selected fixture, unlike
the whole-engine link above. It runs real upstream control/on-exit/event code.
`cli-session.zoeX2jkA` and QEMU `cli-platform.oDaXIV3B` pass 16 subsystem cycles,
144 minor allocations, 16 real event post/read pairs, owner-task enforcement,
busy shutdown, stale-minor rejection, nvlist limit checks, and 16 worker-thread
final releases. Callbacks fire exactly once after the last consumer releases.
The earlier single-thread pass is retained as `cli-platform.vzthF6w1`.

Failure history: `native-control-first.log` lacked prerequisite upstream type
headers; `native-control-headers.log` exposed missing native atomic objects in
the fixture; `cli-platform.U3Yh9BnY` failed control open because the constructor
requires a non-null, retained reader context. The context is now static-lived,
and the runner detects control-test failures immediately. None of these tests
proves complete upstream command execution or public ioctl transport.
The normal (non-full-dispatcher) service/Manager rebuild is retained in
`production-control-regression.log`; QEMU `public-service-run.OrG7a6cX` passes
session ownership/task-exit and mirror DOS lifecycle regression. The experimental
full-dispatcher candidate was not substituted for that production service.

### Required end-to-end acceptance

- Retain upstream command parsing, output, exit status and libzfs/core semantics.
- Connect clients to the existing resident engine through authenticated native
  service transport. Do not embed independent pool engines in CLI processes.
- Capture caller authority at the service boundary and enforce native security
  and ZFS delegation. A confirmation flag is not authentication.
- Implement native discovery, mount handling and stream transport explicitly;
  do not reuse Linux device paths, helper scripts or fabricated successful calls.
- Unsupported operations must fail explicitly until implemented.
- Install both commands in the normal AROS command search path, with required
  runtime libraries and compatibility data.

## Reproducible initial compile inventory

### Resident dispatcher integration inventory

`bash aros/cli/probe-dispatcher.sh` compiles the actual upstream
`module/zfs/zfs_ioctl.c` and `zfs_onexit.c` against the native engine/ZPL
headers into a fresh private directory. It records sources, native headers,
compiler commands and diagnostics; it does not alter service binaries or run
storage operations. Compilation failure remains a nonzero exit.

Initial evidence `build-aros/ioctl-probe.3iuOQbGW`: the dispatcher fails;
on-exit compiles. This changes the integration order: the existing fixed-size
GUI rows cannot serve as the upstream ioctl/nvlist protocol. Before enabling
that protocol, the resident service needs:

- Native mount references and temporary holds (`zfs_vfs_ref/rele/held`,
  `zfsvfs_create_hold/free`), with teardown/config-lock ordering preserved.
  The current read-mount close path explicitly assumes no asynchronous
  borrowers; admitting CLI references without changing that would be unsafe.
- Receive/suspend/resume and snapshot-unmount integration, quota/version
  setters, and native lifecycle declarations instead of Linux ctldir types.
- Service-owned input/output buffer marshalling, caller identity and security
  policy adapters. Raw client pointers must not be treated as validated native
  buffers, and output-size retries must preserve upstream error semantics.
- Real per-client on-exit state and close cleanup. `lib/libzpool/kernel.c`
  currently supplies diagnostic `zfs_onexit_*` substitutes, including an
  add-callback routine that returns zero without retaining a callback. Linking
  the real upstream unit requires replacing those definitions, not accepting
  them as production cleanup or linking duplicate symbols.

This is an integration failure inventory, not a runtime transport test or a
claim that either CLI is now linked. Existing GUI tests do not cover these
new client lifetime requirements.

### Scoped upstream ioctl copies

`aros/engine/ioctl-copy.c` implements ddi_copyin/out, xcopyout and copyinstr
against explicitly registered service-owned buffers. The native sunddi header
exposes the declarations and upstream FKIOCTL value. The full engine recipe
links these implementations, but no public ioctl endpoint is enabled yet.
The eventual marshaller must copy client input into owned allocations before
registering ranges; this internal API must never register client-provided raw
addresses or be presented as arbitrary-pointer validation/authentication.

Ranges carry input/output permissions and checked pointer arithmetic. Nonzero
copies require a live matching thread-local scope and fit wholly within one
range. Wrong flags, wrong direction and out-of-range requests fail with EFAULT
before copying. Nested scopes do not inherit outer ranges; LIFO/thread checks
protect scope removal. Metadata is snapshotted at enter and released on thread
exit; allocation ownership stays with the request. Bounded strings require a
terminator within the registered input and destination limit, returning
ENAMETOOLONG for a too-small limit or EFAULT for an exhausted source range.
The trusted engine-side buffer still must satisfy its normal size contract.

`build-session-test.sh ioctl-copy` retains a host ASan/UBSan run and native
build. `cli-session.KLOBzAbR` and private QEMU `cli-platform.yTSgk4kQ` passed
boundary/direction/overflow checks, unchanged output on failed copies, bounded
strings, nested scope rejection/restoration, and 16 independent worker scopes
with thread-exit cleanup. All ten prior CLI fixtures also passed. The runner
accepts `AROS_TEST_IOCTL_COPY_BINARY`. These are helper tests, not successful
ioctl requests through libzfs_core. Dispatcher probe `ioctl-probe.xCjD13kI`
no longer reports missing copy helpers/FKIOCTL, but other native adapter errors
remain. Full engine build `production-ioctl-copy-build.log` completed and
contains the actual helper definitions, not unresolved symbols or stubs.
Independent full-service QEMU `public-service-run.6eLv2v9v` passed three
lifecycles, both authenticated/session-reclamation suites and the RAM mirror
DOS write/read/export/read-only-reimport regression with those helpers linked.

### Native mounted-filesystem references

Native mounts now publish their fully initialized `zfsvfs_t` through the real
upstream objset user pointer. `zfs_vfs_ref/rele/held` use os_user_ptr_lock, as
required by upstream getzfsvfs_impl. One owner reference keeps the publication
alive; borrowed references make native close return EBUSY before any teardown.
Successful detach clears the pointer atomically with dropping that owner
reference. If later teardown fails, the existing owner retains cleanup state
but it is not republished for new ioctl borrowers. Existing DOS callers still
serialize their own I/O/close; this is not a new concurrent DOS I/O guarantee.

`build-session-test.sh mount-reference` builds a focused fixture with actual
upstream objset accessors, native mount code and native mutexes. Build
`cli-session.x5gpqQEM`, private QEMU `cli-platform.OsYyAkIy`, passed 64
publication/reference/busy-detach cycles and 16 threaded borrower cases.
Duplicate publication is rejected and detached lookup returns ESRCH. This
fixture uses allocated objset structures, not disk data. It ran alongside all
nine prior CLI fixtures; the runner exited zero. Set
`AROS_TEST_MOUNT_REFERENCE_BINARY` to its binary to reproduce.

Separate full build `production-mount-ref-build.log` and QEMU
`public-service-run.8ksU3H40` verified actual RAM mirror creation, native DOS
mount/write/read/unmount, export/reimport read-only and write denial, plus
the public lifecycle/session/abandoned-task regressions. That storage test
does not yet issue concurrent ioctl borrowers against a mounted dataset.
Dispatcher inventory `ioctl-probe.BcmiF4KY` still fails on other native gaps,
including temporary unmounted holds, suspend/resume, copy and policy adapters.

### Upstream on-exit state with native operation references

`session-cleanup.c` now owns a real upstream `zfs_onexit_t`. Upstream callback
registration is factored into `zfs_onexit_add_cb_state`; the existing minor-based
entry still resolves its state and invokes the same implementation. Native
close marks the session closed and drops its owner reference. In-flight
operations retain references, so callbacks run only after the last release,
outside the session lock. Closed sessions reject new holds/registrations.
These are service-internal pointers, not authenticated client handles: a future
registry must serialize lookup/acquisition and map caller identities itself.
Close consumes the owner exactly once; using a freed pointer is not supported.

`AROS_NATIVE_ONEXIT=1` omits the three libzpool diagnostic substitutes when
building the focused upstream cleanup fixture. The public service now links
the upstream state-only portion (`AROS_ONEXIT_STATE_ONLY=1`) with the native
session registry described in `aros/engine/PUBLIC-SERVICE.md`. Its unrelated
libzpool FD/minor diagnostic route remains separate, not a usable CLI transport.
FD/minor mapping is not yet wired. Opening-task removal now retires sessions
through task.resource notifications, verified with children that exit without
closing. This does not cover forced termination during a borrowed DOS packet
or in-flight storage operation; see the public-service evidence and contract.

`build-session-test.sh` links a focused fixture from real upstream on-exit,
list, mutex and diagnostic code. Section selection deliberately excludes the
unimplemented FD/minor route; no replacement success stubs satisfy it.
Build `cli-session.65nDFLEG` and private QEMU `cli-platform.7vY2F7p9` passed
64 deferred-close cycles, empty close, invalid callback rejection and 16
threaded close/last-release cases, with exactly 144 expected callbacks.
Post-close registrations and holds fail with EBADF. All eight existing CLI
fixtures passed in the same guest; the runner exited zero. Reproduce by passing
the built binary as `AROS_TEST_SESSION_BINARY` to `test-qemu-platform.sh`.

Preserved failures: `cli-session.DZbZiovE` lacked a zfs_file type include;
`BBB6sztQ` and `X8tNxMh0` exposed real diagnostic link dependencies. The first
QEMU run `cli-platform.Ohzza80T` used graphical stdout for its result and timed
out without a serial verdict; that run is not counted as passing. The corrected
test emits its verdict to serial. This does not test client crashes, pool
mutation, FD mapping, public service integration, or hardware durability.

Run `bash aros/cli/probe-upstream.sh` with the same AROS SDK and private pthread
headers used by the native engine build. The probe creates a fresh evidence
directory, records compiler/flags/source hashes, and compiles nine real upstream
translation units independently. It exits nonzero if any unit fails. This is a
portability inventory, not a linked CLI build or functional acceptance test.

Initial evidence: `build-aros/cli-probe.PyT65Fm0`, based on engine commit a965121.
Eight units encounter missing `libintl.h`; libzfs_core encounters an undefined
`BIG_PIPE_SIZE`. These are first failures, not the complete dependency list.
The source also requires replacing its device/ioctl transport for AROS.

Use `--all` for all 24 common command/libzfs/libzfs_core/libzutil units (not
Linux OS-specific implementations). `cli-probe.Ox2maGZM` compiled 10/24.
After including the defining sys/sysmacros.h directly in zutil_pool.c,
`cli-probe.oZp5YKDu` compiles 11/24; the pool-history utility no longer treats
ISP2 as an undeclared function. Remaining compile categories are native mount
table/VFS, process pipes/launch, positioned-I/O aliases, crypto dependency
headers, dynamic loading, timers/process titles, direct-I/O and generated
version metadata. Both inventories deliberately exit 1 for remaining failures.
They do not establish linkability or runtime correctness. The expanded probe
retains its own script and source diff alongside the per-unit diagnostics.

`cli-probe.Br2RlKRQ` advances to 13/24 after mapping pread64/pwrite64 to the
existing native 64-bit pread/pwrite exports through the SPL header. No seek/read
emulation was added. `cli-platform.JNZBjS29` passes all six native fixtures,
including positioned overwrite/readback, unchanged sequential offset, negative
offset rejection, and EOF beyond 4 GiB (proving the offset is not truncated).
This small RAM-file test does not establish large-file write durability or
concurrent access to duplicated descriptors; the native CRT still requires
per-handle serialization.

`cli-probe.chMZyMKB` reaches 14/24 with the same configurable BoringSSL headers
used by the engine build (`AROS_CRYPTO_INCLUDE`, read-only). libzfs_crypto.c now
compiles; its CLI runtime/prompt and link remain unverified. Removing unused
dlfcn.h includes from mount/pool exposes native mount gaps and the compatibility
profile reader's file-mapping dependency.
No dynamic-loader functions were removed or stubbed. All remaining failures
are preserved in the per-source logs.

### Native portability progress

The CLI-local `libintl.h` is an explicit no-NLS build: upstream English messages
are retained, with singular/plural selection. It refuses `ENABLE_NLS`; it does
not pretend to load catalogs. Catalog localization remains future work.

`zpool_online_cpus()` queries AROS processor.resource instead of linking the
engine into the client. Other platforms retain sysconf with a minimum of one
CPU. The core receive path does not assume FreeBSD pipe resizing on AROS;
its advisory capacity-tuning call returns zero (unused by the receive caller).
This does not implement or validate CLI stream transport.

Probe `build-aros/cli-probe.50ZeGclP` compiles **five of nine** upstream units:
zfs_iter, zpool_main, zpool_iter, zpool_util and libzfs_core. Remaining failures
include native mount tables/VFS declarations, Unix subprocess launching,
O_DIRECT semantics and generated version metadata. No executable CLI is linked.
Proposed installation roots in probe flags are ENV:OpenZFS for runtime configuration
(saved configuration belongs in ENVARC:OpenZFS and is copied at startup),
SYS:Storage/OpenZFS for data and SYS:Tools/OpenZFS for helpers; packaging still
needs implementation. No Linux helpers have been installed.

`test-qemu-platform.sh <probe-directory>` links only the real upstream utility's
CPU helper plus the message-selection test, using section garbage collection.
This small fixture is expressly **not** a successful zpool link. It runs in an
isolated two-vCPU AROS guest with a read-only ISO, no disks and no networking.
`build-aros/cli-platform.CmcYRYCW` passed with CPUs=2 and runner exit 0.
The earlier `cli-platform.7CUHsY7z` guest printed PASS, but the runner failed to
recognize AROS's timestamp-prefixed logging; it was explicitly stopped and the
matcher corrected before the independent successful rerun. Both logs retained.
Host message-selection tests also pass; ENABLE_NLS is correctly rejected.

### Native helper argument boundary

`arguments.c` encodes Unix argv[1..] into the argument string consumed by direct
AROS RunCommand/NP_Arguments. It quotes every argument, escapes stars, quotes
and newlines using AmigaDOS syntax, and appends the required terminal newline.
Allocation is caller-bounded with overflow-safe checks. This is **not shell
escaping**: do not pass its output to SystemTags or another shell interpreter.

Host ASan/UBSan checks pass for exact output, exact-size success, size-limit
rejection and invalid input. Native QEMU run `cli-platform.PaXAbxrm` passed:
LoadSeg/RunCommand executed a separate child that compared all seven arguments
(empty, whitespace, quotes/stars, newline, tab, literal shell metacharacters,
and native path). Parent verified its actual return value 17. Both this test
and the existing CPU/message fixture passed, runner exit 0.

Initial native fixture compilation in `cli-platform.TSrNbX3w` caught the SDK's
mutable STRPTR RunCommand parameter and 32-bit LONG printf mismatch; both were
corrected before the successful run. No guest was started for that failed build.

This is a tested component for the native subprocess adapter, not that complete
adapter. libzfs_run_process_impl still needs isolated child process lifetime,
environment inheritance/replacement, path search, stdout capture and stderr
policy. Production must not use the test's same-process RunCommand approach
from a shared libzfs worker: native CRT ownership and concurrent caller state
must remain isolated. The upstream Unix implementation remains unchanged until
the native implementation is complete and tested.

### Separate native process lifecycle

`process.c` adds an exact-path native executable launcher using LoadSeg and
CreateNewProcTags, with a real separate process and NP_Synchronous completion.
The current DOS implementation supports NP_Synchronous despite the SDK tag
header's stale comment. An exit callback stores the program return value without
calling the parent's CRT from the child. The parent waits through DOS cleanup,
unloads its segment, and retains ownership of output/error handles. Arguments
are bounded at 64 KiB. DOS launch diagnostics are retained separately from the
program's exit status. No shell, PATH search, credential elevation or fabricated
success is used.

Final private QEMU evidence: `build-aros/cli-platform.xQXuOyN1`, runner exit 0.
Sixteen separate launches passed exact output/return-17 checks; handles remained
usable for parent reads/writes/close. Each child required an inherited local
environment value and changed its own copy; the parent independently retained
the original value after each launch. Missing executable returned failure with
a native DOS error and no successful program status. Existing argument and CPU
fixtures passed in the same boot. Security/delegation and concurrent launch
behavior are not covered by these tests.

Preserved failures: `cli-platform.an9x2cFO`, `LlVqzWAL`, `OLwRiQpM` expose a
post-child buffered-DOS Flush error (219, ERROR_SEEK_ERROR). The file position
was 18 and exact output bytes/continued handle use subsequently passed. A plain
raw-DOS write control in `QXjYOC1s` and the final run flushes successfully, so
this is **not** established as a generic RAM Flush limitation or fixed bug.
Current tests inspect actual child output after CRT exit, without the additional
parent Flush. That does not prove buffered-handle consistency or durability;
the flush mismatch remains open before broad helper-transport acceptance.
`kD7sVRLl` was an earlier environment-isolation pass; the final test additionally
requires presence of the child's inherited value (no absent-variable skip).

This launcher is not yet wired into libzfs_run_process_impl: environment
replacement, search semantics, stdout-to-lines collection, stderr policy,
concurrency/security testing and the buffered-I/O discrepancy remain.

### Buffered-I/O discrepancy resolved in private CRT overlay

Trace `cli-platform.qEn1nUga` identified the exact transition: stdout starts with
flags=0 and pos=end=-1; after child exit flags were restored to 0 while pos=0,
end=4096 remained. `__close_stdio` restored the saved *entire* FileHandle flags,
discarding the live write-direction and buffer-ownership bits. Flush therefore
treated buffer capacity as unread input and attempted an invalid backward seek.

The AROS `compiler/crt/stdc/__stdio.c` fix restores only LINEBUF/NOBUF policy,
preserving live direction, allocation and ownership state for inherited stdin
and stdout. It does not alter child launching or bypass Flush errors.
`build-stdio-overlay.sh` rebuilds this one object against the frozen nightly
ABI and relinks a private stdcio.library. No installed library is overwritten.
The initial overlay build failed because the default compiler includes selected
POSIX fclose; explicit `-noposixc` selects the correct stdcio runtime headers.
Build logs retained in `cli-stdio.uSdofaHr`, `o1My5uhj`, `xPuSGC6J`.

Discriminating QEMU A/B tests with mandatory Flush assertions:

- Original stdcio: `cli-platform.yI0t0ap8` fails error 219 at first child Flush.
- Patched `cli-stdio.xPuSGC6J/stdcio.library`: `cli-platform.SDD7h4Sp` passes all
  16 launches with full/line/unbuffered policies preserved, successful Flush,
  exact output, exit17, environment isolation, handle reuse and missing-helper
  failure. Runner exits 0. Earlier fixed full-buffer run `2GSKZX7L` also passes.

The CLI runner accepts `AROS_TEST_STDCIO_LIBRARY` and freezes/hashes that private
overlay into its ISO. Without it, the new regression assertions deliberately
fail against the old runtime. The stdin counterpart, concurrent users, native
hardware durability and the full upstream CLI remain outside this test's proof.
Failed test builds `AQviRxGG` and `8GSx1HhB` lacked dos/stdio.h for BUF constants;
that test include was corrected before the final A/B runs.

### Helper stdout collection

`output.c` implements all-or-error text stdout collection; `oz_cli_read_lines`
adapts an already-positioned native DOS handle without taking ownership. It
strips LF delimiters, preserves blank lines and an unterminated final line,
and returns zero lines for empty output. Embedded NUL is rejected (EILSEQ),
over-limit output returns EOVERFLOW, and reader errors return EIO. Allocation
failures free all partial results. Caller supplies the accepted byte limit;
this does not limit how much a child can write into its capture file, nor the
reader's bounded chunk read-ahead. Capture-resource policy remains to implement.

Native evidence `cli-platform.BBBBuZ8m` passes separate helper executables
producing empty output, blank/final lines, a 131072-byte line plus final `tail`,
and embedded NUL. Parent verifies real exit23 and Flush before collection,
compares the entire long line, and checks a 1024-byte rejection leaves no result.
The full previous process/arguments/CPU regressions pass in the same boot,
runner exit 0. Final-source rerun `cli-platform.HwIU2j91` also passes all four
fixtures with exit 0. Host ASan/UBSan test `output-host-test.c` passes chunk sizes 1,
3 and 4096, exact byte limits, empty/blank output and read failure after a
complete line; it verifies no partial result survives failures.

The upstream process entry is still not wired: environment replacement, path
search and capture resource management must be implemented rather than silently
ignoring their contracts. No claim that zpool/zfs binaries now link or run.

### Explicit native helper environments

`oz_cli_spawn_env_exact` accepts a caller-supplied array of NAME=value entries.
NULL retains native inheritance; an empty array starts with no copied local
variables. Parent validates/copies entries (1024-entry/64-KiB bound), rejects
malformed entries and case-insensitive name collisions, then starts a separate
native entry trampoline with NP_CopyVars disabled. The trampoline only calls
native APIs: SetVar(GVF_LOCAL_ONLY), then RunCommand. It never accesses the
parent's C runtime. Target setup failures preserve DOS errors and cannot be
reported as a successful helper launch. Synchronous completion retains the
existing resource/exit-status ownership.

`cli-platform.pDujJ0ND` passes all previous fixtures and 16 alternating explicit
and empty environment launches. The target independently checks required
values, an empty string, literal shell metacharacters, and excluded parent-local
variables using both DOS and C getenv. Each target changes its local value;
parent verifies its original value survives. Actual child exit29 is required.
Malformed and case-conflicting arrays fail before execution. Runner exit 0.

The additional private DOS policy `NP_EnvLocalOnly` suppresses automatic global
fallback for explicitly configured helper processes and is inherited by their
children. The trampoline checks the resulting process flag before executing the
helper: an older DOS without this policy returns ENOTSUP instead of silently
exposing global variables. Explicit native GVF_GLOBAL_ONLY queries remain
available. This is lookup semantics, not a security boundary or filesystem
restriction. Ordinary processes retain existing behavior. Runtime settings live
in ENV:, persistent settings in ENVARC:, copied to ENV: at startup; this policy
does not alter saving or startup copying.

`cli-platform.zbZRuN9P` demonstrated the original global-variable leak. With the
private `cli-environment.fgzmQKMR` DOS overlay, `cli-platform.eBDlXoR1` passes all
five fixtures, including explicit global isolation, normal inherited global
visibility, and nested-helper inheritance. The fixture substitutes ENV: with a
guest RAM directory and restores it; it never writes ENVARC:. The overlay builder
preserves package entry order and verifies that only dos.library changes.
`cli-platform.9aElMnQC` verifies the older-runtime rejection: sixteen explicit
empty-environment launches return ENOTSUP/status -1 and leave the output file
empty. A normal inherited launch of the same helper produces its expected
12 bytes and exit23. All other platform fixtures pass in the same boot.
Select this negative test with `AROS_TEST_ENV_UNSUPPORTED=1` and the old private
DOS overlay; it is not a substitute for the supported-policy positive test.

Names differing only by case cannot be faithfully represented in AROS local
variables and are explicitly rejected, never silently merged. PATH lookup,
capture lifecycle and production upstream wiring remain incomplete; neither
upstream command is yet linked or runnable.

## Runtime acceptance still required

`cli-probe.s6ELLEiU` compiles 16/24 common units after the AROS spare-label
open uses O_RDONLY, matching check_file_generic and the native file import
scanner. Linux O_DIRECT is not defined to zero or advertised as supported.
The native raw-device adapter remains separate. This is compile evidence only:
full CLI spare classification against the resident pool namespace remains to
test once the commands link.

### Upstream version metadata

The probe generates ZFS_META_NAME/VERSION/RELEASE/ALIAS from the checkout's
META file, retaining that input, generator, generated header and hashes. It
rejects duplicate/missing required fields and unsafe version tokens rather than
embedding unvalidated text in a C macro. These are upstream release identifiers;
the separately recorded source HEAD identifies AROS port changes.

`cli-probe.X7sKejwI` no longer reports missing ZFS_META_ALIAS in the real client
sources. `cli-platform.T0fN3ctq` passes all eight fixtures and verifies the native
reported version equals its generated header (`zfs-2.4.4-1`). The generator has
four host unit tests covering release/no-release, invalid input, and the pinned
checkout. Full zfs --version still requires CLI linking and service version
transport. The common-unit count remains 15/24.

Process-title API inspection: DOS SetProgramName updates only the current CLI;
pthread workers lack that CLI context. The available pthread_setname_np uses
fixed storage (32 bytes for workers, current-name length for the main task),
so it is not a drop-in process-title replacement. No raw task-structure mutation
or success-returning title stub was introduced. Native title integration remains
open.

### Native send-progress wait

`progress-wait.c` uses timer.device for periodic one-second updates and native
Ctrl-E for an on-demand report. Ctrl-C wakes deferred pthread cancellation;
cleanup aborts/drains an outstanding request before closing the device and
freeing its request/port. No POSIX signal timer or busy polling is used on AROS.
The upstream send-progress thread now uses this adapter, while other platforms
retain their signal-timer implementation. AROS must signal the progress task
with Ctrl-E rather than Unix SIGUSR1; end-to-end command interaction is not yet
validated.

`cli-platform.YXXu7ovm` passes all eight fixtures. Its timer interval measured
1000142 microseconds, manual updates wake with and without a timer, and all
16 alternating periodic/nonperiodic worker cancellations run cleanup exactly
once. Eight cancellations occurred with timer requests pending. Joined workers
have no remaining request, port or open device. This is component evidence,
not proof of a full upstream send operation against the resident service.

`cli-probe.GJDoJUPZ` confirms sendrecv.c no longer fails on timer_create,
timer_settime, timer_delete or pause. Its remaining compile error is native
process-title support; the common-unit count therefore remains 15/24.

### Native upstream compatibility-profile parser

The AROS pool client reads bounded private contents instead of file-backed
mmap. Native direct path opens replace openat (the inspected CRT implementation
is an unimplemented success-returning stub). Relative profiles retain local
configuration-before-distribution precedence; absolute native/POSIX paths open
directly. Native length checks use seek-to-end/rewind because fstat failed on an
assigned ENV: file that opened successfully. Other platforms retain their
mapping and openat paths. The upstream 16-KiB limit and parser remain intact.

`AROS_TEST_BUILD_COMPAT=1 bash aros/cli/probe-upstream.sh --all` builds an
isolated executable containing the real upstream zpool_load_compat and the
existing upstream engine feature-table object, not a replacement parser. The
feature table is a test dependency: it does not prove resident-service feature
discovery. Pass the resulting executable as `AROS_TEST_COMPAT_BINARY` to the
private platform runner. Distribution fixtures are installed read-only in that
test ISO; local fixtures use a temporary RAM-backed ENV: assignment, restored
after the test. No ENVARC write occurs.

Evidence and failure history:

- `cli-probe.dFz597Cm`: 15/24 common units compile, including libzfs_pool.c;
  the optional compatibility test links. Other failures keep probe exit 1.
- `cli-platform.aqdSlJWs`: explicit stack-limit failure, then stopped through
  its own QEMU monitor. Test now requests a 256-KiB stack and the runner detects
  that diagnostic immediately.
- `cli-platform.MbwkP55Z`: absolute-profile parser tests pass.
- `ffyXtmrV`, `ujrDk27j`, `c21IlcfP`, `5TNZT3E0` (cli-platform prefix):
  relative ENV profile failures retained; final trace proves open succeeds but
  fstat returns -1. The internal fstat failure mechanism is not established.
- `cli-platform.Avw90ZrH`: all seven fixtures pass, including off/legacy,
  16 repeated profile/intersection runs, comments, unchanged source bytes,
  absent/empty/oversize/missing-newline rejection, unknown local feature error,
  ENV precedence, packaged fallback and unknown distribution feature warning.

This proves parser operation, not complete CLI linking, service transport or
pool creation with compatibility profiles. Concurrent truncation and allocation
failure paths have not been fault-injected.

### Native pipe2 runtime and complete common-source compile probe

`cli-probe.Uw6xCghJ` compiles all 24 common CLI/client units, including
`libzfs_diff.c`. This is compilation, not a linked or working full command.
The SPL declaration requires the new native posixc pipe2 vector and its
generated import stub; there is no pipe/fcntl emulation.

`build-pipe-overlay.sh` builds a private runtime from the frozen nightly ABI,
appending one vector and preserving all existing indices. It replaces pipe,
start and end objects in the prior tested exec overlay. The implementation
sets FD_CLOEXEC before publishing descriptors, leaves the caller array intact
on failure, frees allocation failures, and preserves DOS/descriptor errors
through cleanup. O_NONBLOCK explicitly returns ENOTSUP until actual
nonblocking PIPE: I/O exists; unknown flags return EINVAL.

`cli-pipe.XSGxwRgI` plus `cli-platform.0dMUkkcT` passes all existing platform
and upstream subprocess gates, plus 64 alternating pipe2 flag/data/EOF cycles,
legacy pipe flags, invalid flags, unsupported nonblocking and null output.
Set AROS_TEST_PIPE_OVERLAY to the overlay directory as well as selecting its
posixc.library to enable the mandatory pipe gate. The fixture mounts the
packaged PIPE DOSDriver. Concurrent exec inheritance and allocation fault
injection are not yet covered by this test.

Retained failures: `cli-pipe.Iu3Qcyad` expected a deflibdefs wrapper that
genmodule does not generate; the script now supplies LC_LIBDEFS_FILE directly.
`cli-platform.mWwDO7Il` lacked the PIPE: mount and was stopped via its own
monitor. `cli-platform.AV4vqZIR` used mount-list syntax for a DOSDriver file;
the screenshot showed the mount error and the runner timed out. The corrected
invocation passed in `cli-platform.TQXtRnRd` and the final run above.

### Whole-command link inventory and resident engine version

`probe-link.sh COMMON_PROBE INIT_DEPENDENCY_PROBE PIPE_OVERLAY` now links the
whole real zfs and zpool command graphs, not a selected test entry point.
It includes genuine upstream uu_list, list, JSON, timestamp, zio value strings,
thread pool and zlib dependencies, plus the native adapters. Failed links remain
failures; no success-returning OS/share stubs are supplied.

`cli-link.YSIvGWvO` failed before dependency resolution because AROS's relocatable
link requires an explicit root for --gc-sections; `-u main` fixes the probe.
`cli-link.2K1uDqcL` records the initial unresolved inventory; `4tXIMPnD` resolves
the missing common libraries. `cli-link.fLYYdjoo` also resolves
zfs_version_kernel. Both commands still fail full linking: sharing and native
mount/snapshot hooks remain for zfs; zpool additionally needs native device
discovery, labeling, path, CPU/page-size and enclosure/power interfaces.

The version interface is a bounded pointer-free authenticated service query.
The engine embeds metadata generated from its own META at build time; the
client never substitutes its own compiled version. zfs_version_kernel returns
an allocated copy or NULL with the actual query errno. The management build
retains generated metadata and source hashes beside its binaries.

`full-native-version` and `public-service-run.PQ3enDcZ` pass the real version
query/wrapper, bounds, missing-service and public-service storage regressions.
The final `public-service-run.0M7mhYSJ` additionally reruns the upstream init,
failure-cleanup, host-identity and high-level mount fixtures against that new
service. It reports `zfs-2.4.4-1`, passes all required gates, and uses disposable
RAM-backed mirror files only. This is real component integration evidence,
not a claim that full command executables yet run.

### Effective native mount policy and upstream mount_setattr

The AROS mount receives readonly/atime/relatime/exec through upstream DSL
callbacks, not Linux namespace mount flags. The authenticated fixed-size
mount-policy query now reads those effective fields from the active native
mount, reporting an explicit supported mask. Missing service/mount and invalid
requests return errors with empty client outputs.

The native zfs_mount_setattr adapter checks the supported requested policies
against the live query after refreshing the per-handle and libzfs mount-option
caches. It does not detach/recreate DOS endpoints. Unsupported policies fail
with ENOTSUP/EZFS_MOUNTFAILED; mismatched values fail without misreporting a
suspended pool. Devices, setuid, xattr and nbmand mount policy are not claimed
implemented by this adapter. Full upstream `zfs set` transport remains separate
from this verified callback/readback path.

`full-mount-policy`, `cli-probe.iJoLkX7x` and
`public-service-run.FsMFJpEy` pass eight live on/off transitions across four
properties, upstream adapter readback after each transition, unsupported-policy
rejection, missing/absent queries, and effective readonly after readonly
reimport. All existing required init, hostid, mount, authenticated service and
RAM-backed mirror DOS round-trip gates pass. `public-service-run.w4URPfT9`
retains the initial passing run before the extra error/readonly checks.

`cli-link.pznFzcDS` confirms zfs_mount_setattr is resolved in the whole-command
graph. Both commands still fail on the remaining native OS/share dependencies.
The preceding `cli-link.kW8yZVab`/`XznG55k0` attempts exposed overlapping object
globs when one probe supplied both command and init-dependency inputs; the
link probe now selects disjoint libzfs/libzfs_core/library groups.

### Native lifecycle ownership and initialization diagnostics

`lifecycle.c` supplies platform contracts used by the real upstream commands.
Initialization errors name the resident service, native caller or protocol,
not Linux modules, /dev/zfs or /proc. The dataset-free mount UI precheck allows
delegation; actual native attach/detach still performs fresh caller and dataset
permission checks. The optional post-unmount volume hook has no client-side
devfs work on AROS and does not claim a zvol .device implementation.

Snapshot preflight deliberately performs no premature client-side detach.
The real upstream destroy ioctl first checks mount/destroy credentials, then
calls the existing owner-task zfsctl_snapshot_unmount adapter before the real
DSL destruction. `cli-probe.0pGUdVIU` and
`public-service-run.JYF69LXB` verify the complete libzfs path: create and mount
a snapshot, hold its native DOS root lock, fail destruction while preserving
both snapshot and mount, release the lock, destroy successfully, and verify
both are absent. The first passing snapshot run is `DBMCMqtq`. The final run
also gates native diagnostic mapping and all existing required storage tests.
This does not yet cover deferred destruction, concurrent remounts or denial of
a mounted snapshot destroy by a separately authenticated unprivileged caller.

`cli-link.HE52rClc` resolves the native init/delegation/snapshot/volume-hook
symbols. The remaining zfs link symbols are find_shares_object and libshare's
interfaces; zpool retains its additional native device dependencies.
`cli-probe.FS5fa87N` failed compiling zpool_main because shared /tmp filled,
not because of source errors. A private build-aros/cli-tmp directory restored
the complete 24/24 compile in `0pGUdVIU`; no other agent's temporary files were
deleted. All failure logs remain available.

### Shares-directory object discovery

The native shares-object query reads `ZFS_SHARES_DIR` from the mounted
dataset's upstream master ZAP. A missing entry means object zero; unavailable
mounts and service failures remain errors. It does not assume imported pools
lack shares metadata. The upstream `find_shares_object` helper uses this
authenticated service query and reports failures through libzfs.

`full-shares-object`, `cli-probe.ysABW81U` and
`public-service-run.pBxpyYeb` pass the native and upstream absent-object checks,
unmounted/missing-service/invalid-input checks, and all required public storage
gates. A nonzero shares object on an imported pool still needs a runtime test.
This helper does not establish full `zfs diff` transport support.
`cli-link.5RX8LheL` confirms the helper resolves; zfs still lacks the libshare
backend interfaces and zpool retains native device dependencies. Both complete
command links therefore remain failing, not completed binaries.

### Shared native file-label discovery

`import-files.c` is shared by the resident engine and upstream command link;
it does not link SPA/DMU into the CLI. It reads explicit regular-file candidates
with the actual upstream label parser, rechecks type/size on the opened handle,
and honors an expected vdev GUID. Label contents never authorize additional
file opens. Automatic native-device discovery remains explicitly unsupported.
Linux udev identity cleanup is limited to file vdevs; AROS has no sysfs path.

`cli-probe.U2MruoeV`, `full-import-files-final` and
`public-service-run.b0hlpOGq` pass the exported-mirror CLI label test: two
members, matching pool identity, distinct vdev identities, expected-GUID match
and mismatch, directory rejection, and missing-file rejection. The rebuilt
service also passes all required public storage and readonly-reimport gates.
This is not yet a complete `zpool import` invocation or native disk discovery.

The first compile lacked the test's AVL header (`import-files-probe.log`);
the next client link exposed the genuine upstream page-size dependency
(`import-files-client.log`). Both logs are retained. The final build includes
upstream `lib/libspl/page.c`, not a fixed page-size substitute.
`cli-link.KNgdotqY` removes file-discovery and page-size undefined symbols;
full commands still fail on the remaining share/device/platform interfaces.

### Upstream property request transport

The native dispatcher now admits real `ZFS_IOC_SET_PROP` and
`ZFS_IOC_INHERIT_PROP` requests. The fixed inline property name and source flag
are validated, source/config buffer shapes are checked, and the existing
private-copy transport scopes every operation to the freshly authenticated
caller. Actual upstream code owns property type/value checks, delegation,
transactions, received/local/inherited state and mount callbacks.

`full-property-ioctl`, `cli-probe.qhhpd9XC` and
`public-service-run.I0NGcEKj` pass four set/inherit cycles with eight fresh
libzfs reads, separate unprivileged-session rejection for set and inherit,
and unchanged state after denial. Received atime remains masked by a local
value until inherit-to-received; invalid values and source flags are rejected.
The initial passing local/inherited run is `public-service-run.w19jotMl`.
Both runs retain all existing required service/storage gates. These are actual
upstream ioctl and readback tests, not complete command binaries. Broader
property-specific side effects, delegated-success cases, and full high-level
`zfs set`/`inherit` command execution remain to be verified.

### Native file-vdev reuse checks and pool query failures

`file-vdev.c` supplies the real zpool `check_file` entry through upstream
`check_file_generic`/`zpool_in_use`, accepting only explicitly named regular
files. The native transport also admits upstream `POOL_GET_PROPS`, including
the actual readonly state needed when exported labels belong to a currently
readonly-imported pool. This is read-only checking, not file overwrite or
native disk/partition support.

`full-file-vdev`, `cli-probe.RnS35ulS` and
`public-service-run.iTvX3laC` pass checks for both mirror members while active,
exported and readonly-imported. Force permits exported reuse but not active
or readonly-imported reuse. Missing files, directories and failed native
control queries are rejected. Real pool size and readonly properties are read
through libzfs and the resident dispatcher. All other required service/storage
gates also pass. Initial ordinary-state evidence is `g7aBgs3n`.

The fault-injection run `f84RuMkU` failed: a negative descriptor reached the
wire as EINVAL and upstream refresh treated that as a missing pool. The native
control client now returns EBADF for nonpositive descriptors; native pool-stat
refresh propagates non-ENOENT errors. Native in-use checks preserve query
failures, including exported-label and subsequent-config/property failures,
rather than allowing force based on unknown state. The test changes only its
own client descriptor field and restores it before cleanup. No candidate
backing-file mutation is performed by these checks.

`build-init-test.sh` now compiles the current native control bridge instead of
silently using the older cached client object. `cli-link.x4wRnQoI` confirms
check_file resolves in the full command graph; the remaining share/native
device/platform link dependencies are still incomplete. Spare/cache-device
query-failure variants and concurrent export/removal races remain unverified.

### Native CPU topology without an embedded engine

`aros/platform/cpu.c` provides the processor.resource query shared by the
resident engine and commands. The old engine-local definition is removed;
both engine build paths include the shared unit. It preserves the single-CPU
fallback if the resource supplies no usable count, rather than reading Linux
host topology. The CLI does not embed libzpool to obtain this platform helper.

`full-native-cpu` and `cli-probe.0T8PTsRj` pass the complete public-service
suite in isolated one-CPU (`public-service-run.Hqra2Sfy`) and four-CPU
(`public-service-run.laVmpHXm`) QEMU guests. Each client makes 128 stable
queries; the runner checks the reported count against its actual QEMU `-smp`
argument and records that argument in `cpus.txt`. All existing required
storage/property/file-reuse gates pass in both configurations. Resource-loss
fallback and physical CPU hotplug are not claimed tested.

`cli-link.0VyQRZFT` resolves aros_zfs_ncpus in the real zpool graph; complete
commands still fail on the remaining share/device/platform implementations.

## Native device path semantics

`device-path.c` implements the upstream `zfs_strip_path`,
`zfs_strip_partition` and `zfs_append_partition` interfaces for the native
`/AROS/<name>.device/<unit>[/partition/<position>]` namespace. Parsing is
lexical only: it never opens a device, verifies identity, or grants admission.
The existing production raw-device restrictions remain in force. The suffix
uses the native root-table position zero, not a guessed Linux `p1` name;
formatting a path does not create or locate a partition.

The parser validates the 255-byte device-name bound, canonical decimal unit
and partition fields, signed native unit limit, and reserved position sentinel.
Malformed names and numbers fail without changing caller output. Append checks
the entire supplied capacity and preserves the buffer on failure. Stripping
only recognizes valid native device paths: ordinary file names ending in digits
or `-part1`, native volume paths and Unix paths are unchanged.

Diskless native AROS run `device-path-run.Uyk6tYHF` passed grammar, integer
overflow, every append-buffer boundary, maximum/overlong device names, repeated
append rejection and unchanged-output tests. The same captured sources passed
host ASan/UBSan. The earlier strict build `device-path-run.VsnfOt7H` rejected an
unbounded test-string copy; the harness now explicitly checks its length.

`cli-link.ZpBs4Z2y` resolves all three functions in the real command graph.
Both whole commands still fail linking on real outstanding native/share
dependencies. No success-returning device or sharing stubs were added.

### Native device readiness waits

`device-wait.c` implements the upstream disk-wait interfaces using actual
read-only resolution, a checked monotonic clock, and the upstream 50 ms settle
interval. Missing devices are retried until the deadline; authorization,
malformed-path and other non-retryable failures are returned immediately.
Native Ctrl-C cancels the wait. This neither labels devices nor establishes
persistent device identity. A blocked service/device query itself is not
preempted by the polling deadline.

Probe `cli-probe.ALL6Ejs1` and strict INIT/CONTROL/PARTITIONS QEMU run
`public-service-run.qi7A2X6n` passed. The tests cover four existing device/file
paths, measured settling and timeout, default wait, malformed arguments,
blank partition failure, native Ctrl-C consumption and reader denial. Existing
service, ioctl and DOS mirror storage checks also passed. `cli-link.ozcUhIsZ`
resolves both wait symbols; the full commands still fail to link on remaining
sharing and native-device operations. Runtime evidence is not a claim that
the complete upstream CLI is available.

Use isolated hosted/QEMU guests and disposable disk files only. Exercise real
commands for pool creation/status/list/export/import, dataset creation/list,
properties, snapshots, and DOS/Wanderer access to mounted filesystems. Verify
CLI-to-GUI and GUI-to-CLI visibility against the same resident service, denied
non-root operations, delegation, absent-service failures, repeat invocations,
and cold-reboot persistence. Retain complete commands, output, exit codes and
artifact hashes. No DUT or existing user pool is part of these tests.

### Native pool GUID policy transition (2026-09-26)

Authenticated manager `pool.reguid` now reserves both old and proposed GUIDs
in the existing vdev path policy while upstream changes the pool identity.
Success retains only the new GUID; failure rolls back to the old GUID. Named
creation/split policies remain name-bound. No path restrictions or backing
device reservations are released during this transition.

Evidence under `build-aros/`:

- `native-import-run.58b9dlrB`: retained failed first attempt. Moving random
  GUID generation out of `spa_change_guid` initially missed its namespace-lock
  requirement; the guest failed in `spa_by_guid`. Fixed before retrying.
- `native-import-run.EcN9meW2`: full native CLI regression suite plus manager
  reguid/reopen, writable DOS payload, export/read-only reimport and reads pass.
- `admission-service-run.Eit17UiC`: final policy tests execute inside AROS;
  both identities reject unlisted paths during transition, duplicate and
  overlapping reservations fail, rollback preserves the old restriction and
  success transfers it. Existing authenticated file-pool creation suite passes.
- `native-import-run.358H3uVA`: final service rebuilt and fresh-boot verified;
  successful reguid/reopen, health, DOS write/export/reimport/read and read-only
  reguid rejection (`EROFS`). Runner exit zero and original raw disk hashes
  unchanged. This is clean export/reimport evidence, not power-loss durability.

All operations used private QEMU overlays or disposable guest RAM files.

### Upstream CLI GUID changes

`POOL_REGUID` now accepts the pool name and optional packed upstream GUID
property. The real dispatcher keeps pool-modify authorization, readonly and
suspended checks and upstream property decoding. Its AROS hook and manager
operation share `oz_pool_change_guid`, retaining the same transition policy;
the CLI does not implement a separate identity change.

`full-cli-reguid` and untraced `cli-link.owgVv1U6` pass the complete lifecycle
suite in `native-import-run.Exi6xtBQ`. Tightened repeat
`native-import-run.jcvHQ9G3` also queries the explicit GUID after export and
read-only reimport from the surviving member. Both runners exit zero and the
original native fixture hashes remain unchanged. Tests exercise generated and
explicit GUIDs, duplicate/zero rejection, checkpoint conflict, readonly and
non-root refusal, reopen/health, subsequent native DOS data access, and retained
GUID 987654321 after reimport. Native AROS preserves this upstream command's
failure return of -1; tests do not assume shell status 1. This proves clean
export/reimport, not crash durability or physical device behavior.

### Refreshed whole-command link: native topology hooks

`cli-link.4MPPaU4y` refreshed the native adapters and reduced the older link
report to `is_mpath_whole_disk`, `update_vdev_config_dev_sysfs_path`, and the
two sharing provider tables. The first two now follow upstream FreeBSD's
non-Linux behavior: no device-mapper whole disks, and no mutation of vdev
metadata to invent sysfs topology. Native device admission and partition
identity checks remain unchanged. These hooks do not add multipath support.

`cli-probe.cF2ZsFY8` rebuilt all 25 common upstream command/client source units
successfully. `cli-link.4fFghh5T` links those fresh units and rebuilt native
adapters, retaining dependency objects from `cli-probe.KAFE4hma`. Both full
commands still fail: only `libshare_nfs_type` and `libshare_smb_type` remain
undefined. The zpool link also reports an `.eh_frame` header-generation
diagnostic; that must be revisited after resolving the provider dependency.
No runnable whole-command binary or CLI runtime success is claimed. This
is a compile/link result, not a new guest runtime test. Logs, source hashes,
input hashes and linker maps are retained in those evidence directories.

### Whole upstream commands: executable startup and output verification

Network export integration remains incomplete. `share-unavailable.c` explicitly
models upstream FreeBSD's unsupported SMB-provider policy for both protocols:
enable/disable/validate/commit return `SA_NOT_SUPPORTED` with `ENOTSUP`; status
does not claim an active export. It is an absent-capability implementation, NOT
a working sharing backend. It creates no export config or daemon state. This
allows the actual pool/dataset command binaries to be tested independently of
the remaining server integration. Do not mark sharing or the full port complete.

Retained failures and discriminating evidence:

- `cli-link.mWIbooJi` linked both commands, but `whole-cli-run.z33KcX83`
  failed at empty help output. Final-link `--gc-sections -u main` had removed
  AROS startup sections. It is removed; section selection now applies only to
  the intermediate upstream `check_file_generic` helper needed by the native
  preflight adapters, never to the final executable startup/constructor tables.
- `cli-link.1V0vXLbS` exposed that helper dependency for zfs; supplying the
  real upstream function closes it. `V2fvuYnA` links both and has no earlier
  `.eh_frame` diagnostic, but `whole-cli-run.jkMeKrGE` still loses output.
- POSIX `-nix` linkage (`cli-link.GHfYCM6D`) alone did not cure the empty
  output (`whole-cli-run.WX5mmA1W`). Diagnostic-only `startup-trace.c` verified
  entry into the real main with correct argv (`whole-cli-run.mdQeUL86`). Its
  optional exit-flush probe recovered help text (`whole-cli-run.7z3FuqqN`),
  while the normal-return list error remained missing. No traced binary is
  used as the final passing runtime evidence.
- Inspection of frozen POSIX teardown found descriptor closure without a
  stream flush pass. Inherited DOS handles deliberately avoid Close(), so
  buffered output is lost. `posix-stream-end.c` supplies an additive private
  CLOSELIB priority-3 hook, before priority-2 descriptor teardown. It flushes
  every writable stream, continues after an error, and neither closes borrowed
  handles nor substitutes application exit statuses. Explicit fflush/fsync is
  still required when the application must report an output error.

Build that private overlay with `build-posix-stream-overlay.sh <posixc.map>`.
The first artifact is `cli-stdio.xGeMcDd3/posixc.library` (created before the
builder was given its distinct name; the existing stdcio builder is unchanged).
`whole-cli-run.jaD5M2dl` passed guest checks but the runner incorrectly expected
unprefixed debug markers, so was stopped with exit 1; that evidence is retained.
The corrected runner accepts the actual debug prefix and checks exact markers.

`whole-cli-run.2d4dKa1J` exits zero using untraced `cli-link.GHfYCM6D` commands
and the private POSIX overlay: both real commands run help, version and list
twice (12 invocations). Help exits 0 with real usage output; version exits 1
with client version plus unavailable-engine error; list exits 1 and reports
`The resident OpenZFS service is not available.` Both absent sharing providers
pass enable/disable/validate/status/commit error checks. This is a diskless,
networkless QEMU guest with no service, not yet create/import/dataset execution
against the resident engine. No shared AROS library, DUT or user pool changed.

### Real CLI against the full service, including GUI API and DOS visibility

`service-command-test.c` invokes the actual linked upstream binaries with exact
argv, captures their output/exit status, and runs against a disposable pool
imported by the authenticated native test. Enable it in the native import
runner with `AROS_TEST_WHOLE_CLI_LINK=<successful link directory>` in writable,
non-GUI mode. No command targets a host path or existing user pool.

The first runner attempt (`native-import-run.ZLVoyPQH`) stopped before boot:
its result check was mistakenly placed before the serial log existed. That
ordering is fixed. The first guest (`6YTHOQjW`) listed the pool and created the
dataset, but `zfs get normalization` returned `-`. The native ioctl transport
omitted `ZFS_IOC_OBJSET_ZPLPROPS`, so upstream libzfs could not retrieve actual
ZPL properties. `ioctl-request.c` now admits that read-only query through the
same authenticated dispatcher, bounded destination buffer and no-source/
no-config validation as existing read-only queries; it does not synthesize
values or bypass upstream policy.

Full service `full-cli-zplquery/OpenZFSServiceNative` fixes the failure.
`native-import-run.r1v16VCc` exits zero: real zpool/zfs list, zfs create with
NFC/UTF8/case-insensitive properties, normalization get, user-property set/get,
snapshot creation/listing, snapshot and dataset destruction, and repeat pool
list. The unchanged native DOS write/export/reimport suite also passes.

`native-import-run.UjRt4tvY` additionally checks that the GUI's actual operation
40 API sees the CLI-created user property. It attaches the CLI-created dataset
through the authenticated mount API, writes `caf\xc3\xa9` through DOS, flushes
and closes it, then reads the exact eight bytes through the case-insensitive
decomposed alias `CAFE\xcc\x81`. It detaches before CLI snapshot/destruction.
All CLI commands, API/DOS integration and the original persistence suite pass;
the two original raw disk hashes remain unchanged. The run uses untraced
`cli-link.GHfYCM6D` binaries and `cli-stdio.xGeMcDd3` private POSIX runtime.

This proves shared backend visibility, not a visual GUI interaction in this
run. It also does NOT prove `zfs mount`: the test uses `mountpoint=none` and the
existing mount API. Source inspection identifies a remaining mismatch between
the Unix-style mountpoint validator and the native adapter's `VOLUME:` format.
CLI pool create/import/export, mountpoint handling, non-root/delegation and
retained CLI-created data across a cold boot still need their own coverage.

### Native volume-root CLI mount/unmount

`native-import-run.BySlBtMV` reproduces the validator mismatch with real
`zfs create -u -o mountpoint=OZCLI:`: status 1, absolute-path error. The shared
namecheck now accepts native AROS volume roots using the handler's existing
1-31 ASCII letter/digit/underscore/hyphen name contract plus a trailing colon.
This branch is AROS-only. Existing Unix absolute-path validation is unchanged
for portable imported properties; accepting such a property is not a promise
that the native handler can mount a Unix directory. Empty native names and
native subdirectory mountpoints remain rejected. The CLI error explains the
native format rather than claiming all mountpoints need a leading slash.

Rebuilt common CLI units/dependencies (`cli-probe.RCMe0xQF`), whole commands
(`cli-link.fUwF9HJh`), the engine namecheck object, and full service
(`full-cli-native-mount`). `native-import-run.PSl4Arz0` passes the real CLI
mount/unmount sequence and previous API/DOS/persistence checks. The two negative
create cases (`:` and `OZCLI:child`) fail without creating `cli-invalid`.

`native-import-run.IoKxiQLc` additionally verifies read-only mounting and exact
held-handle behavior. CLI create uses `-u`; CLI mount publishes `OZCLI:`;
`zfs get mounted` reports yes. A DOS handle remains readable after the CLI's
busy-unmount refusal. Closing it permits unmount, `mounted` becomes no, and
another mount/unmount cycle succeeds. `zfs mount -o ro` rejects DOS creation
with a write-protection error and can then be unmounted. The GUI property API,
Unicode filename readback, snapshot/destroy operations and original pool
export/reimport suite pass. Runner exits 0 and original raw image hashes pass.
The final test-code delta after this run only corrects diagnostic LONG-to-long
format arguments; command behavior and assertions are unchanged.

This covers explicit native volume-root mountpoints, not inherited child
directory mounts, Unix directory mounts, altroot remapping or physical media.
Sharing remains unsupported; libshare's unconditional commit callbacks can
still emit unavailable-provider diagnostics during otherwise successful mounts.

### Private QEMU worker-completion diagnostic

`aros/engine/qemu-worker-trace.patch` instruments QEMU 10.2.0-rc4's thread
pool with monotonic nanosecond timestamps at worker start, worker return,
completion notification and callback delivery. The existing
`thread_pool_complete_aio` event runs at callback delivery, not at host
syscall return; it cannot alone distinguish slow disk I/O from delayed
event-loop delivery. Notification and delivery can race, so their textual
order is not a synchronization guarantee. Match the request pointer within
its submission lifetime (addresses can be reused).

Build and select a private QEMU copy, never replace the installed binary.
Run `test-qemu-public-service.sh` with `AROS_TEST_WORKER_TRACE=1`,
`AROS_TEST_DISK_AIO=threads`, and
`AROS_TEST_QEMU` pointing to that binary. The runner verifies the four
events exist and retains the patch and event-list hashes alongside the
QEMU binary hash. Do not enable strace for the initial discriminator.
Set `AROS_TEST_IDE_TRACE=1` to include the full IDE/AIO event stream;
leave it at zero for worker-only tracing to reduce instrumentation traffic.
These trace points still affect timing; a passing run is not a DMA fix.

Private build `build-aros/qemu-worker.Svcpa0sX` configured with system
Python (`--python=/usr/bin/python3`), KVM, log tracing and optional features
disabled, then built `qemu-system-x86_64` with `ninja -j8`. Development
packages were extracted under its `deps/root`, not installed globally;
package hashes and failed/successful configure logs are retained there.
The current distribution places GLib headers in `libgio-2.0-dev`, while
`libglib2.0-dev` is a metapackage. The earlier failed package request used
`libselinux1-dev`; the available development package is `libselinux-dev`.

First worker-instrumented four-CPU run `public-service-run.Qa4dZg2n`
completed with exit 0, both native read phases and the full storage marker.
It did not reproduce the outstanding ATA timeout; no failure-stage or
stability claim follows from this passing trace.

Repeat `public-service-run.GgBxYm0O` also exited 0. Reduced worker-only
run `public-service-run.q0gNmGzI` likewise passed the strict storage gates.
Neither reproduced the timeout. Untraced control `public-service-run.KDdjwbTU`
also exited 0, but source inspection found that timestamp arguments were
evaluated even with trace output disabled. That is not a clean timing
baseline. The patch now guards clock reads with each event's enabled state;
rebuild and repeat the control before comparing enabled and disabled traces.

## Native device predicates and underlying paths

`native-device.c` implements `zfs_dev_is_whole_disk` by parsing the native
device/unit namespace and querying actual direct-access geometry through the
authenticated resident service. Partition paths and ordinary files are not
whole disks. Missing devices, malformed paths and denied queries return false
with the actual error; valid-looking names alone are not accepted as devices.

`zfs_get_underlying_path` verifies native devices through the same service and
checks real catalog positions before returning a partition's whole-device
path. Missing entries are rejected, including a partition request on a blank
disk. This is a read-only topology query, not persistent physical identity or
permission to label, import or write a raw device.

Explicit ordinary-file paths use native DOS Lock/NameFromLock, accepting both
`volume:file` and `/volume/file` input spellings and returning the actual native
canonical name. They neither strip digit suffixes nor change the current
directory. Relative file names now also use native DOS resolution, translating
POSIX dot and parent components without changing the current directory.
Intermediate components must actually exist and be directories; they are not
lexically removed. A leading colon retains native current-volume semantics.

Failed runs are retained: `public-service-run.GOkQhOhK` failed the initial
underlying-path check, and `8Eft1Ra7` pinpointed CRT realpath returning ENOENT
for an existing `RAM:OZPublicPool/disk0`. `sIYaXhhB` showed that spelling it as
`/RAM/OZPublicPool/disk0` did not fix the CRT walker. Native DOS resolution
replaced that failed approach rather than masking the missing-file error.

Final probe `cli-probe.VlTYfD8i` and native run `public-service-run.Nd9n0KfT`
passed strict INIT, CONTROL and PARTITIONS regression with the whole-device
and underlying-path helpers linked into the real upstream-client fixture.
The same task tested root, reader denial and restored root, with sixteen
whole-device iterations per phase. Both existing partition positions, an
absent position, a blank disk, missing device/file and both explicit file-path
spellings were checked. DOS SameLock proved that the canonical file name
references the original object, not merely a same-sized file.

`cli-link.r66CYIuF` resolves both added symbols in the actual whole-command
graph. Full zfs/zpool links still fail on remaining sharing and native-device
implementations. No raw-device write gate was relaxed and no success stubs
were introduced.

Relative-path regression: `public-service-run.3gXPX3qa` proved the old CRT
fallback returned ENOENT for an existing `disk0` in the current directory.
`3snCSNpL` passed the first lock-based implementation, but expanded boundary
run `6VaG29z4` found it accepted `disk0/` as a regular file. Explicit DOS
directory-component checks fixed that error. Final probe `cli-probe.s4xDFsxU`
and strict INIT/CONTROL/PARTITIONS run `public-service-run.2jj08pLw` passed
five equivalent path spellings (DOS SameLock identity), three missing/empty
paths, three file-as-directory failures and unchanged cwd on success/failure.
The checks repeated after restoring root credentials, and the complete
service/storage regression passed. These tests do not establish persistent
physical disk identity or concurrent namespace-mutation safety.

### Upstream label parsing over native device reads

`zpool_read_label_from` factors the existing upstream synchronous parser around
an exact-read callback. File-descriptor reads still use the same parser; the
native client adapter now supplies read-only bytes from the authenticated
resident service. Whole devices and explicit catalog positions use their actual
geometry/extents. Failed or denied native reads discard partial configurations
instead of reporting a blank device. This does not grant write/import admission
or persistent physical identity across independent read requests.

Client builds enable `OZ_NATIVE_LABEL_CLIENT`: `zpool_open_func` recognizes
explicit native candidates, parses their real labels, applies the requested
vdev GUID filter and records denied-access state. Engine builds intentionally
do not enable this RPC transport: calling the resident service's own admission
port while its owner waits for import workers could deadlock. General path
scanning is covered by the explicit native scanner integration below;
production raw-device import/write admission is not established by these
read-only candidate tests.

Build `full-native-label-reader`, probe `cli-probe.qQT2UGIh` and strict QEMU
runs `public-service-run.y9X5neq2` (2 CPUs, copied GPT mirror member 0) and
`public-service-run.af1ArioJ` (4 CPUs, copied member 1) passed. Each read all
four real labels four times before and after a reader-denial phase, checked
pool/vdev identity, exercised the candidate hook and rejected a mismatched
vdev GUID. Whole-GPT versus partition-relative reads, blank candidates,
absent/malformed paths and the existing file/service/storage regressions also
passed. The initial test compile failed on a missing sys/avl.h include; the
failure remains in `native-label-probe.log`.

Independent Linux `zdb -l` logs `native-label-linux-reference.log` and
`native-label-linux-reference1.log` match AROS's pool GUID 9793164340232638479
and member GUIDs 5737059013304939129 / 4682198142238310590. Both QEMU runs
recorded zero write bytes/operations for the copied label device, and original
fixture and private-copy hashes remained unchanged. No pool was imported from
these candidates and no label-directed extra path was opened. The runner only
accepts existing disposable QEMU pool fixtures, copies them into a new run,
attaches that copy with a private COW layer, and requires the serial markers.

Whole-command link inventory `cli-link.dRWGQEsZ` resolves the new label-reader
dependencies but still fails on the existing sharing/native-operation gaps.
There is still no claim of a runnable complete upstream zfs/zpool pair.

### Explicit native upstream import scanner

The client scanner now accepts canonical `/AROS/<device>/<unit>` and partition
paths without sending them through DOS stat/realpath. It feeds the existing
upstream candidate cache, thread pool, label reader and configuration assembler;
duplicates are deduplicated and malformed native names fail before dispatch.
The integration fixture uses `can_be_active` for read-only metadata preview,
as zdb does. Normal import still requires the unfinished native ownership and
active-pool checks; this is not proof of safe writable import or automatic
device enumeration. The resident engine does not enable the client RPC hook.

Probe `cli-probe.0UwiKzOb` and QEMU runs `public-service-run.VqvuWLFD`
(2 CPUs, member 0) and `public-service-run.OWqB0FbR` (4 CPUs, member 1) passed
the strict init/control/partition suite. Actual worker discovery assembled
`qemu_mirror`, preserved its pool/member GUIDs, and rewrote the discovered
member path to `/AROS/ata.device/3/partition/0`. Root -> denied reader -> root
phases passed, as did duplicate-candidate and malformed-path assertions.
The runner requires two root scanner markers and one denied scanner marker.
Copied label disks had zero write bytes/operations, with fixture/copy hashes
unchanged; the existing RAM mirror DOS round trip also passed.

Preserved failures `public-service-run.koKq5thL`, `6U6WzvMa` and `Yy2dwo4X`
were caused by the new fixture incorrectly calling unique-name nvlist lookup
on the upstream import result, which deliberately permits duplicate pool
names. The worker already returned the pool. Following upstream zpool import,
the test now iterates pairs and verifies the pool GUID and mapped vdev path;
no production authorization or path validation was relaxed. The first scanner
client link also exposed missing genuine zutil path helpers, now linked from
upstream `zutil_device_path.c`, not replacements. Whole-command link inventory
`cli-link.hhZl2E33` still fails on sharing and native-operation dependencies.

The subsequent compile probe includes upstream `lib/libshare/libshare.c`, and
the full command link consumes that actual common dispatcher. All 25 common
units compiled in `cli-probe.kuu5PGgB`. Link inventory `cli-link.ZOK37jJW`
resolves the common `sa_*` entry points and exposes the two missing native
provider tables, `libshare_nfs_type` and `libshare_smb_type`, instead. This is
compile/link dependency progress only: neither command links yet, and no
sharing runtime capability is claimed. Native provider integration remains
required; AROS smbfs is an SMB client, not an export server. No fake provider
tables or success-returning sharing operations were introduced.

### Native label pool-use policy

`zpool_in_use_label` factors the existing upstream pool-state policy from the
file-descriptor entry point and consumes a validated label. The native
`oz_native_pool_in_use` client obtains that label with authenticated read-only
RPC and invokes the same policy, including the resident active-pool query.
This is only a ZFS-use query: a blank ZFS label is not proof of an unused disk,
and this helper does not grant overwrite admission or exclusive ownership.

Probe `cli-probe.XKhazmEI` and strict runs `public-service-run.MXM490Ih`
(2 CPUs/member 0) and `public-service-run.iQyqs0oK` (4 CPUs/member 1) passed.
Both root phases recognized the exported native pool, rejected a deliberately
broken resident query, and distinguished blank and absent devices. Existing
active/exported/readonly file-vdev force-policy checks and service/storage
tests also passed. Label disk writes remained zero and fixture hashes stayed
unchanged. `public-service-run.mbnMojUp` failed before guest startup because
the invocation misspelled the service filename; no guest ran in that attempt.
Live native active-pool policy coverage remains to be added.

The next auxiliary-state regression reproduced an actual fail-open query:
`public-service-run.doV1NGrc` returned success/not-in-use for an in-memory
spare label while the libzfs service descriptor was invalid (errno EBADF).
Both AROS spare and cache branches now propagate a negative namespace-iterator
result rather than interpreting it as no match. `cli-probe.RTJPdx9U` and the
strict 4-CPU/member-1 run `public-service-run.bY748KeZ` passed both auxiliary
failure checks in both root phases, plus the existing complete storage suite.
The auxiliary states were changed only on an in-memory copy of a real label;
no spare/cache pool was created and no on-disk label was changed. Native disk
write statistics and fixture hashes are still gated by the runner. This does
not claim live native spare/cache lifecycle coverage. Whole-command inventory
`cli-link.bBTgusZY` still fails on the previously identified native providers
and device operations, not the factored pool-use helper.

### Native optional lifecycle and enclosure hooks

`zpool_disable_datasets_os` is an optional post-unmount hook, with no extra
native client-side devfs teardown; Linux and FreeBSD implement the same hook
as a no-op. Actual credential-checked DOS unmounting remains in the common
path and resident handler. Enclosure power queries return -1/ENOTSUP and
power changes return ENOTSUP: no native slot controller is implemented, and
device visibility is not treated as evidence of a power state.

Probe `cli-probe.lPG5HxFx` compiles, and `cli-link.VEmgdsZQ` resolves those
three hooks while still failing on sharing providers and remaining device
operations. Strict 4-CPU run `public-service-run.Rambfol4` passed the entire
suite, including explicit unsupported query/on/off checks on actual writable
and readonly pool handles. No enclosure control support is claimed.

The preceding identical-binary run `public-service-run.Y65G6Uco` failed before
the enclosure tests. ATA unit 1 logged a device-completion timeout during the
restored-root native label policy phase; the failed read was not accepted as
an unused device. The repeat passed but this intermittent ATA timeout remains
unresolved. Preserve both runs; do not describe this as unconditional storage
stability or discard the failed run because a repeat succeeded.

The bounded alternating-read reproducer (`cli-probe.fRBe43f8`, strict 4-CPU
`public-service-run.wZsTf2sa`) subsequently failed at cycle 7 reading the blank
`ata.device` unit 1, returning EIO after the ATA completion timeout. It stops
on the first failure; it never retries a failed operation. There are 32
labeled/blank cycles per root phase, with real-label GUID checks and required
completion markers. This is currently a failing regression test, not a fixed
ATA bug or a passing stability claim. Phase-specific policy diagnostics now
record returned errors rather than relying on potentially stale errno.

The public-service runner accepts explicit `AROS_TEST_QEMU` and records the
emulator version and executable hash per run. `AROS_TEST_IDE_TRACE=1` records
selected IDE command/DMA and memory-mapping events in `ide-trace.log`; the
exact event list is copied and hashed. Neither option changes the system
emulator, guest retry behavior, or the test's success requirements.

The default PATH currently selected `/usr/local/bin/qemu-system-x86_64`
10.1.94 (10.2 release candidate). Trace runs `56y9rBPg` passed but `VQM5witT`
failed at cycle 4 and `BT73MzaS` at cycle 0. The latter trace reaches the exact
DMA read and maps both buffers; completion appears only after timeout cleanup
stops DMA. No map-wait event occurred. This motivates emulator/AIO comparison,
not a conclusion that guest or hardware is fixed. Explicit system QEMU
10.2.1 `/usr/bin/qemu-system-x86_64` passed the same PCI-diagnostic guest in
`public-service-run.fVd87znH`, with 64 alternating cycles and the full suite.
The subsequent untraced 10.2.1 run `public-service-run.dtL0rkKk` failed in
the restored-root phase at cycle 31 with the same ATA58/BMDMA01 state. An
emulator version upgrade alone is therefore not a fix. The default has not
been changed. Host I/O backend versus guest behavior remains unresolved.

`AROS_TEST_DISK_AIO=default|threads|io_uring` selects an optional host AIO
backend for the disposable data drives only and records it in `disk-aio.txt`.
The default remains unchanged; CD boot media and snapshot protection are
unchanged. Native/direct I/O is deliberately not included because its cache
requirements would introduce another variable.

The explicit 10.2.1/io_uring run `public-service-run.QMMMuqYo` reproduced
ATA unit 0 timeouts during partition probing, before the OpenZFS test client:
command c8, LBA 0, one sector/512 bytes, ATA status 58, BMDMA command 09/status
01, one valid 512-byte EOT descriptor. Thus failure is not confined to the
client's 114688-byte label reads. The run subsequently terminated with exit 1,
`Public partition RPC failure=176` and `RESULT public service storage FAIL`.
Switching to io_uring is not a demonstrated fix. Retain its complete serial
and IDE traces. Optional IDE tracing now also records host file submission,
thread-pool/io_uring completion and coroutine scheduling to localize the
completion boundary rather than inferring it from guest IRQ state alone.

Explicit `threads` run `public-service-run.5zYwkS0l` also terminated with exit
1 (`Public partition RPC failure=187`). The failed unit 1 read was again
LBA 32/114688 bytes. Host thread-pool submission was traced, but its completion
callback appeared only after guest timeout cleanup stopped DMA. This localizes
the delay before the IDE completion callback, not just in guest IRQ handling.
The thread-pool completion event is emitted by the completion bottom half,
not by the worker immediately after the host read syscall; it therefore does
not yet distinguish slow host I/O from delayed completion dispatch. Neither
explicit backend is a fix, and no production driver change follows from this
comparison. A focused host-read syscall timing capture is the next discriminator.

`AROS_TEST_HOST_READ_TRACE=1` optionally runs QEMU under strace, preserving
the runner's QEMU child PID with `-D`. It records timed pread64/preadv/preadv2
calls in `host-reads.log`, with raw pointer arguments rather than buffer
contents. Seccomp filtering limits tracing overhead to selected calls; the
strace version is retained. This diagnostic is not a performance benchmark.
Initial run `public-service-run.pOHDdq6V` without seccomp filtering failed
early with management operation 1/OZ_TIMEOUT during service initialization,
before exercising the storage reproducer. Preserve it as a diagnostic timing
failure, not evidence resolving the ATA fault. Neither service timeouts nor
test pass requirements were relaxed.

The seccomp-filtered old-service run `public-service-run.G8qqkxzl` also hit
operation 1/OZ_TIMEOUT during initialization. Its RESULT text was split by
concurrent serial output, so the runner eventually reached its overall
deadline rather than recognizing the failure immediately. A retained QEMU
screenshot showed an idle shell; a manually launched client subsequently
passed one lifecycle after initialization. This manual result does not make
the automated run pass and did not exercise the storage phase.

The service previously published its request port before `oz_engine_start`.
It now reserves `<port>.Starting` atomically with the singleton check and
publishes the request port only after engine/admission initialization. Startup
failure removes the reservation; the normal three-second request timeout is
unchanged. The runner also rejects a nonzero `Public service test line` if
the final RESULT text is interleaved.

Rebuilt service `build-aros/full-service-ready/OpenZFSServiceNative` in run
`public-service-run.yfrR8VHF` reached all four startup phases with operations
1/2/3 returning status 0. Both 32-cycle native-read phases and the storage
suite reported PASS. One lifecycle RESULT was interleaved, so the runner's
strict overall gate has not passed; preserve that distinction. No DMA timeout
was observed in this traced run, which does not disprove the retained
untraced DMA failures. Serial result emission needs whole-message handling
before claiming a clean automated regression result.

That traced run ultimately exited 1 at the overall deadline; its interleaved
lifecycle RESULT was not reconstructed or accepted. The public test client
now emits each `note` through one `bug("%s", ...)` kernel formatting call,
instead of independent RawPutChar bytes. The runner preserves `serial.log`
and matches a separate `serial-results.log` with only the standard leading
kernel timestamp/task/CPU prefix removed. Split fragments remain split.

Probe `cli-probe.cFpmBvtt` and the rebuilt client linked successfully. The
untraced four-CPU run `public-service-run.bMUGmj7k` with the readiness-fixed
service recorded all three lifecycle PASS results intact, then failed native
read repetition at cycle 19 (blank device, EIO, ATA58/BMDMA01, LBA32/114688
bytes). The runner immediately exited 1 on the intact storage failure. This
verifies startup/result-reporting progress while explicitly reproducing the
remaining DMA fault; it is not an overall passing storage test.

The subsequent host-read/IDE-traced run `public-service-run.sxC5ZnyZ`, using
the same readiness-fixed service and serialized client, completed the strict
runner with exit 0: three lifecycle results, both 32-cycle read phases, the
RAM-backed mirror/DOS suite, readonly label-disk write statistics and backing
hash checks. The captured host FD map identifies blank.raw as fd 15. All 410
completed traced pread64/preadv calls on that FD returned in at most 0.000304
seconds. This measurement describes a passing traced run only: it does not
measure the outstanding read in the failed untraced run or establish a DMA
fix. Retain both outcomes; tracing may alter the triggering timing.

### Native post-upgrade boot notice and pool-property transport

`after_zpool_upgrade` now queries the real `bootfs` property and warns that
the installed boot loader must support enabled pool features when bootfs is
configured. It does not install or update boot code. An output-stream helper
allows testing the actual message without replacing libzfs queries.

Probe `cli-probe.KAFE4hma` and its client compiled/linked; whole-command probe
`cli-link.arY1D7jf` now resolves this hook. Both commands still fail to link:
sharing providers remain missing, along with the listed native device hooks
for zpool. Runtime `public-service-run.cQr1ROx3` passed lifecycle and both
native read repetition phases but failed the new file-vdev/upgrade-notice
test (storage line 319). It is not a passing hook test. Step-specific
diagnostics have been added for initial bootfs, output stream, set, notice
and restoration; diagnose that failure before claiming the hook verified.

Diagnostic run `public-service-run.CMaVwUhs` identified set-bootfs as the
failure: the native transport had not admitted ZFS_IOC_POOL_SET_PROPS, so it
returned ENOTSUP before the real dispatcher ran. The transport now accepts
its bounded packed nvlist and inline pool name, rejecting config payloads,
caller pointers, received-property cookies and extraneous value strings.
It invokes the existing upstream pool-modify dispatcher under the freshly
resolved caller credentials; it does not implement a parallel property setter.

Service `build-aros/full-pool-property/OpenZFSServiceNative` and client probe
`cli-probe.CkqTBi36` passed strict four-CPU QEMU run
`public-service-run.jpmksrqd` (exit 0). Verified: actual libzfs bootfs set,
fresh query and warning content, restoration to unset, non-root own-session
denial with unchanged fresh-read value, and EROFS on readonly import with no
property change. The runner requires these markers. The earlier
`public-service-run.LKTWDvwc` passed before adding the readonly mutation check.
Both use disposable RAM-backed pools without native-disk fixtures to isolate
this integration from the separately retained ATA DMA regression. Neither
claims native disk stability, bootability, nor installation of boot code.

Use isolated hosted/QEMU guests and disposable disk files only. Exercise real
commands for pool creation/status/list/export/import, dataset creation/list,
properties, snapshots, and DOS/Wanderer access to mounted filesystems. Verify
CLI-to-GUI and GUI-to-CLI visibility against the same resident service, denied
non-root operations, delegation, absent-service failures, repeat invocations,
and cold-reboot persistence. Retain complete commands, output, exit codes and
artifact hashes. No DUT or existing user pool is part of these tests.
