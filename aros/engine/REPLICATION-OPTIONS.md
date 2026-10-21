# Native send options

## Raw encrypted send/receive

### Raw resumable continuation

Cold follow-up writer `native-import-run.yOjklz83` and fresh guest
`native-import-run.i7le8hCx` both pass their complete runners. The writer
retains an interrupted full raw receive, its locked source and snapshots,
syncs and exports. The fresh guest first completes existing read-only crypto
checks, exports, then explicitly reimports writable on new child overlays.
It obtains the persisted token (not a host-saved copy), sends from the still
locked source, completes receive, verifies token removal and locked target,
loads the original key and compares every byte of the changed 1 MiB file plus
EOF. It unloads the key, syncs and exports. Original backing files and both
writer overlays retain their hashes; both child overlays must change.
Independent `control-integrity.Nov1QuwO` passes upstream zdb -bcc with No leaks
and unchanged extracted-member hashes. This is clean-export cold continuation,
not abrupt power-loss or cold incremental-token continuation.

Reproduce the writer with ENCRYPTION_PERSIST=write, ZVOL_DISCARD_PERSIST=write,
ZVOL_TRANSPORT=1, KEEP_CLI=1 and NATIVE_WRITABLE=1 (all prefixed AROS_TEST_).
The reader uses ENCRYPTION_PERSIST=resume, ZVOL_DISCARD_PERSIST=read,
NATIVE_WRITABLE=2 and NATIVE_VERIFY_RUN pointing at that successful writer.
Its mandatory pending-state marker prevents reuse of older incomplete writers.
Read-only-only verification remains ENCRYPTION_PERSIST=read; resume explicitly
permits writes only to newly created child overlays, never the writer.

Manager help now describes raw tokens and the required incremental base;
`raw-resume-manager.06NSqUMW/OpenZFSManagerNative` builds successfully. This
is not a new interactive raw-resume GUI test. Generated test ISOs for completed
S6fLvCf3 and dZQsO6UI were reclaimed for space after hashing; all logs, binaries
and disks remain. Hashes: reclaimed-test-isos-20260928-resume-cold.sha256.

Incremental follow-up `native-import-run.dZQsO6UI` passes the full native
runner (exit zero and original backing hashes unchanged). It rewrites the
encrypted 1 MiB file, truncates the raw incremental stream, obtains the saved
token, rejects continuation without the required base before creating output,
then continues with the matching base snapshot while source and receiver are
locked. Original-key readback matches all changed bytes plus EOF, and receive
state is cleared. This adds same-boot raw incremental resume, not cold pending
state continuation; that remains separate coverage.

The resume adapter accepts upstream boolean `rawok`, applies raw-aware send
authorization before source metadata access, and preserves raw block alignment.
Management preflight uses token-derived authorization instead of first requiring
plaintext send. Existing checksum, GUID, object, offset and caller/output checks
remain; unknown extensions such as redaction are still refused.

Service `raw-resume.MHuHAdM9` and private native QEMU run `S6fLvCf3` pass the
complete runner (exit zero, original backing hashes unchanged). The fixture
truncates a raw encrypted stream at 512 KiB, observes upstream error 1040,
retrieves the real receive token through upstream zfs, rejects a wrong source
snapshot without creating output, and sends a continuation as UID77 granted
only send:raw. The source remains locked. Receive completes while locked;
the token clears, the original key unlocks the result, and native DOS reads
compare all 1 MiB plus EOF. Existing raw full/incremental and delegation tests
also pass. Dedicated raw-resume success is now a mandatory runner gate.
Independent `control-integrity.dfQFjC4A` passes upstream `zdb -bcc` with
No leaks; extracted member and original backing hashes remain unchanged.

Retained `MKnLFf2g` failed fixture compilation because its selected configuration
did not include the helper used for whole-line comparison. No guest launched;
the fixture now checks the complete CLI property output locally. This test is
same-boot full-stream raw resume, not yet cold interrupted-state continuation,
raw incremental resume, cross-platform receive or physical power-loss proof.
Four completed-run disposable test ISOs (FCtmiBjX, LHkmR0ZL, pHZRIiMZ,
JpRUsEVT) were reclaimed for space; logs, binaries and disk images remain,
with pre-removal hashes in reclaimed-test-isos-20260928-resume.sha256.

