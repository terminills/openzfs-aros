# Native upstream send/receive engine proof

The AROS engine already links upstream dmu_send.c and dmu_recv.c. Native tests
now exercise those implementations with actual AROS file I/O, without Linux
forwarding or a recreated stream format. Authenticated receive operation 61
is now advertised when the service enables writes, and the native Manager uses
that API. Authenticated send operation 60 supports existing empty native output
files and staged new-file creation (verified on native RAM:). All verification
below uses a private fixture daemon and disposable QEMU disks, not production
storage. Earlier evidence sections preserve the implementation history;
the authenticated receive section records the current API/UI coverage.

## Interface and lifecycle

`dmu_send()` accepts the upstream output callback (`dmu_send_outparams_t`). The
test callback writes the supplied bytes through `zfs_file_write()`. It never
uses libzpool's unsupported `zfs_file_get()`/`zfs_file_put()` descriptor lookup,
which intentionally aborts in userspace. Receive consumes an explicitly opened
`zfs_file_t`, reading the BEGIN record before `dmu_recv_begin()`. A successful
begin must be paired with `dmu_recv_stream()`; successful stream processing
must be paired with `dmu_recv_end()`. Failed stream processing performs upstream
cleanup itself. These ownership rules are preserved.

The temporary RAM stream is created exclusively in a fresh private QEMU guest.
No existing file is truncated. Exact successful stream bytes are also retained
in `qemu_mirror/stream-artifacts/{full,incremental}.zfs` before the temporary file
is removed. A failed temporary stream is left available for diagnosis.

## Verified sequence

1. Create a real ZPL fixture with root and nested files, snapshot `@base`, send
   a full stream, receive into a separate dataset and compare file contents.
2. Modify/resize the source file, snapshot `@second`, send incrementally from
   `@base`, and receive into the destination with the matching base GUID.
3. Compare live root/nested files and both snapshots. Check the final received
   text against a literal expected value, not only against the source.
4. Cold read-only AROS boot reopens and verifies these objects. The standard
   runner also checks zero read-only writes, disk hashes, DOS/API regressions,
   offline zdb no-leaks, compression and timestamp persistence.
5. Extract the retained stream files with host upstream zdb after QEMU stops.
   Upstream zstream dump validates checksums, complete BEGIN/END records, exact
   stream length and the incremental from-GUID/full to-GUID chain. Its exit
   code alone is insufficient: it can print checksum errors and return zero.
   The verifier explicitly rejects diagnostics and incomplete record counts.

The interruption extension cuts a private full-stream copy halfway through,
expects `ZFS_ERR_STREAM_TRUNCATED` (observed errno 1040), verifies the partially
received dataset no longer exists, then successfully receives the full stream
to the same name. Cold reads compare the retry dataset with the base snapshot.
This is non-resumable receive cleanup/retry, not resume-token support.

## Evidence and retained failures

- `send-receive-build.log`: first compile failed because the replay-record type
  requires sys/zfs_ioctl.h. Corrected the include and used path resolution for
  nested files rather than the root-entry lookup helper.
- `admission-service-run.NY12WLeX`: initial full/incremental native WRITE and
  cold VERIFY plus complete standard regression passed.
- `admission-service-run.JsiyIdoK`: known-value verification and retained exact
  streams passed. Independent host verification also passed; full stream was
  57208 bytes and incremental stream 12192 bytes for that fixture.
- `admission-service-run.b5mph7mK`: repeated native test with the host verifier
  incorporated in the standard runner passed end-to-end.
- `admission-service-run.zZS2Nrgc`: interruption cleanup/full retry extension
  passed WRITE, cold VERIFY, complete regression and independent host stream
  validation, with runner exit 0. See
  `build-aros/typed-import-service/send-receive-run-4.log` and per-phase serial
  logs for the results.

All runs are under build-aros and freeze source/binary inputs with hashes.
Logs are `build-aros/typed-import-service/send-receive-*`; host zstream build log
is `zstream-control-build.log`. Focused initialization fixtures intentionally
do not require replication datasets, preserving their older saved-RO GUI
fixture contract. No DUT, shared hosted session or physical media was used.

## Next production work / not claimed

### Replication delegation foundation

`oz_send_perms()` and `oz_receive_perms()` in cred.c now port the upstream
dataset authorization rules. They require an explicit credential; creating
one is not authentication. Send checks actual DSL delegation on the held
dataset/snapshot: `send` permits normal/raw sends, `send:raw` only permits raw
sends, and `send:encrypted` only permits raw sends of encrypted datasets.
The zoned-dataset restriction is applied before any raw-permission fallback.

Receive resolves the existing filesystem or immediate parent of a new
filesystem, matching libzfs_core's ioctl-name selection. It requires `receive`
(or `receive:append` without force), together with `mount` and `create`, on
that held dataset. It does not search arbitrary ancestors to grant access to
missing intermediate parents. Force never falls back to append-only permission.
Root takes the upstream administrative fast path; actual existence, stream
validity and lifecycle checks remain the engine operation's responsibility.

