# Native project attributes

## Query-specific confirmation

The shared request classifier exempts only exact project `list` and `check`
actions from mutation confirmation. Set, clear and unknown actions retain the
conservative confirmation requirement; the worker still rejects incompatible
options before command execution. Manager updates its button when the action
changes and uses the same classifier for preview wording and confirmation.

Native `native-import-run.wbVqjISd` passes the full regression, including
unconfirmed list/check, unconfirmed set/clear refusal, and query options that
would request mutation. The mandatory worker marker now requires thirteen
negative cases and two unconfirmed queries. Independent
`control-integrity.62EbZw4V` reports No leaks with unchanged extracted-member
and original backing hashes. Manager `project-query.XeQgWmXu` builds with an
empty warning/error log.

Interactive `native-import-run.F3v9bisJ` exits zero with that Manager binary.
02-set-confirm.png retains the recursive id=2000;inherit=on confirmation;
03-set-result.png reports successful execution. Changing to list updates the
button to Send request (04-query-editor.png). One Send click, without a
confirmation click, produces root and child 2000 P rows (05-query-result.png).
After normal Manager close, independent CLI checks verify both IDs; the full
worker and native regression passes, followed by normal final Manager close.
Independent control-integrity.kuRsgNJM reports No leaks and unchanged extracted
member/source hashes. This proves interactive set/list, not interactive check
or every action/option combination. The operation list's static star still
marks operations containing mutations; the action-sensitive button and actual
request confirmation use the shared classifier.

## Native Manager worker identity and validation matrix

Native run `native-import-run.uKUuHtRz` completes the full CLI/zvol regression
with `OZ-NATIVE-COMPLETION-v1 PASS`. The test links the actual Manager project
worker and native process adapter, invokes installed `C:zfs project`, and checks
responses under security.library identities; it is not a simulated backend.
UID 77 is refused when not the owner, then succeeds after root transfers
ownership. Owner set/list/check, clear retaining the ID, and full clear pass.
The mismatch check requires the upstream mismatch text, not merely exit zero.
Root restores ownership and the original project ID before subsequent tests.

Ten invalid requests are rejected before command execution: seven malformed
setter option strings, unsupported dry-run, missing confirmation, and a relative
path. The mandatory runner marker reports malformed=10, nonowner=1,
owner-set=1, list=1, check=1, clear-keep=1 and clear=1. Worker sources and the
consistent pthread dependency are included in the retained input hashes.

Independent `control-integrity.xxpgcdbq` extracts both completed guest members
and passes upstream `zdb -bcc` with No leaks. Extracted member and original
backing SHA256 checks pass. This extends native worker coverage, not interactive
ordinary-owner GUI clicks, hosted verification, crash recovery or all option
combinations. The test changes do not modify the service or worker behavior.

## Native Manager project operation

Manager operation 70 now targets a native file/directory rather than a dataset
or zvol. Its dedicated drainable worker invokes installed C:zfs project with
exact argv through the existing native process adapter, without a shell or
daemon identity. The CLI then uses the authenticated FileLock packet above.
The parser supports list/set/clear/check, id=NUMBER, inherit=on for set,
keep=on for clear, directory=on, and the Recursive checkbox. It rejects
unknown/duplicate/malformed options, invalid IDs, incompatible combinations,
non-native paths and force/dry-run/raw/resumable flags. Set requires an ID.
Check follows upstream's mismatch output semantics; zero exit alone does not
prove no mismatches. This is a local Manager command worker, not an advertised
service-side operation 70. It requires the matching installed native CLI.

Output and diagnostics are captured together in an exclusively created
temporary file, held until the child drains. Accepted output is bounded to
the response size; collection errors are reported without automatic retry,
because mutations may already have occurred. Closing Manager waits for the
worker to finish and join. The original GUI run below retained generic query
confirmation; the query-specific increment above supersedes that behavior.
Every option/error combination still needs coverage. No binary secret storage
is involved in this operation.

