# Native AROS integration patches

## Public service AHCI identification and mixed inventory acceptance

`public-service-run.c6uoUuzW` cold-boots the normal, non-test-mode service
`source-engine.P2gN7Bsv/replacement-production/OpenZFSServiceNative` with
overlay `gyxgtgia`. A fresh snapshot-protected 64 MiB GPT AHCI disk is
present alongside the existing ATA partition/blank/CD fixtures. Through
the real admission packet service and common UI/CLI client routines, root
gets the exact 36-byte SATA/QEMU HARDDISK inquiry and 67108864-byte/512-byte
geometry; an unprivileged caller receives EACCES with unchanged output.
Eight mixed-inventory iterations include AHCI unit 0 plus ATA units 0-2,
with populated model/serial/type, and the original malformed/absent and
permission tests pass. All public lifecycle and RAM mirror DOS checks pass.
The final client build `public-ahci-client3` has no compiler warnings.

The optional runner flag is `AROS_TEST_AHCI_QUERY=1` with
`AROS_TEST_REQUIRE_PARTITIONS=1`; it requires explicit AHCI pass markers.
Negative runs `LS2MCsZ6` and `qIZC0Rh8` first passed root inquiry but failed
an incorrect test expectation that the AHCI disk had fixed-disk type 2.
The actual inventory correctly reports solid-state type 3 because the AHCI
driver uses IDENTIFY DSM/TRIM capability. The assertion now uses the named
AROS solid-state constant; it was not loosened to accept arbitrary types.
This establishes the service/common-client API, not a new interactive
Manager screenshot, full upstream CLI command qualification, or physical
device support. ATA-only default behavior is unchanged.

## Large native AHCI resilver — repaired-member-only cold reads pass

`ahci-large-pool.90W3Zelk` extends the same real 3 TiB-partition mirror
through eleven isolated boots using `source-engine.P2gN7Bsv/ahci-resilver`
and the NCQ-ordering candidate overlay `gyxgtgia`. After each sole-member
writer/export and cold byte verification, a writable recovery boot combines
that written member with the original stale other member in separate COW
children. Upstream automatically resilvers the stale DTL; this does not
substitute a scrub or synthesize a successful scan state.

Both directions reach `POOL_SCAN_RESILVER` / `DSS_FINISHED`, report
1425408 processed bytes, zero scan errors and no remaining
`vdev_resilver_needed` ranges, then validate healthy root state and export.
Each following read-only OS boot physically omits the original writer and
reads all 2097152 payload bytes using only the repaired member. The existing
DOS fixture checks also pass. Backend read-only writes are zero; repaired
children, writer/backing qcow2 hashes remain unchanged during read-only
verification, and the original creation baseline hashes pass at the end.

Enable `AROS_TEST_LARGE_RESILVER=1` together with
`AROS_TEST_LARGE_DEGRADED_WRITE=1` and the matching importer binary.
This proves virtual native-AHCI large-capacity mirror repair/persistence,
not allocation across the full 3 TiB, physical hotplug/power loss, interrupted
resilver, RAIDZ/dRAID on these large partitions, or GUI-driven recovery.

## Large native AHCI degraded writes — cold persistence on either member

Run `ahci-large-pool.3vlYzXo0` passes seven new boots over hash-checked,
backing-only fixture `nOndXVqx` (whose creation is separately qualified):
healthy read-only import, either sole member read-only, then independent
member-0/member-1 writable degraded imports and cold verification of each.
Importer `source-engine.P2gN7Bsv/ahci-degraded4` uses actual upstream ZPL
create/write/sync with retained writable native partition bindings. Each
writer creates a separate `degraded-marker` dataset and a deterministic
2 MiB file, exports cleanly, and records 61 native backend flushes.
Each next OS boot compares all 2097152 bytes and exact file length, while
the usual DOS file tests still pass. Read-only backend writes remain zero;
writer, read-only-child, and original backing qcow2 hashes stay unchanged
during the respective read-only phases. This is orderly-export persistence,
not crash replay, large-pool resilver or physical power-loss durability.

Enable with `AROS_TEST_LARGE_DEGRADED_WRITE=1` and select the matching
binary directory. `AROS_TEST_LARGE_POOL_BASELINE` optionally reuses a
qualified `build-aros/ahci-large-pool.*` creation through new COW children;
it verifies the creation marker/config/hash and rechecks original hashes.
It never attaches the supplied baseline as the writable disk. Without it,
the existing fresh-GPT/create path is unchanged.

Negative harness iterations remain registered: `0pceAGuw` ran the
globally-readonly atime rejection assertion in a writable pool; `qvD359g5`
then exposed the equivalent writable-mount rejection assertion. Both now
remain in every read-only boot, but not the intentionally writable phase.
`m1JyAgzH` wrote/exported successfully, then failed the existing DOS exact
file-list assertion because the marker was in its dataset. Isolating the
marker dataset preserves that original enumeration test rather than
weakening it. Native builds retain known pthread macro redefinition warnings.

