# Legacy atime transaction reservation

The isolated hosted full-service run in `before/` asserted in
`dmu_tx_dirty_buf`: assigned TXG 0 versus transaction 85, while reading a
version-3 file after upgrading its dataset to version 5 and remounting it.

`oz_znode_flush_atime` reserved the timestamp SA update but not the registered
upstream legacy-znode SA upgrade callback. It now calls
`zfs_sa_upgrade_txholds` before transaction assignment, as other native
metadata mutation paths do. Old-format filesystems and already-SA znodes
retain the helper's upstream no-op behavior.

Fresh engine compilation and shared-library linking succeeded. Both isolated
hosted runs (`after/`, `repeat/`) passed the CLI and Manager version-upgrade,
remount, exact payload read and cleanup checks without that assertion.

Neither full suite passed: a later `zpool list -H -o name public_mirror`
returned status zero but its redirected output was empty. The repeated run
adds exact mismatch diagnostics. This is an open failure, not full-service
qualification. Cold-reader checks were consequently not reached. No physical
DUT, shared hosted session or QEMU was used.

The hosted harness now dereferences copied runtime symlinks so private test
writes cannot follow them back into the source runtime. Source patches and
input identities are retained alongside each run; no VM disks are committed.

## POSIX overlay comparison

`posix-overlay/` repeats the isolated hosted run with source-built POSIX
library SHA-256 `32a510b850fc85c322d71b902bb6ca497945ef17e3269a6bfe2cf9a254feede8`.
It still reports a zero-byte `zpool list` output with exit status zero. Merely
replacing POSIX with that qualified native overlay does not resolve this hosted
failure; do not attribute it to missing POSIX flush support on that evidence.
The upgrade regressions pass again. The harness now accepts an explicit private
POSIX overlay and always records the POSIX library hash, even without override.
