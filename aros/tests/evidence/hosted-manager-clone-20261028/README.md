# Current native Manager clone UI

`pass` is private hosted run WtvZt0s1 with the MetaMake zqQd3gIm Manager/CLI,
source-built stdcio gphvn1pg, POSIX flush j099bkwp, matched stdc and the hosted
loader-policy DOS overlay. No DUT or user-hosted session was touched.

Actual UI interactions selected Advanced / Snapshots / Clone, entered
public_mirror/files@gui_clone -> public_mirror/gui_clone with
compression=off;quota=32M;mountpoint=none, reviewed the exact request, and
submitted it. Upstream zfs clone returned exit 0. After closing Manager,
the existing independent fixture queried quota, compression, mounted state
and origin, then removed the clone and its source snapshot. It reports
`Public GUI clone PASS quota=1 compression=1 no-mount=1 origin=1 cleanup=1`.
The harness now supports explicitly selecting this fixture and exact Manager
binary, and requires that marker for acceptance.

The UI also created public_mirror/files@hosted_gui. Independent host zdb
verified the snapshot; fresh cold/degraded read-only import retained it and
left both member hashes unchanged. Both full runtime preflights passed.
The clone itself was intentionally cleaned up before export; this evidence
does not claim cold-persistent clone coverage.

`timeout` is dDbEddwZ. Initial automated typing was malformed, inspected and
cancelled without submission. Corrected clone submission succeeded, but the
900-second harness deadline expired before independent clone and cold checks.
That attempt is incomplete, not a verified pass or an AROS crash. Its logs
and all captured screenshots are retained. The fresh pass used deliberate
input pacing and separate xdotool type invocations.

Screenshots capture the named AROS window on this run's private Xvfb display.
This qualifies these operations and binaries, not all UI features, a stock
nightly runtime, QEMU or hardware durability.
