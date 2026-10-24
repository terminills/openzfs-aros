# Native pool-root filesystem initialization

The AROS resident engine builds without `_KERNEL`. Upstream `dsl_pool_create`
therefore skipped `zfs_create_fs`, leaving the pool root without ZPL metadata,
although child filesystems were usable. Enable the existing AROS ZPL initializer
for the pool root without changing non-AROS libzpool behavior.

Evidence sequence:

- `initial`: prior engine failed mounting a newly created native-partition pool
  root with errno 2. The first rebuild also exposed the missing userland
  declaration; its compiler diagnostic is retained.
- `ordering`: corrected engine passed root handler write/read and import tests,
  then failed a later blank-label assertion. Destroyed pools retain labels:
  run blank-label tests before deliberate pool creation, not after destruction.
- `passing`: complete eight-CPU QEMU run `public-service-run.KyVi5QOr` passed
  lifecycle and storage gates, including 128 CPU-topology queries.

The data test writes and flushes a file through the DOS handler on the pool
root, exports, imports read-only, checks exact contents and denied creation,
then exports/imports writable and checks contents again. A neighboring pool
on a separate partition stays online with its property intact. Both fixture
pools are destroyed at the end. Host backing-image hashes remain unchanged
because the native-discovery drives use QEMU snapshots.

Candidate: `source-engine.yzGELaNm`, `shared-engine.NAQEJFme`; external client
rebuilt by `contrib-openzfs-upstream-test`. The runtime includes the documented
private DOS and AHCI overlays, not an unmodified stock nightly. Input hashes
are retained per run. Successful test ISO was pruned; logs remain.

This is same-boot export/import proof, not cold-boot, physical-device or power
failure qualification. Existing pools with uninitialized roots are not repaired
by this change. Previously packaged bundles do not contain this fix.
