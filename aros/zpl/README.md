# Upstream writable ZPL integration lane

## Mount close lifecycle ownership (2026-10-04)

Close now enforces the creating task's lifecycle ownership before unpublishing
the mount, flushing/closing its ZIL, or freeing caches. A foreign task receives
EDEADLK, matching suspend/resume/end. Upstream publication references protect
ioctl borrowers; they do not account for every transient native DOS pointer
used by the owner. This does not add task affinity to ordinary file I/O or
replace the requirement to serialize lifecycle transitions against DOS work.

Private negative run `qemu-run.Dt8q0vRD` demonstrates the missing enforcement:
a foreign pthread closes the writable mount (errno=0); the lifecycle assertion
fails and the runner exits 1. This quiescent-owner test does not itself prove
a concurrent use-after-free. The test nulls the consumed pointer so it never
tries to reuse the freed negative-control mount.

Fixed run `qemu-run.xshA6Wig` passes foreign writable close refusal (errno=11,
result=0), followed by normal owner metadata/I/O/reopen/lifecycle checks.
The complete five-boot matrix finishes with exit 0 and unchanged original
backing hashes. Read-only foreign close refusal also passes during cold mirror
import and each single-member survivor boot. The mirror runner now requires
the matching owner-test marker for creation and every read-only phase.
A separate normal service build under external `mount-owner-20261004/production`
and shared library `shared-engine.9lLjoDAt` build successfully. Acceptance
package `native-bundle.eIOftMj2` passes production run
`native-import-run.GgVFlH8d` (16 CPUs, 8192 MiB): full enabled CLI/handler/zvol/
lifetime/vdev/repath checks and 320-file DOS pressure with read-only reimport.
The runner exits 0 and original backing hashes are unchanged. This acceptance
bundle uses the existing Manager validation harness, not an interactive UI.

## Synchronous namespace failure contract (2026-10-04)

The native create, mkdir and symlink adapters must return a null output znode
on error. They previously published that reference before the final
`sync=always` ZIL commit. A failed commit therefore returned an error together
with a live reference. Commit acknowledgment now precedes reference publication;
failure releases the adapter's reference. This does not undo a namespace
transaction that already committed, and callers must not assume rollback.
The DOS create wrapper already released a returned reference on error, so this
finding alone does not demonstrate a DOS-path reference leak.

Private mirror diagnostics wrap `zil_commit` only in the pool/import test
links. Each armed call returns EIO once for the target log and object zero;
all unarmed commits call the real implementation. The test checks the error,
null output, injection count and absence of an extra cached reference after
looking up the newly created entry. It covers files, directories and symlinks.
Normal service/library links do not contain this injection wrapper.

Negative run `qemu-run.nUziw9Na` catches the original create bug:
`Native create sync-failure injected=1 result=5`. The subsequent dirty-atime
fixture teardown is secondary to that deliberate test failure; the runner
subsequently exited 1 at its 900-second deadline. Fixed run
`qemu-run.7TQzo1Y7` passes all three `Native namespace sync-failure` cases
(kind 0/1/2, injected=1, result=0) and normal write/fsync/append. Its full
five-boot regression passes with unchanged original backing hashes.
Import/reopen binaries in this run
are the preceding cache baseline, not rebuilt namespace candidates. Neither
this diagnostic nor a later pass qualifies physical-device power-loss durability.

The subsequent all-current-binary run `qemu-run.hFdvn0W2` also completes all
five boots with exit 0 and unchanged backing hashes. Its creation binary and
fresh import/reopen binaries include the corrected namespace adapters; the
runner requires all three injected-failure markers. Normal service build
`create-sync-20261004/production` has full_dispatcher=1, service_test_modes=0
and status_trace=0, and no `__wrap_zil_commit` symbol. Its shared library is
`create-sync-20261004/shared-engine.JtZ1scvd`; acceptance package
`native-bundle.A6OSbn6K` is staged beneath that same external work directory.
The first production attempt `native-import-run.tyxhRMw4` did not boot:
its nested artifact root exceeded QEMU's Unix-socket path limit. A retry at
the shorter root rejected the still-nested bundle at path admission. Neither
is a guest failure. The same binaries are restaged as top-level
`native-bundle.jPFSirR8`; production run `native-import-run.aUu6seKU`
passes with exit 0 and unchanged original backing hashes. This private
16-vCPU/8192-MiB run covers full upstream CLI integration, native handler,
library lifetime, zvol, vdev/property and relocation checks, and 320-file
DOS creation/enumeration/readback including read-only reimport. Its normal
engine has no fault wrapper; the Manager executable in this acceptance bundle
is the existing validation harness, not the normal interactive UI.

## Imported Unix mode fallback (2026-09-30)

Expanded matrix `qemu-run.i8H6dLj8` passes exact protection values for
0700, 0070, 0007, 0000 and 0600 across ZPL versions 1-5. It verifies absent
native attributes, original mode metadata, Examine/ExNext/ExAll output and
readonly write/delete masking. Five boots cover creation, cold mirror, either
survivor and writable reopen; original backing hashes remain unchanged.
Normal hosted production refresh `hosted-security-test.5uuNvzTH` also passes
authentication, both loader APIs on/off/on, persistent mirror, cold reads and
degraded assign checks. Both evidence sets are registered to the development test archive.
Logs: `mode-matrix-*` and `mode-production-hosted-*`.

Public readonly repetition `qemu-run.0rE4LFnu` also passes the five-boot
matrix with `AROS_TEST_SECURITY=0`, exact mode metadata/read-only masking,
ordinary readback, cold mirror/survivors and unchanged backing hashes.
Log: `mode-matrix-public-test.log`. This verifies the explicit public fixture
path, not a relaxation of authenticated production authorization.

Files lacking the AROS native protection attribute previously reported zero
DOS protection, incorrectly advertising execution even for Unix mode 0600.
`qemu-run.j012vNk0` reproduces the failure at the first ZPL version's ExNext
check. The metadata bridge now preserves upstream mode and maps its rwx bits
to DOS owner-deny/group-other-allow representation for Examine and ExAll.
An existing native protection attribute retains precedence. Delete protection
is not inferred from Unix file mode; upstream parent/ACL checks remain
authoritative. This is mode metadata, not a complete ACL-to-DOS translation.

Five-boot native regression `qemu-run.TXEIR0pB` passes ZPL versions 1-5,
including ExNext/ExAll execute denial for imported 0600 nodes, normal payload
readback, cold mirror and either survivor, writable reopen and unchanged
original backing hashes. Both runs are registered to the development test archive. Logs use
`imported-mode-before-` and `imported-mode-fixed-` prefixes. Further mode/ACL
combinations and production shared-bundle refresh remain to be verified.

## Imported Unix special-file metadata (2026-09-30)

Expanded diagnostic `special-file-test.c` explicitly creates ZPL versions
1 through 5. Five-boot run `qemu-run.U2zPljyl` passes enumeration, DOS
ExNext/ExAll/Examine, refusal of unsupported special-node opens and ordinary
file readback for every version during creation, cold mirror import and
either single-survivor import. The suite also passes writable reopen and
unchanged original backing hashes. Evidence is registered to the development test archive;
this is metadata compatibility, not Unix special-file I/O support.

Before-fix native run `qemu-run.z4xiOmtF` creates real upstream FIFO, socket,
character and block-device nodes, then fails enumeration at the first entry
with ENOTSUP (86). `oz_read_mount_stat` rejected every non-regular/directory/
symlink node, so one ordinary imported Unix directory could become unlistable.

The internal metadata bridge now retains these four special kinds. DOS can
enumerate and lock/examine them; FIFO maps to ST_PIPEFILE. DOS has no distinct
socket/character/block entry types, so those use its generic ST_FILE display
type, while internal kind/generation validation remains distinct. Unknown
on-disk types still fail explicitly. This is metadata interoperability, not
an implementation of Unix FIFO/socket/device I/O. No represented device is
opened. Ordinary input/update/output, OpenFromLock and direct stream opens
reject special nodes; output rejects before unlink/replacement.

`special-file-test.c` keeps a separate `qemu_mirror/specials` dataset with
four special nodes plus a normal payload file. It checks public ZPL
enumeration/type, zero-length read refusal, native ExNext/ExAll, per-entry
locks/Examine, old/update/new open refusal, OpenFromLock refusal, normal-file
bytes and unchanged special object identities/types/size. Native run
`qemu-run.scAIpmhN` passes all five boots and authenticated special checks
during creation, cold mirror and either sole survivor. Original backing
hashes remain unchanged during overlay boots. Transcript:
`special-auth-test.log`; evidence registered to the development test archive.

The public readonly diagnostic path independently passes all five boots in
`qemu-run.R6hTqpdK` (`special-public-test.log`), including cold mirror and
each survivor, with original backing hashes unchanged. This fixture mode is
not a replacement for production authentication.

Preserved partial failures `qemu-run.wsBQ1BeG` and `qemu-run.Gys8KnrD`
enumerate all five entries but fail the new DOS fixture. Diagnostic
`stage=1 ioerr=224` identified missing test-thread authentication, not a
production access failure. The test now explicitly logs into the private
oztestroot account and restores its prior identity; handler authentication
was not weakened. Initial compile failures (wrong ZPL compile context,
unavailable ATTR_RDEV/secOWNER_ROOT symbols) remain in `special-*-build*.log`.

## Native DiskInfo startup descriptor repair (2026-09-30)

`hosted-large-pools.VzVtsbxo` exposed a real handler bug: opening Wanderer's
DiskInfo for OZRAID crashed in `strlen(NULL)`. The published FileSysStartupMsg
had a valid environment but a null `fssm_Device`; DiskInfo reads that BSTR
whenever startup metadata exists. This was not capacity overflow or a reason
to patch around the failure in Wanderer. The handler now supplies a valid,
static empty BSTR: the logical dataset does not pretend to be a single Exec
device. Both FAST_BSTR and BCPL layouts use the platform macro, and the string
outlives retained endpoints. The DOS metadata regression previously expected
the invalid null value and is corrected, including its detach/reuse checks.

Fresh private run `hosted-large-pools.2SRKZ8Wd` passes startup validation,
mounted reads and readonly refusal. Actual DiskInfo opens and displays 51.8T
(1,737,661,400 blocks of 32K); browsing succeeds afterward. All drawers were
closed before detach, and all five sparse member structural digests remain
unchanged. `02-info.png` and `03-browse-after-info.png` capture this result;
`hosted-array50-diskinfo-fixed.log` ends PASS. Both failure and success sets
are registered to the development test archive. This remains a disposable local hosted fixture,
not a real remote array or physical-durability qualification.

The screenshot also records unresolved native DiskInfo presentation issues:
filesystem is shown as Unknown and nearly all free space as 1%. The independent
DOS block counts and byte totals agree; do not claim complete desktop metadata
presentation based only on the repaired crash.
The inspected ADT `DiskInfo/support.c::FormatBlocksSized` computes
`100 * blocks / totalblocks` in ULONG arithmetic, overflowing on this block
count; its byte-capacity calculation already widens to UQUAD. That separate
consumer fix is not part of the handler descriptor change.

The rebuilt full production dispatcher (`source-engine.P2gN7Bsv/diskinfo-production`)
also passes the writable native QEMU mirror/CLI/ZVOL/DOS suite in
`native-import-run.bdpFPr2E`, with original backing hashes unchanged. It uses
the explicit private POSIX/pipe/stdio runtime profile, not a stock nightly.
The deeper diagnostic run `qemu-run.x7VSqTAJ` is retained as FAIL: regular
mount metadata and retained-endpoint checks passed, but its separately
constructed adopted-startup fixture still had a null BSTR. The fixture now
supplies the valid empty BSTR and verifies that adoption and retirement
preserve the caller's exact pointer. This is a test correction, not permission
for the handler to rewrite caller-owned startup metadata.

Corrected native diagnostic run `qemu-run.EZ15tG1y` exits zero across all five
boots: create, cold mirror, each surviving member separately, writable reopen.
Authenticated and public metadata queries, retained endpoint/remount checks,
and adopted startup rollback/read/retirement report zero. Original backing
hashes match after the overlay boots. Transcript:
`diskinfo-native-diagnostic-test-3.log`; evidence registered to the development test archive.
The initial diagnostic invocation (`diskinfo-native-diagnostic-test.log`)
exited at preflight while the third binary was still building; it did not
boot a guest and is not a regression pass or a guest failure.

## Large-pool Wanderer visualization (2026-09-30)

`AROS_TEST_ARRAY50_GUI=1` adds an isolated desktop inspection phase to the
50 TB-class hosted fixture. `hosted-large-pools.HARMUGgy` visibly shows the
real `OZRAID` volume, `hello.txt` and `subdir`, and `subdir/nested.txt` in
Wanderer. Named AROS window captures are `01-desktop.png`, `02-volume.png`
and `03-nested.png`. The reader separately reports 56.94 TB available through
DOS Info, exact file-byte reads and write refusal. Capacity was not inspected
in a graphical Info dialog. This is a local diagnostic mount, not the
authenticated production service or remote server-sharing path.

After closing both drawers, creating this run's `array50-gui-finish` marker
requests storage detach. The fixture keeps servicing the retired endpoint
while Wanderer can still hold notification/DeviceProc references; the host
then stops its own private process group. The endpoint is deliberately not
freed or claimed globally quiescent. Detached PASS and unchanged structural
digests of all five members are required. Runner exit is zero; log:
`hosted-array50-gui-test-2.log`. Evidence is registered to the development test archive.

Earlier `hosted-large-pools.rquSqHhV` has correct visual captures but FAILED
fixture teardown with EBUSY: it attempted joined endpoint destruction while
Wanderer was alive. It was stopped only after the explicit failure and is
retained/registered as failed evidence. No production handler lifetime
checks were relaxed to make the fixture pass.

## Real 50 TB-class hosted pool and DOS mount (2026-09-30)

`AROS_TEST_ARRAY50=1` selects five exclusive, newly created 13 TiB sparse
file members in RAIDZ1, rather than the default four-topology 8 GiB matrix.
The creator and independent cold reader both use the real upstream engine.
The reader requires capacity above 50,000,000,000,000 bytes and checks actual
DOS Info against ZPL accounting, reads real files and refuses writes.

Runs `hosted-large-pools.T9LTuXKo` and `hosted-large-pools.dxRakbWt` pass
creation, export, zdb checksum/leak verification and the separate cold DOS
reader. The first reports 56,939,688,495,360 available bytes and 287,936 used;
Info uses 32 KiB reporting blocks to fit the legacy block-count interface.
This observed scaling, not a presumed DOS64 ABI, is what the test proves.
Both runs exit zero. Logs: `hosted-array50-test.log` and
`hosted-array50-test-2.log`; the second also hashes the sparse verifier input.

All five member sizes, hole layouts and allocated bytes have identical
structural digests before/after the stopped read-only guest. This is not
the SHA256 of every logical byte: allocating a zero-filled hole changes the
digest too. The helper rejects symlinks, unsupported extent queries and
detected concurrent changes; its separate tests include a 13 TiB file,
high-offset changes, zero allocation and resizing. Evidence occupies about
349 MB per run, not 65 TiB of physical space.

This verifies large sparse-pool geometry and selected mounted-file access.
It does not prove full-capacity workloads, a visible Wanderer drawer at this
scale, remote sharing or hardware durability. No user array or DUT is used.

## Large native device geometry (2026-09-30)

The separate device-layer capacity runner accepts only
`AROS_TEST_CAPACITY_TIB=3` (default) or `50`. `capacity-run.F6ZCWwT8`
passes at 54,975,581,388,800 bytes (50 TiB): exact final-sector pattern,
the same pattern through a bounded end-of-device range, out-of-range read
rejection and read-only write rejection. Both qcow2 hashes remain unchanged.
The separate GPT disk stays at 3 TiB and verifies a partition starting above
the 32-bit LBA limit. `capacity-run.3Fo6xVDO` passes the original default
case after the change. Both runners exit zero; logs are
`capacity-50t-test.log` and `capacity-default-regression.log`.

The 50 TiB qcow2 occupied 1.1 MB physically; no huge raw disk allocation or
user device access was involved. This is native virtual-ATA geometry evidence,
not creation/import of a 50 TiB pool, desktop capacity display at that scale,
remote server access or physical durability qualification.

## Desktop capacity accounting verification (2026-09-30)

The cold RAID DOS reader now calls real `Lock`/`Info` on each mounted
volume and compares total and used block counts with upstream ZPL space
accounting, including the used-block rounding rule. It also requires a
volume node, in-use status, and read-only disk state. This tests the DOS
information consumed by desktop tools, not a synthetic InfoData object.

Private hosted run `hosted-large-pools.9eaT60X2` passes mirror and RAIDZ1/2/3
creation/roundtrip, independent zdb integrity, a separate read-only guest
with all four capacity checks, file reads/write refusals, and unchanged
full logical-content hashes of all fourteen 8 GiB sparse members. The
runner exits zero (`hosted-capacity-matrix.log`); the new four-marker
assertion was separately checked against that captured reader log.
Both hosted and native reader builds pass. Only the hosted reader was run.

These 112 GiB logical fixtures occupy roughly 370 MB physically. They do
not establish 50 TB geometry, full-disk performance, remote-share access,
physical durability or a new visual Wanderer result. No DUT, shared
hosted instance or remote pool was accessed.

Native credential primitives and their verified/unfinished boundaries are
documented in [CREDENTIALS.md](CREDENTIALS.md). They replace AROS libzpool's
unconditional-root getters; they do not establish a production auth boundary.

This is the next port layer, NOT a second filesystem implementation. The
read-only SA/ZAP fixture adapter is a verified bootstrap, not the final write
path. Writable DOS operations must reach upstream ZPL/ZIL semantics, including
range locks, size/timestamps, ACL checks, intent logging, replay and unlinked
object cleanup. Direct DMU writes alone do not prove a writable filesystem.

The existing libzpool engine excludes `_KERNEL` sections. `compile-sweep.sh`
builds candidate upstream ZPL translation units in a separate object directory,
with `_KERNEL` exposing their structures but without Linux `__KERNEL__`.
It preserves each failure log. This does not imply ABI compatibility with the
existing engine: all shared layouts and omitted kernel-only entry points must
be checked before linking. No success-returning placeholder functions are
permitted to turn a compile/link pass into a fictional runtime capability.

AROS-specific interfaces belong under this directory; retain upstream
algorithms in module/zfs. Missing direct-I/O/mmap/credentials support must be
implemented or explicitly rejected at the boundary, never silently accepted.
Only disposable hosted/QEMU pools may be written by experimental builds.

## Dataset-backed native mount initialization

`oz_zpl_mount_metadata` now adapts the portable metadata portion of upstream
`zfsvfs_init`: ZPL/pool version compatibility, case/normalization/UTF-8/ACL
settings, FUID/SA support, default quotas, root/unlinked objects, optional
quota/FUID/share object IDs, and initial recordsize. It uses upstream
`zfs_get_zplprop` and ZAP lookups, and holds the pool configuration lock for
the DSL property lookup. It does not fabricate an empty quota configuration.
The read mount and the separate upstream vnode-write acceptance mount use it.

The write fixture persists a non-default 64 KiB recordsize, a 1 GiB default
user quota and a real empty user-quota ZAP. Its initializer checks these on
creation and all three cold imports, then deliberately uses the existing
16 KiB record size for the bounded vnode-write test. This verifies metadata
loading, not enforcement of quota exhaustion or live property changes.

The current bootstrap path resolver still lacks normalized/casefolded lookup
and UTF-8 policy; such mounts now return ENOTSUP instead of presenting different
namespace semantics. Full support remains required. This initializer is not
full writable mount setup: property callbacks, intent-log replay, unlink
cleanup, writable namespace lifetime and DOS mutation operations remain open.

Failed native attempt `build-aros/qemu-run.rgn5h2n7/` is retained: the initial
DSL property call lacked its required configuration lock and aborted in
`dsl_prop_get_ds`. After adding that lock, all four mirror/handler boots passed
in `build-aros/qemu-run.tCKmiqEq/`, with no cold writes or changed backing hashes.
Compiler failure before that is retained in `build-aros/mount-metadata-link.log`.
The final binaries with the non-default recordsize check passed all four boots
in `build-aros/qemu-run.3hf1Ksyo/`; summary/compiler logs are
`build-aros/mount-metadata-final-qemu.log` and
`build-aros/mount-metadata-final-link.log`. Upstream zdb reports no block leaks.

## Live core write-policy callbacks

`oz_mount_policy_open/close` registers actual upstream DSL callbacks for
recordsize, readonly, aclmode and aclinherit. Registration delivers initial
values under the pool configuration lock. The callback context retains the
explicit mount-readonly choice, so a dataset `readonly=off` update cannot turn
a read-only mount writable. Snapshot status also forces read-only policy.
The mount owns its callback context; partial registration unwinds only this
context's callbacks, and close uses upstream unregister serialization before
freeing it. The objset remains owned throughout that lifetime.

Read-only DOS mounts now use this registration instead of a permanently stale
recordsize snapshot. The isolated vnode-write fixture tests live recordsize,
readonly and ACL-policy changes, restores its properties, unregisters, then
proves a later property change cannot mutate the detached mount state. A
second registration proves the explicit read-only mount choice survives a
readonly-off notification. Cold imports verify initial callback delivery
without modifying the pool. The write-rejection test now toggles the actual
dataset readonly property and calls upstream `zfs_write`, requiring EROFS and
an unconsumed buffer, rather than merely assigning the native cached flag.

Initial four-boot evidence: `build-aros/qemu-run.QibkbiVg/`. This is core
write-policy wiring, not complete mount-policy support: atime/relatime, xattr,
execution policy and other callbacks still require native integration, along
with journal replay and writable DOS namespace operations.

`build-aros/qemu-run.VnOErO9Q/` is retained as an incomplete evidence run:
creation, cold import and survivor0 guests reported PASS, but the verifier
stopped on survivor0 because security.library task-exit logging split the
required `Authenticated DOS restricted file/directory ACL=0` marker between
`Authe` and `nticated`. Disk write counters were zero. This was not accepted
as a full four-boot result; the same rebuilt binaries were run again with the
original strict marker checks unchanged.
The repeat passed all four boots in `build-aros/qemu-run.GwXXU9Gd/`, including
the new policy markers, authenticated ACL checks, zero cold disk writes,
unchanged backing hashes and upstream zdb with no leaks. Logs:
`build-aros/mount-policy-repeat-qemu.log` and
`build-aros/mount-policy-final-link.log`.

## Upstream intent-log replay dependency audit

The full `module/zfs/zfs_replay.c` now compiles against native declarations.
Native `sys/fcntl.h` supplies an explicitly private 64-bit space-operation
descriptor, not a pretend AROS POSIX locking ABI; `sys/atomic.h` uses the
already linked libspl atomics. Native `sys/zpl.h` includes native ZPL/SA
declarations rather than Linux VFS tables. The compile sweep includes replay
and passes 7/7 in `build-aros/zpl-compile.JZqmeyXK/`.

`zfs_write_simple` is implemented as the upstream internal kernel-credential
helper over real `zfs_write`: native UIO, error propagation, residual reporting
and EIO on short writes when no residual output is supplied. Invalid spans
are rejected without changing the caller's residual sentinel. It is not a
public authentication interface. QEMU now exercises both residual forms for
actual initial and partial-block writes, fsyncs, and cold byte/size comparison.
The append test still exercises upstream `zfs_write` directly.

The complete common `zfs_byteswap.c` is built with native ZPL declarations so
its two ACL conversion routines are exported, not replaced with duplicates.
Native ACL tests check exact swapped fields and round-trip identity for both
legacy and current external ACE forms. This is not cross-endian log replay.

Replay itself is included only in the full link audit, with no section garbage
collection or success stubs. The final audit in `build-aros/zpl-link.A8MJv6h7/`
still fails on 11 real prerequisites: `zfs_cmpldev`, `zfs_create`, `zfs_link`,
`zfs_make_xattrdir`, `zfs_mkdir`, `zfs_remove`, `zfs_rename`, `zfs_rmdir`,
`zfs_setattr`, `zfs_space`, and `zfs_symlink`. Implementing and verifying these
and crash recovery remains necessary before enabling writable DOS mounts.
Earlier missing-header attempts are retained in `replay-compile.HAgcT7y0`
and `replay-compile.Hbxe6Yw7`; the first successful replay compilation is
`replay-compile.SFbgQI2k`, all under `build-aros/`.

The runtime helper and ACL changes pass all four QEMU boots in
`build-aros/qemu-run.Kid0uSID/`: create, full cold import and either mirror
member alone, with authenticated handler access/denials, unchanged backing
images, zero cold disk writes and upstream zdb without leaks. Compiler and
test summaries: `build-aros/replay-helper-final-link.log` and
`build-aros/replay-helper-final-qemu.log`. No replay capability is claimed by
these passes; the unresolved audit artifact is never deployed.

