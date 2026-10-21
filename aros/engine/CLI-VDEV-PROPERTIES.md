# Native per-vdev property transport

Upstream `zpool get <property> <pool> <vdev>` and
`zpool set <property>=<value> <pool> <vdev>` use VDEV_GET_PROPS and
VDEV_SET_PROPS with a member GUID and packed property nvlist. They do not use
the pool-property ioctl. The native transport now admits those bounded
nvlists, rejects unrelated inline fields and reconstructs only service-owned
buffer pointers. Upstream retains GUID lookup, property validation, read/config
authorization, suspended-pool and read-only-write checks.

Read reply resizing preserves upstream ENOMEM/required-size negotiation.
Mutations are not automatically retried after an output failure.

## Native path relocation (2026-10-03)

### File-backed identity prerequisites

Disposable native FAT runs `file-backing-run.kT3wtWMC` and
`file-backing-run.wG0zJeVT` both exit 0, including independent cold boot and
host extraction of the persisted payload. They establish a distinction needed
before extending the native-device setter to file vdevs:

- A writable `MODE_READWRITE` handle permits rename and continues reading
  the original data, but `DupLockFromFH` fails with DOS error 202.
- `NameFromFH` reports the moved pathname from that still-open handle.
- A read-only handle permits `DupLockFromFH`; `SameLock` matches a separately
  acquired lock on that file and distinguishes another file with identical
  contents created at the old name.

The inspected DOS API explicitly restricts lock duplication to shared locks.
The inspected FAT `FillFIB` path stores a sentinel `fib_DiskKey`, not an object
identity suitable for relocation. Neither a pathname nor matching payload or
ZFS labels substitutes for retaining the actual opened file. `NameFromFH`
alone also does not close concurrent rename/reopen races. No production
live file-backed relocation is enabled by these tests: a handle-bound admission
design and engine-level lifecycle regression remain required.

The runner now uses the common 100 GiB free-space reserve and accepts
`AROS_TEST_ARTIFACT_ROOT` on a separate filesystem; both runs were recorded
under the external `OpenZFS-archive/build-aros` directory. The probe executable
can be selected independently with `AROS_TEST_BACKING_BINARY`, without
activating the separate cold pool-discovery suite.

### Exported file pools: cold relocation verified

This is distinct from a live vdev-property setter. The upstream branch in
`module/zfs/vdev.c` restricts a new `path` property to `/dev/`; extending the
AROS setter to ordinary open backing files is not necessary to reproduce
that interface. The standard export/move/import workflow remains important.

`AROS_TEST_COLD_RELOCATE=1` now tests that workflow with authenticated native
pool creation and a freshly linked upstream engine harness. After export and
guest exit, the host renames every backing file in the disposable FAT image.
The next guest verifies that the old names are absent, imports only the new
explicit paths, checks that the members report those paths and ONLINE state,
and reads the exact DOS payload and directory contents. Every member is
compared byte-for-byte before/after this read-only import.

`file-backing-run.v8qBS6uw` (two-file mirror) and
`file-backing-run.3g46uuJF` (four-file RAIDZ2) both exit 0. Independent host
`zdb -e -bcc` inspection is retained as `relocated-zdb.log` in each run. This
uses a static test harness with the production dispatcher, not the shared
release package or a stock-nightly qualification. No live file rename,
device-loss race, or physical power-loss guarantee follows from this result.

### Native-device integration evidence

Current verified integration: `native-import-run.WeegfiKq`, production shared
engine `shared-engine.syFUbJqZ`, CLI `cli-link.3IKcJQu7`, host exit 0. It passes
unauthorized/read-only, online, blank and wrong-member refusals, forward/return
path moves with offline preservation, three reimports, post-move DOS writes
and exact reads, library lifetime and remaining selected API regressions.
Original backings and bytes outside the target partition are unchanged. The
shared-admission-lock fix below removes the reproduced cross-process writer
conflict; the separate CD LoadSeg error still reproduces and requires the
disclosed RAM-executable test isolation. Combined-bundle testing is next.
The subsequent exact `native-bundle.aUnVNLhd` run `native-import-run.Rirbr3AI`
also exits 0 with the strengthened relocation plus Manager vdev API/types/
long replies, CLI property persistence/lifecycle, zvol device and library
lifetime enabled. See `../distribution/README.md` for its exact inputs.

