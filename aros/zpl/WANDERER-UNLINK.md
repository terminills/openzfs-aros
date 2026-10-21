# Wanderer unlink and pending access time

2026-09-22, the development test archive. Private two-disk QEMU only; no DUT or user hosted session.

## Observed failure and repair

The native Wanderer regression failed after successfully removing Iconless.txt:
DeleteFile reported object-not-found. The failed run is
`build-aros/native-gui-run.yHtHXKbn`. Additional diagnostics in
`native-gui-run.dLXwvOK9` exposed an initial object-in-use reply followed by
object-not-found, but stderr from the handler task did not reach the fixture's
service log. Serial diagnostics in `native-gui-run.OPIOGWrV` established the
actual failure: `OpenZFS unlink phase=pre-reap sync name=Iconless.txt errno=2`.
The initial busy response is distinct from that later committed unlink.

The mount-wide access-time flush visited an already-unlinked node. The
single-node flusher correctly rejected that node with ENOENT, but propagating
this through mount sync prevented reclamation and reported failure after the
namespace mutation. This was not evidence of a busy reaper.

Cache sync now skips pending atime on unlinked nodes. If unlink wins while a
flush is assigning a transaction, ENOENT is suppressed only after confirming
the unlinked state under the node lock. Other errors remain visible. Dirty
state remains intact until exclusive reclamation, and cache teardown still
rejects unreclaimed nodes. The single-node flusher's contract is unchanged.

## Verification

`build-aros/native-gui-run.GbERZZDS` booted the rebuilt engine and exercised
the real DOS handler as UID 77/GID 88. Screenshots `before-update.png` and
`after-update.png` show the same open Wanderer drawer changing from Before.txt
and Iconless.txt to After.txt and New.txt. No deletion retry was required in
this passing run. Authenticated unmount and pool export returned zero.

After QEMU exited, `verify-wanderer-ui.sh` passed exact file comparisons,
UID/GID checks, unchanged image hashes during inspection, and upstream zdb
integrity with no leaks. Log:
`build-aros/typed-import-service/wanderer-atime-offline-verify.log`.

Additional regression tests cover dirty-unlinked cache sync, retained dirty
state/teardown refusal, and sync before reclaim after actual unlink/read.
`build-aros/qemu-run.vCug1uu1` passed the atime tests and native unlink tests,
but the broader write fixture returned EBUSY at teardown and the runner later
timed out. This is retained as an unresolved regression, not a suite pass.
Further diagnostics are in the write fixture. This change does not claim a
completed OpenZFS port or a newly invented Wanderer feature: it repairs a
regression in the existing integration.

## Low-level fixture follow-up

`qemu-run.OycyVN2V` identified the EBUSY objects as linked nodes 22, 23 and 29
with dirty atime, zero references, no mappings, directory locks or sync users.
The low-level fixture bypassed mount_close and omitted its timestamp flush.
An explicit cache sync before strict teardown repairs that lifecycle omission.

`qemu-run.crrfZp86` then passed the complete write phase, but cold import
reported EIO in the setattr timestamp test. That test read the file after
assigning its deterministic atime, legitimately dirtying it again. The newly
correct final flush persisted that later access. The fixture now checks content
before its final timestamp assignment; cold import still checks both exact
timestamps and bytes. Neither production timestamp semantics nor the expected
persisted values were relaxed. Both failed runs remain retained.

`qemu-run.kYK2s8lO` subsequently passed create, cold import, and survivor0,
but the wrapper's anchored raw-log check rejected an interrupted rename/move
result. Raw serial lines 897-898 contain a secTaskNotify record inserted into
the middle of that successful result. The already-existing normalized log
contains `Native DOS rename/move write=0 result=0`. All subsequent checks now
use that same normalized log, which removes only the specific task-notification
records; raw recordings remain untouched. This is a verifier repair, not a
filesystem change or permission to ignore arbitrary errors.

Final replay `qemu-run.97TjWVQa` exited zero: native creation and DOS/security
tests, cold two-member read-only import, both independent degraded survivors
with zero guest writes, and three writable reopens followed by export passed.
Original raw image hashes were unchanged by the read/reopen phases. Upstream
zdb reported no leaks on both the original images and converted writer COW
images. Full wrapper log:
`build-aros/typed-import-service/atime-normalized-regression-run.log`.
This closes the low-level fixture failures above; it does not imply coverage
of other unimplemented OpenZFS operations.