`replication-perms-test.c` uses real on-disk DSL grants. It covers NULL and
ungranted callers, supplementary group membership, local versus descendant
snapshot grants, raw/plain separation, refusal of send:encrypted on an
unencrypted fixture, append-versus-force, required mount/create permissions,
new-target parent checks, missing intermediate parents and zoned-dataset
refusal. Grants are removed and a cold read-only boot verifies denied access.
The positive encrypted-dataset send:encrypted branch remains untested until
the encryption fixture exists. Tests of this helper alone are not proof of
native stream-file authority or an authenticated end-to-end send/receive API.

Evidence: `build-aros/admission-service-run.8iZG6FnM`, with build and runner logs
`build-aros/typed-import-service/replication-perms-{build,run-1}.log`.

### Retained native file authority

Admitted jobs now retain their complete immutable `oz_packet_security`
snapshot, including security.library extended owner, supplementary groups and
default protection, until job destruction. They still hold their separate
upstream credential reference. The snapshot is transferred on successful
admission and freed on job release/server cleanup; failed admission frees the
temporary snapshot. Nothing references the sender's request, reply port or
task after capture. The service port must outlive its jobs.

`oz_admitted_security()` returns a borrowed, const snapshot to internal service
code. `oz_packet_security_access_on()` evaluates that original owner against
an explicitly supplied filesystem context, instead of assuming the admission
port is the stream file's handler. The future stream adapter must supply a
trusted handler kept alive by an actual DOS lock/handle and ownership/protection
read from that object. Client-provided handler pointers or protection values
are not trusted. This helper does not by itself resolve paths, prevent alias
races, authorize directory traversal or open a stream file.

The native cross-task test now uses the admitted snapshot after sender logout,
reply-port deletion, task exit and destruction of an independent packet
context. Actual security.library calls still identify UID77 and supplementary
GID99: group-read succeeds, write is denied, NULL filesystem context is refused,
and the service's root identity does not authorize the denied dataset creation.
The existing single-execution and result-owner checks remain in the same test.
This is snapshot-lifetime/policy evidence, not completed stream-file I/O.

Evidence: `build-aros/admission-service-run.s7h2EWyb`; logs
`build-aros/typed-import-service/native-stream-authority-{build,run-1}.log`.

### Locked native stream object authorization

`stream-file.c:oz_stream_lock_access()` now checks a trusted, held native DOS
file lock with `Examine()`, obtains its actual UID/GID/protection and handler,
and evaluates access with the retained caller's security.library identity.
It rejects missing authority, invalid access masks, directories and synchronous
I/O to the service's own DOS endpoint (before sending an Examine packet).
The native test uses a real RAM file owned by UID55/GID99 after the UID77
sender has exited. Supplementary-group read succeeds, write is refused, and
revoking read protection is observed on the same held lock.

Evidence: `build-aros/admission-service-run.GFwX719T` passed writable and cold
read-only boots plus independent stream validation. The extended invalid-mask,
directory and self-handler checks passed both boots and independent validation
in `build-aros/admission-service-run.vjjGHjzk`; logs are
`build-aros/typed-import-service/locked-stream-{build-2,run-2}.log`.
Logs: `build-aros/typed-import-service/locked-stream-{build,run-1}.log`.
This is object authorization, not path traversal authorization or atomic
authorization against concurrent protection changes. Locks are internal trusted
objects, never pointers accepted from the client. Parent traversal and API
dispatch still need integration; no public send/receive feature is claimed by
this helper.

`oz_stream_file_open()` converts that checked lock using native `OpenFromLock`,
without reopening a pathname. Failure leaves the caller's lock intact;
success transfers its ownership into an opaque stream and clears the supplied
lock. The adapter enforces read/write direction itself because a native DOS
handle need not restrict operations to the originally requested access.
Read and write report actual byte counts (including successful EOF), and close
releases the handle. Counts exceeding the signed DOS Read/Write limit are
rejected, not truncated. This is a synchronous, internally owned stream;
concurrent operations/close require external serialization. Permission changes
after open follow open-handle semantics, not per-byte reauthorization.

The native caller-lifetime test now exercises denied open with lock retention,
successful lock consumption, actual file content, EOF, read-only write refusal,
and write-only read refusal plus a real write after native group-write is
granted. Detailed DOS error mapping, flush,
64-bit seek/stat and partial-transfer recovery remain to be wired as needed.

Handle-transfer evidence: `build-aros/admission-service-run.5D5HKX46` passed
both native boots, the full admission regression and independent stream
verification, runner exit 0. Build/run logs:
`build-aros/typed-import-service/stream-handle-{build-1,run-1}.log`.

### Upstream receive input bridge

The AROS `zfs_file_from_reader()` adapter wraps a trusted, borrowed sequential
reader for the unmodified upstream `dmu_recv` stream parser. It serializes reads,
preserves short-read residuals and callback errors, and rejects writes and
seeks. Closing the wrapper does not close the owner's native handle. The
owner must keep that handle alive through receive completion/cleanup and must
serialize destruction with reads. Callback/context pointers are internal only;
they are not part of the management wire protocol.

