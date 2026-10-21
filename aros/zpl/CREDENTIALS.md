# Native credentials: current boundary and remaining work

## Management transport lifetime contract (2026-09-21)

Source audit at OpenZFS HEAD 52a601d, against the local AROS sources:
`security_packetio.c:GetPktTask` obtains `dp_Port->mp_SigTask` for a
PA_SIGNAL port. `GetPktOwner` passes that task to `GetTaskExtOwner`, which
clones its owner under TaskOwnerSem. The lock protects the security task
record and clone, not the caller's packet or reply-port allocation. There is
no additional Exec-message sender identity in this path. This is the native
cooperative DOS packet contract, not protection against arbitrary writes in
AROS's shared address space.

Our existing cross_task_capture regression in packet-cred-test.c observes
the relevant ordering: a logged-in worker sends a real StandardPacket and
waits; the receiver captures and holds the immutable credential before
ReplyPkt; the worker then logs out, deletes its reply port and exits; after
pthread_join the receiver checks the retained identity and native policy.
That test supports post-reply snapshot lifetime. It does NOT support
capturing identity after the sender has timed out and destroyed its port.

The extended native regression also destroys the packet-security context
before checking the separately held credential's UID 77, primary GID 88,
supplementary group 99 and denied administrative policy. Isolated QEMU run
`build-aros/qemu-run.9ODbGhAb` passes that new mandatory gate and all five
mirror boots with the private security/ATA/DOS overlays. Cold readonly and
both sole-member boots issue zero writes; original backing hashes remain
unchanged, and both independent Linux zdb checks pass. Build and harness
logs are `build-aros/credential-retention-{build,qemu}.log`. This proves
retained snapshot ownership, not authenticated management admission.

The current management v2 protocol has different ownership semantics:
oz_submit transfers the allocation to the broker; oz_poll times out after
three seconds without cancelling execution; oz_client_close removes and
deletes the reply port, even while a request is outstanding. The broker
looks up reply_name only when delivering the result. Consequently neither
looking up that name nor adding a raw task/reply-port pointer establishes
a credential lifetime suitable for production authorization. Do not call
secGetTaskExtOwner while holding Forbid around FindPort: the security query
can wait on a semaphore, and Forbid is not an object reference protocol.

The production path therefore needs a separate, explicit admission phase:

1. A DOS packet sender keeps its packet, reply port and task alive until the
   admission reply; the service captures native identity before replying.
   Use the native security API and the existing explicit UID/GID conversion.
2. The service copies and validates request data, retains a credential
   snapshot, and evaluates upstream operation/delegation policy. A client
   supplied UID, confirmation flag or job identifier is not authorization.
3. Long operations return an admitted job identity rather than retaining
   sender memory. Job execution owns its request and credential; status,
   result and cancellation requests require their own caller checks.
4. A UI worker may block in admission while the UI remains responsive, but
   closing a window must not delete that worker's outstanding packet/port.
   Worker shutdown needs an acknowledged drain, not the current timeout
   cleanup. Service absence can be reported before sending; post-send
   timeout means unknown outcome, never automatic mutation retry.

Required native tests before enabling production management mutations:
distinct root/non-root callers; nobody and unavailable/violated security
denial; supplementary groups; sender logout after admission; delayed
admission and UI close; service shutdown/drain; late results; job queries
by a different identity; upstream delegated permissions; exactly-once
admission under retries. Existing fixture IPC and DOS credential tests do
not substitute for these transport and policy tests. This is an audited
implementation contract, not a claim that the new transport is present.

## MuFS filesystem-author queries (2026-09-20)

DOS discovery metadata follow-up (2026-09-21): each mount now owns an embedded
FileSysStartupMsg and DosEnvec with the actual ZFS disk type 0x5a465301.
The environment table extends through DE_DOSTYPE; no physical device, unit,
flags or geometry is invented for a logical dataset. The DLT_DEVICE startup
points to this metadata, which survives detach with the retained endpoint.
This supplies discovery metadata only, not security-server registration.

The first test, qemu-run.luRyB9pO, incorrectly assumed GetDeviceProc returns
a DLT_DEVICE node. It searches LDF_ALL and may return the DLT_VOLUME entry.
The revised test resolves LDF_DEVICES explicitly like security.library,
then checks the metadata before detach and across three retained endpoints.
The test owner keeps these entries alive until clients release and join;
this is not an assertion that arbitrary DOS-list pointers are safe to cache.

Corrected qemu-run.qTh6H58k passes all five boots with real security/ATA/DOS
overlays. Metadata/capability and retained-remount tests pass for authenticated
and public readonly mounts, including cold intact and sole-member imports.
Readonly guests issue zero writes; backing hashes and both independent zdb
No leaks checks pass. Logs: mufs-device-entry-{qemu-pool-test-build,
qemu-import-test-build,qemu}.log; retain mufs-metadata-* for the failed test.

Native endpoint retirement regression (2026-09-21): `qemu-run.y9vFyKK2`
passes all five boots with the private security/ATA/DOS overlays. Each public
readonly and authenticated DOS client retains three real DeviceProc references
across three detach/remount cycles. Every retired port answers filesystem,
current-volume and both MuFS capability queries with DEVICE_NOT_MOUNTED;
the replacement has a distinct port and reports the correct capability and
API version. Authenticated checks occur after logout, so discovery still
requires no file-access grant. The owner pumps all retired endpoints until
the client releases its references and joins, then destroys them. Existing
read counters, zero readonly disk writes, disk hashes and zdb gates still pass.
Logs: `build-aros/mufs-detach-{pool-build,import-build,qemu}.log`.

This verifies the handler's bounded-client retirement contract, not automatic
production reclamation. The management service still retains endpoints and
has eight lifetime mount slots. security.library rescan completion and other
clients' cached ports cannot be inferred from these joined test clients.

The AROS/ADT header defines ACTION_IS_SECFS (2999) and
ACTION_GET_SECFS_VERSION (2998), in addition to ACTION_SET_OWNER.
The handler now answers both without requiring login: authenticated mounts
report ownership enforcement, while the explicitly public readonly fixture
reports false. Both implement the query protocol version secFS_API_VERSION
(2). Authenticated mounts continue to fail closed when identity is missing;
reporting capability is not permission to read files. Detached endpoints
still reject packets before these queries with DEVICE_NOT_MOUNTED.

This follows the native AFS MuFS integration's packet identity, access and
property API pattern, but deliberately does not copy its inactive-security
fallback. AFS gates activation on its MuFS disk type and secIsConfigured;
OpenZFS must retain its own disk format and distinguish unconfigured desktop
policy from a security violation before changing its fail-closed behavior.

Do not confuse native MuFS-aware enforcement with the legacy proxy path:
AROS/rom/security/security_enforce.c currently has an empty packet dispatch
table, documented as unported. That proxy cannot replace the OpenZFS handler's
actual caller checks and upstream ACL enforcement. Volume discovery/readiness
and quarantine lifecycle remain unresolved as described below; these queries
do not solve them or claim complete multi-user support.

`build-aros/qemu-run.A3fomglD/` passes all four strict boots with actual DOS
query packets before login and after logout, diagnostic false/authenticated
true results, unchanged file-denial checks, ExAll fallback coverage, cold
mirror and both sole-member imports, zero cold writes and unchanged backing
hashes. Logs: dos-mufs-link.log and dos-mufs-qemu.log in build-aros.

## API audit: volume lifecycle and configuration (2026-09-20)

Management caller audit (2026-09-21): security.library also exposes
secGetTaskExtOwner(task), which clones the task's extended identity under
TaskOwnerSem and returns NULL on missing identity/security violation. This
is suitable for capturing a *known* task's credentials; it does not establish
which task sent an arbitrary Exec message. Management API v2 routes replies
by a client-provided named port and deliberately does not retain mn_ReplyPort.
The broker currently dispatches without capturing an authenticated caller.
Do not infer authority from that name, UI confirmation, or the service task's
own identity. Production mutations and delegated policy need an explicit
caller/lifetime contract before the fixture restriction can be removed.
DOS packet credentials use the separate documented secGetPktOwner contract.

Further source audit (2026-09-21): secAccess_Control releases VolumesSem
before IsAllowed uses its selected secVolume, whereas FreeVolumes frees
native records under the same semaphore on rescan. Private candidate
`aros/patches/security-access-volume-lifetime.patch` retains the shared lock
through policy evaluation. Private full-library/BSP build and five-boot native
regression pLXIlN9n pass; see aros/patches/README.md for evidence. That suite
does not yet exercise discovered OpenZFS records during rescan. This must
precede adding asynchronous rendezvous notifications, and does not by itself
solve discovery or stale handler-port lifetime.

Rechecked local AROS and ADT sources: `compiler/include/libraries/security.h`
and `rom/security/security.conf` are byte-identical. Packet integration uses
the real API, not a POSIX permission approximation: capture sender/defaults
before ReplyPkt, retain/free the extended owner, use secAccess_Control with
the actual filesystem MsgPort, and use secGetRelationshipA for property
permission. Native root is 65535 and nobody is zero; conversion is explicit.

Volume integration is NOT complete. `secFSRendezVous()` signals the security
server to rescan; it does not register a filesystem or synchronously confirm
discovery. `secRegisterHandler()` is a plugin operation-table API, not volume
registration. `security_volumes.c:FindVolumes` scans DLT_DEVICE entries with
FileSysStartupMsg/DosEnvec metadata. Our dynamic mount now supplies
that metadata, but not the rendezvous notification. Its ZFS disk type
0x5a465301 is not among IsSecFSDosType's multi-user types. Adding the signal
alone therefore cannot establish discovery.

Non-multi-user disk types can be probed for a native key file and entered in
NativeVolumes; secIsVolumeSecured queries that list. This is distinct from
the secVolume list searched by secAccess_Control for quarantine/readonly
context. Native key-file detection alone is not proof of those protections.
The production integration needs an explicit lifecycle contract including
online discovery, policy readiness, readonly/quarantine state, detach and
repeated remount without stale ports; verify each in an isolated guest.
Do not relabel a ZFS volume as a different filesystem just to enter the list.

`secIsConfigured()` returns Configured && !SecurityViolation. FALSE is
therefore ambiguous: it is not a safe instruction to synthesize privileged
credentials. The current authenticated adapter deliberately fails closed.
Although IsAllowed permits access when Configured itself is false, those
two predicates are not interchangeable. Ordinary unconfigured desktop
compatibility needs a deliberate policy/state interface rather than an
automatic root fallback. The explicitly named readonly fixture mount is
not the production answer to this gap.

