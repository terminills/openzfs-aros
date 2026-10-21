# Native upstream pool initialization

Operation 20 is wired through the authenticated admission worker to
`spa_vdev_initialize()`. It is not an independent implementation of free-space
initialization. Root credentials, explicit confirmation, an imported writable
pool, and explicit leaf GUIDs are required.

Request fields:

- target: pool name
- argument: `start`, `suspend`, `cancel`, or `uninitialize`
- options: `guids=123,456` (1–32 unique nonzero decimal GUIDs)

`start` also resumes suspended work. `uninitialize` removes initialization
metadata; it does not undo writes or securely erase a device. Initialization
writes unused space, not allocated files. Force, dry-run and topology fields
are rejected. The existing file-device admission policy still applies.

Upstream can accept some devices while rejecting others. A failed request
therefore retains per-GUID errors and warns that other devices may have
changed. The detail buffer contains the complete bounded error list; structured
error rows are limited to the protocol's 24-row capacity. Do not automatically
retry the entire request.

Pool status includes separately paginated `initialize` rows: state, bytes done,
estimated bytes total, error count, action timestamp and progress per mille.
Like upstream `zpool`, `COMPLETE` reports 100%, even when actual bytes differ
from the estimate. Raw byte counters are preserved. Request acceptance is not
completion.

## Retained verification evidence

All runs use copies of the private two-disk QEMU baseline, with no network,
physical devices, shared hosted session or DUT access.

- `build-aros/admission-service-run.uGAGRLA9`: first assertion failure, errno 5;
  insufficient diagnostics to distinguish the stage.
- `build-aros/admission-service-run.HF50J4p1`: both devices actually COMPLETE,
  zero errors, 191229952 bytes done versus 249118720 estimated. The adapter
  incorrectly reported 767 per mille. Upstream `print_status_initialize()`
  explicitly uses 100% for COMPLETE; the adapter now follows that contract.

The native test initializes both mirror leaves, waits for actual thread exit
and COMPLETE, verifies status, and uninitializes the metadata. The service
tests cover malformed action, unknown GUID, read-only and nonroot refusals.
`build-aros/admission-service-run.fNIc0pIi` passed WRITE and cold VERIFY,
including initialization/uninitialization, authenticated service tests, native
DOS payload/property tests, zero writes on both read-only members, unchanged
baseline hashes, host `zdb -bcc` with no leaks, independent compression and
persisted atime verification. Runner exit status was 0. The log is
`build-aros/typed-import-service/pool-initialize-run-3.log`.

## Remaining verification

`build-aros/admission-service-run.dfFzKbMv` additionally passed a mixed
uninitialize request (valid leaf changed, invalid GUID reported, other valid
leaf remained COMPLETE), plus 32 invalid GUIDs with all errors present in
detail despite the 24-row limit. The full WRITE/cold VERIFY suite and offline
checks passed again, runner exit 0; see `pool-initialize-run-4.log`.

Native GUI run `build-aros/native-gui-run.Aa7ufyVw` used the real Manager to
submit `start` for both leaves, then separately query status and export.
`04-initialize-confirm.png` records exact fields and flags;
`06-initialize-result.png` records accepted control; `08-status-result.png`
shows the live status response (the entire response is retained in serial);
`12-export-result.png` records successful export. The launcher confirms an
empty imported namespace and native GUI PASS. The saved response in
`serial.log` has both COMPLETE rows, nonzero bytes, 1000/1000 and zero errors.
After normal QMP shutdown, `aros/ui/verify-initialize-ui.sh` passed offline
`zdb -bcc` (no leaks) and unchanged inspection hashes. The verifier log is
`build-aros/typed-import-service/pool-initialize-gui-verify.log`.

## Active control and cold state

The `initialize` admission suite sets QEMU's write limit to 8 MiB/s per copied
member during WRITE only. This leaves a real interval for suspend/cancel;
there is no production engine delay or synthesized initialization state.
`WRITE-INITIALIZE` checks suspend with both threads stopped, start/resume,
cancel with both threads stopped, and restart through actual completion.
`VERIFY-INITIALIZE` checks persisted suspension after a fresh read-only boot.

- `admission-service-run.P69f9cDI`: active suspend/resume/cancel and restart
  completed; full WRITE/cold VERIFY and offline checks passed, exit 0.
- `admission-service-run.qGJdiSu8`: retaining suspension exposed an old device
  test which incorrectly required progress rows to be ONLINE. The health
  assertion now excludes initialization rows; dedicated initialization tests
  still validate their states.
- `admission-service-run.PoTCdZga`: WRITE passed; cold suspended-state check
  failed because upstream only calls `vdev_initialize_restart()` in writable
  import. The original read-only status response omitted stored state.

The status adapter now reads the upstream initialization ZAP fields into its
owned response on read-only pools, without changing live vdev state or calling
restart. These rows explicitly say `stored-only=readonly counters=unavailable`.
They retain saved state, action time and physical next-offset. The offset is
not presented as bytes completed; unknown byte totals/counters remain zero
with the availability qualifier. Non-ENOENT metadata errors propagate.

`admission-service-run.ZiNZudoA` passed the corrected complete suite:
active suspend/resume/cancel/restart, persisted suspension, fresh read-only
status with both saved suspended states, native DOS regressions, zero writes
on both read-only members, unchanged baseline/inspection hashes, host zdb no
leaks, independent compression and atime verification. Runner exited 0;
see `build-aros/typed-import-service/pool-initialize-controls-run-4.log`.

`admission-service-run.fMRGkMEc` extends this to three distinct guest boots:
WRITE leaves both leaves suspended, VERIFY proves the stored state read-only
and hashes are checked before the next boot, then RESUME imports writable,
requires both runtime states still SUSPENDED with no initialization threads,
starts upstream initialization and waits for COMPLETE/1000/1000/errors=0 on
both members. It exports and shuts the engine down cleanly. The entire runner
and final offline data/metadata checks passed, exit 0; see
`build-aros/typed-import-service/pool-initialize-controls-run-5.log`.

Persistence of canceled/completed states still needs additional coverage.

## Read-only GUI verification

`build-aros/native-gui-run.fZiTutqQ` booted private copies of the verified
suspended pool from `ZiNZudoA`. The Manager queried real read-only status;
`03-stored-state.png` shows SUSPENDED and explicitly unavailable runtime
progress rather than fictitious zero counters. `06-export-result.png` records
successful export. The complete saved response/activity are in `serial.log`,
followed by exported-namespace and native GUI PASS markers. After normal QMP
shutdown, `verify-initialize-ui.sh` verified both stored rows, unchanged hashes
over the whole GUI session, offline zdb no leaks and unchanged inspection
hashes (exit 0). See `pool-initialize-readonly-gui-verify.log` under
`build-aros/typed-import-service`.

The native UI runner now freezes the service and Manager binaries before
packing, and records initial disk hashes. Its dedicated read-only scenario
requires a verified, inactive initialization evidence directory; it never uses
the user's hosted instance or physical disks.
Physical-device durability is not established by these QEMU tests.