Build project-manager.NMOEj6Qc passes native-import-run.kpcgu9oT (exit zero).
Actual GUI confirmation 02-confirm.png submits recursive set on OZPROJECT:
with id=2000;inherit=on. 03-set-result.png reports exit=0. The second actual
GUI command lists both root and child as 2000 P in 05-list-result.png, with
capture errno=0. Normal close allows independent CLI checks of both IDs,
followed by baseline restoration and full native CLI/zvol regression. The
runner requires the GUI acceptance marker; there is no setter fallback.
Independent control-integrity.7yKdJfEt reports No leaks and unchanged hashes.

Retained preceding mxQqjsUz passed recursive GUI mutation and independent
CLI readback but its GUI list capture failed with EIO (12-project-list.png).
The parent tried to flush/seek its inherited output handle after child writes.
Reopening the same file without truncation after child drain, matching the
working CLI test harness, fixes the observed capture failure in the fresh
guest above. This does not establish the precise lower-level DOS buffering
cause. Input events were also delayed during manual setup; screenshots retain
the attempted fields and the final exact confirmations. Neither input attempts
nor command acceptance alone were counted as successful filesystem edits.

## Cold guest persistence

AROS_TEST_PROJECT_PERSIST=write retains the actual CLI-assigned project dataset
after sync and unmount, alongside the existing retained CLI/zvol fixture.
AROS_TEST_PROJECT_PERSIST=read requires the writer's retained-success marker
and starts a separate readonly guest over fresh overlays of its disks. Both
new success markers are mandatory runner gates.

Writer native-import-run.kIWCzEvg and fresh reader native-import-run.0b9IViGt
both exit zero with service project-packets.AhmEfeMx and actual commands
cli-link.GySv6a7A. The new guest mounts the retained filesystem, reads root and
child project ID 1003 with inheritance using zfs project, independently reads
the child's attributes through its DOS FileLock packet, rejects an actual CLI
setter with EROFS, verifies unchanged ID and unmounts. Source fixture, retained
writer and readonly reader hashes are unchanged. Independent upstream zdb in
reader control-integrity.FVqdCBOk reports No leaks with unchanged extracted
members. This proves clean export/shutdown and fresh-guest persistence on the
tested mirror; it is not unclean power-loss, ordinary-owner positive or broad
topology coverage. Earlier statements below about cold persistence being
outstanding describe the earlier increments and are superseded by this test.

## Actual upstream zfs project command

The AROS-only ioctl adapter in aros/cli/project.c uses the public
__get_default_file descriptor extension and fstat to duplicate the existing
directory lock or filehandle lock. It does not reopen the pathname or depend on
private libc descriptor layouts. The held lock is sent to the authenticated
DOS packet endpoint and released after completion. Query output is populated
only on success; unrelated flag/extent-setting requests are rejected rather
than silently ignored. Upstream option parsing, traversal, output and open/close
logic remain unchanged; only this translation unit's project ioctls are adapted.

All common upstream CLI units compile in cli-probe.ds7spcWZ. Whole commands
link in cli-link.GySv6a7A. Native-import-run.ri02dVco passes initial actual CLI
list/set/clear/recursive operations and independent packet readback, plus the
full regression. Expanded native-import-run.ylfWFPtS also verifies nonowner
setter refusal and readonly refusal with retained ID, and exits zero. The
runner requires the full project CLI marker. Independent upstream zdb in
control-integrity.TRrS01ap reports No leaks; member and original backing hashes
remain unchanged.

Retained native-import-run.ox7ZOOeN failed the test's expected exit status:
upstream project returns -1 on setter error, not 1. The log already showed
Operation not permitted from the real setter. The corrected test checks -1
and the diagnostic, preserving the command's behavior. No new CLI binaries
were needed for that test correction.

Manager operation 70, ordinary-owner positive tests, all CLI option combinations,
cold-machine project persistence and power-loss verification remain outstanding.

## Authenticated DOS packet transport

The private OZ_ACTION_PROJECT packet now accepts an existing native FileLock,
an exact-sized versioned request, and GET or SET. The handler validates lock
lifetime and captures the packet sender on every call; a lock opened by root
does not carry root authority into a later ordinary-user request. SET retains
native property/volume checks and upstream owner, readonly and project policy,
syncs successful changes and notifies watchers. Unsupported request versions,
reserved fields and operations are rejected. This is an OpenZFS-private packet,
not a newly assigned system-wide DOS action.