## Native truncate and hole-punch path

`aros/zpl/space.c` carries the upstream extend/free-range/truncate/freesp
algorithms from `zfs_znode_os.c`, adapted at the Linux page-cache boundary.
Range locks, blocksize growth, DMU transaction reservations/assignment,
long-range frees, SA size/flag/timestamp updates and `zfs_log_truncate` remain
the upstream path. The native shared-page-cache policy has no mapped aliases;
nonzero mapping counts explicitly reject the operation before disk mutation,
rather than pretending to flush a Linux page cache.

`zfs_space` provides native validation, teardown protection, read-only/type
checks and actual upstream ACE authorization. Logged operations require a
real log before mutation; missing credentials, invalid/overflowing ranges and
unsupported seek origins are rejected. This remains internal ZPL plumbing,
not writable DOS namespace support or completed recovery.

The disposable write fixture punches 6003 bytes at offset 4091, reads back the
entire file to verify the zero hole and untouched surrounding bytes, shrinks
to 14001 bytes, and extends back to 16415 with a zero-filled tail. Every step
uses the native ZPL path and fsync; full cold imports and each sole mirror
member compare the resulting bytes and persisted size. Cold mutation attempts
return EROFS. The tests also cover missing log/identity, invalid ranges,
directory targets and an explicitly ACL-denied non-root truncation attempt.

Initial four-boot evidence: `build-aros/qemu-run.fBF7yyVQ/`, including zero
cold writes, unchanged backing images and upstream zdb without leaks.
The complete replay link audit `build-aros/zpl-link.rnd8FKOo/` is still
incomplete, but its unresolved list is down to 10 entries: zfs_space is now
implemented and linked, not stubbed. Actual crash-replay tests remain pending.
The final expanded rejection tests also pass all four boots in
`build-aros/qemu-run.cFoIsMFs/`, with no cold disk writes, matching backing
hashes and no zdb leaks. Logs: `build-aros/space-final-link.log` and
`build-aros/space-final-qemu.log`.

## Upstream directory-entry locking

`aros/zpl/dirlock.c` ports upstream `zfs_match_find`, `zfs_dirent_lock` and
`zfs_dirent_unlock`, retaining per-name condition-variable waiting, shared
reference counts, copied-name lifetime, normalization-aware matching, ZNEW/
ZEXISTS checks and parent name-lock rules. The caller retains parent znode
and mount lifetime until unlock. Native guards reject invalid names and
non-directory parents, initialize lookup output, and reserve root `.zfs`
without claiming that a control-directory namespace is implemented.

Regular frontend lookup now enters this native locking path instead of
looking directly in the directory ZAP. It retains the entry lock through
metadata lookup and releases the child/entry/parent references on all paths.
This is a prerequisite for future namespace mutation, not the implementation
of create, unlink or rename. The existing read-only mount rejection of
normalized/UTF-8/casefolded datasets remains until complete path semantics are
tested; compiling the lock's upstream normalization code is not enough.

QEMU tests verify two shared acquisitions reuse one lock and retain a copied
name after the first caller releases and overwrites its name buffer, expected
existing/missing-name errors, a new-name reservation without an object, and
four native threads each making 100 exclusive acquisitions on the same name.
All child references and directory locks must be released afterward. These
are bounded concurrency checks, not a proof of all future namespace races.
Initial all-four-boot evidence: `build-aros/qemu-run.4bTuih7W/`, including
authenticated DOS access, cold/either-member reads, zero cold writes, matching
disk hashes and zdb without leaks. The initial header compile failure is
retained in `build-aros/dirlock-compile.log`.
The final rebuilt binaries with the explicit per-boot lock-test gate passed
all four boots in `build-aros/qemu-run.iMuoYh8W/`, also with zero cold writes,
unchanged hashes and no zdb leaks. Logs: `build-aros/dirlock-final-link.log`
and `build-aros/dirlock-final-qemu.log`.

## Earlier shared UIO helper history

AROS now includes upstream `uio_impl.h` for the shared alignment/vector helpers
instead of keeping its duplicate libspl versions. Native page-alignment checks
account for partially consumed vectors; synchronous shared-memory prefault
validation rejects malformed spans and unsupported segments. This is not a
claim of Linux page pinning or direct-I/O support.

`build-aros/qemu-run.9wveLkpz/` passes the expanded native buffer tests and all
four mirror/handler boots after a full 178+70 native rebuild. Cold/degraded
boots have zero writes, backing hashes match and zdb finds no leaks.
Summary: `build-aros/uio-shared-qemu.log`. The ZPL sweep remains 4/6, with the
missing shared-helper declarations resolved; vnode/cache/direct-I/O and foreign
identity mapping still prevent the full write path from linking.

## Attribute ABI and first compile pass

The native engine now uses the upstream `xvattr.h` definitions rather than
libzpool's reduced test attributes. `aros/engine/include/sys/vnode.h` defines
the shared native `vattr_t`; both build contexts see the same layout, including
the real optional generation/project fields and ACL flags. AROS uses the
existing native `zfs_file_t` in both contexts. The ZPL kmem include retains
the engine's UMEM allocator instead of selecting libspl's malloc macros.

Validation: hosted and native engine sweeps each compile 178 engine objects
and 70 dependency objects, and the management binary links. The new native
binary passes all four QEMU mirror/handler boots in
`build-aros/qemu-run.Kkgo6M2F/`; summary `attributes-qemu-test.log`.

The first separate ZPL sweep failed 0/6. Earlier logs `zpl-compile.xS2H9zk3`,
`zpl-compile.CcLHLvxf` and `zpl-compile.N1QE9NOm` retain that progression.

## Native mount types and buffered UIO

The native zfsvfs/znode declarations now retain upstream metadata and actual
teardown locking without embedding fake Linux inodes. SID domain storage
reuses the existing libzpool implementation, not a second allocator ABI.
Hold/release and vnode operations are unresolved declarations, not stubs.

`build-aros/zpl-compile.eYt4uoX3/` compiles upstream zfs_log.c, zfs_quota.c,
zfs_sa.c and zfs_znode.c with ZPL enabled (4/6). The remaining failures are
zfs_vnops.c (direct-I/O/page-cache interfaces, resource limits and credential
policy) and zfs_fuid.c (identity mapping/credentials). No experimental ZPL
objects are installed in the service yet; compile success is not runtime
filesystem verification. Native mount setup, znode lifecycle, credential
enforcement and ZIL replay still need implementation and tests.

`aros/engine/uio.c` supplies real synchronous shared-memory scatter/gather
move, non-consuming copy and skip operations. The vector contents remain
unchanged; cursor/remaining count/64-bit offsets advance as upstream expects.
Malformed spans are rejected before mutation. Non-system segments and extended
(including direct-I/O) flags return ENOTSUP rather than pretending support.

After a full native rebuild, `build-aros/qemu-run.TSAmyAKJ/create-serial.log`
reports `Native UIO scatter/gather/copy/skip/64-bit/bounds tests=0`. Four native
boots also pass the mirror/DOS-handler regression, cold/degraded boots issue
zero writes, both original disk hashes match, and Linux zdb reports no leaks.
Summary: `build-aros/uio-qemu-test.log`. These buffer primitives are groundwork
for writes, NOT evidence of DOS Write support. The installed handler remains
read-only.

The hosted engine/dependencies were also fully rebuilt after the UIO layout
change. The restarted isolated AROS instance passes all management/property,
mount/read, busy-unmount, late-packet and remount checks:
`build-aros/uio-hosted-pass.kz92Sqe8/mount-test.log` ends with
`RESULT native mount API failures=0`. Its named-window capture shows the real
manager connected to upstream OpenZFS 2.4.4 and the OZAPI volume on Wanderer.
This run did not reproduce the earlier output-ownership anomaly; it does not
establish that the intermittent anomaly is fixed.

## Upstream FUID integration and multi-drive native verification

The diagnostic link now uses the complete upstream kernel-side zfs_fuid.c,
with an AROS immutable domain/RID-to-local-ID table. User and group mappings
are distinct; unknown foreign identities return an unmapped sentinel, never
an implicitly truncated local identity. Credential construction rejects that
sentinel. This is not a native security.library identity provider, an
authorization boundary, or a completed writable mount implementation.

The separate ZPL compile lane now passes 5/6 units (`zpl-compile.WfHtrJyE`);
zfs_vnops.c remains incomplete. Runtime tests exercise upstream FUID AVL
insertion, lookup, group membership and destruction against an in-memory
mount fixture, not on-disk FUID sync/replay.

Native rebuild and two four-boot QEMU sequences passed:
`build-aros/qemu-run.TzAxA4rG/` and `build-aros/qemu-run.2Sz7eole/`.
Summary logs: `idmap-qemu.log` and `idmap-qemu-replay.log`.
Each creates an upstream OpenZFS mirror on two disposable 256 MiB hard files,
checks actual DOS-handler file operations, cold-boots with both members,
then cold-boots separately with either sole surviving member. Cold/degraded
imports issue zero writes; original image hashes match and zdb reports no
leaks. The slave-position test also attaches a blank nonmember master disk
to satisfy this AROS ATA enumerator; it supplies no pool data.

The handler remains read-only. Pool creation and fixture writes use the
upstream engine, not DOS Write packets. Neither these tests nor the identity
tests establish complete writable ZPL, ACL enforcement, or production readiness.
The DUT and its private-format Work volume were not used.

## Native timestamp metadata

`aros/zpl/timestamps.c` supplies native cached access-time updates and the
upstream transaction timestamp-setup entry point. It uses the existing AROS
clock and znode mutex, not a fabricated Linux inode. Access-time updates honor
noatime, read-only mounts, snapshots and unmounted state; relatime checks both
modification/change times and the 24-hour age threshold. Transaction timestamp
arrays and cached mtime/ctime are updated together, with upstream archive/AV
flags when FUIDs are enabled. Updating mtime/ctime does not silently clear a
dirty atime which has not been persisted.

Callers must hold the mount lifetime/teardown reference and must not hold
z_lock on entry. Property callback synchronization, znode initialization,
inactive/sync persistence and rollback/error behavior of the full transaction
path remain to be wired and verified. This is cached metadata support, not
proof of timestamp durability or a writable DOS handler.

The native engine links the adapter. Native tests in
`build-aros/qemu-run.qon6KOO8/` passed, followed by an expanded replay in
`build-aros/qemu-run.aFrvgHlI/` covering nanosecond age-boundary and backward
clock cases. `timestamps-qemu-replay.log` records all four mirror/handler boots
passing, unchanged backing hashes and zero cold/degraded writes. The ZPL
compile remains 5/6 (`zpl-compile.6lRsEMgI`): the atime-hook error is resolved,
but vnode/cache/direct-I/O and resource-limit interfaces remain unfinished.

## Access-time persistence through upstream SA transactions

`oz_znode_flush_atime` now persists native-SA access times with actual
`dmu_tx_hold_sa`, transaction assignment, `sa_update`, and transaction commit.
Assignment occurs before taking z_lock; the latest cached time is encoded
under that lock, and the dirty flag is cleared only after a successful update.
Assigned transactions are committed even on SA errors. Mount lifetime and SA
handle lifetime must be retained by the caller throughout. Transaction commit
is asynchronous; the caller still needs the normal pool sync/export durability
boundary. The helper is not yet attached to a complete inactive/sync lifecycle.

Read-only/snapshot/unmounted mounts and unlinked objects are rejected without
discarding dirty state. Legacy non-SA znodes return ENOTSUP until their upgrade
transaction path is integrated. The actual pool writeability and object-set
snapshot state are checked as well as the native mount flags.

The test writes hello.txt's atime as seconds=1700000123, nanoseconds=456789123
on the disposable mirror and verifies both words with upstream SA lookup.
It then exports and validates the timestamp in a fresh QEMU boot, and again
with either sole surviving mirror member. `build-aros/qemu-run.vfHPXf8N/`
passed the first persistence run, retaining all handler checks, zero cold
writes, unchanged image hashes and zdb's no-leak check. The initial test build
failed on the unparenthesized upstream SA_ZPL_ATIME macro argument; the failure
is retained in `build-aros/atime-native-link.log` and fixed at the call site.

This proves native-SA timestamp persistence, not DOS Write support, complete
legacy-format compatibility, ZIL replay or a completed writable filesystem.

The hardened replay `build-aros/qemu-run.E8uxq3iO/` also passes all four boots.
During each read-only import it deliberately clears the test mount's cached
read-only flag, marks a newer time dirty, and verifies that the engine-level
pool guard returns EROFS without clearing that flag or writing any sectors.
Summary: `build-aros/atime-guard-qemu.log`.

## Native direct-I/O buffers and shared-layout correction

The AROS UIO now owns a descriptor array for page-aligned synchronous buffers.
These are borrowed shared-address-space addresses, not Linux pages or physical
DMA addresses. The caller must retain the buffers until completion. Invalid
alignment/segments and duplicate acquisition are rejected. Descriptor release
does not free the caller's memory. A gang ABD borrows each page through upstream
linear ABD views, including partial head/tail offsets, without a staging copy.

The complete upstream `_KERNEL` dmu_direct.c is now linked, not a rewritten
read/write algorithm. The native build first passed descriptor/ABD tests in
`qemu-run.VhvISDVy`, then upstream direct-read comparison in `qemu-run.s96yUzsq`.
Initial compile failures (missing PAGESHIFT and a test built outside ZPL context)
remain in `dmu-direct-probe.log` and `dio-upstream-link.log`.

Direct writes exposed an ABI fault, retained in `qemu-run.oHHxW94T` and
`qemu-run.chvFKdWB`. Symbol-relative decoding put the fault at
`dmu_write_direct+0xfd`, loading the dirty-record list. Core dbuf.c used offset
0x4d0; the ZPL-context unit used 0x4c8. `avl_tree_t` loses its userspace ABI pad
when first included under `_KERNEL`, shrinking embedded debug refcounts.
The native ZPL context now includes the shared AVL definition before defining
`_KERNEL`. This preserves one SPL ABI rather than changing core storage layouts.
A cross-context runtime guard compares AVL/refcount/dbuf/dnode/objset/tx/UIO
sizes and the dbuf dirty-list offset; management startup rejects mismatches.

`qemu-run.JxNAaftc` passed after that correction. The final expanded test
`qemu-run.TJUOaiKU`, summarized in `dio-cold-qemu.log`, initializes a 16 KiB
object with zeroes, overwrites it through upstream dmu_write_uio_direct using
the native buffers, exports it, and compares the resulting pattern through
upstream direct reads. Separate healthy and either-survivor cold boots each
verify that pattern through direct reads before other payload reads. Existing
DOS-handler and persisted-atime checks also pass, with zero cold/degraded
writes, unchanged original disk hashes and no leaked blocks.

This verifies the upstream direct-Uio DMU entry points, not a completed DOS
O_DIRECT interface, full writable ZPL, physical DMA pinning, or arbitrary
asynchronous caller-memory lifetimes. The shared UIO layout requires rebuilding
all consumers; never mix old objects with these new adapters.

Both native and hosted consumers were rebuilt (178 engine / 70 dependency
objects). The restarted owned hosted instance passes the real mount/read,
busy-unmount, late-packet and remount API tests:
`build-aros/dio-hosted-pass.GB34iAHP/mount-test.log` ends with failures=0.
`build-aros/dio-hosted-manager.png` is a verified named-window capture of the
connected manager and OZAPI volume. Earlier hosted logs were retained under
`build-aros/pre-dio-hosted.BKdiTMIO/`; the DUT and user sessions were untouched.

## Upstream buffered DMU integration

The complete `_KERNEL` dmu.c is now linked alongside dmu_direct.c. The native
vmsystm header exposes the real shared-memory UIO mover; it does not emulate a
Linux VM. The mount-state query uses upstream object-set user-pointer locking
and distinguishes attached, unmounting and detached native mount state.
Tests install and remove a temporary mount-state fixture under that same lock.
Production mount lifecycle and property synchronization remain unfinished.

The native UIO cursor retains its initial vector origin. `zfs_uio_advance`
now rewinds vector pointer/count/skip as well as offset/residual, as required
when upstream DMU rolls back a failed full-buffer fill. Invalid or overflowing
rewinds leave the cursor unchanged. Tests cover multiple vectors, zero-length
entries, initial skip and full/partial rewind. This verifies the cursor itself;
it does not claim an injected DMU fill-error test. All native callers must use
the native UIO initializer and retain the original vector array throughout I/O.

A full native rebuild passes the original regression in `qemu-run.0VYhGc9M`.
The expanded `qemu-run.JuJ1Pypn` writes the direct-I/O pattern, then overlays
offset 3077 through 9077 using upstream dmu_write_uio and four vectors, including
an empty entry and an initial skip. Upstream buffered read compares all 16 KiB,
including untouched neighbors. Healthy and either-survivor cold boots each
compare the resulting mixed buffered/direct pattern through upstream direct
reads. Handler and atime tests, zero cold writes, backing hashes and zdb leak
checks still pass. Summary: `build-aros/buffered-write-qemu.log`.

The separate ZPL sweep remains 5/6 (`zpl-compile.28eOBIN5`); zfs_vnops still
needs native cache/limits/flag interfaces. This is verified DMU I/O integration,
not a writable DOS handler or complete ZPL/ZIL lifecycle.

After the matching full hosted rebuild (178 engine / 70 dependency objects),
the owned :94 instance was restarted and all management mount/read,
busy-unmount, late-packet and remount checks passed:
`build-aros/buffered-hosted-pass.9ps3mQO3/mount-test.log` reports failures=0.
The connected UI and OZAPI volume are visually verified in
`build-aros/buffered-hosted-manager.png`. The previous runtime logs remain in
`build-aros/pre-buffered-hosted.ALXGKYSw/`. No DUT or user session was touched.

## Native size boundaries and internal request flags

`file-limits.c` enforces the representable signed-64-bit file size/offset range.
The AROS source `compiler/crt/posixc/getrlimit.c` implements only RLIMIT_NOFILE;
there is no native RLIMIT_FSIZE provider to consult here. These helpers do not
claim per-process resource limits or consult the Linux host/service task's
limits. A nonempty write starting at MAXOFFSET_T is rejected; a write starting
below it may reach the common upstream loop, which clips its byte count at the
limit. Validation does not mutate the UIO. Clone endpoints above the limit are
rejected, including UINT64_MAX.

O_DIRECT is a private ZPL request bit (0x10000000), with compile-time checks
against existing AROS open flags. It must never be forwarded as a supported
AROS POSIX open() flag. ATTR_NOACLCHECK retains the upstream internal value;
untrusted frontend requests must never use it to claim permission bypass.
The synchronous shared-memory fault-mode hook validates UIO_SYSSPACE but does
not pretend to change a nonexistent Linux demand-paging mode. Actual transfers
still use the native checked mover.

`build-aros/qemu-run.aeDTRS7I/` passes the new native file-size/flag checks and
all four mirror boots, mixed direct/buffered payload comparisons, persisted
atime, DOS-handler tests, zero cold writes, backing hashes and zdb no-leak test.
Summary: `build-aros/file-limits-qemu.log`. This tests boundary primitives, not
an actual exabyte-sized file, a short ZPL write at the limit, caller-specific
RLIMIT_FSIZE, or public O_DIRECT support.

`build-aros/zpl-compile.2pVzSsik/` remains 5/6: only the missing
zn_has_cached_data and zn_flush_cached_data declarations are compile errors
in zfs_vnops.c now. Their native semantics and further link dependencies still
need implementation; no complete vnode or writable-handler claim follows.

## Full ZPL link audit

The mapping-cache contracts are now external declarations, not implementations.
`build-aros/zpl-compile.kCHS6apY/` compiles all six candidate upstream units.
This does not enable mapping support or writable DOS access.

`bash aros/zpl/link-audit.sh` substitutes the complete kernel-side SA/znode
units and adds vnops/log/quota to the normal engine link, without section
garbage collection. It writes a separate aggregate and diagnostic executable;
it never deploys that candidate. AROS's final undefined-symbol checker can leave
an output file even on failure: its existence must not be treated as success.
The verified service/test artifacts are not replaced by the audit.

`build-aros/zpl-link.jdNzndCt/` retains the real failed final link and its 20
unresolved names: dataset read/write statistics, mapping-cache query/flush/read/
update, znode hold/release/get/async release/VFS update/blocksize, readonly
policy, ACL access and conversion. This is a concrete implementation backlog,
not a reason to replace those contracts with successful stubs. The earlier
`zpl-link.GmZ0ndR9/link.log` preserves the same failure before the audit's
summary filter learned AROS's `U symbol` diagnostic format.

The latest runtime evidence remains `qemu-run.aeDTRS7I`: the tested read-only
handler and upstream engine pass all four mirror boots. No new writable ZPL
runtime result is claimed by this compile/link audit.

## Native effective read-only policy

`mount-state.c` now supplies `zfs_is_readonly()` for upstream callers. Missing
or detached mounts, teardown, explicit mount read-only and snapshot flags are
fail-closed; a real snapshot or a read-only imported pool remains read-only
even if the cached native flags are incorrectly clear. Callers must retain
mount/objset lifetime protection. The eventual mount initializer and dataset
property callbacks must maintain `z_readonly`; those callbacks are not yet
implemented and this helper does not claim dynamic property enforcement.

Native SA atime persistence uses this common guard. Tests cover detached/null,
mount policy, snapshot flag and teardown, plus the actual writable pool and
read-only cold imports. `build-aros/qemu-run.TF7gH63e/` passes all four mirror
boots and handler reads, persisted atime, zero cold writes, backing hashes and
zdb no-leak checks. Summary: `build-aros/readonly-qemu.log`. The snapshot flag
test is not a substitute for a real snapshot mount test, which remains pending.

## Upstream block-size growth

`blocksize.c` carries the upstream zfs-2.4.4 `zfs_grow_blocksize()` algorithm
from the OS znode adapter without a Linux inode dependency. It preserves the
single-block-only growth rule, ENOTSUP handling and querying the actual DMU
geometry after growth. The caller must hold the writer range lock and an
assigned transaction with appropriate holds; this does not extend file length.

The native SA persistence test now exercises the actual hello.txt object:
4096-byte initial block, no shrink to 2048, the cached multi-block guard, then
real growth to 8192. It uses a real range lock and held transaction. Each cold
import verifies 8192-byte geometry and the existing DOS-handler test compares
the original content. The multi-block refusal check deliberately changes only
the cached size temporarily; it is not a real multi-block-file test.

`build-aros/qemu-run.I1PGwFC5/` passes all four boots, both single-member
imports, file content, SA atime, backing hashes, zero cold writes and zdb
no-leak checks. Summary: `build-aros/blocksize-qemu.log`. Link audit
`build-aros/zpl-link.aQRhzzgx/` retains the remaining 18 dependencies.

Dataset statistics need more than adding upstream dataset_kstats.c: the
current libzpool kstat_create() deliberately returns NULL, so real dataset
statistics creation would fail ENOMEM. A native registry/provider and its
lifetime/API tests are still required; no statistics support is claimed.

## Upstream ACL storage primitives

`acl-storage.c` extracts `zfs_external_acl` and `zfs_acl_data_locator` from
upstream zfs-2.4.4, preserving their SA/legacy lookup and list-chunk semantics.
It does not implement access checks or change any authorization decision.
The untouched Linux ACL-unit compile probe is retained in
`build-aros/zpl-acl-probe.log`: native trace declarations and Linux zpl.h/aio
dependencies prevent simply compiling that whole OS adapter on AROS.

The native test covers the SA no-external-object case, multiple chunk traversal,
restart, and upstream's whole-chunk callback contract even with smaller buflen.
`qemu-run.tOaRhn4c/` passed the primitive checks and all four mirror boots.
The expanded test rewrites the existing valid fixture ACL through two fragments
(split inside the ACE) using real sa_bulk_update and an assigned transaction;
it verifies the original ACL count and exact bytes immediately and on each
cold import. It does not invent a different ACL wire format or test permission
evaluation. Legacy external-ACL lookup and upgrade races still need runtime
coverage. Full ACL evaluation, native authorization and mutation remain open.

The diagnostic link `build-aros/zpl-link.O1BftZKz/` retains 16 remaining
dependencies; none are replaced with successful stubs.

The expanded real-SA test passes in `build-aros/qemu-run.cljPAE29/`:
four mirror boots, exact ACL/file bytes, block geometry, atime, zero cold
writes, backing hashes and zdb no-leak checks. Summary:
`build-aros/acl-sa-qemu.log`.

## Native mount-owned znode cache

`znode-cache.c` supplies native zfs_zget and hold/release, using real private SA
handles and upstream metadata attributes rather than Linux inodes. A mount
cache mutex serializes lookup/publication and reference counts. Concurrent
lookups share one znode per object. Idle znodes retain their SA handles in the
mount cache until explicit finalization; this is not memory-pressure eviction.
The upstream append/growth range-lock callback is retained. Metadata includes
full-width owners/generation, four timestamps, project id, flags, links, size,
mode, block geometry and xattr parent. Legacy bonus formats return ENOTSUP.

