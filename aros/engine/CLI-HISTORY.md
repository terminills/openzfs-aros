# Native upstream CLI pool history

`zpool history` uses `ZFS_IOC_POOL_GET_HISTORY` with a raw byte buffer, not
an output nvlist. The native client now marshals this request into the
existing bounded output region. It clears the caller pointer before sending;
the engine registers only its own output allocation for `ddi_copyout` and
clears internal pointers before replying. Returned bytes are copied only on
engine success and only if the returned length fits the original capacity.
The logical history offset and actual byte count are preserved for upstream
decoding and continuation. Export/destroy input-history strings are unchanged.

The server rejects mixed input/config/history-string payloads and unrelated
inline fields. Upstream configuration privilege and suspended-pool checks
remain authoritative. A native precheck rejects offsets beyond logical EOF,
matching the Manager's existing bound before upstream ring-buffer reading.
The request remains capped by the existing aggregate ioctl allocation limit.

## Evidence, 2026-10-03

Unchanged package `native-bundle.mbGPjkip` failed the new history command in
`native-import-run.OEPxe8X3`. The corrected package
`native-bundle.gzSx0YhT` contains `shared-engine.GRDgA2Dp`, freshly relinked
commands `cli-link.JlUKPCri`, service/Manager from
`cli-history-release-20261003`, and the existing handler/ZVOL inputs.

Run `edmJcxAj` returned an internal dataset record and read history on a
readonly pool. Unauthorized access was refused with `permission denied` and
status 1; the test had incorrectly expected -1. That failed run is retained.
The subsequent replay changes only that test expectation, not the package.

Replay `5vZ4Qzqr` exited 0: actual internal dataset history, readonly history
access and nonroot refusal all passed, along with the full selected native
CLI/handler regression. Both original backing-image SHA256 checks passed.

External logs use the `cli-history-` prefix, dated `20261003`, including
before/build/link/library/library-inputs/stage/after/after-2. This initial
coverage does not qualify large multi-page histories, malformed wire buffers,
every offset boundary, concurrent export/recreation, or physical durability.

## Raw wire boundary regression

`native-import-run.oMDn0KWx` uses the same immutable package with a separately
compiled native client helper, not a linked engine implementation. It read
15160 bytes through large buffers, then compared the identical bytes over
237 small pages (at most 64 bytes each), including non-record-aligned splits.
Returned offsets/counts and cleared pointers were checked, as were 16-byte
canaries beyond each requested output allocation.

Six malformed cases were rejected: a nonzero wire history pointer, length
greater than capacity, zero history length, an unrelated cookie, an invalid
history-string envelope, and UINT64_MAX offset. A subsequent valid EOF read
returned zero bytes at the expected offset. Session/descriptor cleanup and
the full selected CLI/handler regression passed with host exit 0 and unchanged
original backing-image hashes. Log: external `history-wire-20261003.log`;
helper source/object hashes and compilation output are in the run directory.
The runner requires the new result marker, preventing a skipped helper from
passing this configuration.

Source audit: history is not on the asynchronous query whitelist in
`admission.c`; synchronous dispatch uses the owner task, and
`oz_cli_pool_export_check` rejects non-owner execution. This is source evidence
for serialization, not a new simultaneous-export stress result. The small-page
test does not establish histories larger than the normal CLI read buffer,
ring overwrite under concurrent writers, or every malformed field combination.

## History larger than the CLI read buffer

`AROS_TEST_LARGE_HISTORY=1` requires the pool-create fixture and at least
4096 MiB guest memory. Only its dedicated growth-pool member starts at
512 MiB, increasing the upstream history ring beyond the small fixture's
128 KiB minimum. The overwrite test writes 180 uniquely marked 6000-byte
user-property values through the real CLI and requires more than 384 KiB of
retained raw history. The oldest marker must be absent and the newest 20 must
remain. Raw reads compare 4093-byte chunks against the larger-buffer reference;
the normal small fixture retains its 64-byte boundary test.

Upstream resets a stale logical cursor forward to the live ring BOF. The wire
verifier records that discontinuity as one overwrite jump, compares the
retained bytes across both the large reference reads and small replay chunks,
then uses the returned logical EOF for the empty-page check. It never treats
the retained byte count as an absolute cursor.

