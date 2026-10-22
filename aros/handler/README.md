# Native OpenZFS handler startup

`OpenZFS-Handler` is a native handler module, not a second embedded engine.
The resident OpenZFS service must already be running and the selected pool
imported. `Mount` creates the device entry; activation authenticates an
administrator attach through the native admission port, adopts that entry,
and serves its files using the real upstream ZPL DOS endpoint.

Example mount file named `OZBOOT`:

```
FileSystem = L:OpenZFS-Handler
Device = openzfs
Unit = 0
Flags = 0
Surfaces = 1
BlocksPerTrack = 1
LowCyl = 0
HighCyl = 0
DosType = 0x5a465301
Control = "pool/files"
StackSize = 262144
Activate = 0
```

`Control` is the already-imported dataset. Flags 0 requests readonly; bit 0
requests writable access (requires a writable import and normal permissions).
Geometry is not used to address media: the resident pool owns its admitted
vdevs. No device probing, implicit imports or fallback private ZFS format is
performed. This entrypoint is administrator-only; later filesystem packets
retain the original caller's identity and normal upstream/native ACL checks.

Run `Mount T:OZBOOT`, then access `OZBOOT:`. GetDeviceProc prefers the engine
port published in the device node. The bootstrap also forwards packets sent
to the process port returned by RunHandler without changing their reply ports.
Both processes remain resident after detach, because old references can still
address their ports. Late packets receive the existing unmounted error. This
does not solve endpoint reclamation, automatically install startup records,
or remove the documented runtime prerequisites.

Build with `bash aros/handler/build.sh`. Generated handler startup warnings
are retained. The bootstrap object is checked for unexpected external symbols:
it must not depend on an uninitialized executable C runtime.

## Acceptance and retained failures

### Large-file directory metadata

The engine implements `ACTION_EXAMINE_OBJECT64`, `ACTION_EXAMINE_NEXT64`,
and `ACTION_EXAMINE_FH64`, as used by AROS `dos64.library`. These return exact
64-bit sizes. Legacy `Examine`/`ExNext`/`ExamineFH` saturate sizes and block
counts at the signed 32-bit maximum when their destination fields are 32-bit;
legacy `ExAll` saturates its unsigned size field at the unsigned maximum.
This prevents one large file from aborting a directory listing. Applications
requiring exact sizes must use the wide interface, not interpret the saturated
legacy value as the actual length. Legacy seek/resize overflow refusal remains.

Native alpha18 QEMU `public-service-run.AGSELC7d` passes the public
`Examine64`, `ExNext64`, and `ExamineFH64` calls with a 4294971392-byte sparse
file, including normal ASCIIZ filename conversion by dos64.library. Raw packet
tests check null output, missing lock/handle, and invalid enumeration key errors.
`public-service-run.kBAVlBMz` repeats these checks after cold healthy, degraded,
and restored mirror imports; member hashes remain unchanged and host `zdb -bcc`
passes. The writer boot also checks legacy and raw-wide enumeration.
These are file-backed virtual-storage tests, not physical-media qualification.

Desktop repeat `native-gui-run.wx4RRXdn` passes the same live Manager,
directory API, post-UI exact-read, and full storage gates. Opening the mounted
volume by its desktop icon shows `OZPUBLIC:` and both files (screenshots
`02.png`, `03.png`), including a second close/reopen. The automatic WBRun
opening has a garbled title (`01.png`) despite correct file contents; that
launch-path observation is tracked separately from filesystem enumeration.
No Wanderer or Zune source was changed for these tests.

- `native-import-run.SJPTtSqo`: first actual activation faults in the strlen
  wrapper because FAST_BSTR's length macro depended on an uninitialized
  StdCBase. The bootstrap now copies BSTRs with a bounded freestanding loop.
- `native-import-run.5Dap8qWd`: activation succeeds, but the test incorrectly
  assumed GetDeviceProc used RunHandler's returned process port. Source audit
  confirmed the published custom port takes precedence. This failed test is
  retained; the corrected test separately probes both ports.
- `native-import-run.zdVGM7s5`: real Mount activation, root/non-root admission,
  invalid version refusal, exact reads through both ports, readonly write
  refusal and late requests after detach pass. Runner exits zero and original
  backing hashes match (`handler-mount-native-3.log`). This uses the normal
  monolithic service and private QEMU runtime overlays. Writable standalone
  startup and shared-library packaging remain to be qualified.
- `native-import-run.GHTNNF9u`: adds actual writable Mount activation, DOS
  creation and flush, followed by export/reimport and exact persisted readback.
  The readonly, denial and detached-port checks still pass. Runner exits zero
  with unchanged original backing images (`handler-mount-native-4.log`).

`native-bundle.xDmJ6iC9` includes the standalone handler under `L:` and the
real shared engine under `Libs:`. Fifteen negative staging cases pass in
`stage-policy.mJkhnyur`, including the four new handler provenance refusals.
Exact-package `native-import-run.rKALYhoQ` passes the full selected CLI/device/
mirror suite, eight library lifecycle cycles, both standalone Mount modes and
actual failed non-root activation followed by successful root retry. Host exit
zero and backing hashes match (`handler-bundle-native-1.log`).

`startup-cold.LUJzLcWM` passes two independent QEMU processes using the same
bundle: protected IFF settings and a saved mount file persist on a private SFS
archive. The second boot runs PREPARE before service startup and the stock ENV
copy, imports without automount, then activates the persisted Mount file and
reads exact bytes. Readonly mutation is refused; archive/member hashes remain
identical across the reader boot, as do original backing hashes. Runner exits
zero (`handler-startup-cold-1.log`). This uses explicit fixture sequencing and
private runtime overlays, not an unattended stock-nightly installer.
The immutable archive's documentation predates these post-package notes.

`native-import-run.QC4ZSHYL` additionally exercises four actual failed handler
startups through `Mount` and `GetDeviceProc`: unknown flags, wrong DosType,
empty Control and a nonexistent dataset. Each registered DOS node remains
inactive. The same boot subsequently passes valid readonly activation,
non-root refusal/root retry, bootstrap forwarding, and writable Mount with
flush/export/reimport exact readback. The runner exits zero and original raw
backing hashes match (`handler-invalid-startup-native-1.log`). This is focused
static-service QEMU coverage with private runtime prerequisites, not an
exact-package rerun or physical qualification. Inactive failed nodes remain
registered during this bounded test; it does not measure reclamation or leaks.

The exact shared-engine bundle also passes the extended negative-startup tests
in `native-import-run.I4UhBvak`, alongside the selected full CLI/device/mirror
suite and eight library lifecycle iterations. Runner exit zero and unchanged
original backing hashes are recorded in `handler-invalid-bundle-native-1.log`.
This strengthens package coverage; private-runtime and physical limitations
remain unchanged.

## Public MetaMake package verification

The handler produced by the clean `contrib-openzfs` aggregate was exercised
alongside that build's shared engine, launcher, and upstream CLI commands in
isolated hosted AROS (`hosted-security-test.hNwsU0bS`). Actual `C:Mount`
activation, exact DOS file reads, read-only write refusal, and detach passed;
the same run also passed CLI snapshot operations and cold/degraded mirror
verification. This is hosted package evidence, not a new bare-metal result.

To repeat this coverage, set `AROS_TEST_HANDLER` to the staged handler when
running `aros/engine/test-hosted-security.sh` with its normal full-service
configuration. The runner copies and hashes that binary, creates a mount file
for its disposable `public_mirror/files` dataset, and requires the packaged
handler acceptance marker. No existing host or physical pool is used.
