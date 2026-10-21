# Native Manager delegation

## Actual recursive revoke GUI acceptance

`AROS_TEST_RECURSIVE_REVOKE_GUI=1` with GUI=1 and ZVOL_TRANSPORT=1
replaces the first recursive revoke in the fixture with a real Manager launch.
There is no API success fallback. In `native-import-run.fexERJel`, capture
`03-confirm.png` shows permission.unallow on qemu_mirror/revoke-tree,
user:77, all=on, Recursive=yes and Dry-run=no. `05-completed.png` shows
authenticated completion of three datasets with errno=0. The intermediate
`04-result.png` is still waiting for admission, not a completed result.

After normal Manager close, the independent client verifies all three
snapshot attempts denied, all three user grants absent, other-group mount
grants retained, and the outside-subtree snapshot allowed. Explicit-permission
recursion and partial-failure/ordinary-user retry tests also pass, followed by
the remaining native writable/readonly mirror, CLI and zvol regression.
The final Manager closes normally; the runner exits zero and original backing
hashes match. Service/Manager inputs are manager-recursive-revoke.MztVLLcn.
This closes the basic recursive GUI path, not concurrent namespace coverage.
No DUT, user hosted session or physical storage was accessed.

## Recursive partial failure and ordinary-user retry

`native-import-run.XlUPNSyy` exits zero with expanded recursive tests. Explicit
permissions=snapshot removes only that permission across three datasets;
each retains user77 mount. A separate three-level fixture gives user77 local
allow only on the leaf. User recursive all=on completes that leaf, then reports
exactly completed=1/errno=EPERM/stopped-at=the parent. CLI checks retain both
ancestor snapshot grants; an actual leaf snapshot is denied after its removal.
Root then grants allow on the fixture root in both scopes. User77 recursively
removes its own remaining grants, returning completed=3/errno=0. All three
ACL listings contain no user77 records afterward, and refused snapshots do not
exist. The client compares the complete partial-error detail, not an errno
substring. Native cleanup and remaining suite pass; original hashes unchanged.
No service change was needed. The mandatory expanded marker was added after
runner snapshot and explicitly checked against serial. Namespace races and
actual recursive GUI interaction remain unverified; no DUT was accessed.

## Recursive revocation

Operation 66 accepts CONFIRMED|RECURSIVE with either explicit permissions or
all=on. Upstream dmu_objset_find visits filesystem/volume descendants before
parents; snapshots are excluded. Each visited dataset uses the existing
credential-scoped upstream SET_FSACL path. Recursive allow, force and dry-run
remain refused. The traversal is live, not an atomic subtree transaction:
the first error stops work, and response detail reports completed dataset count,
errno and the last attempted path. Concurrent namespace changes are not hidden
by retries. Children-first ordering preserves inherited delegation authority
until their ancestor's own grants are removed.

Build `manager-recursive-revoke.MztVLLcn` succeeds. QEMU run
`native-import-run.YXkS3MlW` exits zero. Root revokes user77 all=on across a
three-level tree and receives completed=3/errno=0. UID77 snapshot attempts on
all three datasets return EPERM and independent CLI checks find no refused
snapshots or remaining user grants. Another group's mount grants survive on
each dataset; an outside-subtree dataset still allows the user's snapshot.
All fixture cleanup and preceding native/CLI/zvol tests pass. The mandatory
recursive marker was added after runner snapshot and checked against serial.

Partial-failure reporting, ordinary-user recursive self-revocation, explicit
permission-list recursion, live namespace races and actual recursive GUI action
still require dedicated verification. No DUT or user hosted session was used.

## Delegated-administrator authorization

`native-import-run.YuBBoJv3` exits zero using the current full service. UID77
holding snapshot without allow cannot delegate it. Root adds allow; UID77 can
grant snapshot to group99, but cannot grant unheld destroy or redelegate allow,
and cannot revoke another principal's grants. Self-revoking snapshot works;
delegation remains allowed through supplementary group99 until root removes
that grant. Fresh delegation then returns EPERM. Self all=on removes the user's
remaining allow right, and a subsequent attempt to restore allow is denied.
CLI listing verifies the successfully delegated UID78 snapshot remains and
the refused UID79 grant does not exist. Cleanup and the whole native regression
pass. The mandatory result gate was added after runner snapshot and explicitly
checked against its serial output. No service implementation change was needed.