Full service project-packets.AhmEfeMx and native-import-run.yljAgtkk pass
(runner exit zero). Actual DOS packets verify initial GET, SET to project 1000
with inheritance, inherited child ID, nonowner refusal using the root-opened
lock, malformed version rejection, stale-lock rejection, and EROFS after
readonly remount. The complete existing CLI/zvol/native regression also passes.
Independent control-integrity.ZAhxBiTR reports No leaks; extracted members and
original fixture hashes remain unchanged. The runner now requires the packet
PASS marker and retains the protocol header; this run began before those runner
additions, so its exact packet marker was checked separately after completion.

Retained native-import-run.UOJKQZgd failed test-client compilation before boot:
AROS BPTR is pointer-typed and DoPkt requires a SIPTR argument. Explicit
pointer-width casts fix the client without changing packet or handler semantics.
This increment does not yet wire upstream zfs project or Manager operation 70,
and does not add cold-machine project persistence or power-loss verification.

## Credential-bearing object bridge

oz_zpl_project_get/set and the matching read-mount adapters provide an object
interface for later DOS/CLI/Manager transport. The caller must already have
authorized traversal to the object. Get separately checks READ_ATTRIBUTES;
set uses the upstream owner/root setattr policy and requires an explicit
credential. IDs, inherit booleans and output pointers are validated; failed
reads leave outputs untouched. Znode references are released on all paths.

Build project-bridge.HZIxYTCG and native default-quota-run.q8tQIWI5 pass
(exit zero). In addition to the original tests below, the fixture checks
null-credential read/write refusal, untouched failed-read outputs, invalid
ID/boolean rejection, nonowner refusal, inheritance clear/get/restore and
read-only setter refusal after export/reimport. With inherited project ID
1000, default project byte and object limits each reject new file creation
with EDQUOT. Clearing each limit restores create/remove; sync and the full
default-quota suite pass. These are direct native bridge tests, not public
file descriptor transport, ordinary-owner positive coverage or cold reboot.

Native zfs_setattr now admits only the project ID and project-inherit extended
attributes. It validates the xvattr header, bitmap, 32-bit non-sentinel ID and
boolean inheritance value, copies the request into owned storage, and resets
the return-map pointer rather than writing through the caller's pointer.
Other extended attributes still return ENOTSUP. Native policy requires the
owner or root before mutation; upstream feature, readonly, quota, transaction,
SA layout and intent-log paths remain in use.

## Native verification

Full service project-attributes.ryNP7xTe passed the existing writable native
CLI/zvol regression in native-import-run.neA0kvGG. This baseline alone did not
exercise the new project branch.

The dedicated default-quota fixture then exposed a real crash in sa_add_projid.
Retained failures wSLobEfr, YxOqZokS and qbNcVClX narrow it to project mutation.
In qbNcVClX, zfs_setattr loaded at 0x4e48d20; its ELF offset is 0x2ddc60.
Fault PC 0x4d0fd33 resolves to ELF 0x1a4c73, sa_add_projid+0x43. Disassembly
reads offset 0x698 from a null SA userdata pointer. Upstream sa_add_projid
recovers the owning znode with sa_get_userdata, while the native cache had
passed NULL to sa_handle_get. The cache now passes its owning znode. Handle
destruction precedes znode free, including deletion and load failures.

Build project-test.NLXpWHFZ/build-5.log and native run
default-quota-run.vQoJKEUn pass (runner exit zero). The fixture verifies:

- malformed header, unsupported extended flag and invalid project ID rejection;
- non-owner refusal without changing the root project ID;
- root project ID 1000 and project-inherit flag through upstream setattr;
- inherited project ID 1000 on a newly created child;
- exact SA project-ID readback on root and child;
- unchanged caller return bitmap;
- export/read-only reimport, retained IDs/inheritance and EROFS on mutation;
- the existing full default-quota/dispatcher/DOS lifecycle suite.

This is an isolated RAM-backed pool in QEMU, not a cold-machine durability
test. Public Manager operation 70, native file-level ioctl transport and actual
upstream zfs project command execution are not implemented by this increment.
The later bridge test above adds selected nonzero-project creation enforcement
and inheritance clearing. Owner-positive cases, write exhaustion, legacy xattr
propagation and crash replay still need further verification.