Source references (read-only): AROS/rom/security/secFSRendezVous.c,
secIsConfigured.c, secIsVolumeSecured.c, secAccess_Control.c,
secRegisterHandler.c, security_volumes.c and security_enforce.c;
AROS/rom/filesys/SFS/FS/sfs_security.c illustrates asynchronous rescan and
non-cached negative secIsVolumeSecured answers. Earlier sections below
retain their historical test boundaries; later DOS write support does not
by itself close these volume lifecycle gaps.

`aros/engine/cred.c` replaces libzpool's unconditional-root identity getters
on AROS. Credentials are explicit, immutable identity/group snapshots with
atomic ownership. NULL is anonymous, group membership is tested against the
primary and copied supplementary groups, and the separate internal kernel
identity is only selected explicitly with `kcred`.

The administrative policy hook still requires explicit effective UID zero.
The snapshot, destroy and rename hooks now port the compound policies from
upstream zfs_ioctl.c and use the real dsl_deleg_access_impl on a held dataset.
Destroy requires mount plus destroy; rename requires source rename/mount
and destination-parent create/mount. NULL credentials fail before delegation,
including everyone grants. AROS currently has one global namespace; the
upstream zoned-property restriction remains for non-root callers. Root keeps
the upstream fast path so the operation handles nonexistent datasets.
Creating a credential object does NOT authenticate a caller, and these hooks
do not remove the management service's fixture-only mutation restriction.

Native regression `build-aros/qemu-run.6fdv2KxG` passes all five mirror boots
with all three test executables rebuilt. The new delegation test writes real
upstream ACL nvlists, grants/revokes UID 77 and supplementary GID 99 snapshot
permission, rejects another UID and NULL, verifies destroy needs mount, and
verifies rename needs both destination-parent permissions. Grants are removed
on successful completion. Cold readonly/sole-member boots issue zero writes,
backing hashes are unchanged, and both Linux zdb checks report No leaks.
Logs: `build-aros/delegation-qemu.log` and
`build-aros/delegation-qemu-{pool,import,reopen}-test-build.log`.
Expanded native run `build-aros/qemu-run.sKYalVfY` passes the full five-boot
suite and both independent zdb checks as well. Additional live DSL tests
distinguish local parent grants from inherited descendant grants, expand a
named permission set, allow an authenticated caller through an everyone
grant while denying NULL, reject non-root access to a zoned dataset, and
require descendant mount/destroy permissions for an actual snapshot.
The temporary snapshot, grants and zoned override are removed on success.
Logs: `build-aros/delegation-scope-{build,qemu}.log`. These are policy tests,
not yet delegated management operation or admission-authentication tests.

`CRED()` now reads an explicit thread-local request scope, defaulting to NULL.
The scope owns a retained immutable credential and supports checked LIFO
nesting; popping on the wrong thread or out of order is rejected. Worker
threads do not inherit the initiating thread's scope. An upstream operation
must capture and retain its credential before dispatching a sync task.
Native `dmu_objset_create()` now rejects a missing scope with `EACCES` before
submitting that sync task. This keeps an adapter omission on the calling task
instead of letting a NULL credential reach root ACL creation and abort the txg
sync thread. Internal engine fixtures enter `kcred` explicitly; public paths
still use the authenticated packet credential.

The retained broad run `native-import-run.O6VofuET` had aborted at the root ACL
VERIFY while creating `qemu_mirror/rate-sample`. The same post-import reguid,
clear, reopen and live create/destroy sequence passes with the current package
in `native-import-run.EDllJSSo`; therefore that older abort is retained as an
artifact-specific negative result, not claimed as a current reproduction.
`source-engine.rPcU12uJ` is the fresh production build containing the guard,
and `qemu-run.GXjrEZwe` passes both replacement staging and cold recovery. Its
fixture first requires anonymous DMU create to return `EACCES`, then creates
under explicit internal `kcred` and verifies the payload after the cold boot.
Thread exit releases any remaining heap-owned scope chain through the TLS
destructor. Scope entry is an internal identity-propagation API, NOT an
authentication API or a way for arbitrary IPC clients to select credentials.

Native filesystem creation checks upstream parent create/mount permissions,
then enters the scope around dmu_objset_create. That unchanged upstream
function captures CRED and holds it through limit checking, create-time
delegation and its sync callback. The root-directory callback verifies the
same credential arrives and uses it for root UID/GID/ACL creation. A callback
argument alone would have left upstream create-time delegation anonymous.
The scope is restored on both success and operation error.

Verified with a full native rebuild (178 engine and 70 dependency units,
zero failures) and all three freshly linked mirror-test executables:
`build-aros/qemu-run.Nqnu8lBS` passes five boots. Scope tests check nested
identities, LIFO rejection, four concurrent workers that do not inherit
the parent's root scope, and restoration to anonymous. The live creation
test rejects missing credentials, missing mount permission and another UID;
creates a child owned by UID 77/GID 88; verifies an upstream create-time
snapshot grant belongs to UID 77 rather than the anonymous identity; and
checks scope restoration after success and EEXIST. It closes and destroys
the disposable child and removes grants on success. Thread-exit destructor
cleanup and forced cancellation are not yet independently measured.

The separately rebuilt management executable passes all four boots in
`build-aros/management-run.B5IHg3vk` (writer IPC total 157 for this run,
including scrub polling). Cold readonly/sole-member checks, backing hashes,
and independent zdb No leaks gates pass in both suites. Logs use prefixes
`build-aros/credential-scope-` (sweep, deps, per-executable builds, qemu,
management-build and management-qemu). Previous compile logs were retained
under `build-aros/credential-scope-prior-logs.*` before the full rebuild.

Real MuFS identity-to-create follow-up: `build-aros/qemu-run.EDoAHuiK` passes
all five boots with four additional mandatory packet cases. A worker logs in
as ozreader (UID 77/GID 88), sends a DOS packet and waits. The root receiving
task captures identity before reply, joins the worker after logout/port
deletion/exit, destroys the packet context, then invokes filesystem creation
with the separately held credential. No grant and create-without-mount both
return EPERM; complete permissions create successfully; repetition returns
EEXIST. Scope restores to NULL in every case. Root ownership and create-time
snapshot permission belong to UID 77, not the root receiver. Temporary child
and grants are removed. Readonly-zero-write, backing hashes and both zdb
No leaks gates pass. Logs: `build-aros/mufs-delegated-create-{build,qemu}.log`.
These are real security.library credentials. The subsequent admission-object
test below adds owned request copying; public job identity, result routing
and UI worker shutdown remain separate work.

`aros/engine/admission.c` now owns a validated copy of the management request
and the credential captured from the real DOS packet before ReplyPkt. It
clears reply pointers/names and incoming response fields; only confirmed
dataset creation with no unsupported flags/options is currently admitted.
Execution checks actual upstream permissions again and runs at most once
per in-memory admission object. Result access requires an explicit caller
credential matching the submitting UID or an administrator; NULL/other UIDs
are denied. Calls are serialized by the owning service task, not a concurrent
job implementation. This internal result API requires the transport to
authenticate its caller and is not itself a public credential interface.

`build-aros/qemu-run.UOEKNElm` passes the five-boot suite with all four MuFS
create cases now using these objects. The sender overwrites and frees its
request after admission reply, logs out and exits before execution. Tests
check original copied target, result status, absent reply pointers/names,
pending-result EAGAIN, NULL/other-UID EACCES, owner/admin retrieval and second
execution EALREADY, plus filesystem ownership and create-time delegation.
Readonly-zero-write, hashes and both independent zdb gates pass. Logs:
`build-aros/admission-copy-{build,qemu}.log`.

The native service now opens OpenZFS.Management.Admission.v1 (a separate
Test.v1 name for test modes). It stores up to eight jobs under tickets with
an entropy.resource-backed per-service epoch and monotonic number; released
slots do not reuse numbers. Submit copies/captures before reply. Query and
release independently capture fresh MuFS packet identity before returning
or freeing a result. Completed results consume slots until released; a full
table rejects new work with EBUSY. Service close unregisters the endpoint
and replies ENOENT to queued packets rather than dropping waiting clients.

The blocking functions in aros/ui/admission-client.c keep their packet,
reply port and payload alive until the reply. Find/send is protected by the
same exclusion as server removal. There is deliberately no post-send timeout
that frees live packet storage. These calls must run in a UI worker, not its
event loop. The service currently executes admitted jobs serially after
replying, so subsequent admissions can wait behind an operation.

Native endpoint test `build-aros/qemu-run.3PmxUEPD` passes the full five-boot
suite: real logged-in sender, confirmation refusal, absent service, successful
creation, stale epoch rejection, logout query/release denial, administrator
query/release, released-ticket rejection, eight-slot exhaustion and reuse
with increasing IDs. Child ownership and create-time permissions still match
UID 77/GID 88. Backing hashes, zero readonly writes and both zdb gates pass.
Logs: `build-aros/admission-endpoint-normalized-qemu.log` and
`build-aros/admission-endpoint-build.log`. The standalone native service
also builds at `build-aros/admission-service/OpenZFSServiceNative`, with log
`build-aros/admission-service-build.log`; its own process event loop has not
yet been runtime-tested independently of the shared server-pump test.

Retained failed observation: `build-aros/qemu-run.l5TxIpbr` timed out waiting
for survivor0's completion marker because complete secTaskNotify records
split the word AROS in an otherwise passing final line. The raw log remains.
The mirror harness now creates a separate completion-checked log, removing
only complete known task-notification records; it checks FAIL/assertion/page
fault markers in both raw and normalized streams before accepting PASS.
The recovered old completion log and fresh passing run are distinct evidence;
the timed-out run is not counted as a full-suite pass.

Separate-process follow-up: `build-aros/admission-service-run.VZV1ZlQY`
passes WRITE and cold VERIFY boots. The harness performs only a test-marker-
guarded native pool import before invoking the production service.c event
loop; the client is a separate executable without an upstream engine linked.
It verifies root create/result, logged-out denial, a real logged-in reader's
denied query/release of root's ticket, denied unprivileged create, export,
port removal and the service's shutdown marker. Cold VERIFY sees the created
dataset, rejects even root's create with EROFS, and issues zero writes on both
members. Backing hashes remain unchanged and independent Linux zdb reports
No leaks. The test engine is RW-capable in both boots so that export is
available; VERIFY imports the actual pool readonly, which the disk counters
and hashes verify. This does not widen public native-device import policy.