Retained failures: aSZK1egg incorrectly expected loss of effective snapshot
authority after removing only the personal grant, overlooking supplementary
group99. wUcnUPkj passed the authorization sequence but expected a bare numeric
UID in CLI output; upstream displays unmapped UID78 as `(unknown: 78)`.
Both test corrections preserve upstream semantics rather than weakening policy.
This covers selected delegated-administrator combinations, not all named-set
delegation, recursive removal or concurrent revocation races.

## Actual revoke-all GUI acceptance

`AROS_TEST_REVOKE_ALL_GUI=1` with GUI=1 and ZVOL_TRANSPORT=1 pauses the
fixture before local revoke-all. There is no automatic API revocation in that
branch: the actual Manager must unallow user:77 on qemu_mirror/revoke-all with
all=on;scope=local, then close normally for independent authorization checks.

`native-import-run.bKFuaoXq` exits zero using service/Manager
`manager-revoke-all.RaEKVIqa`. `02-revoke-confirm.png` captures the exact target,
principal, options and disabled dry-run before Submit. `03-revoke-result.png`
shows authenticated revoke errno=0. After normal close, the independent UID77
client is denied the parent snapshot but can snapshot the descendant. Remaining
both-scope/other-principal/creator/set tests and full native regression pass.
The final Manager closes normally and original backing hashes remain unchanged.
Independent `control-integrity.YYctNwiE` reports No leaks and unchanged
extracted-member verification hashes.
No DUT, physical storage or user hosted session was used.

## Explicit revoke-all for one principal/scope

Operation 66 accepts `all=on` instead of `permissions=LIST`. It emits upstream
boolean removal keys for both the direct and named-set-reference ACL classes,
matching upstream zfs unallow semantics. Scope remains local/descendants/both
(default both); set definitions and creator records still reject explicit scope.
This removes the selected principal's stored grants on this dataset, not other
principals, ancestor grants, or recursively stored child grants. Empty permission
lists do not imply all. Grant-all, mixed list/all and duplicate all are refused.
Captured credentials and upstream SET_FSACL policy remain authoritative.

Build `manager-revoke-all.RaEKVIqa` succeeds. Writable native run
`native-import-run.F6uqrzql` exits zero: local-only removal denies a parent
snapshot but retains descendant snapshot authority; both-scope removal then
denies a new child snapshot. CLI inspection verifies the removed user records,
retained other-group grants, explicit creator removal and named-set definition
removal. Denied snapshots remain absent, successful test objects are cleaned
up, and existing scope/named-set/native/zvol suites pass. Independent
`control-integrity.ilgs34ha` reports No leaks and unchanged input hashes.

Readonly cold run `native-import-run.IbEb6NqL` exits zero and verifies all=on
returns EROFS, then completes permission-list boundaries, retained ACL records,
zvol readback and native cold tests. All original/writer/overlay hashes pass.
The new mandatory markers were added after runner snapshots and explicitly
checked against serial logs. Earlier failed `WjujlYn8` had expected numeric
group 99 in CLI output, but upstream resolved it to the fixture name extra;
the corrected assertion accepts that name and the numeric representation.
No engine change was made in response to that test-only mismatch.

The Manager help describes explicit all=on; actual GUI revoke-all interaction,
recursive removal and delegated-administrator combinations remain unverified.
No DUT, external storage or user hosted session was accessed.

## Permission-list request boundaries

`native-import-run.AXvzwJFk` exits zero with 15 explicit boundary cases against
the built full service. Empty root ACL and cursor exactly at the five-row end
return success with no rows/next cursor. Cursor six and UINT64_MAX return
EINVAL; missing dataset returns ENOENT; snapshot target returns EINVAL.
Nonempty argument/options, each of the six request flags and a supplied job ID
are rejected during admission. The client checks exact returned errno text,
status, empty row count, zero continuation cursor, and ticket release where
admission succeeded, then repeats valid structured listing and the cold suite.
All original, retained-writer and read-only overlay hashes pass. The mandatory
boundary marker was added after runner snapshot and checked against serial.
No service/engine change was required. This does not cover oversized ACL buffer
growth, malformed wire lengths or concurrent ACL changes.