The full/incremental fixture now supplies bytes from an authorized native DOS
handle captured for a real UID77 sender after sender exit, without pathname
reopen. `admission-service-run.ZeIjtbWK` passed this bridge's full/incremental
receives, cold payload comparisons, regression suite and independent host
stream validation. Logs: `typed-import-service/native-receive-{build-1,run-1}.log`
under build-aros. That first run establishes native file authority and byte
transport, not delegated receive authority; the subsequent test adds actual
temporary DSL receive/mount/create grants and the captured upstream credential
scope, with denial checks before granting and after revocation.

That strengthened run passed in `build-aros/admission-service-run.wWC9hfs2`:
full and incremental receive, cold-boot content/snapshot verification, complete
admission regression and independent upstream stream parser all passed with
runner exit 0. The dmu_recv object was explicitly rebuilt against the updated
AROS file interface. Logs under `build-aros/typed-import-service` are
`native-receive-core-build-2.log`, `native-receive-build-2.log` and
`native-receive-run-2.log`. Grants are test-fixture setup, not automatic grants
performed by the production service.

Native interruption coverage passed in `admission-service-run.UwyerP3l`:
the half-stream returns `ZFS_ERR_STREAM_TRUNCATED` through the authorized DOS
handle, its partial destination is absent afterward, and reopening the complete
stream through the same authorization/credential path succeeds. Cold boot
checks the retried dataset. All temporary delegation grants are removed even
on the expected truncation path. The full runner and independent stream parser
passed, exit 0; logs are `typed-import-service/native-interruption-{build-1,run-1}.log`.

`replication.c:oz_receive_stream()` factors the receive sequence into a reusable
engine entry point: validate names and caller, check actual receive delegation,
reject read-only/suspended pools, install caller credential scope, and run the
paired upstream begin/stream/end lifecycle with credential restoration. The
input remains borrowed. Callers must authorize its native file access and
serialize/reject mounted-target lifecycle before entry. This currently accepts
normal full/incremental input, not force/property overrides/resume options.
It is linked into management-service builds but is not yet dispatched by the
public API; wiring it there requires the remaining path and lifecycle work.

Reusable receive evidence: `admission-service-run.FmWzn1fi` passed all four
native receive cases (full, expected truncation, full retry, incremental),
cold verification, regression suite and independent stream checks, exit 0.
The same live input first receives rejected NULL-caller, invalid-snapshot and
ungranted-caller requests; subsequent successful parsing proves those refusals
did not consume its header. Credential scope restoration is checked after
success and expected interruption. Logs under `build-aros/typed-import-service`:
`receive-engine-build-2.log` and `receive-engine-run-1.log`.

### Native authorized send

`oz_send_stream()` is the reusable ordinary full/incremental send entry point.
It validates source/base names, checks real send delegation before invoking
output, installs the captured caller's upstream credential, and forwards bytes
from upstream `dmu_send` to a trusted internal sink. Errors from that sink are
returned and the prior credential scope is restored. Output callbacks are not
client-provided function pointers; the caller must already authorize the sink.
The current entry point does not yet expose raw/compressed/embedded/resumable
send options or recursive property packages.

The native fixture now sends through `oz_stream_file_write()` on an actual DOS
handle authorized for the exited UID77 sender's supplementary GID99. It tests
ungranted refusal, temporary descendant-group send delegation, an injected
ENOSPC sink failure, retry, NULL-caller refusal, and revocation. Native output
handles are write-only and short writes are completed before acknowledging a
chunk. Pool grants and fixture-file ownership are test setup, not automatic
production grants. Both source streams still undergo native receive/cold-boot
verification and independent upstream host checksum/GUID validation.

Evidence: `build-aros/admission-service-run.mIxcOSwa` passed both native send
cases, all four receive cases, cold persistence, the full admission regression
and independent stream validation, runner exit 0. Logs under
`build-aros/typed-import-service`: `native-send-build-1.log` and
`native-send-run-1.log`. This still does not expose replication API operations
60/61 or prove advanced send options; path authorization and management
lifecycle integration remain required.

### Directory traversal boundary

`oz_stream_child_lock()` locates one named component beneath a trusted,
already-reached directory lock. It reads that directory's real native owner
and protection and checks the original caller's search (execute) permission
before sending ACTION_LOCATE_OBJECT. It does not use Lock() on a whole path:
that would silently follow soft links with the service task's authority.
Embedded slash/volume separators are refused, native BSTR encoding is used,
and a soft-link result is returned as ELOOP for explicit resolver handling.
The returned child lock is owned by the caller; final file access still needs
the existing authorization/open step. Self-served DOS endpoints are rejected
before synchronous packet I/O.

This is a resolver building block, not a complete pathname authorization claim.
The initial volume/assign/base, all subsequent components, soft-link expansion,
and creation of new output files still need integration. The native test uses
a real private RAM directory whose file is group-readable: directory read
permission alone does not authorize search; adding group execute permits
component lookup and the subsequent authorized file open. It also checks
missing names, embedded separators and refusal to follow a real soft link.

