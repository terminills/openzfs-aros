# Official SDK runtime-check worker and installed service guidance

No engine qualification is claimed. These are real native worker lifecycle
and report-delivery checks against the pristine 20261023 SDK/runtime, plus a
user-facing command-name repair. No private runtime overlays, service fixture,
pool, network, writable disk, DUT or user-hosted session was used.

Baseline ISO SHA-256:
`38b5689f4d5c23b0765a3fbc266c67928d841d81c754675a107dd8bc0b50d6e3`.
External artifacts: `/mnt/short-term/openzfs-ui-worker.khId8Lyi`.
QEMU KVM, CPU host, two CPUs, 2048 MiB, BIOS CD boot, RAM temporary files.
SDK's own pthread archive/header and runtime libraries were used.

## Missing checker

The actual project-worker implementation was linked with runtime-worker-test.c.
All 16 runs created the background thread, rejected destruction before join,
returned OZ_ENGINE_ERROR with the exact missing-tool/no-execution diagnostic,
joined and freed successfully. Serial log includes all 16 individual results,
final PASS, shell success and DONE. This did not invoke a checker or pool.

## Real checker report capture

Four real OpenZFSRuntimeCheck invocations ran through the production worker,
with matching official-SDK ExecHelper installed. Every report was complete,
classified correctly and collected with DOS=0/capture errno=0; all workers
joined/freed. The capture fixture returned success for delivery only.

**The checker itself returned 20 on every run.** Reports showed:

* PASS: errno accessor/POSIX writer, redirected stdout/stderr, and ExecHelper
  subprocess with expected status and captured output.
* FAIL: security.library was unconfigured in this disposable fixture.
* FAIL: LoadSeg and NewLoadSeg loaded the RAM test program after its execute
  protection bit was disabled. This is the already tracked execution-policy
  difference, not a new claim of an SDK/ABI defect or authorization to change DOS.

The full output remains in serial-capture.log; no failures were hidden or
converted into runtime qualification. Command errors are completed responses,
not transport retries. These bounded tests do not prove arbitrary concurrent
worker safety, allocation-failure recovery, cancellation, or leak freedom.

## User-facing correction

Production mmake installs OpenZFSServiceNative, while the Manager's no-service
response incorrectly told users to start OpenZFSService (the older test-fixture
name). Corrected both the main response and unavailable device-scan guidance.
A host test compares user-facing instructions to the actual production target.
manager-guidance.png confirms the corrected native UI at 800x600 after the
real checker capture test. No service was started, and no storage was changed.
The device-scan text was source-checked, not separately exercised in this run.

Both QEMU instances were stopped through their private QMP sockets after
completion. Disposable ISOs were removed after terminal and open-file checks;
source snapshots, binaries, logs, screenshot, input hashes and pruned ISO hashes
remain retained. The failed-checker details are part of the evidence, not a
reason to mark the full OpenZFS port complete.
