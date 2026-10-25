# Old service executable cleanup

The user requested further cleanup of the historical `openzfs-aros` tree.
The initial read-only audit measured 428 GiB total, with 427 GiB under
`build-aros`; tracked source was clean. No active QEMU command referenced it.

This bounded pass archived and removed 944 untracked generated executables
named `OpenZFSServiceNative` or `OpenZFSAdmissionServiceHarness` below that
exact build directory. It reclaimed **34,053,320,704 allocated bytes**
(approximately 31.71 GiB). It did not remove source, logs, screenshots, pool
members, VM disks, or other backing images. The remaining old tree still
requires further inventory; this is not a claim of complete tree cleanup.

Recoverable compressed originals and the executable cleanup script are at:
`/mnt/short-term/openzfs-old-services.cDQ9sJs4`.
External filesystem UUID: `18df445b-e065-44d2-b4ec-d98c69af396a`.

The script verifies that mount identity and a 100 GiB free-space floor, checks
each source is untracked and not open, compresses it, verifies the decompressed
SHA-256, fsyncs archive/directory/manifest, rechecks source identity and open-file
state, and only then unlinks the exact source. Manifest records preserve source
and archive paths, hash, size, allocated bytes, mode and modification time.
The final manifest has 944 unique source paths; all originals are absent and
all corresponding archive files exist. The process exited successfully.

To recover an individual executable, decompress its manifest-listed archive
to its recorded source path, verify SHA-256, and restore its recorded mode.
Do not overwrite a newly regenerated binary without checking it first.
