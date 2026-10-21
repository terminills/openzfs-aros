# AROS OpenZFS development bundle

## Writable file-backing flush admission (2026-10-04)

Commit `50622f48` requires a successful flush when opening an AROS writable
regular-file vdev. Unsupported POSIX flush errors become EIO rather than
allowing the upstream no-write-cache fallback. Native-device and zvol paths
retain their own flush contracts; read-only file imports do not require this
writable admission check.

Production shared engine `bu3lKO8n`, packaged with the actual Manager in
`native-bundle.APOiUfO6`, passed two isolated QEMU checks:

- `file-backing-run.PFHXQBVc`: native pool sync, abrupt guest termination
  without file close or pool export, fresh boot, exact 60-byte DOS payload,
  and independent `zdb` verification. This uses the private flush-capable FAT
  handler from `flush-overlay.ee67h4mi`.
- `file-backing-run.K6e00Ji1`: base FAT rejects pool creation with EIO, leaves
  the pool absent, and leaves both 64 MiB member files byte-identical to the
  original blank fixture. Test support is committed as `195232f2`.
- `file-backing-run.PaFZZQfx`: the flush-capable writer is terminated after
  pool sync, then a fresh read-only importer uses base FAT without the handler
  overlay. Exact payload, unchanged member bytes, and independent `zdb` pass.
  The phase-specific runtime test is committed as `e8b9b83b`.

The same engine in validation-harness bundle `sQ2BWd4h` passes the normal
16-vCPU/8192-MiB native regression `native-import-run.5uK90JQB` with host exit
zero and unchanged original backing hashes. This covers configured CLI/DOS,
vdev/zvol, library lifetime, received properties, relocation, standalone
handler read/write/flush, 320-file cache pressure, and read-only reimport.
It uses the DOS/POSIX/PIPE/stdio runtime profile and is not a GUI-click test.

Actual-Manager run `native-import-run.WEIudEYl` separately passes with bundle
`APOiUfO6`: the executed Manager is byte-identical to the package, displays
OpenZFS 2.4.4 available, and answers an actual List pools click with zero pools
after fixture exports. `flush-manager-01.png` and `flush-manager-02-pools.png`
retain those screens. Normal window close, host exit zero, configured native
regressions and unchanged original backing hashes pass. This interaction does
not imply visual verification of every Manager operation.

The preceding base-FAT tests `wjPeqe2M` and `myrpUhQr` acknowledged pool sync
but recovered a zero-length file. Adding a mounted filesystem flush in
`RJfhV12k` did not repair that failure; the FAT-only control `n15SmZXG` did.
These failures remain retained evidence, not superseded success claims.
Logs and manifests are registered under the development test archive on the external artifact
drive. This does not qualify physical power loss or arbitrary stock-nightly
file backing. A handler acknowledging flush is a necessary contract, not
proof that every storage device honors it.

Post-admission failures remain a distinct durability gap. Private diagnostic
`qemu-run.dOcQTTMH` records `injected=EIO hits=2 parent=0 recovery=0`: after
real pool creation, both leaf flushes failed but their parent reported success.
This exercises upstream `zio_flush`, whose `DONT_PROPAGATE` flag and ZIL
comments explicitly describe the suppression. Label/uberblock paths also
discard flush wait results; removing one flag is not a complete fix. The
admission change above must not be presented as solving later flush failures.
This is an injected software observation, not a physical failure test. The
serial-counter replay completed all five boots with host exit zero, including
cold read-only survivor access and writable reopen. That is successful defect
reproduction and subsequent fixture recovery, not a durability fix.

`3d0200ee` adds a disabled-by-default strict-flush experiment in private
standalone engines. It checks label/uberblock flush results as well as parent
propagation. Shared-library packaging rejects its explicit diagnostic symbol.
Its qualification is separate from the accepted release candidate above.
Private `qemu-run.UccrZVDe` completed the five-boot mirror, read-only survivor,
and writable-reopen suite with the strict experiment enabled, host exit zero,
and unchanged original backing hashes. The runner required the exact injected
two-leaf EIO/parent EIO/recovery-zero observation. That run did not exercise
an actual TXG or ZIL commit failing during its flush. The shared-library
packager independently refused this engine with exit 2 and the explicit
unqualified-strict-flush message (`release-exclusion-after-archive.log`).

The subsequent `qemu-run.DwR498M4` completed all five boots with real TXG
fault injection: suspension occurred before the target transaction synced,
resume succeeded after removing the fault, and the exact recovered DMU
payload passed four fresh imports (both members, each member individually,
and writable COW reopen). Original member hashes remained unchanged. This
closes the tested TXG recovery case, not general durability qualification.

Commit `d6c593cb` adds an opt-in synchronous ZPL write/fsync fault probe.
The create phase of `qemu-run.NmJQuQTF` reports EIO, six injected failures,
pool suspension, successful resume and same-mount retry, and exact immediate
payload. Its complete five-boot suite subsequently exited zero.
Commit `caa5ba0c` additionally requires four cold filesystem payload reads.
`qemu-run.AeUZbi08` passed that complete suite with exit zero: both-member
import, each member alone and writable COW reopen all report exact eight-byte
ZPL payload, alongside the DMU payload checks. Original member hashes remain
unchanged. Neither test claims replay of an uncommitted intent log.
The strict experiment remains excluded from release packaging pending the
remaining durability and redundancy qualification.

Strict-engine `crash-run.vffJH29m` separately completed intent-log replay
qualification with exit zero. TXG syncing was frozen after establishing the
log anchor; synchronous payload and namespace changes were committed to the
log, then the owned VM was forcibly terminated without cleanup. A fresh boot
injected a partial replay failure and was terminated again after persisting
that partial state. Resume and a final verify boot recovered the payload and
namespace metadata; the final boot required no replay. Preserved crash,
partial-replay and baseline image hashes passed, and the runner's independent
zdb check passed. This is software crash/replay evidence, not physical power
loss. Build dependency repair and external artifact placement are committed
as `9cfe1cd0` and `b423f9a0`; the initial missing-symbol build log is retained.

Before production enablement, qualify active-leaf versus absent-leaf failure
policy across redundant topologies and auxiliary devices. The experiment
currently propagates auxiliary cache/spare flush failures through the same
label/uberblock root: this needs explicit testing and policy review, not an
assumption that every auxiliary failure should suspend the data pool.

Persistent single-member failure is separately exercised by `34c6c36d`:
one real file handle continues returning EIO while its mirror peer performs
successful real flushes. `qemu-run.17rb078h` completed with exit zero,
including real TXG/ZPL suspension and recovery, four exact cold DMU/ZPL
payload checks and unchanged original member hashes. This qualifies the
experiment's conservative suspension behavior, not optimal degraded-write
availability. RAIDZ1 `native-raidz-run.28sL0E5s` passed the injected phases
but encountered subsequent ordinary ATA DMA write timeouts; no overall RAIDZ
pass is claimed. Its separate control `native-raidz-run.5P4nTfob` uses the
previously validated QEMU worker build and threaded disk I/O and completed
with exit zero and no ATA timeout logged. Both QEMU build and AIO differ,
so that does not isolate the timeout cause. Non-strict control `S2092imO`
and strict RAIDZ2 `LlOUPJeS` subsequently reached their deadlines after ZPL
fixture creation. No full pass is claimed for either. That next phase creates
320 files and performs three read sweeps; `1cf64f48` adds sparse progress
markers rather than treating silence as a proven hang.

Cold native RAIDZ1 `raidz-cold-run.r0rLAyzu` completed with exit zero: all
three members bound read-only, pool healthy, exact recovered DMU/ZPL bytes,
DOS handler tests, zero guest writes and unchanged original member hashes.
Preceding negatives remain retained: `r93WVA15` could not boot because QEMU
IDE rejected read-only frontends; `EGAiCjud` read both payloads but failed the
health gate because the private fallback only admitted ATA0/1. Commits
`be29358f` and `2113a676` use isolated COW disks with zero-write checks and
explicit read-only fixture bindings without broadening production access.

Non-strict trace `native-raidz-run.9hdz18dY` captured outstanding host QEMU
workers in Linux `jbd2_log_wait_commit` while the external ext4 journal was
in I/O wait. This identifies a host backing-storage delay in that observation,
not a generic AROS filesystem lock. Progress-marked control `FE9TPqmu`
completed all 320 creations and all three read sweeps, then reached its
600-second suite deadline during later tests. It is not a suite pass, but
its progress contradicts treating that entire interval as a guest hang.
`b9af26d5` adds a validated, recorded deadline override without weakening
checks or disabling durable flushes; subsequent long RAIDZ2 qualification
uses 3600 seconds. No host mount or write-cache settings were changed.

`432cd80b` extends the private cold RAIDZ diagnostic to an explicitly absent
member, retaining exact-capacity read-only bindings for every survivor. A
blank nonmember preserves ATA master/slave enumeration without contributing
pool data. `raidz-cold-run.4lFc1spr` passed with member 0 absent: degraded
health, exact recovered DMU/ZPL payloads, DOS access, zero guest writes, and
unchanged source member hashes. Other member positions remain separate tests;
this does not qualify arbitrary multi-device failures or auxiliary devices.
Subsequent `WfarKOQ8` and `eIz4CaoB` passed the same checks with members 1
and 2 absent respectively, completing every single-member omission for that
RAIDZ1 fixture. Strict RAIDZ2 `native-raidz-run.wWD4wTtF` completed the full
creation/DOS suite with exit zero under the recorded 3600-second limit.
`47572912` adds explicit missing-member masks, bounded by topology parity,
for separate fresh-import tests; those results must not be inferred from
the creation pass.
Cold RAIDZ2 `e2uNdfnB` passed healthy import; `LJDLPcrR`, `KeZC9f0w`
and `6d2fKYWp` passed missing masks 3, 5 and 6 respectively. Thus each
single surviving member of that three-member RAIDZ2 fixture recovered the
exact DMU/ZPL payload through the DOS handler, with degraded health, zero
guest writes and unchanged original member hashes. These are read-only
reconstruction results, not degraded-write or resilver qualification.

RAIDZ3 `native-raidz-run.HRsIMkJX` completed the strict-fault/native suite
using the recorded AHCI-capacity BSP. Fresh cold reads passed healthy
(`LgLT66GR`) and with three members absent: masks 7 (`hXkzyzLI`),
11 (`C8uM1HH4`), 13 (`djbIk7nv`) and 14 (`s8iSDgHr`). Each run verified
exact recovered TXG/ZPL payloads, DOS access, zero member writes and unchanged
fixture hashes. This covers every three-member omission of this four-member
fixture, not degraded-write/resilver or hardware qualification.

Actual cache-device fault run `qemu-run.KMT5Iead` exposed a separate private
strict-flush policy defect: two cache flush failures and eight successful data
flushes still suspended the data pool. Resume, cache removal and all five
mirror boots completed, but that does not make the suspension acceptable.
The follow-up private change retains cache flushing while suppressing cache
error propagation to the durable data tree. Data/log errors remain propagated;
spare policy remains unqualified. Release exclusion is unchanged. A fresh
runtime test must show a TXG advance while the cache fault is armed and no
suspension before claiming the cache isolation works.

Follow-up `qemu-run.gAuO1ZtT` failed after real ATA request timeouts before
cache injection (`failures=0`), so it did not judge that policy change. Its
host-thread and QEMU blockstats observations are retained; this snapshot did
not show the earlier host-journal worker wait. `qemu-run.yfQaRaYY`, using the
recorded private worker QEMU and explicit threaded AIO, passed all five boots:
three actual cache flush failures, eight successful healthy flushes, no
suspension, and a TXG advance while injection remained armed. Data-leaf TXG
and synchronous ZPL fault/recovery checks still passed, as did four cold
payload verifications and unchanged readonly source images. This qualifies
that private cache isolation case, not a fix for the preceding ATA timeouts
or release readiness. `93b6ee86` and `2772425b` add the distinct inactive-spare
diagnostic and verify its auxiliary object is outside the active data tree;
its outcome remains a separate runtime requirement.