The current source connects `VDEV_PROP_PATH` to `oz_pool_vdev_setpath` after
upstream configuration authorization. A same-extent alias retains the admitted
handle through the binding/policy transaction. Moving to a different native
extent requires an offline, closed leaf, a writable unclaimed destination and
matching active pool/member GUIDs in every decodable label. The leaf remains
offline; a separate upstream online validates/reopens it. Missing retained
members may be relocated only if explicitly offline. No discovery or automatic
adoption is added. Live file-backed path-property changes remain unsupported;
the separate exported-file workflow above is verified.

Matching labels establish ZFS member identity, not freshness of all blocks in
an old clone. This operation moves a member path; it is not `zpool replace`
and does not turn an arbitrary stale copy into a current device. The member
remains offline so the administrator can verify the actual medium before
onlining it. Hardware identity/freshness qualification is still outstanding.

The service owner coordinates retained names, bindings and policy around the
upstream label-path mutation. Failure before that mutation rolls back pending
admission. As with other upstream mutations, a later property-sync or reply
failure is not proof that nothing changed: callers must read back, not blindly
retry. Path changes must be the only property in an ioctl batch, so another
property cannot commit before path admission fails.

`native-path-setter-20261003` builds the full service/Manager and refreshes
`module/zfs/vdev.c` instead of using its older engine object. Runtime validation
is pending. `AROS_TEST_NATIVE_REPATH=1` provisions a fresh 512 MiB disposable
QEMU disk with one 256 MiB partition. Its test-only raw-copy helper cannot be
linked into the production service/library. The regression checks online,
blank and wrong-member refusals, a valid offline move, online/resilver,
export/reimport, DOS data, and restoration of the original fixture. Host checks
require immutable original backing hashes and unchanged bytes outside the new
partition. This does not qualify physical hot-unplug or arbitrary file moves.

Initial runs `native-import-run.1lR0Odwa` and `djLM5fwT` (focused, omitting
unrelated create-pool tests) both failed loading `C:zpool` immediately after
copying the wrong-member fixture: ENOEXEC, DOS error 333, before its ioctl.
All three original backing hashes match in each run. `oRESmxkn` uses opt-in
`AROS_TEST_NATIVE_REPATH_RAM=1`: it copies zfs/zpool before disk activity,
compares every byte, and records each RAM executable path. Its paired CD
LoadSeg probe fails with 333 while the RAM binary launches. That RAM request
then hits a distinct unhandled ESTALE error in `zfs_verror` and aborts. All
three runs are registered failures, not relocation verification.

The native libzfs reporter now maps ESTALE to a normal bad-device error with
an explicit identity-mismatch explanation. The regression requires that
message and exit 1. CDVDFS contains explicit 333 fallback branches; the
specific branch/underlying I/O cause is not identified. RAM staging isolates
the engine test, not a CD/ATA
fix. Logs use `native-path-setter*20261003.log` on the external archive.

With rebuilt `cli-link.3IKcJQu7`, run `native-import-run.zAahgRYi` passes the
static service path move, exact wrong-member diagnostic, online, two reimports,
DOS reads and restoration (host exit 0). Original hashes and all bytes outside
the fresh target partition match. This older test did not write new user data
between moves and must not be read as stale-clone recovery qualification.
The next regression adds nonroot/read-only refusal and writes after the move;
it copies the current offline member back into the unbound original fixture
before restoring the path. Shared-library validation remains pending.

The strengthened shared run `native-import-run.rzRtX4Y2` failed during the
return copy: service data writers reported `dos-entry-contention`/EBUSY while
the independent test helper held an exclusive entry lock for raw I/O. The pool
suspended and the next setter correctly returned EAGAIN. This is an adapter
locking defect, not a successful shared-runtime qualification.

AROS `AddDosEntry` takes a shared list lock and exclusive entry lock;
`RemDosEntry` takes an exclusive list lock. A raw writer only inspects ownership,
so shared list AND entry locks exclude both namespace mutations without
excluding another admission reader. `oz_device_transfer` now acquires those
shared locks together. Actual publication functions retain exclusive locks,
and contention with a real mutator still fails nonblocking rather than waiting
behind a potential filesystem client. This does not establish cross-process
raw-device ownership: the existing operator exclusion requirement remains.

