# Checksum-qualified native development bundle

Local archive: `mmake-bundle.0uirh9w0.tar.xz` (about 12 MiB).
SHA-256: `ef354dec20a25eca543ae9ec82a5bb7c11872ccd07c9d0c1dabb2c8138ee7a73`.
It supersedes `mmake-bundle.exm_ebrp.tar.xz` and includes the destroyed-member
checksum admission repair. It is a development artifact, not a production
filesystem release or a stock-nightly compatibility claim.

Exact shared engine: `shared-engine.f08bQXlx`.

- Manager/Wanderer: `native-gui-run.GuFzX3Jj` shows mounted files, a real
  connected service and the live `public_mirror ONLINE` response. Closing the
  drawer and Manager leaves Wanderer running; independent post-UI exact
  readback and full storage acceptance pass. Screenshots are retained here.
- Persistent mirror: `public-service-run.K1DY0lTh` passes trusted startup
  restoration and healthy/degraded/restored fresh imports, exact reads,
  unchanged members/archive, and independent host zdb with no leaks.
- Native encrypted root and reuse: `public-service-run.A1PDQd0F`, retained
  under `../destroyed-label-checksum/native`, binds the same engine to the
  encrypted-root, native reuse and file checksum-refusal tests.
- Fresh archive extraction: all SHA256SUMS verify. Its extracted production
  binaries pass `public-service-run.D3ewA5NW`, including the twelve label
  refusal cases and the full service/storage suite. The acceptance client is
  an external test tool, not a packaged application.

All native tests use eight-CPU isolated QEMU and explicitly selected runtime
overlays. The bundle keeps platform overrides separate from application files.
The packaging gate now requires checksum-only refusal evidence in addition to
the encrypted-root, desktop and cold-start gates; seven host gate tests pass.
Those host tests are not substituted for the native runs above.

Physical durability, power loss, opposite-endian labels, external concurrent
writers and the remaining feature/topology matrix are not qualified here.