## NCQ acknowledgement ordering candidate — five cold adapter boots pass

`ahci-completion-ack.patch` moves acknowledgement of observed SDBS/IPMS
before the CI/SACT snapshot and removes the later SDBS clears. Previously,
a completion between the snapshot and acknowledgement could be discarded
while software still retained the old active bit. The QEMU controller
clears SACT before raising SDBS (`hw/ide/ahci.c:ahci_write_fis_sdb`).
This matches, but does not alone prove, the timeout captured below.

Build with `AROS_AHCI_COMPLETION_ACK=1`; the patch remains opt-in pending
broader qualification. Overlay `gyxgtgia` includes timeout-only diagnostics
and builds without warnings. Normal cold runs `qySJj9Ms`, `DnBkicXi`,
`2qe2YAen`, and `hAC8YPOZ` pass; injected flush EIO/retry run `3iWoKnDK`
also passes. Each completes three workers x 64 write/flush/readback rounds,
plus capacity, identification, high-offset, alias and rejection checks.
These are 960 concurrent rounds, not a claim of physical/SMP qualification
or proof that every intermittent failure is resolved. The large upstream
pool runner now accepts `AROS_TEST_AHCI_OVERLAY` for independent regression
testing; its existing qualified default is unchanged.

The real upstream regression `ahci-large-pool.nOndXVqx` also passes with
this exact overlay: create/export the two-3-TiB AHCI mirror, healthy cold
read-only DOS import, then separate cold boots with only member 0 or only
member 1 physically attached. The runner requires zero read-only backend
writes and unchanged child/backing qcow2 hashes. All four boots pass, and
none emits the timeout diagnostic. This extends the candidate evidence
beyond adapter stress; it does not cover large-pool degraded writes,
resilver, physical hardware, or exhaustion of all race schedules.

## AHCI concurrent read timeout — reproduced without fault injection

Normal run `ahci-capacity-run.vd0oWixk` fails workers 0/1 after 37/20
rounds; native NSCMD_TD_READ64 returns -6 (IOERR_UNITBUSY), actual 0,
expected 4096. Thus injected flush failure is not a necessary trigger.
Injected repeat `ROWicxza` passes all 64 rounds per worker; it does not
resolve the normal-run failure.

Set `AROS_AHCI_TIMEOUT_DIAGNOSTIC=1` when building the private overlay to
add `ahci-timeout-diagnostic.patch`. It only records controller state on
timeout, without changing scheduling, retries or timeout durations. It is
not included in default overlays. Initial attempts `emnxthxz` (hunk
rejected) and `ag5hlhpm` (missing debug declaration) failed before boot;
their build logs are retained. Final overlay `xqeoesmu` builds cleanly and
changes only ahci.device in the package.

Cold normal run `YfcHWAPe` reproduces worker 1 failure at round 36:
slot=7, state=5, opcode=60 (NCQ read), flags=00000341,
software sactive=00000080 but controller CI=0 and SACT=0,
IS=00000002, TFD=00000050, CMD=0000c017, serial=14.
The device subsequently returns -6/actual 0 to OpenZFS. This establishes
the timeout path rather than a short-success or comparison mismatch.
It suggests completion handling must be investigated; it does not prove
the root cause or justify silently retrying/accepting the request. Raw
serial, blockstats and input hashes are retained in each run; compact
`evidence/` directories exclude the sparse disks.

## Native Manager AHCI inspection (2026-09-30)

### Inspected partition to pool creation draft

The Devices window now offers **Create draft** after a successful partition
inspection. It shares the bounded selected-partition path builder with Add to
import and tracks that action's invalidation/disabled state. It fills only
the creation editor's path field, preserving the saved layout; it does not
invent a group, add a device, assert blankness, or submit any storage operation.
The user must select/add a group, add the path, and use the ordinary validated
preview/confirmation flow. Service-side path/extent/ownership checks remain
authoritative when a request is eventually submitted.

Native run `native-gui-run.e9Ilo5BV` verifies initial disabled state, explicit
missing-row refusal and exact `/AROS/ahci.device/0/partition/0` handoff.
It also exposed clipped helper text, retained in `03-create-draft-path.png`.
The shorter-text rebuild `ui-partition-draft2-20260930` passes fresh run
`native-gui-run.Leuip3Hu`: `01-create-draft.png` fits, then explicit Add group /
Add device / Use draft produces the typed pool.create preview for
`gui_draft_only` with that exact native path (`03-create-preview.png`).
The existing import handoff still populates its readonly-default draft
(`04-import-draft-regression.png`). No create/import submission was made by
these UI actions. Both runs close normally and pass the public-service
export/reimport and RAM mirror DOS checks, with original disk hashes intact.
These are draft-wiring tests, not a newly created AHCI pool qualification.