Deterministic negative `device-range-run.YnXYB5oq` failed at line 236 when an
independent task held shared list/entry locks. Candidate
`device-range-run.LqZiCrov` passes that case and the original exclusive-list /
exclusive-entry refusals, claim revocation, parallel writes, exact ranges,
cross-4-GiB I/O and cold GPT persistence (exit 0, all original/cold hashes match).
External logs: `entry-reader-before-qemu-20261003.log` and
`entry-reader-after-qemu-20261003.log`. Full shared lifecycle repeat is pending.

### Previous transport guard (superseded by the candidate above)

The upstream `path` setter accepts Linux `/dev/` names and calls
`spa_vdev_setpath`. That is insufficient for AROS: pool-import owns explicit
path policy, parent/file locks and native device bindings keyed by admitted
identity. Merely changing a label path would leave those structures stale.
The transport therefore rejects any property batch containing `path` before
other properties can execute. This is an explicit unfinished feature, not a
claim that all vdev setters are complete. A native relocation transaction must
coordinate path policy, exact backing identity, bindings and rollback before
this guard can be replaced. No device-path alias or arbitrary new backing is
authorized by an ordinary property request.

### Transactional path-admission prerequisite (2026-10-03)

`oz_vdev_policy_repath_begin/end` implements the policy part of relocation.
The service owner must first verify the backing identity and reserve its
binding; this API grants neither. Every matching named/GUID rule must contain
exactly one old path and no new path. Native new paths must already have an
available binding. All rule changes are prepared before publication under the
policy lock, preserving intersection semantics if any rule rejects admission.

During upstream work, old and new paths are admitted. Success replaces the old
slot; failure drops only the pending path. There is no extra permanent slot or
temporary 32-path capacity requirement. Overlapping replacement/extension/GUID
changes are rejected. The caller must retain registrations through completion
and serialize lifecycle work on the service owner, as existing configuration
operations do.

Native policy tests cover commit, rollback, partial preparation failure,
missing pool/path, same-path and already-admitted target refusal, duplicate
old-path ambiguity and competing mutations. They run in the ordinary native
mirror creation diagnostic; its runner requires the policy success marker.
This is an implemented internal transaction, not a connected public setter.
Backing-identity verification, binding transition, upstream path validation
and label-update integration remain unfinished; the public `path` guard stays.

Native run `qemu-run.KOr6tC53` passed (exit 0) with rebuilt binaries in external
`build-aros/repath-policy-checked-20261003`: policy tests, mirror/DOS regressions,
cold import, each sole member, zero readonly writes, writable COW reopen and
unchanged original hashes. Log: external
`OpenZFS-archive/repath-policy-native-qemu-20261003.log`. The earlier
`repath-policy-native-20261003` build was superseded before boot to add the
duplicate-rule rejection and test; it is not the verified artifact.

### Same-extent binding transaction (2026-10-03)

`oz_device_binding_repath_begin/end` adds the binding half. A separately
resolved candidate must match the admitted handle's actual opened device/unit
pointers, byte base, size and sector geometry. Both handles pass media checks;
the candidate never replaces the handle or upgrades its rights. Pending names
participate in collision checks and bound opens. Rollback drops the pending
name; commit retires the old name. Existing references and extent reservations
remain unchanged. Competing name transactions and binding replacement are
refused while pending. The owner must retain the binding through completion.

Native `device-range-run.bQgB5Pax` passed (exit 0) on two disposable disks,
including an extent spanning 4 GiB: mismatched expected handle/extent, existing
and pending name collisions, exact admitted handle reuse, rollback, commit,
retirement and writer reservation. Existing DOS publication/revocation,
concurrent writes, bounded I/O, GPT creation/expansion and cold persistence
also pass; original disk and cold-overlay hashes match. Artifacts and
`binding-repath-native-qemu-20261003.log` are on the external archive. The range
runner now uses common external-artifact/root-reserve checks.

These trusted adapter methods do not authorize arbitrary caller paths or
verify pool/member labels. Public `path` remains disabled until the enclosing
identity-checked policy/binding/label transaction is integrated and verified.

## Regression

`AROS_TEST_VDEV_PROPERTIES=1` requires the existing pool-create fixture. It
compares a GUID read against the independently enumerated member GUID, sets
and reads a comment, checks comment persistence across export/reimport, and
checks read-only and unauthorized writes. A rejected path change must leave
the original path readable. These are selected properties, not every value
or property combination, and not a native GUI-property-editor qualification.