## Actual permission-list GUI acceptance

`AROS_TEST_PERMISSION_LIST_GUI=1` with GUI=1 and permission persistence mode
authorize pauses the native fixture as UID77 while its 35-record ACL exists.
The actual Manager is launched before the independent structured-query check;
close it normally to resume checking and remove only the disposable grants.

`native-import-run.YtLqsyAI` passes this interaction using service/Manager
`manager-permission-list.Vyl9hSkv`. Select Permissions/list grants, target
qemu_mirror/named-permissions/first, clear the default Dry-run checkbox, then
Send request. `03-list-page1.png` shows success, source/scope/grant metadata and
enabled Next page. `04-list-page2.png` shows the successful next authenticated
request and disabled Next page at the end. No confirmation requester is used
for this read operation. Capacity/progress counters are not rendered for ACLs.

After normal GUI close, the independent client verifies all 35 rows over two
pages as UID77, including each of 30 temporary users exactly once. Cleanup,
sync/export and remaining cold native tests pass; final Manager closes normally
and the runner exits zero. Backing originals and retained writer hashes pass.
Screenshots validate the UI path; the separate client validates complete row
contents. This does not add large-buffer retry or concurrent ACL-change coverage.

## Authenticated permission listing

Operation 79 (`permission.list`) calls upstream GET_FSACL under captured native
caller credentials. It accepts a filesystem/volume target and page cursor;
flags, options and arguments are refused. Full-dispatcher services advertise
it even in read-only mode. The Manager routes initial and next-page reads
through the authenticated worker and renders permission records as metadata,
without meaningless capacity counters.

Each row has kind=permission, name=principal (`user:ID`, `group:ID`, everyone,
creator or `set:@name`), parent=source dataset, state=scope/direct-or-set-reference,
and value=one permission or set reference. Ancestor records are preserved;
these are stored grants, not an effective-rights calculation. Pages reread live
state; restart enumeration if grants change. Oversized names fail explicitly
instead of truncating identities. Packed ACL reads grow on upstream ENOMEM
up to 128 MiB; mutations are not retried.

Full service/Manager build `manager-permission-list.Vyl9hSkv` succeeds.
Read-only cold run `native-import-run.55WjnXAx` exits zero and verifies all five
expected structured records, including source dataset, both user-reference
scopes and the acquired snapshot grant. All backing/overlay hashes pass.
Writable cold run `native-import-run.FskZDX1Y` adds 30 unique local grants,
queries as UID77 and verifies exactly 35 rows across two pages with no duplicate
extra principals, then removes the added grants. Remaining cold authorization
and native DOS tests pass; runner exits zero. Independent
`control-integrity.xrOV7UQC` reports No leaks and unchanged verification inputs.
The mandatory pagination gate was added after runner snapshot and explicitly
checked against its retained serial output.

Actual GUI interaction/visual verification, large-buffer retry, concurrent ACL
changes and complete malformed-query coverage are still outstanding. No DUT,
physical storage or user hosted session was touched; release archive unchanged.

## Writable cold authorization

`AROS_TEST_PERMISSION_PERSIST=authorize` extends the cold read mode with an
explicit writable reimport on fresh child overlays of the retained writer.
It requires the healthy two-member mirror fixture and no GUI/GPT mutation.
Original backing and retained-writer hashes remain mandatory; only the two
new overlays are allowed to change. Both are checked for actual changes.

`native-import-run.js4edsmr` exits zero. After the cold ACL inspection and
read-only export, UID77 successfully snapshots the first volume using its
persisted creator-derived grant. Snapshotting the later volume and creating
another volume through the now mount-only nested set return EPERM; independent
CLI listings prove the refused objects absent. Root restores create to the
base set, then UID77 creates a volume through the saved outer-set reference.
The upstream CLI independently confirms type=volume. Root removes the test
objects, revokes create again, syncs and exports; the remaining native cold
DOS-read suite passes. No login authority is substituted by the service.

