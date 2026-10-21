# Authenticated native pool expansion

Operation 9 takes a pool name and a typed topology of NEW groups only. It calls
upstream spa_vdev_add with ashift compatibility checking enabled. Unlike create,
add validation permits auxiliary-only layouts (log/cache/spare/special/dedup)
without inventing a data group. Existing role/layout constraints still apply.
Data groups can use stripe, mirror or RAIDZ1/2/3. API3 also represents dRAID
geometry and builds upstream dRAID addition nvlists. The existing pool must
enable feature@draid. The dedicated runtime evidence below covers adding a
second group for dRAID1, dRAID2 and dRAID3.
Add is not attach: it adds top-level allocation groups,
not replicas to existing groups. Removing them later may not be supported.

Root/config authorization, confirmation, writable/nonsuspended pool and no
force/dry-run/recursive/options/arguments apply. Native file preflight is shared
with create/attach/replace: exact canonical files, identity checks, whole-file
blankness, minimum/aligned size and writable-open checks before mutation.
All files are preflighted before any new labels are written. No file is created
or truncated. Imported-pool path policy extends for every new canonical path;
on upstream failure, only newly added policy entries are removed. Dedicated
backing files without external writers are still required. Explicitly provisioned
native partitions now use the shared authenticated native admission path; see
ui/PROTOCOL.md (`native-import-run.ymkZ7IFj`) for tested native addition,
evacuation and exact native-only reimport. This does not authorize arbitrary
raw devices or exclude unrelated external raw writers.

Automatic add ashift inherits spa_max_ashift; explicit values go through
upstream compatibility checking. Allocation-class features are NOT silently
enabled for additions: upstream rejects a special/dedup group if its feature
is disabled. Enabling all features is not proof of complete AROS integration.

Native UI stores a separate add draft, opens Add devices - new groups only,
validates with the add validator, retains operation 9 on Apply, previews exact
paths/groups and uses authenticated submission. Create/import drafts remain
separate. Confirmation warns about adding groups and removal limitations.

## Evidence

### Cold dRAID expansion

`AROS_TEST_DRAID_EXPAND_PARITY=1|2|3` with `AROS_TEST_TYPED_DRAID=1` and
`AROS_TEST_DRAID_PERSIST=write|read` selects persistent expanded-pool testing.
The writer uses preallocated 128 MiB member files on a private 2 GiB FAT image,
creates a typed group, adds another, verifies new allocation and scrub, then
exports without destroying the pool. The reader copies a successful matching
writer fixture into a fresh guest, imports only the explicit members readonly,
checks exactly two groups/all paths/positive new-group allocation, reads the
exact changed file and 1 MiB payload, rejects writes, and exports. Host checks
compare every extracted member byte-for-byte, preserve the original writer
backing hash, and run independent zdb checksum/leak traversal after guest exit.
This is clean-export cold persistence, not sudden power loss or independent
physical disks. The member files share one FAT image.

- Parity 1: writer `native-import-run.H8S38sTI`, cold reader
  `native-import-run.2RvgwmNp`, six members. Both full native/CLI suites exit
  zero; offline zdb reports No leaks, all readonly member comparisons pass,
  original fixture/writer hashes match. The cold Manager API reports 1343488
  allocated bytes on the new group, matching the writer's readonly sample.
- Parity 2: writer `native-import-run.UXUVJ1Pb`, cold reader
  `native-import-run.HSLbgKvG`, eight members. Both full suites exit zero;
  all member comparisons and source hash checks pass, independent zdb reports
  No leaks. New-group allocation is 2162688 bytes in both readonly samples.
- Parity 3: writer `native-import-run.TagQdHIN`, cold reader
  `native-import-run.dUmQzH0k`, ten members. Both complete suites exit zero;
  readonly members remain byte-identical, fixture/writer hashes match and
  independent zdb reports No leaks. New-group allocation is 2785280 bytes in
  both readonly samples. All three parity levels now have separate writer and
  fresh-reader evidence; no abrupt-power-loss claim follows from these tests.

### Same-guest expansion

