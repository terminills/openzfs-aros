# Retired test-artifact cleanup, 2026-10-28

The user requested old-tree cleanup and retention of logs/screenshots rather
than hundreds of VM disks. The archive mount UUID and open files were checked.

`pruned-successful-overlays-20261028.jsonl` records 490 deleted QCOW2 overlays,
23.922 GiB allocated. Each was older than 72 hours, belonged to a retired
native-import run with an exact successful completion marker, had an explicit
raw backing format, and was hashed immediately before deletion. Files open
in another process and uncertain backing relationships were not eligible.
Underlying raw disks, failed/unqualified runs, logs, screenshots and source
snapshots were not deleted. Deleted overlays are not recoverable.

`runtime-compaction-20261028.jsonl` records 22 old hosted runtimes compacted
across two cleanup passes, reclaiming 4.600 GiB. These remain recoverable in
their `retired-runtime.tar.zst` archives; GNU tar comparison passed before the
original runtime directories were removed. Pool files and evidence remain.

The retained-public ISO scripts archive terminal inactive test images, including
failed tests without reclassifying them as successful. Decompressed SHA-256 must
match the original, file identity must remain unchanged, and no process may hold
the image before its uncompressed copy is unlinked. Original/archive manifests
are retained alongside this note. These images remain recoverable.

Shared free space continued changing during cleanup. Package creation still
uses the 100-GiB reserve and was refused at 98 GiB rather than overriding it.

## External relocation of compressed historical builds

`move-retired-archives.py` moved 741 files from the retired objects and services
archive directories to the Amiwest external drive (UUID
`05930f9c-5f05-476d-81eb-205389db82ad`). It checked open file descriptors and
working directories, copied without overwriting an existing destination,
flushed and SHA-256 checked each copy, rechecked the source identity and hash,
and retained per-file records in `moved-retired-archives.jsonl` before unlinking.
The original directory paths are now symlinks to the external copies.

This reclaimed 10,137,989,120 allocated bytes (9.44 GiB) on short-term storage.
All archive contents and their existing manifests/logs remain recoverable on
the external drive. Source checkouts, current builds, test-pool disks and
`build-control/zdb` were not removed by this operation.
