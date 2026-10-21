# Native read-only mount integration (partial port)

The management service now connects operation 43/44 to the actual upstream
ZPL adapter and reusable DOS handler, not a UI-only acknowledgement.

* Import a supported disposable fixture with operation 7 as before.
* Mount (43): target `pool/files`, argument `OZAPI` (no colon), options
  `readonly=on`, flags `OZ_CONFIRMED` only. Disable the GUI's dry-run checkbox.
* Unmount (44): target dataset only, no argument/options, confirmed only.
* Dataset list (36) reports the actual `read-only mount OZAPI:` state.
* Pool get (31) and dataset get (40) accept one exact property in Argument.
  They read upstream spa properties or DSL properties + objset statistics,
  including upstream default lookup and enum-name conversion. Unknown values
  error explicitly; `all`, lists, recursion and pagination remain unsupported.
* Open files and locks cause unmount to return `OZ_BUSY`, not a false success.
* Pool export refuses an actively mounted dataset. No forced teardown.

The frontend implements read-only file reads/seeks, root/file/directory locks,
examination/enumeration, nested and relative opens, parent lookup, volume
information and current-volume lookup. Space is taken from upstream objset
accounting; it is not the sum of mirror leaf capacities. Legacy 32-bit
InfoData uses larger reporting blocks when needed instead of truncation.

## Endpoint lifetime limitation

Unmount detaches storage and removes DOS entries, but retains their memory
and port to answer previously resolved requests with DEVICE_NOT_MOUNTED.
Eight lifetime mounts are allowed per service instance, including retired
endpoints. Ctrl-C refuses service shutdown after any mount. The service
remains resident until OS shutdown; bounded self-tests may destroy endpoints
only after joining all clients. This is deliberate memory safety, not a
completed general retirement/unload protocol.

## Verification and preserved failure

Private hosted AROS display :94, `Startup-Mount-Test`, no user session or DUT:

* First run: bad handler port during a DOS read, followed by internalflush
  assertion in OpenZFSMountTest. Evidence `build-aros/mount-failure-1.rO4JZTKZ/`
  and `build-aros/mount-hosted-1.png`. Root cause remains unproven. Added
  unbuffered client progress and targeted service mount-stage logging; these
  are diagnostics, NOT a proven fix. Initial build/link invocations also
  overlapped the shared hosted aggregate object; subsequent builds were
  serialized. No claim that this explains the runtime fault.
* Boots 2 and 3: all import/mount/read/lock/enumeration/busy-unmount/late-packet/
  remount checks PASS. Archived logs in `mount-pass-2.bcFG24nR` and
  `mount-pass-3.LH4PMnxq` under build-aros.
* Boot 4 adds DOS Info capacity/read-only checks; all pass. Live logs in
  `build-aros/hosted/mount-test.log` and `mount-service.log`; archived below
  before any future replacement. UI runs `OpenZFSManagerNative TEST-NATIVE`
  connected to the same real service, with OZAPI: left mounted for inspection.
* Native two-hardfile QEMU mirror and separate cold-boot checks also PASS
  after Info support: `build-aros/qemu-run.9gu4cPMM/`, summary
  `build-aros/qemu/info-reproduce.log`. Zero cold-boot guest writes, unchanged
  backing hashes, independent Linux zdb no leaks. This QEMU lane exercises
  the frontend directly; the new management IPC mount lane is hosted-tested.
* Nested directory extension: `build-aros/qemu-run.BBnAkHsp/` passes fresh and
  cold boots, including subdir/nested.txt, ParentDir, relative nested open,
  AROS parent-slash path subdir//hello.txt, and rejection of traversal through
  a regular file. Linux zdb checks all 81 allocated blocks without leaks.
* Hosted boot 5 creates a new pool then runs the API suite in the same boot;
  all checks pass, but client stdout appears in the SERVICE log while its
  redirected client log is empty. Preserved in
  `build-aros/mount-pass-5.5Im0tp3N/`. This is a concrete output-ownership
  anomaly, not proof of the earlier alert's cause. No shared AROS runtime
  changes were made during this test.
* Hosted property replay (`engine-boot-properties-2.log`) passes pool readonly,
  dataset used, default compression/source, invalid property rejection and the
  full mount lifecycle. The preceding test incorrectly expected compression
  off; pinned upstream zio.h defines DEFAULT as ON. Corrected the assertion,
  not the engine result. Returned values are now included in the transcript.
  Actual GUI request was also exercised on :94: Datasets/get, target
  aros_mirror_7f75c620/files, argument used, returned computed 180224.
  Screenshot `build-aros/properties-ui-used.png`; matching service request 2,
  operation 40, status 0. This is the same API as the programmatic test.

Build using `build-management.sh` and `link-engine-test.sh .../pool-test.c`.
Use only an isolated hosted runtime; Startup-Mount-Test creates a fresh
fixture if its import-fixture.log is absent, otherwise reuses that fixture.
The full port remains incomplete: hardlink/symlink semantics, writable ZPL/ZIL/ACL semantics,
production device discovery, endpoint retirement and many management verbs
are not implemented. Do not present this as full OpenZFS parity.
