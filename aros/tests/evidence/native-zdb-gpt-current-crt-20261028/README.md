# Current-CRT GPT debugger qualification

The first run, `zdb-label-run.sqr6jdSX`, failed native pool admission with
ENOENT. Its parent test binary correctly selected native GPT positions 0 and
1. The authenticated root helper merely forwards arguments; it does not need
the fixture's GPT preprocessor flag.

The selected ISO did not include the private GPT gap fix documented in
`../native-zdb-gpt-20261027/README.md`. That earlier controlled experiment
established that the stock partition library fails this deliberately gapped
GPT fixture even with correct native positions. This failure is not evidence
of a new OpenZFS pool traversal regression. The transcript and fixture layout
are retained under `failed-stock/`.

Prepared replacement: `build-audit/zdb-label-run.QRkxXhcQ`. It selects
`partition-gpt.YGZVq2U0/aros-fs.pkg.xz` alongside the same current CRTs and
shared debugger. The harness now extracts an explicitly selected filesystem
package from the final ISO and compares it byte-for-byte, in addition to the
existing runtime library checks. Its hash is retained in `iso-runtime.sha256`.

At preparation time this is NOT a native pass. The replacement still requires
an owned QEMU slot, complete pool/authorization/L2ARC checks, and unchanged
device comparisons. No DUT or user hosted session was used.
