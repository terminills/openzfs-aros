# Upstream engine bootstrap

## Open replacement failure: reproduced and repaired (2026-09-30)

Current status: commits `df08f57` and `17b5fdc` replace the unlink/create
sequence with checked upstream atomic rename and old-object reclamation.
`qemu-run.i2NGRPZq` passes all five boots, including original preservation,
generation and authorization rejection, and hard-link/reclamation tests.
Normal service regressions `hosted-security-test.lSnWpcBG` and
`public-service-run.nmZpiQ8M` pass. Exact shared-bundle qualification and
post-publication error-boundary diagnostics are tracked separately below.
The following is the retained pre-fix failure record, not current behavior.

Diagnostic `qemu-run.nrB3QmWs` fails after injecting ENOSPC into
oz_zpl_create_as during authenticated MODE_NEWFILE replacement. The earlier
handle-allocation failure still preserves the original; the creation-failure
case reports `hits=1 result=5 original-preserved=0`. The production handler
at that revision unlinked the original before creating its replacement. Thus a create
error can lose the original name/data; the prior pre-allocation test did not
cover this boundary. The failure is on disposable QEMU data, not the DUT.

The original diagnostic deliberately required preservation and failed.
The fix requirements were to retain native
write/delete protection, upstream ACL checks, hard-link semantics, exclusive
open checks and default protection on the new file. It must also preserve the
original when preparing the replacement fails and handle publication/sync
errors without deleting a replacement already published. Merely creating a
temporary file before a separate unlink/rename would leave another failure
window and is insufficient; the upstream atomic rename/transaction path needs
to be used with appropriate target identity and authorization checks.

Logs: `replacement-before-build.log`, `replacement-before-qemu.log`.
Only the create boot ran; reused earlier import/reopen diagnostics were never
executed. Allocation fault hooks remain private-diagnostic-only.

## Diagnostic handler allocation failures (2026-09-30)

`AROS_TEST_DOS_ALLOC=1` is accepted only for the private mirror diagnostic
executables. It redirects calloc in dos-handler.c alone to an owner-task
countdown injector; other engine allocations are untouched. The first two
shared-signal mount allocations (mount object and message port) are failed
individually. Tests require ENOMEM, NULL output, no published DOS entry and
successful mount/unmount after disarming the injector. The borrowed signal
remains owned by the caller throughout.

`qemu-run.ZRSbtCZW` completes the native five-boot mirror suite, with both
failure markers in create, cold import and either surviving-member boot;
backing hashes remain unchanged. The runner now requires all eight markers
when the diagnostic build record is present or the option is requested.
Stricter repeat `qemu-run.2rXgZVOX` also exits zero, verifies all eight markers
and preserves backing hashes.
Negative `qemu-run.xqnyh9Ry` requests allocation coverage with ordinary
binaries: filesystem phases complete, but the runner exits 1 because the
injection markers are absent. This is an expected verifier failure, not a
filesystem regression. The runner now prints the missing boot/site explicitly.
Extended `qemu-run.yygOLCQe` also passes both allocations for startup adoption
of a caller-owned DOS device. Failure leaves that exact device, name, type,
inactive task and startup-message pointer intact and publishes no volume;
retry succeeds and joined unmount clears the task without freeing the caller's
entry. All sixteen markers across four handler boots are mandatory. The full
five-boot runner exits zero with unchanged backing hashes. The initial
compile-only candidate had a redundant RemDosEntry in test cleanup, corrected
before boot in `allocation-adopt-2`; neither changes production code.
Logs: `allocation-adopt-build-2.log`, `allocation-adopt-qemu.log`.
`qemu-run.ZznKptrK` adds a separate client task making real DOS Lock calls:
the injected lock-allocation failure returns ERROR_NO_FREE_STORE, followed
by successful retry, Examine and UnLock. All twenty markers (mount ownership
paths plus lock request across four boots) are required. The five-boot suite
exits zero with unchanged backing hashes. Logs use `allocation-lock-`.
`qemu-run.0xKdd5G4` additionally fails the first handle allocation during an
authenticated MODE_NEWFILE open of an existing disposable file. The real DOS
client receives ERROR_NO_FREE_STORE, reopens read-only, checks every original
byte and EOF, and closes successfully. Independent ZPL lookup confirms the
original object ID and size. The security-enabled runner requires this marker
in addition to the twenty prior markers; all five boots and backing hashes
pass. Logs use `allocation-output-`. This covers pre-mutation allocation
failure, not I/O failure after unlink or transaction assignment.
Production service linking refuses this option, and staging rejects its hook
symbol. Normal shared engine `shared-engine.nXkmsFY7` has no such symbol.
Logs use `allocation-failure-` and `allocation-service-refusal` prefixes.

This is targeted deterministic failure injection, not general memory-pressure,
MakeDosEntry/CreateMsgPort failure, allocation leak accounting, or concurrent
failure qualification. The normal handler code is unchanged by these tests.

## Dynamic DOS alias collision (2026-09-30)

AROS AddDosEntry permits duplicate volume labels. Dynamic OpenZFS mounts
previously relied on that call to reject collisions, unlike startup mounts,
so a dataset alias could shadow an existing foreign volume without a matching
device. Both paths now check volume names under the publication lock before
publishing anything. A collision returns EEXIST and leaves the foreign entry
and namespace intact; the caller may choose another alias.

Native negative `qemu-run.W9AIebd4` reproduces acceptance of the conflicting
case-insensitive name (`rejected=0`). Its first boot fails as expected; the
reused older cold-import/reopen binaries were never executed. Corrected
`qemu-run.MDOhe5Fb` passes the five-boot mirror suite with new binaries:
collision refusal, preserved foreign entry, no leftover device and successful
mount after removing the conflict, across creation and cold/degraded imports.
Original backing hashes remain unchanged. Both evidence sets are registered
to the development test archive; logs use the `volume-collision-` prefix.

Normal full-dispatcher services were rebuilt as `volume-production` in the
native P2 and hosted v6 engine trees. Native `public-service-run.JcDCaCcx`
and private hosted `hosted-security-test.uMft7hfN` regressions exit zero,
including loader policy; hosted persistence, cold/degraded reads and runtime
checks also pass. These use explicit private runtime prerequisites and do not
qualify stock nightlies or physical-device durability. Logs use the
`volume-production-` prefix. The earlier packaged shared engine predates this
fix and must not be presented as containing it.

This directory builds real upstream OpenZFS sources for the AROS ABI. It does
not redirect storage operations to Linux or to AROS's private-format handler.

## Private hosted authentication prerequisite (2026-09-29)

`build-hosted-security.sh` compiles the hosted platform's explicit 66-unit
security module list with its matching SDK in a new private output directory.
It does not modify either platform tree or install into a running session.
`hosted-security.MmebctgP` builds successfully; `test-hosted-security.sh`
clones a disposable runtime and uses its own Xvfb/AROS process group with
fixture-only empty-password accounts. `hosted-security-test.oavwWX3e` proves
configured security, login as UID77/GID88 and quiet logout restoring the prior
owner; input hashes pass. Evidence is registered to the development test archive.

Earlier `hosted-security.ivuJbk28` failed because the private build referenced
an ungenerated deflibdefs wrapper; defining LC_LIBDEFS_FILE directly fixed it.
`hosted-security-test.khwqVACE` then reached configured security but timed out
in the probe's interactive `secLogoutA(NULL)`; using the documented secT_Quiet
logout fixes the probe. Both failures remain retained. Logs are
`hosted-security-build-{1,2}.log` and `hosted-security-test-{1,2}.log`.

The hosted kernel already links task.resource (Task_InitLib and task hook
entry points); absence of a separate resource file was not a missing runtime
dependency. This test establishes authentication only, **not** successful
authenticated OpenZFS service/GUI mutations or general hosted security
qualification. Never deploy its fixture accounts or runtime.

The optional `AROS_TEST_HOSTED_SERVICE_BUILD` selects a matching hosted full
service build and runs the existing `OpenZFSPublicServiceTest STORAGE` (no
partition fixture marker or host device access). Build
`source-engine.v6hMwWvC/authenticated-service` and run
`hosted-security-test.vmakODoP` pass authenticated startup, session ownership
and task-exit reclamation, RAM-file mirror creation, DOS write/read, export,
readonly reimport and write refusal. Set `AROS_TEST_HOSTED_SERVICE_MODE=GUI`
to open the actual Manager mid-test; close it normally to finish validation.

GUI run `hosted-security-test.u8ceazjO` additionally verifies a real confirmed
snapshot request for `public_mirror/files@hosted_gui` (errno=0), followed by
dataset enumeration returning that snapshot (one row). Screenshots
`snapshot-confirm.png`, `snapshot-result.png` and `snapshot-query.png` capture
the named AROS window. After normal close, export/readonly reimport and DOS
readback passed, host exit was zero and input hashes matched. The prior GUI
attempt `ehdw9KwE` hit the original 300-second harness deadline before submit;
GUI mode now permits a bounded 900-second interaction window. Both runs are
registered. This establishes one hosted GUI mutation, not all operations or
cold persistence: these vdevs live only in that guest's RAM filesystem.

### Persistent hosted GUI and independent cold validation

Set `AROS_TEST_HOSTED_PERSISTENT=1` to provision two new 128 MiB files in
the private runtime through the explicit hosted `EMU:` device. The fixture
does not accept arbitrary paths or overwrite an existing pool directory.
The cold `STORAGE-COLD` mode cannot create/truncate files; it imports read-only,
reads the exact DOS payload, requires write refusal and exports. GUI mode
additionally requires `public_mirror/files@hosted_gui` to be present; only the
real Manager creates that snapshot. Save its activity/response before closing
to retain those files alongside screenshots.

With `source-engine.v6hMwWvC/persistent-query-service`, run
`hosted-security-test.jYIpzmlR` passes the actual GUI mutation, normal close,
independent host `zdb` (snapshot exists and No leaks), and a fresh hosted
readonly boot finding both the snapshot and exact DOS payload. Both member
hashes remain unchanged across offline verification and cold boot. The host
test exits zero. `Ygns7pFV` separately passed the non-GUI persistent variant.
Logs: `hosted-persistent-gui-test-2.log`, `hosted-persistent-service-test-3.log`.
Evidence and screenshots are registered to the development test archive. This is file-backed
software persistence, not physical flush/power-loss durability or exhaustive
GUI operation coverage.

Retained attempts: `g0uPVDJh` rejected `SYS:` because pool creation currently
admits explicit device/volume names, not assigns; that assign-support gap is
not fixed by using `EMU:`. `Phw0XWC4` waited on an insert-volume requester for
the incorrectly inferred `EMU0:` name (cancelled normally, not a lockup).
`xswhkI7P` created the GUI snapshot and passed independent zdb verification,
but the cold probe mistakenly routed enumeration through mutation admission;
using the normal query API fixed the probe. Its member hashes were unchanged.

Native compatibility regression `public-service-run.XMlmRHFk` passes the
original service lifecycle, authenticated control/session and RAM-backed
mirror DOS tests with the updated client and no hosted marker. It exits zero;
see `hosted-fixture-native-test-1.log` and registered the development test archive evidence.

## Typed pool file-vdev assign resolution (2026-09-29)

The typed management lifecycle now retains the original packet security
context through create/add/attach/replace and import. File paths use the
existing credential-aware namespace resolver instead of rejecting every DOS
assign. Traversal and file access are checked against that retained caller;
root authority, SameLock duplicate identity detection, complete blank-file
scanning and canonical path admission remain required. The resolver rejects
our own handler before sending packets and never implicitly starts handlers
or displays media requesters. The public mount-control callback also passes
its captured context through the internal lifecycle interface.

`hosted-security-test.PPCmx2DU` passes `AROS_TEST_HOSTED_PERSISTENT=1` plus
`AROS_TEST_HOSTED_ASSIGN=1`: typed creation and cold readonly reimport through
`SYS:`, independent zdb No leaks, exact DOS payload and unchanged member
hashes. It rejects duplicate files reached through different bound aliases
and an alias into the service's own mounted pool (EDEADLK), then continues
servicing requests. Native public suite `public-service-run.Hl2SHa8n` passes
the same negative checks and original lifecycle/RAM mirror regression.
Logs: `hosted-assign-policy-test-1.log`, `native-assign-policy-test-1.log`.
Evidence is registered to the development test archive. Initial positive run `yWxvWPfo` is also
retained; pre-fix `g0uPVDJh` documents the original SYS rejection.

Wider native regression `native-import-run.a2txZ46l` completed successfully
with the normal `source-engine.P2gN7Bsv/assign-policy` service. The runner
verified writable mirror DOS write/flush/export/reimport, CLI integration,
protected startup import and read-only import; both original raw backing
images retained their SHA-256 hashes. See `native-assign-regression-1.log`
and the registered the development test archive serial log. This used private QEMU disk overlays
and the recorded POSIX/pipe runtime overlays, not the DUT.

The upstream CLI request path now passes its retained packet security context
through create/add/attach/replace, discovery and import preparation. The same
resolver is mandatory for all file-vdev pinning; there is no context-free or
daemon-identity fallback. Worker jobs retain the context until completion, and
synchronous requests release it only after dispatch. No public wire ABI changed.

`native-import-run.lEoHDAa8` passes the wider native CLI/replication and writable
mirror regression with this interface; original backing hashes are unchanged.
`hosted-security-test.6ajqxqND` passes authenticated SYS creation, alias rejection,
offline zdb and independent cold read-only DOS readback with unchanged hashes.
Logs are `native-cli-assign-test-1.log` and `hosted-cli-assign-test-1.log`, with
evidence registered to the development test archive. These runs do not specifically establish every
CLI alias spelling: positive deferred/multi-assign and degraded missing-member
alias combinations still need separate operation-level coverage.
This does not establish physical durability or complete filesystem readiness.

Follow-up coverage: `native-import-run.J6Mp6XCW` enables
`AROS_TEST_CLI_POOL_CREATE=1` and verifies the exact CLI alias/create/blankness/
destroy markers with `cli-link.AtAI3CKP`. AROS now keeps the resolved vdev path
instead of upstream's Linux udev-oriented restoration of the supplied alias.
The assign is removed immediately after creation; subsequent operations use
canonical identity successfully. `probe-link.sh` recompiles this source rather
than silently reusing its initial probe object. The preceding enabled run
`ZGNyQYqG` retains the permission-denied failure before this fix.

Correction to earlier reporting: `L0EeHVBc` passed the general regression only;
the opt-in pool-create test was disabled. Its evidence description is corrected.
Commit `51149ff` added the test but was not itself proof of alias acceptance.

`hosted-security-test.t1r9GiVr` verifies direct typed deferred-assign creation,
multi-assign import past an empty first directory, and nonbinding-assign import
in the independent cold process. Assigns are removed after admission. Exact
payload, read-only write refusal, offline zdb and unchanged member hashes pass.
Earlier `W7SmMQv2` rejected the fixture's exclusive CreateDir lock with EBUSY;
the fixture now obtains a shared lock before adding the assign. These results
cover those three named paths, not degraded missing-member alias combinations.

