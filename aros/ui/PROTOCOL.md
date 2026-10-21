# OpenZFS management API 3 (AROS-native)

## Typed dRAID geometry

Version 3 expands each typed vdev group with draid_parity (1-3), draid_data
(0 selects upstream's min(8, available) default), and draid_spares. Layout
OZ_DRAID is valid only for data groups. Other layouts require zero in all
three fields. Shared validation checks geometry before any device is opened;
the service constructs upstream draid nvlists and computes the minimum slice
group count using the upstream zpool formula. Creation enables feature@draid;
adding dRAID to an existing pool still requires the appropriate pool feature.
The guided editor carries these values through preview/confirmation/IPC, not
through a shell string. Distributed spares are geometry, not separate paths.

Management ports now end in v3. The authenticated admission endpoint retains
its name and rejects mismatched request/result buffer sizes; old v2 message
clients must be rebuilt. Session/control-file protocols are unchanged. A new
Manager and service must be installed together in a fresh isolated runtime.
Historical v2 evidence below remains evidence for that earlier wire format.

`native-import-run.EfKUaRGs` passes the full runner with service
`full-typed-draid` and CLI `cli-link.Fa0pX0f6`. Its independent authenticated
client creates draid1:1d:3c:1s through this typed API, then real upstream CLI
and DOS calls verify exact 1 MiB content, degraded writes, completed resilver,
distributed-spare activation, active-spare writes/reimport, restoration and
readonly reimport. Geometry tests sweep parity 1-3, 2-32 children and spare
counts, verify default data width/minimal slice count and reject impossible
geometry and dRAID parameters on mirror. Original mirror fixture hashes match.
The API-version error text was corrected after this build; no engine behavior
changed with that diagnostic-only follow-up.

Private GUI run `native-gui-run.jCNTesFI` uses the rebuilt native Manager.
Screenshots 07/11/12 show actual entered geometry, three member paths and
the resulting API 3 preview. Screenshots 08 and 10 show missing-device and
duplicate-path refusal. The early input attempt lost final characters before
the next mouse event; its captures remain, and input was repeated with guest
processing time before accepting the corrected paths. The GUI guest has no
running service: this proves editing/validation/preview, not GUI-created media.
The main window's legacy topology selector still says stripe after applying
a dRAID draft; the typed preview correctly says draid. This visible selector
inconsistency needs correction and revalidation. Full GUI creation, typed
dRAID add, parity 2/3 engine runs and cold typed-created persistence remain
separate acceptance work.

Follow-up `native-gui-run.CVD26KlO` fixes and visually verifies the selector,
and passes actual GUI pool creation, independent topology checks, native DOS
readback, Wanderer browsing and clean unmount/export. See
VERIFICATION-CREATE.md. Typed dRAID add, parity 2/3 engine runs and cold
typed-created persistence are still separate acceptance work. Service greeting
text now derives its advertised API version from OZ_VERSION instead of a stale
literal, and the native CLI fixture checks advertised versus wire versions.

Typed persistent writer `native-import-run.d5xNJ8wK` and fresh reader
`native-import-run.92foC7SY` both pass full runners and independent zdb.
The pool was created through API3, not zpool create. The cold reader imports
that exact exported active-spare state, verifies the changed file and 1 MiB
payload, refuses writes, exports, and preserves all member bytes and the
source backing hash. API3 advertisement and explicit older-version rejection
also pass in the reader. This is clean-export persistence, not power loss.

A stronger Manager inventory check then fails in `native-import-run.w4LUcpJv`:
CLI status says INUSE but the readonly Manager adapter replaces the spare's
live stats with configured-only INACTIVE. The adapter now preserves upstream
VDEV_AUX_SPARED statistics and labels a healthy spare in use as INUSE; unopened
auxiliaries remain explicitly INACTIVE without fabricated counters. Dedicated
corrected runtime acceptance passes in `native-import-run.KFDyzDtJ`: the
Manager reports exactly one INUSE distributed spare, the CLI reports INUSE
with the original member OFFLINE, and exact changed/1 MiB payload reads and
readonly refusal pass. The full runner passes, all retained members and the
writer backing remain unchanged, and independent zdb succeeds. API3 greeting
and explicit version-2 rejection also pass. The failing run remains retained.

Higher-parity runtime `native-import-run.DQuO0tSx` passes the complete runner.
Authenticated typed creation builds draid2:1d:4c:1s and draid3:1d:5c:1s on
fresh private RAM files. Each writes and reads exact DOS data, offlines as many
members as its parity, writes while degraded, restores all members, requires
completed zero-error resilver and ONLINE health, then exports and imports
readonly. Exact changed bytes and 1 MiB payload plus write refusal pass;
writable reimport, destruction and file cleanup finish both cases. This is
same-guest export/import, not cold persistence, physical devices, distributed
spare activation at parity2/3 or independent zdb of these RAM-backed pools.
The initial `tbXHTKI4` failure was a fixture sequencing error: its full-pool
ONLINE waiter ran before the other deliberately offline members were restored.
That run remains retained; production engine code was not changed to pass it.

## Typed native replacement and attach (2026-09-27)

Operations 11 and 13 now admit blank native members through the same shared
preflight as upstream CLI attach/replace. Existing GUID validation, root and
writable checks remain. All new paths are bound and checked before upstream
`spa_vdev_attach`; successful operations retain their native reservations for
the asynchronous resilver and later pool lifetime. The Manager field is now
labelled "New backing path" and its help includes pre-provisioned native
partitions. No force, partition-table editing or external raw-writer exclusion
is implied. These updates supersede the earlier native attach/replace gaps below.

`native-import-run.HNkR9ttD` passes the full replacement regression using
`full-typed-native-replace` and `cli-link.PU8NyHsg`. The test obtains the old
member GUID from the Manager status API, checks typed EROFS/EPERM refusals,
then replaces onto the native partition. It waits for zero-error completed
resilver, detaches the remaining file member, exports and deletes both old
files. Native-only readonly reimport returns the exact 1 MiB payload and
normalized-name file. Complete CLI/native regression, GPT exterior comparisons
and original-image hashes pass without native I/O-failure diagnostics.
Use `AROS_TEST_TYPED_NATIVE_REPLACE=1 AROS_TEST_NATIVE_REPLACE=1`.
This is actual API/storage evidence, not interactive GUI or cold replacement
proof. Manager help/label builds separately.

Typed attach run `yXImR9Ez` also passes the complete suite. It obtains the
existing GUID through Manager status, requires EROFS/EPERM refusals, then
attaches the native partition and waits for completed zero-error resilver and
exact DOS payload readback. The upstream CLI subsequently splits the native
member into an exported destination. Both separate pools are readable; the
native destination is retained for cold testing. Enable
`AROS_TEST_TYPED_NATIVE_ATTACH=1 AROS_TEST_NATIVE_SPLIT=1` and the existing
split-retention/CLI-retention flags for the writer.

Fresh guest `Zxhd7Mhl` imports only that retained native destination, verifies
the exact 1 MiB payload, normalized-name file and readonly denial, then exports.
All original/writer backing and fresh-reader overlay hashes match. Independent
upstream `zdb -bcc` exits zero with No leaks, including the DTL accounting gate.
The complete reader exits zero. This closes native typed-attach/resilver/split
cold-destination coverage, not GUI interaction, all vdev layouts or physical
durability. Logs and failure boundaries remain under the named build-aros runs.

## Typed native-partition pool creation (2026-09-26)

Operation 9 also accepts explicitly provisioned native partitions for pool
expansion. It shares CLI add admission: root/writable checks, complete blankness
checks, retained native bindings and per-pool path-policy extension precede
`spa_vdev_add`. Native leaves inherit the pool's maximum ashift when the typed
request omits it. The earlier file-only path remains unchanged; native attach
and replace are still separate work.

`native-import-run.ymkZ7IFj` passes the full regression with service
`full-typed-native-add` and commands `cli-link.PU8NyHsg`. The actual typed API
rejects readonly EROFS and non-root EPERM, then adds the fresh native partition.
The real CLI reports it, reopens the pool, evacuates the original file vdev,
waits for completed positive-byte removal, exports and deletes that old file.
Readonly reimport using only the native partition returns the exact 1 MiB
payload and normalized-name file, followed by clean destruction. Original
backing hashes, both GPT copies/exterior comparisons and host GPT validation
pass; no native I/O-failure diagnostic appears. Enable
`AROS_TEST_TYPED_NATIVE_ADD=1 AROS_TEST_NATIVE_ADD=1` with the full CLI writer.
The updated Manager help builds separately. This is API/storage proof, not a
visual GUI action, a cold-boot add test, or a test of every allocation-class role.

Operation 5 now accepts explicit native partition leaves as well as regular
files. Native layouts share the upstream CLI's credential, whole-extent
blankness, DOS-claim, retained extent and pool-name policy preflight. All
members are admitted before `spa_create`; successful or retained SPAs keep
their reservations until export/destruction. It does not alter GPT tables,
detach foreign handlers, force reuse of populated media or exclude independent
raw writers outside the service. Media must be exclusively provisioned by the
operator. The existing file-only typed attach/replace/add paths are unchanged.

`native-import-run.jVmGsHZN` passes initial API creation, CLI/DOS round trip,
the full regression, unchanged GPT exterior and independent `zdb -bcc`.
Strengthened repeat `3Wp6NgoR` additionally requires rejection of missing
confirmation (EINVAL), a non-root caller (EPERM), and recreation on the exported
populated partition (EEXIST). Exact 1 MiB data and normalized filename readback
pass before and after readonly reimport. Both runs exit zero with independent
integrity checks and original backing hashes intact.

Fresh reader `1jMj6yDO` imports that retained partition through the real CLI,
mounts it through DOS, verifies exact bytes and write denial, then exports.
All writer/original backings and new reader overlays remain unchanged. Use
`AROS_TEST_TYPED_NATIVE_CREATE=1` with the native-create writer mode, then the
existing `AROS_TEST_NATIVE_CREATE_COLD=1` reader mode. This is authenticated API
and native DOS evidence, not yet an interactive GUI creation test on a native
partition. Manager help is updated; earlier file-backed GUI proof remains
separate. Mixed native/file layouts and all native RAID layouts need coverage.

## Verified native file import (2026-09-22)

The native upstream service now accepts operation 7 through MuFS-authenticated
admission: root caller, flags exactly CONFIRMED, target pool name, empty
argument, options `readonly=on` or `readonly=off`, and topology.device_count
with explicit device paths. group_count/ashift and device group numbers must
be zero; import obtains topology from labels, not a requested creation layout.
File paths may be native Volume:path or /Volume/path. No automatic device scan.
Dedicated regular backing files are required; no external writers are allowed.
Read-only or writable recovery may include an explicitly absent member path under an
existing native directory. List every member, including absent ones, using
the exact stored path spelling for absent files. The parent directory is held
open until export; no file is created and no neighboring files are scanned.
Writable import requires explicit readonly=off. The upstream engine decides whether
the surviving members provide sufficient redundancy; the UI never forces it.
Recovery wording was rebuilt and visually checked in private native GUI run
yIKgRtM0 (02-import-editor.png): both guidance lines fit, and creation-only
controls remain disabled. This editor-only run intentionally has no service;
backend recovery evidence is the separate cold matrix in
../engine/IMPORT-PORT-STATUS.md. The private guest was closed afterward.
The engine retains file identity locks for read-only imports and per-pool path
admission until successful export or shutdown. For writable imports, validated
file locks are handed off to upstream writable handles after pinning their
parent directories: native FAT requires exclusive writable opens. The handoff
is not atomic against external file replacement; dedicated backing files with
no other writers or renames are required. Parent locks are not file identities.
Writable degraded cold verification is recorded in ../engine/IMPORT-PORT-STATUS.md.
No force/rewind/renaming/device imports are implied by this implementation.

The native editor candidate uses Pools / Import / Import files, with separate
draft state from creation, exact file-list preview and read-only default.
UI execution still requires confirmation and uses the authenticated worker.
Native GUI run lb79ifSP verifies create/export, read-only import, export,
writable import, then native mounted-file readback and visible Wanderer icon.
Both test pools were exported before the private guest was stopped. Screenshots
21/22/23 show the read-only preview/confirmation/result; 27/28 show writable
confirmation/result; 31 shows the imported volume's actual file icon. This
fixture uses RAM members, not cold persistence. The subsequent help-text-only
cleanup removes obsolete XDR wording; rebuilt visual check FQl9B7sX passed
(02-import-request.png and 03-import-editor.png). This visual-only guest
deliberately ran without the engine and correctly reported service unavailable;
it does not replace the authenticated end-to-end evidence above. The editor
and manager were closed and the owned private guest was stopped afterward.
Native API evidence:
admission-service-run.nPVfUQU1 (five layouts, mounted RO/RW, non-root denial),
and 1NhP09ds (full existing service/cold-persistence regression).

The older text below describes the initial stub and API envelope. The separate
stub executable remains useful for no-engine UI testing; the native engine is
not a stub. The legacy saved-XDR fixture route is not generic file import.

The executable `OpenZFSService` owns `OpenZFS.Management.v2`. Both the Zune
manager and `OpenZFSAPITest` use the implementation in `api.c`, not parallel
mock paths. The service validates the request and calls `oz_engine_dispatch`.
That adapter currently reports **engine unavailable** for storage operations.
It must eventually call the upstream AROS OpenZFS engine; it must not dispatch
to the old private-format filesystem or shell out to a command string.

## Request and result

`api.h` defines the native ABI; `operations.def` assigns stable operation IDs.
The envelope has a magic, ABI version, message byte length, request ID and
opcode. The fixed-width target, argument and options fields are bounded and
NUL terminated. Unknown versions, opcodes, flags and required-field omissions
are errors. Mutations require a confirmed flag; the GUI obtains this only
after displaying the exact request and asking the user. This confirmation is
NOT an authorization mechanism: engine credentials and delegation checks
are still mandatory when those operations become available.

- Target: pool, dataset, snapshot/bookmark, device or job identity, per opcode.
- Argument: secondary operand(s), stream location, device specification or
  `property=value`, per opcode. It is never passed to a shell.
- Options: `key=value;key=value`. Backend parsers must reject unknown keys,
  invalid values and ambiguous encodings rather than silently ignoring them.
  Values needing separator escaping require a future typed parameter extension;
  do not assume shell quoting. Do not send raw secrets in these fields.
- Flags: confirmed, recursive, force, dry-run, raw stream, resumable stream.
  Unsupported flags must be rejected by the operation's backend implementation.
  A dry-run request must never fall back to a real operation.
- Cursor/next_cursor: opaque pagination tokens. Pagination repeats the original
  request, not subsequently edited GUI fields. Engine implementations must bind
  cursors to the query and enforce lifetimes.
- Response: explicit status and explanatory detail, engine availability,
  capability bitset (bit = operation ID), optional job ID, and typed result rows.
  Rows contain kind/name/parent/state/value, numeric capacity fields and progress
  in thousandths. The GUI renders these directly and can export all fields.

## Typed pool layout (version 2)

The ABI is deliberately versioned because `oz_message` now contains a topology.
Version 1 clients/services use a different port and cannot exchange these
larger messages. A compile-time assertion enforces Exec's 16-bit message size.

For pool.create, either supply the legacy device argument or a typed topology,
never both. Typed layout contains up to eight groups and 32 device paths, each
with a group index. Paths are separate bounded NUL-terminated strings; spaces
and punctuation are literal, not shell syntax. The GUI does not discover or
open devices: the user enters their exact AROS paths. The engine adapter must
translate this structure into upstream nvlist vdev configuration, including
expanding stripe, cache and spare groups into individual leaves. It must not
ignore the structure or reinterpret it as a command string.

Roles: data, log, cache, spare, special, dedup. Layouts: stripe, mirror,
RAIDZ1/2/3. Cache/spare accept individual leaves, and non-data groups reject
RAIDZ. Mirror minimum two and RAIDZ minimum parity+one follow upstream
`cmd/zpool/zpool_vdev.c:is_grouping`. ashift is automatic (zero) or 9-16,
matching `include/sys/spa.h`. The service checks indices, bounded strings,
counts, exact duplicate paths and presence of a data group. It does not claim
that differently spelled paths cannot alias the same device; resolved identity,
pool-name validity, capacity, alignment, existing labels, feature support and
permissions remain mandatory engine checks. dRAID and larger configurations
need a future topology extension, not silent approximation.

The guided editor builds a draft, then populates pool.create with dry-run
enabled. Use draft does not submit. The regular Preview/Review change flow
lists the exact paths before confirmation. Cancel sends nothing. The draft
is retained on reopening; Clear fields clears the applied layout. Changing
roles/layouts selects the type for the next added group, not an existing group.

Never use `OK` to mean 'not implemented'. An unavailable engine does not mean
an empty pool list. `hello` and `capabilities` work now; all storage operations
return `OZ_NO_ENGINE`. OS-specific operations remain visible but must report
unsupported until implemented. The static property catalogue comes from the
pinned upstream source; it is not a claim that the backend supports them all.

The writable service capability bitset is derived from the audited public,
authenticated and project-worker routes. Every stable operation with one of
those routes is advertised, while the six explicit AROS-unavailable operations
(share/unshare, jail/unjail and zone/unzone) remain clear. The read-only service
retains its smaller mode-specific set. This prevents a newly wired operation
from working through Manager while remaining absent from `service.hello`.

## Ownership, timeout and shutdown

Requests are allocated with `AllocVec(MEMF_PUBLIC)` and transferred to the
service by `PutMsg`; the sender must not access them afterward. The service
transfers the response allocation to the named client port, or frees it if the
client has exited. Lookup+enqueue and port removal are protected by `Forbid`.
There are no client-private pointers or `mn_ReplyPort` lifetime dependencies.
The receiver releases messages with `oz_message_free`.

Each client has one outstanding request. Responses must match the request ID,
version, length and bounded row count. After three seconds the UI reports
**outcome unknown**; it does not automatically retry or claim cancellation.
Late replies are discarded; closing a UI does not cancel an engine operation.
Long operations must promptly return an explicit job ID, with progress/final
status available through jobs. Broker concurrency/job execution is a backend
integration requirement, not implemented by the no-engine adapter.

## Verification and current boundaries

`OpenZFSAPITest ABSENT` verifies missing service. Normal mode verifies hello,
capabilities, every storage operation's routing and parameter preservation,
version mismatch, unknown opcode, missing target and confirmation rejection.
It refuses the mutation-routing sweep unless the service explicitly identifies
itself as engine unavailable. `OpenZFSService TEST-DROP` uses a separate test
port and `OpenZFSAPITest TIMEOUT` checks non-response without stopping the GUI's
service. No command opens a device or imports a host pool.

This is an operational UI/API bring-up, not storage feature completion. The
engine integration, authorization, secure key prompting, dRAID topology,
fully typed variable-length parameters, deterministic recovery workflows and
live job/capability transitions need verification as the port lands. Do not
release it as a production storage administration tool yet.
