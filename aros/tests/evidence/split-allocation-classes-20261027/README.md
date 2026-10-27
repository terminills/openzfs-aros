# Persistent split with special and dedup allocation classes

The existing production shared engine `shared-engine.5HkSG7Ab` passes two
additional upstream CLI split layouts. Each starts with a normal mirror plus
a mirrored allocation-class vdev. The native DOS handler writes an exact 4 KiB
payload before splitting, and reads it from both independently imported pools.
The dedup case writes a second identical file with compression disabled.

Final eight-CPU QEMU runs:

| Layout | Writer | Separate cold reader |
| --- | --- | --- |
| special | `NI5sF03m` | `bc7WAMW3` |
| dedup | `5z9Kdpcq` | `gzjZloR0` |

All four runs pass exact data and readonly-write refusal on both resulting
pools, clean backing-FAT checks, independent host block/leak and full MOS-object
checks. Cold readers retain identical member hashes and leave the original
writer containers unchanged. Host checks require the persisted allocation bias
and nonzero allocation in the relevant class in both pools. The dedup reports
also require nonzero deduplicated bytes: both pools report 4,608 deduplicated
bytes and 21,504 bytes in the dedup class.

Earlier attempts are retained, not promoted to full passes:

- `OuNjcCGe` and `EGdDaXJc`: guest readback passed, but the initial host gate
  incorrectly expected allocation bias in `zdb -C` label configuration. The
  property lives in the top-vdev ZAP and is visible in the MOS-object report.
- `hhpv91Lg`: small-payload special-class writer passed the corrected gate.
- `tkLKXiAs`: small-payload dedup writer read back correctly but failed the
  nonzero dedup-allocation check. Small embedded data did not exercise shared
  dedup blocks. The final fixture uses the larger uncompressed duplicate data.

Upstream SPA transfers the top-vdev ZAP during split; no new production fix
was necessary. This is file-backed QEMU coverage, not all allocation policies,
concurrent split workloads, physical device durability or power-loss proof.
The cold fixture never recreates or splits a pool. Full logs and backing images
remain in the external candidate build directory; regenerable terminal ISOs
are hash-recorded and pruned separately. No DUT/user-hosted session was used.
