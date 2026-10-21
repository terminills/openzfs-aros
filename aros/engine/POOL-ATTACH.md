# Native file-vdev attach and replacement

Authenticated operations 11 (attach) and 13 (replace) call upstream
spa_vdev_attach with replacing false/true and sequential rebuild false.
The pool is target, the existing vdev's decimal GUID is argument, and typed
topology contains exactly one new pre-provisioned file (data/stripe group).
This describes the new leaf, not the resulting pool topology. Upstream
decides whether attaching/replacing the selected device is supported.

Root/config authority, confirmation and a writable nonsuspended pool are
required. Unknown modifiers, force, dry-run and recursive requests are refused.
No implicit truncate/create/overwrite: create, attach and replace share native
path canonicalization, file identity, full-file zero check, minimum/aligned
size and writable-open preflight. Supplied files must be exclusively provisioned;
parent-lock handoff is not atomic against unrelated external writers/renames.

An imported pool's exact-path allowlist is extended only after preflight and
immediately before upstream attach. Failed upstream mutation removes only a
newly inserted allowlist entry. Success retains the new path for asynchronous
resilver/reopen until export. Existing entries are never removed by failed
attach. Policy retains its 32-path bound; detached old paths are currently kept
until export, so repeated replacements can exhaust it. Native raw-device
identity/partition ownership remains separate, unfinished work.

UI uses the authenticated worker. New backing file is a dedicated label for
the existing string control; the full path becomes typed topology, not shell
text or options. Overlong paths are rejected rather than silently truncated.
Confirmation shows the exact target GUID and file. Successful submission is
not resilver completion; query pool.status for scan state, errors and topology.

## Evidence and retained failures

- admission-service-run.nO5EOnMy: shared preflight refactor preserves the
  existing five-layout create/lifecycle/DOS regression suite.
- admission-service-run.XNprR4Ct: initial attach returned EACCES because the
  imported pool's old exact-path policy correctly denied the new backing file.
  Fixed by explicit post-preflight extension with failure rollback, not by
  disabling the policy or accepting arbitrary paths.
- admission-service-run.EYnUtT3i: attach/replace and native DOS readback passed,
  but the run later stopped producing serial output during the RAIDZ1 portion
  and hit the runner's 600-second deadline. Cause is unresolved; do not count
  this as a whole-suite pass or erase it because the next run passed.
- admission-service-run.G4ntmKMs: complete five-layout regression passes,
  including mirror attach then replacement, completed resilvers with zero
  scan errors and exact native DOS file readback after each. Root refusal tests
  cover invalid GUID, nonblank existing file, force/dry-run/recursive/options;
  read-only and nonroot requests are refused. The test detaches the new member
  afterward and the original destruction/recovery tests still pass. This is
  live RAM-file evidence, not fresh-boot or independent-new-member proof.
- native-gui-run.6WiGnLFG: native fields (01) and preview (02) show the target,
  existing GUID and new backing-file path as a one-leaf typed topology. The
  editor-only guest honestly reports service unavailable; nothing submitted.
- file-backing-run.657BVUwf: two separate boots, four 64-MiB pre-provisioned
  regular files on private FAT backing. AROS creates a two-file mirror, writes
  hello.txt, attaches disk2, waits for zero-error resilver, replaces disk0 with
  disk3 and waits again. After export, the host withholds both original paths
  (disk0 and disk1). Fresh AROS imports read-only using on-disk discovery, sees
  DEGRADED status with two ONLINE new members and one UNAVAIL old member, reads
  exact hello.txt and enumerates it through native DOS. All four file images
  remain byte-identical during read-only boot. Independent zdb -bcc passes on
  the two new images together, then separately on each image alone (both
  no-leaks). Individual zdb checks were run after the runner and subsequently
  added to its attach branch. Select with AROS_TEST_COLD_ATTACH=1. Logs:
  attach-cold-client-build.log, attach-cold-run.log, attach-zdb.log and
  individual-member{2,3}-zdb.log. This is orderly persistence, not power-loss
  proof; both new files share the private FAT backing device.

Build logs in build-aros: attach-preflight-engine-build.log,
attach-engine-build.log, attach-policy-engine-build.log,
attach-policy-client-build.log and attach-manager-build.log. Private QEMU
runner evidence freezes source/inputs; no DUT or user-hosted session touched.

Still to verify: interrupted/degraded resilver, RAIDZ expansion/replacement, spare/log/
special vdev behavior, repeated-policy-capacity reclamation, GUI submission,
and physical-device adapters. Full OpenZFS completion is not claimed.
