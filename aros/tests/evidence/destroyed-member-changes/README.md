# Destroyed-member add, attach and replace

The backend now admits consistently destroyed members for add/attach/replace,
using the same four-label checksum/state/pool-GUID/member-GUID checks as create.
Caller access, retained identity/reservation checks and writable handoff remain
in place. Arbitrary nonzero, active and exported members are not force-cleared.
The typed Manager path and upstream CLI share the same admission rules; the
Manager help now describes the expanded policy.

## Runtime evidence

The former `shared-engine.f08bQXlx` fails destroyed-member attach in eight-CPU
QEMU run `public-service-run.H6E3JjwM` after successful source-pool destruction.
Final fresh source `source-engine.KNH9Dct2` and shared engine
`shared-engine.rIPiaYWt` pass:

- `public-service-run.EWrdxiaM`: native-partition/encrypted-root regression and
  the new file-backed lifecycle, then full storage and terminal runner PASS.
- `public-service-run.92Psc8yx`: the same file lifecycle plus persistent mirror
  healthy/degraded/restored cold imports, exact reads, unchanged member/archive
  hashes and independent host zdb with no leaks. Terminal runner PASS.

Each lifecycle uses five newly allocated private RAM files. It creates a
source mirror, a target single-device pool and a second donor mirror. All three
operations must refuse the source members both while active and after export
(CLI attempts include force). The target topology and exact data are checked
after each group of refusals. After reimport and explicit destruction of the
source, attach produces a mirror, replace retires the attached member, and add
consumes the explicitly destroyed donor mirror as a second mirrored vdev.
Final online leaf counts and exact target data are checked after each operation.
The entire sequence runs once through upstream zpool and once through the
authenticated Manager request API, followed by cleanup.

Replacement waits for both `resilver` and `replace`, not merely the resilver.
An earlier test stopped at the topology check because replacement retirement
had not completed; it did not report a payload mismatch. Earlier import setup
also omitted `cachefile=none`. These setup failures are retained separately,
not misclassified as engine regressions. Another initial test used noncanonical
member spelling; final CLI operations use the exact queried member GUID.

## Limits

These are file-backed add/attach/replace proofs through the API, not GUI clicks
or native-device reuse proofs for every operation. Native partition creation,
encrypted-root behavior and refusal regressions pass in the same engine.
The RAM reuse fixture is not carried across boots: the cold evidence covers
the separate persistent mirror, not this newly expanded target pool. Native
device reuse for all operations, cold expanded/replaced fixture recovery,
physical durability and external-writer races remain separate qualification.

Follow-up `../changed-pool-cold` now qualifies the retained changed fixture
across a separate VM boot, including degraded reads and unchanged member bytes.
It also records the additional FAT prerequisite uncovered by that workload.

Follow-up `../native-member-changes` qualifies native partition candidates
for all three operations through CLI and Manager API in mixed native/file
pools, using four fresh isolated VMs. The historical limits above describe
this original evidence set, not that later coverage.

The existing `mmake-bundle.0uirh9w0.tar.xz` contains the checksum fix but predates
this expanded add/attach/replace policy.
