# Native Manager recovery API

Production shared engine `Rw1LBfue`, MetaMake CLI/Manager `umJTAT98`, and
eight-CPU isolated QEMU run `native-import-run.hEgdhl32` exercise operation 7
through actual admission, not a mocked UI backend. The fixture is the
damaged mirror described in README.md.

The strict policy parser accepts current, recent-transaction and extreme
recovery, preserves GUID selection, rejects malformed/conflicting options,
and leaves outputs unchanged on failure. Legacy parser callers refuse
recovery rather than silently dropping its policy. Non-root admission
returns EPERM. Two read-only extreme recoveries read exact DOS data without
committing. Writable extreme recovery writes, flushes, exports and reimports
the original and newly written files. Source backings remain unchanged;
both writable overlays change.

Separate guest `native-import-run.k20A0lPK` boots with new overlays over raw
conversions of the completed writer disks (`qemu-run.manager-recovery-cold.CZFQj8ZC`).
Ordinary read-only import, without recovery flags, reads both exact files,
refuses writes and exports. Both cold backings and reader overlays remain
unchanged. This rules out reliance on writer-process cache state.

Initial run `0KkVx2EP` failed a test expectation: the helper expected zero
for the deliberately unauthorized request and translated actual EPERM to
EIO. Correcting the expected errno made the test pass; no authorization
guard was weakened. Its logs are retained externally.

The Manager now offers mutually exclusive load modes and separate
read-only/writable warnings. It builds and opens in AROS, but visual
verification of the recovery editor is NOT complete: GUI run `y6obTfAS`
timed out during manual navigation. Do not count that as a UI pass or a
full public-service-suite pass. Normal recent-transaction recovery, other
corruption classes, interrupted recovery and hardware durability remain
outside this evidence.

Full logs and input hashes are retained under build-audit; selected serial
and runner logs are included alongside this note for review.
