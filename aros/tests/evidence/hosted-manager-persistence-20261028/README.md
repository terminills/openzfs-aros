# Native Manager hosted snapshot persistence

This uses the matched private hosted runtime and full service qualified in
`../hosted-cli-stream-flush-20261028`. No DUT, shared hosted session or physical
storage was used. Screenshots target the isolated X server's named AROS window.

The first run (`timeout/`) displayed the Manager's live Simple pool listing:
`public_mirror`, ONLINE, 112 MiB raw pool capacity. It reached Advanced snapshot
selection but the 900-second harness deadline elapsed before submission.
Cleanup closed that private runtime; this run is not a complete UI pass.

In the fresh `pass/` run, the actual native UI selected Advanced / Snapshots,
entered `public_mirror/files@hosted_gui`, displayed the confirmation dialog,
and submitted the operation through its authenticated worker. The response
reports errno 0; both response and activity were saved through the UI. No
command-line substitute created the snapshot. Closing Manager allowed the
writer to export normally.

The independent host zdb validates the pool and enumerates the exact snapshot.
A fresh hosted process imports read-only and reports
`Public cold GUI snapshot retained PASS`. The remaining cold DOS/degraded
mirror checks pass, and both backing-file hashes remain unchanged. Inputs,
writer/cold serial logs, response, activity, screenshots and hashes are retained.

This proves this UI mutation and persistence path with these private binaries,
not every UI operation, stock-nightly compatibility or hardware durability.
