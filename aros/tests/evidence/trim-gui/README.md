# Native Manager TRIM error presentation

Private QEMU run `native-gui-run.bKsJx5Lx`, eight CPUs, alpha18c5 with
the recorded private platform overlays. Runner completed with exit zero.
This is old-baseline qualification, not validation against the newer stdc ABI.

The actual Manager queried `public_mirror` and displayed both leaf GUIDs.
The first review (without options, due to a missed field click) was cancelled;
nothing was submitted. The second review explicitly displayed both GUIDs and
`start`. Submission started the authenticated worker. Transport and release
errno were zero. Both selected leaves returned error 45 (unsupported), with
aggregate errno 22. The response displayed both device failures and warned
that other devices could have changed. No automatic retry occurred.

Screenshots retain the pool status, exact confirmation, and final response.
After closing the fixture drawer and Manager normally, the runner verified
export, read-only reimport, and exact handler reads, ending in storage PASS.
Wanderer was not terminated manually. The generated ISO was pruned by the
runner; input hashes, action trace, serial results and runner log remain.

This qualifies the real UI-to-engine error path. It does not prove successful
discard: the current file-backed devices do not support TRIM. It also does
not claim physical-device durability or complete OpenZFS feature coverage.
