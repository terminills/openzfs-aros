# UTF-8-only native mounts

The native mount and resume paths admit `utf8only=on`, with normalization
disabled or enabled and all three dataset case modes. Namespace mutation retains upstream ZPL
UTF-8 validation; the direct native lookup bridge now validates using the same
upstream Unicode validator and returns EILSEQ for malformed names.
Normalized lookup uses the already-ported upstream directory locks and ZAP
matching, not a new normalization implementation. Mixed datasets retain
case-sensitive default lookup; this does not add an explicit case-insensitive
DOS lookup request. This does not establish Unicode rendering in Wanderer or
a native DOS packet encoding convention for legacy non-UTF-8 applications.

## Evidence

- `build-aros/default-quota-run.TvFkgaLc`: mount failed with errno 86 before
  filename tests. Not an accepted candidate.
- `build-aros/default-quota-run.rFlCt0gN`: added metadata assertions showed
  version=4, norm=0, utf8=1, case=0. The suite deliberately starts with a
  legacy pool; upstream creation correctly capped the requested ZPL version.
  Failure was a fixture-order error, not proof of a UTF-8 validation defect.
- The new test now follows the existing pool/version upgrade test.
- `build-aros/default-quota-run.JxWuPvP8`: runner exit 0; metadata version=5,
  norm=0, utf8=1, case=0. Valid accented-name creation, lookup, rename, sync,
  close and readonly reopen pass. Overlong UTF-8 is rejected by create,
  mkdir, lookup and rename. Existing default-quota and lifecycle checks pass.
  Binary: `build-aros/utf8-mount-sa/OpenZFSDefaultQuotaTest`; build log:
  `build-aros/utf8-mount-sa-build.log`.
- `build-aros/default-quota-run.9vppcRMg`: NFC mount passes equivalent composed/
  decomposed lookup of the same object, equivalent-name duplicate refusal,
  case-sensitive lookup refusal, rename through the equivalent spelling and
  readonly reopen with unchanged object identity. Full runner exit 0.
- `build-aros/default-quota-run.lIaG9BiN`: expanded NFC/NFD/NFKC/NFKD matrix,
  all four pass those checks and the existing suite, runner exit 0. On-disk
  normalization values are asserted separately for each dataset. Build:
  `build-aros/normalized-mount-four-build.log`; executable under
  `build-aros/normalized-mount-four/`. These are canonical-equivalence checks
  in each mode, not exhaustive Unicode compatibility-character coverage.
- `build-aros/default-quota-run.luO1rMGg`: runner exit 0. Case-insensitive and
  mixed modes each pass with normalization off and NFC on, alongside the
  earlier matrix. Insensitive lookup resolves uppercase accented names to
  the same object, rejects duplicate case aliases and renames through aliases.
  Mixed and sensitive datasets permit distinct case names; deleting one does
  not remove the other. On-disk case metadata, readonly reopen and object
  identity are checked. Build: `build-aros/case-mount-build.log`.

## DOS integration follow-up

`ACTION_RENAME_OBJECT` previously matched cached lock/handle leaf names using
`strcmp`, leaving stale names when renaming through equivalent spellings.
It now precomputes dataset-aware matches before the namespace mutation and
aborts on comparison errors. Separate hard links still require a matching
name as well as object and parent; mixed mode excludes case folding from
this comparison, matching default upstream lookup semantics.

`build-aros/default-quota-run.rxUT04J9` passes all nine dataset cases with
the new comparison helper and a real distinct hard link to the same object.
The link remains distinct and survives rename/reopen. Build log:
`build-aros/rename-name-match-build.log`. Runner exit 0 and existing regressions
pass. This run verifies the comparison helper and ZPL namespace only.

`build-aros/default-quota-run.SbZ1mMza` subsequently passes the authenticated
DOS packet test for all nine dataset cases (runner exit 0). A separate task
logs in through security.library as the isolated fixture root, holds a file
handle and source lock plus a distinct hard-link lock, and calls DOS Rename
through the appropriate equivalent spelling. Examine, NameFromLock and
ExamineFH report the renamed source; the separate hard link keeps its name.
References are closed and the endpoint detached before the next case.
The existing quota/lifecycle suite also passes with the security fixture
configured. Build log: `build-aros/dos-alias-rename-build.log`.
This closes the tested packet-level cache update gap, not graphical Unicode
rendering, concurrent rename stress or exhaustive Unicode coverage. No
installed service was replaced.