Independent `control-integrity.9FrWv8Qt` reports No leaks and unchanged
extracted-member verification hashes. Original and retained-writer images
remain unchanged. The exact two overlay-change gates were added after the
runner snapshot and explicitly checked against its saved overlay-change.log.
This adds ordinary-user writable cold authorization evidence; it does not
cover every permission, concurrent revocation, or physical storage.

## Cold named-permission persistence

`AROS_TEST_PERMISSION_PERSIST=write|read` explicitly opts into retaining and
checking the named-permission fixture, alongside the existing zvol discard
persistence modes. Older retained zvol fixtures are not required to contain
these records. The read runner requires the writer's synced-fixture marker.

Writer `native-import-run.aQgiYth0` exits zero after the named-set, creator,
scope and remaining CLI/native tests, retaining the fixture and syncing the
pool. Independent `control-integrity.LUr3MlDc` reports No leaks; extracted
member and original backing hashes pass.

Fresh read-only guest `native-import-run.8Tvbxp9d` exits zero. Actual upstream
`zfs allow` output verifies base set mount-only, outer-to-base reference,
UID77 outer-set grant, the first volume's acquired snapshot permission, and
absence of creator/snapshot grants on the later volume. The two successful
snapshots exist; the refused snapshot and third volume do not. Retained zvol
content and read-only refusal checks also pass. Original, retained-writer and
cold-overlay hashes remain unchanged. Mandatory result gates are present in
the runner snapshot.

Retained failed cold run `0ftf1fNV` displayed the expected user grant but failed
the test's required trailing-newline comparison. The corrected matcher accepts
an exact whole record terminated by newline or end-of-output, not a substring
permission match. No engine/ACL implementation was changed for this test.

This proves cold persisted ACL records, not writable authorization after reboot,
named-set GUI interaction, all delegation combinations or physical durability.
No DUT, user hosted session or external storage was accessed.

## Named sets and create-time grants

Argument `set:@name` defines or changes an upstream named permission set;
`creator` changes upstream create-time grants. Neither accepts explicit scope,
because their upstream ACL keys are not local/descendant grants. Ordinary
user/group/everyone permission lists can include `@set` references, and sets
can refer to other sets. The adapter separates direct permission names from
set references into the upstream lower/uppercase ACL classes, using upstream
`zfs_deleg_whokey`, name validation and SET_FSACL authorization. It does not
expand sets itself or copy create-time rights in native glue.

Build `manager-named-permissions.jdiTFNsL` succeeds. Native QEMU
`native-import-run.XZbs2ddB` exits zero with the previous nine-case matrix and
new named-set/create-time tests. A set containing create/mount is referenced
by a second set granted to UID 77; that user creates real volumes. Removing
create from the base set causes a fresh creation to return EPERM with no new
dataset. A create-time snapshot grant lets the user snapshot the first volume.
Removing that create-time grant prevents snapshot creation on a later volume
but leaves the first volume's acquired snapshot rights intact. Malformed set
names and scope on creator grants return EINVAL. Independent
`control-integrity.leSmESd7` reports No leaks and unchanged verification/source
hashes. The mandatory named-permissions marker was added after runner snapshot;
its exact result was explicitly verified in the retained serial log.

The updated Manager builds with syntax help. This is native API evidence, not
new GUI interaction or cold grant persistence. Named-set cycles, create-time
set references, mixed direct/reference lists, supplementary-group semantics,
recursive revoke, delegated administration and in-flight revocation races
still need coverage. No DUT/user session/physical storage was accessed.

## Scope matrix and actual GUI acceptance

`native-import-run.dAzwyNV4` exits zero with a nine-case matrix: user 77,
primary group 88 and everyone, each with local, descendants and both scope.
Every case attempts actual volume creation immediately below the granted
filesystem and below a pre-existing child filesystem. Only the intended
locations admit creation. The test removes successful children, revokes the
same grant, then requires EPERM at both locations. No-op success or merely
displayed ACL text cannot satisfy these checks. Independent
`control-integrity.NbGQ0WWY` reports No leaks and unchanged input hashes.