Evidence: `admission-service-run.7qUVJvyB` passed the initial directory-search
tests; `admission-service-run.vAIKeL6n` additionally passed the real soft-link
refusal case. Both completed writable/cold-read-only boots, the full admission
regression and independent stream verification with runner exit 0. Logs under
`build-aros/typed-import-service` are `stream-traversal-build-{1,2}.log` and
`stream-traversal-run-{1,2}.log`. All fixtures are private QEMU RAM files; no
DUT, user hosted session, physical disk or VPN storage was accessed.

`oz_stream_relative_lock()` now composes component lookup beneath a trusted
shared base lock. Every directory is checked for the captured caller's search
permission, including AROS parent (`/`) steps. Relative soft links are read
explicitly and their expanded components are checked again; expansion is
bounded to 16 links and 4095 pathname bytes. Cycles return ELOOP. Absolute
paths/links return EXDEV for the still-unimplemented namespace binder rather
than invoking the service's current-directory or privileged assign resolution.
Final regular-file access remains the separate checked open step. This is not
a directory sandbox: authorized parent traversal is allowed.

Retained failures: `admission-service-run.u5VUOSkd` failed with generic EIO;
diagnostic repeat `admission-service-run.wDtYNXjY` localized it to duplicating
the fixture's exclusive CreateDir lock (DOS error 202), before link resolution.
The resolver now explicitly rejects an exclusive base with EBUSY, and the
fixture verifies that rejection then reacquires a shared base. No protection
checks were relaxed. Logs are `typed-import-service/stream-relative-{build,run}-*.log`
under build-aros; these failed runs are not completion evidence.

Corrected evidence: `admission-service-run.jJTg145v` passed WRITE and cold VERIFY,
the complete admission regression and independent stream validation, exit 0.
Cases include shared/exclusive-base behavior, relative soft links, bounded
cycles, absolute-link EXDEV, denied search on a parent-traversal path, and the
same path succeeding after a group-execute grant. Logs under
`build-aros/typed-import-service`: `stream-relative-build-3.log` and
`stream-relative-run-3.log`. Full namespace binding and public API dispatch
remain unfinished.

### Fully qualified native namespace resolution

`oz_stream_path_lock()` now resolves a live volume/device or named assign and
walks the remaining path using the captured caller's directory checks. Bound
assigns retain copies of their published directory locks; those namespace
bindings are directory capabilities, not client-provided pointers. Multi-assign
candidates are tried in order on ENOENT. Deferred/nonbinding targets are copied
and recursively resolved by this resolver, not GetDeviceProc/Lock, and are not
promoted or modified. Absolute soft links re-enter the same namespace resolver.
All redirections share the bounded expansion budget.

Only fully qualified paths are accepted: current-directory and PROGDIR
resolution would otherwise use the service rather than the original caller.
Offline handlers return ENXIO; the resolver does not start handlers, request
media, or show missing-volume requesters. Self-served endpoints return EDEADLK
before synchronous packets. This still requires a direct-engine/asynchronous
route for streams stored on this service's own mounted volumes. Namespace
references are acquired under DOS-list read protection; testing other native
handlers and concurrent namespace/lifecycle changes remains necessary.

Private tests cover volume paths, absolute links, bound/late/nonbinding assigns,
multi-assign fallback, assign cycles, missing volumes and rejected PROGDIR.
Temporary fixture assigns and RAM-root search protection are removed/restored
after each test; production resolution does not alter permissions or assigns.
Initial evidence `admission-service-run.w1jKuV0N` passed WRITE/cold VERIFY,
regression and independent stream validation, exit 0. Logs under
`build-aros/typed-import-service` are `stream-namespace-{build-1,run-1}.log`.
Public API dispatch, output-file creation, service-owned stream routing and
mounted receive-target coordination are still unfinished.

Extended namespace evidence: `admission-service-run.MbMvakGf` also verifies
late/nonbinding assigns retain their original types and a real namespace entry
whose handler is the current task returns EDEADLK without a packet wait. WRITE,
cold VERIFY, the complete regression and independent stream validation passed,
runner exit 0. Logs under `build-aros/typed-import-service` are
`stream-namespace-build-2.log` and `stream-namespace-run-2.log`.

### Authenticated receive API integration

Operation 61 now dispatches through the admitted-job worker, retaining the
original native security context. The Manager uses that same worker, not the
unauthenticated legacy dispatcher. The target is `dataset@snapshot`; the
argument is a fully qualified native input filename. Flags must be CONFIRMED,
and options are currently empty (no force or resumable policy yet).

Before opening input, receive validates upstream delegation and the actual
destination pool's writable/non-suspended state and refuses mounted target
subtrees. Native path traversal and final file-read authorization use the
captured caller. Self-served inputs return EDEADLK instead of synchronously
waiting on the service's own DOS port. The existing upstream receive helper
then consumes the authorized handle with its cleanup/error semantics intact.