Hosted compatibility run `hosted-security-test.Otp1uOCd` uses the separately
rebuilt Manager from `hosted-inventory-20260930` against the matching hosted SDK
and pthread archive `pthread-consistent.lvaVzQRp`. Its build log is empty.
The isolated Xvfb AROS window shows the connected production service and the
new Devices controls. Scan returns ENODEV and leaves Use device disabled;
the native storage inventory could not instantiate its HIDD storage root in
this runtime. This is an unavailable inventory capability, not zero discovered
devices and not proof of a service failure. The exact named-window capture is
`03-discovery-result.png`. No native disk discovery is claimed for hosted.

Closing both windows normally permits export/read-only reimport and
`RESULT public service storage PASS`; the runner exits zero and verifies its
runtime input hashes. `hosted-inventory-20260930-run.log` retains the complete
runner result. This RAM-backed hosted test does not prove persistent-disk cold
recovery. It used a new private runtime and X server, never the user's session.

Native worker lifetime regression `public-service-run.192CVfrI` passes four
invocations of `inventory-worker-test.c` plus the real service's AHCI/ATA
inventory, partition, authorization, lifecycle and RAM mirror DOS tests.
The test owns a private Exec endpoint and withholds each reply to prove
EAGAIN polling and EBUSY destruction while the request is outstanding.
It then checks valid results, EACCES, malformed count, unterminated device
name, and absent endpoint. Failed results preserve the caller's output;
completed workers join and free. These synthetic endpoint cases prove worker
ownership/error handling, not service authorization or physical discovery.
The real service checks remain separate in the same guest. Enable this test
in a client build with `OZ_TEST_INVENTORY_WORKER`, linking admission-worker.c,
inventory-worker-test.c and the matching private pthread library; require its
marker with `AROS_TEST_REQUIRE_INVENTORY_WORKER=1`. Final native build log
`inventory-worker-native-20260930/build.log` is empty; the initial indentation
warning remains in `build-initial-warning.log` rather than being discarded.

The subsequent `native-gui-run.5YSIXAMy` verifies the newly wired **Scan
devices** selector against the production service. It reports four real HIDD
units. Selecting AHCI unit 0 and pressing Use device / Inspect displays its
GPT partition (`03-discovered-ahci-inspection.png`). Cycling to ATA unit 0
clears the previous results and disables GPT/import actions until inspection;
its next inspection displays two MBR partitions
(`05-discovered-ata-inspection.png`). A second scan followed by normal window
closure completes successfully; the runner then passes export/read-only
reimport and the RAM mirror DOS round trip, with original disk hashes intact.

Inventory uses the existing root-authorized API from a joinable worker, never
MUI from the worker thread. Process exit drains the worker. Scanning grants
no write authority, and selection does not automatically import or format.
The native build log `ui-inventory-20260930-build.log` is empty (successful).
GUI denied response rendering and deliberately delayed worker shutdown are
not established by this run; API denial tests are separate.

Absent-service follow-up `native-gui-run.24TUaDfZ` correctly disabled Use
device and returned to Wanderer after closing, but displayed a vague ENOENT
message. `native-gui-run.N2zGqtdZ` tests the rebuilt UI's explicit service
guidance: "Device scan unavailable (2). Check that OpenZFSService is running."
Its screenshot shows no selectable results or enabled stale mutation actions.
These editor-only guests intentionally start no service. They do not prove
successful inventory, network sharing, or a permission-denied scenario.

Private GUI run `native-gui-run.YmCzQTja` visually verifies the Devices
read-only inspection against `ahci.device` unit 0. Capture
`07-ahci-result.png` shows GPT, one partition, 512-byte sectors, and partition
sectors 2048..131038 (66043392 bytes). This is manual device-name inspection,
not automatic GUI inventory. The API inventory/inquiry tests are separate.

After closing Devices and Manager normally, the public service runner passed
its export/read-only reimport and RAM mirror DOS round-trip checks. Original
fixture disk hashes remained unchanged. Screenshots, input actions, binary
hashes and serial logs are retained in that run; its compact `evidence/`
directory is registered to the development test archive. No physical disk, DUT, user hosted session
or network server was accessed. No GPT write was requested through the GUI.

## ahci-inquiry.patch — native identification verified; intermittent I/O retained

`ahci-capacity-run.OuzTyS1A` reproduces the actual OpenZFS
`oz_device_inquiry` failure (EIO, zero response length). The original AHCI
standard INQUIRY used the CDB as its reply buffer. Its VPD helper also left
reserved bytes uninitialized and used word-pointer arithmetic when clearing
the response tail. The private patch replaces both with one bounded reply
builder for standard INQUIRY, supported-page list and unit serial number.
It preserves the CDB, honors allocation length, initializes returned bytes,
sets actual lengths, rejects unsupported/malformed requests and leaves bytes
beyond the transferred reply untouched. The removable bit now uses the
IDENTIFY removable flag, not a truth test of the entire configuration word.

