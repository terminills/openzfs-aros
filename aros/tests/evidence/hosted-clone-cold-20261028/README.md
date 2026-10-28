# UI-created clone survives cold import

Private hosted run BwSDqWzq uses freshly compiled test client 6md9tmUy,
MetaMake Manager/CLI zqQd3gIm and source-built stdcio/POSIX from the preceding
qualified runs. Actual UI confirmation submits the clone of
public_mirror/files@gui_clone to public_mirror/gui_clone with
compression=off;quota=32M;mountpoint=none. It separately creates the
hosted_gui snapshot. Screenshots and the activity record capture both paths.

The fixture's persistent marker now keeps the clone and source snapshot
through orderly export. The cold reader verifies properties and exact origin,
mounts the clone read-only as OZGUICLONE, reads hello.txt exactly, requires
ERROR_DISK_WRITE_PROTECTED on creation, and detaches. The checks pass during
healthy, missing-member/degraded and restored reads (three PASS markers).
Independent host zdb reports no leaks and enumerates the clone and hello.txt.
Both full runtime preflights pass; final backing-file hashes match those
captured before the cold reader. No shared session, QEMU or DUT was used.

The nonpersistent GUI fixture retains its previous cleanup behavior.
The hosted harness accepts an explicit freshly built client and hashes the
deployed binary. All files under this evidence directory are logs, sources,
hashes or screenshots, not VM backing disks.

QEMU coordination: MAME explicitly released its informal reservation for the
bounded OpenZFS debugger run. Bluetooth VM btusb401 remained live and untouched;
TRIG-1672 requests owner-controlled release. No stale lease or elapsed time is
treated as permission to terminate that VM or bypass the single-QEMU rule.