Initial public-API evidence `admission-service-run.0iXjw4fG` passed WRITE,
including copying a stored full stream through the DOS handler to private RAM,
receiving it through a separate client, mounting the result and comparing its
file bytes. Cold VERIFY failed: input self-handler refusal preceded the
read-only-pool error expected by the test. This exposed a missing management
preflight, not a successful cold run. The service-wide write setting is not
the imported pool's writable state. A destination-pool preflight was added;
the corrected full regression is recorded below. Native GUI verification
remains pending.
Logs: `build-aros/typed-import-service/receive-api-{engine-build-1,client-build-1,manager-build-1,run-1}.log`.

Corrected evidence `admission-service-run.cYaHRFNw` passed WRITE and cold
read-only VERIFY (`Native authenticated receive API write=1/0 result=0`).
The separate client verifies exact received file bytes on both boots, mounted
target refusal, missing input namespace, self-handler refusal, and denial of an
ungranted UID77 caller before input resolution. The complete existing
regression and independent host full/incremental stream parser passed; runner
exit 0. Logs: `receive-api-{engine-build-2,client-build-2,run-2}.log` in the same
directory. This proves public full receive, not public incremental receive or
all receive options. No DUT, physical disks or shared hosted session were used.

Native GUI evidence `native-gui-run.pSW0hMEP`: the real Manager selected
Replication/receive, previewed and confirmed `qemu_mirror/gui-received@ui`
from `RAM:OpenZFS-gui-receive.zfs`, and displayed authenticated errno=0.
Capture `08-receive-complete.png` records the result. The private launcher
then queried the snapshot, mounted the received dataset, compared the exact
hello.txt bytes through DOS, unmounted it and exported the pool. Serial markers
`GUI authenticated receive exact DOS bytes PASS` and
`RESULT authenticated native GUI PASS` confirm the end-to-end check. Saved UI
activity/response text is included in serial.log. The first preview showed
an incomplete filename from injected typing; it was cancelled before submission
and corrected, with the capture/action history retained. QEMU exited cleanly.
Build/run logs are `receive-gui-{launcher-build-1,run-1}.log`. This is isolated
native QEMU UI evidence, not a new hosted-session or hardware test.

Public incremental receive evidence `admission-service-run.1AmGyBb5` extends
the separate-client test: after verifying full-receive bytes and mounted-target
refusal, it unmounts, copies the retained incremental stream into its own private
RAM file, receives `api-received@second`, and verifies the changed hello.txt
through a fresh DOS mount. A cold read-only boot verifies the same incremental
content. Both boots require `Native authenticated incremental receive content
PASS`; the complete regression and independent stream parser passed, runner
exit 0. Logs are `receive-api-client-build-3.log` and `receive-api-run-3.log`.

Wire API operation 60 through captured native identity and upstream
send/receive delegation policy. Validate and retain native stream file identity
and enforce caller file access; privileged daemon file access is not caller
authorization. Prevent self-served synchronous file I/O from deadlocking the
management loop. Serialize receive against mounted/in-use targets and preserve
upstream cleanup/error reporting. Then expose and visually verify the native
UI rather than advertising these private tests as a completed feature.

### Exclusive existing-empty output primitive

`oz_stream_file_open_empty_output()` checks captured caller write permission,
upgrades the resolved native lock to exclusive access, converts that same lock
to a handle, and checks EOF without trusting a wrapping 32-bit size field. It
never truncates, creates or changes ownership. Nonempty input returns EEXIST;
an existing shared lock prevents conversion with EBUSY. A successful handle
retains exclusive access until close. On conversion the supplied lock is
consumed, including a subsequent nonempty-file refusal; no stream bytes are
returned to the caller during the internal emptiness check.

Private QEMU `admission-service-run.UlFp0MR8` passed both boots and the complete
regression with `Native exclusive empty stream output result=0`: original
UID77/GID99 write denial, competing lock refusal, exclusive-handle lock refusal,
actual send-output writes, and nonempty refusal preserving exact original
bytes. Build/run logs are `send-empty-output-{build-1,run-1}.log`.
AROS posixc's current O_EXCL implementation uses a lookup before open, so it is
not used as an atomic creation guarantee. Safe new-file creation remains work,
not silently replaced by this existing-empty-file primitive.

### Authenticated send API

Operation 60 now uses the admission worker and captured original caller.
Target is a snapshot; argument is a fully qualified existing empty native
output file. Optional `from=dataset@snapshot` selects incremental send (the
parser also accepts upstream bookmark syntax, not yet covered by this API test).
All other flag/options combinations remain unsupported. The engine applies
upstream send delegation before resolving output, acquires the exclusive empty
handle above, then calls upstream dmu_send with a complete-chunk write adapter.
Failed sends retain partial output for diagnosis; no implicit truncation or
automatic retry occurs. Sending from a read-only imported pool is permitted.

