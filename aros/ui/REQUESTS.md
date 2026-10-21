# Admission requests and in-flight workers

Advanced / requests (operation 76, wire key `advanced.jobs`) uses the native
authenticated admission service, not a mock job catalogue. The query lists
retained admission tickets owned by the current native UID; the system
identity can see all retained tickets. Each row contains its epoch:number,
operation, owner, queued/completed state and completed result status. It does
not disclose command arguments, paths, key material or request payloads.

`OZ_ADMISSION_LIST` is an additive native packet action. The service takes a
snapshot on its owner task, authenticates the packet before reading tickets,
and returns at most eight admission slots plus eight in-flight ioctl workers.
Released tickets and drained workers disappear. Worker rows expose only an
immutable request identity, ioctl number, owner and operation category, not
concurrently changing worker result fields.
The query is permitted during a stream because it does not alter topology or
storage. It does not enumerate upstream background activities themselves.
Scrub/resilver progress belongs to pool status; durable job history remains
unfinished. Queued/completed state is not a
percentage, and the Manager deliberately omits generic numeric progress fields.

The Manager executes the query in its joined worker with inherited identity.
All request fields and flags, including dry-run, must be empty/off. It neither
submits a new admission job for the query nor releases the tickets it lists.

## Queued cancellation (qualification in progress)

Advanced / cancel-job (77) accepts an `epoch:number` admission ticket and
confirmation, with no other fields or flags. It calls `OZ_ADMISSION_CANCEL`
through the native worker. Fresh packet authentication allows the submitting
UID or system identity; foreign callers get EACCES. Only a queued request is
cancelled. Its retained result is `OZ_CANCELLED`, and the service will not
dispatch it. Repeated/completed cancellation returns EALREADY. Active ioctl
workers return EBUSY; this is not interruption of scrub, send, receive, or
other running storage operations. Unknown/stale tickets return ENOENT.
Cancelled tickets stay queryable until their normal release. The Manager
still permits only one outstanding action per window; another window may
inspect/cancel a queued request. There is no durable cancellation history.

The cancellation core, packet action, client, and Manager compile. Retained
owner/admin cancellation and foreign-identity denial checks passed in the
writable phase of `admission-service-run.bOXawOV5`. The overall test failed
in its readonly phase: the non-full-dispatcher harness returned EPERM where
the harness expects EROFS. `admission-service-run.9rFKbfdd` retains an earlier
timeout using an old client. Full-dispatcher rerun, deterministic queued
transport tests, and graphical cancellation qualification remain pending.

Full-dispatcher follow-up `admission-service-run.caTuP4ix` passes the retained
cancellation/credential checks on both writable and cold readonly boots. The
earlier EPERM/EROFS mismatch is gone. The broader readonly harness then stops
at `Native send/receive VERIFY cold resumable receive errno=5`, before its
service loop. This is still an overall failed suite, not cancellation or full
port acceptance. Current `service-command-test.c` also adds completed-ticket,
stale-epoch, foreign-owner and released-ticket cancellation assertions;
those packet-level additions have not yet been run.

`native-import-run.CHg8A5In` now passes those packet boundaries against the
normal full-dispatcher service, plus the actual Manager worker's rejection of
malformed IDs (missing delimiter, zero, extra components, negative, overflow,
whitespace) and a completed ticket. The selected writable mirror/DOS/export/
reimport suite exits zero and original member hashes remain unchanged
(`cancel-transport-native-1.log`). This is not yet proof of successful queued
cancellation over the packet transport or a graphical cancellation test.

The focused EXEC cold fixture had also assumed that its client performed the
full admission client's receive-resume and abort calls. It does not. The
fixture now verifies those receives remain pending for EXEC; the full suite
still requires completed/aborted state. Failure run `admission-service-run.9UCsZFSm`
records the first corrected pending check passing before the analogous abort
expectation failed. The complete focused correction is under retest.

The corrected focused suite passes both boots and independent offline member
verification in `admission-service-run.45tQxaDi` (host exit zero,
`queued-cancel-native-5.log`). This preserves the full admission suite's
different client-side mutation requirements rather than treating skipped
client actions as storage failures.

## Deterministic queued test

`AROS_TEST_QUEUE_HOLD=1` compiles a private admission hook that holds only
operation 41 on `qemu_mirror/engine-policy` with argument
`oz:cancel=must-not-execute`. Normal builds omit it. Staging rejects the
`oz_test_admission_hold` symbol; rejection of the actual instrumented build is
recorded in `cancel-held-stage-refusal-1.log` (exit 2, no bundle).

