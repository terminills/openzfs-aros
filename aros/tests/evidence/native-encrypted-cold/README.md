# Fresh-guest encrypted native partition recovery

Run `public-service-run.aCHszgw9` passed with eight CPUs and the production
`shared-engine.TEtOTOsm` engine. The runner's `AROS_TEST_NATIVE_COLD=1` mode
retains a freshly created native-partition encrypted root after the writer's
read-only/writable import, wrong-key and exact-data checks. It exports the pool
cleanly and terminates the writer VM before starting a separate reader VM.

The reader uses the same virtual ATA disk, not a RAM file or inherited QEMU
snapshot. It imports read-only, observes an unavailable key, gets EACCES when
mounting the locked root, loads the key from a newly created RAM key file,
reads the exact payload through the DOS handler, verifies write protection,
detaches and exports. Both guest phases reach terminal storage PASS. The whole
native disk's SHA256 is unchanged across the reader phase.

This is clean-export fresh-VM native-partition recovery, not abrupt power loss,
hardware cache durability, writable cold recovery or multi-device native RAID.
The existing file-mirror cold tests remain separate evidence.

## Retained attempts

- `AcnXDdAj`: writer attempted a new pool on a destroyed pool's nonzero extent.
  Current native create admission requires an entirely blank extent, returning
  EEXIST (rendered as "pool already exists"). The test now retains the first
  pool instead; the native reuse limitation is documented in PORT-STATUS.
- `native-cold-qemu-2.log`: preboot root-space guard refusal; no guest test.
- `yHAgJcCI`: writer passed; deriving the reader ISO from the writer ISO caused
  xorriso to reject inherited protective-MBR size metadata. Building both from
  the original base with the same mapped inputs resolved the harness failure.

The pruner now requires cold-reader and unchanged-disk evidence before removing
these disposable ISOs. Explicit external artifact roots require one named run
on a separate filesystem; active images remain excluded. All thirteen host
retention-policy tests pass. Logs, hashes, source snapshots and native fixture
disks are retained; successful disposable ISOs are removed automatically.
