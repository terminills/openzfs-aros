# Archived old test-run service copies

The cleanup script retained here archives only loose `OpenZFSServiceNative`
copies in named, historical test-run directories under the old root-backed
`openzfs-aros/build-aros`. Original build outputs, source, logs, screenshots,
input manifests and RAID members are outside its removal scope.

Archives and the growing per-file manifest are under:

`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T/root-cleanup.MdBxRLrM/`

`archived-services.jsonl` maps every removed path to a compressed executable
and records its uncompressed SHA256, size, allocation and permissions.
Identical bytes share one archive in `service-executables/`. Every selected
file requires retained source/input provenance and no active file holder.
Files with multiple hard links are skipped. Archives are decompressed and
hashed before the loose copy is removed; metadata and holders are rechecked.

To restore an executable, decompress its named `.gz` archive to a new path,
verify the resulting SHA256 against the manifest, and restore the recorded
mode. Do not overwrite an active test or install an old binary in a running
AROS instance merely to recreate the old path.

This record describes the archival procedure, not a completed cleanup total.
The manifest and terminal process result determine actual progress. A
failure or interruption does not authorize deleting unrecorded loose copies.

## Independent restore check

`verification-prefix.jsonl` is a complete-line snapshot of the first 421
records, not the final manifest. Independent `gzip -cd | sha256sum` checks
passed for all 172 distinct archives named in that snapshot. Results are in
`verification-prefix.log`; the verification command exited zero. Later records
are not covered by this second check until a subsequent verification is run.
The archival process itself also verifies every archive before each deletion.
