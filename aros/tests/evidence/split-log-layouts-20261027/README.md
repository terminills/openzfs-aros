# Trailing and mirrored log split layouts

This extends the persistent split harness with a trailing single log and an
interior mirrored log. The real CLI creates two data mirrors, writes and
flushes a handler payload, splits one member of each data mirror, and separately
imports both pools readonly. The host checks FAT, block checksums/accounting
and complete MOS-object reachability. A distinct cold-reader guest must recover
the same data without creating or splitting pools, with unchanged member bytes.

## Reproduced failure

Previous engine `fttbNbqO` passes the mirrored-log writer `DtADklGX`, but
trailing-log writer `vT11IWlq` page-faults during split. The recorded IP is
`0x4edc1f0`, CR2 `0x60`, and first stack return address `0x4ec014c`.
The exact binary places `vdev_is_concrete` at `0x294a70`, whose first instruction
reads `0x60(%rdi)`. With relocation base `0x4c47780`, the stack address resolves
to `spa_ld_log_sm_cb+0x3c`, immediately after its call to that function.
`vdev_lookup_top` returns NULL for a trimmed trailing-log slot still referenced
by an inherited log-space-map entry. This is distinct from the previously fixed
omitted-log metadata leak and from the earlier configuration-lock assertion.

## Candidate

Internal split construction now preserves trailing omitted slots as explicit
holes after validating the source topology and user request. Existing replay
logic skips holes, as it already does for interior omitted logs. An unrelated
invalid log-space-map vdev reference now returns EINVAL instead of dereferencing
NULL; it is not silently accepted. Full engine `mmake-engine.yvpCNmGX` builds,
with shared engine `ndJXbVlr`.

## Qualification

- Trailing-log writer `SizRYsgf` and separate cold reader `MGJZf9UN` pass.
- Mirrored-log writer `QNBWw3ZF` and separate cold reader `b0UDnX2y` pass.
- Each eight-CPU run passes actual CLI/handler data and write-refusal checks,
  clean FAT, independent block verification and full MOS-object reachability
  for both pools. Cold member hashes match their writer exactly, and original
  writer containers remain unchanged.
- General library/service/handler/Manager run `cvi76ijG` passes with the same
  candidate engine. Native runner admission preflight passes without VM media.

The original fault and exact-binary disassembly are retained alongside the
passing logs and hashes. Full logs, frozen sources and backing containers
remain in the external build archive; generated completed ISOs are hash-pruned.

These are isolated selected-runtime file-backed tests, not stock SDK,
concurrent live workload, every layout or physical-power-loss qualification.
