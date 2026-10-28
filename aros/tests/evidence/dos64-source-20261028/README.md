# Source-built DOS64 candidate

`dos64-source.jn47era4` compiles all 25 selected translation units, including
generated library entry/exit code and libentry, then links the module. No cached
DOS64 objects are used. The snapshot retains 74 files and records 26 compiler
commands plus 214 external compiler/SDK/link dependencies. Those external
dependencies are hash-bound, not bundled whole-platform source closure.

Candidate SHA256:
`6ee73efba58908a6e30b59d2449c1a8939b34a4cd1a440a14cf174ae73dbc6e2`.
Relocated replay `fat-replay.jc07gri7` rebuilds byte-identically from retained
sources. The historical helper name also supports non-FAT library artifacts.

Staging requires `--dos64-source-snapshot`, checks the retained manifest, ABI
header and selected-binary recipe, and copies/rechecks Sources/DOS64. Twenty
staging tests pass, including missing support source, external ABI path, wrong
artifact, changed binary and unrecorded source drift. Actual snapshot validation
also passes. Four recording and six replay-path host tests pass separately.

Actual isolated hosted runs with this new binary:

- `hosted-offset.tohG7kh2`: raw and wrapper past-EOF error remain 219; all >4 GiB
  DOS64/POSIX patterns, sampled holes, unchanged cursor and close/reopen pass.
- `hosted-security-test.nAVzVa1d`, inputs eLOljm6z: full service/CLI/handler
  writer, report reverse/export, both runtime checks, independent host zdb,
  cold/degraded DOS reads and unchanged read-only member hashes pass.
- `hosted-zdb.TmpMVCnh`: exact labels, shared-library lifecycle, actual
  absent-security denial, L2ARC byte orders/corruption/bounds/recovery pass.
  Input hashes remain unchanged.

Other runtime inputs: stdc px9y2jvo, stdcio uo0iplm_, POSIX j099bkwp and DOS
4CUMjP0r. These are private hosted tests, not native QEMU or physical-device
qualification. No public bundle was promoted. No DUT, user hosted process or
foreign VM was touched. Sources and binary snapshots remain on the external
artifact drive; logs/provenance are retained here, not runtime or disk images.
