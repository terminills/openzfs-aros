# Native upstream event access

The native dispatcher already links and initializes upstream FMA. SPA sysevent
publication, however, still selected userland no-op branches. This change enables
the existing upstream create/post implementation for `OZ_NATIVE_DISPATCHER`,
without changing ordinary libzpool userland behavior. It exposes EVENTS_NEXT,
EVENTS_SEEK and EVENTS_CLEAR
through bounded ioctl transport. Read/seek handles resolve only in the caller's
authenticated session; upstream control-file state owns each cursor and its
lifetime. Upstream administrative permission checks remain in force.

`zpool events` opens a native resident control handle rather than a POSIX file
named `/dev/zfs`. Follow uses nonblocking upstream reads with a 100-ms client
sleep while empty, so it cannot block admission of other requests. This is not
a recreated event list: upstream supplies IDs, timestamps, drops and payloads.

Manager operation 34 captures real `C:zpool events`, optionally filtered by
pool, with `verbose=on` and/or `scripted=on`. `clear=on` requires confirmation
and rejects a pool filter or other options. Follow is currently CLI-only;
Manager returns the current queue. Long output uses the existing retained
temporary report path.

The native fixture runs scrub to completion,
reads events twice through independent CLI invocations, reads through Manager,
checks malformed/unconfirmed clearing, clears and verifies an empty queue.
It also checks non-root listing/clearing refusal with the queue retained.
CLI overflow formatting tests are still required;
no physical-hardware claim is made.

Cursor test `public-service-run.pUSm5A8q` (`events-cursor-native-1.log`) passes
the public lifecycle suite with host exit 0. Its authenticated ioctl checks
require an undersized reply to return ENOMEM, then read the event, seek-start
and read the identical EID, seek-end and receive ENOENT, and refuse a nonexistent
cursor with EBADF. Build: `events-cursor-service`, log
`events-cursor-build-1.log`. Evidence is registered under the development test archive.

Retained failed runs: `native-import-run.x8lCsqhS` (`events-native-1.log`)
clears the queue successfully but sees no event after dataset creation.
`native-import-run.XC25PZmm` (`events-native-2.log`) also sees no sysevent after
scrub, whose upstream source explicitly emits start/finish notifications.
Both host exits are 1 and evidence is registered under the development test archive. Inspection
then located the `_KERNEL`-only SPA publisher branches; the corrected build is
`events-publisher-service`. `events-build-3.log` retains the first compile
failure because the FMA headers were still kernel-only; `events-build-4.log`
passes after exposing those headers to the native dispatcher as well.

`native-import-run.zNmWn5dt` (`events-native-3.log`) now returns real scrub
start/finish and history events on repeated CLI reads. Both non-root operations
correctly print permission denied and exit -1. The fixture expected exit 1 and
therefore fails; it is corrected to require -1 and the specific denial message.
No authorization policy change was needed. Host exit 1; later checks not reached.

Corrected run `native-import-run.75fm2iQk` (`events-native-4.log`) passes CLI
listing through independent cursors, non-root list/clear refusal with queue
retained, Manager listing, unconfirmed/mixed-option clear refusal, confirmed
clear and empty queue. It completes the entire native mirror suite with host
exit 0 and unchanged original backing-image hashes. Service is
`events-publisher-service`, CLI `cli-link.qUFL56VW` from probe
`cli-probe.WkV5OaL4`; evidence is registered in the development test archive. Manager UI confirmation
is built but not mouse-click verified by this command-worker test.

## Bounded queue overflow

The focused native control-file fixture publishes 600 upstream events while a
retained control cursor points at an evicted event. It verifies the last 512
sequence values in order, a nonzero upstream drop notice followed by reset,
an independent reader starting at the retained oldest event, and a drain count
of 512 with the reader then seeing an empty queue. It does not reinterpret the
upstream drop counter as an exact count of every missing sequence number.

Build `cli-session.DmTSe9Z3` (`events-overflow-build-1.log`) passes. Initial
platform run `cli-platform.SonDXk4J` stops before this fixture at the separate
process test's DOS Flush error 219; that failure remains registered and is not
claimed fixed. Focused runner `test-qemu-control-file.sh` avoids that unrelated
prerequisite without suppressing it. Run `control-event-run.PTJdjhws`
(`events-overflow-native-2.log`) passes overflow plus all 16 control lifetime
cycles, host exit 0; evidence is registered in the development test archive. No writable host disks,
network, user hosted session or DUT are attached.

