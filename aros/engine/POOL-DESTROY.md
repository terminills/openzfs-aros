# Authenticated pool destruction

Operation 6 now calls upstream spa_destroy through MuFS-authenticated admission.
The target is one imported pool name. Root, explicit confirmation, empty
arguments/options and a writable pool are required. Mounted DOS datasets are
refused; the adapter never forces unmount or destruction. Successful destruction
releases imported-file locks and path admission. Destroy does not securely erase
data; previously labeled files still fail the blank-only creation policy.

The native manager routes the action through its authenticated worker and
existing confirmation dialog. The concise warning and pool-name field were
visually checked in native-gui-run.cq5lACTK/01-destroy-action.png. This was an
editor-only no-service guest, not GUI execution proof, and it was shut down.
Backend execution is independently exercised below.

## Native verification

- admission-service-run.6qGSU8SL and final regression JJCsEWag pass all five
  RAM layouts: mounted EBUSY, read-only EROFS, invalid pool name, successful
  destroy, second destroy ENOENT, no blank-only reuse, non-root EPERM, legacy
  route refusal, missing confirmation, force, dry-run, recursion, options and
  argument refusal. The pool files can be deleted after destruction, proving
  retained backing locks were released in these fixtures.
- file-backing-run.xxUjXmtr passes three boots: authenticated creation; import
  and real native file access, mounted-destroy refusal, unmount/destroy and
  namespace absence; then a fresh service refuses normal read-only AND writable
  import and releases backing locks. Independent zdb label inspection verifies
  all four labels of both files carry POOL_STATE_DESTROYED. Every member stays
  byte-identical during the final refusal boot.

## Preserved integration failure

N1pXsJPr removed the runtime pool and persisted destroyed labels, but the next
service imported it normally. libzutil discovery returns destroyed pools;
upstream cmd/zpool/zpool_main.c filters them unless -D was explicitly requested.
The native adapter was missing this policy filter. Normal import now checks
ZPOOL_CONFIG_POOL_STATE and refuses destroyed pools before final spa_import.
The failing run and independent diagnostic-labels-0.log remain available.
Explicit destroyed-pool recovery is not yet wired; this change does not claim it.

Host zdb coalesces identical label configs. The verifier checks each reported
state and the union of label numbers, rather than incorrectly expecting four
separate state lines.

## Scope and retention

Only explicitly provisioned private QEMU/RAM pools were destroyed. No user pool,
DUT or shared hosted session was accessed. No crash-durability, secure erase or
complete production lifecycle claim is made. Known retired DOS endpoints still
require a resident service.

Admission and GUI runners now support /tmp-only optional ISO scratch, retaining
ISO paths/hashes plus source, binaries and serial logs in the normal evidence
directory. Scratch ISOs are ephemeral across host reboot. Closed raw images in
xxUjXmtr, N1pXsJPr, AGKYpbVr and zfXjixrH were sparsified without changing their
logical bytes; per-directory sparse-before.sha256 and sparse-verify.log retain
the before/after verification. No image or log was deleted.
