# Native CLI stream transport

## Verified sequential-file foundation (2026-09-28)

Trusted service code can construct a sequential `zfs_file_t` writer, matching
the existing reader adapter. Both track actual transferred bytes as a logical
offset, as required by upstream send/receive. Neither pretends to support
seeking, file attributes or durable flush. Oversized callback counts fail EIO;
short transfers preserve residual counts; callback errors preserve progress;
position overflow is rejected before invoking the callback. The callback is
internal code, never supplied by a client message.

`cli-session.FJZdNahI/OpenZFSControlFileTest` passes in the complete private
QEMU platform run `cli-platform.NWFXb3YA`. It covers exact/short/error writes,
invalid callback counts, overflow, final-reference destruction and reader
offset/EOF behavior, followed by 16 control-file lifecycle cycles. Earlier
`cli-platform.hRhlS3Pw` failed the unrelated process fixture before reaching
these tests: its invocation omitted the required private stdcio.library.
That failed run remains retained, not counted as an adapter pass.

Full rebuilt service `stream-writer.xUI3b1nX` passes the complete native runner
`native-import-run.7Zezt0Y4`, including ordinary/raw send estimates, encrypted
replication/resume, crypto key lifecycle, quotas, delegation, zvol transport
and native DOS write/export/reimport. Original backing hashes match. Logs are
`build-aros/stream-writer-{build-1,qemu-1,qemu-2,native-1}.log`; the full service
build log is inside its build directory. No DUT or user hosted session was used.

## Bounded native message channel (2026-09-28)

`stream-channel.c` transfers bytes through native Exec messages, one 32 KiB
chunk outstanding per transfer. The worker owns the message and private reply
port until acknowledgment; the receiving process uses its own local I/O
callback. No callback pointer or POSIX descriptor is transmitted. Direction,
operation identity, sizes and progress are validated; partial progress and
errors are preserved and buffers wiped on release. A matching operation number
is NOT authentication: admission must supply and retain the authenticated peer
port before constructing this internal endpoint. There is no timeout-based
free/retry of an in-flight message.

Two complete native platform runs pass: `cli-platform.KOp421RV` and the final
`cli-platform.o048k7T4` using `cli-session.dovpgYyc/OpenZFSControlFileTest`.
The final fixture transfers 98323 patterned bytes in four messages in each
direction; tests a seven-byte short transfer, ENOSPC after seven bytes, wrong
operation rejection without calling I/O, impossible callback byte counts,
and zero-progress EOF/short output. The sequential-file tests and all 16
control-file lifecycle cycles also pass. Build/run logs are
`build-aros/stream-channel-{build-1,build-2,qemu-1,qemu-2}.log`.

These are two-task component tests, not cross-process authenticated admission,
POSIX file/pipe integration, or abrupt-client-exit recovery. The channel has
not been linked into the production service/client yet. Both sides of a
future operation must retain their ports until its final reply. The earlier
completed stream-adapter test ISOs were reclaimed after checking no process
held them, retaining their hashes, logs, binaries and backing disks.

## Pending-ioctl client pump (2026-09-28)

`stream-client.c` implements the client-side blocking packet lifecycle. It
publishes no callback or client descriptor, keeps its private reply port and
original packet alive, dispatches bounded stream messages against the original
packet identity, and waits for final DOS acknowledgment even after a local I/O
error. Further I/O callbacks are refused after the first error. Absent service
returns ENOENT; final service refusal is preserved. The distinct native action
is 0x4f5a4d42 (0x4f5a4d3b already belongs to mount enumeration).

`cli-session.d0wdEqR3/OpenZFSControlFileTest` passes the complete platform run
`cli-platform.N8l4IwUv`. A private test server exercises the actual SendPkt,
stream callbacks and ReplyPkt sequence: 65559 patterned bytes in three messages
in both directions, ENOSPC after seven bytes, final EACCES without calling I/O,
and absent-server refusal. All prior channel, file and control lifecycle tests
also pass. Logs: `build-aros/stream-client-{build-1,qemu-1}.log`.

