# Native checkpoint rewind import

## Public upstream CLI transport (2026-10-03)

`zpool import --rewind-to-checkpoint` now passes `ZFS_IMPORT_CHECKPOINT`
through the native ioctl transport. Previously its flag whitelist rejected
this request even though the Manager path below supported it. The existing
configuration privilege, explicit member admission, readonly property and
upstream dispatcher remain in force; other unimplemented import flags remain
rejected. No alternative rewind algorithm is introduced.

Negative QEMU run `native-import-run.QgNMxn1Q`, using the unchanged library
from `native-bundle.u4jN1b3a`, failed at the first readonly CLI rewind with
`import request not supported`. Corrected bundle `native-bundle.i8xXqim2`
uses `shared-engine.Ym1ZH7yV` and rebuilt service/Manager from
`cli-rewind-release-20261003`; the other components retain the all-fresh
package inputs. Native run `iCFmc1zd` exited 0. Its disposable RAM-file mirror
verified checkpoint-before/after datasets, readonly preview plus native DOS
read/write-refusal, ordinary import retaining post-checkpoint state, writable
rewind, missing-checkpoint refusal and subsequent ordinary reimport. The
existing checkpoint lifecycle and selected full package regression passed;
both original native backing-image hashes remained unchanged.

Logs are retained in the external OpenZFS archive as
`cli-rewind-{before,build,library,library-inputs,stage,after}-20261003.log`.
This CLI test uses export/reimport in one guest; it does not extend the
separate cold-boot Manager evidence below to CLI power-loss recovery.

Authenticated pool.import accepts optional rewind=on/off alongside mandatory
readonly=on/off and optional destroyed=on/off. Unknown/duplicate/malformed
tokens fail without modifying parser outputs. Rewind defaults off. Root/config
authorization, explicit confirmation, exact backing-file identity and existing
import refusal checks remain mandatory. This does not implement generic
transaction recovery or silently fall back to another load policy.

The service calls upstream spa_import with ZFS_IMPORT_CHECKPOINT. Upstream
loads the checkpoint in-core for a read-only preview; writable import commits
the checkpoint rewind to disk and discards later changes. Missing checkpoint
is returned as ZFS_ERR_NO_CHECKPOINT (1026), not success or current-state import.

The native editor has an opt-in Rewind to checkpoint checkbox. Applying the
draft defaults to readonly=on and preview only. Request confirmation describes
the read-only view or warns that writable rewind permanently discards later
pool changes. As with other editor options, the backend enforces policy.

## Evidence

- build-aros/file-backing-run.DOqsTB9F: five separate QEMU boots, private
  two-file mirror on FAT backing, networking disabled. First boot writes
  before.txt, creates a checkpoint, then writes hello.txt. Read-only preview
  sees before.txt but not hello.txt through native DOS reads and directory
  enumeration. Ordinary read-only import then sees both, proving preview did
  not commit the rewind. Writable rewind sees the older state and writes
  rewound.txt. Final boot rejects a second rewind with 1026, ordinarily imports
  the pool, reads before.txt and rewound.txt, and confirms hello.txt is absent.
  Read-only mounts reject writes. Member byte comparisons prove unchanged
  preview/current/final read-only phases and changed writable rewind. Independent
  zdb -bcc reports no leaks on the final members. Manifests remain identical.
- build-aros/admission-service-run.CimEcgrl: native parser and nonroot refusal
  tests plus the existing stripe/mirror/RAIDZ1/2/3 lifecycle, DOS access, and
  forty-mount regression suite pass. RAM-layout tests are not cold persistence.
- build-aros/native-gui-run.F1EDoNVd: screenshots 03/04 show the opt-in import
  editor; 05 shows readonly=on;rewind=on, the explicit file path and read-only
  recovery notice. Preview reports nothing submitted, with service unavailable
  honestly displayed. This is editor wiring evidence, not a GUI engine run.
- build-aros/file-backing-run.3O3zYGwL: the same five-boot lifecycle also
  passes for a three-member RAIDZ1 pool with spacemap_histogram enabled.
  Member comparisons, native DOS checks and independent zdb all pass.
- build-aros/file-backing-run.LeG0UKt7: extended mirror run also attempts a
  writable rewind with no checkpoint on the final boot. Both read-only and
  writable requests return 1026; ordinary import and native reads still pass.
  Final member bytes remain identical to the preceding exported writable
  phase, proving neither failed request rewrote the member files. Independent
  zdb passes. Logs: rewind-refusal-client-build.log, rewind-refusal-run.log.

Retained build logs: rewind-engine-build.log, rewind-cold-client-build.log,
rewind-parser-client-build.log, rewind-manager-build.log under build-aros.
Each runner freezes inputs, source patch and source archive with its evidence.

Not proven: interrupted writable rewind, degraded rewind, combined destroyed
pool recovery and rewind, every topology's rewind, physical power-loss safety,
or GUI-triggered rewind against real pool data. No DUT or user-hosted session
was used. This is one implemented OpenZFS facility, not full-port completion.
