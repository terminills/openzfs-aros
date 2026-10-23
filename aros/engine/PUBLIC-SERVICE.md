# Public service build and lifecycle

## Authenticated native client sessions

The admission endpoint now accepts explicit `OZ_SESSION_OPEN` and
`OZ_SESSION_CLOSE`, with client helpers in `admission-client.c`. Both calls
capture the real packet sender through security.library. Handles contain the
server's random epoch and a monotonically increasing number, never an internal
pointer. Close requires the opening UID; even an administrator does not
implicitly take ownership of another UID's session. Unknown/released handles
return ENOENT, another UID returns EACCES, and the bounded eight-session table
returns EBUSY when full. Closing frees its slot without recycling the number.

Each session owns the real upstream on-exit state through session-cleanup.c.
The full service link includes upstream init/destroy/state-registration code;
the kernel FD/minor wrappers are excluded from that object explicitly. The
old libzpool diagnostic FD route is not used by these session operations and
must still be replaced for an actual upstream CLI transport.

Service Ctrl-C now checks active sessions and pending admissions before engine
teardown and remains responsive when shutdown is refused. Sessions now retire
when their opening task is removed (see below); a still-live unresponsive task
is not guessed dead or evicted by a timeout. In-flight ioctl acquisition,
streaming FD mapping and the upstream dispatcher remain unfinished. Session creation does
not advertise those capabilities or authorize subsequent operations.

`production-session-build.log` rebuilt the full service/Manager bundle without
section garbage collection of the engine. Private diskless QEMU
`public-service-run.uS8CHHY4` passed three lifecycle runs and the authenticated
RAM mirror/DOS round trip. Its two writable runs additionally checked all eight
session slots, ninth-open rejection, foreign-UID denial, wrong-epoch rejection,
double-close rejection, fresh monotonically increasing handles, shutdown
refusal while sessions remain, and eventual normal shutdown when empty.
These tests do not claim crash cleanup or upstream CLI execution.

The independent final run `public-service-run.D1DSDooR` additionally verifies
UID77 can open and close its own session after being denied access to root's.
The client compiled with `-Werror` (`public-session-client-build.log`); the
runner requires both session-test markers, all three lifecycle markers, and
the storage marker and exited zero. Source archives now include the CLI
lifetime code and upstream on-exit source/header as well as engine/UI/ZPL.

### Opening-task exit reclamation

Admission registers a task.resource lifecycle hook before accepting sessions.
The hook only marks matching slots under a native semaphore and signals the
service; it does not call the engine, run callbacks, use the caller's CRT, or
wait for service work. The service pump clears the watch and closes the real
upstream cleanup state. Hooks are removed before the server/port is freed;
the task.resource removal API waits out concurrent hook invocations.

The synchronous session-open packet contract keeps the sender alive until
ReplyPkt, and its watch is installed before that reply. Session lifetime is
explicitly that of the opening task, not an invented Unix process identity.
A future process-wide CLI connection must be owned by a persistent command
task, not a transient pthread. Forced removal while a borrowed DOS packet is
in flight is not covered. In-flight ioctl references must also block engine
teardown independently of registry slots before that transport is enabled.

The new LVOs require task.resource 1.4. AROS initializes resources as Nodes,
not fully populated Library headers; capability checking therefore uses the
matching resident's name pointer and version identity. An unavailable or
unrecognized implementation leaves session open explicitly unsupported while
ordinary admission remains available. The first private runs
`public-service-run.ZW1FKLdL` and `4Et4u7al` incorrectly checked Library version
fields, observed 0.0 and rejected open with ENOTSUP; both failures are retained.
Generated task_start.c confirmed those fields are not initialized. No runtime
resource was replaced to fix this application-side mistake.

Full rebuild `production-session-exit-build-3.log` and private QEMU
`public-service-run.Jn1lhpIb` passed all three lifecycle runs, both session
authorization suites and the RAM mirror/DOS round trip. Each writable run
launched four distinct synchronous child processes which filled eight slots
and deliberately exited without session close. Parent requests rejected all
32 old tickets and successfully opened new sessions afterward: 64 abandoned
sessions across both runs. This verifies real task-removal delivery and slot
reuse, not client-crash recovery during a storage operation or a complete CLI.

