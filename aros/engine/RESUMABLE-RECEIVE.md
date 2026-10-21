# Native resumable receive evidence

Current raw-token support and full/incremental runtime evidence are recorded
in REPLICATION-OPTIONS.md. The raw-token exclusions in the earlier milestones
below are historical; redacted tokens remain unsupported.

Operation 61 accepts only `resumable=on` or `resumable=off` (default off).
The captured caller, delegation, native input authorization, writable-pool
checks and mounted-target exclusion are unchanged. Upstream dmu_recv owns
the saved receive state; no AROS-specific resume metadata is invented.

## Split source-block resumed send

The initial `admission-service-run.OCyWJGFl` interrupted an ordinary stream
after 524288 input bytes. Upstream saved object 2, offset 262144, 396712
committed stream bytes, and a 271-character version-1 token. Sending a
continuation aborted at dmu_send.c's DATA range assertion (original line 928).
The source file uses a 1 MiB block but the ordinary stream splits it into
128 KiB writes. Traversal necessarily returns the containing source block,
whose start precedes the resume offset. The assertion and emission loop did
not account for that case.

The correction permits a resume position inside such a split block only at
a 128 KiB wire-record boundary, rejects misalignment, and advances past prior
pieces without emitting them. Non-split records retain their existing checks.
The native link script recompiles dmu_send.c rather than silently linking a
stale engine object.

`admission-service-run.F83iHFVz` passed WRITE, cold read-only VERIFY and the
existing offline suite after this correction. The continuation receives to
completion and its 1 MiB patterned file, 4 KiB repeated-byte file, and changed
hello file match exactly. Completed receive state is gone. A second interrupted
receive retains its upstream position and token across the cold boot.
Logs: `resumable-engine-build-3.log`, `resumable-run-2.log`.

The original fixture generated the continuation directly through upstream
dmu_send using saved object/offset. That result alone did not prove public
token-based send or completion after restart. The later token path below adds
validation rather than exposing arbitrary user-provided offsets.

## Complete token response

The observed ordinary resume token is 271 characters; a property row's value
field holds only 255 plus its terminator. Exact-property queries now return
`kind=property-long` for values too large for that field. The full, unprefixed
value is `message.detail`, with byte length in `row.total`; `row.value` is only
display guidance. Values exceeding the 4096-byte detail buffer fail explicitly.
Short property values keep their original representation. The Manager displays
and saves the full response detail and does not render storage counters for
this metadata row. This extends row semantics without changing wire layout.

The native test requests `receive_resume_token` on the incomplete dataset and
checks the complete long-property result in both WRITE and cold VERIFY. The
later token-send path consumes this complete value. Raw/redacted tokens and
tokens exceeding the option-field capacity remain outside this workflow.

`admission-service-run.RQTRaK4Y` passed the full WRITE/cold/offline suite with
the long-property API checks. The host upstream parser independently validates
the exact retained continuation's checksums/GUID and confirms object 2 begins
at offset 262144 with 131072-byte writes, never offsets 0 or 131072. Logs:
`resumable-engine-build-4.log`, `resumable-client-build-2.log`, and
`resumable-run-3.log`. Manager compilation passed in
`resumable-manager-build-1.log`; no new GUI screenshot/run is claimed.

## Validated token send

Operation 60 accepts `resume=TOKEN`, optionally with `from=SNAPSHOT_OR_BOOKMARK`
when the token specifies an incremental base. Representation switches cannot
be combined with a token: compressed/embedded/large-block flags come from it.
The source snapshot is explicit in Target; its GUID must match the token even
if its name has changed. The supplied incremental base must have the matching
GUID and be an ancestor. Automatic GUID-to-name discovery is not implemented.

Validation occurs before output acquisition and again before send. It checks
version, strict bounded hex, Fletcher4 checksum, decompression length, packed
nvlist fields, source/base identities, object existence and valid record offset.
Unknown token extensions including raw/redaction metadata return ENOTSUP.
The same captured caller must still have send delegation and native output
authority. Tokens authorize no access themselves.

`admission-service-run.jzGPRnk1` passed WRITE/cold/offline tests with API token
send and native continuation receive. Wrong snapshot returns ESTALE, damaged
checksum/malformed tokens return errors, and rejected requests publish no
output. The API continuation stream is retained and independently parsed by
upstream zstream for checksums and matching destination GUID. Its completed
dataset's exact content and cleared receive state survive cold verification.

The subsequent test runner adds a third writable fresh boot, after proving
the second boot left both disk hashes unchanged. That boot obtains the pending
dataset's persisted upstream token, validates it through the production token
adapter, sends/receives the continuation, checks exact file bytes and cleared
state, then exports the pool and shuts the engine down. It does not rerun
fixture creation. The exact cold continuation is retained for host verification.

`admission-service-run.6iiNj8RU` completed all three native phases, then the
runner failed an obsolete final read-only hash comparison after the intentional
writable third phase. The pre-third-boot read-only hash check had passed. The
runner now treats admission like initialization: check those hashes before the
third boot, not after its writes. Independent post-exit stream verification of
6iiNj8RU passed (`token-send-cold-offline-2.log`). The failed overall runner
status is preserved rather than relabeled as a full-suite pass.

`admission-service-run.wHM1NE1x` passed the corrected complete three-boot suite
and all offline checks. The third phase consumed a token generated from the
persisted receive state, completed the target, verified all fixture bytes and
absence of receive state, and exported successfully. Both API and cold-boot
continuation streams were independently checksum/GUID verified after QEMU exit.
Logs: `token-send-engine-build-3.log`, `token-send-client-build-1.log`,
`token-send-run-3.log`. Manager rebuilt in `token-send-manager-build-1.log`;
visual interaction with the new token workflow remains unverified.

