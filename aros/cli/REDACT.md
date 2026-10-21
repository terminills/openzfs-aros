# Native redaction bookmarks and streams

The transport admits `ZFS_IOC_REDACT` as a bounded packed upstream request,
without client pointers, configuration/history buffers or unrelated inline
flags. Upstream validates the bookmark name, snapshot lineage, feature state,
root config permission and the redaction algorithm itself.

Manager operation 59 invokes real `C:zfs redact` under inherited caller identity
through the asynchronous command worker. Target is a full dataset@snapshot;
argument is the bookmark's short name; options are
`snapshots=full@snapshot,full@snapshot` (1-32, unique, nonempty). Confirmation is
mandatory; unrelated flags, dry-run and malformed lists are rejected. Names
containing commas cannot be represented in this UI list encoding; the CLI
retains upstream's separate argument interface. No shell expansion occurs.

Redaction creates a bookmark; it does not erase source data or automatically
redact subsequent sends. A send must explicitly select the redaction bookmark.
This is not secure deletion, encryption, or a blanket disclosure guarantee.
The caller is responsible for selecting the appropriate redaction snapshots.

Manager send (operation 60) accepts `redact=short-bookmark-name`. The native
replication API passes this selection to upstream `dmu_send`, under the caller's
existing send permission checks. The complete derived bookmark name is validated
before opening output, including its length; full paths, duplicate options and
combining redaction with a resume token are refused. Upstream still decides
bookmark existence, lineage and stream representation compatibility. Existing
nonempty outputs remain protected by the normal send output policy.

The Manager-send regression receives its stream through upstream CLI and checks
the received redaction GUID, omitted secret and retained public block. Additional
cases check six malformed option combinations and unauthorized send refusal,
with no output created. Build: `source-engine.P2gN7Bsv/manager-redact-service`,
log `manager-redact-build-1.log`; CLI `cli-link.zhOWGskB`.

Run `native-import-run.m45c8SjG` (`manager-redact-native-1.log`) passes valid
Manager send and CLI receive. Extended run `native-import-run.wlPisMWb`
(`manager-redact-native-2.log`) additionally passes all six malformed cases,
non-root permission refusal and absent-output check. Both finish the complete
native regression with host exit 0 and unchanged original backing-image hashes;
both evidence sets are registered under the development test archive. These exercise Manager's real
native API, not interactive GUI mouse clicks or redacted resume tokens.

## Regression

The native fixture creates a filesystem with distinct 128-KiB public and secret
test blocks (compression and dedup off), snapshots it, clones it, overwrites the
secret block in the clone, and snapshots the clone. It checks confirmation and
malformed-list refusal, non-root refusal with no bookmark published, and real
Manager bookmark creation. The bookmark's `redact_snaps` must contain the clone
snapshot GUID queried independently through CLI.

It then creates ordinary and redacted upstream send streams. The original
secret canary must occur in the ordinary stream, be absent from the redacted
stream, and the public canary must remain in the redacted stream. A real receive
into a new dataset must retain the same redaction GUID. All datasets, bookmark,
clone and streams are explicitly cleaned up. User data and hardware are not used.

