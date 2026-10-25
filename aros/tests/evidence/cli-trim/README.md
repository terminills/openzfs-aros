# Native CLI TRIM admission

The production engine `shared-engine.PNTma6vL` admits the upstream TRIM ioctl
after bounded transport validation (7836561). Current AROS backing devices
still have no discard capability. This qualifies correct per-device refusal,
not successful discard or physical-media behavior.

Private eight-CPU QEMU run `public-service-run.YWReuI1U` passed the direct CLI
start, suspend and cancel checks, requiring unsupported-device diagnostics
for both mirror members. It also passed the service storage round trip and
native cold read-only checks. The runner exited zero. Inputs, serial output,
cold results and runner output are in `patched-direct/`.

This client (`sibling-production.GyytWrM1/OpenZFSPublicServiceTest`) does not
contain the optional libzfs-init or CPU-topology probes. Those requirements
were explicitly disabled for this run, not falsely reported as passing.
Separate run `public-service-run.SbCfUhKo`, using the same production engine
and the existing full client, passed 80 libzfs-init cycles, two simultaneous
instances, 128 topology queries across eight CPUs, service and cold checks.
Its evidence is in `full-init-regression/`; that older client does not test
CLI TRIM.

## Retained negative and harness evidence

- `old-shell/`: initial old-engine rejection before upstream dispatch.
- `patched-shell/`: upstream per-device errors reached correctly, but the
  shell-based test made an invalid child-status/diagnostic assumption.
- `old-direct/`: corrected exact-path child launcher still detects the old
  engine's generic unsupported-operation error rather than per-device errors.
- `optional-init-mismatch/`: TRIM/storage passed but an absent optional probe
  prevented runner completion. This is not a passing full test.
- `optional-cpu-mismatch/`: likewise awaited the absent CPU probe after
  storage completion; the owned QEMU was deliberately stopped via its monitor.
- `preflight-init.log` and `preflight-cpu.log`: the runner now rejects these
  client/requirement mismatches before boot. Static marker presence is only
  an early capability check; runtime markers remain required.

4a6f680 corrected the child launcher and diagnostics. 3af2c64 added the early
probe checks. Failed disposable ISO hashes are retained in
`cli-trim-failed-images.sha256`; logs and disk fixtures were preserved.
External artifacts remain under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
No DUT or user hosted session was used. These incremental builds do not
establish complete SDK source closure or working backing-device discard.