This initial test is a two-task private protocol fixture, not an authenticated
production server or real POSIX fd/pipeline test. The worker integration below
subsequently connects this helper to control-client for send estimates only.
The new action alone grants no stream access or upstream ioctl authority.
Retained runtime headers and sources accompany the test executable. Completed
channel-test ISOs were reclaimed with pre-removal hashes in
`build-aros/reclaimed-test-isos-20260928-stream-client.sha256`; their logs,
binaries and other evidence remain.

## Authenticated worker integration (2026-09-28)

The admission service now captures native packet identity and a private input
snapshot, retains the authenticated session, and starts a bounded worker for
the new ioctl action. A private completion port wakes the owning service task;
that task joins the worker, copies the result, replies to the original packet
and releases resources. Busy checks include active workers, so engine teardown
cannot race their retained references. The service main loop remains available
for DOS requests. Up to eight jobs can be retained; failure to start a worker
releases all resources and returns an error without a fallback retry.

Only SEND_SPACE is admitted on this path for now. It neither mutates the
management mount registry nor exchanges bytes. Every command still passes the
existing envelope/schema and upstream policy checks. Client code routes actual
send estimates through the new pump with a callback that refuses unexpected
I/O; it does not fall back to the old synchronous action on failure.

Service `stream-worker.GASRzjnG` and real commands `cli-link.1vPvXWX5` pass the
complete native regression `native-import-run.JYRKKaSE`, runner exit zero and
both original backing hashes unchanged. Actual ordinary, locked-raw,
raw-resume-token and raw-incremental send estimates all pass through the worker.
The broader crypto, replication, accounting, delegation, zvol and DOS lifecycle
suite also passes. Logs: the service build directory, plus
`build-aros/stream-worker-{cli-build-1,native-1}.log`. No new independent zdb
traversal, concurrent-stream or abrupt-client-termination acceptance is claimed.

This retains the existing native blocking-request contract: the client must
keep its task, packet and buffers alive until final reply. Forced task removal
while a request is in flight is not made safe merely by retaining credentials.
Byte-stream worker admission, authenticated peer lifetime, client fd mapping,
and mount-registry concurrency restrictions still need implementation/tests.

## Actual upstream CLI raw send (2026-09-28)

SEND_NEW now has a trusted worker-only entry point. The service constructs a
sequential writer bound to the original packet's reply port, registers it in
the retained authenticated session, and substitutes that service descriptor
into a private copy of the packed nvlist. Ordinary ioctl admission still
refuses SEND_NEW; a client cannot select a raw device or another service file
by placing its number in the fd field. Upstream validates send options and
delegated policy before encoding stream bytes.

The CLI duplicates its local output fd for the operation and services bounded
write requests with native POSIX write, preserving EINTR/short/error behavior.
It closes only the duplicate after final acknowledgment. Mount/topology
administration is refused while a send worker is active; queued management
mutations wait. DOS and zvol data I/O continue. Finer concurrent administration
and progress-query support require separate tests.

Service `cli-send-stream.HK6HvWNI`, real commands `cli-link.T6IUCw5l` and full
native run `native-import-run.94VQeoFk` pass (runner exit zero; original disk
hashes unchanged). Actual `zfs send -w qemu_mirror/gui-crypt@raw` writes a RAM
file while the source key is unloaded. The existing management receiver
consumes those CLI-produced bytes, remains locked, accepts the original key,
and exposes both the 512-byte payload and the exact 1 MiB patterned file plus
EOF through native DOS. Existing API-produced raw streams still exercise the
separate interrupted/resumed receive tests. The dedicated CLI raw stream
marker is now required by the runner. Logs: service build directory and
`build-aros/cli-send-stream-{cli-build-1,native-1}.log`.

This proves one real raw full-send path to a RAM file, not CLI receive, pipes,
plain/incremental CLI byte streams, send output on our own mounted filesystem,
negative delegated CLI send cases or abrupt task termination. The older API
replication and dry-run CLI tests must not be substituted for those gates.