The normal `aros/engine/build-management.sh` recipe now links the Manager's
authenticated worker/client and IFF preference implementation through the shared
`aros/ui/build-manager-native.sh` recipe. The older recipe omitted those objects
and libraries. All build-management outputs honor `AROS_OUTPUT_DIR`, and its
native acceptance clients honor `AROS_SDK`. The native Manager recipe checks
that the private pthread archive's header matches the selected pthread include
directory; isolated preference GUI builds use that same recipe.

`build-aros/production-management-build-1.log` records a completed full engine,
public service, Manager and legacy-client build in `production-build-check`.
The existing private-pthread/SDK macro-redefinition warnings remain; no claim
is made that this whole engine build is warning-free. The Manager and focused
public-service client compiled without new warnings.

## Native public endpoint verification

`build-aros/public-service-run.56MkGYeF` / `public-service-run-1.log` passed two
consecutive cycles of the actual `OpenZFSServiceNative` executable in a private
diskless QEMU guest. It was launched with no TEST arguments and without the
fixture-import harness. Both cycles verified:

- Actual upstream engine initialized; public hello and capabilities succeeded.
- Public pool listing returned success and zero rows, with engine available.
- Public admission endpoint existed; dedicated native test endpoints did not.
- With no pools or mounted endpoints, Ctrl-C removed both public endpoints and
  the service reported engine shutdown complete.
- A second process initialized and shut down again in the same guest.

The runner exited zero after both PASS markers. It attaches only a read-only
ISO, with no hard disks or NIC. Service temporary files live in RAM. The client
refuses to run without the private ISO marker, and only signals the service
whose public replies it just verified. Sources, binaries, input hashes, serial
and QEMU logs are frozen in the evidence directory. This does not establish
production authority provisioning, writable public imports, mounted endpoint
retirement, physical I/O durability or full-port completion.

Reproduction uses the existing consistent native build inputs:

```sh
AROS_SDK=/path/to/native/Developer \
AROS_PRIVATE_PTHREAD=/path/to/consistent/libpthread.a \
AROS_PTHREAD_INCLUDE=/path/to/consistent/source \
AROS_ENGINE_OBJECT_DIR=/path/to/consistent/engine \
AROS_NVPAIR_ARCHIVE=/path/to/libnvpair.a \
AROS_LINKED_OBJECT=/path/to/output/full-engine.o \
AROS_OUTPUT_DIR=/path/to/output \
bash aros/engine/build-management.sh

AROS_TEST_SERVICE_BINARY=/path/to/output/OpenZFSServiceNative \
AROS_TEST_CLIENT_BINARY=/path/to/output/OpenZFSPublicServiceTest \
AROS_TEST_POSIX_LIBRARY=/path/to/private/posixc.library \
AROS_TEST_DOS_OVERLAY=/path/to/private/dos-overlay \
AROS_TEST_ATA_OVERLAY=/path/to/private/ata-overlay \
bash aros/engine/test-qemu-public-service.sh
```

The legacy management/write/mount clients built by build-management predate
the current capability set; building them is not a claim that their historical
expectations pass against the current service. The focused public lifecycle
client checks current behavior without relying on those stale exact masks.

## Public mutation boundary

The normal public service now uses `oz_broker_process_public`: after ordinary
message validation, any operation marked as a mutation in the API catalog is
rejected with explicit authenticated-admission guidance before engine dispatch.
A confirmation flag does not authenticate the sender. This includes read-only
mount, which still publishes a DOS endpoint. Isolated legacy fixture services
continue using the historical adapter, while the public Manager's authenticated
worker remains the mutation path. This does not add a writable startup option.

`production-management-build-2.log` rebuilt the complete bundle with that
boundary. `public-service-run.kDGmFy4b` / `public-service-run-2.log` passed both
start/query/stop cycles and, in each, confirmed that a syntactically valid legacy
read-only mount request targeting a nonexistent dataset was rejected by the
admission boundary. No backing disks were attached, and no storage mutation was
attempted by the engine. Both service shutdown markers and runner exit 0 were
observed. This verifies the tested public route, not a complete security audit
of every operation or the AROS shared-address-space model.

## Explicit writable startup

`OpenZFSServiceNative WRITABLE` now selects the public write-capable engine
without selecting a test endpoint. It requires an already configured
security.library and the current native system task identity. The daemon does
not log in, accept passwords, or grant itself privilege. Default startup remains
read-only; unknown/extra arguments fail before publishing ports. A writable
startup also fails if either engine initialization or admission setup fails.
The public legacy mutation gate remains in force for writable startup.

