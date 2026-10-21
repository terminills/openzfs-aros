# Native pool capacity and I/O statistics

## Pool list capacity

Operation 3 now samples each imported pool with upstream `spa_get_stats()`
after releasing the namespace enumeration lock. For `pool` rows, `total` is
root-vdev `vs_space`, `used` is `vs_alloc`, and `available` is their difference.
The response also labels these values as size/allocated/free bytes. These are
raw pool statistics, not a dataset's available space or a sum of leaf sizes.

The owned config snapshot is freed after each sample. A missing/invalid sample
or a pool disappearing during enumeration produces an `UNAVAILABLE` row with
an errno, zeroed numeric fields, and an overall error response; those zeroes
are not measured capacity. Pagination remains a live view, not an atomic
multi-pool snapshot. Filters and list options remain unsupported.

Native QEMU run `native-import-run.04LUazz0` passed the full selected transport
regression and preserved both original fixture hashes. Its client checks a
nonzero allocated/free sample and compares size against real upstream
`C:zpool list -H -p -o size qemu_mirror` output. The comparison helper uses a
substring check; it is not an exact output parser.

The rebuilt final variant in `native-import-run.QvGenWpJ` additionally sets
the explicit unavailable state. Its actual Manager capture
`02-pool-capacity.png` shows size 251658240, allocated 2711552 and free
248946688 bytes. Response/activity save actions were sent to the guest; their
temporary files were not extracted, so the retained visual proof is the
screenshot, not a claimed saved-text artifact. Healthy-pool sampling is runtime
verified; the statistics-error path has source review but no injected-error
runtime proof yet. This final run completed the selected native transport
regression, exited zero and verified unchanged original fixture hashes.

## Pool I/O counters

Operation 35 now reads upstream `spa_get_stats()` counters for one imported
pool and its topology. It does not run a shell command, discover devices or
import storage. Read-only query policy matches existing pool.status; arguments,
options and mutation flags are refused. Cursor pagination follows the same
live topology traversal, including log/cache/spare and allocation-class roles.

Each vdev produces two typed rows with the existing API wire layout:

| kind | used | available | total |
| --- | --- | --- | --- |
| io-bytes | cumulative read bytes | cumulative write bytes | elapsed nanoseconds since vdev load |
| io-ops | cumulative read operations | cumulative write operations | elapsed nanoseconds since vdev load |

These fields are **not capacity** for these row kinds. Name, parent, health,
type, role and GUID identify the sample. Byte/operation rows for a vdev share
one statistics snapshot. Stored-only inactive auxiliary devices report
UNAVAILABLE; zero-filled fields in those rows must not be displayed as measured
zero I/O. Parent/child counters must not be summed. Reopen/import may reset
counters, and separately requested pages are not a frozen snapshot.

The Manager renders explicit read/write bytes or operations and elapsed ns.
Repeated queries now also report interval-average bytes/s or operations/s,
with their measured interval in nanoseconds. These are separate from cumulative
counts, never capacity/progress or instantaneous rates. No protocol layout or
version change was needed: row metadata now includes upstream spa_load_guid.

Each sample is matched by pool, metric, name, parent and full identity metadata
(including vdev GUID, role and pool load GUID). First samples, changed identities,
counter/time rollback and bounded-history eviction establish a baseline instead
of inventing a rate. Equal timestamps cannot yield a rate. UNAVAILABLE devices
invalidate matching samples; missing/invalid load IDs or timestamps report
unavailable. Reconnect and error replies clear UI history. Rates use each
vdev's own elapsed timestamp and cannot be summed across parent/child rows.
The optional Auto I/O refresh (2s) control repeats only operation 35 after a
complete successful single-page query. It binds the sampled pool name, sends
no flags/options/mutations, and waits while any client or authenticated worker
is busy. Clock rollback rearms the deadline; missed periods never produce a
catch-up burst. Leaving the statistics view, editing the pool name, connecting,
paging, closing, or any error response disables refresh. Multi-page automatic
refresh is deliberately not enabled yet; manual paging remains available.

## Evidence

- `native-import-run.5vi872eM`: full native suite passes scheduler tests for
  busy suppression, two-second deadlines, clock rollback, missed-period
  suppression and explicit stop, alongside the nonzero live rate tests.