## Actual upstream CLI raw receive (2026-09-28)

RECV_NEW now uses the same authenticated worker with a sequential reader.
The client's input descriptor is duplicated and retained locally; the service
substitutes its independently retained session descriptor in `input_fd`.
Direction must match the command. Ordinary synchronous admission still refuses
this ioctl. Upstream owns stream parsing, receive policy, options, properties,
encryption and dataset transactions. Other management mutations are excluded
while either send or receive is active; native data I/O remains available.

Native child execution explicitly inherits Input() with NP_CloseInput=FALSE,
so the caller retains ownership of redirected stdin. The fixture opens its
private stream, temporarily selects that input, invokes the actual zfs binary,
restores input and closes its own handle after synchronous child completion.

Service `cli-receive-stream.BTPa7Sui`, real commands `cli-link.6pJx13qB` and
`native-import-run.xoXWO8Ui` pass the complete regression (runner exit zero and
both original backing hashes unchanged). Actual `zfs send -w` produces the
stream from a locked source; actual `zfs receive -u` consumes it. The receiver
is locked, loads the original key, and exposes the exact 512-byte payload and
1 MiB patterned file plus EOF through native DOS. The runner requires the
updated marker with CLI-receive=1, not the earlier API-receive marker. Logs:
service build directory and `build-aros/cli-receive-stream-{cli-build-1,native-1}.log`.

This verifies a sequential file-based CLI round trip, NOT a simultaneous pipe.
The legacy RECV ioctl used by some plain streams is still unadmitted; raw
streams select upstream's newer packed interface. Plain streams, incremental
and resumed CLI streams, concurrent pipeline startup queries, own-volume
stream storage and negative CLI permission/error paths still need tests.
Forced client removal during a pending packet remains outside the blocking
request lifetime contract. No new independent zdb or cold-boot claim is added.

## Plain own-volume CLI stream (2026-09-28)

The next gate uses actual `zfs send` and `zfs receive -u`, storing their
intermediate stream on OZSTREAM:, a dataset served by the same native engine.
AROS now selects upstream's packed RECV_NEW interface for ordinary receives
without enabling raw or resumable flags. The CLI link probe compiles the current
libzfs_core source instead of retaining a stale cached core object.

Initial runs nyf2o8wG and gxPdzOTB failed before sending: upstream correctly
refused an output advertised as a terminal. Investigation found our DOS
handler assigning a port to fh_Port, which AROS aliases to fh_Interactive.
Both ordinary open and OpenFromLock now mark regular files noninteractive;
the upstream terminal safeguard is unchanged.

Verified native-import-run.ZBEIiWoR completed with service
file-interactivity.3lFXM5Vi and CLI cli-link.QSpWoFqB. Plain send/receive succeeded,
the target reported encryption=off, and native DOS read the exact patterned
1 MiB payload plus EOF. The existing locked-source raw CLI round trip and
complete regression also passed; original fixture hashes remained unchanged.
See build-aros/file-interactivity-native-1.log and the registered run evidence.
Repeat BErUdqLc also passed. Final run IGwMTwtc adds and passes the explicit
OpenFromLock/noninteractive assertion in service-command-test.c, with the
same complete regression and original backing hashes passing. The separate
dos-handler-test.c assertion is source coverage only in these runs, since
this runner does not execute that standalone test.
This supersedes the earlier unverified plain/own-volume limitations, not the
remaining pipeline, incremental/resumed CLI, failure-lifetime or persistence
requirements. No new hardware or independent zdb result is claimed.

## Remaining end-to-end bridge

Native run IAz8QwDg additionally passes actual upstream `zfs send -i` and
`zfs receive -u` through an own-volume intermediate file. It receives a base
snapshot, adds a 128 KiB patterned source file, snapshots again, receives the
incremental stream, and verifies every byte and EOF through DOS. The full
regression and immutable backing hashes also pass. Evidence is in
build-aros/cli-incremental-native-1.log and the registered run directory.
This is sequential plain incremental CLI coverage, not raw incremental CLI,
resumption, concurrent pipes or cold-boot qualification.