Degraded follow-up `hosted-security-test.ewK7lfhS` passes cold read-only import
through a nonbinding assign with disk1 temporarily renamed out of scope. It
reads the exact DOS payload, refuses writes, exports, restores the original
filename and verifies both hashes unchanged. `qFlCbkMp` retains the preceding
discovery EACCES failure: missing members incorrectly retained alias spelling
after their parent had already been resolved. The fix keeps the checked
canonical parent plus the exact requested leaf; it does not admit siblings or
case-folded leaf alternatives. The persistent runner requires the new degraded
success marker. Native `native-import-run.fcYVKeQL` also passes the enabled
CLI pool-create and writable mirror regression with this change. Logs:
`hosted-degraded-assign-test-2.log`, `native-degraded-assign-test-1.log`.
This is one absent-member read-only recovery path, not writable degraded repair
or arbitrary multi-assign ambiguity resolution.

## Native write publication locking (2026-09-26)

Native CLI replacement exposed false raw-write failures while ordinary DOS-list
readers were active. `native-import-run.aKpASodj` failed: the new GPT partition
had 12 write errors and became FAULTED, so replacement did not finish. Bounded
failure-only diagnostics in `32WJRZvn` identify `dos-list-contention`, EBUSY,
on aligned, in-bounds requests. That repeat eventually completed and its old
runner passed, but two rejected writes mean it is not a clean-I/O acceptance
result. Scan-summary zero errors alone does not establish healthy device I/O.

The write interval now matches AROS AddDosEntry's shared list + exclusive entry
publication lock order. The shared list excludes RemDosEntry's exclusive lock;
the entry lock excludes AddDosEntry. Both remain nonblocking and all locks are
released on failed acquisition. Ordinary list readers no longer cause false
write failures. Claim checks and permanent revocation on recognized competing
DOS ownership remain unchanged; no physical or external-writer ownership claim
is added. The adapter reports at most 32 I/O failures, with stage and extent,
and emits nothing for successful transfers.

`device-range-run.Cx01jjnl` passes the strict native cross-build and complete
isolated QEMU runner, including fresh cold GPT verification and unchanged raw
backing hashes. Direct two-task tests prove exclusive list and entry owners
refuse writes without modifying bytes or blocking indefinitely, while a shared
list reader coexists with successful writes. Late claim revocation, two parallel
writers with 32 chunks and 64 namespace-publication attempts also pass. The
end-to-end native replacement regression is separate evidence, not implied by
these direct I/O tests.

## Native readonly pool import (2026-09-25)

Authenticated explicit import now accepts canonical `/AROS/<device>/<unit>`
and `/partition/<position>` paths with `readonly=on`. Partition extents come
from the native catalog; retained opened handles serve upstream label reads
and SPA opens. Per-pool path admission remains in force, including paths from
pool metadata. Export releases these bindings. Native writable import remains
unsupported: service-local reservations are not system-wide mount ownership.
Readonly access does not promise a coherent view against an external writer,
hot replacement, or concurrent partition-table modification.

`test-qemu-native-import.sh` exercises the real authenticated public service,
not a saved-config shortcut. `native-import-run.neYhpPfY` passed two cycles of
label discovery/import, readonly DOS mount, `hello.txt` read, unmount and export
on the existing two-disk GPT mirror fixture `qemu-run.gYHUxwAa`. SHA-256 checks
prove both backing files and both COW overlays unchanged. Writable native import
and writable mounting of the readonly pool are rejected. This is not evidence
of writable native pool creation, RAIDZ native import, or GUI rendering.

Retained failures: `jMnH3C50` expected the wrong rejection code in a readonly
engine; `3uN9BbRG` exposed libzutil's remaining regular-file-only candidate
filter; `XyHkiA3B` imported successfully but a writable mount scheduled an
accounting upgrade on the readonly pool and aborted. The mount adapter now
rejects that request before `dmu_objset_own`. `m9ofU6gB` read successfully but
export returned EROFS from the readonly engine. The final test starts the
service in authenticated WRITABLE mode (required by upstream export), while
the pool itself remains readonly and backing hashes independently verify it.
`kacJkB89` was a test-client compile failure from a missing mount API include.

The engine links the shared native path helper instead of the old
`zfs_append_partition` ENOTSUP stub. No shared AROS source, DUT, or user hosted
session was changed for these runs.

## Late DOS claim revocation (2026-09-25)

Writable handles retain the admitted device spelling/unit and check current
recognizable DOS claims before each nonempty write chunk. Detecting a claim
sets a sticky atomic revocation flag: subsequent writes fail EBUSY even after
the DOS entry is removed. Callers must close and reopen after resolving
ownership. Reads and flushing previously issued I/O remain permitted. Device
names and unit ranges are validated before copying them into handle state.

This does NOT eliminate the interval between checking DOS and issuing I/O,
resolve opaque DOS startup messages/device aliases, or exclude external raw
writers. It is a further admission safeguard, not public write authorization
or an atomic mount lease. No generic raw write RPC has been enabled.

Retained harness failure `device-range-run.cUl4jNdD`: the guest passed late
claim denial, unchanged data, sticky refusal, reopen and the warm GPT tests,
but editing its running shell script changed the subsequent read position;
the host runner failed with `=1: command not found` before cold verification.
That run is not counted as a complete pass. Follow-up runs keep the harness
unchanged until its process is terminal.
Complete follow-up `device-range-run.1gkw2l4W` passes the required revocation
marker, writer/range checks, warm/cold GPT persistence and unchanged image
hashes. Rebuilt `full-dos-revocation/OpenZFSServiceNative` passes full service
lifecycle/storage in `k7zCY1Bu` on private QEMU 10.1.94 without tracing. The
installed-QEMU ATA failure remains unresolved; these are not hardware results.

## Service-local native writer reservations (2026-09-25)

Native writable opens now reserve validated byte extents in a mutex-protected
registry keyed by the actual opened Exec device/unit pointers. Overlapping
writers fail EBUSY, including whole-disk versus partition opens; disjoint
extents may coexist. Reservation is performed after bounds validation and
released on close after the caller has finished synchronous I/O. Readonly
inspection remains available and is not claimed to be a coherent snapshot.

This is service-local coordination, NOT exclusion of external AROS programs,
an atomic DOS mount lease or public raw-write authorization. The DOS snapshot
guard remains separate. No public write RPC or native pool admission was
enabled by this change. Standalone tests now link the same private pthread
archive used by the engine rather than selecting an implicit SDK archive.

`device-range-run.YAlQGJtU` passes overlapping whole/exact/partial writer
refusal, another pthread's overlapping open refusal, adjacent writer success,
release/reopen, existing pattern I/O across 4 GiB, guard sectors and bounds,
and warm/cold upstream GPT persistence with unchanged originals/overlays.
`capacity-run.qBf066Ln` passes the 3-TiB patterned last-sector and large-GPT
catalog tests unchanged. `full-writer-reservations/OpenZFSServiceNative`
links successfully. These passes do not resolve the separate ATA timeout.
Full public-service regression `Z2cwJMNt` passes on the private QEMU 10.1.94
build without tracing; invalid-partition regression `tWzTBs44` also passes.
This is not an installed-QEMU 10.2.1 stability result.

## ATA timeout controls: pause and CPU count (2026-09-25)

Installed-QEMU run `JpVRPWUz` reproduces the unit-1 c8/LBA32/224-sector
timeout. HMP statistics queried BEFORE pause already show 391 reads and
30124556939 ns total read time; after pause those values are identical.
This excludes the capture's pause as the event that changed those counters,
but does not establish when the guest received a completion or interrupt.

Single-CPU control `On2KR1wd` also fails with the same command/status shape.
Its before/after counters likewise agree (363 reads, 30110641060 ns). SMP
is therefore not necessary for this observed failure. Neither control changes
the driver or enables continuous host tracing. Host stack attachment remains
unavailable under the existing ptrace policy. These results narrow hypotheses;
they do not prove a fix or identify the failing host/guest component.
Host-read-traced single-CPU run `neA0zmku` passes the entire public-service
suite on installed QEMU. Its retained preadv records include the LBA32,
114688-byte label reads completing in tens of microseconds. Because this run
does not reproduce the failure, that timing cannot explain the untraced
30-second stall. No failed syscall trace or completion-path root cause has
been captured; do not substitute this diagnostic pass for the failing gate.

## Retained ATA DMA ordering experiment (2026-09-25)

`ata-dma-order.patch` is a FAILED-TO-FIX private experiment, not a deployed
driver fix. It moves bus-master start after the ATA command, leaving PRD
setup and handler installation before it. Overlay `ata-dma-order.XW1IvcA3`
builds, but untraced two-CPU public-service run `AfzzOW07` still times out
on unit 1, command c8, LBA 32, 224 sectors, status 58/BMDMA 01. The public
storage gate fails. The shared AROS driver was not edited; the standard
capacity overlay remains the test baseline. This rules out that ordering
change as a sufficient fix, not every possible ordering or completion race.

The public-service runner now optionally captures HMP block state and bounded
host debugger backtraces on an observed test failure before cleaning up its
own QEMU process (`AROS_TEST_CAPTURE_FAILURE=1`). Capture errors cannot turn
the failed test into a pass. Default runtime tracing behavior is unchanged.
Private-QEMU run `m5ZtIy72` passes WITHOUT tracing (QEMU 10.1.94 versus the
installed failing 10.2.1). Thus tracing alone is not established as the cause
of pass/fail differences: executable/version/build timing is also a variable.
This single pass does not establish that either version is reliable.
Installed-QEMU failure capture `113ewv5v` reproduces the timeout and retains
HMP block state: failing ide0-hd1 has 170 reads and about 30.067 seconds total
read time. The original capture paused the VM first, so that result does not
establish whether completion preceded the pause. The runner now requests
statistics both before and after pausing. Host debugger attachment was denied
by the existing ptrace policy; no policy/permission change was made and there
is no host stack evidence from this run.

## Large native partition catalog (2026-09-25)

The catalog now resolves saturated root geometry through the real native
capacity query before validating child extents. It does not trust the GPT
header as evidence of physical capacity and does not modify partition.library
geometry or invoke that library's write path.

`capacity-run.E8e6IZE2` passes an actual partition.library read of a GPT on a
3-TiB virtual disk: the partition begins at sector 4294969344 and ends at
6442446847, both above the 32-bit sector boundary. Exact name, count, disk
capacity and sector size match; both private overlays remain byte-identical.
The independent host GPT verifier passes. Small-GPT regression
`partition-catalog-run.3zkY5yub` and invalid-extent regression `U0M8UsAF`
also pass, including unchanged-output refusal and original-image hashes.
`full-large-capacity/OpenZFSServiceNative` links successfully with this change.
Its untraced full public-service run `5ghULajn` FAILS: native label scanning
hits the previously observed ATA DMA timeout (unit 1, command c8, LBA 32,
224 sectors, alternate status 58, active BMDMA status 01). This reproduction
uses TWO CPUs, so the outstanding timing failure is not limited to four CPUs.
Do not equate the direct catalog passes with a complete service runtime pass.
Separately, traced private-QEMU run `UT2Uw0Kh` passes all public-service
lifecycle/storage checks with the same service/client and two CPUs. As with
earlier traced passes, this is timing-sensitive diagnostic evidence, not a
repair or grounds to erase the untraced failure.
These tests do not prove large-disk pool creation, exclusive device ownership
or large-table writes through partition.library.

## Native capacity beyond 32-bit sector geometry (2026-09-25)

`device-io.c` queries SCSI READ CAPACITY(16) when trackdisk geometry reaches
its saturation range. It validates command completion, the full response,
sector-size agreement, capacity consistency and byte-size overflow. A failed
query fails open; it never substitutes a truncated geometry or CHS estimate.
High-offset transfer still requires the driver's advertised 64-bit commands.

`capacity-run.60Y8xwXG` passes on a fresh 3-TiB qcow2 image: exact capacity,
last-sector read of a distinctive host-written 0x5a pattern, the same read
through a bounded range, end-of-device rejection and readonly write rejection.
The whole qcow2 hash is unchanged after the guest exits. Initial zero-only
run `LdW7Vhfg` passed but was strengthened because zeros cannot distinguish
wrapped reads on an otherwise empty image. Negative control `Cv8gNXkt` uses
the old ATA overlay and correctly fails open (EIO), before any data reads.

This needs driver support: the private `ata-capacity.vtSNuh2f` overlay adds
the PMI=0, full-32-byte READ CAPACITY(16) query used here, using the actual
48-bit ATA capacity and last-LBA semantics. `ata-capacity.patch` preserves
the delta atop the prior inquiry overlay; shared AROS sources were not
modified. This is not a complete SCSI command implementation or proof for
NVMe/AHCI drivers. Their capacity-query support still needs verification.

## Cold upstream GPT persistence (2026-09-25)

`device-range-run.OPvbmT0i` passes two distinct two-CPU QEMU boots. The
first creates and expands the GPT using actual upstream libefi. Explicit
private qcow2 overlays retain those writes. Independent `sgdisk -v` reports
no problems; `sfdisk --json` records the native-written partition layout.
The second guest opens a readonly native handle, checks expanded offsets,
sizes and types, performs a fresh partition.library rescan, and emits all
three GUIDs. The runner compares them exactly against the first boot.
Both overlay hashes and both original raw-image hashes are unchanged after
the cold boot. Source copies, source diff, input hashes and both serial logs
are retained in that evidence directory.

The IDE device is NOT hardware write-protected: QEMU ide-hd rejects readonly
block nodes. Readonly here describes the native handle; unchanged complete
overlay hashes independently establish that no guest writes occurred.
Retained failed harness attempts: `wB9NGpzQ` completed warm GPT verification
but nested ISO remastering returned SORRY/32 before cold boot; `6yUPgULf`
fixed remastering from the original base ISO but QEMU rejected the readonly
IDE block node before boot. Neither was a guest cold-persistence failure.

This proves cold GPT persistence, not native ZFS pool creation, external
device exclusion, complete zpool labeling hooks or four-CPU ATA stability.

## Actual upstream libefi native GPT I/O (2026-09-25)

`lib/libefi/rdwr_efi.c` now has an AROS transport boundary for geometry,
whole-device identity, exact block transfers, flush and fresh partition-table
discovery. The existing upstream GPT layouts, CRCs, partition-type mapping,
protective MBR, readback and expansion algorithm are used unchanged. UUID
generation calls the real native adapter and propagates its errors. Native
backup-array/header/PMBR failures now propagate rather than reporting primary
label success as complete success. Linux ioctl paths remain selected on Linux.

`efi-native.c` owns a bounded private handle registry (not POSIX descriptors),
serialized with a properly initialized pthread mutex. IDs never recycle;
closed handles fail EBADF. Conflicting local writer bindings fail EBUSY, LBA
multiplication overflow fails EOVERFLOW, readonly transfers fail EROFS, and
each successful write requires the native device flush to succeed. Rescan
reopens the actual `partition.library` root; native partition paths already
resolve through fresh roots, not Linux udev or invented device-node events.