Initialization must precede lookup. Callers retain mount lifetime protection;
finalization requires exclusive mount access. Finalization refuses the entire
cache with EBUSY if any node has references, dirty atime, is unlinked, or has
ACL/xattr/directory/mapping/synchronous-open resources. It does not discard
dirty metadata or pretend to finish unlink/ACL cleanup. Those lifecycles and
integration into production mount/unmount remain unfinished. No asynchronous
release or writable-handler completion is claimed.

`build-aros/qemu-run.Hymohuis/` passes all four native mirror boots. The SA test
loads a real file, checks metadata and shared identity, exercises four native
threads performing 100 lookup/hold/release cycles each, verifies live/dirty
teardown refusal, idle reacquisition, missing-object output clearing, and
unmounted rejection, then disposes the idle cache. Existing ACL, block-size,
atime, DOS reads, either-member cold imports, backing hashes, zero cold writes
and zdb no-leak tests still pass. Summary: `build-aros/znode-qemu.log`.
The concurrency test covers cache hits, not a race between first-time loaders.

## Competing lookup and ZIL-safe reference release

Review of upstream zfs_get_data revealed a lock-order hazard in the first native
cache version: holding z_znodes_lock over a DMU load could block a release
needed while ZIL completion holds up TXG syncing. Loading now happens outside
that lock. Publication rechecks the cache under lock, keeps one winner and
disposes an unused private candidate outside the lock. Callers must still
retain mount lifetime protection throughout lookup.

`zfs_zrele_async` now drops a native reference without triggering eviction,
flush or unlink I/O. Idle nodes belong to the mount, so no background task is
needed merely to decrement their consumer count. Any future eviction policy
must preserve this no-I/O release contract. This is not a completed ZIL replay
or unlinked-object cleanup implementation.

Four first-lookup threads are released through a condition-variable gate into
an empty cache; the test checks identical returned pointers and four live
references, followed by the existing 4x100 hit/reference cycles. This tests
competing requests but does not instrument the scheduler to force every thread
through the duplicate-candidate path. `build-aros/qemu-run.lLUnb1fe/` passes
all four mirror boots and existing regressions. The full link audit remains
in `build-aros/zpl-link.0MLCAWOs/`; unresolved dependencies remain explicit.

`build-aros/qemu-run.oDVCVRvb/` repeats all four boots with explicit
last-consumer `zfs_zrele_async`, dirty-cache refusal and identical-pointer
reacquisition. All file/ACL/geometry/atime/handler, zero-cold-write, hash and
zdb checks pass; summary `build-aros/znode-async-qemu.log`.

## Legacy znode metadata loading

The native cache now accepts upstream DMU_OT_ZNODE bonus objects as well as
native SA, rejects undersized legacy bonuses before SA access, and sets
z_is_sa according to the actual bonus type. The same upstream SA bulk lookup
decodes the legacy layout; no replacement serialization is introduced.

`legacy-znode-test.c` creates temporary DMU objects using upstream znode_phys_t
and ACL structures: a legacy regular file with full-width owner ID, timestamps
and a real external-ACL object, plus a truncated legacy object. It checks
metadata, external-ACL lookup and wrong-type/short-bonus rejection, then
destroys the cache before freeing all three test objects. This is a storage
fixture, not a DOS file create/unlink test. The legacy fixtures do not persist
across boot, and legacy-to-SA upgrade and legacy atime persistence remain
unfinished.

`build-aros/qemu-run.GTr7MRtP/` passes that native legacy test and all four
mirror boots, existing handler/ACL/blocksize/atime checks, backing hashes,
zero cold writes and zdb no-leak checks. Summary: `build-aros/legacy-qemu.log`.
The initial compile failure from a missing dmu_write flags argument remains
in `legacy-native-link.log`; the corrected build is in
`legacy-native-link-retry.log`.

## Native cached-atime sync

`oz_znode_cache_sync` visits the nodes present at the start of the pass, holds
a consumer reference while flushing each dirty atime, and never holds the
cache mutex over SA or TXG work. Mount lifetime protection excludes teardown.
The first error is retained while other nodes can still flush. With wait=true,
it waits for TXG completion on a writable pool, including successful writes
that preceded another node's error. Read-only imports do not wait on an absent
syncing writer. New nodes or accesses after their visit belong to a later pass.

Native tests now verify real cache-to-SA atime persistence, reference balance,
clean repeated sync, and read-only dirty-state rejection/retention with no disk
writes. The injected dirty flag in the read-only test is explicitly removed by
the test afterward; the implementation never clears failed writes. Cache
finalization still refuses unlinked/ACL/xattr/mapping resources and pending
legacy atime. This is not a complete filesystem sync/fsync/ZIL implementation.

The first four-boot regression passed in `build-aros/qemu-run.94CKPkSu/`
(`cache-sync-qemu.log`); the bounded-pass refinement is tested separately.
The bounded implementation also passes all four boots in
`build-aros/qemu-run.gtLRsu89/`, including unchanged disk hashes, zero cold
writes and zdb no-leak checks. Summary: `cache-sync-bounded-qemu.log`.

## Legacy atime writeback without format conversion

The existing legacy atime attribute can be updated through upstream SA without
converting its bonus format. `oz_znode_flush_atime` therefore no longer rejects
all non-native-SA znodes: it retains the same readonly/unlinked/transaction
guards and uses dmu_tx_hold_sa plus sa_update for either supported layout.
The previous artificial test that flipped z_is_sa on a native-SA handle is
replaced by a real legacy-object writeback test.

The legacy fixture updates cached atime, calls synchronous cache writeback,
then reads the raw znode_phys_t bonus independently of SA lookup. It checks
both timestamp words, unchanged full-width owner/group and external-ACL
reference, and that the DMU bonus type remains DMU_OT_ZNODE. No implicit format
upgrade is asserted. The disposable fixture is still freed before export;
this is not legacy cold-import or power-loss verification.

`build-aros/qemu-run.7jiKJfYF/` passes this legacy writeback test and all four
native mirror boots, including existing DOS reads, native-SA persistence,
ACL/block geometry, concurrent cache tests, zero cold writes, backing hashes
and zdb no-leak checks. Summary: `build-aros/legacy-atime-qemu.log`.

## Direct upstream ACL core integration

`acl-core.c` compiles the representation/allocation/iteration/conversion,
mode-computation and ACL-reading portion of the actual upstream Linux ACL
source. A narrowly scoped OZ_AROS_ACL_CORE switch omits Linux-only headers and
the later OS permission adapters, and uses the native cached mode during
conversion. Normal Linux compilation is unchanged. No access-check function
is replaced by a successful placeholder.

The earlier copied acl-storage.c was removed: external-ACL lookup and the SA
chunk locator now come directly from upstream, with their existing native
tests retained. The historical implementation remains in git.

The conversion test builds a two-chunk v0 ACL and calls upstream zfs_acl_xform,
checking the FUID representation and preservation of owner/group/everyone,
allow/deny, masks and compact ACE sizes. This does not test named foreign IDs
or persistence of an upgraded ACL. `qemu-run.9XlXiuaT/` passes this test and all
four mirror boots (`acl-core-qemu.log`).

Real native-SA and legacy external ACLs are read using upstream
zfs_acl_node_read with proper ACL locking. Tests verify repeated reads share
the cache and a modification request receives a separate copy without replacing
that cache. `qemu-run.J54T7Ggd/` passes four boots (`acl-read-qemu.log`). The
native znode disposer now frees its owned read cache through zfs_acl_free;
unlinked/xattr/mapping cleanup and authorization remain unfinished. The test
leaves read caches attached so cache finalization exercises that ownership.

The final-link audit `build-aros/zpl-link.AskQ8kqn/` still fails explicitly for
the remaining permission, VM-cache, statistics and native metadata interfaces.
Compiling the common ACL core does not complete the writable handler.

The attached-cache lifetime refinement passes all four native boots in
`build-aros/qemu-run.i78IJTZF/`, including existing handler, ACL, geometry,
atime, legacy and concurrency checks, zero cold writes, backing hashes and
zdb no-leak verification. Summary: `build-aros/acl-lifetime-qemu.log`.

## Ordered ACE evaluation using upstream code

The native core now includes upstream zfs_zaccess_aces_check unchanged in its
ordered allow/deny loop, with native cached mode and existing FUID owner/group
mapping replacing Linux inode/idmapped-mount lookups. A non-null foreign mount
namespace is rejected. `oz_acl_evaluate` validates the request/credential,
returns the remaining denied/ungranted mask, and normalizes upstream's internal
ungranted (-1) result to EACCES. This is only the ACE decision: it is explicitly
not zfs_zaccess, dataset readonly/immutable policy, privilege override, replay
authorization, or authenticated DOS-client identity.

Native tests cover owner allow before a later group deny, explicit group deny,
everyone reads, missing write permission, early owner deny that a later allow
cannot undo, zero masks, unknown bits, NULL credentials, and root receiving no
implicit bypass in this raw evaluator. `qemu-run.t8sd3Heb/` passes those cases
and all four mirror boots (`acl-eval-qemu.log`). Named foreign ACE identities
and directory inheritance evaluation still need dedicated cases.

The storage-backed extension evaluates actual cached ACLs read through SA or
the legacy external object, using a non-root credential: the native fixture
permits reads/writes, while the legacy fixture permits reads and denies writes.
This does not perform a filesystem write or enable public writable access.

`build-aros/qemu-run.5hJ6gTMB/` passes the storage-backed evaluator checks and
all four boots, alongside handler reads, ACL/blocksize/atime/legacy/cache
regressions, zero cold writes, backing hashes and zdb no-leak checks.
Summary: `build-aros/acl-eval-storage-qemu.log`.

## Dataset and file restrictions before ACE decisions

The native core now includes upstream zfs_zaccess_dataset_check and
zfs_zaccess_common. Native mode replaces Linux inode mode; the Linux generic
permission optimization for trivial ACLs is bypassed in favor of the same full
ordered ACE evaluator, not an unconditional allow. Normal Linux builds retain
their original path.

`oz_acl_check` combines dataset/file restrictions and ACE evaluation, rejecting
missing credentials, unsupported masks, unmounted state and replay mode.
It exposes no skip-ACL argument. It does not supply discretionary privilege
overrides, append fallback, xattr-parent translation, or authenticated caller
mapping and is still not the complete zfs_zaccess contract. In particular the
root cases test this component without a later privilege override, not final
superuser behavior for every attribute.

Storage-backed native tests check that read-only imports reject writes even
when the native ACL allows them; immutable writes, nounlink deletion,
quarantine reads and regular-file DOS read-only writes return the upstream
policy errors before ACL evaluation. The checks run with both non-root and
explicit root credentials. The existing stored-ACL allow/deny and mirror
regressions remain in the test, and no writable DOS operation is enabled.

`build-aros/qemu-run.wN4riuvN/` passes the policy cases and four mirror boots,
including existing handler, ACL, geometry, native/legacy atime and concurrency
checks, zero cold writes, backing hashes and zdb no-leak verification.
Summary: `build-aros/acl-policy-qemu.log`.

## Append fallback and directory inheritance decisions

`oz_acl_check_append` retains the same validated identity and dataset checks,
then uses upstream zfs_zaccess_append only for an append request after an
eligible discretionary failure. Upstream requires WRITE_DATA to be the sole
remaining right before retrying APPEND_DATA. Other missing permissions and
non-overridable dataset errors do not become append grants. The original
oz_acl_check remains the non-append entry point. This component does not itself
perform an append or enforce EOF placement; native write/open integration must
carry the trusted operation mode and range-lock semantics.

Native storage-fixture tests temporarily alter only the private cached ACL to
grant append but not write: ordinary write is denied, append can succeed on a
writable pool, a compound request missing read also is denied, NULL identity
is denied, and read-only imports still return EROFS. The cached ACL is restored
before other checks; stored ACLs are unchanged. `qemu-run.HypzCnwt/` passes
these checks and four mirror boots (`acl-append-qemu.log`).

The expanded ordered-evaluator tests also cover inherit-only directory ACEs:
entries intended solely for descendants do not grant access to the directory;
removing inherit-only from an everyone-read ACE restores that grant. This is
evaluation coverage, not generation/persistence of inherited child ACLs.

`build-aros/qemu-run.BpYHJV4u/` passes the expanded tests and the full
four-boot native mirror sequence (`build-aros/acl-inherit-qemu.log`). Two
256 MiB hardfiles are created as an upstream OpenZFS mirror inside AROS;
actual DOS Open/Read/Seek/Close tests pass on creation, fresh two-member
import, and each sole surviving member. Cold imports make zero guest writes,
the backing-file hashes remain unchanged, and Linux zdb reports no leaks.
This proves read-only handler access to the multi-drive pool, not writable
DOS access or completion of the full upstream ZPL port.

## Live handler read path uses native znodes

The read-only frontend now owns a native znode cache for each opened dataset
through `read-mount.c`. Actual handler file reads acquire a cached znode and
upstream reader range lock, then use upstream dmu_read_uio with the native UIO
adapter. They no longer bypass these components with a direct dmu_read call.
The bridge rejects non-regular objects, overflowing signed offsets, missing
buffers and exact reads beyond EOF. The DOS frontend still clamps reads to EOF.
Read-only mounts do not dirty atime. Close finalizes the cache before disowning
the objset and propagates a busy-cache error rather than freeing live resources.

This is not full writable zfsvfs initialization: no property callbacks, ACL
authentication, writable namespace or ZIL replay is implied. Existing SA/ZAP
metadata traversal remains in the bootstrap frontend. Its callers serialize
mount teardown against operations; no znode reference escapes the bridge.

`build-aros/qemu-run.93SrlgFp/` passes the initial integration. The expanded
`build-aros/qemu-run.y6lmWGDQ/` additionally tests EOF/overflow/null-buffer/
directory errors and zero-length EOF reads before actual DOS packet reads.
Both complete four-boot mirror sequences pass, including either sole survivor,
zero cold writes, unchanged backing files, and Linux zdb no leaks.
Summary logs: `read-mount-qemu.log` and `read-mount-boundary-qemu.log`.

Hosted service/manager rebuild also passes. After restarting only the owned
display :94 instance, `build-aros/read-mount-hosted-pass.2dmU2KpM/` records
`RESULT native mount API failures=0`, including busy-unmount, late packets and
remount. `read-mount-hosted-manager.png` captures the named AROS window with
the manager connected and OZAPI mounted. The first staging command used the
wrong cwd and therefore did not copy the binaries; that short old-binary boot
is not counted as verification. It was stopped, staged service hashes were
matched, and the replacement boot supplied the recorded passing evidence.
Prior logs remain in `pre-read-mount-hosted.TyOhp3ck/`.

## Upstream access entry points and native override policy

The same upstream ACL source now supplies zfs_zaccess and zfs_zaccess_rwx,
including implicit owner rights, append fallback and xattr-parent translation.
Native cached mode and full-width FUID ownership replace Linux inode fields.
The Linux policy's unconditional access2 success cannot be reused because
AROS has no Linux VFS permission gate. Native policies honor an ACL grant;
missing discretionary rights require explicit effective POSIX root, with
regular-file execute override requiring at least one mode execute bit.
Owner/root checks implement the owner-policy operations and only root can
override removal denial. Dataset restrictions still precede those overrides.
This does not authenticate native request identities or implement capabilities.
Foreign mount namespaces, skip-ACL requests, replay mode and unsupported flags
explicitly return ENOTSUP pending their integration rather than bypassing checks.
The xattr branch is compiled, but dedicated xattr lifecycle tests remain pending.

Storage-backed tests exercise actual upstream entry points for ordinary and
root access, rwx conversion, append-only rights, implicit owner read attributes,
execute override limits, read-only/immutable/nounlink/quarantine restrictions,
DOS read-only discretionary behavior, NULL credentials and rejected bypasses.
The first run `qemu-run.ICbByeO3` failed: a new test incorrectly truncated the
legacy fixture's foreign FUID into a local owner when expecting implicit rights.
That expectation was corrected, not the mapping weakened. `qemu-run.F5jb4sPU`
passes; `qemu-run.4T9sWyUk` additionally proves the low-bit local identity is
denied those implicit owner rights. Both complete all four mirror boots and
the read-only handler/integrity regressions. Logs: `access-policy-qemu.log`,
`access-policy-retest-qemu.log`, and `access-foreign-qemu.log`.

Full ZPL audit `build-aros/zpl-link.JzAdcVBI/unresolved.txt` now has nine
unresolved symbols (previously eleven): dataset statistics read/write updates,
mappedread, update_pages, zfs_getacl, zfs_setacl, zfs_znode_update_vfs,
zn_flush_cached_data and zn_has_cached_data. No unresolved section was discarded
and no failed audit executable was deployed. This is not writable-handler
completion. Existing hosted running service predates these access changes.

## Upstream ACL retrieval

`zfs_getacl` is now compiled from the upstream ACL source, using the native
access check and native (NULL) mount namespace rather than Linux idmapping.
The native boundary validates null node/output pointers before dereferencing.
The returned ACE buffer is allocated by the upstream routine and released by
the caller with its reported allocation size; count-only requests do not
allocate a buffer. No synthetic ACL is returned.

`build-aros/qemu-run.eGPbK4bI/` passes retrieval against both native SA ACLs
and the temporary legacy external-ACL fixture, checking returned count, byte
size, type, flags and access mask, count-only output, ACL-wide flag conversion,
ordinary-user permission denial, NULL credentials, unsupported skip-ACL and
empty request masks. Flag tests temporarily change cached znode flags only.
The complete mirror/healthy cold boot/either-survivor handler regression,
zero cold writes, backing hashes and zdb no-leak checks also pass.
Summary: `build-aros/getacl-qemu.log`. Object-specific ACE/all-types retrieval
and named foreign ACE export still need dedicated coverage.

The full ZPL audit in `build-aros/zpl-link.GmXc53qY/` now has eight unresolved
symbols: dataset statistics read/write updates, mappedread, update_pages,
zfs_setacl, zfs_znode_update_vfs, zn_flush_cached_data, zn_has_cached_data.
The failed audit binary remains diagnostic only. ACL mutation, journal
integration and writable DOS operations are still unfinished; no hosted
runtime deployment is claimed for this change.

## Upstream ACL input conversion

The native core now includes upstream `zfs_vsec_2_aclp`, a dependency of the
set-ACL transaction path. The AROS boundary requires explicit, exact
`vsa_aclentsz`, an aligned input buffer, bounded positive ACE count and a
credential. It checks the lengths of all standard/object entries before
upstream conversion sees the buffer. The upstream routine still performs
ACE semantic validation, representation selection, identity handling and
ACL-wide hint conversion; there is no alternate on-disk ACL format.

`build-aros/qemu-run.vfsOnKoQ/` passes conversion tests for both legacy and
FUID ACL versions: owner allow, group deny and everyone allow retain order,
mask, flags and types; converted sizes and ACL-wide hints match the upstream
representations. Tests reject missing credentials, incomplete input length
and inherit-only without an inheritance target. Returned ACLs are freed.
The full four-boot mirror and DOS handler suite also passes with zero cold
writes, unchanged backing hashes and zdb no leaks. Summary:
`build-aros/acl-import-qemu.log`.

This does not yet persist a converted ACL or journal its mutation. Named
identity/object-ACE conversion needs broader coverage, and future native
set-ACL callers must supply the exact input byte length rather than relying
on an unspecified zero length. Hosted runtime remains unchanged this turn.

## Upstream ACL storage transaction

The native core now compiles upstream `zfs_aclset_common` and its trivial-ACL
classifier. Native cached owner/group/mode replace Linux inode fields; the
upstream routine still computes mode, serializes ACL/flags/ctime through SA,
handles ACL cache ownership and contains the legacy storage branches.
The timestamp adapter now detects when its caller already holds z_lock,
preserving that lock rather than recursively acquiring a non-recursive mutex.
Its focused test exercises both unlocked and already-locked calls.

`oz_acl_persistence_test` uses converted ACLs and assigned real DMU transactions
under z_acl_lock/z_lock on exclusively owned disposable fixtures. It exercises
a read-only ACL with non-root read allowed/write denied, then leaves hello.txt
with its original all-permissions ACE plus ACL_PROTECTED. On nested.txt it
leaves read-data/read-ACL/read-attributes only, ACL_PROTECTED, mode 0444.
Both stored ACLs are exported and checked after cold imports, with fresh znodes
and ACL caches; the normal DOS packet tests still read both files successfully.

`build-aros/qemu-run.U0mSb6NY/` passes the initial protected-ACL test.
`build-aros/qemu-run.PSAdNklQ/` additionally verifies the persistent restrictive
nested-file ACL/mode across the healthy cold import and either sole survivor.
Both four-boot suites pass with zero cold writes, unchanged backing hashes and
zdb no leaks. Logs: `acl-persist-qemu.log`, `acl-persist-rights-qemu.log`.
The full audit `build-aros/zpl-link.hk2DBYUJ/unresolved.txt` retains the same
eight outstanding symbols; no link-only placeholder was added.

These are native-SA transaction/durability tests across orderly export, not
the complete zfs_setacl entry point, legacy ACL mutation, synchronous ZIL
durability or crash replay. Public DOS writes remain disabled. Hosted service
deployment is unchanged this turn.

## Native VFS metadata publication

`zfs_znode_update_vfs` now updates real native allocated-block metadata using
upstream dmu_object_size_from_db. Native consumers already share z_mode/z_size;
there is no synthetic Linux inode or redundant size/mode copy. A new z_blocks
cache field records 512-byte allocation units including dnode slots, matching
upstream VFS accounting. Synthetic control nodes are skipped. The helper
preserves existing z_lock ownership and also supports unlocked callers.

Actual DOS file information now routes through `oz_read_mount_stat`: it holds
a native cached znode, refreshes allocation, snapshots mode/size/mtime/blocks
under the node lock, and reads parent from SA. This replaces the previous
independent bootstrap SA/stat implementation. Namespace lookup still uses ZAP.

The full native engine/dependency rebuild is in `native-metadata-build.log`.
First run `qemu-run.uiqVDlpF` failed a new test's comparison because raw
dmu_object_info physical blocks exclude dnode slots. Inspection of upstream
dmu_object_size_from_db established the expected accounting, so the test now
compares against raw blocks plus dnode bytes/512. No implementation behavior
was weakened to fit that first expectation. `qemu-run.c0hqKSFZ` then passes
all four boots, concurrent cache checks, refresh from a sentinel block count,
lock preservation, unchanged mode/size, control-node skip and actual DOS
enumeration/reads, as well as ACL persistence and integrity regressions.
Summary: `native-metadata-retest-qemu.log`.

Full ZPL audit `build-aros/zpl-link.iJfiBEbW/unresolved.txt` is down to seven
symbols: dataset read/write statistics, mappedread, update_pages, zfs_setacl,
zn_flush_cached_data and zn_has_cached_data. It is still an incomplete link,
not proof of writable DOS support.

The hosted 178-object engine and 70 dependencies were fully rebuilt, followed
by service/manager linking. Only the owned display :94 instance was restarted;
staged service hashes matched. `build-aros/metadata-hosted-pass.mrQFj1xD/`
records `RESULT native mount API failures=0` with busy-unmount, late packets and
remount checks. `metadata-hosted-manager.png` captures the named AROS window
with the manager connected and OZAPI mounted. Previous logs are preserved in
`pre-metadata-hosted.iywmkrnx/`. This deploys the accumulated access/ACL helper
changes too, but the hosted test exercises the read-only management path,
not all native QEMU-only ACL cases.

## Full set-ACL entry point and explicit journal commit

Native `zfs_setacl` now comes from the upstream ACL source, retaining its
authorization, transaction holds/retry, common ACL update, FUID handling and
zfs_log_acl call. Native mode/namespace adaptations replace Linux inode access;
missing credentials/arguments or an unestablished journal lifetime are rejected.
The normal engine link now includes complete upstream kernel-context zfs_sa,
zfs_znode and zfs_log units, not just a diagnostic audit or discarded sections.

The persistence test now calls zfs_setacl followed by zil_commit rather than
directly invoking the common update helper. It opens/closes the real object-set
ZIL; its ACL-only inline records require no file-write-data callback. This is
not a mount suitable for arbitrary write records until that callback is wired.
Tests retain normal/readonly cold ACL and mode verification, and additionally
reject NULL credentials, skip-ACL requests, immutable mutation, invalid input
length, and changes on read-only imports.