Private `admission-service-run.VVmygGGP` passed full and incremental public
send-to-receive round trips, exact mounted file bytes, cold read-only persistence,
read-only-pool full send, nonempty output refusal and ungranted caller refusal.
The entire regression and upstream host stream parser passed, runner exit 0.
The host parser verifies the retained fixture streams; API-generated output is
validated by the native upstream receiver and exact DOS content, not claimed
as independently extracted host-parser output. Logs are
`send-api-{engine-build-1,client-build-1,manager-build-1,run-1}.log` under
`build-aros/typed-import-service`. Manager send visual verification remains
recorded below; its label explicitly states the existing-empty-file restriction.

Native Manager round-trip evidence `native-gui-run.mFgVKk8j`: the private
launcher prepares an empty owned output, then the real GUI sends
`qemu_mirror/stream-source@base` into it and receives that file as
`qemu_mirror/gui-received@ui`. Captures `03-send-complete.png` and
`06-roundtrip-complete.png` show authenticated errno=0 for both operations.
The post-GUI fixture verifies the snapshot, mounts it and compares exact DOS
file bytes, unmounts and exports. Serial records both authenticated workers,
`GUI authenticated receive exact DOS bytes PASS` and final GUI PASS. The
private QEMU exited cleanly. A Ctrl+A editing mistake was caught and cleared
before submitting receive; screenshots/actions preserve that history.
Logs: `send-gui-{launcher-build-1,run-1}.log`. This does not prove creation of
new output files through the API: the launcher supplies the empty file.

### Staged new-output creation

Missing send outputs now use `stream-output.c`: resolve and retain the parent
with captured caller search/write authorization; atomically create a unique
private staging directory, write there, then publish with the native DOS
no-replace rename contract. The service must be configured native root. Its
task-local creation protection is temporarily private and always restored; the
new staging directory's native owner/protection is checked before writing any
stream data. No daemon working directory or unverified pathname traversal is
used. The original caller's packed owner and default protection are applied
inside staging before publication; parent authorization is checked again.

Abort/collision removes only the owned unpublished scratch file/directory.
Caller delete protection is cleared on that scratch object when necessary,
never on a colliding or published destination. Native lock/close/metadata/
rename failures are returned. Existing-empty output retains its previously
tested behavior. Failed sends to new outputs do not publish partial streams.
Crash recovery of abandoned private staging directories, persistent-filesystem
durability and other native handlers' rename/security contracts need further
testing; these RAM-backed checks are not physical power-loss evidence.

Retained failures: `stream-create-build-{1,2,3}.log` record native packet-cast
and credential-header compile errors, corrected in build 4. Native run
`n5I3znnf` reached published metadata/collision checks but failed on the
fixture's delete-protected file; cleanup needed to respect the difference
between published caller protection and owned unpublished scratch. Build 5
corrects that. `KpdkjYdM` passed WRITE but the runner rejected a ready marker
interrupted by security tracing (`Nativ` + trace + `e authenticated...`). The
normalizer now removes complete prefixed security trace records in the checked
view only, preserving raw serial and raw failure checks. No cold pass is claimed
for that interrupted run.

Verified `admission-service-run.w65kCbSD`: staged creation tests pass in WRITE
and cold VERIFY, including denied parent write, private-until-publication,
UID77/GID88/default protection, unchanged colliding bytes, abort cleanup,
publication refusal after permission revocation, restored service defaults and
no leftover scratch entries. The separate client now sends full and incremental
streams to absent paths through operation 60, receives through operation 61,
and checks exact DOS bytes and cold persistence. Full regression/host parser
pass, runner exit 0. Logs are `send-create-api-{engine-build-1,client-build-1,
manager-build-1,run-1}.log`.

Native GUI creation proof `native-gui-run.uKQviPZ0`: the launcher explicitly
removes its prepared input and logs `output absent; send must create it before
GUI receive`. The real Manager creates the new file by sending @base, then
receives it into `gui-received@ui`. Captures `03-create-send-complete.png` and
`05-created-roundtrip-complete.png` show both errno=0 responses. The post-GUI
fixture queries the snapshot and verifies exact mounted DOS bytes before
unmount/export, logging `GUI authenticated receive exact DOS bytes PASS` and
final GUI PASS. QEMU exited cleanly. Logs are
`send-create-gui-{launcher-build-1,run-1}.log`; raw serial and UI action history
are retained in the evidence directory. This validates native RAM: output
creation, not persistent-native-filesystem or physical durability yet.

### Receive from a volume served by this process

Operation 61 now reads direct device/volume paths on its own mounted ZPL
volumes through a registered internal stream handle, avoiding a synchronous
DOS request to its own service task. Namespace routing checks the live DOS
entry identity, not just its spelling. The original captured credential is
used for upstream traversal/read authorization and native protection checks
against the actual filesystem port. Read permission is rechecked per chunk.
The exclusive handle prevents conflicting opens and unmount while active.
Only ENODEV falls back to external native DOS resolution; authorization and
other own-volume errors never trigger a more privileged fallback.