This is an **internal adapter, not public writable admission**. It still
requires a caller-owned device. Its registry coordinates only these handles;
it does not exclude external tasks, handle aliases or prevent media replacement.
DOS collision checks do not constitute an atomic mount/write lease. Native
At that stage, capacity beyond the geometry limit was also unfinished; the
later large-capacity and catalog tests above extend it for supporting drivers.
No public write RPC or zpool label/relabel hook was enabled by this change.

Fresh two-CPU private QEMU `device-range-run.Dr9sIM0S` passes actual upstream
GPT creation, partition.library CRC-checked rescan, libefi reread of offsets,
sizes/tags/GUIDs, upstream expansion into an unallocated tail with identity
preserved, readonly-write rejection, stale handle, writer collision and offset
overflow checks. The existing native UUID, active/inactive DOS claim,
cross-4-GiB bounded I/O, final mount restoration and original-image hash checks
also pass. Writes are confined to QEMU snapshots of newly created fixtures.
This is same-boot GPT proof, not cold persistence, a newly created ZFS pool,
general native device ownership or four-CPU ATA stability.

Earlier runs: `5y31IdOV` passed create/read before expansion coverage;
`vrYNnoZJ` added expansion successfully. Retained compile failures:
`native-efi-probe.MGgsu4Nd` needed the upstream sysmacros header;
`device-range-run.QfYKLWD5` used a nonexistent test-only sys/vtoc.h include;
`8V44krTz` rejected pthread's incomplete static semaphore initializer under
Werror. The adapter now uses pthread_once plus pthread_mutex_init, checking
initialization/lock errors. The whole zfs/zpool command-link gaps remain the
same four symbols; the native libefi implementation is groundwork for the two
label hooks, not a claim that either command currently links.

## Upstream GPT labeling dependency investigation (2026-09-25)

The native AROS `partition.library` GPT function table has a NULL create-table
callback (`AROS-amiwest-nightly-src/rom/partition/partitiongpt.c`,
`PartitionGPT`). It supports reading and updating existing tables, but it cannot
implement OpenZFS whole-disk label creation merely by calling
CreatePartitionTable. The intended implementation is upstream libefi's actual
GPT generation/validation with an AROS I/O boundary, not a second GPT generator.

The first unmodified AROS compilation of `lib/libefi/rdwr_efi.c` stops at the
missing `uuid/uuid.h` (`build-aros/native-efi-probe.YtVc7tge/compile.log`). It
also contains Linux capacity, device-name, cache-flush, rescan and partition
resize ioctl dependencies which still require native adapters. No runnable
libefi or zpool label/relabel implementation is claimed yet.

`efi-uuid.c` implements the identifier dependency using actual `uuid.library`
version 1. It selects the library's locked time/node UUID generator (not its
documented weak random generator), packs RFC network-order bytes for upstream
UUID_LE_CONVERT, and returns explicit errors without modifying output. The
header avoids exposing AROS's struct uuid_t to libefi's array uuid_t. These
identifiers are not cryptographic keys or authorization tokens.

Fresh private two-CPU QEMU run `device-range-run.5Tlm0myB` passes native UUID
generation (16 nonduplicate identifiers with version/variant byte checks and
NULL rejection) plus the complete DOS-claim and bounded disk-I/O regression,
including cross-4-GiB writes and final DOS mount restoration. Both original
raw-image hashes are unchanged. This establishes the dependency adapter only;
it is not yet wired into upstream libefi, does not prove GPT creation and does
not resolve the independent untraced four-CPU ATA timeout.

## Native DOS-device collision guard (2026-09-25)

Writable `oz_device_open` now rejects a device/unit referenced by a recognizable
filesystem startup message in the locked DOS device list, returning EBUSY with
no handle. This includes configured but not yet started handlers. The check
reserves the whole unit, including opens of bounded partition ranges; readonly
label/discovery reads remain available. It does not inhibit handlers or remove
DOS entries in production.

This is **not exclusive ownership**: a new mount can appear after the check,
existing writable handles are not revoked, aliases and custom/opaque startup
messages are not resolved, and TypeOfMem is not allocation-bound validation.
Stable identity, mount/ownership coordination, foreign-filesystem detection and
large-capacity admission remain necessary before general writable raw import or
partition-table updates. A successful open is not authority to format a unit.

`device-range-run.NDGJav4E` passes a fresh isolated two-CPU QEMU boot. It verifies
that the guest's actual active HD0/HD1 DOS claims reject raw writes, inhibits and
temporarily detaches ONLY these newly created disposable fixtures, checks an
inactive synthetic DOS claim (whole/range rejected, reads and other unit allowed),
then tests bounded writes/readback/guards/readonly/reopen on both disks including
cross-4-GiB I/O. Finally it restores both DOS entries, uninhibits them and verifies
write rejection again. Both original raw image hashes are unchanged (writes were
to QEMU snapshots). The fixture detach is not a production admission recipe.
`device-range-run.XLmle18h` passed before adding the final restoration assertions.

Failures are retained: `GbpOXg7k` rejected SDK pointer-sign/const warnings;
`xF3evmk9`, `45Wed7V8`, and `2OyX0m1S` rejected the supposedly free second unit.
The final mapping diagnostic identified genuine automatic HD0/HD1 mounts; the
test setup was corrected instead of weakening the guard.

Full service build `full-dos-claim-build.log` completed successfully. Broader
four-CPU untraced run `public-service-run.wYcvVelu` passed all three lifecycle
phases but failed at native blank-label read repetition cycle 13: retained ATA
unit 1 DMA command 0xc8, LBA32, 224 sectors, status58/BMDMA01 timeout. This is not
an overall passing storage regression and the collision guard is not an ATA fix.
The separate untraced four-CPU RAM-backed service regression
`public-service-run.B7MtSV2F` passes all lifecycle and storage checks with the
same rebuilt service. This verifies the file-pool/DOS/authenticated-management
path without the native-disk fixture; it does not clear the native DMA failure.

### Native CLI DOS-claim preflight

The service exposes the collision snapshot through a fixed-size, native-root
authenticated request. It snapshots and validates version, reserved fields,
unit and bounded device name, confirms the device can be opened readonly,
then checks the current DOS list without changing any caller memory. No handle,
write lease or success authorization is returned. Unknown devices fail ENODEV;
unknown service ports fail ENOENT; non-root callers fail EACCES.

The upstream command's native `check_device` calls this before label/membership
inspection. A known DOS claim returns EBUSY for both whole units and partition
paths, even with force. Exported-pool force policy remains tested separately
through upstream `check_file_generic`: it does not override DOS claims.
Successful preflight still does not establish foreign-filesystem absence,
exclusive ownership, or safe partition-table mutation.

Service `full-dos-claim-rpc/OpenZFSServiceNative` and client
`cli-probe.9yNVFf1o/OpenZFSPublicServiceTest` passed the strict four-CPU suite in
`public-service-run.cvb6FoAb`, including root/non-root RPC validation, a real
temporary inactive DOS entry, force and non-force whole/partition denials,
native label/default discovery, and RAM pool/DOS regression. This run uses the
private worker-traced QEMU and is not a stability verdict for untraced ATA.
The fixture source and all three attached image hashes remained unchanged.
Whole-command link probe `cli-link.XazuROk5` still fails on the two sharing
providers and native `zpool_label_disk`/`zpool_relabel_disk`; no runnable complete
zfs/zpool command is claimed.
The same service/client in untraced `public-service-run.bewhbAE9` passed the
three lifecycle phases and new root DOS-claim/forced-preflight checks, then
failed the existing ATA unit 1 command 0xc8 timeout (LBA32, 224 sectors,
status58/BMDMA01). The complete untraced suite remains failing. Both outcomes
are retained; the traced pass cannot be promoted to native-device stability.

## Authenticated typed file-pool creation

Operation 5 now translates the manager's typed topology into upstream SPA
nvlist trees and calls `spa_create` under captured native root credentials.
It accepts existing, exclusively provisioned regular files only, using absolute
`/Volume/path` or native `Volume:path` names. It does not format native devices or create/truncate input
files. Every file must be sector-aligned, at least SPA_MINDEVSIZE, writable by
the native daemon, and entirely zero before any pool creation call. SameLock
checks aliases because the AROS CRT's st_ino is a canonical-path hash. DOS
assign prefixes and files served directly by this service task are refused;
recursive synchronous backing I/O would deadlock. Files must not be changed
or retargeted concurrently (including symlink paths): retained DOS locks do
not constitute an exclusive-write transaction. General device identity,
partition ownership and concurrent backing-file replacement remain open work.

The translator supports stripe/mirror/RAIDZ1-3 groups and the typed log/cache/
spare/special/dedup roles; special/dedup request the upstream allocation_classes
feature. Runtime coverage is recorded per layout, not inferred from this code.
No free-form properties, force or dry-run yet. Cachefile is explicitly none;
generic discovery/import and persistent service configuration remain incomplete.

`admission-service-run.OSwdrLfC` passes the dedicated CREATE guest: two blank
64 MiB RAM files become an upstream mirror, a created dataset is mounted through
the native DOS handler, exact payload bytes are written/read, mounted export is
refused, unmount/export succeeds, and existing exported labels reject recreation.
Nonblank second-file rejection also checks that the first file stayed entirely
zero. UID77 creation, native-device paths, DOS assigns and direct self-hosted
backing paths are refused. This is volatile RAM-file proof, not cold persistence
or GUI creation evidence. Run with `AROS_TEST_SUITE=create` and the standard
isolated overlays; normal two-boot admission remains a separate suite.

Failures retained: `RfPyTqMV` passed nonblank rejection but returned errno1223
(MAX_ERRNO+ERROR_WRITE_PROTECTED) on creation. Native ZIO tasks had no MuFS
backing-file authority. The marked test harness now explicitly logs in its
test root before starting the engine, so children inherit native authority;
production startup must provide its own authorized daemon identity. No policy
is bypassed. `QGNO25Ai` passed creation but consumed a ninth lifetime mount in
the combined suite; creation was separated rather than enlarging the eight-slot
limit. Safe retired-endpoint reclamation is still unfinished. Build/evidence
logs use `pool-create-*`.

The normal two-boot regression `InCKIH8y` also passes with the explicitly
authenticated fixture daemon: all prior permission/DOS/property/scrub/export
gates, cold read-only hashes, and offline zdb/compression/recordsize/atime checks.

### Five data layouts and native file-handle serialization

CREATE now runs stripe/mirror/RAIDZ1/RAIDZ2/RAIDZ3 sequentially, each on newly
provisioned 64 MiB RAM files (2/2/2/3/4 files respectively). It checks actual
upstream status kinds, leaf counts and parity, performs the mounted DOS payload
test, and unmounts/exports before deleting these disposable files. It checks
duplicate identity using `/RAM/...` and `RAM:...` spellings. The create-only
timeout is 600 seconds for five layouts; the original suite remains 180 seconds
per boot. Status rows now include upstream RAIDZ parity.

`yJ0n53Uj` initially passed layout/payload checks but did not check error counters.
Strengthened repeats `xE6VkLLs` and `j4TjJCgT` failed: after stripe payload I/O,
each file vdev reported seven read errors (write/checksum errors zero), despite
ONLINE health and successful bytes. These are retained failures, not healthy
pool proofs. AROS posixc pread/pwrite implement save/seek/read-or-write/restore
on a shared DOS handle and explicitly require caller serialization. The ZFS
adapter previously called these concurrently from ZIO tasks.

The AROS `zfs_file_t` now carries a per-file mutex. Positional/stateful I/O,
seek, attribute queries and fsync are serialized; errno survives mutex exit.
Native block devices retain per-request handles and bypass this POSIX-file
lock. Open/close initialize/destroy it only within upstream handle lifetime.
The offset query also now uses `lseek(fd, 0, SEEK_CUR)` in the correct argument
order. No shared CRT or driver code was changed.

`E2QIJEiT` passes all five strengthened creation cases with every reported
device ONLINE and zero read/write/checksum errors after payload I/O; RAIDZ
parity is 1/2/3 as requested. This removes the observed failure, not proof of
all possible races or fault recovery. Repeat/full regression logs use
`pool-file-lock-*`; no GUI creation, degraded-array or cold file-pool import
coverage is implied.

Repeat `YwWD4JMo` also passes all five layouts with zero error counters.
Full two-boot regression `sJPEkFrs` passes all original native gates, cold
read-only image hashes and offline zdb/compression/recordsize/atime inspection
with the new file lock (`pool-file-lock-qemu-2.log`,
`pool-file-lock-regression.log`). The previous no-lock general regression
`6iazWV4G` passed too: only the new file-backed pool test exposed this defect.

## Authenticated pool export

Operation 8 now uses native packet credentials and the existing `secpolicy_zfs`
root policy through admission. Dataset delegation does not grant pool export.
The adapter validates a pool name, refuses live mounted datasets with EBUSY,
and invokes upstream `spa_export` without force or implicit unmount under the
captured credential. Read-only imported pools may also be exported. The manager
routes export through the authenticated worker, with no legacy fallback.
Confirmation is mandatory; arguments, options and mutation flags are rejected.
The explicitly enabled legacy fixture transport remains available to isolated
test launchers; this does not enable generic discovery/import or pool creation.

The two-boot admission suite tests invalid dataset/snapshot targets, UID77
refusal despite dataset delegation, malformed request refusal, mounted-pool
EBUSY, and root export on writable and read-only imports. Its mandatory marker
is `Native authenticated pool export result=0`. Failure evidence
`admission-service-run.lj55Rz5H` records an incorrect test expectation for missing
confirmation (ENOTSUP); validation correctly returned EINVAL. The test was
corrected without relaxing admission. Logs use the `export-*` prefix.
The manager build alone is not interactive GUI verification.

### Authenticated scrub control

Operation 18 now admits start/pause/resume/stop under the captured native root
credential, using the existing upstream scan adapter. Dataset delegation does
not grant pool maintenance; UID77 start and stop are refused. The manager uses
the authenticated worker without falling back to legacy fixture requests.
Success means the control was accepted, not that a scan completed; pool.status
reports the actual upstream scan state and counters. Read-only pools reject
all four controls with EROFS. Confirmed requests only; no options or flags.

The native admission client starts a real scrub, polls for finished state with
nonzero examined/issued bytes and zero errors, and checks the persisted status
after cold read-only import. It also checks invalid action/name and denied
credentials. Successful pause/resume/stop transitions remain covered by the
older private management test, not newly claimed for this admission test.
Build/test logs use `auth-scrub-*`; a manager build is not scrub GUI evidence.

`admission-service-run.otbM2ChD` passes both phases and every existing suite
gate. The writable scan finished with 6,803,456 bytes examined/issued and zero
errors; cold read-only verification retains that completion and refuses all
four controls. Cold disk hashes and independent offline inspection pass
(`auth-scrub-qemu.log`).

`admission-service-run.s3N4nLdG` passes both boots, including the export marker,
all existing DOS/property gates, unchanged cold-read disk hashes, and offline
compression/recordsize/atime verification (`export-qemu-2.log`).