Inactive-spare run `qemu-run.4095oZp1` reproduced the corresponding defect:
two injected spare flush failures, eight successful healthy flushes, and
pool suspension. Recovery and removal succeeded, but the diagnostic correctly
failed. The private rule now excludes auxiliary spare objects as well as
cache objects; it does not exclude `vdev_isspare` data-tree objects. Fresh
inactive-spare verification and active-spare fault coverage are still needed.

Corrected inactive-spare run `qemu-run.3FRMWdIh` completed all five boots
with exit zero: three injected spare flush failures, eight healthy flushes,
no suspension and TXG advancement under fault. Data-leaf TXG/ZPL error and
recovery checks, four cold payload verifications, readonly zero-write checks
and original image hashes passed. This does not qualify active spares.
`523e6754` adds a distinct upstream spare activation/direct flush/recovery/
detach diagnostic; its parent-error propagation must be tested separately.
Release packaging still rejects the current experimental engine, verified
in `strict-flush-experiment-20261004/release-exclusion-after-spare.log`.

Active-spare run `qemu-run.HMsDGHpd` completed all five boots with exit zero.
The actual replacement spare was verified in the data tree with no auxiliary
pointer; one injected flush failure reached its parent as EIO, a real flush
then succeeded and detach restored the original mirror. Inactive-spare error
isolation and all existing data-leaf TXG/ZPL/cold payload checks also passed.
This tests active-spare direct flush propagation, not active-spare TXG/crash
replay. `e386b3d9` adds separate dedicated-SLOG synchronous fallback and four
cold payload checks; those require their own runtime evidence.

Dedicated-log negative `qemu-run.nBLj4tC8` failed as intended: actual SLOG
flush errors reached ZIL, but the private strict TXG flush also required the
log device, suspending the pool and returning EIO despite healthy data flushes.
`8657ea8c` isolates log errors only in label/TXG flush parents, retaining
direct ZIL error propagation and its upstream durable-TXG fallback. A private
counter at the actual ZIL error-fallback branch prevents inferring fallback
from successful writes alone. Fresh run `qemu-run.DguVg6wr` has passed the
fault phase (two log failures, four healthy flushes, one fallback, no
suspension, exact warm payload); its full cold suite is still pending.
`e63bb0a9` adds combined SLOG/data flush faults, requiring failure rather than
false success, followed by recovery and explicit retry. Cold bytes in that
case prove the successful retry, not durability of the failed operation.

`qemu-run.DguVg6wr` subsequently completed all five boots with exit zero.
The actual SLOG failure took the observed upstream ZIL error fallback once;
the write succeeded without suspension and the exact eight-byte payload
survived four fresh import/reopen checks, including both single-member
survivors. Existing data-leaf TXG/ZPL fault checks and original source hashes
also passed. Normal (experiment-disabled) zio/zil/vdev_label objects compile
separately in `strict-flush-experiment-20261004/normal-core-check`; normal
zio/zil objects contain no private test symbols. These results do not qualify
the pending combined SLOG/data failure case or enable release packaging.

Combined-failure `qemu-run.VJlC3TJF` subsequently passed all five boots.
The synchronous operation returned EIO with three targeted log flush failures
and four data flush failures, one observed ZIL fallback and pool suspension.
After disarming the fault, resume, sync and explicit retry succeeded. Four
cold boots recovered the retry payload, and original image hashes remained
unchanged. This demonstrates refusal of false success when the data fallback
also fails; it does not assert durability of the failed operation. The latest
core is separately being rerun through abrupt termination and partial replay;
the earlier `crash-run.vffJH29m` predates these auxiliary/log policy changes.

Latest-core `crash-run.36rjbv76` completed with exit zero: forced termination
after synchronous log commitment, injected partial replay and a second forced
termination, then resume and final no-replay verification. Committed payload
and namespace metadata recovered; crash, partial and baseline image hashes
were preserved, and independent `zdb -bcc` checksum/leak traversal passed.
This is embedded-log crash coverage on the mirror, not dedicated-SLOG crash
coverage or physical drive power-loss proof. The crash runner now requires
the independent verifier before allocating a run rather than silently skipping
it when unavailable. Release packaging remains excluded.

`b6012f73` strengthens the active-spare check to a real transaction pinned to
the activated spare's file handle. `qemu-run.sOgYPoSv` passed all five boots:
the target TXG was 61 while the last synced TXG stayed at 60 during suspension,
two spare flush failures occurred alongside four healthy flushes, and resume
recovered the exact payload. Four cold imports recovered the separate
`activeflush` dataset after detaching the temporary spare; original mirror
hashes remained unchanged. `9eb17ca6` adds the separate dedicated-SLOG crash
fixture, including exact third-device admission, log-anchor device validation,
updated configuration capture, all-member crash/partial preservation and
independent zdb verification. Its runtime outcome is still pending.

Dedicated-SLOG `crash-run.EaRDuYpP` reached a validated log-device anchor and
committed namespace staging, but the harness stopped before its intentional
power cut: interleaved security task notifications made loose hex decoding
produce 49,536 bytes instead of the declared 49,532. This is retained as a
harness failure, not replay success. `6eb5cf73` replaces loose decoding with
a bounded single-block parser which removes only the known notification
format and rejects malformed hex, duplicate/truncated blocks and wrong sizes.
Its parser tests pass and the original capture redecodes to exactly 49,532
bytes without changing the retained malformed output. A fresh run is required
to qualify the resulting configuration import and dedicated-log replay.

The fresh strict run `crash-run.ZXhEureO` passed configuration decoding,
log-device anchoring and the deliberate power cut, but failed partial replay:
the expected SETATTR rejection was never reached (mount returned success).
Experiment-disabled control `crash-run.vMl8fOD2` reproduced that result with
fresh normal zio/zil/vdev-label objects and no strict-test symbols. Independent
read-only host zdb inspection of both preserved crash images finds just 14
records, through TX_LINK sequence 24; no later rename or SETATTR records.
The control's next referenced log block (vdev 1, offset 0x9000) reads as zeros.
This establishes reproduction without the private flush policy, not the cause
or a recovery pass. Private per-operation log snapshots are the next
discriminator; all namespace and partial-replay assertions remain intact.
The first diagnostic run `crash-run.CqSpOpGA` aborted in
`zil_kstat_values_update` from `log_snapshot`: per-dataset `zl_sums` is optional
and this fixture does not supply it. The diagnostic now reports `stats=0`
without dereferencing absent counters. This is a diagnostic failure distinct
from the already reproduced missing-record failure.

Corrected diagnostic `crash-run.0k93V8hA` reports sequence/committed values
24/24 after links, 25/25 after rename and 51/51 after final sync, with
restart/suspend/replay zero; partial recovery still fails. `stats=0` means
optional per-dataset counters are absent, not that errors were measured zero.
Selected-SLOG direct readback in `crash-run.He2Gc94g` finds a 4096-byte write
at partition offset 4231168 returning success but mismatching immediately,
before the deliberate crash; surrounding writes match. Repeat
`crash-run.zX0bBKZq` moves the mismatch to offset 4227072. Its aligned source
buffer remains unchanged across the write, while the readback hash equals
4096 zero bytes; later writes from that same buffer match. These failures
narrow the investigation to the write/transport path, without establishing a
driver cause. No retry workaround or replay assertion weakening is applied.

Private ATA completion-predicate run `crash-run.q1A94hm2` then completed the
full dedicated-SLOG stage, injected partial replay, resume and final no-replay
verification. It isolated stale ATA completion signals: command success had
previously been reported while DRQ and bus-master activity remained asserted.
AROS commits `814a3c93ff` and `18afe7992c` separately promote the command-aware
wait and real `CMD_UPDATE` drive-cache flush. The exact shared-source overlay
in `crash-run.po3sZj20` repeated the complete four-phase run: mirror members
recorded 26 flushes each, the SLOG recorded 33, all immediate readbacks
matched, preserved image hashes and outside-partition comparisons passed, and
independent `zdb -bcc` reported no leaks. These are isolated QEMU/KVM results,
not physical power-loss or DUT qualification.

## AROS flush error propagation release gate (2026-10-05)

AROS builds now retain active data/log leaf flush failures through
`zio_flush()` and wait for the flushes used by uberblock, label and
configuration sync. Auxiliary cache/spare failures remain isolated, and label
callers isolate separate log-vdev failures so direct ZIL flush errors can still
fall back to a durable TXG. Non-AROS builds retain the upstream behavior. The
`OZ_TEST_STRICT_FLUSH` symbol is still reserved for fault-injected engines and
still emits `oz_test_strict_flush_enabled`, so the release packager continues
to reject diagnostic binaries.

Fresh-source build `source-engine.SU2YLH4P`, normal shared engine
`shared-engine.4A9v5a41`, staging refusal matrix `stage-policy.lUeg4EP5` and
minimal bundle `native-bundle.9qsOcpw5` all passed. The bundle contains no
strict-flush diagnostic marker. Exact-bundle KVM run
`native-import-run.xSn26zjt` then passed release-service startup, writable
mirror import, DOS write plus flush, export, re-import and persisted readback,
followed by an independent read-only import; both original fixture members
remained hash-identical. This is software qualification of the normal AROS
package, not a physical power-loss guarantee.

Complete package candidate `native-bundle.tFrPPtJn` adds the normal filesystem
handler and zvol device to that accepted engine, service, CLI and Manager. The
handler and device are byte-identical to the artifacts exercised by
`native-import-run.5uK90JQB`; their recorded input hashes still match the
current handler, device and API sources. Staging rechecked the release-policy,
symbol, ELF/AROS and per-component hash gates and produced a deterministic
archive with SHA-256
`6510b35dcc3513c4682e5edaf814e414eb833cba7d65e4c227c534792b3e3a17`.
This composition proof does not turn the isolated QEMU results into a DUT or
physical-durability result.

## Real Manager and reduced runtime acceptance (2026-10-04)

`native-bundle.h0Gjs0Wd` contains the actual rebuilt native Manager, normal
shared engine `IHjB3u8i`, CLI `f8YUrNaj`, handler `7ZpHp1AH` and normal device
from `fresh-zvol-20261003`. Staging and negative policy tests passed. Its exact
package run `native-import-run.kVRdhJhQ` completed with host exit 0 at
16 virtual CPUs/8192 MiB, including configured CLI/DOS persistence, vdev/zvol,
received properties, relocation and library-lifetime checks. The actual
Manager reported OpenZFS 2.4.4 available and answered List pools with zero
pools after the fixture's exports. Normal close and original backing hashes
passed; screenshots and logs are registered to the development test archive on the external drive.

This run uses the selected local nightly ISO with DOS/POSIX/PIPE/stdio
replacements, without private ATA, partition or filesystem-package overlays.
The replaced base package has the same 26 modules in the same order as that
ISO; only dos.library differs. This is not arbitrary stock-nightly support or
a claim that a single DOS source patch suffices. The packaged archive does not
install these OS replacements. Headless-only handler/cache-pressure coverage
remains the separate `native-import-run.toIPqtFU` pass; the GUI invocation
does not cover that mode. Initial incompatible invocation `EjiXrruV` was
refused before boot and retained rather than weakening the test guard.

The immutable archive's documentation predates its runtime acceptance above.
Do not rewrite the accepted archive to update those historical documents.

## Suspended async configuration recovery (2026-10-03)

Production shared engine `shared-engine.uZ0vOvfz` and exact package
`native-bundle.nNoj0oRw` passed `native-import-run.P1UFD73I` with 16 virtual
CPUs and 8192 MiB (runner exit 0). Enabled CLI, handler, zvol, library-lifetime,
vdev/Manager guards and relocation checks passed; original backing hashes
remained unchanged. Library staging rejected diagnostic wrappers. Private OS
overlays and RAM CLI isolation for CD LoadSeg 333 remain prerequisites.
This does not establish live USB recovery or physical durability. External
log: `async-config-sixteencpu-qemu-20261003.log`; evidence registered the development test archive.

