# Native platform adapters

## CPU topology

`cpu.c` queries processor.resource and is linked into both the resident engine
and upstream CLI. See `../cli/README.md` for one- and four-CPU QEMU evidence.

## Read-only partition catalog

`partitions.c` reads an explicitly named device/unit through AROS
partition.library 3. It copies the immediate children of the root table into a
bounded pointer-free catalog, using the library's exact 64-bit start/end tags,
type bytes, names and positions. It never writes a partition table or calls a
device write operation. Positions are library positions, not Unix partition
suffixes or persistent identities. The caller's output remains unchanged on
any failure. Unrecognized/failed tables are errors, not declarations that a
disk is blank or reusable. Capacity saturation and oversized tables fail
explicitly rather than truncating their extents or entries.
Overlapping sibling extents also reject the entire catalog with EIO. Nested
containers are not flattened into this list, so their children do not count as
overlapping siblings.

`test-qemu-partitions.sh` creates two new 256-MiB files in a private run
directory. One receives a GPT with two ZFS partitions whose starts are not
cylinder-aligned; the other remains blank. QEMU uses disposable snapshots, no
network, and the private DOS/ATA/POSIX overlays. The test executes real native
library calls, not a host-side substitute for the parser.

`build-aros/partition-catalog-run.hr82RiqK` passed 16 repetitions of exact
extent, known ZFS type UUID, name, position and count checks; blank-table and
invalid-request failure; and unchanged output on failure. Before/after hashes
of both backing images match. Snapshot isolation protects those backing files;
the hashes alone do not prove an absence of guest writes. The adapter itself
uses only read/query/close APIs. Logs and source/binary/input hashes are in the
run directory.

The earlier `partition-catalog-run.x4cUUrz3` failed because the adapter treated
GetPartitionTableAttrs' true-on-success result as an error. This was not an
NVMe, ATA or GPT corruption diagnosis; the library had already validated the
GPT header and entries. GetPartitionAttrs uses a different zero result. The
failed run is retained.

Remaining integration is explicit: zpool device import and a GUI device picker
are not yet wired to this catalog. Nested-table traversal, persistent hardware
and partition identity, exclusive ownership, large-capacity geometry, writable
partition operations, and broader partition-format tests remain unfinished.
Catalog results alone must never authorize formatting or writes.

### MBR and invalid metadata

The runner also accepts `AROS_TEST_PARTITION_MODE=mbr`, `overlap`, or
`out-of-bounds`. Its fixture generator exclusively creates a new 256-MiB file;
it cannot overwrite an existing path. MBR entries occupy slots 0 and 2, with
unaligned starts and type 0xBF. The invalid variants respectively share exactly
one sector or extend exactly one sector beyond the image. These tests exercise
the actual native library and catalog validation, not a mock partition parser.

Each of the following QEMU runs passed 16 cycles and retained matching
before/after backing-image hashes:

- `partition-catalog-run.VhG4jsBi`: MBR exact extents, native slot positions,
  type bytes, blank rejection, and unchanged output on invalid requests.
- `partition-catalog-run.JY4Othan`: overlapping siblings rejected with EIO;
  full caller output remains unchanged.
- `partition-catalog-run.VV8SWJfj`: out-of-device extent rejected with EIO;
  full caller output remains unchanged.
- `partition-catalog-run.fNV6r0kR`: GPT regression passed, including exact
  extents, known UUIDs and names. Image generation first waited on host I/O
  (`sgdisk` in `rq_qos_wait`); the original process was retained and completed,
  then QEMU passed. This was not a guest hang or a reason to restart a guest.

These runs do not establish RDB/EBR, nested-table, 4-KiB-sector, or persistent
identity support. MBR type 0xBF alone is not proof that a partition contains a
ZFS pool; import still requires upstream label validation.

### Resident-service discovery API