## Compatibility normalization and enumeration

`build-aros/default-quota-run.DUxnBnCX` passes the expanded nine-case suite:
fullwidth digit U+FF11 matches ASCII 1 only under NFKC/NFKD, with lookup,
duplicate rejection and removal through the equivalent name checked. Other
modes permit distinct entries. DOS ExNext returns exactly the renamed file,
its distinct hard link and an accented filename, preserving its UTF-8 bytes.
Runner exit 0; build log `build-aros/unicode-enumeration-build.log`.

Preceding `LUGfwHKW` completed guest tests but a security notification split
an older DOS identity PASS marker. The runner failed after explicit shutdown;
it is not reclassified as a pass. The runner now retains raw serial and makes
a separate completion stream by removing only complete identified task-notify
records. Failure checks inspect both streams; all required success markers
and counts remain mandatory. This is filename byte enumeration evidence,
not proof that Wanderer renders the glyphs correctly.

## Upstream creation rather than fixture-only properties

`build-aros/default-quota-run.7DDFhspO` runs the full native dispatcher and
passes `ZFS_IOC_CREATE` with NFC, UTF8-only and case-insensitive properties.
A second upstream create with no explicit properties inherits all three
from its parent. Both datasets mount and resolve an uppercase decomposed
filename to the same object as its composed lowercase spelling. This uses
trusted resident nvlist buffers under the real copy-scope and root credential
checks, not the client transport or GUI. Runner exit 0 also covers the full
dispatcher lifecycle, zvol and nine-case DOS tests. Build log:
`build-aros/upstream-create-names-build.log`.

Typed operation 37 now accepts comma-separated normalization, utf8only and
casesensitivity options from the existing manager options field. The full
service translates spelling into an nvlist and calls upstream CREATE under
the retained caller credentials; omitted properties use upstream inheritance.
Unknown or duplicate options fail before creation. Reduced diagnostic builds
still support default-only creation and explicitly reject property options.

`build-aros/default-quota-run.w3eyJhoc` passes the adapter's valid-property,
duplicate-option and unknown-option checks, including absence after rejected
requests and on-disk normalization/case assertions after creation. Full suite
runner exit 0; build `build-aros/typed-create-options-build.log`. This directly
exercises the resident adapter, not yet the separate-client admission or
rebuilt GUI interaction. Those remain required, along with broader creation
properties beyond these three namespace settings.

`build-aros/native-import-run.kXvvwIVS` subsequently verifies separate-client
typed admission against rebuilt `full-typed-create/OpenZFSServiceNative`.
Authenticated requests reject duplicate/unknown options, create the requested
NFC/UTF8/case-insensitive dataset, mount it through DOS, write a composed name
and read exact bytes through uppercase decomposed spelling, then detach and
destroy it. The healthy native RAIDZ1 write/export/reimport regression also
passes, with original raw images unchanged (private worker QEMU; exit 0).
Service build log: `build-aros/full-typed-create-build.log`.

The manager rebuilt successfully as
`build-aros/typed-create-manager/OpenZFSManagerNative` with updated operation
help (`typed-create-manager-build.log`). This is a build result, not GUI
interaction or visual validation; that remains a separate gate.

All runs used isolated diskless QEMU with private RAM-file pools. Reopen is
within one guest, not cold-boot persistence or end-to-end Wanderer proof.
No DUT, shared hosted session or installed service binary was modified.

### Native editor verification

The subsequent editor runs used private copies of the mirror fixture disks,
not the diskless fixture above. No service was started in these guests.
`native-gui-run.D0Nf64L7` exposed a misleading semicolon options hint.
The rebuilt manager now shows comma-separated creation syntax, concise visible
help with the full property list in short help, and joins properties with commas
for dataset creation. Other operations keep their existing property separator.
Target, argument and options fields now explicitly participate in keyboard
tab navigation.

