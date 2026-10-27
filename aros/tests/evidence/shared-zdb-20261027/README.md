# Shared native zdb and service regression

Version 2 of `openzfs.library` contains the upstream debugger and existing
native core-client support alongside the engine. The `zdb` launcher contains
no storage engine. Vector 5 remains the service entry; vector 6 runs the
debugger on an allocated 4-MiB stack. Both entries share the existing owner
and one-shot claim. This is offline debugging, not concurrent access to the
live service's global engine state.

## Builds

- Standalone `shared-engine.nlRQipaF`: initial shared label run.
- Standalone `shared-engine.FJvg6G18`: shared label/lifetime run and service
  regression; generated config and binary hashes remain in the build directory.
- Actual generated MetaMake `mmake-debugger.ZqUKKwpH`: library, service and
  `C:zdb` targets build successfully from the current rules, using the verified
  normal engine `mmake-engine.hZ1s42So` and explicit private dependencies.
  The debugger launcher is approximately 21 KiB, not an embedded engine.

Initial isolated MetaMake setup `NdAOHQOS` lacked libentry.o and generated
header directories. `hpwnUz56` tried to copy a nonexistent libend.o. Those
setup attempts failed, and were corrected without changing production code.
Build `cNyRRWFC` then exposed the real rule defect: native ZPL headers followed
Linux headers in the search order. The corrected rules put ZPL first for the
three upstream debugger translation units. All failure logs remain retained.

## Native checks

- `zdb-label-run.noZ9u1w6`: shared launcher, two label decodes.
- `zdb-label-run.t6797XQU`: standalone-build library, plus lifecycle checks.
- `zdb-label-run.M0NfoPdY`: actual MetaMake library/launcher, same full test.

All use eight QEMU CPUs, the recorded alpha18c5/private DOS/POSIX runtime,
no writable guest disks, no network and no filesystem share. The fixture is
an existing disposable member mapped into the read-only ISO. Two launcher
runs each exit zero and reproduce all 33 host-zdb output lines exactly. All
recorded input hashes remain unchanged. Each shared launcher has only a
64-KiB caller stack, exercising the library's internal stack switch.

The additional direct-ABI test rejects five invalid argument shapes, decodes
the member, then verifies repeated debugger and service entries return 10.
It unloads and repeats with fresh globals. The caller finally exits with 7,
which the parent verifies, detecting stale exit targets into an unloaded
library or its freed stack.

`public-service-run.0kPYLVaS` passes the standalone-build library's existing
service/CLI/handler regression. `public-service-run.uL30YO9M` uses the actual
MetaMake library and current lifetime-test source. Eight owner/reload/denied
service cycles pass; after denied service startup the debugger entry is also
refused. Three boots pass writer operations, cold healthy/degraded/restored
exact DOS reads, and writable cold release with persisted deletion. FAT checks
are clean, read-only member hashes remain unchanged, and all independent host
block and object/MOS reports pass. The runner exits zero and prunes its ISOs.

## Remaining scope

Native zdb full block/MOS traversal, native raw-device authorization and public
release-bundle qualification remain open. Independent **host** zdb still
provides the full integrity verdict in the service regression; label decoding
must not be presented as an equivalent native integrity check. The accepted
official SDK/runtime dependency pair remains a separate unresolved gate.
No DUT or user-hosted session was touched.