Native run XBFyTf7J adds same-boot plain CLI resumption: truncate the actual
full stream at 512 KiB, require `zfs receive -s -u` to exit 1, query its receive
token, run actual `zfs send -t`, and receive the continuation successfully.
The receive token clears and DOS reads the exact original 1 MiB payload plus
EOF. All stream files reside on the service's own mounted volume. The entire
regression and original backing hashes pass; see cli-resumed-native-1.log.
This supersedes only the plain same-boot CLI resumption limitation. Raw CLI
resumption, process-loss cases and cross-boot CLI resumption remain distinct.

Native run uEWoKYVk adds same-boot raw CLI resumption. The truncated input now
comes from actual `zfs send -w` output. A separate CLI receiver exits 1 for the
cut stream, provides its token, and completes with actual `zfs send -t` plus
`zfs receive -s -u`. The source is still locked; the receiver reports
aes-256-gcm and unavailable keys until the original key is loaded. Native DOS
then verifies exact 1 MiB contents and EOF, and the resume token is cleared.
The earlier API/delegation tests remain alongside this additional CLI test.
The complete regression and original backing hashes pass; see
build-aros/cli-raw-resumed-native-1.log and its registered run evidence.
Raw same-boot CLI resume is therefore verified; cross-boot CLI resume,
concurrent pipelines and forced-process-loss behavior are not implied.

Actual upstream CLI raw send/receive now have the file-based verification
above. The first concurrent plain CLI pipeline is verified below. Client
POSIX descriptors are not service/session descriptors and must never be
forwarded as though they were. Remaining receive interfaces need equivalent
authenticated transport and upstream semantics, without widening admission
to client-selected service descriptors.

Ordinary ioctls still execute synchronously in the service's main task;
send estimates now exercise the retained worker/completion path. Byte-stream
operations must use that path: waiting in the main task for client I/O against
its own filesystem would deadlock. Client replies and
buffers must remain alive until final completion; timeout cannot justify
freeing an in-flight message or retrying a mutation. Concurrent send/receive
must also avoid a single-worker producer/consumer deadlock.

Acceptance must exercise actual zfs binaries, binary streams larger than the
transport chunk size, short/error/EOF handling, ordinary and encrypted raw
streams, incremental and resumed transfer, file output/input on the service's
own mounted dataset, and simultaneous producer/consumer operation. Verify
receiver contents, session cleanup, authorization refusals and fresh-boot
persistence independently of command exit status. Do not replace these gates
with the already-passing management API tests.

## Concurrent native CLI pipeline (2026-09-28)

Test history (all retained and registered): lVI7fnCT lacked a mounted PIPE:
handler; h37hTHIy mounted it but the frozen isatty classified pipes as terminals.
The private cli-isatty.wSnzlNPf overlay recompiles ADT's current pipe-aware
isatty against the frozen ABI without modifying any installed SDK. ky8EBT4f
then failed its receiver; cUos07K2 captured the reason before waiting for the
sender: the parent dataset query was rejected as busy during an active send.

Admission now routes a small explicit read-only ioctl allowlist through retained
workers while other ioctls are active. Classification uses the private copied
request, with the existing credential/session and wire validation unchanged.
Queries do not block the DOS owner task on dataset locks. Non-query operations
and deferred management stay excluded until outstanding workers finish. This
is not blanket admission of arbitrary commands or concurrent mount changes.

Native run PUKi4YuF, service pipeline-query.ZvtOS0wZ, CLI cli-link.QSpWoFqB,
and the above POSIX overlay passed the complete regression and immutable backing
hashes. An asynchronous native zfs sender and synchronous zfs receiver use a
real PIPE: without an intermediate stream file. Both exit zero, and DOS reads
the receiver's exact 1 MiB patterned payload plus EOF. Sender closes its pipe
endpoint before signaling completion. Evidence: cli-pipeline-native-5.log.