Build: `source-engine.P2gN7Bsv/redact-service`, log
`redact-service-build-1.log`; CLI `cli-link.zhOWGskB`.
Run `native-import-run.55sxBImu` (`redact-native-1.log`) passes the complete
redaction fixture, full native suite and unchanged original backing-image
hashes, host exit 0. The non-root response is upstream's `internal error:
Operation not permitted`; no bookmark is published. Evidence is registered
under the development test archive. This verifies the real Manager worker, not GUI mouse clicks.
## Multi-snapshot and incremental extension

The extended fixture selects two descendant snapshots. First one still retains
the original secret block, which must therefore remain in the redacted stream.
Then both selected snapshots have replaced it, and the original secret must
be absent while the public block remains. Both bookmarks are made through
Manager's multi-snapshot request path.

An ordinary incremental from the source redaction bookmark to its safe clone
snapshot is then received into a new clone explicitly originating from the
received redacted snapshot (not the intact same-GUID source). The resulting
snapshot must have no redaction GUID list. The native DOS handler mounts that
completed dataset and compares the full 128-KiB public file and 128-KiB safe
replacement file, not just the markers.

Run `native-import-run.90ZR6oAR` (`redact-multi-native-3.log`) passes both
multi-snapshot cases, the redacted-origin incremental receive, full DOS byte
comparisons, the complete native suite and unchanged original backing-image
hashes. Evidence is registered under the development test archive. This is native QEMU verification,
not physical storage durability or an interactive GUI send test.

Initial run `native-import-run.afcpY1vq` (`redact-multi-native-1.log`) passes
the two-snapshot retained/omitted checks but fails incremental receive: the
fixture incorrectly targeted an existing dataset for a clone stream. Upstream
`redacted_incrementals.ksh` receives such streams into new destinations.
The corrected fixture uses a new destination and explicit redacted origin,
then queries that origin independently; it does not force overwrite. Host
exit 1; failure logs are registered in the development test archive, no full-suite pass claimed.

Run `native-import-run.2yrxpJ4F` (`redact-multi-native-2.log`) passes explicit
redacted-origin incremental receive and origin/no-redaction-list checks, but
the fixture changes mountpoint and then redundantly calls mount. Upstream's
property-change path already mounted it; the second mount correctly refuses.
The correction supplies mountpoint during `receive -u` before explicit mount.
Host exit 1; DOS byte checks and final hash gates were not reached. Registered
failure evidence is retained.

This does not establish redacted-to-redacted incremental combinations,
mounting/reading a still-partially-redacted received filesystem, or physical
durability.

## Malformed resume wire qualification

`native-import-run.v87yAqY3` uses the release service from the exact
`native-bundle.7s6OdtOI` package. Starting with a real redacted receive token,
the Manager API fixture rejects an unsupported version, an odd/truncated hex
payload and an altered payload with EINVAL. All three leave the requested
output path absent. The original token then completes the existing Manager
resume and exact DOS readback checks. The full selected native mirror/device
suite exits zero with unchanged original backing hashes; see
`redacted-malformed-wire-native-1.log` and the retained guest serial log.
This exercises wire validation, not malformed decoded GUID arrays or an
incomplete redaction bookmark. Those remain separate coverage gaps. It is
an isolated QEMU/API test, not physical-media or GUI-click qualification.

## Upstream CLI interrupted redacted receive

The native fixture includes a retained uncompressed 1-MiB payload, cuts a real
redacted stream at 512 KiB, and requires `zfs receive -s -u` to fail with a
nonempty upstream receive token. Actual `zfs send -t` resumes that token. The
resumed stream must not contain the secret canary; receive must succeed,
clear its token and retain the expected redaction GUID. A completing incremental
uses this received snapshot explicitly as its origin. After redaction is
resolved, the AROS DOS handler reads the exact retained 1 MiB and EOF.

Run `native-import-run.LZw6FqVe` (`redacted-resume-native-1.log`) passes all
these checks, the full selected native mirror regression and unchanged backing
hashes, host exit 0. It uses `events-publisher-service` and CLI
`cli-link.NgMGW2me`; no replacement redaction algorithm was added. This verifies
the upstream CLI path, not Manager resume tokens, interrupted bookmark
construction, arbitrary token combinations or physical durability.

## Manager redacted resume

Manager send now accepts a resume token carrying `redact_snaps`. After caller
authorization and source snapshot GUID/offset validation, the adapter queries
real upstream bookmark properties. It requires the same source GUID, the same
redaction GUID set and a completed bookmark, then passes the resolved short
name to upstream `dmu_send`. Duplicate token GUIDs and wrong nvpair types are
rejected. No match returns ENOENT; a matching incomplete bookmark returns
EBUSY. Neither condition falls back to an unredacted stream. Explicit
`redact=` remains incompatible with `resume=` so callers cannot override the
token's redaction set. At this stage tokens carrying `book_redact_snaps`
(redacted incremental-base identity) remained unsupported; the following
incremental extension adds their identity validation and regression coverage.

Build `source-engine.P2gN7Bsv/manager-redacted-resume-service`
(`manager-redacted-resume-build-1.log`) passes. Run
`native-import-run.dxa7Redh` (`manager-redacted-resume-native-1.log`) passes
the full native regression with host exit 0 and unchanged backing hashes.
The real Manager API denies an unauthorized caller, refuses a missing
bookmark before creating output, and resumes successfully after recreating
the matching bookmark under another name. CLI receive preserves the redaction
GUID and clears its token. A completing incremental explicitly uses the
Manager-resumed destination as origin; DOS reads the exact retained 1 MiB
and EOF. The resumed stream excludes the secret canary.

The test uses the Manager API, not interactive mouse clicks. Its missing-bookmark
and authorization branches are exercised; malformed GUID-array and incomplete
bookmark branches are source-validated but not separately injected in this run.
This is not a physical durability or arbitrary redacted-incremental claim.

## Redacted incremental-base resume

The Manager now validates `book_redact_snaps` against the caller-selected
`from=bookmark`: both the base GUID and the exact redaction GUID set must
match. Like upstream libzfs's resume lookup, this requires a bookmark rather
than substituting an intact snapshot with the same GUID. The adapter holds
the upstream redaction list while checking its set and completion state;
missing redaction metadata or a different set returns ESTALE, and incomplete
state returns EBUSY. Duplicate token GUIDs and wrong field types are refused.
All identity validation occurs before opening output.

The fixture adds 1 MiB of new data to the safe clone, sends a real incremental
from the redaction bookmark, cuts it at 512 KiB and receives resumably from an
explicit redacted origin. It repeats with actual CLI `send -t` and Manager
send. Both receives must clear their tokens, retain the intended origin,
finish without redaction metadata, and return the exact new 1 MiB plus EOF
through DOS. Manager additionally refuses omitted base, the intact same-GUID
snapshot, and a same-GUID bookmark carrying a different redaction set, leaving
output absent before the valid retry.

Build `source-engine.P2gN7Bsv/redacted-incremental-resume-service`
(`redacted-incremental-resume-build-1.log`) passes. Native QEMU run
`native-import-run.P5HqQwpN` (`redacted-incremental-resume-native-1.log`) passes
the new cases, full selected mirror regression and unchanged backing hashes,
host exit 0. Updated Manager help builds separately in
`redacted-incremental-resume-ui-1.log`. This is API/CLI execution, not a GUI
mouse test. It does not qualify every redacted-to-redacted combination,
incomplete-bookmark injection, malformed token-array injection or physical
durability.
