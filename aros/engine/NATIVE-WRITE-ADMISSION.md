# Native write admission work

2026-09-25, the development test archive.

`pool-import.c:pin_native` deliberately rejects writable imports, while
`pool-create.c` only admits file backing. Do not remove these restrictions
solely because direct GPT writes succeed. Imported native pools retain their
opened device identity and extent, but currently bind them read-only.

## Write/check publication interval

Previously `oz_device_transfer` checked recognizable DOS claims, released the
DOS-list read lock, then submitted the write. A DOS device could be published
between those steps. Each write chunk now attempts an exclusive
`LDF_ALL|LDF_WRITE` interval encompassing both claim check and synchronous
device I/O completion. An ordinary shared lock does not suffice: AROS
`AddDosEntry` itself takes a shared DOS-list lock.

Acquisition never waits, because a DOS-list owner may be waiting on the
service. Contention returns EBUSY without permanently revoking the handle.
An actual matching DOS device entry still causes sticky revocation. Each
path after successful lock acquisition releases it, including claim and I/O
errors. Existing GPT transactions can acquire it recursively as the same
task. Flushing previously submitted I/O remains possible after revocation.

This is **not a lifetime ownership protocol**. DOS publication between chunks
remains possible; a later write then refuses it. Opaque custom filesystem
startup records and unrelated raw-device users remain outside recognition.
General multi-vdev writers also need a policy for transient publication-lock
contention; turning EBUSY into an unconditional retry would hide real claims.
No native writable import/create gate is changed by this work.

## Evidence

`build-aros/device-range-run.5NlnIIvn` exited 0. Its fresh disposable 256 MiB
and 8 GiB images test cross-task DOS-lock contention while joining the writer
under the held lock (proving nonblocking rejection), unchanged disk bytes,
successful subsequent write on the same handle, and restoration. Existing
late-claim/sticky-revocation, extent reservations, cross-4-GiB transfer, real
upstream GPT transaction, host GPT verification and cold-boot persistence
also pass. Original backing-image checks pass.

The full service is rebuilt in `build-aros/full-native-write-guard`; build log
is `build-aros/native-write-guard-build.log`. This tests actual engine linkage,
not only a standalone shim. Authenticated integration results are recorded
separately with their terminal runner outcome.

`build-aros/native-import-run.Cc06K7XW` exited 0 using that rebuilt service.
It passes two native mirror import/DOS read/export cycles, authenticated GPT
worker refusal cases and actual writes, and real libzfs label/relabel calls
including configured start inheritance. Host sgdisk verifies the final GPT;
both original disks and the untargeted overlay retain their original hashes.

## Retained writable bindings

`oz_device_bind_writable` now accepts an already reserved writable extent.
It retains that exact device handle rather than reopening a path. Readonly
handles cannot be upgraded, revoked writers cannot be bound or opened for
writing again, and an already bound writer cannot gain a second path alias.
The existing readonly and explicitly unavailable bindings retain their error
behavior. A bound open is an internal capability reuse, not fresh caller
authorization; per-pool path policy still belongs to admission.

Unbinding removes future name lookup, but handles already returned to the
engine continue owning their reference and writer exclusion until final close.
`device-range-run.4PdMchcU` and expanded `device-range-run.zbD7i7GC` both exit
0. The latter verifies readonly upgrade refusal, duplicate/alias refusal,
exact handle/extent preservation, real writes after admission close and
unbind, overlapping writer exclusion, eventual reservation release, and
revoked bound-write refusal with reads still available. Existing range,
cross-4-GiB, GPT, cold persistence and backing-image checks pass as well.
The post-test nested conditional in bound-open was rewritten as equivalent
explicit branches for readability; full-engine build succeeds at
`build-aros/full-native-write-binding`.
`native-import-run.2Z2IXnPg` then exits 0 with that full service, covering
readonly mirror DOS access, authenticated GPT worker operations, actual
libzfs label/relabel calls and original/untargeted-image hash preservation.