Inspection found test-mode service startup required writing a SYS: error
file, unsuitable for ISO boots. It now retains the launcher's error stream.
The first separate-process run, `admission-service-run.BwMxivWF`, reached
successful operations/export/port removal but failed its final log assertion:
T:admission-service.log contained no shutdown marker. Retained raw evidence
records client error=99. Native service lifecycle messages now go to serial
as well as stdio; the successful rerun checks those markers directly. This
does not claim to repair or fully explain the launcher's empty stdio capture.

Logs: `build-aros/admission-standalone-lifecycle-{client-build,service-build,
qemu}.log`; initial failed-run build/log files keep the
`admission-standalone-` prefix without `lifecycle`. The regular native service
and non-engine stub are rebuilt separately, not substituted for the test
harness. UI event-loop responsiveness and window-close worker draining have
not yet been verified by these command-line client tests.

Durable replay identity, cancellable workers, restart/drain races and the GUI
worker lifecycle remain incomplete.

### Native manager worker (2026-09-21)

The manager routes dataset.create through an owned pthread worker. Its request
and endpoint are copied, borrowed Exec pointers cleared, and results joined
before disposal. Closing the manager retains the process while admission is
outstanding; no timeout frees a DOS packet still owned by a receiving service.
Other management operations remain on the restricted fixture v2 path; this is
not a claim of complete authenticated management coverage.

`build-aros/admission-service-run.YLkxM4uZ` passes WRITE and cold VERIFY with
the actual manager worker linked into the separate native test client. Real
MuFS root identity inherited by the worker creates a dataset; inherited reader
identity receives EPERM. Readonly import rejects root creation with EROFS.
The caller overwrites/frees its original request immediately after start.
A separate transport-only delayed peer holds the actual DOS packet across five
poll/dispose attempts: poll returns EAGAIN and disposal EBUSY until ReplyPkt,
then join and disposal succeed. That peer performs no authentication or storage
operation and is not evidence of either. The real service cases supply those.
Cold backing hashes are unchanged and independent zdb reports No leaks.
Logs: `build-aros/admission-worker-delayed-{build,qemu}.log`. Earlier real
worker run `admission-service-run.B6swexOy` also passed, before adding the
delayed-peer check. The manager links successfully, but actual Zune event-loop
responsiveness/window-close behavior is still unverified. No DUT or shared
hosted runtime was used for these tests.

Hosted GUI follow-up: the private `hosted-ui.VKzPRi3D` image rebuilt all 178
engine and 70 dependency units and linked the service and manager. It has no
security.library installed. The actual Zune manager confirmed dataset.create,
received transport errno 86 with no admitted ticket, displayed the unavailable/
denied state, and then completed another Connect request (service request 3,
operation 1, status 0). This verifies UI recovery after unavailable native
authentication, not an authenticated hosted mutation or closing mid-request.
Named AROS-window captures: `worker-ui-confirm2.png`, `worker-ui-denied.png`,
`worker-ui-after-denial.png` in that private image. Build logs are
`build-aros/worker-hosted-{engine-sweep,deps,service-link,manager-build}.log`.

Actual hosted close-drain follow-up passed using `TEST-DELAY`, an explicit
marker-guarded manager mode with a separate endpoint. The test-only
admission-delayed-peer holds one real DOS packet and performs no storage or
authentication operations. After confirmation the peer logged receipt; clicking
the manager close gadget left its window open with the drain status. Only after
creating the release marker did the peer reply and the manager close, logging
`OpenZFS Manager closed; authenticated worker drained`. Named-window captures
`worker-ui-close-pending.png` and `worker-ui-close-drained.png`, plus
`worker-ui-delayed-{peer,manager}.log`, are retained in `hosted-ui.VKzPRi3D`.
This verifies the actual Zune close path for delayed transport. It does not
prove recovery if a service dies permanently while owning a DOS packet.

Authenticated operation expansion: admission now supports confirmed op50
snapshot creation as well as op37 dataset creation. The snapshot adapter
validates the name, evaluates upstream delegated snapshot permission with
the retained MuFS identity at execution, checks pool writability, and scopes
CRED around upstream dmu_objset_snapshot_one. Options, recursion, dry-run and
extra arguments are rejected, not silently ignored. The manager routes op50
through the same worker. `admission-service-run.Xl5fHdDr` passes WRITE and
cold VERIFY: root snapshot creation, reader EPERM, readonly EROFS, and exact
snapshot enumeration after cold import. Readonly backing hashes and zdb No
leaks checks pass. Logs: `build-aros/admission-snapshot-{service-build,
client-build,manager-build,qemu}.log`. Actual GUI submission of a successful
snapshot and delegated non-root snapshot admission still need verification;
the CLI client exercised the real worker and separate service here.

Authenticated lifecycle expansion: op38 destroy and op39 rename now bind to
management's existing mount-aware adapter before accepting requests. It checks
upstream destroy or source/destination rename permissions using the retained
caller, scopes CRED, and retains name, same-pool, mounted-subtree and readonly
checks. Standalone admission builds without that adapter reject these ops.
`admission-service-run.zORJKCC4` passes WRITE and cold VERIFY: actual rename,
snapshot deletion and filesystem deletion; reader EPERM; readonly EROFS;
renamed dataset present and old/deleted names absent on cold import. Readonly
disk hashes and independent zdb No leaks pass. Logs are
`build-aros/admission-lifecycle-{service-build,client-build,manager-build,qemu}.log`.
The manager routes these operations through its worker and compiles. Successful
GUI lifecycle mutations and mounted-subtree rejection through the new endpoint
remain unverified; the shared adapter's prior fixture tests do not substitute
for those endpoint-specific cases.

Mounted endpoint follow-up `admission-service-run.owxaMFAH` passes the separate
service/client WRITE and cold VERIFY suite. A real child dataset is mounted
through the management fixture API; authenticated worker rename/destroy of
both that child and its parent return EBUSY. Unmount permits child deletion.
The client retains an actual DevProc across detach, pool export and a service
stop request: the service refuses shutdown and continues answering late DOS
packets with DEVICE_NOT_MOUNTED. The writer's resident service is stopped only
by ending its disposable QEMU guest; the untouched cold-reader service exits
normally. Disk hashes and independent zdb No leaks pass. Logs:
`build-aros/admission-mounted-{client-build,qemu}.log`. This proves the tested
retained endpoint stays safe, not general endpoint reclamation or unlimited
mount capacity (the existing eight lifetime slots remain).

Delegated admission follow-up `admission-service-run.HsMQgJqK` passes both
boots. The marker-guarded harness creates only a disposable delegated-root
fixture and writes real upstream validated ul$77/ud$77 grants for create,
mount, snapshot, rename and destroy. The separately logged-in MuFS reader,
through the actual worker and service endpoint, creates a child and snapshots,
renames within the subtree, and deletes a temporary dataset/snapshot. Rename
outside the granted parent returns EPERM. Cold VERIFY retains the renamed
snapshot, confirms deleted objects absent, and receives EROFS for otherwise
authorized operations. The ungranted subtree denial and mounted lifetime
checks still pass. Independent zdb verifies the created root's uid=77 and
gid=88, alongside the suite's unchanged readonly hashes and No leaks check.
Logs: `build-aros/admission-delegated-{service-build,client-build,qemu}.log`
and `build-aros/admission-delegated-owner-zdb.log`. No public grant endpoint or
client-supplied identity was added. Revocation between admission and execution
and successful authenticated GUI mutations remain separate coverage gaps.

Authenticated DOS mount control now admits op43 with an explicit readonly=on
or readonly=off and op44 unmount, gated by upstream mount delegation and retained
MuFS identity. Writable mounts use oz_zpl_open_writable plus oz_dos_mount, not
the public readonly fixture. The legacy fixture interface cannot unmount an
authenticated volume. The manager routes both operations through its worker;
its help documents the explicit mode, confirmation and no-dry-run contract.

`admission-service-run.r7iAliNt` passes native WRITE and cold VERIFY with the
actual service: delegated UID77 mounts its dataset writable, creates written.txt
using AROS Open/Write/Close, reads exact bytes, and unmounts. A cold authenticated
readonly mount reads the same bytes; MODE_NEWFILE returns DISK_WRITE_PROTECTED,
and a writable mount of the readonly pool returns EROFS. Ungranted mount and
legacy unmount are denied. Both runs retain retired endpoints and refuse service
exit while continuing to answer management requests; the writer also answers
a late DOS packet. Readonly disk hashes and independent zdb No leaks pass.
Logs: `build-aros/admission-writable-boundary-{service-build,client-build,qemu}.log`
and `build-aros/admission-writable-manager-build.log`. Earlier PoxPUkiV passed
file persistence before the legacy-unmount boundary was tightened. This is a
real native handler/service write-path test, not proof of complete filesystem
semantics, mount reclamation, general device import, or successful GUI mounting.

File-handle capacity follow-up: dos-handler.c no longer fixes each volume at
eight open files. Stable heap entries grow on demand and reuse closed entries;
allocation occurs before destructive open/create work. Membership validation,
exclusivity, rename metadata, resize peer checks and detach scan the live list.
Entries remain stable until the bounded caller safely destroys the endpoint.
This is high-water reuse, not a claim of memory-pressure reclamation.
`admission-service-run.gyXhwxV5` passes 96 simultaneous AROS opens, independent
full reads on every handle, two close/reopen rounds and EBUSY unmount while
files are open, in WRITE and cold readonly VERIFY. Hashes and zdb pass.
After rebuilding all three executables, `qemu-run.ksMz6REW` passes the full
five-boot native mirror/handler suite, including cold and both sole-member
reads, zero readonly writes, hashes and cold writable COW reopen. Logs:
`build-aros/admission-many-files-{service-build,client-build,qemu}.log` and
`build-aros/dynamic-handles-{qemu-pool-test-build,qemu-import-test-build,
qemu-reopen-test-build,mirror-qemu}.log`. The distinct 32-lock table, record
locks, notifications and retired service mount slots remain separate work.

