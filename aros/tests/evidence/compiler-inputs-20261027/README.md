# Compiler-discovered engine inputs

Fresh full engine `mmake-engine.hZ1s42So` compiled 360 objects and recorded
1,427 compiler/build inputs, including system headers previously omitted by
`-MMD`. The exact source/object dependency inventory is checked before recording.
Four host tests cover dependency syntax, missing/stale inventories, traversal,
header symlink retargeting and changed objects. Retained manifest verification
passes against the actual build.

Shared build `shared-engine.5HkSG7Ab` was produced from that linked engine.
Fresh eight-CPU isolated QEMU run `public-service-run.gRiHdQkU` passes the
persistent writer, cold healthy/degraded/restored exact-data reads and third
writable deferred-snapshot release. FAT checks and independent block plus
MOS-object checks pass; read-only member comparisons are retained.

This verifies a build-evidence change with a real engine regression, not a new
filesystem capability. Input hashes are collected after compilation and do not
attest atomic immutable compilation inputs, toolchain provenance or complete
release source closure. See `aros/engine/BUILD-INPUTS.md`. Full build and QEMU
logs/backings remain under the external candidate's `build-audit` directory.
No DUT or user-hosted session was used.
