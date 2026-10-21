# Repeated native DOS mounts

## Native startup entry adoption (integration in progress)

`oz_dos_mount_adopt` is an internal owner-task API for an already-published,
inactive DOS device entry. It validates exact list identity and name, refuses
an active entry or a conflicting volume, and publishes the real ZPL-backed
endpoint without allocating a duplicate device node. It preserves the caller's
startup metadata and allocation. Failed admission leaves that entry unchanged;
successful detach removes it from DOS but retains it with the endpoint until
clients have joined. Joined unmount clears its task pointer; only its original
owner frees the device allocation.

AROS `RunHandler` can wait with a DOS-list read lock held. The adoption path
therefore uses AddDosEntry's list-read plus entry-write insertion contract,
serialized with this service's raw writes. Normal mount and detach retain the
existing exclusive publication guard. This is not a general relaxation of raw
device ownership or repartitioning checks.

The bounded test supplies an existing device, checks identity/active/volume
refusals, and adopts it while a separate task holds the DOS read lock. Existing
authenticated DOS read, MuFS, retained-reference and remount tests then run
against it. First run `qemu-run.6266f2Qi` failed the existing MuFS metadata
assertion: the new fixture omitted FileSysStartupMsg. The correction supplies
real startup metadata and checks its preservation; the assertion is unchanged.

Corrected `qemu-run.zBx9n96V` passes the complete mirror runner (exit zero):
adoption rollback/read/retirement markers pass in the initial boot, cold import
and both degraded survivors, alongside existing authenticated MuFS and retained
DeviceProc checks. Cold writable reopen also passes; original backing hashes
match. Build and run logs are `startup-adoption-metadata-build-1.log` and
`startup-adoption-metadata-qemu-1.log`. This test uses private runtime overlays;
it does not establish stock-nightly or physical-media compatibility.

This adoption-only result did not exercise a standalone executable. The later
actual Mount handoff and retained startup failures are documented in
`../handler/README.md`. RunHandler returns the process port, but GetDeviceProc
prefers the published custom endpoint; the original test assumption that it
always used the process port was incorrect. Both paths are now exercised.
Service lifetime and retired-endpoint retention remain unchanged.

The destroyed-pool recovery suite exposed an existing lifetime limit:
`admission-service-run.PEyyrAFl` failed with ENOSPC at its ninth mount.
Eight static management entries were retained forever to keep outstanding
DeviceProc references safe. Recycling their ports would be incorrect: an
old reference must never reach a newly mounted filesystem.

Successful detach now removes the closed filesystem's dataset name,
authentication state and other active metadata from the mount array. Its
distinct old port and DOS entries move to a compact retired-endpoint list.
Management allocates one Exec signal owned by the service task. All active
and retired filesystem request ports, notification-reply ports and record-lock
timer ports borrow that signal. The normal service loop pumps every endpoint
after waking; message queues, not signal counts, carry work. Other bounded
handler tests retain their original dedicated CreateMsgPort/DeleteMsgPort
behavior. Shared ports do not free the borrowed signal. A failed mount cleans
up its unpublished ports and filesystem.

The existing resident-service rule is unchanged after any published mount.
Without a proven DOS reference-quiescence protocol, automatic deletion or
port recycling would risk use-after-free or cross-mount packet delivery.
Thus this removes fixed entry/signal ceilings and active-registry history,
not all retained memory. Endpoint memory and linear retired-port dispatch cost
still grow with mount history; bounded endpoint reclamation remains future
work, not a claimed completed feature.

## Detached close failure recovery

DOS detach removes the namespace before ZPL close. A close error must not make
that already-detached endpoint appear mounted. Such entries are marked cleanup
pending immediately, excluded from active mount snapshots and dataset state,
and retried by the owner-task pump before the next request is dispatched. A
same-dataset remount remains `EBUSY` while cleanup is still pending. A
successful retry moves the endpoint into the compact retired list; a later
mount of the same dataset receives a distinct port, while packets sent through
the old DeviceProc still receive `ERROR_DEVICE_NOT_MOUNTED`.

The private fault-injection fixture keeps every close of `OZCLOSEFAIL:` in
`EIO` while a client-controlled RAM sentinel exists. This spans the direct
close, any immediate owner-pump retries and the next request wake. It observes
the unmount error, blocked remount while cleanup remains pending, detached
late-packet response, inactive dataset state, successful retry, distinct
remount port, and final cleanup. The run then completes the ordinary
writable mirror export/reimport and readonly checks with unchanged fixture
backings in registered run `native-import-run.qkjMHY2k`. This proves recovery
and active-registry compaction behavior, not a safe quiescence protocol for
freeing retired endpoint identity.

The two registry-growth allocations have separate private linker wrappers.
Client-held RAM sentinels fail the retirement-node allocation and active-array
growth independently. Each failure must return without publishing a DOS
endpoint or reporting the dataset mounted, and the same dataset must mount,
detach and destroy successfully after the sentinel is removed. Wrapped builds
remain diagnostic-only and are rejected by the distribution staging gate.

## Retired bookkeeping reclamation (in verification)

After successful detach, closed handle/lock records are reclaimed.
Auxiliary ports are retained until the joined destruction path: the inspected
AROS Exec InternalPutMsg enqueues before reading the port again to signal,
so an empty queue alone does not prove the sender has finished accessing the
port. The initial candidate reclaimed drained auxiliary ports, but that was
withdrawn after this source audit; passing functional tests did not establish
SMP sender quiescence. Endpoint identity, DOS entries
and startup metadata remain alive, so this is not a solution to permanent
endpoint retention or a permission to unload the service.

