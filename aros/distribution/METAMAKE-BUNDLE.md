# OpenZFS AROS MetaMake development bundle

Experimental storage software. Use disposable data and keep independent backups.
This is not a general stock-nightly or physical-device qualification.

The application layout is C/, L/, Libs/, and Tools/. The service must be running
before mounting an imported dataset. Do not replace a running resident engine;
export pools and restart AROS after binary changes. No installer or automatic
pool creation is included. Never guess a physical device's unit or partition.

**Platform/ is separate and must not be blindly installed.** Its posixc.library
is the matching alpha18 runtime with the appended pipe2 vector used by the
native test suite. It is an explicit platform integration dependency, not an
application library. A different AROS runtime/SDK can have an incompatible Task
or Process layout even when the CPU target is identical. See BUILDING.md for
the ABI, pthread, pipe2, and file-backing flush requirements. The private FAT
overlay used for persistent file-backed tests is not included or installed.
Those tests do not prove that arbitrary backing filesystems flush safely.

Metadata/qualification.json identifies the exact selected inputs and evidence.
The GUI and cold runs qualify the selected shared library, launcher, zfs/zpool,
handler, device and runtime dependency; Manager is covered by the GUI run.
The four startup/runtime/host-identity utilities are included as build-only
companions: this bundle's referenced tests did not execute those utilities
individually. Do not infer their current standalone runtime qualification.

Metadata retains test logs, source identities and checksums. SHA256SUMS covers
the staged files. Validate it after transfer. Metadata may contain the build
machine's local paths; it contains no physical storage or test-disk images.
The bundle has no security test credentials and does not install startup records.

Active preferences belong in ENV:, persistent preferences in ENVARC:. Import
and mount must be tested manually before enabling trusted startup records.
This port is not the older private AROS ZFS on-disk format.