AROS async configuration updates now use the upstream interruptible TXG wait.
If the pool suspends after the worker acquires the namespace, the worker
releases that namespace and retains the configuration request for explicit
recovery. Suspended configuration work does not busy-retry; removal work
remains dispatchable. Synchronous configuration callers retain their original
completion contract. This is not a claim that all media-removal races work.

The private standalone diagnostic gates the actual async worker after
namespace acquisition, suspends the pool, and exercises the real TXG waiter.
Original-core negative control `qemu-run.uf4dpAv6` returns EBUSY (16); the
candidate passes the same probe. The wrappers are not production components.
The negative binary predates the candidate edits in its later-captured source
patch, so that patch is not an exact negative-binary source snapshot.

Candidate `qemu-run.qCr39ikT` passed the probe but reached the original
180-second runner deadline before its final result. Fresh run
`qemu-run.y2fUJ5b0`, with a 900-second phase deadline, passed the complete
create/DOS phase and all remaining cold-boot phases (runner exit 0): full
mirror import, each surviving member independently, and writable reopen on
COW disks. Original backing hashes stayed unchanged. The cold-import
and reopen executables are unchanged controls from `bound-mirror-20261003`,
not rebuilt candidate-core binaries. All artifacts are on the external
OpenZFS archive; no DUT or user hosted session was used.

## Import editor and combined regression (2026-10-03)

Final package `native-bundle.sffBqT2P` includes all three editor fixes below.
Its exact-package run `native-import-run.ffLyYF1F` passed with 16 virtual CPUs
and 8192 MiB memory (host exit 0), including three relocation reimports,
post-move DOS writes, authorization refusals and original backing hashes.
This extends configuration coverage; it is not arbitrary-concurrency proof.
External logs: `import-validation-bundle-stage-20261003.log` and
`import-validation-sixteencpu-qemu-20261003.log`. Evidence is registered.

Native Manager now offers read-only (default) or read/write import in the
backing-path editor. Reopening preserves the selected access/recovery modes;
clearing the layout restores read-only. Editing paths or access mode retains
an existing expected pool GUID. Malformed nonempty options are rejected before
opening the editor instead of silently losing an identity restriction.

Native QEMU GUI evidence: `native-gui-run.30UJ8hPq` verifies mode/default/reset,
`native-gui-run.3B7s4caB` verifies GUID preservation through mode/path changes,
and `native-gui-run.jYlOG6a3` verifies malformed-option refusal and correction.
These are preview-only tests without a service; all three exited normally and
both backing disks stayed byte-identical. Screenshots and input traces are
registered to the development test archive. Source commits: `5b82265`, `28512a6`, `8b16352`.

Separately, `native-bundle.Jqx8v6K3` / `native-import-run.A0Ln3II8` passed the
combined shared-engine, CLI, handler, zvol, property and native relocation
regression with exit 0. It adds the mode/GUID Manager changes to the prior
package below, but predates the malformed-option guard. This headless test
does not execute GUI controls. Original member and relocation backing hashes
passed; CD LoadSeg 333 still requires the documented byte-verified RAM CLI
isolation. External logs are `import-ui-bundle-stage-20261003.log` and
`import-ui-combined-qemu-20261003.log`. No stock-nightly or hardware claim.

## Current native path and admission integration (2026-10-03)

`native-bundle.aUnVNLhd` combines shared engine `shared-engine.syFUbJqZ`,
Manager from `native-path-shared-readers-20261003`, CLI `cli-link.3IKcJQu7`,
and unchanged handler `handler-build.7ZpHp1AH` / device `fresh-zvol-20261003`.
Stage hash checks pass; the archive and all new artifacts are on the external
OpenZFS archive, not the nearly-full root filesystem.

Exact-package QEMU `native-import-run.Rirbr3AI` exits 0. Enabled coverage:
Manager/API vdev get/set, typed values and 600-byte replies; CLI property
persistence and selected create/attach/remove/reguid/rename lifecycle tests;
zvol device and shared-library lifetime; native path relocation with nonroot,
read-only, online, blank and wrong-member refusals; two moves, three reimports,
post-move DOS writes and reads. Original disk hashes and all bytes outside the
new target partition match. This is headless API evidence, not a new visual
Manager test or a claim covering every upstream feature/combination.

Changes include retained binding/policy/label-path coordination, normal libzfs
reporting of stale identity (instead of abort), and shared DOS list/entry
admission locks (instead of spurious cross-process writer failures/suspension).
Negative controls and exact safety limits are in
`../engine/CLI-VDEV-PROPERTIES.md`.

CD `LoadSeg` error 333 remains separately reproducible (the platform issue tracker). Relocation
tests use byte-verified RAM copies of the actual CLI binaries after the raw
fixture copy; this isolation is not a CD/ATA fix. Live file-backed path setters,
physical failure/durability and stock-nightly qualification remain open;
private SDK/pthread/crypto/OS prerequisites still apply. Logs:
`native-path-bundle-stage-20261003.log` and
`native-path-bundle-qemu-20261003.log` under the external archive.

Exported file-backed pools can be moved and imported at explicit new paths:
mirror `file-backing-run.v8qBS6uw` and RAIDZ2 `file-backing-run.3g46uuJF`
passed fresh native read-only import, DOS payload/enumeration, unchanged
member bytes and independent host zdb checksum/leak traversal. This is the
cold export/move/import workflow, not a live writable-handle path setter.

## All OpenZFS deliverables freshly rebuilt (2026-10-03)

`native-bundle.u4jN1b3a` closes the reused CLI/handler/device gap in the fresh
engine package below. It combines `shared-engine.FyH5ARtZ` and the freshly
built Manager with CLI `cli-link.8LZQ9u8b` (all 25 common upstream units plus
fresh dependency/native-adapter objects from `cli-probe.uJvXdR9W`), handler
`handler-build.7ZpHp1AH`, and device `fresh-zvol-20261003`. The CLI links use
the fresh engine build's nvpair archive. All six command links passed; selected
input hashes passed. SDK/private pthread/crypto and OS overlays are still
external prerequisites, not rebuilt or stock-nightly-qualified here.

Exact-package headless `viuxaxon` exited 0 with selected native CLI/handler,
writable DOS/export/reimport and readonly checks plus original disk hashes.
Negative staging-policy checks also passed. Separate graphical `OgWvpqO1`
exited 0 after the Manager showed upstream OpenZFS 2.4.4 available and its
live List pools request correctly returned zero pools after fixture export.
`fresh-manager-ready.png` and `fresh-manager-pool-response-2.png` preserve
that narrow visual check, alongside original PPMs and `ui-actions.jsonl`.
No claim that an empty-list view verifies every Manager operation.

External logs: `fresh-cli-{source,link}-20261003.log`,
`fresh-handler-build-20261003.log`, `fresh-zvol-build-20261003.log`, and
`fresh-complete-{stage,policy,qemu,gui-qemu}-20261003.log`.
These are development acceptance results, not production-filesystem or
physical-device durability qualification.

The same immutable bundle also passed `native-import-run.4UUW9xOK` with
eight virtual CPUs and 8 GiB guest memory. The enabled pagination regression
created 25 additional pools, verified 26 unique pools across two pages, exact
filtering and end-cursor behavior, then destroyed all 25 additional pools.
Selected CLI/ZVOL/handler checks and writable DOS/export/reimport completed;
host exit was 0 and both original backing-image SHA256 checks passed. Log:
`fresh-complete-eightcpu-20261003.log` on the external archive. This extends
exact-package SMP coverage, not arbitrary concurrency or hardware durability.

## Fresh engine source build (2026-10-03)

`source-engine.7vQ6esGM` compiled all 248 engine-inventory translation units
from scratch, plus the linker's rebuilt native adapters/ZPL/dispatcher units,
a fresh nvpair archive, service and Manager. Build exit 0. No engine objects
were seeded from the earlier incremental tree. Source hashes were rechecked.
It uses source `9660bb6` plus the recorded external-output script change,
subsequently committed as `d2eaae2`.

`shared-engine.FyH5ARtZ` was built from that full-engine object and fresh nvpair;
its input hashes passed. Bundle `native-bundle.CQ2V1KUO` contains that library
and Manager with the existing CLI `cli-link.IEAkGRnK`, handler and ZVOL binaries.
Those latter components were NOT rebuilt by this clean-engine test. Exact
package QEMU verification is pending at this record. Build success is not a
runtime pass, stock-nightly qualification, or complete port claim.

Logs under the designated external archive: `fresh-release-source-20261003.log`,
`fresh-release-library-20261003.log`, `fresh-release-stage-20261003.log`, and
the pending `fresh-release-qemu-20261003.log`. Per-source build logs, source
inventory/hash manifests, generated revision header and binary hashes are in
the named source-engine directory. SDK, private pthread, crypto and OS-overlay
dependencies remain explicit external prerequisites.

`aros/engine/build-source.sh` now honors `AROS_ARTIFACT_ROOT`, checks the
100 GiB disk reserve before allocation, and generates its revision header
inside the output directory. Set that root to the dedicated external drive;
do not use this build as a reason to consume the nearly full system drive.

The exact package subsequently passed `native-import-run.g73rqje0` with host
exit 0: selected native CLI/handler regression, writable DOS/import/export/
reimport, readonly checks and unchanged original backing hashes. This closes
the pending runtime gate above for the fresh engine package; it does not
remove its SDK/private-runtime prerequisites or qualify every upstream feature.

## Temporary-name pool creation (2026-10-01)

`native-bundle.8IQDnzk4` packages shared engine `GPMcfMOx` and Manager from
`temporary-manager-20261001`. Exact-package QEMU `CPgSK193` passed upstream
CLI and authenticated Manager temporary-name creation, four malformed Manager
option refusals, export/permanent-name reimport, retained child dataset,
selected regression and original fixture hashes. Staging policy passed.
Actual GUI `BOBGOlsC` also passed after manually re-entering tname following
layout application. That discovered UI defect is fixed in source `2c564db`
but is NOT fixed in this immutable archive; its rebuilt Manager is undergoing
a separate visual retest. Private runtime overlays remain required; neither
stock-nightly compatibility nor physical durability is established.

## Recursive mounted snapshot rename (2026-10-01)

`native-bundle.nyxnGrzV` contains `shared-engine.gKEPdEis` from normal build
`snapshot-recursive-manager-20261001`, matching Manager and unchanged CLI,
handler and ZVOL inputs from the previous package. Targeted exact-package
QEMU run `rQ72kuC0` exits 0 with CLI/Manager recursive rename, busy-child
protection, parent retention, idle detach/remount and filesystem-recursion
refusal passing. Selected regression, shared lifetime, staging negative policy
and original disk hashes pass. Separate GUI run `SfZ3AVwk` verifies the
Recursive checkbox/confirmation and a successful mounted snapshot rename,
with device withdrawal and exact file read after read-only reimport.
GUI fixture has one snapshot; multi-mount coverage is the native API test.
Private OS overlays remain required. Multi-volume detach is not atomic;
hardware durability and the complete port remain unqualified. This immutable
archive predates the post-test note.

## Mounted single-snapshot rename (2026-10-01)

`native-bundle.G3rxV18m` packages `shared-engine.0kFuSnEX` from normal build
`snapshot-rename-auth-20261001` and its matching Manager, with unchanged CLI
`cli-link.4hjewxHq`, handler and ZVOL components. Explicit CLI pool-creation
mode run `Bumz1agO` passes mounted CLI/Manager snapshot rename, retained-lock
refusal, remount/read and read-only mount preservation, plus the selected
regression, shared lifetime and unchanged original hashes. Host exit 0.
The prior `VAjia7yw` failure was a test expecting "busy" instead of upstream's
snapshot-name rejection for `rename -u`; its evidence is retained.
Dedicated GUI run `OuXvvbdy` also passes device withdrawal and exact file
contents after read-only reimport. Recursive mounted rename remains guarded.
Private OS overlays are required; no hardware/full-port qualification implied.
The immutable archive predates this post-test note.