The corresponding native client first proves EAGAIN, uses the actual Manager
project worker to cancel, checks OZ_CANCELLED and the cancelled list row,
checks repeated cancellation is EALREADY, releases the ticket, checks ENOENT,
then verifies the property was not written using upstream `zfs get`.
`native-import-run.qrwOgUJS` has reached that passing marker; its wider
regression subsequently completed with host exit zero and both original
backing hashes unchanged (`cancel-held-native-1.log`). This test uses a
private scheduling fixture, not a release build or a GUI screenshot.

Graphical follow-up `native-import-run.C7AsQ4ih` uses `AROS_TEST_QUEUE_GUI=1`
to open the actual Manager with the designated ticket queued. Navigation to
Advanced/cancel-job, ticket entry, dry-run off, explicit confirmation and
successful service response are captured in `cancel-confirm.png` and
`cancel-result.png`. After normal window close the test checks the cancelled
result, list row, release and absent property. The wider selected mirror
regression and final Manager close finish with exit zero and unchanged
backing hashes (`cancel-gui-native-1.log`). QMP actions and framebuffer
captures are retained. This remains a private scheduling fixture.

The library builder also rejects the actual queue-instrumented linked object
before creating a shared candidate (`cancel-held-library-refusal-1.log`,
exit 2). The normal cancellation service builds as shared candidate
`shared-engine.ageqebH4`; package acceptance is separate.

Capability follow-up: the native service advertises operation 76
(`advanced.jobs`) and 77 (`advanced.cancel-job`) whenever its engine is ready,
including read-only mode. They change request metadata, not pool data.
`service-command-test.c` checks both capability bits and names on the wire,
alongside rejection of the older API version. The capabilities describe the
authenticated admission control path; they do not turn the legacy broker
into an unauthenticated cancellation endpoint.
`native-import-run.Boa6GdtJ` passes the wire assertions and selected native
CLI/device/mirror regression with exit zero and unchanged original backing
hashes (`request-capability-native-1.log`). This tests the normal monolithic
service in writable mode, not a refreshed shared bundle or a separate
read-only capability response.

Native service build: `source-engine.P2gN7Bsv/request-list-service`, log
`request-list-build-1.log`; rebuilt Manager log `request-list-ui-1.log`.
`native-import-run.X17mmwWi` passes retained completed-ticket visibility,
ordinary-user exclusion and release removal, followed by the full selected
mirror regression with host exit zero and unchanged original backing hashes
(`request-list-native-1.log`). This initial run predates the added Manager
worker assertion and graphical fixture.

Graphical `native-import-run.hKyj8gSf/requests-result.png` shows the actual
Manager query returning one retained completed ticket, operation 41, status 0.
`requests-ready.png` records the request with flags off; `requests-before.png`
and `requests-category.png` retain navigation provenance. The broader
graphical regression is separate from this screenshot's visual proof.
The completed graphical run also passes the actual Manager worker assertion,
foreign-identity filtering and release-removal checks, then the full selected
mirror regression. Both Manager windows close normally, the host exits zero,
and both original backing hashes match (`request-list-gui-1.log`).

Live-worker regression `native-import-run.hA5jOYYM` uses a private
`AROS_TEST_WAIT_PENDING=1` service to hold a real wait pending deterministically.
It verifies the wait is listed in-flight and its identity disappears after
the worker drains, alongside the retained-ticket checks and full selected
writable mirror import/DOS write/export/reimport regression. Both original
backing hashes match (`live-request-native-1.log`). This is an instrumented
test build, not a distribution artifact.

The expanded `native-import-run.VOXzByl0` additionally changes the query caller
to ordinary user 77 while the system-owned wait remains pending and verifies
that its identity is absent. It restores the system identity, stops the scrub,
drains the wait and completes the selected mirror regression with unchanged
backing hashes (`live-request-identity-native-1.log`). This covers cross-user
exclusion for both retained tickets and a live wait worker, not every worker
category or the full space of simultaneous identities.

The uninstrumented `live-request-release-service` build also passes the full
selected mirror regression in `native-import-run.dOmaOZgI`, with host exit zero
and both original backing hashes unchanged (`live-request-release-native-1.log`).
Packaging negative fixtures pass in `stage-policy.F4FPiVTA`. A separate staging
attempt against the real wait-injected service is rejected for its linker
wrappers (`live-request-injected-stage-refusal-1.log`); no bundle is produced.