Overlay `ahci-capacity.ajxoahp2` builds without warnings. The read-only
diagnostic covers fourteen standard/VPD/short/zero/invalid requests on each
disk, including response guard bytes and unchanged CDBs. The real OpenZFS
adapter returns a 36-byte SATA/QEMU HARDDISK identity. The first generated
patch mistakenly omitted the old helper removal; compilation rejected it
in `ahci-capacity.6dqd1f2j`, before any boot. Its log is retained.

Do not treat all combined runs as passes: `ahci-capacity-run.yRsrkesL`
passes identification but fails concurrent worker 0 after three rounds with
a native read-request EIO. The first diagnostic did not distinguish native
error from short completion. `OZ_DEVICE_IO_DIAGNOSTIC` now logs that detail
only in the private adapter executable; it changes no production behavior.
Repeat `aaPo4m0e` passes eight rounds per worker, and stronger `87M6UIjN`
passes 64 rounds per worker plus the injected flush EIO/retry and every
identification/capacity check. **The intermittent concurrent failure remains
unresolved**; passing repeats do not erase it. The runner now requires all
three 64-round markers and at least 196 successful backend flushes.

Evidence, including the negative run, is registered to the development test archive. This is
native adapter/device proof, not a fresh authenticated CLI/GUI inventory
acceptance or physical-device qualification. The prior ordinary INQUIRY
limitation below describes the older capacity-only overlay.

## Native 3 TiB AHCI mirror — create and cold DOS import verified

`build-aros/ahci-large-pool.iVnPYSA2` completes both boots of
`aros/engine/test-qemu-large-ahci-pool.sh`. Each AHCI disk is exactly
3298534883328 bytes; each real GPT ZFS partition is 3298532786176 bytes.
The diagnostic uses explicit retained partition bindings, not a widened
production raw-device fallback. It creates an actual upstream mirror,
runs the existing native DOS/ZPL file and metadata tests, exports the
configuration, and cold-boots a separately linked readonly import test.

Cold import reports healthy state 7 and 3264175144960 usable pool bytes
(explicitly required to exceed 2 TiB). Persisted file bytes, vnode writes,
atime, sparse64 and repeated DOS reads pass. QEMU records zero writes on
both cold members. SHA256 checks prove both created qcow2 images and both
readonly child images are unchanged by the cold boot. Creation records
7993 write operations and 638 flush operations per mirror member. The
qcow2 images consume about 58 MiB each, not their 3 TiB logical size.

Native build outputs are in `source-engine.P2gN7Bsv/ahci-large`; build logs
are `ahci-large-{pool,import}-build.log`. The runtime uses the qualified
private DOS/POSIX/GPT overlays and AHCI overlay `ahci-capacity.nwbhsddj`.
Raw sparse backing files remain private, and the runner hashes compact
qcow2 images rather than scanning terabytes of holes. Its complete log and
source snapshots are retained, with a compact evidence set registered to
the development test archive. Neither the DUT nor the user's hosted session was touched.

The expanded four-boot suite `ahci-large-pool.ipYytRzP` additionally passes
either sole surviving member. Each independent cold boot omits the other
AHCI port entirely and binds its exact native path as unavailable. Both
require DEGRADED state 6 (not merely successful import), retain the same
3264175144960 usable bytes, pass the persisted file/metadata and DOS handler
tests, record zero device writes, and preserve both original qcow2 hashes
and the fresh survivor child hash. The import diagnostic's error path also
releases any previously established bindings. Rebuilt import binaries are
in `source-engine.P2gN7Bsv/ahci-survivors`, alongside the byte-copied qualified
creation binary; the runner now defaults to that directory. Build log:
`ahci-survivors-build.log`. Raw/normalized serial and backend stats for all
four boots are retained in the registered the development test archive evidence set.

This proves the native upstream engine and DOS handler on a large virtual
mirror and either missing-member case, not physical durability, GUI-driven
creation, degraded writes/resilver at this size, full-disk allocation, or a
release-ready port.

## ahci-update-flush.patch — native QEMU backend flush verified

`ahci-capacity-run.VwaCC7J8` establishes the original false-success case:
four successful native `oz_device_flush` calls, four backend writes and
zero backend flushes. The runner now independently checks QEMU blockstats;
a guest PASS alone cannot satisfy the flush test.

