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
