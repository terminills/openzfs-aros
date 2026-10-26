# Native DOS member names

The real upstream CLI previously routed `RAM:OZForceCreate/disk2` through
Linux device-shortname expansion. The isolated QEMU regression failed at
forced replacement with `no such device in pool` (run fbsFjNOj, exit 1).

Explicit DOS volume/assign names now resolve through the existing native
DOS identity helper before that fallback. Linux shortnames and whole-disk
handling are unchanged. The public-service test uses the native colon name
for CLI replacement while retaining slash-path attachment coverage.

Fresh common CLI compilation and all six CLI links passed. QEMU run
wEEy0UWN exited 0 using cli-link.Qob0nTip and the previously qualified
shared-engine.Nmm3fNwi library/launcher. The real forced replacement,
resilver, exact read-only payload checks, active and read-only donor refusal,
Manager operations, and shared-library lifecycle checks passed. Retained
serial logs and input hashes distinguish the failed and successful runs.

This is isolated QEMU file-vdev evidence, not physical-device qualification.
Successful generated media were hashed and pruned; failed-run evidence and
media remain on external storage. No DUT or user-hosted session was touched.