DOS lock capacity follow-up removes the separate 32-entry limit with stable
heap lock entries, reused after release/conversion. CREATE_DIR reserves an
entry before namespace mutation; membership validation, exclusivity, rename
metadata and detach scan the lock list. The native admission run
`admission-service-run.cllwLxsS` passes 96 simultaneous locks (alternating Lock
and DupLock), Examine, OpenFromLock conversion and exact file reads, two reuse
rounds, plus EBUSY unmount while locks are held. Both writable and cold readonly
boots pass alongside the 96-handle tests, hashes and zdb checks. After rebuilding
all three test binaries, `qemu-run.NIRrevEw` passes the full five-boot handler
regression suite. Logs: `build-aros/admission-many-locks-{service-build,
client-build,qemu}.log` and `build-aros/dynamic-locks-{qemu-pool-test-build,
qemu-import-test-build,qemu-reopen-test-build,mirror-qemu}.log`. This removes
fixed DOS file/lock counts, not the retired-mount limit, record-lock gap or
notification gap. Allocation-failure injection and peak-memory reclamation
remain unverified.

Dynamic-table batching correction: handle/lock list heads must be refreshed
for each packet, not once before draining a port. A preceding packet can grow
either list while later packets are already queued. The deterministic native
test queues COPY_DIR(root) then DISK_INFO before one pump and requires the
second result's id_InUse to reflect the new lock. `qemu-run.dilJWiZV` passes
this check (`DOS same-drain dynamic lock visibility=0` in create-serial.log)
and the full five-boot regression after rebuilding all three executables.
Logs: `build-aros/batch-state-{qemu-pool-test-build,qemu-import-test-build,
qemu-reopen-test-build,mirror-qemu}.log`. This correction was found during
record-lock preparation; record locking itself is not yet implemented.
The v2 broker remains unauthenticated and fixture-limited; the new endpoint
does not retrofit authority onto it or enable arbitrary RW pool import.
Per-object single execution does not mean exactly-once effects across crashes.

The current service remains an isolated diagnostic with constrained fixture
paths and read-only DOS mounts, not a production security boundary. Request
scope support and delegated dataset creation do not authenticate management
messages. Do not remove fixture restrictions before implementing admission.

## Native source findings (read-only ADT inspection)

The real OS supplies `security.library`, not Linux credentials:

- `rom/security/secGetPktOwner.c`: `secGetPktOwner(packet)` obtains the sender
  identity before reply; release the returned snapshot with `secFreeExtOwner`.
- `rom/security/secGetTaskExtOwner.c`: task identity plus supplementary groups.
- `compiler/include/libraries/security.h`: `secROOT_UID/GID` are 0xffff;
  **zero is nobody, never POSIX root**. These values need explicit conversion.
- `rom/security/secIsConfigured.c`: returns false both when unconfigured AND
  on a security violation. Therefore false must NOT automatically grant root.

Sources are under `/opt/aros-workspace/aros-adt-src` and were not
modified. The native adapter still needs a consistent unmapped-ID policy,
collision-free native-to-POSIX identity mapping, library availability/version
handling, request-scoped propagation, and the upstream ACL/delegation paths.
FUID mapping must not truncate a foreign domain to a local numeric ID.
The future caller boundary must reject missing identity snapshots before ACL
owner comparisons; a nobody numeric fallback is not proof of authentication.

## Packet identity adapter (2026-09-20)

`aros/engine/packet-cred.c` now implements request-scoped capture through
security.library version 45's secGetPktOwner before reply, frees its extended
owner, and returns an independently owned immutable credential. It rejects
missing signal-port sender, unavailable library, unconfigured security, and
missing/nobody identity. It never substitutes the current handler task or
internal kcred. This follows the OS packet-owner trust contract; it does not
add isolation against arbitrary memory writes in a shared-address-space OS.

Native UID/GID 65535 maps to POSIX zero. Native IDs 1..65534 remain unchanged.
Nobody UID zero is denied (no credential). Nobody group zero maps to 65535,
distinct from every mapped real native group. Supplementary groups use the
same conversion and are copied; an ordinary user's root-group membership
does not grant root policy. Native ownership setters must use this same
mapping; imported POSIX identities outside the native range remain distinct.
Packet ownership supplies one UID, so ruid equals the reported effective UID,
not an invented independent login identity. Creating/converting a test owner
is not authentication; production callers must use the packet capture entry.

### SetOwner object mapping update (2026-09-20)

Object ownership now reserves POSIX UID 65535 for native nobody UID zero,
matching the existing nobody-group mapping. This extends the earlier reverse
mapping, which rejected that UID: native SetOwner must be able to represent
unowned objects without ever mapping them to POSIX root. POSIX zero still
maps to native root 65535, ordinary IDs 1..65534 remain unchanged, and larger
or unmapped foreign IDs still fail rather than truncate. Imported numeric
UID 65535 is consequently displayed as native nobody, but ZFS ACL checks
remain mandatory even where native policy considers an object unowned.

This is an object-metadata conversion only. A security.library sender with
nobody UID remains rejected by oz_cred_from_aros_owner; no credential is
created for it. SetOwner checks native property permission on the old owner
and ROOT_UID or UID_MATCH against the new owner, as AFS/SFS do, then invokes
upstream zfs_setattr with the actual caller credential. Upstream group/ACL
restrictions are additional checks, not replaced with internal kcred.

QEMU `build-aros/qemu-run.0JQQkC54/` tests root/nobody/high-ID distinctions,
supplementary-copy ownership, unprivileged root-group membership, invalid
packets and actual library capture. Its live capture returned EACCES (13),
matching the library/configuration result: this verifies denial, NOT a live
authenticated login. The existing four-boot mirror/write/handler regression
also ran. Full DOS request ACL propagation and an authenticated guest test
remain pending; the current public diagnostic mounts are still read-only.

### Authenticated disposable QEMU fixture

`AROS_TEST_SECURITY=1 bash aros/engine/test-qemu-mirror.sh` now overlays
`aros/engine/security-fixture` at SYS:Security in fresh test ISOs only.
The guest has no NIC and only disposable disks. This opt-in fixture has
empty-password test accounts; never install it on the DUT, user hosted
sessions, or distributable images. A marker gates login testing, and the
harness requires the explicit authenticated-test success marker.

The test uses actual secLoginA for native root 65535 and reader 77/group 88
with supplementary group 99. Packet-owner capture must reproduce those
identities, grant root policy only to root, and preserve the supplementary
group. Quiet secLogoutA restores the previous task identity after each login;
both the return value and secGetTaskOwner are checked. No global login or
changes to other tasks are requested. The test packet's reply port belongs
to the test task; cross-task DOS sender propagation is still a separate test.

`build-aros/qemu-run.Bl5zebY5/` passed authenticated capture and all four
mirror boots with existing write/handler/integrity tests. This closes the
earlier lack of any successful real-library capture, not the still-pending
public handler ACL integration. The default unconfigured test path remains
available and continues to require denied capture rather than root fallback.
Expanded logout-restoration run `build-aros/qemu-run.uxoNr66f/` also passed
all four boots and integrity gates. Logs: `packet-login-restore-link.log`
and `packet-login-restore-qemu.log`.

### Cross-task packet sender and snapshot lifetime

The configured QEMU fixture now logs the receiver in as native root and
starts a separate pthread/AROS task logged in as reader 77/group 88/group 99.
The worker sends an actual StandardPacket with SendPkt, using its own signal
reply port. Before ReplyPkt, the receiver calls the production packet adapter.
It must see the reader, deny root policy, and still itself be root. After the
reply the worker logs out, destroys the port and exits. The captured immutable
credential must still contain the same reader/group identity after join.
The receiver then logs out and verifies its own original identity restored.

`build-aros/qemu-run.g4N2nBSq/` passed this cross-task test and all four
mirror/write/handler/integrity boots. This verifies the documented native
sender contract rather than inferring it from a same-task constructed packet.
It is still not public handler ACL enforcement: the next integration must
carry the request credential through traversal/open/read/write checks and
define held-handle authorization/lifetime without replacing it with kcred.
Final run `build-aros/qemu-run.nYUjnY7g/` passes after tightening the worker
failure-notification path to read its final status only after pthread_join.
Logs: `packet-sender-final-link.log` and `packet-sender-final-qemu.log`.

### Native mount ACL interface

The read mount now exposes an explicit-credential access call backed by the
actual upstream zfs_zaccess (no skip-ACL or replay flags). It initializes and
destroys the FUID lock/table lifecycle and uses the SA/FUID dataset version
already required by the fixture open boundary. NULL credentials are denied.
`oz_zpl_resolve_as` checks directory search permission at each path component,
including parent traversal, and READ_ATTRIBUTES before returning the final
metadata. It clears output on rejection. `oz_zpl_read_as` checks READ_DATA
before copying any bytes. The mount is currently read-only with exclusive
objset ownership; writable namespace concurrency remains future work.

`build-aros/qemu-run.r0DhQ1q2/` passes all four native mirror boots with
explicit primitive-level ACL tests: reader77 resolves/reads nested.txt,
execute is denied by its persisted read-only ACE/mode, writes return EROFS,
invalid permission bits fail, and missing identities leave data unchanged
and metadata empty. Authenticated/cross-task capture and all existing
write/persistence/handler/integrity tests also pass in this run. Logs:
`acl-bridge-final-link.log`, `acl-bridge-qemu.log`.

These new calls are tested building blocks, not yet end-to-end authorization
for every DOS action: the diagnostic handler still uses the older public
fixture calls. Its packet dispatch, enumeration, held-handle policy and
all write/namespace actions must adopt explicit authorization before any
production writable mount can be exposed. Foreign FUID mapping configuration
and restricted-directory traversal fixtures also need further verification.

### Read-only DOS packet authorization

`oz_dos_mount` now defaults to authenticated operation. Before file/metadata
dispatch it captures the actual packet sender, rejects unavailable/missing
identity (read-protected; no-memory remains distinct), and releases the
credential before replying. Lookup/open use credential-aware traversal;
open/read/seek require READ_DATA, object examination/copy/lock comparison
require READ_ATTRIBUTES, and directory enumeration requires LIST_DIRECTORY
and EXECUTE plus the returned object's attributes. Parent lookup checks
the child's attributes and parent attributes/search. Requests on already
open handles are reauthorized with the current sender, not a saved service
identity or a permanent opener privilege. No writable operations are enabled.

Close/unlock and public volume discovery/capacity remain usable without an
identity. This permits cleanup after logout and does not expose file data.
The still-isolated management fixture and old diagnostic tests explicitly
call `oz_dos_mount_fixture`, a named public read-only diagnostic path; they
must not be mistaken for a production authenticated management mount.