Interactive follow-up `native-gui-run.hFOzMHiP` runs the real manager under
the private fixture's authenticated root session (`AROS_UI_SCENARIO=export`).
`05-confirm.png` shows the exact pool/flags confirmation, and
`06-export-result.png` shows authenticated export errno=0 plus its worker log.
The launcher queries the upstream pool list after the manager closes and
requires zero rows; no legacy export is performed in this scenario. Saved
activity/response text is copied to serial. `verify-export-ui.sh` then requires
those markers, a stopped guest, unused images, offline zdb No leaks and
unchanged inspection hashes (`export-gui-offline.log`). This is independent
of the two-boot client tests; GUI permission-refusal coverage is not claimed.

## Authenticated encryption key verification

Operation 62 accepts Confirmed + Dry-run to verify a key file without loading
the key. It retains the same captured caller, load-key permission check,
authorized file access, format decoding and key-buffer clearing as normal load.
The upstream `spa_keystore_load_wkey` noop path authenticates the wrapping key;
its temporary key is freed even on success because ownership is not transferred
to the keystore. Change-key does not accept this flag.

Native `public-service-run.IVlNqUbb`, built from fresh upstream sources,
verifies a correct raw key succeeds while `keystatus` remains unavailable and
mounting remains refused. A wrong key, missing confirmation, force, and a
change-key dry-run are refused. Normal key load and exact payload readback
subsequently succeed, along with the full storage suite. Retained evidence is
under `aros/tests/evidence/key-check`. This is native API/QEMU evidence, not
interactive Manager or physical-media qualification.

The same new engine passes persistent run `public-service-run.qK2sThaw`:
writer acceptance, fresh healthy/degraded/restored reads, encryption and
passphrase lifecycle, unchanged cold-read member hashes and independent
`zdb` with no leaks. Evidence is under `aros/tests/evidence/key-check-cold`.
The cold checks qualify the updated engine's existing lifecycle behavior;
the new check-only request itself is exercised in the writer boot.

## Native dataset rename and destroy (2026-09-21)

### Snapshot holds and releases

Operations 55/56/57 now call upstream `dsl_dataset_user_hold`,
`dsl_dataset_user_release` and `dsl_dataset_get_holds`. Mutations require a
confirmed single snapshot/tag in fixture-write mode; no temporary holds or
mutation dry-run. Queries return structured persistent hold rows with tag,
parent snapshot and created-unix timestamp. Recursive queries enumerate the
same snapshot name on the selected dataset and descendants with one global
cursor. Pages are a live view, not a transactional enumeration. Long row names
fail explicitly.

`management-run.Sq00gkrM` passes all four native boots: add/duplicate hold,
held snapshot destroy refusal, release and repeated-release error, exact
remaining tag and cold intact/sole-member persistence with original file data.
Read-only service and read-only pool mutations are refused. IPC totals are
writer 114 plus scrub polls and readers 25 plus polls; earlier DOS, lifecycle,
scrub, hash and zero-reader-write/zdb gates also pass. Logs:
`snapshot-holds-errors-{build,qemu}.log`. Retain failed
`management-run.R8fBaKrX`: release reported a missing tag under snapshot#tag
in its per-item errlist while returning overall success; the adapter initially
checked only the snapshot key. It now propagates the single operation's
per-item error. GUI holds remain unverified.

Pagination/last-release follow-up: `management-run.0AOphDsF` passes all four
boots with 27 persistent tags enumerated exactly once across 24/3-row pages,
an exhausted cursor, missing-snapshot hold/release/query errors, non-snapshot
query refusal, and an empty hold list. A second snapshot refuses destruction
while held and becomes deletable after its last hold is released; cold boots
verify it stays absent. IPC totals are writer 155 plus scrub polls and readers
31 plus polls. All earlier checks, readonly hashes/write counters and zdb pass.
Logs: `hold-pagination-{build,qemu}.log`.

Hosted hold GUI follow-up uses private :95 pool `aros_mirror_838879ce` with
the 960282a service/UI. Confirmed read-write import, snapshot creation,
hold(ui-keep), holds(one persistent row with timestamp), release(ui-keep),
holds(zero rows), and export all succeed. Evidence under
`build-aros/hosted-ui.VKzPRi3D`: `holds-{add,release}-confirm.png`,
`holds-{add,list,release,empty,export}-result.png`,
`holds-{service-ui,serial-ui,fixture}.log`, input/exported-disk hashes, and
`holds-linux-zdb.log` (No leaks). The prior direct-lookup fixture disk hashes
remain unchanged. The initial import request was refused with Unsupported;
an explicitly re-entered request succeeded. Its screenshots/logs are retained;
the exact input discrepancy was not established. A create confirmation while
the pool was absent was canceled without submission. This GUI test does not
exercise GUI multi-page navigation; the native IPC test covers pagination.

Snapshot follow-up: operation 38 also accepts snapshots and calls upstream
`dsl_destroy_snapshot`. The authenticated route accepts the single exact
option `defer=on` for snapshot targets only. A held snapshot is marked for
deferred destruction, remains queryable while a hold exists, and is removed
by upstream after its last hold is released. Datasets, bookmarks and malformed
options cannot request deferred destruction. Confirmed recursive snapshot
destruction is also supported: operation 38 collects the same snapshot name
from the target dataset and all descendants, then submits one upstream
`ZFS_IOC_DESTROY_SNAPS` request so the set is accepted or refused atomically.
Authenticated recursive snapshot destruction also accepts `defer=on`, using
the same upstream ioctl's defer flag and per-snapshot permission checks. Native
public-package run `public-service-run.rlEPj1HF` verifies immediate removal of
an unheld parent snapshot, retention of a held child with `defer_destroy=on`,
and removal after its last hold is released. Missing confirmation and deferred
dataset destruction are refused. The fresh shared engine and complete mirror
storage suite pass; this is API/runtime evidence, not a GUI interaction or
physical-media test. Force remains unsupported. Destruction dry-run uses the
separate upstream command-worker route described below, not this mutation path.

Operation 38 with Dry-run now invokes installed `C:zfs destroy -n -v -p` via
the asynchronous command worker, with optional Recursive and snapshot-only
`defer=on`. Exact argv is used without a shell, and `-n` is unconditional.
The worker refuses force, missing dry-run, missing confirmation and unrelated
fields/options. Removing Dry-run selects the existing authenticated mutation
route; a successful upstream plan does not override AROS mount or permission
restrictions on execution. Native `public-service-run.IliFjbsQ` verifies a
recursive deferred plan naming both fixture snapshots, unchanged defer flags
and hold count, rejected flags, and the full storage suite.
Native GUI run `native-gui-run.ajXTDe6n` additionally submits this operation
through Manager with Recursive, Dry-run and `defer=on`, displaying the upstream
plan for both snapshots. Independent post-UI checks verify both defer flags
remain off, the child's hold remains present, and releasing that hold does not
destroy the child. Fixture cleanup and the full storage suite pass. Screenshots,
action trace, binary hashes and serial results are retained under
`aros/tests/evidence/gui-destroy-preview`. This is isolated QEMU proof, not
physical-media qualification. The initial capture has a generic mutation
warning even in dry-run mode. Follow-up `native-gui-run.DCRePTo0` verifies the
rebuilt Manager instead displays "Destruction plan only; no storage changes."
and repeats the unchanged-snapshot/hold and full storage checks successfully.
Its evidence is retained under `aros/tests/evidence/gui-destroy-preview-wording`.

Delegation follow-up `public-service-run.JqZeMvsx` verifies UID 77's local-only
mount/destroy grant is insufficient for snapshots, a descendant grant permits
snapshot destruction, and revoking that grant rejects recursive deferred
destruction without marking either parent or child snapshot for removal.
The fixture restores its root identity on every return. Earlier run
`public-service-run.3LRx0YnK` failed an incorrect test expectation that a local
grant permitted a snapshot: upstream `dsl_deleg_access_impl` deliberately treats
snapshots as descendants. That failure is retained, not counted as a pass.

Pending-state persistence is qualified by `public-service-run.no4FZbtl`.
The writer leaves both parent and child snapshots held and marked for deferred
destruction. A fresh guest imports the mirror healthy, degraded and restored;
each import reports `defer_destroy=on` and one user reference for both snapshots.
Recursive hold release on the read-only pool fails and leaves both counts
unchanged. The harness requires all three results, byte-identical pool members
and host `zdb -bcc` with no leaks. This proves pending-state retention across
cold imports; release after a writable cold import is not covered by this run.
Follow-up `public-service-run.fkCVCuGC` adds a third, writable cold boot only
after those read-only checks finish. Authenticated recursive hold release
removes both deferred snapshots; an export/read-only reimport still reports
both absent, while unrelated DOS file contents remain exact. Host `zdb -bcc`
passes on the changed members. This is clean export/reimport evidence, not
an abrupt power-loss guarantee.

Manager submission is now qualified by `native-gui-run.RFfSUBE5`. The
`AROS_TEST_GUI_DEFER=1` fixture creates an unheld parent snapshot and a held
child snapshot. Advanced/Datasets/destroy receives the target, `defer=on` and
Recursive through actual keyboard/mouse entry; preview, confirmation and the
successful authenticated response are captured. After Manager closes,
independent CLI queries require parent absence, child `defer_destroy=on` and
one hold, then release the hold and verify child removal. Fixture cleanup,
post-UI exact DOS readback and the full storage suite pass. These screenshots
are in `aros/tests/evidence/gui-deferred`; no physical device was used.

Authenticated recursive dataset destruction is deliberately non-atomic and
narrower than recursive snapshot destruction. The Manager first refuses any
service-mounted descendant, then enumerates the tree without mutation,
preflights delegated mount/destroy permission and refuses the whole request if
any snapshot exists. A snapshot-free accepted tree is destroyed deepest-first.
If an upstream dependency or concurrent change makes a later destroy fail, the
response reports the completed dataset count and requires inspection of the
live tree before retry; it never claims rollback of already destroyed children.

Operation 40 also accepts Recursive. For one exact property it walks the
selected dataset and descendants and returns one structured property row per
dataset with a global live-view cursor. Exact single-dataset queries retain the
response-detail overflow channel for long values. Recursive exact-property
pages label long values as `property-long`, retain the byte count and require
an exact query for the full bytes rather than returning a truncated usable
string. A cursor exactly equal to the live dataset count is an exhausted empty
page; a larger cursor is rejected as malformed or stale. Pool properties
remain non-recursive.

Operations 31 and 40 accept `all` to enumerate the visible registered
properties plus dynamic pool feature and user properties. One global cursor
continues the live view. The opaque cursor counts catalogue candidates, so a
later page skips earlier candidates without re-reading each upstream value;
inapplicable candidates still occupy a token position. Values that do not fit in a row are labeled
`property-long` with their byte length and no usable truncated value; query
that property by exact name to retrieve its full value through response detail.
Dataset `all` can be combined with Recursive. The cursor then spans ordered
dataset/property pairs across the selected dataset and descendants rather than
restarting at each dataset boundary. Its opaque 64-bit token carries the live
dataset ordinal and that dataset's property cursor, so already completed
datasets are skipped without re-reading every property. Each internal property
page is checked for forward cursor progress. The result is still a live view:
callers must restart if datasets or properties change during enumeration.
All-property catalogue cursors likewise accept the exact live end position as
an empty page and reject positions beyond it. For recursive `all`, only a zero
property cursor may accompany the live dataset-count end position; larger
dataset ordinals and nonzero property positions at that end are rejected.
Rejected cursors return the engine errno diagnostic; admission success text is
cleared before property dispatch so it cannot masquerade as an error detail.
The native regression discovers each live opaque catalogue end and proves the
exact end, end plus one and maximum cursor separately for pool and dataset
`all`; it does not infer the endpoint from returned row counts because skipped
inapplicable candidates still consume cursor positions.

Operation 42 accepts Recursive with the same `zfs inherit -r` execution model:
it applies one authenticated upstream inherit to the selected dataset and each
descendant, skipping descendant types where a built-in property is invalid.
The walk is deliberately non-atomic. A runtime failure reports the completed
count and requires inspection before retry because earlier changes remain;
property-level validation failures report `completed=0`. `received=on` composes
with Recursive to match `zfs inherit -rS`.

Snapshot hold listings use the same live-cursor boundary contract for one
snapshot and for recursive same-name snapshot walks: the exact live hold count
is an exhausted empty page, while a larger cursor is rejected as malformed or
stale with the engine errno detail.

Bookmark listings expose persistent bookmark rows with GUID, creation TXG and
creation time across one live global cursor. The exact live bookmark count is
an exhausted empty page; larger cursors are rejected as malformed or stale
with the engine errno detail.

`native-import-run.XteerhNl` proves the recursive contract in isolated native
AROS under QEMU/KVM. A hold on the grandchild snapshot made the first request
return EBUSY while all three root/child/grandchild snapshots remained. After
the hold was released, the same request removed all three `@batch` snapshots,
preserved all three unrelated `@keep` snapshots and completed cleanup. The
unsupported recursive `defer=on` combination was rejected before submission.
The full service CLI suite, writable/readonly mirror gates and immutable
backing hashes also passed. Earlier run `native-import-run.F77T6It4` proved the atomic
EBUSY half and a successful upstream destroy, but exposed a test-helper bug:
the helper incorrectly required operation 66's recursive-permission detail
for every successful recursive operation. It was not an OpenZFS failure.

Operation 50 now provides the matching authenticated recursive snapshot-create
route. It collects the target dataset and descendants, then submits one
upstream `ZFS_IOC_SNAPSHOT` request. Upstream checks snapshot permission on
every selected dataset and commits the set atomically; an existing same-name
snapshot makes the request fail without creating the other snapshots. No
snapshot properties, force or dry-run are accepted.

Operations 55/56 also accept authenticated recursive hold/release. Existing
same-name snapshots on the selected dataset and descendants are collected;
every snapshot is checked against the retained caller identity. Batch hold is
an upstream atomic transaction. Recursive release first verifies that every
selected snapshot has the requested tag, avoiding upstream's missing-tag skip
semantics, then submits one atomic release transaction. Temporary holds,
options, force and dry-run remain unsupported. Operation 57 accepts the same
recursive selection as a read-only live view; each row identifies its source
snapshot, and pagination advances across the combined hold set.

Pool-member removal follow-up: the existing authenticated `pool.remove`
operation 10 is now covered by a positive Manager path rather than only by the
CLI. A disposable two-file pool resolves the first member's GUID from structured
status, submits it through admission, and polls Manager operation 4 through the
upstream asynchronous evacuation. Completion must report nonzero copied bytes,
omit the removed path, retain the survivor, and have no pagination. The test
then deletes the removed backing, imports from the survivor alone, and rereads
the exact payload. `native-import-run.JQiIVEVX` passed with 1,220,608 bytes
copied, all wider pool/CLI checks, guest completion, and unchanged original QEMU
backing hashes. This proves explicit Manager-initiated file-vdev evacuation; it
does not prove automatic media-change callback removal, physical storage, or
DUT behavior.

