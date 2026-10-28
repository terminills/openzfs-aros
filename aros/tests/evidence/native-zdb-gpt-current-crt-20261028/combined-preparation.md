# Follow-on combined runtime preparation

The release debugger gate accepted `QRkxXhcQ` against the selected
`current-service.RarHl71z/zdb` and `openzfs.library`. All 20 staging unit tests
passed. This is not completion of the other required native release runs.

Older private combined-run recipes selected `debugger-service.0umlrjD3`,
`dependency-client.dIXInP1o`, `mmake-cli.uLkDlrcx`, and the old POSIX snapshot.
They must not be reused to claim qualification of the current binaries.
The replacement private recipe selects the already-tested `3zlIqhLI` shared
engine, `V4Xt4xQS` acceptance client, `hG2vWP1T` CLI/Manager, and the same
four CRT libraries recorded by the current native debugger run. Its lifecycle
test was freshly compiled against the selected generated library includes.
All four package binaries pass their recorded SHA-256 checks.

The combined native runner now extracts and compares each selected CRT and
shared engine from its final ISO before launching QEMU. The actual new check
block was executed against the retained `LuO2nC45/test.iso`; all five runtime
files matched. Results are retained externally in
`build-audit/iso-runtime-check.N1aXhsKl`, with hashes copied alongside this note.
This checks ISO contents, not a new native GUI/cold/encrypted-root execution.

The release stager now requires `iso-runtime.sha256` for every selected CRT
in each of the GUI, cold, encrypted-root, and debugger acceptance runs.
Input hashes alone no longer qualify an older combined run. The 20-test
staging suite exercises absent, empty, duplicate, malformed, and mismatched
ISO manifests independently for all four runs. Missing evidence must be
obtained from the actual test image, not synthesized from the intended inputs.

Completed `sqr6jdSX` and `QRkxXhcQ` ISO files were compressed with zstd.
Decompressed hashes matched the originals, file identity remained unchanged,
and no open users were found before removing only the uncompressed ISO files.
About 8.25 GiB was reclaimed. Each ISO remains recoverable from `test.iso.zst`;
logs, screenshots, pool images, source, and selected runtime inputs remain.
The archive manifests are copied here for recovery.
