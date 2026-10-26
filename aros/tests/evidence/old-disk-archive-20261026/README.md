# Inactive old-tree disk archive

The system filesystem fell below the 100 GiB build/test reserve. The new
Manager replay refused allocation; the reserve was not lowered or bypassed.

Two bounded archive batches moved 113 inactive old test backing files from
`/home/terminills/Documents/GitHub/openzfs-aros/build-aros` to the verified
external filesystem UUID `18df445b-e065-44d2-b4ec-d98c69af396a`:

- 111 native-import fixture raw files: 19,087,347,712 allocated bytes reclaimed.
- Two preallocated disks from the aborted hosted-large-pools.D5fH0jgJ fixture:
  9,310,195,712 allocated bytes reclaimed. This historical run did not complete
  a pool test; its recorded allocation problem and evidence are preserved.

Total: 28,397,543,424 allocated bytes (26.447 GiB). Sparse copies avoid storing
the original physically allocated zero padding on the external filesystem.
This is separate from the earlier 40-file/5.86 GiB archive.

Every file was checked untracked and unopened, copied without overwrite,
SHA-256 checked on both sides, fsynced, and rechecked for source identity and
open-file changes before replacing its original path with an external symlink.
Manifests were fsynced before replacement. A final audit verified all 113
symlinks resolve to their recorded archive and exact file sizes.

Archives remain at:

- `/mnt/short-term/openzfs-old-disks-20261026b.ljqA1RCn`
- `/mnt/short-term/openzfs-old-hosted-disks-20261026.xbYs9LyA`

The script and manifests are retained here. Source, logs and screenshots were
not removed. Backing data is still reachable by its original pathname while
the external filesystem is mounted. Recovery can copy a recorded archive back
with sparse preservation and verify its manifest hash before replacing the
symlink. No foreign active VM, DUT, user-hosted session or shared SDK changed.
