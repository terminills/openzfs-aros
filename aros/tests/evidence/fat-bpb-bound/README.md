# FAT backing-volume allocation boundary

The expanded OpenZFS reuse fixture exposed an AROS FAT geometry bug before
cold boot. Run `public-service-run.YrIorywW` reached guest storage PASS and clean
detach, but read-only host fsck found an out-of-range cluster and a shortened
`OZReuseChanges/disk4`. The runner stopped without repairing or cold-booting it.

The BPB describes 1,835,001 sectors; the containing virtual device has
1,835,008. With 512 overhead sectors and 32 sectors per cluster, the filesystem
has 57,327 clusters, not the 57,328 obtained from device geometry. The parser's
historical 64-sector geometry tolerance must not turn padding into allocatable
filesystem space. `fat-bpb-volume-bound.patch` computes data/cluster counts from
the validated BPB total and sets the mounted volume's sector count accordingly.

`test-fat-bpb-bound.py` executes both actual parser sources on the host with
only platform scalar/ID declarations stubbed. It reproduces the old extra
cluster, checks padding 0 through 64, and rejects too-short/too-large media and
metadata extending past the recorded filesystem. This is arithmetic evidence,
not native runtime proof.

The private full FAT rebuild `flush-overlay.wmpgpvg0` additionally retains the
existing flush/clean-bit/read-only patches. SHA-256 of `fat.handler`:
`7e15a444306fe0a1b901fcd66c7a403cca17e925385475bdaedec3760881cad3`.
It links a privately rebuilt parser object ahead of the SDK archive, leaving
the SDK and shared AROS source untouched. Enable with `AROS_FAT_BPB_BOUND=1`
alongside the existing full-FAT rebuild settings.

Native eight-CPU QEMU run `public-service-run.yp3paeiP` passes the identical
writer workload, read-only fsck after both boots, the changed-pool and baseline
mirror cold suites, unchanged member hashes and independent zdb checks. Logs
and exact build inputs are retained here; full pool evidence is in
`../changed-pool-cold`. No image repair, geometry enlargement workaround or
MTOOLS_SKIP_CHECK bypass was used. Physical storage remains unqualified.