- `native-import-run.S8c80NE6` strengthens the three-parity expansion matrix
  with numeric Manager API allocation queries. The new group is identified by
  the parent of its exact member path, not display order. It must be the single
  matching ONLINE top-level dRAID row. Samples after add/sync and after later
  writes/scrub require increased allocation; readonly reimport must retain
  allocation above the initial baseline. Parity 1/2/3 measurements respectively:
  180224 -> 1638400 (readonly 1343488), 270336 -> 2236416 (readonly 2088960),
  and 360448 -> 2949120 (readonly 2785280) bytes. Export can change metadata
  allocation, so equality across export is not asserted. This proves use of
  each added group, not the location of every payload block or cold persistence.
  Full native/CLI regression exits zero; both original backing-image hashes
  match. All three allocation PASS markers are now mandatory runner gates.

- `native-import-run.smZaHC1L`: typed expansion matrix covers parity 1/2/3
  with two groups and respectively 6/8/10 private 128 MiB RAM members. Each
  group uses one data column and one distributed spare. Assertions require
  exactly two matching dRAID group names, every member path, increased size,
  existing data readback, subsequent writes, completed zero-repair/zero-error
  scrub, and exact changed bytes plus a 1 MiB payload after explicit readonly
  reimport. Readonly writes are refused; writable reimport, destroy and member
  cleanup pass. Reported pool sizes grow from 167772160 to 335544320,
  251535360 to 503070720, and 335544320 to 671088640 bytes respectively.
  This is same-guest lifecycle evidence, not cold persistence, proof of data
  allocation to each new group, parity2/3 spare activation or physical media.
  The complete native/CLI runner exits zero and both original fixture image
  hashes remain unchanged. The runner now requires all three expansion markers.

- `native-import-run.PjXS9BIi`: authenticated API3 creates one
  draid1:1d:3c:1s group, writes a 1 MiB DOS payload, then adds a second group
  through operation 9. Upstream status lists both groups and all six paths;
  size increases from 167772160 to 335544320 bytes. Existing data survives,
  subsequent writes and a zero-repair/zero-error scrub pass, and explicit
  six-member readonly reimport reads exact changed bytes and the 1 MiB payload
  while refusing writes. Writable reimport/destroy and cleanup pass. These
  are private RAM-backed files, not cold-persistence or physical-array tests.
  The complete CLI/native runner exits zero and original mirror backing hashes
  match. Service `full-draid-capabilities` also corrects stale greeting text:
  typed creation accepts explicitly admitted native partitions as well as files.
- Retained `3Ay2vYDU` reaches successful add and doubled capacity, but then
  times out in a reused resilver waiter. The pool is ONLINE with zero errors
  and no resilver record. Expansion changes allocation choices, so those writes
  do not establish that the offlined original group required repair. The add
  fixture now requires scrub completion; separate parity fixtures still require
  maximum-parity degraded writes and completed resilver. No production repair
  logic was weakened to pass this test.

- admission-service-run.bn3p3Z09: full five-layout lifecycle regression plus
  dedicated pool-add test pass. Create mirror, write native DOS before file,
  refuse a proposed second mirror with nonblank second file and verify the
  first proposed file remains entirely zero, then add that mirror after fixing
  the private fixture. Status shows four ONLINE files/two mirrors and increased
  capacity. Write after file, export, reimport RO and read both exact payloads.
  RO addition is refused; RW reimport/destruction and fixture cleanup succeed.
- admission-service-run.U8Rg4143: focused ADD mode passes the same expansion
  test plus explicit force/dry-run/recursive/options refusal and nonroot EPERM.
  Run with AROS_TEST_SUITE=add. This is not a replacement for the full suite.
- native-gui-run.5MNHrP6p: add-specific editor (01) and Apply/preview (02)
  retain operation pool.add, one new group/path and the expansion warning.
  Editor-only guest reports service unavailable and nothing submitted; this
  proves draft wiring, not a GUI-originated mutation.

Logs in build-aros: add-engine-build.log, add-client-build.log,
add-create-run.log, add-focused-client-build.log, add-focused-run.log,
add-manager-build.log. All runner source, inputs and serial evidence retained.

## Cold expansion and read-only allocation reporting