The first runs `qemu-run.HgIRcFIc` and `qemu-run.0VpmUgRX` crashed during
explicit zil_commit, after zfs_setacl returned zero. Trace relocation mapped
the null-call return to vdev_default_psize+0x68: libzpool's disk vdev table
omitted vdev_op_asize_to_psize, unlike the adjacent file table. Commit 954f589
supplies the same upstream default callback. `qemu-run.f5I8qLMi` passes after
that fix; expanded rejection coverage in `qemu-run.5apTeTeE` also passes all
four boots, actual DOS access, zero cold writes, backing hashes and zdb no leaks.
Logs: `setacl-zil-qemu.log`, `setacl-trace-qemu.log`,
`setacl-vdev-fix-qemu.log`, `setacl-final-qemu.log`. Small test-only serial
breadcrumbs retain call addresses and setacl/commit results for attribution.

Full ZPL audit `build-aros/zpl-link.AZDEDSD6/unresolved.txt` is down to six:
dataset read/write statistics, mappedread, update_pages, zn_flush_cached_data,
zn_has_cached_data. It still does not link the entire writable stack. Normal
export/cold-import and successful explicit commit are not a crash-replay test;
native replay dispatch and public writable handler integration remain pending.
The owned hosted service has not been redeployed with this turn's changes.

## Native dataset statistics and multi-hardfile acceptance (2026-09-20)

`dataset-stats.c` supplies a mount-owned named/virtual kstat provider around
the unchanged upstream dataset_kstats.c implementation. Queries snapshot its
real wmsum counters under a native mutex. This does not enable unrelated
global/raw kstats, authenticate DOS clients, or add writable handler support.
Mount lifetime must exclude statistics destruction while readers are active.
Snapshots retain upstream's explicit absence of dataset statistics.

The read bridge accounts only successful nonempty reads. The DOS acceptance
test checks exact deltas (8 reads, 30 + 5 * file-size bytes), unchanged counters
after rejected/empty reads, zero write counters, and errors for unknown,
non-numeric and invalid queries without modifying the caller's result.

`build-aros/qemu-run.3KRJJIZu/` passed all four fresh native AROS boots with
two disposable 256 MiB hardfiles: create a real upstream mirror, cold import
and DOS file access, then independent degraded reads from each sole member.
The second survivor uses a blank nonmember master for AROS ATA enumeration.
Cold guests performed zero disk writes; backing hashes and host zdb checks
passed. Logs: `dataset-counters-link.log`, `dataset-counters-qemu.log`.
This is real Open/Read/Seek/Close plus directory/lock access through the
read-only AROS handler, not just a Linux pool test or an engine-only read.

The complete writable-ZPL link audit remains incomplete, now with four
unresolved page-cache interfaces: mappedread, update_pages,
zn_flush_cached_data, zn_has_cached_data. Evidence is retained under
`build-aros/zpl-link.2o5jtmiU/`. No no-op page-cache implementations were added.
The management UI statistics display and hosted redeployment remain pending.

### Management statistics follow-up

Mounted dataset-list rows now append `mount-reads` and `mount-read-bytes`
from the actual upstream counters; unavailable statistics report an errno,
not invented zeros. These are mount-lifetime logical read counters, not
physical disk I/O or lifetime pool statistics. Existing UI row rendering
displays the values without parsing them or changing the IPC ABI.

The owned hosted :94 service and manager were rebuilt and restarted with
these changes. `build-aros/stats-hosted-pass.I09CqK7c/mount-test.log` proves
zero before reading, one read/60 bytes after ordinary DOS Read, and zero
after detach/remount, along with the existing busy/late-packet tests:
`RESULT native mount API failures=0`. The executable and installed copy
had matching SHA256 abfec0cd10f02bd41fc95dacaa35dd98c05ffbeedbd52a1bd8fd05796fa9f9ae.
`build-aros/stats-hosted-manager.png` captures the named AROS window and
connected manager; it is a startup/capability image, not a statistics-row
screenshot. The prior hosted evidence was preserved in
`build-aros/pre-stats-hosted.Mk3aDhSz/`. Full writable integration remains
unfinished; this does not eliminate the four page-cache link interfaces.

## Upstream vnode reads and explicit native mapping policy (2026-09-20)

The AROS source inspected at `aros-adt-src/compiler/linuxso/lso_mem.c`
describes private file mappings as pread-populated copies, and explicitly
rejects file MAP_SHARED with ENOTSUP (lines 515-519 in that checkout).
The DOS handler itself has no shared pager or separate dirty mapped pages.
`mapping-policy.c` therefore enforces z_mapcnt == 0 before reporting no
mapping cache. mappedread rejects with ENOTSUP; unexpected page-update or
flush calls panic rather than pretending to succeed. This is an explicit
unsupported-feature policy, NOT a completed shared-file-mapping port.
Any future pager must replace it with real dirty-page coherence/lifetime.

2026-10-02 scope clarification from Nick Andrews, relayed by terminills:
mmap is intentionally absent on AROS. Implementing a shared file pager is not
a completion gate for this port. The preceding future-pager note describes a
conditional architectural requirement, not planned work or a promise to add it.

AROS pointer-bearing message passing already supplies the appropriate native
alternative for cooperating applications. ACTION_READ and ACTION_WRITE use
dp_Arg2 as the caller buffer and dp_Arg3 as its length; the handler performs
oz_zpl_read/oz_zpl_write_as before ReplyPkt. The caller must retain the message
and buffer until reply and must not concurrently modify an in-flight write
buffer or access an in-flight read destination. A reply ends this borrowed
buffer access; it does not leave an ARC pointer or persistent file mapping in
the caller. Explicit writes, not arbitrary later pointer stores, change files.

The inspected write adapter calls zfs_write and zfs_fsync before successful
completion, preserving partial-write/error reporting. This is the established
DOS I/O path, not a new mapping API. Recent native USB tests TavdV4Pn and fresh
guest 8H1IrfA6 demonstrate explicit write/flush and exact persisted readback
without mmap. They do not prove physical power-loss durability, protection
against malicious pointers, or recovery from a caller that frees an outstanding
buffer. Message passing by itself is neither a memory-protection boundary nor
automatic synchronization of unrelated accesses. File MAP_SHARED continues to
fail explicitly; no additional shared-buffer API or raw ARC exposure is needed
for these ordinary filesystem operations.

Normal builds now include full upstream common zfs_vnops.c and zfs_quota.c
alongside zfs_log.c. The read mount initializes/destroys its teardown lock
and calls upstream zfs_read instead of its own DMU read/accounting path.
Upstream provides quarantine checks, range locking, ARC/DMU I/O and dataset
counters. The exact-length DOS wrapper still checks its bounds first;
the bootstrap mount is read-only, without concurrent namespace mutation.
Authentication and writable namespace integration remain incomplete.

The complete diagnostic link passes in `build-aros/zpl-link.oWX56HUK/`;
this proves symbol resolution only, not all writable behavior. Native QEMU
`qemu-run.RvSsAE15` passes all four boots through the new upstream read path.
Additional znode tests exercise actual read bytes, quarantine denial,
negative offsets, EOF, unmounted rejection and mappedread ENOTSUP with
unchanged UIO state. They run within the existing SA/cache test aggregate.
The expanded test run `build-aros/qemu-run.LqbXNukW/` passed all four boots,
zero cold writes, unchanged backing hashes and host zdb checks. Retained
logs are `upstream-read-guards-link.log` and `upstream-read-guards-qemu.log`.
Hosted service redeployment with this read-path change remains pending.

## Native upstream write/fsync/append persistence (2026-09-20)

`write-persistence-test.c` owns a separate `qemu_mirror/writefiles` fixture
dataset on disposable QEMU disks. It calls upstream zfs_write, zfs_fsync and
zfs_read with native mount/znode state, and opens the real ZIL with upstream
zfs_get_data. This is no longer just direct DMU writes: vnode range locking,
block growth, SA size, write statistics and journal operations execute.

The test writes a 16 KiB pattern synchronously, overwrites 6001 bytes at
offset 3077 then fsyncs, and appends 31 bytes using O_APPEND|O_SYNC. It
compares all 16415 resulting bytes and persisted SA size. Real upstream
statistics must report exactly three writes and 22416 bytes. Read-only cold
imports must reject a write with EROFS without consuming its UIO, and must
read the same bytes/size with either sole mirror member. Additional writable
run guards reject readonly, immutable, nonappend writes to append-only
files, and negative offsets with unchanged UIO/size.

Initial run `build-aros/qemu-run.YhileOTd/` passed all four boots, original
handler regression tests, zero cold guest writes, unchanged backing hashes
and host zdb validation. This proves orderly export/cold-import persistence,
NOT power-loss journal replay, full quota enforcement, authenticated DOS
writes, or namespace creation through the public handler. Kernel credentials
are explicit test-fixture identities, not admission of unauthenticated clients.
Expanded guard run `build-aros/qemu-run.uHgxbLZU/` also passed all four boots
and integrity gates. Logs: `vnode-write-guards-link.log` and
`vnode-write-guards-qemu.log`. The harness now requires explicit vnode-write
and cold persisted-byte markers, not merely the older aggregate PASS marker.

## Native directory mutation and hard links (2026-09-20)

`dir-mutation.c` ports upstream directory-entry mutation, link-count and
unlinked-set helpers plus `zfs_link` onto native znodes. Transactions, SA/ZAP
updates, entry locks, ACL admission and ZIL records remain upstream operations.
Native admission adds the checks otherwise supplied by Linux VFS: immutable
and append-only source rejection and a locked maximum link-count check. Null
credentials fail closed; an absent writable ZIL is unsupported. Anonymous
tmpfiles are not exposed. Final unlink reclamation is still unfinished.

`link-test.c` exercises journaled add/remove/relink of a second name while the
original survives, checking persisted SA link counts, parent size and object
identity. It checks null/non-owner credentials, directory links, duplicates,
immutable/append-only files, maximum link count and readonly rejection. The
restricted DOS fixture also resolves and reads the alias as root while denying
reader 77; its exact counters now require three reads and three bytes.
The remove helper is an internal fixture transaction, not a public unlink API.

Final native run `build-aros/qemu-run.IPHCQjvp/` passed four separate boots:
creation on two 256 MiB hard files, readonly cold import with both members,
and a cold import with each sole surviving mirror member. All four passed
handler access and hard-link persistence checks. Cold boots made zero guest
disk writes, backing hashes remained unchanged, and host `zdb -bcc` reported
no leaks. Logs: `build-aros/link-mutation-final-link.log` and
`build-aros/link-mutation-final-qemu.log`. Initial successful run
`build-aros/qemu-run.lp4GIeJb/` precedes the final native guard tests.

Full replay audit `build-aros/zpl-link.Z1uclH77/` now has nine unresolved
interfaces: zfs_cmpldev, zfs_create, zfs_make_xattrdir, zfs_mkdir, zfs_remove,
zfs_rename, zfs_rmdir, zfs_setattr and zfs_symlink. This audit intentionally
fails rather than supplying success stubs. Public DOS writes, namespace
creation/deletion and power-loss replay are not claimed complete. The proven
handler path is authenticated read access to the real multi-device pool;
writable vnode tests run through an internal fixture. No DUT was used.

## Upstream node creation and initial ACLs (2026-09-20)

`mknode.c` ports upstream `zfs_mknode`: real DMU object allocation, SA
templates, ownership, mode, timestamps, project inheritance and ACL storage.
The native adaptation replaces Linux inode allocation with a mount-cache
`zfs_zget` after closing the construction SA handle. The caller must own an
assigned transaction and namespace serialization; the new object is not
published in its parent until its metadata is initialized. The upstream root
and replay branches are retained but not yet verified as runtime capabilities.

The ACL translation unit now includes upstream mode-mask construction, chmod,
inheritance, ACL-ID create/free and quota-admission helpers. Native creation
selects ownership from explicit credentials, admits supplementary-group
selection, inherits setgid directory ownership and rejects null credentials.
Linux idmapped-mount input remains explicitly unsupported. Existing Linux
branches remain separate. Native tests reject a forged root owner/group,
check supplementary group 99 and inherited group 999/setgid, and exercise
mode-derived ACL creation. Full inheritable-ACE policy coverage remains work.

`mknode-test.c` creates an actual file and directory under the independent
write fixture, links them with upstream mutation helpers and records real
TX_CREATE/TX_MKDIR journal entries. Cold boots verify mode, ownership, size,
link counts, parent object, generation, SA storage and ACL access. The DOS
handler resolves both new objects and reports their types to an authenticated
root client; traversal is denied to reader 77 by the parent ACL.

The first link exposed the still-excluded ACL-ID helpers
(`build-aros/mknode-link.log`). Initial runtime `qemu-run.YcocP44B` aborted in
dbuf_dirty while creating the directory: the test transaction lacked the new
ZAP reservation used by upstream mkdir. Adding that reservation produced
four passing boots in `qemu-run.TpGiTCOS`. The expanded credential and handler
tests then passed all four boots in `build-aros/qemu-run.uIqS2lpE/`, with zero
cold writes, unchanged backing hashes and no host zdb leaks. Final logs:
`build-aros/mknode-final-link.log`, `build-aros/mknode-final-qemu.log`.

Replay audit `build-aros/zpl-link.au39Q3nt/` retains eight unresolved operations:
create, make_xattrdir, mkdir, remove, rename, rmdir, setattr and symlink.
This change provides their shared creation prerequisite; it does not expose
public DOS create/write or claim crash-replay completion. Normal native engine
builds include the actual implementation, not only a diagnostic stub.

## Native condition-wait lifetime regression (2026-09-20)

During mkdir validation, `build-aros/qemu-run.FtrtxX6g/survivor0-serial.log`
reported `ObtainSemaphore called on a not initialized semaphore` in the
four-thread directory-lock test, before the cold mkdir test. This run is NOT
a passing four-boot result. Read-only inspection of the ADT pthread sources
showed that broadcast dequeues/signals waiters, destroy clears the condition,
and a waking waiter reacquires the condition's semaphore after reacquiring
the external mutex. Immediate upstream kernel-style CV destruction therefore
freed a condition that native waiters still used. Earlier passing runs had
not excluded this timing-sensitive failure.

`dirlock.c` now wraps each directory lock with a waiter count and retired flag,
both protected by the parent z_lock. Unlock removes the entry and broadcasts,
but the last returning waiter frees a retired entry; an entry without waiters
is still freed immediately. The name-lock/shared-lock semantics are unchanged.
No user AROS checkout or pthread implementation was modified. Combined native
validation `build-aros/qemu-run.aACkY8Rc/` passed four boots, concurrent entry
locking, handler tests and all integrity gates. Further repetition is retained
separately; this does not claim that unrelated native races are eliminated.

## Upstream mkdir and unpublished-node rollback (2026-09-20)

`mkdir.c` ports the complete upstream directory-creation transaction: locked
name admission, ACL and quota checks, ERESTART retry, DMU allocation, parent
entry/link-count updates, FUID syncing and ZIL create records. Native admission
rejects missing credentials, invalid names/types, readonly mounts and missing
writable ZIL state. Linux idmapped mounts and extended attributes supplied via
ATTR_XVATTR explicitly return ENOTSUP pending their native implementation.

The native zfs_znode_delete adapter frees the upstream object and external ACL
and closes its SA handle for failed, unpublished creation. Cache tombstones
remain until teardown so cache traversal and consumer pointers remain valid;
zget ignores them if an object number is reused. This is not final unlink
cleanup: callers must hold the creation transaction and namespace exclusion.
The normal linked-object cache and asynchronous reference-drop rules remain.

The fixture now invokes real zfs_mkdir for its new directory, tests duplicate,
invalid-name, null-credential, restrictive-parent-ACL, missing-log and readonly
rejections, and allocates/deletes both an unpublished file and directory.
Deleted objects cannot be reloaded, and neither name appears after cold import.
The authenticated DOS test resolves the actual mkdir-created directory.

Initial `qemu-run.cXI1q3cO` failed a test assumption: the initial fixture ACL
granted everyone ACE_ALL_PERMS, so a reader was allowed to create. The test now
installs an actual restrictive ACL and restores the original afterward; no
permission bypass was added. `qemu-run.FtrtxX6g` then exposed the independent
condition lifetime regression documented above. Final `qemu-run.aACkY8Rc`
passed all four boots, mkdir/rollback gates, zero cold disk writes, unchanged
backing hashes and host zdb with no leaks. Build/run logs are
`build-aros/mkdir-final-link.log` and `build-aros/mkdir-final-qemu.log`.
An unchanged-binary repeat `build-aros/qemu-run.bqa5d6Nf/` also passed all four
boots and integrity gates (`build-aros/mkdir-repeat-qemu.log`), including the
concurrent directory-lock test in every boot. Both failed runs remain retained.

Replay audit `build-aros/zpl-link.7dqy3E0s/` retains seven unresolved operations:
create, make_xattrdir, remove, rename, rmdir, setattr and symlink. Public DOS
mutation dispatch and crash replay remain incomplete; this advances their
real implementation prerequisites without claiming a writable public mount.

## Upstream symbolic-link storage and reads (2026-09-20)

`symlink.c` ports upstream zfs_symlink and zfs_readlink to native znodes.
Creation retains ACL/quota admission, directory locking, transaction retry,
FUID updates, SA/legacy target storage, insertion rollback and ZIL records.
The native adapter validates credentials, parent/object type, name and target
length, readonly state and writable-log availability; xattr directories cannot
contain symbolic links. Linux idmapped mounts and ATTR_XVATTR remain explicit
unsupported cases. SA target/size failures are preserved instead of overwritten
by a later successful directory insertion. Native readlink requires explicit
credentials and a symlink znode; path traversal authorization is the caller's
responsibility, not a fabricated per-link read-permission check.

The first link (`build-aros/symlink-link.log`) found sa_lookup_uio missing from
the libzpool version of sa.c. Normal engine builds now compile the whole upstream
sa.c in the shared native ZPL context, not a duplicate replacement function.
The expanded compile sweep includes sa.c and passes 8/8 upstream units in
`build-aros/zpl-compile.rMFv6DDC/`; this is compile evidence only.

`symlink-test.c` creates 19-byte and MAXPATHLEN-byte targets under the directory
created by the mkdir fixture. It compares complete and truncated seven-byte
reads, residual counts and untouched bytes beyond the target. It checks
null credentials/targets, denied reader creation, unsupported xattr parent,
oversized target, duplicate name, missing log, readonly creation, and readlink
of a directory. Cold boots verify persisted target bytes, type, size and links
with both disks and either sole survivor. The earlier mknode cold check now
expects the two persisted symlink entries in its directory size.

Initial native run `build-aros/qemu-run.T4qj9l65/` passed all four boots; expanded
final run `build-aros/qemu-run.5YFxd431/` also passed all four, with zero cold
guest disk writes, unchanged raw backing hashes and host zdb reporting no
leaks. Logs: `build-aros/symlink-final-link.log` and
`build-aros/symlink-final-qemu.log`.

Full replay audit `build-aros/zpl-link.c3J8cnNu/` retains six unresolved
operations: create, make_xattrdir, remove, rename, rmdir and setattr. Legacy
symlink runtime coverage and crash replay remain unproven. Public DOS
stat/read still explicitly reject symlinks: following them, loop limits and
AROS link packet semantics remain integration work, not a claimed capability.

## Upstream create, existing-file open and truncate (2026-09-20)

`create.c` ports upstream zfs_create: locked lookup/exclusive semantics, ACL and
quota admission, allocation transactions/retry, native mknode and insertion
rollback, FUID sync and ZIL create records. Existing regular-file truncation
uses the upstream zfs_freesp path. Native admission validates credentials,
types, flags and access modes; Linux idmapped mounts, ATTR_XVATTR and nonzero
initial ATTR_SIZE remain explicitly unsupported. No skip-ACL request is
admitted through this entry point. Device-node metadata paths are retained,
but device behavior and replay-created special nodes are not runtime-proven.

Readonly/log checks apply to mutation rather than indiscriminately rejecting
existing-file reads. Truncation requires ACE_WRITE_DATA even for a mode=0
caller and rejects immutable/append-only nodes. Missing writable ZIL state
cannot silently admit creation or truncation. Root/reader admission uses real
credential snapshots, not the receiving task identity.

The mknode fixture now creates its regular file through actual zfs_create.
`create-test.c` reopens it, writes/fsyncs 64 bytes with upstream vnode I/O,
checks exclusive-open and rejected-truncate preservation byte for byte, then
authorizes truncation and verifies zero cached and persisted size. It checks
null credentials, reader denial with mode=0, immutable/append-only flags and
missing log. Cold boots reopen the existing empty file without a writable log,
reject truncation/new-file creation and verify no new entry was added.
The existing three-write counter test runs before this additional 64-byte
write; it remains an exact assertion for its own I/O sequence, not the total
of the expanded fixture. DOS handler regression tests use fresh read mounts.

Initial `build-aros/qemu-run.keDGVtzp/` and expanded final
`build-aros/qemu-run.R46OfQch/` both passed all four boots. Final evidence
includes zero cold guest writes, unchanged backing hashes and host zdb with
no leaks. Logs: `build-aros/create-final-link.log` and
`build-aros/create-final-qemu.log`. Strict harness gates require the new
create/open/truncate marker in every boot.

Full replay audit `build-aros/zpl-link.SIonDWsL/` retains five unresolved
operations: make_xattrdir, remove, rename, rmdir and setattr. Public writable
DOS packet dispatch, final unlink lifetime, crash replay and the complete
management/runtime integration remain unfinished. These passing native tests
do not establish those remaining capabilities.

## Directory-backed xattrs and multi-drive regression (2026-09-20)

`xattr-dir.c` ports upstream zfs_make_xattrdir/zfs_get_xattrdir with real ACL
construction, quota admission, DMU/SA transactions, FUID updates and ZIL
records. ZXATTR locking now permits the empty-name SA reference on regular
files without weakening ordinary directory-entry validation. These are
internal helpers: callers retain mount-lifetime and permission-admission
responsibility. This is not yet a public DOS xattr API or inline-SA xattr CRUD.

The native test concurrently requests one directory from two threads and
requires the same cached znode, verifies parent/owner/group/xattr metadata,
then creates, writes, fsyncs and reads a real attribute file. Named-attribute
ACL checks reject a non-owner's creation through the upstream base-file ACL
mapping. Null credentials, nested xattrs, missing log and missing-directory
lookup are checked. Cold imports verify persistent bytes and metadata.

Final `build-aros/qemu-run.rPe3O9oo/` passed creation on two 256 MiB disk
files, full cold import and both separate sole-survivor imports. All four
boots passed actual AROS DOS handler access and xattr tests. Cold boots made
zero disk writes, backing hashes were unchanged, and host zdb reported no
leaks. Logs: `build-aros/xattr-dir-final-link.log` and
`build-aros/xattr-dir-final-qemu.log`.

Earlier `build-aros/qemu-run.TQFxlQ0n/` is retained as incomplete: security
output split a hard-link marker, so the strict harness stopped before its
integrity checks despite guest success markers. It is not counted as a full
pass. No validation gate was relaxed for the successful repeat.

Full replay audit `build-aros/zpl-link.4D6bIFQi/` now retains four unresolved
operations: remove, rename, rmdir and setattr. Actual crash recovery, final
unlink/xattr cleanup and public DOS mutation dispatch remain unfinished.
Orderly export and cold import are not evidence of power-loss replay.

## Upstream rmdir and exclusive unlinked reclamation (2026-09-20)

`rmdir.c` ports upstream directory removal, including name/parent locks,
delete ACL admission, nonempty and current-directory checks, transaction
retry, unlinked-set insertion and ZIL records. Native admission rejects
missing credentials, unsupported flags, readonly mounts and missing logs.
The actual upstream delete ACL routine is now compiled for AROS; native
owner/privilege and sticky-directory adapters replace Linux inode policy.
As with the upstream entry point, callers must have resolved the parent
through an authorized path; this is not yet public DOS mutation dispatch.

`unlinked.c` retains upstream purge/rmnode transaction algorithms: directory
xattrs are purged, regular-file data is freed, attached xattr directories are
queued, external ACLs and the object are freed, and the unlinked entry is
removed. Failed reclamation retains the persistent queue and cached state.
Native `oz_unlinked_drain_exclusive` requires explicit exclusion of all
other mount operations. It is synchronous and never runs inside zrele or
ZIL reference completion. Live references return EBUSY rather than permitting
reclamation. Deleted native nodes retain handle-less cache tombstones until
teardown, preserving traversal lifetime and object-number reuse handling.
Automatic runtime/background draining and writable mount admission still
need integration; the exclusive helper must not be called concurrently.

The QEMU fixture creates a directory with an xattr directory and a real
64-byte attribute file. Removal keeps the held directory alive, then allows
the directory/xattr directory to retire while the held attribute remains
readable. After releasing the final reference, all three DMU objects are
absent and the persistent unlinked set is empty. Tests also cover nonempty,
non-directory, current-directory, immutable, missing-log and null-credential
denials. The expanded test persists a target DENY ACE and verifies it wins
over parent deletion rights, plus sticky-directory reader/root admission.