## I/O histogram refresh (2026-10-01)

`native-bundle.SfOBb8MV` packages `shared-engine.k8HMfh2j` from normal
service `io-histograms-20261001`, its matching Manager, and trace-disabled
CLI `cli-link.4hjewxHq`. Handler and ZVOL inputs remain unchanged.
Exact-package QEMU run `sCeFndx3` exits 0 with latency and request-size
histograms, the selected native regression, library lifetime and original
backing hashes passing. Negative staging-policy tests also pass.
Logs: `io-histograms-bundle-20261001-{stage,policy,native}.log`.
Separate normal-service GUI run `39FOLNk0` verifies histogram rendering and
pagination and also finishes the selected regression and backing hash checks.
Private OS overlays remain required; this is not full-port or hardware
qualification. The immutable archive predates this post-test record.

## Live redacted receive and snapshot enumeration refresh (2026-10-01)

`native-bundle.3l3aQJlc` packages `shared-engine.LrNwg7ba` from normal service
`redacted-live-final-20261001`, its matching Manager, and trace-disabled CLI
`cli-link.4hjewxHq`. Handler and ZVOL components remain those below.
Exact-package run `OGdH9hVi` exits 0: Manager and CLI mounted redacted receive,
retained-read refusal, namespace withdrawal, automatic mount cleanup and dataset
destruction/recreation pass, as do the remaining selected regression, library
lifetime and original backing hashes. Staging negative-policy tests pass.
Logs: `snapshot-growth-bundle-20261001-{stage,policy,native}.log`.

Private OS overlays remain required. This is not stock-nightly, physical
durability, graphical validation, or full-port qualification. The immutable
archive predates this post-test record.

## Receive conflict explanation refresh (2026-10-01)

`native-bundle.ePvxg9OP` contains `shared-engine.mwp66L8l` from normal build
`receive-conflict-20261001`, matching Manager, and the existing CLI/handler/ZVOL
components below. Exact-package run `nEnZwJZz` passed the complete selected
regression, including explicit-force raw receive on a writable mounted target,
the explanatory no-force conflict response, shared lifetime and original hashes.
Host exit 0; negative staging-policy tests pass. Logs:
`receive-conflict-bundle-20261001-{stage,policy,native}.log`.

Private OS overlays remain required. This is not stock-nightly, physical
durability, GUI rendering, or complete-port qualification. The immutable
archive predates this post-test note.

## Explicit forced Manager receive refresh (2026-10-01)

`native-bundle.bELY4G0G` packages `shared-engine.QMraXhrY` from normal
`manager-force-receive-admitted-20261001` with its matching Manager. CLI,
handler and ZVOL device remain those listed below. Exact-package run
`RUMbEgtg` passed with host exit 0, including forced mounted receive,
receive:append denial, grant/revoke checks, exact file contents and rollback,
the selected transport regression, shared-library lifecycle, standalone handler,
ZVOL and unchanged original backing hashes. Staging negative-policy tests pass.
Logs: `manager-force-bundle-20261001-{stage,policy,native}.log`.

This headless package qualification is not visual proof. Private OS overlays
are still required. Hardware durability, stock-nightly compatibility and full
port completion remain unproven. The immutable archive predates this note.

## Manager mounted receive refresh (2026-10-01)

`native-bundle.EvfOvnBm` packages `shared-engine.kQm5jmdV` from committed-source
`manager-receive-final-20261001` and its matching Manager, retaining the CLI,
handler and ZVOL device listed below. Exact-package run `odTV5UxP` exits 0:
mounted Manager incremental receive, truncated-stream recovery, exact files,
wait/rollback, promotion/delegation, full selected transport, shared-library
lifecycle, standalone handler, ZVOL and original backing hashes passed.
Negative staging policy tests also pass. Logs:
`manager-receive-bundle-20261001-{stage,policy,native}.log`.

The package run is headless, not GUI verification. Private AROS overlays are
required; hardware durability, mounted-redacted Manager receive and full-port
completion are not implied. The archive is immutable and predates this note.

## Manager mounted promotion refresh (2026-10-01)

`native-bundle.qubo4sd1` contains `shared-engine.40PjOFlG` from normal
`manager-promotion-20261001` and its matching Manager, with the same CLI,
handler and device as the rollback bundle below. Exact-package run
`CnQ6DrUe` completed with host exit 0: mounted promotion and delegation,
snapshot protection, rollback, receive recovery, full selected transport,
shared-library lifecycle, standalone handler, ZVOL and original disk hashes
passed. Negative packaging policy tests also pass. Logs:
`manager-promotion-bundle-20261001-{stage,policy,native}.log`.

This headless package run verifies the actual Manager API, not GUI clicks.
Private OS overlays remain required; published origin snapshots require
explicit unmount before promotion. The immutable archive predates this
post-test record. Hardware and full-port completion are not claimed.

## Manager mounted rollback refresh (2026-10-01)

`native-bundle.VFTOvlKa` packages `shared-engine.7qX9Wyi3`, built from
normal `manager-rollback-20261001`, its matching Manager, upstream CLI
`cli-link.UQWqkMs8`, handler `handler-build.Ta4ra5c4`, and device
`zvol-unmap.JxcNv91a/device`. Exact-package run `native-import-run.HZ640upO`
completed with host exit 0: the selected full transport regression, mounted
rollback/delegation, truncated receive recovery, shared-library lifecycle,
standalone handler, ZVOL and original backing-image hash gates passed.
Negative staging policy cases also passed. Logs are
`manager-rollback-bundle-20261001-{stage,stage2,policy,native}.log`.
The first stage invocation incorrectly supplied a Manager directory rather
than executable and failed; stage2 corrected the invocation.

Actual Manager clicking was separately verified in `ihGln6Kq`, documented
in [WAIT.md](../cli/WAIT.md). The immutable archive predates this post-test
record and the subsequent mounted-promotion work. Private OS overlays remain
required: this is not stock-nightly, hardware durability or full-port qualification.

## Mounted receive owner-task refresh (2026-10-01)

`native-bundle.e1i9MKGu` contains `shared-engine.DljdDEXJ` from normal
`receive-owner-release-20260930`, with its Manager, upstream commands
`cli-link.UQWqkMs8`, handler `handler-build.Ta4ra5c4`, and device
`zvol-unmap.JxcNv91a/device`. The engine fixes the reproduced mounted receive
EDEADLK/CLI abort by running finalization on the native DOS owner.

Exact-package run `native-import-run.g59nFcO9` exits zero with the mandatory
two-cycle mounted incremental receive, exact DOS readback, shared library
lifecycle, full selected transport suite, standalone handler and ZVOL checks.
Both original fixture disk hashes remain unchanged. Negative staging-policy
tests pass. Logs: `receive-owner-bundle-20260930-{stage,policy,native}.log`.

This archive predates the subsequent truncated-mounted-stream regression;
that additional result is not implied by this entry. The archive is immutable,
and its embedded documentation predates this post-test record. Private OS
overlays remain required; this is not stock-nightly, physical durability or
full-port qualification. See [WAIT.md](../cli/WAIT.md) for the original failures
and the separate pending-wait cancellation verification.

## Pool capacity and suspended-wait refresh (2026-09-30)

`native-bundle.VXaxBrSm` packages `shared-engine.HRtklXqm`, rebuilt from
normal `pool-capacity-final-20260930`, the matching Manager, upstream CLI
`cli-link.UQWqkMs8`, handler `handler-build.Ta4ra5c4`, and ZVOL device
`zvol-unmap.JxcNv91a/device`. It adds native suspend/resume wait cancellation
and real Manager pool-list capacity to the prior wait bundle below.

Exact-package run `native-import-run.5HCMC32M` completed with exit zero:
shared library lifecycle, standalone handler, full selected CLI/transport
regression, ZVOL device checks, writable import/export/reimport and read-only
verification passed. Both original fixture hashes remain unchanged. Negative
staging-policy tests also passed. Logs are
`pool-capacity-bundle-20260930-{stage,policy,native}.log` in `build-aros`.
The archive is `native-bundle.VXaxBrSm.tar.xz`; do not rewrite this tested
archive to add this post-test documentation.

The package test is headless. Actual Manager capacity rendering was separately
verified in `native-import-run.QvGenWpJ`, documented in
[pool statistics](../engine/POOL-IOSTAT.md). Pending wait/rollback cancellation
has separate instrumented evidence in [WAIT.md](../cli/WAIT.md); the bundle
does not contain those test hooks. Private OS overlays remain required;
stock nightly, physical durability and full-port completion are not claimed.

## Filesystem wait and native lifecycle refresh (2026-09-30)

`native-bundle.WD0V8QoJ` packages `shared-engine.rBrpiSHU`, rebuilt from the
normal `fs-wait-release-20260930` engine (no test linker wrappers), upstream
CLI `cli-link.UQWqkMs8`, and the matching updated Manager. It retains standalone
handler `handler-build.Ta4ra5c4` and ZVOL `zvol-unmap.JxcNv91a/device`.
The update includes real native deletion-queue state, unpublish cancellation,
republish reset, and Manager filesystem wait operation 49. The pool-import
recovery controls are shown only for import, not create/add.

Exact-package run `native-import-run.XXjKNf69` exits zero with shared-library
lifecycle, the full selected CLI/transport regression (`ZVOL_TRANSPORT=1`),
ZVOL device checks, standalone handler, writable import/export/reimport and
unchanged original backing hashes. This is a headless package test; it does
not click GUI controls or retain a deletion artificially. Separate actual
pending/cancellation evidence and GUI error feedback are documented in
[native wait verification](../cli/WAIT.md).

Logs: `fs-wait-bundle-20260930-native.log`,
`fs-wait-bundle-20260930-policy.log`, and
`fs-wait-bundle-20260930-stage2.log`. The initial staging attempt used the
ZVOL parent directory and was rejected for missing provenance; its log is
retained. Negative package-policy cases pass without changing selected inputs.
The archive is `native-bundle.WD0V8QoJ.tar.xz` (approximately 13 MiB).
This remains a development bundle requiring private OS overlays; it is not
stock-nightly, physical durability, or full OpenZFS port qualification.

## Inspected-partition creation UI bundle (2026-09-30)

`native-bundle.3LU2z6uI` updates the Manager to
`ui-partition-draft2-20260930/OpenZFSManagerNative`. Devices can copy the
selected, inspected partition into the creation editor without submitting a
request or silently adding a vdev. Other components are the shared engine,
CLI, handler and ZVOL builds listed in the discovery refresh below.

Exact-package test `native-import-run.DbaANoLf` exits zero: shared library
lifecycle, native CLI/ZVOL, standalone handler startup, DOS writes and
export/reimport readback pass. Original fixture disk hashes are unchanged.
Logs: `partition-draft-bundle-native-20260930.log` and
`partition-draft-stage-policy-20260930.log` (negative staging checks pass).
This headless package run does not click Manager controls. Separate actual
creation/Wanderer/cold-import evidence, including the writer's missing saved
response and failed strict verification, is in
[GUI creation verification](../ui/VERIFICATION-CREATE.md).

The archive is `native-bundle.3LU2z6uI.tar.xz`. It remains a development bundle
using the private OS runtime, not a stock-nightly or full-port qualification.
No DUT or user hosted session was used.

## Native device discovery UI bundle refresh (2026-09-30)