Configured QEMU `build-aros/qemu-run.UhixvtKs/` passes both diagnostic and
authenticated handler clients on all four boots. The latter starts with a
denied anonymous Open, logs in as reader77, exercises ordinary DOS reads,
seeks, locks, relative lookup, examination, enumeration and busy unmount,
then logs out with a file/lock held. Read/seek/examine are denied afterward,
the read sentinel remains unchanged, and Close/UnLock permit clean teardown.
Exact read counters still match, showing rejected reads copied no data.
Restricted-file/directory ACL denial fixtures and authenticated management
mount selection still require work, as do writes and namespace mutation.
Final configured run `build-aros/qemu-run.SQZ0EyBL/` requires and passes the
authenticated-handler marker on every boot. Default unconfigured diagnostic
run `build-aros/qemu-run.9x5a5hMA/` also passes all four boots. Logs:
`secure-handler-final-link.log`, `secure-handler-final-qemu.log`, and
`secure-handler-diagnostic-qemu.log`. Hosted runtime deployment is pending.

### Persisted restrictive ACLs through the DOS handler

The separate `qemu_mirror/writefiles` acceptance dataset now receives real
upstream `zfs_setacl` transactions and `zil_commit` after its vnode-write test.
`hello.txt` grants everyone only READ_ATTRIBUTES/READ_ACL. `subdir` also grants
LIST_DIRECTORY, but not EXECUTE; its child remains independently readable.
No ACL is synthesized by the handler or patched directly into disk metadata.

With the explicitly disposable security fixture enabled, a separate native
task mounts this dataset through the authenticated default `oz_dos_mount`.
Reader 77 can inspect the directory metadata but cannot open the restricted
file, traverse to the readable child, or enumerate the non-searchable directory.
The explicit authenticated root can read both and enumerate it. Each login
is undone and its prior task identity checked. Dataset counters must report
exactly the two permitted one-byte reads and no writes, excluding hidden reads
on denied operations.

The four-boot harness requires the restricted ACL marker during creation,
fresh import, and each sole-survivor mirror import. Initial evidence:
`build-aros/qemu-run.fXdXL3Nc/`, including unchanged backing-image hashes,
zero cold-boot disk writes, and upstream zdb reporting no block leaks.
The final rebuilt binaries with exact read-counter assertions also pass all
four boots in `build-aros/qemu-run.JOVGJSlq/`; summary and compiler logs are
`build-aros/restricted-acl-final-qemu.log` and
`build-aros/restricted-acl-final-link.log`.
This is read-only handler authorization coverage, not a claim of writable DOS
support, crash replay, complete namespace permissions, or delegated management.

## Verification

`build-aros/qemu-run.GPu96MtP/create-serial.log` reports native credential
identity/groups/refcount/policy tests=0. It covers NULL versus kernel identity,
owned supplementary-group copies, admin rejection, four threads each taking
and releasing 10,000 references, and invalid constructor arguments.

The rebuilt engine also passes create/healthy cold boot/either-survivor DOS
handler tests, zero cold/degraded writes, unchanged backing hashes and Linux
zdb's no-leak check. Summary: `build-aros/cred-qemu-test.log`.

A second full four-boot replay also passes after making each credential test
worker retain its own lifetime reference: `build-aros/qemu-run.QtOvUNa2/`
and `build-aros/cred-qemu-replay.log`. A failed pthread join therefore cannot
cause the test controller to free a still-running worker's credential.

The hosted engine/dependencies and management/fixture/snapshot binaries were
rebuilt. `build-aros/cred-write-pass.cVn77pN3/write-result.txt` proves zero
snapshot-write test failures on the new disposable pool. A separate cold boot
in `build-aros/cred-snapshot-verified.Qru95xl2/` proves zero verification
failures and a fresh-engine byte comparison of the actual 16 KiB snapshot
object. This is an upstream snapshot operation, not DOS file-write support.

The pre-existing StdC stdout-ownership anomaly reproduced during the write
test. Preserve `build-aros/cred-stdout-anomaly.XKBTTw0v/`: client output entered
the service log and the final summary interleaved/truncated during shutdown.
`write-test.c` now opens an independent DOS result file for completion proof;
it does not fix or conceal the runtime-library anomaly. Raw logs remain saved.

The ZPL sweep still stands at 4/6 (`build-aros/zpl-compile.zlzOlzSE/`). Identity
snapshot primitives do not themselves provide Solaris SID mapping, native
caller authentication, vnode/page-cache interfaces, or a linked ZPL engine.

The final hosted reboot also passes the actual management mount/read,
busy-unmount, late-packet and remount tests with zero failures, and the native
manager is visibly connected: `build-aros/cred-mount-pass.o3ZVnyXk/`.

These tests establish credential primitives, not authenticated client access,
complete Unix permission semantics, ZPL writes, or full OpenZFS completion.

## Set-ID retention policy

The native `secpolicy_vnode_setid_retain` hook now allows only an explicit
effective-POSIX-root credential to preserve executable setuid/setgid bits.
NULL credentials, real UID zero without effective UID zero, and group zero
alone are denied. There is no claim that AROS native UID zero means root;
the security.library conversion and authentication boundary remain unfinished.
No per-task capability or delegated retention privilege is currently exposed.
Upstream zfs_write uses a denial to clear and journal the bits; its full native
write path is not linked yet, so this test proves the policy, not disk-mode
mutation or ZIL replay.

Native QEMU tests in `build-aros/qemu-run.YFIMYScU/create-serial.log` pass both
root-owned setuid and other set-ID cases as part of the credential tests.
The complete four-boot mirror/handler regression also passes; summary:
`build-aros/setid-qemu.log`. The ZPL compile lane remains 5/6 in
`build-aros/zpl-compile.GNesPg59/`, with the missing policy declaration removed
from the remaining zfs_vnops errors.

The accumulated shared-UIO, upstream-FUID and policy changes were then fully
rebuilt for hosted AROS (178 engine / 70 dependency objects) and the service
and UI relinked. After restarting only the owned :94 instance,
`build-aros/setid-hosted-pass.7wHB84eN/mount-test.log` reports
`RESULT native mount API failures=0`, including busy-unmount, late-packet and
remount checks. `build-aros/setid-hosted-manager.png` captures the named AROS
window with the manager connected to OpenZFS 2.4.4 and OZAPI mounted. Earlier
runtime logs are retained under `build-aros/pre-setid-hosted.rV745wNQ/`.

## Classic DOS record locking (2026-09-21)

The handler now implements ACTION_LOCK_RECORD and ACTION_FREE_RECORD with
per-handle, per-object cooperative byte ranges. Shared ranges coexist;
overlapping exclusive ranges conflict between handles, while adjacent ranges
do not. Immediate collisions and zero-timeout waits return explicit DOS errors.
Blocked timed requests retain their DOS packet and use real asynchronous
timer.device requests, whose signal is included in the mount service mask.
Unlock and Close release records and retry waiters. Delayed grants revalidate
the current packet caller; a closed waiting handle is rejected. Timer I/O is
aborted/joined and destroyed before the packet is replied or storage freed.
Record locks do not block ordinary file reads/writes.

`admission-service-run.qxJjYGTb` passes WRITE and cold readonly VERIFY with
shared overlap, exclusive collision, adjacent ranges, missing-unlock error,
timer expiry, zero timeout, wake on unlock, wake on owner Close, maximum classic
32-bit offset/length, successful bulk LockRecords/UnLockRecords, and a read
through another handle's exclusive record. The preceding 0i9z5gK9 run passed
before bulk/cooperative-read assertions were added. Both retain writable file
persistence, MuFS/delegation, 96 handles/locks, hashes and independent zdb checks.
After rebuilding all three executables, `qemu-run.C8bPNwmt` passes the full
five-boot handler regression. Logs: `build-aros/admission-record-{service-build,
client-build,qemu}.log`, `build-aros/admission-record-bulk-{client-build,qemu}.log`
and `build-aros/record-locks-{qemu-pool-test-build,qemu-import-test-build,
qemu-reopen-test-build,mirror-qemu}.log`.

Follow-up `admission-service-run.Tvr4khv3` passes native WRITE and cold readonly
VERIFY with two competing exclusive waiters acquiring in sequence, a short
waiter's timeout followed by a still-pending long waiter's successful grant,
and closing the waiting handle itself. The last test sends a real lock packet
then a same-handler FIFO barrier before Close, and requires ERROR_INVALID_LOCK
on the retained packet. It does not race a worker dereferencing a freed DOS
FileHandle. Both boots retain file persistence, MuFS/delegation checks, 96
handles/locks, readonly hashes and independent zdb validation. Earlier
`admission-service-run.QXSVqOG6` passed the two-waiter tests before the waiting
handle close test was added. Logs: `build-aros/admission-multiwait-{client-build,
qemu}.log` and `build-aros/admission-record-close-{client-build,qemu}.log`.
The threaded two-waiter tests allow scheduling time after the ready message;
this is not a deterministic queue-admission handshake or a fairness proof.

Not yet verified: starvation/fairness, authorization revocation
while waiting, forced client death, resource
failure injection and nonstandard 64-bit record packet extensions. Successful
bulk calls do not prove dos.library's partial-batch rollback behavior. These
limits must not be presented as complete record-lock conformance.

## Native DOS notifications (2026-09-21)

ACTION_ADD_NOTIFY and ACTION_REMOVE_NOTIFY now implement message and signal
delivery, existing-object initial events, missing-path registration below an
accessible ancestor, and WAIT_REPLY coalescing. Mutating DOS operations notify
affected path/object and parent-directory watches: creation, write, resize,
modified-handle close, metadata, rename, delete, and hard/soft link creation.
Path watches are resolved again after mutation, so rename-away and restoration
change their presence rather than permanently following the original inode.

Registration owns its captured MuFS security context. Current upstream ACLs,
native protection and volume policy are rechecked using that identity before
delivery, including a coalesced event after the previous message is replied.
This retains the registration's UID and supplementary-group snapshot; live
group-membership/login revocation is not yet proven. Registration does not
invent handler/root identity. Removal remains available for cleanup after logout.

Each sent message owns a reference to a private watch record. EndNotify removes
the active request pointer; later replies never dereference the client's freed
NotifyRequest/fullname. The service's separate notification reply port is part
of its wait mask. Active watches or outstanding messages refuse detach with
EBUSY, including messages held by a client after EndNotify. No forced timeout
frees a message still owned by another task. Returned messages release their
private records; bounded unmount frees the empty reply port.

