# Native whiteout rename

`3387a2c` enables the upstream native whiteout transaction and replay callback.
It replaces caller-input assertions with EINVAL checks for missing/extra
whiteout attributes, incorrect mode, nonzero device and incompatible flags.
The existing upstream ACL, quota and transaction paths remain in use.

Eight-CPU five-boot run `qemu-run.GHeP0YIN` passed. The writer creates a tagged
file, rejects malformed requests, renames it and leaves a character-device
marker with device zero, zero size, one link and the expected parent. The
destination retains the original object and bytes. Cold healthy and each
surviving-member imports recover this state and refuse read-only mutations.
Writable reopen and the remaining mirror regression also pass.

Independent original and reopened-pool `zdb -bcc` checks report no leaks;
all four extracted member hashes remain unchanged during inspection.
Logs and hashes are retained here; complete external artifacts are under
`/mnt/short-term/openzfs-qualification-20261024.aGFlVF7T`.

This qualifies internal ZPL whiteout metadata, not union/overlay filesystem
semantics, a new DOS packet, arbitrary special-device I/O, or physical media.
This run does not qualify pending whiteout log replay after abrupt loss.
The separate crash test added in `838981d` covers that boundary below.
Builds reuse upstream objects and selected patched AROS runtime prerequisites.

## Interrupted replay

Two-CPU `crash-run.91PtI73q` passed staging, partial recovery, restarted recovery
and final verification. Whiteout and exchange operations were committed to ZIL
after TXG persistence was frozen. The private VM was killed without export;
the next VM ran real replay callbacks before an injected SETATTR error. It too
was killed without export. A new recovery VM observed replay needed and
recovered the zero-length character marker and exact moved payload. The final
boot observed no replay needed and retained both. Independent zdb reported no
leaks; original crash, partial recovery and baseline hashes stayed unchanged.
The retained `crash/` evidence qualifies this controlled record sequence, not
arbitrary faults or physical-device durability.

## Existing destination and cross-parent replacement

`af1f213` adds a distinct retained fixture: a source file in one directory
replaces a target in another, while a separate hardlink preserves the old
target. NOREPLACE first returns EEXIST without changing either link count or
payload. Whiteout replacement then preserves the source's identity and bytes,
leaves the zero-device character marker at the old name, decrements the old
target to one link and keeps its original data accessible through the alias.
Parent identities, directory sizes and link counts are checked explicitly.

Eight-CPU run `qemu-run.7nMBMXt3` passes writer, cold healthy, both single-member
read-only imports and writable reopen. The replacement fixture is checked in
the writer and three read-only imports. Independent original/reopened zdb
checks report No leaks; hashes of all four inspected partition images remain
unchanged. Evidence is retained in `replacement/`.

This extends clean-export/cold-import qualification, not abrupt-crash replay
of destination replacement. The earlier interrupted-replay test covers a
non-replacing whiteout. Production code was unchanged for this added test.

## Interrupted replacement replay

`2e18dca` adds an explicit internal replacing-whiteout entry point, keeping the
existing whiteout entry point's NOREPLACE behavior. No DOS packet or management
operation was added. The native crash fixture creates distinct source/target
payloads and a retained hardlink to the target before the cross-parent whiteout.

Two-CPU `crash-run.py50jPRT` passes staging, interrupted replay, resumed recovery
and final verification. The private VM is killed after committing ZIL with TXG
persistence frozen, then killed again after an injected SETATTR replay failure
and partial-replay persistence. Resume observes replay needed and recovers the
character marker, moved source bytes and distinct original-target bytes through
the surviving alias. Final boot needs no replay and verifies those again.
Independent zdb reports No leaks; original crash, partial-replay and baseline
disk hashes remain unchanged. Logs and hashes are in `replacement-crash/`.

This qualifies the controlled replacing-whiteout record sequence, not every
possible interruption point or physical-device durability.

The non-diagnostic shared engine was rebuilt as `shared-engine.tbM9IzcN` from
`sibling-production.IXtA7XZ4` (incremental upstream objects, not a fresh SDK).
Eight-CPU `public-service-run.RzqI8zfH` passes the service lifecycle, mirror DOS
round trip and native cold exact-read/read-only regression. See
`replacement-production/`. This production regression is separate from the
diagnostic crash fixture above.