Pool-root snapshots are valid, but destroying the pool root itself is not.
`build-aros/management-run.5D80yYTf` passes all four native boots with the
new path: create/delete a disposable snapshot, reject missing confirmation
and dry-run, refuse deletion of the existing clone's origin with EEXIST,
then verify absent snapshot and intact parent data after cold intact and
sole-member imports. Writer IPC total is now 97 plus scrub polls; readers
remain 17 plus polls. Hash/zero-reader-write and independent zdb gates pass.
Logs are `snapshot-destroy-build.log` and `snapshot-destroy-qemu.log`.
Hosted snapshot follow-up at cfc7589 uses fresh private pool
`aros_mirror_58e94964` on :95. The UI imports read-write, creates
`files@ui-delete`, lists it from its parent with snapshots=on, destroys it,
lists the surviving parent without that snapshot, and exports successfully.
Evidence in `build-aros/hosted-ui.VKzPRi3D`: `snapshot-*-confirm.png`,
`snapshot-create-result.png`, `snapshot-parent-list.png`,
`snapshot-destroy-result.png`, `snapshot-after-list.png`,
`snapshot-export-result.png`, `snapshot-service-ui.log`,
`snapshot-serial-ui.log`, `snapshot-fixture.log`, `snapshot-inputs.sha256`,
`snapshot-exported-disks.sha256` and `snapshot-linux-zdb.log` (No leaks).
Prior lifecycle fixture hashes remain unchanged. Direct snapshot listing
failed before parent traversal succeeded; `snapshot-listed.png` preserves
that gap. An initial destroy confirmation with stale snapshots=on options
was canceled without submission, then resubmitted without options.

Direct snapshot lookup follow-up: operation 36 now uses the ordinary dataset
row reader for a snapshot leaf rather than passing it to the filesystem-root
walker. Existing snapshots return their actual name, parent and snapshot type;
an exhausted cursor returns an empty page, but still validates existence.
`management-run.CVW54Xjt` passes all four QEMU boots, including exact snapshot
lookup with/without recursive snapshots=on, exhausted cursor, and missing
snapshot at both zero and nonzero cursor. All prior lifecycle, scrub, DOS read,
readonly write-counter, hash and zdb checks pass. IPC totals are writer 102
plus scrub polls and readers 22 plus polls. Logs: `snapshot-list-build.log`
and `snapshot-list-qemu.log`.

Hosted direct-lookup retest at 5df92a9 succeeds on private :95 pool
`aros_mirror_bda5ec5a`: confirmed import/create `files@ui-direct`, direct
lookup returns one snapshot with correct parent/read-only state, then export.
Named capture `snapshot-list-direct-result.png` and the matching
`snapshot-list-{service-ui,serial-ui,fixture}.log`, input/exported-disk hashes,
and `snapshot-list-linux-zdb.log` (No leaks) are retained in
`build-aros/hosted-ui.VKzPRi3D`. The preceding snapshot fixture hashes remain
unchanged. This closes the GUI-observed direct lookup gap, not the remaining
production authorization or full management feature work.

Management operations 39 and 38 now invoke upstream `dsl_dir_rename` and
`dsl_destroy_head`. These are fixture-only, confirmed mutations; unsupported
options, dry-run and implicit unmount are rejected. Rename requires the same
pool, and the initial filesystem-only implementation rejected pool roots,
snapshots/bookmarks and active
service-mounted subtrees. Upstream child/dependency checks remain in force.
This does not supply production management caller authentication.

`build-aros/management-run.coSHOtuq` passes all four native QEMU boots:
writer, cold intact mirror, and each sole surviving member. The IPC test
renames a parent with a child and file data, destroys children, checks absent
old names and surviving data through ZPL and DOS, and checks mounted and
readonly rejection. Each boot verifies five mounted DOS reads and exact IPC
totals (writer 92 plus scrub polls; readers 17 plus polls). Readonly guests
perform no disk writes; backing hashes remain unchanged; independent zdb
reports No leaks. Raw and normalized serial logs are both retained.

The writer deliberately tests service mount/unmount, so its retired DOS
endpoint remains resident after pool export and PASS until QEMU exits. That
is the existing safe endpoint-lifetime contract, not a clean service shutdown
claim.

Hosted GUI follow-up uses only private `build-aros/hosted-ui.VKzPRi3D` on
display :95 and fresh pool `aros_mirror_d65b8d99`. Confirmed read-write import,
create `lifecycle-ui`, rename to `renamed-ui`, list, destroy, and export all
return success. Listing the destroyed name fails as expected. Independent
`lifecycle-linux-datasets.log` contains only the original root/files/payload
datasets; `lifecycle-linux-zdb.log` reports No leaks. Named-window captures
`lifecycle-*-confirm.png`, `lifecycle-rename-result.png`,
`lifecycle-renamed-list.png`, `lifecycle-destroy-result.png`,
`lifecycle-destroyed-list.png` and `lifecycle-export-result.png` preserve the
actual dialogs/results. `lifecycle-service-ui.log`, `lifecycle-serial-ui.log`,
`lifecycle-fixture.log`, `lifecycle-inputs.sha256` and
`lifecycle-exported-disks.sha256` retain provenance. The first import omitted
the required readonly=off option and was refused without importing; request 3
corrects it explicitly. The canceled create dialog before that correction
did not submit an operation. No DUT or other hosted runtime was used.

## Native large-device fixture (2026-09-21)

### Cold hosted RAID handler matrix

Degraded-write follow-up uses separate fresh fixtures and logs, preserving
the readonly matrix below. `WRITE-DISPOSABLE-DEGRADED` imports read-write,
verifies actual topology/leaf/parity/unavailable-member counts, writes a
deterministic 256 KiB `raid-write.bin` through the ZPL adapter using explicit
fixture credentials, syncs, verifies every byte through the readonly DOS
handler, then exports. `VERIFY-DEGRADED` verifies that file through DOS after
a new OS boot in globally readonly mode. Neither mode enables production
writable DOS mounts or bypasses the management service's fixture restriction.

Both passes succeed for mirror, RAIDZ1, RAIDZ2 and RAIDZ3 with 1/1/2/3 members
absent. Evidence under `build-aros/hosted-ui.VKzPRi3D`:
`raid-degraded-write.log`, `raid-degraded-verify.log`,
`raidwrite-{create,writer,verify}-serial.log`, input hashes, and
`raidwrite-readonly-hashes.log` (all 14 post-writer hashes unchanged).
`raidwrite-change-audit.log` establishes that every surviving member changed
during the writer while every removed member retains its original hash.
Independent `raidwrite-*-linux-zdb.log` checks use explicit surviving file
paths only, exclude the stale removed files, validate all checksums, and
report No leaks for all four pools.

Fixtures: mirror `OpenZFS-test-85263ced`, RAIDZ1 `OpenZFS-test-62a4586e`,
RAIDZ2 `OpenZFS-test-4f59e035`, RAIDZ3 `OpenZFS-test-82294073`. Missing files
were initially retained as removed-disk0 (mirror/RAIDZ1), removed-disk0/1
(RAIDZ2), and removed-disk0/1/2 (RAIDZ3). At that point they were stale, NOT
synchronized members. Build: `hosted-raid-write-isolated-build.log`.

Recovery follow-up preserves that exact state in the hash-verified private
`raid-prerecovery.CWg6u2KJ` directory before restoring member filenames.
`RECOVER-DISPOSABLE` imports writable and tests upstream's automatic stale-DTL
resilver, without synthesizing success or substituting a scrub. Each topology
must reach POOL_SCAN_RESILVER/DSS_FINISHED, nonzero processed bytes, zero scan
errors and no remaining vdev_resilver_needed ranges before DOS readback/export.
`raid-resilver.log` passes all four: mirror 782336, RAIDZ1 425984, RAIDZ2
970752, RAIDZ3 1642496 processed bytes, zero errors. Build log is
`hosted-raid-resilver-build.log`.

A fresh readonly boot (`raid-resilver-cold.log`) verifies the entire payload
with healthy topology and every member present. Another fresh boot
(`raid-repaired-members.log`) must use repaired members: mirror disk1 hidden;
RAIDZ1 disk1 hidden (disk0 repaired); RAIDZ2 disk2/3 hidden (only repaired
disk0/1 available); RAIDZ3 disk2/3/4 hidden (only repaired disk0/1 available).
All four pass. This is not exhaustive testing of every missing-member subset.
Every filename is restored afterward, all 14 post-resilver hashes remain
unchanged by both cold tests (`raid-resilver-cold-hashes.log`,
`raid-repaired-members-hashes.log`), and four independent
`raid-resilver-*-linux-zdb.log` checks validate checksums with No leaks.
Input hashes, bootstrap/serial logs and pre-recovery copies retain provenance.
All pools are unloaded and the private bootstrap is stopped. These are hosted
file-vdev resilvers, not physical hotplug, replacement-device APIs, interrupted
resilver recovery, or production authenticated management verification.

`raid-read-test.c` and the three `Startup-Hosted-RAID-*` scripts extend the
earlier DMU-only topology tests to actual AROS DOS packets after an OS restart.
The creator makes fresh mirror/RAIDZ1/RAIDZ2/RAIDZ3 pools with 2/3/4/5 sparse
256 MiB files and real ZPL fixture files. The cold reader imports readonly,
checks root-vdev health, mounts the public readonly diagnostic DOS handler,
opens both hello.txt and subdir/nested.txt, compares bytes, checks seek/EOF,
and verifies MODE_NEWFILE refusal. Each reader client joins before unmount.
The fixture text still mentions a mirror; it is fixed comparison data, not
the test's topology identification.

Private hosted :95 evidence in `build-aros/hosted-ui.VKzPRi3D`:

- Mirror `OpenZFS-test-2cc7a2ab`, RAIDZ1 `OpenZFS-test-89301566`, RAIDZ2
  `OpenZFS-test-b387d351`, RAIDZ3 `OpenZFS-test-cf58bc2b`.
- `raid-*-create.log`: all four creation/DMU roundtrips pass.
- `raid-cold-read.log`: all four cold DOS tests pass with healthy root state 7.
- After stopping that private bootstrap, disk0 was moved to removed-disk0
  for mirror/RAIDZ1; disk0/1 for RAIDZ2; disk0/1/2 for RAIDZ3. A fresh OS boot
  with `Startup-Hosted-RAID-Degraded` passes all four DOS tests with degraded
  root state 6 (`raid-degraded-read.log`). No member contents were altered.
- After stopping the reader, every filename was restored. All 14 original
  hashes match (`raid-disks-before.sha256`, `raid-restored-hashes.log`).
- Four independent `raid-*-linux-zdb.log` runs using upstream `zdb -bcc`
  validate metadata/data checksums and report No leaks.
- Input hashes, bootstrap logs and `raid-{create,read,degraded}-serial.log`
  retain provenance. Build failures for a missing vdev implementation header
  and omitted ZPL adapter are preserved in `hosted-raid-read-build.log` and
  `hosted-raid-read-vdev-build.log`; corrected builds are
  `hosted-raid-read-adapter-build.log` and `hosted-raid-create-build.log`.

This is hosted file-vdev readonly handler evidence, not native-device RAIDZ,
degraded writes, reconstruction/resilver, or a production authenticated mount.
The new native executable and startup scripts must be installed only into an
isolated hosted runtime. The reader accepts only the generated fixture naming
pattern; creation always uses exclusive fresh directories. DUT/user runtimes
were not used. All pools are exported/unloaded and member names restored.

`AROS_TEST_LARGE_DISKS=1` selects two fresh sparse 8 GiB disks in
`test-qemu-mirror.sh`; the default remains 256 MiB. Creation still requires
an explicit DISPOSABLE-8G argument, exact geometry, and blank label regions.
Before pool creation, each disk receives distinct patterns at 1 MiB,
4 GiB + 1 MiB, and across the 4 GiB boundary. Flush/read comparisons detect
32-bit aliasing; all three regions are zeroed, flushed and checked again.
The native adapter permits only the two exact fixture sizes and the existing
explicit ata.device unit 0/1 opt-in. This is not general device discovery.

Negative evidence: `qemu-run.IyxFmVW1` passed all six boundary comparisons
and both restorations, but spa_create returned EINVAL because kernel.c still
restricted the device to 256 MiB. `qemu-run.JhXYvv3E` repeated that result:
the link helper reused the old kernel object despite the edited source.
The helper now rebuilds kernel.c along with zil.c before linking. Logs are
`large-device-*`, `large-guard-*`, and `large-refresh-*` in build-aros.
Native device results do not resolve the separate hosted POSIX large-file
pread/pwrite limitation tracked by the platform issue tracker.

Corrected run `qemu-run.Og2rLNSC` passes all five fresh-kernel boots:
creation, cold read-only mirror, each sole surviving member, and cold writable
reopen. All six boundary comparisons and both zero restorations pass. Native
DOS access, security identity/access and MuFS endpoint/remount gates pass;
read-only guests issue zero disk writes, original hashes remain unchanged,
and both original and writable-COW pools pass independent Linux zdb -bcc
with No leaks. ATA flush-operation gates pass on both writable phases.
This does not prove full-capacity allocation or repair/resilver above 4 GiB.
Default-size regression `qemu-run.P3MdigfU` then passed the same five-boot
suite with the identical rebuilt binaries on 256 MiB disks, including hash,
zero-readonly-write, ATA flush, security/MuFS and both zdb No leaks gates.
Log: `build-aros/large-default-regression-qemu.log`.

## Imported pool and device status (2026-09-21)

Dataset creation: operation 37 now binds a confirmed filesystem name to
upstream dmu_objset_create and the native adaptation of zfs_create_fs in
aros/zpl/create-fs.c. It retains upstream master/SA/unlinked/root initialization
and uses the ported zfs_acl_ids_create/zfs_mknode, not the sample-file fixture
creator or a hand-built public ACL. Linux inode scaffolding is replaced with
a temporary native root znode. Default case-sensitive filesystem creation only;
custom properties, zvols and general management authorization remain unfinished.
No implicit mount or parent creation; arguments/options/dry-run are rejected.

`management-run.UVtduPT7` passes all four management boots, including intact
and each sole-member cold import, created-file DOS access, zero reader writes,
hashes and independent zdb No leaks. The root begins empty, uid/gid 0,
self-parented; an unrelated user can read/traverse but receives EACCES for
write on a writable mount. Cold readonly mounts reject writes with EROFS.
Duplicate creation, missing parent, invalid snapshot name, missing confirmation,
dry-run and readonly creation checks pass. Independent created-linux-zdb.log
confirms ZPL version 5, master/SA/delete queue and the root mode 40755;
created-child-linux-zdb.log inspects the separately created child filesystem.

Negative evidence retained: create-fs-compile.log lacked two upstream headers;
create-fs-headers-compile.log passes with them. management-run.8JSKVoGC created
the filesystem but its test expected ACL EACCES on a readonly mount. The
corrected test separates writable ACL enforcement from readonly EROFS and
logs every root check. Logs: dataset-create-*, create-acl-* in build-aros.
This is native IPC/DOS proof, not yet a hosted GUI creation test.

