# Native pool GUID changes

## Retained import identity (2026-10-03)

The common native/CLI reguid adapter now lives beside the retained imports in
`pool-import.c`. After upstream succeeds, every retained record for that pool
and old GUID receives the new GUID, including records retained by later member
additions. Failed upstream changes leave identities unchanged. Path policy
still admits the old/new transition under its lock; no path, device, extent or
permission is added by the identity update.

Previously those records retained their old GUID while the pool and policy
changed. Recovery matched both name and GUID and silently skipped the stale
records. Clear/readmission now fail with ESTALE for a matching pool name whose
retained identity disagrees, rather than treating it as absent admission.

Native reproduction `native-import-run.njtV4FdN` added that check before the
update fix: authenticated reguid returned zero, then clear returned ESTALE
(70) and the test failed. Its original backing hashes remain unchanged.
The corrected full-dispatcher build passed `native-import-run.fpWOP5Gx`, exit
0: native API reguid/clear/reopen and CLI generated/explicit reguid followed by
clear, plus duplicate/zero/readonly/checkpoint/permission refusals, export and
reimport, DOS access and original backing hashes. `yawQGCh2` is a retained
pre-boot runner setup failure (CLI metadata directory used as binary directory),
not a guest or filesystem failure. All artifacts and `reguid-retained-*.log`
are under `/mnt/openzfs-artifacts/`.

This verifies identity coherence after GUID mutation, not alternate-device
relocation or recovery from physical media removal.

Production shared packaging `shared-engine.g3zrpvRT` repeats successfully in
`native-import-run.GBCmpxGE` (exit 0), with the library lifetime probe and the
same API/CLI reguid-clear regressions. Both original backing hashes match.
The exact runner log is `reguid-retained-shared-qemu-20261003.log` on the
external archive. This includes the preceding native rename ancestry fix;
the library build rejects fault-injected engine objects.

Operation 23 now calls upstream spa_change_guid through authenticated native
admission. Blank argument generates a GUID upstream; an explicit decimal
uint64 requests that GUID. Zero, overflow and non-decimal strings are invalid.
Root/config privilege and a writable, non-suspended pool are required.
Upstream retains duplicate-GUID, checkpoint and pool-health checks, label
updates, transaction processing and internal history. No replacement GUID
generator or hand-written disk-label mutation is used.

Confirmation is mandatory; force, dry-run, recursion and extra options are
refused. The legacy unauthenticated path is unsupported. The capability bit
is advertised by writable-capable engines. The native manager uses its
authenticated worker and confirmation dialog. API validation explicitly
allows this operation's optional argument.

## Evidence (under build-aros)

- admission-service-run.XIdYYXME: first run failed. Explicit-GUID rejection
  checks passed, but blank-argument requests were incorrectly rejected by
  generic required-argument validation before reaching the engine. Fixed
  the API validator rather than weakening engine tests.
- admission-service-run.3EBZ574n: all five disposable RAM-file layouts passed:
  stripe, mirror and RAIDZ1/2/3. Tests cover generated GUIDs, restoration of
  a previously used explicit GUID, existing-GUID rejection, invalid decimal
  values, missing pool, invalid pool name, export/discovered-label re-import,
  read-only refusal and nonroot refusal. Native filesystem data survives.
- admission-service-run.WuuDy4xc: final test adds legacy/confirmation/flag/
  options admission rejection. Guest reports error=0 and standalone PASS;
  all five layouts report result=0, baseline checksums pass and runner logs
  its final PASS. The execution handle nevertheless returned 143; this
  process-status discrepancy is retained, not represented as exit 0. Its
  private QEMU process was confirmed gone.
- file-backing-run.RQg3g6To: separate create and fresh-guest discovery boots
  both report GUID 11161476059299209568. Expected GUID was saved as a pool
  user property after mutation, then compared with the actually imported
  GUID. Fresh read-only mutation is refused, native DOS payload/enumeration
  passes, members remain byte-identical and independent zdb checks pass.
- native-gui-run.CTaQFMM2: manager displays reguid with optional GUID, and
  screenshots 06-reguid-ready.png and 07-reguid-confirm.png show its real
  request/confirmation. The first confirmation still had dry-run checked;
  it was cancelled, not submitted. Subsequent input opened the draft pool
  editor and eventually no longer produced visible pointer/UI changes.
  No serial exception was emitted. QMP reported running, which does not
  establish guest responsiveness. Guest was stopped through QMP. Therefore
  successful reguid execution from the UI is NOT verified in this run.

Outstanding coverage: checkpoint and degraded-device rejection, suspended
pool behavior, event delivery, mounted-dataset GUID change, and a successful
GUI submission/readback. The stale-confirmation/draft-editor interaction
also needs investigation. No DUT or user-hosted session was accessed.

## Follow-up GUI verification

`native-gui-run.xS6HslXv` repeats with the same engine/manager binaries in a
fresh private guest. Pointer samples fit an inset mapping: requested QMP
coordinates `x = 0.9 * screen_x + 0.05 * width` and the corresponding y
mapping reach the displayed controls. This is observed calibration for this
guest, not a change to AROS/KVM/HID configuration and not a universal rule.

- 02-reguid-prepared.png: pool name supplied, optional GUID blank, dry-run
  visibly unchecked.
- 03-confirm.png: real confirmation states dry-run=no.
- 04-submitted.png: authenticated pool reguid returned errno=0, worker ticket
  completed without transport/release error.
- 05-guid-readback.png: pool.get returns GUID 1406703922331804308.
- 06-draft-editor.png and 07-draft-cancel-and-readback.png: opened and cancelled
  the actual draft editor, then a second property request returns the same
  GUID. UI remains responsive. No storage operation was sent from the draft.

The previous suspected editor lockup was not reproduced; missed controls
from the uncalibrated input explain the observed wrong actions. The final
lack of visible movement in the old guest remains unexplained, not proof of
an AROS crash. Successful GUI submission/readback is now verified. Private
guest shut down through QMP. Original failed/ambiguous captures are retained.