`production-management-build-3.log` and `public-service-run.zMcWP6Ly` /
`public-service-run-3.log` passed two default-readonly lifecycle cycles followed
by invalid-mode rejection, UID77/GID88 writable rejection without endpoints,
and a system-identity writable cycle. The capability response distinguishes
readonly/write-capable mode; both use only public endpoints, reject legacy
mount mutations, and shut down cleanly while empty. These are diskless startup
tests, not yet public writable storage-operation evidence. Only the marked
test launcher uses the disposable security accounts; production service code
contains no test-account login.

## Public authenticated storage path

`public-service-run.REx5LHSN` / `public-service-run-4.log` adds an actual storage
round trip through the normal public WRITABLE service, not the fixture-import
harness. The marked private launcher provisions two new zero-filled 128 MiB
RAM files. Through the public authenticated admission endpoint, it creates an
upstream mirror and filesystem, mounts a writable DOS volume, writes and
flushes `hello.txt`, then reads and compares every payload byte. It unmounts and
exports, discovers/reimports the same file labels read-only, mounts again,
reads identical content and verifies that a new-file write returns
ERROR_DISK_WRITE_PROTECTED. Final unmount and export both succeed.

The client then signals the service and checks that it remains resident with
both public endpoints present: retired DOS endpoints are still deliberately
retained. The serial log confirms the service's resident-after-mount message.
The runner requires three empty-service lifecycle PASS markers plus the public
storage PASS marker and exits zero after terminating its private QEMU guest.
`public-service-client-build-4.log` is warning-free; the service executable is
the one from production-management-build-3, copied and hashed by the runner.

This proves the production public API-to-upstream-engine-to-DOS-handler path
with authenticated caller identity. The files live only in guest RAM: it is
not a cold-boot, offline-zdb, power-loss, native-device, or public-Wanderer GUI
test. No physical disks, host pools, DUT or shared hosted session are involved.
Safe retired-endpoint reclamation remains a real completion gap.

## Native Manager on the public service

`native-gui-run.uwpaJ8s2` / `public-service-gui-run-1.log` passed the same storage
round trip with the real Manager opened on the mounted fixture. The Manager
received no TEST argument and used the public query/admission endpoints.
Captured evidence:

- `public-manager.png`: upstream OpenZFS 2.4.4, engine available/write-capable.
- `public-pool-list.png`: actual `public_mirror` ONLINE, not an invented row.
- `public-pool-status.png`: mirror and both RAM file members ONLINE; zero
  reported read/write/checksum errors.
- `public-datasets-recursive.png`: child `public_mirror/files` mounted writable
  as `OZPUBLIC`, alongside the root dataset.
- `public-snapshot-review.png` and `public-snapshot-created.png`: confirmation
  and successful authenticated-worker creation of `public_mirror/files@public_gui`.
- `public-snapshot-enumerated.png`: a subsequent dataset enumeration returns
  the real snapshot and its originating mounted filesystem.

Closing the Manager resumed independent launcher checks: unmount/export,
read-only label import, exact DOS payload reread, denied write, final
unmount/export, and intentional resident-endpoint retention. All passed and
the runner exited zero. The snapshot itself was visually enumerated before
export; this run does not separately assert its survival after reimport or
physical reboot. Source snapshots, executables, UI actions and serial logs
are retained. The shell background includes intentionally rejected startup
attempts from the preceding authority tests, not an unexplained GUI failure.

Reproduce the interactive variant with `AROS_PUBLIC_GUI=1` and
`AROS_TEST_MANAGER_BINARY` set to the built native Manager, alongside the
existing public-service runner variables. The fixture opens the Manager after
creating and verifying the mounted file. Close it only after recording GUI
checks; no shared hosted session or DUT is involved.
# Native discovery admission regression (2026-10-22)

## Native CLI subprocess packaging

The MetaMake aggregate previously omitted `SYS:Tools/OpenZFS/ExecHelper`,
required by the native upstream process adapter. The helper now has a native
MetaMake target and is a dependency of both CLI targets. The package stager
includes it and requires runtime evidence plus matching GUI/cold hashes.
`public-service-run.ySc0xoyJ` passed a real `zpool list` through
`oz_cli_run_process`, exact captured pool-name output and exit 255 for a missing
executable, followed by the full storage suite. The test ISO was pruned.
Earlier bundles lack the helper; their ordinary CLI/storage evidence does not
establish external-command support. Fresh GUI/cold bundle qualification is
still required after this packaging change.