Build `creation-hints-keyboard/OpenZFSManagerNative` completed successfully
(`creation-hints-keyboard-build.log`). Fresh private QEMU run
`native-gui-run.pyeFRu7D/07-preview-settled.png` shows keyboard-entered target
`qemu_mirror/gui-names` and the complete preview of
`normalization=formC,utf8only=on,casesensitivity=insensitive`, with
"Preview only - nothing submitted". Immediate captures sometimes preceded
guest input processing; earlier unsuccessful attempts remain in the run's
action log and screenshots. Both copied disk hashes passed after shutdown.
The intermediate `native-gui-run.5E4C75G4` also retained unchanged disks.

This proves field entry and request preview, not authenticated GUI submission,
the property-picker interaction, or cold persistence of the created dataset.
The separate-client service proof above remains distinct. Those GUI/service
and persistence checks are still required.

### Authenticated GUI creation

`native-import-run.a0d2vfiw` completes the GUI-to-service creation check.
The full service imports the disposable mirror writable and rejects invalid
creation options. Only the real manager creates `qemu_mirror/api-names`, with
NFC, UTF8 and insensitive case settings. Capture `03-confirm.png` records the
exact request with dry-run off; `04-submitted.png` records authenticated
creation errno=0. After the manager exits, the independent client mounts this
dataset through DOS, writes a composed UTF8 filename, reads identical bytes
through uppercase decomposed spelling, detaches and destroys the dataset.
`RESULT native GUI create PASS normalized-case-read=1 destroy=1` and the full
mirror write/export/reimport suite pass; runner exit 0, original backing
image hashes unchanged. This is TCG software emulation, explicitly recorded
by the runner; it is not a performance or hardware result.

The test runner supports `AROS_TEST_GUI=2` only with a writable, healthy mirror
on disposable overlays. Ordinary GUI and non-GUI modes retain their existing
constraints. `AROS_TEST_ACCEL=tcg` selects CPU max; default remains KVM/host.
The QMP evidence helper accepts the runner's private directory and socket.

Retained earlier attempts: `native-import-run.yS8ZhliL` reached GUI-ready but
the private worker QEMU has no screendump command; it was explicitly stopped
and backing hashes passed. `native-import-run.KmH4Vp5j` was interrupted and
its QEMU log records SIGTERM; it has no success verdict. Neither counts as
GUI verification. The successful run still does not cover the property picker
or cold persistence of the namespace settings: the tested dataset is destroyed.

Fresh TCG guest `native-import-run.c2pJJfMx` then imported child overlays of
the completed `a0d2vfiw` mirror read-only and verified the exact ordinary DOS
write payload. The runner exited 0; raw backing, prior written overlay and new
read-only overlay hashes all matched. This closes cold readback for that
ordinary file, not cold persistence of the already-destroyed namespace dataset.

### Cold namespace persistence and property reporting correction

`AROS_TEST_KEEP_NAMES=1` retains the namespace dataset on disposable overlays,
then checks exact namespace property values and DOS alias reads after export
and read-only reimport. A separate verify run starts a fresh guest on child
overlays; it requires the retained/reopened markers in the source evidence.
The option is restricted to non-GUI writable/verify modes, keeping the previous
GUI destroy proof unchanged.

Failed runs `native-import-run.M4UqZfnc` and `native-import-run.vLVKii7x`
exposed an actual property-reporting bug: normalization was reported as `none`
after successful normalized/case-insensitive writes. The latter records the
exact query response. `oz_property_get` consulted DSL properties/defaults,
whereas these settings live in the ZPL master node. It now uses upstream
`zfs_get_zplprop` for version, normalization, UTF8 and case, matching the
OBJSET_ZPLPROPS path, and rejects inconsistent/non-filesystem objects.

Rebuilt full service `full-zpl-properties/OpenZFSServiceNative` (build log
`full-zpl-properties-build.log`) passed `native-import-run.oK56LDA2`: retained
creation, three exact property values, normalized alias DOS read, ordinary
DOS write and export/reimport regressions. Independent fresh TCG guest
`native-import-run.ALIKfRut` passed the same three property values and exact
alias read on a read-only import, plus the ordinary-file cold readback.
Both runners exited 0. Original raw images remained unchanged; the cold
run additionally verified unchanged prior overlays and its own overlays.
The retained namespace dataset is confined to these disposable test images.
This proves cold namespace persistence on this mirror fixture, not broad
concurrency, crash recovery, RAIDZ2/3 native devices, or production packaging.