This API is not yet enabled by native pool import or creation. The pool
admission/lifetime protocol remains unfinished; concurrent writer scheduling
was the next investigated prerequisite, below.

## Concurrent writer regression

The new two-worker/disjoint-extent test reproduced a real internal conflict
in `device-range-run.zVPhv8R3`: the runner exits 1, with `errno=16 first=16
second=0`. One writer mistook the other writer's DOS publication interval for
external lock contention. Both original backing files were separately
hash-verified unchanged after this expected failed test.

Native write chunks now queue on a service-local mutex before attempting the
exclusive DOS-list interval. The mutex is separate from binding/reference
locks. It is released on revoked-handle, DOS-contention, claim, and I/O-error
paths as well as success. It does not serialize reads or busy-wait on DOS.
This preserves nonblocking failure against external DOS-list owners, while
two legitimate service writers no longer reject each other. This is an
intentional throughput tradeoff matching the global DOS-list exclusion, not
a claim that parallel disk throughput has been optimized.

`device-range-run.5Kl5aF6T` exits 0 with both writers succeeding, all 32 chunks
and readback verified. The existing external-DOS contention test still returns
promptly and allows a later retry; sticky claim revocation, writable bindings,
cross-4-GiB I/O, GPT transactions, host validation and cold persistence also
pass. Both original backing images remain unchanged.
Full rebuild `full-native-write-queue` and authenticated integration run
`native-import-run.D5RwUh22` also exit 0, including real GPT/libzfs operations,
mirror DOS reads, and original/untargeted-overlay hash checks.

## Explicit writable native import

The earlier import gate is now replaced with the tested retained writable
binding for an explicitly requested `readonly=off` import. Existing root
authorization, writable-service requirement, exact per-pool member path
policy, upstream label discovery, minimum size, extent reservation, DOS claim
checks and sticky write revocation still apply. Native bindings bypass only
the file-specific parent-directory handoff, not admission validation.
Readonly import behavior is unchanged; creation still admits files only.

This is cooperative device ownership, not a system-wide raw-I/O capability
enforcement mechanism. Operators must exclude unrelated raw-device users and
not mount the same storage elsewhere. Recognizable new DOS claims stop writes,
but opaque/custom handlers are not covered. Do not advertise universal disk
exclusivity or silently detach foreign mounts.

Evidence, all private QEMU overlays using `full-native-writable-import`:

- `native-import-run.pfcpKGRt`: FAIL at writable DOS mount after successful
  writable pool import. The exact errno was not yet logged; remains an
  unexplained initial failure, not retroactively a pass.
- `native-import-run.s3upfBhw`: writable DOS mount/write/flush/export/reimport
  and exact readback PASS, then an extra import cycle fails. Inspection found
  the test helper leaked completed admission tickets; it now releases every
  successfully retrieved result. Both failed runs' original backing images
  were subsequently hash checked unchanged.
- `native-import-run.D4b1RUV7`: exit 0 with that helper correction. Actual
  native mirror writable import, AROS DOS file creation/write/flush/close,
  unmount/export, readonly reimport and exact file readback; followed by two
  existing readonly mirror/DOS cycles. Original backing images unchanged.
- `native-import-run.Qxb2oq0Z`: separate cold QEMU boot, exit 0. Uses fresh
  readonly-test overlays backed by D4b1RUV7's saved overlays, verifies the file
  from the earlier boot without rewriting it, then runs readonly regressions.
  Original images, prior write overlays, and cold-test overlays all unchanged.
- `native-import-run.KwC3csNp`: independent writable-repeat exit 0 with the
  same write/export/reimport/readback and readonly regressions.
- `native-import-run.8zn8yDpd`: authenticated GPT worker and actual libzfs
  label/relabel/configured-start regression exit 0 with this same full service;
  originals and untargeted overlay unchanged.