`admission-service-run.G0EGv5nQ` passes native WRITE and cold readonly VERIFY
with initial message and signal delivery, missing-name creation, coalescing,
metadata/rename/delete events, denied registration and late replies after
freeing the request. `admission-service-run.mIiV6Pko` additionally passes resize
and close events, a second non-WAIT_REPLY registration accumulating four
messages before EndNotify drains them, and suppression of a coalesced event
after read permission is revoked. Both retain MuFS/delegation, file persistence,
record locks, readonly hashes and independent zdb validation. Full five-boot
handler regression `qemu-run.n29TvyDA` also passes after rebuilding all three
native executables.

Failed hypothesis retained: `admission-service-run.CGwHDMUy` stopped at the
denial assertion because ordinary SetProtection preserves group-read bits and
MuFS correctly grants via that group. The test now uses secSetProtection for
the full mask; handler permission policy was not weakened. Initial compilation
also rejected an unsigned STRPTR/char conditional; an explicit string view fixed
that build error. Logs: `build-aros/admission-notify-service-build{,-2}.log`,
`admission-notify-client-build{,-2,-3}.log`, `admission-notify-qemu{,-2,-3}.log`,
`notifications-qemu-{pool,import,reopen}-test-build.log` and
`notifications-mirror-qemu.log` (all under build-aros).

Final `admission-service-run.bQZ3MJfH` repeats both boots with the stricter
harness requiring the notification and record-lock success markers, plus the
client's corrected failed-StartNotify cleanup (no duplicate reply-port free).
Logs: `build-aros/admission-notify-{client-build,qemu}-4.log`.

Remaining notification coverage includes ancestor-directory moves, links and
ownership/date mutations, duplicate/invalid registration, task death,
allocation failure, high watch counts, and changes made outside this DOS
frontend. Delivery currently resolves active watched paths linearly; no claim
of scalable indexing or complete notification conformance is made.

## Authenticated native GUI operation (2026-09-21)

The previous successful-GUI-mutation gap now has bounded native evidence.
`native-gui-run.LDEskH0I` boots a private offline native QEMU image with copied
mirror disks. A marker-guarded launcher logs in as MuFS UID77/GID88, launches the
normal Zune manager, and only queries/exports after the window closes. It does
not create the expected GUI objects. Real clicks, typing, review dialogs and
confirmation submitted dataset.create for
`qemu_mirror/delegated-root/gui-created` and snapshot.snapshot for its `@ui`
snapshot. Both authenticated tickets completed with transport/release errno=0;
the window showed the real successful backend response and OK status. Saved
activity and response files were dumped to retained serial before pool export.

Independent userspace zdb confirms both exact on-disk objects and UID77/GID88,
reports No leaks, and leaves the disk hashes unchanged. The new
`aros/ui/verify-native-ui.sh` requires both GUI activity entries/tickets, saved
snapshot result, orderly export and those disk checks after the guest exits.
It passed; `build-aros/native-gui-verify.log` retains its output. Screenshots
and the step-by-step procedure are indexed in `aros/ui/NATIVE-UI-TEST.md`.

The first run, `native-gui-run.xe0NzGOW`, already created the real delegated
dataset and passed independent zdb ownership/integrity checks, but revealed
that the manager interpreted the authenticated response's absent hello metadata
as an unavailable engine. The manager now uses engine_available to specialize
hello status only; ordinary operation status comes from the operation result.
The fresh second run above verifies this fix visually. The first run also
preserves cancelled incomplete keyboard input; no truncated request was
submitted. Slower emulator key timing and observed tablet coordinate calibration
fixed the test interaction without changing host or DUT input configuration.

Native builds: `build-aros/native-gui-launcher-build.log` and
`native-gui-manager-build{,-2}.log`. Launch logs: `native-gui-qemu{,-2}.log`.
Both guests were quit only after GUI close and orderly export, and their owning
sessions exited. No user hosted runtime or DUT was used. This proves two real
GUI mutations, not all management features, authenticated hosted GUI operation,
general import admission or release readiness.

## Authenticated user property transactions (2026-09-21)

Operations 41/42 now retain the native packet caller through admission, check
current upstream USERPROP delegation at execution, and run actual dsl_props_set
or dsl_prop_inherit transactions under that credential. The manager routes both
through its authenticated worker. Built-in properties remain explicitly
unsupported here: their upstream validation and special setters must not be
bypassed by treating them as user properties. No substitute property database
was introduced. Recursive/received/bulk changes and values exceeding the current
request transport still require implementation.

`build-aros/admission-service-run.7D54kcc8` passed fresh WRITE and cold readonly
VERIFY boots as MuFS UID77/GID88. Tests cover delegation denial outside the
granted subtree, malformed arguments, overlong names, empty values, literal
equals/semicolons, dataset and snapshot properties, local override and inherited
value/source, persisted values, and readonly denial. Exact property reads in
this test still use the isolated legacy fixture query, not an authenticated
read API. The GUI routing compiled; property clicks have not yet been exercised.
Raw serial, disk hashes and independent zdb checks are retained in that run.
Build/test logs are `build-aros/admission-userprop-*.log`.

The broader build initially failed before boot: standalone admission-bearing
test programs lacked the newly required properties adapter (undefined
oz_user_property_change). `build-aros/userprop-qemu-pool-test-build.log` retains
the failure. link-engine-test.sh now adds that dependency once to every such
program. Sequential rebuilds and the complete five-boot mirror regression then
passed in `build-aros/qemu-run.yPLSA7vk`, including native DOS access, cold
readonly imports, both survivor cases, zero readonly writes, and writable reopen
on private COW disks. See `build-aros/userprop-mirror-qemu-2.log` and the
`userprop-*-build-2.log` files. No DUT or shared hosted session was used.

## Authenticated snapshot clone and persistent holds (2026-09-21)

Operations 52, 55 and 56 now use admission's copied request and retained MuFS
caller. The service-owned lifecycle callback rechecks upstream delegation at
execution: clone on the source snapshot plus create/mount on the destination
parent; hold or release, separately, on the snapshot's filesystem. These match
zfs_secpolicy_create_clone/hold/release in module/zfs/zfs_ioctl.c. The upstream
dsl_dataset_clone and dsl_dataset_user_hold/release transactions run with the
caller scoped in CRED(), not the service's internal kernel credential.

The existing diagnostic clone path and authenticated path share name, same-pool
and writability checks. Holds retain per-snapshot error handling from upstream's
bulk API. The manager routes all three through the authenticated worker; its
updated native binary compiled, but these three GUI operations have not yet
been exercised by clicks. Properties on clone creation, recursive/bulk changes,
temporary holds with process-lifetime cleanup, and authenticated hold queries
still require implementation. This does not enable unrestricted pool import.

`build-aros/admission-service-run.6yM37ow8` passed WRITE and cold readonly VERIFY.
`build-aros/admission-service-run.565jSdEn` additionally proves a separate local
hold-only grant outside the inherited grant subtree: UID77 can add the hold but
cannot release it. The tag persists into the readonly boot, where mutation is
denied. Both runs cover denied source/destination permissions, duplicate clone
and hold errors, a held snapshot refusing destruction, final release permitting
destruction, missing-tag error, an origin with a clone refusing destruction,
persistent hold enumeration, and exact cloned file bytes read through the real
AROS handler on both boots. Both keep readonly disk hashes, zero readonly writes,
all earlier DOS/record/notification tests and independent zdb No leaks checks.

Logs: `build-aros/auth-snapshot-service-build{,-2,-3}.log`,
`auth-snapshot-client-build{,-2}.log`, `auth-snapshot-manager-build.log` and
`auth-snapshot-qemu{,-2}.log`. The first service build failed before boot because
dmu_fsname was included through the wrong header; it is declared by
sys/dmu_objset.h. That failed build remains preserved. All tests use private
disk files and isolated QEMU, not physical hardware or shared hosted sessions.

The separately rebuilt existing management regression also passed all four
boots (WRITE, cold VERIFY, each surviving mirror member) in
`build-aros/management-run.4DPmQVLi`. This exercises shared clone code, clone
independence, persistent holds/pagination, disk hashes and integrity after the
refactor. Logs: `auth-snapshot-management-build.log` and
`auth-snapshot-management-qemu.log`. Both owning test sessions exited normally.

## Authenticated rollback and ZIL cleanup regression (2026-09-21)

Operation 54 now checks the actual retained caller's ROLLBACK delegation on
the filesystem and invokes upstream dsl_dataset_rollback with the exact named
snapshot. The service rejects mounted subtrees until the AROS ZPL suspension/
resume path exists; it never passes NULL ownership for an active mount. Pool
writability, malformed targets, missing snapshots and intervening snapshots
remain checked. There is no implicit destruction of newer snapshots. The
manager routes the operation through authenticated admission and its native
binary compiled; rollback GUI clicks remain untested.

The new native test changes a file's bytes and creates an extra file after a
snapshot, verifies the changed bytes, then rolls back and reads the restored
original bytes through the handler. The new file must be absent. A separate
cold readonly boot verifies that restored state persisted. Tests also require
EPERM outside the delegation, EBUSY while mounted, EINVAL for a nonsnapshot,
EEXIST with an intervening snapshot, ESRCH for a missing snapshot, and EROFS
on the readonly import. All earlier admission/DOS tests remain enabled.

Important failed hypothesis retained: correct bytes and two guest PASS results
did NOT prove disk correctness. Runs `admission-service-run.oB0Ej05J` and an
unchanged repeat `admission-service-run.7lDARV5b` both failed independent zdb
with exactly 4096 leaked bytes at vdev 0 offset 0x3534000. The empty block dump
is `build-aros/rollback-leaked-block.log`. Source inspection found that zil_close
can retain an allocated empty tail, while synchronous destroy traversal skips
unclaimed ZIL blocks on writable pools. The adapter now calls upstream
zil_suspend(filesystem,NULL), waits for the cleanup TXG, and only then swaps
objsets through rollback. This uses upstream flush/destroy handling, not direct
space-map edits, and zil_suspend refuses an unreplayed log. That refusal still
needs its own rollback-specific fault-injection test.

With this change, `build-aros/admission-service-run.sdoq9ptZ` passes both boots,
unchanged readonly hashes, zero readonly writes and independent zdb No leaks.
Logs retain the failed and successful builds/runs under
`build-aros/auth-rollback-{service-build,client-build,manager-build,qemu}*.log`.
General mounted rollback, recursive rollback/destruction, zvol behavior,
encryption and interrupted-rollback recovery are still unverified or unfinished.