`../ui/partition-api.h` exposes `oz_partitions_read(port, device, unit, out)`
through the real admission service. The engine links the native catalog adapter;
the client does not link a private device parser or engine. Each request captures
fresh security.library packet credentials. Raw-device metadata queries require
native root; this is separate from dataset delegation and cannot reuse a cached
root session or the service task's identity. Requests contain no caller UID.

The server validates the exact packet size, protocol version, reserved fields,
nonnegative unit and terminated device name, then reads a private copy of the
request. It copies the catalog back only on success. Errors including blank
tables, absent service, malformed packets and denied identity leave the caller's
catalog unchanged. No write operation or writable-device admission is added.

`public-service-run.sij7ohb0` passed the real service lifecycle, control-ioctl and
RAM mirror gates plus 16 partition RPC queries. The native client verified exact
MBR extents, types and noncontiguous slot positions, malformed requests, blank
table rejection, and missing service. Switching the same client task from root
to ozreader denied the query; switching back restored access without restarting
the service. The two new snapshot-protected backing-image hashes match.

The follow-up `public-service-run.lsYrLty2` also passed with both strict upstream
initialization and control gates enabled. Service `full-partition-rpc` and
fresh client `cli-probe.j1VqGNzK` exercise the same API alongside upstream init,
property changes, snapshots, mount lifecycle and file-vdev checks. This proves
the integrated API, not a finished device picker or full raw-device import.

Set `AROS_TEST_REQUIRE_PARTITIONS=1` with `test-qemu-public-service.sh` to create
the private MBR/blank fixtures and require the partition RPC result alongside all
other selected gates. This does not access preexisting host disks or the DUT.

### Native Manager device view

The Manager's **Devices...** window now calls the resident API through an
asynchronous worker. It accepts an explicit AROS device/unit, displays the real
table format, sector size, native slot positions, exact extents, byte sizes,
names and native type bytes. It performs no automatic device scan and creates
no invented raw-device path. Inspection does not add partitions to a writable
pool draft: stable identity and exclusive ownership are still prerequisites.

The worker copies its inputs, never calls MUI, and is joined before freeing
request storage. The window can be hidden while a request is pending; closing
the application waits for outstanding workers rather than cancelling packets.
Query errors clear old rows and report failure, not an empty/reusable disk.
Input validation accepts only a nonempty device and a decimal signed-32-bit
nonnegative unit. No credentials or device selection are persisted in prefs.

Native QEMU `native-gui-run.xHNRaSjP` used the real `full-partition-rpc` service
and the fresh `partition-ui/OpenZFSManagerNative` build (no compiler warnings).
Visual evidence and `ui-actions.jsonl` are retained in that run:

- `10-native-partitions-result.png`: real MBR slots 0 and 2, exact sectors,
  67108864 bytes per partition and type BF.
- `12-blank-error.png`: blank unit 1 reports I/O error; previous rows removed.
- `13-invalid-unit.png`: nonnumeric unit rejected with clear input guidance.
- `14-repeat-success.png`: unit 0 queried successfully again after errors.
- `15-device-window-closed.png`: normal close returns to Manager; activity pane
  records both successful queries and the failed query.

The private guest's pointer needed visual coordinate verification; initial
missed clicks are retained, not attributed to an application lockup. No input
driver settings were changed. After closing Manager normally, strict INIT,
CONTROL and PARTITIONS gates all completed, including mirror export/read-only
reimport, with matching backing-image hashes. Closing during deliberately
delayed discovery and worker fault injection are not yet tested by this run.

## Exact-range native device I/O

`../engine/device-io.c` now exposes `oz_device_open_range` for an explicitly
identified and authorized device byte extent. Length must be nonzero; offset
and length must be sector-aligned and entirely within native geometry. The handle
reports the extent's size, validates every transfer relative to that extent,
then translates to an absolute device offset before selecting 32/64-bit I/O.
Whole-device handles retain base zero. Read-only handles still reject writes.
This primitive does not discover partitions, establish identity, or confer
exclusive access. Production raw import authorization guards remain unchanged.