Hosted creation follow-up: refreshed private :95 runtime VKzPRi3D created
fresh pool aros_mirror_4eadc49e. The native GUI confirmed writable import,
created `aros_mirror_4eadc49e/from-ui`, enumerated that filesystem, and confirmed
pool export. Service requests 2/3/4/5 record operations 7/37/36/8 with status
zero. Named AROS-window captures create-import-confirm, create-dataset-confirm,
create-dataset-result/listed and create-export-confirm/result preserve the
sequence. Independent create-linux-zdb.log reports No leaks;
create-dataset-linux-zdb.log confirms an empty uid/gid-zero 40755 root with
size 2 and self parent. create-inputs.sha256 and create-exported-disks.sha256
identify binaries/startup and exported backing files. create-service-ui,
create-serial-ui, create-errors-ui and create-fixture logs are retained beside
the images. The previous scrub fixture hashes remain unchanged. This closes
hosted GUI default-filesystem creation proof, not properties/zvols, delegated
authorization or production mount/discovery lifecycle.

Scrub integration: operation 18 accepts confirmed `start`, `pause`, `resume`
or `stop` in Argument for a pool name, with no options/dry-run/cursor. It uses
upstream spa_scan/spa_scrub_pause_resume/spa_scan_stop, rejects readonly pools,
and remains restricted to TEST-WRITE fixtures pending real management
authorization. Acceptance is not completion. Operation 4 appends live scan
state, examined/issued bytes, errors and timestamps using spa_scan_get_stats;
ENOENT from that statistics call means no scan history, not a missing pool.
The UI catalogue requires and labels the action and describes this boundary.

Initial `management-run.IKvQmlI3` completed a scrub (4,952,064 bytes issued,
zero errors) and preserved the result on cold import. Its host gate counted
14 instead of 15 IPC lines: security.library output split the operation-40
transport line at `managemen` / `t IPC transport=0`. The next run YAPBkYcQ
exposed that reducing the expected count was wrong. Both logs are retained;
the guest now counts successful joined IPC round trips and checks the exact
expected total itself. The host checks that aggregate, not a count of
interleavable individual serial lines. Logs: scrub-management-*, scrub-cold-*,
scrub-count-* in build-aros. Pause/resume/stop transitions and hosted GUI
interaction still require dedicated runtime verification.

`management-run.pt55hdzZ` then demonstrated the same notification interleave
inside a DOS-read result. The harness now preserves raw serial and creates a
separate checked log removing only complete security secTaskNotify records,
rejoining the interrupted test line. Replaying this on the failures recovers
the exact 15 IPC and three DOS-read results, without suppressing assertions,
crashes or access-control errors. `management-run.NT9Yp290` passes all four
boots, including each sole mirror member: completed scrub, positive issued
bytes, zero scan errors, readonly mutation rejection, zero cold disk writes,
unchanged fixture/reader hashes and independent zdb No leaks. Native service
also links successfully (`scrub-service-build.log`); this is not GUI proof.

Control transitions now verified in `management-run.nhKFiJNE`: the disposable
guest uses upstream zfs_scan_suspend_progress only to hold the tiny scan alive
while real IPC requests pause, resume and stop it. Operation 4 verifies paused,
scanning and canceled states; duplicate pause returns EBUSY and duplicate stop
returns ENOENT. The hold is cleared before a second real scrub finishes with
positive issued bytes and zero errors. All four boots pass, with 67 joined IPC
requests in the writer and 15 in each cold reader, both sole-member imports,
zero reader writes, unchanged hashes and independent zdb No leaks. This
tunable is confined to the test executable, not the management service. Logs:
scrub-controls-{build,qemu}.log. Hosted GUI scrub interaction remains unverified.

Hosted GUI follow-up (same date): the refreshed private :95 runtime
`build-aros/hosted-ui.VKzPRi3D` used Startup-QA-Pool-Write to create fresh
exclusive fixture `OpenZFS-test-f23d4b3d`. The native UI confirmed writable
import of `aros_mirror_f23d4b3d`, submitted scrub/start, queried status showing
finished with 921,600 bytes examined/issued and zero errors, then confirmed
export. Service requests 2/3/4/5 record operations 7/18/4/8, all status zero.
Named-window captures scrub-import-confirm/result, scrub-action-confirm/result,
scrub-status-complete and scrub-export-confirm/result preserve the UI sequence.
Independent scrub-linux-zdb.log reports No leaks after export. Input hashes,
exported disk hashes and scrub-service-ui/serial-ui/errors-ui logs are retained
in that private directory. The earlier read-only status fixture's hashes remain
unchanged. This verifies GUI start/completion/export, not GUI pause/resume under
load or production multi-user management authorization. The startup recipe is
private QA only; it does not run in the release image or the user's session.

Hosted GUI follow-up: fresh private fixture `OpenZFS-test-16105802` in
`build-aros/hosted-ui.VKzPRi3D` passed native file-backed creation/export and
read-back. Its `aros_mirror_16105802` pool was then imported read-only through
the real confirmation requester, and GUI operation 4 returned ONLINE rows
for the pool, mirror and both file leaves. `ui-service.log` records successful
operations 7 and 4. Named AROS-window captures `status-import-confirm.png`,
`status-ui-health.png`, and `status-wrapped-health.png` preserve the sequence.
The first status capture exposed clipped row error counters: the manager had
wrapped prose but inserted data rows directly. It now renders rows and their
numeric metadata through the same wrapping path used for the detail text.
The corrected capture shows the counters and capacities rather than cutting
them off horizontally. Hash checks confirm neither fixture disk changed
during GUI import/status; independent `status-linux-zdb.log` passes all 81
blocks with No leaks. `status-wrapped-inputs.sha256` identifies deployed inputs.

`aros/ui/Startup-QA-Pool` is an explicitly private hosted recipe that first
creates new exclusive random fixture files and stops on fixture failure.
For the second UI boot, ordinary Startup-QA reused the existing exported
fixture; it did not recreate or rewrite it. No shared hosted session, source
tree, release build or DUT was changed. This establishes healthy-mirror GUI
status, not GUI degraded status, large-vdev pagination, or all storage modes.

Operation 4 now queries upstream `spa_get_stats` for an already imported pool.
The adapter walks its owned vdev-statistics nvlist into paginated pool/device
rows: topology parent, upstream state/aux interpretation, GUID, error counts,
and raw per-vdev allocation/space. It includes children, spare and cache arrays
and preserves log/allocation-class roles. It rejects unsupported request
filters and invalid pool names, does not import/discover devices, and clears
partial rows on errors. A returned config may report an upstream load error
in the detail alongside usable health rows. Suspended root configurations
are reported as SUSPENDED. Capabilities and UI labels now expose this binding.

`build-aros/management-run.yioNNVy4` passes four native boots: writable mirror,
cold readonly mirror, and each sole-member import. Real IPC verifies root,
mirror and both leaf rows; intact health is ONLINE, degraded parents report
DEGRADED and the absent leaf UNAVAIL. Missing pools and dataset names passed
as pool names return errors. The 49 writer and 13-per-reader IPC roundtrips
also retain clone data, mounted DOS and pagination checks. Reader boots have
zero writes and unchanged member hashes; writer flush checks and independent
zdb No leaks pass. Logs: pool-status-unit-{build,qemu}.log under build-aros.

Initial run `management-run.7PhgWlBf` passed the first three boots but failed
second-survivor import with ENODEV. The harness had omitted the existing ATA
unit-numbering arrangement: a blank non-member at index zero keeps the second
member at AROS unit one. That correction, not a pool-status change, enabled
the fourth boot. The blank disk is never a member and must also show zero
writes. Initial logs pool-status-{build,qemu}.log are retained.

Spare/cache/special/log topologies, suspended/faulted pool cases and >24-vdev
pagination are implemented paths but not established by this mirror test.
The matching hosted service links (private status-service-build.log) and its
healthy-mirror GUI path is verified by the follow-up above.
Status is a live per-page view, not a consistent multi-page topology snapshot.
Topology, cumulative-I/O, latency-histogram and size-histogram status cursors
accept the exact live row count as an exhausted empty page. Larger cursors are
rejected as malformed or stale with the engine errno detail. The native proof
walks both multi-page histogram views before exercising their exact and stale
boundaries, so the accepted end is the measured live row count rather than a
fixed fixture constant.

## Native dataset pagination and snapshot hierarchy (2026-09-21)

The management test now creates a private root clone, 26 child clones and a
snapshot through the real broker/upstream bindings. Enumeration verifies
27 filesystem rows or 28 rows with snapshots over two 24-row-limit pages,
exactly once per expected name, monotonic next cursors, correct kinds/parents,
nonrecursive results of one/two rows, and empty results for out-of-range
cursors including UINT64_MAX. Both writer and cold readonly boots execute
all six cases (46 and 10 total IPC roundtrips respectively).

Initial run `build-aros/management-run.meFRjD36` exposed incorrect snapshot
parent metadata: page-root@page-snapshot claimed qemu_mirror rather than
qemu_mirror/page-root. The row adapter now splits snapshots at '@' and other
datasets at the last '/'. Corrected run `management-run.WecM840o` passes
both boots, prior mounted DOS/clone-independence checks, positive writer ATA
flushes, zero readonly writes, unchanged baseline/readonly hashes, and
independent zdb No leaks. Logs: pagination-native-{build,qemu}.log (initial)
and pagination-parent-{build,qemu}.log (corrected), all under build-aros.

These are native API/broker tests, not a claim that GUI Next page has been
visually tested with this fixture. Pagination explicitly reports a live
namespace view; concurrent changes between pages are not a consistent
snapshot and stable-generation cursors remain future work.

## Hosted management UI verification (2026-09-21)

Required-field follow-up: Import now labels pool/configuration inputs,
pool/dataset Get labels its property input, and dataset List labels its root.
These previously empty catalogue labels became misleading "unused" text when
operation-specific labels were enabled. The catalogue also feeds validation,
so missing required values now fail at the API boundary with BAD_REQUEST.
`aros/ui/fieldtest.c` runs as an AROS executable against the actual validator;
it checks the exact missing-target/argument reasons and valid envelopes for
all four operations. Corrected hosted run reports zero failures in
`build-aros/hosted-ui.VKzPRi3D/ui-fields.log`. The initial test accidentally
omitted mn_Length; `fields-failed-envelope.log` retains those five failures.
Named-window captures `fields-import.png`, `fields-datasets.png`, and
`fields-property.png` verify visible labels. No pool was imported or changed.

The visible request field labels now follow each operation's definitions;
Clone displays Source snapshot and Destination dataset. Read-only management
reports that clone/snapshot/export are unavailable in this service mode,
rather than incorrectly claiming the fixture-write implementations do not
exist. Unsupported operations remain explicit failures, not simulated success.

Private runtime `build-aros/hosted-ui.VKzPRi3D` on display `:95` exercised real
service hello, clone preview, confirmation and backend rejection. Captures
`mode-clone-fields.png`, `mode-confirmation.png`, and `mode-response.png` show
the named AROS window. `ui-service.log` records operation 52 returning status
5; no pool was imported and no mutation executed. This verifies the read-only
GUI/service path, not successful GUI clone execution or production authority.

The initial native/SMP-SDK binaries trapped on this older hosted runtime;
`failed-smp-sdk-serial.log` and `failed-smp-sdk-bootstrap.log` retain that
failure. Rebuilding with the hosted toolchain resolved this run's startup
failure. Hosted objects/artifacts remain private, separate from native builds;
`AROS_OUTPUT_DIR` now permits that separation. The exact ABI failure mechanism
has not been isolated. `hosted-qa.conf` uses a relative log path and `Startup-QA`
starts the diagnostic service without importing or creating pools.

Native regression `build-aros/management-run.omgE5KmT` independently passed
both boots: IPC clone operations, mounted DOS reads, clone independence and
cold persistence, unchanged baseline/read-only disk hashes and zdb checks.
Driver/hardware durability and MuFS lifecycle integration are not established
by these UI and file-backed guest results.

## Current management increment: snapshot cloning (2026-09-21)

Operation 52 (`snapshot.clone`) now calls upstream `dsl_dataset_clone`, with
source snapshot in Target and new destination dataset in Argument. The UI
labels now identify those roles. The operation is advertised only in the
explicit fixture-write engine mode, requires confirmation, rejects unsupported
options/flags/cursors, validates names and same-pool destination, and checks
pool writability before the low-level sync task. It does not implicitly mount
the clone. Normal readonly service mode still reports this operation unavailable.
Management caller authentication/delegation and task credential propagation
remain incomplete; this fixture binding is not a production authorization claim.

`qemu-management-test.c` exercises real API envelope validation and dispatch,
not a mock engine, using private copies of the native mirror fixture. It tests
snapshot creation, missing confirmation, unsupported dry-run, non-snapshot
source, cross-pool destination, missing snapshot, successful clone, duplicate
destination, readonly pool denial, and readonly-service unavailability. It
reads original data through the native ZPL bridge, modifies only the clone,
and proves the source and snapshot remain unchanged. A second readonly kernel
boot verifies all three datasets and the clone's exact `origin` property.
This test does not yet exercise Exec IPC, the GUI, or a mounted clone via DOS.

Negative management-run.IL2Fuczl preserved: clone/data and cold reads passed,
but the test incorrectly attempted export from a READ-only engine, returning
EROFS. Corrected test shutdown uses ordinary engine teardown on that boot.
management-run.1OKZIkC2 then passed both boots. Expanded origin/capability
verification management-run.HzI3la4l also passed both boots. The writer issued
real ATA flushes on both members; cold verification wrote zero bytes and its
disk hashes stayed unchanged. Original fixture hashes remained unchanged;
independent upstream zdb -bcc reports No leaks. Logs under build-aros:
management-clone-{link,qemu}.log, management-clone-cleanup-{link,qemu}.log,
management-clone-origin-{link,qemu}.log. Native service and manager rebuilds
also passed (management-clone-service-link.log, management-clone-ui-link.log);
these builds are not a claim of hosted GUI runtime verification.

Run with the native SDK/object environment described by the existing QEMU
tests: link-engine-test.sh aros/engine/qemu-management-test.c, then
AROS_TEST_ATA_OVERLAY and AROS_TEST_DOS_OVERLAY pointing at the private overlays,
`bash aros/engine/test-qemu-management.sh build-aros/qemu-run.88qtOZBN`.
Only owned disposable copies are attached to the no-network test VMs.

Later native DOS and crash-recovery work is documented in ../zpl/README.md.
Older dated sections below retain original evidence and limitations; they
must not be read as the current complete feature inventory.

### Clone follow-up: DOS mounts and native Exec IPC

management-run.kss54YWL verifies the cloned filesystem, its source snapshot
and the original source through actual DOS mounts on writer and cold-read
boots. A separate AROS client task uses Lock/Examine, Open/Read, EOF, Seek and
Close, and requires MODE_NEWFILE to fail with ERROR_DISK_WRITE_PROTECTED.
Each test client quiesces before its diagnostic mount endpoint is destroyed.
These are explicitly public readonly fixture mounts, not new authenticated
production writable mount support. Log: management-mounted-clone-{link,qemu}.log.