Run `native-import-run.QloiNJ5x` passed with 170040 raw bytes across 42 pages,
all six malformed-request checks, and all 160 marked records found in output
from the real `zpool history -i` command. Output is captured to a guest file
instead of relying on the ordinary command harness's 16 KiB text buffer.
The unchanged `native-bundle.gzSx0YhT` package completed the full selected
regression, host exit 0, and both original backing-image hash checks passed.
External log: `history-large-20261003.log`.

That historical run exercises upstream's 128 KiB read/leftover-record
continuation without overwrite. The stronger current fixture adds serial
circular-ring overwrite and logical-BOF-jump proof. Concurrent overwrite is
qualified separately below, and the approximately 1 MiB ioctl-output logging
boundary is qualified in the megarecord section. No production service
behavior is weakened to accommodate stale cursors.

## Separate gap: user command logging

The read tests above verify internal records, not successful
`ZFS_IOC_LOG_HISTORY` writes. The current transport does not admit that ioctl.
Both upstream CLI main functions intentionally ignore `zpool_log_history()`'s
return value, so a successful mutation does not prove its command was logged.
`AROS_TEST_COMMAND_HISTORY=1` adds a distinct opt-in regression: set a unique
user property and require its marker in plain `zpool history`, without `-i`.
The runner requires a dedicated result marker; ordinary history-read tests
cannot satisfy this gate.

Source audit before implementation:

- `module/zfs/zfs_ioctl.c` sets `zfs_allow_log_key` after successful
  logging-eligible ioctls. `zfs_ioc_log_history` consumes that pool name once;
  its security policy rejects even root without that context.
- `aros/engine/ioctl-request.c` dispatches on service threads, not the original
  client thread. It currently does not save/restore that history context.
- `aros/cli/control-client.c` shares one authenticated session across a
  process's threads and multiple control descriptors. A session-global pool
  string alone would not preserve upstream's per-thread semantics.
- Manager operations also call the upstream dispatcher directly. Their TLS
  context must not become a CLI client's logging authorization.

Therefore simply adding LOG_HISTORY to the whitelist is unsafe. The adapter
needs authenticated caller-scoped, single-use context, correct lifecycle
cleanup and isolation from Manager calls. Verification must include two pools,
interleaved clients and threads, no-prior-operation rejection, single-use
rejection, session cleanup, and plain CLI output. These are pending requirements,
not implemented or verified behavior. No file-backed mmap is needed.

Negative control `native-import-run.Isx2ohPo` ran the opt-in command-history
test against unchanged `native-bundle.gzSx0YhT`. The marked `zfs set` and
following `zpool sync` both returned status 0. Plain history returned status 0
and 27 bytes containing only the pool heading; the marker assertion failed at
service-command-test.c line 3031. The host runner exited 1 with its explicit
guest-failure diagnostic. This is retained expected-negative evidence, not a
passing suite or a newly introduced production regression. External log:
`command-history-before-20261003.log`; complete output is in the run's
`serial.log`. No production binary was changed.

## Command logging implementation and qualification

The version-3 ioctl envelope adds a session-local history control descriptor.
The native client owns one such descriptor per pthread, lazily opened under
the existing connection lock. Thread exit closes it; closing the process
session discards all remaining slots. A monotonically increasing connection
generation invalidates a main thread's old descriptor after reconnect.
These slots share the existing bounded 64-entry native descriptor table.

The engine transfers the slot's pool string into upstream `zfs_allow_log_key`
only around the ioctl and restores the service thread's previous TLS afterward.
Upstream still decides which successful operations enable logging, validates
the message nvlist and consumes the context once. Zero history descriptor
means isolated, nonpersistent context, never inherited Manager authorization.
Concurrent reuse of one slot returns EBUSY; close refuses an in-flight slot;
session shutdown discards any result returned by a retained operation.
LOG_HISTORY accepts no client pointers, pool-name override, output buffer or
unrelated inline fields. Its packed nvlist uses the aggregate ioctl bound,
not the separate 4096-byte legacy export-history-string limit.

