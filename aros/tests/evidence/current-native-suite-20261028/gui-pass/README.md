# Native Manager and Wanderer acceptance, 2026-10-28

Current candidate: 1e7c854. Private eight-CPU QEMU, no DUT or user-hosted
session. This is runtime qualification, not a new engine implementation.

The original `native-gui-run.YTmahcmq` reached the desktop and returned a
real pool health response, but its harness terminated before the interactive
save/close sequence completed. That original run remains partial, not PASS.

`build-audit/gui-replay-20261028` booted the same read-only ISO again, with
fresh RAM-backed test pools and private serial/QMP/monitor endpoints. No
member disks were shared. ISO SHA-256:

    481071040550ef8fc1cfd6607c693ab803f08d95c9141f95d5b39023d455f12d

The original run's input and actual-extracted-ISO runtime manifests apply to
this unchanged image; copies accompany the replay directory. The guest
executed the full startup sequence, rather than resuming an old VM snapshot.

Actual UI sequence: choose Pool health, enter `public_mirror`, Show results,
Save response to `T:OpenZFS-Response.txt`, close the pool drawer and Manager.
Wanderer remained running. Screenshot feedback corrected virtual-tablet
placement; one helper attempt mistook requester icon pixels for the pointer
and refused to click. No guest input settings were changed.

The retained serial proves:

- `Public GUI health saved response PASS`: raw topology, counters, scan and
  checkpoint fields, rather than only the abbreviated display summary.
- `Public desktop post-UI data PASS exact-read=1`.
- Authenticated mirror DOS write/read and read-only reimport PASS.
- `Public service test line=0` and `RESULT public service storage PASS`.

The VM was quit through its own QMP endpoint only after terminal acceptance.
Screenshots retain the health response, save confirmation and post-UI desktop.
This does not establish physical-device durability or whole-port completion.
