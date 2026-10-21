# Native upstream zfs diff

## Implemented transport

On AROS, libzfs_diff opens/closes its cleanup control handle through the
authenticated native session API, not the Linux ZFS_DEV pathname. Failure
returns a libzfs error instead of asserting. Other platforms retain upstream
open/close behavior.

ZFS_IOC_DIFF uses the retained stream channel and a service-owned writer
descriptor. The client descriptor is duplicated locally, never imported into
the service's descriptor table. Admission requires a bounded inline source
snapshot name and destination name, rejects unrelated payloads/fields, replaces
the cookie only for execution, and restores it before returning. Upstream
zfsdev_ioctl_common retains diff permissions and snapshot validation.

OBJ_TO_STATS and NEXT_OBJ are admitted as bounded read-only queries. They can
run on workers while a stream is active, so the DOS owner task remains able
to pump traffic. Diff's client releases the connection mutex during streaming
but counts the request until final acknowledgment; control closure waits for
that count to reach zero. This allows the consumer pthread's object queries
without invalidating the session underneath the producer.

## Native evidence, 2026-09-28

- OIeMTV99: real command aborted at native-inapplicable control-device open;
  runner exit 1. Log cli-diff-red-native-1.log.
- bQGOrh7D: native control, stream and object-stat path printed the added file
  and xattr paths, then returned status 1 on missing NEXT_OBJ admission.
  Log cli-diff-native-1.log. This is partial output, not success.
- QIfczMlj: actual `zfs diff -H` between two snapshots returns status 0 and
  includes the file created through native DOS between those snapshots. Both
  snapshots and the dataset are cleaned up. The full remaining native
  regression exits 0 and original backing hashes match. Log
  cli-diff-native-2.log. All three evidence sets are registered under the development test archive.

The passing service is source-engine.P2gN7Bsv/diff-next-service, rebuilt from
the freshly compiled engine with the NEXT_OBJ adapter change. Its predecessor
service remains separately retained. CLI cli-link.EFsIw7on uses freshly
compiled common units from cli-probe.nC0lHXVJ. Private pipe-policy.EzbhH6gz and
the existing private DOS/POSIX/ATA overlays are still used. The fixture mounts
PIPE before invoking diff and is excluded from read-only cold-reader runs.

## Remaining verification and implementation

The extended run jv5dEUJy (`cli-diff-changes-native-1.log`) checks exact `-H`
output records for addition, modification, rename (both paths), and removal.
The real CLI returns success; dataset/snapshot cleanup, the remaining native
regression, and unchanged original backing hashes pass. Evidence is registered
under the development test archive. This remains an explicit-two-snapshot test, not full diff
qualification.

Run uik9XpXJ (`cli-diff-live-native-1.log`) additionally passes both omitted
destination and explicit live-filesystem destination. TMP_SNAPSHOT admission
validates bounded inline fields and uses the authenticated session file scope;
upstream performs authorization and registers its own on-exit destruction.
Both diff commands succeed, followed by nonrecursive dataset destruction after
removing only the two explicitly created snapshots. The full regression exits
0 and base image hashes match. Service: source-engine.P2gN7Bsv/diff-live-service;
CLI and private overlays are unchanged. Evidence is registered under the development test archive.

Manager operation 58 now routes through the existing asynchronous exact-command
worker with inherited native identity, not a shell. It accepts optional target
snapshot/filesystem and explicit classify/scripted/timestamps/unescape options.
The native Manager builds as manager-diff-worker/OpenZFSManagerNative. Run
MUyxIw4R (`manager-diff-worker-native-1.log`) exercises this worker for explicit
snapshots and implicit live comparison, rejects six malformed/unsupported
requests before execution, and passes cleanup plus the full native regression
with unchanged base hashes. Evidence is registered under the development test archive. Output
capture in that revision was bounded by the response buffer and reported
overflow as an error. The retained-report extension below supersedes that
behavior. This headless run is not visual UI proof.

Clone comparisons, broader delegated/group combinations, encrypted snapshots, malformed
transport requests, large concurrent query loads and interruption still need
their own implementation or runtime checks. The headless run above does not
establish visual behavior; see the subsequent graphical run below. No production ABI,
stock-nightly compatibility or hardware claims follow from this run.

The native delegation fixture now switches to MuFS user ozreader (77:88),
checks worker failure without permission, grants only upstream `diff`, verifies
live comparison output, revokes that grant and checks failure again. It restores
the root fixture identity after every completed call, then nonrecursively
destroys the dataset after removing the two named snapshots. This exercises
the same worker as Manager, including its child-process identity inheritance.
Run iQ81gQBF (`manager-diff-delegation-native-1.log`) passes this sequence and
the full regression with exit 0 and unchanged original backing hashes. Both
denied responses specifically report that diff delegated permission is needed;
the permitted response contains the changed file. Evidence is registered under
the development test archive. This does not qualify every delegation/clone/encryption combination.

## Graphical diff verification

`AROS_TEST_DIFF_GUI=1` with `AROS_TEST_GUI=1` pauses the native regression
while the diff fixture still exists and opens the supplied Manager. Closing
Manager resumes nonrecursive fixture cleanup and the rest of the regression.
This switch does not automatically establish that a UI action was performed.

Dh7YoEQm reached that fixture but was deliberately stopped: the storage-worker
QEMU lacks `screendump`. No GUI pass is claimed for that attempt.
FqpKb9gZ uses the graphics-capable /usr/local/bin/qemu-system-x86_64
(d93c221a08ef8ad69d0ecc77cd52920927d22f82d12177a0c609aae48b7a823f).
`diff-fields.png` records the actual request, and `diff-completed.png` shows
Manager returning exit=0, DOS=0, capture errno=0 with modified, renamed,
removed and added paths. `diff-after-close.png` shows clean worker drainage.
The UI action is visually verified; final regression/hash results are separate.
After closing the final inspection window, this run completed with exit 0,
the full native regression passed, and both original backing-image hashes
matched. Log: manager-diff-gui-native-2.log. The evidence set, screenshots and
recorded QMP actions are registered under the development test archive.

## Large output retention

The shared project/diff command worker retains its exclusively created report
when the output exceeds the response buffer. The response gives the complete
`T:openzfs-command-*` path and explicitly warns that it is temporary, not
persistent storage. It does not return a truncated file list as complete.
Command failure still produces an error status; report retention only removes
the response-buffer overflow condition. Other capture/close failures remain
errors. Small reports are still collected and removed automatically.

The new native fixture creates 128 files after a named snapshot, requests a
live diff through the Manager worker, opens the returned report independently,
and requires each exact added path once before explicitly deleting the report.
Graphical paging and a persistent save-file chooser are not implemented by
this change. The complete file remains accessible through native DOS tools.

Run 0fjGfa87 (`manager-diff-report-native-1.log`) passes all 128 exact-path
checks, report cleanup, and the full native regression with exit 0 and unchanged
original backing hashes. Manager builds as manager-diff-report/OpenZFSManagerNative.
Evidence is registered under the development test archive; this run tests the worker, not a new
graphical interaction.