`native-bundle.U5mGkVBF` packages the real inventory-enabled Manager from
`ui-inventory-message-20260930` with the previously qualified shared engine
`shared-engine.ksynYXVZ`, CLI `cli-link.NFJeF5zM`, standalone handler
`handler-build.Ta4ra5c4` and normal ZVOL device `zvol-unmap.JxcNv91a/device`.
The Manager's Scan devices / Use device controls query the authorized native
inventory and select the actual device/unit for read-only partition inspection.
Missing service is reported explicitly; scanning does not import or format.

Exact-package QEMU run `native-import-run.hclUCL4C` exits zero, including
shared-library lifecycle, CLI/ZVOL checks, standalone handler startup, and
writable DOS export/reimport readback. Original fixture disk hashes remain
unchanged. Log: `inventory-bundle-native-20260930.log`. Negative staging-policy
checks pass in `inventory-stage-policy-20260930.log`.

This headless integration run does not exercise Manager controls. Their native
GUI evidence is `native-gui-run.5YSIXAMy` (real AHCI/ATA selection/inspection)
and `native-gui-run.N2zGqtdZ` (no-service feedback and normal closure), described
in `aros/patches/README.md`. Native worker ownership/error tests additionally
pass in `public-service-run.192CVfrI`.

The bundle retains explicit private DOS-loader/ATA/POSIX/stdio/PIPE runtime
requirements. It is not a stock-nightly qualification, full-port completion,
physical durability test or network access to the user's array. No DUT or
user hosted session was touched. The archive is `native-bundle.U5mGkVBF.tar.xz`;
its component checksums and build provenance are included in the bundle.

## Volume-alias collision shared engine refresh (2026-09-30)

`native-bundle.N1D4eqOy` packages `shared-engine.nXkmsFY7` from the normal
`volume-production` native service, with the locked foreign-volume alias
collision fix. Exact-package QEMU run `native-import-run.QzgwbetT` passes
the selected writable mirror, standalone handler, CLI, ZVOL, runtime-checker
and shared-library lifecycle regression. The runner exits zero and original
backing hashes are unchanged. Evidence and negative staging-policy run
`stage-policy.vOu4UbQy` are registered to the development test archive. Logs use `volume-bundle-`,
`volume-shared-` and `volume-stage-policy` prefixes.

The explicit private DOS-loader/ATA/POSIX/pipe/stdio runtime profile remains;
no stock-nightly, remote-array or physical durability claim is made.

## Execute-protection runtime checker refresh (2026-09-30)

`cli-link.NFJeF5zM` rebuilds all five native commands, including the new
OpenZFSRuntimeCheck loader checks. Exact bundle `native-bundle.93Jsr5W7`
passes QEMU run `native-import-run.hEjyPrUV`: both LoadSeg and NewLoadSeg
allow/deny/allow across protection changes, redirected stdio passes, and
the selected shared-library, CLI, ZVOL and standalone writable handler
regression completes. The runner exits zero and original backing hashes
remain unchanged. Evidence is registered to the development test archive; build/stage/test logs
use the `runtime-loader-` prefix. Negative packaging tests also pass in
`stage-policy.QhibnYbw`.

This run explicitly uses the private runtime profile with DOS loader overlay
`loader-overlay.L7ctzwkQ`, ATA, POSIX, pipe and stdio replacements. It does not
qualify a stock nightly or physical durability. The bundle does not install
those OS replacements. An unpatched DOS loader is expected to fail the new
runtime check; do not interpret that result as pool corruption.

New staging includes `Docs/EXEC-POLICY.md` with the loader prerequisite and
its limits, plus available CLI build-head, source/input hashes and thin archive
member hashes under `Metadata/cli-*`. These describe build inputs; packaging
checkout identity remains separate. They do not assert that every reused
upstream object was rebuilt from the packaging checkout.

## Imported Unix mode metadata refresh (2026-09-30)

`native-bundle.CHwtCaAR` packages `shared-engine.NS5mWEq7` from normal
`source-engine.P2gN7Bsv/mode-production`, the matching Manager, existing
whole CLI and qualified standalone handler/ZVOL device. Imported Unix modes
now supply DOS protection metadata when no AROS native attribute is present.
Exact-bundle QEMU run `native-import-run.O9dS6zqF` exits zero, with original
backing hashes unchanged, shared library lifetime, CLI, ZVOL, and standalone
writable handler export/reimport coverage. `stage-policy.BvimDVJz` passes
negative provenance/hash/component tests. Both are registered to the development test archive.
Logs use `mode-bundle-`, `mode-shared-` and `mode-stage-policy` prefixes.

The separate DOS loader execute-veto patch is NOT silently installed by this
bundle. Its hosted/native verification and remaining limits are documented in
`aros/engine/EXEC-POLICY.md`; unpatched DOS may still load exec=off files.
Private posix-pipe-stdio prerequisites remain. This is not stock-nightly,
physical durability, complete ACL translation or remote-server qualification.

## Special-file metadata shared engine refresh (2026-09-30)

`native-bundle.YCGhkAZv` contains `shared-engine.1OMqRGTI` from normal
`source-engine.P2gN7Bsv/special-production`. Exact-bundle QEMU run
`native-import-run.3BpVyfND` exits zero, including shared-library lifetime,
CLI, ZVOL and standalone writable handler export/reimport readback, with
unchanged original backing hashes. Evidence is registered to the development test archive.
The engine permits enumeration of imported Unix special nodes but refuses
their unsupported I/O; it does not implement Unix device/socket/FIFO I/O.
Private posix-pipe-stdio runtime requirements remain unchanged. This does
not qualify stock nightlies, physical hardware or remote server access.

## DiskInfo-compatible shared engine refresh (2026-09-30)

`native-bundle.rwq8lzaQ` packages `shared-engine.GJxlzZdk`, rebuilt from
`source-engine.P2gN7Bsv/diskinfo-production`, with the valid empty logical
device BSTR fix. The normal full dispatcher is retained; test modes are off.
The bundle includes current Manager output from that production build,
whole CLI `cli-link.f3IfUHrf`, the qualified standalone handler and ZVOL
device. Staging creates a new directory/archive and changes no installation.

Exact-bundle QEMU run `native-import-run.x5dhlt7W` exits zero with unchanged
original backing hashes. Coverage includes shared library nested-open,
foreign-task refusal/reload, standalone handler startup and DOS access,
writable mirror export/reimport, real CLI and ZVOL regression. Build/stage/
test logs use the `diskinfo-shared-` and `diskinfo-bundle-` prefixes.
`stage-policy.E998Vpsq` passes negative provenance/hash/handler/library checks.
Both evidence sets are registered to the development test archive.

The separate native DiskInfo consumer patch (filesystem label and capacity
percentage) is documented in `aros/patches/README.md`; the bundle does not
silently install an OS desktop replacement. Private posix-pipe-stdio runtime
prerequisites still apply. This is not stock-nightly, hardware durability or
remote-server qualification.

## Legacy engine and runtime UI bundle refresh (2026-09-30)

`native-bundle.6rhwmeAw` and its `.tar.xz` package shared engine
`shared-engine.f7nF1dfb` from the normal legacy-capable production engine,
`cli-link.f3IfUHrf`, and `native-runtime-ui/OpenZFSManagerNative` with the
real Runtime check action. Existing qualified standalone handler and ZVOL
device binaries are retained, with package hashes checked before boot.

Exact-package QEMU run `native-import-run.y9M6axRI` exits zero, with guest
completion PASS and unchanged original backing hashes. Enabled coverage
includes writable mirror lifecycle, CLI pool creation, ZVOL transport and
device operations, and standalone handler Mount/DOS-write/flush/export/
reimport readback. Log: `legacy-runtime-bundle-qemu.log`.
`stage-policy.BZeuaUU2` rejects incomplete/diagnostic services, stale or
missing binary hashes and missing shared libraries without changing inputs.
Its log is `legacy-runtime-stage-policy.log`.

This retains the explicit posix-pipe-stdio runtime prerequisite profile;
it is not stock-nightly or physical durability qualification. Actual
Manager Runtime check button verification is separately recorded in
`aros/ui/NATIVE-UI-TEST.md`. No DUT or shared hosted session was used.

## User-facing runtime check (2026-09-29)

Follow-up `native-import-run.YEWVGQW6` uses the explicit
`AROS_TEST_RUNTIME_PROFILE=posix-pipe-stdio` profile. It permits only the same
POSIX and pipe replacements as the earlier failing posix-pipe run, plus the
required recorded stdcio overlay; unrelated DOS/ATA/partition/filesystem
replacements are rejected. The checker and selected mirror regression pass
with unchanged backing hashes (`runtime-check-native-4.log`). This narrows
the observed stdio dependency without claiming arbitrary-nightly support.

New CLI builds include `C:OpenZFSRuntimeCheck`. It opens no pools or devices,
changes no global assigns or credentials, and uses a new private RAM directory
for a synchronous child stdout/stderr test. It reports library availability
and versions, configured security.library, exact redirected bytes, and whether
the parent can still use its output handles after child exit. Failures return
20; a pass applies only to these checks. On a broken runtime it may leave its
named RAM directory for inspection rather than closing a possibly invalid
inherited handle again. No automatic OS replacement or repair is performed.

`native-import-run.xwnYUc7O` fails the stdout check with the posix-pipe profile
despite successful child exit. `native-import-run.ojVj9ob0` passes the checker
and selected mirror regression using the explicit private profile plus
`cli-stdio.xPuSGC6J/stdcio.library`. These profiles differ in other overlays;
the comparison is not a new single-variable root-cause proof. The latter
runner exits zero with unchanged backing hashes. Logs:
`runtime-check-native-1.log`, `runtime-check-native-3.log`. Build 1 failed on
an unnecessary g_zfs dependency; the checker now links only native helpers.
The second native launch failed before boot on runner variable ordering and
is retained in `runtime-check-native-2.log`.

Pipe disconnect behavior, storage I/O, power-loss durability, GUI operation
and pool operations are explicitly outside this command's coverage. A pass
is not permission to use an experimental filesystem for irreplaceable data.

## Authenticated assign and canonical CLI refresh (2026-09-29)

`native-bundle.zpVlZgxR` combines `shared-engine.A67Qu3US` with
`cli-link.AtAI3CKP`, retaining caller security for pool path resolution and
canonical AROS vdev identities in upstream zpool. Exact-package native run
`native-import-run.FJ3GnMjA` enables CLI pool creation and verifies the alias
marker, complete mirror lifecycle, selected zvol checks, and standalone
writable/read-only handler access. Runner exit is zero and original backing
hashes match. `startup-cold.WgqCzq0M` independently passes protected SFS startup
records and read-only handler access across two QEMU processes.
`stage-policy.wYEkMWZG` passes packaging rejection and input-integrity checks.
Logs: `cli-canonical-bundle-native-1.log`, `cli-canonical-startup-cold-1.log`,
`cli-canonical-stage-policy-1.log`. These retain the private runtime prerequisite;
they do not establish stock-nightly compatibility or physical durability.

## Capability and startup CHECK refresh (2026-09-29)

`native-bundle.UbHuGosa` combines `shared-engine.0W9xMSGA` (request-list and
queued-cancel capability advertisement) with `cli-link.EFWrMXn2` (protected
startup record CHECK). Exact-package `native-import-run.4aqB9TZi` exits zero
after the selected CLI/device/handler/mirror regression and library lifecycle
checks, with original backing hashes unchanged. `startup-cold.edhzemAV` also
passes across two separate QEMU processes, including disabled/damaged record
CHECK, protected ENV restoration and readonly handler access. Both evidence
sets are registered to the development test archive. Logs: `request-capability-bundle-native-1.log`
and `request-capability-startup-cold-1.log`.

These are private-runtime tests, not stock-nightly installation or physical
durability qualification. Hosted authenticated GUI coverage is still separate.

## Queued cancellation package (2026-09-29)

