# Encrypted native-partition pool-root regression

This test creates an encrypted pool root through upstream `zpool create -O`,
with raw key, lz4 compression, mixed case sensitivity and formD normalization.
It checks six properties, handler data, locked-root mount denial, key reload,
read-only import/write denial and writable reimport while a neighboring pool
remains online on a separate disposable partition.

Initial run `public-service-run.mQbQfkv8` aborted in the resident service during
the property sequence after read-only import and key reload. The screenshot
records the abort requester. The harness timed out because the caller was
waiting for the terminated service. No root failure cause was present in the
serial log because stderr was redirected to a guest RAM file.

Diagnostic engine `source-engine.14ltEdvc` / `shared-engine.eHUOmIAH` adds
AROS fatal assertion reporting to the debug channel. Repeat run
`public-service-run.aBGfk3HJ` reproduces the failure and terminates with:

```
ASSERT at module/zfs/arc.c:1750:arc_buf_try_copy_decompressed_data()
VERIFY3B(!copied, ==, hdr->b_l1hdr.b_freeze_cksum == NULL) failed (1 == 0)
```

The existing checksum assertion is retained. These are failure records, not
qualification of encrypted native pool roots. The last verified local package
predates any ARC repair. The test uses QEMU snapshot-backed disk files only.

## Authentication-only repair and passing evidence

`arc_untransform` is also called for cached, already-decompressed metadata
whose MAC has not yet been authenticated. Previously it refilled that buffer,
reentering decompression even when the buffer already carried a freeze
checksum. `ARC_FILL_AUTH_ONLY` now distinguishes that initialized plaintext
case. The existing protected-header authentication and error path still run;
the caller's bytes are not recopied, decompressed or byteswapped again. The
freeze checksum is verified, not removed, and the failing invariant remains.

Fresh engine `source-engine.dStCp732` / `shared-engine.TEtOTOsm` passes run
`public-service-run.R3HVgA65`: all six root properties survive both imports,
locked mounts are refused, keys reload, exact root data survives and read-only
writes are denied. The neighboring pool and full eight-CPU storage suite also
pass. Original snapshot-backed images stay byte-identical. This focused run
does not include the optional AHCI inquiry fixture.

Cold regression `public-service-run.u8VlIKS6` passes clean export/shutdown,
fresh-guest healthy/degraded/restored mirror imports, root and child data,
encrypted child/key-rotation checks, unchanged member hashes and independent
zdb traversal with no leaks. This is software qualification, not power-loss
or physical-storage qualification.

The original combined AHCI run `M7HY60RD` was still preboot at evidence capture:
host `sgdisk` waited on writeback to the external archive drive. It was not
restarted or represented as a passing test. Failed directories `mQbQfkv8`,
`aBGfk3HJ` and GUI timeout `Wj9aMb4L` moved to
`/run/media/terminills/Dev_Stuff/OpenZFS-archive/public-regressions-20261024.sL1Wxp5O`,
with ISO hashes checked and original paths retained as symlinks. No artifacts
were discarded by that move; hash checks alone do not prove durable USB flush.