Initial `build-aros/qemu-run.bLc29BSX/` and expanded final
`build-aros/qemu-run.EHwkXW8R/` passed all four boots: native two-disk
mirror, full cold import and each sole survivor, with actual DOS handler
regression tests. Final checks include zero cold writes, unchanged backing
hashes and zdb reporting no leaks. Logs: `build-aros/rmdir-final-link.log`
and `build-aros/rmdir-final-qemu.log`. The new cleanup marker is mandatory
in all four boots. This does not yet test draining after a crash or importing
a populated unlinked set, nor concurrent background reclamation.

Full replay audit `build-aros/zpl-link.jMq2CjkX/` retains three unresolved
operations: remove, rename and setattr. No unresolved sections were discarded
and no successful stubs were introduced. Full replay remains unproven.

## Upstream file unlink and open-reference lifetime (2026-09-20)

`remove.c` ports the upstream zfs_remove locked namespace/ACL/transaction
path, including transaction restart, link-count updates, unlinked-set
insertion and ZIL remove records. Native nodes always take upstream's
deferred-deletion path: Linux inode-count/cache heuristics and eager xattr
detachment are not used. The exclusive drainer already owns actual data,
xattr and object reclamation. This is a native lifetime adaptation, not
a fake successful unlink or a replacement on-disk implementation.

The old internal hard-link test helper is removed: its remove/relink sequence
now uses actual zfs_remove and still verifies the surviving original link.
The new test creates and writes an 8192-byte file, removes its final name,
checks that held references prevent reclamation and that relinking the
unlinked object fails, then appends one byte and reads all 8193 bytes through
the existing reference. Last release plus explicit draining frees its object.
Symlink unlink/drain is also exercised. A two-thread same-name removal race
requires exactly one success and one ENOENT, with zero remaining links.

Guards cover directory removal through the wrong API, missing names, null
credentials, unsupported flags, NOUNLINK, missing writable log and readonly
mounts. Cold boots verify every transient name remains absent and the
unlinked set remains empty. As with rmdir, these internal vnode entry points
assume the caller resolved an authorized parent path; public DOS mutation
dispatch and concurrent/background draining remain separate integration work.

Initial `build-aros/qemu-run.pMZzVOZH/` and expanded final
`build-aros/qemu-run.0MVWuwa0/` passed all four boots, DOS handler
regressions, zero cold writes, unchanged backing hashes and zdb no-leak
checks. Logs: `build-aros/remove-final-link.log` and
`build-aros/remove-final-qemu.log`. Initial build failure
`build-aros/remove-link.log` records a test call with the wrong symlink
signature, corrected before any test image was booted.

Full replay audit `build-aros/zpl-link.0xnaXrWD/` now retains only
zfs_rename and zfs_setattr as unresolved operations. This is not proof of
runtime replay: native replay admission, crash tests and recovery of a
populated persistent unlinked set still remain.

## Rename ancestry read errors (2026-10-03)

The native ancestry walker now propagates parent-SA lookup errors. Previously
it ignored the error and reused the preceding object ID; a failed first lookup
could attempt to acquire an already-held parent lock instead of returning the
I/O error. The existing rename exit path releases the accumulated ancestry
locks and znode references before returning, without starting a transaction.

`test-rename-lock.sh` extracts and compiles the production ancestry helper with
deterministic host fault injection, using an explicitly supplied external
artifact directory. Baseline `rename-lock.TFegvk8W` hit the double-lock assertion;
fixed `rename-lock.QiufWLIf` passes normal traversal, first and deeper lookup
failures, and complete lock/reference cleanup. Both are beneath
`/mnt/openzfs-artifacts/build-aros/`. The preceding
`rename-lock.sCf2aTw0` was a harness extraction compile failure, not a filesystem
failure. These host checks do not establish native filesystem or SMP behavior;
native rebuilt regression results are recorded separately when available.

Rebuilt native binaries in `build-aros/rename-ancestry-native-20261003` on the
external archive passed `qemu-run.Ga0wZvL0` (runner exit 0): two-member
partition-backed mirror creation, rename/move/replace/exchange, cold readonly
import, each sole mirror survivor, zero writes during readonly boots, unchanged
original hashes, and cold writable reopen on COW disks. The full runner log is
`OpenZFS-archive/rename-ancestry-native-partition-qemu-20261003.log`.
The preceding whole-disk run `qemu-run.TNRRFqTo` was refused with EBUSY because
DOS had claimed the disk; it never reached rename. The partition-backed run
preserved that ownership check. Native injected-SA failures are not claimed:
fault injection is the host helper test, while QEMU proves normal-path
filesystem regression coverage with the rebuilt native implementation.

The subsequent `qemu-run.vspglzwJ` closes that native injection gap. Diagnostic
executables alone link `--wrap=sa_lookup`; a task-bound, exact handle/attribute
filter injects EIO at the first and deeper parent in the real ancestry walk.
Each failed rename preserves source identity, target absence, and both parent
sizes/link counts. The next uninjected rename succeeds. The runner requires
`Native rename ancestry SA failure/retry result=0`, then verifies persistence
and the rest of the mirror, cold import, both survivors and writable COW reopen
suite. Runner exit 0 and original member hashes match. No injection code is
linked into the production service or library. Build logs and runner log use
the `OpenZFS-archive/rename-injected-*-20261003.log` prefix; binaries are in
external `build-aros/rename-ancestry-injected-20261003`.

## Upstream rename, replacement and directory moves (2026-09-20)

`rename.c` ports upstream rename and ancestry-lock routines, retaining
name/object lock ordering, normalization paths, project inheritance checks,
transaction restart, ZIL records, replacement and broken-link rollback.
The actual upstream rename ACL path is now compiled for native znodes.
Native admission validates names, credentials, mount identity, flags,
readonly/log state and target name length. Same-name requests explicitly
look up the source because the Linux VFS no longer does that for us; absent
sources cannot incorrectly succeed, and NOREPLACE rejects an existing target.
File exchange also requires source-directory add permission.

Whiteout requests and directory exchange are explicitly ENOTSUP. The latter
still needs native reverse-ancestry/two-tree admission previously provided
outside this upstream routine. Their retained upstream implementation is not
advertised as working. Normal directory moves and file exchanges are tested.
Casefold/Unicode variants and injected low-space rollback remain unverified.
These internal operations assume authorized resolution of parent paths;
public DOS mutation dispatch is still unfinished.

The fixture persists a renamed directory tree and distinct file contents
across cold imports. It checks cross-directory moves, NOREPLACE, replacement
of a held file (old bytes stay readable until release/reclamation), atomic
file exchange, cycle rejection, type mismatches, missing/null-credential/log
admission and reader denial. Failed replacement of a nonempty directory must
restore the source name and link counts. Expanded tests replace an empty
directory successfully and reclaim the old target, then run two opposing
threads through 32 file exchanges each and verify the final names/content.
Cold checks verify directory parent SA, sizes/link counts, file bytes and
an empty unlinked set.

Initial `build-aros/qemu-run.mmc8GVsS/` and expanded final
`build-aros/qemu-run.ZSdBVxhj/` passed all four boots and actual DOS handler
regressions, including either sole mirror survivor, zero cold writes,
unchanged backing hashes and zdb with no leaks. Final logs:
`build-aros/rename-final-link.log`, `build-aros/rename-final-qemu.log`.
Full replay audit `build-aros/zpl-link.RnnHij7I/` now retains only
zfs_setattr as unresolved. A successful link, once reached, still will not
prove recovery: native replay admission and crash tests remain required.

## Upstream setattr and native security boundary (2026-09-20)

`setattr.c` retains upstream SA transactions, ACL/FUID ownership updates,
xattr-directory owner propagation and ZIL records. Native cached metadata
replaces Linux inode fields. Admission checks all requested rights before
the upstream early truncate, so an unauthorized combined owner/size request
cannot truncate a file. Caller attributes are copied, timestamps are supplied
locally, and unsupported flags, namespaces and XVATTR requests fail explicitly.
`xvattr.c` retains the real upstream SA/flag helper needed by the complete
link; this does not advertise public XVATTR support.

`setattr-test.c` verifies denied combined requests preserve size, null
credentials, append-only/missing-log/invalid-time guards, ownership and group
changes, supplementary-group admission, chmod, truncation, explicit times,
and readonly rejection. Both cached and persisted IDs are checked on the
file, xattr directory and its attribute file. Cold imports verify bytes,
size, permissions and timestamps. Named-ACL ownership changes and concurrent
xattr-owner propagation still need expanded tests.

Evidence: `build-aros/qemu-run.lbfTybNv/`, with all four boots passing,
zero cold writes, unchanged backing hashes and zdb reporting no leaks.
Build and harness logs are `build-aros/setattr-helper-link.log` and
`build-aros/setattr-helper-qemu.log`. Prior failed links are retained in
`build-aros/setattr-link.log` (missing real zfs_xvattr_set helper).
Full replay link audit `build-aros/zpl-link.HKSNJTBs/` now passes without
discarding unresolved operations. Runtime replay is NOT yet verified.

The security.library API review found a separate integration gap. Current
DOS credentials use secGetPktOwner and correctly translate native root
65535 to POSIX root 0, rejecting native nobody UID 0. Native packet admission
still needs secAccess_Control with the filesystem port context, property
checks using secGetRelationshipA/ secRelF_PROPERTY_ACCESS, and creation
defaults from secGetPktDefProtection. secRegisterHandler registers a plugin
operation table, not a filesystem automatically. AROS AFS is the reference
for packet lifetime and these native calls. Its unconfigured-security policy
also differs from our current fail-closed behavior; that choice must be
explicit at the volume boundary. Native 16-bit owners/protection flags must
not truncate imported FUIDs or replace OpenZFS NFSv4 ACL enforcement.
Public DOS mutation dispatch remains unfinished.

## Native packet security contexts (2026-09-20)

The DOS frontend now owns an opaque packet security context from receipt
through reply. It retains security.library, the actual sender's secExtOwner,
translated immutable credentials, filesystem port and secGetPktDefProtection
snapshot; cleanup frees each once. Existing identity-only callers retain a
credential reference before releasing the context. Missing/unconfigured
security remains fail-closed, not implicit handler/root access.

The context API calls real secAccess_Control with secAC_FILESYSTEM_CONTEXT
and preserves native denial flags, maps readonly denial to EROFS, and checks
secGetRelationshipA against secRelF_PROPERTY_ACCESS. Inputs are explicitly
native packed owner IDs and Amiga protection bits, not ZFS IDs or POSIX mode.
The context is now captured by the DOS handler, but object-level use of these
checks and creation defaults still needs native metadata policy wiring.
No ZFS ACL is replaced, no ID truncation is introduced, and public mutations
remain unavailable. Volume quarantine/readonly integration requires further
tests with a security.library-registered volume; the current isolated port
tests exercise user/group/property policy, not a registered volume fixture.

Cross-task QEMU tests run the handler as root and sender as reader77/group88,
with supplementary group99. They verify owner denial, other/group read
allowance, combined access denial, property ownership/unowned behavior,
invalid access masks, native creation-default capture, and repeat policy
checks after sender logout and exit. Explicit secSetDefProtectionA establishes
the sender's defaults: passing that tag to secLoginA alone did not set them.
That failed test remains in `build-aros/qemu-run.3YIVgHOY/` and
`build-aros/packet-policy-qemu.log`; no passing result is claimed for it.

Corrected full four-boot run `build-aros/qemu-run.ApMoDxwt/` passes the new
strict security marker, existing authenticated DOS access, cold import and
both sole-survivor boots. All cold writes are zero, backing hashes unchanged,
and zdb reports no leaks. Logs: `build-aros/packet-policy-final-link.log` and
`build-aros/packet-policy-final-qemu.log`. DUT and user hosted sessions were
not used.

## DOS owner reporting without identity truncation (2026-09-20)

The read-mount metadata bridge now preserves full stored UID/GID FUIDs and
separately resolves their local IDs with the existing domain mapper. DOS
FileInfoBlock ownership is populated through an explicit reverse conversion:
POSIX root0 -> native65535, ordinary1..65534 unchanged, and the mapped nobody
group65535 -> native0. Unmapped, oversized and conflicting UID65535 values
return EOVERFLOW, not a truncated ID, root or an invented unowned identity.
The prior zero-filled FIB silently advertised every ZFS file as unowned.
This changes metadata reporting, not the richer upstream ACL access checks.
FIB protection is still the readonly mount projection; persistent native
protection metadata, property policy and extended owner reporting for foreign
IDs remain unfinished. Native Examine currently reports overflow for an
identity it cannot represent; ordinary open/read is not narrowed this way.

Native conversion tests include root, ordinary IDs, high IDs, nobody group,
encoded high-bit identities, unmapped sentinel and invalid output pointers.
Actual DOS Examine tests verify root ownership on root/file and 77/99 on
the persisted setattr fixture. The reader is correctly denied traversal of
that deliberately non-searchable parent even though it owns the child; root
can examine the child and sees the correct identity and size.

Initial run `build-aros/qemu-run.7xyog6ZN/` passed four boots. Expanded run
`build-aros/qemu-run.bmhMd2No/` failed because the test incorrectly expected
the owner to traverse that parent; its ACL was preserved and the expectation
corrected. Final `build-aros/qemu-run.tPRXSJjX/` passed all four boots,
authenticated DOS tests, zero cold writes, unchanged disk hashes and zdb
no-leak checks. Logs: `build-aros/native-owner-verified-link.log` and
`build-aros/native-owner-verified-qemu.log`. Both earlier run logs are retained.

## Native upstream xattr read selection and lifetime (2026-09-20)

`xattr.c` adapts upstream SA-first/directory-fallback lookup without changing
either on-disk representation. The caller supplies an explicit credential,
holds mount/znode lifetime, and receives positive errno plus a separate size.
Named-attribute rights on the base are checked before either backend; the
directory backend also checks attribute-file read rights. SA errors other
than absence do not silently fall back to another value. Length queries,
short-buffer ERANGE, missing ENODATA and copied result storage are supported.
There is no borrowed nvlist pointer after the xattr lock is released.

Native cache disposal now frees owned xattr nvlists. Their presence alone is
not an active reference and must not permanently prevent unmount or exclusive
unlinked draining. Existing external-reference/dirty-atime/mapping/sync guards
remain. The rmdir test populates a cache before unlink, retains the existing
held-reference EBUSY checks, then verifies eventual object reclamation.

Tests read the existing 64-byte directory-backed attribute through the new
API, check size/short buffers/missing names/null and denied credentials, and
persist a binary inline SA attribute using actual zfs_sa_set_xattr. Every cold
boot reads both original formats successfully. Initial K8nQnVxV and expanded
final `build-aros/qemu-run.MJjNLfSI/` passed all four boots, actual authenticated
DOS regressions, zero cold writes, unchanged backing hashes and zdb no leaks.
Logs: `build-aros/xattr-read-final-link.log`,
`build-aros/xattr-read-final-qemu.log`.

This is a prerequisite for native protection metadata, not its completed
storage/API contract. Generic xattr mutation/listing and namespace admission,
AROS protection-bit persistence and per-object security.library integration
remain work; inline SA writes above are internal fixture construction only.

## Native xattr create/replace/remove (2026-09-20)

`oz_xattr_set` now implements upstream-style SA/directory backend selection,
create-only and replace-only checks, empty values and removal. It holds the
base xattr writer lock across existence checks and mutation. Small values
prefer SA when requested by mount policy; oversized values use real attribute
files. Successful migration removes the previous backend copy. Non-size SA
failures are returned, not hidden by another backend, and failed SA mutations
drop the potentially inconsistent cached nvlist for authoritative reload.
Directory mutation uses native upstream create/setattr/write/remove with
explicit credentials and records base ctime in SA, rather than depending on
a Linux inode dirty callback. Missing removal succeeds unless REPLACE was
requested, matching the upstream internal setter convention.

Admission checks explicit credentials, named-attribute rights, readonly,
immutable/append-only, live log, names, sizes and flags. This remains an
internal named-attribute interface: namespace policy and security.library
property admission must precede any public DOS exposure. It does not yet
define the persistent AROS protection-bit encoding or expose DOS writes.
Backend migration is multi-transaction as upstream: cleanup failures are
returned, but power-loss/fault-injection recovery of partial migrations and
directory truncate/write failures remains unverified. No atomic/crash-proof
replacement claim is made from orderly export tests.

QEMU tests exercise SA -> 40000-byte directory value -> SA, a persistent
large value, zero length, removal, missing/duplicate/invalid flag guards,
missing log, immutable, null/denied credentials and readonly cold rejection.
Two concurrent CREATE requests require exactly one success and one EEXIST.
Cold tests compare bytes and assert the old backend name is actually absent,
not merely hidden by SA lookup precedence. Existing unlinked draining reclaims
the migration's removed attribute file without leaks.

Initial `build-aros/qemu-run.ye2EUl1T/` passed four boots. Expanded
`build-aros/qemu-run.V9VXQM0D/` is incomplete: security.library serial output
split the survivor0 final PASS marker and the strict harness timed out before
remaining gates. Same binaries repeated as `build-aros/qemu-run.j66W3DcA/`
passed all four boots, zero cold writes, unchanged hashes and zdb no leaks.
Logs: `build-aros/xattr-write-final-link.log`,
`build-aros/xattr-write-verified-qemu.log`; failed observation is retained in
`build-aros/xattr-write-final-qemu.log`. No DUT/user hosted use.

## Persistent native protection and DOS read enforcement (2026-09-20)

`native-protection.c` stores OS-specific metadata in the normal upstream
xattr `system.aros.protection`: exactly eight bytes, ASCII `ARP1` followed
by the complete 32-bit Amiga protection word in big-endian order. This does
not change the pool format or replace the native ZFS ACL. Absent metadata
adds no per-object native restriction. Wrong size/version is an error, not
silently absent/permissive metadata. The internal store requires ZFS write
attributes and named-attribute rights; it is not a public property setter.

The read-mount bridge retrieves this private metadata internally; the DOS
frontend separately enforces the requesting packet's upstream ZFS ACL and
then security.library native protection/volume policy. It checks traversal
on each parent, final metadata, open and each read, not just at handle creation.
Without native metadata it calls a volume-only native check, leaving object
authorization to ZFS; no POSIX mode projection replaces the richer ACL.
FIB reports the native bits with the readonly mount's write/delete denial
overlaid. The borrowed packet security context is cleared before reply.
Unmapped owners on native-protected objects fail rather than being narrowed.

Tests create ZFS-readable files which native bits deny or explicitly allow
to the reader, plus a ZFS-searchable directory which native protection makes
non-searchable. Actual DOS opens verify the distinction for reader/root;
Examine verifies protection bits. A root-opened handle is denied on Read after
its sender logs in as the reader. Internal tests check absence, malformed
record rejection/repair, exact persistent bits, null credentials and readonly
store rejection. Creation defaults and public SetProtection/owner/date/write
packets, namespace protection against generic xattr modification, registered
volume quarantine/readonly tests and power-loss recovery remain unfinished.

Initial MOtrBU6u passed creation but failed cold import: the old hardlink test
assumed no later root siblings. Its final assertion now compares persisted
directory size to actual ZAP count plus dot entries; initial mutation deltas
and link counts remain checked. Intermediate eXQBN0c9 and expanded final
`build-aros/qemu-run.ogTIHoBC/` passed all four boots, strict new protection
markers, actual DOS tests, zero cold writes, unchanged backing hashes and
zdb no leaks. Logs: `build-aros/native-protection-verified-link.log` and
`build-aros/native-protection-verified-qemu.log`. Failed initial evidence is
retained. No DUT/user hosted session was used.

## Explicit writable bridge lifetime (2026-09-20)

The reusable native mount bridge now has an explicit writable constructor;
existing `oz_zpl_open` remains readonly. Writer admission requires a writable
pool, a non-snapshot dataset and writable property state. Recovery is not yet
wired, so any inherited ZIL block pointer or claim TXG is rejected with
ENOTSUP (including potentially empty allocated chains) rather than opening a
writer over unresolved recovery state. This conservative restriction is not
a substitute for the required full replay implementation.

Accepted mounts open the actual upstream ZIL with zfs_get_data. Explicit sync
commits the log and flushes cached atimes/TXGs. Close synchronizes and closes
the ZIL while its native nodes still exist, then disposes the cache, property
callbacks and metadata before objset disown. A close error retains ownership;
after a log has closed, retry cleanup rather than issue new mutations. Public
DOS is still readonly: the internal protection-store bridge takes explicit
credentials but is not the native packet property-policy boundary.

The new isolated mountlife dataset test opens a writer, persists a full-width
protection word through the real xattr path, syncs/closes, reopens readonly,
verifies metadata, rejects mutations and syncs/closes again. Three separate
cold boots verify the same record and reject writer admission on the readonly
imported pool. `build-aros/qemu-run.sqFkUd4E/` passed all four boots, strict
lifecycle markers, existing authenticated DOS tests, zero cold writes,
unchanged hashes and zdb no leaks. Logs: `build-aros/mount-lifecycle-link.log`
and `build-aros/mount-lifecycle-qemu.log`. Pending-log rejection is implemented
but actual crash/replay and partial-close error injection remain unverified.

## Native DOS protection mutation (2026-09-20)

ACTION_SET_PROTECT now reaches the upstream native-protection xattr on an
explicit writable mount. It checks the packet sender's property relationship,
filesystem-context volume policy, ZFS WRITE_ATTRIBUTES and WRITE_NAMED_ATTRS,
then synchronizes before acknowledging success. Parent traversal remains
authorized, but final-object native READ is not required to repair protection.
Readonly mounts reject changes. FIB protection reflects the stored full-width
word on writable mounts; readonly mounts additionally overlay write/delete
denials. ACTION_FLUSH now reports real synchronization errors.

Isolated QEMU evidence `build-aros/qemu-run.ZbTk9t35/` exercised real DOS
SetProtection calls: root changes and restores protection, all 32 bits survive
Examine and cold imports, a different user is denied despite permissive ZFS
fixture ACLs, missing names fail, and readonly mutations fail. Both members
independently passed cold boot with zero writes, unchanged backing hashes and
zdb no leaks. Logs: `build-aros/dos-protection-link.log` and
`build-aros/dos-protection-qemu.log`. The harness now requires explicit property
markers when the security fixture is enabled. Lifecycle tests link only into
QEMU test programs, not the management service or replay link audit.

This supersedes the blanket readonly DOS statement above only for protection
mutation. File-content and namespace mutations are not yet public DOS
operations. Ordinary non-root owner restoration and registered-volume
quarantine remain separate required tests; root success does not prove them.

Rebuilt after the test-link correction and repeated the entire suite with the
new strict gates: `build-aros/qemu-run.Szv5gNpH/` PASS (all four boots, zero cold
writes, unchanged hashes, zdb no leaks). Retained final build/test logs are
`build-aros/dos-protection-final-link.log` and
`build-aros/dos-protection-final-qemu.log`.

## Native DOS modification dates (2026-09-20)

ACTION_SET_DATE uses the same packet property/volume policy and parent
traversal checks, then calls actual upstream zfs_setattr with ATTR_MTIME and
the sender's credential. It syncs before success. DateStamp days/minutes/ticks
are validated before mutation and converted from the 1978 epoch; fractional
50 Hz ticks round-trip through ZFS nanoseconds and FileInfoBlock. This retains
the existing timestamp epoch convention, not a new timezone conversion.

`build-aros/qemu-run.Wsaxcl5A/` passed all four boots with strict SetFileDate
markers: live and cold Examine return the exact DateStamp, five malformed
date shapes fail, missing names fail, a non-owner is denied and readonly calls
fail. Both sole mirror survivors preserve the value, cold writes are zero,
backing hashes are unchanged and zdb reports no leaks. Logs:
`build-aros/dos-date-link.log`, `build-aros/dos-date-qemu.log`.

## Ordinary-owner property repair regression (2026-09-20)

The mountlife fixture now assigns nested.txt to POSIX 77:88 through real
upstream setattr (an internal explicit-credential ownership bridge, not yet
a DOS SetOwner packet). Root applies native READ/WRITE denial; the actual
ozreader task must see Open denied, change the date while READ is denied,
restore protection and read the original byte. Cold imports repeat ownership,
exact timestamp and read checks through DOS.

Initial run `build-aros/qemu-run.Y3KJqOvz/` failed: date mutation reached ZFS,
but xattr replacement opened/truncated the hidden storage file as the caller.
Root had created that file, so its private ownership incorrectly overrode
the base object's named-attribute permission. Upstream zpl_xattr_set_dir
instead looks up existing storage, uses zfs_freesp and zfs_write_simple after
base admission. The native replacement path now follows those operations.
Creation still carries the sender credential and rechecks the xattr
directory's base-object named-attribute ACL; no general ACL bypass is enabled.

Intermediate `build-aros/qemu-run.1RClCUjD/` failed with ENOTSUP after trying
upstream's ATTR_NOACLCHECK creation flag; the native ACL layer intentionally
rejects that flag. That attempt was removed rather than relaxing the ACL
guard. Both failed runs and their build logs remain available. This work
does not claim transactional atomicity across xattr truncate/write or crash
recovery; those remain required tests and integration work.

