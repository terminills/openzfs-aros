# Import admission error reporting

An import can fail before the engine produces a packed load-info reply. The
upstream client previously attempted to unpack the untouched destination buffer
anyway; its unpack helper calls `no_memory()` on decoding failure. On native AROS
this masked the actual admission rejection with `internal error: out of memory`.

`zpool_import_props()` now decodes an error reply only when the ioctl marks the
destination filled. Successful imports retain the previous decode path. A bare
ENOTSUP without load information reports `import request not supported`, rather
than claiming an unsupported pool version. Feature/version load-info diagnostics
remain unchanged when the engine actually supplies them.

Native import accepts `-o cachefile=none`, an absolute cache path, or a protected
native ENVARC default. Missing/empty policy fails when that secured default is
unavailable; it never selects a Unix fallback. Explicit cache serialization and cross-boot import are now qualified
with private runtime overlays; see `WAIT.md`. The diagnostic fix below
predates that integration and never silently discarded the user's setting.

## Regression

The native upgrade fixture exports its upgraded pool, attempts an import without
the required property, requires status 1 and the unsupported-request diagnostic,
rejects any false out-of-memory message, and checks the pool remains absent.
It then imports with `cachefile=none`, mounts the dataset, compares exact payload
bytes, and cleans up. A mandatory host-side marker prevents a skipped fixture
from being reported as a pass.

Prior reproduction: `build-aros/native-import-run.H37GSX3P` showed the false
out-of-memory message after successful upgrade/export.

Corrected run `build-aros/native-import-run.FGUYXEbo` passes the diagnostic,
absent-after-rejection, valid reimport and exact-data checks. The full native
suite passes with host exit 0 and unchanged original backing-image hashes.
Evidence is registered under the development test archive. Build logs are
`import-errors-compile-1.log`, `import-errors-link-1.log`, and
`import-errors-native-1.log` in `build-aros`; the fresh common objects are in
`cli-probe.ggjKTJQx` and commands in `cli-link.zhOWGskB`.

This proves the specific early-admission error and recovery, not every malformed
reply or unsupported-feature combination, physical durability, or stock-nightly
integration. The QEMU suite uses private OS overlays.
