# Force-create qualification in progress

The private QEMU baseline in `build-audit/public-service-run.KL3OjoXH`
reached `Public force-create prerequisites PASS`, preserving the source
payload across active and no-force refusals. It then failed at
`exported-force-create`, as expected with the previous service and CLI.
This is failure reproduction, not a passing acceptance test.

The current implementation transports an AROS-only boolean create intent
from zpool to the service, strips it before upstream creation, and requires
captured privileged credentials. Exported members must have four coherent,
checksummed labels. The live SPA namespace is checked even for exported
labels, since a read-only import can retain those labels.

The service cross-build passed using retained upstream engine objects and
rebuilt adapters; see `build-audit/force-create/service-build.log`.
The updated runtime test additionally checks read-only imported source
refusal and preservation. The rebuilt CLI and passing QEMU replay are
recorded below. This does not qualify arbitrary nonzero-media overwrite, Manager
force semantics, or force add/attach/replace.

## Current CLI build

All common CLI sources and dependencies were freshly compiled in
`build-audit/cli-probe.miMoVFxl`. The final link in
`build-audit/cli-link.dr92BqmY` passed for zfs, zpool, startup commands,
runtime check, and host identity. This uses the private pthread/CRT inputs,
not the blocked official-SDK qualification lane.

An intermediate link used a stale pipe2 import slot (793). Inspection of
the retained CRT's generated dispatch table established slot 799; the
private import was corrected and all commands relinked before runtime.
Neither intermediate CLI build was executed. Logs are retained under
`build-audit/force-create/cli-link*.log`.

The exported-member denial regression now checks omission of force, while
active-member denial still supplies force. The separate acceptance test
requires exported recreation to succeed and checks readonly imported
source preservation. Runtime results are recorded in
`build-audit/force-create/replay-matched.log`.

## QEMU result

The eight-core private QEMU run `public-service-run.PNPRlR82` exited zero.
It proved active and readonly-imported source refusal with payload retained,
no-force exported refusal, successful force recreation of exported file
members, absence of the old payload, and exact new payload after readonly
reimport. The complete public-service storage suite also passed. Retained
serial results and input hashes are in `evidence/force-create-20261026`.
The completed test ISO was hashed and pruned by the runner.

This is file-backed runtime qualification with private CRT/pthread inputs;
it is not native-device force recreation, physical durability, or full-port
completion. Manager force and arbitrary nonzero-media force remain open.

## Native virtual-device result

Run `public-service-run.GAiH0Pp2` also exited zero with eight virtual CPUs,
the native partition fixture, freshly rebuilt commands, and the opt-in force
test. `Public native force-create PASS` proves active and readonly-imported
refusal, exported recreation, old-payload absence, new payload verified after
readonly reimport, and cleanup on `/AROS/ata.device/0/partition/0`.
The complete public-service storage suite passed. Evidence is retained in
`evidence/native-force-create-20261026`; the completed ISO and disposable
disk images were hashed and pruned by the runner.

An earlier launch stopped before QEMU because the default SDK path lacked
partition.library. The successful retry explicitly selected the clean ADT
SDK. This remains QEMU native-device API evidence, not physical hardware
durability or controller qualification.

## Confirmed Manager API result

The initial typed-force test `public-service-run.HYfiw777` failed because
authenticated admission did not yet permit create plus confirmed force.
Its serial results are retained in the Manager evidence directory. Enabling
that exact flag combination, while retaining credential and backing-member
checks, allowed `public-service-run.hGXGYDIR` to exit zero.

`Public Manager force-create PASS` verifies confirmation and force are both
required, active and readonly-imported pools are refused, exported recreation
succeeds, old data is absent, and new data survives readonly reimport. The
complete storage suite also passed. Evidence and binary hashes are in
`evidence/manager-force-create-20261026`.

The Manager binary cross-build passed with an added overwrite warning in
request review. The visual dialog has not yet been requalified; the result
above exercises its authenticated API, not mouse-driven interaction.
Arbitrary nonzero-media force and force add/attach/replace remain open.

## Manager visual result

Private PS/2 QEMU run `native-gui-run.G1sRAZ8x` displayed the rebuilt
Manager's Advanced/Pools/create preview with Force selected. The captured
preview explicitly says nothing was submitted and displays the exported-pool
overwrite/data-loss warning and backup instruction. The preview used a
nonexistent RAM path and was not submitted. This is UI rendering evidence,
separate from the authenticated API write tests above.

The Manager was closed normally through its close gadget. The acceptance
client resumed, readonly reimport and DOS round trip passed, and the runner
exited zero with `RESULT public service storage PASS`. Screenshot, QMP
actions, serial results, hashes, and pruned-image record are retained in
`evidence/manager-force-visual-20261026`.

## Manager native-device API result

Run `public-service-run.dfUqNFqx` passed the native partition fixture through
the Manager's typed authenticated create API as well as the CLI. It refused
active and readonly-imported pools, omitted confirmation, omitted force, and
the unsupported dry-run flag combination. Confirmed forced recreation then
succeeded; the old payload was absent, and new data survived readonly
reimport. Cleanup and the entire storage suite passed, with runner exit zero.
Evidence is retained in `evidence/native-manager-force-20261026`.
This tests a virtual ATA partition, not physical controller behavior.