CMD_UPDATE now submits SYNCHRONIZE CACHE through the existing asynchronous
AHCI completion/error path. IDENTIFY word 83 validity and flush support are
checked; FLUSH CACHE EXT is preferred, otherwise FLUSH CACHE is used.
Unsupported devices/ATAPI are rejected instead of receiving fictitious
success. The request stays pending until completion or timeout (45 seconds).
Flush uses ATA_F_EXCLUSIVE. The non-NCQ issue-loop exclusivity condition was
reversed; the patch corrects it to stop dispatch after an exclusive command.
The private builder therefore also rebuilds ahci.c using its release
dependency closure. Only ahci.device changes in the boot package.

Initial flush overlay `4g513xf2` passes in `ahci-capacity-run.TWcFaQtK`.
Final warning-free overlay `ahci-capacity.nwbhsddj`, including exclusivity
and the issue-loop correction, passes in `ahci-capacity-run.ZoIaXnVz`:
four writes and **four backend flush operations**, with all high-offset,
alias, capacity and boundary checks passing. Failed and successful evidence
is retained and registered to the development test archive. All targets are private QEMU snapshots.

The subsequent fault/concurrency extension is verified in
`ahci-capacity-run.jtdL2i8g`: QEMU blkdebug injects one real backend EIO on
flush (not a synthetic adapter return). `oz_device_flush` returns EIO, its
retry succeeds, and three pthread workers each complete eight independent
high-offset write/flush/readback rounds. QEMU reports 28 successful writes
and 28 successful flush operations. The runner requires all three worker
markers, the explicit error and retry markers, and backend flush counts.
Raw serial bytes are retained alongside a copy removing only complete,
identified security task notifications that can interrupt result lines.
Preliminary passing runs `Rpj6BIq7` (failure/retry) and `ljdwicrx`
(concurrency before stricter marker checking) are retained as well.

Enable the injection with `AROS_TEST_DEVICE_IO=1 AROS_TEST_FLUSH_ERROR=1`.
The graph is raw -> blkdebug -> private qcow2 -> fresh sparse raw backing;
the fault targets flush only and the guest uses report-not-stop error policy.
This verifies error propagation, recovery and bounded concurrent readback,
not an exhaustive trace proof of every queued barrier ordering. Unsupported
or base-command-only devices, power-loss recovery and physical durability
remain unverified.
No physical disks, shared AROS sources, installed drivers or DUT were changed.

## ahci-query-lifetime.patch — native high-offset adapter verified

The supported-command table in AHCI BeginIO was automatic storage, but
NSCMD_DEVICEQUERY returned its address for callers to traverse after return.
With the capacity patch, `ahci-capacity-run.Zj1MA3fE` opened the full 3 TiB
extent but rejected all high-offset I/O with EOVERFLOW: the adapter could not
find usable 64-bit commands. Making this immutable table static gives the
returned pointer the required device lifetime. No fallback to truncated I/O
or relaxation of the adapter's bounds/admission checks was introduced.

Private overlay `ahci-capacity.c8jieyrg` passes the actual `device-io.c`
adapter test in `ahci-capacity-run.DNmdHiaP`. Snapshot-only write/readback
checks cover above 4 GiB, a transfer crossing 2 TiB, above 2 TiB, and near
the end of the 3 TiB disk. The corresponding truncated 32-bit offsets retain
zero data, distinguishing correct high-offset access from aliasing. Bounds,
overflow, alignment and readonly-write checks return the expected errors.
Capacity and twenty malformed/truncated capacity-request cases still pass.

Enable with `AROS_TEST_DEVICE_IO=1 bash aros/engine/test-qemu-ahci-capacity.sh`.
This links the real native adapter and qualified private pthread archive,
not a mock transport. All disk targets are newly created private files,
attached via QEMU snapshots. It uses a ZFS-type GPT partition and qualified
private GPT/DOS/POSIX prerequisites. The initial blank-disk experiment
`ahci-capacity-run.SWiPn158` correctly failed EBUSY because AFS had auto-claimed
the disk; that failure is retained, not bypassed. Test offsets avoid GPT
metadata. These checks do not prove flush durability, cold persistence or
large-pool import. The CMD_UPDATE no-op found at this stage is addressed by
the separately verified flush patch above, not by the lifetime patch itself.

## ahci-scsi-capacity.patch — private 3 TiB QEMU capacity verified

The geometry-only overlay fails the native read-only diagnostic in
`build-aros/ahci-capacity-run.uuFq6I5I`: READ CAPACITY(10) modifies the CDB,
leaves the response sentinel untouched and reports zero actual lengths;
READ CAPACITY(16) returns IOERR_NOCMD. Both 512 MiB and 3 TiB disks reproduce
this. The original response pointer/length incorrectly refer to the CDB.

The new private patch handles capacity commands before allocating an ATA
transfer. It returns the IDENTIFY capacity minus one and logical sector
size in the response buffer, saturates RC10 last LBA at 0xffffffff, supplies
RC16's full 64-bit last LBA, and sets response/command actual lengths.
It supports whole-device queries only; nonzero LBA/PMI, invalid service
action, malformed command length, write direction, missing response buffer,
and insufficient response space are rejected. RC16 allocation truncation
and zero allocation are supported. Other SCSI operations are unchanged,
including the separate, still-unfixed ordinary INQUIRY response path.

