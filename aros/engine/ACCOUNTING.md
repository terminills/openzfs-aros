# Authenticated native accounting queries

## Native default quota enforcement

native-import-run.aSM5rlej exits zero using the existing full service and an
expanded actual DOS client. A separate quota-enforce filesystem is mounted,
each default quota is set to 1 through authenticated Dataset Set, and pool
sync precedes creation. User/group byte and object limits all reject Open
with ERROR_DISK_FULL (221). Clearing the active quota restores creation,
close and delete. Both project defaults permit project-0 creation: upstream
zfs_acl_ids_overquota explicitly exempts ZFS_DEFAULT_PROJID and invalid IDs.
The test checks this contract rather than changing engine semantics. All six
clear/create cycles pass, followed by unmount/destroy and the full native suite.

Retained failed run 0WqhIb8d incorrectly expected project-0 creation rejection;
its successful Open is explained by the upstream exemption. Nonzero-project
assignment/enforcement remains unverified by this fixture. This adds default
identity quota create-admission coverage, not write-exhaustion, explicit-ID
override, ordinary-user matrix or cold enforcement coverage. Existing
admission-service-test.c quota_io_test covers dataset quota/refquota write
exhaustion separately. Original fixture backing hashes remain unchanged.

## Authenticated quota setters

### Actual Manager setter acceptance

`AROS_TEST_ACCOUNTING_SET_GUI=1` with GUI=1, NATIVE_WRITABLE=1 and
ZVOL_TRANSPORT=1 pauses the native client for real Manager interaction.
There is no automatic setter fallback: after Manager closes, separate actual
CLI commands must read userquota@1000=2097152 and defaultuserquota=8388608.
The client then restores baseline values and runs the remaining regression.

native-import-run.6eTyhiAr exits zero with build
accounting-set-final.bh9TadUN. Actual Dataset Set clicks submit both mutations;
03-user-confirm.png and 05-default-confirm.png retain the confirmed target,
values and flags. 06-default-result.png shows errno=0 and both successful
requests in activity. The independent CLI checks pass, followed by the full
native CLI/zvol regression and normal final Manager close. Original backing
hashes remain unchanged. This proves the generic Dataset Set GUI route for
one per-user byte quota and one default; it is not every quota GUI combination.

Dataset Set (operation 41) now marshals upstream SET_PROP quota values for
numeric user/group/project IDs: all six byte/object quota kinds and all six
default quotas. It uses captured credentials and the upstream dispatcher,
including feature, delegation, target and read-only checks. `none` clears a
quota. Per-ID inherit and used-counter writes are rejected. Numeric IDs are
bounded to UINT32_MAX; name/SID resolution is not implemented by this adapter.
Exact default-property reads use the ZPL master node rather than DSL defaults.

Full build accounting-set-final.bh9TadUN passed writable native run
native-import-run.2m0DdtZK (exit 0). The client sets thirty user quotas, group
and project quotas, object quotas and six defaults through the Manager API;
independent accounting and actual CLI queries check the values. Unauthorized
mutation, delegated mutation, malformed IDs, overflow, used-counter writes
and quota inheritance are exercised. Clearing and restoring a user quota
passes both literal and human-readable CLI checks.

Retained failed run native-import-run.ePOddI5K reached successful setters and
default reads, then expected `none` from `zfs get -Hp`. Upstream libzfs emits
numeric `0` when literal mode is selected; the observed successful command
returned exactly that. The corrected test checks `0` with -p and `none`
without it. No service change was needed for that test failure.

Fresh cold read-only run native-import-run.15085cuW exits zero, rechecking
all twelve accounting metrics, the six exact default-property reads, actual
CLI accounting output, and EROFS for both per-ID and default quota changes.
Original backing files, retained writer overlays and read-only child overlays
preserve their recorded hashes. These are API/CLI runtime tests, not actual
quota-setting GUI interaction or quota-exhaustion enforcement coverage.
Offline control-integrity.ASrHn4Bl extracts both retained GPT members and
passes independent `zdb -bcc` with No leaks; member and original backing
hashes remain unchanged by verification.

Operations 67/68/69 now call upstream ZFS_IOC_USERSPACE_MANY for user,
group and project accounting with the captured caller credentials. They do
not execute commands or bypass upstream delegated read permissions. Argument
selects used (default), quota, objects or objquota. Flags, options and job IDs
are refused. Dataset snapshots/bookmarks are not accepted by this adapter.

Each structured accounting row contains numeric principal type/ID in name,
the upstream domain in parent, selected metric in state, bytes/objects in
value, the exact counter in used and (for used/objects only) upstream default
quota in total. Quota/objquota queries do not supply default quotas; their
total field is not a default measurement. These fields are not pool capacity.
Usage-query default quota is also in the response detail for empty pages.
Quota queries instead explain that defaults require a usage query. Error
responses do not fabricate a measured default. Oversized domains fail rather
than being truncated.
The upstream serialized cursor is opaque; pages are live, not an atomic
snapshot. A full final page can require a subsequent empty page to detect EOF.
No implicit accounting upgrade or feature enablement occurs on a query.

The Manager routes initial and subsequent pages through its authenticated
worker, renders counters/default quotas with units, and advertises all three
operations. This is not full CLI formatting/filtering/sorting parity, project
assignment administration or a dedicated quota editor. Dataset Set exposes
the quota-setting API described above; selected actual GUI setter acceptance
is recorded above.

