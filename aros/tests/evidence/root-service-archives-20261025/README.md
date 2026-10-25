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

## Completed pass

The resumed fast-compression pass exited zero. Including the first 24 copies
archived before the compression-setting change, the final manifest records
881 loose copies removed, 280 unique executable archives, and 41383436288
allocated bytes reclaimed from root (38.54 GiB). Archives occupy approximately
3.5 GiB on the separate artifact filesystem. Final root free space measured
167 GiB; this is a filesystem observation, not attribution of all concurrent
space changes to this task.

`verification-final.jsonl` is the complete manifest. Independent decompression
and SHA256 verification passed for all 280 unique archives, with exit zero;
see `verification-final.log`. `archive-files.sha256` additionally hashes the
compressed archive bytes themselves. Original final build outputs, source,
logs, screenshots and all RAID images were preserved. This completes the
scoped service-copy archival pass, not cleanup of every old build artifact.