`native-import-run.QFdKAdXR` then passes actual Manager grant/revoke interaction.
The fixture waits for a real Permissions/allow request for user:77, without an
automatic grant fallback. After that Manager closes, the separate client logs
in as UID 77 and creates a volume, independently confirmed as type volume by
the upstream CLI. A second Manager performs Permissions/unallow. After it
closes, fresh creation is denied and its destination is absent. Both requests
and results are captured in `03-grant-confirm.png`, `04-grant-result.png`,
`05-revoke-confirm.png`, and `06-revoke-result.png`. The same guest repeats
the full nine-case matrix and remaining selected native/CLI regression.
The final Manager closes normally and the runner exits zero. Independent
`control-integrity.P0heaIU6` reports No leaks and unchanged hashes.

Use `AROS_TEST_PERMISSION_GUI=1 AROS_TEST_ZVOL_TRANSPORT=1 AROS_TEST_GUI=1`
with the private native-import runner and service/Manager from
`manager-delegation.gPn3YOV0`. Mandatory GUI and scope markers are part of the
snapshotted passing runner. These results supersede earlier unverified direct
principal/scope and GUI notes below, but do not cover supplementary groups,
cold retained grants, named sets, create-time grants, recursive revocation,
delegated administration or in-flight revocation races. No physical storage,
DUT or user hosted session was touched; the development archive is unchanged.

Operations 65/66 grant/revoke explicit upstream delegation permissions through
ZFS_IOC_SET_FSACL. The Manager uses the authenticated worker; direct public
mutation messages remain refused. The adapter retains captured caller identity
through upstream zone, grant/revoke authority, pool state and ACL validation.
There is no root substitution or shell-command execution.

Target is a filesystem or volume, not a snapshot/bookmark. Argument accepts
`user:77`, `group:88`, or `everyone`. Numeric IDs are checked as unsigned 32-bit
values without truncation. Options are `permissions=create,mount;scope=both`.
Scope may be local, descendants, or both (default). Duplicate/empty permissions,
unknown properties/scopes and unsupported fields are refused. Every request
requires confirmation; force/dry-run/recursive are not implemented. Revocation
removes only the named permissions. Named sets and create-time grants remain
unimplemented; group/everyone/scope variants need dedicated runtime acceptance.

## Verification

Full service and Manager build `manager-delegation.gPn3YOV0/build-2.log` passes.
The first build failed because the broad kernel ioctl header exposed VFS types
not defined in this userspace translation unit; its log is retained. The adapter
now uses the ioctl wire header and exact common-dispatcher declaration.

Native QEMU `native-import-run.CfDjVcN5` exits zero. Independent native client
tests malformed scope, duplicate permission and unknown permission refusals;
an unprivileged self-grant returns EPERM. System identity grants create/mount
through the new API; UID 77 then creates a real volume. API revocation makes a
fresh creation fail with EPERM, and independent CLI listing confirms absence.
Root cleanup, subsequent block I/O and selected native/CLI regression pass.
Independent `control-integrity.1bFB1N6m` reports No leaks and unchanged inputs.

Fresh read-only QEMU `native-import-run.Agwqrtb3` exits zero. Both grant and
revoke return EROFS; volume creation remains refused and retained volume bytes
remain exact. Independent `control-integrity.PeH15MVE` reports No leaks, with
unchanged original/retained writer and verification input hashes. This client
uses exact errno-text comparison; the prior writable client's retained output
also contains the exact expected codes. Mandatory permission-result gates were
added after both runner snapshots and checked explicitly against their logs.

These prove API behavior, not interaction with the permissions page. The new
Manager is built and routes these operations correctly by inspection; actual
GUI grant/revoke, cold persistence of retained grants, additional principal and
inheritance combinations, delegated administration, and in-flight revocation
remain to test. No DUT, user hosted session or physical array was accessed.
The immutable development archive is not yet updated with these binaries.
