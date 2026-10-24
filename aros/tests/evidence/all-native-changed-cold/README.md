# Changed all-native pool: cold recovery and transport controls

`public-service-run.YYdSs7XP` passes the full eight-CPU writer, separate-VM
read-only import, whole-disk hash comparisons, and terminal runner. All five
initial members use AROS native partition paths, not filesystem backing files.

The fixture supplies two 64 MiB partitions on a private 256 MiB ATA disk and
three on a private 256 MiB AHCI disk. The authenticated Manager API attaches,
replaces and adds checked destroyed members; active/exported members remain
refused. Topology and exact DOS data are checked after changes. The resulting
two-mirror pool is exported, renamed on import to `public_native_target`, and
exported again. A fresh VM imports it read-only, verifies ONLINE health and
exact handler data, refuses a write and exports. Both entire disks remain
byte-for-byte identical to their pre-reader hashes.

An independent host `zdb -e -p native-zdb-members -bcc public_native_target`
check reports **No leaks**. Its four input files are exact 64 MiB slices of the
actual native partitions, extracted only after both VMs exit. Both these slices
and the original whole disks retain their hashes after zdb. They are not the
files used by the guest writer.

Production engine: `shared-engine.rIPiaYWt`. Required platform inputs include
the existing DOS package and the separately qualified AHCI capacity/inquiry
overlay `ahci-capacity.j_4bxi4z/aros-bsp.pkg.xz` (SHA256
`0d1b3b2feb3311fe7617f31ebb65ccbbf5406448a071ae57b30625e9dfcb3db2`).
This AHCI overlay is not included in `mmake-bundle.hntel_xu.tar.xz`.

## Failed controls, not fixes or passing qualifications

- Stock AHCI: `udtI2n8d` reports 268434432 bytes for the 268435456-byte image,
  1024 bytes short. The fixture refuses to proceed; the known AHCI overlay
  restores the exact geometry. No partition bound was relaxed to pass.
- Two-channel ATA: `5btayh11` (eight CPUs) and `5xbwEzUP` (one CPU) reach
  native pool creation but produce ATA completion timeouts and device I/O
  errors. The eight-CPU run additionally records an ATAPI READ packet timeout
  and reset. Host vmstat sampled zero I/O wait and no blocked processes.
  Both private VMs were captured and stopped after actual I/O errors;
  failed disks remain intact. This does not establish the root cause or an
  SMP-only fault. The passing ATA+AHCI case does not fix or qualify this path.
  Reported to Claude as TRIG-1590; no shared driver sources were modified.
- Setup-only failures `B0HGokAg` and `HP5SiodD` used stale fixture expectations
  for unit count and AHCI media type. The inventory now checks the additional
  unit and accepts the respective stock/corrected AHCI disk type while still
  checking device, unit, identity, duplicate units and exact counts.

Reproduce with the normal explicit public-service prerequisites plus
`AROS_TEST_REQUIRE_PARTITIONS=1 AROS_TEST_NATIVE_DISCOVERY=1` and
`AROS_TEST_NATIVE_CHANGES=6` for ATA+AHCI, selecting the AHCI overlay via
`AROS_TEST_BASE_AHCI_PACKAGE`. Value `5` reproduces the two-channel ATA case.
Both values generate new disks exclusively and retain the exported pool for
a second VM; neither uses physical disks nor combines with native-encrypted
cold mode. The runner requires the matching writer/reader markers and hashes
both changed disks. Fourteen host retention tests pass, including refusal to
prune when either disk's verification is missing or failed.

## Missing-device and restored cold recovery

`public-service-run.mvOL0QeI` additionally passes a writer and three separate
eight-CPU reader VMs: healthy ONLINE, physically omitted ATA disk DEGRADED,
then restored ATA disk ONLINE. Each reader verifies exact handler payload,
read-only write denial and export. Both whole native disk images match their
pre-reader SHA256 hashes after every phase. The terminal runner exits zero.
Logs and input hashes are retained in `recovery/`. This is clean-export,
read-only recovery, not degraded writable operation or power-loss recovery.

The missing member's path remains explicitly authorized in the import request.
The device itself is absent from QEMU. This preserves the import admission
policy: labels cannot grant access to additional paths. Initial attempt
`lwEjYVZh` omitted that authorization and was rejected as no pool available;
its healthy reader passed. Evidence is in `recovery-omitted-admission/`.
No production policy was relaxed to make the test pass.

Earlier attempt `y5Sf5QaR` passed the writer but failed image construction:
replaying hybrid boot metadata from a remastered ISO produced xorriso SORRY
errors. Both reader ISOs are now constructed from the original base using
explicit boot settings. The failure log is in `recovery-image-failure/`.
Failed run artifacts remain external. Passing-run disposable ISOs were pruned
only after all three reader phases and both hashes per phase passed; their
hashes are retained in the pruning manifest. Fourteen host retention tests pass.

## Degraded writes and healed-member cold reads

`public-service-run.9CLTmLeH` passes seven separate eight-CPU VMs: the writer,
the three read-only recovery cases above, then degraded write, restored-device
healing, and final read-only verification. Enable this extension with
`AROS_TEST_NATIVE_HEALING=1` alongside native changes mode 6.

The missing-ATA writer imports writable, checks DEGRADED health, writes and
flushes a new deterministic 1 MiB file through the DOS handler, syncs and
exports. The absent ATA disk retains its prior whole-image hash. The next VM
restores that disk, waits for resilver, checks ONLINE health and reports
270336 bytes issued with zero scan errors. It verifies both payloads, offlines
the AHCI peer in the repaired mirror and exports. The final fresh VM imports
read-only with that peer still OFFLINE, checks DEGRADED health, verifies both
payloads exactly and rejects writes. Both entire disks remain unchanged in
that last phase. The peer isolation is intentional: a same-VM read could be
satisfied by ARC rather than the repaired disk. The retained test pool ends
degraded, not fully online; this is not a production startup configuration.

Independent host `zdb -e -p healed-zdb-members -bcc public_native_target`
reports No leaks using exact native-partition slices extracted after all VMs
exit. Both the slices and original images retain their hashes. Evidence is
under `healing/`; terminal runner and zdb exit zero. Fifteen host retention
tests pass. Successful disposable ISOs (27.41 GiB) were removed with hash
manifests; disks, binaries and logs remain. Failed/aborted test artifacts are
not pruned.

Setup history: the first healing launch was reserve-blocked before QEMU.
The private build workspace was relocated with full file-hash and symlink
verification to external `build-workspace.98Zx7n6B`, preserving its old path.
The build required explicit original source/build paths and an external `aros`
symlink for a generated relative dependency. Attempt `cLcPjiED` was stopped
after an unsuccessful rebuild left the previous client in place; it is not
acceptance evidence. The passing run uses the successfully rebuilt client.

Remaining gates include the two-channel ATA failure, abrupt power loss, and
physical-media durability. The previous file-backed recovery proof is separate.
Large artifacts remain under the external qualification directory; successful
ISO images are pruned, while logs, screenshots, disks and manifests remain.