This proves one concurrent plain pipeline, not raw/incremental/resumed pipes,
repeated stress, unrelated-user concurrent clients, negative admission races,
pipe consumer failure or forced process removal. Those remain separate gates.

Native run dDYC82iJ extends this to eight complete same-boot pipeline cycles,
reusing the pipe name and recreating the receive dataset. Each sender/receiver
pair exits zero and every received 1 MiB payload plus EOF is checked through
DOS before unmount/destroy. The full regression and original backing hashes
pass (cli-pipeline-repeat-native-1.log). This is bounded repeated lifecycle
coverage, not a leak-free or endurance claim. Consumer-failure behavior remains
unverified: the AROS PIPE handler's reader-close/write-wait semantics need a
separate test and must not be inferred from successful EOF handling.

## Reader-close failure gate (currently failing)

Native run tzUThMLE reads 8192 bytes from actual zfs send, closes its PIPE:
reader, and observes no sender completion after 50 ticks. It then reopens the
same pipe and drains 1097536 bytes to EOF, safely allowing the sender to finish
and release its live completion storage. Sender exits zero. The new required
reader-close gate therefore fails (runner exit 1), after eight successful
ordinary pipelines. See cli-reader-close-native-1.log and registered evidence.

This is an observed native named-pipe compatibility gap, not a timeout-based
claim of an OpenZFS deadlock. The handler's CheckWaiting processes pending
writes when buffer space becomes available and has writer-close EOF handling,
but this path does not fail writes when a connected reader closes. A fix must
define disconnected-consumer semantics without silently breaking intentional
named-pipe reopen behavior. The recovery drain is diagnostic only, not a
production substitute for cancellation/error delivery. This gate is not yet
passed and full pipeline failure handling is not complete.

## Private opt-in disconnect experiment

build-pipe-policy.sh copies ADT's handler sources to a private build and applies
pipe-disconnect.patch; shared AROS sources and installed SDKs remain unchanged.
The experimental scalar Startup=1 policy marks reader-close as terminal,
rejects reopening that live endpoint, fails queued writes with a native
invalid-endpoint error and discards residual data when the last endpoint closes.
Startup=0 follows the legacy branch. This private policy is not an approved
AROS ABI or complete POSIX EPIPE/SIGPIPE implementation.

First run coAr1h1Z unblocked the sender but aborted inside upstream error
formatting: the bridge had passed an unmapped AROS errno (MAX_ERRNO+IoErr).
control-client.c now normalizes such stream errors to EIO while preserving
ordinary errno values. Native run hUrFEiTp with CLI cli-link.8yPb0rfy,
private handler pipe-policy.TbGdg4bJ and service pipeline-query.ZvtOS0wZ
passes eight normal pipelines and reader-close: after 8192 bytes, sender exits
normally with status1, no diagnostic drain or abort. The remaining complete
regression passes and original backing hashes are unchanged. Logs are
cli-reader-policy-native-2.log; all failures/passing evidence are registered.
The scripted handler rebuild matches prototype vsu3xhRR byte-for-byte.

Default-policy compatibility must still be runtime-tested with this patched
binary. Proper errno/signaling semantics, partial-write contracts and any
production integration require further work; the private test success does
not mean ordinary deployed PIPE: cancellation has been fixed.

Default-policy compatibility is now runtime-verified in native run gGTdNrhl:
the same patched handler binary uses a captured Startup=0 mount configuration.
After eight successful pipelines the reader-close probe must reopen/drain the
pipe and observe sender exit0; early sender failure would fail this test mode.
It passes, followed by the entire regression and unchanged backing hashes.
See cli-reader-policy-legacy-native-1.log. The explicit
AROS_TEST_PIPE_POLICY_LEGACY=1 test mode requires a supplied private handler and
a different result marker; it cannot satisfy the strict disconnect gate.
This closes the default-policy runtime check, not the outstanding production
ABI, POSIX error/signaling, partial-write or endpoint stress requirements.

