# Manager engine policy properties

Dataset set (41) and inherit (42) now support `copies`, `primarycache`,
`secondarycache`, `logbias`, `redundant_metadata`, `checksum`, `dedup`, and
`sync`, plus filesystem `dnodesize`, through the full native
upstream dispatcher. Set uses `property=value`; inherit uses the property
name. Existing confirmation, no-options and caller-authentication rules apply.
Filesystem and zvol targets are checked against upstream property type rules;
snapshots are refused. These do not configure host boot, automatic device
discovery or OS-specific sharing services.

The adapter parses values using upstream index tables, checks the caller's
property delegation, and calls ZFS_IOC_SET_PROP or ZFS_IOC_INHERIT_PROP under
that caller's credentials with scoped nvlist input. It does not bypass the
dispatcher by assigning a generic DSL value. Upstream DMU property callbacks
consume the resulting settings. Explicit `copies=3` on an encrypted dataset
is rejected as in libzfs. These operations modify dataset properties, not
stream-output files.

`dnodesize` uses the upstream index table and ioctl large_dnode feature gate;
it is not accepted on volume targets. Enabling the pool feature is an explicit
separate operation, never a side effect of the Manager setter. The native
fixture tests disabled-feature refusal with unchanged `legacy` readback, then
enables the feature only on its disposable pool and exercises `auto` set,
inheritance, invalid-value rejection and exact DOS file writes/reads.
Retained run `native-import-run.jV7kRaz3` failed because the first harness
incorrectly expected auto to succeed on a feature-disabled fixture. Production
validation was preserved; the corrected fixture tests that refusal explicitly.

Corrected `native-import-run.dJgv0GXm` passes feature denial/enablement, nine
property setters/inheritance/invalid-value checks, native DOS I/O, and the full
selected mirror regression. Host exit zero and unchanged original backing
hashes are recorded in `dnodesize-native-2.log`. Service and rebuilt Manager
logs are `dnodesize-build-1.log` and `dnodesize-ui-1.log`. This does not inspect
each file's physical dnode allocation or qualify every indexed dnodesize value.

The extended fixture additionally sets and reads back every upstream indexed
value (`legacy`, `auto`, `1k`, `2k`, `4k`, `8k`, `16k`), and checks EINVAL for
both set and inherit on a snapshot and on a real sparse zvol. It removes those
test objects before the existing nine-property and DOS-I/O sequence. This is
property/type coverage; file I/O remains exercised with inherited `auto`, not
separately with each size, and physical dnode sizes are not inspected.
`native-import-run.A7Th0vgj` completes that extended matrix and the full
selected mirror regression with host exit zero and unchanged original backing
hashes (`dnodesize-native-3.log`).

Build: `source-engine.P2gN7Bsv/engine-policy-service`, log
`engine-policy-build-1.log`. Manager help is separately rebuilt in
`engine-policy-ui-1.log`.

Initial native run `aJuSkq4c` passes set and inheritance for all five
properties, CLI readback of values and sources, invalid values leaving the
old value unchanged, and unauthorized refusal. It also passes the selected
full transport/mirror regression with host exit zero and unchanged original
backing hashes (`engine-policy-native-1.log`). Follow-up `lOG1vjOD` also passes
delegated set/inherit, unrelated-property denial, refusal after revocation,
encrypted copies=3 rejection with unchanged readback, and copies=2 acceptance
followed by restoration to 1. Its full selected regression exits zero with
unchanged original backing hashes (`engine-policy-native-2.log`). Both runs
use the posix-pipe profile and are registered to the development test archive.

These are real Manager API operations plus upstream CLI readback, not GUI
mouse tests. They do not independently measure physical DVA copy counts,
cache effectiveness, performance, or cold persistence for every property.
The port as a whole remains incomplete.

## Integrity and synchronous-I/O policy extension

`integrity-policy-service` adds checksum, dedup and sync using the same
upstream parser/dispatcher path. Upstream checksum feature gates still apply.
`sync=disabled` deliberately weakens synchronous-write durability; confirmation
and property delegation are required, and Manager help warns about that trade-off.
Supporting the property is not a claim of physical power-loss qualification.

`native-import-run.8H4IJZrS` passes all eight setters and inheritance paths,
eight invalid values with unchanged readback, delegation/revocation and the
encrypted-copy checks. The child inherits sha256, dedup=verify and sync=always;
two native DOS files are written, flushed and read back exactly, totaling
256 KiB with EOF checks. The selected full mirror regression exits zero with
unchanged original backing hashes (`integrity-policy-native-1.log`). Build
and updated Manager help logs are `integrity-policy-build-1.log` and
`integrity-policy-ui-1.log`. This does not measure dedup space savings or
independently inspect the checksum/DVA of every resulting on-disk block.

The graphical run `native-import-run.vpu2SXlr` additionally submits
`checksum=fletcher4` on `qemu_mirror/engine-policy/child` through the actual
Manager request editor and confirmation dialog, with dry-run off. The engine
returns errno=0; after Manager exits, the independent `zfs get` calls verify
both value `fletcher4` and source `local` (the GUI PASS marker in serial.log).
Screenshots `policy-target-filled.png`, `policy-confirm.png`, and
`policy-submitted.png` preserve the entered request, confirmation, and result.
Earlier screenshots retain unsuccessful coordinate/focus attempts; those did
not submit a request and are not engine failures. The GUI marker alone does
not establish completion of the remainder of this run's regression suite.
The subsequent full selected regression completed with host exit zero and
both original disk-image hashes unchanged (`integrity-policy-gui-1.log`);
the second Manager window was closed normally after the backend cycles.