## Concurrent CLI follow and cancellation

The native fixture starts real `zpool events -Hf` in a separate AROS process,
executes status and scrub/wait requests alongside it, and then sends native
Ctrl-C. Its exit callback retains child-task lifetime until the parent has
acknowledged completion, avoiding a stale task pointer during signaling.
Captured output must contain scrub-start and scrub-finish. A five-second
completion deadline is a test failure, not an implicit success or restart.

Baseline `native-import-run.1idbguNr` (`events-follow-native-1.log`) services
concurrent requests but emits `FAIL no-break-completion`. Native nanosleep uses
timer.device without converting SIGBREAKF_CTRL_C into EINTR; the follow loop
now explicitly checks and consumes that break signal between bounded reads and
sleeps. After the confirmed failure the exact isolated QEMU was stopped through
its monitor; host exit 1 and registered logs remain. The runner now recognizes
this failure marker directly instead of waiting for its overall deadline.

Corrected CLI build: `cli-probe.dJuL2rs7` / `cli-link.RoxUJ44o`, logs
`events-follow-compile-1.log` and `events-follow-link-1.log`. Corrected run
`native-import-run.KuYo94Iy` (`events-follow-native-2.log`) passes concurrent
status, scrub-start/finish capture and Ctrl-C completion, then the entire native
mirror regression with host exit 0 and unchanged original backing hashes.
Service remains `events-publisher-service`; evidence is registered in the development test archive.
This does not promise preemption of a device request that itself never returns.

## Pool-filter payload lifetime

Source inspection found that upstream's event-printing loop continued past a
nonmatching pool without freeing that event nvlist. The skip branch now frees
it, matching the normal printed-event path. The native regression additionally
requires an unrelated pool filter to produce no output and the matching pool
to retain both scrub-start and scrub-finish. This output test is not a heap-leak
measurement; the lifetime correction follows the explicit allocation ownership.
Build `cli-probe.mSYvP8ox` / `cli-link.NgMGW2me` passes. Native run
`native-import-run.qZInzbPR` (`events-filter-native-1.log`) passes exclusion and
matching scrub-start/finish, all existing events/follow checks, the full mirror
suite and unchanged backing hashes, host exit 0. Evidence is registered in
the development test archive. The explicit free is established by source ownership review; the
runtime test establishes filter behavior, not a measured allocation balance.

## Packaged GUI and broad platform acceptance

Packaged QEMU run `native-import-run.cAQw0K6W` visually verifies Events command
success and oversized-output reporting, the clear confirmation, cancellation,
confirmed clearing of 69 events, and the subsequent empty listing. The actual
empty-queue screenshot is `events-empty-verified.png`; `events-empty.png`
retains an unsuccessful keyboard-shortcut attempt and still shows confirmation.
The full GUI regression did not complete: the 1800-second harness deadline
includes manual interaction, and the harness stopped QEMU after the GUI check
while later tests were running. Exit 1 and signal-15 shutdown are retained;
original backing hashes still match. This is not evidence of a guest crash or
a full passing run. Exact-bundle headless `native-import-run.giW2T68x` remains
the completed regression evidence. The runner now emits distinct completion,
guest-diagnostic, QEMU-exit and deadline reasons instead of a silent exit 1.

The earlier broad platform failure `cli-platform.SonDXk4J` was a missing
dependency: it omitted the established `cli-stdio.xPuSGC6J/stdcio.library`
overlay, whose inherited-handle flag repair is documented in the CLI README.
No assertion was removed and no new runtime fix was needed. Corrected run
`cli-platform.wHBEmTFw` (`events-platform-corrected-1.log`) passes the complete
selected platform sequence, including 16 process/Flush cycles, event overflow
(600 published, 512 retained in order), and 16 control-file lifetime cycles;
host exit 0. Its runtime hashes include the explicit stdcio overlay.

Runner diagnostic qualification `native-import-run.QQuGEaPL`
(`runner-outcome-fail-1.log`) intentionally enables the existing
`AROS_TEST_COMPLETION_FAIL=1` fixture. It exits 1 and explicitly reports
`FAIL: guest completion marker` with the retained completion-log path.
This qualifies the negative completion path, not a successful pool operation
or the other termination branches.