The owner-task diagnostic `oz_dos_bookkeeping_bytes` counts handle/lock
records and auxiliary ports, not endpoint identity or subscription storage.
The repeated-mount test checks it falls to zero after detaching a previously
used mount while retaining actual old DeviceProc and DOS-entry references.
Build logs retain two preliminary failures:
`retired-resources-tests-build-1.log` used an unavailable VERIFY macro (now
replaced with defensive live-resource checks); build 2 enabled the full
management dispatcher in the standalone pool harness and lacked its hooks.

The corrected standalone build (log 3) passes in `qemu-run.FJgWfQnU`:
partitioned mirror creation, both authenticated and fixture retired-reference
checks with zero remaining bookkeeping, cold readonly imports, both degraded
survivors and writable reopen on COW disks. Original backing hashes match
(`retired-resources-mirror-2.log`). The earlier whole-disk attempt
`qemu-run.ICbodABs` failed before handler exercise with spa_create EBUSY while
AROS had AFS claims on the blank disks; the collision guard was not weakened.
Normal full-dispatcher service regression `native-import-run.o9VU5Br1` also
passes with unchanged original backing hashes (`retired-resources-native-1.log`).
The explicit held-notification-after-detach regression subsequently passes
in the write phase of `admission-service-run.DW3fRnnz`, including record
locks, normal notifications, held reply across detach/EndNotify and late
DOS packets. The overall run fails in the cold verification phase during
credential fixture setup before the service is ready; it is not a complete
multi-boot pass (`mount-preflight-admission-4.log`).

## Mount publication contention follow-up

`admission-service-run.seIZI846` timed out before notification checks: the
client deliberately held the DOS entry publication lock during a mount
request, and the service opened writable ZPL storage before publication
failed. Cleanup can synchronize storage while the client is still awaiting
its reply; the log records native dos-entry-contention write failures.
Management now checks publication availability before opening ZPL storage,
releases that check immediately, and retains the final publication check.
This does not provide a lifetime device lease or solve arbitrary later
contention during writes.

`LWEha4i5` exposed a separate client readiness race: the test waited for the
admission port but asserted the management port before publication completed.
It now waits for both. `s1iZnQhx` passes the mount busy/retry sequence, then
fails an obsolete expectation that raw=on is invalid; raw send is implemented,
so the negative input is now raw=invalid. These are retained partial/failing
runs, not complete notification-lifetime acceptance.

`gL1vaalz` subsequently reaches the user-property test and rejects an
unauthorized dedup change with EPERM; the old test expected ENOTSUP from
before dedup support existed. Its expectation now verifies delegation denial.
Normal service build `mount-preflight-service` passes the full selected
mirror regression in `native-import-run.KdTuTfUy`, with unchanged original
backing hashes (`mount-preflight-native-1.log`). This is separate from the
still-running complete admission/notification lifecycle sequence.

Cold diagnostic `571fbvXe` identifies actual CREATE errno=30 (EROFS), not
the fixture's expected EPERM. Upstream ZFS_IOC_CREATE registers
POOL_CHECK_READONLY, and zfsdev_ioctl_common runs that check before its
security policy. The fixture now preserves EPERM on writable imports and
expects EROFS on readonly imports. `pXOdVbQz` then starts the readonly service
and reaches the matching stale client expectation; both client CREATE
refusal checks now follow that same ordering. Production authorization is
unchanged. Full multi-boot acceptance is still pending.

## Retained-port correction and complete admission acceptance

The rebuilt retained-port candidate passes the full write, cold readonly and
replication sequence in `admission-service-run.6U2acqXM`, including held
notification replies across detach in both initial phases. The runner exits
zero (`retained-ports-admission-1.log`). Independent upstream host checks pass
for pool block integrity/leaks, compression/record sizes, persisted timestamps,
and complete full/incremental stream checksums and GUID chains. Original
backing images and extracted member hashes remain unchanged.

The normal service binary independently passes the full selected native mirror
regression in `native-import-run.07VrOJIi` (host exit zero and unchanged original
backing hashes, `retained-ports-native-1.log`). The strengthened offline verifier
also passes separately with executable-tool hashes retained in
`host-verification.vEcW8Bk5` (`retained-ports-offline-2.log`).

The earlier `admission-service-run.pUSQ8WZP` passed its guest phases but its
runner exited one: host zdb was given whole GPT disks rather than their pool
partitions. A separate offline correction passed (`cold-credential-offline-1.log`);
that does not retroactively make the original runner successful. The corrected
runner validates on-disk partition geometry and checks newly extracted private
member copies. It does not change pool data or weaken the integrity assertions.

These functional passes do not establish SMP sender quiescence, remove the
retained-endpoint lifetime limitation, or qualify physical hardware durability.

## Verification

- `admission-service-run.mijXOIwh`: previously failing complete five-layout
  creation/recovery suite now passes, including final non-root import and
  recovery EPERM checks.
- `admission-service-run.F6fYbBk0`: write/cold-read admission regression
  passes. Includes record-lock waits/timeouts, ordinary notifications,
  held notification replies across detach/EndNotify, late DeviceProc
  requests, persistent DOS data/metadata and resident-service behavior.
- The extended creation suite performs forty consecutive same-name mounts,
  retaining the oldest DeviceProc throughout. It checks distinct ports,
  valid current endpoint responses, DEVICE_NOT_MOUNTED from retired ports
  while the new mount is live, then the remaining layout/recovery tests.
  `admission-service-run.fpN1Xgko` passes the forty-retirement marker,
  all five layouts, and the final standalone admission result.

All tests use private QEMU instances and disposable files, not the DUT or
the user's hosted AROS session.