Initial package `native-bundle.WC5L4qCa` / `shared-engine.7gA8ekxa` /
`cli-link.CNOiHfg8` passed plain command-history detection and the selected
regression in `native-import-run.AsVZajmQ`, host exit 0. Both original backing
hashes passed. This first candidate still imposed the extra packed-input cap;
the final candidate removes it and must be replayed independently.

`AROS_CLI_HISTORY_TEST=1` links a standalone real-libzfs client test. The first
two link attempts lacked test-harness symbols (`check_file_generic`, `g_zfs`);
their logs remain in `cli-link.8tT5HXS8` and `cli-link.j8cMrD1t`. Corrected test
`cli-link.6ug9UvEI/OpenZFSCommandHistoryTest` is optionally mapped by
`AROS_TEST_COMMAND_HISTORY_BINARY`, not shipped as a production command.

Run `native-import-run.94scgRGG` used the initial production package plus that
test. Two threads each modify a different pool before either logs. Both log
once, a second log is refused, and fresh threads cannot inherit another
thread's authority. Plain histories contain the correct distinct marker and
not the other pool's marker or the forbidden marker. Seventy-two sequential
thread lifetimes exceed the descriptor-table capacity without exhausting it,
and reconnect rejects unearned logging. The complete selected regression and
original backing hashes passed, host exit 0.

These tests do not yet prove malicious descriptor races, every malformed
history nvlist, long record boundaries, pool export/recreation between mutation
and logging, or physical durability. The source-level Manager TLS isolation
also still needs a dedicated adversarial runtime test.

Final candidate `native-bundle.Pzr88Y99` contains `shared-engine.hCrRXyxs`,
commands from `cli-link.6ug9UvEI`, service/Manager from
`cli-command-history-full-release-20261003`, and the unchanged handler/device
inputs. Its independent run `native-import-run.2ZnJdQFG` passed the command
history and isolation assertions, full selected native CLI/handler regression,
and both original backing hashes; host exit 0. Logs use the external
`command-history-full-` prefix dated `20261003`. The removed packed-input cap
is source-verified here; a dedicated long-message boundary test remains open.

## Long records and malformed-request recovery

Test commit `d31e581` extends the standalone real-client executable. Seven
malformed requests cover pool-name override, unrelated cookie, nonzero wire
pointer, unknown descriptor, missing message, wrong message type and unknown
nvlist key. A subsequent valid log succeeds and a duplicate is refused.
An empty second session and zero-context request cannot consume the first
session's pending authority. Closing and reopening a descriptor does not
revive its stale handle. Plain output contains the valid marker and not the
forbidden schema marker.

The client also submits a 6000-byte command record and requires exactly one
byte-for-byte identical decoded `ZPOOL_HIST_CMD` from `zpool_get_history`.
This is a bounded long-record test, not maximum-size record qualification.

The initial helper compile `cli-link.WlyucjvB` caught a const-qualified nvpair
API mismatch; corrected helper `cli-link.yOIxKN1y` linked successfully. The
negative run `native-import-run.GK183W3g` against capped package WC5L4qCa
passed wire refusals but failed the long record, host exit 1. Both original
backing hashes were checked independently afterward and were unchanged.
The same helper against Pzr88Y99 passed in `native-import-run.dzPZ7rXs`:
wire boundaries/recovery, long exact record, previous thread/pool isolation,
full selected regression and original backing hashes, host exit 0. No
production binary changed during this test extension. External logs have the
`command-history-boundary-` prefix dated `20261003`.

## Records larger than the summary bound

`AROS_TEST_HISTORY_MEGARECORD=1` is a separate opt-in qualification. It
requires the pool-create and command-history fixtures, the standalone real
libzfs helper, 8192 MiB guest memory, and cannot be combined with the serial
overwrite fixture. Only this test grows the first history-pool member to
1536 MiB. The normal package and ordinary regressions keep their previous
memory and storage requirements.

After a successful property mutation arms upstream's single-use context, the
helper submits one deterministic 1,052,672-byte command through the real
`zpool_log_history` path. It then uses `zpool_get_history` and requires exactly
one byte-for-byte match. Upstream starts that reader with a 128 KiB buffer, so
recovering this record exercises its no-progress doubling path; the complete
logical read required two libzfs batches in the qualified runs.