That refresh subsequently passed: `public-service-run.sWX1IgC5` qualifies the
helper-inclusive writer and fresh healthy/degraded/restored imports, persistent
startup restore/mount and independent zdb verification. `native-gui-run.mNvyyTl6`
qualifies live Manager pool queries, settings save, mounted Wanderer files and
post-UI data checks with the same helper-inclusive payload. Both runs execute
the real subprocess test. `mmake-bundle.biw8z7ob` is hash-bound to those runs,
includes `Tools/OpenZFS/ExecHelper`, and passes all SHA256SUMS. Test images were
pruned; screenshots, logs and provenance remain. Hardware and stock-runtime
boundaries are unchanged.

`OpenZFSRuntimeCheck` now directly probes the installed helper, using its own
canonical executable path rather than a PROGDIR relative to the helper. Native
run `public-service-run.UjXmlWsK` reports `PASS ExecHelper subprocess: status=23
lines=1` and full storage PASS. This diagnostic changes no pool or global
assign; its temporary capture uses T:. The helper-inclusive `biw8z7ob` archive
predates this extra diagnostic and remains immutable. This run proves the
positive execution path; a missing-helper runtime failure injection remains
to be qualified separately.

## Discovery selection

Automatic discovery can admit both a whole device and its partitions as scan
candidates. Writable import must reserve only the leaves referenced by the
validated pool configuration, not every scanned candidate: reserving a whole
device and its partition produces a false overlapping-device busy error.
Configuration admission and extent-overlap protections remain enforced.

Isolated native QEMU evidence: `public-service-run.OYEaC9cB` reproduced the
writable automatic-import failure after successful readonly import/export.
With the selection fix, `public-service-run.ONdHQ42N` passed create, default
scan, readonly import, writable import and destroy, followed by the full
public service storage suite. Both backing-file hashes remained unchanged
(QEMU snapshot mode). The successful test ISO was pruned; logs, input hashes,
source snapshot and source patch remain. Candidate library SHA256:
`0073dd441d2628014a028e2d988005c9ebc5267d447119d0adc39e632e7c47c2`.
This is not physical USB qualification or a resolution of the separately
reported Manager startup crash (TRIG-1523).

The same candidate also passed persistent writer/fresh-boot qualification in
`public-service-run.PdrbuDoG`: healthy, degraded and restored mirror imports,
trusted ENVARC startup restore and read-only automatic mounting, exact DOS
data and links, encryption/key rotation, zvol readback, and beyond-parity
refusal. Host zdb traversal passed; pool member and startup archive hashes
remained unchanged during the reader. The harness pruned 9.78 GiB of test
images and retained logs, source, binaries and hash records. This does not
substitute for fresh GUI qualification of this engine candidate.

Fresh GUI qualification subsequently passed in `native-gui-run.t6xqK4TV`:
Manager connected to the candidate, List pools returned `public_mirror ONLINE`,
Save UI settings acknowledged ENV/ENVARC, and Wanderer displayed mounted pool
files. After closing the drawer and Manager, exact-read/export/reimport checks
completed with storage PASS. Screenshots `pools.png`, `preferences.png` and
`drawer-closed.png` and input records are retained; the 4.52 GiB ISO was pruned.
The evidence-bound bundle `mmake-bundle._bkcuna0` matches this GUI run and
`PdrbuDoG` cold run byte-for-byte and passed its complete SHA256SUMS check.
Platform prerequisites remain explicit; this is not stock-nightly or physical
USB qualification.

`public-service-run.RDq1KM4o` extends discovery qualification with a second
writable pool on the same virtual disk's disjoint partition 2. That neighbor
stays imported throughout partition 0 pool discovery, readonly import/export,
and writable import/destroy. Its ONLINE health and stored user property are
checked afterward, then it is explicitly destroyed. Both snapshot backing
hashes stayed unchanged, the full storage suite passed, and the ISO was pruned.
The runner now requires both discovery and coexistence markers; those exact
checks were also run against this retained result. Production bytes are unchanged.