## Incremental token continuation

`admission-service-run.3TC6MGwh` passed the full three-boot admission suite,
including incremental receives interrupted after 300000 input bytes. Tokens
are retrieved through the parent dataset while upstream stores interrupted
state in its `%recv` child. Missing bases fail EINVAL and wrong base GUIDs fail
ESTALE. One continuation uses a snapshot; the other uses its bookmark after
the source base snapshot is deleted. Both preserve the destination base's
524288 bytes of A and restore the current dataset's 524288 bytes of B.
Cold read-only verification confirms those bytes and removal of `%recv`.

The original full and incremental streams plus both continuation streams are
retained in `stream-artifacts`. After QEMU exits, upstream host zstream checks
their checksums and matching base/destination GUID chains. Logs are
`inc-resume-engine-build-1.log` and `inc-resume-run-1.log`. This extends engine
coverage; it is not evidence of visual interaction with token controls.

## Native Manager token workflow

Manager's `Use token` button accepts only a successful exact
`receive_resume_token` response that fits the send-options field. It copies
the full value to `resume=`, selects Replication/send, clears target/output
and incompatible flags, and submits nothing. The user supplies the actual
source snapshot, output file and any incremental `from=` base. A new response
invalidates the saved token. Backend checksum/GUID/permission checks remain
authoritative. Oversized tokens are not silently truncated.

`native-gui-run.Poc5WHzD` passed the real graphical workflow: get token,
Use token, confirm send into mounted `OZGUIOUT:gui.zfs`, then confirm resumable
receive into `qemu_mirror/resume-pending@second`. The native fixture read and
checked all 1048576 large-file bytes, 4096 embedded-file bytes and 70 hello
bytes through DOS, verified absence of the receive token, unmounted and
exported successfully (`RESULT authenticated native GUI PASS`). After guest
exit, upstream host zstream parsed the generated continuation without checksum
errors. The token confirmation now names source/output and dry-run state in
a bounded dialog; the complete options remain in the Response pane.

Retained unsuccessful runs:
- `Cll8KnWx`: token display-only exploration; deliberately closed before the
  old own-send fixture's required send/receive, so its completion gate failed.
- `WonBosd2`: graphical send/receive both returned success, but the fixture
  incorrectly expected a literal `-` computed token property after completion.
  Upstream omits the property and dsl_prop_get returns ENOENT. The corrected
  fixture checks that exact error only after verifying the dataset and files.
  This run also exposed an oversized token confirmation dialog.
- `wrocfDJ5`: ISO packing failed before boot because /tmp was full. The passing
  rerun used its own build-directory ISO scratch, without deleting shared files.

Build logs: `resume-ui-manager-build-3.log`, `resume-ui-launcher-build-2.log`.
Run log: `resume-ui-run-3.log`; screenshots, serial, UI actions and exact input
hashes are retained in the passing private run directory.

## Interrupted receive abort adapter

`oz_receive_abort` follows upstream `zfs receive -A`: prefer the hidden `%recv`
child; otherwise accept only an inconsistent head with saved receive state.
It checks pool writability/suspension and the caller's destroy/mount permissions,
then invokes upstream destruction under the captured credential. Its caller
must serialize lifecycle and reject mounted targets, just as for receive.

`admission-service-run.S6RpGy0g` passed the complete three-boot suite and offline
checks. The fixture interrupted both full and incremental streams, aborted
them, rejected missing credentials and healthy targets, checked repeat-abort
errors, and verified retained head/base bytes plus absent abandoned receive
state on cold boot. This evidence covers the internal engine adapter; public
authenticated API/UI wiring is a separate next step.

Operation 78, `replication.abort-receive`, now binds that adapter through
captured MuFS credentials. It requires confirmation, no arguments/options or
extra flags, and an unmounted target. Manager sends it through the authenticated
worker and explains the new-filesystem versus incremental consequences.

`admission-service-run.MuQYsomF` passed the complete three-boot API suite and
offline verification: UID77 without grants is denied EPERM, mounted targets
are denied EBUSY, unmounted readonly targets EROFS, healthy targets EINVAL,
and both interrupted receive forms abort successfully. Cold verification
checks retained head/base bytes and absence of the discarded state.
The earlier `lQvr3yEp` passed WRITE and cold engine data verification but failed
the client's wrong expectation of EROFS for a mounted readonly target. The
adapter intentionally checks mount use first. Only that test expectation was
corrected; both rejection cases remain independently tested.
Logs: `receive-abort-api-engine-build-1.log`,
`receive-abort-api-client-build-2.log`, `receive-abort-api-run-2.log`.
The newly added graphical abort fixture is not covered by this API pass.

The separate `native-gui-run.jsCjddIQ` subsequently passed both real Manager
abort confirmations and the complete graphical fixture. It read 524288 bytes
of A from both the retained incremental head and its base snapshot through
native DOS mounts, checked absence of the new incomplete dataset and `%recv`,
checked cleared receive-token state, then exported successfully. Final marker:
`RESULT authenticated native GUI PASS`. The two requests were initiated only
through the Manager, not by the fixture. Confirmation/result screenshots,
saved activity/response output, serial logs and UI actions are retained.
Logs: `abort-ui-launcher-build-1.log`, `receive-abort-api-manager-build-2.log`,
`abort-ui-run-1.log`. Guest exited cleanly after the final PASS was collected.