Unchanged confirmation run `build-aros/admission-service-run.TTRELy8Y` also
passes both boots, original/restored byte checks, all denial cases, hashes and
independent zdb No leaks. See `build-aros/auth-rollback-qemu-4.log`. Both successful
test sessions exited normally; the failing disk images were not repaired or
reused. No DUT or shared hosted session was touched.

## Authenticated upstream clone promotion (2026-09-21)

Operation 53 now goes through native admission with the retained MuFS caller,
not the diagnostic fixture transport. The policy follows upstream
zfs_secpolicy_promote: PROMOTE and MOUNT on the clone, PROMOTE on its actual
origin, with the existing zoned-dataset restriction. Both objects are held
under the pool hold for the compound permission check. The upstream
dsl_dataset_promote transaction runs with the caller scoped as CRED.

The service validates a filesystem target (not a snapshot), checks writable
pool state, and refuses live native mounts beneath either clone or origin.
Native snapshot remount/suspend support is still unfinished; this refusal is
not a claim of full mounted-promotion support. No force/options/dry-run or
automatic conflicting-snapshot destruction is accepted. Upstream conflict
names are returned as response rows. The GUI uses the authenticated worker,
labels its input Clone dataset, and explains the permission/unmount contract.

Private two-boot evidence: `build-aros/admission-service-run.Bf0BvHee` passes
write, cold readonly, unchanged readonly hashes, zero readonly device writes
and independent zdb No leaks. Expanded run
`build-aros/admission-service-run.JfjIYFw2` also checks a mounted origin in
addition to a mounted clone, and passes the same full gate. Cases include
outside-grant denial, clone-side grants without origin PROMOTE denial,
non-clone and snapshot-target rejection, snapshot-name collision, successful
promotion, snapshot relocation, exact origin-property reversal, and original
file bytes read through the native DOS handler on both boots. Existing
rollback, holds, properties, record locks, notifications and 96-file tests
remain in both runs. Logs: `build-aros/auth-promote-*.log`.

Actual native GUI evidence: `build-aros/native-gui-run.lc372mYE` creates a
filesystem, snapshots it, clones it and promotes the clone through four
reviewed GUI requests as UID77/GID88. Screenshots 03/06/08/10 retain the full
confirmed requests; 04/07/09/11 retain results. The launcher verifies the
original filesystem now points at gui-promoted@ui, exports, restores caller
identity and reports PASS. After guest exit, verify-native-ui.sh independently
finds the promoted snapshot with UID77/GID88, zdb No leaks and unchanged disk
hashes. No DUT, shared hosted session or remote VPN storage was accessed.

## Authenticated upstream bookmarks (2026-09-21)

Operation 51 creates/copies bookmarks through dsl_bookmark_create. Its Target
is the source snapshot or bookmark; Argument is the destination dataset#name.
Upstream nvlist/name validation and ancestry checks are preserved. Permission
is BOOKMARK on the destination filesystem, matching zfs_secpolicy_bookmark.
Operation 38 accepts a bookmark and checks DESTROY on its filesystem (not the
dataset-destruction MOUNT+DESTROY policy). Missing filesystem destruction is a
no-op after that policy result, without attempting an unchecked later lookup.
Both paths scope the authenticated caller, reject readonly mutation and return
upstream errors. They do not enable on-disk features automatically.

The private admission fixture explicitly enables feature@bookmarks with
spa_prop_set on copied disposable disks. Dataset list supports bookmarks=on
for one filesystem, with native cursor pagination and upstream GUID, creation
TXG and creation time. This is the existing diagnostic read transport, not a
claim that general read/admin authorization is complete. Recursive bookmark
enumeration, redaction and public pool feature administration remain pending.

Two complete native runs, `build-aros/admission-service-run.9aEmHpOX` and
`build-aros/admission-service-run.qE722MZ8`, pass writable+cold readonly tests,
baseline and readonly hashes, zero readonly writes and independent zdb No
leaks. Tests cover separate BOOKMARK/DESTROY grants, denied destination,
invalid source/destination, duplicate create, snapshot deletion with bookmarks
surviving, bookmark-to-bookmark copy, idempotent deletion, readonly refusal,
27 persisted entries over two pages with no duplicates, identical source
GUID/TXG/time including the copy, and a cursor past the end. Prior promotion,
rollback, property, notification and native file tests still run in full.

GUI run `build-aros/native-gui-run.4fwkRdlv` creates the filesystem, snapshot
and bookmark as UID77/GID88 through the real manager. Screenshots 02/04/06
record reviewed mutation requests; 07 shows bookmark success, 09 shows the
actual upstream bookmark row. Saved response/activity, launcher identity
check and orderly export passed. Post-exit verify-native-ui.sh found the
filesystem/snapshot ownership, zdb No leaks and unchanged disk hashes.
Logs are retained under `build-aros/auth-bookmark-*.log`.

Screenshot 09 also exposes a generic UI formatting defect: metadata-only
rows display unused zero capacity/progress fields. This does not represent
bookmark size and needs a separate display correction/retest. No DUT, shared
hosted session or remote VPN storage was accessed.

## Upstream space properties and native DOS enforcement (2026-09-21)

Authenticated dataset.set now dispatches quota, refquota, reservation and
refreservation to their upstream DSL special setters. These are not generic
property writes: upstream limit/accounting checks remain active. Each uses
its own delegated property permission, retained caller credentials, dataset
type validation and readonly-pool rejection. USERPROP permission alone does
not authorize them. The current wire values are exact decimal bytes or none;
unit/fraction parsing, received-property resets and other built-ins remain
unfinished. Inherit is rejected for these non-inheritable properties.

Native two-boot run `build-aros/admission-service-run.ZqFGtnMP` passed the
full gate (`build-aros/space-property-qemu-3.log`). Tests independently grant
USERPROP without space-property permission; cover all four setters, none,
exact local values/sources after cold import, invalid/overflowing values,
snapshot/inherit refusal, upstream ENOSPC constraints and readonly EROFS.
Worker errno checks now compare the complete numeric suffix, not a prefix
that could confuse errno=1 with errno=17.

Native DOS writes separately hit refquota and quota, report ERROR_DISK_FULL,
then resume on the same open file after an authenticated limit increase.
Both limits were enforced (`full=3`); files were removed and original limits
restored before export. Cold readonly verification has zero writes and
unchanged disk hashes; independent upstream zdb reports No leaks. Earlier
runs 0TZjo6zq and OK9OXJ3j are retained as incremental evidence; ZqFGtnMP
adds the strict errno comparison and is the final engine run for this change.

Native GUI run `build-aros/native-gui-run.EhU7c5es` creates a dataset and
snapshot as UID77/GID88, sets quota=16777216 through the authenticated worker,
then reads its real upstream value/source. Saved response/activity, launcher
quota check, export, zdb ownership/integrity and unchanged verification hashes
all pass (`build-aros/space-property-gui-verify.log`). Screenshot
08-quota-readback.png records the actual response without invented counters.
An incomplete simulated keyboard entry was cancelled before submission and
is retained in 04-snapshot-confirm.png; the corrected confirmation follows.
This guest used the older confirmation copy; ba6959e was built separately
while it ran, not injected into its memory. No DUT/shared hosted/VPN access.

### Shared upstream size conversion

The numeric algorithm formerly in libzfs_util.c is now shared through
libzutil's zfs_nicestrtonum_parse. The existing libzfs entry point still emits
the same localized diagnostics and returns the same values/codes. The AROS
authenticated setters call that shared implementation, so decimal fractions
and upstream case-insensitive B/K/M/G/T/P/E/Z suffix syntax are not separately
recreated. This supersedes the exact-bytes-only limitation above. Other
built-in properties and received-property resets are still unfinished.

`aros/engine/test-size-parser.sh` extracts the original functions from pinned
upstream 71a9f9578616a90c3c14bb59629fb4d31bfd68d1 and compares them against the
current wrapper and built libzutil object. All 1,370 conversions match return
codes, output values (including on failure) and diagnostics, both with and
without handles. Inputs include fractional forms, suffix variants, invalid
values and boundaries. Evidence: build-aros/size-parser-compare.FUq2FR4N and
size-parser-differential.log. The complete Linux libzfs.la also builds:
size-parser-libzfs-build.log. These are host checks, not native runtime proof.

Native two-boot admission-service-run.EOPx5GCy passes the full gate, including
authenticated 16M, 8MiB, 1mb and .5M setters with exact persisted values,
readonly refusal, integer/fractional overflow and invalid suffix rejection.
Previous native DOS quota enforcement/recovery and independent zdb/hash
checks still pass. Log: build-aros/size-parser-qemu.log.

Fresh native GUI native-gui-run.Ia1wbz0I submits quota=16M and reads back
16777216 bytes. The launcher quota check, three authenticated transactions,
export, ownership, zdb No leaks and verification hashes pass; see
build-aros/size-parser-gui-verify.log. Screenshots 06-size-confirm.png and
08-size-readback.png verify the real request/result and the shortened
confirmation text from ba6959e. No shared sessions or remote storage accessed.

The earlier direct native libzfs_util.c compile failure is retained in
build-aros/libzfs-util-native-probe.log (missing libintl.h). This work shares
the numeric layer; it does not claim that all native libzfs OS dependencies
or the full administration library have been ported.

### Quota zero-value validation follow-up

Review of upstream zprop_parse_value exposed a semantic gap after parser
integration: quota/refquota must use `none` to disable the limit, whereas the
native setter accepted numeric zero. Reject numeric zero, including parsed
fractions truncating to zero bytes, before permission/transaction dispatch.
This is property validation, not a change to the shared numeric parser.
Reservation/refreservation continue accepting numeric zero, as upstream does.

Native run `build-aros/admission-service-run.R6hRNOqj` passes both boots and
the complete gate (`build-aros/quota-zero-qemu.log`). Each quota rejects 0,
0B, 0MiB, .0, 0.5 and .0001K with exact EINVAL and unchanged value/source.
Both reservations accept 0 on the writable pool, read back 0, and restore
their original suffixed value; readonly writes return EROFS. Explicit none,
DOS quota enforcement/recovery, cold values, readonly zero-write/hash checks
and independent upstream zdb No leaks remain covered and pass.

