# Native mirror splitting (in progress)

Operation 14 now calls upstream `spa_vdev_split_mirror` through native
credential-aware admission. A populated two-mirror split and independent cold
boots of both resulting pools have passed in private QEMU. This is not full-port
completion or physical hardware evidence.

## Request and configuration

The v2 request uses target = source pool, argument = destination pool,
options = `guids=<decimal leaf>,<decimal leaf>,...`. This leaves the wire layout
unchanged and does not reinterpret topology path fields as GUIDs. Require one
explicit file leaf from every concrete mirror; never silently choose a disk.

`split-config.c` implements strict GUID parsing and constructs the upstream
`ZPOOL_CONFIG_VDEV_TREE` input from a live source configuration. Selection order
does not reorder top-level vdev slots. Interior logs/holes become holes; trailing
logs/holes are omitted. Indirect vdevs are copied without selecting a leaf.
Cache/spare arrays are not transferred. Missing, unknown, duplicate and
same-mirror selections are rejected. The source nvlist is unchanged.

The service checks root credentials, writable/non-suspended source,
valid destination name, request flags, and rejects mounted source datasets until
the split/mount interaction is verified. Upstream remains responsible for
checkpoint, health, DTL and resilver restrictions at execution time.

## Generated pool identity and access policy

`spa_vdev_split_mirror` generates a new pool GUID internally, loads the new pool,
and can export it before returning. Imported pools currently retain a path
allowlist keyed by the original input configuration GUID. An unregistered GUID
currently has no policy restriction (needed by existing creation fixtures).
Consequently, invoking split without arranging the new pool's policy would
**bypass**, not inherit, the imported pool's path restriction.

`pool-split.c` now registers an exact destination-name rule containing only the
selected paths before upstream execution. `vdev_file.c` checks both name and
GUID rules; matching rules intersect. Source path admission is checked first.
The named rule remains through the temporary new pool load and is released only
after export/unload. If upstream leaves the destination imported after an export
failure, the rule remains until explicit export/destruction or engine shutdown.
The source's existing backing locks remain alive throughout the call. As with
other file-backed operations, files must be dedicated and free of external
writers/renames; pathname rules do not provide atomic identity handoff.

The UI operation leaves the destination exported. Importing it is a separate
explicit-file request. No API struct change, shell invocation, automatic import,
implicit feature enablement, force or fallback from dry-run to real execution.

## Evidence (2026-09-22)

- `build-aros/split-config-run.uqVaABFO`: strict host compile and parser/tree tests.
- `build-aros/split-config-run.H85wdFQA`: same tests with AddressSanitizer and
  UndefinedBehaviorSanitizer; upstream shared libnvpair itself is not instrumented.
- `build-aros/typed-import-service/split-native-build.log`: AROS cross compilation
  of the configuration implementation, not native execution.
- Initial host compiler failures are retained in `split-config-build.log`:
  const array types and a nonexistent convenience lookup were corrected.
- `build-aros/admission-service-run.LYEf9f4o`: native named/GUID policy tests,
  two-mirror split, retained source and exported destination, explicit destination
  import, original DOS file readable from both, independent later source file,
  separate exports/reimports, and destruction. Includes read-only, mounted,
  malformed/duplicate/unknown/missing GUID and existing-destination refusals;
  non-root refusal is tested by the suite driver. RAM-file test, not cold evidence.
- `build-aros/native-gui-run.wCNRL62F/03-split-preview.png`: native AROS UI
  operation labels and warning preview. Backend deliberately unavailable and no
  mutation submitted. QEMU exited normally after capture.
- `build-aros/admission-service-run.ddUzXiKm`: expanded native suite passes
  force/dry-run/recursive/raw/resumable refusal, active checkpoint split refusal,
  explicit checkpoint discard and completion, then successful split/readback.
- `build-aros/admission-service-run.4Xv4yQqQ`: regression on the default-property
  fix passes stripe/mirror/RAIDZ1/RAIDZ2/RAIDZ3 creation, native DOS payload and
  refusal tests, plus expansion, auxiliary additions/removal and populated
  mirror evacuation. Both original baseline images retain their hashes.
- `split-engine-build.log` preserves a compile failure from using an internal
  namespace lock; corrected to upstream `spa_namespace_enter/exit` APIs in
  `split-engine-build-2.log`. Engine, client and manager builds completed.

### Independent three-boot persistence

`build-aros/file-backing-run.caD7yrE2` passed all three boots and host checks:

1. AROS creates a mirror in blank disk0/disk1 files on a private FAT-backed image,
   writes the initial file, adds disk2/disk3 as another mirror, and writes an exact
   8 MiB pattern with allocation verified on the new mirror. It splits disk1 and
   disk3 into `aros_split_51a20519`, leaves that pool exported, and exports the
   source `aros_mirror_51a20519`.
2. Host renames disk1/disk3 out of reach. A fresh guest imports only disk0/disk2
   read-only and verifies source identity, pool properties, native DOS directory
   enumeration and both payloads, then exports it.
3. Host restores disk1/disk3 and withholds disk0/disk2. Another fresh guest imports
   only disk1/disk3 read-only, verifies the distinct destination GUID, upstream
   default comment and copied MOS properties, and performs the same DOS reads
   and enumeration before export.

All four member files compare byte-for-byte with the post-split export after
each read boot. Host `zdb -bcc` is supplied only the two appropriate members for
each pool; both traversals pass checksums with **No leaks**. The runner exits 0.
Entry point: `AROS_TEST_COLD_SPLIT=1` with the existing API-create/mirror/add
private file-backing suite. Source snapshots, frozen binaries, logs, extracted
members, configuration and independent `read-zdb.log`/`split-zdb.log` are retained.

Special/dedup mirrors, retained log/cache/spare roles, indirect vdevs, interrupted
split recovery and export-failure retention still need runtime tests. Native DOS
enumeration is proven here; this is not a screenshot of Wanderer browsing both.

### Cold-test failures retained

- `file-backing-run.aUWlz5rQ`: split and source-only cold read passed. The new
  destination imported ONLINE, but a reused reguid test incorrectly expected
  the destination GUID to equal the source's saved user property. Changed the
  assertion to require a distinct destination GUID while preserving the
  read-only mutation refusal and the source GUID persistence check.
- `file-backing-run.QXXqJ7zW`: destination identity then passed; its unset label
  comment exposed missing upstream-default handling in `oz_property_get`.
  Upstream `libzfs_pool.c:zpool_get_prop_string/int` supplies defaults when
  recognized properties are absent. The adapter now follows that behavior;
  unknown user properties remain absent. Upstream split does not copy the
  source label comment, whereas its MOS user properties are copied. The cold
  test expects the destination comment's default `-`, with source `default`,
  while still checking copied MOS properties and their read-only refusal.