## Verification

### Actual accounting Manager interaction

AROS_TEST_ACCOUNTING_GUI=1 alongside ACCOUNTING_PERSIST=read and GUI=1 pauses
the cold fixture for actual Manager input, then independently runs all twelve
API metric checks and the real CLI checks after normal Manager close.

Initial native-import-run.6eqC5FxE exits zero with actual user quota first/last
page, group used and project objects queries. Next page is enabled on page one
and disabled on the final six rows. The usage screens show 5242880 bytes group
default and 103 objects project default. This run exposed misleading quota-list
default-quota=0 labels: upstream returns no default for those metrics. Service
detail and Manager rendering now distinguish that from measured zero, and
exact native response tests enforce the revised text.

Full build accounting-display.XAJAZG7i supplies the rebuilt service/Manager
for fresh run native-import-run.c7Gs6a7x. Captures 02-users-page1.png and
03-users-page2.png show corrected quota rendering and pagination; capture
04-project-objects.png preserves the valid 103-object default. These are actual
authenticated UI responses, not client-generated screenshots or mock data.
After normal close the independent cold API/CLI checks pass; the final Manager
closes normally and the runner exits zero. Original backing, retained writer
and both readonly child overlays keep their hashes. No DUT or physical array
was touched.

### Cold persistence and real upstream CLI transport

AROS_TEST_ACCOUNTING_PERSIST=write/read retains the fixture alongside the
existing zvol-discard cold fixture. Writer native-import-run.hWuqvayW exits
zero after sync/export; a separately booted reader uses new overlays over
those exact disks, never their live service memory. Writer offline verification
control-integrity.vzuR69gc reports No leaks and unchanged verification inputs.

The first reader iIDyOmXG passed all twelve API metrics and two default-property
CLI reads, but failed projectquota@1000 readback: zfs get printed '-'. This was
a real integration gap, not lost quota data. The public CLI ioctl transport
had not admitted USERSPACE_ONE/MANY. It now admits only their audited inline
identity/metric/cursor fields and bounded output arrays, retaining upstream
credential-based permissions. Enumeration publishes the returned raw array
through the existing copied-output flag, because these records are not packed
nvlists. Single-record queries return the scalar in zc_cookie. No new client
pointers are trusted and no CLI binary replacement is required.

Full service/Manager cli-accounting.z7K0LGRi builds successfully. Fresh reader
native-import-run.pxeVpMRH exits zero: all twelve API selections and six
defaults survive cold import; real zfs get returns the project quota; real
zfs userspace lists all thirty user quotas; groupspace/projectspace return
exact 2/3 MiB quotas and object quotas 8/9, alongside the user object quota 7.
All original backing, retained writer and fresh readonly-overlay hashes match.
The accounting CLI result gate was added after this runner snapshot and its
exact marker explicitly checked in serial. The failed first reader is retained.
This is orderly cold persistence, not sudden power-loss or endurance proof.

### Populated metric and default-quota matrix

`native-import-run.SvUKrXUv` exits zero with the same service/Manager build.
The independent CLI sets groupquota@1000=2M, projectquota@1000=3M and user,
group and project object quotas 7/8/9. The API checks exact IDs, counts,
values and bytes/objects units. Existing thirty user byte quotas still pass
two-page root/delegated enumeration. Six explicit default properties are set
to 4/5/6 MiB and 101/102/103 objects; after pool sync, all six usage queries
return nonzero native accounting and exact default values in both structured
rows and response detail. Upstream returns defaults for usage queries, not
quota-list queries; the adapter preserves that distinction. These usage
records include filesystem metadata, not a measured user-payload benchmark.
The remaining native regression and original backing hashes pass. The new
metrics gate was added after the runner snapshot and its exact marker checked
in serial. This verifies all twelve query selections, not exhaustive identities
or cold persistence. No service change was needed for these additional tests.

Full service/Manager build manager-accounting.JnnK15cW succeeds.
Private QEMU native-import-run.PsqXTmal exits zero. Actual upstream zfs set
creates thirty userquota records, each 1 MiB; the new API checks all numeric
IDs exactly once across two pages as root and as a delegated ordinary user.
Unprivileged enumeration fails; userquota delegation does not grant
groupquota access; revocation denies a fresh query. Invalid metric fails.
Empty group quota, enabled-project quota and user object-quota queries pass.
Project accounting on the original feature-disabled dataset returns ENOTSUP.
Only the fixture then explicitly enables project_quota/userobj_accounting and
creates a new dataset. The API never upgrades it implicitly. Existing native
mirror/DOS/CLI/zvol regression and original backing-file hashes pass.
The mandatory accounting result gate was added after this runner snapshot;
its exact marker was explicitly checked in the retained serial log.

Retained failed run R3Sv8q2s correctly returned ENOTSUP for project accounting
but the original test incorrectly expected an empty success on the disabled
pool. No engine behavior was weakened to pass the corrected test.

Still unverified: domain identities, concurrent updates
between pages, the remaining accounting GUI option combinations, and exhaustive CLI
option/malformed-transport coverage. No DUT, user hosted session or
physical disk was accessed.