### Incremental and cold received-data verification

Writer native-import-run.ClI6Ezxg passes the full regression after sending a
second encrypted snapshot with from=base;raw=on while the source key is
unloaded. The destination key is also unloaded during receive. Loading the
original key exposes the exact new 512-byte file. The retained receiver keeps
both snapshots and is synced before export. Fresh read-only guest Tarpnub6
starts with keystatus unavailable, loads the original key, reads every byte of
the incremental file, confirms both snapshots and refuses file creation.
Both runners exit zero; original backing, writer and reader disk hashes pass.
Independent control-integrity.va0cCjzt passes zdb -bcc with No leaks and
unchanged extracted-member hashes. Dedicated incremental/retained/cold markers
are mandatory; an older writer without the retained marker is refused.

Retained lFt7wHgN failed incremental receive with ETXTBSY after the test
mounted the first received dataset writable for verification. Upstream refuses
a changed target without force. The revised test makes that verification mount
read-only, then restores the property after unmount; no receive check is relaxed
and no force flag is added. This isolates verification-induced writes, without
claiming which individual metadata update caused the first failure.

Cold-reader attempt C0JscscM failed before QEMU launched: xorriso exit 32,
insufficient host disk space. Four completed-run generated test ISOs were
removed (LRsY1YP5, WFEgJcEu, SxHZlPZu, aZsj71O7), preserving logs, inputs,
binaries and disks. Their pre-removal hashes are retained in
build-aros/reclaimed-test-isos-20260928.sha256. Tarpnub6 is the successful
retry with unchanged code and writer disks. This is clean export/cold recovery,
not raw resume, interrupted receive, or physical-media qualification.

### Full-stream foundation

Operation 60 accepts raw=on/off and passes the flag to upstream dmu_send.
Both preflight and execution use raw-aware caller authorization: send:raw
does not grant plaintext send. Upstream owns stream encoding and the receiver
uses the stream's raw feature metadata; no substitute encryption is added.
The raw option is distinct from the UI's generic Raw checkbox, which is not
admitted for replication requests. Use the option field. Raw resume tokens
are supported by the later continuation work above; token/format exclusion
still applies.

Service raw-replication.UYd4RAAS passes native-import-run.RLuRaep2, then the
expanded native-import-run.tyudwUpP passes the full native regression with
delegation. The fixture snapshots an encrypted DOS file, unloads the source
key, checks unavailable, and sends through the authenticated management API.
API receive creates an encrypted locked dataset. Actual CLI key load with the
original source key permits mounting and exact 512-byte DOS file readback.
A UID77 caller granted only send:raw is refused for raw=off but succeeds with
raw=on into its owned empty output. Both dedicated markers are mandatory.
Outputs/snapshots/datasets are cleaned up; original disk hashes are unchanged.
Independent control-integrity.WrlzUPAL passes zdb -bcc with No leaks and
unchanged extracted-member hashes. Manager raw-send-manager.Khmf6x0j builds
with updated help; no new raw-send GUI interaction is claimed. Raw resumable
coverage is recorded above; all representation combinations and cross-platform receive remain separate
coverage gaps. Incremental/cold recovery is covered by the later section above.

The older compressed/large/embedded representation fixture now uses the
explicit OZ_SEND_REPRESENTATION mask (7), rather than all supported flags, so
adding raw support cannot silently turn its ordinary-stream tests into raw
tests. This preserves its previous selection; that fixture was not rerun by
the native-import regression described here.

Authenticated operation 60 accepts a semicolon-separated list:

* `from=pool/dataset@snapshot` or `from=pool/dataset#bookmark`
* `compressed=on|off`
* `large-blocks=on|off`
* `embedded=on|off`
* `raw=on|off`
* `resume=TOKEN` (instead of representation switches; optional matching `from=`)

Omitted switches default to off. Unknown names, duplicate names, empty values,
empty components and trailing separators are rejected before opening output.
The original captured caller still needs upstream send delegation and native
output permission. Resume tokens are validated before output is opened; see
`RESUMABLE-RECEIVE.md`. No option enables force or property replication.

