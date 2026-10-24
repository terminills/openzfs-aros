# OpenZFS AROS MetaMake development bundle

Experimental storage software. Use disposable data and keep independent backups.
This is not a general stock-nightly or physical-device qualification.

The application layout is C/, L/, Libs/, and Tools/. The service must be running
before mounting an imported dataset. Do not replace a running resident engine;
export pools and restart AROS after binary changes. No installer or automatic
pool creation is included. Never guess a physical device's unit or partition.

Keep `Tools/OpenZFS/ExecHelper` at that exact SYS: location. The upstream CLI
process adapter launches this native child-process trampoline; omitting it
breaks external-command execution even when ordinary pool queries work.
Packaging requires matching GUI/cold input hashes and native real-zpool/output
capture/missing-executable acceptance, not merely the presence of the file.

**Platform/ is separate and must not be blindly installed.** Its posixc.library
is the matching alpha18 runtime with the appended pipe2 vector used by the
native test suite. It is an explicit platform integration dependency, not an
application library. A different AROS runtime/SDK can have an incompatible Task
or Process layout even when the CPU target is identical. See BUILDING.md for
the ABI, pthread, pipe2, and file-backing flush requirements. The private FAT
overlay used for persistent file-backed tests is included separately at
`Platform/L/fat-handler`. It contains the flush, read-only, clean-detach and BPB
volume-boundary corrections used by those tests. It is not automatically installed.
Those tests do not prove that arbitrary backing filesystems flush safely.

`Sources/FAT/` retains the full private FAT compilation sources, local and
generated headers, applied patches, source provenance, upstream license files,
and a hash-bound replay recipe. To rebuild in a new directory, use:

```
python3 Sources/FAT/rebuild-fat-snapshot.py Sources/FAT --output-parent /path/on/external/build-drive
```

The recorded matching SDK/toolchain must be available. The helper verifies
retained inputs and recorded external headers/archives, creates a fresh output,
and requires byte-identical output. It does not fetch dependencies or install
anything. Review recipes from untrusted sources before running them: hashes
detect drift, not malicious provenance.

`Sources/DOS/` similarly retains the pinned DOS source/catalog revisions,
generated sources and headers, execute-policy patch and upstream license
texts. Its replay command uses the same helper (whose historical filename
mentions FAT):

```
python3 Sources/DOS/rebuild-fat-snapshot.py Sources/DOS --output-parent /path/on/external/build-drive
```

It rebuilds `dos.library`, not every other member of the boot package. The
snapshot's selected boot package is hash-bound to native qualification;
repacking changes only the DOS member and preserves the other module bytes
and order. These FAT/DOS snapshots are not a complete corresponding-source
package for posixc, other boot modules, the SDK or toolchain. Those remain
public-binary-release gates.

Metadata/qualification.json identifies the exact selected inputs and evidence.
The GUI and cold runs qualify the selected shared library, launcher, zfs/zpool,
handler, device and runtime dependency; Manager is covered by the GUI run.
The four startup/runtime/host-identity utilities are hash-bound to native
companion tests, including persistent trusted ENVARC restore, import and mount
in healthy/degraded/restored mirror cases. This is file-backed QEMU evidence,
not physical USB/NVMe qualification. The tested DOS execute-policy module is
included separately at `Platform/Boot/aros-base.pkg.xz`, not silently installed
by this archive. That boot package is tied to the exact tested base ISO;
do not replace a different installation's entire boot package with it;
see Docs/EXEC-POLICY.md. Stock alpha18's loader failed the execute-veto test.

Metadata retains test logs, source identities and checksums. SHA256SUMS covers
the staged files. Validate it after transfer. Metadata may contain the build
machine's local paths; it contains no physical storage or test-disk images.
The bundle has no security test credentials and does not install startup records.

Active preferences belong in ENV:, persistent preferences in ENVARC:. Import
and mount must be tested manually before enabling trusted startup records.
This port is not the older private AROS ZFS on-disk format.