The service verifier uses the same authenticated admission submit/query/release
route as the Manager. Summary operation 33 must return EOVERFLOW with errno
detail and no partial rows at the large-record boundary. It then restarts raw
operation 33 at that boundary, checks every absolute offset and hex row, parses
the original record framing and requires exactly one matching packed command.
The separate ioctl-wire verifier raises only its private capture allocation to
the existing 64-page maximum for this opt-in mode; production request bounds
are unchanged.

The retained failure sequence distinguishes storage behavior from verifier
defects. `native-import-run.cnXJLGFg` never booted because its DOS overlay
argument named an archive instead of its containing directory. Run `YWoKUsRd`
proved exact libzfs retrieval but incorrectly exercised the public client for
the Manager check. Run `jFzdsF6l` switched to admission but omitted the message
envelope. Run `9SALU50e` passed both libzfs and Manager proofs, then exposed an
undersized private wire baseline. Run `4dEihSqC` showed that the separately
compiled wire object had not received the opt-in define. None of those partial
runs is a release qualification.

Dirty candidate `native-import-run.WMpNqnXq` first passed the complete suite.
Commit `527c3394` was then relinked as helper `cli-link.uo3yeOjo` and staged in
clean package `native-bundle.oTAr5WRm` (archive SHA256
`f0ccd7502ac6f0d74be79a308a6997b861e3efdeb3bf8bcc89968fdb04a5b789`,
13,179,848 bytes). Exact-package run `native-import-run.WUElB0Al` recovered the
1,052,672-byte command in two libzfs batches; summary mode returned errno 84 at
cursor 13600 with zero rows; raw mode reconstructed it across 462 pages. The
wire verifier captured 1,077,408 bytes and replayed them across 264 pages with
all malformed, guard, EOF and recovery checks passing. The full native mirror
regression completed `PASS line=0`, and both original backing hashes were
unchanged. This proves a stable retained ring, not overwrite concurrent with a
reader; the following independent fixture qualifies that race.

## Concurrent circular overwrite

`AROS_TEST_HISTORY_RACE=1` requires the pool-create and command-history
fixtures, the ordinary standalone command-history helper, at least 4096 MiB
guest memory, and cannot be combined with the serial-overwrite or megarecord
fixtures. A pthread writer issues 240 real `zfs set` commands with deterministic
6000-byte values. After four writes, an authenticated reader advances in
1024-byte pages and deliberately delays between requests so the circular ring
overwrites unread data while both tasks remain active.

Every response checks cleared private pointers, returned capacity and 16-byte
canaries. The reader accepts a discontinuity only when upstream explicitly
returns a start cursor greater than the requested cursor. Only that event may
discard an incomplete record header or payload; otherwise the reader preserves
eight-byte record framing across pages and requires lengths from one byte
through 1 MiB. Success requires more than 384 KiB read, multiple pages, a
cursor jump while the writer is still active, at least one complete record and
a coherent final EOF after the writer finishes. A stable post-race replay then
re-runs the normal malformed-request, guard, EOF and recovery suite, and real
`zpool history -i` output must omit marker 000 while retaining markers 220
through 239.

The first attempt, `native-import-run.TnFK1yzK`, accidentally used the
megarecord-enabled helper with the smaller race pool. Its helper failed before
the race started, so that run neither qualifies nor refutes concurrency.
Dirty candidate `native-import-run.qldFtJge` then passed with 240 writes,
965,800 bytes across 944 pages, 172 overlap-time jumps, 199 complete records
and coherent EOF. That candidate is retained as development evidence, not the
release qualification.

Commit `f09ac659` was relinked as ordinary helper `cli-link.3Kyc1hVb` and
staged with a zero-byte packaging patch in clean package
`native-bundle.i1hURMsL` (archive SHA256
`2cba18d4b7a0d61597aa6557ebfd0f85bcb6c6e588f0d358a27f624927210c2f`,
13,181,300 bytes). Exact-package run `native-import-run.WCogqLXM` completed all
240 writes. Its concurrent reader consumed 966,824 bytes across 945 pages,
observed 172 cursor jumps while the writer remained active, discarded 172
partial records only at those jumps, parsed 200 complete records and reached
coherent EOF. Stable replay retained 499,880 bytes across 123 pages with one
logical BOF jump; all malformed, guard, EOF and recovery checks passed. The
newest 20 CLI markers remained and the oldest was absent. The full native
mirror regression completed `PASS line=0`, both original backing hashes were
unchanged, and the runner exited 0.