`AROS_TEST_COLD_ADD=1 AROS_TEST_COLD_API_CREATE=1` selects an isolated
two-boot mirror expansion test in `test-qemu-file-backing.sh`. Four blank
64 MiB files live on one private FAT image; these are not independent physical
drives. First boot creates a two-file mirror, writes hello.txt, adds the second
mirror while the dataset is mounted, checks increased capacity and writes an
8 MiB deterministic expanded.bin through the native DOS handler. Status must
show allocated space on the mirror owning disk2 (identified by parent identity,
not display ordering). This proves use of the group, not the exact placement
of every file block. The second guest imports all four explicit paths read-only
and checks topology, allocation and every payload byte. All four member images
must be byte-identical before/after read-only use; independent zdb checks the
exported pool's blocks and leak accounting.

- file-backing-run.Pf1To6l7: retained failed first attempt. Expansion and both
  native payload reads succeeded, but the fresh read-only guest reported zero
  allocated bytes and failed its allocation assertion. Upstream metaslab_init
  skips space-map opens for read-only libzpool unless spa_read_spacemaps is set.
- The management service now sets upstream spa_mode_readable_spacemaps before
  kernel initialization, as zdb does. This enables space-map/DTL reads, not
  writes; no write permissions or device-path policy are relaxed.
- file-backing-run.eZJbi0sA: rebuilt service passes both boots, increased
  capacity, four ONLINE leaves/two mirrors, new-group allocation, exact 8 MiB
  DOS readback, unchanged read-only members and independent zdb No leaks.
  Logs: typed-import-service/add-cold-stats-{engine-build,client-build,run}.log.
- file-backing-run.skywzKKf: regression with the same rebuilt service passes
  cold attach/replace, both original paths withheld, native degraded readback,
  unchanged read-only members and zdb verification of each new member alone.
  Log: typed-import-service/add-stats-degraded-run.log.

## Auxiliary additions and retained read-only configuration

The focused ADD client also provisions three extra private RAM files. It
rejects special and dedup additions with ENOTSUP while allocation_classes is
disabled and verifies every byte of the proposed file remains zero. It then
adds a log, cache and spare separately, checks roles, writes native DOS data,
exports, imports read-only, reads both files, and imports writable again before
destroying the test pool. This tests configuration lifecycle, not cache hit
rate, automatic spare replacement, log replay or enabled allocation classes.

- admission-service-run.hLlwhwqf and M9dTM9nh retain the initial failure:
  additions succeed but cache/spare rows disappear after read-only import.
  JA3kIy9a retains the failed reporting-only repair: the configuration was
  already dropped by upstream read-only spa_tryimport, so status alone could
  not recover it. No failed run is counted as passing.
- Native libzpool intentionally skips activation of shared auxiliary devices
  on read-only imports. AROS-specific spa_add_spares/spa_add_l2cache paths now
  retain their stored configuration when there are no active vdevs, preserving
  it through discovery into the subsequent import. Upstream non-AROS behavior
  is unchanged. The native status adapter marks these stored entries INACTIVE
  and statistics=unavailable, never fabricating ONLINE state or live counters.
- admission-service-run.F88U38hD passes: log stays ONLINE; cache/spare are
  INACTIVE on read-only import and ONLINE after writable reimport. Both native
  payloads survive; disabled-feature refusals, blank-file verification,
  destruction/cleanup and baseline disk hashes pass. Logs are in
  typed-import-service/add-aux-*.log, including failed runs/build diagnostics.
  link-engine-test.sh rebuilds spa.c to avoid stale objects hiding this patch.

Cold auxiliary persistence: `AROS_TEST_COLD_AUX=1` with the cold expansion
suite provisions seven private 64 MiB files. file-backing-run.TYzGXYWB passes
addition of log/cache/spare before the 8 MiB DOS write; a separate guest reads
all bytes and verifies exact auxiliary paths/roles, log ONLINE and cache/spare
INACTIVE. All seven members remain byte-identical across read-only use, and
independent host zdb reports No leaks. Logs: typed-import-service/aux-cold-*.log.
All files share a private FAT image, not independent physical devices. A clean
export/reboot is not crash/log-replay proof, nor evidence of cache hits or
automatic spare activation.

Remaining: enabled allocation-class tests, actual
cache/spare/log workloads, failure after upstream starts
labeling, physical-device integration, policy-path reclamation, and actual GUI
submission. Full port is
still incomplete. No DUT or user-hosted session was accessed.