The negative control `native-import-run.N4U2QOE6` used the previous Pzr88Y99
package. Its first GUID property read printed an unsupported-operation error
and failed the expected-content assertion at service-command-test.c line
3028. The host exited 1. Both original backing-image hashes were checked
afterward and remained unchanged. Log: external
`vdev-properties-before-20261003.log` and the run's complete `serial.log`.

Candidate lKuiafyR contains shared-engine.snk8v3qL, cli-link.SRsFBMos and
service/Manager from cli-vdev-properties-release-20261003, with unchanged
handler/device inputs. Run `native-import-run.isHBlew8` passed GUID and comment
reads/writes, both export/reimport persistence checks, readonly reads,
readonly/unauthorized write refusal and path-change refusal with unchanged
path. The full selected CLI/handler regression and both original backing
hashes passed, host exit 0. The same run included the stronger history helper
from cli-link.yOIxKN1y. External logs use the `vdev-properties-` prefix dated
`20261003`.

## Manager exact-property query

Operation 80 (`vdev.get`) reads properties through the authenticated admission
worker, using the caller's captured identity and upstream VDEV_GET_PROPS. Target
is the pool, Argument is the decimal member GUID, and Options is either one exact
property name or `all`. The Manager property picker preserves the GUID. No
confirmation or mutation flags are accepted for this read.

The response contains a structured property row. Values exceeding the row's
value field use `property-long` and the response detail; values exceeding that
field fail explicitly rather than being silently truncated. A missing property
uses its upstream default only after successful member lookup: an absent member
must remain ENOENT. The query currently has a bounded 64 KiB upstream reply.

`all` walks the visible applicable upstream vdev property catalogue and appends
dynamic vdev user properties from the live vdev property ZAP. One global cursor pages
the combined live view. Its opaque token counts catalogue candidates before
value lookup, so later pages do not re-read every earlier value; inapplicable
candidates still occupy a token position. Long values are returned as explicit `property-long`
placeholders with their byte length; an exact-property request is still required
to retrieve the full value through response detail. The Manager's Next action
continues through a new authenticated worker and never sends operation 80 to the
public service port.

The exact live opaque catalogue end is a valid empty page. End plus one and the
maximum cursor are rejected with EINVAL. The native authenticated-worker test
discovers that endpoint from the live fixture and checks all three boundaries;
it does not use the visible row count as a cursor because skipped inapplicable
catalogue candidates still consume positions.

`AROS_TEST_MANAGER_VDEV_PROPERTIES=1` adds API-level checks to the vdev-property
fixture: independently known GUID, comment, unset user-property default, and
missing member refusal. It also sets a 600-byte comment through upstream zpool,
checks the exact full value through the Manager API, and restores the comment
before persistence checks. This does not qualify GUI interactions or maximum
value boundaries.

QEMU run `native-import-run.LNzYpDlf` passed those four checks using bundle
`native-bundle.VWCLdAry` and `shared-engine.h3XaknoB` (checked Manager build).
The complete selected CLI/handler regression exited 0; both original backing
image hashes remained unchanged. Log: external
`manager-vdev-properties-after-20261003.log`, with full serial evidence in the
run directory. This was an isolated two-CPU, 2 GiB native AROS guest with private
OS prerequisite overlays, not stock-nightly or hardware qualification.

The extended long-value run `native-import-run.kmvfFmIP` passed the exact
600-byte response and the selected regression with host exit 0. Its log is
`manager-vdev-properties-long-20261003.log`; the runner requires the long-value
marker in addition to the original checks. The production bundle is unchanged.

## Picker namespace correction

The static catalogue previously mislabeled all 55 upstream vdev registrations
as pool properties. They now use `vdev:`; comparison with `vdev_prop_init()` in
`module/zcommon/zpool_prop.c` matches names, order and read-only markers.
The picker refuses pool/dataset entries for vdev.get and vdev entries for other
operations. Only recognized catalogue prefixes are stripped; custom property
names retain their namespace colon.

