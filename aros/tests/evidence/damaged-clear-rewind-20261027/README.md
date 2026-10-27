# Damaged-pool CLEAR rewind on native AROS

These tests exercise the upstream `zpool clear` recovery path, not an import
rewind renamed as a clear test. All guests are isolated eight-CPU QEMU with
fresh writable overlays on retained disposable mirror fixtures. No DUT or
user hosted session is involved.

## Changes and safety boundaries

Upstream's test-oriented `zpool import -V` creates an uninitialized SPA in
the namespace without opening its MOS. AROS now admits this flag through
the same authenticated, explicit member reservation path as normal import.
It retains the configuration GUID before upstream has a loaded SPA GUID.
Read-only verbatim import is explicitly refused: upstream defers activation
to `spa_mode_global` and would not preserve that requested read-only mode.
Combining verbatim import with checkpoint rewind is also refused.

For an uninitialized SPA, recovery requires the retained matching GUID,
writable service mode, writable admission, member count and active path
policy. No matching admission is a denial, not permission to proceed.
Loaded-pool identity/read-only checks and mounted-subtree exclusion remain.

The generic ioctl pool-status check previously called ordinary `spa_open`
before CLEAR could supply its recovery policy. On damaged metadata this
returned EIO before reaching `spa_open_rewind`. The AROS rewind-clear case
now uses the admission-aware preflight at that boundary. Ordinary clear
retains the upstream status/read-only check; other platforms are unchanged.

## Native evidence

Production engine: `shared-engine.VpiTT74c`, built from all normal engine
units in `mmake-engine.yUQKZkX9`. Actual upstream CLI:
`mmake-cli.JZ90tGIt/stage/C/zpool`. These use the selected private SDK,
runtime and dependencies, not a stock-nightly compatibility claim.

- Old engine control `native-import-run.bXOEz7E4` refuses writable `-V`.
- First admitted candidate `guoGsdrD`, then diagnostic `kk1lUYsp`, accepts
  writable `-V` but fails CLEAR assessment with I/O error at the ordinary
  pool-status guard. These failed attempts are retained.
- Extreme writer `snsiycvF`: ordinary damaged import fails twice; readonly
  import recovery reads exact old data twice; non-root `-V` is refused;
  readonly `-V` is refused and leaves no pool; writable `-V` succeeds.
  **Actual `clear -nFX` reports a recoverable historical state; `clear -FX`
  succeeds.** The native handler writes/flushes `after-recovery`, exports,
  then ordinary readonly import reads both original and new exact data.
  Both overlays change and both original corrupt backing images remain
  unchanged. This is not a zero-write assertion for the assessment alone.
- Cold reader `XrrAIefQ`: the writer exits, its overlays are flattened into
  new raw fixtures `qemu-run.clear-cold.Tax9gyqA`, and a separate guest
  performs ordinary readonly import (no recovery flags). Both exact files
  match, writes are refused, export succeeds, and raw/overlay hashes remain
  unchanged. Extracted members pass independent host `zdb -e -bcc` with no
  leaks. The writer's memory cannot supply this result.
- Normal writer `j43qLvyA`: the independently prepared recent-damage fixture
  `qemu-run.recent-corrupt.zj_ccalr` passes the same flow using actual
  **`clear -nF` and `clear -F`**, without extreme fallback. This run includes
  ordinary reimport/readback but not a separate cold boot of its own output.
- General regression `public-service-run.NCDWOlW7` passes the full public
  service/mirror suite, including Manager healthy clear recovery, malformed
  wire policy, changed-user/own-session denial, mounted refusal and readonly
  refusal. Host tests exercise the actual uninitialized-admission functions
  with controlled state fields; they are not native storage proof.

## Reproduction and limits

Use `test-qemu-native-import.sh` with `AROS_TEST_IMPORT_REWIND=1`,
`AROS_TEST_IMPORT_REWIND_MODE=commit`, and `AROS_TEST_CLEAR_REWIND=1`.
Supply the exact service/library, CLI, handler, SDK/dependencies, private
runtime and disposable native fixture as usual. The recent-damage variant
also sets `AROS_TEST_IMPORT_REWIND_NORMAL=1`. The runner requires the
verbatim authorization, assessment report, clear commit, exact DOS readback
and changed-overlay/unchanged-backing markers. Manager/cut variants are
rejected for this dedicated CLI test rather than silently conflated.

For independent cold readback, flatten only a completed writer's overlays
to a new fixture and use mode `cold` without `AROS_TEST_CLEAR_REWIND`.
The existing damaged-import evidence describes the exact corruption and
member partition offsets used by both fixtures.

The Manager worker now also passes the damaged-pool end-to-end run below.
A visible damaged-pool UI session, assessment-only disk equality,
interrupted CLEAR, other topologies and physical durability remain
separate qualification work. `-V` is an expert diagnostic mechanism, not a
default import policy or automatic startup action.

## Manager worker integration

`native-import-run.p9JiiFji` uses the same production engine and damaged
extreme fixture, but invokes the actual Manager `oz_project_worker_start`,
poll and join path for both assessment and commit. The operation-17 requests
use `recovery=extreme;assessment=on` and `recovery=extreme` respectively.
The worker validates the API request, launches the exact CLI argv under the
inherited identity, captures `exit=0, DOS=0, capture errno=0`, and returns
the actual historical recovery report. Both calls join successfully; exact
DOS write/flush/export/ordinary-readonly-reimport checks then pass. This is
not a replacement worker or a mock command response. No GUI screenshot is
claimed for this headless execution-path test.

Add `AROS_TEST_CLEAR_MANAGER=1` to the commit-mode CLEAR replay to select
this path. Its explicit input hashes and serial log are retained here. The
runner rejects incompatible whole-CLI-link and import-Manager/cut modes,
and requires a separate Manager completion marker in addition to the data
and image checks.