Corrected run `build-aros/qemu-run.0aFqs7Jr/` PASS: ordinary-owner repair,
exact dates and owner IDs, full existing suite, cold mirror and both sole
survivors, zero cold writes, unchanged hashes, zdb no leaks. Build/test logs:
`build-aros/dos-owner-repair-verified-link.log` and
`build-aros/dos-owner-repair-verified-qemu.log`. Earlier replay link audit
`build-aros/zpl-link.gGq6PXte/` passed after DOS date integration; this is
link evidence only, not runtime replay evidence.

## DOS existing-file content writes (2026-09-20)

ACTION_WRITE now admits writable mounted files through both current-sender
native policy and ZFS WRITE_DATA, then uses upstream zfs_write and zfs_fsync.
The bridge requires explicit credentials, validates offsets/lengths and
preserves transferred-byte accounting. A full transfer followed by a sync
error reports failure rather than successful completion; partial transfers
retain their count and error. Readonly writes still fail, including zero-byte
requests. Files opened with MODE_OLDFILE can be updated; FINDUPDATE/FINDOUTPUT
creation/truncation is still pending and fails explicitly on writable mounts.

Read and Seek now refresh size from the current znode instead of retaining
an open-time EOF snapshot. Read clamps safely when a position is beyond EOF.
The dedicated doswrite dataset test writes 4096 patterned bytes via real DOS,
uses a second pre-existing handle to read/seek the extended file, switches
identity with the writer still open and verifies native write denial, then
checks the exact bytes through DOS after cold mirror/sole-member imports.
Partial I/O fault injection, full new-file lifetime and crash recovery remain
unverified; this is not yet the complete writable DOS interface.

`build-aros/qemu-run.mAA3QLnP/` passed all four strict-gated boots with
zero cold writes, unchanged backing hashes and zdb no leaks. Retained logs:
`build-aros/dos-content-link.log`, `build-aros/dos-content-qemu.log`.

## DOS resize (2026-09-20)

ACTION_SET_FILE_SIZE now uses sender-authorized upstream zfs_setattr
(ATTR_SIZE) and fsync. Classic DOS beginning/current/end offsets are checked
for negative sizes and the signed LONG return limit; 64-bit DOS packet
extensions remain pending. Like AROS AFS setFileSize, a request may not
truncate behind another open handle's position. The requesting handle is
clamped to the new EOF after shrinking and other cached sizes are refreshed.

The real DOS client shrinks 4096 bytes to 32, checks the retained prefix and
EOF through another handle, verifies peer-position and negative-length
rejection, extends relative to the current position and verifies zero-filled
new bytes. It then rewrites the complete pattern for cold verification.
Held-handle caller-switch and readonly resize denial are also checked.
`build-aros/qemu-run.ExcNocs9/` passed all four boots and strict resize gates,
zero cold writes, unchanged backing hashes and zdb no leaks. Logs:
`build-aros/dos-resize-link.log`, `build-aros/dos-resize-qemu.log`.
Cold tests verify the final rewritten content and readonly rejection; they
do not independently prove a crash during resize or a persisted sparse hole.

## DOS update-open creation and native defaults (2026-09-20)

ACTION_FINDUPDATE (MODE_READWRITE) now opens an existing regular file without
truncation or creates a missing leaf through upstream zfs_create. Parent
resolution preserves AROS slash semantics and checks EXECUTE/ADD_FILE through
both native policy and ZFS. Existing objects require read/write admission;
readonly update opens fail. FileHandle validation and handle-slot reservation
precede mutation. MODE_NEWFILE remains unsupported: its AROS replacement and
exclusive-lock contract must not be silently replaced by simple truncation.

New nodes take ownership from the packet sender's credential and initial
native protection from secGetPktDefProtection. Initial metadata uses an
internal credential after successful creation so restrictive inherited ACLs
cannot prevent initialization; subsequent user edits still require property
and named-attribute authorization. Success waits for synchronization.
Initialization failure attempts upstream unlink; atomic create-plus-native
metadata recovery and reclamation after failed initialization remain pending.
This is a serialized mount bridge, not a claim of crash-atomic publication.

`build-aros/qemu-run.n0jocKJH/` passed all four strict-gated boots. A real
ozreader DOS client created subdir/created-by-reader, wrote bytes, and restored
its task defaults. Live/cold Examine verified UID 77, GID 88, native default
EXECUTE-deny/GROUP-READ bits (plus readonly overlay when applicable), size and
exact content. Existing MODE_READWRITE preserved contents. Both sole mirror
members passed with zero cold writes, unchanged hashes and zdb no leaks.
Logs: `build-aros/dos-create-link.log`, `build-aros/dos-create-qemu.log`.

## DOS directory creation (2026-09-20)

ACTION_CREATE_DIR uses upstream zfs_mkdir with the sender's credential,
native/ZFS parent search and ADD_SUBDIRECTORY checks, packet default
protection, synchronization and a real DOS lock result. Lock capacity is
checked before mutation. File and directory creation share initial-metadata
handling; directory initialization failure attempts upstream rmdir rather
than file unlink. Crash-atomic initialization and failed-init unlinked cleanup
remain pending, as for file creation.

The real non-root client creates made-by-reader, checks duplicate rejection,
then creates and writes a child. Live and cold DOS checks verify directory
type, UID 77/GID 88, inherited task protection defaults and child bytes.
Readonly CreateDir is rejected. `build-aros/qemu-run.gN6o7a6n/` passed all
four strict-gated boots, zero cold writes, unchanged backing hashes and zdb
no leaks. Logs: `build-aros/dos-mkdir-link.log`,
`build-aros/dos-mkdir-qemu.log`. Management-service mount admission remains
readonly pending complete writable lifetime/recovery integration; this test
uses the explicit isolated writable mount constructor.

## DOS deletion and serialized reclamation (2026-09-20)

ACTION_DELETE_OBJECT checks native target DELETE and parent WRITE, matching
AROS AFS's security boundary. Parent traversal remains ZFS/native authorized.
Actual zfs_remove/zfs_rmdir performs the upstream DELETE-or-DELETE_CHILD ACL
decision; native admission does not incorrectly require both ZFS rights.
Open DOS handles or locks on the target reject deletion with OBJECT_IN_USE.
Nonempty directories and missing objects return their distinct DOS errors.

The private removal bridge releases its znode references, synchronizes,
drains the real persistent unlinked set under exclusive single-owner mount
admission, and synchronizes again. Writable close also drains that set before
closing the ZIL. This is not a concurrent background reaper: callers must
exclude competing operations/target references. Failures after unlink can
leave the name removed and the persistent queue awaiting a retry; crash
recovery and fault-injected cleanup remain unverified.

`build-aros/qemu-run.mC2E9rkk/` passed all four strict-gated QEMU boots. A
non-root DOS client checked file/directory lock and open-handle rejection,
DELETE protection denial, nonempty-directory rejection, successful file and
empty-directory removal, repeated-delete missing errors, and cold absence.
Readonly deletion remained denied. Both sole mirror members passed with
zero cold writes, unchanged backing hashes and zdb no leaks. Logs:
`build-aros/dos-delete-link.log`, `build-aros/dos-delete-qemu.log`.

## DOS rename and move (2026-09-20)

ACTION_RENAME_OBJECT resolves both parent locks/paths and enforces AROS AFS
native policy: property permission on the source and WRITE permission on both
parents. Upstream zfs_rename provides ZFS ACL admission, ancestry/cycle checks
and namespace transactions. RENAME_NOREPLACE prevents replacing an existing
destination. Success synchronizes before reply. Handles keep stable object
IDs; matching source locks update their cached leaf after a successful move.
Fault-injected post-rename sync failure and multi-hardlink alias behavior
remain unverified; this is not crash recovery proof.

Initial `build-aros/qemu-run.i6k90ddp/` passed all four boots: a non-root caller
moved a file while its handle and lock remained open, verified Read, Examine
and NameFromLock, moved a directory with a child, and checked destination
collision, non-owner and directory-cycle rejection. Cold imports verified
new paths/bytes and absence of old paths; readonly rename failed. The compiler
also identified possible truncation in the old 108-byte lock-name cache.
The FIB boundary already rejected oversized names; it was not an unchecked
copy. Locks and parent/name lookup buffers now retain the full 255-byte name,
allowing that existing legacy-FIB limit to report ENAMETOOLONG accurately.
An additional test creates a 120-byte name, checks the legacy Examine error,
renames it to a short name with the same lock still open, verifies the updated
FIB name, then removes it.

Expanded run `build-aros/qemu-run.KJ3eqv72/` is incomplete evidence: all
operation markers passed, but security task-exit logging split the last
survivor's final RESULT line. The strict harness timed out and stopped its
owned guest. Its logs were retained; no matcher was relaxed. The same built
binaries were then rerun from fresh disposable disks.

Repeat `build-aros/qemu-run.Fdw8muHy/` PASS: all four strict-gated boots,
zero cold writes, unchanged backing hashes and zdb no leaks. Expanded build
log: `build-aros/dos-rename-verified-link.log`; passing repeat log:
`build-aros/dos-rename-repeat-qemu.log`. Management service link was also
rechecked separately in `build-aros/dos-rename-service-link.log`; no service
deployment or hosted UI runtime claim is implied.

## DOS replacement and exclusive output opens (2026-09-20)

MODE_NEWFILE now uses actual upstream removal and creation, rather than
truncating the existing object. Existing handles/locks and native WRITE or
DELETE denial prevent replacement. A successful output open starts empty,
receives the caller's ownership/default protection, and excludes other opens,
locks, rename, protection/date changes and deletion until close. Ordinary
MODE_READWRITE remains non-truncating. Explicit Lock(EXCLUSIVE_LOCK) is still
separate unfinished work. Removal followed by creation is not crash-atomic;
allocation or sync failure after removal can leave the old name absent.

`build-aros/qemu-run.FEmI3fav/` passed all four strict-gated boots. Real
non-root DOS tests verified busy and delete-protected replacement rejection,
unchanged old bytes after rejection, empty replacement, new bytes, competing
operation denial, and creation of a previously absent output file. Cold
mirror and either sole survivor verified bytes, UID/GID and default protection.
Cold boots issued zero block writes, backing hashes were unchanged and zdb
reported no leaks. Logs: `build-aros/dos-exclusive-link.log` and
`build-aros/dos-exclusive-qemu.log`. This does not prove failure-atomic
replacement, crash recovery or unrestricted writable management admission.

## Native write-only open and seek admission (2026-09-20)

Following AROS AFS main.c's FINDUPDATE policy, MODE_READWRITE now checks
WRITE on an existing target, not READ plus WRITE. Final path admission uses
the operation's actual right instead of an incidental metadata READ check.
Read still reauthorizes the current packet sender. Seek accepts either
READ or, on a writable mount, WRITE; it does not make a held handle a
permanent authorization grant. Unexpected access errors are not bypassed.

Initial `build-aros/qemu-run.jX3aH07W/` failed the new test because its setup
used ordinary SetProtection: the native limiter preserved GROUP_READ and
security.library correctly allowed reading via group membership. The test
now uses secSetProtection to set the entire mask, including group/other,
without changing handler authorization or global security configuration.
Retained failed logs: `build-aros/dos-write-only-qemu.log`.

`build-aros/qemu-run.IOR5pXZc/` passed all four strict boots. The real non-root
DOS client checked OLDFILE rejection, READWRITE success, denied Read with
unchanged destination byte, end/beginning seeks and writes without truncation,
exact resulting bytes after protection restoration, and denied Seek on an
already-open handle after READ and WRITE were revoked. The temporary file
was removed afterward. Existing cold mirror/sole-survivor tests passed with
zero cold writes, unchanged backing hashes and zdb no leaks. Logs:
`build-aros/dos-write-only-verified-link.log` and
`build-aros/dos-write-only-verified-qemu.log`.

## Explicit DOS exclusive locks (2026-09-20)

Lock(EXCLUSIVE_LOCK) now creates an exclusive reference, preserving that
mode in fl_Access. Existing handles or locks reject it; existing exclusive
locks reject further opens/shared or exclusive locks, duplication and new
ParentDir references to the same object. Metadata through the original lock
remains usable. Exclusivity is per object, not recursive over descendants.
UnLock releases the exclusion. Native/ZFS authorization remains per packet.
An exclusive lock is not itself a disk write and works on readonly mounts.

`build-aros/qemu-run.jFKayqDm/` passed all four boots with a strict exclusive
lock marker in each. Tests checked existing shared-lock and open-handle
conflicts, actual fl_Access, Examine through the exclusive lock, both new
lock modes, DupLock/Open rejection, restored access after release, and
ParentDir conflict for a child of an exclusively locked directory. Cold
mirror and either member alone retained zero writes/unchanged hashes; zdb
reported no leaks. Logs: `build-aros/dos-lock-link.log` and
`build-aros/dos-lock-qemu.log`. OpenFromLock and filehandle-based lock and
metadata operations remain the next separate implementation work.

## DOS filehandle and lock interoperability (2026-09-20)

OpenFromLock now transfers a file lock to a handle only after validation,
authorization and slot allocation succeed. Shared/exclusive state is retained,
the initial position is zero, and directory/error cases leave the lock alive.
DupLockFromFH returns an independent shared lock, rejecting exclusive handles.
ParentOfFH shares ParentDir's authorization and parent-exclusivity checks.
ExamineFH reports metadata through the real object with the existing legacy
FIB bounds/owner/date handling. Every operation captures current sender
credentials; holding a handle does not preserve access after logout.

Initial `build-aros/qemu-run.cHxT1ut0/` passed the new operation checks but
failed the exact dataset-I/O counter assertion: the test added one seven-byte
Read without updating its expected totals. The counter gate remains exact,
now nine reads and 37 + 5*fixture-size bytes above the pre-client baseline.
The failed run is retained in `build-aros/dos-fh-lock-qemu.log`.

Expanded `build-aros/qemu-run.qpPRj11f/` passed all four strict boots, including
shared and exclusive conversion, contents/metadata, surviving duplicate lock
after Close, exclusivity after conversion, directory failure retaining its
lock, and ExamineFH/DupLockFromFH/ParentOfFH denial after logout. Cold mirror
and either member alone had zero block writes, unchanged backing hashes and
zdb no leaks. Logs: `build-aros/dos-fh-lock-verified-link.log` and
`build-aros/dos-fh-lock-verified-qemu.log`. Management service link is checked
separately; no hosted deployment or UI completeness is implied. Slot-exhaustion
failure injection, ChangeMode, DOS64 packets and general namespace recovery
remain further work.

## DOS ChangeMode (2026-09-20)

ACTION_CHANGE_MODE accepts native CHANGE_LOCK and CHANGE_FH arguments; the
latter carries a public FileHandle BPTR, unlike ordinary handle packets.
Upgrades reject any other handle/lock on the same object and leave the old
mode unchanged. Downgrades release exclusivity, repeated changes are valid,
and both forms reauthorize the current sender. A mode change itself does
not mutate disk contents and is permitted on readonly mounts.

Initial `build-aros/qemu-run.RFNNJ8Ln/` failed the new ChangeMode test. The
public API's mode argument is ULONG, so negative lock constants arrive
zero-extended in the 64-bit packet. The handler now normalizes a valid signed
or unsigned 32-bit representation, rejecting invalid modes/high bits. It does
not compare a zero-extended value against a sign-extended constant.

`build-aros/qemu-run.2V1B0qHa/` passed operation checks through the first
survivor, but that guest's final RESULT line was interrupted by security
task-exit logging. This run is incomplete evidence; the strict gate is not
relaxed. The harness retains its logs and enforces its existing deadline.

After that timeout completed, unchanged binaries passed the full repeat in
`build-aros/qemu-run.CTllGycH/`: four strict boots, exact dataset counters,
zero cold block writes, unchanged backing hashes and zdb no leaks. Both lock
and FH upgrade/downgrade paths, duplicate-reference rejection, idempotence,
invalid mode preserving exclusivity and logout denial were exercised through
actual dos.library calls. Logs: `build-aros/dos-change-mode-verified-link.log`,
`build-aros/dos-change-mode-repeat-qemu.log`. Management service link also
passed in `build-aros/dos-change-mode-service-link.log`; no deployment claim.

## Native DOS64 sparse files (2026-09-20)

On 64-bit AROS the handler now implements CHANGE_FILE_POSITION64,
GET_FILE_POSITION64, CHANGE_FILE_SIZE64 and GET_FILE_SIZE64 using ordinary
SIPTR packet fields, following the native PFS3 contract. Queries return the
64-bit value or -1; changes return boolean. A 32-bit DosPacket64 overlay is
not used, and these actions are not advertised as implemented on 32-bit
targets. Relative arithmetic handles INT64_MIN without signed negation
overflow and rejects positions/sizes outside the signed 64-bit range.
Classic Seek refuses unrepresentable old/new positions rather than wrapping
its LONG result or changing position on error. Reads/writes use the full
handle position. Resize still invokes actual upstream zfs_setattr/fsync.

`build-aros/qemu-run.ZV7ol9iA/` passed all four boots with a sparse file of
4 GiB + 4096 bytes on the two 256 MiB hardfiles. Actual DOS packets created
and queried the size, moved beyond 4 GiB, and wrote a marker across that
boundary. Live and cold mirror/either-survivor imports checked marker bytes,
zero-filled surrounding holes, final sparse byte/EOF, position queries,
INT64_MIN/out-of-range seeks, size overflow, classic LONG rejection and
legacy FIB size rejection. Failed operations preserved position/size.
Readonly resize failed rather than issuing writes.

Expanded `build-aros/qemu-run.vdUJnN3N/` also passed all four strict boots,
adding logout denial of all four DOS64 operations through a held handle,
including their distinct numeric/boolean failure results. Both runs retained
zero cold block writes, unchanged backing hashes and zdb no leaks. Logs:
`build-aros/dos-sparse64-verified-link.log` and
`build-aros/dos-sparse64-verified-qemu.log`. Management service link was checked
separately in `build-aros/dos-sparse64-service-link.log`. This is not a claim
of large-file GUI metadata support, 32-bit packet compatibility, quota fault
injection or crash recovery.

## DOS SetOwner and native object IDs (2026-09-20)

ACTION_SET_OWNER now uses the same packet shape as native dos.library,
checks native property permission on the current owner and ROOT_UID or
UID_MATCH on the requested owner, and invokes upstream zfs_setattr with
the actual caller credential. Path admission requests WRITE_OWNER, not
WRITE_ATTRIBUTES or an incidental native READ. Volume readonly policy,
exclusive references and upstream ACL/group restrictions still apply.
Successful changes synchronize before reply.

Native nobody UID zero now has an explicit object-only mapping to POSIX
UID 65535; it never maps to root and never authenticates a sender. Root is
native 65535/POSIX zero. IDs 1..65534 remain unchanged and IDs outside the
native range still fail. See CREDENTIALS.md for the effect on imported
numeric UID 65535 and the distinction between metadata and credentials.
Twenty-five root/nobody/ordinary/high-ID round trips are checked alongside
the existing nobody-sender rejection and foreign-ID overflow tests.

`build-aros/qemu-run.Va7nUzL4/` passed all four strict boots. Real DOS callers
tested root transfers through nobody, root, 65534 and reader77; reader77's
allowed supplementary-group99 change; denied giveaway, root transfer,
unrelated group66 and another user's file; missing-file and readonly errors.
Final UID77/GID99 and file bytes were verified live and after cold mirror
and both sole-member imports. Cold boots issued zero block writes; backing
hashes were unchanged and zdb reported no leaks. Logs:
`build-aros/dos-setowner-link.log`, `build-aros/dos-setowner-qemu.log`.
Native security volume discovery/quarantine, delegated ownership policy and
failure-injected persistence remain separate unfinished work.

## Persistent DOS comments (2026-09-20)

ACTION_SET_COMMENT stores native comment bytes in the upstream named
attribute `system.aros.comment`; no private filesystem or sidecar database
is used. Absent and zero-length values represent an empty comment. The
classic API accepts at most 79 bytes, matching AFS and the 80-byte FIB's
BCPL length-plus-data capacity. Its input is a BSTR, not a C pointer.
Examine/ExamineFH/ExNext use the shared FIB builder, which copies length plus
bytes and lets dos.library perform its normal BCPL-to-C conversion. Reads
are bounded and malformed embedded-NUL or oversized stored data is rejected,
not silently truncated. These malformed-storage paths still need direct
fault-injection tests.

Mutation requires native property/volume permission plus upstream attribute
and named-attribute authorization using the actual caller; successful changes
sync before reply. The internal comment getter is metadata-only and called
after the DOS object's metadata admission, like native protection decoding.

`build-aros/qemu-run.Ea0VyPVb/` passed four strict boots. Real DOS clients
tested creation/replacement, 79-byte round trip, clearing, rejected 80-byte
comment preserving the previous value, missing-file error, non-owner denial,
owner update and readonly rejection. Examine and ExamineFH reported the
expected strings. Two final comments persisted across cold mirror and each
sole-member import. Zero cold writes, unchanged backing hashes and zdb no
leaks were retained. Logs: `build-aros/dos-comment-link.log` and
`build-aros/dos-comment-qemu.log`; service linking is separate from hosted
deployment or UI validation.

### Public DOS ExAll fallback acceptance (2026-09-20)

The native dos.library emulates ExAll via Examine/ExNext when the handler
returns ACTION_NOT_KNOWN. Actual guest clients now cover ED_NAME through
ED_OWNER, complete and deliberately single-entry buffers, cursor continuation,
duplicate/missing-entry rejection, terminal NO_MORE_ENTRIES with a populated
buffer, BUFFER_OVERFLOW followed by retry, and case-insensitive parsed pattern
filtering. Tests run on both diagnostic and authenticated readonly mounts.
`build-aros/qemu-run.9WNd7sUj/` passed all four boots and integrity gates;
logs are `dos-exall-verified-link.log` and `dos-exall-verified-qemu.log` under
build-aros. A misleading-indentation warning was subsequently removed without
changing logic. Earlier wider-buffer run: `qemu-run.PmkIKOFg`.

This is fallback coverage, not a native ACTION_EXAMINE_ALL implementation.
Classic FIB limits still prevent enumeration of long names and large files;
native ExAll, matching hooks and authorization changes between calls require
further coverage. No DUT or user hosted session was used.

### Native ExAll enumeration (2026-09-20)

ACTION_EXAMINE_ALL now reads upstream directory entries directly rather than
constructing a classic FileInfoBlock. It supports all seven metadata levels,
aligned packed records, persisted comments and mapped ownership, readonly
protection reporting, continuation, pattern filtering and the native DOS hook
ABI (entry as object, type pointer as message). ACTION_EXAMINE_ALL_END resets
the stateless index cursor. Each request checks directory list/search and
returned-object metadata permission; rejected authentication clears the entry
count, including after logout. Unsupported metadata widths fail explicitly:
classic ExAllData32 sizes above UINT32_MAX are not truncated. ED_NAME does not
require size or native-owner conversion. Directory mutation during traversal
is not snapshot-consistent, and hook execution needs dedicated runtime tests.

`build-aros/qemu-run.6KVlvAwQ/` passed all four strict boots, existing seven-level
continuation/filter/overflow tests, new logout stale-count checks, and direct
native packets enumerating a 120-character filename that Examine rejects.
The long-name test also checks explicit enumeration teardown. Zero cold writes,
unchanged disk hashes and zdb no leaks remain required. Logs:
`dos-native-exall-v4-link.log` and `dos-native-exall-v4-qemu.log` in build-aros.

Failure history: initial compile rejected missing BPTR-to-SIPTR casts in test
packets (dos-native-exall-link.log); fixed without ABI changes. Runs ptcjqy72
and BfKVVbeF rejected enumeration of the mixed root fixture: the latter's
diagnostic proved ERROR_READ_PROTECTED at key 6 after returning one entry.
The long-name test now uses the reader-owned directory; authorization was
not weakened. Those failed run artifacts are retained. Service linking is a
separate check, not hosted deployment or full UI verification.

### ExAll hook ABI and boundary acceptance (2026-09-20)

Real AROS_UFH3 hook execution is now verified in the guest, receiving the
ExAllData pointer as object and LONG detail level as message, with correctly
populated ownership/comments. Accept-one, reject-all and accept-all runs use
single-entry buffers and check each source entry is offered exactly once,
correct accepted counts, list termination and explicit ExAllEnd reset. Guard
bytes beyond the declared buffer capacity remain untouched. Direct packets
with invalid levels (zero, negative, too large), negative size, misalignment,
null buffer or null control return BAD_NUMBER without advancing the cursor;
valid controls have their stale entry count cleared on failure.

