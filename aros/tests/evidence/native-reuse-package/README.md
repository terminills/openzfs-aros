# Current destroyed-member reuse development bundle

Local archive: `mmake-bundle.hntel_xu.tar.xz`, 12,344,572 bytes.
SHA-256: `e97bf9a41532a81756552d8b55a7ae3c742272d41592d708618dc651eff52728`.
Location: `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/`.
This supersedes `mmake-bundle.0uirh9w0.tar.xz`. No GitHub binary release has
been published; this remains experimental storage software for disposable data.

The bundle uses production engine `shared-engine.rIPiaYWt`, including checked
destroyed-member admission for create/add/attach/replace. It additionally
includes the tested FAT handler and DOS boot package under separate Platform/
paths. They are exact-runtime prerequisites, not automatically installed
application files or interchangeable stock-nightly components.

Qualification:

- `native-gui-run.GdR5Oa3h`: actual Manager connection and pool ONLINE response,
  mounted files visible in Wanderer, post-UI exact readback and full storage
  PASS. Wanderer was left running until the private VM finished its suite.
- `public-service-run.yp3paeiP`: staging's source cold qualification, including
  changed-pool healthy/degraded/restored recovery and all five member hashes;
  retained separately in `../changed-pool-cold`.
- `public-service-run.5bpVRnI7`: staging's exact-engine native encrypted-root
  and native attach/replace qualification, in `../native-member-changes`.
- `public-service-run.gPh8stD3`: fresh archive extraction, every SHA256SUMS
  entry verified, then two-VM full native acceptance using extracted binaries
  AND the extracted FAT/DOS prerequisites. Both original and changed-pool
  healthy/degraded/restored cases pass; all member bytes remain unchanged;
  independent host zdb reports no leaks and FAT fsck is clean after both boots.

All runs use eight-CPU isolated QEMU. The acceptance client and test-only
fixtures are external test tools, not packaged applications. Eight host gate
tests verify rejection of missing phases, changed/missing members and failed
integrity evidence; they do not replace native execution.

The first extracted attempt stopped at the 100 GiB root-space reserve before
starting QEMU. Twelve inactive project directories were subsequently moved
intact to external `retained-package-space.cK8qfVKE` and
`retained-package-space.lqjPRKRr`, with before/after full-file SHA256 checks and
symlinks preserving their old locations. This was not an engine failure.

Hardware durability, all-native changed-pool cold recovery, opposite-endian
labels, external writers and the wider feature matrix remain unqualified.
Platform source/license packaging should be completed before public binary
distribution; this local archive is not a release approval.
