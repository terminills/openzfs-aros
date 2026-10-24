# Pool-root cold-boot qualification

Run `public-service-run.o9PVwdq4` completed on eight-CPU isolated QEMU with
`shared-engine.NAQEJFme` (the root ZPL fix in e6a8971), a newly rebuilt
MetaMake upstream acceptance client, private DOS overlay and flush-capable
FAT handler. The disposable mirror members are regular files on a private
FAT disk image. This is not physical media or a native-partition cold test.

The first guest writes, flushes and detaches a file on `public_mirror` itself,
in addition to the existing child-dataset tests. After clean export and
shutdown, a fresh guest imports read-only and checks the root file three
times: healthy, one missing mirror member, and restored after the separate
beyond-redundancy refusal check. Each check verifies exact bytes, EOF and
write rejection, then detaches. Existing child, encrypted-data, key-rotation,
volume and lifecycle gates also pass.

The harness compares both pool members before/after cold testing, runs host
`zdb -bcc` (No leaks), and verifies that zdb did not modify the members.
Input and member hashes, guest logs and zdb output are retained here. Both
successful test ISOs and eligible scratch images were pruned, reclaiming
9.89 GiB.

Initial attempts are retained:

- `jcPKA2Rk` rejected incompatible native-partition/persistent-file options
  before boot. The harness now reports that incompatibility explicitly.
- `tkLw9yeK` passed the root write but stopped at an upstream import test that
  hardcoded RAM fixture paths. Import/file-vdev tests now receive the actual
  fixture directory; no production behavior or pass criteria were relaxed.

This proves clean shutdown/reimport, not abrupt power-loss durability. It
does not repair previously created roots without ZPL metadata or update the
previously published package archive.