`build-aros/qemu-run.5ontMv1w/` passed all four strict boots, authenticated and
diagnostic clients, both survivor imports and all backing-integrity gates.
Logs: dos-exall-hooks-verified-link.log and dos-exall-hooks-verified-qemu.log.
Earlier run Pate9Uto also passed before adding buffer guards and making the
AROS erased hook-pointer cast explicit (removing its compiler warning).
Only the two previously documented upstream warnings remain. These tests
close the earlier hook execution gap, not directory mutation consistency,
large-file metadata ABI coverage or the remaining filesystem feature gaps.

### DOS hard-link creation and persistence (2026-09-20)

ACTION_MAKE_LINK/LINK_HARD now resolves the destination through native path
policy, validates the source lock against this mount, checks source metadata
and property ownership plus destination native write permission, then calls
the actual upstream zfs_link with the caller credential. Upstream ACL and
hard-link policy remain mandatory. The bridge retains/releases both znodes
and syncs before success. Directories and foreign-mount locks are rejected;
soft-link creation remains unsupported, not silently converted to hard links.

`build-aros/qemu-run.jVSO6QDJ/` passed four strict boots. Native MakeLink tests
create an alias, reject an existing destination, prove SameLock identity,
write through the alias and read the original, remove the original and retain
alias content, reject directory/foreign links and a reader linking a root
object, and reject readonly creation. Surviving alias bytes persist through
cold mirror and both single-member imports. Zero cold writes, unchanged disk
hashes and zdb no leaks passed. Logs: dos-hardlink-verified-link.log and
dos-hardlink-verified-qemu.log. Initial J3ZiNBZX also passed; test formatting
warnings were removed and explicit hardlink markers added to the harness.

This is not complete alias-aware DOS semantics. Existing locks and handles
reconstruct names/parents from the object's primary z_parent and first matching
directory entry; with multiple names this is ambiguous. Next required work is
retaining the path's actual parent/name in locks and handles and preserving it
through duplication, conversion and rename. Cross-directory alias ParentDir,
NameFromLock and ExamineFH must be tested before claiming full hard-link
support. No private-format fallback or DUT/user hosted access was used.

### Alias-aware DOS lock and handle identity (2026-09-20)

Locks and open handles now retain the actual path's parent object and leaf
name. File lookup/open no longer selects an arbitrary name via primary
z_parent. Directory names remain canonical because directory hard links are
not supported. DupLock, DupLockFromFH and OpenFromLock preserve the location;
ExamineFH uses it and ParentDir/ParentOfFH authorize and return the stored
parent. Rename updates only references matching object, old parent and old
name, including held file handles, leaving another alias of the same object
unchanged. Empty relative references retain the supplied lock's location.

`build-aros/qemu-run.sikiOFZG/` passes all four strict boots. Tests hold two
same-named aliases in different directories, check names and parents through
native DOS metadata APIs and NameFromLock, duplicate/convert a lock to a file
handle, move only that alias with references held, and verify the original
and other alias remain unchanged. Cold imports verify both surviving names
and SameLock object identity after original-name removal. Existing relative
paths, rename, conversion, ACL and exact read-count tests still pass. All
backing hash/zero-cold-write/zdb integrity gates pass. Logs:
dos-alias-verified-link.log and dos-alias-verified-qemu.log in build-aros.
Earlier d99PIsWO passed before the expanded cold metadata checks.

This closes the ambiguous-name/parent gap identified above for the tested
native paths. Conservative deletion still rejects objects with any held
references; open-unlink semantics, directory-enumeration mutation consistency,
soft links, notifications and the remaining port work are not claimed done.

### Native DOS symbolic links (2026-09-20)

The ZPL metadata bridge now represents symlinks and creates/reads them with
actual upstream zfs_symlink/zfs_readlink, caller credentials, creator native
protection and synchronous persistence. DOS MakeLink LINK_SOFT uses this
bridge. Traversal/Open/Lock report ERROR_IS_SOFT_LINK, and ACTION_READ_LINK
expands the first link with the original relative prefix and remaining path
suffix, matching the native DOS/SFS contract. DOS owns recursive resolution
and its loop limit. Absolute targets replace the prefix. Small readlink
buffers return -2 without copying partial output; ordinary failure is -1,
including authentication/detachment failure. Metadata reports ST_SOFTLINK.
MODE_NEWFILE follows a final symlink before replacing its target, preserving
the link. DeleteFile removes the link itself, not its target.

`build-aros/qemu-run.NYTE0SOA/` passed all four strict boots: relative file
links, parent-relative targets, directory links with a remaining component,
Lock of a directory link, dangling targets, recursive-loop rejection,
ReadLink sizing and absolute expansion, repeated output through an absolute
link, deleting/recreating a link without losing target data, and ExAll type
reporting. All links/content persist on cold mirror and each sole member;
readonly creation fails. Zero cold writes, disk hashes and zdb gates pass.
Logs: dos-symlink-verified-link.log and dos-symlink-verified-qemu.log.

Initial compilation showed AROS has no EREMOTE constant; an explicit internal
OZ_ZPL_SOFTLINK traversal result replaces that assumption (initial log retained).
Earlier vxwdXHcf passed before the output/type/deletion extensions. Service
linking is separate from hosted/UI validation. Permission transitions through
links, cross-volume targets, extreme-length expansion and every metadata
operation through an intermediate link still need dedicated acceptance.

### Symlink target authorization (2026-09-20)

`build-aros/qemu-run.VRYFACvI/` passes all four strict boots with a public
symlink pointing to a root-owned, native-read-protected file. Authenticated
reader77 can obtain the link destination with ReadLink but cannot Open or
Lock its target. In the write boot, root temporarily grants target read access,
reader77 opens through the link and reads the expected bytes, then a nested
root login revokes target permission. Returning to reader77, Read on that
already-open handle fails with READ_PROTECTED and leaves its byte sentinel
unchanged; Close still works. Both nested logout identities are checked.
The denied state survives cold mirror and each single-member import.

No handler changes were needed. Native packet checks remain layered with
upstream permissions, rather than treating link readability or an earlier
successful Open as permanent permission. All prior tests, zero cold writes,
unchanged backing hashes and zdb gates passed. Logs:
dos-symlink-auth-link.log and dos-symlink-auth-qemu.log. This verifies the
specific read/lock/revocation path, not all delegation/volume policy or every
operation through a link. DUT and user hosted sessions were untouched.

### Cross-volume DOS routing and separator joins (2026-09-20)

The isolated DOS patch in `aros/patches/dos-fs-open-port.patch` fixes reuse of
the previous filesystem's fh_Type during softlink recursion. RWWmsPqm passes
four boots with actual RAM-file Open/Read/Close, SameLock comparisons and
700-byte ReadLink expansion (exact capacity rejection, sentinel preservation,
and overlapping input/output). See `aros/patches/README.md` for build evidence.
These tests require the explicit boot-module overlay; the stock release ISO
has not been changed and still has the reproduced cross-volume failure.

Next regression ULQ1XsZs failed stage 6 before the handler fix: joining a
target ending in ':' or '/' blindly introduced incorrect separators. AROS
uses '/' for parent traversal, so those are semantic changes, not cosmetic.
ACTION_READ_LINK now removes one trailing target slash before a suffix, and
omits the suffix's separator after a device colon. Tests check exact expanded
strings and real Lock/SameLock for RAM-root and trailing-slash directory links.

35VHcOsh passes all four strict boots with this change and the DOS overlay,
including cold mirror and both sole-member imports, zero cold writes,
unchanged backing hashes, and the existing allocation/integrity gates.
Retained logs: dos-symlink-separator-negative-{link,qemu}.log and
dos-symlink-separator-{link,qemu}.log. This does not establish every metadata
operation through intermediate links or complete volume security integration.

### Orderly writable reopen and cold writer import (2026-09-21)

Negative run 25kOn34a reproduced ENOTSUP (native errno 86) when reopening the
mountlife dataset writable after a clean close. The old bridge rejected any
allocated ZIL block, including upstream's normal retained empty first block.
Admission now tests the upstream ZIL_REPLAY_NEEDED flag; such logs still fail
closed. For a clean log, the writer uses the same zil_destroy(keep_first=TRUE)
branch as upstream zil_replay, after zil_open. No pending log records are
intentionally discarded and full crash replay remains unimplemented.

XwGMR46N passed the existing four boots with repeated in-process writer opens.
Expanded run vkJEpNv3 passed five boots: create, cold readonly mirror, each
sole survivor, then cold writable import using separate QCOW2 overlays.
The fifth boot changes native protection from 0x81234567 to 0x81234568,
closes/reopens and verifies it, restores the original word, verifies through
a readonly reopen and exports cleanly. Both virtual disks recorded 6152192
written bytes; the readonly tests still recorded zero writes and original
member hashes remained unchanged. Linux zdb -bcc reports No leaks on both
the original pool and converted writer overlays. This proves orderly reopen,
not power-loss durability: the writer blockstats show zero flush_operations,
so the device flush path must be audited before crash-recovery claims.

The normal harness now requires OpenZFSQEMUReopenTest (build with
link-engine-test.sh aros/engine/qemu-reopen-test.c), retains all five boots,
and checks the written copy with zdb when available. Retained logs:
writer-reopen-negative-{link,qemu}.log, writer-reopen-{link,qemu}.log,
writer-cold-reopen-link-v2.log and writer-cold-reopen-qemu.log. The first cold
test compile missed spa_impl.h; its failed log is retained. Native service
relink passed in writer-reopen-service-link.log; no hosted UI runtime claim.

Flush audit after that run: `aros/engine/device-io.c:oz_device_flush` correctly
issues CMD_UPDATE; lib/libzpool/kernel.c:zfs_file_fsync dispatches to it.
However, the guest release source
AROS-amiwest-nightly-src/rom/devs/ata/ata.c:cmd_Update (line 342) is explicitly
a no-op with a comment saying cache flush is future work. ATA_FLUSH_CACHE and
ATA_FLUSH_CACHE_EXT constants exist, but no command use was found in that
driver. This explains the zero QEMU flush count. Do not treat successful
CMD_UPDATE as verified stable-storage completion on this driver. Next work:
isolated ATA flush integration/boot verification, then dirty-log recovery
tests. No shared ATA source or binary was changed by this audit.

### Native intent-log write recovery and failure preservation (2026-09-21)

New test-qemu-crash.sh uses only private raw disk copies from a validated
five-boot run. Like upstream ztest, the guest anchors a log block, syncs a
baseline write, freezes pool TXG persistence, then writes different bytes
and commits the real ZIL. At CRASH READY the harness records virtual disk
flush counters and SIGKILLs that exact owned QEMU PID: no guest close/export.
Recovery operates on another copy; baseline and raw crash images are retained.

Initial MxcxjvPD proved a real pending log: the next kernel imported the pool,
reported ZIL_REPLAY_NEEDED=1, and writer admission returned ENOTSUP (86).
Full upstream zfs_replay.c is now linked into normal native builds, not only
the link audit. Writable mount setup drains old unlinked objects before
calling the upstream replay vector and exposes the mount only after success.

Added zil_replay_checked alongside the unchanged legacy API behavior. The
checked path propagates parser/callback errors, syncs successful replay
sequence updates and retains the log instead of destroying it on failure.
This is important while individual native namespace replay operations remain
incomplete. It is not a replacement reimplementation of ZIL parsing/replay.

XSjFyd7I first passed committed-write recovery. Expanded Yin9dJou additionally
injects a callback returning EIO through the actual parser, verifies the
error and pending log survive, then successfully recovers with the native
upstream vector. A third cold boot verifies the committed bytes again and
ZIL_REPLAY_NEEDED=0. Linux zdb -bcc reports No leaks. Original crash-image and
baseline hashes remain unchanged. This covers TX_WRITE recovery, first-record
callback failure/retry, and persistence after clean recovery/export; it does
not prove every record type, partial-prefix failure, torn logs, devices losing
acknowledged flushes, encryption or power loss during replay.

Broader qFpcoUr7 five-boot regression passes with the new replay-enabled
binaries: authenticated DOS operations, readonly mirror/single-member reads
with zero writes, cold writable reopen/export, real ATA flush counters,
unchanged original member hashes and both zdb integrity gates. Native service
relink also passed; hosted UI runtime is not claimed. Logs:
crash-fixture-{link,qemu}.log, crash-replay-{link,qemu}.log,
crash-replay-checked-{link,qemu}.log, replay-regression-{link,qemu}.log,
crash-replay-service-link.log. Enabling the upstream replay unit also exposes
five existing uint8_t/char pointer-sign warnings, in addition to the retained
UIO signedness and unused ACL-helper warnings; no new warnings were hidden.

## Namespace and metadata intent-log recovery (2026-09-21)

Expanded the isolated power-cut test beyond TX_WRITE: create/mkdir, hardlink
(including a surviving alias), rename, symlink, deletion of pre-existing files
and directories, resize with zero extension, owner/group, mtime and AROS comment.
The upstream replay vector remains authoritative. Native ACL entry points now
admit internal replay only with kcred; the public ACL-check API still rejects
replay state. Native setattr accepts replay's ATTR_CTIME only internally, not
as a caller-settable timestamp. Admission regression checks cover both limits.

Preserved failures and corrections:

- crash-run.VTjFNTz0: namespace replay rejected by native ACL replay guard.
- crash-run.BPRupcJ7: initial namespace test passed after internal ACL fix.
- crash-run.YezuwXKk: stronger test failed TX_SETATTR (type 11, sequence 0x2e)
  with ENOTSUP. The recovered screen proves ATTR_CTIME admission was the
  primary failure. Cleanup then masked it with EBUSY and triggered a dataset
  eviction assertion. That failed-mount cleanup bug is NOT fixed by successful
  replay and still needs a dedicated partial-replay failure/retry test.
- crash-run.Pk14BuEI and s8Qhu2GD: writable recovery succeeded after CTIME
  admission, but owner/mtime assertions failed. The fixture had never synced
  its final metadata changes; added an explicit oz_zpl_sync before power loss.
  This corrects the durability precondition, not the expected recovered data.
- crash-run.EuhpXuUA: all three boots passed, including injected callback
  failure preserving the log, full real-vector recovery, and a fresh cold
  verification of every namespace/metadata assertion with replay-needed clear.
  zdb -bcc reports No leaks; original and crash-source disk hashes unchanged.

Logs live under build-aros/crash-namespace-{negative,acl,metadata,ctime,detail,
sync}-{link,qemu}.log; each run retains source/binary provenance and disks.
The harness now captures a failure screenshot/blockstats before cleanup when
the owned VM remains alive. The five-boot ACL regression qemu-run.Pa0Ca0Dq
passed; the subsequent CTIME/admission five-boot regression qemu-run.FujpMccA
also passed, including the new caller/replay rejection checks, authenticated
DOS operations, zero-write readonly/single-member boots, cold writable reopen,
unchanged original hashes and zdb integrity checks. Its build/run logs are
namespace-ctime-regression-{link,qemu}.log.
These results do not establish torn-log handling, power loss during replay,
all record types, physical durability, or completion of the full port/UI.

## Failed partial-replay cleanup and same-kernel retry (2026-09-21)

The diagnostic now linker-wraps zil_replay_checked only in its own executable
to replace TX_SETATTR with EIO while leaving earlier real replay callbacks
intact. No injection control is linked into the production service. Negative
crash-run.5HtZ1C9r reproduced EBUSY masking EIO followed by dataset eviction
assertion. The source of the busy cache was replayed deletions: unlinked nodes
still had SA handles, although no external references remained.

Failed admission now uses unpublished-cache teardown after ZIL users drain.
It releases private cache/SA handles without deleting on-disk objects or
draining their persistent unlinked queue. It does not discard a borrowed,
mapped, directory-locked, or asynchronous node. Normal published unmount keeps
its previous stricter dirty-atime/unlinked checks. Original admission errors
are preserved. If an unexpected borrowed reference still blocks cleanup,
the adapter retains the owned objset rather than freeing it.

Owner-local retry was added on 2026-10-04 (`40246cdc`). Before mounting another
dataset, the original owner retries its retained failed admissions. A still
borrowed mount blocks the new admission; after the loan drains, cleanup can
release the objset and normal admission proceeds. The wrapper is retained in
pthread-local state allocated before admission, not a global task-address
registry. Other tasks cannot reclaim it. Thread teardown attempts cleanup but
never forcibly frees borrowed state: a loan that outlives its owner remains a
lifecycle defect, with retention and warning, not a solved cross-owner recovery
case.

Private diagnostic `AROS_TEST_FAILED_MOUNT=1` linker-wraps root znode lookup
to return EIO while retaining a real loan. Negative `qemu-run.VdknPEes`
returned EIO then EBUSY even after the loan drained; its test-only saved mount
pointer successfully cleaned up. Fixed `qemu-run.ySkBhCKW` passes admission
EIO, still-loaned EBUSY, foreign-owner EBUSY, and original-owner successful
reopen after release. Its guest write phase passed, but the host runner exited
127 before cold verification; the script was edited while executing, so this
is not whole-run acceptance. `9419c28b` freezes the runner text at startup;
`qemu-run.LsxkMJXA` then passed its new create/write checks but used an older
cold importer missing the required foreign-owner-close assertion, so the host
gate rejected it (exit 1). All three executables were rebuilt for
`qemu-run.fpp1hYtd`, which remains in progress. `d84fd909` freezes and hashes
binary copies before boot, not just the runner text. `75410f0f` additionally
clears empty retry TLS records after successful admission. Normal shared
package `shared-engine.QZw0qYOW` / `native-bundle.Bv8tMsFl` separately passed
`native-import-run.3NjCgi2B` (exit 0): current non-injected service, library
lifetime, CLI/error reporting, received-property expansion, vdev/zvol/repath,
DOS pressure, reimports and unchanged backing hashes. This deliberately injected
failure is separate from the zero-reference replay tests below.

crash-run.AUWjFrYH passes same-kernel retry and cold validation. Stronger
crash-run.3bpnWtiU additionally proves nonzero persisted replay sequence,
deletion of the pre-existing file, and a nonempty persistent unlinked queue
after the injected failure. Retry recovers all namespace/metadata assertions,
and the third cold boot confirms them with no replay-needed flag. Both runs
pass zdb integrity and original/crash-source hash guards. Logs:
crash-partial-{negative,cleanup,prefix}-{link,qemu}.log.

Five-boot regression qemu-run.rV3q9y2V passed, including added cache tests:
unpublished abort refuses live references and discards simulated dirty/unlinked
cache state without changing disk metadata. Readonly boots still write zero
bytes. Cold writer, both single-member reads, hashes, and zdb checks pass.
Logs: partial-cleanup-regression-{link,qemu}.log. The subsequent small common
admission-failure helper refactor was rebuilt and exercised by 3bpnWtiU.
Cold restart between failed partial replay and retry remains the next test;
this section does not claim that test or general I/O-fault recovery complete.

### Follow-up: cold restart after partial replay failure

`AROS_TEST_PARTIAL_REBOOT=1` selects four private boots: stage and hard cut;
real prefix replay with injected SETATTR failure and hard cut before export;
fresh-kernel resume; then another cold verification. The normal three-boot
mode remains available for same-kernel retry. The four-boot path preserves
separate copies of the partial-replay disks as well as the initial crash pair.

crash-run.6VXyPI0n passed all four boots: EIO returned with prefix sequence and
unlinked queue retained, resumed mount starts with replay-needed set, every
namespace/metadata assertion passes, and final cold verification has that flag
clear. Both writing cut phases have real ATA flush counters on both members.
Original, crash-source and partial-source hash guards pass; recovered zdb -bcc
reports No leaks. Logs: crash-partial-cold-{link,qemu}.log. The native management
service also relinks without test-only wrappers (partial-cleanup-service-link.log).
No hosted UI runtime or hardware durability result is implied by this relink.
This supersedes the preceding cold-restart test gap, not the remaining general
I/O-fault, torn-log, other-record-type, or complete management/UI work.
# Native default-quota transactions and read-only export

## DOS identity negative-case expansion

`default-quota-run.R0QH56bc` extends the real packet test to three independent
cases: file generation, file type, and parent-directory generation changes.
Each case rejects Read, Examine and ParentDir through retained references,
restores the original real SA metadata, and verifies that Read/Examine still
reject those references. Close/UnLock remain usable and a fresh open reads
the original byte. All mutations stay in disposable RAM-file pools, through
test-owner operations; the DOS endpoint remains read-only. The runner now
requires the three-case marker. All other integrated regressions pass.

This closes the targeted parent/type gaps noted below, not the staged-stream
negative-case gap or actual online rollback/receive lifecycle integration.

## DOS handle and lock identity

Native stat now exposes the real ZPL generation. DOS file handles and locks
retain object generation/type and parent generation at creation (including
lock-to-handle conversion); rename updates saved parent identity. Normal
packet lookups validate these identities before using the saved object number.
Once validation fails the reference is marked stale; this conservative policy
also invalidates it when stat cannot establish identity. Close, free-lock and
free-record retain registry-only cleanup so stale references can be released.
Deferred record-lock admission validates the handle again before granting it.

Direct send/receive stream reads and writes also validate the saved handle.
Staged output finish checks the payload, private staging directory and final
parent identities before publication/removal. On stale identity it returns
ESTALE and releases the handle without deleting by stale object numbers;
private staging data is left for recovery rather than guessing its ownership.

`default-quota-run.TFmIMgIz` exercises real DOS packets through the explicit
read-only fixture endpoint: open file and lock, change actual SA generation
under exclusive test ownership, invalidate the old cached node, reject old
Read/Examine with ERROR_INVALID_LOCK, allow Close/UnLock, and read through a
fresh open. The generation is restored afterward. Existing full-engine
regressions pass. `public-service-run.SUwOQHNL` separately passes rebuilt
authenticated service/session and mirrored DOS storage regressions.

Failed `default-quota-run.lBEaVqNP` correctly rejected a writable mount through
the read-only fixture helper. `default-quota-run.ahpI8RTV` failed initial client
admission after switching to the authenticated endpoint: this small fixture
does not configure security.library. The final test uses a read-only endpoint;
SA changes are internal test operations, not unauthenticated DOS writes. No
production admission checks were relaxed. Targeted changed-parent/type and
staged-stream rejection tests remain to add; actual online rollback/receive
still requires complete lifecycle orchestration and namespace notification.

## Native cached-node detach and reload

Native `zfs_znode_dmu_fini` and `zfs_rezget` now provide the upstream lifecycle
semantics without a fabricated Linux inode or BSD vnode. The caller must hold
teardown writer and exclude native operations, with dirty atime and active
namespace/mapping work drained. Detach releases the SA handle but preserves
node address, references, locks and list membership. Reload invalidates cached
ACL/xattrs, loads real SA metadata into private candidate state, checks object
generation and file type, then transfers the handle and refreshed metadata.
A changed identity is rejected with EIO and no attached handle. A received
unlinked node remains detached, matching upstream behavior. Existing native
`zfs_verify_zp` therefore rejects operations through a doomed node.

`default-quota-run.KTaatjGd` verifies detach/reload with two retained references,
stale cached metadata/xattr removal, real on-disk SA generation/type rejection,
unlinked detachment, restoration, close and reopen. The fixture changes and
restores identity fields only in its disposable RAM-file pool. All existing
ownership, version, quota, send and snapshot detach regressions also pass.
The rebuilt service passes lifecycle and mirrored DOS storage regression in
`public-service-run.D2ESOD0o`.
Initial compiler logs `node-resume-engine.log` and
`node-resume-engine-retry.log` retain an obsolete TXG_WAIT constant in the new
fixture; this checkout requires DMU_TX_WAIT, corrected in the final build.

This is not yet actual online rollback/receive: mount suspend/resume/end still
must orchestrate ZIL, property callbacks, SA/objset publication and DOS state.
In particular, current DOS handles/locks retain object numbers without a
generation, so enabling online replacement before adding handle identity
validation could redirect an old handle to a reused object. That integration
must precede exposing mounted rollback/receive through the dispatcher.

## Mount-private reference lifetime

Mount reference counts now use `z_native_ref_lock`, initialized and destroyed
with the native mount (including temporary ioctl holds). Lookup/publication
still takes the objset user-pointer mutex first, then this mount-private lock.
Borrower release and held-state queries take only the mount-private lock and
do not dereference `z_os`. This is required because suspend/resume can replace
or evict the objset while a dispatcher caller retains the mount reference.
It does not implement suspend/resume or permit arbitrary unsynchronized
publication changes; callers must still serialize close and transitions.

The focused native fixture `cli-platform.NLV7B4CU` passes 64 publication cycles
and 16 worker cycles, now removing publication and setting `z_os` to NULL
before each retained worker releases its reference, then restoring publication
and closing normally. This models detached-reference lifetime only, not an
actual rollback. Full-engine `default-quota-run.WyHG5CXn` also passes ownership,
temporary holds, version upgrades, quotas, send identities and DOS snapshot
detach with all native ZPL objects rebuilt against the changed mount layout.
The rebuilt production service passes `public-service-run.FjakpNNg`, including
authenticated session lifecycle and mirrored DOS write/read/read-only import.

