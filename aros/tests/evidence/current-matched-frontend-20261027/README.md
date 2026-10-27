# Current frontend qualification, 2026-10-27

Private isolated QEMU evidence; not physical-DUT or public-SDK qualification.
Production source baseline: 8d65236. The current MetaMake frontend is
`build-audit/mmake-cli.uLkDlrcx`, with the debugger-qualified shared engine
`build-audit/debugger-service.0umlrjD3`. Build recipes and hashes are retained
under `build/`. Private DOS/POSIX runtime overlays remain required.

## Passed

- `native-gui-run.iDeROIvp`: current Manager, real pool health/topology,
  saved full response, mounted Wanderer drawer, and exact file read after
  closing the UI. Screenshots and serial evidence are under `gui/`.
- `public-service-run.rVNd5kP5`: native encrypted-root locked import,
  wrong-key denial, correct-key recovery, exact data and cleanup. Logs are
  under `encrypted/`.

## Failed attempts retained

- `public-service-run.YZIrtGBp`: enabled startup RESTORE returned ENOENT.
  Cause unresolved; see `startup-missing-record/`.
- `public-service-run.SpHNV8UK`: metadata-only inventory proves the enabled
  ENVARC record survived reboot; RESTORE imported and mounted read-only.
  The subsequent rewind assertion failed; see `startup-inventory/`.
- `public-service-run.MihXjqbZ`: repeated RESTORE passed. Bounded diagnostics
  identify rewind return EBUSY (16), not expected EROFS (30). The restored
  filesystem is still mounted. `oz_cli_pool_rewind_check` deliberately
  checks mounted-subtree exclusion before read-only state. See
  `readonly-order/`.

The test now executes its strict read-only rewind assertions after cold
handler data verification and detachment. Production safety policy is
unchanged.

## Corrected full rerun

`public-service-run.f2muYonW`, using rebuilt acceptance client
`dependency-client.VYAAdBpI`, completed with runner exit zero. `cold-passed/`
retains the writer, cold and writable-release logs, input hashes, independent
zdb results and automatic image-pruning record. Healthy/degraded imports,
exact DOS reads, strict EROFS rejection after unmount and persisted cold
release/deletion passed. The original ENOENT attempt remains unresolved;
this successful rerun is not evidence that its underlying cause is fixed.