Exact-package run `native-import-run.b0mEwuBG` changes only the scheduler
topology to eight virtual CPUs. The concurrent reader consumed 973,992 bytes
across 952 pages, observed 172 jumps while the writer remained active,
discarded 172 partials only at those jumps, parsed 204 complete records and
reached coherent EOF. Stable replay again retained 499,880 bytes across 123
pages with one logical BOF jump; the retained suffix, full native mirror
regression, completion marker and both original backing hashes passed. The
qualified IDE set therefore covers isolated 4 GiB guests at two and eight
virtual CPUs.

### AHCI topology qualification

Commit `bc0e4d47` adds a strictly gated `AROS_TEST_NATIVE_BUS=ahci` race
configuration. It attaches both disposable mirror members to one QEMU
`ich9-ahci` controller, selects `ahci.device` in the native importer and CLI
path checks, and refuses unrelated AHCI layouts. The production client timeout
remains three seconds. Only the debug-heavy AHCI verifier executables are
compiled with a ten-second timeout; this is required because the instrumented
driver creates and reports an AHCI callout task for every request.

The retained failure chain separates storage, harness and client behavior:

- The first build attempt stopped at the storage reserve guard with
  104,239,620 KiB free versus 104,857,600 KiB required and created no test
  artifact. Inactive evidence was moved intact to the external archive.
- Dirty run `native-import-run.3UGuVg3O` passed AHCI import and the concurrent
  history proof (973,992 bytes, 952 pages and 172 writer-overlap jumps), then
  failed CLI discovery because the test still named `ata.device` explicitly.
- Dirty run `native-import-run.jbCwuHWX` passed the corrected AHCI history
  path (967,848 bytes, 946 pages and 172 overlap jumps), then timed out in a
  read-only recursive property page at the production three-second client
  bound amid the driver callout diagnostics.
- Dirty run `native-import-run.JHGu5G3A` used the test-only ten-second bound
  and passed the full suite: 956,584 bytes across 935 pages, 172 overlap-time
  jumps, 200 complete records, stable replay, property catalogues, AHCI CLI
  discovery/import, service CLI, two final import cycles, completion line zero
  and both pristine backing hashes.
- Clean run `native-import-run.f85GJgm1` stopped before boot because the DOS
  overlay argument named the archive rather than its containing directory.
  Clean run `native-import-run.c3icfYih` reproduced every AHCI-specific gate,
  CLI import and service CLI but was deliberately classified INCOMPLETE when
  the host runner reached its 600-second default deadline before guest
  completion. Neither run is a release qualification.

The final committed helper `cli-link.WMI6GYez` records source `bc0e4d47` and
ordinary non-megarecord options; its `OpenZFSCommandHistoryTest` SHA256 is
`1e4fb6ce8dd3ff5319c42ef53bb5eb779981132c61b8441986bc3ec6d7d07940`.
It is staged in patch-free package `native-bundle.58W5Goph` (archive SHA256
`d07d113df8f98bcadadbb47586c93e896c545ad418caa644d212401fb3af8fe0`,
13,176,124 bytes). Final clean run `native-import-run.89xxTVT9` used an
explicit 1800-second host deadline and passed with 240 writes, 969,896 bytes
across 948 pages, 172 jumps all while the writer remained active, 172 partials
discarded only at those jumps, 201 complete records and coherent EOF. Stable
replay retained 499,880 bytes across 123 pages with one logical BOF jump. The
newest-marker suffix, full native regression, two final import cycles,
out-of-band completion line zero and both pristine backing hashes passed.

Exact-package run `native-import-run.n1ru63XV` changes only the AHCI guest to
eight virtual CPUs. Its concurrent reader consumed 975,016 bytes across 953
pages, observed 172 jumps while the writer remained active, discarded 172
partials only at those jumps, parsed 204 complete records and reached coherent
EOF. Stable replay again retained 499,880 bytes across 123 pages with one
logical BOF jump. The newest-marker suffix, full native regression, two final
import cycles, completion line zero and both pristine backing hashes passed.