## CLI force-add result

Commit 011260e carries an explicit transient add-force boolean from upstream
zpool's force/allow-in-use selection into service admission. It is removed
before the upstream ioctl. The retained member checks accept coherently
exported labels only when requested, while retaining live-namespace checks,
captured caller authority, topology policy and member reservations.

Run `public-service-run.cAcAgy2H` exited zero using the rebuilt service and
`cli-link.EOrlHpek`. It refused no-force exported addition and forced use of
a readonly-imported source, then successfully added the exported mirror
to a separate pool. Existing target payload remained exact before and after
readonly reimport; health was ONLINE. Full public-service storage passed.
Serial results, inputs, and pruned-image records are in
`evidence/force-add-20261026`.

This qualifies CLI file-backed force-add. Native force-add, the typed Manager
add path, force attach/replace, potentially-active labels, and arbitrary
nonzero-media force remain unqualified or unimplemented.

## Manager force-add result

The first typed add run (`qf2oSc43`) reached upstream but returned
ZFS_ERR_ASHIFT_MISMATCH (1050). The typed force path still passed true for
spa_vdev_add's alignment check, unlike upstream zpool add -f. The corrected
path passes false only for explicit force and retains all caller/member
admission. The UI review warns that force permits differing ashift values.

The corrected run `public-service-run.aZ3SYiLI` exited zero. The Manager API
required confirmation and force, refused active and readonly-imported
source members, added exported members, and preserved the target payload
through readonly reimport. The full storage suite passed. Both the failure
and passing evidence are in `evidence/manager-force-add-20261026`.
The changed UI wording still needs a fresh visual run.

## Native force-add CLI and Manager result

Run `public-service-run.sOg7EliC` exited zero with both CLI and Manager native
force-add markers. Each lane used the disposable ATA partition fixture,
refused active and readonly-imported source members, refused exported reuse
without force, added the exported member, and verified exact target data
after readonly reimport. Cleanup and the complete storage suite passed.
Evidence is in `evidence/native-force-add-20261026`.

Force create/add now have file and virtual-native-device runtime evidence
through both interfaces. This does not close force attach/replace,
potentially-active or arbitrary nonzero-media admission, physical failure
qualification, or the remaining full-port requirements.

## CLI force attach/replace and DOS alias lookup

The initial run `public-service-run.ogJJTcex` failed to locate the existing
member: `/RAM/...` did not match the canonical RAM Disk path stored in the
pool. A GUID-based replay (`NBvxLcKg`) passed, isolating the path lookup
problem from protected force admission. For native AROS file clients,
absolute path mismatches now compare existing DOS canonical identities via
Lock/NameFromLock. Native device names and missing-path literal matching
are unchanged; this is not a general relative-path alias qualification.

Fresh CLI compilation/link and the rebuilt acceptance client passed in
`public-service-run.EWS7gGdC` with the normal `/RAM/...` command arguments.
Attach and replace each refused active, readonly-imported, and no-force
exported reuse, then accepted explicit exported reuse. Resilver completed,
six ONLINE leaves were verified, and target data remained exact following
readonly reimport. The complete storage suite and runner exited zero.
Evidence and the initial failure are retained in
`evidence/force-attach-alias-20261026`.

This uses private CRT/pthread dependencies and an adapter rebuild over
retained upstream engine objects. Manager and native-device force
attach/replace, arbitrary nonzero-media force, official SDK qualification,
and physical failure testing remain open; this is not full-port completion.

## Manager file-backed force attach/replace

`public-service-run.TjFqAZ52` passed both operations through the Manager's
authenticated typed API, targeting member GUIDs as the UI does. Missing
confirmation, missing force, active donors, and readonly-imported donors
were refused. Explicit exported reuse completed, with seven ONLINE leaves
after the added mirror member and subsequent replacement, exact target
payload, and readonly reimport/write-refusal checks. The unchanged CLI
cases and full storage suite also passed; runner exit was zero.
Evidence is in `evidence/manager-force-attach-20261026`. This is API runtime
coverage, not a fresh mouse-driven UI test or native-device qualification.

## Virtual-native force attach/replace

`public-service-run.xpDq4al7` exited zero with all four CLI/Manager and
attach/replace combinations passing. The runner's disposable 256 MiB ATA
MBR fixture supplies the candidate partition; a RAM-file target provides
the existing pool data. Active and readonly-imported candidates, and
exported candidates without force, were refused. Manager cases also
require confirmation. Forced exported reuse completed with resilver waits,
exact ONLINE leaf counts, exact target data and readonly reimport/write
refusal. Each pool was destroyed normally before the next case, without
harness label erasure. The full native discovery and storage suite passed.
Evidence is in `evidence/native-force-attach-20261026`.

Create/add/attach/replace now have exported-reuse evidence through both
interfaces on file and virtual-native candidates. This does not qualify
all-native topologies, physical devices, arbitrary nonzero-media force,
potentially-active labels, or the official SDK/runtime dependency set.
