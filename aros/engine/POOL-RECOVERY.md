# Explicit destroyed-pool recovery

The authenticated native pool.import operation accepts mandatory
`readonly=on` or `readonly=off`, plus optional `destroyed=on/off` separated
by a semicolon. Fields may appear in either order; duplicate, unknown,
empty and malformed fields are rejected. No force, rewind or any-host
flags are implied. Root authorization and explicit backing-file policy
remain unchanged. The upstream CLI's `-D` state selection is preserved:
recovery selects destroyed pools only, normal import excludes them.

The import editor has an unchecked-by-default Recover destroyed pool
checkbox. Use draft produces `readonly=on;destroyed=on` and a preview,
not a storage operation. Writable recovery requires an explicit option
change and normal confirmation. Destroy is not a secure erase.

## Native verification, 2026-09-22

`build-aros/file-backing-run.eu0MK1wr` (mirror) and
`build-aros/file-backing-run.JYnuebBT` (RAIDZ3) passed five independent
AROS QEMU boots against new disposable member files:

1. Create through authenticated API; create dataset and write DOS payload.
2. Cold writable import, read payload, unmount and destroy.
3. Both normal imports refuse; explicit read-only recovery reads the
   original file and enumerates its directory. Writes refuse. Export does
   not resurrect the pool; both normal imports still refuse.
4. Cold explicit writable recovery reads the original file, writes and
   rereads a new file, then exports.
5. Cold normal read-only import reads both files and refuses writes.

Host extraction proves every member unchanged during both read-only
boots, and changed during destruction and writable recovery. Independent
upstream zdb verifies all four label states on every member (destroyed
then exported), checks the recovered pool and reports no leaks. Native
FAT retry and fsync error-path tests also pass in every boot.

`build-aros/native-gui-run.0yfA6Q2b/04-import-editor.png` shows the native
unchecked checkbox. `05-recovery-preview.png` shows the checked choice
producing the exact readonly/destroyed options and explicit file list.
This is an editor-only scenario: it correctly reports unavailable service
and does not claim a GUI-triggered recovery occurred.

## Retained failure and remaining work

`build-aros/admission-service-run.PEyyrAFl` expanded the existing five-layout
RAM suite with recovery and strict parser tests. Stripe, mirror and RAIDZ1
passed; RAIDZ2 stopped at dataset mount with ENOSPC. management.c retains
an eight-entry lifetime endpoint table for stale DeviceProc safety. The
extra recovery mounts exhaust it. This is NOT a passing combined suite,
and must not be hidden by dropping coverage or reusing a stale endpoint.
Fix the endpoint lifetime/resource model and rerun the complete suite,
including non-root recovery refusal (not reached in this failed run).

Follow-up: the dynamic registry/shared-signal change described in
MOUNT-ENDPOINT-LIFETIME.md passes the entire recovery suite in
`admission-service-run.mijXOIwh`, including those non-root refusals.
The original failed evidence is retained.

Recovery of overwritten/missing labels, rewind, concurrent external
writers and physical power loss are not established by these fixtures.
The files are on one private FAT-backed virtual disk, not independent
physical devices. No DUT or shared hosted session was used.
