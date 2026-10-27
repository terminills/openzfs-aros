# Native Manager recovery assessment

The import API accepts `assessment=on` only with `readonly=on` and
`recovery=safe` or `recovery=extreme`. Checkpoint assessment and writable
assessment are rejected. Existing identity, path, privilege and reservation
checks remain in force. No wire layout or operation ID changes.

The service uses upstream `ZPOOL_TRY_REWIND` and returned load-info, not a
synthetic report. A successful assessment returns one `recovery-assessment`
row: `RECOVERABLE` with upstream recovery timestamp/estimated loss, or
`CURRENT` when ordinary read-only import succeeds. The latter temporary,
unmounted view is exported before success. A failed export retains bindings
and explicitly reports that the pool remains imported. Failed assessments
retain the original upstream error; they are not reported as recoverable.

Fresh production engine: actual MetaMake `mmake-engine.MJyEh30Q`, shared
library `shared-engine.YQjwKnG1`. Actual CLI/Manager build: `IDNG3d6B`;
final UI wording/draft correction rebuilt as `JbDgWkI2`. Tests use the
recorded private SDK/pthread/crypto and alpha18c5 DOS/posix overlays, not an
official-SDK or physical-device qualification.

Eight-CPU native tests:

- `imbVYzRr`: recent damaged mirror, two normal assessments with structured
  recoverability results and pool absence, then two read-only recoveries with
  exact DOS content and write rejection. PASS; backing and overlay hashes
  unchanged.
- `dRGj2Ycd`: healthy mirror, two CURRENT assessments, temporary read-only
  views unloaded, pool absent afterward. PASS; backing and overlay hashes
  unchanged.
- `ACatwJxl`: first extreme run reached successful assessments but failed
  a test expecting ENXIO from refused normal assessment. Upstream TRY_REWIND
  returns original EIO rather than DO_REWIND's ENXIO. Retained as failed test.
- `UwTDE8fo`: corrected extreme run also verifies wrong-GUID rejection,
  pool absence, normal-assessment EIO, normal-recovery ENXIO and real CLI
  refusal, followed by two extreme recoveries/exact reads. PASS; all backing
  and overlay hashes unchanged.
- All Manager runs exercise non-root rejection and strict assessment option
  parsing (two valid modes, five malformed cases, output unchanged on error).

GUI `MCKON9B5` displays both assessment modes, preserves the draft on reopen,
rejects a writable assessment draft, and previews options without submission.
Its supported close is followed by a passing public storage regression suite.
The screenshot exposed misleading generic mutation wording and an unrelated
generic dry-run flag; these were corrected in `JbDgWkI2`. No GUI recovery was submitted in this
first run; actual assessment execution is proven by the native API tests above.

Fresh GUI `UXACpaAD` verifies the final build: request preview and confirmation
identify read-only assessment, and generic Dry-run is off. An explicitly
confirmed assessment of `assessment_missing` at the deliberately nonexistent
`ram:missing-assessment.img` reaches authenticated admission and returns
ENOENT (`errno=2`), displayed as an engine failure. Transport and ticket
release return zero; no automatic retry occurs. This is a real GUI-to-backend
error path, not a successful assessment of that nonexistent pool. The supported
window close is followed by a PASS from the full public storage regression
suite (terminal exit 0). Runner admission preflight also passes. Four completed
native-test ISOs, including the failed assertion run, were hashed and pruned
to reclaim approximately 18 GiB; logs and disk backing chains remain.

Logs, screenshots, action records, hashes and failed attempts are retained.
This does not qualify arbitrary corruption/topology, interrupted assessments,
physical power loss, or the complete port.