`native-bundle.p0pgIpJu` and its `.tar.xz` include shared engine
`shared-engine.ageqebH4`, the cancellation-capable Manager, upstream CLI,
native device and standalone handler. Exact-package run
`native-import-run.S6mmEp5A` passes the selected native CLI/device/handler/
mirror regression, cancellation rejection/Manager parser checks and eight
library lifecycle cycles. Host exit is zero; original backing hashes match.
Logs: `cancel-bundle-stage-1.log`, `cancel-bundle-native-1.log`.
Packaging policy `stage-policy.ZONuPo8J` passes all 15 refusal cases.

Successful queued cancellation and its actual GUI confirmation/result were
separately verified with a private scheduling fixture in
`native-import-run.C7AsQ4ih`; that instrumented engine is rejected by both
library construction and distribution staging. It is not in this bundle.
This remains an experimental development package, not production filesystem
qualification or proof of normal-nightly installation compatibility.

## Refreshed Manager package (2026-09-29)

`native-bundle.pjoMYAUw` includes the native Manager Clear fields correction
verified graphically in `native-import-run.leYPpFuQ`, alongside the shared
engine and standalone handler. Exact-package `native-import-run.PgPA7FrN`
passes the selected full CLI/device/handler/mirror regression and eight
library lifecycle cycles, exits zero and preserves original backing hashes.
Logs: `manager-clear-bundle-stage-1.log` and `manager-clear-bundle-native-1.log`.
This headless packaged regression is separate from the actual Manager GUI
clickthrough; it does not establish stock-nightly compatibility or production
readiness. The documented runtime prerequisites still apply.

This is a native x86-64 AROS development/test bundle, not a completed production
release. It contains the real upstream `zfs` and `zpool`, the native Manager,
and the resident native service with upstream engine and DOS handler. Shared
bundles place the engine in `Libs/openzfs.library` behind a thin service launcher;
older monolithic bundles remain supported. Handler-inclusive bundles also
provide `L:OpenZFS-Handler` for normal Mount activation against an imported
dataset; see HANDLER.md. Production installation/startup qualification remains
unfinished.

The bundle includes operation guides for events, redaction, Lua channel
programs, DDT pruning, snapshot diff, file-vdev label clearing, prefetch,
pool upgrades, activity waits and import diagnostics. These distinguish
verified native behavior from pending UI, concurrency and hardware coverage.
They are not a declaration that every upstream feature is production-ready.

## Release startup qualification

`native-bundle.xDmJ6iC9` includes `L:OpenZFS-Handler` alongside the shared
engine. Exact-package `native-import-run.rKALYhoQ` passes the full selected
mirror/CLI/device and eight-cycle library tests plus real readonly/writable
Mount activation, denied-user retry, readback and late-port behavior.
`startup-cold.LUJzLcWM` separately passes saved mountfile activation following
protected settings restoration and import across two independent guest boots.
Both runners exit zero with unchanged original backing hashes; the cold reader
also preserves archive/member hashes. See HANDLER.md for limits and failures.

`native-bundle.bhgGRwWh` is the first shared-engine bundle: it includes the
real `openzfs.library`, thin launcher, rebuilt native Manager, upstream commands
and normal ZVOL device. Exact-package `native-import-run.ZgS4fE5N` passes the
full selected mirror/device regression, dedicated owner/unload lifecycle tests,
denied startup/repeat-run/caller-exit checks, and unchanged original backing
hashes, with host exit zero (`shared-engine-bundle-native-1.log`). Eleven
negative staging cases pass in `stage-policy.6PYAQsdz`; the normal monolithic
staging path still passes its seven cases and builds `native-bundle.eNPD1xuS`.
This remains a development bundle requiring the documented runtime fixes.
Its immutable archived docs precede these acceptance notes.

The same shared bundle passes two independent guest boots in
`startup-cold.bSKBiMO1`: protected saved records, pre-service PREPARE,
malformed/non-root refusals, normal ENV copy, authenticated import/mount,
exact DOS read and readonly write refusal. Host exit zero, archive/member
hashes and original backing hashes match (`shared-engine-bundle-cold-1.log`).
This uses the recorded private runtime overlays and test identities, not a
production installation or power-failure durability test.

`native-bundle.GEp56QAr` packages the retained-reply-port service, rebuilt
Manager, PREPARE-capable upstream CLI and normal ZVOL device. Exact-package
`native-import-run.iVD0l7OF` passes the full selected mirror/device regression
with host exit zero and unchanged original backing hashes
(`retained-ports-bundle-native-1.log`). This includes live volume resizing,
device geometry/read/write/flush/abort/eject, import authority and cold-style
export/reimport within the test. Separate actual multi-boot admission coverage
is recorded in `engine/MOUNT-ENDPOINT-LIFETIME.md`; this packaged run is not
itself a power-loss test. Auxiliary reply ports remain retained because an
empty queue does not establish sender quiescence. The same private runtime
prerequisites and non-production limitations apply; the immutable archive's
documentation predates this acceptance note.

`native-bundle.EZl7XcAj` includes the uninstrumented live-request service,
rebuilt Manager, PREPARE-capable CLI and normal ZVOL device, plus REQUESTS.md.
Exact-package `native-import-run.bZruzyyV` passes the full selected mirror/
device regression with host exit zero and both original backing hashes
unchanged (`live-request-bundle-native-1.log`). Retained request identity
filtering and release removal pass in this normal build. Deterministic
pending-worker visibility, foreign-user exclusion and drain removal use the
separate private injected run documented in REQUESTS.md; that test service
is not packaged. This remains a development bundle, not final shared-engine
packaging or production/hardware qualification.

`native-bundle.oxqBvSYa` includes `dnodesize-service`, its rebuilt Manager,
PREPARE-capable CLI and normal ZVOL device. Exact-package run
`native-import-run.QDUvmCq8` passes the full selected mirror/device regression,
including all seven dnodesize values, unsupported target-type rejection and
disabled-feature refusal before explicit enablement. Host exit zero and
original backing hashes match (`dnodesize-bundle-native-1.log`). No per-size
physical dnode inspection or production deployment is implied; the archived
documentation predates this acceptance note.

`native-bundle.ZzLVrHMp` adds the native `OpenZFSStartup GUID PREPARE`
command through CLI build `cli-link.AUPAiRcg`. Exact-package regression
`native-import-run.eo5Ubx9v` passes the full selected mirror/device suite,
including release-mode rejection, eight engine policies, CLI transport and
native DOS mount/write/export/reimport tests, with host exit zero and both
original backing hashes unchanged (`startup-prepare-bundle-native-1.log`).
The separate pre-service cold-boot and authorization checks are recorded in
PREFERENCES.md. This is still a development bundle, not production startup
installation; its immutable archived docs precede these acceptance notes.

`native-bundle.zegUt12I` packages the eight-property `integrity-policy-service`
and rebuilt Manager. Exact-binary `native-import-run.QYzCHDNO` passes the
selected full mirror/device suite with the posix-pipe runtime profile,
including release startup rejection checks and eight-property validation.
Host exit is zero and original backing hashes match
(`integrity-policy-bundle-native-1.log`). The same bundle passes protected
startup settings across two separate QEMU boots in `startup-cold.dXJik77E`:
persist, restore, import, mount, exact DOS read and readonly write denial;
original member images remain unchanged (`integrity-policy-bundle-cold-1.log`).
That cold-start test uses the documented private DOS/ATA/partition/filesystem
overlays and test security configuration, not an unattended production install.
The immutable archive's documentation predates these acceptance results.

The newer `native-bundle.2T3N1jY3` packages `engine-policy-service`, its rebuilt
Manager, the upstream CLI and normal device 1.10. Exact-binary run
`native-import-run.wZrXCJqc` passes its manifest, release startup, five-property
Manager set/inherit/delegation/encryption checks and full selected device/
mirror regression. The runner exits zero with unchanged backing hashes
(`engine-policy-bundle-native-1.log`). It uses the posix-pipe runtime profile.
Its immutable docs predate this acceptance note. Further property additions
are not part of this tested archive until separately packaged and qualified.

`native-bundle.7s6OdtOI`, built from `release-modes-service`, passes the exact
packaged native mirror/device regression in `native-import-run.S8lnNiSS`.
The runner exits zero and both original backing-disk hashes match. Before
normal service startup, the guest rejects all three diagnostic modes, two
malformed invocations and an unauthorized WRITABLE launch without publishing
service ports. The subsequent full suite includes CLI and Manager redacted
incremental resume with exact DOS readback. Logs remain in the run directory
and `release-modes-bundle-native-1.log`; evidence is registered to the development test archive.
This is isolated QEMU with private runtime overlays, not stock-nightly,
physical durability or additional GUI-click qualification. The immutable
bundle predates these acceptance notes and the expanded stager diagnostics.

## Baseline runtime dependency qualification

The native runner accepts `AROS_TEST_RUNTIME_PROFILE=private` (default),
`base`, `dos-only`, `posix-only`, or `posix-pipe`. Restricted profiles reject
unrelated OS-overlay variables instead of silently repairing the baseline.
`base` preserves the selected ISO's OS packages and libraries; `dos-only`
replaces its base package, `posix-only` only posixc.library, and `posix-pipe`
adds the disconnect-policy pipe handler and its mount configuration. Test
startup, disposable accounts and OpenZFS executables are still injected.

These comparisons used the local amiwest nightly ISO whose exact hash is in
each run's inputs.sha256, not an independently downloaded public nightly:

- `NsjbKKMZ` (base) and `UTAmpEsJ` (DOS only) boot, initialize the engine and
  import the mirror, but lose the expected stderr diagnostic from a Manager
  worker's invalid ddtprune command. Both terminate with a test failure.
- `hrmYJhsf` (POSIX only) restores that diagnostic and passes DDT pruning and
  resumed-stream checks. It reaches the 600-second deadline before the pipe
  partial-close result; this is incomplete, not proof of a guest crash.
- `FTk3muvR` (POSIX plus pipe) passes the entire selected transport/mirror
  suite, including partial-close, replication, permissions, DOS writes and
  export/reimport. Host exit is zero and original backing hashes match.
  Packaged zvol device checks were not enabled in this run.

The expanded `ob0iZi19` run enables the exact packaged zvol device as well;
its geometry, I/O, bounds, readonly, eject, authorization, abort and remapping
checks pass alongside the transport/mirror regression. Runner exit is zero
and original backing hashes match (`posix-pipe-device-runtime-native-1.log`).

The passing profile uses `cli-isatty.wSnzlNPf/posixc.library` (which includes
earlier stream-teardown fixes, not just isatty) and
`pipe-policy.EzbhH6gz/pipe-handler`. It needs none of the private DOS, ATA,
partition, filesystem or z1 replacements for this selected suite. This does
not remove their requirements from other tests or establish universal nightly
compatibility. The separate stdcio parent-Flush platform test remains distinct.
Extracted-file comparisons in `runtime-identity.NkHQ3nPw` confirm five baseline
runtime artifacts unchanged in the base run and the four non-POSIX artifacts
unchanged in the passing profile. the development test archive retains the failed and passing runs.

Initial attempts `KD4SEgqQ` and `aLlqjxx7` did not boot: the first requested
an unsupported readonly/whole-CLI combination; the second exposed redaction
fixtures depending on disabled transport helpers. Commit `04635c9` fixes the
conditional compilation; plain and transport object builds both pass in
`fixture-config.h9csUtvd`. Neither initial attempt is runtime evidence.

## Earlier event-capable bundle acceptance

`native-bundle.uobw901W` combines `events-publisher-service`, upstream CLI
`cli-link.NgMGW2me`, its matching Manager and the normal device 1.10 build.
The exact packaged binaries pass `native-import-run.giW2T68x`: writable mirror
import, DOS writes, export/reimport, readonly access and the selected device
regression. The runner completes successfully with unchanged backing hashes;
`events-bundle-native-1.log` retains its final gates. These are development
runtime results, not stock-nightly or physical durability qualification.