`build-ahci-overlay.py` now privately rebuilds both ahci_io and ahci_scsi,
retaining both release dependency closures and the previously qualified
kernel flags. Overlay `ahci-capacity.4tzcs19n` changes only ahci.device in
the ordered boot package. `ahci-capacity-run.iHEP075A` passes basic capacity
checks; `ahci-capacity-run.6QM1tL0d` additionally passes ten boundary cases
on each disk. Both cold boots return last LBA 6442450943 for the 3 TiB disk,
512-byte sectors, unchanged CDBs and intact response-buffer guards. The
expanded diagnostic compiles without warnings. Logs are retained and linked
to the development test archive; evidence subdirectories exclude the multi-terabyte sparse files.

The runner only creates fresh sparse backing files and attaches snapshot
overlays. Both backing files retain zero allocated blocks. It does not read
or write real disks, touch the DUT, or use the user's hosted session.
This is capacity-command verification, not yet end-to-end OpenZFS creation,
high-offset I/O or cold import on a pool larger than 2 TiB. Native 4K sectors,
physical durability and general SCSI emulation remain unverified.

## ahci-exact-capacity.patch — private native RAIDZ3 fixture verified

`native-raidz-run.091MkgUE` confirms that the original AHCI geometry reports
536868864 bytes for a 536870912-byte disk: CHS multiplication drops four tail
sectors. The native OpenZFS fixture correctly refuses that size mismatch
before pool creation. The patch uses the IDENTIFY-derived sector count and
saturates only the 32-bit geometry field; it does not relax partition bounds.

`build-ahci-overlay.py` patches a private source copy, preserves the ordered
release link inputs and replaces only ahci.device in the qualified ATA BSP.
The shared AROS sources, generated release tree and installed drivers remain
unchanged. Failed candidates `native-raidz-run.q5NONUhR` (boot page fault) and
`BKbcIeRs` (stopped before fixture) are retained. Fixing the generated-header
include alone produced identical bytes and did not explain the failure. The
final recipe includes the release kernel's general-register-only and
AROS_USE_LOGRES flags; the latter changes AHCIBase layout.

`ahci-capacity.sm1dd2fx` passes `native-raidz-run.BHWwaqb0`: exact raw capacity,
GPT discovery, blank-label checks, upstream four-device RAIDZ3 creation and
DOS access, with unchanged partition exteriors. Three members use ata.device
and one uses ahci.device. Cold/degraded recovery is separate ongoing coverage.
This does not establish physical durability or large-disk READ CAPACITY(16)
support in AHCI. Logs and failed attempts are registered to the development test archive.

Normal-service follow-up `native-import-run.4lm2wkub` passes authenticated
RAIDZ3 DOS writes, flush, export/reimport and namespace-property persistence.
Independent cold runs `ZsIkOWNI` (healthy), `9sdXpGoy` (only ATA member 0) and
`DAVIg1QO` (only AHCI member 0) pass the saved payload and namespace checks.
The latter two each omit three of four members and report DEGRADED. Every
raw member, saved writer overlay and readonly child overlay retains its hash.
This covers these selected native RAIDZ3 recovery cases, not every missing
subset, interrupted resilver or physical power-loss behavior.

## diskinfo-large-volume.patch — isolated hosted desktop verified

The 50 TB-class mounted pool exposed a distinct consumer arithmetic bug:
DiskInfo evaluated `100 * blocks` as 32-bit ULONG. It displayed 1% free for
1,737,661,391 free blocks out of 1,737,661,400. Widening before multiplication
preserves its normal integer truncation (99%) without altering DOS capacity
accounting. Its static ID table now names `0x5a465301` OpenZFS, distinct from
the private AROS ZFS v3 format. This is separate from the handler's fixed
null startup BSTR crash (commit 571396e).

`aros/engine/build-diskinfo-overlay.sh` requires explicit source, generated
catalog-header and SDK paths, patches a new private copy, and compiles both
DiskInfo and a nine-case test linked against its actual formatter. No shared
source or installed desktop binary changes. Hosted output is
`diskinfo-overlay.utOxDN1y`; native-SMP output `diskinfo-overlay.qPyrl0ht`
is compile-verified only. Initial build `diskinfo-overlay.2pxlvuWi` failed
because the standalone command omitted ADATE; its log is retained.

The hosted runner accepts `AROS_TEST_DISKINFO` and optional
`AROS_TEST_DISKINFO_CHECK` only with its isolated array50 GUI mode. It stages
and hashes the explicit binaries. `hosted-large-pools.jTl31noa` passes all
nine formatter boundaries, actual DiskInfo visual inspection (OpenZFS, 51.8T,
99% free), DOS reads, readonly refusal, detach and unchanged sparse members.
`01-info-patched.png` retains the native window; the development test archive evidence registered.
Transcript: `hosted-array50-diskinfo-ui-2.log`.