### USB topology qualification

Commit `54ad893e` adds a strictly gated writable USB race configuration and
selects `usbscsi.device` for the native CLI member paths. It requires
`AROS_TEST_USB_CONTROLLER=qemu-xhci`, both mirror members, no GPT or GUI, and
refuses removal, reinsertion, total-loss, swap and created-member combinations.
Commit `b21d6055` adds a separate strict cold-read gate: it accepts only a
completed qemu-xhci writer that retained the CLI dataset, excludes all USB
recovery modes, and requires the fresh guest to verify the persisted per-pool
command marker as well as the dataset. Existing readonly USB recovery fixtures
keep their previous gate. The immutable production package and ordinary
command-history helper remain `native-bundle.58W5Goph` and
`cli-link.WMI6GYez` from `bc0e4d47`.

The retained `pci-ohci` attempt `native-import-run.QnRZa3xR` reached neither
the writable import result nor the history race and timed out at its explicit
1800-second deadline. Live monitor evidence shows CPU0 recursively acquiring
`MemListSpinLock` in `nommu_FreeMem`: the lock owner and interrupted current
task are identical. The stack passes through OHCI completion SoftInt,
`ohciFreeEDContext`, `usbReleaseBuffer` and `FreeVec`; CPU1 is idle and both
USB members' QMP I/O counters remain unchanged. This is preserved under linked
the historical test archive the development test archive as an AROS OHCI/Exec defect, not counted as an OpenZFS history
failure and not hidden by a readonly warm-up.

Development xHCI run `native-import-run.4wG5G54a` first passed the full suite.
The independent committed-source replay `native-import-run.aD5cw6Hg` then
completed all 240 writes. Its concurrent reader consumed 971,944 bytes across
950 pages, observed 172 jumps while the writer remained active, discarded 172
partials only at those jumps, parsed 204 complete records and reached coherent
EOF. Stable replay retained 499,880 bytes across 123 pages with one logical
BOF jump. The newest 20 command markers remained and the oldest was absent.
USB-path CLI discovery/import, service CLI, two final import cycles,
completion line zero and both pristine backing hashes passed; the host exited
zero.

Exact-source run `native-import-run.klSKOeOC` changes only the xHCI guest to
eight virtual CPUs. Its concurrent reader consumed 1,013,928 bytes across 991
pages, observed 170 jumps while the writer remained active, discarded 170
partials only at those jumps, parsed 237 complete records and reached coherent
EOF. Stable replay again retained 499,880 bytes across 123 pages with one
logical BOF jump. The newest-marker suffix, full native regression, service
CLI, two final import cycles, completion line zero and both pristine backing
hashes passed.

Exact-source SMP2 writer `native-import-run.7XP6K60F` at `b21d6055` has a
zero-byte source patch and deliberately retains `qemu_mirror/cli-import` for a
second boot. Its concurrent reader consumed 972,968 bytes across 951 pages,
observed 172 overlap-time jumps, discarded 172 partials only at those jumps,
parsed 202 complete records and reached coherent EOF. Stable replay, the
newest-marker suffix, service CLI, two final import cycles, completion line
zero and both pristine backing hashes passed.

Fresh-guest run `native-import-run.VfARC5Cq` uses new child qcow2 overlays over
that completed writer and the same zero-patch source commit. Its readonly
import found `history-thread-beta` in `qemu_mirror` while excluding the
`history-thread-alpha` cross-pool marker and forbidden marker. It then reread
the retained dataset, passed the independent native cold-write verification,
completed two final import cycles and exited at completion line zero. The
complete backing chain and cold overlays passed their hash checks. This proves
persistence across an independent virtual boot; it is not physical-media
durability evidence.

The qualified set now covers isolated 4 GiB IDE and AHCI guests at two and
eight virtual CPUs plus isolated 4 GiB qemu-xhci guests at two and eight
virtual CPUs, including an xHCI SMP2 cold writer/reader chain. USB
hotplug/removal, pci-ohci pending the development test archive and physical hardware remain outside
this evidence.