The next built-in, compression, remains unsupported pending its complete
validation path. Source audit: zfs_check_settable requires pool-version checks
for gzip/ZLE and enabled features for LZ4/ZSTD; zfs_prop_set_special invokes
dsl_dataset_set_compression before the generic property write. Its sync task
activates the ZSTD dataset feature. A generic DSL property write alone would
miss those requirements. Inherit must follow the upstream source semantics,
and mounted-object callback behavior needs native data-path verification.

### Authenticated compression set/inherit and stored-block proof

Compression now uses the upstream index table, property-specific delegation,
filesystem/volume type and snapshot checks, readonly-pool rejection and
retained caller credentials. Match zfs_check_settable's gzip/ZLE version
and LZ4/ZSTD feature gates. Call dsl_dataset_set_compression before the
generic DSL property transaction so ZSTD dataset-feature activation is not
skipped. Inherit uses upstream dsl_prop_inherit, not a synthetic copied value.
Requests do not enable pool features. The copied-disk fixture proves LZ4 and
ZSTD refusal while disabled, then explicitly enables them for positive tests.

Native run `build-aros/admission-service-run.KZFriUp6` passes both boots and
the full gate (`build-aros/compression-qemu-2.log`). Coverage: independent
USERPROP grant cannot set/inherit compression, snapshots and invalid index
values fail, local off then parent LZ4 inheritance/source, mounted transitions
through LZ4/gzip-6/ZSTD-3, three 512 KiB DOS files, exact bytes during the
writing boot and after cold readonly import, and readonly set/inherit refusal.
Earlier lifecycle, property and quota tests continue to pass.

Independent zdb checks (`build-aros/compression-block-verify.log`) resolve
each test filename to its real object and inspect every L0 data block. Each
file has four 128 KiB logical / 4 KiB physical blocks with the expected
algorithm, and ZSTD headers report level 3. Disk hashes remain unchanged by
verification. This distinguishes real compression from a stored property or
a same-boot cache read. The standalone verify-compressed-files.sh requires
both guest passes and no live monitor/storage users; it is also invoked by
the normal two-boot harness after guest exit and zdb No leaks.

Preserve the failed first run yEbwMRA7 and compression-qemu.log: it stopped
at a test's property-string assertion, not an engine fault. Upstream maps
gzip-6 back to the canonical string gzip. Corrected only the expected name;
stored-block evidence independently still requires gzip-6. The initial
fixture marker went to redirected stdout, so it now uses direct serial output.
The initial build warnings and failure images/logs remain retained.

Manager help and its existing authenticated set/inherit routes now cover
compression; the rebuilt manager compiles, but this change has not yet had
its own GUI click-through. No legacy-version pool, ZVOL, all compression
levels, allocation-failure or concurrent administration stress claim is made.
This advances the full port, not a completion claim. No DUT/shared hosted or
remote VPN storage was touched.

Fresh repeat `build-aros/admission-service-run.cEEe2pgK` also passes the
complete two-boot gate with the block-compression verifier integrated, not
run as an optional afterthought. Log: build-aros/compression-qemu-3.log.

The native compression GUI gap is now covered by
`build-aros/native-gui-run.Rs9wCeQM`: five real authenticated operations as
UID77/GID88, visible local ZSTD-3 followed by inherited parent LZ4, independent
post-manager property/source checks, orderly export, zdb ownership/No leaks
and unchanged verification hashes. See ui/NATIVE-UI-TEST.md and
build-aros/compression-gui-verify.log. All screenshots and GUI input actions
are retained, and the private guest exited normally. This does not broaden
the earlier compression-level/legacy-version/ZVOL coverage claims.

## Filesystem and snapshot count limits (2026-09-21)

Authenticated dataset.set now supports filesystem_limit and snapshot_limit.
The shared upstream numeric parser handles counts; none maps to UINT64_MAX,
not zero. Numeric zero is a real limit. Both properties are non-inheritable.
They use their own delegation, dataset type checks and retained caller creds.
For finite limits, dsl_dir_activate_fs_ss_limit initializes recursive counts
and performs feature activation before dsl_props_set. Pool feature enablement
is never implicit; the test fixture explicitly enables filesystem_limits
only after proving finite limits return ENOTSUP while it is disabled.

The userspace-engine build had excluded root's upstream limit exemption under
_KERNEL. The AROS-only condition now includes its real secpolicy_zfs/retained
credential implementation. Other libzpool users retain their original
behavior; kernel zone handling is unchanged. Null/unauthenticated credentials
do not acquire root privilege. link-engine-test.sh explicitly rebuilds the
changed dsl_dir translation unit so stale bootstrap objects cannot mask it.

Native run `build-aros/admission-service-run.qvihg58j` passes the complete
two-boot gate (`build-aros/count-limit-qemu.log`). Root can create a filesystem
and snapshot despite zero limits, then removes them and sets limits to two.
A separate dataset gives MuFS UID77 create/mount/snapshot/destroy but no
limit-changing grants: changing limits fails EPERM; creating the third
filesystem or snapshot fails exact EDQUOT. Descendant objects count against
the ancestor. Deleting an object frees capacity and permits its replacement.
Both limit values and both computed counts read back as two after cold import;
readonly creation/property writes return EROFS. The earlier quota, compression,
byte-integrity, zero readonly writes, backing-file hashes and zdb No leaks
checks remain part of the same gate.

Manager help lists the properties and the existing authenticated set route
is rebuilt; a count-limit-specific GUI clickthrough is not yet claimed.
Broader delegated-limit exemptions, ancestor rename/clone pressure, ZVOL
snapshot limits, exhaustion races and large-count overflow stress remain
untested. Full pool administration and release packaging remain unfinished.
No DUT/shared hosted session or remote VPN storage was accessed.

Expanded repeat `build-aros/admission-service-run.V32xLwQz` also passes
(`build-aros/count-limit-qemu-2.log`), adding exact negative-value, uint64
overflow and snapshot-target rejection for both properties. Linux libzpool.la
build passes (`build-aros/count-limit-linux-build.log`); native client and
manager builds are warning-free. Upstream native replay/ACL build warnings
remain recorded in the service log and are not claimed fixed.

### Concurrent engine count-limit verification (2026-09-21)

The private admission-service fixture now starts two engine threads behind a
condition-variable gate, competing for one available filesystem or snapshot
slot. Each uses an explicit UID77/GID88 credential snapshot without permission
to change either limit. Eight rounds of each operation require exactly one
success and one EDQUOT, with computed counts zero before creation, one after,
and zero after privileged cleanup. Failed objects are retained for diagnosis.
The fixture is not an authentication interface: real MuFS transport tests run
separately afterward. Production admission still executes jobs serially.

`build-aros/admission-service-run.V3GtdFr1` passes the initial return-code
version. `build-aros/admission-service-run.RMIto2aJ` passes the stronger count
assertions and the full two-boot gate, including readonly hash preservation,
independent compressed-block verification and zdb No leaks. Logs are
`build-aros/count-race-qemu.log` and `build-aros/count-race-qemu-2.log`;
service builds are recorded in `count-race-service-build*.log`.

Both runs consistently selected contender 1 as winner. The start gate permits
overlapping calls but does not prove simultaneous internal check phases or
exhaust every SMP interleaving. This is bounded engine concurrency coverage,
not proof that all exhaustion races are eliminated. No production engine
change was needed for these cases, and no DUT or shared hosted session was used.

### Authenticated recordsize administration (2026-09-21)

Dataset set/inherit now dispatches recordsize through upstream DSL with its
own delegation, filesystem-only and snapshot rejection, shared upstream size
parser, power-of-two/range checks, the upstream zfs_max_recordsize tunable,
large_blocks feature gating, and readonly-pool refusal. The existing mounted
ZPL property callback applies changes without a remount. No feature is enabled
implicitly by production requests; the copied-disk fixture first verifies
1 MiB refusal with the feature disabled, then explicitly enables it.

`build-aros/admission-service-run.8eBq44HV` passes the complete two-boot gate
(`build-aros/recordsize-qemu.log`). Authenticated UID77 tests cover permission
separation from userprop, invalid sizes and overflow, snapshot refusal,
512-byte minimum, parent inheritance and exact value/source reads, live set,
readonly set/inherit refusals, and cold byte-exact reads. Three new 2 MiB files
created at 32 KiB, 128 KiB and 1 MiB retain those block sizes after later changes.
The mandatory independent offline zdb verifier finds respectively 64, 16 and
2 L0 file blocks at those logical sizes. Full checksum traversal reports No
leaks; readonly guest I/O counters and backing-file hashes remain unchanged.

The UI uses the same existing authenticated route; manager help includes
recordsize and its build is warning-free (`recordsize-manager-build.log`).
This is not yet a recordsize-specific GUI clickthrough, maximum-size stress
test, crash recovery test, or proof of the entire property API. Service/client
build logs and all copied disk images are retained. Physical DUT, user hosted
session and remote storage were not used.

### Authenticated filesystem readonly (2026-09-21)

Set/inherit now supports filesystem readonly with upstream on/off parsing,
its own delegation, snapshot rejection and readonly-pool refusal. Changes go
through upstream DSL and the existing mounted property callback; no separate
AROS property store is introduced. ZVOL requests deliberately return ENOTSUP:
upstream additionally requires zvol_set_ro, and native volume-device policy
is not yet integrated. A property-only success would be misleading.

Initial native run `build-aros/admission-service-run.TKCblb5A` and expanded
repeat `build-aros/admission-service-run.LbGJ9pEV` pass the full two-boot gate
(`readonly-qemu.log`, `readonly-qemu-2.log`). The real MuFS UID77 caller toggles
a mounted filesystem while holding one open file: readonly refuses writes,
truncate, create, delete and rename with ERROR_DISK_WRITE_PROTECTED. Local off
overrides parent on; inheritance then refuses writes; changing the parent to
off permits writes on the same handle. Exact ABCD bytes survive cold import,
so refused writes neither changed content nor advanced the position.

The expanded case leaves a second dataset readonly=on, verifies that property
after cold import, and refuses an explicit writable mount. A separate explicit
readonly mount remains protected after a privileged on/off property toggle.
Permission separation from userprop, invalid values, snapshot refusal,
readonly-pool set/inherit denial and exact inherited source are covered.
Full zdb traversal reports No leaks, readonly guest write counters are zero,
backing hashes remain unchanged, and the earlier compression/recordsize block
verification passes. Native client and manager build logs are warning-free;
service replay/ACL warnings remain recorded. Dedicated readonly GUI clicks,
ZVOL policy and concurrent mutation/property-change races remain unverified.
No DUT, shared hosted session or remote storage was accessed.