Preserved earlier `hosted-large-pools.drbavCBl` has correct UI and disk hashes
but FAILED the runner tail because its source was edited while Bash was still
reading it. It is not a whole-run PASS. The runner now executes an immutable
private script snapshot to prevent this development-time harness failure.

This validates a private consumer integration candidate, not a stock-nightly
deployment, remote share or physical-array durability.

## security-access-volume-lifetime.patch — native regression verified

The MuFS lifecycle audit found that secAccess_Control selects a secVolume
under VolumesSem, releases the semaphore, then passes the borrowed record to
IsAllowed. FreeVolumes can free native volume records under that semaphore
during asynchronous rescans. The candidate keeps the shared lock through the
policy call. IsAllowed and secGetRelationshipA were inspected: neither takes
VolumesSem or sends filesystem packets. The non-filesystem-context path is
unchanged. This protects record lifetime; it does not implement volume
registration/readiness, synchronize every global security setting, or prove
the complete rescan/detach lifecycle safe.

`aros/engine/build-security-overlay.py` applies this patch to a private copy,
compiles with the native x86_64 SMP SDK, links the library using the ordered
release object inputs, and composes the tested ATA BSP overlay. It verifies
that only security.library changed among 33 entries. Build evidence lives in
`build-aros/security-overlay/run.4f9igojm`; the command transcript is
`build-aros/security-overlay/build-native.log`. No shared library/source,
release ISO, DUT or user hosted session was modified.

`build-aros/qemu-run.pLXIlN9n` passed all five native boots with this BSP and
the same on-disk security.library: creation/authenticated DOS operations,
cold readonly mirror, either sole member, and cold writable reopen. The
existing security packet/ownership checks and MuFS capability queries pass;
readonly boots wrote zero bytes, original disk hashes match, ATA flushes
were observed, and independent zdb checks report no leaks. Full transcript:
`build-aros/security-overlay/native-regression.log`.

This verifies compatibility of the patched resident library with the current
native integration suite. It does NOT exercise registered OpenZFS volume
records during rescan: discovery, readiness, quarantine and detach remain
unimplemented, and the lifetime fix itself is supported by source/locking
analysis rather than a forced race reproduction. Do not claim complete MuFS
volume integration or general SMP safety from this regression.

## dos-fs-open-port.patch — isolated QEMU runtime verified

The native OpenZFS cross-volume symlink test reproduced ERROR_INVALID_LOCK
when opening an OpenZFS link to a RAM file. Retained runs:
`build-aros/qemu-run.PItYPwX7/` and `build-aros/qemu-run.vGw4f9Tv/`.
The second logs `Symlink boundary failed stage=3 ioerr=211`: creation of the
guest RAM target succeeds, but access through the link fails.

Source trace: AROS rom/dos/fs_driver.c fs_Open assigns fh_Type even when
the handler returns ERROR_IS_SOFT_LINK. rom/dos/open.c recursively calls
InternalOpen with the same FileHandle. A target handler that leaves fh_Type
alone inherits the previous filesystem's port, so subsequent Read/Close are
sent to the wrong filesystem. This candidate clears the port for each attempt
and retains the handler's explicit redirect only on success.

The isolated DOS module/package overlay passes all four strict boots in
`build-aros/qemu-run.RWWmsPqm/`: create, cold mirror, and each sole member.
Every boot reports `Native DOS symlink cross-volume/bounds ... result=0`.
Cold imports write zero bytes and both original member hashes remain intact.
This verifies the port-routing fix, not a completed OpenZFS port.

No shared source, DOS binary, release ISO, DUT or user hosted session was
changed. A make dry-run unexpectedly refreshed `fs_driver.d` and attempted
a host genmodule rebuild (failed without replacing its executable). The
failed discovery logs are retained; actual overlay builds replay direct
compiler/linker commands with private output paths only.

Build routing found: native target `kernel-dos`, generated objects under
aros-amiwest-nightly-release/bin/pc-x86_64-smp/gen/rom/dos/dos; generated module
settings are Makefile.doslibrary. dos.library is included in the boot
aros-base.pkg.xz as well as Libs, so replacing only the on-disk library is
not a valid reboot test. AROS/tools/package/pkg creates the module package.
The release build source is `AROS-amiwest-nightly-src`, not the live AROS
checkout. Overlay source/object/library/package and input hashes are retained
under `build-aros/dos-overlay/`. Build procedure is retained in
`build-aros/build-dos-overlay.py`; logs are `dos-overlay-build-v3.log` and
`dos-overlay-qemu-v2.log`. Package comparison confirms that only dos.library
differs; all other module names, order and payloads are preserved.

