# Upstream CLI pool initialization

The native transport now admits `ZFS_IOC_POOL_INITIALIZE`, which previously
worked only through the separate Manager API. It accepts a bounded packed
nvlist naming existing member GUIDs; the upstream ioctl validates command
and schema and enforces configuration privilege, readonly/suspended-pool
checks and per-device results. No client pointers, alternative inline flags,
history buffers or new backing paths are admitted by this addition.

## Native QEMU verification, 2026-10-03

The unchanged bundle `native-bundle.6Z2tUHi1` failed new regression
`native-import-run.pov94gY9` at `zpool initialize -w` with an unsupported
operation. Corrected package `native-bundle.mbGPjkip` contains library
`shared-engine.evVe1ewa`, service/Manager from
`cli-initialize-release-20261003`, and the existing all-fresh CLI, handler
and ZVOL inputs.

First corrected run `DEN5On0q` completed initialization, reported 100 percent
initialized, reset progress with `initialize -u`, and preserved the exact
1048576-byte file payload after both. It then failed a test expectation:
upstream initialization returns -1 on failure, not 1. The actual readonly
operation was refused with `pool is read-only`; the library was not changed
to accommodate the test.

Run `kkBlrz9m` with corrected return-code expectations and the same package
exited 0. Completion/reset/data checks and readonly/nonroot refusals passed,
as did the full selected native CLI/handler regression and both original
backing-image SHA256 checks. External archive logs:
`cli-initialize-{before,build,library,library-inputs,stage,after,after-2}-20261003.log`.

This proves CLI start-and-wait, completed-state reporting, uninitialize and
the named admission refusals on a disposable RAM-file vdev. It does not
independently qualify CLI suspend/cancel races, every topology, interrupted
initialization, physical media durability or stock-nightly compatibility.
Initialization writes unused space; it is not formatting or secure erasure.
