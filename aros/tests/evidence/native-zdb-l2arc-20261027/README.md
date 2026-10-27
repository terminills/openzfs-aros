# Native zdb empty L2ARC header coverage

Production debugger/library from `mmake-debugger.JLOOcjrH`, isolated QEMU
with private DOS/POSIX overlays. All backing inputs are disposable fixtures.

- ISO run `zdb-label-run.CaLV7RkO`: little-endian, byte-swapped and missing
  empty-cache headers match host zdb output exactly.
- Device run `zdb-label-run.EsMKxiba`: same ISO cases plus authenticated
  `/AROS/ata.device/1` inspection of the little-endian fixture. Exact output
  matches host. `qemu-img compare` reports identical images after inspection.
- Both runners exited zero, including existing label/pool/damaged-label and
  root/unprivileged/root checks. Input hashes and logs retained.
- Six verifier unit tests pass, including corrupted evidence and changed
  device-image negative controls. These unit tests are not runtime proof.

Generator uses upstream host libnvpair for XDR labels, label-offset SHA256
checksums and exclusive creation. It creates a **synthetic empty header**,
not a cache produced by ARC. Its header tail is not populated; upstream zdb's
header-decoding path does not validate that tail. No log blocks are present.
The first exploratory host fixture placed the header at 512 KiB and correctly
reported it missing; the corrected offset is VDEV_LABEL_START_SIZE = 4 MiB.

This proves bounded header decoding through AROS file and native-device
readers, not persistent-cache correctness, cache-header checksum validation,
populated log-block traversal, rebuild behavior, or physical-media durability.
The device case covers little endian; byte swapping is covered through ISO.
The independent muFS rename defect remains open.