The unchanged production service message-processing sequence was extracted
into ui/broker.c and is linked by both native and no-engine service builds.
The QEMU test now uses oz_submit/oz_poll across real native Exec ports and
separate tasks, with that same broker performing validation/engine dispatch/
reply delivery. It no longer directly invokes dispatch from its caller.
This is still a diagnostic broker in the test executable, not a standalone
service-process startup or GUI runtime test. The existing three-second client
timeout is unchanged; a timeout fails the test rather than retrying a mutation.

management-run.UCbzrVBx passed ten management IPC requests in the writer boot
and two in the readonly boot, plus all six DOS mounted-dataset checks. Clone
independence, snapshot origin, readonly rejection, hashes, zero readonly disk
writes and zdb integrity gates still pass. Logs:
management-clone-ipc-link.log, management-clone-ipc-frozen-qemu.log,
management-broker-service-link.log, management-broker-noengine-link.log.
The word frozen in the log name denotes leaving the rerun inputs unchanged;
it is not a new harness snapshot feature.

Preserved harness mistake: management-run.IaBoyKST stopped with Bash unexpected
EOF because its script was edited while executing. This was not an engine
failure. The process was confirmed terminal before rerunning the unchanged
script as UCbzrVBx; do not modify executing harnesses or their binaries.

`bash aros/engine/build-nvpair.sh` currently builds upstream nvpair/fnvpair and
its allocators, with libtirpc 1.3.7's memory-only XDR sources. The dependency
archive is pinned by SHA-256; its license notices remain in the extracted
source. No RPC network service is built. The temporary static bootstrap archive
is not the final shared engine packaging.

Portable arithmetic/type definitions in the existing userspace SPL headers
are reused. The sysmacros wrapper omits its unavailable host include on AROS;
this does not import Linux device numbering or Linux memory allocation.
AROS assertion diagnostics identify the actual Exec task; assertions still
abort. Final port configuration must separate and audit all OS-specific paths.

## Native verification, 2026-09-18

Built with ADT's x86_64-aros cross compiler, using upstream's warning/aliasing
policy. `build-aros/build-nvpair-3.log` has checksum OK and no compiler warnings.
Fresh isolated hosted boot on :94 uses `hosted.conf` and `Startup-Sequence`.
The 262144-byte task stack is explicitly set by the AROS shell.

- `build-aros/hosted/nvpair-test.log`: 17 PASS, zero failures in AROS.
- `build-aros/nvpair-linux.log`: the same test against the Linux control's
  upstream libnvpair also passes all 17 checks.
- `cmp build-aros/hosted/nvpair-aros.xdr build-aros/nvpair-linux.xdr` succeeds:
  the nested nvlist with a path containing spaces and a high-bit 64-bit value
  encodes byte-for-byte identically. Truncated input is rejected by both.

That first nvpair test was a dependency-only pass. Subsequent engine/pool
results below supersede that initial boundary; the administration backend
integration and DOS filesystem frontend are still outstanding.

## Engine and platform bring-up

`compile-sweep.sh` compiles the 178 C sources listed by upstream libzpool.
`compile-deps.sh` compiles the 70 upstream x86-64 ICP, AVL, Unicode and Zstd
sources. `link-engine-test.sh` links the complete object set, without garbage
collecting uncalled engine functions to disguise missing symbols. zlib uses
the existing `z1.library` import library, not a reimplementation.

Native adaptations currently include AROS memory/CPU queries, real pthreads,
timed waits, chunked getentropy, explicit 64-bit off_t, and native byte order
conversions. Lua's structure tag and LZ4's local byte typedef are renamed to
avoid Exec namespace collisions, without changing algorithms. No MMU guard
pages are claimed. Optional ARC watch is rejected, not silently emulated.
Internal SPL errno values above 4095 require translation at the future DOS and
administration API boundary. File-vdev bootstrap rejects nonregular files;
raw-device discovery and partition suffix generation are not implemented.
The final machine identity must be provisioned and persisted; this bootstrap
only accepts an explicit nonzero 32-bit ZFS_HOSTID for write-mode tests.

The upstream tunable linker set is preserved with section-relative bounds in
`tunables.ld`, because the AROS loader consumes relocatable ELF. This is not
an empty substitute registry.

### Failure retained: positioned I/O beyond 4 GiB

**2026-09-29 qualification update:** the historical failure below is resolved
for the explicitly tested private hosted runtime with the nightly
`dos64.library` replacement and its original hosted `posixc.library`.
`hosted-offset.smDU8IeM` passes the original direct DOS64 and POSIX growth
probes. `hosted-offset.FARRTUNI` then passes the strengthened probe: six
positions bracketing 2 GiB/4 GiB, rereads of all earlier blocks after growth,
three sampled zero-filled holes, unchanged sequential position, fsync and
close. Both runners exit zero; exact library/probe hashes and outputs are
retained. Logs: `hosted-offset-dos64-only-1.log` and
`hosted-offset-boundaries-1.log`. The runner accepts explicit DOS64/POSIX
library and POSIX probe candidates but copies them only into a fresh private
runtime. No shared runtime or user session is modified.

The first comparison, `hosted-offset.czXDdgej`, also substituted native
`cli-isatty.wSnzlNPf/posixc.library` and trapped before probe output
(CR2=fffffffffffff4b8; deadline exit one). That is retained as a distinct
runtime compatibility failure, not a positioned-I/O result. The successful
combination does not qualify arbitrary library mixing, hosted engine pool
durability, or physical storage. The older failures and diagnosis follow for
provenance; their statements of unresolved status are historical.

`posix-io-test.c` has no OpenZFS dependencies. Inside isolated hosted AROS it
passes writes/reads at offsets 0 and 16384, but offset 4294967296 returns -1
with errno zero on both calls. File size stays 16896. The native platform
probe also failed: its file remained 512 bytes and both worker comparisons
failed. Upstream propagates errno, so this runtime defect originally became
a false successful ZFS return. The adapter now maps missing errno on failed
pread/pwrite to EIO. This does NOT fix large-offset I/O.

Evidence: `build-aros/platform-offset-mismatch.log`,
`build-aros/hosted/posix-io-test.log`, `build-aros/hosted/platform-test.log`.
the historical test archive the development test archive / the platform issue tracker asks the shared POSIX/DOS64 owner for diagnosis.
Only exclusively created disposable SYS: files in our isolated runtime are
used. No DUT, host storage pool or existing AROS private-format Work volume
has been opened. Later sections record the subsequent pool/RAID tests; this
large-offset failure is still unresolved.

2026-09-21 discriminator: this is not a general inability to address offsets
above 4 GiB. Private hosted run `build-aros/hosted-offset.vrvr7xRN` pre-sizes an
exclusive scratch file to 4294971392 bytes using SetFileSize64. Direct DOS64
seek/write/read and POSIX reads pass at 0, 2147483136, 2147483648, 4294966784
and 4294967296. Before extending the empty file, raw ACTION_SEEK64 returns
-1/IoErr219 (ERROR_SEEK_ERROR); dos64.library50.1 Seek64 returns -1/207
(ERROR_OBJECT_TOO_LARGE). The separate original POSIX growth probe still
fails at 4 GiB with errno zero and size16896; the combined runner exits1.

Source inspection of ADT dos64_support.c shows the matching mechanism:
dos64_UnsupportedPkt classifies every failed packet with an error as unsupported,
so a real past-EOF seek error is replaced by fallback's 32-bit range refusal.
posixc __fcb_seek only parks the offset on ERROR_SEEK_ERROR. Shared-source
integration remained pending; no shared library was changed.
The separate zero errno mapping and gap-fill performance still need work.

Reproduce with `-D_GNU_SOURCE` standalone builds of dos64-offset-test.c
(link -ldos64) and posix-io-test.c, then test-hosted-offset.sh. The runner
copies only OS directories into a fresh private runtime and owns a new Xvfb
and bootstrap process group. It retains exact input hashes and files; it
does not attach to existing AROS sessions. UhhqhF8V passed the direct probe;
run4 and vrvr7xRN retain the reproduced POSIX failure. Initial U1M3aqYA and
wVpaJTAW never reached the test because my copier omitted AROS.boot; those
setup failures are retained, not counted as I/O failures. Build1 lacked the
feature macro for pread; build2/3 with -D_GNU_SOURCE pass. Logs:
build-aros/hosted-offset-run*.log and dos64-offset-build*.log.

### Native engine initialization/shutdown PASS

Fresh isolated boot 10 links all upstream libzpool objects and completes
`kernel_init(SPA_MODE_READ)` and `kernel_fini()` with assertions enabled.
`build-aros/hosted/engine-init-test.log` ends with
`RESULT engine initialization and shutdown passed`. The real tunable registry
enumerates 290 entries and reads `zfs_arc_meta_balance=500` through its API.
`engine-init-errors.log` is empty. This does not open or import a pool.

Two failures were preserved before this pass:

- Boot 5: optional cache open returned false success after failed stat with
  errno zero, leaving an uninitialized pointer. All failed file calls now
  return a nonzero error, and open clears its output before attempting I/O.
- Boot 8: the exact AROS-window screenshot exposed the namespace mutex
  assertion. AROS's main pthread ID is zero, colliding with SPL's empty-owner
  sentinel. AROS pthread IDs now encode as id+1 at SPL boundaries, and decode
  before join. Mutex/RW-lock owners and curthread use the same mapping. No
  assertion is removed or disabled. Main-thread ownership and 2-thread I/O
  regressions pass. Other platforms retain their original identity mapping.

Opt-in `OPENZFS_BOOT_TRACE` emits only shutdown phase markers. Native probe
stderr is retained separately so an abort requester cannot be mistaken for
a silent hang. Logs and `engine-shutdown-boot8.png` remain under build-aros.

### Native mirror on-disk PASS, independently checked on Linux

#### Hosted 8 GiB matrix follow-up (2026-09-29)

`OPENZFS_TEST_LARGE_FILES=1` selects 8 GiB exclusive file members in the
standalone `pool-test.c` (discovery fixtures retain their existing size).
`test-hosted-large-pools.sh` clones a new private hosted runtime and runs
mirror/RAIDZ1/RAIDZ2/RAIDZ3 create/export/reimport/data roundtrips. It requires
an explicit test binary and DOS64 library, checks all fourteen member sizes,
then invokes independent readonly host `zdb -bcc` after ending its private
guest. It never accepts real-device paths or attaches to a user's session.
This exercises large member geometry and end-label I/O, not allocation of
8 GiB of payload or physical durability.

First run `hosted-large-pools.y4mHEanK` trapped before pool-test output when
using the current native/SMP-SDK-linked executable in the older hosted
runtime. The serial register dump and exact inputs are retained; the private
guest was stopped after the trap, and the runner exited one. This repeats the
SDK/runtime incompatibility class described in the hosted UI section, but
does not prove its precise mechanism. No pool result is claimed. Follow-up
uses a separately rebuilt, complete hosted-SDK pthread archive and a fresh
hosted-SDK engine source build, not mixed native objects.

The complete hosted-SDK source build `source-engine.v6hMwWvC` and matching
pthread archive `pthread-consistent.lvaVzQRp` compile/link successfully.
`hosted-large-pools.D5fH0jgJ` passes startup but its POSIX `ftruncate` physically
zero-fills the large backing member; the first file had allocated roughly
6 GiB before pool creation. That private guest was stopped to avoid filling
fourteen members. Logs/member remain as failed fixture evidence, not a pool
failure or completed run.

The large-file fixture now uses native DOS64 `SetFileSize64` after exclusive
file creation; ordinary smaller/discovery fixtures retain their original
sizing path. This deliberately isolates engine large-member geometry from
the separate POSIX gap-fill performance issue, which is not claimed fixed.

`hosted-large-pools.ItuPNwWs` passes the full four-topology runner, exit zero:
all fourteen members have exactly 8 GiB logical size; native upstream
create/export/reinitialize-readonly/import/payload comparisons pass; host
`zdb -bcc` traverses every pool and reports No leaks. Input binary/library
hashes match after the run. The new test is linked from the hosted-only source
build, not native/SMP objects. Logs are `hosted-large-pool-build-3.log` and
`hosted-large-pool-run-3.log`; exact inputs, source patch and independent zdb
outputs are retained and registered in the historical test archive. This is one hosted OS process
with engine reinitialization, not independent cold boot, full-capacity payload
allocation, physical disk or power-loss qualification.

The optional `AROS_TEST_POOL_READER` extends this runner with a separate hosted
process, readonly import and ordinary DOS reads for every topology. It hashes
the complete logical contents of all fourteen members before and after that
boot. The first run `hosted-large-pools.SnvxZKiJ` completed creator checks,
independent zdb and pre-read hashes, but exited before reader startup: reusing
the display-number file let the second Xvfb's truncation race the launcher's
read. Each phase now has its own display file, Xvfb log, configuration, serial
log and guest PID. This retained harness failure proves no cold result; the
corrected full rerun remains pending qualification.

Corrected `hosted-large-pools.lqBlBl8A` completes the full runner with exit zero
(`hosted-large-cold-run-2.log`). The creator and independent host zdb checks
pass for all four topologies. A fresh hosted process then imports their saved
exported configurations readonly and verifies topology, healthy state, normal
DOS reads/seek/EOF and write refusal for mirror/RAIDZ1/RAIDZ2/RAIDZ3. After that
process ends, all fourteen 8 GiB members match full logical-content SHA-256
hashes taken before the reader boot, including their holes. Test/library/tool
hashes also match. Both phases' PIDs, serial logs and configurations are
retained and registered in the historical test archive. This closes the independent-process large-pool
read gap for these fixtures; it uses fixture DOS mounts and explicit saved
configurations, not new production authentication or automatic-discovery
coverage. The broader durability/runtime limitations above remain.

Boot 12 used the coherent upstream debug feature configuration (DEBUG and
ZFS_DEBUG on, NDEBUG off), not just assertions with release-mode helpers.
The earlier incoherent build's dbuf assertion and binary remain as boot 11
evidence. All 178 engine and 70 dependency sources compiled in this mode.

`pool-test.c` creates exclusive 256 MiB files in a fresh random directory,
checks their sizes and end-of-file read/write/fsync, then calls upstream
spa_create, spa_open and spa_export. The test links with AROS `-nix` for its
standard POSIX path adapter; no vdev path assertion was relaxed.

Boot 12: `aros_test_6041b499`, GUID 8543306608869341522, two-file mirror:
create/open/export all returned zero, shutdown passed. Linux userspace
`zdb -e -p <runtime>/OpenZFS-test-6041b499 -bc aros_test_6041b499` independently
traversed all 42 metadata blocks, verified their checksums and reported no
leaks. Evidence: `build-aros/pool-mirror-pass-boot12.log` and
`build-aros/native-mirror-linux-zdb-boot12.log`.

This verifies a real AROS-created upstream-format pool, NOT user file I/O,
AROS re-import, degraded recovery, RAIDZ, large-vdev support or a mounted DOS
handler. The 4 GiB hosted I/O failure remains open. The pool test now accepts
only mirror/raidz1/raidz2/raidz3 topologies; it never accepts a device path.