The switches are passed to upstream `dmu_send`, not a custom stream encoder.
They permit corresponding records; a stream from small uncompressed files does
not prove that such records were emitted. The API regression exercises the
switches and native full/incremental receive and cold persistence. The separate
representation fixture below supplies large/compressible/embedded data.
Bookmark syntax acceptance alone is not evidence of a working incremental
base; the separate deleted-snapshot fixture described below covers that case.

The initial isolated run `admission-service-run.UGeCjSvc` failed because the
admission layer retained the older from-only option gate. It was corrected to
let the bound credential-aware replication adapter validate the full grammar
before any output acquisition. This failure is retained, not relabeled a pass.

`admission-service-run.aUKA7JrK` passed writable execution, cold read-only
verification, and the existing offline regression verifiers. Nine malformed
option cases left no destination file. Full send used all three switches on;
incremental send combined `from` with compressed on and the other switches off.
The native receiver and file readback verified those API-produced streams.
The existing host parser checks the separate ordinary fixture streams, not
these newly optioned API outputs. Build/run logs use `send-options-*` names
under `build-aros`; Manager was rebuilt but no new GUI interaction is claimed.

## Actual representation proof

`admission-service-run.zdj2IAuf` passed WRITE, cold read-only VERIFY and offline
checks. A separate fixture explicitly enables embedded data after the existing
disabled-feature tests, sets compression=lz4 and recordsize=1M through the
production property setter, and writes a 1 MiB patterned file and a 4 KiB repeated
byte file. The same snapshot is sent with all switches off and all switches on.

* Ordinary stream: no compressed, large, or embedded records; 1,120,752 bytes.
* Advanced stream: 1 compressed WRITE, 1 large WRITE, 11 WRITE_EMBEDDED records;
  62,480 bytes.
* Both streams receive through upstream into separate datasets. Both file sizes
  and every byte are checked immediately and after cold read-only import.
* After QEMU exited, `verify-native-streams.sh RUN advanced` extracted both exact
  retained streams with zdb and checked upstream zstream checksums, BEGIN/END,
  stream length, identical source GUID, embedded record counts and size reduction.
  Disk hashes remained unchanged. Logs: `advanced-stream-run-1.log` and
  `advanced-stream-host-verify-1.log`.

The representation fixture uses a privileged fixture caller; captured-caller
delegation and native output checks are covered separately by the API tests.
This does not claim raw/encrypted/resumable/recursive sends, compressed
incremental record coverage, or Linux kernel receive.
The runner's additional representation gates were added after its source was
captured for this run; the advanced host check was therefore run explicitly
after completion. Subsequent admission runs require those gates automatically.

## Bookmark-based incremental fixture

The fixture bookmarks `stream-advanced@base`, changes `hello.txt`, creates
`@second`, and destroys `@base` before sending from `#base`. Both WRITE and
cold VERIFY check that the original source snapshot is absent and the bookmark
still exists. A missing bookmark must return upstream ESRCH, not ENOENT.

The engine receives the incremental stream onto `advanced-received@base`.
The authenticated API independently sends from the same bookmark to a new
native file on the mounted own-volume handler and receives it onto
`plain-received@base`. Changed file bytes are checked through the native DOS
handler and again after cold read-only import. The API missing-bookmark case
also checks that failed send cleanup leaves no published destination file.

Offline verification extracts both exact incremental streams, verifies upstream
checksums and lengths, requires their from-GUIDs to match the original full
stream's to-GUID, and requires the same new to-GUID for both incremental paths.

The first run `admission-service-run.ALjBGWfd` failed a test assertion expecting
ENOENT for a missing bookmark. Upstream dsl_bookmark_lookup documents ESRCH
when the containing dataset exists. The corrected fixture preserves that
distinction and logs the observed error and each send/receive stage.

`admission-service-run.uO8MZ2xz` passed the complete writable/cold/offline suite
with these gates enabled. Engine and API incremental streams both have
from-GUID `437d48633b06e94b` and to-GUID `c251e1092c469111`; the former matches
the original advanced full stream. Logs are `bookmark-stream-{engine,client}-build-2.log`
and `bookmark-stream-run-2.log` under `build-aros`. No hardware was used.
