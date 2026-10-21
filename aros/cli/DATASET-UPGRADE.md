# Native dataset upgrade

Manager operation 47 now executes the installed upstream `C:zfs upgrade`
through the inherited-identity command worker.  It upgrades one filesystem to
the current ZPL version, or to explicit `version=1..5`; Recursive passes
upstream `-r`.  The request requires confirmation and rejects force, dry-run,
arguments, malformed versions, and shell-like option expansion before child
execution.

This is an irreversible filesystem-format change, distinct from operation 25's
pool format and feature upgrade.  Upstream performs dataset lookup, prevents
downgrade, checks the pool's compatible SPA version, and enforces the caller's
property/configuration authority.  Manager warns about compatibility before
submission and captures the exact child result without an automatic retry.

The native fixture creates a version-4 filesystem in a disposable version-26
file-backed pool, writes data through its DOS mount, and proves a non-root
request leaves version 4 unchanged.  It then rejects malformed, unconfirmed,
argument-bearing, and forced requests, upgrades to version 5 as root, verifies
the property, and independently reads the exact payload after the existing
pool upgrade/export/reimport sequence.  The fixture remains isolated from the
DUT and uses a private QEMU overlay.

## Qualification (2026-10-05)

Manager cross-build `dataset-upgrade.9nIuYB1X` completed without diagnostics;
`OpenZFSManagerNative` is 1,808,088 bytes with SHA-256
`710fa66cda5a35ab6b438c28d06c63047953048a66bcc085cd57abb93660a33a`.

Isolated KVM run `native-import-run.R8ggtlJd` completed through the harness
port with `OZ-NATIVE-COMPLETION-v1 PASS line=0`.  Its mandatory dataset
upgrade marker reports `legacy4=1 denied-unchanged=1 version5=1 exact-data=1
invalid=3 unconfirmed=1 cleanup=1`.  The existing pool-upgrade, labelclear,
channel-program, redaction, and event regressions also passed afterward.  Both
immutable source fixture hashes verified unchanged.  The generated test ISO
SHA-256 is
`a1c5f0b70dc4570987af77d99aaffd1b254c2d7407980b7160e4a193c73150d5`.

The earlier retained directory `native-import-run.8bZucWJO` is not a runtime
result: the harness stopped before ISO packing and boot because the DOS overlay
package path was supplied where its containing directory was required.  The
successful run changed only that host-side path shape.  Neither run touched a
physical DUT.

The complete normal package `native-bundle.LjvljAOk`, staged from committed
HEAD `2796e3b6`, was then exercised through the focused runner mode
`AROS_TEST_ZVOL_TRANSPORT=1 AROS_TEST_DATASET_ADMIN_ONLY=1`.  Isolated KVM run
`native-import-run.j0O1lies` completed with the same version-4 to version-5
dataset marker, version-26 to version-28 pool upgrade and feature activation,
export/reimport, exact payload readback, and rejection-recovery marker.  Both
native fixture backing hashes remained unchanged.  This mode deliberately
omits the unrelated zvol, replication, redaction, channel-program, and event
matrix; it proves the packaged upgrade/rewrite path, not those other features.