Graphical run `native-import-run.cAQw0K6W` verifies Manager Events through the
real upstream command: successful oversized output reports its temporary
file, clearing requires confirmation, Cancel submits nothing, confirmed clear
reports 69 events removed, and the next listing contains only its column
header. `events-empty-verified.png` is the empty-queue proof; the earlier
`events-empty.png` instead preserves an unsuccessful keyboard shortcut attempt
and must not be described as an empty queue. Raw framebuffer captures and UI
actions are retained and registered in the development test archive. This visual check does not
claim inspection of the oversized output file. The full graphical regression
is separately tracked; these observations alone do not establish its final
completion. That graphical run subsequently reached its 1800-second harness
deadline during later tests and exited 1; original backing hashes still match.
Its visual checks stand, but it is not a completed full regression. The
headless packaged run above is the full passing regression. The immutable
archive's README predates these acceptance notes.

Contents under `C/` are intended for the corresponding AROS command directory:

- `OpenZFSServiceNative`: resident engine, authenticated API and DOS endpoints.
- `OpenZFSManagerNative`: native management UI.
- `zfs` and `zpool`: whole upstream commands, not subset replacement scripts.
- `OpenZFSStartup`: trusted saved-record import and pool-scoped mounting.
- `OpenZFSStartupMount`: explicit scoped mount of an already imported pool.
- Optional `OpenZFSDevice`: explicit resident zvol mappings as units of
  `openzfs.device`, readonly by default. It is included when the fourth staging
  argument names a verified device build directory. No startup mapping or DOS
  mount is installed automatically; see the included ZVOL-DEVICE.md.

Shared bundles additionally require `Libs/openzfs.library` beside the command
payload; copying only the thin launcher is insufficient. The manifest covers
both artifacts. Staging binds the library to a hash recorded by its builder,
checks real engine/host symbols, and rejects an engine-embedded or injected
launcher. The library builder requires the matching normal full-dispatcher
engine-object provenance. See `SHARED-ENGINE.md` in those bundles for the
owner-task ABI and retained-endpoint shutdown constraints.

Do not overwrite a running installation. Use isolated QEMU test media first.
The bundle deliberately contains no kernel modules, OS library replacements,
test security accounts, saved credentials, automatic imports or Startup-Sequence
edits. It is not self-contained: the validated AROS DOS, ATA/partition, POSIX,
security.library/MuFS, z1.library, Zune and IFF parsing runtime are prerequisites.
Shared CLI output handles also require the inherited-handle flags correction
in stdcio.library; the broad platform acceptance uses the private
`cli-stdio.xPuSGC6J` build. An uncorrected runtime fails the mandatory parent
Flush check after child exit. This bundle does not silently replace that OS
library; the frozen overlay and its hashes are in the platform test evidence.
Exact QEMU overlay and executable hashes are retained with each test run.
An arbitrary stock nightly is not yet a supported compatibility claim.

The public service defaults to read-only engine mode. `OpenZFSServiceNative
WRITABLE` requires configured security.library and an authorized system task
identity; individual operations also retain their caller-based authorization.
Start it as a resident process before using Manager or commands. Never copy the
test launcher's accounts or automatic login into a real installation. The
service refuses shutdown while active clients or DOS endpoints require it.

Default native service builds accept only the readonly default and authenticated
`WRITABLE` mode. Diagnostic `TEST-DROP`, `TEST-WRITE` and `TEST-NATIVE` modes
require explicit `AROS_SERVICE_TEST_MODES=1` when building, and such builds must
not be distributed. Staging requires a full dispatcher, diagnostic modes off,
and a matching selected-service hash recorded by the successful builder. Old
builds without this provenance must be rebuilt, not relabelled. The no-engine
UI protocol fixture and admission harness explicitly retain their test modes.

Production unattended startup is not installed or qualified. Isolated combined
cold-boot saved-record restoration/import/mount is verified in PREFERENCES.md.
The Manager's
Startup editor supports a separate secure **Save for boot** action, requiring
configured MuFS and system ownership. `OpenZFSStartup GUID-16-HEX` consumes
only a protected system-owned ENV record, authenticates explicit member paths
and expected GUID, and optionally mounts that pool's eligible filesystems.
Disabled records do nothing; duplicate import reports failure, not fresh success.
`OpenZFSStartup GUID-16-HEX CHECK` validates the protected ENV record without
restoring ENVARC, probing devices, starting the service or importing a pool.
It requires the same configured MuFS/system identity and trusted file metadata
as boot import. It reports the pool, GUID, member count and enabled/read-only/
mount flags. A valid disabled record succeeds as a configuration check;
success does not establish that its devices exist or that import will succeed.
Missing, malformed and unauthorized records fail rather than falling back.
Bundle `native-bundle.ESzOIokv` includes this command. Two-boot test
`startup-cold.DiJ9X73R` passes trusted CHECK before service startup, missing
record/malformed argument/non-root refusals, subsequent import, saved handler
activation, exact DOS read and readonly write refusal. Host exits zero with
original backing hashes unchanged (`startup-check-cold-1.log`). The test uses
private runtime overlays and is not unattended production installation proof.
Follow-up `startup-cold.ybFB4XNf` also verifies a disabled record referencing a
nonexistent device succeeds in CHECK, while a deliberately damaged IFF header
fails and remains unchanged. The disposable malformed record is removed by
the test; the original record still imports and mounts successfully. Both
boots finish with exit zero and unchanged original backing hashes
(`startup-check-damaged-1.log`).
Read-only flags remain enforced by the service. No force, sharing, key prompts
or automatic import retries are performed. A mount failure leaves the pool
imported and reports partial progress; inspect it before taking further action.

Optional `RESTORE` explicitly validates the protected ENVARC record and
publishes a protected ENV copy before importing. It can overwrite runtime
settings with persistent settings; it is never an implicit fallback. Unsafe
existing files or unfinished transactions are refused, not repaired. Normal
AROS boot uses `Copy NOPRO`, so trusted metadata across actual boot still needs
careful deployment verification. See PREFERENCES.md for test evidence and
limitations. Do not add automatic startup commands on a production machine.
The separate Manager window/category preferences grant no import authority.
No production data or physical array should be used to test this bundle.

Run `bash aros/distribution/stage.sh <full-service-build> <whole-cli-link> [manager-binary] [device-build-directory]` on
the build host. It creates a fresh directory and compressed archive under
`build-aros/`, rejecting traced/fault-injected CLI builds. `SHA256SUMS` covers
all packaged files. Packaging HEAD records the checkout at staging time, not
unproven binary provenance; `source-binaries.sha256` records exact inputs.
The documentation set carries `DATASET-UPGRADE.md` and `DATASET-REWRITE.md`
separately from the pool-format `UPGRADE.md`; packages that expose Manager
operations 47 and 48 must not omit their irreversible-format and I/O warnings.
The optional Manager input allows a separately built and tested UI to be
packaged without silently choosing an older UI from the service directory.
The Manager must carry its `.inputs.sha256` build manifest. Its final record
is the output binary hash emitted by `build-manager-native.sh`; staging checks
that hash against the selected binary rather than following an old absolute
path. Missing/empty manifests and mismatched binaries are refused before any
bundle is created. Native QEMU bundle `native-bundle.wcyovb5q` passed in
`native-import-run.jgw8vjdl` with the rebuilt inventory Manager; the Manager's
separate interactive scan/inspect/refresh proof is `native-import-run.gQyzR5fC`.
Negative Manager staging tests passed alongside existing service/library/handler
policy tests in external `manager-stage-policy-20261002.log` (2026-10-02).
The optional device input must have diagnostics and startup-test flags disabled;
the stager additionally rejects embedded tracing/startup-barrier strings. Its
binary and input hashes are retained. Bundle device tests copy that exact
packaged executable; they must not silently build a replacement from source.
FFS-on-zvol tests currently require the separately verified private AFS
ACTION_DIE fix. No replacement filesystem handler is included in this bundle.
The stager also rejects any native service containing diagnostic linker
wrappers. `full-cli-split-retained` was refused with exit 2 before staging;
the normal `full-cli-split-lifetime` service staged successfully as
`native-bundle.eEaer0tl`, with all manifest checks passing. That staging check
alone is not a packaged-runtime test. Logs: `bundle-split-injection-refused.log`
and `bundle-split-lifetime-stage.log` under `build-aros/`.

For native validation, set `AROS_TEST_NATIVE_BUNDLE` to the resulting directory
with the existing private native-import QEMU runner. It verifies the manifest
and tests the frozen packaged service and CLI binaries. This validates command
and handler behavior, not a GUI visual test or boot-time automatic import.

## Verification

`native-bundle.LzVdV4dy` (staged at `2001428`, about 13 MiB compressed)
contains device 1.10, the capabilities-aware upstream zvol discard service
from `zvol-unmap.JxcNv91a/service`, and the existing verified Manager/whole CLI.
Exact packaged binaries pass writable `native-import-run.hMtBadHv` (exit zero):
standard SCSI UNMAP, malformed-list prevalidation, capability pages, read-only
refusal, direct transport discard, Manager resize/readonly and selected full
native/CLI regression. Cold `native-import-run.URHliT2v` also exits zero:
persisted UNMAP hole/neighbors through the device, retained FAT data, API
mutation refusals and mandatory independent `zvol-integrity.REl2g7uW` GPT/zdb
checks with unchanged source/writer/reader hashes. This is logical zvol
deallocation, not physical or secure SSD TRIM. Runtime prerequisites and
non-production limits still apply. The immutable archive documentation predates
these acceptance results; package and executable hashes are retained per run.

`native-bundle.tMQQLH2l` (staged at `75bf7e7`) refreshes device 1.9 with
the verified Manager zvol size API and effective inherited readonly policy,
including Manager set/get/inherit. It uses normal service
`manager-zvol-readonly.kbwY2d3K/service`, whole CLI `cli-link.Fa0pX0f6` and
`OpenZFSManagerNative-zvol-readonly-v1`; all archive manifests pass.

Exact packaged binaries pass writable `native-import-run.B22BjK38` (exit zero),
including both resize and readonly API/device suites, direct write refusals,
restored writes and the selected native/CLI regression. Independent
`control-integrity.QIj4tr6C` reports No leaks and matching verification inputs.
Packaged cold `native-import-run.mPBVmFba` also exits zero: retained raw/FAT
bytes, read-only mutation refusals, mandatory GPT and upstream zdb checks
(`zvol-integrity.QC6GxHCx`) and unchanged source/writer/reader hashes.
The same runtime prerequisites and non-production limitations still apply.
GUI readonly acceptance and its serial-summary limitation are recorded in
ZVOL-DEVICE.md; the immutable archive's documentation predates these results.

`native-bundle.V4kSsH9S` (staged at `572a83e`) contains normal device 1.9,
`OpenZFSManagerNative-eject-v1`, the existing ZIL-fix full service and API3
upstream commands. This adds shared CLI/GUI mountfile generation and guarded
administrative eject to the earlier device 1.7 archive. The manifest and
Manager source-input hashes pass. The archive is about 13 MiB; it remains a
development bundle requiring the explicit runtime dependencies listed above.

Its exact packaged binaries pass `native-import-run.T43MmSCs` with exit zero:
actual FAT format/write/readback, mounted-handler eject refusal, guarded and
CLI eject validation, write/eject/readonly-remap/exact-read, and the remaining
selected CLI/native lifecycle. Independent `control-integrity.xIQalXlU`
reports No leaks with unchanged extracted-member and original backing hashes.
Fresh packaged cold FAT reader `native-import-run.hPbOrXYm` also exits zero,
including mutation refusals, exact retained bytes and mandatory independent
GPT/zdb check `zvol-integrity.cTuSCfGb`; all writer/source/reader hashes match.
The private FAT handler correction is still a separate prerequisite, not
silently included in the archive. GUI evidence and the retained interleaved
completion-marker limitation are recorded in ZVOL-DEVICE.md; these headless
package runs do not add GUI coverage. No stock-nightly, physical-media,
service-death or power-loss qualification is claimed. This immutable archive's
README predates these post-package acceptance results.