`device-range-run.ROnXNCMz` passed actual AROS/ATA QEMU I/O on two newly created
snapshot-protected images (256 MiB and 8 GiB). One range starts at sector 4097;
the other starts 512 bytes below 4 GiB and crosses that boundary. Each runs four
2-MiB write/flush/read cycles, compares relative-handle and absolute whole-device
reads, checks unchanged adjacent sectors, rejects overflowing/unaligned/outside
extents and transfers, rejects read-only writes, and verifies reopen readback.
Both host backing-image hashes match. The adapter/test compiled with
`-Wall -Wextra -Werror`. Logs, sources, binary and input hashes are retained.
This is not yet an upstream pool imported from a partition, exclusive ownership,
or a cold-boot durability test of the range path.

The rebuilt native whole-device harness passed `qemu-run.EAaukw0X`: mirror
creation, cold read-only import, each surviving mirror member (including a
blank nonmember alongside one survivor), and cold writable reopen on COW disks.
Its checks require zero writes for read-only/survivor boots and unchanged base
image hashes. This protects the existing whole-device behavior; it does not
extend that cold-boot claim to partition-backed pools yet.

The three fresh binaries are in `native-range-regression`; the mirror runner
now accepts `AROS_TEST_BINARY_DIR` and the private POSIX overlay so tests cannot
silently use older default binaries. Build logs `native-range-harness-*-build.log`
are retained. The initial `native-range-pool-build.log` used the full service
dispatcher with an older standalone pool harness and failed to link its absent
snapshot-unmount callback. Rebuilding in the harness's intended non-dispatcher
mode resolved that configuration mismatch; no success stub was added and the
full production service's real callback was not changed.

## Upstream pools inside native partitions

`../engine/partition-io.c` resolves a root-table native position through
partition.library, rejects missing/ambiguous positions, and opens the exact
sector-aligned range through the bounded I/O adapter. Native sector size must
remain consistent. This does not yet establish persistent identity or exclusive
ownership against other users of the device or partition-table changes.

The existing explicit disposable-QEMU lane now recognizes only two additional
test paths, `/AROS/ata.device/0/partition/0` and its unit-1 counterpart. They
still require `OPENZFS_QEMU_DEVICE_IO=1`, reject create/truncate flags, and
require 512-MiB enclosing devices and the existing allowed range sizes. This
is NOT unrestricted native raw import, and management's production device
admission policy remains unchanged.

`AROS_TEST_PARTITION_POOLS=1` makes the mirror runner create two new 512-MiB
images, each with a 256-MiB MBR partition starting at sector 4097. The actual
upstream engine creates its mirror through partition-resolved native I/O;
label preflight and pool I/O are relative to the partition, not the whole disk.
The exported configuration retains the partition paths across cold boots.

`qemu-run.s35JVfMQ` passed with fresh `native-partition-regression` binaries:

- Mirror creation and full native DOS handler/ACL/file-operation tests.
- Cold read-only import and each surviving mirror member, with zero guest
  writes in the read-only boots.
- Cold writable reopen and export using private COW images.
- Matching hashes of **all bytes outside each partition** after creation and
  writable reopen, including MBR and guard patterns; unchanged original images
  throughout the subsequent cold-boot tests.
- Upstream host `zdb -bcc` on exact extracted partitions from both creation and
  writable reopen: `No leaks (block sum matches space maps exactly)` in both logs.

Logs, exported configuration, source archive, input hashes, outside-region
digests and extracted validation images are retained in that run. The fixture
generator uses exclusive creation and never overwrites existing images.
Production identity/ownership admission, general partition selection, nested
tables remain separate unfinished work. GPT pool verification is described
below; discovery alone is not evidence of a usable GPT-backed pool.

### Full resident service after partition extent integration