- `native-gui-run.YrhNxVBL`: actual checkbox refuses enabling before a
  successful query (02), then sends recurring read-only queries (04) with
  measured intervals around 2 seconds. Editing the pool stops it; captures
  05/06 are byte-identical across a further interval. Leaving the view for
  authenticated export stops it again. After export, deliberately re-enabling
  the old sampled pool produces one ENOENT reply and stops; captures 09/10
  remain byte-identical after another interval. Saved response contains all
  eight rate rows, saved activity preserves automatic requests and final error,
  and orderly close reports GUI PASS with the pool absent. This is not a
  service-restart or simultaneous topology-change stress test.
  The rate-aware verifier passes, including independent zdb of extracted GPT
  members and unchanged disk hashes (`iostat-offline.OXvk1gpg`).

- `native-import-run.IqmNjn30`: complete native CLI suite passes native rate
  arithmetic tests (known deltas, equal/reversed clocks, counter rollback,
  changed generation, unavailable metrics, bounded eviction and maximum 64-bit
  counters without multiplication overflow). Real service queries sample all
  eight rows of an imported mirror before and after upstream dataset creation
  and pool sync. Both virtual-device leaves show positive write deltas and formatted
  interval rates. The temporary dataset is destroyed. Full runner exits zero
  and original backing hashes match. The earlier `sPBMIPJ3` also passes the
  full suite but its idle live samples are zero; it is not nonzero-I/O proof.
  Actual GUI `native-gui-run.x5D1rhtT` shows baseline on the first query
  (07-baseline.png), then bytes/s and operations/s with intervals on the next
  (08-rates.png). Its idle samples are zero, not nonzero-throughput evidence.
  Saved full response contains all eight rate rows. Authenticated GUI export
  succeeds and the launcher reports PASS; QEMU exits normally. Verification
  with AROS_VERIFY_IO_RATES=1 additionally requires those rows before offline
  integrity checks. The original verifier expected whole-device leaf names;
  this fixture uses explicit partition/0 paths, now also accepted exactly.
  The whole-disk zdb attempt likewise found no pool: the members are GPT
  partitions. The verifier now validates single ZFS GPT extents and extracts
  them into a fresh verification directory. `iostat-offline.xbDkFALB` passes
  zdb with No leaks and unchanged whole-image hashes; the original failed
  iostat-zdb-check.log remains intact.
  Retained CNjfXqg1 and ASTPysJ4 failed before Manager launch because Wanderer
  contended with the harness's publication-lock test. The export startup now
  uses the existing delayed-desktop launcher path and a rebuilt API3 launcher;
  production mount locking is unchanged. Early field-entry captures in the
  successful run preserve the invalid-input attempts, not successful queries.

- `build-aros/admission-service-run.PTgXXutT`: complete native WRITE/cold
  read-only VERIFY suite passed. Separate native client checks the eight rows
  for root, mirror and two leaves; positive real leaf reads and writable-run
  writes; matching timestamps within each pair; cursor 3 (inside a pair), cursor
  8 (end), monotonic subsequent samples and invalid request rejection. Existing
  DOS/API regression, read-only image hashes, offline zdb no-leaks, compression
  and atime persistence verification also passed.
- `build-aros/native-gui-run.e8FEppVv`: actual native Manager pool.iostat request
  succeeded. `03-iostat.png` and `04-iostat-counters.png` show live rendering.
  Saved full response/activity are in serial.log, including both leaf counters.
  Authenticated export removed the pool from the imported namespace and the
  launcher reported PASS. QEMU exited normally before offline verification.
- `bash aros/ui/verify-iostat-ui.sh build-aros/native-gui-run.e8FEppVv` passed:
  saved GUI numeric/units checks, export completion, offline zdb no-leaks and
  unchanged hashes during offline inspection. Screenshot inspection is separate
  from the text verifier and was performed on the actual captured UI.

Build/run logs: `build-aros/typed-import-service/iostat-{build,client-build,
manager-build,run-1,gui-run-1,gui-verify}.log`. Each run freezes source and binary
inputs and keeps their hashes. All storage was private QEMU fixture copies;
neither DUT nor the user's hosted session was used.

## Remaining coverage and functionality

This is the cumulative-counter API/UI foundation, not full parity with all
zpool iostat options. Latency/size histograms, queue statistics,
grouping/filter controls and wide multi-page automatic sampling
remain work. The generic configured-only auxiliary path is implemented but
not specifically exercised by this mirror run. Reset handling has deterministic
native coverage; concurrent topology changes, cold-service restart while the
Manager remains open, and wide multi-page workloads still need end-to-end tests.
