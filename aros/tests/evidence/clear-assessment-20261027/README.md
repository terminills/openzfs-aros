# Damaged CLEAR assessment without recovery commit

Three independent eight-CPU QEMU guests used the production engine
`shared-engine.VpiTT74c`, CLI `mmake-cli.JZ90tGIt`, the same original damaged
mirror fixtures and fresh overlays. The engine is unchanged from the
damaged CLEAR commit/cold tests; this extends qualification, not a new
recovery implementation.

| Run | Assessment path | Complete member comparisons |
| --- | --- | --- |
| `native-import-run.sopWnp3k` | actual `zpool clear -nFX` | both identical |
| `native-import-run.nOJodleo` | actual asynchronous Manager worker | both identical |
| `native-import-run.IwGrGVIJ` | visible Manager preview, confirmation, submission and report; then automated CLI assessment | both identical |
| `native-import-run.XJlS2fmF` | actual normal `zpool clear -nF` on the recent-damage fixture | both identical |

Each run first demonstrates ordinary damaged-import refusal and readonly
import recovery, refuses non-root and readonly verbatim admission, then
admits the same pool for assessment through explicit native member
reservations. The test deliberately exits before any recovery commit,
dataset write or export. The private VM is terminated after its completion
marker. Original backing hashes are unchanged. Host `qemu-img compare`
then checks every guest-visible byte of each complete disk against the
original raw backing, rather than treating qcow2 container hash changes as
data changes. The comparison logs are retained here; full run directories
also retain overlay hashes and allocation maps.

This proves unchanged disk content for this damaged mirror assessment. It
does not prove that no write commands were issued: upstream may probe a
device with identical writes. The UI therefore retains that qualification.

## Visible UI verification

In the isolated native-import guest, Advanced / Pools / clear was selected,
target `qemu_mirror`, options `recovery=extreme;assessment=on`. The preview
and confirmation state that assessment commits no rewind, device probes may
write unchanged data, extreme recovery can expose inconsistent data, and
files/datasets must be closed/unmounted. Submit returned `exit=0, DOS=0,
capture errno=0` and the actual historical recoverability report shown in
`assessment-result.png`. The activity log recorded it too. The Manager was
closed normally and the client/harness finished successfully. No other
hosted session or physical DUT was inspected or controlled.

The screenshots and action log are direct QMP observations/actions of that
owned guest. The harness's GUI-return marker proves orderly completion,
not the contents of a clicked response; the screenshots supply that
separate visual evidence.

## Reproduction

Use the damaged CLEAR commit-mode fixture with
`AROS_TEST_CLEAR_ASSESSMENT_ONLY=1`. This preserves the admission/setup
sequence but compiles out the commit and stops before file writes. The
runner requires an assessment-only marker, refuses a commit marker, and
uses complete-disk comparisons instead of requiring changed writer images.
Add `AROS_TEST_CLEAR_MANAGER=1` for the worker route. For the visible route,
set `AROS_TEST_CLEAR_GUI=1`, `AROS_TEST_GUI=1` and the explicit matching
`AROS_TEST_MANAGER_BINARY`; close the Manager after the manual UI actions.
GUI mode is allowed only with assessment-only mode, and does not count as
pass until the subsequent disk checks succeed.

The fourth run extends unchanged-content proof to normal bounded `-nF`
assessment. Other topologies and physical-device durability remain separate
tests. The later `interrupted-clear-20261027` evidence covers one interrupted
CLEAR commit boundary and independent cold recovery.