## Native dataset ownership for dispatcher lifecycle

The production mount path now acquires its objset using the native `zfsvfs`
as the actual upstream dataset ownership tag. Previously the outer `oz_zpl`
adapter owned it, which did not satisfy mounted rollback/receive's ownership
contract. Ownership is acquired once and retained through admission, use and
teardown; there is no disown/re-own gap. Final close disowns only after caches,
ZIL users, callbacks and publication references have drained. Borrowed-objset
fixture entry points retain their existing caller-owned lifetime contract.

The outer adapter no longer caches a second objset, SA table or root pointer;
it obtains the current native mount state for each serialized operation.
Suspend/resume/end remain unimplemented: this change establishes ownership,
not safe concurrent lifecycle transitions or completed rollback/receive.

`default-quota-run.iGlMQ7uQ` verifies owner equality and long holds across four
writable/read-only phases and a snapshot mount, duplicate-open rejection,
borrower-blocked close preserving ownership, close releasing ownership and
publication, and two failed legacy admissions releasing their owner. Existing
version upgrade, data readback, send, quota and snapshot tests also pass.
`public-service-run.vIDkPjzq` verifies the rebuilt production service's session
lifecycles and authenticated mirrored DOS write/read/read-only reimport.

Failed run `default-quota-run.0gPKOY0B` is retained. Its new verification called
`dmu_objset_get_user` without the required user-pointer mutex and aborted. The
test now holds that mutex; the runner also recognizes the native SIGABRT
marker instead of waiting for its timeout. No production assertion was removed.

## Send-progress caller identity

Native sessions now own distinct retained caller identities. These are opaque
internal objects, not Linux processes, native task-address IDs, user-selected
PIDs or credentials. Thread-local scopes are explicit, nested/LIFO, and not
inherited by workers. Thread exit releases abandoned scopes. A send captures
and retains its current caller when inserting its actual upstream progress
record, then releases it after removing that record. No scope means no caller
match, not a fallback to libzpool's shared p0 placeholder. Non-AROS builds keep
their existing curproc behavior.

`oz_session_cleanup_caller` returns a borrowed identity only while its caller
retains a session reference. The pending ioctl transport must authenticate the
ticket, retain the session for the whole operation, and enter its caller scope;
this helper does not authenticate a request or implement that transport. A
retained identity alone does not keep the engine or session cleanup alive.

`default-quota-run.0t6jwO6a` passes 16 workers with no inherited scope,
explicit cross-thread use, nested identity switching, wrong-thread/out-of-order
leave rejection, and abandoned-scope thread exit. It also observes the real
upstream send progress list during a complete send and an output-error send:
the owner matches, the second identity does not, and both terminal paths remove
the progress record. Credential, legacy upgrade/data readback, temporary hold,
snapshot DOS detach and quota regressions remain green. This is not yet an
end-to-end CLI FD/progress ioctl test or forced-client-death send test.

`ioctl-probe.4MvkSl35` removes the caller-predicate declaration failure; five
dispatcher compile errors remain (suspend/resume/end and module lifecycle).

The rebuilt production service passes `public-service-run.akM6FL4I`, including
authenticated session ownership, task-exit reclamation, mirrored DOS writes
and read-only reimport. The rebuilt focused session fixture also passes with
the CLI platform regressions in `cli-platform.v7WQ6YPV`. These remain isolated
QEMU tests; neither result proves a linked upstream CLI or complete dispatcher.

## Upstream snapshot unmount bridge

The native `sys/zfs_ctldir.h` exposes only the supported snapshot-unmount
interface, not Linux inode/fid/superblock interfaces. `zfsctl_snapshot_unmount`
uses management's actual dataset registry and DOS detach/close path. It must
run on the owning service task; another task gets EDEADLK before registry
access. Invalid names/flags fail, an unmounted snapshot succeeds, and a busy
DOS endpoint returns EBUSY even with MNT_FORCE. Native client locks/handles
cannot be forcibly invalidated. This matches the upstream caller's best-effort
contract: it ignores the unmount error and the subsequent destroy must still
handle a mounted dataset. The bridge is internal, not an authentication API.

`default-quota-run.Mniyv35z` mounts a real snapshot through management, starts
a native DOS client holding a lock, verifies forced unmount refuses it, releases
the lock while pumping actual packets, then detaches and reopens the snapshot.
It also tests wrong-task rejection, invalid flags/non-snapshot name, and
already-unmounted success. Version/temporary-hold/credential/quota phases pass.
The fixture now includes real management and retains detached DOS endpoints
until its private QEMU terminates, just like the resident service; it does not
pretend that endpoint reclamation or clean shutdown after mounts is finished.

`ioctl-probe.myMcyfWg` eliminates the Linux fid_t/header and MNT_FORCE errors.
The complete dispatcher and CLI still need their remaining adapters and link
integration. Automatic `.zfs/snapshot` traversal, force-invalidated handles and
end-to-end CLI snapshot destruction are not established by this bridge test.

`production-snapshot-unmount.log` and `public-service-run.wSMZ4mrt` record the
full service/Manager rebuild and passing public-service/session/task-exit and
mirror/DOS regressions. Six dispatcher declaration errors remain, covering
suspend/resume/end, send-progress caller identity and module lifecycle.

## Native dispatcher privilege checks

`secpolicy_sys_config` and `secpolicy_zinject` use the port's authenticated
effective-root credential policy. Anonymous, ordinary-user, real-UID-zero and
group-zero identities do not gain pool-configuration/fault-injection authority.
System configuration denial returns EPERM; fault-injection denial returns
EACCES, matching the upstream Linux policy's distinction. `checkonly` never
acquires or modifies privilege. Dataset delegation does not grant these
global privileges. The native adapter has no Linux capability/namespace model.

`is_system_labeled` reports false: MuFS/security.library discretionary ownership
is not Solaris Trusted Extensions MLS. The real upstream mlslabel guard thus
rejects unsupported label changes rather than claiming an implemented policy.

`default-quota-run.5ut98UMP` passes the native credential matrix (both checkonly
values, kernel/effective root, real-root/group-zero and anonymous identities),
existing threaded credential snapshot/TLS tests, and the version/hold/quota
storage regression sequence. The unit test calls internal credential adapters;
it does not expose caller-selected identities as an authentication interface
or claim that the still-unlinked CLI ioctl endpoint is verified.
`ioctl-probe.Scobwah9` removes these three declaration errors; eight remaining
platform-adapter errors still prevent full dispatcher compilation.

`production-dispatcher-policy.log` and `public-service-run.Oeg8jzhr` record
the full service/Manager rebuild and passing public-service/session ownership,
task-exit reclamation and mirror/DOS lifecycle regressions.

## Native filesystem-version upgrades

### Pre-SA mount admission (2026-09-29)

The dataset mount entry point no longer rejects every version other than SA.
It validates the version against the pool, passes SA object zero for pre-SA
datasets as upstream does, and registers the upstream upgrade callback when
SA is available. It does not create SA metadata or upgrade a legacy dataset
merely by mounting it.

The version regression now opens and closes a genuinely created version-4
filesystem twice before upgrading it. Each read-only mount checks the root
directory; after closing, the test verifies version 4, absent SA master entry,
and released objset ownership. The unchanged implementation failed that test
with ENOTSUP in `default-quota-run.hfqOptgR`. The rebuilt implementation passes
in `default-quota-run.XofSh1o1`, including the subsequent version upgrade,
post-upgrade file write/readback and the runner's quota/lifecycle regressions.
Both evidence sets are registered under the development test archive. These are isolated native
QEMU tests with private runtime overlays and disposable in-guest fixtures,
not evidence of cold-disk durability, all legacy ACL layouts, versions 1-3,
or pre-upgrade writable file operations in that first run.

`default-quota-run.UAghjC2T` subsequently passes creation, write and sync on
version 4, exact readback through two read-only remounts, and preservation of
that original file across the version-5 upgrade. Version and absent SA-master
checks still run before upgrading. This extends file-I/O coverage but remains
an in-guest remount test, not cold-reboot or power-loss durability evidence.

`default-quota-run.EC5Z6Xka` extends the regression to genuinely created
filesystem versions 1, 2, 3 and 4. Each creates and writes a file, syncs,
closes and reopens read-only, checks exact contents, and verifies unchanged
version, no SA master entry, and no remaining objset owner. The native runner
requires the explicit `RESULT native legacy versions PASS` marker as well as
its existing quota/lifecycle results. This covers basic file I/O across those
formats; non-root legacy ACL combinations and cold recovery remain separate
qualification work.

`qemu-run.VCRcjgEx` adds separate-boot evidence for versions 1-4 using two
fresh disposable hardfiles with MBR ZFS partitions. `legacy-cold-test.c`
creates each dataset through upstream filesystem creation and writes a
version-specific payload. Separate healthy read-only, survivor-0,
survivor-1 and writable-import boots check exact bytes, the original version,
absent SA master metadata and released ownership. The mirror runner requires
the legacy marker in all five phases, verifies zero writes in the read-only
phases, unchanged raw backing hashes and unchanged outside-partition extents,
and performs its existing zdb checks. This is orderly export/cold import,
not abrupt-power-loss recovery, nor a test of legacy non-root ACL variants.

The first whole-disk attempt, `qemu-run.vupMms2s`, failed before the legacy
test: AROS had published AFS claims on the blank disks and pool creation
correctly returned EBUSY. The successful test uses the existing partition
fixture; no production ownership guard was weakened. Both runs are registered
under the development test archive. Build attempts are retained in
`build-aros/legacy-cold-*-build*.log`: missing native ZPL compilation flags,
an inappropriate full-dispatcher configuration for these disk diagnostics,
and the shared helper's missing creation-adapter link were corrected before
boot testing. The full-dispatcher production service separately passed
`native-import-run.6mwE5nPX` with the pre-SA mount fix.

`qemu-run.DAzKic55` extends all five phases with non-root legacy ACL checks.
The fixture applies upstream modes with `zfs_setattr` separately from native
AROS protection xattrs: public file 0644 and private file 0600. A uid-77,
gid-88 credential reads the public file but cannot read the private file;
denied reads leave the complete sentinel buffer unchanged. The writable
phase rejects a real non-root write with EACCES, zero bytes written and
unchanged contents. Cold read-only mounts reject write access with EROFS,
as upstream's dataset check precedes ACL evaluation. All four legacy
versions pass, including both single-survivor boots; backing hashes,
outside-partition extents, zero-write checks and zdb checks pass too.
This does not cover arbitrary named/inherited ACE combinations or prove
all native DOS packet protection behavior for every legacy version.

Preserved failures: `qemu-run.ihEnY32r` and `qemu-run.tqNryr32` exposed a
test assumption, not an ACL defect: passing Unix modes to the AROS-protection
API does not set the upstream ACL. `qemu-run.l8AaFHef` then aborted because
the new fixture helper called `dmu_objset_get_user` without its required
lock. The helper now locks that lookup and releases its temporary objset
hold before assigning transactions; its enclosing single-threaded owner
keeps the mount alive. The runner now also detects the signal-6 abort line
instead of waiting for timeout. All failures and the passing run are in the historical test archive.

`crash-run.MVlINxWf` exercises version-4 legacy ZIL replay, not just orderly
export. The crash diagnostic selects the real pre-SA dataset from the cold
fixture, checks its version on every boot, freezes TXG persistence, commits
new bytes and namespace/metadata changes, and waits for the harness to kill
only that owned QEMU process. Recovery injects rejected replay callbacks and
a partial SETATTR failure, verifies the retry log remains, then mounts with
normal upstream replay and verifies content, links, deletions, owner, time,
size and comment. A third boot verifies the recovered state and cleared
replay-needed flag. The runner exits zero, keeps the pre-recovery crash
images and baseline unchanged, checks outside-partition extents, and reports
zdb no leaks. This is virtual power-cut testing with intentional TXG freeze,
not proof of physical controller/cache durability or all replay record forms.

The crash runner accepts an explicit binary, legacy version 1-4, POSIX and
partition-library overlays; it freezes the executable and optional libraries
in the evidence set and hashes them. The default non-legacy diagnostic is
preserved. Partitioned fixtures are extracted for the Linux zdb inspection
without changing the AROS-tested images.

The repeated-interruption matrix also passes for every legacy version:
`crash-run.Hd5XOhfN` (1), `crash-run.jUwkNzSC` (2),
`crash-run.eB1nI57i` (3), and `crash-run.WYp8QH5F` (4).
Each uses four separate QEMU boots: commit and hard cut; deliberately fail
partway through replay and hard cut again without export; resume recovery;
then cold verification with replay-needed cleared. Required namespace and
byte checks, preserved baseline/crash/partial hashes, outside-partition
checks and zdb no-leak validation all pass. All four sets are in the development test archive.
This strengthens retry-after-interruption evidence but is still a selected
fault sequence, not exhaustive concurrency, I/O-failure or durability proof.

`set-version.c` adapts upstream `zfs_set_version` with real master-node version
transactions, SA-master creation/registration, history logging and cached
FUID/SA capability updates. It rejects out-of-range versions, downgrades,
versions unsupported by the pool, snapshots and read-only pools. The caller
still authorizes the operation and holds teardown writer access; it is not a
standalone public command or replacement ioctl transport.

`default-quota-run.CIakPleI` created a version-4 filesystem on a version-15
pool using the native upstream filesystem-creation adapter (not by falsifying
the version of SA objects). It proved version-5 upgrade refusal on that pool,
upgraded the pool, upgraded the filesystem, checked the version and SA ZAP
object, reopened the temporary state and mounted the legacy root successfully.
`default-quota-run.aoWj66R1` extended that test with native file creation,
write/sync, unmount/remount and exact data readback under the upgraded root.
Both runs also passed the temporary-hold and default-quota regression phases.
The same-version update after reopening also exercises idempotence.

`production-version-upgrade.log` and `public-service-run.x263n4wJ` record
the full service/Manager rebuild and passing public-service session,
task-exit reclamation and mirror/DOS lifecycle regressions.

`ioctl-probe.ybjdCiUn` no longer reports a missing `zfs_set_version`; other
dispatcher adapters still fail compilation. Versions 1-3, interrupted upgrade,
all ACL/legacy-object variations and cold-reboot durability remain unverified.
The dedicated quota runner now also requires the filesystem-version marker.

## Temporary upstream ioctl filesystem holds

`temporary-hold.c` implements `zfsvfs_create_hold`/`zfsvfs_free` for the real
upstream dispatcher's unmounted-filesystem path. Success retains the objset
hold and pool-config read lock, initializes native metadata/FUID/teardown
state and upstream SA tables, and does not publish a DOS mount, open a ZIL,
or take ownership of an already owned dataset. The caller drops the config
lock before acquiring teardown and reacquires it before objset release, as
`zfs_ioctl.c` specifies. Free touches only private state after objset release.
Temporary holds do not use the native mount znode cache; a nonempty cache or
published reference count is an invariant violation, not silently discarded.

The shared metadata reader now reuses an already-held DSL config lock: the
upstream lock is explicitly nonrecursive. Ordinary mounts still acquire and
release their own lock. No temporary hold is substituted for normal DOS mount
initialization/replay/publication.

`default-quota-run.gzmeEKzV` passes 16 temporary-hold/update/release cycles,
null/missing/wrong-type/missing-ZPL-metadata failures, then real native
mount/remount quota persistence and read-only import/export. Tests assert the
config-lock handoff and absence of mount publication. `TPneNghK` was the
earlier successful cycle test before adding wrong-type/metadata failures.
`ioctl-probe.0EaVz3rc` no longer reports the create_hold/free/z_use_hold errors;
the complete dispatcher still fails on other native adapters. Concurrent
temporary holds, encryption/key transitions and legacy-version upgrades are
not proved by these tests.

`production-temporary-hold.log` and `public-service-run.WXIyin6r` record the
full service/Manager rebuild and passing public-service lifecycle, session,
task-exit reclamation and mirror/DOS regression after the metadata-lock change.

`default-quota.c` adapts upstream `zfs_set_default_quota`: the six user/group/
project byte/object defaults are stored in the master-node ZAP with real DMU
transactions. Zero removes the key (including an already absent key); cached
mount values change only on success. Unknown properties are rejected before
opening a transaction. Administrative quota changes remain permitted on a
read-only filesystem mounted from a writable pool, matching upstream; snapshots
and read-only pools reject writes. The caller must authorize the operation and
hold teardown writer protection. This is not a public IPC endpoint.

The full-engine `OpenZFSDefaultQuotaTest` creates an exclusive pair of 128-MiB
RAM files in an isolated, marked QEMU guest. It uses native mount publication,
upstream references and teardown locking to set six distinct values above
32 bits, unmount/remount, verify both cached and on-disk values, clear them
twice, remount again, then export/reimport read-only and verify rejected writes
leave both representations unchanged. It ends with an ordinary pool export.
This does not prove quota enforcement under load, CLI transport, delegation,
snapshot rejection, power-loss durability or a cold guest reboot.

Evidence / retained hypotheses:

- `default-quota-run.xRe9xV9U`: initial direct-objset metadata fixture passed;
  superseded by actual mounted-filesystem testing.
- `default-quota-mounted-build.log`: stronger fixture initially failed to
  compile because its mount-reference declarations were missing; corrected by
  including the real `zfs_ioctl_impl.h`, not local guessed prototypes.
- `default-quota-run.6rtDppEa`: actual native mount/remount phases passed.
- `default-quota-run.rB6yyKHv`: read-only write rejection passed, but final
  export returned EBUSY. Upstream export drained deferred objset/dbuf eviction
  only under `spa_sync_on`, which is false for this read-only pool.
- `default-quota-run.cr4P1phb`: explicit eviction drain in the fixture made
  export pass, supporting the pending-reference hypothesis.
- `default-quota-run.T93yhf7q`: the drain was moved into `spa_export_common`
  before its reference-count check and the fixture workaround removed. All
  four phases passed with normal export. No force, sleep or retry was added.
- `ioctl-probe.UgcjYrzd`: the quota-setter declaration error is gone; other
  native dispatcher adapters still prevent compilation. No full CLI claim.
- `production-default-quota.log` and `public-service-run.G6eWTjJJ`: full
  service/Manager rebuild and public-service regression pass: three service
  lifecycles, authenticated session ownership/capacity/stale-handle tests,
  task-exit reclamation, and mirror DOS write/read/read-only-reimport roundtrip.

Build via `aros/engine/link-engine-test.sh aros/zpl/default-quota-test.c` using
the consistent private SDK/pthread/engine variables, then run
`aros/zpl/test-qemu-default-quota.sh <binary>` with the same private DOS, ATA
and POSIX-library overlays as the other native fixtures. The runner records
sources, input hashes and serial output and owns/terminates only its QEMU PID.

## Native mounted rollback lifecycle

`zfs_suspend_fs` now requires the native mount owner task, retains dataset
ownership and mount references, drains writes, detaches cached SA handles,
unregisters callbacks and clears the old objset publication while holding the
teardown writer. `zfs_resume_fs` reacquires the current objset from the retained
dataset, rebuilds metadata/SA/policy/ZIL state, reloads surviving znodes and
leaves removed or changed identities invalid. A setup failure makes the mount
close-only. This relies on owner-serialized DOS dispatch; it is not permission
for workers to suspend a mount concurrently with raw DOS packet operations.

`default-quota-run.RGqjPgK5` passes the complete integrated fixture, including
two real upstream `dsl_dataset_rollback` calls on a mounted disposable dataset.
Each restores original bytes, removes a post-snapshot file, retains a valid
existing znode and invalidates the removed znode. Wrong-task suspend is rejected,
close while suspended is busy, and a failed rollback is followed by successful
resume/readback. Existing quota, identity, ownership and snapshot tests pass.
The earlier `PYru5R4p` reached all results but the final pass line was interrupted
by a security.library task log, causing the strict runner to time out; its raw
log is retained and the runner was not weakened to accept it.

`ioctl-probe.5oPm1tBo` removes the suspend/resume declaration failures. The real
dispatcher still needs `zfs_end_fs`, `zfs_init` and `zfs_fini`, then linking and
actual client transport. Online receive, redacted receive/end-filesystem,
concurrent clients, resume setup failures and power-loss behavior remain
unverified; this is not completion of the upstream CLI or full port.

The full service/Manager rebuild is retained in `production-native-rollback.log`.
`public-service-run.mxCVGCeI` passes public lifecycle, session ownership and
task-exit cleanup, plus authenticated RAM-backed mirror DOS write/read and
read-only reimport checks with that rebuilt service.

## Native global ZPL accounting lifecycle

The native management engine now calls `zfs_init` after `kernel_init` and
`zfs_fini` after `kernel_fini`. Mount caches and DOS ports remain native,
mount-owned resources; the global hooks install/remove the actual upstream
`zpl_get_file_info` DMU callback. Removal follows pool/taskq shutdown so the
callback cannot disappear during final accounting writes. Startup still uses
an empty pool cache and explicit imports. These hooks must not be combined
with a second `spa_init` via unchanged `zfs_kmod_init`.

Previously the callback was absent: storing quota properties did not establish
working DMU user/group usage accounting. `default-quota-run.YicZcb95` proves
callback dispatch and accounting enablement. The stronger complete run
`default-quota-run.LWkmsZoG` also queries real upstream user/group usage after
export and read-only reimport and requires nonzero persisted byte counts.
Its marker is mandatory in the runner. This does not yet prove quota-limit
enforcement, project accounting, every legacy SA layout or crash durability.

The initial `native-zpl-init.log` build failed because the common upstream
VFS header hides native declarations outside kernel context. The shared native
`lifecycle.h` resolves that without changing management's ABI/build context;
`native-zpl-init-retry.log` and `native-zpl-accounting.log` retain successful
full-engine builds. `ioctl-probe.XWE1gxTw` now reports only the missing
`zfs_end_fs` declaration; dispatcher linking/transport remain unimplemented.

`production-zpl-accounting.log` records the full service/Manager rebuild;
`public-service-run.YrAStzIw` passes three start/stop lifecycles, session and
task-exit cleanup, and the authenticated mirror/DOS/read-only roundtrip with
accounting registration enabled. No DUT or shared hosted session was used.

## Ending a suspended native filesystem

`zfs_end_fs` reacquires the current owned objset after a matching owner-task
suspend, marks the mount permanently unmounted, keeps cached nodes detached,
and releases teardown writer protection. It does not reopen SA/ZIL state or
resume a namespace that upstream online receive has deliberately ended.
Ownership and publication references remain until safe native close. The DOS
frontend rejects further I/O with DEVICE_NOT_MOUNTED but still accepts handle,
lock, record and notification cleanup. The service pump retries detach/close
as those references drain; busy references are never forcibly freed, and late
packet endpoints remain resident under the existing retirement policy.

`default-quota-run.xmFnEkFk` passes a real DOS open-file/lock transition: wrong
task and unmatched end calls fail, native nodes stay invalid, Read/Examine are
rejected after end, Close/UnLock succeed, retained native refs prevent early
disown, and the owner pump finally releases storage. Fresh native open/read
then succeeds. Rollback, accounting and all prior integrated checks pass too.
This exercises the terminal lifecycle directly, not a redacted receive stream;
that upstream receive integration and public ioctl transport remain work.

`ioctl-probe.HUhj6WVF` compiles both complete upstream `zfs_ioctl.c` and
`zfs_onexit.c`. This is compile evidence only. Link inventory still includes
native device/private-state, event, zvol and filesystem-creation integration.

`production-end-fs.log` and `public-service-run.NWmjvpYA` retain the rebuilt
service/Manager and passing public lifecycle/session/mirror DOS regression.

## Upstream filesystem creation callback

The native creation adapter now exports the actual `zfs_create_fs` signature
needed by upstream ioctl create. It preserves validated ZPL property nvlists,
requested older versions, normalization, case mode and UTF-8-only metadata,
and applies upstream case-fold flags when creating the root directory ZAP.
The existing authorized management create wrapper calls the same implementation
with default properties; it retains its name, permission and credential-scope
checks. This low-level syncing callback is not an unauthenticated public API.

`default-quota-run.oefBGE8G` passes three property cases with ZPL 3/4,
sensitive/mixed/insensitive mode and normalization/UTF-8 flags. It reacquires
stored metadata and reads the new root's actual micro-ZAP normalization flags,
then passes the complete rollback/end/accounting regression. Creation does
not imply native normalized namespace traversal: such mounts remain rejected
until that implementation is complete.

Retained failed hypothesis: `default-quota-run.BERLAkZS` aborted in root ACL
creation. Symbol/relocation mapping identified `zfs_acl_ids_create`'s VERIFY;
the direct fixture had not entered a credential scope, so DMU captured NULL.
The fixture now uses the same scoped credential propagation as management.
No ACL checks were bypassed and no credential fallback was added to production.

`production-create-properties.log` and `public-service-run.ONTyOAIk` record
the full service/Manager rebuild and passing public lifecycle/session/mirror
DOS regression with the shared creation callback.
