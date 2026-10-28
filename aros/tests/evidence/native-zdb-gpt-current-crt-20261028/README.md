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

## Native result

The replacement subsequently ran in an owned QEMU slot (8 vCPU, 2 GiB,
no network). `pass/` retains its native transcript and host verification:

- Full pool traversal matches three dataset descriptions, 21 feature checks,
  and 72 object rows from the independent host debugger.
- Authenticated root access, unprivileged EACCES, partial-admission failure,
  duplicate-path recovery, and member-zero reopening passed.
- Shared debugger lifecycle and caller-exit checks, damaged labels, and
  L2ARC endian/corruption/bounds/recovery checks passed.
- Both native ATA overlays compare byte-for-byte identical to their bases;
  selected input hashes also remain unchanged.

The private replay script initially invoked the L2ARC verifier before creating
its required unchanged-device log. That postprocessing failed with a missing
file after the guest had finished successfully. After the VM exited, both
actual device comparisons were performed, and the L2ARC verifier passed.
The retained replay recipe now orders the comparisons before that verifier.
No guest rerun or fabricated result was used to cover the missing check.

The QEMU lease was released after this run. No DUT or user hosted session was
used. This closes the selected-runtime GPT debugger test, not the full port or
the remaining combined GUI/cold/encrypted-root packaging qualification.