The same archive passes pre-service EMPTY startup in
`native-import-run.7VTfNXk2` (exit zero): 32 absent units, failed remap before
service availability, subsequent readonly/writable mapping and full selected
CLI/native regression. Independent `control-integrity.SMuLF38M` reports
No leaks and unchanged extracted-member/source hashes.

`native-bundle.68mzVmSL` refreshes the frozen archive to normal device 1.7
and `OpenZFSManagerNative-remap-v1`, retaining the ZIL-fix resident service
and API3 whole commands. All archive manifests pass. The Manager is byte-for-byte
the binary with separate readonly and writable interactive remap acceptance
in ZVOL-DEVICE.md; headless packaged tests do not substitute for GUI evidence.

Exact packaged executables pass `native-import-run.1bnLkKCP`: EMPTY device
publication before any engine service, 32 absent units, refused pre-service
remapping without state change, late readonly/writable mappings, and the full
selected CLI/native lifecycle. The runner exits zero. Independent extracted
member check `control-integrity.iqRaZIlc` reports No leaks and preserves member
and original backing hashes.

Fresh cold FAT reader `native-import-run.5xpUXaNU` uses the same archive and
the retained ST2gDP2o writer. Exact file bytes, six mutation refusals, device
and filesystem teardown, and mandatory GPT/zdb gate
`zvol-integrity.McwJS9rB` pass with runner exit zero and No leaks. Source,
writer and readonly-reader hashes are unchanged. The separate private FAT
TD_PROTSTATUS correction is required and is not silently bundled. These
results do not establish stock-nightly compatibility or power-loss durability.

Exact-bundle cold dRAID reader `native-import-run.IyVuat42` also exits zero
against the retained UNTjyHWk active-spare writer: real zpool and Manager API
report INUSE with the original member OFFLINE, exact changed bytes and the
1 MiB payload survive, writes are refused, and export succeeds. Independent
upstream zdb reports No leaks, all three extracted members match the writer,
and the original writer image remains unchanged. The remaining CLI/native
suite passes. This directly verifies the service/CLI inputs' cold-spare fix;
build dates or older packaging notes alone cannot establish its absence.
No arbitrary dRAID geometry or automatic failure response is claimed.
Retained preboot failure `6hhGrZAy` omitted required CLI_POOL_CREATE test mode;
the guard rejected it before QEMU. The corrected invocation changed no guard.

`native-bundle.mTc6zXx2` refreshes the frozen bundle to device 1.4 and the
live-mapping-verified Manager `OpenZFSManagerNative-zvol-mappings-v2`, retaining
the ZIL-destroy-fix service and API3 commands below. Exact packaged binaries
pass `native-import-run.exwTlQUA`: two-unit 6 GiB/3 TiB I/O, mapping queries,
inquiry/capacity, protection/eject and the full selected CLI/native regression.
Independent `control-integrity.2XEqKJTJ` reports No leaks with unchanged
extracted members and original backing hashes. Fresh cold FFS reader
`native-import-run.C3p1oCEj` uses the same packaged binaries and passes exact
retained file contents, write refusal, handler/device shutdown and the mandatory
GPT/zdb integrity gate. Both runners exit zero. These are private-overlay QEMU
results, not stock-nightly or physical durability qualification. The immutable
archive's enclosed README predates this post-package verification entry.
Manager visual acceptance is separately recorded in ZVOL-DEVICE.md; the
packaged-runtime runs do not exercise its GUI.

`native-bundle.nzV7QgZ0` adds the nontracing resident zvol device, the
ZIL-destroy-fix service from `sync-destroy-zil.KGho64Cx/service`, API3 commands
from `cli-link.Fa0pX0f6`, and the visually verified
`OpenZFSManagerNative-iostat-auto`. The device comes from the normal
`native-import-run.XtVgKOz3/zvol-device` build, with both diagnostics and
startup-test flags zero. A barrier-test device was refused before staging
(`bundle-zvol-barrier-refused.log`, exit 1).

The exact packaged binaries pass `native-import-run.V6mxGnns`: all 32 zvol
units, distinct geometry/data, bounds and partial-open cleanup, readonly
protection and independent ejects, plus the full selected CLI/native suite.
Independent `control-integrity.ofI5tcfA` reports No leaks and unchanged
extracted members/original fixtures. Fresh guest `native-import-run.qH000Xvu`
uses the same packaged binaries to read the FFS volume retained by writer
`mTih84aE`: exact bytes, readonly denial, clean handler/device shutdown and the
mandatory independent GPT/zdb integrity gate all pass. Original, retained
writer and cold-reader hashes remain unchanged. Both runners exit zero;
packaged and executed device binaries compare byte-for-byte identical.

These tests require the documented private OS overlays, including the AFS
shutdown fix; they are not stock-nightly, physical-array, GUI-repeat or
power-loss qualification. The archive is immutable and its enclosed README
predates these post-package results. Do not rewrite the tested archive.

`native-bundle.VLRo1Gcz` is the API3 refresh of all six executables, using
the inputs below. Its manifest and exact packaged service/CLI pass full
`native-import-run.Evqt0CUn` (exit zero), including cold dRAID active-spare
import, CLI and Manager API INUSE reporting, exact data and readonly refusal.
All three retained members and writer backing remain unchanged; independent
zdb and the full native/CLI regression pass. This does not add packaged GUI
interaction coverage or stock-nightly/physical-array qualification. The archive
is immutable; its enclosed README predates this post-package result.

Current source uses management API3. The tested `full-typed-draid-manager-stats`
service, `cli-link.Fa0pX0f6` commands and
`OpenZFSManagerNative-typed-draid-v2` Manager are the next bundle inputs.
API3 expands typed groups with dRAID geometry and uses versioned v3 management
ports; replace Manager and service together in a fresh isolated runtime.
The actual Manager created a dRAID pool and browsed its files in Wanderer
(`native-gui-run.CVD26KlO`). Separate typed-created persistent writer
`d5xNJ8wK` and cold reader `KFDyzDtJ` verify active-spare persistence and
correct CLI/Manager INUSE reporting, exact data, readonly denial, unchanged
retained members and independent zdb. These input results are not yet proof
of a newly staged archive; exact packaged runtime verification is separate.
The previous immutable API2 bundles below are not silently upgraded.

`native-bundle.u2WkmGXM`, staged at `7eedde6`, contains the normal
`full-draid-readonly-stats-v2` service, `cli-link.S1jF9lh5` upstream commands
and the existing `OpenZFSManagerNative-typed-vdev`. Its frozen manifest and
packaged binaries pass `native-import-run.PaFrNdMa` (runner exit zero).
That run cold-imports a copy of retained dRAID writer `UNTjyHWk`: spare INUSE,
original member OFFLINE, exact changed bytes and 1 MiB payload, write refusal
and successful export. All three extracted members and the source backing
image remain unchanged; independent `zdb -bcc` passes. The full remaining
CLI/native regression and original mirror hash checks pass as well.
This bundle includes the dRAID CLI/import fixes missing from `PFF9EWim`.
The Manager still lacks a guided dRAID topology editor. No new GUI coverage,
stock-nightly compatibility, physical-media or power-loss claim is made.
The immutable archive contains the documentation as it stood before this
post-package runtime result; do not rewrite the tested archive.

`native-bundle.PFF9EWim` refreshes all six commands using the normal full
`full-typed-native-replace` service, `cli-link.PU8NyHsg` upstream tools and
`OpenZFSManagerNative-typed-vdev`. It includes typed native creation/add/attach/
replacement and the explicit missing-member CLI discovery fix. Its frozen
manifest and binaries pass the complete packaged-runtime suite in
`native-import-run.0Ee7FX1M` (runner exit zero, original backing hashes match).
This run exercises CLI lifecycle, split, attach/replace/resilver, expansion/
evacuation, API/DOS integration and export/reimport; it is not an exhaustive
run of all native-partition permutations or GUI commands.

Separate `native-gui-run.bXl0nzzT` uses the same source service/Manager binaries
to create a native-partition pool through the actual UI, browse it in Wanderer,
verify exact DOS bytes and export. `pnPEKHQY` cold-imports that exact pool
readonly, checks bytes and the saved icon, rejects writes, and exports without
changing source/reader hashes. Both independent offline zdb checks pass.
See `../ui/VERIFICATION-CREATE.md`. These remain private-overlay QEMU tests,
not stock-nightly or physical-media qualification. The immutable archive's
README predates this post-package verification entry; do not rewrite the
tested archive to add it.

The corrected `native-bundle.oufX6tvZ` passes the complete packaged-runtime
suite in `native-import-run.Xd9kLbP3`: manifest verification, frozen command
and service execution, all required CLI/native results, unchanged original
backing-disk hashes and runner exit zero. It contains `full-cli-split-dtl`.
Independent cold split-destination integrity remains separately established
by `dtHrKkdZ`; the packaged-runtime run does not add a GUI or stock-nightly test.

The earlier split/lifetime bundle `native-bundle.eEaer0tl` passed the complete
private QEMU suite in `native-import-run.aYA6Pq4L`. The runner verified the
manifest and executed its frozen service and upstream commands, including
mirror split with independent member readback, attach/replace/resilver, pool
growth/evacuation, startup import/mount and the existing CLI/native lifecycle
checks. Both original disk hashes match and the runner exited zero. This does
not add GUI visual coverage or stock-nightly compatibility.
It predates the upstream DTL split cleanup backport `6a48011`; after attaching
and resilvering a member, splitting can leave orphaned DTL space-map objects.
Do not treat that archive as the latest candidate. The corrected service is
`full-cli-split-dtl`, verified through native writer `VVl1U7bQ`, fresh reader
`dtHrKkdZ`, and an unchanged independent `zdb -bcc` verifier (exit zero).

The updated six-command bundle `native-bundle.4cPIqqNp` uses service
`full-import-guid`, CLI `cli-link.fCo2QjTj` and a separately rebuilt Manager.
`native-import-run.4nwdyhVR` verifies its manifest and exercises the frozen
packaged binaries through the full CLI/native suite, including actual startup
commands and explicit trusted RESTORE. The runner exits zero, backing hashes
match, and read-only mutation refusal passes. This verifies the packaged
service/commands, not unattended cold boot.

Native GUI run `native-gui-run.1DNtKH6K` separately tests a rebuilt Manager
verified byte-identical to the packaged executable. Actual clicks load the
record, enable it and Save for boot; `04-secure-save.png` shows success. After
the windows close, the independent harness verifies ENV/ENVARC values,
trusted ENV reload and protected system-owned ENVARC metadata. Runner exit
zero confirms both GUI PASS markers. No service or disks were attached in
this focused UI test; its unavailable-service message is intentional.

On 2026-09-26, `native-bundle.qIoDxpwo` built from `full-cli-reguid` and
`cli-link.owgVv1U6` passed the complete private QEMU lifecycle suite in
`native-import-run.eqYIvjtm`. The runner verified the bundle manifest before
copying its binaries, exited zero, and confirmed unchanged original raw-disk
hashes. Actual commands exercised creation, DOS reads/writes, mirror recovery,
scrub, checkpoints, GUID changes, detach, dataset/snapshot rename, export,
reimport, destruction and permission/read-only refusals. Manager was packaged
but not visually exercised by this run.

The first archive and repeated `native-bundle.YKTbeAiU.tar.xz` were byte-identical
with unchanged staging inputs. Fault-injected CLI build `cli-link.WyDr89Sm`
was refused before a staging directory was created. Archive checksums and the
full native serial log remain under `build-aros/`. These tests do not establish
stock-nightly compatibility, startup persistence or production readiness.