Verified `admission-service-run.uOTUqycg`: WRITE and cold VERIFY both report
`Native own-volume full/incremental receive content PASS` and original-caller
policy result=0. The client receives both saved streams directly from the
mounted artifacts dataset, then mounts the destination and compares exact
incremental file contents. Busy source, directory and missing-file errors are
checked. A captured UID77 caller survives sender exit; native file read and
directory search revocation are enforced, EOF is checked, and a live stream
prevents detach. The complete regression and offline host stream verification
pass. Logs: `own-stream-engine-build-2.log`, `own-stream-client-build-1.log`,
`own-stream-api-run-1.log`. Raw serial and frozen build inputs are retained.

This direct path currently supports device/volume names, not own-volume
assigns or soft-link expansion. Direct send output is described below.

Follow-up `admission-service-run.Vq4cADy0` adds dedicated upstream ACL-denial
coverage using the baseline's persisted `writefiles` fixture, whose ACLs were
written through upstream `zfs_setacl`. Before testing the stream adapter, it
verifies that UID77 is denied file READ_DATA and directory EXECUTE while
kcred is allowed both. Direct stream opens then return EACCES for the file
and the child below the non-searchable directory. Both WRITE and cold VERIFY
report `Native direct stream upstream ACL denial result=0`; the complete
admission regression and independent host parser pass, runner exit 0.
Logs: `own-stream-acl-build-1.log`, `own-stream-acl-run-1.log`.

### Send into an existing empty own-volume file

Operation 60 now routes direct device/volume output paths to an exclusive
internal ZPL writer before considering external DOS paths. Only ENODEV allows
external fallback. Source delegation is checked first, then caller traversal
and native/upstream write permissions. Existing nonempty files are refused
without truncation; live references conflict, read-only mounts reject writes,
each output chunk rechecks caller access, and finish synchronizes data and
notifies registered DOS watchers. Errors retain any partial existing-file
output, as with the external native-file writer.

Verified `admission-service-run.KygQtRLT`, runner exit 0: the public API sends
full and incremental streams to two empty files created on a writable ZFS
volume, receives both into another dataset, compares exact final DOS bytes,
and verifies persistence on a cold read-only boot. Nonempty refusal is followed
by successful receive of the unchanged stream. Open-file conflicts and
read-only output refusal are checked. The captured UID77 test checks write
revocation before any bytes are written, restored access, read-on-output
refusal, exclusive handles, busy unmount and persisted `written` contents.
Full regression and independent host fixture-stream parser pass. Logs:
`own-send-engine-build-{1,2}.log`, `own-send-client-build-1.log`,
`own-send-run-1.log`. This stage does not create missing own-volume files yet.

### Staged creation on own ZPL volumes

Missing own-volume send output now creates a unique `.ozsend-*` directory
under the authorized parent with caller ownership and private native
protection, then writes a payload there. All ZPL creation, write, metadata,
publication and cleanup calls retain the original caller credential; there
is no service-root fallback. Parent traversal/add-file/add-directory rights
and native write protection are checked before staging; parent write/search
authorization is checked again before publication. The caller's captured
default native protection is applied to the payload. Publication reuses
`oz_read_mount_rename` and upstream `RENAME_NOREPLACE`.

Finish takes an explicit commit/abort decision. Abort or collision removes
only this operation's scratch file/directory; a published destination is never
removed. If rename changes the namespace but its subsequent sync fails, an
object-identity check detects publication while preserving the error, rather
than trying to undo the destination. Cleanup errors are returned; upstream
unlink phase diagnostics remain in serial. Crash-abandoned stages still need
recovery policy and physical durability is not proven by these tests.

Verified `admission-service-run.7D2M0BAA`: WRITE and cold VERIFY pass the full
regression, original-caller policy and own-volume send roundtrip. The API now
creates the full-stream file from an absent destination, sends the incremental
stream into an existing empty file, receives both directly from ZFS and checks
exact final DOS contents after cold boot. UID77 tests verify denied parent
write, rejection of a root-parent path, absent final name before publication,
caller UID/GID/default protection, unchanged colliding destination identity,
abort/revoked-publication cleanup and no leftover `.ozsend-*` entries. All
offline host fixture-stream verifiers pass, runner exit 0. Logs:
`own-create-engine-build-1.log`, `own-create-client-build-1.log`,
`own-create-run-1.log`. Own-volume assigns and soft links remain unsupported.

Native Manager proof: `native-gui-run.YtJ7j6Mk`, scenario `ownsend`, starts with
the output path `OZGUIOUT:gui.zfs` absent. Manager sends @base to that ZFS file
then receives it into `qemu_mirror/gui-received@ui`, both errno=0. Screenshots
`03-send-complete.png` and `05-roundtrip-complete.png` retain the UI results.
Saved activity/response logs are dumped into raw serial. After Manager exits,
the launcher independently mounts the destination, reads 60 expected bytes
with match=1, unmounts both volumes on attempt 0, exports with errno=0, and
reports `RESULT authenticated native GUI PASS`. QEMU then exits cleanly.
Logs: `own-send-gui-launcher-build-4.log`, `own-send-gui-manager-build-1.log`,
`own-send-gui-run-4.log`.

Retained GUI failures and corrections:

