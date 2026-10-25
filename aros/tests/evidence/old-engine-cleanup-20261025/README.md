# Old engine object archive

Archived 294 inactive regular `full-engine.o` files from the old
`/home/terminills/Documents/GitHub/openzfs-aros/build-aros` tree.
Reclaimed 14,662,455,296 allocated bytes (13.66 GiB).

Recovery archive: `/mnt/short-term/openzfs-old-objects.3tItza0x`
on filesystem UUID `18df445b-e065-44d2-b4ec-d98c69af396a`.
The directory occupies 3.9 GiB and must be retained for recovery.

Each source was checked with fuser before reading and again before removal.
Compressed content was decompressed and SHA-256 checked against the source,
then fsynced. Source inode, size and modification timestamp were checked
unchanged. A durable manifest entry preceded each individual unlink.
No source, disk fixture, log, screenshot or running process was removed.

`manifest.jsonl` maps original paths to gzip archives and original hashes.
Restore a selected object by decompressing its archive to its recorded source
path, only after checking that the destination does not already exist; compare
its SHA-256 with the manifest. These objects are also rebuildable from source.

Final observation: old tree 428 GiB; root free 162 GiB; external free 597 GiB.
This is a bounded object cleanup, not completion of all old-tree cleanup.
Concurrent system activity means filesystem free-space change differs from
the sum of reclaimed allocated bytes.