Run with `AROS_TEST_DOS_OVERLAY="$PWD/build-aros/dos-overlay"
AROS_TEST_SECURITY=1 bash aros/engine/test-qemu-mirror.sh` (one shell command).
The optional overlay maps both boot package and on-disk library directly
onto the original ISO and records their hashes in each evidence directory.
Without the overlay, the cross-volume regression intentionally still fails.

Retained packaging failure: WHOpXkFI never booted because repacking an
intermediate ISO encountered stale protective partition geometry (xorriso
SORRY 32). Direct overlay avoids that intermediate ISO. Upstream's Python 3
pkg extractor also fails on padded byte filenames; the private builder uses
its existing reader with decoded/validated basenames, and its normal writer.

## ata-update-flush.patch — isolated QEMU runtime verified

The release ATA CMD_UPDATE handler previously returned success without a
device command. OpenZFS native fsync reaches CMD_UPDATE through
oz_device_flush, so successful calls did not prove stable-storage completion.
Prior run vkJEpNv3 recorded zero flush operations on both writing disks.

This patch executes FLUSH CACHE EXT when IDENTIFY word 83 advertises it,
otherwise FLUSH CACHE when supported. The validity bits are checked, matching
the bit definitions in Linux include/linux/ata.h. Invalid/unsupported devices
and ATAPI return IOERR_NOCMD rather than fictitious success. ATAPI needs its
own SYNCHRONIZE CACHE implementation and is not claimed supported here.
The native no-data command path supplies completion, timeout and ATA status
propagation. CMD_UPDATE=4 is not in the immediate-command mask, so execution
is ordered on the existing bus task, not an unsynchronized direct I/O path.

Private sources, objects, module, command transcript and build script are in
build-aros/ata-overlay. Shared source and release binaries were not modified.
The dry-run command extraction explicitly marked the two dependency files
old to prevent their implicit makefile rebuild. The boot package comparison
found only ata.device changed among 33 ordered entries in aros-bsp.pkg.xz.

Run 88qtOZBN passes the five-boot suite with both the DOS and ATA overlays:
create, readonly mirror, either sole member, cold writable reopen/export.
QEMU recorded 373 flush operations per disk in create and 26 per disk in
reopen. Readonly boots still wrote zero bytes; original backing hashes stayed
unchanged; Linux zdb -bcc reported No leaks on original and writer copies.
Logs: build-aros/ata-overlay/build.log and build-aros/ata-flush-qemu.log.
The harness enables this with AROS_TEST_ATA_OVERLAY=$PWD/build-aros/ata-overlay
and then requires positive flush counts on both disks in both writer boots.
It still also needs AROS_TEST_DOS_OVERLAY for cross-volume symlink acceptance.

This demonstrates end-to-end virtual-device flush submission, not complete
power-loss recovery or physical-drive durability. Dirty intent-log replay,
flush failure injection, base (non-EXT) selection, and ATAPI are unverified.

## Rejected standalone ATA command/DMA ordering fix

`ata-command-before-dma.patch` is a retained experiment, NOT a verified fix
and NOT automatically applied. It prepares the DMA state, issues the ATA
command, then starts DMA (compare Linux libata-sff.c ata_bmdma_setup/start).
Private `build-aros/ata-dma-order.KoBDjV0m` also retains timeout-only logging.
The four-CPU alternating-read suite passed in `public-service-run.io14m9Ao`
but failed in `public-service-run.sXeoTl3W` at restored-root cycle 18 on blank
unit 1. Both used the same binaries. Thus ordering alone does not fix this
reproduced problem; no shared AROS tree or DUT was changed.

Control diagnostic overlay `ata-timeout.mz8YScKB` reproduced the same failure
in `public-service-run.ifsMIkz4`, cycle 24. At timeout: command c8 (DMA read),
LBA 32, 224 sectors/114688 bytes, ATA status 58, DMA completion handler still
installed, no pending completion signal. All writes remain prohibited to the
label fixture. Original reproducer `public-service-run.wZsTf2sa` failed at
cycle 7 without low-level instrumentation. The next private overlay
`ata-pci-timeout.XkRlwLfa` restores original ordering and observes PCI DMA/PRD
state on timeout. Its PCI interface cast is QEMU-only, not production code.

PCI diagnostic run `public-service-run.XxrNa2hX` failed at first-root cycle
15 on the same blank unit read. DMA command=09, status=01 (active, no error or
IRQ), descriptor register/pointer=0165f000, two PRDs totaling 114688 bytes with
EOT set; ATA status remained 58. This narrows the failure beyond a missing
task wakeup but does not establish whether guest or virtual-controller state
causes it. No automatic retries or polling-based completion workaround were
added. The original ATA ordering is retained in this diagnostic overlay.