- `pMjkcNOH`: launching Wanderer alongside the service's bounded temporary
  endpoint tests stopped initialization after the stream callbacks and before
  admission readiness. The launcher timed out. Starting desktop consumers only
  after service readiness avoids exposing these disposable test endpoints.
  A DOS-list/packet lock inversion is suspected, not proven fixed; production
  endpoint lifecycle under concurrent enumeration needs a focused test.
- `0rKIk0Sa`: both GUI operations returned zero, but the new asynchronous
  desktop launch inherited launcher console handles. Manager shutdown emitted
  BAD handler action 87. AROS SystemTagList explicitly owns/closes asynchronous
  handles; the fixture now passes separate NIL input/error and log output.
- `6DEVrBqf`: Manager exited cleanly and both operations succeeded, but the
  combined readback/cleanup check failed and export returned EBUSY. The old
  fixture did not separate byte comparison from unmount failure, so no exact
  data-failure claim is justified. It now logs the comparison separately and
  retries only explicitly busy unmounts, bounded to 100 attempts with Delay(1).
  The passing run needed no retry, so transient unmount contention remains a
  plausible explanation rather than a demonstrated root cause for this run.

### DOS-list contention during detach

The native AROS `RemDosEntry` contract warns that a DOS-list holder can be
waiting for filesystem service. Blocking on that list from the service task
therefore permits a lock/packet inversion. Detach now attempts the write lock
and returns EBUSY without changing the namespace if unavailable; its caller
can resume pumping queued packets before a later unmount request. Removal
still occurs under the acquired write lock, including the queued-packet
check. It neither force-closes handles nor silently retries the operation.

Bounded negative control `admission-service-run.eQjHzTzG` uses a second task
holding the DOS list for two guest seconds. The old implementation waited and
then detached, producing `observed=0 expected=16 result=5` and fixture FAIL.
This is retained evidence, not a timeout-only hang inference. With the change,
`admission-service-run.jBj64kIj` produces `observed=16 expected=16 result=0`
on WRITE and cold VERIFY. Subsequent original-caller reads on the same mount
pass before orderly unmount; the complete regression and host stream parser
pass with runner exit 0. Logs: `detach-contention-control-{build,run}-1.log`,
`detach-contention-fixed-{build,run}-1.log`.

Public API follow-up `admission-service-run.k1cNjBhp`: the separate client
holds `LockDosList(LDF_ALL|LDF_READ)` while its authenticated worker submits
unmount operation 44. It requires an engine EBUSY reply before releasing the
list, then continues full/incremental send/receive on that same mounted volume.
WRITE and cold VERIFY both pass, with complete regression and offline host
verifiers, runner exit 0. Logs: `detach-contention-api-client-build-1.log`,
`detach-contention-api-run-1.log`. This exercises the real service loop rather
than only a direct call to the detach helper.

Mount publication has the analogous lock-order requirement. Native AROS
`AddDosEntry` nests a DOS-list read lock and a separate entry write lock.
The handler now attempts the combined list/entry write lock before either
entry is added, returning EBUSY without publication on contention. Both adds
and any rollback occur while that lock is held, so readers cannot observe a
half-published device/volume pair. Disk-insert notification remains outside
the lock. The bounded negative control `admission-service-run.piWmOzld`
held the entry lock in another task and observed the old mount waiting then
succeeding (`observed=0 expected=16 result=5`, fixture FAIL). Logs:
`mount-contention-control-{build,run}-1.log`.

Fixed `admission-service-run.VmJXi8HV` passes WRITE and cold VERIFY: contended
mount returns EBUSY with no endpoint/name published, retry succeeds after the
lock is released, and a duplicate-name refusal leaves the original mount
intact. The separate API client holds the entry lock while requesting mount,
gets EBUSY, releases it, retries the same name successfully, then performs the
unmount contention and replication roundtrip tests. Complete regression and
offline verifiers pass, runner exit 0. Logs:
`mount-contention-fixed-{build,run}-1.log`,
`mount-contention-api-client-build-1.log`.

Dataset enumeration now reports each mounted filesystem's actual read-only
state instead of hard-coding `read-only mount` for writable mounts. Verified
`admission-service-run.jl5QAKyh`: WRITE reports the source artifacts mount as
read-only and the output mount as writable in the same writable pool; cold
VERIFY reports both as read-only. These are exact API row-state assertions,
not an inference from pool writability. Full regression and offline verifiers
pass, runner exit 0. Logs: `mount-state-engine-build-1.log`,
`mount-state-client-build-{1,2}.log`, `mount-state-run-1.log`.

This proves the specific detach contention behavior. It does not by itself
prove the earlier GUI initialization stall had this exact stack, or resolve
every namespace-publication/retired-endpoint concurrency issue.

Raw/encrypted/compressed/large-block/redacted streams, bookmarks as incremental
bases, recursive replication/property packages, resumable receive/send tokens,
progress/cancel controls, receive property policy, network transport, zvols,
and cross-version receiver interoperability remain separate work. The host
parser proof is not a claim that Linux received/mounted the stream. Existing
tests do not establish preservation of every metadata/property/ACL combination.