### Native RAID topology matrix

Boot 13 passes native create/open/export for mirror, RAIDZ1, RAIDZ2 and RAIDZ3.
Independent upstream Linux `zdb -bc` traversals of all four exported pools
pass with no leaks. Exact logs, source revision and native binary SHA-256:
`build-aros/linux-verify.XZJ8sWjm/`. The read-only verification runner is
`bash aros/engine/verify-native-pools.sh`; it validates strict test-directory
and pool-name patterns and never loads a host kernel module or mounts a pool.

The extended payload probe uses the actual upstream DMU transaction API to
allocate an object and write 16 KiB, sync, export, shut down the engine,
reinitialize read-only, import and compare every byte. This is a DMU object
test, not an AROS DOS file test or a separately restarted OS instance.
Boot 14 reached successful import/read/compare but its final verdict was FAIL
because the probe wrongly expected export in globally read-only mode to work.
Upstream spa_export_common explicitly returns EROFS in that mode. The test
now asserts that refusal and lets kernel_fini unload the read-only pool.
The original FAIL logs are retained as `pool-*-readonly-expectation-boot14.*`.

Boot 15 PASS for all four topologies: 16 KiB native DMU write, export, complete
engine shutdown, read-only engine reinitialization/import, exact byte comparison,
expected EROFS on read-only export, and clean unload/shutdown. Independent Linux
`zdb -bcc` verifies **data and metadata** checksums and finds no leaks in each
pool. Complete native logs, empty stderr logs, zdb output and build provenance
are in `build-aros/linux-verify.nmkmAJrj/`. The verification script now requires
both topology and data-roundtrip PASS records and checks all block checksums.

## Native management integration

`bash aros/engine/build-management.sh` builds the full-engine diagnostic service,
native IPC test and existing Zune manager under `build-aros/`. This is temporary
static diagnostic packaging, not the final shared library or mounted handler.
`OpenZFSServiceNative` initializes upstream read-only, disables cache discovery
with an empty cache pathname, and implements hello, capabilities and pool.list.
Pool enumeration holds the real SPA namespace lock. No import, discovery,
mutation or capacity-statistics support is advertised. Other operations return
UNSUPPORTED, not success. Pagination describes a live namespace view.

`Startup-Management-Test` runs the dedicated NativeTest port acceptance checks,
requests clean shutdown, then starts the normal service and Zune manager.
Boot 16 failed all submits because the launch command had two redirections;
`management-test-boot16-failed.log` retains that harness failure. Boot 17 passed
all six IPC checks and full shutdown (`management-*-boot17-pass.log`). Boot 18
repeated the pass and the exact named AROS-window capture
`build-aros/management-boot18.png` shows the manager reporting the real engine
available with pool.list capability. These initial checks query an **empty**
namespace; populated pool enumeration and UI imports are not yet established.
The runtime is isolated on display :94; user :10.0 and DUT are untouched.

Boot 19 repeats all six IPC checks with the manager's actual confirmation and
dry-run flag defaults. Clean shutdown is logged. The service was then relaunched
and the GUI's Pools/list/Send request clicked: request 2, operation 3, status 0
in `management-live.log`; `management-pool-list-pass.png` shows the corresponding
real empty-namespace reply. Built/deployed service SHA-256:
`f837cbf6ca1d842843f878e6a62d4f76f1af487c234bab7468281fc510f7cb8a`.
Final boot-19 IPC/service logs are retained directly under `build-aros/`.

## Still required for the full AROS handler

### September 19: cold-boot import through the actual management API

`Startup-Import-Fixture` creates a new exclusive file mirror. The pool test now
saves the actual upstream export configuration using `nvlist_pack(NV_ENCODE_XDR)`
as `export.nvlist` before its own read-only comparison. This is upstream nvlist
encoding, not a new pool format. `Startup-Management-Test` reads the completed
fixture log and imports it via the service after a full hosted restart.

The initial import capability is deliberately bounded: operation 7 requires
target=pool name, argument=/SYS/OpenZFS-test-XXXXXXXX/export.nvlist,
options=readonly=on, and only the confirmation flag. All vdevs must be regular
disk0..disk4 files in that same fixture directory. Only root/mirror/RAIDZ trees
are accepted; no device scans, spare/cache devices or force/dry-run semantics.
Configuration size is bounded to 1 MiB. Engine/adapter errors return the appended
OZ_ENGINE_ERROR status with the numeric error; unsupported flags are explicit.

Boot 20 created `aros_mirror_39d95c0b` and verified its DMU payload. Boots 21 and
22 cold-imported it through native IPC: 14 checks PASS, including no-confirmation,
dry-run and outside-fixture rejection, actual import, exact populated list name,
duplicate import rejection and clean shutdown. Boot 22 used the warning-free
build (`build-management-import4.log`). Then the Zune UI imported the same fixture
through the normal service port and listed it ONLINE. Named-window evidence:
`build-aros/import-ui-preview.png`, `import-ui-confirm.png`, `import-ui-result.png`,
`import-ui-populated-list.png`. Logs: `management-import-boot22.log`,
`management-import-service-boot22.log`, `management-import-ui-boot22.log`.
Independent Linux `zdb -bcc` found no checksum errors or leaks (59 blocks):
`build-aros/import-fixture-linux-zdb.log`.

Both complete disk hashes match before and after IPC and GUI imports:

- disk0: `071419ca3dc570aa00564ee1a444a59b6527de7822398592dfb1edfc4da56557`
- disk1: `601a1e6e68951ea2daab67cad3e33f0f75707050e5bfe044aae9ec5792f38d34`

This establishes a separate-boot **native-created mirror** import, not arbitrary
Linux pool import, mounted DOS files, general label discovery or production
shared-library packaging. Read-only imported pools unload via engine shutdown;
administrative export in global read-only mode remains upstream EROFS.

### Remaining work

### Writable fixture administration and persisted snapshots

The diagnostic service accepts `TEST-WRITE` on the dedicated NativeTest port.
This explicitly initializes a writable engine but still permits imports only
from the validated owned regular-file fixture configurations. Normal service
startup remains read-only. Import accepts `readonly=off` only in TEST-WRITE;
export and single-dataset snapshot operations are advertised only in that mode.
Both require confirmation and reject dry-run/force/extra options. Recursive
snapshots, destruction, rollback and general pool administration are not wired.
Dataset listing accepts `snapshots=on` and returns real snapshot rows.

New boot-24 mirror: `aros_mirror_92251a51` (the earlier read-only fixture is
unchanged). Boot 25 exposed a missing caller-side per-pool writability check:
the service aborted on a snapshot call against a readonly imported pool. The
client timed out; the screenshot confirms an abort rather than a stalled host.
Logs `write-test-boot25-failed.log`, `write-service-boot25-failed.log` and
`snapshot-timeout-boot25.png` retain the failure. The original stderr was not
captured, so the exact assertion text is not claimed. The adapter now rejects
that path using `spa_writeable` before entering the low-level snapshot call.
The test now stops on timeout instead of submitting further unknown-outcome
mutations. Test-service stderr is explicitly retained in
`SYS:management-native-errors.log` (copy it before another test launch).

Boot 26 PASS: read-only snapshot rejection EROFS; real export; writable import;
confirmation/dry-run rejection; actual snapshot creation; duplicate EEXIST;
three-row snapshot listing; real export; empty pool namespace; clean shutdown.
Logs: `build-aros/write-test-boot26-pass.log`, `write-service-boot26-pass.log`,
empty `write-errors-boot26.log`. Service SHA-256:
`69469a874ca29fc8d65d6965a36ee04f88f1af8641d8c3d68135f58dc9090e20`.

Boots 27/28 cold-import the exported fixture read-only and find
`payload@api_snapshot`, with write capability absent and clean shutdown.
Boot 28 also launches an independent fresh native engine which reads object 1
from that snapshot and compares all 16384 bytes with the original pattern:
`snapshot-bytes-boot28.log` PASS. This uses upstream DMU reads, not a UI stub.
Independent Linux `zdb -bcc` verifies all 66 blocks and no leaks
(`snapshot-linux-zdb.log`); `snapshot-linux-objects.log` independently identifies
the snapshot and its one 16 KiB data object. All paths are under `build-aros/`.
`Startup-Write-Test` must use a fresh fixture; rerunning creation against an
existing snapshot is intentionally not silently destructive/idempotent.

### Outstanding integration

Dataset enumeration is now bound to upstream `dmu_objset_find`,
`dmu_objset_hold`, `dmu_objset_type` and `dmu_objset_space`. Operation 36 requires
a target; recursive selects children, otherwise only the target is listed.
Snapshots are included with options=snapshots=on; property filters remain
unsupported. Results are paginated by live
traversal index and reject names too long for the current ABI rather than silently
truncating them. Referenced bytes are explicitly labelled, not substituted for
ZFS used bytes. All rows state that no DOS mount exists.

Boot 23: fifteen IPC checks PASS including root and payload names/types and
clean engine shutdown. Payload reports referenced=131072, available=125112320,
objects=1; root reports referenced=8192, objects=0. GUI import and recursive
Datasets/list both return OK and show the two real rows. Evidence:
`build-aros/management-datasets-boot23.log`,
`management-datasets-service-boot23.log`, `management-datasets-ui-boot23.log`,
and `datasets-ui-pass.png`. Warning-free build `build-management-datasets1.log`;
matching built/deployed service SHA-256:
`4f1a77d72f1644186c1520b52332ab7a508d17b7cba9318c0b232a66981618c2`.
The >24-row boundary is now verified by the native pagination fixture above;
the historical boot 23 GUI run did not cover that boundary.

- Repair/verify native large-offset I/O (the platform issue tracker); small file-vdev tests do
  not establish large-device support or native NVMe flush durability.
- Shared engine packaging and remaining management-service operation bindings;
  the existing UI must continue reporting unavailable operations honestly.
- The AROS DOS filesystem frontend and upstream ZPL integration: this probe's
  DMU object is not yet an ordinary file accessible through AROS DOS packets.
- Separate-boot imports, Linux-created pool imports, feature/property matrix,
  snapshots/clones/send-receive, encryption, degraded repair/resilver, crash
  recovery, and sustained memory-pressure/SMP testing.

## Native three-device RAIDZ2 and two-missing-member cold recovery

The native fixture now accepts `DISPOSABLE-RAIDZ2`, setting the real upstream
RAIDZ vdev's `nparity=2` over three admitted ATA partition extents. The existing
mirror/RAIDZ1 modes are preserved. The runner selects this with
`AROS_TEST_RAIDZ_PARITY=2`; it creates only private raw disk files, checks bytes
outside the partitions, and requires the exact parity-specific success marker.

The initial full-dispatcher fixture link failed on `zfsctl_snapshot_unmount`
(`native-raidz2-fixture-build.log`). This standalone test does not supply the
management service's snapshot-unmount callback. The normal standalone fixture
configuration built successfully (`native-raidz2-standalone-build.log`). This
is not a claim that the full-dispatcher fixture link was fixed.

Verified QEMU runs, using the private worker-wakeup QEMU and native DOS/ATA/
partition/POSIX overlays already recorded by each runner:

- `native-raidz-run.F2wJ9rOb`: real RAIDZ2 creation, DMU and ZPL/DOS writes,
  export/reimport, repeated DOS reads; partition-exterior comparisons pass.
- `native-import-run.r6oPqApA`: a fresh guest running the full
  `full-zpl-properties/OpenZFSServiceNative` imports all three members, writes
  through DOS, creates and retains the NFC/UTF8/case-insensitive dataset, then
  exports/reimports and verifies the data and all three namespace properties.
- `native-import-run.IOCNhrFs`: an independent cold guest reads child overlays
  of that writer, verifying ordinary data, normalized filename lookup and
  namespace properties. Pool health is ONLINE.
- `native-import-run.8A8wW1YZ`: an independent cold guest attaches only unit 0
  (units 1 and 3 are absent). The full service reports DEGRADED and passes the
  same exact readback and namespace checks. Every raw backing file, writer
  overlay and new read-only overlay retains its original hash.

All four runners exit zero. The last two use `AROS_TEST_NATIVE_WRITABLE=2`
(read-only verification, not write mode), with `AROS_TEST_KEEP_NAMES=1`.
The two-missing-member case additionally uses `AROS_TEST_NATIVE_MEMBERS=0`;
the runner requires a healthy RAIDZ2 writer as its backing source. Logs,
binaries, build source snapshots and hashes remain in each evidence directory.
This closes native QEMU RAIDZ2 healthy/cold/degraded-read coverage, not RAIDZ3,
replacement/resilver, arbitrary crash recovery or physical-device durability.
No DUT or user pool was accessed.

### DOS replacement failure preservation (2026-09-30)

`qemu-run.nrB3QmWs` reproduced data loss when MODE_NEWFILE removed its old
target before a failing create (ENOSPC). The replacement now prepares an
empty same-directory file, then uses the upstream rename transaction with
source/target object-and-generation checks and publication-time native and
upstream access checks. This path requires the existing single DOS owner;
it is not a concurrent mount-worker API. Cleanup never removes a published
replacement after a later sync error and only targets the prepared identity.

`qemu-run.lJzmXm7h` exits zero through all five native mirror boots. Both
injected handle-allocation and replacement-creation failures preserve the
original identity and bytes. Normal replacement/exclusive access, cold
reopen and survivor reads pass; backing hashes remain unchanged. Logs are
registered to the development test archive. This is diagnostic-build coverage, not production
bundle qualification or proof of arbitrary crash durability. Publication
failure, sync failure and negative identity-guard cases remain to test.

Follow-up `qemu-run.APFhK8FP` passes injected failure inside the real guarded
rename, preserving the original and removing the temporary file. The next
regression, `qemu-run.gW4dCvtx`, exposed deferred old-object reclamation after
successful replacement (create boot fails; staged older import/reopen binaries
were never run). Replacement now syncs, drains the unlinked set under the same
exclusive DOS-owner admission as remove, and syncs again. An old object with
another hard link is not unlinked and must remain readable.

`qemu-run.i2NGRPZq` passes all five boots with freshly rebuilt diagnostics:
stale source and target generations reject before the policy callback; denied
publication preserves original identity and bytes; successful last-link
replacement reclaims the old object; a surviving hard link retains its data
until removed. All three injected failure stages and temporary cleanup pass.
Both failure and success evidence sets are registered to the development test archive. Exact
shared-bundle qualification and post-publication sync-error injection remain.

Exact shared bundle `native-bundle.5AkE9Pvn` / `shared-engine.ksynYXVZ` passes
`native-import-run.CAaNftd0` (runner exit zero, backing hashes unchanged), using
the documented private runtime prerequisites. Staging-policy negative checks
also pass. `qemu-run.tihIhnV4` passes five fresh diagnostic boots and adds a
synthetic error returned after successful publication: Open fails, the new
empty destination remains readable, and no temporary name remains. This
verifies the caller's post-publication cleanup boundary, not a real device
flush error or power-loss durability. All results are registered to the development test archive.
