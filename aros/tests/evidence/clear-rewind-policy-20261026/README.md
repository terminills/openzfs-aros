# Native CLEAR rewind policy integration

Production engine: full actual make-fragment output `mmake-engine.qpj8vRuH`,
packaged as shared engine `IiSPGMPM`. Upstream CLI: fresh MetaMake output
`mmake-cli.EksGNipO`. The private compiler/SDK, pthread, crypto and DOS/posixc
runtime inputs remain explicit prerequisites, not an official SDK claim.

Eight-CPU isolated QEMU runs:

- `tzZPy00O` (file): real CLI `clear -nF`, `-F`, `-nFX`, `-FX` accepted on
  a healthy unmounted pool. Mounted dry-run and committed requests return
  nonzero with a busy diagnostic, not a misleading nvlist/out-of-memory error.
- `eSExu9sQ` (native partition/discovery): repeats those checks and adds
  readonly preview/commit EROFS checks before native mount attachment.
- Both exercise the real public RPC interface: flat and wrapped policies
  return decodable matching pool configs; mismatched flags, unknown bits,
  absent input/output, ambiguous wrapper, wrong field type and unknown keys
  return EINVAL. A changed user cannot reuse a root ticket; its own session
  is denied by configuration privilege checks.
- Both finish the full normal handler DOS-write/read and readonly-reimport
  suite with `RESULT public service storage PASS` and runner exit zero.

The first new client build failed a const-pointer type check in a test helper;
the corrected build passed. Its build logs are retained externally in
`build-audit/rewind-client-wire-build*.log`, alongside full engine/CLI logs.
The second client includes the added readonly test; binary hashes distinguish
the runs. Both generated ISOs were automatically hashed and pruned after exit.

These are integration and safety tests, **not damaged-pool rewind proof**.
Upstream returns the current config for an already loaded healthy SPA, so
acceptance does not demonstrate an older transaction group being selected.
The Manager still uses ordinary clear; no recovery wizard is claimed here.
No DUT, user hosted session, physical storage or Linux kernel pool was used.