## Actual CLI encrypted resume across boots (2026-09-28)

Writer native-import-run.yeXr757I uses actual `zfs send -w`, cuts the stream
at 512 KiB, and requires `zfs receive -s -u` to exit 1 with a retained token.
The source key remains unloaded and the pool is synced. The full native
regression and immutable original backing checks pass (runner exit 0).

Fresh QEMU boot native-import-run.UsxzWkiM attaches new child overlays of
that writer, retrieves the persisted token and runs actual `zfs send -t`
and `zfs receive -s -u`. It verifies the token is cleared, the receiver key
is initially unavailable, the original key unlocks it, and all 1 MiB plus
EOF match through the native DOS handler. The cold regression exits 0;
the retained writer and original backings remain unchanged. Both evidence
sets are registered under the development test archive. Logs: cli-cold-resume-writer-1.log and
cli-cold-resume-reader-2.log.

The first reader invocation was rejected before boot because it mistakenly
included the writer-only AROS_TEST_KEEP_CLI=1 flag. Its empty preflight log
cli-cold-resume-reader-1.log and frozen runner are retained; it is not a guest
failure or pass. The corrected invocation omits that flag.

This replaces manager-API calls only in the retained/cold resume fixture;
other manager/API raw-resume coverage remains. CLI=1 result gates prevent
an older API-produced fixture from satisfying this CLI persistence test.
The service is pipeline-query.ZvtOS0wZ, CLI cli-link.8yPb0rfy, with the private
pipe-policy.TbGdg4bJ and cli-isatty.wSnzlNPf overlays. This proves this native
CLI cold-resume path, not release integration, hardware validation, or full
POSIX pipe cancellation. Neither the DUT nor the user hosted session was used.

## Partial write on reader disconnect (2026-09-28)

The private policy originally discarded partial progress when replying to a
blocked writer after reader-close. Native red run yLNqak8z submitted 65536
bytes, verified the reader consumed 256, closed the reader and received -1
instead of a positive short count. The test completed with runner exit 1;
its evidence is registered and retained in pipe-partial-red-native-1.log.

The patch now returns requested-minus-remaining bytes when positive, with no
error on that partial result. A write with no transferred bytes still fails
with ERROR_INVALID_LOCK. This does not invent a DOS broken-pipe error or claim
POSIX EPIPE/SIGPIPE semantics. It corrects byte accounting inside the private
handler policy only; pipe taps and broader endpoint stress need separate work.

Handler pipe-policy.EzbhH6gz is the rebuilt candidate. The new native packet
test retains its buffer and packet until actual completion, verifies read bytes,
requires a positive short count, and requires the next write to fail. The
strict native runner requires its distinct PASS marker; the legacy-policy
mode skips this terminal-disconnect-specific test.

Native run 8hvjOQfy returns 4352 accepted bytes (256 read plus buffered bytes),
error 0, followed by -1/ERROR_INVALID_LOCK on the next write. It then passes
eight concurrent CLI pipelines, sender failure on reader-close, the remaining
full native regression and unchanged original backing hashes, runner exit 0.
Log: pipe-partial-native-1.log; registered under the development test archive. The newly rebuilt
handler's default-policy behavior has not been rerun after this change; prior
default-policy evidence gGTdNrhl applies to the preceding handler binary.

Follow-up iLk2VnHn closes that default-policy gap for EzbhH6gz. With captured
Startup=0, eight normal pipelines pass. Reader-close leaves the writer pending;
a replacement reader drains 1097536 bytes to EOF and the sender exits 0.
The complete native regression then exits 0 and both original backing hashes
match. See pipe-partial-legacy-native-1.log and registered the development test archive evidence.
This establishes strict/default policy compatibility for the current private
binary, not production policy/ABI approval or POSIX EPIPE/SIGPIPE semantics.
