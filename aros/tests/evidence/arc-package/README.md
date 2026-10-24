# ARC repaired engine package qualification

The refreshed local archive is `mmake-bundle.ncbjd29s.tar.xz`, stored under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
SHA256: `15b8792da3d12ad571570f569ad64e759cd3d0b30c6d4cd211be4e9ee3469f54`.

The package selects `shared-engine.TEtOTOsm`, including the ARC plaintext
authentication repair, and the existing native MetaMake tools. The packaging
gate matches binary hashes against all three qualification runs:

- GUI `native-gui-run.5MWlrm2h`: eight CPUs, live ONLINE pool query, mounted
  OZPUBLIC drawer in Wanderer, post-UI exact read and terminal storage PASS.
- Cold `public-service-run.Rx3TMKVN`: eight CPUs, three persistent startup
  restores, healthy/degraded/restored mirror reads, unchanged member hashes
  and independent zdb with no leaks.
- Encrypted root `public-service-run.R3HVgA65`: native partition discovery,
  two locked imports, denied mounts before key loading, successful key reloads
  and exact payload reads. Full evidence is in `../encrypted-root`.

Fresh extraction passed every SHA256SUMS entry. The separate eight-CPU
extracted-binary run `public-service-run.w6Jkrjkw` passed all service lifecycle,
event cursor, loader policy and upstream storage acceptance checks. The test
client was supplied externally, not included as a production executable.
Two preboot attempts are retained: a relative checksum manifest was corrected,
then the 100 GiB root-space guard refused allocation until inactive artifacts
were relocated. Neither attempt reached the guest or counts as runtime proof.
The seven packaging-gate tests pass, including missing terminal result,
incorrect key-reload count, assertion and explicit failure rejection.

This is isolated QEMU qualification with explicit platform overlays, not stock
nightly, physical durability or production-storage qualification. No DUT access
was used. Successful test images are pruned while logs and screenshots remain.
