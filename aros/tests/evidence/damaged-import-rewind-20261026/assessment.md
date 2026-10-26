# Native CLI recovery assessment

Actual MetaMake CLI `mmake-cli.xSOZUKNQ`, production shared engine
`shared-engine.Rw1LBfue`, alpha18c5 with explicit DOS/posix overlays, eight
virtual CPUs. No DUT or user-hosted session involved.

`AROS_TEST_IMPORT_REWIND_DRYRUN=1` runs real `zpool import -nF` (normal)
or `-nFX` (extreme), without adding `readonly=on`. The client requires the
upstream recoverability report and confirms the pool remains absent. The
expected nonzero status is not sufficient proof: libzfs deliberately returns
failure while printing the successful recovery assessment from load-info.
Two subsequent cycles must still refuse ordinary import, recover read-only,
read the original DOS payload exactly, reject writes and export.

- `R3ilInoz`: normal assessment on recent-root damaged fixture, PASS.
- `UFOpS4zy`: extreme assessment on older damaged fixture, PASS.
- Both original backing hashes are unchanged in each run. `qemu-img compare`
  proves both complete guest disks remain byte-identical to their backings.
- Initial `suGEQULc` and `j41l0Nbo` reached guest PASS but failed the overly
  strict QCOW2 container-hash gate. These are retained as harness failures,
  not retroactively relabeled successful runs.

Upstream `vdev_probe_done()` in `module/zfs/vdev.c` writes the just-read
buffer back when the SPA is writable. This can allocate QCOW2 clusters while
preserving every guest-visible byte. Therefore the dry-assessment gate saves
container hashes and allocation maps but requires full-disk content equality.
Other read-only tests retain their original strict hash gate. This does NOT
prove zero issued writes or hardware power-loss safety, and dry assessment
must not be advertised as a substitute for read-only device access.

The per-run evidence directories retain serial/completion/runner logs,
toolchain and input hashes, allocation maps and comparison output. Large
test ISOs can be pruned after termination with hashes recorded separately;
source fixtures and overlay chains are retained externally.
