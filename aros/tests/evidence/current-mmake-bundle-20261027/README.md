# Current matched development bundle

The exact-binary staging gate produced `mmake-bundle.utejexbb` from separately
generated MetaMake targets, assembled by the retained `stage-current-qualified.sh`.
It is a development bundle, not a public stock-nightly qualification or proof
of complete OpenZFS support.

The selected inputs are bound to these retained native runs:

- Manager and desktop interaction: `native-gui-run.DKW8o83D`.
- Writer, persistent settings, cold restore and writable release:
  `public-service-run.6iuhXkGy`.
- Encrypted-root and native discovery: `public-service-run.CS0D8oKi`.
- Shared upstream debugger, native pool/partition and permission tests:
  `zdb-label-run.6Y7sxTkv`.

The stager reevaluated these logs, exact module hashes and independent pool
integrity reports. No test result was invented or changed to make staging pass.
The archive itself has not been booted as a new complete distribution; the
acceptance here binds its individual selected bytes to those prior runs.

## POSIX source reproducibility repair

The first staging attempt was correctly rejected: the qualified POSIX
snapshot forwarded its ABI include to an absolute private build header.
The retained local ABI header had the same hash. `localize-posix-snapshot.py`
made a separate verified snapshot, changed only that forwarding definition
and recipe dependency, and retained the original expected runtime hash.
The original snapshot and native binary were not changed.

Full replay of `posix-localized.e7euqhlb` produced the byte-identical tested
`posixc.library` in `fat-replay.xk70fv5y`. Thus no substitute runtime was
introduced. The first localization attempt's Python path-type error is also
retained; it did not modify the original snapshot or produce a sealed result.

The resulting bundle's SHA256SUMS verification passed. Replaying its shipped
Sources/AFS produced byte-identical `afs-handler` in `fat-replay.pyvck03e`.
SDK headers/link archives remain explicit external prerequisites. The platform
overlays are separate integration inputs, not automatic installation changes.
Hardware power-loss behavior and stock SDK/runtime closure remain unproven.
