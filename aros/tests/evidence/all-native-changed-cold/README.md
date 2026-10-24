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

Remaining gates include degraded/missing-device and restored all-native cold
recovery, the two-channel ATA failure, abrupt power loss, and physical-media
durability. The previous file-backed degraded/recovery proof is separate.
Large artifacts remain under the external qualification directory; successful
ISO images are pruned, while logs, screenshots, disks and manifests remain.
