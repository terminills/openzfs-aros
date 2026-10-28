# Runtime preflight: child teardown must publish output

The previous runtime checker explicitly flushed in its child and again in its
parent, masking loss of buffered output at normal return or exit. The checker
now tests all three exit paths and reopens each output file independently
before the parent flushes or seeks its inherited handles. It still verifies
the parent's handles remain usable and checks cleanup. `--streams-only` runs
this narrow non-storage probe without implying other runtime checks passed.

Final test binary SHA-256:
`7dad34e7795386587f1048543df8fe79a36afff33837813ba3de0ba51bcc221e`.

- `S45nE8Z6`: matched stdc, retained stdcio flag-restoration fix, POSIX without
  teardown hook. Explicit-flush control passes. Normal-return and exit cases
  fail independent stdout visibility, while parent recovery succeeds. This
  demonstrates exactly why the old parent-flush check missed the defect.
- `QAxtOoUC`: same checker/stdc/stdcio, source-built POSIX flush hook. All three
  independent visibility and inherited-handle checks pass with clean teardown.
- `YaaWgy5k`, `EyOQIeoE`, `7j9WjzTA`: older hosted or SDK-installed stdcio
  combinations fail inherited stdout Flush with DOS error 219, even when
  independently visible. Do not regard the SDK-installed runtime as qualified.
- Earlier probe attempts (`UIbRfqo9`, `3163rzHp`) used exclusive output handles
  and reused a directory after failed cleanup. `K6JitaMc`, `fkEoRRdH` correct
  those fixture issues using shareable MODE_READWRITE and per-case directories,
  exposing the additional inherited-handle failure. All attempts are retained.

Runtime input hashes and source patches accompany each result. These are
private hosted controls, not hardware, stock-runtime or full-package proof.
No pools or devices are opened by this focused check. No global settings,
credentials, DUT or user's hosted session were changed.