Private QEMU run `native-import-run.WWP4diD0` used the rebuilt Manager from
`manager-vdev-picker-release-20261003`, with the previously tested shared engine.
It passed the selected native regression, opened the actual GUI, and exited 0
after normal Manager close; original disk hashes passed. Captures
`picker-popup-vdev2.png` and `picker-comment-success.png` show the corrected
popup and successful application of **vdev:path** to Options (the latter capture
filename reflects the intended comment click, not the observed selection).
The mismatched dataset/pool rejection was also observed. No live-member GUI
query or custom-property typing verification is claimed. The previous rejection
status remains visible after a successful picker application: cosmetic feedback
still needs correction. Full input provenance is in `ui-actions.jsonl`.

## Manager setter

Operation 81 (`vdev.set`) accepts a pool, decimal member GUID and one exact
`property=value` Options string. It requires confirmation and rejects all
other flags, cursors and job IDs. The caller-owned project worker launches
upstream `C:zpool set` using exact argv, retaining spaces in values without
shell interpretation. Upstream libzfs parses property types and the authenticated
native ioctl boundary applies permissions and read-only policy. No change is
automatically retried after command or output-collection failure. Path relocation
remains refused at the native ioctl boundary.

The vdev picker sends the assignment to Options without replacing the GUID,
refuses marked read-only properties for this setter, and rejects a truncated
assignment. API-level regression uses the same project worker, then independent
zpool get and Manager get. It checks missing confirmation, dry-run, zero GUID,
read-only GUID property, path relocation, read-only pool and unprivileged caller.
These checks do not replace an actual GUI confirmation-and-submit test.
`AROS_TEST_MANAGER_VDEV_GUI=1` (with GUI=1 and Manager vdev tests enabled)
pauses on an imported disposable pool, reports its member GUID, and opens the
real Manager. After the user/automation closes Manager, independent zpool get
and the authenticated read API must both return `GUI-native-vdev`. No helper
writes that expected value. The runner requires the explicit GUI result marker.

Native QEMU run `native-import-run.LVkl3cob` passed this worker coverage and the
selected regression, exit 0, including unchanged original backing hashes.
Log: `manager-vdev-setter-after-20261003.log`. The rebuilt Manager is
`manager-vdev-setter-release-20261003/OpenZFSManagerNative`; bundle
`native-bundle.DXKsavqA` packages it with the previously qualified shared engine,
CLI and handler. The worker regression compiles the current project-worker
source into its test executable; it is not evidence of GUI button submission.

GUI run `native-import-run.aK4auOJi` used bundle DXKsavqA and the setter Manager.
`setter-confirmation.png` records the exact pool/GUID/comment and mutation
confirmation; `setter-result.png` shows upstream exit 0; `getter-result.png`
shows the actual authenticated GUI read returning `GUI-native-vdev`.
After normal Manager close the independent read-back result marker passed.
The complete selected regression then finished with host exit 0 after normal
closure of the final Manager window. Both original backing hashes passed.
Log: `manager-vdev-setter-gui-20261003.log`. Full actions are retained in
`ui-actions.jsonl`. Failed initial field clicks made no storage changes. This
qualifies the displayed comment write/read, not all setters or physical media.

## Typed property regression

The Manager worker/API test also writes and reads numeric `checksum_n=42`,
boolean `failfast=off` then `on`, a namespaced `test:manager` string containing
spaces, an equals sign and a semicolon, and an empty comment. Independent CLI
reads check the numeric, boolean and custom values. No shell expansion or
option splitting is intended for the custom value.

Two failed test assumptions are retained, not classified as implementation
regressions. Run `f10BxWnv` expected `allocating=off` to succeed on the growth
fixture's only top-level file vdev; upstream refused it. Run `nhm7iolB` then
incorrectly expected the leaf-only not-applicable value `-`; this fixture is not
a mirror leaf. The corrected test requires refusal with `allocating` still
`on`, and uses `failfast` for the mutable boolean round trip. Both failed runs
exited 1 and their original disk hashes were checked independently afterward.
Their logs are `manager-vdev-types-20261003.log` and
`manager-vdev-types-leaf-20261003.log`. No production change was made to bypass
upstream allocation policy.

Corrected run `native-import-run.nutLeaXt` passed every typed-property check and
the complete selected regression, host exit 0; both original backing hashes
passed. Log: `manager-vdev-types-top-level-20261003.log`. This tests live values;
only the earlier comment test explicitly checks persistence across reimport.

Still pending: native path relocation, broader Manager vdev setter combinations,
other writable property combinations, oversized reply negotiation and malformed
request stress. This run does not establish those capabilities or physical
storage durability.
