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
