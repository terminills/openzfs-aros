# DOS source replay and exact-package qualification

Local development archive: `mmake-bundle.jvc1mxmr.tar.xz`, 13,514,940 bytes.
SHA256: `ce8192b1df270c58ca51c25161e0d33d1c52192149c18ebbfd3546bc48f76192`.
External location: `/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/`.
No public binary release has been published.

`dos-revision.Pa9psAFv` rebuilds pinned AROS revision
`f81cd211c43c727ffd0a292a58bc729542cda0a7`, retaining its catalog revision,
generated sources/headers, execute-policy patch and upstream license texts.
It records 185 compiler commands and hashes 190 used external dependencies.
These are SDK/toolchain inputs, not files from the shared source checkout.
No cached DOS object files are replay inputs. The source snapshot does not
provide the full SDK/toolchain or sources for every boot-package member.

Direct replay `fat-replay.k72bijo1` and archive-extracted replay
`fat-replay.qokpui1c` both produce DOS SHA256
`23cec3255eaf2ef50bcd2ace2a02e4a382c970a0ae97acb48f0d45a0eaa9e639`.
Repacking the direct replay also reproduces boot-package SHA256
`0a6d8c05a0bc33142f6d2f1ac0363f93771edf936b583ccdc3872ec0f37175b9`.
The repacker checks that only dos.library changes from the original package;
all other module bytes and their order are preserved. Replay uses a compiler
prefix map to preserve recorded __FILE__ strings when output paths change.
This is not a claim of independence from the recorded compiler/SDK.

The new DOS hash differs from the older qualified module, so all required
runtime qualification was repeated with the new package, in isolated
eight-CPU QEMU instances:

- `native-gui-run.edidgU5f`: Manager connects, a live pool query reports ONLINE,
  mounted files are visible in Wanderer, normal UI closure leaves Wanderer
  running, and post-UI exact readback plus terminal storage PASS complete.
  Screenshots are retained in `gui/`.
- `public-service-run.bHtUibZz`: persistent writer and fresh cold boot pass,
  including original and changed-pool healthy/degraded/restored cases, exact
  DOS payloads, unchanged read-only member hashes, clean FAT after each boot,
  and independent zdb No leaks for both pools.
- `public-service-run.caP9wECT`: native partition discovery/data and encrypted
  root/key denial/reload tests pass, followed by the full terminal suite.

The archive's SHA256SUMS verifies after extraction. Its retained DOS sources
rebuild byte-identically using the included helper. Existing FAT replay also
passes with the extended helper (`fat-replay.16sav2wr`). Ten packaging-gate,
four command-recording, five snapshot-path and fifteen retention host tests
pass; none substitutes for the native runtime evidence above.

Staging now requires `--dos-source-snapshot`, verifies its source manifest,
license/policy records and module recipe, and binds its boot-package bytes to
the selected tested package. Checksums detect drift, not malicious provenance;
review downloaded compiler recipes before running them.

Successful disposable images reclaimed 19.65 GiB; manifests, logs, screenshots,
binaries and source snapshots remain external. No DUT or user-hosted session
was used. Posixc, other boot modules, complete SDK/toolchain source packaging,
the broader feature matrix and hardware durability remain open.