`full-partition-extents` rebuilds the complete resident dispatcher with the
partition-range adapter, not just the standalone pool harness. Its strict
`public-service-run.rC6RFjHq` regression passed INIT, CONTROL and PARTITIONS:
fresh credential checks, malformed/denied partition queries with unchanged
output, repeated real upstream libzfs initialization and failure cleanup,
snapshot/property/mount operations, file-vdev discovery/reuse policy, and
the native handler RAM-backed mirror round trip. This does not enable general
raw-device writes in the management service.

### GPT pool fixture

Set `AROS_TEST_PARTITION_POOLS=1 AROS_TEST_POOL_PARTITION_TABLE=gpt` for
two new 512-MiB GPT disk files with 256-MiB ZFS partitions starting at sector
4097. The fixture uses actual `sgdisk`, leaves the backup GPT intact, and the
existing outside-extent hashes cover both partition tables and all guard bytes.
Creation is exclusive; no existing disk image or physical device is accepted
as a fixture output. The normal cold-import, degraded-member, writable-reopen
and host `zdb` checks still apply.

The first GPT fixture attempt `qemu-run.Cnre7uiH` stopped before guest boot:
the generator wrote an MBR before invoking `sgdisk`, which refused implicit
conversion. GPT mode now starts with a blank image. This was a host fixture
error, not an AROS partition, pool or device failure.

Host regression `partition-fixture-host.RdJPmc57` confirms the default MBR
fixture still creates correctly, refuses an existing output without changing
its outside hash, and extracts exactly 268435456 bytes. GPT runtime results
must be recorded separately; adding this fixture mode alone is not a guest pass.

`qemu-run.gYHUxwAa` subsequently completed the full GPT-backed upstream
mirror sequence: creation, cold read-only import, both degraded-member boots,
and cold writable reopen. All bytes outside both partitions were unchanged
after creation and reopen, including primary and backup GPT structures. Both
extracted-pool `zdb -bcc` checks reported no leaks. The original run remained
live through a long host `sgdisk` I/O wait and was never restarted; its eventual
exit was zero. That wait was not a guest failure.

### Authenticated native whole-device geometry

`OZ_DEVICE_GEOMETRY_ACTION` supplies actual Exec device capacity and sector
size independently of partition-table recognition. It uses the existing
pointer-free device/unit request, copies it before validation, and checks
fresh security.library root credentials before opening the device read-only.
Handles are closed before reply. Output is ignored on input and unchanged on
any service error; the client also validates the returned version, sector-size
range/alignment and nonzero byte capacity before publication.

`full-native-geometry` and `public-service-run.s34hbL68` passed the real native
RPC on partitioned and blank 256-MiB QEMU disks for 16 cycles each. Missing
device/service, malformed packet size/version/reserved fields/unit/name and
same-task root-to-reader-to-root transitions were verified; failed queries
preserved caller output. Existing CONTROL, partition discovery, service
lifecycle and native DOS storage tests passed too. This run did not enable
the optional upstream INIT fixture. Geometry remains neither persistent
identity nor write admission; it does not relax raw-device policy.

Run with the existing private `AROS_TEST_POSIX_LIBRARY`,
`AROS_TEST_DOS_OVERLAY` and `AROS_TEST_ATA_OVERLAY` variables, then
`bash aros/platform/test-qemu-partitions.sh`. Never substitute a physical disk
for either generated image or run against the user's hosted/DUT session.

### ISO staging and shared temporary storage

the platform issue tracker identified old optional ISO staging in shared `/tmp` consuming
roughly 117 GiB and disrupting unrelated agent builds. The admission-service,
file-backing and native-UI runners now reject `AROS_TEST_ISO_TMPDIR`; images
stay with the private `build-aros` run evidence. New cross-build invocations
must also use a private repository TMPDIR, not fill the shared tmpfs.

`archive-iso-scratch.sh` inventories only owned regular ISO files in narrowly
named staging directories referenced by this repository's `iso-location.txt`
records. Live files are skipped. It retains original SHA-256 hashes and run
references, archives with a long-window Zstandard stream, compares the archive
against all original files, and rechecks their hashes. It never deletes files.

