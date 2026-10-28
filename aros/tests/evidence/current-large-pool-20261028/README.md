# Current-engine large-pool hosted qualification

`hosted-large-pools.2evbSUUu` passed using freshly compiled pool/reader frontends
from `large-pool-fixtures.fyWhOPGh`, linked against the normal engine object
`mmake-engine.cjQbkyg6`. Before reuse, its compiler-input manifest, source hashes,
engine-object hash and non-diagnostic provenance were verified. This is a fresh
frontend build against a verified existing engine, not a clean engine rebuild.
The selected source-built DOS64/POSIX/stdc/stdcio runtime matches the preceding
runtime-only qualification; this run no longer depends on old fixture binaries.

Five sparse members each have 14293651161088 bytes. Creation, a 16384-byte DMU
write/read comparison, real ZPL fixture creation, export/import and independent
host zdb integrity checks passed. A separate hosted boot read the filesystem
through DOS packets, validated its startup descriptor and reported
56939688246688 available bytes at 32768-byte blocks. Structural sparse digests
matched before/after that read-only boot. They cover size, hole layout and
allocated bytes, not a full logical hash of every hole byte. These are hosted
virtual file-vdev results, not QEMU, physical disks or NAS access.

Two rejected runs are retained:

* `TpSv7Z2q`, frontends `CasWxCbw`: creation passed but direct dataset creation
  returned EACCES because this old diagnostic omitted an explicit kernel
  credential scope. Only the private fixture now scopes its creation operations,
  following the existing QEMU pool diagnostic. Production authorization was not
  weakened. The live private guest was stopped after the explicit failure was
  inspected; it was not treated as a stalled successful boot. The runner now
  exits promptly when a diagnostic reports FAIL.
* `A4eDU3Ie`, corrected frontends `fyWhOPGh`: data roundtrip passed but the harness
  rejected nonempty stderr before host zdb. The full upstream libzpool engine's
  fm_init emits `libzpool: NOTICE: failed to create fm/misc kstat` because its
  host-kstat sink is absent. The harness now admits only zero or two copies of
  that exact notice (two kernel initializations); all other diagnostics and
  unexpected repeats fail. This does not claim that general kstat support exists.

Build logs retain pthread-header redefinition and BSTR signedness warnings.
No pool disks, runtime trees or binaries are committed. All original evidence
remains under the candidate build-audit directory. The next GPT native debugger
image `zdb-label-run.sqr6jdSX` was prepared separately and is not runtime-qualified.

Storage maintenance: completed BygYEOql and ZtNr9leW test ISOs were compressed
with zstd, decompressed SHA256 compared, synced, checked unused and only then
their uncompressed copies unlinked. Each original remains recoverable with
`zstd -d --keep test.iso.zst`. Archive/original manifests are retained here.
Logs, overlays, original pool files and the pending GPT ISO were untouched.
