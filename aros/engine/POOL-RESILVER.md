# Native resilver restart

Operation 19 uses the retained MuFS-authenticated caller and upstream
`secpolicy_zfs`, then `spa_scan(spa, POOL_SCAN_RESILVER)`, matching the native
OpenZFS pool-scan ioctl. It requires confirmation and an exact pool name;
arguments, options, topology, force and dry-run are not accepted. Read-only and
suspended pools are refused. Upstream enforces the `resilver_defer` feature.
No feature is automatically enabled by the production operation.

The Manager routes this operation through the authenticated worker. Capability
advertisement means the adapter is available, not that the selected pool has
the required feature or that a repair completed. Query pool status for the
upstream scan state and counters. A healthy pool can accept a restart with no
repair necessary; this must not be presented as successful data recovery.

## Native evidence

All tests used private QEMU disk copies, no networking, no DUT or shared hosted
session. Builds and runner logs are in `build-aros/typed-import-service/`:
`resilver-build.log`, `resilver-client-build.log`,
`resilver-client-build-2.log`, `resilver-manager-build.log`,
`resilver-run-1.log`, `resilver-recovery-run-1.log`.

- `admission-service-run.yX8qg4zM`: full WRITE and cold read-only VERIFY passed.
  Tests cover non-root refusal, invalid request fields, read-only refusal,
  feature-gated restart and an explicitly verified healthy/no-DTL no-op.
  Separate native clients exercise authenticated restart and bad pool names.
  Existing DOS/API regression suite, whole-image read-only/baseline hashes,
  offline zdb no-leaks, compression and atime persistence checks passed.
- `admission-service-run.wyAgYoNz`: take a mirror member offline; create and
  flush 262144 patterned bytes through native DOS; online the member; submit
  explicit authenticated restart; wait for upstream resilver FINISHED with
  zero errors. Cold read-only boot with the original healthy member absent
  verified the payload from the repaired member. Independent host zdb using
  only that repaired member found no leaks and recovered every byte correctly.
  Both QEMU boots and all offline verifiers exited successfully.

The second client build and runner include the recovery restart assertion and
an explicit required native resilver-validation marker. Test inputs, binary
hashes and source snapshots are retained per run. Test fixtures may enable
`resilver_defer` after checking its refusal when absent; the adapter never does.

## Still unproven

The small repair can complete before the explicit restart reaches the worker.
The recovery test proves accepted restart plus actual repaired-member data,
not necessarily interruption/restart of an in-flight scan. A deliberately
long-running repair, deferred repair merging, cold interruption/resumption and
GUI interaction for this specific command remain separate verification work.
