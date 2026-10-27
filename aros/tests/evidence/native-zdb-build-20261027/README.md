# Native zdb compile and dependency audit

Scope: isolated cross-build only. No debugger executed, no pools opened, no
DUT or user-hosted session touched. This does not qualify native zdb or change
the meaning of existing independent host-zdb integrity checks.

The private dependency environment is
`build-audit/dependencies.YF6P1Tca/dependencies.env`. Reproduce compilation with
`bash aros/cli/probe-zdb.sh` after sourcing it and setting `AROS_ARTIFACT_ROOT`
to an external artifact directory. Fresh probe `zdb-probe.3isWnXd4` compiles
`cmd/zdb/zdb.c`, `cmd/zdb/zdb_il.c`, and `lib/libzdb/libzdb.c` successfully.
Per-source logs, commands, dependencies and hashes are retained there.

Initial probe `zdb-probe.vG9eubte` rejected the unavailable `open64` interface.
The AROS-only mapping uses the existing native `open` implementation, with
`sizeof(off_t) == 8` enforced at compile time. It does not stub file I/O.

The reproducible `aros/cli/probe-zdb-link.sh` dependency audit uses the renamed
engine object from `shared-engine.5HkSG7Ab`. Run `zdb-link-probe.6SrlUNyq`
fails (exit 1) on exactly these unresolved symbols:

- `dump_nvlist`: upstream `lib/libnvpair/libnvpair.c`.
- `libzfs_core_init`, `libzfs_core_fini`, `lzc_get_props`: upstream core client,
  used by zdb's cachefile discovery; AROS core calls the authenticated IPC layer.
- `zfs_dev_is_whole_disk`: existing AROS native-device implementation uses
  authenticated geometry discovery, not a Linux device-name heuristic.

An earlier ad-hoc link failed before dependency analysis because final
`--gc-sections` had no ELF entry symbol. The corrected probe omits final
section collection, retaining AROS startup/autoinit sections. Its original
log remains in `build-audit/zdb-probe.d7YtgofI/link-audit.log`; the corrected
ad-hoc result is alongside it as `link-audit-no-gc.log`.

The audit is deliberately not a production statically embedded engine build.
Before delivering native zdb, implement its shared-library entry/lifetime and
read-only authorization model, then verify offline disposable pools in QEMU
with unchanged member hashes and comparison to independent host zdb. Existing
`OpenZFSRunService` permits one owner and one engine initialization per library
lifetime; zdb must not initialize or tear down that live service's global state.
Compilation and five unresolved symbols do not prove these runtime properties.