Only after that succeeds may `prune-iso-scratch.sh <retention-directory>`
remove exact duplicate originals. It verifies the archive checksum, validates
every target path and owner, checks its hash again, and checks live use both
before and after hashing. It never recursively removes directories. Changed
or live files are skipped. Each removed path/hash is logged; all removed bytes
remain recoverable from `isos.tar.zst` using `zstd -d --long=30` and tar into
a new private directory. Logs and source snapshots remain at their original
repository evidence paths. This protocol is not permission to clean arbitrary
`/tmp` content, user artifacts, live runs or another agent's files.

Retention run `iso-retention.FDJWaNVS` archived 183 originals (117 GiB) in
a 5.94-GiB Zstandard archive. The archive's full byte comparison and all 183
original SHA-256 rechecks passed. The first cleanup wrapper waited inside the
host's Rust coreutils `sync` before deleting anything; an explicit `fsync` of
the same archive completed immediately. The helper now calls file/directory
`fsync` directly. The old wrapper was terminated before the corrected cleanup
was started. Its completed archive remains the recovery source; `prune.log`
records the exact copies removed as cleanup proceeds. Do not infer all scratch
space recovered until the cleanup's final result and filesystem usage confirm it.

Final verification matched all 183 REMOVED entries against the original
hash/path manifest, confirmed every original absent, and rechecked the archive
checksum. `/tmp` usage fell from 120 GiB to 2.3 GiB (122 GiB available).
The first corrected cleanup removed all files but exited 2 printing its final
banner because a source comment was edited while Bash was reading that script.
This operator mistake is retained, not reported as a clean exit. The helper now
executes a frozen, hashed copy and skips already absent paths before directory
ownership checks. A subsequent idempotent run exited zero with all originals
absent and the archive checksum still valid. No logs or source evidence were
removed; the ISO bytes remain in the archive.

## Authenticated native device byte reads

The partition/geometry query API now also provides `oz_device_read_bytes`.
It performs exact-length, sector-aligned reads of an explicitly named native
device/unit through the resident admission service. Requests are capped at
1 MiB. The service copies and validates the request header, authenticates the
current packet caller, requires UID 0, and opens the device read-only. It
checks capacity/alignment before allocating a private I/O buffer. Only a
complete successful native transfer copies bytes back to the caller; malformed,
denied, missing-device and failed reads leave the response payload unchanged.

This supplies device bytes for subsequent upstream label inspection; it does
not yet connect native devices to every CLI label/import path. Each request
opens/closes its device independently. It is not a persistent device identity,
exclusive ownership claim, partition admission, or permission to write/import.
The current native geometry capacity limit and synchronous device-I/O wait
limitations still apply. There is no new raw write operation.

Build `full-native-device-read` and strict INIT/CONTROL/PARTITIONS QEMU run
`public-service-run.p2TUDNE3` passed against fresh disposable MBR/blank images.
Checks include the exact MBR signature and both partition starting LBAs,
eight blank-unit reads, the last valid sector, a maximum-size 1 MiB read,
misalignment, EOF/overflow, zero/oversized count, missing device/service,
malformed packets, unchanged error output, reader denial, and restored-root
success in the same caller task. Both original disk hashes remained unchanged.
Existing upstream-client, service and DOS mirror storage regressions passed.

Earlier run `SaLib23I` also passed the checks, but its new per-read PASS lines
were sent only to the guest console by mistake. The fixture now uses the serial
note function and the runner requires exactly two root PASS markers plus one
denied/unchanged marker before accepting the partition-enabled run. Do not use
the earlier run as evidence that the new result markers were retained.

Four-CPU repeat `public-service-run.CDyduA4U` also exited zero with the same
read, authorization and storage gates and `cpus=4` topology evidence. This is
an SMP guest regression, not a claim of concurrent raw-read stress coverage.
