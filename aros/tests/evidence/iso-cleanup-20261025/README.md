# Disposable ISO cleanup, 2026-10-25

Removed 62 inactive generated ISO files from the explicitly scoped workspace
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.
Recorded allocated space: 303398576128 bytes (282.56 GiB).
Each subdirectory retains the exact removed paths, sizes, allocated blocks and
SHA256 values. The cleanup command completed with exit zero.

The first four images belonged to `qemu-run.BkLlOzNw`. The subsequent pass
was restricted to immediate `qemu-run.*`, `public-service-run.*`,
`native-gui-run.*` and `crash-run.*` children with source and input manifests.
Symlink targets were refused; open-file checks were performed before hashing
and again before removal. Logs, sources, patches, screenshots, binaries,
package archives and disk fixtures were not removed by this pass.

These ISOs must be regenerated if needed; the hash records cannot restore
their bytes. Failed and incomplete runs retain their original evidence status.
Deleting their disposable images does not turn them into successful tests.

Artifact-filesystem free space reached 609 GiB. Main-filesystem free space
reached 47 GiB during the same interval, but these external-disk deletions did
not reclaim main-filesystem space. No attribution of that separate increase
is made here. Nested retained directories and capacity-test ISOs were outside
this pass; this is not a claim that all obsolete artifacts have been removed.