Runner modes: `AROS_TEST_NATIVE_WRITABLE=1` explicitly permits both disposable
mirror overlays to change; `=2` requires a prior successful writable run via
`AROS_TEST_NATIVE_VERIFY_RUN`, and permits no overlay changes. Neither mode
weakens original backing-image hash checks. No DUT or shared hosted runtime
is involved. Native RAIDZ/degraded writable recovery, native pool creation,
and broader fault/race testing remain incomplete.

### Degraded writable mirror verification

The writable runner also accepts member `0` alone or `1-empty-peer` (member
1 plus an unrelated empty GPT disk in the master slot). Only attached original
member overlays may change. The absent member overlay and the empty peer are
hash protected. Cold verification requires the same member selection as the
successful write run. The test checks DEGRADED health before mounting writable.

With `full-native-mount-diagnostic/OpenZFSServiceNative`:

- `native-import-run.XUKFkXVw` and `native-import-run.ge3G3bN2` failed writable
  mount with EBUSY after import. Original backings and untargeted overlays were
  checked unchanged afterward. These failures remain unresolved.
- `native-import-run.Pke5hWBL`: member 1 with empty peer, writable DOS
  creation/flush/export/reimport/readback and readonly cycles PASS. Empty peer,
  absent member overlay and original backings unchanged.
- `native-import-run.loMmtdSF`: separate cold boot against Pke5hWBL's saved
  member overlay; persisted file readback and readonly cycles PASS. Original
  backings, prior write overlays and all cold-test overlays unchanged.
- `native-import-run.SAv83JNw`: member 0 alone, writable DOS file roundtrip
  and readonly cycles PASS; originals and absent member overlay unchanged.

Failure-only mount-stage serial diagnostics were added to localize the
intermittent EBUSY. No underlying race fix is claimed: subsequent passes do
not explain earlier failures. Native RAIDZ, native creation, resilver and
broader fault/race verification still remain.

Follow-up contention localization:

- `native-import-run.KP7lPLpe` passed; `l2fNvdcM` reproduced mount EBUSY
  without any ZPL-stage failure diagnostic.
- Rebuilt service `full-native-dos-diagnostic` adds a failure-only diagnostic
  around the existing nonblocking DOS-list publication lock, without changing
  behavior. `lEV82sbp` passed; `xDQ5TKPR` failed with
  `OpenZFS DOS mount failed stage=publication-lock errno=16`.
- Both failed repeats' original backing images and unattached member overlays
  were hash checked unchanged. This identifies the refusal site, not the lock
  holder or a complete fix. Investigate contention with engine-native write
  guards and unrelated DOS clients; do not replace the nonblocking check with
  an unconditional wait while this service owns volumes clients may await.

### Coordinate publication with native write chunks

Mount and detach now take the same service-local queue used by native write
chunks before attempting the DOS-list locks. This removes contention between
our own publication and our own raw writes. It does not wait for an external
DOS owner: a failed AttemptLockDosList still returns EBUSY and releases the
queue. Only bounded namespace edits run inside the interval; disk insertion
events, engine I/O and cleanup run after release. Callers may not enter with
DOS-list or engine locks held.

Validation with `full-native-publication-queue`:

- `device-range-run.8kDL9oHe`, exit 0: two disjoint writers (32 chunks) plus
  64 publication intervals, exact readback, external DOS-lock refusal for both
  writes and publication, subsequent retry, existing bounded-I/O/GPT tests and
  cold GPT persistence. Original backings unchanged.
- Five independent full writable degraded mirror runs passed DOS file
  creation/flush/export/reimport/readback plus readonly cycles:
  `KaZFZ5bQ`, `UtwH1z5l`, `Of8gNVmm` (member 0), and `CCGcwhfG`, `dhyb203f`
  (member 1 with unrelated empty peer). Each run verified immutable original
  backings and untargeted overlays.

These results address the reproduced service-local contention path. External
DOS contention remains an explicit retryable refusal; this is not proof of
general stress stability or a diagnosis of every earlier uninstrumented error.
